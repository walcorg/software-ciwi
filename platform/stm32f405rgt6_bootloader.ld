/* =============================================================================
 * stm32f405rgt6.ld  —  Linker Script para WalCorg Cold Wallet
 * -----------------------------------------------------------------------------
 * Define el mapa de memoria del STM32F405RGT6 y cómo el linker
 * distribuye las secciones del firmware (.text, .data, .bss, etc.)
 *
 * El linker script es el "plano de construcción" de la memoria.
 * Sin él, el compilador no sabe en qué dirección de memoria colocar
 * el código, los datos, y el stack.
 *
 * Layout de memoria adoptado del firmware de Trezor One (adaptado para F405):
 *
 *   0x08000000 ┌─────────────────┐  ← Inicio de Flash
 *              │   BOOTLOADER    │  Sectores 0-3 (4×16KB = 64KB)
 *   0x08010000 ├─────────────────┤
 *              │    FIRMWARE     │  Sectores 4-8 (64+128+128+128+128 = 576KB)
 *              │   (este .ld)    │
 *   0x080A0000 ├─────────────────┤
 *              │   ALMACÉN DE    │  Sectores 9-11 (3×128KB = 384KB)
 *              │    CLAVES       │  (NORCOW storage de Trezor)
 *   0x08100000 └─────────────────┘  ← Fin de Flash (1MB total)
 *
 *   0x20000000 ┌─────────────────┐  ← Inicio de SRAM
 *              │   SRAM (128KB)  │  Stack, heap, variables globales
 *   0x20020000 └─────────────────┘
 *
 *   0x10000000 ┌─────────────────┐  ← CCM (Core Coupled Memory)
 *              │   CCM (64KB)    │  Para datos de acceso crítico
 *   0x10010000 └─────────────────┘
 * =============================================================================
 */


/* -----------------------------------------------------------------------------
 * REGIONES DE MEMORIA
 * Cada región tiene: nombre, atributos, dirección de inicio, tamaño.
 *
 * Atributos:
 *   r  = readable    (legible)
 *   w  = writable    (escribible)
 *   x  = executable  (ejecutable — solo Flash)
 *   !  = NOT
 * --------------------------------------------------------------------------- */
MEMORY
{
    /* Flash: donde vive el código (read + execute) */
    /* [v0.2] Layout CON bootloader: la app empieza en 0x08010000 (sector 4).
     *        Solo arranca si hay un bootloader en 0x08000000 que salte aquí.
     *        Requiere USER_VECT_TAB_ADDRESS + VECT_TAB_OFFSET=0x10000 (lo pone el Makefile). */
    FLASH  (rx)  : ORIGIN = 0x08010000, LENGTH = 576K   /* Sectores 4–8  */
    STORAGE (r)  : ORIGIN = 0x080A0000, LENGTH = 384K   /* Sectores 9–11 */

    /* SRAM principal: stack, heap, variables globales */
    SRAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K

    /* CCM: Core Coupled Memory — acceso directo del CPU sin AHB bus */
    /* Útil para buffers críticos de crypto (más rápido que SRAM) */
    CCM    (rwx) : ORIGIN = 0x10000000, LENGTH = 64K
}


/* -----------------------------------------------------------------------------
 * SÍMBOLO DE STACK
 * El stack crece hacia abajo desde el final de la SRAM.
 * 8KB reservados para el stack — suficiente para Trezor legacy.
 * Si hay stackoverflow, aumentar este valor.
 * --------------------------------------------------------------------------- */
_stack_size = 0x2000;   /* 8KB de stack */
_stack_end  = ORIGIN(SRAM) + LENGTH(SRAM);          /* 0x20020000 */
_stack_start = _stack_end - _stack_size;             /* 0x2001E000 */


/* -----------------------------------------------------------------------------
 * PUNTO DE ENTRADA
 * El CPU arranca buscando el Reset_Handler en la tabla de vectores.
 * Este símbolo debe estar definido en startup_stm32f405xx.s
 * --------------------------------------------------------------------------- */
ENTRY(Reset_Handler)


/* -----------------------------------------------------------------------------
 * SECCIONES
 * Define cómo se mapean las secciones del ELF a las regiones de memoria.
 * --------------------------------------------------------------------------- */
SECTIONS
{
    /* -------------------------------------------------------------------------
     * .isr_vector — Tabla de vectores de interrupción
     *
     * DEBE ser la primera sección en Flash y estar alineada a 512 bytes
     * (requerimiento del Cortex-M4 para el registro VTOR).
     * La tabla contiene: [0]=Stack pointer inicial, [1]=Reset_Handler,
     * [2]=NMI_Handler, [3]=HardFault_Handler, ... y todas las IRQs.
     * ----------------------------------------------------------------------- */
    .isr_vector :
    {
        . = ALIGN(512);                 /* Alinear a 512 bytes — requerido por VTOR */
        KEEP(*(.isr_vector))            /* KEEP = no eliminar aunque parezca sin uso */
        . = ALIGN(4);
    } >FLASH


    /* -------------------------------------------------------------------------
     * .text — Código ejecutable
     *
     * Aquí va todo el código C compilado: funciones del firmware,
     * drivers, código de Trezor (crypto, signing, layout, etc.)
     * También incluye constantes de solo lectura (.rodata).
     * ----------------------------------------------------------------------- */
    .text :
    {
        . = ALIGN(4);

        *(.text)            /* Código de todos los archivos .o */
        *(.text*)           /* Código en subsecciones (ej: .text.Reset_Handler) */

        *(.glue_7)          /* Código de interoperabilidad ARM/Thumb */
        *(.glue_7t)
        *(.eh_frame)        /* Información de excepciones C++ (mínima en bare-metal) */

        KEEP (*(.init))     /* Código de inicialización del runtime C */
        KEEP (*(.fini))     /* Código de finalización */

        . = ALIGN(4);

        *(.rodata)          /* Constantes de solo lectura: strings, tablas */
        *(.rodata*)         /* Tablas de monedas de Trezor, mensajes protobuf, etc. */

        . = ALIGN(4);
    } >FLASH


    /* -------------------------------------------------------------------------
     * .ARM.extab y .ARM.exidx — Tablas de excepciones ARM
     *
     * Requeridas por el runtime de C para manejo de errores y
     * para que el debugger pueda hacer stack unwinding.
     * ----------------------------------------------------------------------- */
    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } >FLASH

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH


    /* -------------------------------------------------------------------------
     * .preinit_array, .init_array, .fini_array
     *
     * Tablas de constructores/destructores de C++.
     * En C puro son mínimas, pero el runtime de ARM las busca.
     * ----------------------------------------------------------------------- */
    .preinit_array :
    {
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP (*(.preinit_array*))
        PROVIDE_HIDDEN (__preinit_array_end = .);
    } >FLASH

    .init_array :
    {
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array*))
        PROVIDE_HIDDEN (__init_array_end = .);
    } >FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP (*(SORT(.fini_array.*)))
        KEEP (*(.fini_array*))
        PROVIDE_HIDDEN (__fini_array_end = .);
    } >FLASH


    /* -------------------------------------------------------------------------
     * Símbolos de frontera de Flash
     *
     * _sidata: dirección en Flash donde están los valores iniciales de .data
     * El startup code copia desde _sidata a _sdata durante el boot.
     * ----------------------------------------------------------------------- */
    _sidata = LOADADDR(.data);


    /* -------------------------------------------------------------------------
     * .data — Variables globales inicializadas
     *
     * Ejemplo: int contador = 5;
     *
     * En Flash se guarda el valor inicial (5).
     * En SRAM se reserva el espacio donde vivirá en tiempo de ejecución.
     * El startup code (Reset_Handler) copia Flash → SRAM antes de main().
     *
     * AT>FLASH significa: "ocupa espacio en SRAM pero el valor inicial
     * está almacenado en Flash"
     * ----------------------------------------------------------------------- */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;             /* Símbolo: inicio de .data en SRAM */

        *(.data)                /* Variables globales inicializadas */
        *(.data*)

        . = ALIGN(4);
        _edata = .;             /* Símbolo: fin de .data en SRAM */
    } >SRAM AT>FLASH            /* Reside en SRAM, inicializado desde Flash */


    /* -------------------------------------------------------------------------
     * .bss — Variables globales NO inicializadas (o inicializadas a 0)
     *
     * Ejemplo: static uint8_t buffer[256];
     *
     * NO ocupan espacio en Flash (no tienen valor inicial que guardar).
     * El startup code las pone a 0 antes de main().
     * ----------------------------------------------------------------------- */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;              /* Símbolo: inicio de .bss */

        *(.bss)
        *(.bss*)
        *(COMMON)               /* Variables "common" de C (declaradas extern) */

        . = ALIGN(4);
        _ebss = .;              /* Símbolo: fin de .bss */
    } >SRAM


    /* -------------------------------------------------------------------------
     * .ccm — Sección especial en CCM (Core Coupled Memory)
     *
     * Para buffers críticos de crypto que necesitan máxima velocidad.
     * La CCM es accedida directamente por el CPU sin pasar por el bus AHB,
     * lo que la hace ~30% más rápida que la SRAM para ciertas operaciones.
     *
     * Uso en código C:
     *   __attribute__((section(".ccm"))) uint8_t sha_buffer[64];
     * ----------------------------------------------------------------------- */
    .ccm (NOLOAD) :
    {
        . = ALIGN(4);
        _sccm = .;

        *(.ccm)
        *(.ccm*)

        . = ALIGN(4);
        _eccm = .;
    } >CCM


    /* -------------------------------------------------------------------------
     * HEAP — Memoria dinámica (malloc/free)
     *
     * Trezor legacy usa muy poco heap (lo evita por seguridad).
     * Lo definimos para compatibilidad con el runtime de C.
     * _heap_start queda justo después de .bss.
     * ----------------------------------------------------------------------- */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE ( end = . );         /* Símbolo 'end' usado por malloc de newlib */
        PROVIDE ( _end = . );
        . = . + 0x800;               /* 2KB de heap mínimo */
        . = ALIGN(8);
    } >SRAM


    /* -------------------------------------------------------------------------
     * Secciones descartadas
     *
     * Eliminar información de debug de las librerías estándar de ARM
     * que no necesitamos en el firmware final (ahorran espacio en Flash).
     * ----------------------------------------------------------------------- */
    /DISCARD/ :
    {
        libc.a ( * )        /* Librería C estándar — no usar en bare-metal */
        libm.a ( * )        /* Librería matemática — usar la de Trezor/crypto */
        libgcc.a ( * )
    }

    /* Información de debug DWARF — se incluye en el ELF para GDB
     * pero NO se flashea al chip (el linker lo sabe automáticamente) */
    .debug_info     0 : { *(.debug_info) }
    .debug_abbrev   0 : { *(.debug_abbrev) }
    .debug_line     0 : { *(.debug_line) }
    .debug_frame    0 : { *(.debug_frame) }
    .debug_str      0 : { *(.debug_str) }
    .debug_loc      0 : { *(.debug_loc) }
}


/* -----------------------------------------------------------------------------
 * VERIFICACIONES DE TAMAÑO
 *
 * El linker falla si el firmware no cabe en la Flash o si el
 * stack + .data + .bss no caben en la SRAM.
 * Estas aserciones atrapan el error en tiempo de compilación,
 * no en tiempo de ejecución.
 * --------------------------------------------------------------------------- */

/* Verificar que el código cabe en los 576KB de firmware Flash */
ASSERT(
    (SIZEOF(.text) + SIZEOF(.data)) <= 576K,
    "ERROR: El firmware no cabe en Flash. Revisar BITCOIN_ONLY=1 o eliminar codigo."
)

/* Verificar que los datos caben en los 128KB de SRAM */
ASSERT(
    (SIZEOF(.data) + SIZEOF(.bss) + _stack_size) <= 128K,
    "ERROR: Los datos no caben en SRAM. Reducir buffers o aumentar el chip."
)
