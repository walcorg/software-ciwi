/**
 * @file    ui_test.c
 * @brief   CiWi interface on a PC: scripted input, checks, and screenshots.
 *
 * The interface code (ciwi_ui/) is compiled unchanged. The graphics backend draws into a 320x480
 * framebuffer; the platform runs a simulated clock and replays a script of button presses, wheel
 * turns and touches. Screenshots are written as PPM files to shots/ (make test converts them to
 * PNG and builds a contact sheet).
 *
 *   ./ui_test [shots-dir]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ui_app.h"
#include "ui_gfx.h"
#include "ui_input.h"

/* ================================================================ framebuffer backend */

static ui_color_t fb[UI_W * UI_H];

void ui_gfx_backend_fill(int16_t x, int16_t y, int16_t w, int16_t h, ui_color_t c)
{
    for (int16_t j = 0; j < h; j++)
        for (int16_t i = 0; i < w; i++) fb[(y + j) * UI_W + x + i] = c;
}

void ui_gfx_backend_blit(int16_t x, int16_t y, int16_t w, int16_t h, const ui_color_t *px)
{
    for (int16_t j = 0; j < h; j++) memcpy(&fb[(y + j) * UI_W + x], px + j * w, (size_t)w * 2);
}

static const char *g_dir = "shots";
static int g_shot;

static void snap(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%02d-%s.ppm", g_dir, ++g_shot, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", UI_W, UI_H);
    for (int i = 0; i < UI_W * UI_H; i++) {
        ui_color_t c = fb[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 8) & 0xF8), (uint8_t)((c >> 3) & 0xFC),
                          (uint8_t)((c << 3) & 0xF8)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

/** True if the pixel at (x, y) has colour c (to check what a screen shows). */
static bool pixel_is(int x, int y, ui_color_t c) { return fb[y * UI_W + x] == c; }

/* ================================================================ checks */

static int g_pass, g_fail;
#define EXPECT(name, cond)                                                         \
    do {                                                                           \
        if (cond) { g_pass++; printf("  PASS  %s\n", name); }                      \
        else { g_fail++; printf("  FAIL  %s  (line %d)\n", name, __LINE__); }      \
    } while (0)

/* ================================================================ simulated platform + script */

typedef enum {
    A_DOWN, A_UP,            /* a = button, b = source */
    A_ROT,                   /* a = steps, b = source */
    A_TAP,                   /* a = x, b = y */
    A_SNAP,                  /* s = name */
    A_STOP,
} act_t;

typedef struct {
    uint32_t dt;             /* ms after the previous step */
    act_t act;
    int a, b;
    const char *s;
} step_t;

static uint32_t g_now;
static const step_t *g_script;
static int g_n, g_i;
static uint32_t g_next_at;
static bool g_stop;
static ui_settings_t g_saved;
static int g_unlocks;

static void run_script(const step_t *s, int n)
{
    g_script = s;
    g_n = n;
    g_i = 0;
    g_stop = false;
    g_next_at = g_now + (n ? s[0].dt : 0);
}

static uint32_t now_ms(void) { return g_now; }

static void poll(void)
{
    g_now += 10;
    while (!g_stop && g_i < g_n && g_now >= g_next_at) {
        const step_t *st = &g_script[g_i++];
        switch (st->act) {
        case A_DOWN: ui_input_button((ui_button_t)st->a, true, g_now, (ui_src_t)st->b); break;
        case A_UP: ui_input_button((ui_button_t)st->a, false, g_now, (ui_src_t)st->b); break;
        case A_ROT: ui_input_rotate((int16_t)st->a, (ui_src_t)st->b); break;
        case A_TAP:
            ui_input_touch(true, (int16_t)st->a, (int16_t)st->b, g_now);
            ui_input_touch(false, 0, 0, g_now + 50);
            break;
        case A_SNAP: snap(st->s); break;
        case A_STOP: g_stop = true; break;
        }
        if (g_i < g_n) g_next_at = g_now + g_script[g_i].dt;
    }
    if (g_i >= g_n) g_stop = true;
}

static bool running(void) { return !g_stop; }
static uint32_t rnd_state = 12345;
static uint32_t random32(void) { rnd_state = rnd_state * 1103515245u + 12345u; return rnd_state >> 8; }
static void settings_changed(const ui_settings_t *s) { g_saved = *s; }
static const char *status_text(void) { return "USB \xC2\xB7 TROPIC01"; }

static bool unlock(void) { g_unlocks++; return true; }
static const char *const ID_LINES[] = {"ciwiId", "0x2fa1...c09e", "Firmware 0.5.0", "TROPIC01 2.2.0",
                                       NULL};
static const char *const *identity(void) { return ID_LINES; }

static const ui_platform_t PLAT = {now_ms, poll, random32, settings_changed, status_text, running};
static const ui_actions_t ACT = {unlock, NULL, NULL, identity, "0.5.0"};

#define BTN UI_SRC_BUTTON
#define EMU UI_SRC_EMULATED
#define WHEEL UI_SRC_WHEEL
#define C UI_BTN_CENTER
#define M UI_BTN_MENU
/* a click = down, then up 100 ms later */
#define CLICK(btn) {50, A_DOWN, btn, BTN, 0}, {100, A_UP, btn, BTN, 0}
#define HOLD(btn, ms) {50, A_DOWN, btn, BTN, 0}, {ms, A_UP, btn, BTN, 0}

static void fresh(bool touch)
{
    ui_settings_t s = {.touch = touch, .reverse = false, .brightness = 3, .autolock_min = 5,
                       .lang = UI_LANG_ES};
    ui_app_init(&PLAT, &ACT, &s);
}

/* ================================================================ scenarios */

static void test_home_and_menus(void)
{
    printf("== Home, menus, back ==\n");
    fresh(true);
    const step_t sc[] = {
        {100, A_SNAP, 0, 0, "home"},
        {50, A_ROT, 2, WHEEL, 0},
        {50, A_SNAP, 0, 0, "home-anchor-selected"},
        CLICK(C),
        {50, A_SNAP, 0, 0, "anchor-menu"},
        CLICK(C),                                   /* CiWi identity */
        {50, A_SNAP, 0, 0, "identity"},
        CLICK(C),                                   /* close the message */
        {50, A_ROT, 2, WHEEL, 0},
        CLICK(C),                                   /* "Registrar" = not yet */
        {50, A_SNAP, 0, 0, "soon"},
        CLICK(M),                                   /* close */
        CLICK(M),                                   /* back to Home */
        {50, A_SNAP, 0, 0, "home-again"},
        {50, A_STOP, 0, 0, 0},
    };
    run_script(sc, sizeof(sc) / sizeof(sc[0]));
    ui_app_run();
    EXPECT("home highlights row 3 after two wheel steps", pixel_is(10, 80 + 2 * 48 + 20, UI_C_ACCENT));
}

static void test_settings_touch(void)
{
    printf("== Settings: touch off needs the centre button ==\n");
    fresh(true);
    const step_t sc[] = {
        {100, A_ROT, 4, WHEEL, 0},
        CLICK(C),                                   /* Settings */
        {50, A_SNAP, 0, 0, "settings"},
        {50, A_TAP, 160, 80 + 24, 0},               /* tap "Pantalla táctil" */
        {50, A_SNAP, 0, 0, "touch-off-question"},
        {50, A_TAP, 160, 200, 0},                   /* a tap cannot confirm */
        CLICK(C),                                   /* the button does */
        {50, A_SNAP, 0, 0, "settings-touch-off"},
        {50, A_TAP, 160, 80 + 24, 0},               /* touch is off: ignored */
        CLICK(C),                                   /* wheel/button: back on, no question */
        {50, A_ROT, 1, WHEEL, 0},
        CLICK(C),                                   /* wheel direction: now reversed */
        {50, A_ROT, -1, WHEEL, 0},                  /* so the wheel turns the other way */
        CLICK(C),                                   /* brightness 3 -> 4 */
        {50, A_ROT, -2, WHEEL, 0},
        CLICK(C),                                   /* language -> English */
        {50, A_SNAP, 0, 0, "settings-english"},
        {50, A_STOP, 0, 0, 0},
    };
    run_script(sc, sizeof(sc) / sizeof(sc[0]));
    ui_app_run();
    const ui_settings_t *s = ui_app_settings();
    EXPECT("touch back on", s->touch);
    EXPECT("wheel reversed", s->reverse);
    EXPECT("brightness 4", s->brightness == 4);
    EXPECT("language English", s->lang == UI_LANG_EN);
    EXPECT("settings saved through the platform", g_saved.brightness == 4 && g_saved.lang == UI_LANG_EN);
    ui_set_lang(UI_LANG_ES);
}

static void test_pin(void)
{
    printf("== PIN with the wheel ==\n");
    fresh(false);
    /* random32() decides the start digit; read it the same way ui_run_pin does */
    uint32_t saved = rnd_state;
    uint32_t start = random32() % 10;
    rnd_state = saved;
    int to2 = (int)(2 - start);
    const step_t sc[] = {
        {100, A_ROT, to2, WHEEL, 0}, CLICK(C),        /* 2 */
        {50, A_ROT, 2, WHEEL, 0}, CLICK(C),           /* 4 */
        {50, A_ROT, 2, WHEEL, 0}, CLICK(C),           /* 6 */
        {50, A_ROT, 1, WHEEL, 0}, CLICK(C),           /* 7 (wrong on purpose) */
        {50, A_SNAP, 0, 0, "pin-4-digits"},
        CLICK(M),                                     /* delete it */
        {50, A_ROT, 1, WHEEL, 0}, CLICK(C),           /* 8 */
        {50, A_ROT, 2, WHEEL, 0},                     /* after 9 comes ✓ */
        {50, A_SNAP, 0, 0, "pin-ok-mark"},
        CLICK(C),
        {50, A_STOP, 0, 0, 0},
    };
    run_script(sc, sizeof(sc) / sizeof(sc[0]));
    char pin[9] = {0};
    ui_pin_result_t r = ui_run_pin("PIN", "Quedan 9 intentos", pin);
    EXPECT("PIN entered with the wheel = 2468", r == UI_PIN_OK && strcmp(pin, "2468") == 0);

    fresh(true);
    const step_t sc2[] = {{100, A_SNAP, 0, 0, "pin-touch-keypad"}, CLICK(M), {50, A_STOP, 0, 0, 0}};
    run_script(sc2, 3);
    r = ui_run_pin("PIN", NULL, pin);
    EXPECT("MENU on an empty PIN cancels", r == UI_PIN_CANCELLED);
}

static const ui_field_t CLAIM[] = {
    {"Shield", "Nodo 3 \xC2\xB7 0x1f12...d19b"},
    {"Ventana", "2026-10-28 08:00 a 08:20 (hora local)"},
    {"CO2 capturado", "123.457 kg"},
    {"CO2in estimados", "123.45 CO2in"},
    {"Due\xC3\xB1o", "0xc3ca d1d8 dfe6 edf4 fb02 0910 171e 252c 333a"},
    {"ciwiCounter", "3"},
};

static void test_confirm(void)
{
    printf("== Signing: only the physical centre button, held 2 s ==\n");
    fresh(true);
    ui_confirm_t c = {"Claim CO2in", CLAIM, 6, true, NULL};
    const step_t sc[] = {
        {100, A_SNAP, 0, 0, "claim-page1"},
        {50, A_DOWN, C, BTN, 0}, {2500, A_UP, C, BTN, 0},   /* hold on page 1: not the last page */
        {50, A_ROT, 1, WHEEL, 0},
        {50, A_SNAP, 0, 0, "claim-last-page"},
        {50, A_DOWN, C, EMU, 0}, {2500, A_UP, C, EMU, 0},   /* emulated hold: ignored */
        {50, A_DOWN, C, BTN, 0}, {1000, A_UP, C, BTN, 0},   /* released at 1 s: cancelled */
        {50, A_DOWN, C, BTN, 0},
        {1300, A_SNAP, 0, 0, "claim-holding"},
        {900, A_UP, C, BTN, 0},                             /* held 2.2 s in total: signs */
        {300, A_STOP, 0, 0, 0},
    };
    run_script(sc, sizeof(sc) / sizeof(sc[0]));
    ui_confirm_result_t r = ui_run_confirm(&c);
    EXPECT("approved only after a 2 s physical hold on the last page", r == UI_CONFIRM_APPROVED);
    /* 11 steps consumed = approval came from the last, physical hold (not from the hold on
     * page 1, the emulated hold or the 1 s hold). */
    EXPECT("approval came from the last hold, not an earlier one", g_i == 11);

    fresh(true);
    const step_t sc2[] = {{100, A_ROT, 5, WHEEL, 0}, CLICK(M), {50, A_STOP, 0, 0, 0}};
    run_script(sc2, 4);
    EXPECT("MENU rejects", ui_run_confirm(&c) == UI_CONFIRM_REJECTED);

    fresh(true);
    const step_t sc3[] = {{100, A_ROT, 5, WHEEL, 0}, {50, A_TAP, 270, 56, 0}, {50, A_STOP, 0, 0, 0}};
    run_script(sc3, 3);
    EXPECT("touch \"Rechazar\" rejects", ui_run_confirm(&c) == UI_CONFIRM_REJECTED);

    fresh(true);
    const step_t sc4[] = {{100, A_ROT, 5, WHEEL, 0},
                          {50, A_TAP, 280, 200, 0}, {50, A_TAP, 280, 200, 0},
                          {50, A_TAP, 160, 300, 0}, {50, A_TAP, 160, 300, 0},
                          {2600, A_STOP, 0, 0, 0}};
    run_script(sc4, 6);
    EXPECT("touch alone can never approve", ui_run_confirm(&c) == UI_CONFIRM_STOPPED);

    printf("== Shield signature invalid: only reject ==\n");
    fresh(true);
    ui_confirm_t bad = {"Claim CO2in", CLAIM, 3, false, "Este shield no firm\xC3\xB3 estos datos."};
    const step_t sc5[] = {
        {100, A_SNAP, 0, 0, "claim-invalid-shield"},
        {50, A_ROT, 5, WHEEL, 0},
        {50, A_DOWN, C, BTN, 0}, {2500, A_UP, C, BTN, 0},
        {50, A_STOP, 0, 0, 0},
    };
    run_script(sc5, sizeof(sc5) / sizeof(sc5[0]));
    EXPECT("invalid shield data cannot be approved", ui_run_confirm(&bad) == UI_CONFIRM_STOPPED);
}

static void test_lock(void)
{
    printf("== Lock: MENU held on Home, and auto-lock ==\n");
    fresh(false);
    int before = g_unlocks;
    const step_t sc[] = {
        HOLD(M, 2200),                               /* lock */
        {50, A_SNAP, 0, 0, "locked"},
        CLICK(C),                                    /* unlock -> PIN callback */
        {50, A_ROT, 4, WHEEL, 0}, CLICK(C),          /* into Settings */
        {6 * 60 * 1000, A_SNAP, 0, 0, "autolocked"}, /* 6 min idle > 5 min auto-lock */
        CLICK(C),
        {50, A_STOP, 0, 0, 0},
    };
    run_script(sc, sizeof(sc) / sizeof(sc[0]));
    ui_app_run();
    EXPECT("two unlocks (manual lock, then auto-lock)", g_unlocks - before == 2);

    printf("== Auto-lock rejects a pending signature ==\n");
    fresh(false);
    ui_confirm_t c = {"Bind shield", CLAIM, 2, true, NULL};
    const step_t sc2[] = {{6 * 60 * 1000, A_STOP, 0, 0, 0}};
    run_script(sc2, 1);
    EXPECT("idle on a signature screen = rejected", ui_run_confirm(&c) == UI_CONFIRM_REJECTED);
}

static void test_message(void)
{
    fresh(false);
    const step_t sc[] = {{100, A_SNAP, 0, 0, "signed"}, CLICK(C), {50, A_STOP, 0, 0, 0}};
    run_script(sc, 4);
    ui_run_message("Firmado", "Claim firmado con el TROPIC01. ciwiCounter = 3.", UI_MSG_GOOD);
}

int main(int argc, char **argv)
{
    if (argc > 1) g_dir = argv[1];
    mkdir(g_dir, 0755);
    test_home_and_menus();
    test_settings_touch();
    test_pin();
    test_confirm();
    test_lock();
    test_message();
    printf("\n%d passed, %d failed. Screenshots: %s/\n", g_pass, g_fail, g_dir);
    return g_fail ? 1 : 0;
}
