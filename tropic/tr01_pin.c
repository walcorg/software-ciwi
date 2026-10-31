/**
 * @file    tr01_pin.c
 * @brief   PIN verification with TROPIC01 MAC-and-Destroy. See tr01_pin.h.
 *
 * Notation (application note ODN_TR01_app_002):
 *   s      32-byte secret, random, never stored
 *   t      tag   = KDF(s, 0x00)            stored, lets us recognise s
 *   u      arm   = KDF(s, 0x01)            M&D input that (re)initialises a slot
 *   v      = KDF(0, PIN || bind)           M&D input for a PIN attempt
 *   w_i    = M&D(slot i, v)                only reproducible if slot i is armed and the PIN is right
 *   k_i    = KDF(w_i, PIN || bind)
 *   c_i    = s XOR k_i                     stored, one per slot
 *   pin_key = KDF(s, "2")                  released to the wallet
 * KDF = HMAC-SHA256(key, data).
 */
#include "tr01_pin.h"

#include <string.h>

#include "ciwi_log.h"
#include "hmac.h"
#include "memzero.h"
#include "tr01_platform.h"

#define PIN_STATE_VERSION 1

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t left;                                   /* attempts left */
    uint8_t c[CIWI_PIN_ATTEMPTS][TR01_MAC_AND_DESTROY_DATA_SIZE];
    uint8_t t[32];
} pin_state_t;

_Static_assert(sizeof(pin_state_t) <= 444, "PIN state does not fit in one R-memory slot");
_Static_assert(CIWI_PIN_ATTEMPTS <= 12, "too many PIN attempts for one R-memory slot");

static void kdf(const uint8_t *key, uint32_t key_len, const uint8_t *data, uint32_t len,
                uint8_t out[32])
{
    hmac_sha256(key, key_len, data, len, out);
}

/** PIN || bind, the input of v and k_i. */
static uint32_t pin_input(const char *pin, const uint8_t *bind, uint8_t bind_len, uint8_t *buf,
                          uint32_t cap)
{
    size_t n = strlen(pin);
    if (n < CIWI_PIN_LEN_MIN || n > CIWI_PIN_LEN_MAX || n + bind_len > cap) return 0;
    for (size_t i = 0; i < n; i++) {
        if (pin[i] < '0' || pin[i] > '9') return 0;
    }
    memcpy(buf, pin, n);
    if (bind && bind_len) memcpy(buf + n, bind, bind_len);
    return (uint32_t)(n + bind_len);
}

static lt_ret_t state_write(ciwi_tr01_t *t, const pin_state_t *st)
{
    lt_ret_t r = lt_r_mem_data_erase(&t->lt, CIWI_PIN_RMEM_SLOT);
    if (r != LT_OK) return r;
    return lt_r_mem_data_write(&t->lt, CIWI_PIN_RMEM_SLOT, (const uint8_t *)st, sizeof(*st));
}

static lt_ret_t state_read(ciwi_tr01_t *t, pin_state_t *st)
{
    uint16_t n = 0;
    lt_ret_t r = lt_r_mem_data_read(&t->lt, CIWI_PIN_RMEM_SLOT, (uint8_t *)st, sizeof(*st), &n);
    if (r != LT_OK) return r;
    if (n != sizeof(*st) || st->version != PIN_STATE_VERSION || st->left > CIWI_PIN_ATTEMPTS) {
        return LT_FAIL;
    }
    return LT_OK;
}

/** Arm (initialise) slots [from, CIWI_PIN_ATTEMPTS) with u. */
static lt_ret_t arm_slots(ciwi_tr01_t *t, const uint8_t u[32], int from)
{
    uint8_t ignore[TR01_MAC_AND_DESTROY_DATA_SIZE];
    for (int i = from; i < CIWI_PIN_ATTEMPTS; i++) {
        lt_ret_t r = lt_mac_and_destroy(&t->lt, CIWI_PIN_MACANDD_FIRST + i, u, ignore);
        if (r != LT_OK) return r;
    }
    return LT_OK;
}

/** Set the PIN for a given secret s (random for a new PIN, the same s when changing it). */
static lt_ret_t pin_set_secret(ciwi_tr01_t *t, const char *pin, const uint8_t *bind,
                               uint8_t bind_len, const uint8_t s_in[32],
                               uint8_t pin_key[CIWI_PIN_KEY_LEN])
{
    uint8_t in[CIWI_PIN_LEN_MAX + 64];
    uint8_t s[32], u[32], v[32], w[32], k[32];
    uint8_t ignore[TR01_MAC_AND_DESTROY_DATA_SIZE];
    pin_state_t st;
    lt_ret_t r = LT_FAIL;

    memset(pin_key, 0, CIWI_PIN_KEY_LEN);
    memset(&st, 0, sizeof(st));
    uint32_t in_len = pin_input(pin, bind, bind_len, in, sizeof(in));
    if (!in_len) return LT_PARAM_ERR;
    if (!t->session_open) return LT_HOST_NO_SESSION;

    if (s_in) {
        memcpy(s, s_in, sizeof(s));
    } else if ((r = ciwi_tr01_random(t, s, sizeof(s))) != LT_OK) {
        goto out;
    }

    st.version = PIN_STATE_VERSION;
    st.left = CIWI_PIN_ATTEMPTS;
    kdf(s, 32, (const uint8_t[]){0x00}, 1, st.t);
    kdf(s, 32, (const uint8_t[]){0x01}, 1, u);
    static const uint8_t zeros[32] = {0};
    kdf(zeros, 32, in, in_len, v);

    for (int i = 0; i < CIWI_PIN_ATTEMPTS; i++) {
        const uint8_t slot = CIWI_PIN_MACANDD_FIRST + i;
        /* arm, read w_i with the PIN, arm again (reading consumes the slot) */
        if ((r = lt_mac_and_destroy(&t->lt, slot, u, ignore)) != LT_OK) goto out;
        if ((r = lt_mac_and_destroy(&t->lt, slot, v, w)) != LT_OK) goto out;
        if ((r = lt_mac_and_destroy(&t->lt, slot, u, ignore)) != LT_OK) goto out;
        kdf(w, 32, in, in_len, k);
        for (int j = 0; j < 32; j++) st.c[i][j] = s[j] ^ k[j];
    }

    if ((r = state_write(t, &st)) != LT_OK) goto out;
    kdf(s, 32, (const uint8_t *)"2", 1, pin_key);
    r = LT_OK;

out:
    memzero(in, sizeof(in));
    memzero(s, sizeof(s));
    memzero(u, sizeof(u));
    memzero(v, sizeof(v));
    memzero(w, sizeof(w));
    memzero(k, sizeof(k));
    memzero(&st, sizeof(st));
    return r;
}

/** Check a PIN; on success the slots are re-armed and the secret s is returned. */
static lt_ret_t pin_check_secret(ciwi_tr01_t *t, const char *pin, const uint8_t *bind,
                                 uint8_t bind_len, uint8_t s_out[32])
{
    uint8_t in[CIWI_PIN_LEN_MAX + 64];
    uint8_t s[32], u[32], v[32], w[32], k[32], tag[32];
    pin_state_t st;
    lt_ret_t r;

    memset(s_out, 0, 32);
    uint32_t in_len = pin_input(pin, bind, bind_len, in, sizeof(in));
    if (!in_len) return LT_PARAM_ERR;
    if (!t->session_open) return LT_HOST_NO_SESSION;

    if ((r = state_read(t, &st)) != LT_OK) goto out;
    if (st.left == 0) {
        CIWI_LOG("[PIN] locked: no attempts left\r\n");
        r = LT_FAIL;
        goto out;
    }

    /* Record the attempt first. */
    st.left--;
    if ((r = state_write(t, &st)) != LT_OK) goto out;
    const int i = st.left;

    static const uint8_t zeros[32] = {0};
    kdf(zeros, 32, in, in_len, v);
    if ((r = lt_mac_and_destroy(&t->lt, CIWI_PIN_MACANDD_FIRST + i, v, w)) != LT_OK) goto out;
    kdf(w, 32, in, in_len, k);
    for (int j = 0; j < 32; j++) s[j] = st.c[i][j] ^ k[j];
    kdf(s, 32, (const uint8_t[]){0x00}, 1, tag);

    /* constant-time compare */
    uint8_t diff = 0;
    for (int j = 0; j < 32; j++) diff |= tag[j] ^ st.t[j];
    if (diff != 0) {
        CIWI_LOG("[PIN] wrong PIN, %u attempt(s) left\r\n", st.left);
        r = LT_FAIL;
        goto out;
    }

    /* Correct PIN: re-arm every slot consumed since the last success (i .. ATTEMPTS-1) and reset
     * the counter. (The libtropic example stops one slot short, which leaves the top slot
     * destroyed after a successful entry.) */
    kdf(s, 32, (const uint8_t[]){0x01}, 1, u);
    if ((r = arm_slots(t, u, i)) != LT_OK) goto out;
    st.left = CIWI_PIN_ATTEMPTS;
    if ((r = state_write(t, &st)) != LT_OK) goto out;
    memcpy(s_out, s, 32);
    r = LT_OK;

out:
    memzero(in, sizeof(in));
    memzero(s, sizeof(s));
    memzero(u, sizeof(u));
    memzero(v, sizeof(v));
    memzero(w, sizeof(w));
    memzero(k, sizeof(k));
    memzero(tag, sizeof(tag));
    memzero(&st, sizeof(st));
    return r;
}

lt_ret_t ciwi_pin_set(ciwi_tr01_t *t, const char *pin, const uint8_t *bind, uint8_t bind_len,
                      uint8_t pin_key[CIWI_PIN_KEY_LEN])
{
    return pin_set_secret(t, pin, bind, bind_len, NULL, pin_key);
}

lt_ret_t ciwi_pin_check(ciwi_tr01_t *t, const char *pin, const uint8_t *bind, uint8_t bind_len,
                        uint8_t pin_key[CIWI_PIN_KEY_LEN])
{
    uint8_t s[32];
    memset(pin_key, 0, CIWI_PIN_KEY_LEN);
    lt_ret_t r = pin_check_secret(t, pin, bind, bind_len, s);
    if (r == LT_OK) kdf(s, 32, (const uint8_t *)"2", 1, pin_key);
    memzero(s, sizeof(s));
    return r;
}

lt_ret_t ciwi_pin_change(ciwi_tr01_t *t, const char *old_pin, const char *new_pin,
                         const uint8_t *bind, uint8_t bind_len, uint8_t pin_key[CIWI_PIN_KEY_LEN])
{
    uint8_t s[32];
    memset(pin_key, 0, CIWI_PIN_KEY_LEN);
    lt_ret_t r = pin_check_secret(t, old_pin, bind, bind_len, s);
    if (r == LT_OK) r = pin_set_secret(t, new_pin, bind, bind_len, s, pin_key);
    memzero(s, sizeof(s));
    return r;
}

lt_ret_t ciwi_pin_attempts_left(ciwi_tr01_t *t, uint8_t *left)
{
    pin_state_t st;
    lt_ret_t r = state_read(t, &st);
    *left = (r == LT_OK) ? st.left : 0;
    memzero(&st, sizeof(st));
    return r;
}
