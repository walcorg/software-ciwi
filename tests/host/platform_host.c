/**
 * @file    platform_host.c
 * @brief   tr01_platform.h for the PC test bench.
 *
 * Bus:      TROPIC01 model (ts-tvl model_server) over TCP, 127.0.0.1:28992
 * RNG:      getrandom()
 * Keystore: files (CIWI_KEYSTORE.<id>, default ./ciwi-keystore.<id>) standing in for the STM32 flash
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#include "libtropic_port_posix_tcp.h"
#include "tr01_platform.h"

static lt_dev_posix_tcp_t g_dev;

static void keystore_path(tr01_ks_id_t id, char *out, size_t cap)
{
    const char *p = getenv("CIWI_KEYSTORE");
    snprintf(out, cap, "%s.%d", p ? p : "ciwi-keystore", (int)id);
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

bool tr01_keystore_load(tr01_ks_id_t id, uint8_t *buf, uint16_t max, uint16_t *len)
{
    char path[256];
    keystore_path(id, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, max, f);
    fclose(f);
    if (len) *len = (uint16_t)n;
    return n > 0;
}

bool tr01_keystore_save(tr01_ks_id_t id, const uint8_t *buf, uint16_t len)
{
    char path[256];
    keystore_path(id, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(buf, 1, len, f) == len;
    fclose(f);
    return ok;
}

bool tr01_keystore_erase(tr01_ks_id_t id)
{
    char path[256];
    keystore_path(id, path, sizeof(path));
    return remove(path) == 0;
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
