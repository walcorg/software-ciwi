# =============================================================================
# Makefile — WalCorg Cold Wallet Firmware
# -----------------------------------------------------------------------------
# Uso:
#   make              → compila el firmware completo
#   make blink        → compila solo el test de blink (Fase 1)
#   make flash        → flashea el firmware completo
#   make flash-blink  → flashea el test de blink
#   make debug        → abre sesión GDB interactiva
#   make clean        → elimina archivos compilados
#   make size         → muestra uso de Flash y RAM
#   make info         → muestra configuración actual
# =============================================================================


# -----------------------------------------------------------------------------
# CONFIGURACIÓN DEL TOOLCHAIN
# -----------------------------------------------------------------------------
# Prefijo del compilador cross para ARM Cortex-M4.
# Instalación:
#   Ubuntu/Debian: sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi
#   macOS:         brew install arm-none-eabi-gcc
#   Windows:       Descargar de https://developer.arm.com/tools-and-software/
#                              open-source-software/developer-tools/gnu-toolchain
TOOLCHAIN_PREFIX = arm-none-eabi-

# Herramientas del toolchain
CC      = $(TOOLCHAIN_PREFIX)gcc        # Compilador C
AS      = $(TOOLCHAIN_PREFIX)as         # Ensamblador
LD      = $(TOOLCHAIN_PREFIX)gcc        # Linker (usamos gcc como linker)
OBJCOPY = $(TOOLCHAIN_PREFIX)objcopy    # Convierte ELF → BIN/HEX
OBJDUMP = $(TOOLCHAIN_PREFIX)objdump    # Desensambla el firmware
SIZE    = $(TOOLCHAIN_PREFIX)size       # Muestra tamaños de secciones
GDB     = $(TOOLCHAIN_PREFIX)gdb        # Debugger


# -----------------------------------------------------------------------------
# DIRECTORIOS DEL PROYECTO
# -----------------------------------------------------------------------------
BUILD_DIR   = build          # Archivos compilados (.o, .elf, .bin)
SRC_DIR     = src            # Código fuente propio
DRIVERS_DIR = drivers        # Drivers (clickwheel, display, etc.)
LD_DIR      = ld             # Linker scripts
SCRIPTS_DIR = scripts        # Scripts de OpenOCD

# Directorio del TREZOR firmware (clonar en el nivel superior del proyecto)
# Si clonaste trezor-firmware en otro lugar, ajusta este path:
TREZOR_DIR  = ../trezor-firmware/legacy

# Directorio del STM32 HAL (vendor)
# Puedes usar el que viene con trezor-firmware o descargar el de ST:
HAL_DIR     = vendor/STM32F4xx_HAL_Driver


# -----------------------------------------------------------------------------
# CONFIGURACIÓN DEL CHIP
# -----------------------------------------------------------------------------
# MCU: STM32F405RGT6
#   Cortex-M4 con FPU (Floating Point Unit)
#   -mcpu=cortex-m4       → arquitectura objetivo
#   -mthumb               → usar instrucciones Thumb-2 (más densas que ARM)
#   -mfpu=fpv4-sp-d16     → FPU de precisión simple con 16 registros dobles
#   -mfloat-abi=hard      → usar registros FPU para argumentos de punto flotante
#                           (más rápido que "soft" que usa registros enteros)
MCU_FLAGS = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

# Definición del chip para el HAL de ST
# HAL usa esta macro para seleccionar el header correcto del chip
CHIP_DEFINE = -DSTM32F405xx


# -----------------------------------------------------------------------------
# FLAGS DE COMPILACIÓN
# -----------------------------------------------------------------------------

# Flags de optimización
# -Og  = optimización para debugging (mantiene info de debug, algo de optimización)
# -O2  = optimización para producción (más rápido, menos legible en debug)
# -Os  = optimización para tamaño (importante si el firmware es grande)
# Para Fase 1 usamos -Og para poder debuggear fácilmente
OPT = -Og

# Flags de warnings — activar todos los warnings importantes
# Tratar warnings como errores en producción (-Werror) pero no en desarrollo
WARN_FLAGS = \
    -Wall \           # Warnings básicos importantes
    -Wextra \         # Warnings adicionales
    -Wshadow \        # Detectar variables que "ocultan" otras del mismo nombre
    -Wno-unused-parameter  # No avisar de parámetros sin usar (común en HAL callbacks)

# Flags de debug — información para GDB
# -g3 = máxima información de debug (incluye macros)
# -gdwarf-2 = formato DWARF v2, compatible con todos los debuggers
DEBUG_FLAGS = -g3 -gdwarf-2

# Flags del compilador C
CFLAGS = \
    $(MCU_FLAGS) \          # Configuración del CPU
    $(CHIP_DEFINE) \        # STM32F405xx
    $(OPT) \                # Nivel de optimización
    $(WARN_FLAGS) \         # Warnings
    $(DEBUG_FLAGS) \        # Info de debug
    -std=c11 \              # Estándar C11
    -ffunction-sections \   # Cada función en su propia sección (permite eliminar no usadas)
    -fdata-sections \       # Cada variable global en su propia sección
    -fno-common \           # No usar sección "common" (más seguro)
    -fstack-usage \         # Generar archivo .su con uso de stack por función
    -ffreestanding \        # No asumir librería estándar completa (bare-metal)
    -DUSE_HAL_DRIVER \      # Habilitar el HAL de ST
    -DPRODUCTION=0          # 0=desarrollo (debug habilitado), 1=producción


# -----------------------------------------------------------------------------
# INCLUDES — directorios de archivos .h
# -----------------------------------------------------------------------------
INCLUDES = \
    -I$(SRC_DIR) \
    -I$(DRIVERS_DIR) \
    -Ivendor/CMSIS/Include \                    # CMSIS core para Cortex-M4
    -Ivendor/CMSIS/Device/ST/STM32F4xx/Include \# Registros del STM32F405
    -I$(HAL_DIR)/Inc \                          # HAL de ST
    -I$(TREZOR_DIR)/firmware \                  # Headers de Trezor legacy
    -I$(TREZOR_DIR)/crypto                      # Crypto library de Trezor


# -----------------------------------------------------------------------------
# FLAGS DEL LINKER
# -----------------------------------------------------------------------------
LDFLAGS = \
    $(MCU_FLAGS) \                              # Mismo MCU que el compilador
    -T$(LD_DIR)/stm32f405rgt6.ld \             # Nuestro linker script
    -Wl,--gc-sections \                         # Eliminar secciones no usadas
    -Wl,-Map=$(BUILD_DIR)/walcorg.map \         # Generar mapa de memoria
    -Wl,--print-memory-usage \                  # Mostrar uso de Flash/RAM al linkear
    -Wl,--no-warn-rwx-segments \               # Suprimir warning de CCM ejecutable
    -specs=nano.specs \                         # Newlib-nano: librería C mínima
    -specs=nosys.specs \                        # Sin syscalls del SO (bare-metal)
    -lc \                                       # Librería C (newlib-nano)
    -lm \                                       # Librería matemática
    -lnosys                                     # Stubs de syscalls vacíos


# -----------------------------------------------------------------------------
# ARCHIVOS FUENTE
# -----------------------------------------------------------------------------

# Fuentes del HAL de ST (solo los módulos que usamos)
# Agregar/quitar según los periféricos que uses
HAL_SOURCES = \
    $(HAL_DIR)/Src/stm32f4xx_hal.c \
    $(HAL_DIR)/Src/stm32f4xx_hal_cortex.c \    # NVIC, SysTick
    $(HAL_DIR)/Src/stm32f4xx_hal_gpio.c \      # GPIO
    $(HAL_DIR)/Src/stm32f4xx_hal_rcc.c \       # Reset and Clock Control
    $(HAL_DIR)/Src/stm32f4xx_hal_rcc_ex.c \    # RCC extensiones (PLL)
    $(HAL_DIR)/Src/stm32f4xx_hal_i2c.c \       # I2C (para click-wheel)
    $(HAL_DIR)/Src/stm32f4xx_hal_spi.c \       # SPI (para display TFT)
    $(HAL_DIR)/Src/stm32f4xx_hal_uart.c \      # UART (para debug)
    $(HAL_DIR)/Src/stm32f4xx_hal_pcd.c \       # USB Device
    $(HAL_DIR)/Src/stm32f4xx_hal_pcd_ex.c \
    $(HAL_DIR)/Src/stm32f4xx_hal_flash.c \     # Flash (escritura de claves)
    $(HAL_DIR)/Src/stm32f4xx_hal_flash_ex.c \
    $(HAL_DIR)/Src/stm32f4xx_hal_rng.c         # Hardware RNG (crypto)

# Archivo de startup en ensamblador
# Define el Reset_Handler, la tabla de vectores, e inicializa .data y .bss
STARTUP_FILE = vendor/CMSIS/Device/ST/STM32F4xx/Source/startup_stm32f405xx.s

# Fuentes propias del WalCorg
WALCORG_SOURCES = \
    $(DRIVERS_DIR)/clickwheel.c     # Driver click-wheel I2C (AS5600)
    # $(DRIVERS_DIR)/display_tft.c  # ← Agregar en Fase 3
    # $(DRIVERS_DIR)/ui_theme.c     # ← Agregar en Fase 4
    # $(SRC_DIR)/ui_components.c    # ← Agregar en Fase 4


# =============================================================================
# TARGETS
# =============================================================================

# -----------------------------------------------------------------------------
# Target por defecto: compilar firmware completo
# make (sin argumentos) ejecuta este target
# -----------------------------------------------------------------------------
.PHONY: all
all: $(BUILD_DIR)/walcorg.elf $(BUILD_DIR)/walcorg.bin
	@echo ""
	@echo "✓ Firmware compilado exitosamente"
	@$(MAKE) size


# -----------------------------------------------------------------------------
# Target: test de blink para Fase 1
# Solo compila blink_test.c + HAL + startup, sin el resto del firmware
# -----------------------------------------------------------------------------
.PHONY: blink
blink: $(BUILD_DIR)/blink_test.elf $(BUILD_DIR)/blink_test.bin
	@echo ""
	@echo "✓ Blink test compilado"
	@$(SIZE) $(BUILD_DIR)/blink_test.elf


# Compilar el blink test
$(BUILD_DIR)/blink_test.elf: $(BUILD_DIR)/blink_test.o \
                              $(HAL_SOURCES:.c=.o) \
                              $(BUILD_DIR)/startup_stm32f405xx.o
	@mkdir -p $(BUILD_DIR)
	@echo "Linkando blink test..."
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/blink_test.o: $(SRC_DIR)/blink_test.c
	@mkdir -p $(BUILD_DIR)
	@echo "Compilando blink_test.c..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@


# -----------------------------------------------------------------------------
# Regla genérica: compilar cualquier .c → .o
# El % es un wildcard — aplica a TODOS los archivos .c del proyecto
# -----------------------------------------------------------------------------
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)              # Crear directorio si no existe
	@echo "  CC  $<"                 # Mostrar qué archivo se está compilando
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Compilar el archivo de startup en ensamblador
$(BUILD_DIR)/startup_stm32f405xx.o: $(STARTUP_FILE)
	@mkdir -p $(BUILD_DIR)
	@echo "  AS  $<"
	$(CC) $(MCU_FLAGS) -c $< -o $@


# -----------------------------------------------------------------------------
# Generar archivo BIN desde ELF
# El ST-Link puede flashear tanto ELF como BIN/HEX
# BIN = solo el contenido de la flash, sin información de debug
# ELF = incluye info de debug para GDB
# -----------------------------------------------------------------------------
$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	@echo "  BIN $@"
	$(OBJCOPY) -O binary $< $@       # Extraer sección de Flash como binario puro

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	@echo "  HEX $@"
	$(OBJCOPY) -O ihex $< $@         # Formato Intel HEX (alternativa al BIN)


# -----------------------------------------------------------------------------
# FLASHEAR — escribir el firmware al chip via OpenOCD + ST-Link
# -----------------------------------------------------------------------------

# Flashear el firmware completo
.PHONY: flash
flash: $(BUILD_DIR)/walcorg.elf
	@echo "Flasheando firmware completo..."
	openocd \
	    -f $(SCRIPTS_DIR)/openocd_weact_f405.cfg \
	    -c "program $(BUILD_DIR)/walcorg.elf verify reset exit"
	@echo "✓ Firmware flasheado y verificado"

# Flashear solo el blink test (Fase 1)
.PHONY: flash-blink
flash-blink: $(BUILD_DIR)/blink_test.elf
	@echo "Flasheando blink test..."
	openocd \
	    -f $(SCRIPTS_DIR)/openocd_weact_f405.cfg \
	    -c "program $(BUILD_DIR)/blink_test.elf verify reset exit"
	@echo "✓ Blink test flasheado — el LED debe parpadear cada 500ms"

# Borrar toda la flash del chip (útil si el firmware anterior causó problemas)
.PHONY: erase
erase:
	@echo "Borrando toda la flash..."
	openocd \
	    -f $(SCRIPTS_DIR)/openocd_weact_f405.cfg \
	    -c "init; reset halt; flash erase_address 0x08000000 0x100000; reset; exit"
	@echo "✓ Flash borrada"


# -----------------------------------------------------------------------------
# DEBUG — sesión interactiva con GDB
# -----------------------------------------------------------------------------

# Iniciar servidor OpenOCD en background, luego conectar GDB
.PHONY: debug
debug: $(BUILD_DIR)/blink_test.elf
	@echo "Iniciando sesión de debug..."
	@echo "En otra terminal: make openocd"
	$(GDB) \
	    -ex "target remote :3333" \          # Conectar a OpenOCD
	    -ex "monitor reset halt" \           # Resetear y detener el CPU
	    -ex "load" \                         # Cargar el ELF al chip
	    -ex "break main" \                   # Breakpoint al inicio de main()
	    -ex "continue" \                     # Ejecutar hasta el breakpoint
	    $(BUILD_DIR)/blink_test.elf

# Iniciar solo el servidor OpenOCD (sin GDB)
# Usar en una terminal separada mientras debug corre en otra
.PHONY: openocd
openocd:
	openocd -f $(SCRIPTS_DIR)/openocd_weact_f405.cfg


# -----------------------------------------------------------------------------
# UTILIDADES
# -----------------------------------------------------------------------------

# Mostrar uso de Flash y RAM
.PHONY: size
size: $(BUILD_DIR)/walcorg.elf
	@echo ""
	@echo "=== Uso de memoria ==="
	$(SIZE) --format=berkeley $<
	@echo ""
	@echo "  text  = código (.text) + constantes (.rodata) → va a Flash"
	@echo "  data  = variables inicializadas → ocupa Flash + RAM"
	@echo "  bss   = variables sin inicializar → solo RAM"
	@echo "  dec   = total en decimal"
	@echo ""
	@echo "  Flash usada = text + data"
	@echo "  RAM usada   = data + bss"
	@echo "  Flash total = 1,048,576 bytes (1MB)"
	@echo "  RAM total   =   131,072 bytes (128KB)"

# Desensamblar el firmware (útil para verificar que el compilador
# generó el código correcto, especialmente para funciones críticas de crypto)
.PHONY: disasm
disasm: $(BUILD_DIR)/walcorg.elf
	$(OBJDUMP) -d -S $< > $(BUILD_DIR)/walcorg.disasm
	@echo "✓ Desensamblado guardado en $(BUILD_DIR)/walcorg.disasm"

# Mostrar configuración actual del proyecto
.PHONY: info
info:
	@echo "=== WalCorg Firmware Build Info ==="
	@echo "Compilador : $$($(CC) --version | head -1)"
	@echo "OpenOCD    : $$(openocd --version 2>&1 | head -1)"
	@echo "Chip       : STM32F405RGT6"
	@echo "Flash      : 1MB @ 0x08010000 (firmware) + 0x08000000 (bootloader)"
	@echo "RAM        : 128KB @ 0x20000000"
	@echo "Stack      : 8KB"
	@echo "Clock      : 168MHz (HSE 8MHz × PLL)"
	@echo "PRODUCTION : 0 (debug habilitado)"
	@echo "==================================="

# Verificar que el toolchain está instalado correctamente
.PHONY: check-tools
check-tools:
	@echo "Verificando herramientas..."
	@which $(CC)      && echo "✓ $(CC) encontrado"      || echo "✗ $(CC) NO encontrado"
	@which $(OBJCOPY) && echo "✓ $(OBJCOPY) encontrado" || echo "✗ $(OBJCOPY) NO encontrado"
	@which openocd    && echo "✓ openocd encontrado"    || echo "✗ openocd NO encontrado"
	@which $(GDB)     && echo "✓ $(GDB) encontrado"     || echo "✗ $(GDB) NO encontrado"

# Limpiar archivos compilados
.PHONY: clean
clean:
	@echo "Limpiando build..."
	rm -rf $(BUILD_DIR)
	@echo "✓ Build limpiado"

# Mostrar ayuda
.PHONY: help
help:
	@echo "=== WalCorg Firmware — Comandos disponibles ==="
	@echo "  make              → Compilar firmware completo"
	@echo "  make blink        → Compilar solo el test de blink (Fase 1)"
	@echo "  make flash        → Flashear firmware completo"
	@echo "  make flash-blink  → Flashear blink test (primer paso)"
	@echo "  make debug        → Sesión GDB interactiva"
	@echo "  make openocd      → Solo el servidor OpenOCD (terminal separada)"
	@echo "  make size         → Ver uso de Flash y RAM"
	@echo "  make disasm       → Desensamblar el firmware"
	@echo "  make erase        → Borrar toda la Flash"
	@echo "  make check-tools  → Verificar que el toolchain está instalado"
	@echo "  make clean        → Borrar archivos compilados"
	@echo "  make info         → Mostrar configuración del proyecto"
	@echo ""
	@echo "Primer uso (Fase 1):"
	@echo "  1. make check-tools    ← verificar instalación"
	@echo "  2. make blink          ← compilar test"
	@echo "  3. make flash-blink    ← flashear al chip"
	@echo "  4. Observar LED en PC13 parpadeando cada 500ms"
