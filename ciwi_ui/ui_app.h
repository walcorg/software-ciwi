/**
 * @file    ui_app.h
 * @brief   CiWi interface: screens and navigation (spec: "CiWi — Especificación de la interfaz").
 *
 * Portable: the platform provides ui_platform_t (time, input polling, settings storage) and the
 * graphics backend (ui_gfx.h). The same code runs on the CiWi and in tests/ui on a PC.
 *
 * The runners (ui_run_*) are blocking: they draw a screen, read events until the user decides,
 * and return. ui_app_run() is the main loop: Home, menus and Settings.
 */
#ifndef UI_APP_H
#define UI_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "ui_input.h"
#include "ui_strings.h"

typedef struct {
    bool touch;            /**< touch screen enabled */
    bool reverse;          /**< wheel direction reversed */
    uint8_t brightness;    /**< 1..5 */
    uint8_t autolock_min;  /**< 1, 5 or 15 */
    uint8_t lang;          /**< ui_lang_t */
} ui_settings_t;

typedef struct {
    uint32_t (*now_ms)(void);
    /** Read the hardware and feed ui_input_*(). */
    void (*poll)(void);
    /** Random number (PIN wheel start digit, keypad order). */
    uint32_t (*random32)(void);
    /** Called after any change in Settings: apply brightness, store the settings. */
    void (*settings_changed)(const ui_settings_t *s);
    /** Text for the left of the status bar (e.g. "USB · TROPIC01"). May be NULL. */
    const char *(*status_text)(void);
    /** Test bench only: return false to make every runner return. May be NULL. */
    bool (*running)(void);
} ui_platform_t;

typedef struct {
    /** Unlock: ask for the PIN and check it (TROPIC01). Returns true when unlocked. */
    bool (*unlock)(void);
    /** Anchor > Test signatures. */
    void (*anchor_test)(void);
    /** Settings > Change PIN (asks for the old PIN and the new one twice). May be NULL. */
    void (*change_pin)(void);
    /** Lines for Anchor > CiWi identity and Settings > Information (NULL-terminated). */
    const char *const *(*identity_lines)(void);
    const char *fw_version;
} ui_actions_t;

/* ---------------------------------------------------------------- setup and main loop */

void ui_app_init(const ui_platform_t *p, const ui_actions_t *a, const ui_settings_t *s);
const ui_settings_t *ui_app_settings(void);
/** Home, menus, Settings, auto-lock. Returns only when platform->running() turns false. */
void ui_app_run(void);

/* ---------------------------------------------------------------- blocking screens */

typedef enum {
    UI_PIN_OK = 0,
    UI_PIN_CANCELLED,      /**< MENU on an empty PIN */
    UI_PIN_STOPPED,        /**< test bench stopped */
} ui_pin_result_t;

/**
 * PIN entry with the digit wheel (and a shuffled keypad when touch is on).
 * @param note  optional second line, e.g. "Quedan 9 intentos" (NULL = none)
 * @param out   receives 4..8 digits and a terminating 0
 */
ui_pin_result_t ui_run_pin(const char *title, const char *note, char out[9]);

typedef struct {
    const char *label;
    const char *value;
} ui_field_t;

typedef struct {
    const char *title;
    const ui_field_t *fields;
    uint8_t nfields;
    /** false = the data failed verification: show `warning` and allow only rejecting. */
    bool can_approve;
    const char *warning;
} ui_confirm_t;

typedef enum {
    UI_CONFIRM_REJECTED = 0,
    UI_CONFIRM_APPROVED,
    UI_CONFIRM_STOPPED,
} ui_confirm_result_t;

/**
 * Show what is about to be signed, page by page. Approving = holding the PHYSICAL centre button
 * for 2 s on the last page. Touch and emulated input can read and reject, never approve.
 * Auto-lock while waiting = rejected.
 */
ui_confirm_result_t ui_run_confirm(const ui_confirm_t *c);

typedef enum { UI_MSG_INFO, UI_MSG_GOOD, UI_MSG_BAD } ui_msg_kind_t;

/** A short message; waits for ◉, MENU or a tap. */
void ui_run_message(const char *title, const char *body, ui_msg_kind_t kind);

/** Show a message without waiting (e.g. "Firmando…"). */
void ui_show_message(const char *title, const char *body, ui_msg_kind_t kind);

#endif /* UI_APP_H */
