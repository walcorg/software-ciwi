# =============================================================================
# software-ciwi v0.5 — Makefile
# Target: STM32F405RGT6 (WeAct Core Board / CiWi)
#
#   make                 → phase 1 (default)
#   make PHASE=1         → phase 1 firmware (build/phase1/ciwi-phase1.elf/.bin/.hex)
#   make PHASE=7         → phase 7: TROPIC01 secure element + Anchor Protocol
#   make PHASE=8         → phase 8: the CiWi interface (portrait, wheel, hold to sign)
#   make blink           → minimal LED test (build/blink/ciwi-blink.*)
#   make flash PHASE=1   → flash with OpenOCD + ST-Link
#   make flash-blink
#   make size PHASE=1
#   make clean
#
# Note: in Make, "\" must be the LAST character on the line.
#       Comments go on their own line, never after "\".
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

# Extra defines from the command line, e.g. make PHASE=7 EXTRA_DEFS="-DCIWI_TR01_APPLY_RCONFIG=1"
EXTRA_DEFS ?=
DEFS += $(EXTRA_DEFS)

# ---------------------------------------------------------------- flags
OPT   ?= -Og
WARN  := -Wall -Wextra -Wno-unused-parameter
CFLAGS = $(MCU_FLAGS) $(DEFS) $(OPT) $(WARN) -g3 -std=gnu11 \
         -ffunction-sections -fdata-sections -fno-common \
         $(INCLUDES)

# LAYOUT=standalone → firmware at 0x08000000 (dev board, no bootloader)
# LAYOUT=bootloader → firmware at 0x08010000 (requires a bootloader at 0x08000000)
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

DISPLAY_SRCS := drivers/display/display_tft.c drivers/display/font.c drivers/display/font5x7.c drivers/touch/touch_xpt2046.c
UI_INPUT_SRCS := ui/input_manager.c ui/menu.c drivers/clickwheel/clickwheel.c
UI_SRCS := ui/ui_components.c ui/ui_screens.c ui/ui_animations.c drivers/backlight/backlight.c
WALLET_SRCS := wallet/crypto_validate.c wallet/storage_hal.c wallet/usb_hid.c wallet/wallet_core.c \
               wallet/fsm_walcorg.c wallet/signing_walcorg.c wallet/trezor_rand_hw.c
SECURITY_SRCS := security/flash_protect.c security/mpu_hardening.c security/pin_delay.c \
                 security/security_audit.c security/tamper_detect.c

# trezor-crypto (crypto library only; hardware independent)
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

# ---------------------------------------------------------------- phase 7: TROPIC01 + Anchor
# libtropic (Tropic Square), configured like its CMake defaults; logging: errors only.
LT_DIR  := $(VENDOR)/libtropic
LT_DEFS := -DLT_HELPERS -DLT_SILICON_REV_ACAB -DLT_LOG_ENABLE_ERROR=1 -DLT_LOG_ENABLE_WARN=0 \
           -DLT_LOG_ENABLE_INFO=0 -DLT_LOG_ENABLE_DEBUG=0 -DLT_CRC_ERR_RETRY_ATTEMPTS=3 \
           -DLT_L1_READ_MAX_TRIES=50 -DLT_L1_READ_RETRY_DELAY_MS=25 -DLT_L1_SPI_TIMEOUT_MS=70 \
           -DLT_L1_INT_TIMEOUT_MS=200 -DLT_HAVE_EXPLICIT_BZERO -DAES_VAR
LT_SRCS := $(addprefix $(LT_DIR)/src/,libtropic.c lt_crc16.c lt_port_wrap.c lt_l1.c libtropic_l2.c \
           lt_l2_frame_check.c lt_l3_process.c libtropic_l3.c lt_hkdf.c lt_asn1_der.c \
           libtropic_default_sh0_keys.c lt_tr01_attrs.c libtropic_secure_memzero.c) \
           $(addprefix $(LT_DIR)/cal/trezor_crypto/,lt_trezor_crypto_common.c lt_trezor_crypto_aesgcm.c \
           lt_trezor_crypto_sha256.c lt_trezor_crypto_hmac_sha256.c lt_trezor_crypto_x25519.c) \
           $(LT_DIR)/hal/stm32/stm32f4xx/libtropic_port_stm32f4xx.c
TROPIC_SRCS := tropic/tr01_ciwi.c tropic/tr01_pin.c tropic/tr01_platform_stm32.c \
               anchor/anchor_eip712.c anchor/anchor_ciwi.c anchor/anchor_selftest.c
ifeq ($(PHASE),7)
DEFS += $(LT_DEFS)
PHASE_DIRS := phases/phase7 tropic anchor $(TREZOR_CRYPTO) $(TREZOR_CRYPTO)/aes \
              $(LT_DIR)/include $(LT_DIR)/src $(LT_DIR)/cal/trezor_crypto $(LT_DIR)/hal/stm32/stm32f4xx
PHASE_SRCS := phases/phase7/main_phase7.c phases/phase1/rng_test.c wallet/storage_hal.c wallet/trezor_rand_hw.c \
              $(TROPIC_SRCS) $(LT_SRCS) $(CRYPTO_SRCS) \
              $(TREZOR_CRYPTO)/aes/aesgcm.c $(TREZOR_CRYPTO)/aes/gf128mul.c
endif

# ---------------------------------------------------------------- phase 8: the CiWi interface
CIWI_UI_SRCS := ciwi_ui/ui_app.c ciwi_ui/ui_gfx.c ciwi_ui/ui_input.c ciwi_ui/ui_strings.c \
                ciwi_ui/ui_gfx_tft.c
ifeq ($(PHASE),8)
DEFS += $(LT_DEFS)
PHASE_DIRS := phases/phase8 ciwi_ui tropic anchor $(TREZOR_CRYPTO) $(TREZOR_CRYPTO)/aes \
              $(LT_DIR)/include $(LT_DIR)/src $(LT_DIR)/cal/trezor_crypto $(LT_DIR)/hal/stm32/stm32f4xx
PHASE_SRCS := phases/phase8/main_phase8.c phases/phase8/ui_platform_stm32.c phases/phase1/rng_test.c \
              wallet/storage_hal.c wallet/trezor_rand_hw.c $(CIWI_UI_SRCS) \
              $(DISPLAY_SRCS) drivers/backlight/backlight.c \
              $(TROPIC_SRCS) $(LT_SRCS) $(CRYPTO_SRCS) \
              $(TREZOR_CRYPTO)/aes/aesgcm.c $(TREZOR_CRYPTO)/aes/gf128mul.c
endif

INCLUDES := $(COMMON_INCLUDES) -Iphases/phase1 -Idrivers/display -Idrivers/touch -Idrivers/backlight \
            -Iui -Iwallet -Isecurity $(addprefix -I,$(PHASE_DIRS))

TARGET ?= ciwi-phase$(PHASE)
BUILD  ?= build/phase$(PHASE)

SRCS := $(PLATFORM_SRCS) $(PHASE_SRCS) $(HAL_SRCS)
OBJS := $(addprefix $(BUILD)/,$(SRCS:.c=.o)) $(BUILD)/$(STARTUP:.s=.o)

# ---------------------------------------------------------------- rules
.PHONY: all blink flash flash-blink size clean check-vendor erase-storage

all: check-vendor $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex
	@$(SIZE) $(BUILD)/$(TARGET).elf

check-vendor:
	@test -d $(HAL_DIR)/Src || (echo "vendor/ missing: run tools/fetch_vendor.sh" && false)

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

# Erase the NORCOW storage sectors 9-11 (wallet PIN/seed and the TROPIC01 pairing key).
# Only for a development board whose TROPIC01 was replaced: the old chip cannot be used again.
erase-storage:
	$(OPENOCD) -f tools/openocd_weact_f405.cfg -c "init; reset halt; flash erase_sector 0 9 11; reset; exit"

clean:
	rm -rf build
