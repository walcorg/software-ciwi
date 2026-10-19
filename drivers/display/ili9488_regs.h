/**
 * @file    ili9488_regs.h
 * @brief   Registros y comandos del controlador ILI9488 — WalCorg Fase 2
 *
 * El ILI9488 es el controlador del display TFT de 3.5" (480×320).
 * Se comunica via SPI de 4 hilos:
 *   SCK  → reloj
 *   MOSI → datos hacia el display
 *   MISO → datos desde el display (solo touch y lectura de registros)
 *   DC   → Data/Command selector (HIGH=dato, LOW=comando)
 *   CS   → Chip Select (LOW=activo)
 *
 * PROTOCOLO SPI DEL ILI9488:
 *   Modo SPI: CPOL=0, CPHA=0 (SPI Mode 0)
 *   Velocidad máxima: 15MHz escritura, 6.66MHz lectura
 *   Orden de bits: MSB primero
 *
 *   Para enviar un COMANDO:
 *     1. DC = LOW  (indica que el siguiente byte es un comando)
 *     2. CS = LOW
 *     3. Enviar 1 byte del comando (ej: 0x2A para CASET)
 *     4. DC = HIGH (los bytes siguientes son parámetros/datos)
 *     5. Enviar N bytes de parámetros
 *     6. CS = HIGH
 *
 * SISTEMA DE COORDENADAS:
 *   Resolución nativa: 480 × 320 píxeles
 *   Orientación landscape (horizontal): ancho=480, alto=320
 *   Orientación portrait  (vertical):   ancho=320, alto=480
 *
 * FORMATO DE COLOR:
 *   El ILI9488 soporta múltiples formatos. Usamos RGB666 (18 bits)
 *   que es el formato nativo del IC, o RGB565 (16 bits) que es
 *   más eficiente para transferencia SPI.
 *
 * @license GNU GPL v3
 */

#ifndef ILI9488_REGS_H
#define ILI9488_REGS_H

#include <stdint.h>

/* =========================================================
 * COMANDOS NIVEL 1 — Comandos básicos de control
 * Definidos en la especificación MIPI DCS (Display Command Set)
 * ========================================================= */

/** No Operation — no hace nada, útil para timing */
#define ILI9488_CMD_NOP                 0x00

/** Software Reset — reinicia el IC a valores de fábrica
 *  Esperar 5ms antes del siguiente comando */
#define ILI9488_CMD_SWRESET             0x01

/** Read Display Identification — retorna 3 bytes del fabricante */
#define ILI9488_CMD_RDDID               0x04

/** Read Display Status — 4 bytes con el estado actual */
#define ILI9488_CMD_RDDST               0x09

/** Sleep In — modo de bajo consumo (display apagado) */
#define ILI9488_CMD_SLPIN               0x10

/** Sleep Out — salir del modo sleep
 *  Esperar 120ms antes del siguiente comando */
#define ILI9488_CMD_SLPOUT              0x11

/** Partial Mode On — activar modo parcial */
#define ILI9488_CMD_PTLON               0x12

/** Normal Display Mode On — desactivar modo parcial */
#define ILI9488_CMD_NORON               0x13

/** Display Inversion Off — colores normales */
#define ILI9488_CMD_INVOFF              0x20

/** Display Inversion On — colores invertidos
 *  Útil si el display aparece con colores incorrectos */
#define ILI9488_CMD_INVON               0x21

/** Gamma Set — seleccionar curva gamma (parámetro: 1/2/4/8) */
#define ILI9488_CMD_GAMSET              0x26

/** Display Off — apagar display (backlight sigue encendido) */
#define ILI9488_CMD_DISPOFF             0x28

/** Display On — encender display */
#define ILI9488_CMD_DISPON              0x29

/**
 * Column Address Set — define el rango horizontal de escritura
 * Parámetros (4 bytes): SC_H, SC_L, EC_H, EC_L
 *   SC = Start Column (columna de inicio, 16 bits)
 *   EC = End Column   (columna de fin, 16 bits)
 * Ejemplo landscape: SC=0, EC=479 → escribir toda la fila
 */
#define ILI9488_CMD_CASET               0x2A

/**
 * Page Address Set — define el rango vertical de escritura
 * Parámetros (4 bytes): SP_H, SP_L, EP_H, EP_L
 *   SP = Start Page (fila de inicio, 16 bits)
 *   EP = End Page   (fila de fin, 16 bits)
 */
#define ILI9488_CMD_PASET               0x2B

/**
 * Memory Write — escribir datos de píxeles al display
 * Después de este comando, enviar los bytes de color de cada píxel.
 * El IC incrementa automáticamente la posición según MADCTL.
 */
#define ILI9488_CMD_RAMWR               0x2C

/** Memory Read — leer datos de píxeles del display (lectura) */
#define ILI9488_CMD_RAMRD               0x2E

/**
 * Memory Access Control — controla orientación y orden de los datos
 * Parámetro: 1 byte con flags:
 *   Bit 7 (MY):  Row Address Order       (0=top→bottom, 1=bottom→top)
 *   Bit 6 (MX):  Column Address Order    (0=left→right, 1=right→left)
 *   Bit 5 (MV):  Row/Column Exchange     (0=normal,  1=intercambiar X/Y)
 *   Bit 4 (ML):  Vertical Refresh Order  (0=top→bottom)
 *   Bit 3 (BGR): Color Order             (0=RGB, 1=BGR)
 *   Bit 2 (MH):  Horizontal Refresh      (0=left→right)
 *
 * Valores comunes para orientación:
 *   0x48 = Landscape (480×320), USB arriba a la izquierda
 *   0x28 = Landscape invertido
 *   0x88 = Portrait  (320×480)
 *   0xE8 = Portrait  invertido
 */
#define ILI9488_CMD_MADCTL              0x36

/** MADCTL flags individuales */
#define ILI9488_MADCTL_MY               0x80  /**< Row order invertido */
#define ILI9488_MADCTL_MX               0x40  /**< Column order invertido */
#define ILI9488_MADCTL_MV               0x20  /**< Intercambiar filas/columnas */
#define ILI9488_MADCTL_ML               0x10  /**< Refresh vertical invertido */
#define ILI9488_MADCTL_BGR              0x08  /**< Orden BGR en vez de RGB */
#define ILI9488_MADCTL_MH               0x04  /**< Refresh horizontal invertido */

/**
 * Interface Pixel Format — seleccionar profundidad de color
 * Parámetro: 1 byte
 *   0x55 = 16 bits por píxel (RGB565) — 2 bytes por píxel
 *   0x66 = 18 bits por píxel (RGB666) — 3 bytes por píxel (nativo del IC)
 *
 * Usamos 0x55 (RGB565) porque:
 *   - Ahorra 33% de ancho de banda SPI vs RGB666
 *   - Compatible con el formato de la crypto lib de Trezor
 *   - 65,536 colores son suficientes para la UI del wallet
 */
#define ILI9488_CMD_COLMOD              0x3A
#define ILI9488_COLMOD_16BIT            0x55  /**< RGB565 — 2 bytes/píxel */
#define ILI9488_COLMOD_18BIT            0x66  /**< RGB666 — 3 bytes/píxel */


/* =========================================================
 * COMANDOS NIVEL 2 — Configuración avanzada del IC
 * Específicos del ILI9488, no parte del estándar MIPI DCS
 * ========================================================= */

/** Power Control 1 — voltaje del panel LCD */
#define ILI9488_CMD_PWCTR1              0xC0

/** Power Control 2 — voltaje del step-up del panel */
#define ILI9488_CMD_PWCTR2              0xC1

/** Power Control 3 — configuración de amplificador operacional */
#define ILI9488_CMD_PWCTR3              0xC2

/** VCOM Control 1 — ajuste del voltaje VCOM
 *  Afecta el contraste del display */
#define ILI9488_CMD_VMCTR1              0xC5

/** Interface Mode Control — configuración del bus SPI */
#define ILI9488_CMD_IFMODE              0xB0

/** Frame Rate Control — velocidad de refresco (Normal Mode)
 *  Parámetros: DIVA, RTNA
 *  Valores típicos: 0xA0=60Hz (DIVA=1, RTNA=0) */
#define ILI9488_CMD_FRMCTR1             0xB1

/** Display Function Control — configuración del panel */
#define ILI9488_CMD_DISCTRL             0xB6

/** Entry Mode Set */
#define ILI9488_CMD_ETMOD               0xB7

/**
 * Positive Gamma Control — curva de gamma para colores claros
 * 15 parámetros que definen la respuesta gamma del panel
 */
#define ILI9488_CMD_PGAMCTRL            0xE0

/**
 * Negative Gamma Control — curva de gamma para colores oscuros
 * 15 parámetros
 */
#define ILI9488_CMD_NGAMCTRL            0xE1

/** Adjust Control 3 — ajustes internos del IC */
#define ILI9488_CMD_ADJCTRL3            0xF7

/** SPI Read/Write Control */
#define ILI9488_CMD_SPI_RW_CTRL         0xFB

/** Set Image Function — habilitar/deshabilitar procesamiento de imagen */
#define ILI9488_CMD_SET_IMAGE_FUNC      0xE9


/* =========================================================
 * DIMENSIONES DEL DISPLAY
 * ========================================================= */

/** Ancho en landscape (orientación horizontal) */
#define ILI9488_WIDTH_LANDSCAPE         480

/** Alto en landscape (orientación horizontal) */
#define ILI9488_HEIGHT_LANDSCAPE        320

/** Ancho en portrait (orientación vertical) */
#define ILI9488_WIDTH_PORTRAIT          320

/** Alto en portrait (orientación vertical) */
#define ILI9488_HEIGHT_PORTRAIT         480

/** Total de píxeles del display */
#define ILI9488_TOTAL_PIXELS            (ILI9488_WIDTH_LANDSCAPE * ILI9488_HEIGHT_LANDSCAPE)


/* =========================================================
 * COLORES PREDEFINIDOS EN FORMATO RGB565
 *
 * RGB565: RRRRRGGGGGGBBBBB
 *   5 bits rojo + 6 bits verde + 5 bits azul = 16 bits total
 *
 * Macro para convertir RGB888 → RGB565:
 *   R(0-255), G(0-255), B(0-255) → uint16_t
 * ========================================================= */

/** Macro de conversión RGB888 a RGB565 */
#define RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))   /* [v0.2] arguments parenthesized */

/* Colores básicos */
#define ILI9488_BLACK       RGB565(  0,   0,   0)
#define ILI9488_WHITE       RGB565(255, 255, 255)
#define ILI9488_RED         RGB565(255,   0,   0)
#define ILI9488_GREEN       RGB565(  0, 255,   0)
#define ILI9488_BLUE        RGB565(  0,   0, 255)
#define ILI9488_YELLOW      RGB565(255, 255,   0)
#define ILI9488_CYAN        RGB565(  0, 255, 255)
#define ILI9488_MAGENTA     RGB565(255,   0, 255)
#define ILI9488_ORANGE      RGB565(255, 165,   0)
#define ILI9488_GRAY        RGB565(128, 128, 128)
#define ILI9488_DARKGRAY    RGB565( 64,  64,  64)
#define ILI9488_LIGHTGRAY   RGB565(192, 192, 192)

/* Colores del tema WalCorg (de ui_theme.h en Fase 4) */
#define WALCORG_BG          RGB565( 18,  18,  18)  /**< Fondo oscuro */
#define WALCORG_PRIMARY     RGB565(  0, 163, 255)  /**< Azul principal */
#define WALCORG_SUCCESS     RGB565(  0, 200, 100)  /**< Verde confirmación */
#define WALCORG_WARNING     RGB565(255, 165,   0)  /**< Naranja advertencia */
#define WALCORG_ERROR       RGB565(220,  50,  50)  /**< Rojo error */
#define WALCORG_TEXT        RGB565(230, 230, 230)  /**< Texto principal */
#define WALCORG_SUBTEXT     RGB565(150, 150, 150)  /**< Texto secundario */
#define WALCORG_DARKGRAY    RGB565( 60,  60,  60)  /**< [v0.2] Dark gray (used by menu.c) */


/* =========================================================
 * SECUENCIA DE INICIALIZACIÓN DEL ILI9488
 *
 * Esta es la secuencia de comandos que debe enviarse al IC
 * después del reset para configurarlo correctamente.
 *
 * Cada entrada es: { comando, num_params, param1, param2, ... }
 * Un delay especial se indica con comando = 0xFF y param = ms
 *
 * Valores basados en el datasheet del ILI9488 y ajustados
 * para el módulo específico de 3.5" SPI 480×320.
 * ========================================================= */

/** Tipo para una entrada de la secuencia de init */
typedef struct {
    uint8_t cmd;        /**< Código del comando (0xFF = delay) */
    uint8_t num_args;   /**< Número de parámetros (0xFF con cmd=0xFF = delay ms) */
    uint8_t args[16];   /**< Parámetros del comando */
} ili9488_init_cmd_t;

/** Marcador de fin de la secuencia de inicialización */
#define ILI9488_INIT_END    { 0x00, 0x00, {0} }

/** Delay especial en la secuencia: { 0xFF, ms, {0} } */
#define ILI9488_DELAY(ms)   { 0xFF, (ms), {0} }

/**
 * Secuencia de inicialización completa del ILI9488
 * Basada en el datasheet ILI9488 v0.24 y ajustada
 * para el módulo TFT 3.5" 480×320 SPI de la imagen del listing.
 */
static const ili9488_init_cmd_t ILI9488_INIT_SEQ[] = {

    /* Software Reset — resetear a valores de fábrica */
    { ILI9488_CMD_SWRESET,  0, {0} },
    ILI9488_DELAY(150),             /* Esperar 150ms tras reset */

    /* Sleep Out — salir del modo sleep (el IC arranca en sleep) */
    { ILI9488_CMD_SLPOUT,   0, {0} },
    ILI9488_DELAY(120),             /* Esperar 120ms — requerido por datasheet */

    /* Interface Pixel Format — RGB565 (16 bits por píxel) */
    { ILI9488_CMD_COLMOD,   1, {ILI9488_COLMOD_16BIT} },

    /* Power Control 1 — configurar voltajes del panel
     * 0x17: VREG1OUT = 4.8V (voltaje del panel positivo)
     * 0x15: VREG2OUT = -4.8V (voltaje del panel negativo)
     * Valores recomendados para el módulo de 3.5" */
    { ILI9488_CMD_PWCTR1,   2, {0x17, 0x15} },

    /* Power Control 2 — factor del step-up del voltaje
     * 0x41: VGH = 2×VCI (14.4V), VGL = -2×VCI (-7.2V) */
    { ILI9488_CMD_PWCTR2,   1, {0x41} },

    /* VCOM Control — ajuste de contraste
     * 0x00: VCOM_SEL habilitado
     * 0x12: VMH = 0.525V
     * 0x80: VML automático */
    { ILI9488_CMD_VMCTR1,   3, {0x00, 0x12, 0x80} },

    /* Memory Access Control — orientación landscape, BGR
     * MX=1, MV=1 → landscape (480×320)
     * BGR=1 → el panel físico usa BGR en vez de RGB */
    { ILI9488_CMD_MADCTL,   1, {ILI9488_MADCTL_MX |
                                 ILI9488_MADCTL_MV |
                                 ILI9488_MADCTL_BGR} },

    /* Interface Mode Control — SPI 4 hilos */
    { ILI9488_CMD_IFMODE,   1, {0x00} },

    /* Frame Rate Control — 60Hz
     * DIVA=0xA0, RTNA=0x11 → ~60 frames por segundo */
    { ILI9488_CMD_FRMCTR1,  2, {0xA0, 0x11} },

    /* Display Function Control */
    { ILI9488_CMD_DISCTRL,  3, {0x02, 0x22, 0x3B} },

    /* Entry Mode Set */
    { ILI9488_CMD_ETMOD,    1, {0xC6} },

    /* Positive Gamma Control — 15 parámetros
     * Calibrados para el panel del módulo de 3.5" */
    { ILI9488_CMD_PGAMCTRL, 15,
      {0x00, 0x04, 0x0E, 0x08, 0x17, 0x0A, 0x40,
       0x79, 0x4D, 0x07, 0x0E, 0x0A, 0x1A, 0x1E, 0x0F} },

    /* Negative Gamma Control — 15 parámetros */
    { ILI9488_CMD_NGAMCTRL, 15,
      {0x00, 0x1B, 0x1F, 0x02, 0x10, 0x05, 0x32,
       0x34, 0x43, 0x02, 0x0A, 0x09, 0x33, 0x37, 0x0F} },

    /* Adjust Control 3 */
    { ILI9488_CMD_ADJCTRL3, 4, {0xA9, 0x51, 0x2C, 0x82} },

    /* Display On */
    { ILI9488_CMD_DISPON,   0, {0} },
    ILI9488_DELAY(20),

    /* Normal Mode On */
    { ILI9488_CMD_NORON,    0, {0} },
    ILI9488_DELAY(10),

    /* Fin de la secuencia */
    ILI9488_INIT_END
};

#endif /* ILI9488_REGS_H */
