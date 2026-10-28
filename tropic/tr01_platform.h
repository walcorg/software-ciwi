/**
 * @file    tr01_platform.h
 * @brief   What the portable TROPIC01 code needs from the platform.
 *
 * Two implementations exist:
 *   - tropic/tr01_platform_stm32.c  (CiWi: SPI2, STM32 RNG, NORCOW flash)
 *   - tests/host/platform_host.c    (PC: TROPIC01 model over TCP, /dev/urandom, a file)
 */
#ifndef TR01_PLATFORM_H
#define TR01_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libtropic.h"

/** Size of the host's X25519 pairing keys. */
#define TR01_PAIRING_KEY_LEN 32

/**
 * Connect the libtropic handle to the physical bus (sets h->l2.device).
 * Must be called before lt_init().
 */
bool tr01_platform_attach(lt_handle_t *h);

/** Fill buf with random bytes from the MCU's own generator (independent of TROPIC01). */
bool tr01_platform_random(uint8_t *buf, size_t len);

/** Load the CiWi's pairing key pair (slot 1). Returns false if none is stored. */
bool tr01_keystore_load(uint8_t priv[TR01_PAIRING_KEY_LEN], uint8_t pub[TR01_PAIRING_KEY_LEN]);

/** Store the CiWi's pairing key pair (slot 1). */
bool tr01_keystore_save(const uint8_t priv[TR01_PAIRING_KEY_LEN],
                        const uint8_t pub[TR01_PAIRING_KEY_LEN]);

/** Forget the stored pairing key pair. */
bool tr01_keystore_erase(void);

#endif /* TR01_PLATFORM_H */
