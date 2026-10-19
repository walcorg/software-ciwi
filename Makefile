# =============================================================================
# software-ciwi-v0.2 — Makefile
# Target: STM32F405RGT6 (WeAct Core Board / CiWi)
#
#   make                 → fase 1 (por defecto)
#   make PHASE=1         → firmware de la fase 1 (build/phase1/ciwi-phase1.elf/.bin/.hex)
#   make blink           → prueba mínima de LED (build/blink/ciwi-blink.*)
#   make flash PHASE=1   → carga con OpenOCD + ST-Link
#   make flash-blink
#   make size PHASE=1
#   make clean
#
# Nota: en Make, "\" debe ser el ÚLTIMO carácter de la línea.
#       Los comentarios van en su propia línea, nunca después de "\".
# =============================================================================

PHASE ?= 1

# ---------------------------------------------------------------- toolchain
PREFIX  := arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size
OPENOCD ?= openocd

# ---------------------------------------------------------------- vendor (tools/fetch_vendor.sh)
VENDOR      := vendor
HAL_DIR     := $(VENDOR)/stm32f4xx_hal_driver
CMSIS_DEV   := $(VENDOR)/cmsis_device_f4
CMSIS_CORE  := $(VENDOR)/cmsis_core
TREZOR_DIR  := $(VENDOR)/trezor-firmware

# ---------------------------------------------------------------- MCU
MCU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS      := -DSTM32F405xx -DUSE_HAL_DRIVER -DHSE_VALUE=8000000U -DPRODUCTION=0

# ---------------------------------------------------------------- flags
OPT   ?= -Og
WARN  := -Wall -Wextra -Wno-unused-parameter
CFLAGS = $(MCU_FLAGS) $(DEFS) $(OPT) $(WARN) -g3 -std=gnu11 \
         -ffunction-sections -fdata-sections -fno-common \
         $(INCLUDES)

# LAYOUT=standalone → firmware en 0x08000000 (tarjeta de desarrollo, sin bootloader)
# LAYOUT=bootloader → firmware en 0x08010000 (requiere bootloader en 0x08000000)
LAYOUT ?= standalone
LDSCRIPT := platform/stm32f405rgt6_$(LAYOUT).ld
ifeq ($(LAYOUT),bootloader)
DEFS += -DUSER_VECT_TAB_ADDRESS -DVECT_TAB_OFFSET=0x00010000U
endif
LDFLAGS   = $(MCU_FLAGS) -T$(LDSCRIPT) -Wl,--gc-sections \
            -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--print-memory-usage \
            -Wl,--no-warn-rwx-segments \
            -specs=nano.specs -specs=nosys.specs -lc -lm -lnosys

# ---------------------------------------------------------------- HAL
HAL_MODULES := hal hal_cortex hal_gpio hal_rcc hal_rcc_ex hal_dma hal_dma_ex \
               hal_pwr hal_pwr_ex hal_i2c hal_spi hal_uart hal_pcd hal_pcd_ex \
               ll_usb hal_flash hal_flash_ex hal_rng hal_tim hal_tim_ex
HAL_SRCS := $(addprefix $(HAL_DIR)/Src/stm32f4xx_,$(addsuffix .c,$(HAL_MODULES)))

COMMON_INCLUDES := -Iplatform -Idrivers/clickwheel \
                   -I$(HAL_DIR)/Inc -I$(CMSIS_DEV)/Include -I$(CMSIS_CORE)/CMSIS/Core/Include

PLATFORM_SRCS := platform/clock_config.c platform/uart_debug.c platform/stm32f4xx_it.c \
                 platform/system_stm32f4xx.c platform/stm32_walcorg.c
STARTUP       := platform/startup_stm32f405xx.s

# ---------------------------------------------------------------- phases
ifeq ($(PHASE),1)
PHASE_DIRS := phases/phase1
PHASE_SRCS := phases/phase1/main_phase1.c phases/phase1/rng_test.c \
              phases/phase1/usb_test.c drivers/clickwheel/clickwheel.c
endif

DISPLAY_SRCS := drivers/display/display_tft.c drivers/display/font.c drivers/touch/touch_xpt2046.c
UI_INPUT_SRCS := ui/input_manager.c ui/menu.c drivers/clickwheel/clickwheel.c
UI_SRCS := ui/ui_components.c ui/ui_screens.c ui/ui_animations.c drivers/backlight/backlight.c
WALLET_SRCS := wallet/crypto_validate.c wallet/storage_hal.c wallet/usb_hid.c wallet/wallet_core.c \
               wallet/fsm_walcorg.c wallet/signing_walcorg.c wallet/trezor_rand_hw.c
SECURITY_SRCS := security/flash_protect.c security/mpu_hardening.c security/pin_delay.c \
                 security/security_audit.c security/tamper_detect.c

# trezor-crypto (solo la biblioteca criptográfica, independiente del hardware)
TREZOR_CRYPTO := $(TREZOR_DIR)/crypto
CRYPTO_SRCS := $(addprefix $(TREZOR_CRYPTO)/,bignum.c ecdsa.c curves.c secp256k1.c nist256p1.c \
               bip32.c bip39.c bip39_english.c sha2.c sha3.c ripemd160.c hmac.c pbkdf2.c \
               base58.c segwit_addr.c address.c hasher.c blake256.c blake2b.c groestl.c \
               memzero.c consteq.c rand.c rfc6979.c hmac_drbg.c \
               aes/aescrypt.c aes/aeskey.c aes/aestab.c aes/aes_modes.c \
               ed25519-donna/ed25519.c ed25519-donna/curve25519-donna-32bit.c \
               ed25519-donna/curve25519-donna-helpers.c ed25519-donna/modm-donna-32bit.c \
               ed25519-donna/ed25519-donna-basepoint-table.c ed25519-donna/ed25519-donna-32bit-tables.c \
               ed25519-donna/ed25519-donna-impl-base.c ed25519-donna/ed25519-keccak.c \
               ed25519-donna/ed25519-sha3.c ed25519-donna/curve25519-donna-scalarmult-base.c)

ifeq ($(PHASE),2)
PHASE_DIRS := phases/phase2
PHASE_SRCS := phases/phase2/main_phase2.c phases/phase2/display_test.c phases/phase1/rng_test.c \
              $(DISPLAY_SRCS)
endif
ifeq ($(PHASE),3)
PHASE_DIRS := phases/phase3 drivers/compat/phase2
PHASE_SRCS := phases/phase3/main_phase3.c phases/phase3/input_test.c phases/phase1/rng_test.c \
              $(DISPLAY_SRCS) $(UI_INPUT_SRCS) drivers/compat/phase2/display.c
endif
ifeq ($(PHASE),4)
PHASE_DIRS := phases/phase4 drivers/compat/phase4
PHASE_SRCS := phases/phase4/main_phase4.c phases/phase1/rng_test.c \
              $(DISPLAY_SRCS) $(UI_INPUT_SRCS) $(UI_SRCS) drivers/compat/phase4/display.c
endif
ifeq ($(PHASE),5)
PHASE_DIRS := phases/phase5 drivers/compat/phase4 $(TREZOR_CRYPTO)
PHASE_SRCS := phases/phase5/main_phase5.c phases/phase1/rng_test.c \
              $(DISPLAY_SRCS) $(UI_INPUT_SRCS) $(UI_SRCS) $(WALLET_SRCS) security/tamper_detect.c security/flash_protect.c \
              $(CRYPTO_SRCS)
endif
ifeq ($(PHASE),6)
PHASE_DIRS := phases/phase6 drivers/compat/phase4 $(TREZOR_CRYPTO)
PHASE_SRCS := phases/phase6/main_phase6.c phases/phase1/rng_test.c \
              $(DISPLAY_SRCS) $(UI_INPUT_SRCS) $(UI_SRCS) $(WALLET_SRCS) $(SECURITY_SRCS) \
              $(CRYPTO_SRCS)
endif

INCLUDES := $(COMMON_INCLUDES) -Iphases/phase1 -Idrivers/display -Idrivers/touch -Idrivers/backlight \
            -Iui -Iwallet -Isecurity $(addprefix -I,$(PHASE_DIRS))

TARGET ?= ciwi-phase$(PHASE)
BUILD  ?= build/phase$(PHASE)

SRCS := $(PLATFORM_SRCS) $(PHASE_SRCS) $(HAL_SRCS)
OBJS := $(addprefix $(BUILD)/,$(SRCS:.c=.o)) $(BUILD)/$(STARTUP:.s=.o)

# ---------------------------------------------------------------- rules
.PHONY: all blink flash flash-blink size clean check-vendor

all: check-vendor $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex
	@$(SIZE) $(BUILD)/$(TARGET).elf

check-vendor:
	@test -d $(HAL_DIR)/Src || (echo "Falta vendor/: ejecuta tools/fetch_vendor.sh" && false)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(MCU_FLAGS) -x assembler-with-cpp -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

blink:
	$(MAKE) TARGET=ciwi-blink BUILD=build/blink PHASE=blink \
	        PHASE_SRCS=phases/phase1/blink_test.c PLATFORM_SRCS=platform/system_stm32f4xx.c \
	        PHASE_DIRS=phases/phase1 all

flash: all
	$(OPENOCD) -f tools/openocd_weact_f405.cfg -c "program $(BUILD)/$(TARGET).elf verify reset exit"

flash-blink:
	$(MAKE) blink
	$(OPENOCD) -f tools/openocd_weact_f405.cfg -c "program build/blink/ciwi-blink.elf verify reset exit"

size: $(BUILD)/$(TARGET).elf
	$(SIZE) $<

clean:
	rm -rf build
