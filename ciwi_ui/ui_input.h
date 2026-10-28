/**
 * @file    ui_input.h
 * @brief   CiWi input events: click-wheel, centre button, MENU button and touch screen.
 *
 * The platform feeds raw input (button up/down with a timestamp, wheel steps, touch samples) and
 * calls ui_input_tick() regularly; screens read events with ui_input_next().
 *
 *   Wheel             ROTATE(steps)      + clockwise (direction can be swapped in Settings)
 *   Centre, short     SELECT
 *   Centre, held      HOLD_PROGRESS(%)   then HOLD_DONE at 2 s; releasing earlier gives HOLD_CANCEL
 *   MENU, short       BACK
 *   MENU, held 2 s    HOME
 *   Touch tap         TOUCH_TAP(x, y)    only when touch is enabled
 *   Touch drag        TOUCH_DRAG(dy)
 *
 * Every event says where it came from (ui_src_t). Screens that approve a signature accept only
 * HOLD_DONE from UI_SRC_BUTTON: no touch, no emulated input can sign.
 */
#ifndef UI_INPUT_H
#define UI_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#define UI_HOLD_MS        2000u  /**< hold time for sensitive actions */
#define UI_SHORT_MS       350u   /**< a press shorter than this is a click */
#define UI_TAP_MOVE_PX    12     /**< touch movement below this is a tap */

typedef enum {
    UI_EV_NONE = 0,
    UI_EV_ROTATE,
    UI_EV_SELECT,
    UI_EV_HOLD_PROGRESS,
    UI_EV_HOLD_DONE,
    UI_EV_HOLD_CANCEL,
    UI_EV_BACK,
    UI_EV_HOME,
    UI_EV_TOUCH_TAP,
    UI_EV_TOUCH_DRAG,
    UI_EV_IDLE_TIMEOUT, /**< produced by ui_app when auto-lock expires, not by the hardware */
} ui_ev_type_t;

typedef enum {
    UI_SRC_WHEEL = 0,   /**< the capacitive wheel */
    UI_SRC_BUTTON,      /**< a physical button: centre or MENU */
    UI_SRC_TOUCH,       /**< the touch screen */
    UI_SRC_EMULATED,    /**< UART keys on the development board */
} ui_src_t;

typedef struct {
    ui_ev_type_t type;
    ui_src_t src;
    int16_t steps;     /**< ROTATE: + clockwise (after the direction setting) */
    uint8_t progress;  /**< HOLD_PROGRESS: 0..100 */
    int16_t x, y;      /**< TOUCH_*: screen coordinates; DRAG: y = dy */
} ui_event_t;

typedef enum { UI_BTN_CENTER = 0, UI_BTN_MENU = 1 } ui_button_t;

void ui_input_reset(void);

/** Raw button edge. src = UI_SRC_BUTTON for real buttons, UI_SRC_EMULATED for UART keys. */
void ui_input_button(ui_button_t b, bool down, uint32_t now_ms, ui_src_t src);

/** Wheel movement in steps (+ = clockwise as the hardware reports it). */
void ui_input_rotate(int16_t steps, ui_src_t src);

/** Touch sample: down with screen coordinates, or up. Ignored while touch is disabled. */
void ui_input_touch(bool down, int16_t x, int16_t y, uint32_t now_ms);

/** Call every few ms: produces hold progress and long presses. */
void ui_input_tick(uint32_t now_ms);

bool ui_input_next(ui_event_t *ev);

void ui_input_set_touch_enabled(bool on);
bool ui_input_touch_enabled(void);
void ui_input_set_reverse(bool reverse);

#endif /* UI_INPUT_H */
