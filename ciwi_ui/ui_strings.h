/**
 * @file    ui_strings.h
 * @brief   Interface text in Spanish and English (Settings > Language).
 */
#ifndef UI_STRINGS_H
#define UI_STRINGS_H

typedef enum { UI_LANG_ES = 0, UI_LANG_EN = 1 } ui_lang_t;

typedef enum {
    S_HOME_TITLE,
    S_RECEIVE, S_SEND, S_ANCHOR, S_ACCOUNTS, S_SETTINGS,
    S_HELP_LIST, S_HELP_HOME, S_HELP_PAGES, S_HELP_SIGN, S_HELP_PIN, S_HELP_OK, S_HELP_DANGER,
    S_HELP_REJECT_ONLY,
    S_TOUCH, S_WHEEL_DIR, S_BRIGHTNESS, S_AUTOLOCK, S_LANGUAGE, S_INFO,
    S_ON, S_OFF, S_CW_DOWN, S_CW_UP, S_LANG_NAME, S_MINUTES,
    S_TOUCH_OFF_Q, S_TOUCH_OFF_BODY,
    S_SOON,
    S_PIN_ENTER, S_PIN_NEW, S_PIN_REPEAT, S_PIN_WRONG, S_PIN_LEFT, S_PIN_MISMATCH, S_PIN_LOCKED,
    S_REJECTED, S_SIGNED, S_REJECT_BTN, S_LOCKED, S_UNLOCK_HINT,
    S_ANCHOR_ID, S_ANCHOR_TEST, S_ANCHOR_REGISTER, S_ANCHOR_BIND, S_ANCHOR_CLAIMS,
    S_INVALID_SHIELD, S_PAGE, S_FIRMWARE, S_SIGN_HOLD_TITLE, S_DONE, S_CHANGE_PIN, S_PIN_OLD,
    S_PIN_CHANGED, S_SETUP,
    S__COUNT
} ui_str_id_t;

void ui_set_lang(ui_lang_t lang);
ui_lang_t ui_lang(void);
const char *ui_str(ui_str_id_t id);

#endif /* UI_STRINGS_H */
