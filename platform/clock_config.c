/**
 * @file    clock_config.c
 * @brief   Configuración del clock del sistema — WALCORG Fase 1
 *
 * Implementación de SystemClock_Config() como módulo reutilizable.
 * Separado de blink_test.c para que todos los demás archivos
 * del firmware puedan incluirlo sin duplicar código.
 *
 * Diagrama del árbol de clock configurado:
 *
 *   HSE (8MHz cristal WeAct)
 *       │
 *       ▼
 *   PLLM ÷8 → 1MHz
 *       │
 *       ▼
 *   PLLN ×336 → 336MHz (VCO)
 *       │
 *       ├── PLLP ÷2 → 168MHz → SYSCLK → HCLK (168MHz)
 *       │                                   │
 *       │                           ┌───────┴────────┐
 *       │                       APB1 ÷4          APB2 ÷2
 *       │                       42MHz             84MHz
 *       │                    (I2C, SPI2/3)    (SPI1, UART1)
 *       │
 *       └── PLLQ ÷7 → 48MHz → USB / SDIO / RNG
 *
 * @license GNU GPL v3
 */

#include "clock_config.h"
#include "uart_debug.h"

/* =========================================================
 * IMPLEMENTACIÓN PÚBLICA
 * ========================================================= */

/**
 * SystemClock_Config — Configurar PLL para 168MHz desde HSE 8MHz
 *
 * Este es el archivo que modifica legacy/firmware/stm32.c de Trezor.
 * El original de Trezor One usa STM32F205 con HSE de 12MHz y
 * diferentes valores de PLL. Aquí adaptamos para F405 con HSE 8MHz.
 *
 * Cambios respecto al stm32.c original de Trezor:
 *   - PLLM: 6 → 8       (ajuste para HSE 8MHz en vez de 12MHz)
 *   - PLLN: 168 → 336   (ajuste para mantener VCO en rango válido)
 *   - Flash latency: 3 → 5 (F405 a 168MHz requiere 5 wait states)
 *   - Voltage scaling: añadido Scale1 (F405 lo requiere a 168MHz)
 */
void SystemClock_Config(void) {

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* -----------------------------------------------------------------
     * PASO 1: Regulador de voltaje interno
     *
     * El STM32F405 tiene un regulador de voltaje interno que alimenta
     * el núcleo del CPU. Para operar a 168MHz necesita el máximo
     * voltaje de núcleo (Scale 1 = ~1.2V).
     *
     * Scale 1 → hasta 168MHz (lo que queremos)
     * Scale 2 → hasta 144MHz (ahorro de energía moderado)
     * Scale 3 → hasta  60MHz (máximo ahorro, solo en STM32F42x/43x)
     *
     * Sin esto: el chip opera a voltaje reducido y puede ser inestable
     * a 168MHz, causando resets aleatorios o corrupción de datos.
     * ----------------------------------------------------------------- */
    __HAL_RCC_PWR_CLK_ENABLE();     /* Habilitar clock del módulo de potencia */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* Esperar a que el regulador alcance el voltaje configurado */
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) { __NOP(); }

    /* -----------------------------------------------------------------
     * PASO 2: Configurar osciladores
     *
     * Habilitamos el HSE (cristal externo 8MHz de la WeAct board)
     * y el PLL que multiplicará esa frecuencia hasta 168MHz.
     *
     * ¿Por qué HSE en vez de HSI (interno)?
     *   HSI = 16MHz interno, menos preciso (±1% de tolerancia)
     *   HSE = 8MHz cristal externo, muy preciso (±30ppm)
     *   USB Full Speed REQUIERE exactamente 48MHz → necesita HSE preciso
     *   El firmware de Trezor también usa HSE por esta razón.
     *
     * Cálculo del PLL:
     *   Entrada al PLL = HSE / PLLM = 8MHz / 8 = 1MHz
     *   VCO             = 1MHz × PLLN = 1MHz × 336 = 336MHz
     *   SYSCLK          = VCO / PLLP  = 336MHz / 2 = 168MHz  ✓
     *   USB/RNG         = VCO / PLLQ  = 336MHz / 7 =  48MHz  ✓
     *
     * Restricciones del hardware que hay que cumplir:
     *   - Entrada al PLL (HSE/PLLM): debe estar entre 1MHz y 2MHz
     *   - VCO (PLLN): debe estar entre 100MHz y 432MHz
     *   - PLLP: solo puede ser 2, 4, 6 u 8
     *   - USB clock DEBE ser exactamente 48MHz (no aproximado)
     * ----------------------------------------------------------------- */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;      /* Encender cristal HSE */
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;      /* Activar el PLL */
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE; /* PLL usa HSE como entrada */
    RCC_OscInitStruct.PLL.PLLM       = 8;    /* Divisor: 8MHz ÷ 8 = 1MHz al PLL */
    RCC_OscInitStruct.PLL.PLLN       = 336;  /* Multiplicador: 1MHz × 336 = 336MHz VCO */
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;  /* 336MHz ÷ 2 = 168MHz SYSCLK */
    RCC_OscInitStruct.PLL.PLLQ       = 7;    /* 336MHz ÷ 7 = 48MHz para USB/RNG */

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        /* Si llegamos aquí, el HSE no respondió en el tiempo esperado.
         * Causas posibles:
         *   1. El cristal de 8MHz no está soldado en la WeAct board
         *   2. Los capacitores de carga del cristal están mal
         *   3. El cristal está dañado o es de frecuencia incorrecta
         *   4. Problema de alimentación en la sección del cristal
         *
         * Debug: medir con osciloscopio los pines OSC_IN (PH0) y
         * OSC_OUT (PH1) de la WeAct board. Deben mostrar una señal
         * senoidal de 8MHz cuando HSE está activo. */
        DEBUG_PRINT("ERROR: HSE no responde. Verificar cristal 8MHz.\r\n");
        Error_Handler();
    }

    /* -----------------------------------------------------------------
     * PASO 3: Configurar latencia de Flash ANTES de cambiar el clock
     *
     * La memoria Flash del STM32F405 tiene una velocidad máxima de
     * acceso. A frecuencias altas, el CPU es más rápido que la Flash,
     * así que hay que insertar "wait states" (ciclos de espera).
     *
     * Tabla de wait states para Vcc = 2.7V - 3.6V (nuestra situación):
     *   0 WS → hasta  30MHz
     *   1 WS → hasta  60MHz
     *   2 WS → hasta  90MHz
     *   3 WS → hasta 120MHz
     *   4 WS → hasta 150MHz
     *   5 WS → hasta 168MHz ← lo que necesitamos
     *
     * CRÍTICO: si configuramos el clock a 168MHz ANTES de aumentar
     * los wait states, el CPU leerá instrucciones corruptas de Flash
     * y el comportamiento es impredecible. Siempre configurar
     * latencia PRIMERO, clock DESPUÉS.
     * ----------------------------------------------------------------- */
    __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_5);   /* 5 wait states para 168MHz */

    /* Verificar que el cambio de latencia se aplicó */
    if (__HAL_FLASH_GET_LATENCY() != FLASH_LATENCY_5) {
        DEBUG_PRINT("ERROR: No se pudo configurar latencia de Flash.\r\n");
        Error_Handler();
    }

    /* Habilitar prefetch de Flash (lee instrucciones por adelantado
     * para reducir el impacto de los wait states en el rendimiento) */
    __HAL_FLASH_PREFETCH_BUFFER_ENABLE();

    /* Habilitar caché de instrucciones y datos de Flash */
    __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
    __HAL_FLASH_DATA_CACHE_ENABLE();

    /* -----------------------------------------------------------------
     * PASO 4: Configurar los divisores de los buses
     *
     * AHB (HCLK): bus principal — CPU, DMA, memoria
     *   HCLK = SYSCLK / AHBCLKDivider = 168MHz / 1 = 168MHz
     *
     * APB1 (PCLK1): bus de periféricos lentos
     *   PCLK1 = HCLK / APB1CLKDivider = 168MHz / 4 = 42MHz
     *   Máximo permitido por hardware: 42MHz (límite fijo del F405)
     *   Periféricos: I2C1 (click-wheel), SPI2/3, UART2-5
     *
     * APB2 (PCLK2): bus de periféricos rápidos
     *   PCLK2 = HCLK / APB2CLKDivider = 168MHz / 2 = 84MHz
     *   Máximo permitido por hardware: 84MHz (límite fijo del F405)
     *   Periféricos: SPI1 (display TFT), UART1 (debug)
     *
     * NOTA: Los timers conectados a APB1 y APB2 tienen el doble de
     * frecuencia que su bus si el divisor del bus > 1:
     *   TIM en APB1: 42MHz × 2 = 84MHz
     *   TIM en APB2: 84MHz × 2 = 168MHz
     * ----------------------------------------------------------------- */
    RCC_ClkInitStruct.ClockType = (
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_HCLK   |
        RCC_CLOCKTYPE_PCLK1  |
        RCC_CLOCKTYPE_PCLK2
    );
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK; /* Usar salida del PLL */
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;   /* AHB  = 168MHz ÷ 1 = 168MHz */
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;     /* APB1 = 168MHz ÷ 4 =  42MHz */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;     /* APB2 = 168MHz ÷ 2 =  84MHz */

    /* Aplicar configuración de buses.
     * El segundo parámetro es la latencia de Flash que ya configuramos —
     * HAL la verifica internamente para asegurar coherencia. */
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        DEBUG_PRINT("ERROR: Configuracion de buses de clock fallida.\r\n");
        Error_Handler();
    }

    /* -----------------------------------------------------------------
     * PASO 5: Reconfigurar SysTick para la nueva frecuencia
     *
     * HAL_Init() configuró el SysTick para 1ms usando HSI (16MHz).
     * Ahora que cambiamos a 168MHz, hay que reconfigurarlo.
     * HAL_RCC_ClockConfig() lo hace automáticamente internamente,
     * pero lo llamamos explícitamente por claridad.
     * ----------------------------------------------------------------- */
    HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);
    /* HAL_RCC_GetHCLKFreq() retorna 168,000,000
     * ÷ 1000 = 168,000 ticks por ms → SysTick cada 1ms exacto */

    /* Configurar la fuente del SysTick como HCLK (no dividido) */
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

    /* Prioridad del SysTick: la más baja posible (15)
     * para no interrumpir operaciones críticas de crypto */
    HAL_NVIC_SetPriority(SysTick_IRQn, 15, 0);
}


/**
 * clock_verify — Verificar que SYSCLK está en 168MHz
 *
 * Compara la frecuencia real reportada por el HAL con la esperada.
 * Si hay discrepancia, el PLL no configuró correctamente.
 */
uint8_t clock_verify(void) {
    uint32_t sysclk = HAL_RCC_GetSysClockFreq();

    /* Tolerancia de ±1% para variaciones del cristal */
    uint32_t min = SYSCLK_FREQ_HZ - (SYSCLK_FREQ_HZ / 100);
    uint32_t max = SYSCLK_FREQ_HZ + (SYSCLK_FREQ_HZ / 100);

    return (sysclk >= min && sysclk <= max) ? 1 : 0;
}


/**
 * clock_get_sysclk_hz — Retorna la frecuencia actual de SYSCLK
 */
uint32_t clock_get_sysclk_hz(void) {
    return HAL_RCC_GetSysClockFreq();
}


/**
 * clock_print_status — Imprimir resumen del clock por UART
 *
 * Útil al inicio del firmware para confirmar visualmente que
 * el clock está configurado correctamente.
 * Solo compila cuando PRODUCTION=0.
 */
void clock_print_status(void) {
#if (PRODUCTION == 0)
    DEBUG_PRINTF("\r\n=== Clock Status ===\r\n");
    DEBUG_PRINTF("SYSCLK : %lu MHz\r\n", HAL_RCC_GetSysClockFreq() / 1000000);
    DEBUG_PRINTF("HCLK   : %lu MHz\r\n", HAL_RCC_GetHCLKFreq()     / 1000000);
    DEBUG_PRINTF("PCLK1  : %lu MHz\r\n", HAL_RCC_GetPCLK1Freq()    / 1000000);
    DEBUG_PRINTF("PCLK2  : %lu MHz\r\n", HAL_RCC_GetPCLK2Freq()    / 1000000);

    /* Verificar fuente del SYSCLK */
    uint32_t src = __HAL_RCC_GET_SYSCLK_SOURCE();
    if (src == RCC_CFGR_SWS_PLL) {
        DEBUG_PRINT("Fuente : PLL (correcto)\r\n");
    } else if (src == RCC_CFGR_SWS_HSE) {
        DEBUG_PRINT("Fuente : HSE (PLL no activo!)\r\n");
    } else {
        DEBUG_PRINT("Fuente : HSI (PLL no activo!)\r\n");
    }

    /* Verificar que USB puede funcionar */
    /* PLLQ = 336/7 = 48MHz exactos */
    DEBUG_PRINTF("USB    : 48 MHz (PLLQ=%lu)\r\n",
        (RCC->PLLCFGR & RCC_PLLCFGR_PLLQ) >> RCC_PLLCFGR_PLLQ_Pos);

    DEBUG_PRINTF("Latencia Flash: %lu WS\r\n",
        __HAL_FLASH_GET_LATENCY() >> FLASH_ACR_LATENCY_Pos);

    if (clock_verify()) {
        DEBUG_PRINT("Estado : OK - 168MHz confirmado\r\n");
    } else {
        DEBUG_PRINT("Estado : ERROR - frecuencia incorrecta!\r\n");
    }
    DEBUG_PRINT("====================\r\n\r\n");
#endif
}
