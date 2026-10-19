/**
 * @file    stm32f4xx_it.c
 * @brief   Manejadores de interrupción — WalCorg Fase 1
 *
 * Los "fault handlers" (HardFault, MemManage, BusFault, UsageFault)
 * son especialmente importantes durante el desarrollo: cuando el CPU
 * encuentra un error grave, llega aquí. Sin un handler útil, el
 * debugger solo muestra que estás en un loop infinito sin contexto.
 *
 * Implementamos un HardFault handler que extrae el stack frame del
 * CPU para que GDB pueda mostrar exactamente qué instrucción causó
 * el error y el estado de todos los registros en ese momento.
 *
 * @license GNU GPL v3
 */

#include "stm32f4xx_it.h"
#include "stm32f4xx_hal.h"
#include "uart_debug.h"    /* Para imprimir info del fault por UART */

/* =========================================================
 * ESTRUCTURA DEL STACK FRAME DEL CORTEX-M4
 *
 * Cuando ocurre una excepción, el hardware automáticamente guarda
 * estos 8 registros en el stack (en este orden exacto).
 * Leyendo este stack frame podemos saber exactamente qué estaba
 * haciendo el CPU cuando ocurrió el error.
 * ========================================================= */
typedef struct {
    uint32_t r0;    /* Argumento 1 / resultado de función */
    uint32_t r1;    /* Argumento 2 */
    uint32_t r2;    /* Argumento 3 */
    uint32_t r3;    /* Argumento 4 */
    uint32_t r12;   /* Registro de propósito general */
    uint32_t lr;    /* Link Register: dirección de retorno de la función */
    uint32_t pc;    /* Program Counter: instrucción que causó el fault */
    uint32_t xpsr;  /* Estado del procesador: flags de condición, modo, etc. */
} cortex_m4_stack_frame_t;


/* =========================================================
 * FUNCIÓN PRIVADA: analizar y reportar un Hard Fault
 *
 * Recibe el puntero al stack frame que guardó el hardware.
 * Esta función se llama desde el handler en ensamblador de abajo.
 *
 * __attribute__((used)) le dice al compilador que NO elimine
 * esta función aunque parezca que nadie la llama directamente
 * (la llama el código ensamblador inline).
 * ========================================================= */
__attribute__((used))
static void hard_fault_handler_c(uint32_t *sp) {

    /* Castear el stack pointer al struct para acceder a cada registro */
    cortex_m4_stack_frame_t *frame = (cortex_m4_stack_frame_t *)sp;

    /* Registros de diagnóstico del Cortex-M4:
     *
     * CFSR = Configurable Fault Status Register
     *   Bits 7:0   = MMFSR (MemManage Fault Status)
     *   Bits 15:8  = BFSR  (Bus Fault Status)
     *   Bits 31:16 = UFSR  (Usage Fault Status)
     *
     * HFSR = Hard Fault Status Register
     *   Bit 1  = VECTTBL: error leyendo la tabla de vectores
     *   Bit 30 = FORCED:  otro fault escaló a HardFault
     *   Bit 31 = DEBUGEVT: debug event
     *
     * BFAR = Bus Fault Address Register
     *   Dirección de memoria que causó el bus fault
     *
     * MMAR = MemManage Fault Address Register
     *   Dirección de memoria que violó la MPU
     */
    volatile uint32_t cfsr  = SCB->CFSR;   /* Estado del fault */
    volatile uint32_t hfsr  = SCB->HFSR;   /* Estado del hard fault */
    volatile uint32_t bfar  = SCB->BFAR;   /* Dirección del bus fault */
    volatile uint32_t mmar  = SCB->MMFAR;  /* Dirección del MPU fault */

    /* Imprimir diagnóstico por UART (solo en PRODUCTION=0) */
    DEBUG_PRINTF("\r\n\r\n");
    DEBUG_PRINTF("========================================\r\n");
    DEBUG_PRINTF("!!! HARD FAULT DETECTADO !!!\r\n");
    DEBUG_PRINTF("========================================\r\n");
    DEBUG_PRINTF("PC   = 0x%08lX  <- instruccion que fallo\r\n", frame->pc);
    DEBUG_PRINTF("LR   = 0x%08lX  <- funcion que lo llamo\r\n", frame->lr);
    DEBUG_PRINTF("R0   = 0x%08lX\r\n", frame->r0);
    DEBUG_PRINTF("R1   = 0x%08lX\r\n", frame->r1);
    DEBUG_PRINTF("R2   = 0x%08lX\r\n", frame->r2);
    DEBUG_PRINTF("R3   = 0x%08lX\r\n", frame->r3);
    DEBUG_PRINTF("R12  = 0x%08lX\r\n", frame->r12);
    DEBUG_PRINTF("xPSR = 0x%08lX\r\n", frame->xpsr);
    DEBUG_PRINTF("CFSR = 0x%08lX\r\n", cfsr);
    DEBUG_PRINTF("HFSR = 0x%08lX\r\n", hfsr);
    DEBUG_PRINTF("BFAR = 0x%08lX\r\n", bfar);
    DEBUG_PRINTF("MMAR = 0x%08lX\r\n", mmar);
    DEBUG_PRINTF("========================================\r\n");
    DEBUG_PRINTF("Pega el valor de PC en:\r\n");
    DEBUG_PRINTF("arm-none-eabi-addr2line -e build/walcorg.elf 0x%08lX\r\n", frame->pc);
    DEBUG_PRINTF("para ver la linea de codigo exacta.\r\n");

    /* Silenciar warnings de variables volátiles no usadas en PRODUCTION=1 */
    (void)cfsr; (void)hfsr; (void)bfar; (void)mmar;

    /* Detener el CPU con un breakpoint si GDB está conectado.
     * Si GDB no está conectado, __BKPT(0) dispara otro HardFault
     * que se convierte en loop infinito — comportamiento esperado. */
    __BKPT(0);

    /* Loop infinito — el sistema está en estado de error irrecuperable.
     * En producción (PRODUCTION=1) aquí iría: NVIC_SystemReset() */
    while (1) { __NOP(); }
}


/* =========================================================
 * EXCEPCIONES DEL NÚCLEO ARM CORTEX-M4
 * ========================================================= */

/**
 * NMI_Handler — Non-Maskable Interrupt
 *
 * En STM32F4 se activa si el CSS (Clock Security System) detecta
 * que el cristal externo (HSE) falló. Si llegamos aquí durante
 * el desarrollo, significa que el cristal de 8MHz de la WeAct
 * tiene un problema de conexión o está dañado.
 */
void NMI_Handler(void) {
    /* Verificar si fue un fallo del CSS (Clock Security System) */
    if (__HAL_RCC_GET_IT(RCC_IT_CSS)) {
        /* El cristal HSE falló — esto es crítico porque el USB
         * y el PLL dependen del HSE. Sin HSE no hay 168MHz ni 48MHz para USB. */
        DEBUG_PRINT("NMI: Fallo del cristal HSE. Verificar cristal 8MHz.\r\n");
        __HAL_RCC_CLEAR_IT(RCC_IT_CSS);  /* Limpiar el flag de la interrupción */
    }
    while (1) { __NOP(); }
}

/**
 * HardFault_Handler — Error grave del procesador
 *
 * Este handler usa un truco de ensamblador inline para detectar
 * si el stack activo es el MSP (Main Stack Pointer, usado en
 * el contexto de interrupción) o el PSP (Process Stack Pointer,
 * usado en el contexto de thread).
 *
 * Luego pasa el stack pointer correcto a hard_fault_handler_c()
 * para que pueda leer el stack frame y mostrar el diagnóstico.
 *
 * __attribute__((naked)) significa: el compilador NO genera
 * prólogo/epílogo de la función. Necesario porque manipulamos
 * el stack manualmente — si el compilador agrega código de
 * prólogo, corrompería el stack frame que queremos leer.
 */
__attribute__((naked))
void HardFault_Handler(void) {
    __asm volatile (
        /* Verificar bit 2 del LR (EXC_RETURN):
         *   bit 2 = 1 → estábamos usando PSP (Process Stack)
         *   bit 2 = 0 → estábamos usando MSP (Main Stack)    */
        "tst lr, #4          \n"   /* Test bit 2 del LR */
        "ite eq              \n"   /* If-Then-Else */
        "mrseq r0, msp       \n"   /* Si bit2=0: r0 = MSP */
        "mrsne r0, psp       \n"   /* Si bit2=1: r0 = PSP */

        /* r0 ahora apunta al stack frame guardado por el hardware.
         * Llamar a la función C que lo analiza. */
        "b hard_fault_handler_c \n"
    );
}

/**
 * MemManage_Handler — Violación de la MPU
 *
 * Trezor usa la MPU para proteger las regiones de memoria donde
 * viven las claves privadas. Si el código del firmware intenta
 * leer esas regiones directamente, llega aquí.
 * En Fase 1 la MPU no está configurada, así que esto no debería
 * dispararse — si llega aquí hay un bug serio.
 */
void MemManage_Handler(void) {
    DEBUG_PRINTF("MemManage Fault en: 0x%08lX\r\n", SCB->MMFAR);
    DEBUG_PRINTF("MMFSR = 0x%02lX\r\n", (SCB->CFSR & 0xFF));
    __BKPT(0);
    while (1) { __NOP(); }
}

/**
 * BusFault_Handler — Error en el bus de memoria
 *
 * Causa más común: olvidar habilitar el clock de un periférico
 * antes de escribir sus registros. Por ejemplo, escribir en
 * GPIOC->ODR sin antes llamar __HAL_RCC_GPIOC_CLK_ENABLE().
 */
void BusFault_Handler(void) {
    /* BFSR está en los bits 15:8 del CFSR */
    uint8_t bfsr = (SCB->CFSR >> 8) & 0xFF;
    DEBUG_PRINTF("Bus Fault. BFSR = 0x%02X\r\n", bfsr);

    /* Bit 7 del BFSR = BFARVALID: si está en 1, BFAR tiene la dirección exacta */
    if (bfsr & 0x80) {
        DEBUG_PRINTF("Direccion del fault: 0x%08lX\r\n", SCB->BFAR);
        DEBUG_PRINTF("Posible causa: clock del periferico no habilitado.\r\n");
    }
    __BKPT(0);
    while (1) { __NOP(); }
}

/**
 * UsageFault_Handler — Error de uso del CPU
 *
 * Causas comunes:
 *   - División por cero (si está habilitada la detección en CCR)
 *   - Instrucción Thumb inválida
 *   - Acceso no alineado a memoria
 */
void UsageFault_Handler(void) {
    /* UFSR está en los bits 31:16 del CFSR */
    uint16_t ufsr = (SCB->CFSR >> 16) & 0xFFFF;
    DEBUG_PRINTF("Usage Fault. UFSR = 0x%04X\r\n", ufsr);

    if (ufsr & 0x0200) DEBUG_PRINT("Causa: division por cero\r\n");
    if (ufsr & 0x0100) DEBUG_PRINT("Causa: acceso no alineado\r\n");
    if (ufsr & 0x0008) DEBUG_PRINT("Causa: co-procesador no disponible\r\n");
    if (ufsr & 0x0002) DEBUG_PRINT("Causa: retorno de excepcion invalido\r\n");
    if (ufsr & 0x0001) DEBUG_PRINT("Causa: instruccion Thumb invalida\r\n");

    __BKPT(0);
    while (1) { __NOP(); }
}

/**
 * SVC_Handler — Llamada al supervisor
 * En Fase 1 no usamos SVC. Handler vacío por completitud.
 */
void SVC_Handler(void) {
    /* En Trezor: aquí se maneja la transición bootloader → firmware */
}

/**
 * DebugMon_Handler — Monitor de debug
 * Usado por GDB para breakpoints en modo monitor.
 */
void DebugMon_Handler(void) {
    /* Nada que hacer — el hardware lo maneja automáticamente */
}

/**
 * PendSV_Handler — Servicio pendiente
 * En sistemas RTOS: cambio de contexto de tareas.
 * En bare-metal: no se usa pero debe estar definido.
 */
void PendSV_Handler(void) {
    /* No usado en bare-metal */
}

/**
 * SysTick_Handler — Temporizador de 1ms
 *
 * Esta es la interrupción más frecuente del sistema: se dispara
 * exactamente cada 1ms (configurado en HAL_Init → HAL_InitTick).
 *
 * HAL_IncTick() incrementa el contador uwTick que usa HAL_GetTick()
 * y HAL_Delay(). Sin este handler, HAL_Delay() nunca termina.
 *
 * IMPORTANTE: esta función sobreescribe el handler "weak" que
 * viene en el startup assembly. Si hubiera dos definiciones,
 * el linker daría error — por eso blink_test.c ya no lo define.
 */
void SysTick_Handler(void) {
    HAL_IncTick();  /* Incrementa uwTick en 1 — base de tiempo del HAL */
}


/* =========================================================
 * INTERRUPCIONES DE PERIFÉRICOS
 * ========================================================= */

/**
 * USART1_IRQHandler — Interrupción de UART1
 *
 * Llamada cuando llegan datos por UART (PA10 RX) o cuando
 * el buffer de transmisión está vacío (PA9 TX).
 * HAL_UART_IRQHandler procesa el estado y llama los callbacks.
 */
extern UART_HandleTypeDef huart1;  /* Definido en uart_debug.c */

void USART1_IRQHandler(void) {
    /* HAL maneja todos los sub-estados de UART (TX completo,
     * RX recibido, error de paridad, etc.) internamente */
    HAL_UART_IRQHandler(&huart1);
}

/**
 * OTG_FS_IRQHandler — Interrupción USB Full Speed
 *
 * El controlador USB del STM32F405 genera esta interrupción para:
 *   - Conexión/desconexión del cable USB
 *   - Recepción de paquetes del host (trezorctl)
 *   - Fin de transmisión de paquetes
 *   - Inicio de frame SOF (cada 1ms en Full Speed)
 *
 * En Fase 1 verificamos que esta interrupción se dispara cuando
 * conectamos el USB — si no se dispara, hay un problema en la
 * configuración del USB OTG o en los pines PA11/PA12.
 */
/* [v0.2] Definición débil: la reemplaza usb_test.c (fase 1) o usb_hid.c (fase 5+).
 *        Así las fases sin USB (2-4) enlazan sin errores. */
__attribute__((weak)) PCD_HandleTypeDef hpcd_USB_OTG_FS;

void OTG_FS_IRQHandler(void) {
    /* HAL_PCD_IRQHandler procesa todos los eventos USB y llama
     * los callbacks correspondientes (connect, disconnect, setup, data) */
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
}
