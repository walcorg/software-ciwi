/**
 * @file    touch_xpt2046.h
 * @brief   [v0.2] Header reconstruido para touch_xpt2046.c (faltaba en el zip original)
 *
 * Reconstruido a partir de los símbolos que usa touch_xpt2046.c y display_test.c.
 * El XPT2046 comparte SPI1 con el ILI9488 (ver display_tft.h).
 *
 * [VERIFICAR] TOUCH_IRQ: el pin no aparece en el código original; PC7 es un supuesto.
 * [VERIFICAR] Valores de calibración por defecto: típicos para un panel de 3.5".
 * [NOTA] La CiWi final usa rueda capacitiva, no touch resistivo; este driver
 *        solo sirve para probar el módulo de pantalla ILI9488 que trae XPT2046.
 *
 * @license GNU GPL v3
 */

#ifndef TOUCH_XPT2046_H
#define TOUCH_XPT2046_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

/* ---------------------------------------------------------------- pines */
#define TOUCH_CS_PORT        GPIOC
#define TOUCH_CS_PIN         GPIO_PIN_6      /* T_CS = PC6 (según touch_xpt2046.c) */
#define TOUCH_IRQ_PORT       GPIOC
#define TOUCH_IRQ_PIN        GPIO_PIN_7      /* [VERIFICAR] T_IRQ, activo en LOW */

#define TOUCH_CS_LOW()   HAL_GPIO_WritePin(TOUCH_CS_PORT, TOUCH_CS_PIN, GPIO_PIN_RESET)
#define TOUCH_CS_HIGH()  HAL_GPIO_WritePin(TOUCH_CS_PORT, TOUCH_CS_PIN, GPIO_PIN_SET)

/* ---------------------------------------------------------------- comandos XPT2046 */
#define XPT2046_CMD_READ_X   0xD0   /* canal X, 12 bits, diferencial */
#define XPT2046_CMD_READ_Y   0x90   /* canal Y */
#define XPT2046_CMD_READ_Z1  0xB0   /* Z1 (presión) */
#define XPT2046_CMD_READ_Z2  0xC0   /* Z2 (presión) */

/* ---------------------------------------------------------------- parámetros */
#define TOUCH_SAMPLES        7      /* lecturas por canal; se descartan máx y mín */
#define TOUCH_DEBOUNCE_MS    30
#define TOUCH_PRESSURE_MIN   100

/* Calibración por defecto [VERIFICAR con touch_calibration()] */
#define TOUCH_CAL_X_MIN      200
#define TOUCH_CAL_X_MAX      3900
#define TOUCH_CAL_Y_MIN      200
#define TOUCH_CAL_Y_MAX      3900
#define TOUCH_INVERT_X       false
#define TOUCH_INVERT_Y       false
#define TOUCH_SWAP_XY        false

/* ---------------------------------------------------------------- tipos */
typedef struct {
    int16_t  x;          /**< coordenada de pantalla */
    int16_t  y;
    uint16_t pressure;   /**< Z1 crudo */
    bool     valid;
} touch_point_t;

typedef struct {
    uint16_t raw_x;
    uint16_t raw_y;
    uint16_t raw_z1;
    uint16_t raw_z2;
} touch_raw_t;

typedef struct {
    uint16_t x_min;
    uint16_t x_max;
    uint16_t y_min;
    uint16_t y_max;
    bool     invert_x;
    bool     invert_y;
    bool     swap_xy;
    bool     calibrated;
} touch_cal_t;

/* ---------------------------------------------------------------- API */
bool touch_init(void);
bool touch_is_pressed(void);
bool touch_read(touch_point_t *point);
bool touch_read_raw(touch_raw_t *raw);
void touch_set_calibration(const touch_cal_t *cal);
void touch_get_calibration(touch_cal_t *cal);
void touch_wait_release(uint32_t timeout_ms);
void touch_print_status(void);

#endif /* TOUCH_XPT2046_H */
