/**
 * @file    rng_test.h
 * @brief   Verificación del Hardware RNG — WalCorg Cold Wallet Fase 1
 *
 * El RNG (Random Number Generator) del STM32F405 es el componente
 * más crítico de seguridad del cold wallet. Sin números verdaderamente
 * aleatorios, las semillas BIP39 generadas serían predecibles y
 * los fondos podrían ser robados.
 *
 * EL STM32F405 tiene un RNG por hardware basado en ruido térmico
 * de circuitos analógicos internos. Es diferente a un PRNG
 * (Pseudo-Random) que usa algoritmos matemáticos — el hardware
 * RNG es no determinístico por naturaleza física.
 *
 * UBICACIÓN DEL PERIFÉRICO:
 *   Registro base: 0x50060800
 *   Clock:         habilitado por __HAL_RCC_RNG_CLK_ENABLE()
 *   Clock fuente:  PLLQ = 48MHz (mismo que USB)
 *   IRQ:           HASH_RNG_IRQHandler (IRQ[80])
 *
 * TESTS QUE EJECUTA ESTE MÓDULO:
 *
 *   TEST A — Funcional básico:
 *     Genera 1000 números y verifica que ninguno es 0x00000000
 *     ni 0xFFFFFFFF (valores que indicarían fallo del periférico)
 *
 *   TEST B — Detección de duplicados consecutivos:
 *     El STM32F405 tiene hardware de detección de error que
 *     setea el flag SEIS si dos números consecutivos son iguales.
 *     Verificamos que este mecanismo funciona.
 *
 *   TEST C — Distribución estadística mínima:
 *     Genera 256 bytes y verifica que los 8 bits están presentes
 *     (no todos son 0 o todos son 1). No es un test estadístico
 *     riguroso — para eso usar dieharder en los datos exportados.
 *
 *   TEST D — Velocidad:
 *     El RNG del F405 debe generar ~5 Mbit/s a 48MHz.
 *     Mide el tiempo de 1000 lecturas y verifica que está en rango.
 *
 * USO EN CÓDIGO:
 *   rng_init();
 *   if (!rng_run_tests()) { Error_Handler(); }
 *   uint32_t random_word = rng_get32();
 *   rng_fill_buffer(buffer, 32);  // 32 bytes para semilla BIP39
 *
 * @license GNU GPL v3
 */

#ifndef RNG_TEST_H
#define RNG_TEST_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* =========================================================
 * CONFIGURACIÓN
 * ========================================================= */

/** Número de palabras generadas en el test funcional */
#define RNG_TEST_SAMPLES        1000U

/** Timeout máximo por lectura individual del RNG (ms)
 *  El RNG tarda ~40 ciclos de su clock (48MHz) = ~0.83 µs
 *  Un timeout de 10ms es extremadamente generoso */
#define RNG_READ_TIMEOUT_MS     10U

/** Número de bits que deben aparecer en el test de distribución
 *  De 256 bytes, esperamos los 8 posibles valores de bit */
#define RNG_DIST_MIN_UNIQUE     250U    /* Al menos 250 de 256 valores únicos */


/* =========================================================
 * ESTRUCTURA DE RESULTADOS
 * ========================================================= */

/**
 * Resultados detallados de los tests del RNG.
 * Permite diagnosticar exactamente qué falló.
 */
typedef struct {
    bool    test_a_functional;      /**< Test A: generación básica OK */
    bool    test_b_consecutive;     /**< Test B: sin duplicados consecutivos */
    bool    test_c_distribution;    /**< Test C: distribución mínima OK */
    bool    test_d_speed;           /**< Test D: velocidad en rango */
    bool    all_passed;             /**< Todos los tests pasaron */

    uint32_t samples_generated;     /**< Palabras generadas en test A */
    uint32_t zero_count;            /**< Cantidad de ceros (debe ser 0) */
    uint32_t ones_count;            /**< Cantidad de 0xFFFFFFFF (debe ser 0) */
    uint32_t consecutive_equal;     /**< Pares consecutivos iguales (debe ser 0) */
    uint32_t unique_bytes;          /**< Bytes únicos encontrados (test C) */
    uint32_t speed_us_per_1000;     /**< Microsegundos para 1000 lecturas */
} rng_test_results_t;


/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Inicializa el periférico RNG del STM32F405.
 *
 * Habilita el clock de RNG (desde PLLQ=48MHz), configura
 * el periférico y verifica que el hardware está listo.
 *
 * DEBE llamarse después de SystemClock_Config() porque
 * el RNG necesita que PLLQ esté configurado a 48MHz.
 *
 * @return true  si el RNG inicializó correctamente
 * @return false si hay un error de hardware (periférico no responde)
 */
bool rng_init(void);

/**
 * @brief Ejecuta todos los tests de validación del RNG.
 *
 * Llama internamente a los 4 tests (A, B, C, D) y
 * reporta los resultados por UART.
 *
 * @param results  Puntero donde se guardan los resultados detallados.
 *                 Puede ser NULL si solo interesa el valor de retorno.
 * @return true    si TODOS los tests pasaron
 * @return false   si algún test falló (ver results para detalles)
 */
bool rng_run_tests(rng_test_results_t *results);

/**
 * @brief Genera un número aleatorio de 32 bits.
 *
 * Espera hasta que el RNG tenga un número disponible
 * (flag DRDY) y lo retorna.
 *
 * Verifica internamente los flags de error del hardware:
 *   CEIS = Clock Error Interrupt Status (error de clock)
 *   SEIS = Seed Error Interrupt Status  (error de semilla)
 *
 * @param out    Puntero donde se guarda el número generado
 * @return true  si la generación fue exitosa
 * @return false si hubo timeout o error de hardware
 */
bool rng_get32(uint32_t *out);

/**
 * @brief Llena un buffer con bytes aleatorios.
 *
 * Genera ceil(len/4) palabras de 32 bits y las copia al buffer.
 * Uso principal: generar la entropía para semillas BIP39 (32 bytes).
 *
 * @param buf    Buffer destino
 * @param len    Número de bytes a generar
 * @return true  si todos los bytes fueron generados sin error
 * @return false si hubo algún error de hardware durante la generación
 */
bool rng_fill_buffer(uint8_t *buf, uint32_t len);

/**
 * @brief Verifica si el RNG está listo para generar números.
 *
 * Útil para verificar el estado antes de una operación crítica
 * como la generación de una semilla.
 *
 * @return true  si el RNG está inicializado y funcionando
 * @return false si hay algún error pendiente
 */
bool rng_is_healthy(void);

/**
 * @brief Deshabilita el RNG para ahorrar energía.
 *
 * Llamar cuando no se necesita el RNG por un período largo.
 * Llamar rng_init() para volver a habilitarlo.
 */
void rng_disable(void);

/** Handle global del RNG — accedido desde stm32f4xx_it.c */
extern RNG_HandleTypeDef hrng;

#endif /* RNG_TEST_H */
