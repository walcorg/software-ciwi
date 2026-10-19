# Changes from v0.1 (original draft) to v0.2

Every change is marked `[v0.2]` in the source code.

## Errors that prevented building or booting

| # | Problem | Effect | Fix |
| --- | --- | --- | --- |
| 1 | All 3 Makefiles had comments after a line-continuation `\` | `make` stopped at line 87 (`missing separator`) before compiling anything | New single Makefile with `PHASE=1..6` |
| 2 | Variables with trailing comments (`BUILD_DIR = build  # ...`) | Paths with trailing spaces | Removed |
| 3 | The Makefile expected folders that did not exist (`src/`, `drivers/`, `vendor/`...) and phase 1 only listed `clickwheel.c` | Nothing to compile | New layout and per-phase source lists |
| 4 | **Linker origin at 0x08010000** with no bootloader | The board **would not boot** (the STM32 reads the vector table at 0x08000000) | `stm32f405rgt6_standalone.ld` (0x08000000, default) and `stm32f405rgt6_bootloader.ld` (0x08010000 + `VECT_TAB_OFFSET`) |
| 5 | **LED on PC13** | On the WeAct F405, PC13 is the KEY button; the LED is on **PB2**. Neither the blink nor the error indication would have been visible | LED on PB2 in `blink_test.c`, `main_phase1.c` and `Error_Handler()` |
| 6 | `stm32f4xx_hal_conf.h` lacked `assert_param`, `EXTERNAL_CLOCK_VALUE`, `LSI_VALUE`, `VDD_VALUE`... | The ST HAL did not compile or link | Added from ST's template |
| 7 | `trezor_makefile.patch` was corrupt (`git apply`: line 36) and its context does not exist in Trezor's current `Makefile.include` | Could not be applied | No longer used: the build uses the ST HAL plus trezor-crypto only (moved to `docs/original-notes/`) |
| 8 | `touch_xpt2046.h` was missing | Phases 2-6 did not compile | Reconstructed from the `.c` file |
| 9 | `OTG_FS_IRQHandler` required `hpcd_USB_OTG_FS` in every phase | Phases 2-4 did not link | Weak definition in `stm32f4xx_it.c` |
| 10 | `menu.h` used `input_action_t` without including `input_manager.h`; `WALCORG_DARKGRAY` was undefined | Phase 3 did not compile | Include and color added |
| 11 | `wallet_core.c` and `crypto_validate.c` used `secp256k1` without `#include "secp256k1.h"` | Phase 5 did not compile | Include added |
| 12 | `ecdsa_sig_to_der(sig, der, &len)`: outdated function signature | Phase 5 did not compile; the current API returns the length | `der_len = ecdsa_sig_to_der(sig, der)` |
| 13 | `signing_walcorg.c` used `INPUT_CONFIRM` without including `input_manager.h` | Phase 5 did not compile | Include added |
| 14 | `fsm_walcorg.c` called `rng_get_uint32()`, which does not exist | Phase 5 did not link | Uses `rng_get32()` |
| 15 | trezor-crypto needs `random_buffer()` and `tc_fault_handler()` from the platform | Phase 5 did not link | `wallet/trezor_rand_hw.c` (hardware RNG; halts on failure) |

## Behavior bugs fixed

| # | Problem | Fix |
| --- | --- | --- |
| 16 | `RGB565(r,g,b)` macro did not parenthesize its arguments: `RGB565(x, y, z + 10)` computed `z + (10 >> 3)` | Arguments parenthesized |
| 17 | `main_phase2/4/5.c` used `snprintf`, `memset`, `strtok`... without `#include` | `<stdio.h>` and `<string.h>` added |

## Other

- `tools/openocd_weact_f405.cfg`: `interface hla` / `hla_layout` (deprecated) → `source [find interface/stlink.cfg]`. Checked with OpenOCD 0.12.
- `tools/fetch_vendor.sh`: downloads the ST HAL, CMSIS and trezor-crypto at pinned commits.
- The "modified" files that replace Trezor legacy files moved to `trezor-integration/` (not built).
- The `display.c`/`display.h` compatibility layer (Trezor's display API on top of the ILI9488) is built
  in phase 3 (phase-2 version) and phases 4-6 (phase-4 version): `drivers/compat/`.
- Binaries in `dist/` differ between builds only by the embedded build timestamp (`__DATE__`/`__TIME__`).

## Inaccurate comments found (code unchanged)

- `wallet/usb_hid.h`: the header table lists message numbers that do not match the `enum` (the `enum` is correct).
- `wallet/storage_hal.h`: the comment places sector 10 at `0x08080000`; the `#define` (`0x080C0000`) is correct.
