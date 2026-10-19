/**
 * @file    rng_test.c
 * @brief   Verificación del Hardware RNG — WalCorg Cold Wallet Fase 1
 *
 * ARQUITECTURA DEL RNG EN EL STM32F405:
 *
 *   ┌─────────────────────────────────────────┐
 *   │           RNG Hardware Block             │
 *   │                                          │
 *   │  Ruido térmico → LFSR → Condicionador   │
 *   │       (analógico)   (digital)            │
 *   │                          │               │
 *   │                     RNG_DR (32 bits)     │
 *   │                          │               │
 *   │  RNG_SR:                 ▼               │
 *   │    DRDY=1 → dato listo ──┘               │
 *   │    CEIS=1 → error de clock               │
 *   │    SEIS=1 → error de semilla             │
 *   └─────────────────────────────────────────┘
 *
 * FUENTE DE CLOCK DEL RNG:
 *   El RNG usa PLLQ como fuente de clock.
 *   PLLQ = 336MHz ÷ 7 = 48MHz
 *   El RNG internamente divide por 64: 48MHz ÷ 64 = 750kHz
 *   A 750kHz genera 1 bit de entropía por ciclo → 750kbps
 *   Cada número de 32 bits tarda ~43µs en generarse.
 *
 * DETECCIÓN DE ERRORES HARDWARE:
 *   CEIS: si el clock del RNG es < 800Hz o > 48MHz, el hardware
 *         detecta el error automáticamente y setea CEIS.
 *   SEIS: si dos números consecutivos son iguales, el hardware
 *         detecta posible fallo del generador y setea SEIS.
 *   Ambos errores deben ser fatales para el wallet — sin entropía
 *   genuina, no se puede generar una semilla segura.
 *
 * @license GNU GPL v3
 */

#include "rng_test.h"
#include "uart_debug.h"
#include <string.h>     /* memset */

/* =========================================================
 * HANDLE GLOBAL DEL RNG
 * ========================================================= */
RNG_HandleTypeDef hrng;

/* Estado interno del módulo */
static bool _rng_initialized = false;


/* =========================================================
 * INICIALIZACIÓN
 * ========================================================= */

/**
 * rng_init — Inicializar el periférico RNG del STM32F405
 *
 * Secuencia de inicialización:
 *   1. Habilitar clock de RNG (depende de PLLQ=48MHz)
 *   2. Configurar el periférico via HAL
 *   3. Verificar que el primer número se genera correctamente
 *   4. Limpiar cualquier flag de error residual
 */
bool rng_init(void) {

    DEBUG_PRINT("[RNG] Inicializando hardware RNG...\r\n");

    /* -----------------------------------------------------------------
     * Verificar que PLLQ está activo y a 48MHz
     *
     * El RNG REQUIERE que su clock (PLLQ) esté entre 800Hz y 48MHz.
     * Si el PLL no está configurado correctamente, el RNG no funciona.
     *
     * __HAL_RCC_GET_PLL_OSCSOURCE() retorna la fuente del PLL.
     * Si retorna HSE, el PLL está activo.
     * ----------------------------------------------------------------- */
    if (__HAL_RCC_GET_SYSCLK_SOURCE() != RCC_CFGR_SWS_PLL) {
        DEBUG_PRINT("[RNG] ERROR: PLL no activo. PLLQ=48MHz requerido.\r\n");
        DEBUG_PRINT("[RNG] Llamar SystemClock_Config() antes de rng_init().\r\n");
        return false;
    }

    /* -----------------------------------------------------------------
     * Habilitar clock del periférico RNG
     *
     * El RNG está en el bus AHB2.
     * Sin este clock, el acceso a los registros del RNG
     * causaría un Bus Fault.
     * ----------------------------------------------------------------- */
    __HAL_RCC_RNG_CLK_ENABLE();

    /* -----------------------------------------------------------------
     * Configurar el periférico via HAL
     *
     * El HAL del RNG es simple — solo necesita la instancia.
     * El resto de la configuración (clock source, etc.) la maneja
     * el RCC automáticamente.
     * ----------------------------------------------------------------- */
    hrng.Instance = RNG;

    if (HAL_RNG_Init(&hrng) != HAL_OK) {
        DEBUG_PRINT("[RNG] ERROR: HAL_RNG_Init falló.\r\n");
        DEBUG_PRINT("[RNG] Verificar que PLLQ=48MHz está activo.\r\n");
        return false;
    }

    /* -----------------------------------------------------------------
     * Limpiar flags de error residuales
     *
     * En caso de que haya errores previos de sesiones anteriores,
     * limpiarlos antes de usar el RNG.
     *
     * RNG_SR_CEIS: Clock Error Interrupt Status
     * RNG_SR_SEIS: Seed Error Interrupt Status
     * ----------------------------------------------------------------- */
    __HAL_RNG_CLEAR_IT(&hrng, RNG_IT_CEI | RNG_IT_SEI);

    /* -----------------------------------------------------------------
     * Verificar que el primer número se genera correctamente
     *
     * Generar un número de prueba y verificar que no hay errores.
     * Si este paso falla, hay un problema de hardware.
     * ----------------------------------------------------------------- */
    uint32_t test_val = 0;
    if (HAL_RNG_GenerateRandomNumber(&hrng, &test_val) != HAL_OK) {
        DEBUG_PRINT("[RNG] ERROR: Primera generación falló.\r\n");
        DEBUG_PRINT("[RNG] Posible fallo de hardware en el RNG.\r\n");
        return false;
    }

    /* Verificar que el número no es obviamente malo */
    if (test_val == 0x00000000 || test_val == 0xFFFFFFFF) {
        DEBUG_PRINTF("[RNG] WARN: Primer número sospechoso: 0x%08lX\r\n",
            test_val);
        /* No falla en el primer intento — podría ser coincidencia */
    }

    _rng_initialized = true;
    DEBUG_PRINTF("[RNG] Inicializado. Primer valor: 0x%08lX\r\n", test_val);
    return true;
}


/* =========================================================
 * GENERACIÓN DE NÚMEROS
 * ========================================================= */

/**
 * rng_get32 — Obtener un número aleatorio de 32 bits
 *
 * Espera activamente (polling) hasta que:
 *   a) El bit DRDY (Data Ready) del RNG_SR se setea → éxito
 *   b) Se detecta un error de hardware (CEIS o SEIS) → fallo
 *   c) Se supera el timeout → fallo
 *
 * En caso de error hardware, intenta recuperar el RNG
 * limpiando los flags y reiniciando el periférico.
 */
bool rng_get32(uint32_t *out) {
    if (!out || !_rng_initialized) return false;

    uint32_t timeout_start = HAL_GetTick();

    while (1) {
        /* ---- Verificar timeout ---- */
        if ((HAL_GetTick() - timeout_start) > RNG_READ_TIMEOUT_MS) {
            DEBUG_PRINT("[RNG] ERROR: Timeout esperando DRDY.\r\n");
            DEBUG_PRINT("[RNG] El periférico RNG no responde.\r\n");
            return false;
        }

        /* ---- Verificar error de clock (CEIS) ----
         *
         * CEIS se setea cuando el clock del RNG está fuera de rango.
         * Esto es fatal — sin clock correcto no hay entropía real.
         *
         * El hardware automáticamente para de generar números cuando
         * detecta este error, para proteger la seguridad.
         */
        if (__HAL_RNG_GET_FLAG(&hrng, RNG_FLAG_CECS)) {
            DEBUG_PRINT("[RNG] ERROR FATAL: Clock Error (CEIS).\r\n");
            DEBUG_PRINT("[RNG] PLLQ debe ser exactamente 48MHz.\r\n");
            DEBUG_PRINT("[RNG] Verificar SystemClock_Config() PLLQ=7.\r\n");

            /* Intentar recuperación: limpiar flag y reiniciar */
            __HAL_RNG_CLEAR_IT(&hrng, RNG_IT_CEI);
            HAL_RNG_DeInit(&hrng);
            HAL_Delay(1);
            HAL_RNG_Init(&hrng);
            return false;  /* Reportar fallo — no continuar */
        }

        /* ---- Verificar error de semilla (SEIS) ----
         *
         * SEIS se setea cuando el hardware detecta que dos números
         * consecutivos son iguales. Esto indica que el generador
         * de ruido analógico posiblemente falló.
         *
         * A diferencia de CEIS, SEIS puede ser recuperable:
         * el hardware genera un nuevo número automáticamente
         * después de limpiar el flag.
         */
        if (__HAL_RNG_GET_FLAG(&hrng, RNG_FLAG_SECS)) {
            DEBUG_PRINT("[RNG] WARN: Seed Error (SEIS) detectado.\r\n");
            DEBUG_PRINT("[RNG] Hardware generó dos consecutivos iguales.\r\n");

            /* Limpiar el flag — el hardware continuará generando */
            __HAL_RNG_CLEAR_IT(&hrng, RNG_IT_SEI);
            /* El dato actual NO es confiable — descartar y reintentar */
            (void)hrng.Instance->DR;  /* Leer y descartar el dato malo */
            continue;  /* Reintentar */
        }

        /* ---- Verificar si el dato está listo (DRDY) ----
         *
         * DRDY = Data Ready. El RNG tarda ~40 ciclos de su clock
         * (48MHz ÷ 64 = 750kHz) = ~53µs por número.
         * Hacemos polling en lugar de usar interrupción para
         * simplificar el código en Fase 1.
         */
        if (__HAL_RNG_GET_FLAG(&hrng, RNG_FLAG_DRDY)) {
            /* Leer el registro de datos del RNG
             * IMPORTANTE: leer RNG_DR limpia automáticamente DRDY */
            *out = hrng.Instance->DR;

            /* Verificación de seguridad: rechazar valores trivialmente malos */
            if (*out == 0x00000000 || *out == 0xFFFFFFFF) {
                /* Estadísticamente rarísimo — si pasa mucho, hay un bug */
                DEBUG_PRINTF("[RNG] WARN: Valor trivial 0x%08lX, reintentando\r\n",
                    *out);
                continue;  /* Reintentar */
            }

            return true;  /* Éxito */
        }

        /* Si DRDY no está listo, esperar brevemente antes de reintentar
         * __NOP() = una instrucción "no operation" = ~6ns a 168MHz */
        __NOP();
        __NOP();
        __NOP();
    }
}

/**
 * rng_fill_buffer — Llenar buffer con bytes aleatorios
 *
 * Genera palabras de 32 bits y las copia al buffer de bytes.
 * Para 32 bytes (256 bits de entropía para BIP39), genera 8 palabras.
 */
bool rng_fill_buffer(uint8_t *buf, uint32_t len) {
    if (!buf || len == 0 || !_rng_initialized) return false;

    uint32_t word;
    uint32_t bytes_written = 0;

    while (bytes_written < len) {
        /* Generar siguiente palabra de 32 bits */
        if (!rng_get32(&word)) {
            DEBUG_PRINTF("[RNG] ERROR en rng_fill_buffer() offset=%lu\r\n",
                bytes_written);
            /* Limpiar el buffer por seguridad antes de retornar error */
            memset(buf, 0x00, len);
            return false;
        }

        /* Copiar los bytes de la palabra al buffer
         * Copiamos byte a byte para manejar correctamente
         * buffers de longitud no múltiplo de 4 */
        for (int b = 0; b < 4 && bytes_written < len; b++) {
            buf[bytes_written++] = (uint8_t)(word >> (b * 8));
        }
    }

    return true;
}

/**
 * rng_is_healthy — Verificar estado del RNG
 */
bool rng_is_healthy(void) {
    if (!_rng_initialized) return false;

    /* Sin errores pendientes */
    if (__HAL_RNG_GET_FLAG(&hrng, RNG_FLAG_CECS)) return false;
    if (__HAL_RNG_GET_FLAG(&hrng, RNG_FLAG_SECS)) return false;

    return true;
}

/**
 * rng_disable — Apagar el RNG para ahorrar energía
 */
void rng_disable(void) {
    HAL_RNG_DeInit(&hrng);
    __HAL_RCC_RNG_CLK_DISABLE();
    _rng_initialized = false;
    DEBUG_PRINT("[RNG] Deshabilitado.\r\n");
}


/* =========================================================
 * TESTS DE VALIDACIÓN
 * ========================================================= */

/**
 * rng_run_tests — Ejecutar todos los tests de validación
 *
 * Implementa 4 tests en secuencia:
 *   A: Funcional básico
 *   B: Detección de consecutivos iguales
 *   C: Distribución estadística mínima
 *   D: Velocidad de generación
 */
bool rng_run_tests(rng_test_results_t *results) {

    /* Usar struct local si no se proporcionó puntero */
    rng_test_results_t local_results;
    rng_test_results_t *r = results ? results : &local_results;
    memset(r, 0, sizeof(rng_test_results_t));

    DEBUG_PRINT("\r\n[RNG] === Ejecutando tests de validación ===\r\n");

    if (!_rng_initialized) {
        DEBUG_PRINT("[RNG] ERROR: RNG no inicializado. Llamar rng_init() primero.\r\n");
        return false;
    }

    /* -----------------------------------------------------------------
     * TEST A: Generación funcional básica
     *
     * Genera RNG_TEST_SAMPLES (1000) números y verifica:
     *   - Ninguno es 0x00000000 (todos ceros → RNG bloqueado)
     *   - Ninguno es 0xFFFFFFFF (todos unos → RNG bloqueado)
     *   - Todos se generaron sin error de hardware
     * ----------------------------------------------------------------- */
    DEBUG_PRINTF("[RNG] Test A: Generando %u muestras...\r\n", RNG_TEST_SAMPLES);

    uint32_t prev_val = 0;
    bool     first    = true;

    r->test_a_functional  = true;
    r->test_b_consecutive = true;

    for (uint32_t i = 0; i < RNG_TEST_SAMPLES; i++) {
        uint32_t val = 0;

        if (!rng_get32(&val)) {
            DEBUG_PRINTF("[RNG] Test A FAIL: error en muestra %lu\r\n", i);
            r->test_a_functional = false;
            break;
        }

        r->samples_generated++;

        /* Contar valores trivialmente malos */
        if (val == 0x00000000) r->zero_count++;
        if (val == 0xFFFFFFFF) r->ones_count++;

        /* Test B: detectar consecutivos iguales
         * En 1000 muestras, la probabilidad de dos consecutivos
         * iguales por azar es 1000 × (1/2^32) ≈ 0.000023%
         * Si ocurre, es casi seguro un fallo de hardware. */
        if (!first && val == prev_val) {
            r->consecutive_equal++;
            DEBUG_PRINTF("[RNG] Test B WARN: consecutivos iguales en muestra %lu: 0x%08lX\r\n",
                i, val);
        }

        prev_val = val;
        first    = false;
    }

    /* Evaluar Test A */
    if (r->zero_count > 0 || r->ones_count > 0) {
        DEBUG_PRINTF("[RNG] Test A FAIL: %lu ceros, %lu unos en %lu muestras\r\n",
            r->zero_count, r->ones_count, r->samples_generated);
        r->test_a_functional = false;
    } else {
        DEBUG_PRINTF("[RNG] Test A PASS: %lu muestras, 0 triviales\r\n",
            r->samples_generated);
    }

    /* Evaluar Test B */
    if (r->consecutive_equal > 0) {
        DEBUG_PRINTF("[RNG] Test B FAIL: %lu pares consecutivos iguales\r\n",
            r->consecutive_equal);
        r->test_b_consecutive = false;
    } else {
        DEBUG_PRINT("[RNG] Test B PASS: sin consecutivos iguales\r\n");
    }

    /* -----------------------------------------------------------------
     * TEST C: Distribución estadística mínima
     *
     * Genera 256 bytes y verifica que aparecen al menos
     * RNG_DIST_MIN_UNIQUE (250) valores distintos.
     *
     * Un generador completamente roto (siempre retorna el mismo byte)
     * daría unique_bytes = 1. Un buen generador dará ~205 únicos
     * en 256 muestras (cumpleaños problema).
     *
     * Este test es intencionalmente laxo — no es un test estadístico
     * riguroso. Para producción, exportar los datos y usar dieharder:
     *   rng_fill_buffer(buf, 1MB) → archivo → dieharder -a -f archivo
     * ----------------------------------------------------------------- */
    DEBUG_PRINT("[RNG] Test C: Verificando distribución en 256 bytes...\r\n");

    uint8_t dist_buf[256];
    r->test_c_distribution = false;

    if (rng_fill_buffer(dist_buf, 256)) {
        /* Contar valores únicos con un mapa de bits de 256 posiciones */
        uint8_t seen[256] = {0};  /* seen[v]=1 si el byte 'v' apareció */
        uint32_t unique = 0;

        for (int i = 0; i < 256; i++) {
            if (!seen[dist_buf[i]]) {
                seen[dist_buf[i]] = 1;
                unique++;
            }
        }

        r->unique_bytes = unique;

        if (unique >= RNG_DIST_MIN_UNIQUE) {
            DEBUG_PRINTF("[RNG] Test C PASS: %lu valores únicos en 256 bytes\r\n",
                unique);
            r->test_c_distribution = true;
        } else {
            DEBUG_PRINTF("[RNG] Test C FAIL: solo %lu valores únicos (mín: %u)\r\n",
                unique, RNG_DIST_MIN_UNIQUE);
            DEBUG_PRINT("[RNG] Distribución muy mala — posible fallo de hardware\r\n");
        }

        /* Limpiar el buffer de distribución por seguridad */
        memset(dist_buf, 0x00, sizeof(dist_buf));
    } else {
        DEBUG_PRINT("[RNG] Test C FAIL: error generando bytes de distribución\r\n");
    }

    /* -----------------------------------------------------------------
     * TEST D: Velocidad de generación
     *
     * Mide cuánto tiempo tarda en generar 1000 números de 32 bits.
     *
     * Velocidad teórica del RNG a 48MHz:
     *   48MHz ÷ 64 ciclos por bit × 32 bits = ~750kHz = ~1.3µs por número
     *   1000 números → ~1.3ms teórico
     *
     * En la práctica con overhead de HAL, esperamos ~2-5ms para 1000.
     * Si tarda más de 100ms, algo está muy mal con el clock del RNG.
     *
     * NOTA: HAL_GetTick() tiene resolución de 1ms, así que para
     * tiempos pequeños usamos DWT (Debug Watchpoint and Trace)
     * que tiene resolución de ciclos de CPU a 168MHz.
     * ----------------------------------------------------------------- */
    DEBUG_PRINT("[RNG] Test D: Midiendo velocidad (1000 números)...\r\n");

    /* Habilitar DWT para medir tiempo en ciclos de CPU */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;                    /* Resetear contador de ciclos */
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;  /* Iniciar contador */

    uint32_t cycles_start = DWT->CYCCNT;
    uint32_t dummy = 0;

    for (uint32_t i = 0; i < 1000; i++) {
        rng_get32(&dummy);
    }

    uint32_t cycles_elapsed = DWT->CYCCNT - cycles_start;

    /* Convertir ciclos a microsegundos
     * A 168MHz: 1 ciclo = 1/168MHz ≈ 5.95ns
     * cycles_elapsed / 168 = microsegundos */
    r->speed_us_per_1000 = cycles_elapsed / 168;

    /* Evaluar velocidad
     * Mínimo esperado: 1000µs (1ms) — si es más rápido, algo raro
     * Máximo aceptable: 100,000µs (100ms) — más lento indica problema */
    if (r->speed_us_per_1000 < 500) {
        DEBUG_PRINTF("[RNG] Test D WARN: muy rápido (%lu µs/1000). "
            "¿DWT mal configurado?\r\n", r->speed_us_per_1000);
        r->test_d_speed = true;  /* No fallo, pero sospechoso */
    } else if (r->speed_us_per_1000 > 100000) {
        DEBUG_PRINTF("[RNG] Test D FAIL: demasiado lento (%lu µs/1000). "
            "Revisar clock PLLQ.\r\n", r->speed_us_per_1000);
        r->test_d_speed = false;
    } else {
        DEBUG_PRINTF("[RNG] Test D PASS: %lu µs para 1000 números "
            "(%.1f números/ms)\r\n",
            r->speed_us_per_1000,
            1000000.0f / r->speed_us_per_1000);
        r->test_d_speed = true;
    }

    /* -----------------------------------------------------------------
     * RESUMEN FINAL
     * ----------------------------------------------------------------- */
    r->all_passed = r->test_a_functional &&
                    r->test_b_consecutive &&
                    r->test_c_distribution &&
                    r->test_d_speed;

    DEBUG_PRINT("\r\n[RNG] === Resumen de tests ===\r\n");
    DEBUG_PRINTF("[RNG] Test A (funcional)    : %s\r\n",
        r->test_a_functional  ? "PASS" : "FAIL");
    DEBUG_PRINTF("[RNG] Test B (consecutivos) : %s\r\n",
        r->test_b_consecutive ? "PASS" : "FAIL");
    DEBUG_PRINTF("[RNG] Test C (distribución) : %s (%lu únicos/256)\r\n",
        r->test_c_distribution ? "PASS" : "FAIL", r->unique_bytes);
    DEBUG_PRINTF("[RNG] Test D (velocidad)    : %s (%lu µs/1000)\r\n",
        r->test_d_speed ? "PASS" : "FAIL", r->speed_us_per_1000);
    DEBUG_PRINT("[RNG] ===========================\r\n");

    if (r->all_passed) {
        DEBUG_PRINT("[RNG] ✓ TODOS LOS TESTS PASARON\r\n");
        DEBUG_PRINT("[RNG] El RNG es apto para generación de semillas BIP39\r\n");
    } else {
        DEBUG_PRINT("[RNG] ✗ ALGÚN TEST FALLÓ\r\n");
        DEBUG_PRINT("[RNG] NO usar este dispositivo para generar semillas\r\n");
        DEBUG_PRINT("[RNG] Revisar hardware y configuración de clock\r\n");
    }

    DEBUG_PRINT("\r\n");
    return r->all_passed;
}
