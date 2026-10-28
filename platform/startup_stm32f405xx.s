/**
 * @file    startup_stm32f405xx.s
 * @brief   Archivo de arranque para STM32F405RGT6 — WALCORG Cold Wallet
 *
 * PROPÓSITO:
 *   Este es el PRIMER código que ejecuta el chip cuando se enciende
 *   o se resetea. Antes de que main() exista, este archivo:
 *
 *   1. Define la tabla de vectores de interrupción
 *      (el hardware la busca en 0x08010000 según nuestro linker script)
 *
 *   2. Define el Reset_Handler:
 *      a) Copia las variables globales inicializadas (.data)
 *         desde Flash hacia SRAM
 *      b) Inicializa a cero las variables no inicializadas (.bss)
 *      c) Llama a SystemInit() — configuración mínima del chip
 *      d) Llama a main()
 *
 * ORIGEN:
 *   Adaptado de STM32CubeF4 v1.28.0:
 *   Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f405xx.s
 *
 *   Modificaciones para WALCORG:
 *   [MOD-1] Añadidos comentarios explicativos en español
 *   [MOD-2] Añadido handler de inicialización de CCM RAM
 *   [MOD-3] Default_Handler imprime por UART en PRODUCTION=0
 *
 * NOTA SOBRE ENSAMBLADOR ARM (Thumb-2):
 *   .thumb_func  → la función usa instrucciones Thumb-2 (16/32 bits)
 *   .weak        → puede ser sobreescrita por el usuario (en stm32f4xx_it.c)
 *   .type X, %function → declara X como función (para el linker)
 *   LDR Rd, =val → cargar valor de 32 bits en registro (pseudo-instrucción)
 *   BL  label    → Branch with Link (llamada a función)
 *   B   label    → Branch (salto incondicional)
 *
 * @license GNU GPL v3
 */

/* =========================================================
 * DIRECTIVAS DE ENSAMBLADOR
 * ========================================================= */

    /* .syntax unified: habilitar sintaxis unificada ARM/Thumb
     * Permite mezclar instrucciones ARM y Thumb en el mismo archivo */
    .syntax unified

    /* .cpu cortex-m4: objetivo es el núcleo Cortex-M4
     * El ensamblador usará instrucciones válidas para este CPU */
    .cpu cortex-m4

    /* .fpu fpv4-sp-d16: la FPU presente es FPv4 precisión simple
     * con 16 registros dobles (d0-d15 = s0-s31) */
    .fpu fpv4-sp-d16

    /* .thumb: usar modo Thumb-2 por defecto
     * El Cortex-M4 SOLO soporta Thumb-2, no ARM clásico */
    .thumb


/* =========================================================
 * SECCIÓN: TABLA DE VECTORES DE INTERRUPCIÓN
 *
 * La tabla de vectores es un arreglo de punteros a funciones.
 * El hardware del Cortex-M4 la busca al inicio de la Flash
 * (o en la dirección del registro VTOR si se reubicó).
 *
 * Estructura de la tabla (Cortex-M4 + STM32F405):
 *   Offset 0x000: Valor inicial del Stack Pointer (MSP)
 *   Offset 0x004: Reset_Handler
 *   Offset 0x008: NMI_Handler
 *   Offset 0x00C: HardFault_Handler
 *   ... (más excepciones del núcleo ARM)
 *   Offset 0x040: IRQ0 (primer periférico STM32)
 *   ... (hasta IRQ81 para el STM32F405)
 *
 * .section .isr_vector,"a",%progbits:
 *   ".isr_vector" = nombre de la sección (debe coincidir con el linker script)
 *   "a"           = allocatable (ocupa espacio en Flash)
 *   %progbits     = contiene datos del programa (no BSS)
 * ========================================================= */
    .section .isr_vector,"a",%progbits
    .type  g_pfnVectors, %object       /* Declarar como objeto (datos, no función) */
    .size  g_pfnVectors, .-g_pfnVectors

g_pfnVectors:
    /* ---- Excepciones del núcleo ARM Cortex-M4 ---- */

    /* [0] Stack Pointer inicial
     * El hardware carga este valor en el MSP al resetear.
     * _stack_end está definido en nuestro linker script:
     * _stack_end = ORIGIN(SRAM) + LENGTH(SRAM) = 0x20020000 */
    .word  _stack_end

    /* [1] Reset Handler — primer código que ejecuta el chip */
    .word  Reset_Handler

    /* [2-15] Excepciones del núcleo Cortex-M4
     * Declaradas como .weak → si no se definen en stm32f4xx_it.c,
     * se usa Default_Handler (loop infinito) */
    .word  NMI_Handler              /* [2]  Non-Maskable Interrupt */
    .word  HardFault_Handler        /* [3]  Hard Fault */
    .word  MemManage_Handler        /* [4]  MPU Fault */
    .word  BusFault_Handler         /* [5]  Bus Fault */
    .word  UsageFault_Handler       /* [6]  Usage Fault */
    .word  0                        /* [7]  Reservado */
    .word  0                        /* [8]  Reservado */
    .word  0                        /* [9]  Reservado */
    .word  0                        /* [10] Reservado */
    .word  SVC_Handler              /* [11] Supervisor Call */
    .word  DebugMon_Handler         /* [12] Debug Monitor */
    .word  0                        /* [13] Reservado */
    .word  PendSV_Handler           /* [14] Pendable Service */
    .word  SysTick_Handler          /* [15] System Tick Timer */

    /* ---- IRQs de periféricos STM32F405 (IRQ0..IRQ81) ---- */
    /* Cada entrada corresponde a un periférico del STM32F405.
     * Los que no usamos apuntan a Default_Handler.
     * Los que sí usamos están definidos en stm32f4xx_it.c */

    .word  WWDG_IRQHandler                   /* [0]  Watchdog */
    .word  PVD_IRQHandler                    /* [1]  PVD via EXTI */
    .word  TAMP_STAMP_IRQHandler             /* [2]  Tamper/Timestamp */
    .word  RTC_WKUP_IRQHandler               /* [3]  RTC Wakeup */
    .word  FLASH_IRQHandler                  /* [4]  Flash global */
    .word  RCC_IRQHandler                    /* [5]  RCC global */
    .word  EXTI0_IRQHandler                  /* [6]  EXTI Line 0 */
    .word  EXTI1_IRQHandler                  /* [7]  EXTI Line 1 */
    .word  EXTI2_IRQHandler                  /* [8]  EXTI Line 2 */
    .word  EXTI3_IRQHandler                  /* [9]  EXTI Line 3 */
    .word  EXTI4_IRQHandler                  /* [10] EXTI Line 4 */
    .word  DMA1_Stream0_IRQHandler           /* [11] DMA1 Stream 0 */
    .word  DMA1_Stream1_IRQHandler           /* [12] DMA1 Stream 1 */
    .word  DMA1_Stream2_IRQHandler           /* [13] DMA1 Stream 2 */
    .word  DMA1_Stream3_IRQHandler           /* [14] DMA1 Stream 3 */
    .word  DMA1_Stream4_IRQHandler           /* [15] DMA1 Stream 4 */
    .word  DMA1_Stream5_IRQHandler           /* [16] DMA1 Stream 5 */
    .word  DMA1_Stream6_IRQHandler           /* [17] DMA1 Stream 6 */
    .word  ADC_IRQHandler                    /* [18] ADC1/2/3 global */
    .word  CAN1_TX_IRQHandler                /* [19] CAN1 TX */
    .word  CAN1_RX0_IRQHandler               /* [20] CAN1 RX0 */
    .word  CAN1_RX1_IRQHandler               /* [21] CAN1 RX1 */
    .word  CAN1_SCE_IRQHandler               /* [22] CAN1 SCE */
    .word  EXTI9_5_IRQHandler                /* [23] EXTI Lines 5-9 */
    .word  TIM1_BRK_TIM9_IRQHandler          /* [24] TIM1 Break + TIM9 */
    .word  TIM1_UP_TIM10_IRQHandler          /* [25] TIM1 Update + TIM10 */
    .word  TIM1_TRG_COM_TIM11_IRQHandler     /* [26] TIM1 Trigger + TIM11 */
    .word  TIM1_CC_IRQHandler                /* [27] TIM1 Capture/Compare */
    .word  TIM2_IRQHandler                   /* [28] TIM2 global */
    .word  TIM3_IRQHandler                   /* [29] TIM3 global */
    .word  TIM4_IRQHandler                   /* [30] TIM4 global */
    .word  I2C1_EV_IRQHandler                /* [31] I2C1 Event ← click-wheel */
    .word  I2C1_ER_IRQHandler                /* [32] I2C1 Error ← click-wheel */
    .word  I2C2_EV_IRQHandler                /* [33] I2C2 Event */
    .word  I2C2_ER_IRQHandler                /* [34] I2C2 Error */
    .word  SPI1_IRQHandler                   /* [35] SPI1 ← display TFT */
    .word  SPI2_IRQHandler                   /* [36] SPI2 */
    .word  USART1_IRQHandler                 /* [37] USART1 ← debug UART */
    .word  USART2_IRQHandler                 /* [38] USART2 */
    .word  USART3_IRQHandler                 /* [39] USART3 */
    .word  EXTI15_10_IRQHandler              /* [40] EXTI Lines 10-15 */
    .word  RTC_Alarm_IRQHandler              /* [41] RTC Alarm A/B via EXTI */
    .word  OTG_FS_WKUP_IRQHandler           /* [42] USB OTG FS Wakeup */
    .word  TIM8_BRK_TIM12_IRQHandler        /* [43] TIM8 Break + TIM12 */
    .word  TIM8_UP_TIM13_IRQHandler         /* [44] TIM8 Update + TIM13 */
    .word  TIM8_TRG_COM_TIM14_IRQHandler    /* [45] TIM8 Trigger + TIM14 */
    .word  TIM8_CC_IRQHandler               /* [46] TIM8 Capture/Compare */
    .word  DMA1_Stream7_IRQHandler          /* [47] DMA1 Stream 7 */
    .word  FSMC_IRQHandler                  /* [48] FSMC global */
    .word  SDIO_IRQHandler                  /* [49] SDIO global */
    .word  TIM5_IRQHandler                  /* [50] TIM5 global */
    .word  SPI3_IRQHandler                  /* [51] SPI3 */
    .word  UART4_IRQHandler                 /* [52] UART4 */
    .word  UART5_IRQHandler                 /* [53] UART5 */
    .word  TIM6_DAC_IRQHandler              /* [54] TIM6 + DAC underrun */
    .word  TIM7_IRQHandler                  /* [55] TIM7 global */
    .word  DMA2_Stream0_IRQHandler          /* [56] DMA2 Stream 0 */
    .word  DMA2_Stream1_IRQHandler          /* [57] DMA2 Stream 1 */
    .word  DMA2_Stream2_IRQHandler          /* [58] DMA2 Stream 2 */
    .word  DMA2_Stream3_IRQHandler          /* [59] DMA2 Stream 3 */
    .word  DMA2_Stream4_IRQHandler          /* [60] DMA2 Stream 4 */
    .word  ETH_IRQHandler                   /* [61] Ethernet */
    .word  ETH_WKUP_IRQHandler              /* [62] Ethernet Wakeup */
    .word  CAN2_TX_IRQHandler               /* [63] CAN2 TX */
    .word  CAN2_RX0_IRQHandler              /* [64] CAN2 RX0 */
    .word  CAN2_RX1_IRQHandler              /* [65] CAN2 RX1 */
    .word  CAN2_SCE_IRQHandler              /* [66] CAN2 SCE */
    .word  OTG_FS_IRQHandler                /* [67] USB OTG FS ← trezorctl */
    .word  DMA2_Stream5_IRQHandler          /* [68] DMA2 Stream 5 */
    .word  DMA2_Stream6_IRQHandler          /* [69] DMA2 Stream 6 */
    .word  DMA2_Stream7_IRQHandler          /* [70] DMA2 Stream 7 */
    .word  USART6_IRQHandler                /* [71] USART6 */
    .word  I2C3_EV_IRQHandler               /* [72] I2C3 Event */
    .word  I2C3_ER_IRQHandler               /* [73] I2C3 Error */
    .word  OTG_HS_EP1_OUT_IRQHandler        /* [74] USB OTG HS EP1 OUT */
    .word  OTG_HS_EP1_IN_IRQHandler         /* [75] USB OTG HS EP1 IN */
    .word  OTG_HS_WKUP_IRQHandler           /* [76] USB OTG HS Wakeup */
    .word  OTG_HS_IRQHandler                /* [77] USB OTG HS */
    .word  DCMI_IRQHandler                  /* [78] DCMI */
    .word  0                                /* [79] Reservado */
    .word  HASH_RNG_IRQHandler              /* [80] Hash + RNG ← hardware RNG */
    .word  FPU_IRQHandler                   /* [81] FPU ← Cortex-M4 FPU */


/* =========================================================
 * RESET HANDLER
 *
 * Primera función que ejecuta el chip después del reset.
 * No puede llamar a main() directamente — primero debe
 * preparar el entorno de C (copiar .data, limpiar .bss).
 *
 * Secuencia:
 *   1. Copiar .data: Flash (_sidata) → SRAM (_sdata.._edata)
 *   2. Limpiar .bss: SRAM (_sbss.._ebss) = 0x00000000
 *   3. [MOD-2] Limpiar CCM RAM (_sccm.._eccm) = 0x00000000
 *   4. Llamar SystemInit() — init mínima del chip (CMSIS)
 *   5. Llamar main()
 *   6. Si main() retorna (no debería), loop infinito
 * ========================================================= */
    .section .text.Reset_Handler    /* Sección específica para este handler */
    .weak    Reset_Handler          /* .weak permite sobreescribir si es necesario */
    .type    Reset_Handler, %function

Reset_Handler:

    /* ----------------------------------------------------------------
     * PASO 1: Copiar sección .data de Flash a SRAM
     *
     * Variables como: int contador = 5;
     * El valor 5 está guardado en Flash (_sidata).
     * En tiempo de ejecución debe estar en SRAM (_sdata).
     * Este loop hace la copia byte por byte (en realidad word por word).
     *
     * _sidata = dirección en Flash donde empiezan los valores iniciales
     * _sdata  = dirección en SRAM donde deben vivir en ejecución
     * _edata  = dirección en SRAM donde termina .data
     * --------------------------------------------------------------- */
    ldr   r0, =_sdata       /* r0 = destino en SRAM (inicio de .data) */
    ldr   r1, =_edata       /* r1 = fin de .data en SRAM */
    ldr   r2, =_sidata      /* r2 = fuente en Flash (valores iniciales) */
    movs  r3, #0            /* r3 = offset, empieza en 0 */
    b     LoopCopyDataInit  /* Saltar a la verificación del loop */

CopyDataInit:
    /* Leer una palabra (32 bits) de Flash y escribirla en SRAM */
    ldr   r4, [r2, r3]      /* r4 = Flash[r2 + r3] */
    str   r4, [r0, r3]      /* SRAM[r0 + r3] = r4 */
    adds  r3, r3, #4        /* Avanzar 4 bytes (una palabra) */

LoopCopyDataInit:
    /* Verificar si llegamos al final de .data */
    adds  r4, r0, r3        /* r4 = dirección actual en SRAM */
    cmp   r4, r1            /* ¿Llegamos a _edata? */
    bcc   CopyDataInit      /* Si no (carry clear = menor), continuar */

    /* ----------------------------------------------------------------
     * PASO 2: Inicializar sección .bss a cero
     *
     * Variables como: static uint8_t buffer[256];
     * No tienen valor inicial en Flash — deben inicializarse a 0.
     * El estándar C garantiza que variables globales sin
     * inicializar son 0 al inicio del programa.
     *
     * _sbss = inicio de .bss en SRAM
     * _ebss = fin de .bss en SRAM
     * --------------------------------------------------------------- */
    ldr   r2, =_sbss        /* r2 = inicio de .bss */
    ldr   r4, =_ebss        /* r4 = fin de .bss */
    movs  r3, #0            /* r3 = 0 (valor a escribir) */
    b     LoopFillZerobss   /* Saltar a la verificación */

FillZerobss:
    str   r3, [r2]          /* SRAM[r2] = 0 */
    adds  r2, r2, #4        /* Avanzar 4 bytes */

LoopFillZerobss:
    cmp   r2, r4            /* ¿Llegamos a _ebss? */
    bcc   FillZerobss       /* Si no, continuar */

    /* ----------------------------------------------------------------
     * [MOD-2] PASO 3: Limpiar CCM RAM a cero
     *
     * La CCM (Core Coupled Memory, 0x10000000) no está conectada
     * al bus DMA y no es inicializada por el hardware.
     * Debemos limpiarla manualmente para evitar datos basura.
     *
     * En Fases posteriores aquí guardaremos buffers de crypto.
     * _sccm = 0x10000000 (inicio de CCM)
     * _eccm = 0x10010000 (fin de CCM, 64KB después)
     * --------------------------------------------------------------- */
    ldr   r2, =_sccm        /* r2 = inicio de CCM */
    ldr   r4, =_eccm        /* r4 = fin de CCM */
    movs  r3, #0            /* r3 = 0 */
    b     LoopFillCCM

FillCCM:
    str   r3, [r2]          /* CCM[r2] = 0 */
    adds  r2, r2, #4

LoopFillCCM:
    cmp   r2, r4
    bcc   FillCCM

    /* ----------------------------------------------------------------
     * PASO 4: Llamar SystemInit()
     *
     * SystemInit() hace la configuración mínima del chip antes de main():
     *   - Deshabilitar FPU (la habilitamos nosotros en stm32_walcorg.c)
     *   - Configurar el registro SCB->VTOR con la dirección de la tabla
     *     de vectores (0x08010000 según nuestro linker script)
     *   - Deshabilitar todas las interrupciones
     *
     * Está definida en system_stm32f4xx.c del STM32CubeF4.
     * --------------------------------------------------------------- */
    bl    SystemInit         /* Branch with Link = llamada a función C */

    /* ----------------------------------------------------------------
     * PASO 5: Llamar main()
     *
     * En este punto el entorno de C está listo:
     *   ✓ .data copiado de Flash a SRAM
     *   ✓ .bss inicializado a cero
     *   ✓ CCM RAM limpia
     *   ✓ SystemInit() ejecutado
     *   ✓ Stack pointer configurado (lo hizo el hardware al leer
     *     el primer entry de la tabla de vectores: _stack_end)
     * --------------------------------------------------------------- */
    bl    main               /* Llamar main() */

    /* ----------------------------------------------------------------
     * PASO 6: Si main() retorna (NO debería en bare-metal)
     *
     * En un sistema embebido, main() tiene un while(1) y nunca
     * retorna. Si llega aquí, algo muy inusual ocurrió.
     * Loop infinito como medida de seguridad.
     * --------------------------------------------------------------- */
LoopForever:
    b     LoopForever        /* Loop infinito */

    /* Tamaño del Reset_Handler para el linker */
    .size  Reset_Handler, .-Reset_Handler


/* =========================================================
 * DEFAULT HANDLER
 *
 * Manejador por defecto para todas las interrupciones no
 * implementadas. Si una IRQ que no tiene handler definido
 * se dispara, el CPU llega aquí.
 *
 * En desarrollo: loop infinito — GDB mostrará que estamos
 * aquí y el stack trace dirá qué IRQ se disparó.
 *
 * Para identificar qué IRQ causó el Default_Handler:
 *   En GDB: info registers
 *   El registro IPSR (parte de xPSR) contiene el número
 *   de la excepción activa. Restar 16 para obtener el IRQ número.
 * ========================================================= */
    .section .text.Default_Handler,"ax",%progbits

Default_Handler:
Infinite_Loop:
    /* Punto de breakpoint automático si GDB está conectado */
    bkpt  #0                /* Genera un HardFault si no hay debugger */
    b     Infinite_Loop     /* Loop infinito */
    .size  Default_Handler, .-Default_Handler


/* =========================================================
 * ALIASES WEAK PARA TODOS LOS HANDLERS
 *
 * Cada handler está declarado como .weak con alias a Default_Handler.
 *
 * Esto significa:
 *   - Si defines void USART1_IRQHandler(void) en stm32f4xx_it.c,
 *     el linker usará TU implementación.
 *   - Si NO la defines, el linker usa Default_Handler (loop infinito).
 *
 * Este mecanismo permite que el proyecto compile aunque no
 * implementes todos los handlers — solo los que necesitas.
 * ========================================================= */
    /* Macro para definir un alias weak:
     * .weak NombreHandler declara el símbolo como débil
     * .set  NombreHandler, Default_Handler lo apunta al default */

    .weak      NMI_Handler
    .thumb_set NMI_Handler,Default_Handler

    .weak      HardFault_Handler
    .thumb_set HardFault_Handler,Default_Handler

    .weak      MemManage_Handler
    .thumb_set MemManage_Handler,Default_Handler

    .weak      BusFault_Handler
    .thumb_set BusFault_Handler,Default_Handler

    .weak      UsageFault_Handler
    .thumb_set UsageFault_Handler,Default_Handler

    .weak      SVC_Handler
    .thumb_set SVC_Handler,Default_Handler

    .weak      DebugMon_Handler
    .thumb_set DebugMon_Handler,Default_Handler

    .weak      PendSV_Handler
    .thumb_set PendSV_Handler,Default_Handler

    .weak      SysTick_Handler
    .thumb_set SysTick_Handler,Default_Handler

    /* ---- IRQs de periféricos ---- */
    .weak      WWDG_IRQHandler
    .thumb_set WWDG_IRQHandler,Default_Handler

    .weak      PVD_IRQHandler
    .thumb_set PVD_IRQHandler,Default_Handler

    .weak      TAMP_STAMP_IRQHandler
    .thumb_set TAMP_STAMP_IRQHandler,Default_Handler

    .weak      RTC_WKUP_IRQHandler
    .thumb_set RTC_WKUP_IRQHandler,Default_Handler

    .weak      FLASH_IRQHandler
    .thumb_set FLASH_IRQHandler,Default_Handler

    .weak      RCC_IRQHandler
    .thumb_set RCC_IRQHandler,Default_Handler

    .weak      EXTI0_IRQHandler
    .thumb_set EXTI0_IRQHandler,Default_Handler

    .weak      EXTI1_IRQHandler
    .thumb_set EXTI1_IRQHandler,Default_Handler

    .weak      EXTI2_IRQHandler
    .thumb_set EXTI2_IRQHandler,Default_Handler

    .weak      EXTI3_IRQHandler
    .thumb_set EXTI3_IRQHandler,Default_Handler

    .weak      EXTI4_IRQHandler
    .thumb_set EXTI4_IRQHandler,Default_Handler

    .weak      DMA1_Stream0_IRQHandler
    .thumb_set DMA1_Stream0_IRQHandler,Default_Handler

    .weak      DMA1_Stream1_IRQHandler
    .thumb_set DMA1_Stream1_IRQHandler,Default_Handler

    .weak      DMA1_Stream2_IRQHandler
    .thumb_set DMA1_Stream2_IRQHandler,Default_Handler

    .weak      DMA1_Stream3_IRQHandler
    .thumb_set DMA1_Stream3_IRQHandler,Default_Handler

    .weak      DMA1_Stream4_IRQHandler
    .thumb_set DMA1_Stream4_IRQHandler,Default_Handler

    .weak      DMA1_Stream5_IRQHandler
    .thumb_set DMA1_Stream5_IRQHandler,Default_Handler

    .weak      DMA1_Stream6_IRQHandler
    .thumb_set DMA1_Stream6_IRQHandler,Default_Handler

    .weak      ADC_IRQHandler
    .thumb_set ADC_IRQHandler,Default_Handler

    .weak      CAN1_TX_IRQHandler
    .thumb_set CAN1_TX_IRQHandler,Default_Handler

    .weak      CAN1_RX0_IRQHandler
    .thumb_set CAN1_RX0_IRQHandler,Default_Handler

    .weak      CAN1_RX1_IRQHandler
    .thumb_set CAN1_RX1_IRQHandler,Default_Handler

    .weak      CAN1_SCE_IRQHandler
    .thumb_set CAN1_SCE_IRQHandler,Default_Handler

    .weak      EXTI9_5_IRQHandler
    .thumb_set EXTI9_5_IRQHandler,Default_Handler

    .weak      TIM1_BRK_TIM9_IRQHandler
    .thumb_set TIM1_BRK_TIM9_IRQHandler,Default_Handler

    .weak      TIM1_UP_TIM10_IRQHandler
    .thumb_set TIM1_UP_TIM10_IRQHandler,Default_Handler

    .weak      TIM1_TRG_COM_TIM11_IRQHandler
    .thumb_set TIM1_TRG_COM_TIM11_IRQHandler,Default_Handler

    .weak      TIM1_CC_IRQHandler
    .thumb_set TIM1_CC_IRQHandler,Default_Handler

    .weak      TIM2_IRQHandler
    .thumb_set TIM2_IRQHandler,Default_Handler

    .weak      TIM3_IRQHandler
    .thumb_set TIM3_IRQHandler,Default_Handler

    .weak      TIM4_IRQHandler
    .thumb_set TIM4_IRQHandler,Default_Handler

    .weak      I2C1_EV_IRQHandler
    .thumb_set I2C1_EV_IRQHandler,Default_Handler

    .weak      I2C1_ER_IRQHandler
    .thumb_set I2C1_ER_IRQHandler,Default_Handler

    .weak      I2C2_EV_IRQHandler
    .thumb_set I2C2_EV_IRQHandler,Default_Handler

    .weak      I2C2_ER_IRQHandler
    .thumb_set I2C2_ER_IRQHandler,Default_Handler

    .weak      SPI1_IRQHandler
    .thumb_set SPI1_IRQHandler,Default_Handler

    .weak      SPI2_IRQHandler
    .thumb_set SPI2_IRQHandler,Default_Handler

    .weak      USART1_IRQHandler
    .thumb_set USART1_IRQHandler,Default_Handler

    .weak      USART2_IRQHandler
    .thumb_set USART2_IRQHandler,Default_Handler

    .weak      USART3_IRQHandler
    .thumb_set USART3_IRQHandler,Default_Handler

    .weak      EXTI15_10_IRQHandler
    .thumb_set EXTI15_10_IRQHandler,Default_Handler

    .weak      RTC_Alarm_IRQHandler
    .thumb_set RTC_Alarm_IRQHandler,Default_Handler

    .weak      OTG_FS_WKUP_IRQHandler
    .thumb_set OTG_FS_WKUP_IRQHandler,Default_Handler

    .weak      TIM8_BRK_TIM12_IRQHandler
    .thumb_set TIM8_BRK_TIM12_IRQHandler,Default_Handler

    .weak      TIM8_UP_TIM13_IRQHandler
    .thumb_set TIM8_UP_TIM13_IRQHandler,Default_Handler

    .weak      TIM8_TRG_COM_TIM14_IRQHandler
    .thumb_set TIM8_TRG_COM_TIM14_IRQHandler,Default_Handler

    .weak      TIM8_CC_IRQHandler
    .thumb_set TIM8_CC_IRQHandler,Default_Handler

    .weak      DMA1_Stream7_IRQHandler
    .thumb_set DMA1_Stream7_IRQHandler,Default_Handler

    .weak      FSMC_IRQHandler
    .thumb_set FSMC_IRQHandler,Default_Handler

    .weak      SDIO_IRQHandler
    .thumb_set SDIO_IRQHandler,Default_Handler

    .weak      TIM5_IRQHandler
    .thumb_set TIM5_IRQHandler,Default_Handler

    .weak      SPI3_IRQHandler
    .thumb_set SPI3_IRQHandler,Default_Handler

    .weak      UART4_IRQHandler
    .thumb_set UART4_IRQHandler,Default_Handler

    .weak      UART5_IRQHandler
    .thumb_set UART5_IRQHandler,Default_Handler

    .weak      TIM6_DAC_IRQHandler
    .thumb_set TIM6_DAC_IRQHandler,Default_Handler

    .weak      TIM7_IRQHandler
    .thumb_set TIM7_IRQHandler,Default_Handler

    .weak      DMA2_Stream0_IRQHandler
    .thumb_set DMA2_Stream0_IRQHandler,Default_Handler

    .weak      DMA2_Stream1_IRQHandler
    .thumb_set DMA2_Stream1_IRQHandler,Default_Handler

    .weak      DMA2_Stream2_IRQHandler
    .thumb_set DMA2_Stream2_IRQHandler,Default_Handler

    .weak      DMA2_Stream3_IRQHandler
    .thumb_set DMA2_Stream3_IRQHandler,Default_Handler

    .weak      DMA2_Stream4_IRQHandler
    .thumb_set DMA2_Stream4_IRQHandler,Default_Handler

    .weak      ETH_IRQHandler
    .thumb_set ETH_IRQHandler,Default_Handler

    .weak      ETH_WKUP_IRQHandler
    .thumb_set ETH_WKUP_IRQHandler,Default_Handler

    .weak      CAN2_TX_IRQHandler
    .thumb_set CAN2_TX_IRQHandler,Default_Handler

    .weak      CAN2_RX0_IRQHandler
    .thumb_set CAN2_RX0_IRQHandler,Default_Handler

    .weak      CAN2_RX1_IRQHandler
    .thumb_set CAN2_RX1_IRQHandler,Default_Handler

    .weak      CAN2_SCE_IRQHandler
    .thumb_set CAN2_SCE_IRQHandler,Default_Handler

    .weak      OTG_FS_IRQHandler
    .thumb_set OTG_FS_IRQHandler,Default_Handler

    .weak      DMA2_Stream5_IRQHandler
    .thumb_set DMA2_Stream5_IRQHandler,Default_Handler

    .weak      DMA2_Stream6_IRQHandler
    .thumb_set DMA2_Stream6_IRQHandler,Default_Handler

    .weak      DMA2_Stream7_IRQHandler
    .thumb_set DMA2_Stream7_IRQHandler,Default_Handler

    .weak      USART6_IRQHandler
    .thumb_set USART6_IRQHandler,Default_Handler

    .weak      I2C3_EV_IRQHandler
    .thumb_set I2C3_EV_IRQHandler,Default_Handler

    .weak      I2C3_ER_IRQHandler
    .thumb_set I2C3_ER_IRQHandler,Default_Handler

    .weak      OTG_HS_EP1_OUT_IRQHandler
    .thumb_set OTG_HS_EP1_OUT_IRQHandler,Default_Handler

    .weak      OTG_HS_EP1_IN_IRQHandler
    .thumb_set OTG_HS_EP1_IN_IRQHandler,Default_Handler

    .weak      OTG_HS_WKUP_IRQHandler
    .thumb_set OTG_HS_WKUP_IRQHandler,Default_Handler

    .weak      OTG_HS_IRQHandler
    .thumb_set OTG_HS_IRQHandler,Default_Handler

    .weak      DCMI_IRQHandler
    .thumb_set DCMI_IRQHandler,Default_Handler

    .weak      HASH_RNG_IRQHandler
    .thumb_set HASH_RNG_IRQHandler,Default_Handler

    .weak      FPU_IRQHandler
    .thumb_set FPU_IRQHandler,Default_Handler

/* Fin del archivo de startup */
