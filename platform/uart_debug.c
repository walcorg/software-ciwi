/**
 * @file    uart_debug.c
 * @brief   Debug por UART1 — WALCORG Fase 1
 *
 * Implementa el canal de debug por UART y redirige la función
 * _write() del sistema para que printf() funcione sobre UART.
 *
 * ¿Por qué PA9 y no otro pin?
 *   PA9 = USART1_TX es el pin de TX más accesible en la WeAct
 *   board y está en el borde de la placa, fácil de alcanzar
 *   con un cable en la protoboard.
 *
 * ¿Por qué 115200 baud?
 *   Es el estándar de facto para debug en sistemas embebidos.
 *   Con PCLK2 = 84MHz, el BRR (Baud Rate Register) resulta en
 *   un divisor entero exacto para 115200, minimizando el error.
 *   Error real: 0.0% (exacto con 84MHz)
 *
 * @license GNU GPL v3
 */

#include "uart_debug.h"
#include <stdio.h>      /* Para vsnprintf */
#include <string.h>     /* Para strlen */

/* Handle global — accedido desde stm32f4xx_it.c para el IRQ handler */
UART_HandleTypeDef huart1;

/* Buffer estático para debug_printf — evita malloc en bare-metal */
static char _printf_buf[DEBUG_PRINTF_BUF_SIZE];


/* =========================================================
 * INICIALIZACIÓN
 * ========================================================= */

/**
 * debug_uart_init — Inicializar USART1 en PA9 a 115200 baud
 *
 * Configura:
 *   1. Clock de GPIOA y USART1
 *   2. PA9 como Alternate Function 7 (USART1_TX)
 *   3. Periférico USART1: 115200-8N1
 *
 * Solo TX configurado (PA9). PA10 (RX) no necesario para debug
 * unidireccional — conectar solo TX + GND al adaptador USB-UART.
 */
void debug_uart_init(void) {

    /* -----------------------------------------------------------------
     * GPIO: configurar PA9 como TX de USART1
     *
     * PA9 tiene múltiples funciones alternativas (AF):
     *   AF7 = USART1_TX  ← la que queremos
     *   AF3 = TIM1_CH2
     *   etc.
     *
     * El modo AF_PP (Alternate Function Push-Pull) significa que
     * el periférico USART1 controla el pin directamente,
     * no el registro GPIO ODR.
     * ----------------------------------------------------------------- */
    __HAL_RCC_GPIOA_CLK_ENABLE();   /* Habilitar clock de GPIOA */

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_9;        /* PA9 = USART1_TX */
    gpio.Mode      = GPIO_MODE_AF_PP;   /* Alternate Function Push-Pull */
    gpio.Pull      = GPIO_NOPULL;       /* Sin pull-up/down — la línea UART está en HIGH idle */
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH; /* Alta velocidad para 115200 baud */
    gpio.Alternate = GPIO_AF7_USART1;   /* Conectar PA9 al periférico USART1 */
    HAL_GPIO_Init(GPIOA, &gpio);

    /* -----------------------------------------------------------------
     * USART1: configurar el periférico serial
     *
     * 115200-8N1 significa:
     *   115200 = velocidad en bits por segundo (baud rate)
     *   8      = 8 bits de datos por trama
     *   N      = sin bit de paridad (None)
     *   1      = 1 bit de parada
     *
     * Esta es la configuración más universal — compatible con
     * todos los monitores seriales y adaptadores USB-UART.
     * ----------------------------------------------------------------- */
    __HAL_RCC_USART1_CLK_ENABLE();  /* Habilitar clock de USART1 */

    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = DEBUG_UART_BAUDRATE;   /* 115200 bps */
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;    /* 8 bits de datos */
    huart1.Init.StopBits     = UART_STOPBITS_1;       /* 1 bit de parada */
    huart1.Init.Parity       = UART_PARITY_NONE;      /* Sin paridad */
    huart1.Init.Mode         = UART_MODE_TX;           /* Solo TX (no RX en debug) */
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;   /* Sin control de flujo */
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;  /* Oversampling ×16 (más preciso) */

    /* HAL_UART_Init() configura los registros del periférico USART1.
     * Si falla, el debug no funcionará pero el firmware sí puede
     * continuar (debug no es crítico para el funcionamiento). */
    HAL_UART_Init(&huart1);

    /* Mensaje de inicio — si aparece en el monitor serial,
     * la UART está funcionando correctamente */
    debug_print("\r\n");
    debug_print("=====================================\r\n");
    debug_print(" WALCORG Cold Wallet — Debug UART\r\n");
    debug_print(" 115200-8N1 | PA9 TX | STM32F405\r\n");
    debug_print("=====================================\r\n");
}


/* =========================================================
 * FUNCIONES DE TRANSMISIÓN
 * ========================================================= */

/**
 * debug_print — Transmitir string por UART (polling, sin IRQ)
 *
 * Usa polling (HAL_UART_Transmit) en vez de DMA o interrupciones.
 * Es más simple y suficiente para debug — no necesitamos alta
 * velocidad en los mensajes de debug.
 *
 * Timeout de 1000ms: si la UART no termina de transmitir en 1 segundo,
 * algo está muy mal (¿clock incorrecto? ¿periférico dañado?).
 */
void debug_print(const char *str) {
    if (!str) return;
    uint16_t len = (uint16_t)strlen(str);
    if (len == 0) return;

    /* HAL_UART_Transmit envía 'len' bytes y espera hasta que
     * el hardware confirme que se transmitieron todos */
    HAL_UART_Transmit(&huart1, (uint8_t *)str, len, 1000);
}

/**
 * debug_printf — Printf por UART
 *
 * Formatea el string en un buffer estático y lo transmite.
 * El buffer es de DEBUG_PRINTF_BUF_SIZE bytes (256 por defecto).
 * Mensajes más largos se truncan — ajustar la constante si es necesario.
 *
 * NOTA: NO es seguro para uso en interrupciones (IRQ handlers)
 * porque modifica _printf_buf que es global. Solo llamar desde
 * el contexto principal (main loop).
 */
void debug_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    /* vsnprintf formatea el string en el buffer, respetando el tamaño
     * máximo para evitar buffer overflow — siempre termina en '\0' */
    int len = vsnprintf(_printf_buf, DEBUG_PRINTF_BUF_SIZE, fmt, args);
    va_end(args);

    if (len > 0) {
        /* Transmitir los bytes formateados por UART */
        HAL_UART_Transmit(
            &huart1,
            (uint8_t *)_printf_buf,
            (uint16_t)(len < DEBUG_PRINTF_BUF_SIZE ? len : DEBUG_PRINTF_BUF_SIZE - 1),
            1000
        );
    }
}

/**
 * debug_hexdump — Imprimir buffer en formato hex
 *
 * Útil para inspeccionar datos binarios: respuestas del AS5600,
 * datos del display, paquetes USB, etc.
 *
 * Formato de salida:
 *   label[00..07]: AA BB CC DD EE FF 00 11  |........|
 *   label[08..0F]: 12 34 56 78 9A BC DE F0  |.4Vx....|
 */
void debug_hexdump(const char *label, const uint8_t *buf, uint32_t len) {
    if (!buf || len == 0) return;

    char line[80];   /* Buffer para una línea del dump */

    for (uint32_t i = 0; i < len; i += 8) {
        /* Offset de inicio de esta línea */
        int pos = snprintf(line, sizeof(line), "%s[%02lX..%02lX]: ",
                           label, i, (i + 7 < len) ? i + 7 : len - 1);

        /* Bytes en hex */
        for (uint32_t j = 0; j < 8 && (i + j) < len; j++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", buf[i + j]);
        }

        /* Padding si la línea está incompleta */
        while (pos < 40) { line[pos++] = ' '; }

        /* Caracteres ASCII (imprimibles) */
        line[pos++] = '|';
        for (uint32_t j = 0; j < 8 && (i + j) < len; j++) {
            uint8_t c = buf[i + j];
            line[pos++] = (c >= 32 && c < 127) ? (char)c : '.';
        }
        line[pos++] = '|';
        line[pos++] = '\r';
        line[pos++] = '\n';
        line[pos]   = '\0';

        HAL_UART_Transmit(&huart1, (uint8_t *)line, (uint16_t)pos, 1000);
    }
}


/* =========================================================
 * REDIRECCIÓN DE printf() AL UART
 *
 * En sistemas bare-metal con newlib (la librería C que usa
 * arm-none-eabi-gcc), printf() internamente llama a _write()
 * para transmitir los caracteres.
 *
 * Si definimos nuestra propia _write(), printf() usará
 * nuestra implementación — es decir, transmitirá por UART.
 *
 * Esto permite usar printf() directamente en lugar de
 * debug_printf() si se prefiere, aunque en producción
 * ambos deben eliminarse.
 *
 * __attribute__((weak)) permite que otra parte del proyecto
 * sobreescriba esta función si es necesario.
 * ========================================================= */
#include <sys/types.h>  /* Para ssize_t */

__attribute__((weak))
int _write(int file, char *ptr, int len) {
    /* 'file' podría ser stdout (1) o stderr (2) — los manejamos igual */
    (void)file;  /* Silenciar warning de parámetro no usado */

    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len, 1000);
    return len;
}
