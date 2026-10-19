/**
 * @file    touch_xpt2046.h
 * @brief   [v0.2] Header reconstructed for touch_xpt2046.c (missing from the original draft)
 *
 * Rebuilt from the symbols used by touch_xpt2046.c and display_test.c.
 * The XPT2046 shares SPI1 with the ILI9488 (see display_tft.h).
 *
 * [VERIFY] TOUCH_IRQ: the pin does not appear in the original code; PC7 is an assumption.
 * [VERIFY] Default calibration values: typical for a 3.5" panel.
 * [NOTE]   The final CiWi uses a capacitive wheel, not resistive touch; this driver
 *          is only for testing the ILI9488 display module that ships with an XPT2046.
 *
 * @license GNU GPL v3
 */

#ifndef TOUCH_XPT2046_H
#define TOUCH_XPT2046_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

/* ---------------------------------------------------------------- pins */
#define TOUCH_CS_PORT        GPIOC
#define TOUCH_CS_PIN         GPIO_PIN_6      /* T_CS = PC6 (per touch_xpt2046.c) */
#define TOUCH_IRQ_PORT       GPIOC
#define TOUCH_IRQ_PIN        GPIO_PIN_7      /* [VERIFY] T_IRQ, active LOW */

#define TOUCH_CS_LOW()   HAL_GPIO_WritePin(TOUCH_CS_PORT, TOUCH_CS_PIN, GPIO_PIN_RESET)
#define TOUCH_CS_HIGH()  HAL_GPIO_WritePin(TOUCH_CS_PORT, TOUCH_CS_PIN, GPIO_PIN_SET)

/* ---------------------------------------------------------------- XPT2046 commands */
#define XPT2046_CMD_READ_X   0xD0   /* X channel, 12-bit, differential */
#define XPT2046_CMD_READ_Y   0x90   /* Y channel */
#define XPT2046_CMD_READ_Z1  0xB0   /* Z1 (pressure) */
#define XPT2046_CMD_READ_Z2  0xC0   /* Z2 (pressure) */

/* ---------------------------------------------------------------- parameters */
#define TOUCH_SAMPLES        7      /* samples per channel; max and min are discarded */
#define TOUCH_DEBOUNCE_MS    30
#define TOUCH_PRESSURE_MIN   100

/* Default calibration [VERIFY with touch_calibration()] */
#define TOUCH_CAL_X_MIN      200
#define TOUCH_CAL_X_MAX      3900
#define TOUCH_CAL_Y_MIN      200
#define TOUCH_CAL_Y_MAX      3900
#define TOUCH_INVERT_X       false
#define TOUCH_INVERT_Y       false
#define TOUCH_SWAP_XY        false

/* ---------------------------------------------------------------- types */
typedef struct {
    int16_t  x;          /**< screen coordinate */
    int16_t  y;
    uint16_t pressure;   /**< raw Z1 */
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
