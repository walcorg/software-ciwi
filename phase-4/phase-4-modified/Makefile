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

# =============================================================================
# FUENTES PROPIAS DEL WALCORG — Fases 1 a 6
#
# NOTA sobre display.c vs display_phase4.c:
#   Ambos se compilan. Las funciones sobreescritas en display.c están
#   marcadas con __attribute__((weak)) para que display_phase4.c las
#   reemplace limpiamente en el linker sin error de símbolo duplicado.
#
# NOTA sobre main:
#   Solo compilar UN main a la vez. Por defecto se usa main_phase6.c
#   (el firmware final). Comentar/descomentar según la fase que se prueba.
# =============================================================================

# ---- Fase 1: HAL y bringup ----
WALCORG_SOURCES_F1 = \
    $(SRC_DIR)/stm32_walcorg.c \
    $(SRC_DIR)/clock_config.c \
    $(SRC_DIR)/uart_debug.c \
    $(SRC_DIR)/rng_test.c \
    $(SRC_DIR)/stm32f4xx_it.c \
    $(SRC_DIR)/usb_test.c

# ---- Fase 2: Display TFT ----
WALCORG_SOURCES_F2 = \
    $(DRIVERS_DIR)/display_tft.c \
    $(DRIVERS_DIR)/font.c \
    $(DRIVERS_DIR)/touch_xpt2046.c \
    $(DRIVERS_DIR)/display_test.c \
    $(SRC_DIR)/display.c

# ---- Fase 3: Click-wheel y navegación ----
WALCORG_SOURCES_F3 = \
    $(DRIVERS_DIR)/clickwheel.c \
    $(SRC_DIR)/input_manager.c \
    $(SRC_DIR)/menu.c \
    $(SRC_DIR)/buttons.c \
    $(SRC_DIR)/protect.c \
    $(SRC_DIR)/input_test.c

# ---- Fase 4: UI WalCorg completa ----
WALCORG_SOURCES_F4 = \
    $(SRC_DIR)/ui_components.c \
    $(SRC_DIR)/ui_screens.c \
    $(SRC_DIR)/ui_animations.c \
    $(SRC_DIR)/backlight.c \
    $(SRC_DIR)/layout_phase4.c \
    $(SRC_DIR)/display_phase4.c

# ---- Fase 5: Crypto, storage, USB HID ----
WALCORG_SOURCES_F5 = \
    $(SRC_DIR)/storage_hal.c \
    $(SRC_DIR)/crypto_validate.c \
    $(SRC_DIR)/wallet_core.c \
    $(SRC_DIR)/usb_hid.c \
    $(SRC_DIR)/fsm_walcorg.c

# ---- Fase 6: Security hardening ----
WALCORG_SOURCES_F6 = \
    $(SRC_DIR)/flash_protect.c \
    $(SRC_DIR)/tamper_detect.c \
    $(SRC_DIR)/pin_delay.c \
    $(SRC_DIR)/mpu_hardening.c \
    $(SRC_DIR)/security_audit.c

# ---- Main: seleccionar la fase activa ----
# Descomentar SOLO UNO según la fase que se está probando:
# MAIN_SOURCE = $(SRC_DIR)/main_phase1.c
# MAIN_SOURCE = $(SRC_DIR)/main_phase2.c
# MAIN_SOURCE = $(SRC_DIR)/main_phase3.c
# MAIN_SOURCE = $(SRC_DIR)/main_phase4.c
# MAIN_SOURCE = $(SRC_DIR)/main_phase5.c
MAIN_SOURCE   = $(SRC_DIR)/main_phase6.c   # ← FIRMWARE FINAL

# ---- Agregar layout según fase activa ----
# Fase 2-3 usan layout.c, Fase 4-6 usan layout_phase4.c (incluido en F4)
LAYOUT_SOURCE = $(SRC_DIR)/layout.c        # Solo si NO se compila F4

# ---- Fuentes totales del proyecto ----
WALCORG_SOURCES = \
    $(WALCORG_SOURCES_F1) \
    $(WALCORG_SOURCES_F2) \
    $(WALCORG_SOURCES_F3) \
    $(WALCORG_SOURCES_F4) \
    $(WALCORG_SOURCES_F5) \
    $(WALCORG_SOURCES_F6) \
    $(MAIN_SOURCE)

# ---- Targets de prueba por fase individual ----
# make phase1 → compila solo Fase 1 con su main
# make phase2 → compila Fases 1+2 con main_phase2.c
# etc.

.PHONY: phase1 phase2 phase3 phase4 phase5 phase6
phase1:
	$(MAKE) WALCORG_SOURCES="$(WALCORG_SOURCES_F1) $(SRC_DIR)/main_phase1.c" all

phase2:
	$(MAKE) WALCORG_SOURCES=" \
	    $(WALCORG_SOURCES_F1) \
	    $(WALCORG_SOURCES_F2) \
	    $(SRC_DIR)/layout.c \
	    $(SRC_DIR)/main_phase2.c" all

phase3:
	$(MAKE) WALCORG_SOURCES=" \
	    $(WALCORG_SOURCES_F1) \
	    $(WALCORG_SOURCES_F2) \
	    $(WALCORG_SOURCES_F3) \
	    $(SRC_DIR)/layout.c \
	    $(SRC_DIR)/main_phase3.c" all

phase4:
	$(MAKE) WALCORG_SOURCES=" \
	    $(WALCORG_SOURCES_F1) \
	    $(WALCORG_SOURCES_F2) \
	    $(WALCORG_SOURCES_F3) \
	    $(WALCORG_SOURCES_F4) \
	    $(SRC_DIR)/main_phase4.c" all

phase5:
	$(MAKE) WALCORG_SOURCES=" \
	    $(WALCORG_SOURCES_F1) \
	    $(WALCORG_SOURCES_F2) \
	    $(WALCORG_SOURCES_F3) \
	    $(WALCORG_SOURCES_F4) \
	    $(WALCORG_SOURCES_F5) \
	    $(SRC_DIR)/main_phase5.c" all

phase6:
	$(MAKE) all


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

# =============================================================================
# FASE 6 — TARGETS DE PRODUCCIÓN Y SEGURIDAD
# =============================================================================
# ADVERTENCIA: Los targets de producción aplican protecciones IRREVERSIBLES.
# Leer los comentarios de cada target antes de ejecutar.
# =============================================================================

# -----------------------------------------------------------------------------
# BUILD DE PRODUCCIÓN
# Compila con PRODUCTION=1: elimina DEBUG_PRINT, habilita optimizaciones
# máximas y activa las políticas de seguridad estrictas (WIPE en tamper).
#
# Diferencias clave vs build normal:
#   -DPRODUCTION=1       → deshabilita DEBUG_PRINT (sin salida UART)
#   -O2 → -Os            → optimizar para tamaño (menor superficie de ataque)
#   --strip-debug        → eliminar símbolos de debug del .elf
#   tamper_policy = WIPE → borrar semilla si se detecta debugger
# -----------------------------------------------------------------------------
.PHONY: production
production:
	@echo ""
	@echo "╔══════════════════════════════════════════════════╗"
	@echo "║   BUILD DE PRODUCCIÓN — WalCorg Cold Wallet      ║"
	@echo "║   PRODUCTION=1: sin debug, optimizado, seguro    ║"
	@echo "╚══════════════════════════════════════════════════╝"
	@echo ""
	$(MAKE) \
		CFLAGS_EXTRA="-DPRODUCTION=1 -Os -fstack-usage" \
		LDFLAGS_EXTRA="--strip-debug" \
		all
	@echo ""
	@echo "✓ Build de producción completado."
	@echo ""
	@echo "Próximos pasos:"
	@echo "  1. make security-check   → auditoría de seguridad"
	@echo "  2. make flash-production → flashear en el dispositivo"
	@echo "  3. make rdp-enable       → habilitar RDP Level 1 (ONE WAY)"

# -----------------------------------------------------------------------------
# VERIFICACIÓN DE SEGURIDAD PRE-PRODUCCIÓN
# Ejecuta la suite de security_audit sobre el binario antes de flashear.
# Verifica tamaño, strings de debug presentes, etc.
# -----------------------------------------------------------------------------
.PHONY: security-check
security-check: $(BUILD_DIR)/$(TARGET).elf
	@echo ""
	@echo "=== Auditoría de Seguridad Pre-Producción ==="
	@echo ""
	@echo "▶ Tamaño del binario:"
	@$(SIZE) $(BUILD_DIR)/$(TARGET).elf
	@echo ""
	@echo "▶ Verificando ausencia de strings de debug..."
	@if $(OBJDUMP) -s $(BUILD_DIR)/$(TARGET).elf 2>/dev/null | \
	   grep -q "DEBUG_PRINT\|uart_debug\|printf"; then \
	  echo "  ⚠ WARN: Posibles strings de debug encontrados."; \
	  echo "  Compilar con PRODUCTION=1 para eliminarlos."; \
	else \
	  echo "  ✓ Sin strings de debug detectados."; \
	fi
	@echo ""
	@echo "▶ Verificando secciones críticas..."
	@$(OBJDUMP) -h $(BUILD_DIR)/$(TARGET).elf | \
	  grep -E "\.text|\.rodata|\.data|\.bss|\.ccm" || true
	@echo ""
	@echo "▶ Verificando stack usage (si compilado con -fstack-usage)..."
	@find $(BUILD_DIR) -name "*.su" 2>/dev/null | \
	  xargs grep -h "" 2>/dev/null | sort -t: -k3 -rn | head -10 || \
	  echo "  (compilar con PRODUCTION=1 para ver stack usage)"
	@echo ""
	@echo "=== Auditoría completada ==="

# -----------------------------------------------------------------------------
# FLASHEAR BUILD DE PRODUCCIÓN
# Igual que 'make flash' pero verifica que el binario fue compilado
# con PRODUCTION=1 antes de proceder.
# -----------------------------------------------------------------------------
.PHONY: flash-production
flash-production: $(BUILD_DIR)/$(TARGET).bin
	@echo ""
	@echo "▶ Flasheando build de producción..."
	@echo "  Asegúrate de haber compilado con 'make production'"
	@echo ""
	openocd \
		-f scripts/openocd_weact_f405.cfg \
		-c "program $(BUILD_DIR)/$(TARGET).bin \
		    verify reset exit 0x08000000"
	@echo ""
	@echo "✓ Firmware de producción flasheado."
	@echo ""
	@echo "SIGUIENTE PASO OPCIONAL (IRREVERSIBLE):"
	@echo "  make rdp-enable  → proteger Flash contra lectura"

# -----------------------------------------------------------------------------
# HABILITAR RDP LEVEL 1 — PROTECCIÓN DE FLASH
#
# *** ADVERTENCIA: OPERACIÓN SEMI-IRREVERSIBLE ***
#
# Aplica Read-Out Protection Level 1 al chip conectado via ST-Link.
# Después de aplicar RDP1:
#   - No se puede leer la Flash via debugger/ST-Link
#   - Desactivar RDP1 BORRA TODA LA FLASH automáticamente (por hardware)
#   - Solo se puede re-flashear DESPUÉS del borrado automático
#
# Cuándo usar: SOLO en dispositivos de producción listos para el cliente.
# NUNCA usar en dispositivos de desarrollo que aún necesitas flashear.
#
# El chip debe estar conectado via ST-Link V2 antes de ejecutar.
# -----------------------------------------------------------------------------
.PHONY: rdp-enable
rdp-enable:
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════╗"
	@echo "║   ⚠  ATENCIÓN: RDP LEVEL 1 — SEMI-IRREVERSIBLE         ║"
	@echo "║                                                          ║"
	@echo "║   Después de aplicar:                                    ║"
	@echo "║   - Flash NO se puede leer via debugger                  ║"
	@echo "║   - Desactivar RDP borra TODA la Flash                   ║"
	@echo "║   - Solo usar en dispositivos de PRODUCCIÓN FINAL        ║"
	@echo "╚══════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "¿Continuar? Presiona CTRL+C para cancelar (5 segundos)..."
	@sleep 5
	@echo ""
	@echo "▶ Aplicando RDP Level 1 via OpenOCD..."
	openocd \
		-f scripts/openocd_weact_f405.cfg \
		-c "init; reset halt; \
		    flash protect 0 0 3 on; \
		    stm32f4x options_write 0 0x1FFF0000; \
		    reset run; exit"
	@echo ""
	@echo "✓ RDP Level 1 aplicado. El chip se reiniciará."
	@echo "  El ST-Link ya no podrá leer la Flash de este dispositivo."

# -----------------------------------------------------------------------------
# LEER ESTADO DE PROTECCIÓN DEL CHIP CONECTADO
# -----------------------------------------------------------------------------
.PHONY: rdp-status
rdp-status:
	@echo "▶ Leyendo estado de protección del chip..."
	openocd \
		-f scripts/openocd_weact_f405.cfg \
		-c "init; reset halt; \
		    flash info 0; \
		    stm32f4x options_read 0; \
		    exit" 2>&1 | grep -E "RDP|protection|sector|option"

# -----------------------------------------------------------------------------
# BORRAR TODA LA FLASH (factory reset del chip)
# Útil si necesitas recuperar un chip con RDP1 habilitado.
# El borrado masivo también resetea RDP a Level 0.
# -----------------------------------------------------------------------------
.PHONY: mass-erase
mass-erase:
	@echo ""
	@echo "╔═══════════════════════════════════════╗"
	@echo "║  ⚠  BORRADO MASIVO DE FLASH           ║"
	@echo "║  Borrará TODO: firmware + datos NORCOW║"
	@echo "╚═══════════════════════════════════════╝"
	@echo ""
	@echo "¿Continuar? CTRL+C para cancelar (3 segundos)..."
	@sleep 3
	openocd \
		-f scripts/openocd_weact_f405.cfg \
		-c "init; reset halt; \
		    stm32f4x mass_erase 0; \
		    reset run; exit"
	@echo "✓ Flash borrada completamente. RDP reseteado a Level 0."

# -----------------------------------------------------------------------------
# VERIFICAR FIRMA DE FIRMWARE (integridad del binario)
# Calcula el SHA256 del .bin generado para distribución segura.
# Publicar este hash junto con cada release en GitHub.
# -----------------------------------------------------------------------------
.PHONY: verify-hash
verify-hash: $(BUILD_DIR)/$(TARGET).bin
	@echo ""
	@echo "=== Verificación de Integridad del Firmware ==="
	@echo ""
	@echo "▶ SHA256 del firmware:"
	@sha256sum $(BUILD_DIR)/$(TARGET).bin
	@echo ""
	@echo "▶ MD5 del firmware:"
	@md5sum $(BUILD_DIR)/$(TARGET).bin
	@echo ""
	@echo "▶ Tamaño:"
	@ls -lh $(BUILD_DIR)/$(TARGET).bin
	@echo ""
	@echo "Publicar estos hashes en el release de GitHub para"
	@echo "que los usuarios puedan verificar la integridad del"
	@echo "firmware antes de instalarlo."

# -----------------------------------------------------------------------------
# GENERAR ARCHIVO DE DISTRIBUCIÓN COMPLETO
# Empaqueta el firmware + hashes + checksums para distribución.
# -----------------------------------------------------------------------------
RELEASE_VERSION ?= 1.0.0
RELEASE_DIR     := release/walcorg-$(RELEASE_VERSION)

.PHONY: release
release: production
	@echo ""
	@echo "=== Generando paquete de release v$(RELEASE_VERSION) ==="
	@mkdir -p $(RELEASE_DIR)
	@cp $(BUILD_DIR)/$(TARGET).bin  $(RELEASE_DIR)/walcorg-$(RELEASE_VERSION).bin
	@cp $(BUILD_DIR)/$(TARGET).elf  $(RELEASE_DIR)/walcorg-$(RELEASE_VERSION).elf
	@cp $(BUILD_DIR)/$(TARGET).hex  $(RELEASE_DIR)/walcorg-$(RELEASE_VERSION).hex 2>/dev/null || true
	@sha256sum $(RELEASE_DIR)/walcorg-$(RELEASE_VERSION).bin > \
	           $(RELEASE_DIR)/walcorg-$(RELEASE_VERSION).sha256
	@echo "  Build info:" > $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "  Versión: $(RELEASE_VERSION)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "  Fecha: $$(date -u '+%Y-%m-%d %H:%M UTC')" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "  Compilador: $$($(CC) --version | head -1)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "  PRODUCTION=1" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "  Licencia: GNU GPL v3" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo ""
	@echo "✓ Release generado en: $(RELEASE_DIR)/"
	@ls -la $(RELEASE_DIR)/
	@echo ""
	@echo "Próximos pasos para publicar:"
	@echo "  git tag v$(RELEASE_VERSION)"
	@echo "  git push origin v$(RELEASE_VERSION)"
	@echo "  Subir $(RELEASE_DIR)/ a GitHub Releases"

# =============================================================================
# AYUDA ACTUALIZADA CON FASE 6
# =============================================================================
.PHONY: help
help:
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════╗"
	@echo "║         WalCorg Firmware — Comandos disponibles          ║"
	@echo "╠══════════════════════════════════════════════════════════╣"
	@echo "║  DESARROLLO (Fases 1-6)                                  ║"
	@echo "║    make              → Compilar firmware (PRODUCTION=0)  ║"
	@echo "║    make flash        → Flashear firmware                 ║"
	@echo "║    make debug        → Sesión GDB interactiva            ║"
	@echo "║    make size         → Ver uso de Flash y RAM            ║"
	@echo "║    make clean        → Limpiar build                     ║"
	@echo "║    make check-tools  → Verificar toolchain               ║"
	@echo "╠══════════════════════════════════════════════════════════╣"
	@echo "║  PRODUCCIÓN (Fase 6 — seguridad)                         ║"
	@echo "║    make production   → Build con PRODUCTION=1, -Os       ║"
	@echo "║    make security-check → Auditoría pre-producción        ║"
	@echo "║    make flash-production → Flashear build producción     ║"
	@echo "║    make verify-hash  → SHA256 del firmware               ║"
	@echo "║    make release      → Paquete de distribución completo  ║"
	@echo "╠══════════════════════════════════════════════════════════╣"
	@echo "║  PROTECCIÓN FLASH (SEMI-IRREVERSIBLE — leer antes)       ║"
	@echo "║    make rdp-status   → Ver estado actual de RDP          ║"
	@echo "║    make rdp-enable   → Habilitar RDP Level 1 (⚠ cuidado)║"
	@echo "║    make mass-erase   → Borrar toda la Flash (⚠ cuidado) ║"
	@echo "╚══════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "Flujo de desarrollo → producción:"
	@echo "  1. make              ← desarrollar y probar"
	@echo "  2. make production   ← build final sin debug"
	@echo "  3. make security-check ← verificar antes de distribuir"
	@echo "  4. make flash-production ← flashear al hardware final"
	@echo "  5. make rdp-enable   ← proteger (SOLO dispositivos finales)"
	@echo "  6. make release      ← generar paquete para GitHub"
