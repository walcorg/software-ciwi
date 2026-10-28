/**
 * @file    clickwheel.c
 * @brief   Driver click-wheel via I2C — WALCORG Cold Wallet
 *
 * Implementación para AS5600 (encoder magnético de 12 bits via I2C).
 * El AS5600 entrega el ángulo absoluto en 12 bits (0–4095),
 * lo que permite detectar dirección y velocidad del giro sin
 * necesidad de interrupciones en los canales A/B de un encoder
 * mecánico convencional.
 *
 * Flujo de datos:
 *   AS5600 --I2C--> cw_read_raw_angle()
 *                       ↓
 *                   cw_poll()  ← llamar cada 10ms
 *                       ↓
 *                   cw_event_t → firmware principal (layout.c / protect.c)
 *
 * Referencia AS5600:
 *   Registro ANGLE_H : 0x0E  (bits 11:8)
 *   Registro ANGLE_L : 0x0F  (bits  7:0)
 *   Ángulo = (ANGLE_H << 8) | ANGLE_L   rango 0–4095
 *
 * @license GNU GPL v3
 */

#include "clickwheel.h"

/* STM32 HAL — ajusta el path según tu estructura de proyecto.
 * En el firmware de Trezor legacy este header está en:
 *   vendor/libopencm3/  o  legacy/vendor/STM32F4xx_HAL/
 * Para desarrollo independiente con CubeIDE usa el path estándar: */
#include "stm32f4xx_hal.h"

/* =========================================================
 * REGISTROS DEL AS5600
 * ========================================================= */
#define AS5600_REG_STATUS       0x0B   /**< STATUS: MH, ML, MD flags   */
#define AS5600_REG_ANGLE_H      0x0E   /**< Ángulo bits 11:8           */
#define AS5600_REG_ANGLE_L      0x0F   /**< Ángulo bits 7:0            */
#define AS5600_REG_AGC          0x1A   /**< Automatic Gain Control     */
#define AS5600_REG_MAGNITUDE_H  0x1B   /**< Magnitud del campo magn.   */
#define AS5600_STATUS_MD        0x20   /**< Magnet Detected bit        */

/* =========================================================
 * HANDLE I2C — el handle global de HAL
 * ========================================================= */
static I2C_HandleTypeDef hi2c1;

/* =========================================================
 * FUNCIONES PRIVADAS — declaración adelantada
 * ========================================================= */
static void     _i2c1_gpio_init(void);
static void     _i2c1_peripheral_init(void);
static void     _btn_gpio_init(void);
static uint32_t _get_ms(void);
static int16_t  _angle_delta(uint16_t new_angle, uint16_t old_angle);

/* =========================================================
 * IMPLEMENTACIÓN PÚBLICA
 * ========================================================= */

/**
 * cw_init — Inicializa I2C1 en PB6/PB7 y GPIO del botón en PB8.
 *
 * Secuencia:
 *  1. Habilitar clocks de GPIOB e I2C1
 *  2. Configurar PB6 como I2C1_SCL, PB7 como I2C1_SDA (AF4, open-drain)
 *  3. Configurar PB8 como GPIO input con pull-up interno
 *  4. Inicializar periférico I2C1 a 400kHz (Fast Mode)
 *  5. Verificar presencia del AS5600 y que detecte imán
 */
bool cw_init(cw_state_t *state) {
    if (!state) return false;

    /* Limpiar estado */
    state->last_angle       = 0;
    state->accumulated      = 0;
    state->last_scroll_ms   = 0;
    state->btn_press_ms     = 0;
    state->last_click_ms    = 0;
    state->btn_was_pressed  = false;
    state->long_press_fired = false;
    state->click_count      = 0;
    state->initialized      = false;

    /* 1. Inicializar hardware */
    _i2c1_gpio_init();
    _i2c1_peripheral_init();
    _btn_gpio_init();

    /* 2. Verificar que el AS5600 responde */
    if (!cw_is_connected()) {
        /* Sin respuesta: revisar:
         *   - Pull-ups 4.7kΩ en SDA y SCL
         *   - Conexión VCC (3.3V) y GND del AS5600
         *   - Dirección correcta (0x36, no configurable en AS5600) */
        return false;
    }

    /* 3. Leer ángulo inicial como referencia */
    if (!cw_read_raw_angle(&state->last_angle)) {
        return false;
    }

    state->initialized = true;
    return true;
}

/**
 * cw_poll — Leer estado y generar evento. Llamar cada ~10ms.
 */
cw_event_t cw_poll(cw_state_t *state) {
    if (!state || !state->initialized) return CW_EVENT_NONE;

    uint32_t now = _get_ms();

    /* ---- ENCODER: leer ángulo actual ---- */
    uint16_t current_angle = 0;
    if (cw_read_raw_angle(&current_angle)) {

        int16_t delta = _angle_delta(current_angle, state->last_angle);
        state->last_angle = current_angle;

        /* Acumular movimiento (filtra ruido dentro del deadzone) */
        if (delta > CW_DEADZONE || delta < -CW_DEADZONE) {
            state->accumulated += delta;
        }

        /* ¿Acumulamos suficiente movimiento para un paso? */
        uint32_t ms_since_scroll = now - state->last_scroll_ms;
        if (ms_since_scroll >= CW_SCROLL_DEBOUNCE_MS) {

            if (state->accumulated >= CW_TICKS_PER_STEP) {
                state->accumulated    = 0;
                state->last_scroll_ms = now;
                return CW_EVENT_SCROLL_UP;

            } else if (state->accumulated <= -CW_TICKS_PER_STEP) {
                state->accumulated    = 0;
                state->last_scroll_ms = now;
                return CW_EVENT_SCROLL_DOWN;
            }
        }
    }

    /* ---- BOTÓN: leer estado GPIO ---- */
    /*
     * PB8 configurado con pull-up interno → LOW cuando presionado.
     * GPIO_PIN_RESET = presionado, GPIO_PIN_SET = suelto.
     */
    bool btn_pressed = (HAL_GPIO_ReadPin(CW_BTN_PORT, CW_BTN_PIN) == GPIO_PIN_RESET);

    /* Flanco de bajada: botón presionado */
    if (btn_pressed && !state->btn_was_pressed) {
        state->btn_press_ms     = now;
        state->long_press_fired = false;
        state->btn_was_pressed  = true;
    }

    /* Mientras está presionado: detectar long press */
    if (btn_pressed && state->btn_was_pressed && !state->long_press_fired) {
        if ((now - state->btn_press_ms) >= CW_LONGPRESS_MS) {
            state->long_press_fired = true;
            return CW_EVENT_LONG_PRESS;
        }
    }

    /* Flanco de subida: botón soltado */
    if (!btn_pressed && state->btn_was_pressed) {
        state->btn_was_pressed = false;

        /* Si ya disparamos long press, ignorar el release */
        if (state->long_press_fired) {
            state->long_press_fired = false;
            return CW_EVENT_NONE;
        }

        /* Debounce mínimo */
        if ((now - state->btn_press_ms) < CW_BTN_DEBOUNCE_MS) {
            return CW_EVENT_NONE;
        }

        /* Detectar double-click */
        uint32_t ms_since_last_click = now - state->last_click_ms;
        state->last_click_ms = now;

        if (ms_since_last_click < 400 && state->click_count > 0) {
            state->click_count = 0;
            return CW_EVENT_DOUBLE_CLICK;
        }

        state->click_count++;
        return CW_EVENT_CLICK;
    }

    return CW_EVENT_NONE;
}

/**
 * cw_is_connected — Prueba si el AS5600 responde en el bus I2C.
 */
bool cw_is_connected(void) {
    HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(
        &hi2c1,
        AS5600_I2C_ADDR,
        3,              /* reintentos */
        CW_I2C_TIMEOUT
    );
    return (status == HAL_OK);
}

/**
 * cw_read_raw_angle — Lee los 12 bits de ángulo del AS5600.
 *
 * Protocolo:
 *   Master → [START][0x36 W][0x0E][RESTART][0x36 R][ANGLE_H][ANGLE_L][STOP]
 */
bool cw_read_raw_angle(uint16_t *angle) {
    if (!angle) return false;

    uint8_t reg = AS5600_REG_ANGLE_H;
    uint8_t buf[2] = {0, 0};

    /* Escribir dirección del registro */
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(
        &hi2c1,
        AS5600_I2C_ADDR,
        &reg,
        1,
        CW_I2C_TIMEOUT
    );
    if (st != HAL_OK) return false;

    /* Leer 2 bytes: ANGLE_H y ANGLE_L */
    st = HAL_I2C_Master_Receive(
        &hi2c1,
        AS5600_I2C_ADDR,
        buf,
        2,
        CW_I2C_TIMEOUT
    );
    if (st != HAL_OK) return false;

    /* Combinar: solo los 4 bits bajos de buf[0] son válidos */
    *angle = ((uint16_t)(buf[0] & 0x0F) << 8) | buf[1];
    return true;
}

/**
 * cw_event_name — String del evento para debug por UART.
 */
const char *cw_event_name(cw_event_t event) {
    switch (event) {
        case CW_EVENT_SCROLL_UP:    return "SCROLL_UP";
        case CW_EVENT_SCROLL_DOWN:  return "SCROLL_DOWN";
        case CW_EVENT_CLICK:        return "CLICK";
        case CW_EVENT_LONG_PRESS:   return "LONG_PRESS";
        case CW_EVENT_DOUBLE_CLICK: return "DOUBLE_CLICK";
        default:                    return "NONE";
    }
}

/* =========================================================
 * FUNCIONES PRIVADAS — inicialización de hardware
 * ========================================================= */

/**
 * _i2c1_gpio_init — Configura PB6 (SCL) y PB7 (SDA) para I2C1.
 *
 * Configuración requerida por STM32F4 para I2C en Fast Mode:
 *   - Modo: Alternate Function (AF4 = I2C1)
 *   - Output type: Open-Drain (OBLIGATORIO para I2C)
 *   - Speed: High (necesario a 400kHz)
 *   - Pull: None (los pull-ups son EXTERNOS 4.7kΩ a 3.3V)
 *
 * ⚠️  NO usar pull-ups internos para I2C — son demasiado débiles
 *      (40kΩ) y causarán errores de señal a 400kHz.
 */
static void _i2c1_gpio_init(void) {
    /* Habilitar clock de GPIOB */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_6 | GPIO_PIN_7;   /* PB6=SCL, PB7=SDA */
    gpio.Mode      = GPIO_MODE_AF_OD;            /* Alternate, Open-Drain */
    gpio.Pull      = GPIO_NOPULL;                /* Pull-ups EXTERNOS */
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;             /* AF4 = I2C1 en STM32F405 */
    HAL_GPIO_Init(GPIOB, &gpio);
}

/**
 * _i2c1_peripheral_init — Inicializa I2C1 a 400kHz (Fast Mode).
 *
 * Valores calculados para PCLK1 = 42MHz (típico con HSE 8MHz en F405):
 *   ClockSpeed = 400000
 *   DutyCycle  = DUTY_2 (Tlow/Thigh = 2:1, requerido >100kHz)
 */
static void _i2c1_peripheral_init(void) {
    __HAL_RCC_I2C1_CLK_ENABLE();

    hi2c1.Instance              = I2C1;
    hi2c1.Init.ClockSpeed       = 400000;          /* 400kHz Fast Mode */
    hi2c1.Init.DutyCycle        = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1      = 0;
    hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2      = 0;
    hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

    HAL_I2C_Init(&hi2c1);
}

/**
 * _btn_gpio_init — Configura PB8 como entrada digital para el botón.
 *
 * Pull-up interno habilitado → el pin lee HIGH en reposo,
 * LOW cuando el botón conecta PB8 a GND.
 *
 * Conexión física:
 *   PB8 ──── [botón] ──── GND
 *   (el pull-up interno mantiene PB8 en HIGH cuando no se presiona)
 */
static void _btn_gpio_init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();  /* ya habilitado, pero seguro */

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = CW_BTN_PIN;          /* PB8 */
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;         /* Pull-up interno ~40kΩ */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CW_BTN_PORT, &gpio);
}

/**
 * _get_ms — Wrapper del tick de HAL (ms desde el boot).
 */
static uint32_t _get_ms(void) {
    return HAL_GetTick();
}

/**
 * _angle_delta — Calcula diferencia de ángulo con manejo de rollover.
 *
 * El AS5600 retorna 0–4095 continuamente. Cuando el eje gira
 * pasando por 0 (ej: 10 → 4090), sin manejo de rollover se
 * calcularía un delta de −4080 en vez de +16.
 *
 * Este cálculo normaliza el delta al rango [−2048, +2047].
 *
 * @param  new_angle  Ángulo recién leído
 * @param  old_angle  Ángulo de la lectura anterior
 * @return            Delta con signo: positivo=horario, negativo=antihorario
 */
static int16_t _angle_delta(uint16_t new_angle, uint16_t old_angle) {
    int16_t delta = (int16_t)new_angle - (int16_t)old_angle;

    /* Normalizar al rango −2048 … +2047 */
    if (delta >  2048) delta -= 4096;
    if (delta < -2048) delta += 4096;

    return delta;
}
