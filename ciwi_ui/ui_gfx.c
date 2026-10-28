/**
 * @file    ui_gfx.c
 * @brief   Drawing for the CiWi interface. See ui_gfx.h.
 */
#include "ui_gfx.h"

#include <string.h>

extern const uint8_t font5x7[95][5];

/* Extra glyphs, same format as font5x7 (5 columns, bit 0 = top row). Lower-case accents use
 * rows 0-1, which are empty in lower-case letters. */
typedef struct {
    uint32_t cp;
    uint8_t col[5];
} glyph_t;

static const glyph_t EXTRA[] = {
    {0x00E1, {0x20, 0x54, 0x56, 0x55, 0x78}}, /* á */
    {0x00E9, {0x38, 0x54, 0x56, 0x55, 0x18}}, /* é */
    {0x00ED, {0x00, 0x44, 0x7C, 0x42, 0x01}}, /* í */
    {0x00F3, {0x38, 0x44, 0x46, 0x45, 0x38}}, /* ó */
    {0x00FA, {0x3C, 0x40, 0x42, 0x21, 0x7C}}, /* ú */
    {0x00FC, {0x3C, 0x41, 0x40, 0x21, 0x7C}}, /* ü */
    {0x00F1, {0x7E, 0x09, 0x06, 0x05, 0x78}}, /* ñ */
    {0x00D1, {0x7E, 0x05, 0x09, 0x12, 0x7D}}, /* Ñ */
    {0x00BF, {0x30, 0x48, 0x45, 0x40, 0x20}}, /* ¿ */
    {0x00A1, {0x00, 0x00, 0x7D, 0x00, 0x00}}, /* ¡ */
    {0x00B7, {0x00, 0x00, 0x08, 0x00, 0x00}}, /* · */
    {0x2026, {0x40, 0x00, 0x40, 0x00, 0x40}}, /* … */
    {0x2039, {0x00, 0x08, 0x14, 0x22, 0x00}}, /* ‹ */
    {0x203A, {0x00, 0x22, 0x14, 0x08, 0x00}}, /* › */
    {0x2713, {0x10, 0x20, 0x10, 0x08, 0x04}}, /* ✓ */
    {0x25CF, {0x1C, 0x3E, 0x3E, 0x3E, 0x1C}}, /* ● */
    {0x25C9, {0x1C, 0x22, 0x2A, 0x22, 0x1C}}, /* ◉ (centre button) */
    {0x21BB, {0x0E, 0x11, 0x51, 0x31, 0x72}}, /* ↻ */
};

/* Upper-case accented letters: drawn as the plain letter (no room above capitals in 5x7). */
static uint32_t fold(uint32_t cp)
{
    switch (cp) {
    case 0x00C1: return 'A';
    case 0x00C9: return 'E';
    case 0x00CD: return 'I';
    case 0x00D3: return 'O';
    case 0x00DA: case 0x00DC: return 'U';
    default: return cp;
    }
}

static const uint8_t *glyph(uint32_t cp)
{
    cp = fold(cp);
    if (cp >= 32 && cp <= 126) return font5x7[cp - 32];
    for (unsigned i = 0; i < sizeof(EXTRA) / sizeof(EXTRA[0]); i++) {
        if (EXTRA[i].cp == cp) return EXTRA[i].col;
    }
    return font5x7['?' - 32];
}

/** Decode one UTF-8 code point; advances *s. Invalid bytes become '?'. */
static uint32_t next_cp(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t cp;
    int n;
    if (p[0] < 0x80) { cp = p[0]; n = 1; }
    else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F); n = 2;
    } else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        n = 3;
    } else { cp = '?'; n = 1; }
    *s += n;
    return cp;
}

int ui_utf8_len(const char *s)
{
    int n = 0;
    while (*s) {
        next_cp(&s);
        n++;
    }
    return n;
}

/* ================================================================ primitives */

static int16_t clamp(int16_t v, int16_t lo, int16_t hi) { return v < lo ? lo : v > hi ? hi : v; }

void ui_fill(int16_t x, int16_t y, int16_t w, int16_t h, ui_color_t c)
{
    int16_t x1 = clamp(x + w, 0, UI_W), y1 = clamp(y + h, 0, UI_H);
    x = clamp(x, 0, UI_W);
    y = clamp(y, 0, UI_H);
    if (x1 > x && y1 > y) ui_gfx_backend_fill(x, y, x1 - x, y1 - y, c);
}

void ui_frame(int16_t x, int16_t y, int16_t w, int16_t h, ui_color_t c)
{
    ui_fill(x, y, w, 1, c);
    ui_fill(x, y + h - 1, w, 1, c);
    ui_fill(x, y, 1, h, c);
    ui_fill(x + w - 1, y, 1, h, c);
}

/** Horizontal inset of a rounded corner of radius r at row dy (0 = top). */
static int16_t corner_inset(int16_t r, int16_t dy)
{
    if (dy >= r) return 0;
    int16_t yy = r - dy;            /* distance from the corner centre */
    int16_t xx = 0;
    while ((xx + 1) * (xx + 1) + yy * yy <= r * r) xx++;
    return r - xx;
}

void ui_round(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, ui_color_t c)
{
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    for (int16_t dy = 0; dy < r; dy++) {
        int16_t in = corner_inset(r, dy);
        ui_fill(x + in, y + dy, w - 2 * in, 1, c);
        ui_fill(x + in, y + h - 1 - dy, w - 2 * in, 1, c);
    }
    ui_fill(x, y + r, w, h - 2 * r, c);
}

void ui_round_frame(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, ui_color_t c,
                    int16_t thick, ui_color_t inside)
{
    ui_round(x, y, w, h, r, c);
    ui_round(x + thick, y + thick, w - 2 * thick, h - 2 * thick, r > thick ? r - thick : 0, inside);
}

void ui_disc(int16_t cx, int16_t cy, int16_t r, ui_color_t c)
{
    for (int16_t dy = -r; dy <= r; dy++) {
        int16_t dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
        ui_fill(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}

void ui_ring(int16_t cx, int16_t cy, int16_t r, int16_t thick, ui_color_t c)
{
    int16_t ri = r - thick;
    for (int16_t dy = -r; dy <= r; dy++) {
        int16_t dx = 0, dxi = -1;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
        if (dy > -ri && dy < ri) {
            dxi = 0;
            while ((dxi + 1) * (dxi + 1) + dy * dy < ri * ri) dxi++;
        }
        if (dxi < 0) {
            ui_fill(cx - dx, cy + dy, 2 * dx + 1, 1, c);
        } else {
            ui_fill(cx - dx, cy + dy, dx - dxi, 1, c);
            ui_fill(cx + dxi + 1, cy + dy, dx - dxi, 1, c);
        }
    }
}

/* ================================================================ text */

#define CELL_W 6
#define CELL_H 8

int16_t ui_text_width(const char *s, uint8_t size)
{
    int n = ui_utf8_len(s);
    return n ? (int16_t)(n * CELL_W * size - size) : 0; /* no spacing after the last one */
}

int16_t ui_text(int16_t x, int16_t y, const char *s, uint8_t size, ui_color_t fg, ui_color_t bg)
{
    /* One character cell at a time, sent as a block: one SPI window per character. */
    ui_color_t buf[CELL_W * 4 * CELL_H * 4];
    if (size < 1) size = 1;
    if (size > 4) size = 4;
    const int16_t cw = CELL_W * size, ch = CELL_H * size;
    while (*s) {
        const uint8_t *g = glyph(next_cp(&s));
        if (x + cw > UI_W + size) break; /* clip at the right edge */
        for (int16_t py = 0; py < ch; py++) {
            int row = py / size;
            for (int16_t px = 0; px < cw; px++) {
                int col = px / size;
                bool on = col < 5 && row < 7 && (g[col] >> row) & 1;
                buf[py * cw + px] = on ? fg : bg;
            }
        }
        int16_t w = (x + cw > UI_W) ? UI_W - x : cw;
        if (w == cw && x >= 0 && y >= 0 && y + ch <= UI_H) {
            ui_gfx_backend_blit(x, y, cw, ch, buf);
        } else if (w > 0) {
            for (int16_t py = 0; py < ch; py++) {
                for (int16_t px = 0; px < w; px++) ui_fill(x + px, y + py, 1, 1, buf[py * cw + px]);
            }
        }
        x += cw;
    }
    return x;
}

void ui_text_center(int16_t cx, int16_t y, const char *s, uint8_t size, ui_color_t fg,
                    ui_color_t bg)
{
    ui_text(cx - ui_text_width(s, size) / 2, y, s, size, fg, bg);
}

void ui_text_right(int16_t xr, int16_t y, const char *s, uint8_t size, ui_color_t fg,
                   ui_color_t bg)
{
    ui_text(xr - ui_text_width(s, size), y, s, size, fg, bg);
}
