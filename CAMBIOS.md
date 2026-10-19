# Cambios de v0.1 (scripts-anthropic-for-ciwi-based-on-trezor) a v0.2

En el código, cada cambio está marcado con `[v0.2]`.

## Errores que impedían compilar o arrancar

| # | Problema | Efecto | Corrección |
| --- | --- | --- | --- |
| 1 | Los 3 Makefiles tenían comentarios después de `\` | `make` fallaba en la línea 87 (`missing separator`) antes de compilar nada | Makefile nuevo y único, con `PHASE=1..6` |
| 2 | Variables con comentario en la misma línea (`BUILD_DIR = build  # ...`) | Rutas con espacios al final | Eliminado |
| 3 | El Makefile esperaba carpetas que no existían (`src/`, `drivers/`, `vendor/`...) y la fase 1 solo listaba `clickwheel.c` | No había nada que compilar | Estructura nueva y lista de fuentes por fase |
| 4 | **Linker en 0x08010000** sin bootloader | La tarjeta **no habría arrancado** (el STM32 lee los vectores en 0x08000000) | `stm32f405rgt6_standalone.ld` (0x08000000, por defecto) y `stm32f405rgt6_bootloader.ld` (0x08010000 + `VECT_TAB_OFFSET`) |
| 5 | **LED en PC13** | En la WeAct F405, PC13 es el botón KEY; el LED está en **PB2**. Ni el blink ni el aviso de error se habrían visto | LED en PB2 en `blink_test.c`, `main_phase1.c` y `Error_Handler()` |
| 6 | `stm32f4xx_hal_conf.h` sin `assert_param`, `EXTERNAL_CLOCK_VALUE`, `LSI_VALUE`, `VDD_VALUE`... | El HAL de ST no compilaba ni enlazaba | Agregados según la plantilla de ST |
| 7 | `trezor_makefile.patch` corrupto (`git apply`: línea 36) y con contexto que no existe en el `Makefile.include` actual de Trezor | No aplicaba | Ya no se usa: se compila con el HAL de ST y solo `trezor-crypto` (movido a `docs/original-notes/`) |
| 8 | Faltaba `touch_xpt2046.h` | Fases 2-6 no compilaban | Reconstruido desde el `.c` |
| 9 | `OTG_FS_IRQHandler` requería `hpcd_USB_OTG_FS` en todas las fases | Fases 2-4 no enlazaban | Definición débil en `stm32f4xx_it.c` |
| 10 | `menu.h` usaba `input_action_t` sin incluir `input_manager.h`; faltaba `WALCORG_DARKGRAY` | Fase 3 no compilaba | Include y color agregados |
| 11 | `wallet_core.c` y `crypto_validate.c` usaban `secp256k1` sin `#include "secp256k1.h"` | Fase 5 no compilaba | Include agregado |
| 12 | `ecdsa_sig_to_der(sig, der, &len)`: firma vieja de la función | Fase 5 no compilaba; la API actual devuelve la longitud | `der_len = ecdsa_sig_to_der(sig, der)` |
| 13 | `signing_walcorg.c` usaba `INPUT_CONFIRM` sin incluir `input_manager.h` | Fase 5 no compilaba | Include agregado |
| 14 | `fsm_walcorg.c` llamaba `rng_get_uint32()`, que no existe | Fase 5 no enlazaba | Usa `rng_get32()` |
| 15 | trezor-crypto necesita `random_buffer()` y `tc_fault_handler()` de la plataforma | Fase 5 no enlazaba | `wallet/trezor_rand_hw.c` (RNG por hardware; si falla, se detiene) |

## Errores de comportamiento corregidos

| # | Problema | Corrección |
| --- | --- | --- |
| 16 | Macro `RGB565(r,g,b)` sin paréntesis en sus argumentos: `RGB565(x, y, z + 10)` calculaba `z + (10 >> 3)` | Argumentos entre paréntesis |
| 17 | `main_phase2/4/5.c` usaban `snprintf`, `memset`, `strtok`... sin `#include` | `<stdio.h>` y `<string.h>` agregados |

## Otros

- `tools/openocd_weact_f405.cfg`: `interface hla` / `hla_layout` (obsoletos) → `source [find interface/stlink.cfg]`. Verificado con OpenOCD 0.12.
- `tools/fetch_vendor.sh`: descarga HAL, CMSIS y trezor-crypto en commits fijos.
- Los archivos "modified" que reemplazan a los de Trezor legacy se movieron a `trezor-integration/` (no se compilan).
- La capa de compatibilidad `display.c`/`display.h` (API de pantalla de Trezor sobre el ILI9488) se compila
  en las fases 3 (versión de la fase 2) y 4-6 (versión de la fase 4): `drivers/compat/`.

## Comentarios inexactos detectados (sin cambiar el código)

- `wallet/usb_hid.h`: la tabla del encabezado lista números de mensaje que no coinciden con el `enum` (el `enum` es correcto).
- `wallet/storage_hal.h`: el comentario ubica el sector 10 en `0x08080000`; el `#define` (`0x080C0000`) es el correcto.
