# software-ciwi-v0.2

Firmware de prueba de la **CiWi** (WALCORG) para la tarjeta **WeAct STM32F405RGT6 Core Board**,
basado en el fork de Trezor. Esta versión no agrega funciones: reorganiza el borrador
`scripts-anthropic-for-ciwi-based-on-trezor` para que **compile y arranque en la tarjeta**,
y corrige los errores que lo impedían. Lista de cambios: [`CAMBIOS.md`](CAMBIOS.md).

Las 6 fases y la prueba de LED compilan con `arm-none-eabi-gcc 13.2`. Los binarios ya
compilados están en `dist/`.

## Qué necesitas

| Cosa | Para qué |
| --- | --- |
| WeAct STM32F405RGT6 Core Board | Cristal HSE de 8 MHz, LED en **PB2**, botón KEY en PC13 |
| ST-Link V2 (o clon) | Cargar firmware y depurar por SWD (DIO, CLK, GND, 3V3) |
| Adaptador USB-UART 3.3 V | Ver los mensajes de prueba: **PA9 (TX) → RX del adaptador**, GND común, 115200 8N1 |
| Cable USB-C | Probar USB (fases 1 y 5) |
| Pantalla ILI9488 3.5" SPI con XPT2046 | Fases 2 a 6 (pines en la tabla de abajo) |
| `arm-none-eabi-gcc`, `make`, `openocd`, `git` | Compilar y cargar (Ubuntu: `sudo apt install gcc-arm-none-eabi make openocd git`) |

## Cargar un binario ya compilado (lo más rápido)

```bash
openocd -f tools/openocd_weact_f405.cfg -c "program dist/ciwi-blink.elf verify reset exit"
openocd -f tools/openocd_weact_f405.cfg -c "program dist/ciwi-phase1.elf verify reset exit"
```

Abre la terminal serie (por ejemplo `picocom -b 115200 /dev/ttyUSB0`) antes de dar reset.

## Compilar

```bash
./tools/fetch_vendor.sh      # descarga HAL de ST, CMSIS y trezor-crypto (commits fijos)
make blink                   # prueba mínima de LED
make PHASE=1                 # o 2, 3, 4, 5, 6 → build/phaseN/ciwi-phaseN.{elf,bin,hex}
make flash PHASE=1           # compila y carga con OpenOCD
make flash-blink
```

`LAYOUT=standalone` (por defecto) coloca el firmware en **0x08000000** para que la tarjeta arranque
sin bootloader. `LAYOUT=bootloader` lo coloca en 0x08010000 para cuando exista un bootloader.

## Plan de pruebas, una fase a la vez

No pases a la siguiente fase hasta que la anterior pase.

| Paso | Binario | Qué conectar | Debe pasar |
| --- | --- | --- | --- |
| 0 | `ciwi-blink` | Solo ST-Link | El LED azul (PB2) parpadea |
| 1 | `ciwi-phase1` | + USB-UART en PA9 y USB-C | En la terminal: reloj 168 MHz, LED, UART y RNG en **OK**; la PC detecta un dispositivo USB. La prueba del click-wheel dará **WARN** (busca un AS5600, ver notas) |
| 2 | `ciwi-phase2` | + pantalla ILI9488 | Colores, texto y patrones de prueba en pantalla |
| 3 | `ciwi-phase3` | + pantalla | Menú navegable (entradas de prueba) |
| 4 | `ciwi-phase4` | + pantalla | Pantallas de la interfaz, retroiluminación y animaciones |
| 5 | `ciwi-phase5` | + pantalla + USB-C | Vectores BIP39/BIP32/ECDSA en OK por UART; `trezorctl get-features` responde. **Solo semillas de prueba y testnet** |
| 6 | `ciwi-phase6` | + pantalla + USB-C | Reporte de auditoría de seguridad completo por UART. RDP **no** se aplica automáticamente; no lo actives en la tarjeta de desarrollo |

### Pines usados

| Función | Pin |
| --- | --- |
| LED de usuario | PB2 |
| UART debug TX | PA9 (USART1) |
| USB FS | PA11 / PA12 |
| Pantalla SPI1 | PA5 SCK, PA7 MOSI, PA6 MISO, PC4 CS, PC5 DC, PB0 RST, PB1 BL |
| Touch XPT2046 | PC6 CS, **PC7 IRQ [verificar]** (comparte SPI1) |
| Click-wheel (I2C) | PB6 SCL / PB7 SDA |

## Estructura

| Carpeta | Contenido |
| --- | --- |
| `platform/` | Arranque, linker scripts, reloj, UART, interrupciones, configuración del HAL |
| `drivers/` | Pantalla ILI9488, touch, retroiluminación, click-wheel, capa de compatibilidad con la API de pantalla de Trezor |
| `ui/` | Entradas, menú, componentes y pantallas |
| `wallet/` | Cripto (vectores), almacenamiento NORCOW, USB HID, núcleo de la wallet, FSM, firma |
| `security/` | Protección de flash, MPU, retardo de PIN, detección de manipulación, auditoría |
| `phases/phaseN/` | Programa de prueba de cada fase |
| `trezor-integration/` | Archivos que **reemplazan** a los de Trezor legacy (`protect.c`, `buttons.c`, `layout.c`...). No se compilan aquí; son para cuando se integre con el árbol de Trezor |
| `tools/` | OpenOCD, gdbinit, `fetch_vendor.sh` |
| `dist/` | Binarios compilados |
| `docs/original-notes/` | Makefiles y notas originales, como referencia |

## Pendientes conocidos (no bloquean las pruebas)

- **VID:PID 1209:53C1 es el de Trezor.** Sirve para probar con `trezorctl`, pero hay que pedir un PID propio
  en pid.codes antes de que salga cualquier unidad del taller.
- **Click-wheel:** el driver es para el sensor magnético AS5600; la CiWi ya usa rueda capacitiva.
- **Touch:** `touch_xpt2046.h` se reconstruyó porque faltaba; el pin IRQ (PC7) es un supuesto.
- **TROPIC01:** aún no hay driver; el PIN se guarda con PBKDF2 en la flash del STM32.
- **Anchor Protocol:** ninguna función del protocolo está implementada todavía (siguiente etapa).
- **Licencia:** los archivos indican GPL v3, igual que `trezor-firmware`. trezor-crypto (`vendor/`) es MIT.
