/**
 * @file    trezor_rand_hw.c
 * @brief   [v0.2] random_buffer() para trezor-crypto usando el RNG por hardware del STM32F405.
 *
 * trezor-crypto declara random_buffer() en rand.h pero deja la implementación a la plataforma.
 * Si el RNG falla, se detiene el sistema: nunca se generan claves con aleatoriedad débil.
 *
 * @license GNU GPL v3
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "rand.h"
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
    (void)value;   /* El TRNG por hardware no necesita semilla */
}

/* [v0.2] trezor-crypto llama tc_fault_handler() si detecta un fallo de cómputo
 *        (por ejemplo, un ataque de inyección de fallas). Se detiene el sistema. */
#include "fault_handler.h"
void tc_fault_handler(const char *msg) {
    DEBUG_PRINT("\r\n[CRYPTO] FALLO: ");
    DEBUG_PRINT(msg);
    Error_Handler();
    while (1) { }
}
