# software-ciwi

Test firmware for the **CiWi** cold wallet (WALCORG) on an **STM32F405RGT6** board (WeAct STM32F4 64-pin Core Board architecture),
based on a fork of the Trezor firmware. Version 0.2 adds no new features: it reorganizes the
original draft so that it **builds and boots on the board**, and fixes the errors that prevented
it. Full list of fixes: [`CHANGES.md`](CHANGES.md).

All 6 phases and the LED test build with `arm-none-eabi-gcc 13.2`. Prebuilt binaries are in `dist/`.

## Hardware path

The CiWi board is designed in Autodesk Fusion using the **WeAct STM32F4 64-pin Core Board V1.1**
as its reference design ([schematic](docs/hardware/WeAct-STM32F4_64PIN-CoreBoard_V11_Schematic.pdf),
© WeAct Studio, included for reference). Development follows three steps:

1. **Test board (now):** an STM32F405RGT6 development board with the same core architecture as the
   WeAct reference, plus an extra USB port that makes flashing easier. This firmware is tested here first.
2. **Intermediate step:** a PCB that **keeps the WeAct core architecture unchanged** (STM32F405RGT6,
   8 MHz HSE, LED on PB2, KEY on PC13, USB on PA11/PA12, SWD and UART headers) but already has the
   **CiWi form factor**: outline, mounting holes, display header and the click-wheel opening.
   Code that passes on the test board should run on it without pin changes.
3. **CiWi main board:** the final design, adding the TROPIC01 secure element and the CiWi peripherals.

| Test board | Intermediate step: WeAct architecture in the CiWi form factor |
| --- | --- |
| ![STM32F405RGT6 development board used for testing](docs/images/test-board-stm32f405.png) | ![PCB with the WeAct core architecture in the CiWi form factor](docs/images/ciwi-form-factor-pcb.png) |

## What you need

| Item | Purpose |
| --- | --- |
| STM32F405RGT6 test board (WeAct STM32F4 64-pin Core Board V1.1 architecture) | 8 MHz HSE crystal, user LED on **PB2**, KEY button on PC13 |
| ST-Link V2 (or clone) | Flashing and debugging over SWD (DIO, CLK, GND, 3V3) |
| 3.3 V USB-UART adapter | Test output: **PA9 (TX) → adapter RX**, common GND, 115200 8N1 |
| USB-C cable | USB tests (phases 1 and 5) |
| 3.5" ILI9488 SPI display with XPT2046 | Phases 2 to 6 (pins below) |
| `arm-none-eabi-gcc`, `make`, `openocd`, `git` | Build and flash (Ubuntu: `sudo apt install gcc-arm-none-eabi make openocd git`) |

## Flash a prebuilt binary (fastest)

```bash
openocd -f tools/openocd_weact_f405.cfg -c "program dist/ciwi-blink.elf verify reset exit"
openocd -f tools/openocd_weact_f405.cfg -c "program dist/ciwi-phase1.elf verify reset exit"
```

Open a serial terminal (for example `picocom -b 115200 /dev/ttyUSB0`) before resetting the board.

## Build

```bash
./tools/fetch_vendor.sh      # downloads ST HAL, CMSIS and trezor-crypto (pinned commits)
make blink                   # minimal LED test
make PHASE=1                 # or 2..6 → build/phaseN/ciwi-phaseN.{elf,bin,hex}
make flash PHASE=1           # build and flash with OpenOCD
make flash-blink
```

`LAYOUT=standalone` (default) places the firmware at **0x08000000** so the board boots without a
bootloader. `LAYOUT=bootloader` places it at 0x08010000 for when a bootloader exists.

## Test plan, one phase at a time

Do not move to the next phase until the current one passes.

| Step | Binary | Connect | Pass criteria |
| --- | --- | --- | --- |
| 0 | `ciwi-blink` | ST-Link only | Blue LED (PB2) blinks |
| 1 | `ciwi-phase1` | + USB-UART on PA9 and USB-C | Terminal shows 168 MHz clock, LED, UART and RNG **OK**; the PC detects a USB device. The click-wheel test reports **WARN** (it looks for an AS5600, see notes) |
| 2 | `ciwi-phase2` | + ILI9488 display | Colors, text and test patterns on screen |
| 3 | `ciwi-phase3` | + display | Navigable menu (test inputs) |
| 4 | `ciwi-phase4` | + display | UI screens, backlight and animations |
| 5 | `ciwi-phase5` | + display + USB-C | BIP39/BIP32/ECDSA vectors OK over UART; `trezorctl get-features` responds. **Test seeds and testnet only** |
| 6 | `ciwi-phase6` | + display + USB-C | Full security audit report over UART. RDP is **not** applied automatically; do not enable it on the dev board |

### Pins

Pin assignments follow the [WeAct STM32F4 64-pin Core Board V1.1 schematic](docs/hardware/WeAct-STM32F4_64PIN-CoreBoard_V11_Schematic.pdf). If the LED does not blink
on a different test board, check which pin its LED uses and adjust `LED_PIN` in `blink_test.c`.

| Function | Pin |
| --- | --- |
| User LED | PB2 |
| Debug UART TX | PA9 (USART1) |
| USB FS | PA11 / PA12 |
| Display SPI1 | PA5 SCK, PA7 MOSI, PA6 MISO, PC4 CS, PC5 DC, PB0 RST, PB1 BL |
| Touch XPT2046 | PC6 CS, **PC7 IRQ [to verify]** (shares SPI1) |
| Click-wheel (I2C) | PB6 SCL / PB7 SDA |

## Layout

| Folder | Contents |
| --- | --- |
| `platform/` | Startup, linker scripts, clock, UART, interrupts, HAL configuration |
| `drivers/` | ILI9488 display, touch, backlight, click-wheel, compatibility layer for Trezor's display API |
| `ui/` | Input, menu, components and screens |
| `wallet/` | Crypto test vectors, NORCOW storage, USB HID, wallet core, FSM, signing |
| `security/` | Flash protection, MPU, PIN delay, tamper detection, security audit |
| `phases/phaseN/` | Test program for each phase |
| `trezor-integration/` | Files that **replace** Trezor legacy files (`protect.c`, `buttons.c`, `layout.c`...). Not built here; kept for integration with the Trezor tree |
| `tools/` | OpenOCD config, gdbinit, `fetch_vendor.sh` |
| `dist/` | Prebuilt binaries and SHA-256 checksums |
| `docs/hardware/` | Reference schematic of the WeAct STM32F4 64-pin Core Board V1.1 (© WeAct Studio) |
| `docs/original-notes/` | Original Makefiles and notes, for reference |

Source-code comments are mostly in Spanish, as in the original draft.

## Known open items (they do not block testing)

- **VID:PID 1209:53C1 belongs to Trezor.** It is convenient for testing with `trezorctl`, but a dedicated
  PID must be requested from pid.codes before any unit leaves the workshop.
- **Click-wheel:** the driver targets the AS5600 magnetic sensor; the CiWi now uses a capacitive wheel.
- **Touch:** `touch_xpt2046.h` was reconstructed because it was missing; the IRQ pin (PC7) is an assumption.
- **TROPIC01:** no driver yet; the PIN is stored with PBKDF2 in the STM32 flash.
- **Anchor Protocol:** not implemented yet (next stage), see [walcorg/anchor-protocol](https://github.com/walcorg/anchor-protocol).
- **License:** source files are GPL v3, like `trezor-firmware`. trezor-crypto (`vendor/`) is MIT.
