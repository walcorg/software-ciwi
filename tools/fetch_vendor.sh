#!/usr/bin/env bash
# =============================================================================
# fetch_vendor.sh — descarga las dependencias externas en vendor/
#
# Se fijan los commits exactos con los que se compiló y verificó software-ciwi-v0.2,
# para que todos obtengan el mismo resultado.
#
#   ./tools/fetch_vendor.sh
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p vendor

fetch() {
  local name="$1" url="$2" commit="$3" sparse="${4:-}"
  if [ -d "vendor/$name/.git" ]; then
    echo "· vendor/$name ya existe"
    return
  fi
  echo "· descargando $name @ ${commit:0:10}"
  git init -q "vendor/$name"
  git -C "vendor/$name" remote add origin "$url"
  if [ -n "$sparse" ]; then
    git -C "vendor/$name" sparse-checkout set $sparse
  fi
  git -C "vendor/$name" fetch -q --depth 1 origin "$commit"
  git -C "vendor/$name" checkout -q FETCH_HEAD
}

# HAL y CMSIS de ST (BSD-3-Clause / Apache-2.0)
fetch stm32f4xx_hal_driver https://github.com/STMicroelectronics/stm32f4xx_hal_driver.git 1f6451c3e07728b4c830744de380e56bf5bc0026
fetch cmsis_device_f4      https://github.com/STMicroelectronics/cmsis_device_f4.git      9192c7b9df75a142f2027ab266601fe061fc00b3
fetch cmsis_core           https://github.com/STMicroelectronics/cmsis_core.git           01d18147527e30ed69ace1cb03b818de78bc7062

# Solo la biblioteca criptográfica de Trezor (crypto/ es MIT)
fetch trezor-firmware      https://github.com/trezor/trezor-firmware.git                  72489948b3c44a37598de9da658be28e4fabb4ae "crypto"

echo "Listo. Ahora: make PHASE=1"
