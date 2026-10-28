# Changes from v0.3 to v0.4

## New: phase 8, the CiWi interface

Implements the interface specification (portrait, click-wheel, hold to sign, optional touch).

| Area | Files | What it does |
| --- | --- | --- |
| Input events | `ciwi_ui/ui_input.{c,h}` | ROTATE, SELECT, HOLD_PROGRESS / HOLD_DONE / HOLD_CANCEL (2 s), BACK, HOME (MENU 2 s), TOUCH_TAP, TOUCH_DRAG; every event carries its source |
| Screens | `ciwi_ui/ui_app.{c,h}` | Home, Anchor menu, Settings, lock and auto-lock, PIN (digit wheel with random start, shuffled keypad with touch), signing confirmation in pages, messages |
| Signing rule | `ui_run_confirm()` | Approve only with the physical centre button held 2 s on the last page; touch and emulated input can read and reject; invalid shield data can only be rejected; auto-lock rejects |
| Drawing | `ciwi_ui/ui_gfx.{c,h}`, `ciwi_ui/ui_gfx_tft.c` | Zones, rounded boxes, rings, UTF-8 text with Spanish characters; two-function backend (ILI9488 or framebuffer) |
| Languages | `ciwi_ui/ui_strings.{c,h}` | Spanish and English, switchable in Settings |
| Phase 8 | `phases/phase8/` | KEY = centre button, UART keys for wheel and MENU, XPT2046 touch rotated to portrait, settings in NORCOW, TROPIC01 PIN, Anchor self-test with the confirmation screens, DEMO mode without TROPIC01 |
| Tests | `tests/ui/` | The interface on a PC with scripted input: 16 checks and screenshots |

## Other

- `drivers/display/font5x7.c`: the font table moved out of `font.c` (no hardware dependency), so
  the interface tests can use it.
- `tests/host/Makefile`: waits 4 s for the TROPIC01 model to start.

# Changes from v0.2 to v0.3

Every change in existing files is marked `[v0.3]` in the source code.

## New: phase 7, TROPIC01 and the Anchor Protocol

| Area | Files | What it does |
| --- | --- | --- |
| TROPIC01 driver | `tropic/tr01_ciwi.{c,h}` | Bring-up, chip info, sessions, idempotent provisioning (SH0 → CiWi key in slot 1, P-256 anchor key in ECC slot 0, claim counter), optional access policy (R-config), optional and irreversible SH0 invalidation, low-s signatures verified before release |
| PIN | `tropic/tr01_pin.{c,h}` | MAC-and-Destroy PIN, 10 attempts, bound to the STM32 unique ID; releases a 32-byte key for the seed |
| STM32 port | `tropic/tr01_platform_stm32.c` | SPI2 on PB13/PB14/PB15, CS PB12, GPO PB10; pairing key in NORCOW; uses libtropic's STM32F4 port |
| Anchor Protocol v0.2 | `anchor/anchor_eip712.{c,h}`, `anchor/anchor_ciwi.{c,h}` | EIP-712 hashing of every message, shield signature checks, Claim / Bind / NftClaim signing with TROPIC01 |
| Self-test | `anchor/anchor_selftest.{c,h}`, `phases/phase7/main_phase7.c` | End-to-end run with a test shield key, KEY-button confirmation, JSON vectors on the UART |
| Host test bench | `tests/host/` | Same sources on a PC against the TROPIC01 model; `verify.js` checks the vectors with ethers.js and @noble/curves |
| Build | `Makefile` (`PHASE=7`, `EXTRA_DEFS`, `erase-storage`), `tools/fetch_vendor.sh` (libtropic v4.2.0, pinned) | |

## Bugs fixed in existing code

| # | Problem | Effect | Fix |
| --- | --- | --- | --- |
| 18 | `stm32_walcorg.c`: the minimal MPU makes all flash read-only and has no background region | On the board, every NORCOW write (PIN in phase 5, pairing key in phase 7) and every read of the unique ID would raise a MemManage fault | Region 4: storage sectors 9-11 read/write, not executable; region 5: system memory/UID read-only |
| 19 | `storage_hal.c`: `_norcow_find()` returned the **first** entry for a key | NORCOW is copy-on-write, so updates (fail counter, flags) were written but never read back | Returns the last entry |
| 20 | LED on PC13 in `main_phase2.c` to `main_phase6.c` | Same problem as #5 in phases 2-6: PC13 is KEY; driving it as an output against the button | LED on PB2 |
| 21 | "WalCorg" in strings and comments | Brand spelling | "WALCORG" |

## Found, not changed

- `security/mpu_hardening.c` (phase 6): region 2 is 512 KB at `0x080A0000`, which is not aligned to
  its size, so the MPU uses `0x08080000` and the region also covers sector 8 (code).
- libtropic `examples/model/mac_and_destroy`: after a correct PIN, the slots are re-armed only up to
  `MACANDD_ROUNDS - 2`, so the top slot stays destroyed and the next correct PIN fails. Confirmed on the
  model; `tr01_pin.c` re-arms every used slot. To report to Tropic Square.

## Other

- `storage_hal`: generic `storage_put()` / `storage_get()` for apps other than the wallet.
- `dist/`: phase 7 binaries and `THIRD-PARTY-NOTICES.txt` (libtropic Clear BSD notice).

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
