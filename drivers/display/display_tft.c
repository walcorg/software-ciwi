/**
 * @file    display_tft.c
 * @brief   Driver TFT ILI9488 via SPI1 — WALCORG Fase 2
 *
 * Implementación del driver de display. Organizado en capas:
 *
 *   Capa alta  : primitivas gráficas (fillRect, drawLine, etc.)
 *       ↓
 *   Capa media : ventana de escritura (set_window, write_pixel)
 *       ↓
 *   Capa baja  : SPI raw (spi_write_byte, spi_write_buf)
 *       ↓
 *   Hardware   : SPI1 STM32F405 + pines GPIO
 *
 * @license GNU GPL v3
 */

#include "display_tft.h"
#include "font.h"
#include "uart_debug.h"
#include <string.h>
#include <stdlib.h>   /* abs() */

/* =========================================================
 * HANDLES Y ESTADO GLOBAL
 * ========================================================= */

SPI_HandleTypeDef hspi1;        /* Handle del periférico SPI1 */
tft_state_t       g_tft = {0}; /* Estado global del driver */


/* =========================================================
 * FUNCIONES PRIVADAS — CAPA BAJA SPI
 * ========================================================= */

/**
 * _spi_write_byte — Enviar 1 byte por SPI (polling, sin DMA)
 *
 * En Fase 2 usamos polling puro para simplicidad.
 * En Fase 4 (UI personalizada) se puede añadir DMA para
 * acelerar la transferencia de imágenes completas.
 *
 * CS debe estar en LOW antes de llamar esta función.
 */
static inline void _spi_write_byte(uint8_t byte) {
    /* HAL_SPI_Transmit espera hasta que el byte se envía completamente.
     * Timeout de 10ms — si tarda más, hay un problema de hardware. */
    HAL_SPI_Transmit(&hspi1, &byte, 1, 10);
}

/**
 * _spi_write_buf — Enviar buffer de bytes por SPI
 *
 * Para transferencias largas (ej: un rectángulo de color),
 * enviar el buffer completo es más eficiente que byte a byte
 * porque reduce el overhead de llamadas a función.
 */
static inline void _spi_write_buf(const uint8_t *buf, uint32_t len) {
    /* HAL_SPI_Transmit maneja internamente la transmisión en chunks
     * si len > 65535 (límite del DMA de 16 bits).
     * Para Fase 2 con polling, len puede ser cualquier valor. */
    HAL_SPI_Transmit(&hspi1, (uint8_t *)buf, (uint16_t)len, len / 10 + 100);
}

/**
 * _spi_write_color — Enviar un color RGB565 (2 bytes) por SPI
 *
 * El ILI9488 en modo RGB565 espera los bytes en orden:
 *   Byte 1: bits 15:8 del color (RRRRRGGG)
 *   Byte 2: bits  7:0 del color (GGGBBBBB)
 * Es decir, big-endian (MSB primero).
 */
static inline void _spi_write_color(tft_color_t color) {
    uint8_t buf[2] = {
        (uint8_t)(color >> 8),    /* Byte alto: RRRRRGGG */
        (uint8_t)(color & 0xFF)   /* Byte bajo: GGGBBBBB */
    };
    HAL_SPI_Transmit(&hspi1, buf, 2, 10);
}

/**
 * _write_cmd — Enviar un comando al ILI9488
 *
 * Protocolo: DC=LOW, CS=LOW, enviar byte, CS=HIGH
 * DC=LOW indica al IC que el byte es un comando (no datos).
 */
static void _write_cmd(uint8_t cmd) {
    TFT_DC_CMD();       /* DC = LOW = comando */
    TFT_CS_LOW();       /* Seleccionar el display */
    _spi_write_byte(cmd);
    TFT_CS_HIGH();      /* Liberar el display */
}

/**
 * _write_data — Enviar un byte de dato/parámetro al ILI9488
 *
 * Protocolo: DC=HIGH, CS=LOW, enviar byte, CS=HIGH
 * DC=HIGH indica al IC que el byte es un parámetro o dato de píxel.
 */
static void _write_data(uint8_t data) {
    TFT_DC_DAT();       /* DC = HIGH = dato */
    TFT_CS_LOW();
    _spi_write_byte(data);
    TFT_CS_HIGH();
}

/**
 * _write_cmd_data — Enviar comando seguido de N bytes de parámetros
 *
 * Patrón muy común: comando + sus parámetros en una transacción.
 * Mantener CS=LOW durante toda la transacción es más eficiente
 * que alternar CS entre comando y parámetros.
 */
static void _write_cmd_data(uint8_t cmd, const uint8_t *data, uint8_t len) {
    /* Fase de comando: DC=LOW */
    TFT_DC_CMD();
    TFT_CS_LOW();
    _spi_write_byte(cmd);

    /* Fase de datos: DC=HIGH (sin soltar CS entre medio) */
    if (data && len > 0) {
        TFT_DC_DAT();
        _spi_write_buf(data, len);
    }

    TFT_CS_HIGH();
}


/* =========================================================
 * FUNCIONES PRIVADAS — INICIALIZACIÓN DE HARDWARE
 * ========================================================= */

/**
 * _gpio_init — Configurar todos los pines GPIO del display
 *
 * Pines de control:
 *   PC4 (CS)  → Salida push-pull, HIGH por defecto (display inactivo)
 *   PC5 (DC)  → Salida push-pull, HIGH por defecto (modo dato)
 *   PB0 (RST) → Salida push-pull, HIGH por defecto (sin reset)
 *   PB1 (BL)  → Salida push-pull, LOW por defecto (backlight apagado)
 *
 * Pines SPI (configurados en _spi_init):
 *   PA5 (SCK), PA6 (MISO), PA7 (MOSI) → Alternate Function 5
 */
static void _gpio_init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};

    /* PC4 (CS) y PC5 (DC) — control en GPIOC */
    gpio.Pin   = TFT_CS_PIN | TFT_DC_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;   /* Push-pull: puede dar HIGH y LOW */
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;  /* Alta velocidad — señales SPI */
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PB0 (RST) y PB1 (BL) — control en GPIOB */
    gpio.Pin   = TFT_RST_PIN | TFT_BL_PIN;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* Estado inicial seguro */
    TFT_CS_HIGH();   /* Display inactivo */
    TFT_DC_DAT();    /* Modo dato (el más común) */
    TFT_RST_HIGH();  /* Sin reset */
    TFT_BL_OFF();    /* Backlight apagado hasta que esté inicializado */
}

/**
 * _spi_init — Configurar SPI1 para el display ILI9488
 *
 * PA5 → SPI1_SCK  (Alternate Function 5)
 * PA6 → SPI1_MISO (Alternate Function 5)
 * PA7 → SPI1_MOSI (Alternate Function 5)
 *
 * Configuración SPI:
 *   Modo: Master (el STM32 genera el clock)
 *   CPOL=0, CPHA=0: modo SPI 0 (clock idle LOW, captura en subida)
 *   MSB First: bit más significativo primero
 *   8 bits por trama
 *   Velocidad: PCLK2(84MHz) / 8 = 10.5MHz
 *
 * ¿Por qué /8 en vez de /4 (21MHz)?
 *   En protoboard, los cables son antenas. A 21MHz pueden causar
 *   reflexiones y errores. En el PCB final con trazas cortas
 *   podemos usar /4 sin problemas.
 */
static void _spi_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    /* Configurar PA5 (SCK), PA6 (MISO), PA7 (MOSI) como AF5 (SPI1) */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode      = GPIO_MODE_AF_PP;      /* Alternate Function Push-Pull */
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;        /* AF5 = SPI1 en STM32F405 */
    HAL_GPIO_Init(GPIOA, &gpio);

    /* Configurar el periférico SPI1 */
    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;  /* Full duplex */
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;      /* CPOL=0 */
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;       /* CPHA=0 */
    hspi1.Init.NSS               = SPI_NSS_SOFT;          /* CS manual via GPIO */
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8; /* 84/8 = 10.5MHz */
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;      /* MSB primero */
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial     = 7;

    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        DEBUG_PRINT("[TFT] ERROR: HAL_SPI_Init falló.\r\n");
        DEBUG_PRINT("[TFT] Verificar pines PA5/PA6/PA7 y clock PCLK2.\r\n");
    }
}

/**
 * _hardware_reset — Reset físico del ILI9488
 *
 * Secuencia: RST=HIGH → RST=LOW (10ms) → RST=HIGH → esperar 120ms
 *
 * El reset por hardware es más confiable que el reset por software
 * (comando 0x01). Si el IC está en un estado inconsistente,
 * el reset por hardware lo recupera.
 */
static void _hardware_reset(void) {
    TFT_RST_HIGH();
    HAL_Delay(5);
    TFT_RST_LOW();    /* Activar reset */
    HAL_Delay(10);    /* Mantener 10ms — requerido por datasheet */
    TFT_RST_HIGH();   /* Liberar reset */
    HAL_Delay(120);   /* Esperar que el IC complete su secuencia de arranque */
}

/**
 * _apply_rotation — Aplicar la orientación al ILI9488
 *
 * El MADCTL controla cómo el IC mapea las coordenadas de software
 * a las posiciones físicas de los píxeles del panel.
 */
static void _apply_rotation(tft_rotation_t rotation) {
    uint8_t madctl = ILI9488_MADCTL_BGR;  /* El panel es BGR — siempre */

    switch (rotation) {
        case TFT_ROTATION_0:   /* Portrait normal */
            /* Sin intercambio de ejes — coordenadas nativas del panel */
            g_tft.width  = ILI9488_WIDTH_PORTRAIT;
            g_tft.height = ILI9488_HEIGHT_PORTRAIT;
            break;

        case TFT_ROTATION_90:  /* Landscape normal — DEFAULT */
            /* MX+MV: intercambiar X/Y y reflejar X → landscape */
            madctl |= ILI9488_MADCTL_MX | ILI9488_MADCTL_MV;
            g_tft.width  = ILI9488_WIDTH_LANDSCAPE;
            g_tft.height = ILI9488_HEIGHT_LANDSCAPE;
            break;

        case TFT_ROTATION_180: /* Portrait invertido */
            madctl |= ILI9488_MADCTL_MX | ILI9488_MADCTL_MY;
            g_tft.width  = ILI9488_WIDTH_PORTRAIT;
            g_tft.height = ILI9488_HEIGHT_PORTRAIT;
            break;

        case TFT_ROTATION_270: /* Landscape invertido */
            madctl |= ILI9488_MADCTL_MY | ILI9488_MADCTL_MV;
            g_tft.width  = ILI9488_WIDTH_LANDSCAPE;
            g_tft.height = ILI9488_HEIGHT_LANDSCAPE;
            break;
    }

    g_tft.rotation = rotation;
    _write_cmd_data(ILI9488_CMD_MADCTL, &madctl, 1);
}

/**
 * _run_init_sequence — Ejecutar la secuencia de comandos de init
 *
 * Itera sobre ILI9488_INIT_SEQ y envía cada comando con sus parámetros.
 * Las entradas con cmd=0xFF son delays en ms.
 */
static void _run_init_sequence(void) {
    const ili9488_init_cmd_t *seq = ILI9488_INIT_SEQ;

    while (seq->cmd != 0x00 || seq->num_args != 0x00) {
        if (seq->cmd == 0xFF) {
            /* Entrada de delay: num_args = milisegundos a esperar */
            HAL_Delay(seq->num_args);
        } else {
            /* Entrada de comando: enviar cmd + parámetros */
            _write_cmd_data(seq->cmd, seq->args, seq->num_args);
        }
        seq++;
    }
}


/* =========================================================
 * API PÚBLICA — INICIALIZACIÓN
 * ========================================================= */

bool tft_init(tft_rotation_t rotation) {
    DEBUG_PRINT("[TFT] Inicializando display ILI9488 3.5\" 480x320...\r\n");

    /* 1. Configurar GPIO de control */
    _gpio_init();
    DEBUG_PRINT("[TFT] GPIO configurado (CS=PC4, DC=PC5, RST=PB0, BL=PB1)\r\n");

    /* 2. Configurar SPI1 */
    _spi_init();
    DEBUG_PRINT("[TFT] SPI1 configurado (10.5MHz, CPOL=0, CPHA=0)\r\n");
    DEBUG_PRINT("[TFT]   SCK=PA5, MISO=PA6, MOSI=PA7\r\n");

    /* 3. Reset hardware */
    DEBUG_PRINT("[TFT] Aplicando reset hardware...\r\n");
    _hardware_reset();

    /* 4. Enviar secuencia de inicialización */
    DEBUG_PRINT("[TFT] Enviando secuencia de init al ILI9488...\r\n");
    _run_init_sequence();

    /* 5. Aplicar orientación */
    _apply_rotation(rotation);
    DEBUG_PRINTF("[TFT] Orientación: %ux%u px\r\n", g_tft.width, g_tft.height);

    /* 6. Limpiar pantalla a negro */
    DEBUG_PRINT("[TFT] Limpiando pantalla...\r\n");
    tft_clear(ILI9488_BLACK);

    /* 7. Encender backlight */
    TFT_BL_ON();

    g_tft.initialized = true;
    DEBUG_PRINT("[TFT] Display inicializado correctamente.\r\n");
    return true;
}

void tft_set_rotation(tft_rotation_t rotation) {
    _apply_rotation(rotation);
}

void tft_set_brightness(uint8_t brightness) {
    /* Fase 2: solo ON/OFF sin PWM */
    if (brightness > 0) {
        TFT_BL_ON();
    } else {
        TFT_BL_OFF();
    }
    /* En Fase 4 se añadirá PWM via TIM3_CH4 para control fino */
}

uint16_t tft_get_width(void)  { return g_tft.width; }
uint16_t tft_get_height(void) { return g_tft.height; }


/* =========================================================
 * VENTANA DE ESCRITURA — CAPA MEDIA
 * ========================================================= */

/**
 * _set_addr_window — Configurar la ventana de escritura en el IC
 *
 * Envía CASET (columna inicio/fin) y PASET (fila inicio/fin)
 * seguido de RAMWR para comenzar la transferencia de píxeles.
 *
 * Después de esto, el IC está listo para recibir exactamente
 * (x2-x1+1) × (y2-y1+1) píxeles en la siguiente transacción SPI.
 */
static void _set_addr_window(uint16_t x1, uint16_t y1,
                              uint16_t x2, uint16_t y2) {
    /* CASET — Column Address Set (rango horizontal) */
    uint8_t caset[4] = {
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF),  /* Start column */
        (uint8_t)(x2 >> 8), (uint8_t)(x2 & 0xFF)   /* End column   */
    };
    _write_cmd_data(ILI9488_CMD_CASET, caset, 4);

    /* PASET — Page Address Set (rango vertical) */
    uint8_t paset[4] = {
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF),  /* Start page */
        (uint8_t)(y2 >> 8), (uint8_t)(y2 & 0xFF)   /* End page   */
    };
    _write_cmd_data(ILI9488_CMD_PASET, paset, 4);

    /* RAMWR — Memory Write: el siguiente SPI data son píxeles */
    _write_cmd(ILI9488_CMD_RAMWR);

    /* Después de RAMWR, DC debe quedar en HIGH (modo dato)
     * y CS debe quedar en LOW para que el usuario siga enviando píxeles */
    TFT_DC_DAT();
    TFT_CS_LOW();
}

void tft_set_window(tft_coord_t x, tft_coord_t y,
                    tft_coord_t w, tft_coord_t h) {
    _set_addr_window((uint16_t)x, (uint16_t)y,
                     (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
}

void tft_write_pixel(tft_color_t color) {
    _spi_write_color(color);
}

void tft_write_pixels(tft_color_t color, uint32_t count) {
    /* Para N píxeles del mismo color, generar el buffer una vez
     * y enviarlo. Usar un buffer de 64 píxeles para balance
     * entre uso de RAM y eficiencia de transferencia. */
    uint8_t buf[128];  /* 64 píxeles × 2 bytes = 128 bytes */
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);

    /* Llenar el buffer con el color */
    for (int i = 0; i < 128; i += 2) {
        buf[i]     = hi;
        buf[i + 1] = lo;
    }

    /* Enviar en chunks de 64 píxeles */
    while (count >= 64) {
        _spi_write_buf(buf, 128);
        count -= 64;
    }
    /* Píxeles restantes */
    while (count > 0) {
        _spi_write_color(color);
        count--;
    }
}

void tft_write_buffer(const uint16_t *pixels, uint32_t count) {
    /* Los píxeles en el buffer están en formato host (little-endian).
     * El ILI9488 espera big-endian.
     * Debemos invertir los bytes de cada píxel antes de enviar. */
    for (uint32_t i = 0; i < count; i++) {
        _spi_write_color(pixels[i]);
    }
}

void tft_end_write(void) {
    TFT_CS_HIGH();   /* Liberar el bus SPI */
}


/* =========================================================
 * PRIMITIVAS GRÁFICAS
 * ========================================================= */

void tft_draw_pixel(tft_coord_t x, tft_coord_t y, tft_color_t color) {
    /* Verificar límites */
    if (x < 0 || x >= (tft_coord_t)g_tft.width)  return;
    if (y < 0 || y >= (tft_coord_t)g_tft.height) return;

    _set_addr_window((uint16_t)x, (uint16_t)y,
                     (uint16_t)x, (uint16_t)y);
    _spi_write_color(color);
    TFT_CS_HIGH();
}

void tft_fill_rect(tft_coord_t x, tft_coord_t y,
                   tft_coord_t w, tft_coord_t h,
                   tft_color_t color) {
    /* Clipping — asegurar que el rectángulo no sale de la pantalla */
    if (x >= (tft_coord_t)g_tft.width  || y >= (tft_coord_t)g_tft.height) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if ((x + w) > (tft_coord_t)g_tft.width)  w = g_tft.width  - x;
    if ((y + h) > (tft_coord_t)g_tft.height) h = g_tft.height - y;
    if (w <= 0 || h <= 0) return;

    /* Configurar ventana y escribir w×h píxeles del mismo color */
    _set_addr_window((uint16_t)x, (uint16_t)y,
                     (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    tft_write_pixels(color, (uint32_t)w * h);
    TFT_CS_HIGH();
}

void tft_clear(tft_color_t color) {
    tft_fill_rect(0, 0, (tft_coord_t)g_tft.width,
                  (tft_coord_t)g_tft.height, color);
}

void tft_draw_hline(tft_coord_t x, tft_coord_t y,
                    tft_coord_t len, tft_color_t color) {
    tft_fill_rect(x, y, len, 1, color);
}

void tft_draw_vline(tft_coord_t x, tft_coord_t y,
                    tft_coord_t len, tft_color_t color) {
    tft_fill_rect(x, y, 1, len, color);
}

void tft_draw_rect(tft_coord_t x, tft_coord_t y,
                   tft_coord_t w, tft_coord_t h,
                   tft_color_t color) {
    tft_draw_hline(x,         y,         w, color);  /* Superior */
    tft_draw_hline(x,         y + h - 1, w, color);  /* Inferior */
    tft_draw_vline(x,         y,         h, color);  /* Izquierda */
    tft_draw_vline(x + w - 1, y,         h, color);  /* Derecha */
}

void tft_draw_line(tft_coord_t x0, tft_coord_t y0,
                   tft_coord_t x1, tft_coord_t y1,
                   tft_color_t color) {
    /* Algoritmo de Bresenham — dibuja línea entre dos puntos
     * sin usar punto flotante */
    int16_t dx  =  abs(x1 - x0);
    int16_t dy  = -abs(y1 - y0);
    int16_t sx  = (x0 < x1) ? 1 : -1;
    int16_t sy  = (y0 < y1) ? 1 : -1;
    int16_t err = dx + dy;

    while (1) {
        tft_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Función auxiliar para esquinas redondeadas (cuadrantes del círculo) */
static void _draw_circle_helper(tft_coord_t cx, tft_coord_t cy,
                                 tft_coord_t r, uint8_t corner,
                                 tft_color_t color) {
    int16_t f     = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x     = 0;
    int16_t y     = r;

    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        if (corner & 0x4) {  /* Cuadrante inferior derecho */
            tft_draw_pixel(cx + x, cy + y, color);
            tft_draw_pixel(cx + y, cy + x, color);
        }
        if (corner & 0x2) {  /* Cuadrante superior derecho */
            tft_draw_pixel(cx + x, cy - y, color);
            tft_draw_pixel(cx + y, cy - x, color);
        }
        if (corner & 0x8) {  /* Cuadrante inferior izquierdo */
            tft_draw_pixel(cx - y, cy + x, color);
            tft_draw_pixel(cx - x, cy + y, color);
        }
        if (corner & 0x1) {  /* Cuadrante superior izquierdo */
            tft_draw_pixel(cx - y, cy - x, color);
            tft_draw_pixel(cx - x, cy - y, color);
        }
    }
}

/* Función auxiliar para relleno de esquinas redondeadas */
static void _fill_circle_helper(tft_coord_t cx, tft_coord_t cy,
                                  tft_coord_t r, uint8_t corner,
                                  tft_coord_t delta, tft_color_t color) {
    int16_t f     = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x     = 0;
    int16_t y     = r;

    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        if (corner & 0x1) {
            tft_draw_vline(cx + x, cy - y, 2 * y + 1 + delta, color);
            tft_draw_vline(cx + y, cy - x, 2 * x + 1 + delta, color);
        }
        if (corner & 0x2) {
            tft_draw_vline(cx - x, cy - y, 2 * y + 1 + delta, color);
            tft_draw_vline(cx - y, cy - x, 2 * x + 1 + delta, color);
        }
    }
}

void tft_draw_round_rect(tft_coord_t x, tft_coord_t y,
                          tft_coord_t w, tft_coord_t h,
                          tft_coord_t r, tft_color_t color) {
    /* Líneas rectas de los lados */
    tft_draw_hline(x + r,     y,         w - 2 * r, color);  /* Top */
    tft_draw_hline(x + r,     y + h - 1, w - 2 * r, color);  /* Bottom */
    tft_draw_vline(x,         y + r,     h - 2 * r, color);  /* Left */
    tft_draw_vline(x + w - 1, y + r,     h - 2 * r, color);  /* Right */
    /* Esquinas */
    _draw_circle_helper(x + r,         y + r,         r, 0x1, color);
    _draw_circle_helper(x + w - r - 1, y + r,         r, 0x2, color);
    _draw_circle_helper(x + w - r - 1, y + h - r - 1, r, 0x4, color);
    _draw_circle_helper(x + r,         y + h - r - 1, r, 0x8, color);
}

void tft_fill_round_rect(tft_coord_t x, tft_coord_t y,
                          tft_coord_t w, tft_coord_t h,
                          tft_coord_t r, tft_color_t color) {
    tft_fill_rect(x + r, y, w - 2 * r, h, color);
    _fill_circle_helper(x + w - r - 1, y + r, r, 0x1, h - 2 * r - 1, color);
    _fill_circle_helper(x + r,         y + r, r, 0x2, h - 2 * r - 1, color);
}

void tft_draw_circle(tft_coord_t cx, tft_coord_t cy,
                     tft_coord_t r, tft_color_t color) {
    _draw_circle_helper(cx, cy, r, 0xF, color);
    tft_draw_pixel(cx, cy + r, color);
    tft_draw_pixel(cx, cy - r, color);
    tft_draw_pixel(cx + r, cy, color);
    tft_draw_pixel(cx - r, cy, color);
}

void tft_fill_circle(tft_coord_t cx, tft_coord_t cy,
                     tft_coord_t r, tft_color_t color) {
    tft_draw_vline(cx, cy - r, 2 * r + 1, color);
    _fill_circle_helper(cx, cy, r, 0x3, 0, color);
}

void tft_draw_bitmap(tft_coord_t x, tft_coord_t y,
                     tft_coord_t w, tft_coord_t h,
                     const uint16_t *bitmap) {
    if (!bitmap) return;
    tft_set_window(x, y, w, h);
    tft_write_buffer(bitmap, (uint32_t)w * h);
    tft_end_write();
}

void tft_draw_bitmap_1bit(tft_coord_t x, tft_coord_t y,
                           tft_coord_t w, tft_coord_t h,
                           const uint8_t *bitmap,
                           tft_color_t fg, tft_color_t bg) {
    tft_set_window(x, y, w, h);
    for (tft_coord_t row = 0; row < h; row++) {
        for (tft_coord_t col = 0; col < w; col++) {
            uint8_t byte = bitmap[(row * ((w + 7) / 8)) + (col / 8)];
            uint8_t bit  = 7 - (col % 8);
            tft_write_pixel((byte >> bit) & 1 ? fg : bg);
        }
    }
    tft_end_write();
}


/* =========================================================
 * TEXTO
 * ========================================================= */

void tft_draw_char(tft_coord_t x, tft_coord_t y,
                   char c, tft_color_t fg, tft_color_t bg,
                   uint8_t size) {
    /* Delegar al módulo font.c */
    font_draw_char(x, y, c, fg, bg, size);
}

void tft_draw_string(tft_coord_t x, tft_coord_t y,
                     const char *str,
                     tft_color_t fg, tft_color_t bg,
                     uint8_t size) {
    if (!str) return;
    tft_coord_t cx = x;
    /* Ancho de un caracter según el tamaño */
    uint8_t char_w = 6 * size;   /* Fuente 5×7 + 1 separación = 6 píxeles */
    uint8_t char_h = 8 * size;   /* Fuente 7 filas + 1 separación = 8 píxeles */
    (void)char_h;

    while (*str) {
        if (*str == '\n') {
            /* Salto de línea */
            y  += char_h;
            cx  = x;
        } else if (*str == '\r') {
            cx = x;  /* Retorno de carro */
        } else {
            tft_draw_char(cx, y, *str, fg, bg, size);
            cx += char_w;
            /* Salto de línea automático si se sale del borde */
            if (cx > ((tft_coord_t)g_tft.width - char_w)) {
                cx  = x;
                y  += char_h;
            }
        }
        str++;
    }
}

void tft_draw_string_centered(tft_coord_t y,
                               const char *str,
                               tft_color_t fg, tft_color_t bg,
                               uint8_t size) {
    uint16_t w = tft_string_width(str, size);
    tft_coord_t x = ((tft_coord_t)g_tft.width - (tft_coord_t)w) / 2;
    if (x < 0) x = 0;
    tft_draw_string(x, y, str, fg, bg, size);
}

uint16_t tft_string_width(const char *str, uint8_t size) {
    if (!str) return 0;
    uint16_t len = 0;
    while (*str++) len++;
    return len * 6 * size;  /* 6 píxeles por caracter × tamaño */
}


/* =========================================================
 * UTILIDADES Y DEBUG
 * ========================================================= */

bool tft_verify_id(void) {
    /* Leer el ID del display (comando RDDID = 0x04)
     * El ILI9488 responde con 3 bytes: 0x00, 0x94, 0x88 */
    uint8_t id[4] = {0};

    TFT_DC_CMD();
    TFT_CS_LOW();
    _spi_write_byte(ILI9488_CMD_RDDID);
    TFT_DC_DAT();
    HAL_SPI_Receive(&hspi1, id, 4, 100);
    TFT_CS_HIGH();

    /* id[0] = dummy byte, id[1..3] = ID del fabricante */
    DEBUG_PRINTF("[TFT] ID: 0x%02X 0x%02X 0x%02X (esperado: 0x00 0x94 0x88)\r\n",
        id[1], id[2], id[3]);

    /* El ILI9488 debería retornar 0x94 0x88 en los bytes 2 y 3 */
    return (id[2] == 0x94 && id[3] == 0x88);
}

void tft_print_status(void) {
#if (PRODUCTION == 0)
    DEBUG_PRINT("\r\n[TFT] === Estado del Display ===\r\n");
    DEBUG_PRINTF("[TFT] Inicializado : %s\r\n",
        g_tft.initialized ? "SI" : "NO");
    DEBUG_PRINTF("[TFT] Resolución   : %ux%u px\r\n",
        g_tft.width, g_tft.height);
    DEBUG_PRINTF("[TFT] Orientación  : %u\r\n", g_tft.rotation);
    DEBUG_PRINTF("[TFT] Errores SPI  : %lu\r\n", g_tft.spi_errors);
    DEBUG_PRINTF("[TFT] SPI clock    : 10.5 MHz (PCLK2/8)\r\n");
    DEBUG_PRINTF("[TFT] Pines        : SCK=PA5 MISO=PA6 MOSI=PA7\r\n");
    DEBUG_PRINTF("[TFT]               CS=PC4 DC=PC5 RST=PB0 BL=PB1\r\n");
    DEBUG_PRINT("[TFT] ==============================\r\n\r\n");
#endif
}
