/**
 * @file    display_test.c
 * @brief   Tests visuales del display TFT — WalCorg Fase 2
 *
 * Cada test dibuja algo en la pantalla y espera 2 segundos.
 * Si la imagen en pantalla coincide con lo descrito, el test pasó.
 * Simultáneamente imprime el progreso por UART.
 *
 * @license GNU GPL v3
 */

#include "display_test.h"
#include "display_tft.h"
#include "touch_xpt2046.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* Duración de cada test en ms */
#define TEST_DURATION_MS    2500

/* =========================================================
 * TEST 1: BARRAS DE COLOR
 *
 * Divide la pantalla en 8 franjas verticales, cada una
 * de un color primario/secundario diferente.
 * Verifica: fill_rect, colores RGB565, dimensiones correctas.
 *
 * Lo que deberías ver:
 *   8 franjas verticales: negro, blanco, rojo, verde, azul,
 *   amarillo, cyan, magenta — de izquierda a derecha.
 * ========================================================= */
void display_test_color_bars(void) {
    DEBUG_PRINT("[DTEST] Test 1: Barras de color...\r\n");

    uint16_t w = tft_get_width();
    uint16_t h = tft_get_height();
    uint16_t bar_w = w / 8;

    /* 8 colores de prueba estándar para displays */
    const tft_color_t colors[8] = {
        ILI9488_BLACK,    /* 0: negro   */
        ILI9488_WHITE,    /* 1: blanco  */
        ILI9488_RED,      /* 2: rojo    */
        ILI9488_GREEN,    /* 3: verde   */
        ILI9488_BLUE,     /* 4: azul    */
        ILI9488_YELLOW,   /* 5: amarillo*/
        ILI9488_CYAN,     /* 6: cyan    */
        ILI9488_MAGENTA,  /* 7: magenta */
    };

    const char *names[8] = {
        "NEGRO", "BLANCO", "ROJO", "VERDE",
        "AZUL", "AMARILLO", "CYAN", "MAGENTA"
    };

    for (int i = 0; i < 8; i++) {
        tft_fill_rect((tft_coord_t)(i * bar_w), 0,
                      (tft_coord_t)bar_w, (tft_coord_t)h,
                      colors[i]);
        DEBUG_PRINTF("[DTEST]   Franja %d: %s\r\n", i + 1, names[i]);
    }

    DEBUG_PRINT("[DTEST] Test 1 completo. Deberias ver 8 franjas de colores.\r\n");
    HAL_Delay(TEST_DURATION_MS);
}


/* =========================================================
 * TEST 2: FORMAS GEOMÉTRICAS
 *
 * Dibuja una colección de formas sobre fondo negro.
 * Verifica: draw_rect, draw_circle, fill_circle,
 *           draw_round_rect, draw_line, draw_hline, draw_vline.
 * ========================================================= */
void display_test_shapes(void) {
    DEBUG_PRINT("[DTEST] Test 2: Formas geometricas...\r\n");

    tft_clear(WALCORG_BG);  /* Fondo del tema WalCorg */

    uint16_t w = tft_get_width();
    uint16_t h = tft_get_height();

    /* Título */
    tft_draw_string(10, 5, "Formas geometricas",
                    WALCORG_TEXT, WALCORG_BG, 2);

    /* Líneas diagonales cruzadas en la esquina */
    tft_draw_line(0, 30, 100, 100, ILI9488_RED);
    tft_draw_line(100, 30, 0, 100, ILI9488_GREEN);
    DEBUG_PRINT("[DTEST]   Lineas diagonales: OK\r\n");

    /* Rectángulo sin relleno */
    tft_draw_rect(10, 110, 140, 80, WALCORG_PRIMARY);
    /* Rectángulo con relleno */
    tft_fill_rect(160, 110, 140, 80, WALCORG_PRIMARY);
    DEBUG_PRINT("[DTEST]   Rectangulos: OK\r\n");

    /* Rectángulos con esquinas redondeadas */
    tft_draw_round_rect(10, 210, 140, 70, 15, WALCORG_SUCCESS);
    tft_fill_round_rect(160, 210, 140, 70, 15, WALCORG_SUCCESS);
    DEBUG_PRINT("[DTEST]   Rect. redondeados: OK\r\n");

    /* Círculos */
    tft_draw_circle(360, 80, 50, WALCORG_WARNING);
    tft_fill_circle(360, 200, 50, WALCORG_ERROR);
    DEBUG_PRINT("[DTEST]   Circulos: OK\r\n");

    /* Líneas horizontales y verticales — patrón de rejilla */
    for (int x = (int)w - 100; x < (int)w; x += 10) {
        tft_color_t col = (x % 20 == 0) ? WALCORG_TEXT : WALCORG_SUBTEXT;
        tft_draw_vline((tft_coord_t)x, 30, (tft_coord_t)(h - 60), col);
    }
    for (int y = 30; y < (int)h - 30; y += 10) {
        tft_color_t col = (y % 20 == 0) ? WALCORG_TEXT : WALCORG_SUBTEXT;
        tft_draw_hline((tft_coord_t)(w - 100), (tft_coord_t)y, 100, col);
    }
    DEBUG_PRINT("[DTEST]   Rejilla: OK\r\n");

    DEBUG_PRINT("[DTEST] Test 2 completo.\r\n");
    HAL_Delay(TEST_DURATION_MS);
}


/* =========================================================
 * TEST 3: TEXTO EN MÚLTIPLES TAMAÑOS
 *
 * Muestra la fuente bitmap en 3 tamaños distintos.
 * También prueba caracteres especiales y números.
 * Verifica que el texto del wallet (PINs, semillas, saldos)
 * es legible en pantalla.
 * ========================================================= */
void display_test_text(void) {
    DEBUG_PRINT("[DTEST] Test 3: Rendering de texto...\r\n");

    tft_clear(WALCORG_BG);

    uint16_t y = 10;

    /* Size 1: muy pequeño — información secundaria */
    tft_draw_string(5, (tft_coord_t)y,
                    "Size 1: ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
                    WALCORG_SUBTEXT, WALCORG_BG, 1);
    y += 12;
    tft_draw_string(5, (tft_coord_t)y,
                    "Size 1: 0123456789 !@#$%^&*()_+-=[]{}|;:',.<>?",
                    WALCORG_SUBTEXT, WALCORG_BG, 1);
    y += 20;

    /* Size 2: lectura normal — UI principal */
    tft_draw_string(5, (tft_coord_t)y,
                    "Size 2: WalCorg Cold Wallet",
                    WALCORG_TEXT, WALCORG_BG, 2);
    y += 20;
    tft_draw_string(5, (tft_coord_t)y,
                    "Size 2: 0.00123456 BTC",
                    WALCORG_SUCCESS, WALCORG_BG, 2);
    y += 20;
    tft_draw_string(5, (tft_coord_t)y,
                    "Size 2: PIN: *** ***",
                    WALCORG_WARNING, WALCORG_BG, 2);
    y += 30;

    /* Size 3: títulos y alertas */
    tft_draw_string(5, (tft_coord_t)y,
                    "Size 3: CONFIRMAR",
                    WALCORG_PRIMARY, WALCORG_BG, 3);
    y += 28;
    tft_draw_string(5, (tft_coord_t)y,
                    "Size 3: ERROR",
                    WALCORG_ERROR, WALCORG_BG, 3);
    y += 35;

    /* Texto centrado */
    tft_draw_string_centered((tft_coord_t)y,
                              "-- Texto centrado --",
                              WALCORG_SUBTEXT, WALCORG_BG, 2);

    DEBUG_PRINT("[DTEST] Test 3 completo. Verificar legibilidad del texto.\r\n");
    HAL_Delay(TEST_DURATION_MS);
}


/* =========================================================
 * TEST 4: PANTALLA DE DIRECCIÓN BITCOIN
 *
 * Simula la pantalla de "recibir" del wallet.
 * Es el caso de uso más importante del display:
 * el usuario necesita leer la dirección claramente para
 * verificarla antes de compartirla.
 *
 * Lo que deberías ver:
 *   - Título "RECIBIR BTC"
 *   - Dirección de ejemplo en formato bc1... (segwit)
 *   - Indicación de red (Mainnet / Testnet)
 *   - Botón "Copiar" simulado
 * ========================================================= */
void display_test_bitcoin_address(void) {
    DEBUG_PRINT("[DTEST] Test 4: Pantalla de direccion Bitcoin...\r\n");

    tft_clear(WALCORG_BG);

    uint16_t w = tft_get_width();

    /* Barra de título */
    tft_fill_rect(0, 0, (tft_coord_t)w, 36, WALCORG_PRIMARY);
    tft_draw_string_centered(8, "RECIBIR BITCOIN", ILI9488_WHITE, WALCORG_PRIMARY, 2);

    /* Separador */
    tft_draw_hline(0, 37, (tft_coord_t)w, WALCORG_SUBTEXT);

    /* Etiqueta de dirección */
    tft_draw_string(10, 48, "Direccion (SegWit):", WALCORG_SUBTEXT, WALCORG_BG, 1);

    /* Dirección Bitcoin de ejemplo — 42 caracteres bc1q... */
    /* En producción esta vendría del firmware de Trezor */
    const char *addr_line1 = "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq";
    const char *addr_line2 = "(direccion de ejemplo — no usar)";

    /* Fondo de la dirección */
    tft_fill_round_rect(5, 60, (tft_coord_t)(w - 10), 52, 6, ILI9488_DARKGRAY);

    /* Dirección en size 1 — la más compacta para caber en pantalla */
    tft_draw_string(10, 68,  addr_line1, WALCORG_TEXT, ILI9488_DARKGRAY, 1);
    tft_draw_string(10, 80, addr_line2, WALCORG_SUBTEXT, ILI9488_DARKGRAY, 1);

    /* Red */
    tft_draw_string(10, 122, "Red:", WALCORG_SUBTEXT, WALCORG_BG, 1);
    tft_fill_round_rect(40, 118, 80, 14, 3, WALCORG_SUCCESS);
    tft_draw_string(45, 121, "MAINNET", ILI9488_WHITE, WALCORG_SUCCESS, 1);

    /* Separador */
    tft_draw_hline(0, 145, (tft_coord_t)w, WALCORG_SUBTEXT);

    /* Instrucción */
    tft_draw_string_centered(152,
        "Verifica la direccion en este dispositivo",
        WALCORG_WARNING, WALCORG_BG, 1);
    tft_draw_string_centered(164,
        "antes de compartirla.",
        WALCORG_WARNING, WALCORG_BG, 1);

    /* Botones simulados */
    tft_fill_round_rect(10, 185, 210, 40, 8, WALCORG_PRIMARY);
    tft_draw_string_centered(198, "CONFIRMAR", ILI9488_WHITE, WALCORG_PRIMARY, 2);

    tft_draw_round_rect(230, 185, 210, 40, 8, WALCORG_ERROR);
    tft_draw_string(260, 198, "CANCELAR", WALCORG_ERROR, WALCORG_BG, 2);

    DEBUG_PRINT("[DTEST] Test 4 completo. Verificar legibilidad de la direccion.\r\n");
    HAL_Delay(TEST_DURATION_MS + 1000);  /* 1s extra para leer la dirección */
}


/* =========================================================
 * TEST 5: BARRA DE PROGRESO ANIMADA
 *
 * Anima una barra de progreso de 0% a 100%.
 * Verifica: redibujado parcial eficiente, animación fluida.
 * Simula la pantalla de "generando semilla" del wallet.
 * ========================================================= */
void display_test_progress_bar(void) {
    DEBUG_PRINT("[DTEST] Test 5: Barra de progreso animada...\r\n");

    tft_clear(WALCORG_BG);

    uint16_t w = tft_get_width();
    uint16_t h = tft_get_height();

    /* Título */
    tft_draw_string_centered(60, "Generando semilla...", WALCORG_TEXT, WALCORG_BG, 2);
    tft_draw_string_centered(85, "Por favor espera", WALCORG_SUBTEXT, WALCORG_BG, 1);

    /* Marco de la barra de progreso */
    tft_coord_t bar_x = 40;
    tft_coord_t bar_y = (tft_coord_t)(h / 2 - 15);
    tft_coord_t bar_w = (tft_coord_t)(w - 80);
    tft_coord_t bar_h = 30;

    tft_draw_round_rect(bar_x, bar_y, bar_w, bar_h, 8, WALCORG_SUBTEXT);

    /* Animación: rellenar la barra incrementalmente */
    char pct_str[8];
    tft_coord_t inner_x = bar_x + 3;
    tft_coord_t inner_y = bar_y + 3;
    tft_coord_t inner_h = bar_h - 6;
    tft_coord_t inner_max_w = bar_w - 6;

    for (int pct = 0; pct <= 100; pct++) {
        /* Calcular ancho del relleno */
        tft_coord_t fill_w = (tft_coord_t)((inner_max_w * pct) / 100);

        /* Dibujar el relleno de la barra */
        if (fill_w > 0) {
            /* Color cambia de azul a verde conforme avanza */
            tft_color_t bar_color;
            if (pct < 50) {
                bar_color = WALCORG_PRIMARY;   /* Azul */
            } else if (pct < 80) {
                bar_color = WALCORG_WARNING;   /* Naranja */
            } else {
                bar_color = WALCORG_SUCCESS;   /* Verde */
            }
            tft_fill_round_rect(inner_x, inner_y, fill_w, inner_h, 5, bar_color);
        }

        /* Porcentaje centrado */
        snprintf(pct_str, sizeof(pct_str), " %3d%% ", pct);
        tft_draw_string_centered((tft_coord_t)(bar_y + bar_h + 15),
                                  pct_str, WALCORG_TEXT, WALCORG_BG, 2);

        /* Mensaje de estado cada 25% */
        if (pct == 25) {
            tft_draw_string_centered(200,
                "Recopilando entropia...  ",
                WALCORG_SUBTEXT, WALCORG_BG, 1);
        } else if (pct == 50) {
            tft_draw_string_centered(200,
                "Aplicando BIP39...       ",
                WALCORG_SUBTEXT, WALCORG_BG, 1);
        } else if (pct == 75) {
            tft_draw_string_centered(200,
                "Derivando claves...      ",
                WALCORG_SUBTEXT, WALCORG_BG, 1);
        } else if (pct == 100) {
            tft_draw_string_centered(200,
                "Semilla generada!        ",
                WALCORG_SUCCESS, WALCORG_BG, 1);
        }

        HAL_Delay(25);  /* 25ms × 101 pasos = ~2.5 segundos total */
    }

    DEBUG_PRINT("[DTEST] Test 5 completo. Barra llego a 100%.\r\n");
    HAL_Delay(1000);
}


/* =========================================================
 * TEST 6: CALIBRACIÓN DE TOUCH
 *
 * Muestra 4 círculos en las esquinas de la pantalla.
 * El usuario debe tocar cada uno. El test registra los
 * valores raw y los usa para actualizar la calibración.
 *
 * Este test es interactivo — requiere que el usuario toque.
 * Si nadie toca en 10 segundos, salta al siguiente test.
 * ========================================================= */
void display_test_touch_calibration(void) {
    DEBUG_PRINT("[DTEST] Test 6: Calibracion de touch (toca los circulos)...\r\n");

    tft_clear(WALCORG_BG);

    uint16_t w = tft_get_width();
    uint16_t h = tft_get_height();

    tft_draw_string_centered(h/2 - 20,
        "Toca cada circulo para calibrar",
        WALCORG_TEXT, WALCORG_BG, 1);
    tft_draw_string_centered(h/2,
        "el panel tactil.",
        WALCORG_SUBTEXT, WALCORG_BG, 1);
    tft_draw_string_centered(h/2 + 20,
        "(Espera 10s para omitir)",
        WALCORG_SUBTEXT, WALCORG_BG, 1);

    /* Puntos de calibración: 4 esquinas */
    const struct { tft_coord_t x; tft_coord_t y; const char *label; } pts[4] = {
        { 30,              30,              "SUP-IZQ" },
        { (tft_coord_t)(w-30), 30,          "SUP-DER" },
        { 30,              (tft_coord_t)(h-30), "INF-IZQ" },
        { (tft_coord_t)(w-30), (tft_coord_t)(h-30), "INF-DER" },
    };

    touch_raw_t raw_pts[4] = {0};
    bool touched[4] = {false, false, false, false};

    /* Dibujar los 4 círculos */
    tft_clear(WALCORG_BG);
    for (int i = 0; i < 4; i++) {
        tft_draw_circle(pts[i].x, pts[i].y, 20, WALCORG_PRIMARY);
        tft_fill_circle(pts[i].x, pts[i].y, 5,  WALCORG_PRIMARY);
    }

    /* Esperar toques por hasta 10 segundos */
    uint32_t deadline = HAL_GetTick() + 10000;
    int current_pt = 0;

    /* Resaltar el primer punto */
    tft_fill_circle(pts[0].x, pts[0].y, 20, WALCORG_WARNING);

    while (HAL_GetTick() < deadline && current_pt < 4) {
        touch_point_t tp;
        if (touch_read(&tp) && tp.valid) {
            /* Registrar el raw del punto tocado */
            touch_read_raw(&raw_pts[current_pt]);
            touched[current_pt] = true;

            DEBUG_PRINTF("[DTEST]   Punto %s: raw_x=%u raw_y=%u\r\n",
                pts[current_pt].label,
                raw_pts[current_pt].raw_x,
                raw_pts[current_pt].raw_y);

            /* Marcar como tocado */
            tft_fill_circle(pts[current_pt].x, pts[current_pt].y,
                            20, WALCORG_SUCCESS);
            touch_wait_release(1000);

            current_pt++;

            /* Resaltar el siguiente punto */
            if (current_pt < 4) {
                tft_fill_circle(pts[current_pt].x, pts[current_pt].y,
                                20, WALCORG_WARNING);
            }
        }
        HAL_Delay(10);
    }

    if (current_pt == 4) {
        /* Todos los puntos tocados — actualizar calibración */
        touch_cal_t new_cal = {
            .x_min     = raw_pts[0].raw_x < raw_pts[2].raw_x ?
                         raw_pts[0].raw_x : raw_pts[2].raw_x,
            .x_max     = raw_pts[1].raw_x > raw_pts[3].raw_x ?
                         raw_pts[1].raw_x : raw_pts[3].raw_x,
            .y_min     = raw_pts[0].raw_y < raw_pts[1].raw_y ?
                         raw_pts[0].raw_y : raw_pts[1].raw_y,
            .y_max     = raw_pts[2].raw_y > raw_pts[3].raw_y ?
                         raw_pts[2].raw_y : raw_pts[3].raw_y,
            .invert_x  = TOUCH_INVERT_X,
            .invert_y  = TOUCH_INVERT_Y,
            .swap_xy   = TOUCH_SWAP_XY,
            .calibrated = true,
        };
        touch_set_calibration(&new_cal);
        tft_draw_string_centered(h/2, "Calibracion OK!", WALCORG_SUCCESS, WALCORG_BG, 2);
        DEBUG_PRINT("[DTEST] Test 6: calibracion completada.\r\n");
    } else {
        tft_draw_string_centered(h/2, "Omitido (timeout)", WALCORG_SUBTEXT, WALCORG_BG, 2);
        DEBUG_PRINT("[DTEST] Test 6: omitido por timeout.\r\n");
    }

    HAL_Delay(1500);
}


/* =========================================================
 * EJECUTAR TODOS LOS TESTS
 * ========================================================= */
bool display_run_all_tests(void) {
    DEBUG_PRINT("\r\n[DTEST] === Iniciando suite de tests del display ===\r\n");

    display_test_color_bars();
    display_test_shapes();
    display_test_text();
    display_test_bitcoin_address();
    display_test_progress_bar();
    display_test_touch_calibration();

    /* Pantalla final: resumen */
    tft_clear(WALCORG_BG);
    tft_draw_string_centered(100, "FASE 2 COMPLETADA", WALCORG_SUCCESS, WALCORG_BG, 2);
    tft_draw_string_centered(130, "Display TFT funcional", WALCORG_TEXT, WALCORG_BG, 1);
    tft_draw_string_centered(145, "Touch inicializado", WALCORG_TEXT, WALCORG_BG, 1);
    tft_draw_string_centered(165, "Listo para Fase 3", WALCORG_SUBTEXT, WALCORG_BG, 1);
    tft_draw_string_centered(180, "(Click-wheel integration)", WALCORG_SUBTEXT, WALCORG_BG, 1);

    DEBUG_PRINT("[DTEST] === Suite completada ===\r\n\r\n");
    return true;
}
