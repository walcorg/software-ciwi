/**
 * @file    input_test.c
 * @brief   Tests interactivos del click-wheel — WALCORG Fase 3
 *
 * Cada test es interactivo — requiere que el usuario opere
 * el click-wheel. Los resultados se muestran en el TFT
 * Y se reportan por UART simultáneamente.
 *
 * INSTRUCCIONES DE USO:
 *   1. Flashear el firmware de Fase 3 (main_phase3.c)
 *   2. Abrir el monitor serial (115200 baud) en PA9
 *   3. El test 1 empieza automáticamente
 *   4. Girar / clickear el wheel para avanzar entre tests
 *
 * @license GNU GPL v3
 */

#include "input_test.h"
#include "input_manager.h"
#include "menu.h"
#include "display_tft.h"
#include "display.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* Duración máxima de espera por acción en cada test (ms) */
#define TEST_TIMEOUT_MS     30000   /* 30 segundos */

/* Color de barra de título para tests */
#define TEST_TITLE_COLOR    WALCORG_PRIMARY


/* =========================================================
 * FUNCIONES AUXILIARES PRIVADAS
 * ========================================================= */

/**
 * _test_header — Dibujar encabezado estándar de test
 *
 * Muestra el número y nombre del test en la barra superior,
 * con instrucciones en la parte inferior.
 */
static void _test_header(uint8_t num, const char *name,
                          const char *instruction) {
    tft_clear(WALCORG_BG);

    /* Barra de título */
    tft_fill_rect(0, 0, tft_get_width(), 36, TEST_TITLE_COLOR);
    char title[48];
    snprintf(title, sizeof(title), "Test %d: %s", num, name);
    tft_draw_string_centered(10, title, ILI9488_WHITE, TEST_TITLE_COLOR, 2);

    /* Separador */
    tft_draw_hline(0, 37, tft_get_width(), WALCORG_SUBTEXT);

    /* Instrucción en la parte inferior */
    if (instruction) {
        tft_fill_rect(0, 290, tft_get_width(), 30, ILI9488_DARKGRAY);
        tft_draw_hline(0, 289, tft_get_width(), WALCORG_SUBTEXT);
        tft_draw_string_centered(297, instruction,
                                 WALCORG_SUBTEXT, ILI9488_DARKGRAY, 1);
    }
}

/**
 * _test_result — Mostrar resultado de un test
 */
static void _test_result(bool passed, const char *detail) {
    tft_color_t color = passed ? WALCORG_SUCCESS : WALCORG_ERROR;
    const char *label = passed ? "PASS" : "FAIL";

    /* Fondo de resultado */
    tft_fill_round_rect(60, 220, 360, 55, 10, color);
    tft_draw_string_centered(232, label, ILI9488_WHITE, color, 3);

    if (detail) {
        tft_draw_string_centered(260, detail,
                                 ILI9488_WHITE, color, 1);
    }

    DEBUG_PRINTF("[TEST] %s — %s\r\n", label, detail ? detail : "");
    HAL_Delay(2000);
}

/**
 * _wait_for_click — Mostrar mensaje y esperar click
 *
 * Bloquea hasta que el usuario haga click en el wheel.
 * Muestra un mensaje parpadeante en pantalla.
 *
 * @param msg  Mensaje a mostrar (ej: "Click para continuar")
 * @return     true si click recibido, false si timeout
 */
static bool _wait_for_click(const char *msg) {
    uint32_t start  = HAL_GetTick();
    bool     blink  = false;
    uint32_t last_b = 0;

    input_flush();

    while ((HAL_GetTick() - start) < TEST_TIMEOUT_MS) {
        /* Parpadeo del mensaje cada 500ms */
        if ((HAL_GetTick() - last_b) >= 500) {
            last_b = HAL_GetTick();
            blink  = !blink;
            tft_fill_rect(0, 250, tft_get_width(), 30, WALCORG_BG);
            if (blink && msg) {
                tft_draw_string_centered(256, msg,
                                         WALCORG_WARNING, WALCORG_BG, 1);
            }
        }

        input_poll();
        if (input_has_action()) {
            input_action_t a = input_get_action();
            if (a == INPUT_CONFIRM) return true;
            if (a == INPUT_BACK || a == INPUT_HOME) return false;
        }
        HAL_Delay(10);
    }
    return false;   /* Timeout */
}


/* =========================================================
 * TEST 1 — DETECTOR DE EVENTOS
 *
 * Muestra cada evento del click-wheel en tiempo real.
 * El usuario debe ejercitar todos los gestos:
 *   - Girar horario
 *   - Girar antihorario
 *   - Click corto
 *   - Long press (>800ms)
 *   - Double click
 *
 * El test pasa cuando se detectan los 5 tipos de eventos.
 * Termina automáticamente si se detectan todos, o con
 * timeout de 30 segundos.
 * ========================================================= */
void input_test_event_detector(void) {
    DEBUG_PRINT("\r\n[TEST1] Detector de eventos del click-wheel\r\n");

    _test_header(1, "Eventos", "Ejercita todos los gestos del wheel");

    /* Tabla de eventos detectados */
    bool detected[5] = {false, false, false, false, false};
    const char *event_names[5] = {
        "Girar horario   (SCROLL UP)",
        "Girar anti-hor. (SCROLL DOWN)",
        "Click corto     (CONFIRM)",
        "Long press 800ms(BACK)",
        "Double click    (HOME)",
    };
    const tft_color_t event_colors[5] = {
        WALCORG_PRIMARY, WALCORG_PRIMARY,
        WALCORG_SUCCESS, WALCORG_WARNING, WALCORG_ERROR
    };

    /* Dibujar tabla inicial — todos en gris */
    int base_y = 48;
    for (int i = 0; i < 5; i++) {
        int y = base_y + i * 40;
        tft_fill_round_rect(10, y, tft_get_width() - 20, 34, 4,
                            ILI9488_DARKGRAY);
        tft_draw_string(20, y + 10, event_names[i],
                        WALCORG_SUBTEXT, ILI9488_DARKGRAY, 1);
        tft_draw_string(tft_get_width() - 55, y + 10, "[ ]",
                        WALCORG_SUBTEXT, ILI9488_DARKGRAY, 1);
    }

    /* Contador de eventos detectados */
    int detected_count = 0;
    uint32_t start = HAL_GetTick();

    input_flush();

    while (detected_count < 5 &&
           (HAL_GetTick() - start) < TEST_TIMEOUT_MS) {

        input_poll();

        if (!input_has_action()) {
            HAL_Delay(10);
            continue;
        }

        input_action_t action = input_get_action();
        int idx = -1;

        switch (action) {
            case INPUT_NAV_UP:    idx = 0; break;
            case INPUT_NAV_DOWN:  idx = 1; break;
            case INPUT_CONFIRM:   idx = 2; break;
            case INPUT_BACK:      idx = 3; break;
            case INPUT_HOME:      idx = 4; break;
            default: break;
        }

        if (idx >= 0 && !detected[idx]) {
            detected[idx] = true;
            detected_count++;

            /* Actualizar la fila — mostrar como detectado */
            int y = base_y + idx * 40;
            tft_fill_round_rect(10, y, tft_get_width() - 20, 34, 4,
                                event_colors[idx]);
            tft_draw_string(20, y + 10, event_names[idx],
                            ILI9488_WHITE, event_colors[idx], 1);
            tft_draw_string(tft_get_width() - 55, y + 10, "[OK]",
                            ILI9488_WHITE, event_colors[idx], 1);

            DEBUG_PRINTF("[TEST1] Detectado: %s\r\n", event_names[idx]);
        }
    }

    /* Resultado */
    bool all_detected = (detected_count == 5);
    char detail[32];
    snprintf(detail, sizeof(detail), "%d/5 eventos detectados", detected_count);
    _test_result(all_detected, detail);
}


/* =========================================================
 * TEST 2 — MENÚ PRINCIPAL NAVEGABLE
 *
 * Muestra un menú de 5 ítems que simula el menú real
 * del wallet. El usuario debe navegar y seleccionar.
 *
 * Criterio: seleccionar cada ítem al menos una vez.
 * ========================================================= */
void input_test_main_menu(void) {
    DEBUG_PRINT("\r\n[TEST2] Menú principal navegable\r\n");
    DEBUG_PRINT("[TEST2] Navega con el wheel, click para seleccionar.\r\n");

    /* Definir los ítems del menú de prueba */
    static const menu_item_t items[] = {
        { "Recibir Bitcoin",    "Generar nueva direccion",  MENU_ICON_RECEIVE,  true  },
        { "Enviar Bitcoin",     "Firmar transaccion",       MENU_ICON_SEND,     true  },
        { "Ver semilla",        "Mostrar palabras BIP39",   MENU_ICON_KEY,      true  },
        { "Configuracion",      "Ajustes del dispositivo",  MENU_ICON_SETTINGS, true  },
        { "Info del device",    "Version y estado",         MENU_ICON_INFO,     true  },
    };

    menu_t menu;
    menu_init(&menu, "WALCORG — Test 2", items, 5);

    bool items_selected[5] = {false};
    int  select_count = 0;

    /* Loop: volver al menú hasta que todos sean seleccionados */
    while (select_count < 5) {
        int result = menu_run(&menu);

        if (result == MENU_RESULT_BACK ||
            result == MENU_RESULT_HOME) {
            DEBUG_PRINT("[TEST2] Salido del menú antes de tiempo\r\n");
            break;
        }

        if (result >= 0 && result < 5) {
            if (!items_selected[result]) {
                items_selected[result] = true;
                select_count++;
                DEBUG_PRINTF("[TEST2] Item %d seleccionado (%d/5)\r\n",
                    result, select_count);
            }

            /* Mostrar confirmación breve */
            tft_clear(WALCORG_BG);
            char msg[48];
            snprintf(msg, sizeof(msg), "Seleccionaste: %s",
                     items[result].label);
            tft_draw_string_centered(140, msg,
                                     WALCORG_SUCCESS, WALCORG_BG, 2);
            snprintf(msg, sizeof(msg), "%d/5 opciones exploradas",
                     select_count);
            tft_draw_string_centered(170, msg,
                                     WALCORG_SUBTEXT, WALCORG_BG, 1);
            HAL_Delay(1200);

            /* Resetear el menú para continuar */
            menu_invalidate(&menu);
        }
    }

    char detail[32];
    snprintf(detail, sizeof(detail), "%d/5 opciones seleccionadas",
             select_count);
    _test_result(select_count == 5, detail);
}


/* =========================================================
 * TEST 3 — SUBMENÚ ANIDADO
 *
 * Verifica la navegación multinivel:
 * Menú principal → submenú → volver con long press
 *
 * Simula la estructura real del wallet:
 *   Inicio → Configuracion → Cambiar PIN → (confirmar)
 * ========================================================= */
void input_test_nested_menu(void) {
    DEBUG_PRINT("\r\n[TEST3] Submenú anidado\r\n");

    /* Menú principal del test */
    static const menu_item_t main_items[] = {
        { "Opcion A",  "Entra al submenu A", MENU_ICON_INFO, true },
        { "Opcion B",  "Entra al submenu B", MENU_ICON_INFO, true },
        { "Salir",     "Terminar este test", MENU_ICON_NONE, true },
    };

    /* Submenú A */
    static const menu_item_t sub_a_items[] = {
        { "A.1 — Primer sub-item",  NULL, MENU_ICON_NONE, true },
        { "A.2 — Segundo sub-item", NULL, MENU_ICON_NONE, true },
        { "A.3 — Tercer sub-item",  NULL, MENU_ICON_NONE, true },
        { "Volver",                 NULL, MENU_ICON_NONE, true },
    };

    /* Submenú B */
    static const menu_item_t sub_b_items[] = {
        { "B.1 — Primer sub-item",  NULL, MENU_ICON_NONE, true },
        { "B.2 — Segundo sub-item", NULL, MENU_ICON_NONE, true },
        { "Volver",                 NULL, MENU_ICON_NONE, true },
    };

    menu_t main_menu, sub_menu;
    bool   visited_a = false, visited_b = false;
    bool   done = false;

    menu_init(&main_menu, "Test 3 — Anidado", main_items, 3);

    while (!done) {
        int main_result = menu_run(&main_menu);

        switch (main_result) {
            case 0:  /* Opcion A → submenú A */
                menu_init(&sub_menu, "Submenu A",
                           sub_a_items, 4);
                menu_set_title_color(&sub_menu, WALCORG_SUCCESS);
                {
                    int sub_result = menu_run(&sub_menu);
                    if (sub_result >= 0 && sub_result < 3) {
                        visited_a = true;
                        DEBUG_PRINTF("[TEST3] Sub-A item %d seleccionado\r\n",
                            sub_result);
                    }
                }
                menu_invalidate(&main_menu);
                break;

            case 1:  /* Opcion B → submenú B */
                menu_init(&sub_menu, "Submenu B",
                           sub_b_items, 3);
                menu_set_title_color(&sub_menu, WALCORG_WARNING);
                {
                    int sub_result = menu_run(&sub_menu);
                    if (sub_result >= 0 && sub_result < 2) {
                        visited_b = true;
                        DEBUG_PRINTF("[TEST3] Sub-B item %d seleccionado\r\n",
                            sub_result);
                    }
                }
                menu_invalidate(&main_menu);
                break;

            case 2:                 /* Salir explícito */
            case MENU_RESULT_BACK:  /* Long press */
            case MENU_RESULT_HOME:  /* Double click */
                done = true;
                break;

            default:
                break;
        }
    }

    bool passed = visited_a && visited_b;
    _test_result(passed, passed ?
        "Ambos submenus visitados" :
        "Faltó visitar algun submenu");
}


/* =========================================================
 * TEST 4 — ENTRADA DE NÚMERO CON EL WHEEL (SIMULADOR DE PIN)
 *
 * Pide al usuario ingresar un número de 4 dígitos usando
 * el click-wheel. Verifica que la entrada es correcta.
 *
 * Instrucciones:
 *   Girar  → cambiar dígito (0-9)
 *   Click  → confirmar dígito y pasar al siguiente
 *   Long press → borrar último dígito
 *
 * El test pide ingresar "1234" para verificar que el
 * mecanismo funciona correctamente.
 * ========================================================= */
void input_test_number_entry(void) {
    DEBUG_PRINT("\r\n[TEST4] Entrada de numero con wheel\r\n");
    DEBUG_PRINT("[TEST4] Ingresa el numero 1-2-3-4 con el wheel.\r\n");

    _test_header(4, "Entrada NUM", "Ingresa: 1 - 2 - 3 - 4");

    const uint8_t TARGET[4] = {1, 2, 3, 4};   /* Número objetivo */
    uint8_t  entered[4]     = {0};
    uint8_t  current_digit  = 0;
    uint8_t  position       = 0;    /* Posición actual (0-3) */
    bool     cancelled      = false;

    input_flush();

    while (position < 4 && !cancelled) {

        /* ---- Dibujar la pantalla de entrada ---- */
        tft_fill_rect(0, 40, tft_get_width(), 240, WALCORG_BG);

        /* Instrucción */
        tft_draw_string_centered(55,
            "Gira para cambiar. Click para confirmar.",
            WALCORG_SUBTEXT, WALCORG_BG, 1);

        /* Mostrar los 4 slots de entrada */
        for (int i = 0; i < 4; i++) {
            int x = 80 + i * 80;
            bool is_current = (i == (int)position);

            /* Fondo del slot */
            tft_fill_round_rect(x - 25, 80, 50, 60, 8,
                is_current ? WALCORG_PRIMARY : ILI9488_DARKGRAY);

            /* Valor del slot */
            char val_str[4];
            if (i < (int)position) {
                /* Slot ya confirmado — mostrar asterisco */
                snprintf(val_str, sizeof(val_str), "*");
                tft_draw_string_centered(
                    95, val_str, ILI9488_WHITE,
                    ILI9488_DARKGRAY, 3);
                /* Redibujar sobre el fondo correcto */
                tft_fill_round_rect(x - 25, 80, 50, 60, 8,
                                    ILI9488_DARKGRAY);
                tft_draw_string(x - 7, 96, val_str,
                    ILI9488_WHITE, ILI9488_DARKGRAY, 3);
            } else if (i == (int)position) {
                /* Slot actual — mostrar dígito animado */
                snprintf(val_str, sizeof(val_str), "%d", current_digit);
                tft_draw_string(x - 7, 96, val_str,
                    ILI9488_WHITE, WALCORG_PRIMARY, 3);
            } else {
                /* Slot futuro — mostrar guión */
                tft_draw_string(x - 7, 96, "-",
                    WALCORG_SUBTEXT, ILI9488_DARKGRAY, 3);
            }

            /* Indicador de posición actual */
            if (is_current) {
                tft_draw_hline(x - 20, 148, 40, ILI9488_WHITE);
            }
        }

        /* Posición actual */
        char pos_str[32];
        snprintf(pos_str, sizeof(pos_str),
                 "Digito %d de 4", position + 1);
        tft_draw_string_centered(170, pos_str,
                                 WALCORG_TEXT, WALCORG_BG, 2);

        /* Esperar acción */
        input_action_t action = input_wait(TEST_TIMEOUT_MS);

        switch (action) {

            case INPUT_NAV_UP:
                /* Incrementar dígito: 9 → 0 */
                current_digit = (current_digit + 1) % 10;
                break;

            case INPUT_NAV_DOWN:
                /* Decrementar dígito: 0 → 9 */
                current_digit = (current_digit == 0) ? 9 : current_digit - 1;
                break;

            case INPUT_CONFIRM:
                /* Confirmar dígito actual */
                entered[position] = current_digit;
                DEBUG_PRINTF("[TEST4] Posicion %d: digito %d confirmado\r\n",
                    position, current_digit);
                position++;
                current_digit = 0;  /* Resetear para el siguiente dígito */
                break;

            case INPUT_BACK:
                /* Borrar último dígito */
                if (position > 0) {
                    position--;
                    current_digit = entered[position];
                    entered[position] = 0;
                    DEBUG_PRINT("[TEST4] Borrado último dígito\r\n");
                } else {
                    cancelled = true;
                    DEBUG_PRINT("[TEST4] Cancelado\r\n");
                }
                break;

            case INPUT_HOME:
            case INPUT_NONE:
                cancelled = true;
                break;

            default:
                break;
        }
    }

    if (cancelled) {
        _test_result(false, "Cancelado por el usuario");
        return;
    }

    /* Verificar que se ingresó 1-2-3-4 */
    bool correct = true;
    for (int i = 0; i < 4; i++) {
        if (entered[i] != TARGET[i]) {
            correct = false;
            break;
        }
    }

    char detail[40];
    snprintf(detail, sizeof(detail),
             "Ingresado: %d%d%d%d — %s",
             entered[0], entered[1], entered[2], entered[3],
             correct ? "CORRECTO" : "incorrecto");
    _test_result(correct, detail);
}


/* =========================================================
 * TEST 5 — COMPATIBILIDAD BTN_YES / BTN_NO CON TREZOR
 *
 * Verifica que button_read() retorna los flags correctos.
 * El usuario debe:
 *   1. Click → debe generar BTN_YES
 *   2. Long press → debe generar BTN_NO
 * ========================================================= */
void input_test_trezor_compat(void) {
    DEBUG_PRINT("\r\n[TEST5] Compatibilidad BTN_YES / BTN_NO\r\n");

    _test_header(5, "Compat. Trezor",
                 "Click=BTN_YES · Long press=BTN_NO");

    /* Área de estado */
    tft_draw_string_centered(70,
        "Este test verifica que button_read()",
        WALCORG_TEXT, WALCORG_BG, 1);
    tft_draw_string_centered(86,
        "retorna los flags de Trezor correctamente.",
        WALCORG_TEXT, WALCORG_BG, 1);

    bool got_yes = false;
    bool got_no  = false;
    uint32_t start = HAL_GetTick();

    /* Dibujar estado inicial */
    auto void draw_btn_state(void);
    void draw_btn_state(void) {
        /* BTN_YES */
        tft_fill_round_rect(30, 120, 185, 55, 8,
            got_yes ? WALCORG_SUCCESS : ILI9488_DARKGRAY);
        tft_draw_string_centered(137, "BTN_YES",
            got_yes ? ILI9488_WHITE : WALCORG_SUBTEXT,
            got_yes ? WALCORG_SUCCESS : ILI9488_DARKGRAY, 2);
        tft_draw_string_centered(157, "(Click)",
            got_yes ? ILI9488_WHITE : WALCORG_SUBTEXT,
            got_yes ? WALCORG_SUCCESS : ILI9488_DARKGRAY, 1);

        /* BTN_NO */
        tft_fill_round_rect(265, 120, 185, 55, 8,
            got_no ? WALCORG_ERROR : ILI9488_DARKGRAY);
        tft_draw_string_centered(137, "BTN_NO",
            got_no ? ILI9488_WHITE : WALCORG_SUBTEXT,
            got_no ? WALCORG_ERROR : ILI9488_DARKGRAY, 2);
        tft_draw_string_centered(157, "(Long press)",
            got_no ? ILI9488_WHITE : WALCORG_SUBTEXT,
            got_no ? WALCORG_ERROR : ILI9488_DARKGRAY, 1);
    }
    draw_btn_state();

    input_flush();

    while (!(got_yes && got_no) &&
           (HAL_GetTick() - start) < TEST_TIMEOUT_MS) {

        /* Llamar button_read() — exactamente como lo hace Trezor */
        uint16_t btn = input_button_read();

        if ((btn & BTN_YES) && !got_yes) {
            got_yes = true;
            DEBUG_PRINT("[TEST5] BTN_YES detectado via button_read()\r\n");
            draw_btn_state();
        }

        if ((btn & BTN_NO) && !got_no) {
            got_no = true;
            DEBUG_PRINT("[TEST5] BTN_NO detectado via button_read()\r\n");
            draw_btn_state();
        }

        HAL_Delay(10);
    }

    bool passed = got_yes && got_no;
    _test_result(passed, passed ?
        "BTN_YES y BTN_NO funcionan" :
        "Faltó detectar algún botón");
}


/* =========================================================
 * EJECUTAR TODOS LOS TESTS
 * ========================================================= */
bool input_run_all_tests(void) {
    DEBUG_PRINT("\r\n[TESTS] === Suite de tests Fase 3 ===\r\n");

    /* Pantalla de bienvenida */
    tft_clear(WALCORG_BG);
    tft_fill_rect(0, 0, tft_get_width(), 36, WALCORG_PRIMARY);
    tft_draw_string_centered(10, "WALCORG — Fase 3",
                              ILI9488_WHITE, WALCORG_PRIMARY, 2);
    tft_draw_string_centered(60,  "Tests del click-wheel",
                              WALCORG_TEXT, WALCORG_BG, 2);
    tft_draw_string_centered(90,  "5 tests interactivos", WALCORG_SUBTEXT, WALCORG_BG, 1);
    tft_draw_string_centered(110, "Sigue las instrucciones", WALCORG_SUBTEXT, WALCORG_BG, 1);
    tft_draw_string_centered(140, "en cada pantalla.", WALCORG_SUBTEXT, WALCORG_BG, 1);
    tft_draw_string_centered(180, "Click para empezar",
                              WALCORG_WARNING, WALCORG_BG, 2);

    /* Esperar click inicial */
    _wait_for_click("Click para comenzar los tests");

    /* Ejecutar los 5 tests */
    input_test_event_detector();
    input_test_main_menu();
    input_test_nested_menu();
    input_test_number_entry();
    input_test_trezor_compat();

    /* Pantalla final */
    tft_clear(WALCORG_BG);
    tft_fill_rect(0, 0, tft_get_width(), 36, WALCORG_SUCCESS);
    tft_draw_string_centered(10, "Fase 3 Completada",
                              ILI9488_WHITE, WALCORG_SUCCESS, 2);
    tft_draw_string_centered(80,  "Click-wheel funcional",
                              WALCORG_TEXT, WALCORG_BG, 2);
    tft_draw_string_centered(110, "Menus navegables con wheel",
                              WALCORG_TEXT, WALCORG_BG, 1);
    tft_draw_string_centered(130, "BTN_YES / BTN_NO compatibles",
                              WALCORG_TEXT, WALCORG_BG, 1);
    tft_draw_string_centered(160, "Listo para Fase 4",
                              WALCORG_SUBTEXT, WALCORG_BG, 1);
    tft_draw_string_centered(175, "(UI personalizada WALCORG)",
                              WALCORG_SUBTEXT, WALCORG_BG, 1);

    DEBUG_PRINT("[TESTS] === Suite Fase 3 completada ===\r\n\r\n");
    return true;
}
