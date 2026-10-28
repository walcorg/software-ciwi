#!/usr/bin/env bash
# =============================================================================
# fetch_vendor.sh — downloads external dependencies into vendor/
#
# Commits are pinned to the exact versions used to build and verify v0.2,
# so everyone gets the same result.
#
#   ./tools/fetch_vendor.sh
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p vendor

fetch() {
  local name="$1" url="$2" commit="$3" sparse="${4:-}"
  if [ -d "vendor/$name/.git" ]; then
    echo "· vendor/$name already exists"
    return
  fi
  echo "· fetching $name @ ${commit:0:10}"
  git init -q "vendor/$name"
  git -C "vendor/$name" remote add origin "$url"
  if [ -n "$sparse" ]; then
    git -C "vendor/$name" sparse-checkout set $sparse
  fi
  git -C "vendor/$name" fetch -q --depth 1 origin "$commit"
  git -C "vendor/$name" checkout -q FETCH_HEAD
}

# ST HAL and CMSIS (BSD-3-Clause / Apache-2.0)
fetch stm32f4xx_hal_driver https://github.com/STMicroelectronics/stm32f4xx_hal_driver.git 1f6451c3e07728b4c830744de380e56bf5bc0026
fetch cmsis_device_f4      https://github.com/STMicroelectronics/cmsis_device_f4.git      9192c7b9df75a142f2027ab266601fe061fc00b3
fetch cmsis_core           https://github.com/STMicroelectronics/cmsis_core.git           01d18147527e30ed69ace1cb03b818de78bc7062

# Only the Trezor crypto library (crypto/ is MIT)
fetch trezor-firmware      https://github.com/trezor/trezor-firmware.git                  72489948b3c44a37598de9da658be28e4fabb4ae "crypto"

# libtropic, the TROPIC01 SDK by Tropic Square (Clear BSD), v4.2.0 (phase 7)
fetch libtropic            https://github.com/tropicsquare/libtropic.git                  9085ea5387a8a1d5a946c7e4f633c7134e4909a7 "include src cal hal scripts/tropic01_model LICENSE.md"

echo "Done. Next: make PHASE=1"
