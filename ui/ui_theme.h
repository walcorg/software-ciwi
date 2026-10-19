/**
 * @file    ui_theme.h
 * @brief   Tema visual centralizado — WalCorg Cold Wallet Fase 4
 *
 * PUNTO ÚNICO DE VERDAD para todos los valores visuales del wallet.
 * Cambiar un color aquí lo cambia en toda la UI automáticamente.
 *
 * FILOSOFÍA DE DISEÑO:
 *   - Fondo oscuro (dark theme): reduce fatiga visual, mejor
 *     contraste para leer direcciones y cantidades.
 *   - Paleta de 6 colores semánticos: cada color tiene un
 *     significado específico (azul=acción, verde=seguro,
 *     rojo=peligro, naranja=advertencia).
 *   - Tipografía escalable: 3 tamaños (small/body/title)
 *     que se adaptan al contexto.
 *   - Espaciado consistente basado en una unidad base de 8px.
 *
 * @license GNU GPL v3
 */

#ifndef UI_THEME_H
#define UI_THEME_H

#include "ili9488_regs.h"   /* Para RGB565() y tft_color_t */

/* =========================================================
 * PALETA DE COLORES PRINCIPAL
 *
 * Todos los colores de la UI deben venir de aquí.
 * Nunca usar RGB565() directamente en los archivos de UI.
 * ========================================================= */

/* Fondos */
#define THEME_BG            RGB565( 12,  12,  18)  /**< Fondo principal — negro azulado   */
#define THEME_BG_CARD       RGB565( 22,  24,  35)  /**< Fondo de cards / paneles          */
#define THEME_BG_INPUT      RGB565( 30,  33,  48)  /**< Fondo de campos de entrada        */
#define THEME_BG_OVERLAY    RGB565(  8,   8,  12)  /**< Fondo de diálogos modales         */

/* Texto */
#define THEME_TEXT_PRIMARY  RGB565(235, 237, 245)  /**< Texto principal — blanco suave    */
#define THEME_TEXT_SECONDARY RGB565(140, 145, 170) /**< Texto secundario — gris azulado   */
#define THEME_TEXT_DISABLED RGB565( 70,  73,  90)  /**< Texto deshabilitado — gris oscuro */
#define THEME_TEXT_INVERSE  RGB565( 10,  10,  15)  /**< Texto sobre fondos claros         */

/* Colores de acción — azul primario */
#define THEME_PRIMARY       RGB565( 64, 156, 255)  /**< Acción principal — azul brillante */
#define THEME_PRIMARY_DIM   RGB565( 30,  80, 140)  /**< Variante oscura del primario      */
#define THEME_PRIMARY_GLOW  RGB565( 90, 180, 255)  /**< Variante clara / hover            */

/* Colores semánticos */
#define THEME_SUCCESS       RGB565( 50, 210, 120)  /**< Confirmado / seguro — verde       */
#define THEME_SUCCESS_DIM   RGB565( 20,  90,  50)  /**< Variante oscura del success       */
#define THEME_WARNING       RGB565(255, 185,  50)  /**< Advertencia — naranja ámbar       */
#define THEME_WARNING_DIM   RGB565(120,  80,  15)  /**< Variante oscura del warning       */
#define THEME_ERROR         RGB565(230,  65,  65)  /**< Error / peligro — rojo coral      */
#define THEME_ERROR_DIM     RGB565(100,  25,  25)  /**< Variante oscura del error         */
#define THEME_INFO          RGB565( 90, 185, 255)  /**< Información — azul claro          */

/* Bitcoin */
#define THEME_BITCOIN       RGB565(242, 169,   0)  /**< Naranja Bitcoin oficial (#F29000)  */
#define THEME_BITCOIN_DIM   RGB565(120,  80,   0)  /**< Variante oscura del bitcoin       */

/* Bordes y separadores */
#define THEME_BORDER        RGB565( 45,  50,  72)  /**< Borde de cards y inputs           */
#define THEME_BORDER_FOCUS  RGB565( 64, 156, 255)  /**< Borde cuando el elemento tiene foco*/
#define THEME_SEPARATOR     RGB565( 35,  38,  55)  /**< Línea separadora sutil            */

/* Selector de menú */
#define THEME_SELECTED_BG   RGB565( 25,  55, 100)  /**< Fondo del ítem seleccionado       */
#define THEME_SELECTED_BAR  RGB565( 64, 156, 255)  /**< Barra lateral del seleccionado    */


/* =========================================================
 * TIPOGRAFÍA
 *
 * Tamaños de fuente para la fuente bitmap 5×7 de font.c
 * ========================================================= */

#define FONT_SMALL      1   /**< 5×7 px — metadatos, notas al pie    */
#define FONT_BODY       2   /**< 10×14 px — texto principal de la UI  */
#define FONT_TITLE      3   /**< 15×21 px — títulos y valores grandes */

/* Altura en píxeles de cada tamaño de fuente (para calcular layouts) */
#define FONT_H_SMALL    (8 * FONT_SMALL)    /*  8 px */
#define FONT_H_BODY     (8 * FONT_BODY)     /* 16 px */
#define FONT_H_TITLE    (8 * FONT_TITLE)    /* 24 px */

/* Ancho por caracter en píxeles */
#define FONT_W_SMALL    (6 * FONT_SMALL)    /*  6 px/char */
#define FONT_W_BODY     (6 * FONT_BODY)     /* 12 px/char */
#define FONT_W_TITLE    (6 * FONT_TITLE)    /* 18 px/char */


/* =========================================================
 * SISTEMA DE ESPACIADO
 *
 * Basado en unidades de 8px para consistencia visual.
 * ========================================================= */

#define SPACING_XS      4    /**<  4px — separación mínima           */
#define SPACING_SM      8    /**<  8px — espaciado interno de cards   */
#define SPACING_MD      16   /**< 16px — espaciado entre secciones   */
#define SPACING_LG      24   /**< 24px — márgenes principales        */
#define SPACING_XL      32   /**< 32px — separación entre pantallas  */


/* =========================================================
 * LAYOUT DE LA PANTALLA (480×320)
 * ========================================================= */

/* Barra de título superior */
#define LAYOUT_TOPBAR_H     40   /**< Altura de la barra superior        */
#define LAYOUT_TOPBAR_Y     0    /**< Posición Y de la barra superior    */

/* Área de contenido principal */
#define LAYOUT_CONTENT_Y    (LAYOUT_TOPBAR_H + 2)   /**< Inicio del contenido */
#define LAYOUT_CONTENT_H    (320 - LAYOUT_TOPBAR_H - LAYOUT_BOTTOMBAR_H - 2)

/* Barra inferior (botones / ayuda) */
#define LAYOUT_BOTTOMBAR_H  44   /**< Altura de la barra inferior        */
#define LAYOUT_BOTTOMBAR_Y  (320 - LAYOUT_BOTTOMBAR_H) /**< Pos Y barra inferior */

/* Márgenes del contenido */
#define LAYOUT_MARGIN_X     SPACING_LG   /**< Margen horizontal del contenido  */
#define LAYOUT_MARGIN_Y     SPACING_MD   /**< Margen vertical del contenido    */

/* Ancho del área de contenido sin márgenes */
#define LAYOUT_INNER_W      (480 - 2 * LAYOUT_MARGIN_X)   /* 432px */


/* =========================================================
 * COMPONENTES — DIMENSIONES
 * ========================================================= */

/* Botones */
#define BTN_HEIGHT          40   /**< Altura estándar de botón          */
#define BTN_RADIUS          8    /**< Radio de esquinas del botón       */
#define BTN_FULL_W          LAYOUT_INNER_W     /**< Botón ancho completo */
#define BTN_HALF_W          ((LAYOUT_INNER_W - SPACING_SM) / 2)  /**< Botón mitad */

/* Cards */
#define CARD_RADIUS         10   /**< Radio de esquinas de la card      */
#define CARD_PADDING        SPACING_SM

/* Barra de progreso */
#define PROGRESS_H          12   /**< Altura de la barra de progreso    */
#define PROGRESS_RADIUS     6    /**< Radio de las esquinas             */

/* Ítems de lista */
#define LIST_ITEM_H         44   /**< Altura de cada ítem de lista      */
#define LIST_ITEM_PADDING_X SPACING_MD

/* Diálogos modales */
#define DIALOG_MARGIN_X     30   /**< Margen horizontal del diálogo     */
#define DIALOG_MARGIN_Y     60   /**< Margen vertical del diálogo       */
#define DIALOG_W            (480 - 2 * DIALOG_MARGIN_X)  /* 420px */
#define DIALOG_H            (320 - 2 * DIALOG_MARGIN_Y)  /* 200px */
#define DIALOG_RADIUS       12


/* =========================================================
 * MACROS DE CONVENIENCIA
 * ========================================================= */

/** Centrar un elemento de ancho W en la pantalla */
#define CENTER_X(w)     ((480 - (w)) / 2)

/** Centrar texto de N caracteres en font size S */
#define CENTER_TEXT_X(n, s)   CENTER_X((n) * FONT_W_BODY * (s) / FONT_BODY)

/** Posición Y centrada verticalmente en el área de contenido */
#define CENTER_CONTENT_Y(h) \
    (LAYOUT_CONTENT_Y + (LAYOUT_CONTENT_H - (h)) / 2)

/** Verificar si las coordenadas están en el área de contenido */
#define IN_CONTENT(x, y) \
    ((x) >= 0 && (x) < 480 && \
     (y) >= LAYOUT_CONTENT_Y && (y) < LAYOUT_BOTTOMBAR_Y)

#endif /* UI_THEME_H */
