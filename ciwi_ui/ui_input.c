/**
 * @file    ui_input.c
 * @brief   CiWi input events. See ui_input.h.
 */
#include "ui_input.h"

#include <string.h>

#define QUEUE_LEN 16

static ui_event_t q[QUEUE_LEN];
static uint8_t q_head, q_len;

static struct {
    bool down;
    uint32_t t0;
    ui_src_t src;
    bool long_sent;     /* HOLD_DONE or HOME already produced for this press */
    uint8_t last_pct;
} btn[2];

static struct {
    bool down;
    int16_t x0, y0, last_y;
    bool dragging;
} touch;

static bool touch_on = true;
static bool reverse;

static void push(ui_ev_type_t type, ui_src_t src, int16_t steps, uint8_t pct, int16_t x, int16_t y)
{
    if (q_len == QUEUE_LEN) {
        /* Full: drop the oldest event (the screen reads the queue every loop, so this only
         * happens if the UI stalls). */
        q_head = (q_head + 1) % QUEUE_LEN;
        q_len--;
    }
    ui_event_t *e = &q[(q_head + q_len) % QUEUE_LEN];
    e->type = type;
    e->src = src;
    e->steps = steps;
    e->progress = pct;
    e->x = x;
    e->y = y;
    q_len++;
}

void ui_input_reset(void)
{
    q_head = q_len = 0;
    memset(btn, 0, sizeof(btn));
    memset(&touch, 0, sizeof(touch));
}

void ui_input_button(ui_button_t b, bool down, uint32_t now, ui_src_t src)
{
    if (b > UI_BTN_MENU) return;
    if (down) {
        if (btn[b].down) return;
        btn[b].down = true;
        btn[b].t0 = now;
        btn[b].src = src;
        btn[b].long_sent = false;
        btn[b].last_pct = 0;
        return;
    }
    if (!btn[b].down) return;
    btn[b].down = false;
    uint32_t held = now - btn[b].t0;
    if (btn[b].long_sent) return; /* the long press already acted */
    if (b == UI_BTN_CENTER) {
        if (held < UI_SHORT_MS) push(UI_EV_SELECT, btn[b].src, 0, 0, 0, 0);
        else push(UI_EV_HOLD_CANCEL, btn[b].src, 0, 0, 0, 0);
    } else {
        push(UI_EV_BACK, btn[b].src, 0, 0, 0, 0);
    }
}

void ui_input_tick(uint32_t now)
{
    for (int b = 0; b < 2; b++) {
        if (!btn[b].down || btn[b].long_sent) continue;
        uint32_t held = now - btn[b].t0;
        if (held >= UI_HOLD_MS) {
            btn[b].long_sent = true;
            if (b == UI_BTN_CENTER) {
                push(UI_EV_HOLD_PROGRESS, btn[b].src, 0, 100, 0, 0);
                push(UI_EV_HOLD_DONE, btn[b].src, 0, 0, 0, 0);
            } else {
                push(UI_EV_HOME, btn[b].src, 0, 0, 0, 0);
            }
        } else if (b == UI_BTN_CENTER && held >= UI_SHORT_MS) {
            uint8_t pct = (uint8_t)(held * 100u / UI_HOLD_MS);
            if (pct >= btn[b].last_pct + 5) {
                btn[b].last_pct = pct;
                push(UI_EV_HOLD_PROGRESS, btn[b].src, 0, pct, 0, 0);
            }
        }
    }
}

void ui_input_rotate(int16_t steps, ui_src_t src)
{
    if (!steps) return;
    push(UI_EV_ROTATE, src, reverse ? (int16_t)-steps : steps, 0, 0, 0);
}

void ui_input_touch(bool down, int16_t x, int16_t y, uint32_t now)
{
    (void)now;
    if (!touch_on) {
        touch.down = false;
        return;
    }
    if (down && !touch.down) {
        touch.down = true;
        touch.dragging = false;
        touch.x0 = x;
        touch.y0 = touch.last_y = y;
    } else if (down) {
        int16_t dy = y - touch.last_y;
        int16_t moved = (int16_t)((y - touch.y0) < 0 ? touch.y0 - y : y - touch.y0);
        if (touch.dragging || moved >= UI_TAP_MOVE_PX) {
            touch.dragging = true;
            if (dy) push(UI_EV_TOUCH_DRAG, UI_SRC_TOUCH, 0, 0, x, dy);
            touch.last_y = y;
        }
    } else if (touch.down) {
        touch.down = false;
        if (!touch.dragging) push(UI_EV_TOUCH_TAP, UI_SRC_TOUCH, 0, 0, touch.x0, touch.y0);
    }
}

bool ui_input_next(ui_event_t *ev)
{
    if (!q_len) return false;
    *ev = q[q_head];
    q_head = (q_head + 1) % QUEUE_LEN;
    q_len--;
    return true;
}

void ui_input_set_touch_enabled(bool on)
{
    touch_on = on;
    touch.down = false;
}

bool ui_input_touch_enabled(void) { return touch_on; }
void ui_input_set_reverse(bool r) { reverse = r; }
