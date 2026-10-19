/**
 * @file    clock_config.h
 * @brief   Configuración del clock del sistema — WalCorg Fase 1
 *
 * Separa SystemClock_Config() en su propio módulo para que
 * sea reutilizable por todos los archivos del firmware:
 * blink_test.c, usb_test.c, y eventualmente el firmware
 * principal de Trezor (legacy/firmware/main.c).
 *
 * Frecuencias configuradas:
 *   SYSCLK  = 168 MHz  (máximo del STM32F405, igual que Trezor One)
 *   HCLK    = 168 MHz  (AHB bus — CPU, DMA, Flash)
 *   PCLK1   =  42 MHz  (APB1 — I2C1, SPI2/3, UART2-5, TIM2-7)
 *   PCLK2   =  84 MHz  (APB2 — SPI1, UART1, TIM1/8-11, ADC)
 *   USB_CLK =  48 MHz  (requerido exacto para USB Full Speed)
 *
 * @license GNU GPL v3
 */

#ifndef CLOCK_CONFIG_H
#define CLOCK_CONFIG_H

#include "stm32f4xx_hal.h"

/* =========================================================
 * CONSTANTES DE FRECUENCIA
 *
 * Definir estas constantes permite al resto del código calcular
 * divisores de baud rate, timings de SPI, periodos de I2C, etc.
 * sin números mágicos.
 * ========================================================= */

/** Frecuencia del cristal externo (HSE) en Hz — cristal de la WeAct board */
#define HSE_FREQ_HZ         8000000UL       /* 8 MHz */

/** Frecuencia del sistema tras el PLL en Hz */
#define SYSCLK_FREQ_HZ      168000000UL     /* 168 MHz */

/** Frecuencia del bus AHB (CPU, DMA, Flash) en Hz */
#define HCLK_FREQ_HZ        168000000UL     /* 168 MHz */

/** Frecuencia del bus APB1 en Hz
 *  Periféricos en APB1: I2C1 (click-wheel), SPI2/3, UART2-5, TIM2-7
 *  Importante para calcular el divisor de I2C a 400kHz */
#define PCLK1_FREQ_HZ       42000000UL      /* 42 MHz */

/** Frecuencia del bus APB2 en Hz
 *  Periféricos en APB2: SPI1 (display TFT), UART1 (debug), TIM1/8-11, ADC
 *  Importante para calcular la velocidad máxima del SPI del display */
#define PCLK2_FREQ_HZ       84000000UL      /* 84 MHz */

/** Frecuencia del USB — DEBE ser exactamente 48MHz para USB Full Speed */
#define USB_CLK_FREQ_HZ     48000000UL      /* 48 MHz */

/* =========================================================
 * CÁLCULOS DERIVADOS
 *
 * Con PCLK2 = 84MHz, la velocidad máxima de SPI1 es:
 *   SPI_BAUDRATEPRESCALER_2  → 84/2  = 42 MHz  (máximo teórico ILI9488)
 *   SPI_BAUDRATEPRESCALER_4  → 84/4  = 21 MHz  (recomendado ILI9488)
 *   SPI_BAUDRATEPRESCALER_8  → 84/8  = 10.5MHz (seguro para cables largos)
 *
 * Con PCLK1 = 42MHz, el divisor de I2C para 400kHz es:
 *   ClockSpeed = 400000 → el HAL calcula automáticamente los registros
 *   con PCLK1 = 42MHz → CCR = 35, TRISE = 13
 * ========================================================= */

/** Velocidad máxima recomendada de SPI1 para el display ILI9488 */
#define TFT_SPI_MAX_HZ      21000000UL      /* 21 MHz (PCLK2/4) */

/** Velocidad de I2C para el click-wheel AS5600 */
#define CW_I2C_SPEED_HZ     400000UL        /* 400 kHz Fast Mode */


/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Configura el PLL y todos los buses de clock.
 *
 * DEBE llamarse como primera función en main(), antes que
 * cualquier periférico. El order correcto es:
 *   1. HAL_Init()           ← configura SysTick con HSI temporal
 *   2. SystemClock_Config() ← cambia a HSE+PLL 168MHz
 *   3. Inicializar periféricos (GPIO, I2C, SPI, USB, etc.)
 *
 * Si el cristal HSE no responde (no está soldado, está dañado,
 * frecuencia incorrecta), esta función entra en Error_Handler().
 */
void SystemClock_Config(void);

/**
 * @brief Verifica que el clock está correcto comparando
 *        la frecuencia real con la esperada.
 *
 * @return 1 si SYSCLK == 168MHz, 0 si hay discrepancia.
 *         Una discrepancia indica que el PLL no configuró bien.
 */
uint8_t clock_verify(void);

/**
 * @brief Retorna la frecuencia actual de SYSCLK en Hz.
 *        Útil para debug y para calcular divisores de periféricos.
 */
uint32_t clock_get_sysclk_hz(void);

/**
 * @brief Imprime por UART un resumen del estado del clock.
 *        Solo disponible cuando PRODUCTION=0.
 */
void clock_print_status(void);

/** [v0.2] Defined in platform/stm32_walcorg.c */
void Error_Handler(void);

#endif /* CLOCK_CONFIG_H */
