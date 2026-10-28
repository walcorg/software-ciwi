/**
 * @file    main_phase7.c
 * @brief   Phase 7: TROPIC01 secure element and the Anchor Protocol on the CiWi.
 *
 * Hardware: the phase 1 setup (ST-Link, USB-UART on PA9) plus a TROPIC01 on SPI2
 * (wiring in tropic/tr01_platform_stm32.c). The display is not needed in this phase: the
 * owner confirms with the KEY button (PC13) and everything is reported on the UART.
 *
 * Steps (each prints PASS/FAIL):
 *   1. TROPIC01 bring-up: SPI, application firmware, chip ID and firmware versions.
 *   2. Provisioning, first run only: hold KEY for 3 s. Writes the CiWi pairing key (slot 1),
 *      generates the P-256 anchor key (ECC slot 0) and initialises the claim counter.
 *   3. Secure session with the CiWi key: ping, random numbers, sign + verify, counter.
 *   4. PIN with MAC-and-Destroy (test PIN 2468, bound to this STM32's unique ID).
 *   5. Anchor Protocol: a TEST shield signs a receipt, a bind and an NFT window record; the CiWi
 *      verifies each one and, after a KEY press, signs it with TROPIC01. The vectors are printed
 *      as JSON for tests/host/verify.js.
 *
 * LED (PB2): slow blink = all passed, fast blink = something failed.
 */
#include <stdio.h>
#include <string.h>

#include "anchor_selftest.h"
#include "ciwi_log.h"
#include "rng_test.h"
#include "stm32f4xx_hal.h"
#include "storage_hal.h"
#include "tr01_ciwi.h"
#include "tr01_pin.h"
#include "uart_debug.h"

#define LED_PORT GPIOB
#define LED_PIN  GPIO_PIN_2
#define KEY_PORT GPIOC
#define KEY_PIN  GPIO_PIN_13 /* pressed = high (button to 3V3 on the WeAct board) */

#define CONFIRM_TIMEOUT_MS 60000u
#define PROVISION_HOLD_MS  3000u

extern void stm32_init(void);

static int g_pass, g_fail;
static ciwi_tr01_t g_t;

/* ------------------------------------------------------------------ board helpers */

static void io_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = LED_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &g);
    g.Pin = KEY_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(KEY_PORT, &g);
}

static bool key_down(void) { return HAL_GPIO_ReadPin(KEY_PORT, KEY_PIN) == GPIO_PIN_SET; }

/** Wait for a short press (released). False on timeout. */
static bool wait_press(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while (key_down()) {           /* wait for a previous press to end */
        if (HAL_GetTick() - t0 > timeout_ms) return false;
    }
    while (!key_down()) {
        if (HAL_GetTick() - t0 > timeout_ms) return false;
        if (((HAL_GetTick() - t0) / 250) % 2) HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
        else HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    }
    HAL_Delay(30);                 /* debounce */
    while (key_down()) {}
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    return true;
}

/** Wait until KEY is held for hold_ms. False if nobody does it within timeout_ms. */
static bool wait_hold(uint32_t hold_ms, uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < timeout_ms) {
        if (key_down()) {
            uint32_t p0 = HAL_GetTick();
            while (key_down()) {
                if (HAL_GetTick() - p0 >= hold_ms) {
                    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
                    while (key_down()) {}
                    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
                    return true;
                }
            }
        }
    }
    return false;
}

static void mcu_uid(uint8_t out[12]) { memcpy(out, (const void *)UID_BASE, 12); }

static void expect(const char *name, bool ok)
{
    if (ok) g_pass++;
    else g_fail++;
    CIWI_LOG("  %s  %s\r\n", ok ? "PASS" : "FAIL", name);
}

static void halt(int fast)
{
    CIWI_LOG("\r\n== %d passed, %d failed ==\r\n", g_pass, g_fail);
    while (1) {
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        HAL_Delay(fast ? 100 : 1000);
    }
}

/* ------------------------------------------------------------------ anchor self-test I/O */

/* The JSON is collected in RAM and printed in one block after the confirmations, so the PASS
 * lines and prompts do not end up inside it. */
static char g_json[12 * 1024];
static size_t g_json_len;
static bool g_json_overflow;

static void emit_json(const char *text)
{
    size_t n = strlen(text);
    if (g_json_len + n >= sizeof(g_json)) {
        g_json_overflow = true;
        return;
    }
    memcpy(g_json + g_json_len, text, n + 1);
    g_json_len += n;
}

/** UART terminals expect CR LF. */
static void print_json(void)
{
    char line[256];
    size_t p = 0;
    for (size_t i = 0; i < g_json_len; i++) {
        char c = g_json[i];
        if (c == '\n' || p == sizeof(line) - 3) {
            line[p++] = '\r';
            line[p++] = '\n';
            line[p] = '\0';
            debug_print(line);
            p = 0;
            if (c == '\n') continue;
        }
        line[p++] = c;
    }
}

static bool confirm_key(const char *title, const char *detail)
{
    CIWI_LOG("\r\n  >>> %s\r\n      %s\r\n      Press KEY to sign (60 s)...\r\n", title, detail);
    bool ok = wait_press(CONFIRM_TIMEOUT_MS);
    CIWI_LOG("      %s\r\n", ok ? "approved" : "timeout: not signed");
    return ok;
}

static void result_line(const char *name, bool ok) { expect(name, ok); }

/* ------------------------------------------------------------------ main */

int main(void)
{
    stm32_init(); /* HAL, 168 MHz clock, debug UART, minimal MPU */
    io_init();

    CIWI_LOG("\r\n========================================\r\n");
    CIWI_LOG(" WALCORG CiWi - phase 7: TROPIC01 + Anchor\r\n");
    CIWI_LOG(" software-ciwi v0.3, built %s %s\r\n", __DATE__, __TIME__);
    CIWI_LOG("========================================\r\n");

    expect("STM32 RNG", rng_init());
    expect("NORCOW storage", storage_hal_init());

    /* ---- 1. bring-up */
    CIWI_LOG("\r\n== 1. TROPIC01 bring-up (SPI2: PB13 SCK, PB14 MISO, PB15 MOSI, PB12 CS) ==\r\n");
    lt_ret_t r = ciwi_tr01_open(&g_t);
    expect("TROPIC01 answers, application firmware running", r == LT_OK);
    if (r != LT_OK) {
        CIWI_LOG("  Check wiring, 3V3 and GND. LT error: %s\r\n", lt_ret_verbose(r));
        halt(1);
    }
    expect("chip ID and firmware versions", ciwi_tr01_print_info(&g_t) == LT_OK);

    /* ---- 2. provisioning (first run) */
    CIWI_LOG("\r\n== 2. CiWi session / provisioning ==\r\n");
    r = ciwi_tr01_start(&g_t);
    if (g_t.state == CIWI_TR01_STATE_NEW || g_t.state == CIWI_TR01_STATE_FOREIGN ||
        !g_t.anchor_ready) {
        if (g_t.state == CIWI_TR01_STATE_FOREIGN) {
            CIWI_LOG("  This STM32 holds a pairing key that this TROPIC01 does not accept.\r\n");
            CIWI_LOG("  Check the wiring first. Provisioning only proceeds if slot 1 is empty;\r\n");
            CIWI_LOG("  nothing is erased.\r\n");
        }
        CIWI_LOG("  Not provisioned. Hold KEY for 3 s to provision this TROPIC01 (60 s)...\r\n");
        if (!wait_hold(PROVISION_HOLD_MS, 60000u)) {
            CIWI_LOG("  Skipped.\r\n");
            halt(1);
        }
        r = ciwi_tr01_provision(&g_t);
        expect("provisioning", r == LT_OK);
        if (r != LT_OK) halt(1);
    }
    expect("secure session with CiWi key (SH1)", g_t.state == CIWI_TR01_STATE_READY);
    expect("anchor key loaded", g_t.anchor_ready);
    if (!g_t.anchor_ready) halt(1);
    ciwi_log_hex("  anchor pubX : ", ciwi_tr01_anchor_pub(&g_t), 32);
    ciwi_log_hex("  anchor pubY : ", ciwi_tr01_anchor_pub(&g_t) + 32, 32);
    ciwi_log_hex("  ciwiId      : ", ciwi_tr01_ciwi_id(&g_t), 32);

    /* ---- 3. secure channel */
    CIWI_LOG("\r\n== 3. Secure channel ==\r\n");
    expect("ping", ciwi_tr01_ping(&g_t) == LT_OK);
    uint8_t buf[64];
    expect("random (TROPIC01 TRNG xor STM32 RNG)", ciwi_tr01_random(&g_t, buf, sizeof(buf)) == LT_OK);
    uint8_t digest[32], sig[64];
    memcpy(digest, buf, 32);
    uint32_t t0 = HAL_GetTick();
    r = ciwi_tr01_anchor_sign(&g_t, digest, sig);
    CIWI_LOG("  P-256 sign + local verify: %lu ms\r\n", (unsigned long)(HAL_GetTick() - t0));
    expect("anchor signature (low-s, verified before release)", r == LT_OK);
    uint64_t ctr = 0;
    expect("claim counter", ciwi_tr01_claim_counter(&g_t, &ctr) == LT_OK);
    CIWI_LOG("  ciwiCounter = %lu\r\n", (unsigned long)ctr);

#ifndef CIWI_PHASE7_PIN_TEST
#define CIWI_PHASE7_PIN_TEST 1
#endif
#if CIWI_PHASE7_PIN_TEST
    /* ---- 4. PIN */
    CIWI_LOG("\r\n== 4. PIN with MAC-and-Destroy (test PIN 2468; replaces any PIN state) ==\r\n");
    uint8_t uid[12], k1[CIWI_PIN_KEY_LEN], k2[CIWI_PIN_KEY_LEN], left = 0;
    mcu_uid(uid);
    t0 = HAL_GetTick();
    r = ciwi_pin_set(&g_t, "2468", uid, sizeof(uid), k1);
    CIWI_LOG("  set PIN: %lu ms\r\n", (unsigned long)(HAL_GetTick() - t0));
    expect("set PIN", r == LT_OK);
    t0 = HAL_GetTick();
    r = ciwi_pin_check(&g_t, "2468", uid, sizeof(uid), k2);
    CIWI_LOG("  check PIN: %lu ms\r\n", (unsigned long)(HAL_GetTick() - t0));
    expect("correct PIN releases the same key", r == LT_OK && memcmp(k1, k2, 32) == 0);
    expect("wrong PIN refused", ciwi_pin_check(&g_t, "1111", uid, sizeof(uid), k2) != LT_OK);
    expect("one attempt used", ciwi_pin_attempts_left(&g_t, &left) == LT_OK &&
                                   left == CIWI_PIN_ATTEMPTS - 1);
    expect("correct PIN again", ciwi_pin_check(&g_t, "2468", uid, sizeof(uid), k2) == LT_OK &&
                                    memcmp(k1, k2, 32) == 0);
    expect("attempts reset", ciwi_pin_attempts_left(&g_t, &left) == LT_OK &&
                                 left == CIWI_PIN_ATTEMPTS);
    memset(k1, 0, sizeof(k1));
    memset(k2, 0, sizeof(k2));
#endif

    /* ---- 5. Anchor Protocol */
    CIWI_LOG("\r\n== 5. Anchor Protocol (TEST shield key; confirm each signature with KEY) ==\r\n");
    anchor_selftest_io_t io = {.emit = emit_json, .confirm = confirm_key, .result = result_line};
    anchor_selftest_run(&g_t, &io);
    expect("vectors fit in the buffer", !g_json_overflow);
    CIWI_LOG("\r\n-----BEGIN CIWI ANCHOR VECTORS-----\r\n");
    print_json();
    CIWI_LOG("-----END CIWI ANCHOR VECTORS-----\r\n");
    CIWI_LOG("Copy the block between the markers to vectors.json and run:\r\n");
    CIWI_LOG("  cd tests/host && node verify.js vectors.json\r\n");

    halt(g_fail != 0);
}
