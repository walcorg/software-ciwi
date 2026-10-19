/**
 * @file    touch_xpt2046.c
 * @brief   Driver touch resistivo XPT2046 via SPI1 — WalCorg Fase 2
 *
 * El XPT2046 comparte SPI1 con el ILI9488 pero usa T_CS=PC6
 * y requiere velocidad SPI más baja (max 2MHz vs 10.5MHz del display).
 * Bajamos temporalmente el baudrate antes de cada lectura de touch.
 *
 * NOTA IMPORTANTE sobre compartir SPI:
 *   Nunca tener CS del display Y CS del touch en LOW al mismo tiempo.
 *   La secuencia correcta es:
 *     TFT_CS_HIGH()  → asegurar display inactivo
 *     cambiar SPI baudrate a lento
 *     TOUCH_CS_LOW() → activar touch
 *     leer XPT2046
 *     TOUCH_CS_HIGH() → desactivar touch
 *     cambiar SPI baudrate a rápido
 *
 * @license GNU GPL v3
 */

#include "touch_xpt2046.h"
#include "display_tft.h"
#include "uart_debug.h"
#include <string.h>

/* =========================================================
 * ESTADO INTERNO
 * ========================================================= */
static touch_cal_t  _cal;           /* Calibración activa */
static uint32_t     _last_touch_ms; /* Para debounce */
static bool         _initialized = false;

/* Referencia al handle SPI del display (definido en display_tft.c) */
extern SPI_HandleTypeDef hspi1;


/* =========================================================
 * FUNCIONES PRIVADAS
 * ========================================================= */

/**
 * _spi_slow — Reducir velocidad del SPI1 para el XPT2046
 *
 * El XPT2046 tiene una velocidad máxima de 2MHz.
 * El ILI9488 opera a 10.5MHz con prescaler /8.
 * Cambiamos temporalmente a /64 = 1.3MHz para el touch.
 *
 * Esto funciona porque el SPI1 es compartido pero nunca
 * ambos ICs están activos al mismo tiempo (CS separados).
 */
static void _spi_slow(void) {
    /* Deshabilitar SPI antes de cambiar el baudrate */
    __HAL_SPI_DISABLE(&hspi1);

    /* Cambiar prescaler: bits 5:3 del CR1 = baudrate */
    hspi1.Instance->CR1 &= ~SPI_CR1_BR;                       /* Limpiar bits BR */
    hspi1.Instance->CR1 |= SPI_BAUDRATEPRESCALER_64;          /* /64 = 1.3MHz */

    __HAL_SPI_ENABLE(&hspi1);
}

/**
 * _spi_fast — Restaurar velocidad del SPI1 para el ILI9488
 */
static void _spi_fast(void) {
    __HAL_SPI_DISABLE(&hspi1);
    hspi1.Instance->CR1 &= ~SPI_CR1_BR;
    hspi1.Instance->CR1 |= SPI_BAUDRATEPRESCALER_8;           /* /8 = 10.5MHz */
    __HAL_SPI_ENABLE(&hspi1);
}

/**
 * _read_channel — Leer un canal del ADC del XPT2046
 *
 * Protocolo: enviar 1 byte de comando, recibir 2 bytes de respuesta.
 * Los 12 bits de datos están en bits 14:3 de la respuesta de 16 bits.
 *
 * @param cmd  Comando de lectura (XPT2046_CMD_READ_X, etc.)
 * @return     Valor ADC de 12 bits (0-4095)
 */
static uint16_t _read_channel(uint8_t cmd) {
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = { 0x00, 0x00, 0x00 };

    /* Transacción SPI: 3 bytes (1 cmd + 2 respuesta) */
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 3, 10);

    /* Reconstruir el valor de 12 bits:
     * rx[1] contiene los bits 11:5 en sus bits 6:0
     * rx[2] contiene los bits 4:0 en sus bits 7:3
     * Formato: rx[1]=XXXX XXX0, rx[2]=YYYY Y000 */
    uint16_t value = ((uint16_t)(rx[1] & 0x7F) << 5) |
                     ((uint16_t)(rx[2] >> 3));

    return value & 0x0FFF;  /* Mask a 12 bits */
}

/**
 * _read_channel_avg — Leer canal con promedio de N muestras
 *
 * Toma TOUCH_SAMPLES lecturas y retorna el promedio.
 * Descarta el valor más alto y más bajo para mayor robustez.
 */
static uint16_t _read_channel_avg(uint8_t cmd) {
    uint32_t sum = 0;
    uint16_t min_val = 0xFFFF;
    uint16_t max_val = 0x0000;
    uint16_t val;

    for (int i = 0; i < TOUCH_SAMPLES; i++) {
        val = _read_channel(cmd);
        sum += val;
        if (val < min_val) min_val = val;
        if (val > max_val) max_val = val;
    }

    /* Descartar min y max, promediar el resto */
    sum -= min_val;
    sum -= max_val;
    return (uint16_t)(sum / (TOUCH_SAMPLES - 2));
}

/**
 * _raw_to_screen — Convertir coordenadas raw a píxeles de pantalla
 *
 * Aplica la transformación lineal de calibración:
 *   pixel = (raw - min) * screen_size / (max - min)
 * Con clipping a los límites de la pantalla.
 */
static int16_t _raw_to_screen(uint16_t raw, uint16_t raw_min,
                                uint16_t raw_max, uint16_t screen_size,
                                bool invert) {
    /* Proteger contra división por cero */
    if (raw_max <= raw_min) return 0;

    /* Clipping del valor raw a los límites de calibración */
    if (raw < raw_min) raw = raw_min;
    if (raw > raw_max) raw = raw_max;

    /* Transformación lineal */
    int32_t pixel = ((int32_t)(raw - raw_min) *
                     (int32_t)screen_size) /
                    (int32_t)(raw_max - raw_min);

    /* Invertir si el panel está montado al revés */
    if (invert) {
        pixel = (int32_t)screen_size - 1 - pixel;
    }

    /* Clipping al tamaño de pantalla */
    if (pixel < 0) pixel = 0;
    if (pixel >= (int32_t)screen_size) pixel = (int32_t)screen_size - 1;

    return (int16_t)pixel;
}


/* =========================================================
 * API PÚBLICA
 * ========================================================= */

bool touch_init(void) {
    DEBUG_PRINT("[TOUCH] Inicializando XPT2046...\r\n");

    /* Configurar PC6 (T_CS) como salida */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = TOUCH_CS_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(TOUCH_CS_PORT, &gpio);
    TOUCH_CS_HIGH();  /* Touch inactivo por defecto */

    /* Configurar PB10 (T_IRQ) como entrada con pull-up
     * T_IRQ = LOW cuando hay toque, HIGH cuando no hay */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin  = TOUCH_IRQ_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;  /* Pull-up: sin toque → HIGH */
    HAL_GPIO_Init(TOUCH_IRQ_PORT, &gpio);

    /* Cargar calibración por defecto */
    _cal.x_min     = TOUCH_CAL_X_MIN;
    _cal.x_max     = TOUCH_CAL_X_MAX;
    _cal.y_min     = TOUCH_CAL_Y_MIN;
    _cal.y_max     = TOUCH_CAL_Y_MAX;
    _cal.invert_x  = TOUCH_INVERT_X;
    _cal.invert_y  = TOUCH_INVERT_Y;
    _cal.swap_xy   = TOUCH_SWAP_XY;
    _cal.calibrated = false;  /* Calibración por defecto, no verificada */

    _last_touch_ms = 0;
    _initialized   = true;

    DEBUG_PRINT("[TOUCH] XPT2046 inicializado.\r\n");
    DEBUG_PRINT("[TOUCH]   T_CS=PC6, T_IRQ=PB10\r\n");
    DEBUG_PRINT("[TOUCH]   Cal: usar touch_set_calibration() para calibrar\r\n");
    return true;
}

bool touch_is_pressed(void) {
    if (!_initialized) return false;
    /* T_IRQ = LOW cuando hay toque activo */
    return (HAL_GPIO_ReadPin(TOUCH_IRQ_PORT, TOUCH_IRQ_PIN) == GPIO_PIN_RESET);
}

bool touch_read(touch_point_t *point) {
    if (!point || !_initialized) return false;

    point->valid    = false;
    point->x        = 0;
    point->y        = 0;
    point->pressure = 0;

    /* Verificar si hay toque activo via IRQ */
    if (!touch_is_pressed()) return false;

    /* Debounce: ignorar toques demasiado rápidos */
    uint32_t now = HAL_GetTick();
    if ((now - _last_touch_ms) < TOUCH_DEBOUNCE_MS) return false;

    /* Asegurar que el display no está en medio de una transacción */
    TFT_CS_HIGH();

    /* Cambiar SPI a velocidad lenta para el XPT2046 */
    _spi_slow();
    TOUCH_CS_LOW();

    /* Leer coordenadas raw con promedio */
    uint16_t raw_x  = _read_channel_avg(XPT2046_CMD_READ_X);
    uint16_t raw_y  = _read_channel_avg(XPT2046_CMD_READ_Y);
    uint16_t raw_z1 = _read_channel(XPT2046_CMD_READ_Z1);

    TOUCH_CS_HIGH();
    _spi_fast();  /* Restaurar velocidad para el display */

    /* Verificar presión mínima */
    if (raw_z1 < TOUCH_PRESSURE_MIN) return false;

    /* Verificar que el IRQ sigue bajo (toque sostenido) */
    if (!touch_is_pressed()) return false;

    /* Aplicar swap de ejes si es necesario */
    uint16_t coord_x = _cal.swap_xy ? raw_y : raw_x;
    uint16_t coord_y = _cal.swap_xy ? raw_x : raw_y;

    /* Convertir raw a coordenadas de pantalla */
    uint16_t screen_w = 480;  /* Landscape */
    uint16_t screen_h = 320;

    point->x = _raw_to_screen(coord_x, _cal.x_min, _cal.x_max,
                               screen_w, _cal.invert_x);
    point->y = _raw_to_screen(coord_y, _cal.y_min, _cal.y_max,
                               screen_h, _cal.invert_y);
    point->pressure = raw_z1;
    point->valid    = true;

    _last_touch_ms = now;
    return true;
}

bool touch_read_raw(touch_raw_t *raw) {
    if (!raw || !_initialized) return false;

    TFT_CS_HIGH();
    _spi_slow();
    TOUCH_CS_LOW();

    raw->raw_x  = _read_channel_avg(XPT2046_CMD_READ_X);
    raw->raw_y  = _read_channel_avg(XPT2046_CMD_READ_Y);
    raw->raw_z1 = _read_channel(XPT2046_CMD_READ_Z1);
    raw->raw_z2 = _read_channel(XPT2046_CMD_READ_Z2);

    TOUCH_CS_HIGH();
    _spi_fast();

    return true;
}

void touch_set_calibration(const touch_cal_t *cal) {
    if (!cal) return;
    memcpy(&_cal, cal, sizeof(touch_cal_t));
    _cal.calibrated = true;
    DEBUG_PRINT("[TOUCH] Calibracion actualizada.\r\n");
}

void touch_get_calibration(touch_cal_t *cal) {
    if (!cal) return;
    memcpy(cal, &_cal, sizeof(touch_cal_t));
}

void touch_wait_release(uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();
    while (touch_is_pressed()) {
        if ((HAL_GetTick() - start) > timeout_ms) break;
        HAL_Delay(10);
    }
    HAL_Delay(TOUCH_DEBOUNCE_MS);
}

void touch_print_status(void) {
#if (PRODUCTION == 0)
    DEBUG_PRINT("\r\n[TOUCH] === Estado XPT2046 ===\r\n");
    DEBUG_PRINTF("[TOUCH] Inicializado : %s\r\n", _initialized ? "SI" : "NO");
    DEBUG_PRINTF("[TOUCH] Tocando ahora: %s\r\n",
        touch_is_pressed() ? "SI" : "no");
    DEBUG_PRINTF("[TOUCH] Cal X        : %u - %u\r\n", _cal.x_min, _cal.x_max);
    DEBUG_PRINTF("[TOUCH] Cal Y        : %u - %u\r\n", _cal.y_min, _cal.y_max);
    DEBUG_PRINTF("[TOUCH] Swap XY      : %s\r\n", _cal.swap_xy ? "SI" : "no");
    DEBUG_PRINTF("[TOUCH] Calibrado    : %s\r\n",
        _cal.calibrated ? "SI" : "valores por defecto");
    DEBUG_PRINT("[TOUCH] ========================\r\n\r\n");
#endif
}
