/**
 * @file    ui_platform_stm32.c
 * @brief   Phase 8 inputs and services for ciwi_ui on the development board.
 *
 * Centre button  KEY (PC13, pressed = high): a physical button, so it can approve signatures.
 * MENU button    not on the WeAct board yet: emulated from the UART (below).
 * Wheel          the capacitive wheel driver is pending: emulated from the UART.
 * Touch          XPT2046 (if a display with touch is attached), polled every 20 ms.
 *
 * UART keys (115200 8N1 on PA10 RX / PA9 TX), all UI_SRC_EMULATED, so they can navigate and
 * reject but never approve a signature:
 *   a / d   wheel anticlockwise / clockwise       e   centre click
 *   m       MENU click (back)                      h   MENU held 2 s (Home / lock)
 */
#include "ui_platform_stm32.h"

#include <string.h>

#include "backlight.h"
#include "rng_test.h"
#include "storage_hal.h"
#include "stm32f4xx_hal.h"
#include "touch_xpt2046.h"
#include "uart_debug.h"

#define KEY_PORT GPIOC
#define KEY_PIN  GPIO_PIN_13

#define STORAGE_APP_UI    0x03
#define STORAGE_KEY_UISET 0x01

/* [VERIFY] The touch driver reports landscape coordinates (480x320). For the portrait UI they
 * are rotated; flip these if taps land mirrored on your panel. */
#ifndef CIWI_TOUCH_FLIP_X
#define CIWI_TOUCH_FLIP_X 0
#endif
#ifndef CIWI_TOUCH_FLIP_Y
#define CIWI_TOUCH_FLIP_Y 0
#endif

static bool g_touch_hw;
static bool g_key_down;
static uint32_t g_last_touch_poll;
static const char *g_status = "";

static uint32_t now_ms(void) { return HAL_GetTick(); }

static void uart_rx_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_10;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &g);
    USART1->CR1 |= USART_CR1_RE; /* debug_uart_init() enables TX only */
}

static void poll_uart(uint32_t now)
{
    while (USART1->SR & USART_SR_RXNE) {
        char c = (char)(USART1->DR & 0xFF);
        switch (c) {
        case 'a': ui_input_rotate(-1, UI_SRC_EMULATED); break;
        case 'd': ui_input_rotate(+1, UI_SRC_EMULATED); break;
        case 'e':
            ui_input_button(UI_BTN_CENTER, true, now, UI_SRC_EMULATED);
            ui_input_button(UI_BTN_CENTER, false, now + 1, UI_SRC_EMULATED);
            break;
        case 'm':
            ui_input_button(UI_BTN_MENU, true, now, UI_SRC_EMULATED);
            ui_input_button(UI_BTN_MENU, false, now + 1, UI_SRC_EMULATED);
            break;
        case 'h':
            /* a press that started 2 s ago: the next tick produces HOME */
            ui_input_button(UI_BTN_MENU, true, now - UI_HOLD_MS - 1, UI_SRC_EMULATED);
            ui_input_tick(now);
            ui_input_button(UI_BTN_MENU, false, now, UI_SRC_EMULATED);
            break;
        default: break;
        }
    }
    if (USART1->SR & USART_SR_ORE) (void)USART1->DR; /* clear an overrun */
}

static void poll(void)
{
    uint32_t now = now_ms();

    bool down = HAL_GPIO_ReadPin(KEY_PORT, KEY_PIN) == GPIO_PIN_SET;
    if (down != g_key_down) {
        HAL_Delay(15); /* debounce */
        if ((HAL_GPIO_ReadPin(KEY_PORT, KEY_PIN) == GPIO_PIN_SET) == down) {
            g_key_down = down;
            ui_input_button(UI_BTN_CENTER, down, now, UI_SRC_BUTTON);
        }
    }

    poll_uart(now);

    if (g_touch_hw && ui_input_touch_enabled() && now - g_last_touch_poll >= 20) {
        g_last_touch_poll = now;
        touch_point_t p;
        if (touch_read(&p) && p.valid) {
            int16_t x = (int16_t)(CIWI_TOUCH_FLIP_X ? (UI_W - 1 - p.y) : p.y);
            int16_t y = (int16_t)(CIWI_TOUCH_FLIP_Y ? p.x : (UI_H - 1 - p.x));
            ui_input_touch(true, x, y, now);
        } else {
            ui_input_touch(false, 0, 0, now);
        }
    }
}

static uint32_t random32(void)
{
    uint32_t v = 0;
    rng_get32(&v);
    return v;
}

static void settings_changed(const ui_settings_t *s)
{
    backlight_set((uint8_t)(s->brightness * 51));
    uint8_t b[5] = {s->touch, s->reverse, s->brightness, s->autolock_min, s->lang};
    storage_put(STORAGE_APP_UI, STORAGE_KEY_UISET, b, sizeof(b));
}

static const char *status_text(void) { return g_status; }

static const ui_platform_t PLATFORM = {now_ms, poll, random32, settings_changed, status_text, NULL};

const ui_platform_t *ui_platform_stm32_init(bool touch_hw)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = KEY_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(KEY_PORT, &g);
    uart_rx_init();
    g_touch_hw = touch_hw;
    return &PLATFORM;
}

void ui_platform_stm32_set_status(const char *text) { g_status = text ? text : ""; }

void ui_platform_stm32_load_settings(ui_settings_t *s)
{
    uint8_t b[5];
    uint16_t n = 0;
    s->touch = true;
    s->reverse = false;
    s->brightness = 3;
    s->autolock_min = 5;
    s->lang = UI_LANG_ES;
    if (storage_get(STORAGE_APP_UI, STORAGE_KEY_UISET, b, sizeof(b), &n) && n == sizeof(b)) {
        s->touch = b[0] != 0;
        s->reverse = b[1] != 0;
        s->brightness = b[2];
        s->autolock_min = b[3];
        s->lang = b[4];
    }
    if (!g_touch_hw) s->touch = false;
}
