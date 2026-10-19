/**
 * @file    trezor_rand_hw.c
 * @brief   [v0.2] Platform hooks required by trezor-crypto, backed by the STM32F405 hardware RNG.
 *
 * trezor-crypto declares random_buffer() in rand.h and tc_fault_handler() in fault_handler.h,
 * but leaves the implementation to the platform.
 * If the RNG fails, the system halts: keys are never generated with weak randomness.
 *
 * @license GNU GPL v3
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "rand.h"
#include "fault_handler.h"
#include "rng_test.h"
#include "uart_debug.h"

extern void Error_Handler(void);

void random_buffer(uint8_t *buf, size_t len) {
    if (!rng_fill_buffer(buf, (uint32_t)len)) {
        memset(buf, 0, len);
        Error_Handler();
    }
}

void random_reseed(const uint32_t value) {
    (void)value;   /* The hardware TRNG needs no seed */
}

/* Called by trezor-crypto when it detects a computation fault
 * (for example, a fault-injection attack). The system halts. */
void tc_fault_handler(const char *msg) {
    DEBUG_PRINT("\r\n[CRYPTO] FAULT: ");
    DEBUG_PRINT(msg);
    Error_Handler();
    while (1) { }
}
