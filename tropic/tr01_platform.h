/**
 * @file    tr01_platform.h
 * @brief   What the portable TROPIC01 code needs from the platform.
 *
 * Two implementations exist:
 *   - tropic/tr01_platform_stm32.c  (CiWi: SPI2, STM32 RNG, NORCOW flash)
 *   - tests/host/platform_host.c    (PC: TROPIC01 model over TCP, /dev/urandom, files)
 */
#ifndef TR01_PLATFORM_H
#define TR01_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libtropic.h"

/** Size of the host's X25519 pairing keys. */
#define TR01_PAIRING_KEY_LEN 32

/** Records the CiWi keeps in the MCU flash. */
typedef enum {
    TR01_KS_BOOT = 1,  /**< SH2 boot key, priv || pub (64 B). Can only check the PIN. */
    TR01_KS_SH1 = 2,   /**< SH1 key, encrypted with the PIN key (see tr01_ciwi.c). */
} tr01_ks_id_t;

#define TR01_KS_MAX 128

/**
 * Connect the libtropic handle to the physical bus (sets h->l2.device).
 * Must be called before lt_init().
 */
bool tr01_platform_attach(lt_handle_t *h);

/** Fill buf with random bytes from the MCU's own generator (independent of TROPIC01). */
bool tr01_platform_random(uint8_t *buf, size_t len);

/** Load a record. Returns false if it does not exist. */
bool tr01_keystore_load(tr01_ks_id_t id, uint8_t *buf, uint16_t max, uint16_t *len);

/** Store (replace) a record. */
bool tr01_keystore_save(tr01_ks_id_t id, const uint8_t *buf, uint16_t len);

/** Forget a record. */
bool tr01_keystore_erase(tr01_ks_id_t id);

#endif /* TR01_PLATFORM_H */
