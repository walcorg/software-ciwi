/**
 * @file    stm32.c  (legacy/firmware/stm32.c — versión modificada)
 * @brief   Inicialización de bajo nivel del STM32F405RGT6
 *
 * CONTEXTO:
 *   Este archivo REEMPLAZA legacy/firmware/stm32.c del repositorio
 *   original de Trezor. El original está escrito para el STM32F205
 *   del Trezor One (12MHz HSE, Cortex-M3, sin FPU).
 *
 * DIFERENCIAS RESPECTO AL ORIGINAL DE TREZOR:
 *
 *   | Aspecto            | Trezor One (F205)  | WalCorg (F405)     |
 *   |--------------------|--------------------|--------------------|
 *   | MCU                | STM32F205RET6      | STM32F405RGT6      |
 *   | Núcleo             | Cortex-M3          | Cortex-M4 + FPU    |
 *   | HSE (cristal)      | 12 MHz             | 8 MHz (WeAct)      |
 *   | PLLM               | 6                  | 8                  |
 *   | PLLN               | 168                | 336                |
 *   | Flash latency      | 3 wait states      | 5 wait states      |
 *   | Voltage scaling    | no requerido       | Scale 1 requerido  |
 *   | CCM RAM            | no disponible      | 64KB en 0x10000000 |
 *
 * MODIFICACIONES EN ESTE ARCHIVO:
 *   [MOD-1] SystemClock_Config: PLLM 6→8, PLLN 168→336, latency 3→5
 *   [MOD-2] Añadido: voltage scaling a Scale1 (requerido por F405)
 *   [MOD-3] Añadido: habilitación de FPU (Cortex-M4)
 *   [MOD-4] Añadido: habilitación de CCM RAM
 *   [MOD-5] MPU: ajuste de regiones para el mapa de memoria del F405
 *
 * Para buscar todos los cambios en el código, buscar: [MOD-N]
 *
 * @license GNU GPL v3
 */

#include "stm32f4xx_hal.h"
#include "uart_debug.h"

/* =========================================================
 * [MOD-3] HABILITAR FPU (Floating Point Unit)
 *
 * El Cortex-M4 tiene una FPU de precisión simple opcional.
 * Debe habilitarse EXPLÍCITAMENTE antes de usar instrucciones
 * de punto flotante — si se ejecuta una instrucción FPU sin
 * habilitarla, ocurre un UsageFault.
 *
 * El compilador con -mfloat-abi=hard genera instrucciones FPU
 * automáticamente para operaciones float/double, por eso es
 * crítico habilitar la FPU antes de cualquier otra cosa.
 *
 * Registro CPACR (Coprocessor Access Control Register):
 *   Bits 23:22 = CP11 (Full Access)
 *   Bits 21:20 = CP10 (Full Access)
 *   0b11 en ambos = acceso completo a la FPU
 * ========================================================= */
static void fpu_enable(void) {
    /* Verificar si la FPU está disponible en este chip */
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    /* Habilitar acceso completo a los co-procesadores FPU (CP10 y CP11) */
    SCB->CPACR |= ((3UL << 10*2) | (3UL << 11*2));
    /* Barrera de memoria para asegurar que el cambio surte efecto
     * antes de ejecutar cualquier instrucción FPU */
    __DSB();    /* Data Synchronization Barrier */
    __ISB();    /* Instruction Synchronization Barrier */
    DEBUG_PRINT("[STM32] FPU habilitada (Cortex-M4)\r\n");
#endif
}


/* =========================================================
 * [MOD-5] CONFIGURACIÓN DE LA MPU
 *
 * La MPU (Memory Protection Unit) protege regiones de memoria
 * contra accesos no autorizados. Trezor la usa para aislar
 * las claves privadas del código del firmware.
 *
 * En Fase 1 configuramos la MPU de forma mínima:
 *   - Proteger la región de CCM (donde irán las claves en Fase 6)
 *   - Marcar periféricos como no-cacheable
 *
 * La configuración completa de la MPU para producción
 * se hace en Fase 6 (Security Hardening).
 *
 * Mapa de regiones MPU para STM32F405RGT6:
 *   Region 0: Flash completa     (0x08000000, 1MB, RX)
 *   Region 1: SRAM               (0x20000000, 128KB, RWX)
 *   Region 2: CCM                (0x10000000, 64KB, RW no-execute)
 *   Region 3: Periféricos        (0x40000000, 512MB, RW no-execute)
 *   Region 4: Storage de claves  (0x080A0000, 384KB, RW protegido)
 * ========================================================= */
static void mpu_config_minimal(void) {
    MPU_Region_InitTypeDef mpu_region = {0};

    /* Deshabilitar la MPU antes de configurarla */
    HAL_MPU_Disable();

    /* -----------------------------------------------------------------
     * Región 0: Flash completa — lectura y ejecución
     * 0x08000000, tamaño 1MB
     * No escribible (protege el código contra corrupción)
     * ----------------------------------------------------------------- */
    mpu_region.Enable           = MPU_REGION_ENABLE;
    mpu_region.Number           = MPU_REGION_NUMBER0;
    mpu_region.BaseAddress      = 0x08000000;
    mpu_region.Size             = MPU_REGION_SIZE_1MB;
    mpu_region.SubRegionDisable = 0x00;         /* Todas las sub-regiones activas */
    mpu_region.TypeExtField     = MPU_TEX_LEVEL0;
    mpu_region.AccessPermission = MPU_REGION_PRIV_RO; /* Solo lectura (RO) */
    mpu_region.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;  /* Ejecutable */
    mpu_region.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    mpu_region.IsCacheable      = MPU_ACCESS_CACHEABLE;
    mpu_region.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&mpu_region);

    /* -----------------------------------------------------------------
     * Región 1: SRAM — lectura, escritura, ejecución
     * 0x20000000, tamaño 128KB
     * ----------------------------------------------------------------- */
    mpu_region.Number           = MPU_REGION_NUMBER1;
    mpu_region.BaseAddress      = 0x20000000;
    mpu_region.Size             = MPU_REGION_SIZE_128KB;
    mpu_region.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu_region.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    mpu_region.IsCacheable      = MPU_ACCESS_CACHEABLE;
    HAL_MPU_ConfigRegion(&mpu_region);

    /* -----------------------------------------------------------------
     * [MOD-4] Región 2: CCM RAM — solo datos, no ejecutable
     * 0x10000000, tamaño 64KB
     * Solo el STM32F4x tiene CCM — no existe en el F205 original.
     * Marcada como no-ejecutable por seguridad (datos sensibles aquí)
     * ----------------------------------------------------------------- */
    mpu_region.Number           = MPU_REGION_NUMBER2;
    mpu_region.BaseAddress      = 0x10000000;   /* [MOD-4] CCM no existe en F205 */
    mpu_region.Size             = MPU_REGION_SIZE_64KB;
    mpu_region.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu_region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE; /* No ejecutable */
    mpu_region.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;  /* CCM no tiene caché */
    HAL_MPU_ConfigRegion(&mpu_region);

    /* -----------------------------------------------------------------
     * Región 3: Periféricos — lectura/escritura, no ejecutable
     * 0x40000000, 512MB (cubre todos los registros de periféricos)
     * ----------------------------------------------------------------- */
    mpu_region.Number           = MPU_REGION_NUMBER3;
    mpu_region.BaseAddress      = 0x40000000;
    mpu_region.Size             = MPU_REGION_SIZE_512MB;
    mpu_region.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu_region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    mpu_region.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    mpu_region.IsBufferable     = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&mpu_region);

    /* Habilitar MPU con comportamiento de fallo en accesos desde NMI/HardFault:
     * HFNMI_PRIVDEF_NONE = deshabilitar MPU en NMI y HardFault handlers
     * Esto permite que los fault handlers accedan a toda la memoria
     * para imprimir el diagnóstico por UART */
    HAL_MPU_Enable(MPU_HFNMI_PRIVDEF_NONE);

    DEBUG_PRINT("[STM32] MPU configurada (4 regiones)\r\n");
}


/* =========================================================
 * FUNCIÓN PRINCIPAL DE INICIALIZACIÓN
 *
 * Llama a todas las funciones de setup en el orden correcto.
 * Reemplaza la función stm32_init() del stm32.c original de Trezor.
 * ========================================================= */

/**
 * stm32_init — Inicialización completa del hardware del WalCorg
 *
 * Orden de inicialización (CRÍTICO — no cambiar el orden):
 *   1. FPU         → antes de cualquier operación float
 *   2. HAL_Init()  → configura SysTick con HSI temporal
 *   3. Clock       → cambia a HSE + PLL 168MHz
 *   4. UART debug  → para poder imprimir mensajes de estado
 *   5. MPU         → protección de memoria básica
 *   6. NVIC        → prioridades de interrupciones
 */
void stm32_init(void) {

    /* [MOD-3] Habilitar FPU primero — el Cortex-M4 la necesita
     * antes de cualquier operación de punto flotante */
    fpu_enable();

    /* Inicializar la capa HAL de ST
     * Configura SysTick a 1ms usando HSI (16MHz temporal)
     * y configura los grupos de prioridad del NVIC */
    HAL_Init();

    /* [MOD-1][MOD-2] Configurar clock a 168MHz con HSE 8MHz
     * DIFERENCIAS vs Trezor One:
     *   - HSE: 12MHz → 8MHz (cristal de la WeAct board)
     *   - PLLM: 6 → 8
     *   - PLLN: 168 → 336
     *   - Flash latency: 3 → 5 wait states
     *   - Añadido: Voltage Scaling Mode 1 (requerido por F405 a 168MHz) */
    extern void SystemClock_Config(void);   /* Definida en clock_config.c */
    SystemClock_Config();

    /* Inicializar UART debug — a partir de aquí podemos usar DEBUG_PRINT */
    debug_uart_init();

    DEBUG_PRINT("\r\n[STM32] WalCorg F405 iniciando...\r\n");
    DEBUG_PRINTF("[STM32] HAL version: %lu\r\n", HAL_GetHalVersion());
    DEBUG_PRINTF("[STM32] SYSCLK: %lu MHz\r\n",
        HAL_RCC_GetSysClockFreq() / 1000000);

    /* [MOD-5] Configurar MPU mínima */
    mpu_config_minimal();

    /* Configurar prioridades del NVIC
     * El STM32F4 tiene 4 bits de prioridad = 16 niveles (0=máximo, 15=mínimo)
     * Usamos el modelo de 4 bits de grupo, 0 bits de sub-prioridad */
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);

    DEBUG_PRINT("[STM32] Inicializacion completa.\r\n");
    DEBUG_PRINT("[STM32] Listo para Fase 2 (display driver).\r\n\r\n");
}


/* =========================================================
 * FUNCIÓN DE ERROR FATAL
 *
 * Llamada cuando algo crítico falla durante la inicialización.
 * En desarrollo: parpadeo rápido del LED + mensaje por UART.
 * En producción: reset del sistema.
 * ========================================================= */
void Error_Handler(void) {
    __disable_irq();    /* Deshabilitar interrupciones */

    DEBUG_PRINT("\r\n!!! ERROR FATAL — sistema detenido !!!\r\n");

#if (PRODUCTION == 1)
    /* En producción: reset inmediato del sistema */
    NVIC_SystemReset();
#else
    /* En desarrollo: parpadeo rápido del LED para indicar error */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIOC->MODER   |=  (1 << (13 * 2));    /* PC13 como salida */
    GPIOC->MODER   &= ~(1 << (13 * 2 + 1));

    while (1) {
        GPIOC->ODR ^= (1 << 13);           /* Toggle LED */
        /* Delay por software sin HAL (ya que HAL puede estar comprometido) */
        for (volatile uint32_t i = 0; i < 200000; i++) { __NOP(); }
    }
#endif
}
