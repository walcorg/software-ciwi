/**
 * @file    platform_host.c
 * @brief   tr01_platform.h for the PC test bench.
 *
 * Bus:      TROPIC01 model (ts-tvl model_server) over TCP, 127.0.0.1:28992
 * RNG:      getrandom()
 * Keystore: a file (CIWI_KEYSTORE, default ./ciwi-keystore.bin) standing in for the STM32 flash
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#include "libtropic_port_posix_tcp.h"
#include "tr01_platform.h"

static lt_dev_posix_tcp_t g_dev;

static const char *keystore_path(void)
{
    const char *p = getenv("CIWI_KEYSTORE");
    return p ? p : "ciwi-keystore.bin";
}

bool tr01_platform_attach(lt_handle_t *h)
{
    memset(&g_dev, 0, sizeof(g_dev));
    g_dev.addr = inet_addr("127.0.0.1");
    g_dev.port = 28992;
    h->l2.device = &g_dev;
    return true;
}

bool tr01_platform_random(uint8_t *buf, size_t len)
{
    return getrandom(buf, len, 0) == (ssize_t)len;
}

bool tr01_keystore_load(uint8_t priv[TR01_PAIRING_KEY_LEN], uint8_t pub[TR01_PAIRING_KEY_LEN])
{
    FILE *f = fopen(keystore_path(), "rb");
    if (!f) return false;
    bool ok = fread(priv, 1, TR01_PAIRING_KEY_LEN, f) == TR01_PAIRING_KEY_LEN &&
              fread(pub, 1, TR01_PAIRING_KEY_LEN, f) == TR01_PAIRING_KEY_LEN;
    fclose(f);
    return ok;
}

bool tr01_keystore_save(const uint8_t priv[TR01_PAIRING_KEY_LEN],
                        const uint8_t pub[TR01_PAIRING_KEY_LEN])
{
    FILE *f = fopen(keystore_path(), "wb");
    if (!f) return false;
    bool ok = fwrite(priv, 1, TR01_PAIRING_KEY_LEN, f) == TR01_PAIRING_KEY_LEN &&
              fwrite(pub, 1, TR01_PAIRING_KEY_LEN, f) == TR01_PAIRING_KEY_LEN;
    fclose(f);
    return ok;
}

bool tr01_keystore_erase(void)
{
    return remove(keystore_path()) == 0;
}

/* trezor-crypto asks the platform for randomness (ECDSA nonces use RFC 6979, so this is only
 * used for blinding). */
void random_buffer(uint8_t *buf, size_t len)
{
    if (!tr01_platform_random(buf, len)) abort();
}

/* trezor-crypto reports detected faults here. */
void tc_fault_handler(const char *msg)
{
    fprintf(stderr, "crypto fault: %s\n", msg ? msg : "?");
    abort();
}
