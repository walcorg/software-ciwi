/**
 * @file    main_phase8.c
 * @brief   Phase 8: the CiWi interface (portrait, wheel + centre + MENU, optional touch) on the
 *          development board, connected to the TROPIC01 PIN and the Anchor self-test.
 *
 * Hardware: phase 7 setup (TROPIC01 on SPI2) plus the ILI9488 display with XPT2046 touch.
 * Without a TROPIC01 the interface still runs in DEMO mode (no PIN, no signatures).
 *
 * Controls on the WeAct board: KEY (PC13) = centre button. The wheel and MENU are emulated
 * from the serial terminal: a/d wheel, e centre click, m MENU, h MENU held. Emulated input can
 * navigate and reject, but only KEY held 2 s approves a signature.
 *
 * First boot with a new TROPIC01: the owner chooses the PIN (twice) and the chip is provisioned
 * with it (boot key, encrypted signing key, access policy, anchor key). Every boot after that
 * starts LOCKED and asks for the PIN; only the right PIN makes the signing key usable.
 */
#include <stdio.h>
#include <string.h>

#include "anchor_selftest.h"
#include "backlight.h"
#include "ciwi_log.h"
#include "display_tft.h"
#include "rng_test.h"
#include "stm32f4xx_hal.h"
#include "storage_hal.h"
#include "touch_xpt2046.h"
#include "tr01_ciwi.h"
#include "tr01_pin.h"
#include "uart_debug.h"
#include "ui_app.h"
#include "ui_platform_stm32.h"

#define FW_VERSION "0.5.0"

extern void stm32_init(void);

static ciwi_tr01_t g_t;
static bool g_tr01;          /* TROPIC01 present, provisioned and in session */
static uint8_t g_uid[12];

/* ------------------------------------------------------------------ identity */

static char g_id1[24], g_id2[24], g_tr01_ver[24];
static const char *g_lines[6];

static void short_hex(const uint8_t *b, size_t n, char *out)
{
    static const char hx[] = "0123456789abcdef";
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 3; i++) { out[2 + 2 * i] = hx[b[i] >> 4]; out[3 + 2 * i] = hx[b[i] & 15]; }
    memcpy(out + 8, "...", 3);
    for (int i = 0; i < 2; i++) {
        out[11 + 2 * i] = hx[b[n - 2 + i] >> 4];
        out[12 + 2 * i] = hx[b[n - 2 + i] & 15];
    }
    out[15] = 0;
}

static const char *const *identity_lines(void)
{
    int i = 0;
    if (g_tr01 && ciwi_tr01_ciwi_id(&g_t)) {
        g_lines[i++] = "ciwiId";
        short_hex(ciwi_tr01_ciwi_id(&g_t), 32, g_id1);
        g_lines[i++] = g_id1;
        g_lines[i++] = g_tr01_ver;
    } else if (!g_tr01) {
        g_lines[i++] = "DEMO: sin TROPIC01";
    }
    snprintf(g_id2, sizeof(g_id2), "Firmware %s", FW_VERSION);
    g_lines[i++] = g_id2;
    g_lines[i] = NULL;
    return g_lines;
}

/* ------------------------------------------------------------------ PIN */

/** Ask for a new PIN twice. Returns false if the owner cancelled. */
static bool ask_new_pin(char out[9])
{
    char again[9];
    for (;;) {
        if (ui_run_pin(ui_str(S_PIN_NEW), NULL, out) != UI_PIN_OK) return false;
        if (ui_run_pin(ui_str(S_PIN_REPEAT), NULL, again) != UI_PIN_OK) continue;
        bool same = strcmp(out, again) == 0;
        memset(again, 0, sizeof(again));
        if (same) return true;
        ui_run_message(ui_str(S_PIN_NEW), ui_str(S_PIN_MISMATCH), UI_MSG_BAD);
    }
}

/** First boot: the owner chooses the PIN and the TROPIC01 is provisioned with it. */
static bool setup(void)
{
    char pin[9];
    ui_run_message(ui_str(S_SETUP), ui_str(S_PIN_NEW), UI_MSG_INFO);
    while (!ask_new_pin(pin)) {}
    ui_show_message(ui_str(S_SETUP), "TROPIC01...", UI_MSG_INFO);
    lt_ret_t r = ciwi_tr01_provision(&g_t, pin, g_uid, sizeof(g_uid));
    memset(pin, 0, sizeof(pin));
    if (r != LT_OK) {
        ui_run_message(ui_str(S_SETUP), lt_ret_verbose(r), UI_MSG_BAD);
        return false;
    }
    return true;
}

/** Lock screen / boot: the signing key is usable only after the right PIN. */
static bool unlock(void)
{
    if (!g_tr01) return true; /* DEMO mode */
    if (g_t.state == CIWI_TR01_STATE_NEW) return setup();
    if (g_t.state == CIWI_TR01_STATE_READY) ciwi_tr01_lock(&g_t);
    for (;;) {
        uint8_t left = 0;
        ciwi_tr01_attempts_left(&g_t, &left);
        if (left == 0) {
            ui_run_message(ui_str(S_LOCKED), ui_str(S_PIN_LOCKED), UI_MSG_BAD);
            return false;
        }
        char note[32], pin[9];
        snprintf(note, sizeof(note), ui_str(S_PIN_LEFT), left);
        if (ui_run_pin(ui_str(S_PIN_ENTER), left < CIWI_PIN_ATTEMPTS ? note : NULL, pin) !=
            UI_PIN_OK) {
            return false;
        }
        ui_show_message(ui_str(S_PIN_ENTER), "TROPIC01...", UI_MSG_INFO);
        lt_ret_t r = ciwi_tr01_unlock(&g_t, pin, g_uid, sizeof(g_uid), &left);
        memset(pin, 0, sizeof(pin));
        if (r == LT_OK) return true;
        if (r != LT_FAIL) {                     /* not a wrong PIN: a chip or storage error */
            ui_run_message(ui_str(S_PIN_ENTER), lt_ret_verbose(r), UI_MSG_BAD);
            continue;
        }
        snprintf(note, sizeof(note), ui_str(S_PIN_LEFT), left);
        ui_run_message(ui_str(S_PIN_WRONG), note, UI_MSG_BAD);
    }
}

/** Settings > Change PIN. */
static void change_pin(void)
{
    char old[9], neu[9];
    if (!g_tr01) {
        ui_run_message(ui_str(S_CHANGE_PIN), "DEMO: sin TROPIC01.", UI_MSG_BAD);
        return;
    }
    if (ui_run_pin(ui_str(S_PIN_OLD), NULL, old) != UI_PIN_OK) return;
    if (!ask_new_pin(neu)) {
        memset(old, 0, sizeof(old));
        return;
    }
    ui_show_message(ui_str(S_CHANGE_PIN), "TROPIC01...", UI_MSG_INFO);
    lt_ret_t r = ciwi_tr01_change_pin(&g_t, old, neu, g_uid, sizeof(g_uid));
    memset(old, 0, sizeof(old));
    memset(neu, 0, sizeof(neu));
    if (r == LT_OK) {
        ui_run_message(ui_str(S_CHANGE_PIN), ui_str(S_PIN_CHANGED), UI_MSG_GOOD);
    } else {
        ui_run_message(ui_str(S_CHANGE_PIN), ui_str(S_PIN_WRONG), UI_MSG_BAD);
        unlock(); /* a failed change leaves the CiWi locked */
    }
}

/* ------------------------------------------------------------------ Anchor self-test */

static char g_json[12 * 1024];
static size_t g_json_len;
static int g_checks, g_failed;

static void emit_json(const char *text)
{
    size_t n = strlen(text);
    if (g_json_len + n < sizeof(g_json)) {
        memcpy(g_json + g_json_len, text, n + 1);
        g_json_len += n;
    }
}

static bool confirm_ui(const char *title, const char *detail)
{
    const ui_field_t f[] = {{"Test", "Vectores de prueba (shield de prueba)"}, {"Detalle", detail}};
    ui_confirm_t c = {title, f, 2, true, NULL};
    return ui_run_confirm(&c) == UI_CONFIRM_APPROVED;
}

static void result(const char *name, bool ok)
{
    g_checks++;
    if (!ok) g_failed++;
    CIWI_LOG("  %s  %s\r\n", ok ? "PASS" : "FAIL", name);
}

static void anchor_test(void)
{
    if (!g_tr01) {
        ui_run_message("Anchor", "DEMO: sin TROPIC01.", UI_MSG_BAD);
        return;
    }
    g_json_len = 0;
    g_checks = g_failed = 0;
    anchor_selftest_io_t io = {emit_json, confirm_ui, result};
    anchor_selftest_run(&g_t, &io);
    CIWI_LOG("\r\n-----BEGIN CIWI ANCHOR VECTORS-----\r\n");
    debug_print(g_json);
    CIWI_LOG("-----END CIWI ANCHOR VECTORS-----\r\n");
    char msg[64];
    snprintf(msg, sizeof(msg), "%d/%d OK. JSON en el UART.", g_checks - g_failed, g_checks);
    ui_run_message(ui_str(S_DONE), msg, g_failed ? UI_MSG_BAD : UI_MSG_GOOD);
}

/* ------------------------------------------------------------------ main */

int main(void)
{
    stm32_init();
    CIWI_LOG("\r\n== WALCORG CiWi - phase 8: interface (software-ciwi %s) ==\r\n", FW_VERSION);
    CIWI_LOG("UART keys: a/d wheel, e centre, m MENU, h MENU held. KEY (PC13) = centre button.\r\n");
    rng_init();
    storage_hal_init();
    memcpy(g_uid, (const void *)UID_BASE, sizeof(g_uid));

    if (!tft_init(TFT_ROTATION_0)) CIWI_LOG("[UI] display not found\r\n");
    backlight_init();
    bool touch_hw = touch_init();

    const ui_platform_t *plat = ui_platform_stm32_init(touch_hw);
    ui_settings_t s;
    ui_platform_stm32_load_settings(&s);
    backlight_set((uint8_t)(s.brightness * 51));

    /* TROPIC01: present and provisioned (phase 7) -> real PIN and signatures; else DEMO. */
    ciwi_tr01_state_t st = CIWI_TR01_STATE_UNKNOWN;
    if (ciwi_tr01_open(&g_t) == LT_OK) {
        ciwi_tr01_start(&g_t);
        st = g_t.state;
    }
    if (st == CIWI_TR01_STATE_LOCKED || st == CIWI_TR01_STATE_NEW) {
        g_tr01 = true;
        uint8_t v[TR01_L2_GET_INFO_RISCV_FW_SIZE];
        if (lt_get_info_riscv_fw_ver(&g_t.lt, v) == LT_OK) {
            snprintf(g_tr01_ver, sizeof(g_tr01_ver), "TROPIC01 %u.%u.%u", v[3], v[2], v[1]);
        }
        ui_platform_stm32_set_status("USB \xC2\xB7 TROPIC01");
    } else {
        CIWI_LOG("[UI] TROPIC01 not found or keys rejected: DEMO mode\r\n");
        ui_platform_stm32_set_status("DEMO \xC2\xB7 sin TROPIC01");
    }

    static const ui_actions_t actions = {unlock, anchor_test, change_pin, identity_lines,
                                         FW_VERSION};
    ui_app_init(plat, &actions, &s);

    while (!unlock()) {
        /* cancelled or locked out: ask again */
    }
    ui_app_run();
    for (;;) {}
}
