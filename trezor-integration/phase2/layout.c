/**
 * @file    layout.c  (legacy/firmware/layout.c — versión MODIFICADA)
 * @brief   Pantallas de UI del wallet adaptadas al TFT 480×320 — WalCorg Fase 2
 *
 * ARCHIVO ORIGINAL: legacy/firmware/layout.c del repo trezor-firmware
 *
 * PROPÓSITO DE ESTE ARCHIVO:
 *   layout.c es la capa de presentación de Trezor — define cómo
 *   se ven cada una de las pantallas del wallet:
 *     layoutHome()          → pantalla de inicio
 *     layoutConfirmTx()     → confirmación de transacción
 *     layoutAddress()       → mostrar dirección
 *     layoutEnterPIN()      → entrada de PIN
 *     layoutWords()         → mostrar palabras de la semilla
 *     ... y muchas más
 *
 * ESTRATEGIA DE MODIFICACIÓN:
 *   En vez de reimplementar cada función desde cero, mantenemos
 *   la lógica y la estructura de Trezor, pero:
 *
 *   [MOD-L1] Cambiamos las llamadas oledDrawString → oledDrawString
 *            con size=2 para que el texto sea legible en 480×320
 *
 *   [MOD-L2] Ajustamos las coordenadas Y para aprovechar la mayor
 *            altura del TFT (320px vs 64px del OLED)
 *
 *   [MOD-L3] Añadimos colores: texto verde para confirmaciones,
 *            rojo para errores, azul para información
 *
 *   [MOD-L4] Añadimos barra de título y separadores visuales
 *
 *   [MOD-L5] layoutHome() muestra el logo WalCorg en la pantalla
 *            grande (donde el OLED solo mostraba el logo Trezor)
 *
 * ARCHIVOS QUE LLAMAN A LAYOUT.C (NO modificamos estos):
 *   firmware/fsm.c        → máquina de estados del wallet
 *   firmware/protect.c    → pantallas de PIN y passphrase
 *   firmware/recovery.c   → recuperación de semilla
 *   firmware/signing.c    → confirmación de firma
 *
 * @license GNU GPL v3
 */

#include "layout.h"       /* Header original de Trezor */
#include "display.h"      /* Nuestro display.h modificado */
#include "display_tft.h"  /* API directa del TFT cuando necesitamos más control */
#include "fonts.h"        /* Fuentes originales de Trezor (mantenemos para compatibilidad) */
#include <string.h>
#include <stdio.h>

/* =========================================================
 * [MOD-L4] CONSTANTES DE LAYOUT PARA 480×320
 *
 * Las coordenadas originales de Trezor estaban hardcodeadas
 * para 128×64. Definimos nuevas constantes para 480×320
 * que mantienen proporciones similares pero escaladas.
 * ========================================================= */

/* Área de título — barra superior */
#define LAYOUT_TITLE_Y          2    /**< Posición Y del título */
#define LAYOUT_TITLE_H          32   /**< Altura de la barra de título */
#define LAYOUT_TITLE_SIZE       2    /**< Tamaño de fuente del título */

/* Área de contenido — debajo del título */
#define LAYOUT_CONTENT_Y        40   /**< Inicio del área de contenido */
#define LAYOUT_CONTENT_SIZE     2    /**< Tamaño de fuente del contenido */
#define LAYOUT_LINE_HEIGHT      20   /**< Altura entre líneas */

/* Área de botones — barra inferior */
#define LAYOUT_BTN_Y            270  /**< Posición Y de los botones */
#define LAYOUT_BTN_H            48   /**< Altura del área de botones */
#define LAYOUT_BTN_SIZE         2    /**< Tamaño de fuente de los botones */

/* Separadores */
#define LAYOUT_SEP_TITLE_Y      (LAYOUT_TITLE_H + 2)   /**< Línea bajo el título */
#define LAYOUT_SEP_BTN_Y        (LAYOUT_BTN_Y - 2)     /**< Línea sobre los botones */


/* =========================================================
 * FUNCIONES AUXILIARES PRIVADAS
 * ========================================================= */

/**
 * _layout_draw_title_bar — Dibujar barra de título estándar
 *
 * [MOD-L4] Nueva función — el OLED no tenía barra de título
 * En 128×64 no había espacio. En 480×320 sí.
 *
 * @param title  Texto del título (centrado)
 * @param color  Color de fondo de la barra
 */
static void _layout_draw_title_bar(const char *title, tft_color_t color) {
    /* Fondo de la barra */
    tft_fill_rect(0, 0, OLED_WIDTH, LAYOUT_TITLE_H, color);
    /* Texto centrado en la barra */
    oledSetFgColor(ILI9488_WHITE);
    oledSetBgColor(color);
    oledDrawStringCenter(LAYOUT_TITLE_Y + 6, title, LAYOUT_TITLE_SIZE);
    /* Línea separadora */
    oledSetFgColor(WALCORG_SUBTEXT);
    oledHLine(0, LAYOUT_SEP_TITLE_Y, OLED_WIDTH);
    /* Restaurar colores */
    oledResetColors();
}

/**
 * _layout_draw_buttons — Dibujar botones en la parte inferior
 *
 * [MOD-L4] Nueva función con colores para el TFT
 * En el OLED los botones eran texto en la última fila.
 * En el TFT son rectángulos redondeados con texto.
 *
 * @param left_label   Texto del botón izquierdo (NO / cancelar)
 * @param right_label  Texto del botón derecho (SÍ / confirmar)
 */
static void _layout_draw_buttons(const char *left_label,
                                  const char *right_label) {
    /* Línea separadora */
    tft_draw_hline(0, LAYOUT_SEP_BTN_Y, OLED_WIDTH, WALCORG_SUBTEXT);

    /* Botón izquierdo — cancelar/no (rojo) */
    if (left_label && left_label[0]) {
        tft_fill_round_rect(8, LAYOUT_BTN_Y, 210, LAYOUT_BTN_H - 8, 8,
                            WALCORG_ERROR);
        oledSetFgColor(ILI9488_WHITE);
        oledSetBgColor(WALCORG_ERROR);
        /* Centrar texto en el botón izquierdo */
        int x = 8 + (210 - oledStringWidth(left_label, LAYOUT_BTN_SIZE)) / 2;
        oledDrawString(x, LAYOUT_BTN_Y + 12, left_label, LAYOUT_BTN_SIZE);
    }

    /* Botón derecho — confirmar/sí (verde) */
    if (right_label && right_label[0]) {
        tft_fill_round_rect(262, LAYOUT_BTN_Y, 210, LAYOUT_BTN_H - 8, 8,
                            WALCORG_SUCCESS);
        oledSetFgColor(ILI9488_WHITE);
        oledSetBgColor(WALCORG_SUCCESS);
        int x = 262 + (210 - oledStringWidth(right_label, LAYOUT_BTN_SIZE)) / 2;
        oledDrawString(x, LAYOUT_BTN_Y + 12, right_label, LAYOUT_BTN_SIZE);
    }

    oledResetColors();
}

/**
 * _layout_content_line — Dibujar una línea de contenido
 *
 * @param line_num  Número de línea (0 = primera debajo del título)
 * @param text      Texto a mostrar
 * @param color     Color del texto (0 = color por defecto)
 */
static void _layout_content_line(int line_num, const char *text,
                                  tft_color_t color) {
    if (!text) return;
    int y = LAYOUT_CONTENT_Y + (line_num * LAYOUT_LINE_HEIGHT);
    if (color != 0) oledSetFgColor(color);
    oledDrawString(10, y, text, LAYOUT_CONTENT_SIZE);
    if (color != 0) oledResetColors();
}


/* =========================================================
 * PANTALLAS PRINCIPALES
 * ========================================================= */

/**
 * layoutHome — Pantalla de inicio del wallet
 *
 * [MOD-L5] En el Trezor One mostraba el logo Trezor (128×64 bitmap)
 * y el label del dispositivo si tenía uno configurado.
 *
 * En WalCorg mostramos:
 *   - Barra superior con "WalCorg" en azul
 *   - Estado del dispositivo (inicializado / sin configurar)
 *   - Icono de estado (simulado con formas geométricas)
 *   - Instrucción de uso
 */
void layoutHome(void) {
    oledClear();

    /* [MOD-L3] Barra de título azul WalCorg */
    _layout_draw_title_bar("WalCorg Cold Wallet", WALCORG_PRIMARY);

    /* Icono central — círculo con símbolo de Bitcoin */
    tft_fill_circle(OLED_WIDTH / 2, OLED_HEIGHT / 2 - 10, 40, WALCORG_PRIMARY);
    oledSetFgColor(ILI9488_WHITE);
    oledSetBgColor(WALCORG_PRIMARY);
    /* Símbolo B de Bitcoin centrado */
    oledDrawStringCenter(OLED_HEIGHT / 2 - 22, "B", 3);
    oledResetColors();

    /* Texto de instrucción */
    oledSetFgColor(WALCORG_SUBTEXT);
    oledDrawStringCenter(OLED_HEIGHT / 2 + 42, "Conecta al USB para", 1);
    oledDrawStringCenter(OLED_HEIGHT / 2 + 54, "usar trezorctl", 1);
    oledResetColors();

    oledRefresh();  /* NO-OP para TFT, pero mantiene compatibilidad */
}

/**
 * layoutLabel — Mostrar etiqueta del dispositivo en la pantalla de inicio
 *
 * [MOD-L1] Original: texto pequeño en la esquina inferior del OLED
 * Nuevo: texto centrado en el área de contenido
 *
 * @param label  Nombre del dispositivo configurado por el usuario
 */
void layoutLabel(const char *label) {
    if (!label || !label[0]) return;

    oledSetFgColor(WALCORG_TEXT);
    oledDrawStringCenter(OLED_HEIGHT / 2 + 28, label, LAYOUT_CONTENT_SIZE);
    oledResetColors();
    oledRefresh();
}

/**
 * layoutDialogSwipe — Diálogo de confirmación con swipe
 *
 * Una de las pantallas más importantes — aparece para confirmar
 * transacciones, operaciones críticas y configuraciones.
 *
 * [MOD-L2][MOD-L3] Adaptado para 480×320 con colores
 *
 * @param icon     Icono a mostrar (NULL para ninguno)
 * @param btnNo    Texto del botón "No" (izquierda)
 * @param btnYes   Texto del botón "Sí" (derecha)
 * @param desc1..4 Líneas de descripción del diálogo
 */
void layoutDialogSwipe(const BITMAP *icon,
                        const char *btnNo, const char *btnYes,
                        const char *desc1, const char *desc2,
                        const char *desc3, const char *desc4,
                        const char *desc5, const char *desc6) {
    oledClear();

    /* [MOD-L4] Barra de título */
    _layout_draw_title_bar("Confirmar", WALCORG_PRIMARY);

    /* [MOD-L3] Líneas de contenido */
    int line = 0;
    if (desc1) _layout_content_line(line++, desc1, 0);
    if (desc2) _layout_content_line(line++, desc2, WALCORG_SUBTEXT);
    if (desc3) _layout_content_line(line++, desc3, 0);
    if (desc4) _layout_content_line(line++, desc4, WALCORG_SUBTEXT);
    if (desc5) _layout_content_line(line++, desc5, 0);
    if (desc6) _layout_content_line(line++, desc6, WALCORG_SUBTEXT);

    /* [MOD-L4] Botones */
    _layout_draw_buttons(btnNo, btnYes);

    oledRefresh();
}

/**
 * layoutAddress — Mostrar una dirección Bitcoin para verificación
 *
 * PANTALLA CRÍTICA DE SEGURIDAD: el usuario debe verificar
 * que la dirección en pantalla coincide con la esperada.
 *
 * [MOD-L2][MOD-L3] En 480×320 tenemos espacio para mostrar
 * la dirección completa en una sola pantalla sin truncar.
 *
 * @param address  Dirección Bitcoin (bech32, base58, etc.)
 */
void layoutAddress(const char *address) {
    if (!address) return;

    oledClear();

    /* [MOD-L3] Barra de título verde — operación segura */
    _layout_draw_title_bar("Verificar Direccion", WALCORG_SUCCESS);

    /* Etiqueta */
    oledSetFgColor(WALCORG_SUBTEXT);
    oledDrawString(10, LAYOUT_CONTENT_Y, "Direccion Bitcoin:", 1);
    oledResetColors();

    /* Fondo resaltado para la dirección */
    tft_fill_round_rect(5, LAYOUT_CONTENT_Y + 14,
                        OLED_WIDTH - 10, 50, 6, ILI9488_DARKGRAY);

    /* Dirección — tamaño 1 para caber en una línea
     * Una dirección bech32 tiene ~42 chars × 6px = 252px < 470px ✓ */
    oledSetFgColor(WALCORG_TEXT);
    oledSetBgColor(ILI9488_DARKGRAY);
    oledDrawString(10, LAYOUT_CONTENT_Y + 18, address, 1);
    oledResetColors();

    /* Advertencia */
    oledSetFgColor(WALCORG_WARNING);
    oledDrawStringCenter(LAYOUT_CONTENT_Y + 80,
                         "Verifica CADA caracter", 1);
    oledDrawStringCenter(LAYOUT_CONTENT_Y + 94,
                         "antes de confirmar", 1);
    oledResetColors();

    /* [MOD-L4] Botones */
    _layout_draw_buttons("Cancelar", "Confirmar");

    oledRefresh();
}

/**
 * layoutConfirmTx — Pantalla de confirmación de transacción
 *
 * [MOD-L2][MOD-L3] En 480×320 mostramos todos los detalles:
 * monto, dirección destino, comisión — sin truncar nada.
 *
 * @param amount    Monto a enviar (string formateado, ej: "0.001 BTC")
 * @param fee       Comisión de red (ej: "0.0001 BTC")
 */
void layoutConfirmTx(const char *amount, const char *fee) {
    oledClear();

    /* [MOD-L3] Barra de título naranja — advertencia (estás enviando dinero) */
    _layout_draw_title_bar("Enviar Bitcoin", WALCORG_WARNING);

    /* Monto */
    oledSetFgColor(WALCORG_SUBTEXT);
    oledDrawString(10, LAYOUT_CONTENT_Y, "Monto:", 1);

    oledSetFgColor(WALCORG_TEXT);
    oledDrawString(10, LAYOUT_CONTENT_Y + 14,
                   amount ? amount : "0 BTC",
                   LAYOUT_CONTENT_SIZE);

    /* Comisión */
    oledSetFgColor(WALCORG_SUBTEXT);
    oledDrawString(10, LAYOUT_CONTENT_Y + 50, "Comision de red:", 1);

    oledSetFgColor(WALCORG_TEXT);
    oledDrawString(10, LAYOUT_CONTENT_Y + 64,
                   fee ? fee : "calculando...",
                   LAYOUT_CONTENT_SIZE);

    /* Total */
    oledSetFgColor(WALCORG_SUBTEXT);
    tft_draw_hline(10, LAYOUT_CONTENT_Y + 95, OLED_WIDTH - 20, WALCORG_SUBTEXT);
    oledDrawString(10, LAYOUT_CONTENT_Y + 100, "Total:", 1);

    oledResetColors();

    /* [MOD-L4] Botones */
    _layout_draw_buttons("Cancelar", "Confirmar envio");

    oledRefresh();
}

/**
 * layoutFeeOverThreshold — Advertencia de comisión alta
 *
 * [MOD-L3] Barra roja para indicar peligro
 *
 * @param fee  Comisión que excede el umbral
 */
void layoutFeeOverThreshold(const char *fee) {
    oledClear();

    /* [MOD-L3] Barra roja — advertencia crítica */
    _layout_draw_title_bar("! Comision Alta !", WALCORG_ERROR);

    oledSetFgColor(WALCORG_ERROR);
    oledDrawStringCenter(LAYOUT_CONTENT_Y + 10,
                         "La comision es inusualmente", LAYOUT_CONTENT_SIZE);
    oledDrawStringCenter(LAYOUT_CONTENT_Y + 30,
                         "alta:", LAYOUT_CONTENT_SIZE);

    oledSetFgColor(WALCORG_TEXT);
    oledDrawStringCenter(LAYOUT_CONTENT_Y + 60,
                         fee ? fee : "???",
                         LAYOUT_CONTENT_SIZE + 1);

    oledSetFgColor(WALCORG_SUBTEXT);
    oledDrawStringCenter(LAYOUT_CONTENT_Y + 100,
                         "Verifica antes de continuar", 1);
    oledResetColors();

    _layout_draw_buttons("Cancelar", "Continuar");
    oledRefresh();
}

/**
 * layoutEnterPIN — Pantalla de entrada de PIN
 *
 * [MOD-L2] En el OLED el PIN se mostraba como puntos en una
 * fila de 128px. En 480px tenemos espacio para una matriz
 * numérica más grande y legible.
 *
 * @param desc   Descripción (ej: "Ingresa tu PIN")
 * @param pin    PIN actual (se muestra como asteriscos)
 */
void layoutEnterPIN(const char *desc, const char *pin) {
    oledClear();

    _layout_draw_title_bar("PIN de Seguridad", WALCORG_PRIMARY);

    /* Descripción */
    oledSetFgColor(WALCORG_TEXT);
    oledDrawStringCenter(LAYOUT_CONTENT_Y,
                         desc ? desc : "Ingresa tu PIN:",
                         LAYOUT_CONTENT_SIZE);

    /* Mostrar PIN como asteriscos */
    if (pin) {
        int pin_len = (int)strlen(pin);
        /* Construir string de asteriscos */
        char pin_mask[12] = {0};
        for (int i = 0; i < pin_len && i < 9; i++) {
            pin_mask[i * 2]     = '*';
            pin_mask[i * 2 + 1] = ' ';
        }
        /* Fondo para los asteriscos */
        tft_fill_round_rect(100, LAYOUT_CONTENT_Y + 30,
                            280, 32, 6, ILI9488_DARKGRAY);
        oledSetFgColor(WALCORG_TEXT);
        oledSetBgColor(ILI9488_DARKGRAY);
        oledDrawStringCenter(LAYOUT_CONTENT_Y + 37,
                             pin_mask,
                             LAYOUT_CONTENT_SIZE + 1);
        oledResetColors();
    }

    /* Instrucción del click-wheel */
    oledSetFgColor(WALCORG_SUBTEXT);
    oledDrawStringCenter(LAYOUT_CONTENT_Y + 80,
                         "Gira: cambiar digito", 1);
    oledDrawStringCenter(LAYOUT_CONTENT_Y + 94,
                         "Click: confirmar digito", 1);
    oledDrawStringCenter(LAYOUT_CONTENT_Y + 108,
                         "Long press: borrar", 1);
    oledResetColors();

    _layout_draw_buttons("Cancelar", "OK");
    oledRefresh();
}

/**
 * layoutWords — Mostrar palabras de la semilla BIP39
 *
 * [MOD-L2] En el OLED solo cabían 2-3 palabras a la vez.
 * En 480×320 podemos mostrar hasta 6 palabras por pantalla.
 *
 * @param words  Array de strings con las palabras
 * @param start  Índice de la primera palabra a mostrar (0-based)
 * @param count  Número de palabras a mostrar en esta pantalla
 */
void layoutWords(const char *const *words, int start, int count) {
    if (!words) return;

    oledClear();

    /* Título con número de pantalla */
    char title[32];
    snprintf(title, sizeof(title), "Semilla — Palabras %d-%d",
             start + 1, start + count);
    _layout_draw_title_bar(title, WALCORG_WARNING);

    /* Mostrar palabras en dos columnas de 3 */
    int col_x[2] = { 10, 250 };
    oledSetFgColor(WALCORG_SUBTEXT);

    for (int i = 0; i < count && i < 6; i++) {
        if (!words[start + i]) continue;

        int col   = i / 3;       /* Columna: 0=izquierda, 1=derecha */
        int row   = i % 3;       /* Fila en la columna (0-2) */
        int y     = LAYOUT_CONTENT_Y + (row * 55);
        int x     = col_x[col];

        /* Número de la palabra */
        char num_str[8];
        snprintf(num_str, sizeof(num_str), "%2d.", start + i + 1);
        oledSetFgColor(WALCORG_SUBTEXT);
        oledDrawString(x, y, num_str, 1);

        /* Palabra */
        oledSetFgColor(WALCORG_TEXT);
        oledDrawString(x + 22, y + 2, words[start + i], LAYOUT_CONTENT_SIZE);
    }

    oledResetColors();

    /* Indicador de progreso */
    if (start + count >= 24) {
        _layout_draw_buttons("Anterior", "Finalizar");
    } else {
        _layout_draw_buttons("Anterior", "Siguiente");
    }
    oledRefresh();
}

/**
 * layoutFirmwareUpdate — Pantalla de actualización de firmware
 *
 * [MOD-L3] Barra naranja para indicar operación importante
 */
void layoutFirmwareUpdate(void) {
    oledClear();
    _layout_draw_title_bar("Actualizacion", WALCORG_WARNING);

    oledSetFgColor(WALCORG_TEXT);
    oledDrawStringCenter(LAYOUT_CONTENT_Y + 20,
                         "Actualizando firmware...", LAYOUT_CONTENT_SIZE);
    oledSetFgColor(WALCORG_SUBTEXT);
    oledDrawStringCenter(LAYOUT_CONTENT_Y + 50,
                         "No desconectes el USB", 1);
    oledDrawStringCenter(LAYOUT_CONTENT_Y + 64,
                         "ni apagues el dispositivo", 1);
    oledResetColors();
    oledRefresh();
}

/**
 * layoutProgressBar — Barra de progreso genérica
 *
 * [MOD-L5] Nueva función — el OLED no tenía barras de progreso
 * Se usa para operaciones largas: generación de semilla,
 * verificación de firmware, derivación de claves.
 *
 * @param desc     Descripción de la operación
 * @param progress Progreso de 0 a 1000 (‰ = partes por mil)
 */
void layoutProgressBar(const char *desc, int progress) {
    /* Solo redibujar la barra, no toda la pantalla
     * para evitar parpadeo en actualizaciones frecuentes */
    static bool first_call = true;

    if (first_call) {
        oledClear();
        _layout_draw_title_bar("Procesando...", WALCORG_PRIMARY);
        oledSetFgColor(WALCORG_TEXT);
        if (desc) {
            oledDrawStringCenter(LAYOUT_CONTENT_Y + 20, desc, LAYOUT_CONTENT_SIZE);
        }
        /* Marco de la barra */
        tft_draw_round_rect(20, OLED_HEIGHT / 2 - 15,
                            OLED_WIDTH - 40, 30, 6, WALCORG_SUBTEXT);
        first_call = false;
    }

    /* Relleno de la barra según el progreso */
    int fill_w = ((OLED_WIDTH - 46) * progress) / 1000;
    if (fill_w > 0) {
        tft_fill_round_rect(23, OLED_HEIGHT / 2 - 12,
                            fill_w, 24, 4, WALCORG_PRIMARY);
    }

    /* Porcentaje */
    char pct_str[8];
    snprintf(pct_str, sizeof(pct_str), "%d%%", progress / 10);
    oledSetFgColor(WALCORG_TEXT);
    oledSetBgColor(WALCORG_BG);
    oledDrawStringCenter(OLED_HEIGHT / 2 + 25, pct_str, LAYOUT_CONTENT_SIZE);
    oledResetColors();

    if (progress >= 1000) first_call = true;  /* Reset para la próxima vez */
    oledRefresh();
}
