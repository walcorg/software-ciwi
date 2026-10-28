/**
 * @file    ui_strings.c
 * @brief   Interface text. Keep each entry short: 24 characters fit on one line at text size 2.
 */
#include "ui_strings.h"

static ui_lang_t g_lang = UI_LANG_ES;

static const char *const T[S__COUNT][2] = {
    [S_HOME_TITLE] = {"CiWi", "CiWi"},
    [S_RECEIVE] = {"Recibir", "Receive"},
    [S_SEND] = {"Enviar", "Send"},
    [S_ANCHOR] = {"Anchor (CO2in, NFT)", "Anchor (CO2in, NFT)"},
    [S_ACCOUNTS] = {"Cuentas", "Accounts"},
    [S_SETTINGS] = {"Ajustes", "Settings"},
    [S_HELP_LIST] = {"\xE2\x97\x89 Elegir \xC2\xB7 MENU Atr\xC3\xA1s", "\xE2\x97\x89 Select \xC2\xB7 MENU Back"},
    [S_HELP_HOME] = {"\xE2\x97\x89 Elegir \xC2\xB7 MENU 2s Bloquear", "\xE2\x97\x89 Select \xC2\xB7 MENU 2s Lock"},
    [S_HELP_PAGES] = {"\xE2\x97\x89 Siguiente \xC2\xB7 MENU Rechazar", "\xE2\x97\x89 Next \xC2\xB7 MENU Reject"},
    [S_HELP_SIGN] = {"Mant\xC3\xA9n \xE2\x97\x89 2s para firmar", "Hold \xE2\x97\x89 2s to sign"},
    [S_HELP_PIN] = {"\xE2\x97\x89 A\xC3\xB1" "adir \xC2\xB7 MENU Borrar", "\xE2\x97\x89 Add \xC2\xB7 MENU Delete"},
    [S_HELP_OK] = {"\xE2\x97\x89 Aceptar", "\xE2\x97\x89 OK"},
    [S_HELP_DANGER] = {"Mant\xC3\xA9n \xE2\x97\x89 2s \xC2\xB7 MENU Cancelar", "Hold \xE2\x97\x89 2s \xC2\xB7 MENU Cancel"},
    [S_HELP_REJECT_ONLY] = {"MENU Rechazar", "MENU Reject"},
    [S_TOUCH] = {"Pantalla t\xC3\xA1" "ctil", "Touch screen"},
    [S_WHEEL_DIR] = {"Sentido rueda", "Wheel"},
    [S_BRIGHTNESS] = {"Brillo", "Brightness"},
    [S_AUTOLOCK] = {"Bloqueo auto.", "Auto-lock"},
    [S_LANGUAGE] = {"Idioma", "Language"},
    [S_INFO] = {"Informaci\xC3\xB3n", "Information"},
    [S_ON] = {"S\xC3\xAD", "On"},
    [S_OFF] = {"No", "Off"},
    [S_CW_DOWN] = {"Normal", "Normal"},
    [S_CW_UP] = {"Invertido", "Reversed"},
    [S_LANG_NAME] = {"Espa\xC3\xB1ol", "English"},
    [S_MINUTES] = {"min", "min"},
    [S_TOUCH_OFF_Q] = {"\xC2\xBF" "Apagar el t\xC3\xA1" "ctil?", "Turn touch off?"},
    [S_TOUCH_OFF_BODY] = {"La rueda puede volver a encenderlo.", "The wheel can turn it back on."},
    [S_SOON] = {"Disponible en la siguiente versi\xC3\xB3n.", "Coming in the next version."},
    [S_PIN_ENTER] = {"PIN", "PIN"},
    [S_PIN_NEW] = {"Nuevo PIN", "New PIN"},
    [S_PIN_REPEAT] = {"Repite el PIN", "Repeat the PIN"},
    [S_PIN_WRONG] = {"PIN incorrecto", "Wrong PIN"},
    [S_PIN_LEFT] = {"Quedan %u intentos", "%u attempts left"},
    [S_PIN_MISMATCH] = {"Los PIN no coinciden", "The PINs do not match"},
    [S_PIN_LOCKED] = {"Sin intentos: usa tu copia de la semilla.", "No attempts left: use your seed backup."},
    [S_REJECTED] = {"Rechazado", "Rejected"},
    [S_SIGNED] = {"Firmado", "Signed"},
    [S_REJECT_BTN] = {"Rechazar", "Reject"},
    [S_LOCKED] = {"Bloqueada", "Locked"},
    [S_UNLOCK_HINT] = {"\xE2\x97\x89 para desbloquear", "\xE2\x97\x89 to unlock"},
    [S_ANCHOR_ID] = {"Identidad CiWi", "CiWi identity"},
    [S_ANCHOR_TEST] = {"Probar firmas", "Test signatures"},
    [S_ANCHOR_REGISTER] = {"Registrar (web)", "Register (web)"},
    [S_ANCHOR_BIND] = {"Vincular shield", "Bind shield"},
    [S_ANCHOR_CLAIMS] = {"Claims pendientes", "Pending claims"},
    [S_INVALID_SHIELD] = {"Este shield no firm\xC3\xB3 estos datos.", "This shield did not sign this data."},
    [S_PAGE] = {"P\xC3\xA1gina", "Page"},
    [S_FIRMWARE] = {"Firmware", "Firmware"},
    [S_SIGN_HOLD_TITLE] = {"Firmar", "Sign"},
    [S_DONE] = {"Listo", "Done"},
};

void ui_set_lang(ui_lang_t lang) { g_lang = lang == UI_LANG_EN ? UI_LANG_EN : UI_LANG_ES; }
ui_lang_t ui_lang(void) { return g_lang; }

const char *ui_str(ui_str_id_t id)
{
    if (id >= S__COUNT || !T[id][g_lang]) return "?";
    return T[id][g_lang];
}
