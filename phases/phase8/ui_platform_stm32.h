/**
 * @file    ui_platform_stm32.h
 * @brief   Phase 8: ciwi_ui platform on the development board (KEY, UART keys, touch).
 */
#ifndef UI_PLATFORM_STM32_H
#define UI_PLATFORM_STM32_H

#include <stdbool.h>

#include "ui_app.h"
#include "ui_gfx.h"

/** @param touch_hw  true if touch_init() found the XPT2046 */
const ui_platform_t *ui_platform_stm32_init(bool touch_hw);
void ui_platform_stm32_set_status(const char *text);
/** Settings from NORCOW, or the defaults. */
void ui_platform_stm32_load_settings(ui_settings_t *s);

#endif /* UI_PLATFORM_STM32_H */
