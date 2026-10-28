/**
 * @file    tr01_platform_stm32.c
 * @brief   tr01_platform.h for the CiWi (STM32F405RGT6).
 *
 * TROPIC01 wiring (SPI2, header P2 of the WeAct STM32F4 64-pin Core Board):
 *
 *   STM32        TROPIC01   Notes
 *   -----------  ---------  -------------------------------------------------------
 *   PB13 SCK     SCK        SPI2, AF5
 *   PB14 MISO    MISO       SPI2, AF5
 *   PB15 MOSI    MOSI       SPI2, AF5
 *   PB12         CSN        GPIO output, driven by libtropic
 *   PB10         GPO        optional, input (only with LT_USE_INT_PIN)
 *   3V3, GND     VCC, GND   TROPIC01 runs at 1.8-3.6 V; 100 nF close to the chip
 *
 * SPI mode 0, MSB first. SPI2 runs from APB1 (42 MHz); prescaler 16 gives 2.6 MHz, well inside
 * TROPIC01's limit and tolerant of jumper wires on the test board.
 *
 * Pins chosen to avoid the display (SPI1 PA5-PA7, PC4, PC5, PB0, PB1), touch (PC6, PC7),
 * click-wheel (PB6, PB7), microSD (PC8-PC12, PD2, PA8), USB (PA11, PA12), UART (PA9, PA10),
 * LED (PB2), KEY (PC13) and VCAP (PB11).
 *
 * Pairing key: stored in NORCOW (STORAGE_APP_TROPIC, key 0x01) as priv || pub, 64 bytes.
 * It is protected by the flash read-out protection (RDP), as in Trezor's design. Without RDP a
 * debugger can read it; on the development board that is expected.
 */
#include <string.h>

#include "libtropic_port_stm32f4xx.h"
#include "rng_test.h"
#include "stm32f4xx_hal.h"
#include "storage_hal.h"
#include "tr01_platform.h"

#define TR01_SPI           SPI2
#define TR01_SPI_PRESCALER SPI_BAUDRATEPRESCALER_16
#define TR01_SCK_PIN       GPIO_PIN_13
#define TR01_MISO_PIN      GPIO_PIN_14
#define TR01_MOSI_PIN      GPIO_PIN_15
#define TR01_CS_PIN        GPIO_PIN_12
#define TR01_GPO_PIN       GPIO_PIN_10
#define TR01_PORT          GPIOB

#define KEYSTORE_KEY_PAIRING 0x01

static lt_dev_stm32f4xx_t g_dev;

/**
 * Called by HAL_SPI_Init() (from libtropic's lt_port_init). Only SPI2 is handled here: the display
 * driver configures SPI1 by itself.
 */
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != TR01_SPI) return;

    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin = TR01_SCK_PIN | TR01_MISO_PIN | TR01_MOSI_PIN;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(TR01_PORT, &g);
    /* MISO pulled up so a missing chip reads 0xFF instead of noise. */
    g.Pin = TR01_MISO_PIN;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(TR01_PORT, &g);
}

void HAL_SPI_MspDeInit(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != TR01_SPI) return;
    __HAL_RCC_SPI2_CLK_DISABLE();
    HAL_GPIO_DeInit(TR01_PORT, TR01_SCK_PIN | TR01_MISO_PIN | TR01_MOSI_PIN);
}

bool tr01_platform_attach(lt_handle_t *h)
{
    memset(&g_dev, 0, sizeof(g_dev)); /* the libtropic STM32 port requires a zeroed struct */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g_dev.spi_instance = TR01_SPI;
    g_dev.baudrate_prescaler = TR01_SPI_PRESCALER;
    g_dev.spi_cs_gpio_bank = TR01_PORT;
    g_dev.spi_cs_gpio_pin = TR01_CS_PIN;
#if LT_USE_INT_PIN
    g_dev.int_gpio_bank = TR01_PORT;
    g_dev.int_gpio_pin = TR01_GPO_PIN;
#endif
    g_dev.rng_handle = &hrng; /* rng_init() must have run */
    h->l2.device = &g_dev;
    return hrng.Instance == RNG;
}

bool tr01_platform_random(uint8_t *buf, size_t len)
{
    return rng_fill_buffer(buf, (uint32_t)len);
}

bool tr01_keystore_load(uint8_t priv[TR01_PAIRING_KEY_LEN], uint8_t pub[TR01_PAIRING_KEY_LEN])
{
    uint8_t buf[2 * TR01_PAIRING_KEY_LEN];
    uint16_t n = 0;
    bool ok = storage_get(STORAGE_APP_TROPIC, KEYSTORE_KEY_PAIRING, buf, sizeof(buf), &n) &&
              n == sizeof(buf);
    if (ok) {
        memcpy(priv, buf, TR01_PAIRING_KEY_LEN);
        memcpy(pub, buf + TR01_PAIRING_KEY_LEN, TR01_PAIRING_KEY_LEN);
    }
    memset(buf, 0, sizeof(buf));
    return ok;
}

bool tr01_keystore_save(const uint8_t priv[TR01_PAIRING_KEY_LEN],
                        const uint8_t pub[TR01_PAIRING_KEY_LEN])
{
    uint8_t buf[2 * TR01_PAIRING_KEY_LEN];
    memcpy(buf, priv, TR01_PAIRING_KEY_LEN);
    memcpy(buf + TR01_PAIRING_KEY_LEN, pub, TR01_PAIRING_KEY_LEN);
    bool ok = storage_put(STORAGE_APP_TROPIC, KEYSTORE_KEY_PAIRING, buf, sizeof(buf));
    memset(buf, 0, sizeof(buf));
    return ok;
}

bool tr01_keystore_erase(void)
{
    /* A zero-length entry marks the key as deleted (the old bytes stay until the NORCOW sector is
     * compacted or erased). */
    return storage_put(STORAGE_APP_TROPIC, KEYSTORE_KEY_PAIRING, NULL, 0);
}
