/**
 * @file    stm32f4xx_hal_conf.h
 * @brief   Configuración del HAL de ST — WalCorg Cold Wallet
 *
 * Este archivo controla qué módulos del HAL se compilan.
 * Cada módulo habilitado suma código a la Flash.
 * Solo habilitamos los que realmente usamos para minimizar
 * el tamaño del firmware y la superficie de ataque.
 *
 * MÓDULOS HABILITADOS Y POR QUÉ:
 *
 *   HAL_MODULE_ENABLED → núcleo del HAL (siempre requerido)
 *   HAL_RCC_MODULE_ENABLED → Reset and Clock Control (siempre)
 *   HAL_GPIO_MODULE_ENABLED → GPIO: LED, botones, pines SPI/I2C
 *   HAL_CORTEX_MODULE_ENABLED → NVIC, SysTick, MPU (siempre)
 *   HAL_FLASH_MODULE_ENABLED → escritura de Flash (almacén de claves)
 *   HAL_I2C_MODULE_ENABLED → I2C1: click-wheel AS5600
 *   HAL_SPI_MODULE_ENABLED → SPI1: display TFT ILI9488
 *   HAL_UART_MODULE_ENABLED → UART1: debug por PA9
 *   HAL_PCD_MODULE_ENABLED → USB Device (trezorctl)
 *   HAL_RNG_MODULE_ENABLED → Hardware RNG (generación de semillas)
 *   HAL_PWR_MODULE_ENABLED → Power management (voltage scaling)
 *   HAL_DMA_MODULE_ENABLED → DMA: acelerar SPI del display
 *   HAL_TIM_MODULE_ENABLED → Timers: PWM para backlight del display
 *
 * MÓDULOS DESHABILITADOS (no los usamos):
 *   HAL_ADC, HAL_CAN, HAL_CRC, HAL_DAC, HAL_DCMI,
 *   HAL_ETH, HAL_HASH, HAL_I2S, HAL_IRDA, HAL_IWDG,
 *   HAL_LTDC, HAL_NAND, HAL_NOR, HAL_PCCARD, HAL_SAI,
 *   HAL_SD, HAL_SMARTCARD, HAL_SDRAM, HAL_SRAM, HAL_WWDG
 *
 * @license GNU GPL v3
 */

#ifndef STM32F4XX_HAL_CONF_H
#define STM32F4XX_HAL_CONF_H

/* =========================================================
 * MÓDULOS HAL HABILITADOS
 *
 * Comentar cualquier línea para deshabilitar ese módulo.
 * El compilador excluirá el código correspondiente.
 * ========================================================= */
#define HAL_MODULE_ENABLED          /* Núcleo del HAL — SIEMPRE habilitar */
#define HAL_RCC_MODULE_ENABLED      /* Clocks — SIEMPRE habilitar */
#define HAL_GPIO_MODULE_ENABLED     /* GPIO: LED, SPI, I2C, USB pines */
#define HAL_CORTEX_MODULE_ENABLED   /* NVIC, MPU, SysTick — SIEMPRE */
#define HAL_FLASH_MODULE_ENABLED    /* Flash: almacén de claves NORCOW */
#define HAL_PWR_MODULE_ENABLED      /* Power: voltage scaling para 168MHz */
#define HAL_DMA_MODULE_ENABLED      /* DMA: SPI rápido para el display TFT */
#define HAL_I2C_MODULE_ENABLED      /* I2C1: click-wheel AS5600 en PB6/PB7 */
#define HAL_SPI_MODULE_ENABLED      /* SPI1: display TFT ILI9488 en PA5-7 */
#define HAL_UART_MODULE_ENABLED     /* USART1: debug en PA9 a 115200 baud */
#define HAL_PCD_MODULE_ENABLED      /* USB Device: trezorctl en PA11/PA12 */
#define HAL_RNG_MODULE_ENABLED      /* Hardware RNG: semillas criptográficas */
#define HAL_TIM_MODULE_ENABLED      /* TIM: PWM para backlight del display */


/* =========================================================
 * PARÁMETROS DEL OSCILADOR EXTERNO (HSE)
 *
 * HSE_VALUE: frecuencia del cristal externo en Hz
 *   WeAct STM32F405RGT6 tiene un cristal de 8MHz.
 *   CRÍTICO: si este valor no coincide con el cristal real,
 *   el PLL calculará frecuencias incorrectas.
 *
 * HSE_STARTUP_TIMEOUT: tiempo máximo de espera para que el
 *   cristal HSE estabilice (en ms). 100ms es generoso —
 *   normalmente toma 1-2ms. Aumentar si hay problemas de
 *   arranque en ambientes fríos.
 * ========================================================= */
#if !defined(HSE_VALUE)
  #define HSE_VALUE    8000000U    /* 8 MHz — cristal WeAct F405 */
#endif

#if !defined(HSE_STARTUP_TIMEOUT)
  #define HSE_STARTUP_TIMEOUT    100U   /* ms */
#endif


/* =========================================================
 * PARÁMETROS DEL OSCILADOR INTERNO (HSI)
 *
 * HSI_VALUE: frecuencia del oscilador interno en Hz
 *   El HSI del STM32F405 es de 16MHz.
 *   Solo se usa como fuente temporal durante el boot antes
 *   de que el PLL con HSE esté listo.
 * ========================================================= */
#if !defined(HSI_VALUE)
  #define HSI_VALUE    16000000U   /* 16 MHz — oscilador interno */
#endif


/* =========================================================
 * PARÁMETROS DEL OSCILADOR LSE (Low Speed External)
 *
 * LSE = cristal de 32.768 kHz para el RTC (reloj de tiempo real)
 * En Fase 1 no usamos RTC, pero el HAL requiere esta constante.
 * Si en el futuro se añade un cristal de 32.768kHz para timestamps,
 * la WeAct board tiene pads para ello (PC14/PC15).
 * ========================================================= */
#if !defined(LSE_VALUE)
  #define LSE_VALUE    32768U      /* 32.768 kHz */
#endif

#if !defined(LSE_STARTUP_TIMEOUT)
  #define LSE_STARTUP_TIMEOUT    5000U  /* ms */
#endif


/* =========================================================
 * CONFIGURACIÓN DEL TICK DEL SISTEMA
 *
 * TICK_INT_PRIORITY: prioridad de la interrupción del SysTick
 *   0 = máxima prioridad (no recomendado — bloquearía todo)
 *   15 = mínima prioridad (recomendado para bare-metal sin RTOS)
 *
 * El SysTick genera una interrupción cada 1ms.
 * Con prioridad 15 (mínima), las interrupciones críticas
 * (USB, I2C) pueden interrumpirlo si es necesario.
 *
 * USE_RTOS: si se define, el HAL delega el tick al RTOS.
 *   En Trezor bare-metal: 0 (sin RTOS)
 * ========================================================= */
#define  TICK_INT_PRIORITY   15U   /* Prioridad mínima para SysTick */
#define  USE_RTOS            0U    /* Sin RTOS — bare-metal puro */

/* [v0.2] Valores que exige el HAL de ST (tomados de stm32f4xx_hal_conf_template.h) */
#if !defined(LSI_VALUE)
  #define LSI_VALUE               32000U
#endif
#if !defined(EXTERNAL_CLOCK_VALUE)
  #define EXTERNAL_CLOCK_VALUE    12288000U
#endif
#define  VDD_VALUE                3300U
#define  PREFETCH_ENABLE          1U
#define  INSTRUCTION_CACHE_ENABLE 1U
#define  DATA_CACHE_ENABLE        1U
#define  USE_SD_TRANSCEIVER  0U    /* Sin transceptor SD */
#define  USE_SPI_CRC         0U    /* Sin CRC de SPI (ahorra ciclos) */


/* =========================================================
 * CONFIGURACIÓN DE ETHERNET (DESHABILITADA)
 *
 * El STM32F405 tiene controlador Ethernet, pero la WeAct board
 * no tiene el PHY externo necesario. Estas constantes son
 * requeridas por el HAL pero no se usan.
 * ========================================================= */
#define ETH_TX_DESC_CNT    4U
#define ETH_RX_DESC_CNT    4U


/* =========================================================
 * INCLUSIÓN DE LOS HEADERS HAL HABILITADOS
 *
 * El HAL de ST usa una macro por módulo para incluir
 * condicionalmente cada header. Solo se incluyen los
 * módulos que definimos arriba.
 * ========================================================= */
#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32f4xx_hal_rcc.h"
  #include "stm32f4xx_hal_rcc_ex.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32f4xx_hal_gpio.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32f4xx_hal_dma.h"
  #include "stm32f4xx_hal_dma_ex.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32f4xx_hal_cortex.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
  #include "stm32f4xx_hal_pwr.h"
  #include "stm32f4xx_hal_pwr_ex.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
  #include "stm32f4xx_hal_flash.h"
  #include "stm32f4xx_hal_flash_ex.h"
  #include "stm32f4xx_hal_flash_ramfunc.h"
#endif

#ifdef HAL_I2C_MODULE_ENABLED
  #include "stm32f4xx_hal_i2c.h"
  #include "stm32f4xx_hal_i2c_ex.h"
#endif

#ifdef HAL_SPI_MODULE_ENABLED
  #include "stm32f4xx_hal_spi.h"
#endif

#ifdef HAL_UART_MODULE_ENABLED
  #include "stm32f4xx_hal_uart.h"
#endif

#ifdef HAL_PCD_MODULE_ENABLED
  #include "stm32f4xx_hal_pcd.h"
  #include "stm32f4xx_hal_pcd_ex.h"
#endif

#ifdef HAL_RNG_MODULE_ENABLED
  #include "stm32f4xx_hal_rng.h"
#endif

#ifdef HAL_TIM_MODULE_ENABLED
  #include "stm32f4xx_hal_tim.h"
  #include "stm32f4xx_hal_tim_ex.h"
#endif


/* =========================================================
 * VERIFICACIONES EN TIEMPO DE COMPILACIÓN
 *
 * Detectar configuraciones incorrectas antes de compilar.
 * ========================================================= */

/* Verificar que HSE_VALUE está en rango razonable */
#if (HSE_VALUE < 4000000U) || (HSE_VALUE > 26000000U)
  #error "HSE_VALUE fuera de rango. Debe estar entre 4MHz y 26MHz."
#endif

/* Verificar que el tick tiene una prioridad válida (0-15) */
#if (TICK_INT_PRIORITY > 15U)
  #error "TICK_INT_PRIORITY debe ser 0-15 (0=max, 15=min)"
#endif

/* Advertir si se habilitan módulos que requieren hardware no disponible */
#if defined(HAL_ETH_MODULE_ENABLED)
  #warning "HAL_ETH habilitado pero la WeAct board no tiene PHY Ethernet."
#endif

/* [v0.2] Macro de aserciones del HAL (faltaba: el HAL la usa en todos sus módulos) */
#ifdef USE_FULL_ASSERT
  #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t *file, uint32_t line);
#else
  #define assert_param(expr) ((void)0U)
#endif

#endif /* STM32F4XX_HAL_CONF_H */
