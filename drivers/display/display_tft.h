/**
 * @file    display_tft.h
 * @brief   Driver del display TFT 3.5" ILI9488 — WalCorg Fase 2
 *
 * API pública del driver de display. El resto del firmware
 * (layout.c, ui_components.c) solo usa estas funciones —
 * nunca accede al SPI o a los registros del ILI9488 directamente.
 *
 * HARDWARE:
 *   Display : 3.5" TFT SPI 480×320 con driver ILI9488
 *   Bus     : SPI1 del STM32F405RGT6
 *   Pines   :
 *     PA5 → SPI1_SCK   (reloj SPI)
 *     PA6 → SPI1_MISO  (datos de vuelta — touch y lectura)
 *     PA7 → SPI1_MOSI  (datos al display)
 *     PC4 → TFT_CS     (chip select display, activo LOW)
 *     PC5 → TFT_DC     (Data/Command: HIGH=dato, LOW=cmd)
 *     PB0 → TFT_RST    (reset hardware, activo LOW)
 *     PB1 → TFT_BL     (backlight PWM via TIM3_CH4)
 *
 * VELOCIDAD SPI:
 *   SPI1 usa PCLK2 (84MHz). Con prescaler /4 = 21MHz.
 *   El ILI9488 soporta hasta 15MHz en escritura — usamos
 *   /8 = 10.5MHz para mayor estabilidad con cables de protoboard.
 *   En el PCB final podemos subir a /4 = 21MHz.
 *
 * @license GNU GPL v3
 */

#ifndef DISPLAY_TFT_H
#define DISPLAY_TFT_H

#include "stm32f4xx_hal.h"
#include "ili9488_regs.h"
#include <stdbool.h>
#include <stdint.h>

/* =========================================================
 * PINES DE CONTROL — ajustar si cambias el cableado
 * ========================================================= */
#define TFT_SPI_INSTANCE    SPI1

#define TFT_CS_PORT         GPIOC
#define TFT_CS_PIN          GPIO_PIN_4

#define TFT_DC_PORT         GPIOC
#define TFT_DC_PIN          GPIO_PIN_5

#define TFT_RST_PORT        GPIOB
#define TFT_RST_PIN         GPIO_PIN_0

#define TFT_BL_PORT         GPIOB
#define TFT_BL_PIN          GPIO_PIN_1

/* =========================================================
 * MACROS DE CONTROL — manipulación rápida de pines
 * ========================================================= */

/** CS: LOW = display activo, HIGH = display liberado */
#define TFT_CS_LOW()   HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_RESET)
#define TFT_CS_HIGH()  HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_SET)

/** DC: LOW = enviando comando, HIGH = enviando datos/píxeles */
#define TFT_DC_CMD()   HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_RESET)
#define TFT_DC_DAT()   HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_SET)

/** RST: LOW = reset activo, HIGH = operación normal */
#define TFT_RST_LOW()  HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_RESET)
#define TFT_RST_HIGH() HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_SET)

/** BL: backlight simple ON/OFF (sin PWM en Fase 2) */
#define TFT_BL_ON()    HAL_GPIO_WritePin(TFT_BL_PORT, TFT_BL_PIN, GPIO_PIN_SET)
#define TFT_BL_OFF()   HAL_GPIO_WritePin(TFT_BL_PORT, TFT_BL_PIN, GPIO_PIN_RESET)


/* =========================================================
 * DIMENSIONES Y ORIENTACIÓN
 * ========================================================= */

/** Orientaciones disponibles */
typedef enum {
    TFT_ROTATION_0   = 0,   /**< Portrait  normal    (320×480) */
    TFT_ROTATION_90  = 1,   /**< Landscape normal    (480×320) ← default */
    TFT_ROTATION_180 = 2,   /**< Portrait  invertido (320×480) */
    TFT_ROTATION_270 = 3,   /**< Landscape invertido (480×320) */
} tft_rotation_t;

/** Tipo de coordenada */
typedef int16_t tft_coord_t;

/** Tipo de color RGB565 */
typedef uint16_t tft_color_t;

/** Estructura de punto 2D */
typedef struct { tft_coord_t x; tft_coord_t y; } tft_point_t;

/** Estructura de rectángulo */
typedef struct {
    tft_coord_t x;   /**< Esquina superior izquierda X */
    tft_coord_t y;   /**< Esquina superior izquierda Y */
    tft_coord_t w;   /**< Ancho en píxeles */
    tft_coord_t h;   /**< Alto en píxeles */
} tft_rect_t;


/* =========================================================
 * ESTADO DEL DRIVER
 * ========================================================= */

/** Estado completo del driver — opaco para el usuario */
typedef struct {
    uint16_t        width;       /**< Ancho actual (depende de rotation) */
    uint16_t        height;      /**< Alto actual  (depende de rotation) */
    tft_rotation_t  rotation;    /**< Orientación actual */
    bool            initialized; /**< Driver inicializado */
    uint32_t        fps_count;   /**< Contador de frames para debug */
    uint32_t        spi_errors;  /**< Errores SPI acumulados */
} tft_state_t;

/** Estado global accesible para debug */
extern tft_state_t g_tft;

/** Handle SPI — accedido desde DMA callbacks */
extern SPI_HandleTypeDef hspi1;


/* =========================================================
 * API PÚBLICA — INICIALIZACIÓN
 * ========================================================= */

/**
 * @brief Inicializa el display TFT completo.
 *
 * Secuencia:
 *   1. Configura SPI1 a 10.5MHz (PCLK2/8), CPOL=0, CPHA=0
 *   2. Configura GPIO: CS, DC, RST, BL como salidas
 *   3. Aplica reset hardware (RST LOW 10ms → HIGH)
 *   4. Envía la secuencia de comandos ILI9488_INIT_SEQ
 *   5. Establece orientación landscape (480×320)
 *   6. Enciende el backlight
 *   7. Limpia la pantalla a negro
 *
 * @param rotation  Orientación inicial del display
 * @return true     Si la inicialización fue exitosa
 * @return false    Si hay error de comunicación SPI
 */
bool tft_init(tft_rotation_t rotation);

/**
 * @brief Cambia la orientación del display en tiempo de ejecución.
 * @param rotation  Nueva orientación
 */
void tft_set_rotation(tft_rotation_t rotation);

/**
 * @brief Controla el backlight del display.
 * @param brightness  Brillo: 0=apagado, 255=máximo
 *                    (En Fase 2 solo 0 o 255 — sin PWM todavía)
 */
void tft_set_brightness(uint8_t brightness);

/**
 * @brief Retorna el ancho actual en píxeles (depende de rotation).
 */
uint16_t tft_get_width(void);

/**
 * @brief Retorna el alto actual en píxeles (depende de rotation).
 */
uint16_t tft_get_height(void);


/* =========================================================
 * API PÚBLICA — PRIMITIVAS GRÁFICAS
 * ========================================================= */

/**
 * @brief Dibuja un píxel en las coordenadas dadas.
 *
 * Operación más básica del driver. Cada llamada tiene el
 * overhead de configurar CASET+PASET+RAMWR — para dibujar
 * múltiples píxeles, usar fillRect() es mucho más eficiente.
 *
 * @param x      Coordenada X (0 = izquierda)
 * @param y      Coordenada Y (0 = arriba)
 * @param color  Color en formato RGB565
 */
void tft_draw_pixel(tft_coord_t x, tft_coord_t y, tft_color_t color);

/**
 * @brief Rellena un rectángulo con un color sólido.
 *
 * Operación más utilizada en la UI — fondos, botones, barras.
 * Mucho más eficiente que llamar draw_pixel N×M veces porque
 * configura CASET+PASET una sola vez y envía todos los píxeles
 * en una sola transferencia SPI (posiblemente con DMA).
 *
 * @param x, y   Esquina superior izquierda
 * @param w, h   Ancho y alto del rectángulo
 * @param color  Color de relleno en RGB565
 */
void tft_fill_rect(tft_coord_t x, tft_coord_t y,
                   tft_coord_t w, tft_coord_t h,
                   tft_color_t color);

/**
 * @brief Limpia toda la pantalla con un color.
 * Equivalente a tft_fill_rect(0, 0, width, height, color).
 * @param color  Color de fondo (usar ILI9488_BLACK para la UI del wallet)
 */
void tft_clear(tft_color_t color);

/**
 * @brief Dibuja una línea horizontal.
 * Optimizada para el ILI9488 — más rápida que draw_line genérico.
 */
void tft_draw_hline(tft_coord_t x, tft_coord_t y,
                    tft_coord_t len, tft_color_t color);

/**
 * @brief Dibuja una línea vertical.
 * Optimizada para el ILI9488.
 */
void tft_draw_vline(tft_coord_t x, tft_coord_t y,
                    tft_coord_t len, tft_color_t color);

/**
 * @brief Dibuja una línea entre dos puntos (algoritmo Bresenham).
 */
void tft_draw_line(tft_coord_t x0, tft_coord_t y0,
                   tft_coord_t x1, tft_coord_t y1,
                   tft_color_t color);

/**
 * @brief Dibuja el borde de un rectángulo (sin relleno).
 */
void tft_draw_rect(tft_coord_t x, tft_coord_t y,
                   tft_coord_t w, tft_coord_t h,
                   tft_color_t color);

/**
 * @brief Dibuja un rectángulo con esquinas redondeadas.
 * @param r  Radio de las esquinas en píxeles
 */
void tft_draw_round_rect(tft_coord_t x, tft_coord_t y,
                          tft_coord_t w, tft_coord_t h,
                          tft_coord_t r, tft_color_t color);

/**
 * @brief Rellena un rectángulo con esquinas redondeadas.
 */
void tft_fill_round_rect(tft_coord_t x, tft_coord_t y,
                          tft_coord_t w, tft_coord_t h,
                          tft_coord_t r, tft_color_t color);

/**
 * @brief Dibuja el borde de un círculo.
 */
void tft_draw_circle(tft_coord_t cx, tft_coord_t cy,
                     tft_coord_t r, tft_color_t color);

/**
 * @brief Rellena un círculo.
 */
void tft_fill_circle(tft_coord_t cx, tft_coord_t cy,
                     tft_coord_t r, tft_color_t color);

/**
 * @brief Dibuja una imagen bitmap RGB565 en la pantalla.
 *
 * El bitmap debe ser un array de uint16_t con w×h píxeles
 * en formato RGB565, almacenado en Flash (const).
 *
 * Uso principal: logo de WalCorg en la pantalla de inicio.
 *
 * @param x, y    Posición de la esquina superior izquierda
 * @param w, h    Dimensiones del bitmap
 * @param bitmap  Puntero al array de píxeles en Flash
 */
void tft_draw_bitmap(tft_coord_t x, tft_coord_t y,
                     tft_coord_t w, tft_coord_t h,
                     const uint16_t *bitmap);

/**
 * @brief Dibuja un bitmap de 1 bit (máscara monocromática).
 * Útil para íconos pequeños y símbolos en la UI.
 * @param fg  Color de los píxeles activos (bit=1)
 * @param bg  Color de los píxeles inactivos (bit=0)
 */
void tft_draw_bitmap_1bit(tft_coord_t x, tft_coord_t y,
                           tft_coord_t w, tft_coord_t h,
                           const uint8_t *bitmap,
                           tft_color_t fg, tft_color_t bg);


/* =========================================================
 * API PÚBLICA — TEXTO
 * ========================================================= */

/**
 * @brief Dibuja un caracter ASCII en la pantalla.
 * @param x, y  Posición de la esquina superior izquierda del caracter
 * @param c     Caracter ASCII (32-126)
 * @param fg    Color del texto
 * @param bg    Color del fondo (usar color de pantalla para transparente)
 * @param size  Factor de escala: 1=8×16px, 2=16×32px, 3=24×48px
 */
void tft_draw_char(tft_coord_t x, tft_coord_t y,
                   char c, tft_color_t fg, tft_color_t bg,
                   uint8_t size);

/**
 * @brief Dibuja un string en la pantalla.
 * @param x, y  Posición inicial
 * @param str   String terminado en '\0'
 * @param fg    Color del texto
 * @param bg    Color del fondo
 * @param size  Factor de escala (1-3)
 */
void tft_draw_string(tft_coord_t x, tft_coord_t y,
                     const char *str,
                     tft_color_t fg, tft_color_t bg,
                     uint8_t size);

/**
 * @brief Dibuja un string centrado horizontalmente.
 * @param y     Posición vertical
 * @param str   String a centrar
 * @param fg    Color del texto
 * @param bg    Color del fondo
 * @param size  Factor de escala
 */
void tft_draw_string_centered(tft_coord_t y,
                               const char *str,
                               tft_color_t fg, tft_color_t bg,
                               uint8_t size);

/**
 * @brief Calcula el ancho en píxeles de un string.
 * Útil para centrar texto manualmente.
 * @return Ancho en píxeles
 */
uint16_t tft_string_width(const char *str, uint8_t size);


/* =========================================================
 * API PÚBLICA — VENTANA DE ESCRITURA (Window)
 *
 * Para escritura eficiente de múltiples píxeles en una región.
 * Protocolo:
 *   1. tft_set_window(x, y, w, h) → define la región
 *   2. tft_write_pixel(color)     → N veces (w×h píxeles)
 *   3. tft_end_write()            → libera el bus
 * ========================================================= */

/**
 * @brief Define la ventana de escritura y prepara el bus SPI.
 * Después de esta llamada, enviar exactamente w×h píxeles
 * con tft_write_pixel() o tft_write_pixels().
 */
void tft_set_window(tft_coord_t x, tft_coord_t y,
                    tft_coord_t w, tft_coord_t h);

/**
 * @brief Escribe un píxel en la ventana activa.
 * Solo válido entre tft_set_window() y tft_end_write().
 */
void tft_write_pixel(tft_color_t color);

/**
 * @brief Escribe N píxeles del mismo color en la ventana activa.
 * Más eficiente que llamar tft_write_pixel() N veces.
 */
void tft_write_pixels(tft_color_t color, uint32_t count);

/**
 * @brief Escribe un buffer de píxeles en la ventana activa.
 * @param pixels  Array de colores RGB565
 * @param count   Número de píxeles a escribir
 */
void tft_write_buffer(const uint16_t *pixels, uint32_t count);

/**
 * @brief Finaliza la escritura y libera el bus SPI (CS HIGH).
 */
void tft_end_write(void);


/* =========================================================
 * API PÚBLICA — UTILIDADES Y DEBUG
 * ========================================================= */

/**
 * @brief Verifica que el IC responde correctamente.
 * Lee el ID del display (comando 0x04) y verifica que
 * los bytes corresponden al ILI9488.
 * @return true si el display responde, false si hay error SPI
 */
bool tft_verify_id(void);

/**
 * @brief Imprime el estado del driver por UART (debug).
 */
void tft_print_status(void);

#endif /* DISPLAY_TFT_H */
