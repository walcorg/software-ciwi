/**
 * @file    ui_app.c
 * @brief   CiWi interface: screens and navigation. See ui_app.h.
 *
 * Screen zones (portrait 320x480):
 *   status bar   y   0..31
 *   title        y  32..79   ("‹" back target on the left when touch is on)
 *   content      y  80..431  (up to 7 rows of 48 px)
 *   help bar     y 432..479  (what each control does here; hold progress at its foot)
 */
#include "ui_app.h"

#include <stdio.h>
#include <string.h>

#include "ui_gfx.h"

#define Y_STATUS   0
#define H_STATUS   32
#define Y_TITLE    32
#define H_TITLE    48
#define Y_CONTENT  80
#define H_CONTENT  352
#define Y_HELP     432
#define H_HELP     48
#define ROW_H      48
#define ROWS       7
#define TXT        2          /* body text size: 12x16 px cells */
#define CHARS_LINE 24         /* characters per line at TXT in the content width */

static const ui_platform_t *P;
static const ui_actions_t *A;
static ui_settings_t S;
static uint32_t g_last_activity;

/* ================================================================ events */

static bool running(void) { return !P->running || P->running(); }

/** Next event, or false when the test bench stops. Produces UI_EV_IDLE_TIMEOUT for auto-lock. */
static bool wait_event(ui_event_t *ev)
{
    while (running()) {
        P->poll();
        uint32_t now = P->now_ms();
        ui_input_tick(now);
        if (ui_input_next(ev)) {
            g_last_activity = now;
            return true;
        }
        if (S.autolock_min && now - g_last_activity > (uint32_t)S.autolock_min * 60000u) {
            g_last_activity = now;
            memset(ev, 0, sizeof(*ev));
            ev->type = UI_EV_IDLE_TIMEOUT;
            return true;
        }
    }
    return false;
}

/* ================================================================ chrome */

static void draw_status(void)
{
    ui_fill(0, Y_STATUS, UI_W, H_STATUS, UI_C_BAR);
    const char *st = P->status_text ? P->status_text() : NULL;
    if (st) ui_text(10, Y_STATUS + 8, st, TXT, UI_C_TEXT2, UI_C_BAR);
}

static void draw_title(const char *title, bool back)
{
    ui_fill(0, Y_TITLE, UI_W, H_TITLE, UI_C_BG);
    if (back && S.touch) ui_text(10, Y_TITLE + 12, "\xE2\x80\xB9", 3, UI_C_TEXT2, UI_C_BG);
    uint8_t size = ui_text_width(title, 3) <= UI_W - 64 ? 3 : 2;
    ui_text_center(UI_W / 2, Y_TITLE + (size == 3 ? 12 : 16), title, size, UI_C_TEXT, UI_C_BG);
    ui_fill(0, Y_TITLE + H_TITLE - 1, UI_W, 1, UI_C_BORDER);
}

static void draw_help(const char *help)
{
    ui_fill(0, Y_HELP, UI_W, H_HELP, UI_C_BAR);
    const char *dot = strstr(help, " \xC2\xB7 ");
    if (ui_text_width(help, TXT) <= UI_W - 16 || !dot) {
        ui_text_center(UI_W / 2, Y_HELP + 16, help, TXT, UI_C_TEXT, UI_C_BAR);
        return;
    }
    /* Too wide: one line per control, split at the " · " */
    char a[64];
    size_t n = (size_t)(dot - help) < sizeof(a) - 1 ? (size_t)(dot - help) : sizeof(a) - 1;
    memcpy(a, help, n);
    a[n] = 0;
    ui_text_center(UI_W / 2, Y_HELP + 5, a, TXT, UI_C_TEXT, UI_C_BAR);
    ui_text_center(UI_W / 2, Y_HELP + 24, dot + 4, TXT, UI_C_TEXT, UI_C_BAR);
}

static void draw_hold_bar(uint8_t pct)
{
    int16_t w = (int16_t)(UI_W * pct / 100);
    ui_fill(0, Y_HELP + H_HELP - 6, w, 6, UI_C_ACCENT);
    ui_fill(w, Y_HELP + H_HELP - 6, UI_W - w, 6, UI_C_BAR);
}

static void chrome(const char *title, bool back, const char *help)
{
    ui_fill(0, Y_CONTENT, UI_W, H_CONTENT, UI_C_BG);
    draw_status();
    draw_title(title, back);
    draw_help(help);
}

static bool tap_is_back(const ui_event_t *e)
{
    return e->type == UI_EV_TOUCH_TAP && e->y >= Y_TITLE && e->y < Y_TITLE + H_TITLE && e->x < 64;
}

/* ================================================================ text layout */

/** Copy the next line of at most CHARS_LINE code points from *s, breaking at a space. */
static bool next_line(const char **s, char *line, size_t cap)
{
    const char *p = *s;
    while (*p == ' ') p++;
    if (!*p) return false;
    const char *brk = NULL, *q = p;
    int n = 0;
    while (*q && *q != '\n' && n < CHARS_LINE) {
        if (*q == ' ') brk = q;
        q++;
        while ((*q & 0xC0) == 0x80) q++; /* skip UTF-8 continuation bytes */
        n++;
    }
    const char *end = q;
    if (*q && *q != '\n' && *q != ' ' && brk) end = brk; /* would split a word */
    size_t len = (size_t)(end - p);
    if (len >= cap) len = cap - 1;
    memcpy(line, p, len);
    line[len] = 0;
    *s = (*end == '\n') ? end + 1 : end;
    return true;
}

/** Draw wrapped text from y; returns the y below the last line. */
static int16_t text_block_bg(int16_t x, int16_t y, const char *s, ui_color_t fg, ui_color_t bg,
                             int16_t y_max)
{
    char line[CHARS_LINE * 3 + 1];
    while (y + 16 <= y_max && next_line(&s, line, sizeof(line))) {
        ui_text(x, y, line, TXT, fg, bg);
        y += 22;
    }
    return y;
}

static int16_t text_block(int16_t x, int16_t y, const char *s, ui_color_t fg, int16_t y_max)
{
    return text_block_bg(x, y, s, fg, UI_C_BG, y_max);
}

static int16_t text_block_height(const char *s)
{
    char line[CHARS_LINE * 3 + 1];
    int16_t h = 0;
    while (next_line(&s, line, sizeof(line))) h += 22;
    return h;
}

/* ================================================================ list */

typedef struct {
    const char *label;
    const char *value;   /* right-aligned, may be NULL */
    bool dim;            /* shown in grey (not available yet) */
} item_t;

enum { LIST_BACK = -1, LIST_HOME = -2, LIST_STOP = -3, LIST_TIMEOUT = -4 };

static void draw_row(const item_t *it, int16_t y, bool sel)
{
    ui_color_t bg = sel ? UI_C_SELECTED : UI_C_BG;
    ui_fill(8, y + 2, UI_W - 16, ROW_H - 4, bg);
    if (sel) ui_fill(8, y + 2, 4, ROW_H - 4, UI_C_ACCENT);
    ui_text(24, y + 16, it->label, TXT, it->dim ? UI_C_DISABLED : UI_C_TEXT, bg);
    if (it->value) ui_text_right(UI_W - 20, y + 16, it->value, TXT, UI_C_TEXT2, bg);
}

static void draw_rows(const item_t *items, int n, int top, int sel)
{
    ui_fill(0, Y_CONTENT, UI_W, H_CONTENT, UI_C_BG);
    for (int i = 0; i < ROWS && top + i < n; i++) {
        draw_row(&items[top + i], Y_CONTENT + i * ROW_H, top + i == sel);
    }
    if (top > 0) ui_text_right(UI_W - 4, Y_CONTENT + 2, "\xE2\x80\xA6", 1, UI_C_TEXT2, UI_C_BG);
    if (top + ROWS < n) ui_text_right(UI_W - 4, Y_HELP - 10, "\xE2\x80\xA6", 1, UI_C_TEXT2, UI_C_BG);
}

/**
 * A list of up to 16 items. Returns the chosen index or LIST_*; *sel keeps the position.
 * *src receives the source of the choice (button, wheel, touch, emulated).
 */
static int run_list(const char *title, bool back, const item_t *items, int n, int *sel,
                    const char *help, ui_src_t *src)
{
    int top = *sel >= ROWS ? *sel - ROWS + 1 : 0;
    int drag = 0;
    chrome(title, back, help);
    draw_rows(items, n, top, *sel);
    ui_event_t e;
    while (wait_event(&e)) {
        int old = *sel, old_top = top;
        switch (e.type) {
        case UI_EV_ROTATE:
            *sel += e.steps;
            if (*sel < 0) *sel = 0;
            if (*sel >= n) *sel = n - 1;
            break;
        case UI_EV_SELECT:
            if (src) *src = e.src;
            return *sel;
        case UI_EV_BACK: if (back) return LIST_BACK; break;
        case UI_EV_HOME: return LIST_HOME;
        case UI_EV_IDLE_TIMEOUT: return LIST_TIMEOUT;
        case UI_EV_TOUCH_TAP:
            if (tap_is_back(&e) && back) return LIST_BACK;
            if (e.y >= Y_CONTENT && e.y < Y_CONTENT + ROWS * ROW_H) {
                int i = top + (e.y - Y_CONTENT) / ROW_H;
                if (i < n) {
                    *sel = i;
                    if (src) *src = UI_SRC_TOUCH;
                    return i;
                }
            }
            break;
        case UI_EV_TOUCH_DRAG:
            drag -= e.y;
            while (drag >= ROW_H && *sel < n - 1) { (*sel)++; drag -= ROW_H; }
            while (drag <= -ROW_H && *sel > 0) { (*sel)--; drag += ROW_H; }
            break;
        default: break;
        }
        if (*sel < top) top = *sel;
        if (*sel >= top + ROWS) top = *sel - ROWS + 1;
        if (top != old_top) {
            draw_rows(items, n, top, *sel);
        } else if (*sel != old) {
            draw_row(&items[old], Y_CONTENT + (old - top) * ROW_H, false);
            draw_row(&items[*sel], Y_CONTENT + (*sel - top) * ROW_H, true);
        }
    }
    return LIST_STOP;
}

/* ================================================================ messages */

static ui_color_t kind_color(ui_msg_kind_t k)
{
    return k == UI_MSG_GOOD ? UI_C_GOOD : k == UI_MSG_BAD ? UI_C_BAD : UI_C_ACCENT;
}

void ui_show_message(const char *title, const char *body, ui_msg_kind_t kind)
{
    chrome(title, false, ui_str(S_HELP_OK));
    ui_ring(UI_W / 2, Y_CONTENT + 80, 40, 6, kind_color(kind));
    const char *mark = kind == UI_MSG_GOOD ? "\xE2\x9C\x93" : kind == UI_MSG_BAD ? "!" : "i";
    ui_text_center(UI_W / 2, Y_CONTENT + 64, mark, 4, kind_color(kind), UI_C_BG);
    text_block(16, Y_CONTENT + 148, body ? body : "", UI_C_TEXT, Y_HELP - 8);
}

void ui_run_message(const char *title, const char *body, ui_msg_kind_t kind)
{
    ui_show_message(title, body, kind);
    ui_event_t e;
    while (wait_event(&e)) {
        if (e.type == UI_EV_SELECT || e.type == UI_EV_BACK || e.type == UI_EV_HOME ||
            e.type == UI_EV_TOUCH_TAP || e.type == UI_EV_IDLE_TIMEOUT) {
            return;
        }
    }
}

/** Yes/no that only a centre-button press (physical or emulated) can answer yes. */
static bool run_question(const char *title, const char *body)
{
    chrome(title, false, ui_str(S_HELP_LIST));
    text_block(16, Y_CONTENT + 24, body, UI_C_TEXT, Y_HELP - 8);
    ui_event_t e;
    while (wait_event(&e)) {
        if (e.type == UI_EV_SELECT && e.src != UI_SRC_TOUCH) return true;
        if (e.type == UI_EV_BACK || e.type == UI_EV_HOME || e.type == UI_EV_IDLE_TIMEOUT) return false;
        if (tap_is_back(&e)) return false;
    }
    return false;
}

/* ================================================================ PIN */

#define PIN_DIGITS_Y  (Y_CONTENT + 24)
#define PIN_WHEEL_CY  (Y_CONTENT + 136)
#define PIN_KEYS_Y    (Y_CONTENT + 200)
#define PIN_OK        10           /* wheel position after 9: "✓" */

typedef struct {
    char d[9];
    uint8_t len;
    uint8_t cur;                   /* 0..9 digit, PIN_OK = submit */
    uint8_t keys[12];              /* touch keypad: 0-9 shuffled, 10 = delete, 11 = OK */
} pin_t;

static uint8_t wheel_size(const pin_t *p) { return p->len >= 4 ? 11 : 10; }

static void wheel_label(uint8_t v, char *out)
{
    if (v == PIN_OK) strcpy(out, "\xE2\x9C\x93");
    else { out[0] = (char)('0' + v); out[1] = 0; }
}

static void draw_pin_digits(const pin_t *p)
{
    ui_fill(0, PIN_DIGITS_Y, UI_W, 32, UI_C_BG);
    const int16_t gap = 32, x0 = UI_W / 2 - (8 * gap) / 2 + gap / 2;
    for (int i = 0; i < 8; i++) {
        int16_t cx = (int16_t)(x0 + i * gap);
        if (i < p->len) ui_disc(cx, PIN_DIGITS_Y + 14, 8, UI_C_TEXT);
        else ui_ring(cx, PIN_DIGITS_Y + 14, 8, 2, i < 4 ? UI_C_TEXT2 : UI_C_BORDER);
    }
}

static void draw_pin_wheel(const pin_t *p)
{
    char a[4], b[4], c[4];
    uint8_t m = wheel_size(p);
    ui_fill(0, PIN_WHEEL_CY - 60, UI_W, 120, UI_C_BG);
    ui_ring(UI_W / 2, PIN_WHEEL_CY, 52, 4, UI_C_ACCENT);
    wheel_label((uint8_t)((p->cur + m - 1) % m), a);
    wheel_label(p->cur, b);
    wheel_label((uint8_t)((p->cur + 1) % m), c);
    ui_text_center(UI_W / 2 - 104, PIN_WHEEL_CY - 12, a, 3, UI_C_TEXT2, UI_C_BG);
    ui_text_center(UI_W / 2, PIN_WHEEL_CY - 16, b, 4, UI_C_TEXT, UI_C_BG);
    ui_text_center(UI_W / 2 + 104, PIN_WHEEL_CY - 12, c, 3, UI_C_TEXT2, UI_C_BG);
}

static void key_rect(int i, int16_t *x, int16_t *y)
{
    *x = (int16_t)(16 + (i % 3) * 100);
    *y = (int16_t)(PIN_KEYS_Y + (i / 3) * 38);
}

static void draw_pin_keys(const pin_t *p)
{
    for (int i = 0; i < 12; i++) {
        int16_t x, y;
        key_rect(i, &x, &y);
        uint8_t k = p->keys[i];
        bool off = (k == 11 && p->len < 4);
        ui_round(x, y, 88, 32, 8, UI_C_ROW);
        char lbl[4];
        if (k < 10) { lbl[0] = (char)('0' + k); lbl[1] = 0; }
        else strcpy(lbl, k == 10 ? "<" : "\xE2\x9C\x93");
        ui_text_center(x + 44, y + 8, lbl, TXT, off ? UI_C_DISABLED : UI_C_TEXT, UI_C_ROW);
    }
}

static void pin_add(pin_t *p, uint8_t digit)
{
    if (p->len < 8) p->d[p->len++] = (char)('0' + digit);
    p->d[p->len] = 0;
}

ui_pin_result_t ui_run_pin(const char *title, const char *note, char out[9])
{
    pin_t p;
    memset(&p, 0, sizeof(p));
    p.cur = (uint8_t)(P->random32() % 10);   /* random start: turns do not reveal the PIN */
    for (int i = 0; i < 10; i++) p.keys[i] = (uint8_t)i;
    for (int i = 9; i > 0; i--) {                        /* Fisher-Yates */
        int j = (int)(P->random32() % (uint32_t)(i + 1));
        uint8_t t = p.keys[i]; p.keys[i] = p.keys[j]; p.keys[j] = t;
    }
    /* keypad layout: 9 digits, then delete, last digit, OK */
    p.keys[11] = 11;
    p.keys[10] = p.keys[9];
    p.keys[9] = 10;

    chrome(title, false, ui_str(S_HELP_PIN));
    if (note) ui_text_center(UI_W / 2, Y_CONTENT + 4, note, TXT, UI_C_WARN, UI_C_BG);
    draw_pin_digits(&p);
    draw_pin_wheel(&p);
    if (S.touch) draw_pin_keys(&p);

    ui_event_t e;
    ui_pin_result_t res = UI_PIN_STOPPED;
    while (wait_event(&e)) {
        bool changed = false, submit = false;
        uint8_t m = wheel_size(&p);
        switch (e.type) {
        case UI_EV_ROTATE: {
            int v = ((int)p.cur + e.steps) % m;
            p.cur = (uint8_t)(v < 0 ? v + m : v);
            changed = true;
            break;
        }
        case UI_EV_SELECT:
            if (p.cur == PIN_OK) submit = true;
            else { pin_add(&p, p.cur); changed = true; }
            break;
        case UI_EV_BACK:
            if (p.len == 0) { res = UI_PIN_CANCELLED; goto out; }
            p.d[--p.len] = 0;
            changed = true;
            break;
        case UI_EV_TOUCH_TAP:
            for (int i = 0; i < 12; i++) {
                int16_t x, y;
                key_rect(i, &x, &y);
                if (e.x >= x && e.x < x + 88 && e.y >= y && e.y < y + 32) {
                    uint8_t k = p.keys[i];
                    if (k < 10) pin_add(&p, k);
                    else if (k == 10 && p.len) p.d[--p.len] = 0;
                    else if (k == 11 && p.len >= 4) submit = true;
                    changed = true;
                }
            }
            break;
        default: break;
        }
        if (p.cur >= wheel_size(&p)) p.cur = 0; /* "✓" vanished after deleting a digit */
        if (submit && p.len >= 4) {
            memcpy(out, p.d, 9);
            res = UI_PIN_OK;
            goto out;
        }
        if (changed) {
            draw_pin_digits(&p);
            draw_pin_wheel(&p);
            if (S.touch) draw_pin_keys(&p);
        }
    }
out:
    memset(&p, 0, sizeof(p));
    return res;
}

/* ================================================================ confirm (signing) */

#define CF_TOP      (Y_CONTENT + 12)
#define CF_BOTTOM   (Y_HELP - 36)
#define CF_MAX_PAGES 8

static int confirm_pages(const ui_confirm_t *c, uint8_t first[CF_MAX_PAGES + 1])
{
    int pages = 0;
    int16_t y = CF_TOP;
    first[0] = 0;
    for (uint8_t i = 0; i < c->nfields; i++) {
        int16_t h = 22 + text_block_height(c->fields[i].value) + 12;
        if (y + h > CF_BOTTOM && y != CF_TOP && pages < CF_MAX_PAGES - 1) {
            first[++pages] = i;
            y = CF_TOP;
        }
        y += h;
    }
    first[++pages] = c->nfields;
    return pages;
}

static void draw_confirm_page(const ui_confirm_t *c, const uint8_t *first, int page, int pages)
{
    ui_fill(0, Y_CONTENT, UI_W, H_CONTENT, UI_C_BG);
    int16_t y = CF_TOP;
    if (!c->can_approve && page == 0 && c->warning) {
        int16_t h = text_block_height(c->warning) + 14;
        ui_round(8, y, UI_W - 16, h, 8, UI_C_BAD);
        text_block_bg(16, y + 8, c->warning, UI_C_TEXT, UI_C_BAD, y + h);
        y += h + 12;
    }
    for (uint8_t i = first[page]; i < first[page + 1]; i++) {
        ui_text(16, y, c->fields[i].label, TXT, UI_C_TEXT2, UI_C_BG);
        y = text_block(16, y + 22, c->fields[i].value, UI_C_TEXT, CF_BOTTOM);
        y += 12;
    }
    char ind[32];
    snprintf(ind, sizeof(ind), "%s %d/%d", ui_str(S_PAGE), page + 1, pages);
    ui_text_center(UI_W / 2, Y_HELP - 28, ind, TXT, UI_C_TEXT2, UI_C_BG);
    /* Touch can always reject: a button on the right of the title bar. */
    if (S.touch) {
        ui_round(UI_W - 104, Y_TITLE + 6, 96, 36, 8, UI_C_BAD);
        ui_text_center(UI_W - 56, Y_TITLE + 16, ui_str(S_REJECT_BTN), TXT, UI_C_TEXT, UI_C_BAD);
    }
}

static const char *confirm_help(const ui_confirm_t *c, int page, int pages)
{
    if (!c->can_approve) return ui_str(S_HELP_REJECT_ONLY);
    return page == pages - 1 ? ui_str(S_HELP_SIGN) : ui_str(S_HELP_PAGES);
}

ui_confirm_result_t ui_run_confirm(const ui_confirm_t *c)
{
    uint8_t first[CF_MAX_PAGES + 1];
    int pages = confirm_pages(c, first), page = 0;
    ui_fill(0, Y_CONTENT, UI_W, H_CONTENT, UI_C_BG);
    draw_status();
    ui_fill(0, Y_TITLE, UI_W, H_TITLE, UI_C_BG);
    {
        int16_t room = S.touch ? UI_W - 136 : UI_W - 24;
        uint8_t size = ui_text_width(c->title, 3) <= room ? 3 : 2;
        ui_text(12, Y_TITLE + (size == 3 ? 12 : 16), c->title, size, UI_C_TEXT, UI_C_BG);
    }
    ui_fill(0, Y_TITLE + H_TITLE - 1, UI_W, 1, UI_C_BORDER);
    draw_confirm_page(c, first, page, pages);
    draw_help(confirm_help(c, page, pages));

    ui_event_t e;
    while (wait_event(&e)) {
        int old = page;
        bool last = page == pages - 1;
        /* Only the physical centre button approves, only on the last page. */
        bool approve_src = e.src == UI_SRC_BUTTON && last && c->can_approve;
        switch (e.type) {
        case UI_EV_ROTATE:
            page += e.steps > 0 ? 1 : -1;
            break;
        case UI_EV_SELECT:
            if (!last) page++;
            break;
        case UI_EV_HOLD_PROGRESS:
            if (approve_src) draw_hold_bar(e.progress);
            break;
        case UI_EV_HOLD_CANCEL:
            if (approve_src) draw_hold_bar(0);
            break;
        case UI_EV_HOLD_DONE:
            if (approve_src) return UI_CONFIRM_APPROVED;
            break;
        case UI_EV_BACK:
        case UI_EV_HOME:
        case UI_EV_IDLE_TIMEOUT:
            return UI_CONFIRM_REJECTED;
        case UI_EV_TOUCH_TAP:
            if (e.y >= Y_TITLE && e.y < Y_TITLE + H_TITLE && e.x >= UI_W - 104) {
                return UI_CONFIRM_REJECTED;
            }
            if (e.y >= Y_CONTENT && e.y < Y_HELP) page += e.x >= UI_W / 2 ? 1 : -1;
            break;
        default: break;
        }
        if (page < 0) page = 0;
        if (page >= pages) page = pages - 1;
        if (page != old) {
            draw_confirm_page(c, first, page, pages);
            draw_help(confirm_help(c, page, pages));
        }
    }
    return UI_CONFIRM_STOPPED;
}

/* ================================================================ settings */

static void apply_settings(void)
{
    ui_input_set_touch_enabled(S.touch);
    ui_input_set_reverse(S.reverse);
    ui_set_lang((ui_lang_t)S.lang);
    if (P->settings_changed) P->settings_changed(&S);
}

static void show_lines(const char *title, const char *const *lines)
{
    char buf[512];
    size_t n = 0;
    buf[0] = 0;
    for (int i = 0; lines && lines[i] && n < sizeof(buf) - 2; i++) {
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, "%s\n", lines[i]);
    }
    ui_run_message(title, buf, UI_MSG_INFO);
}

static int run_settings(void)
{
    int sel = 0;
    for (;;) {
        char bright[8], lock[12];
        snprintf(bright, sizeof(bright), "%u/5", S.brightness);
        snprintf(lock, sizeof(lock), "%u %s", S.autolock_min, ui_str(S_MINUTES));
        const item_t items[] = {
            {ui_str(S_TOUCH), ui_str(S.touch ? S_ON : S_OFF), false},
            {ui_str(S_WHEEL_DIR), ui_str(S.reverse ? S_CW_UP : S_CW_DOWN), false},
            {ui_str(S_BRIGHTNESS), bright, false},
            {ui_str(S_AUTOLOCK), lock, false},
            {ui_str(S_LANGUAGE), ui_str(S_LANG_NAME), false},
            {ui_str(S_INFO), NULL, false},
        };
        ui_src_t src = UI_SRC_BUTTON;
        int r = run_list(ui_str(S_SETTINGS), true, items, 6, &sel, ui_str(S_HELP_LIST), &src);
        if (r < 0) return r;
        switch (r) {
        case 0:
            if (S.touch && src == UI_SRC_TOUCH &&
                !run_question(ui_str(S_TOUCH_OFF_Q), ui_str(S_TOUCH_OFF_BODY))) {
                break;
            }
            S.touch = !S.touch;
            break;
        case 1: S.reverse = !S.reverse; break;
        case 2: S.brightness = (uint8_t)(S.brightness % 5 + 1); break;
        case 3: S.autolock_min = S.autolock_min == 1 ? 5 : S.autolock_min == 5 ? 15 : 1; break;
        case 4: S.lang = S.lang == UI_LANG_ES ? UI_LANG_EN : UI_LANG_ES; break;
        case 5: show_lines(ui_str(S_INFO), A->identity_lines ? A->identity_lines() : NULL); break;
        }
        apply_settings();
    }
}

/* ================================================================ anchor menu */

static int run_anchor(void)
{
    int sel = 0;
    for (;;) {
        const item_t items[] = {
            {ui_str(S_ANCHOR_ID), NULL, false},
            {ui_str(S_ANCHOR_TEST), NULL, false},
            {ui_str(S_ANCHOR_REGISTER), NULL, true},
            {ui_str(S_ANCHOR_BIND), NULL, true},
            {ui_str(S_ANCHOR_CLAIMS), NULL, true},
        };
        int r = run_list("Anchor", true, items, 5, &sel, ui_str(S_HELP_LIST), NULL);
        if (r < 0) return r;
        if (r == 0) show_lines(ui_str(S_ANCHOR_ID), A->identity_lines ? A->identity_lines() : NULL);
        else if (r == 1 && A->anchor_test) A->anchor_test();
        else ui_run_message(items[r].label, ui_str(S_SOON), UI_MSG_INFO);
    }
}

/* ================================================================ lock and home */

static void lock_screen(void)
{
    for (;;) {
        chrome(ui_str(S_LOCKED), false, ui_str(S_UNLOCK_HINT));
        /* padlock: shackle (ring) behind a body */
        ui_ring(UI_W / 2, Y_CONTENT + 110, 28, 8, UI_C_TEXT2);
        ui_fill(UI_W / 2 - 28, Y_CONTENT + 110, 56, 20, UI_C_BG);
        ui_fill(UI_W / 2 - 28, Y_CONTENT + 108, 8, 22, UI_C_TEXT2);
        ui_fill(UI_W / 2 + 20, Y_CONTENT + 108, 8, 22, UI_C_TEXT2);
        ui_round(UI_W / 2 - 44, Y_CONTENT + 130, 88, 68, 10, UI_C_TEXT2);
        ui_disc(UI_W / 2, Y_CONTENT + 158, 7, UI_C_BG);
        ui_fill(UI_W / 2 - 3, Y_CONTENT + 160, 6, 18, UI_C_BG);
        ui_event_t e;
        do {
            if (!wait_event(&e)) return;
        } while (e.type != UI_EV_SELECT && e.type != UI_EV_TOUCH_TAP);
        if (!A->unlock || A->unlock()) return;
        if (!running()) return;
    }
}

void ui_app_init(const ui_platform_t *p, const ui_actions_t *a, const ui_settings_t *s)
{
    P = p;
    A = a;
    S = *s;
    if (S.brightness < 1 || S.brightness > 5) S.brightness = 3;
    if (S.autolock_min != 1 && S.autolock_min != 15) S.autolock_min = 5;
    ui_input_reset();
    ui_input_set_touch_enabled(S.touch);
    ui_input_set_reverse(S.reverse);
    ui_set_lang((ui_lang_t)S.lang);
    g_last_activity = P->now_ms();
}

const ui_settings_t *ui_app_settings(void) { return &S; }

void ui_app_run(void)
{
    int sel = 0;
    while (running()) {
        const item_t items[] = {
            {ui_str(S_RECEIVE), NULL, false},
            {ui_str(S_SEND), NULL, false},
            {ui_str(S_ANCHOR), NULL, false},
            {ui_str(S_ACCOUNTS), NULL, false},
            {ui_str(S_SETTINGS), NULL, false},
        };
        int r = run_list(ui_str(S_HOME_TITLE), false, items, 5, &sel, ui_str(S_HELP_HOME), NULL);
        if (r == LIST_STOP) return;
        if (r == LIST_HOME || r == LIST_TIMEOUT) {   /* MENU held on Home, or auto-lock */
            lock_screen();
            continue;
        }
        int sub = 0;
        if (r == 2) sub = run_anchor();
        else if (r == 4) sub = run_settings();
        else ui_run_message(items[r].label, ui_str(S_SOON), UI_MSG_INFO);
        /* From a submenu, MENU held (LIST_HOME) or MENU (LIST_BACK) returns to Home. */
        if (sub == LIST_TIMEOUT) lock_screen();
    }
}
