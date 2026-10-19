/**
 * @file    blink_test.c
 * @brief   Test de bringup Fase 1 — WalCorg Cold Wallet
 *
 * PROPÓSITO:
 *   Este es el primer programa que debes flashear al WeAct F405.
 *   Si el LED parpadea, significa que:
 *     ✓ arm-none-eabi-gcc compiló correctamente
 *     ✓ El linker script está bien configurado
 *     ✓ OpenOCD + ST-Link flasheó el chip sin errores
 *     ✓ El chip arrancó y ejecutó el Reset_Handler
 *     ✓ SystemClock_Config() configuró el clock a 168MHz
 *     ✓ HAL_Init() funciona
 *
 *   Si el LED NO parpadea, revisar en este orden:
 *     1. Conexión SWD (DIO/CLK/GND)
 *     2. Alimentación (3V3 o USB-C)
 *     3. Salida de OpenOCD (¿dice "verified OK"?)
 *     4. Si compila pero no parpadea: revisar pin del LED en la placa
 *
 * HARDWARE:
 *   WeAct STM32F405RGT6 Mini Core Board
 *   LED integrado en la placa: PC13 (LED azul, activo en LOW)
 *   El LED se enciende cuando PC13 = LOW (0V)
 *   El LED se apaga  cuando PC13 = HIGH (3.3V)
 *
 * COMPILAR Y FLASHEAR:
 *   make blink          <- compila solo el blink test
 *   make flash-blink    <- compila y flashea
 *
 * @license GNU GPL v3
 */

#include "stm32f4xx_hal.h"

/* -----------------------------------------------------------------------------
 * Definición del LED integrado en la WeAct F405
 *
 * Mirando la imagen de la placa, el pin C13 está etiquetado claramente
 * en la esquina superior. El LED en la WeAct es activo en LOW:
 *   GPIO_PIN_RESET (0V)   → LED ENCENDIDO
 *   GPIO_PIN_SET   (3.3V) → LED APAGADO
 * --------------------------------------------------------------------------- */
#define LED_PORT    GPIOC
#define LED_PIN     GPIO_PIN_13
#define LED_ON()    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET)
#define LED_OFF()   HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET)
#define LED_TOGGLE() HAL_GPIO_TogglePin(LED_PORT, LED_PIN)


/* -----------------------------------------------------------------------------
 * Prototipos de funciones privadas
 * (se definen abajo, después de main)
 * --------------------------------------------------------------------------- */
static void SystemClock_Config(void);
static void GPIO_LED_Init(void);
static void Error_Handler(void);


/* =============================================================================
 * MAIN — Punto de entrada del firmware
 *
 * Secuencia de arranque estándar STM32 HAL:
 *   1. HAL_Init()           → inicializa SysTick (base de tiempo en ms)
 *   2. SystemClock_Config() → configura PLL para 168MHz con HSE 8MHz
 *   3. GPIO_LED_Init()      → habilita GPIOC y configura PC13
 *   4. Loop infinito        → parpadeo del LED cada 500ms
 * ============================================================================= */
int main(void) {

    /* -------------------------------------------------------------------------
     * 1. Inicializar la capa HAL de ST
     *
     * HAL_Init() hace tres cosas importantes:
     *   a) Configura el SysTick para generar una interrupción cada 1ms
     *      (esta es la base de tiempo para HAL_Delay())
     *   b) Configura la prioridad de grupos de interrupciones (NVIC)
     *   c) Llama a HAL_MspInit() para inicialización de bajo nivel
     *
     * DEBE ser la primera llamada en main() — todo lo demás depende del SysTick.
     * ----------------------------------------------------------------------- */
    if (HAL_Init() != HAL_OK) {
        Error_Handler();
    }

    /* -------------------------------------------------------------------------
     * 2. Configurar el reloj del sistema a 168MHz
     *
     * El STM32F405 arranca con el oscilador interno (HSI) a 16MHz.
     * Necesitamos configurar el PLL para alcanzar 168MHz usando
     * el cristal externo de 8MHz (HSE) que tiene la WeAct board.
     *
     * 168MHz es el máximo del STM32F405 y el mismo que usa Trezor One.
     * Esto es importante porque los timings de USB y SPI están calculados
     * para esta frecuencia.
     * ----------------------------------------------------------------------- */
    SystemClock_Config();

    /* -------------------------------------------------------------------------
     * 3. Inicializar el GPIO del LED
     * ----------------------------------------------------------------------- */
    GPIO_LED_Init();

    /* -------------------------------------------------------------------------
     * 4. Verificar que el clock está correcto
     *
     * Si SystemClock_Config() funcionó, HAL_GetTick() debe avanzar
     * a razón de 1 tick por milisegundo.
     *
     * Prueba: si el LED parpadea exactamente cada 500ms (medio segundo),
     * el clock está correcto. Si parpadea más lento (ej: cada 3 segundos),
     * el PLL no configuró bien y sigue en HSI de 16MHz.
     * ----------------------------------------------------------------------- */

    /* Secuencia de inicio: 3 destellos rápidos = "arranqué correctamente" */
    for (int i = 0; i < 3; i++) {
        LED_ON();
        HAL_Delay(100);   /* 100ms encendido */
        LED_OFF();
        HAL_Delay(100);   /* 100ms apagado */
    }

    /* Pausa de 500ms antes del loop principal */
    HAL_Delay(500);

    /* -------------------------------------------------------------------------
     * 5. Loop principal — parpadeo continuo cada 500ms
     *
     * HAL_Delay(500) bloquea 500ms usando el SysTick.
     * LED_TOGGLE() invierte el estado del pin PC13.
     *
     * Resultado esperado: LED parpadea a 1Hz (0.5s ON / 0.5s OFF)
     * ----------------------------------------------------------------------- */
    while (1) {
        LED_TOGGLE();
        HAL_Delay(500);
    }

    /* El while(1) nunca termina — esta línea nunca se ejecuta.
     * La incluimos para satisfacer al compilador (return en main). */
    return 0;
}


/* =============================================================================
 * SystemClock_Config — Configura PLL para 168MHz
 *
 * STM32F405RGT6 con HSE = 8MHz (cristal externo de la WeAct board)
 * Target: SYSCLK = 168MHz, AHB = 168MHz, APB1 = 42MHz, APB2 = 84MHz
 *
 * Fórmula del PLL:
 *   PLLCLK = (HSE / PLLM) × PLLN / PLLP
 *   PLLCLK = (8MHz / 8) × 336 / 2 = 168MHz  ✓
 *
 * APB1 (PCLK1) = 168MHz / 4 = 42MHz  → límite máximo del APB1
 * APB2 (PCLK2) = 168MHz / 2 = 84MHz  → límite máximo del APB2
 *
 * El I2C1 usa PCLK1 (42MHz) — importante para calcular el divisor de I2C.
 * El SPI1 usa PCLK2 (84MHz) — importante para calcular velocidad del TFT.
 * ============================================================================= */
static void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* ---- Configurar el regulador de voltaje interno ----
     * Scale 1 = máximo rendimiento (requerido para 168MHz)
     * Scale 2 = modo de ahorro (máximo ~144MHz)
     * Scale 3 = ultra-ahorro (máximo ~120MHz) */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* ---- Configurar osciladores ----
     *
     * HSE = High Speed External = cristal de 8MHz en la WeAct board
     * PLL = Phase-Locked Loop = multiplicador de frecuencia
     *
     * OscillatorType: habilitamos HSE (externo) y deshabilitamos HSI (interno)
     * HSEState: ON = encender el cristal externo
     * PLL.PLLState: ON = activar el PLL
     * PLL.PLLSource: HSE = el PLL toma su entrada del cristal externo
     *
     * PLLM: pre-divisor (8MHz / 8 = 1MHz de entrada al PLL)
     *   → Siempre ajustar para que la entrada al PLL sea 1-2MHz
     * PLLN: multiplicador (1MHz × 336 = 336MHz en el VCO)
     *   → El VCO debe operar entre 192MHz y 432MHz
     * PLLP: post-divisor para SYSCLK (336MHz / 2 = 168MHz)
     *   → Valores válidos: 2, 4, 6, 8
     * PLLQ: post-divisor para USB/SDIO/RNG (336MHz / 7 = 48MHz exactos)
     *   → USB REQUIERE exactamente 48MHz — este valor es crítico */
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState            = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM            = 8;      /* 8MHz / 8  = 1MHz   */
    RCC_OscInitStruct.PLL.PLLN            = 336;    /* 1MHz × 336 = 336MHz */
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV2;  /* 336/2 = 168MHz */
    RCC_OscInitStruct.PLL.PLLQ            = 7;      /* 336/7 = 48MHz para USB */

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        /* Si el cristal externo no responde (no está soldado, está dañado,
         * o la frecuencia es incorrecta), HAL_RCC_OscConfig falla aquí.
         * En ese caso revisar el cristal de 8MHz en la WeAct board. */
        Error_Handler();
    }

    /* ---- Configurar los buses de clock ----
     *
     * ClockType: qué buses estamos configurando
     * SYSCLKSource: PLLCLK = el sistema usa la salida del PLL (168MHz)
     * AHBCLKDivider: DIV1 = AHB = 168MHz (no dividir)
     * APB1CLKDivider: DIV4 = APB1 = 42MHz (máximo permitido por el hardware)
     * APB2CLKDivider: DIV2 = APB2 = 84MHz (máximo permitido por el hardware)
     *
     * FLatency: ciclos de espera de la Flash
     * A 168MHz necesitamos 5 wait states (ver tabla del datasheet STM32F405)
     * Sin los wait states correctos, el CPU leerá datos corruptos de Flash. */
    RCC_ClkInitStruct.ClockType = (
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_HCLK   |
        RCC_CLOCKTYPE_PCLK1  |
        RCC_CLOCKTYPE_PCLK2
    );
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;   /* 168MHz */
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;     /*  42MHz */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;     /*  84MHz */

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}


/* =============================================================================
 * GPIO_LED_Init — Inicializar PC13 como salida digital para el LED
 * ============================================================================= */
static void GPIO_LED_Init(void) {

    /* ---- Habilitar el clock del periférico GPIOC ----
     *
     * En STM32, TODOS los periféricos empiezan con el clock APAGADO
     * para ahorrar energía. Antes de usar cualquier GPIO, SPI, I2C,
     * USB, etc., DEBES habilitar su clock. Si olvidas esto, el
     * periférico simplemente no funciona (sin error, sin excepción). */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* ---- Configurar PC13 ----
     *
     * Mode: OUTPUT_PP = salida push-pull
     *   Push-pull puede manejar corriente hacia afuera (source) y
     *   hacia adentro (sink), a diferencia de open-drain que solo
     *   puede hacer sink. Para un LED es la configuración correcta.
     *
     * Pull: NOPULL = sin resistencia pull-up/down interna
     *   El LED ya tiene su resistencia limitadora en la placa.
     *
     * Speed: LOW = frecuencia de slew rate baja
     *   Para un LED que parpadea a 1Hz, LOW es más que suficiente.
     *   Usar HIGH solo para señales de alta frecuencia (SPI, I2C). */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = LED_PIN;               /* PC13 */
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;   /* Push-pull */
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);

    /* Estado inicial: LED apagado */
    LED_OFF();
}


/* =============================================================================
 * Error_Handler — Manejador de errores fatales
 *
 * Si algo falla en la inicialización (clock, HAL, periférico),
 * llegamos aquí. En desarrollo: el LED parpadea muy rápido para
 * indicar error. En producción: reset del sistema.
 * ============================================================================= */
static void Error_Handler(void) {

    /* Deshabilitar interrupciones — estamos en estado de error */
    __disable_irq();

    /* Parpadeo rápido (100ms) = señal de error
     * Si ves este patrón en vez del parpadeo normal de 500ms,
     * hay un problema en la inicialización del clock o HAL. */
    while (1) {
        /* Manipulación directa de registro — sin HAL,
         * porque HAL podría estar comprometido */
        GPIOC->ODR ^= GPIO_PIN_13;   /* Toggle directo del registro */
        for (volatile int i = 0; i < 500000; i++) { __NOP(); }
    }
}


/* =============================================================================
 * SysTick_Handler — Interrupción del SysTick (cada 1ms)
 *
 * Esta función es llamada automáticamente cada 1ms por el hardware.
 * HAL_IncTick() incrementa el contador interno de HAL que usa HAL_Delay().
 *
 * IMPORTANTE: esta función DEBE estar definida en el proyecto.
 * Si usas el archivo de startup de ST (startup_stm32f405xx.s),
 * este handler ya está declarado como weak — aquí lo sobreescribimos.
 * ============================================================================= */
void SysTick_Handler(void) {
    HAL_IncTick();   /* Incrementa el tick de 1ms usado por HAL_Delay() */
}
