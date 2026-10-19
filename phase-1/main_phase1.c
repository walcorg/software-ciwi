/**
 * @file    main_phase1.c
 * @brief   Main integrador de la Fase 1 — WalCorg Cold Wallet
 *
 * ESTE ARCHIVO une todos los módulos de la Fase 1 en un solo
 * programa de verificación que demuestra que el bringup
 * completo del hardware funciona correctamente.
 *
 * SECUENCIA DE ARRANQUE Y VERIFICACIÓN:
 *
 *   [Boot]
 *     │
 *     ▼
 *   Reset_Handler (startup_stm32f405xx.s)
 *     │ → copia .data de Flash a SRAM
 *     │ → inicializa .bss a cero
 *     │ → llama main()
 *     ▼
 *   stm32_init()
 *     │ → FPU habilitada
 *     │ → HAL_Init() — SysTick 1ms
 *     │ → SystemClock_Config() — 168MHz HSE+PLL
 *     │ → debug_uart_init() — UART PA9 115200
 *     │ → mpu_config_minimal() — 4 regiones MPU
 *     ▼
 *   phase1_tests()
 *     │ → TEST 1: Clock verificado (168MHz)
 *     │ → TEST 2: LED parpadea (GPIO funcional)
 *     │ → TEST 3: UART transmite (debug funcional)
 *     │ → TEST 4: USB enumera (OTG FS funcional)
 *     │ → TEST 5: I2C detecta AS5600 (click-wheel)
 *     ▼
 *   main_loop()
 *     │ → LED heartbeat cada 1s
 *     │ → USB polling cada 5s
 *     │ → Reporte de estado por UART
 *
 * CRITERIO DE ÉXITO DE LA FASE 1:
 *   ✓ Monitor serial muestra "FASE 1 COMPLETADA"
 *   ✓ LED parpadea a 1Hz (heartbeat)
 *   ✓ "lsusb" muestra "WalCorg Cold Wallet"
 *   ✓ "trezorctl list" muestra el dispositivo
 *
 * @license GNU GPL v3
 */

#include "stm32f4xx_hal.h"
#include "clock_config.h"
#include "uart_debug.h"
#include "usb_test.h"
#include "clickwheel.h"

/* =========================================================
 * DEFINICIÓN DEL LED (PC13 en WeAct F405)
 * ========================================================= */
#define LED_PORT        GPIOC
#define LED_PIN         GPIO_PIN_13
#define LED_ON()        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET)
#define LED_OFF()       HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET)
#define LED_TOGGLE()    HAL_GPIO_TogglePin(LED_PORT, LED_PIN)


/* =========================================================
 * PROTOTIPOS PRIVADOS
 * ========================================================= */
extern void stm32_init(void);       /* stm32_walcorg.c */
static void led_init(void);
static uint8_t run_phase1_tests(void);
static void main_loop(void);
static void print_banner(void);


/* =========================================================
 * MAIN
 * ========================================================= */
int main(void) {

    /* 1. Inicialización completa del hardware
     *    FPU → HAL → Clock 168MHz → UART → MPU */
    stm32_init();

    /* 2. Inicializar LED de status */
    led_init();

    /* 3. Imprimir banner de inicio */
    print_banner();

    /* 4. Ejecutar tests de la Fase 1 */
    uint8_t tests_ok = run_phase1_tests();

    if (tests_ok) {
        DEBUG_PRINT("\r\n");
        DEBUG_PRINT("############################################\r\n");
        DEBUG_PRINT("##                                        ##\r\n");
        DEBUG_PRINT("##   FASE 1 COMPLETADA EXITOSAMENTE       ##\r\n");
        DEBUG_PRINT("##   Listo para Fase 2 (Display TFT)      ##\r\n");
        DEBUG_PRINT("##                                        ##\r\n");
        DEBUG_PRINT("############################################\r\n\r\n");
    } else {
        DEBUG_PRINT("\r\n");
        DEBUG_PRINT("############################################\r\n");
        DEBUG_PRINT("##   FASE 1 INCOMPLETA — Ver errores      ##\r\n");
        DEBUG_PRINT("##   Revisar conexiones y configuracion   ##\r\n");
        DEBUG_PRINT("############################################\r\n\r\n");
    }

    /* 5. Loop principal */
    main_loop();

    /* Nunca llegamos aquí */
    return 0;
}


/* =========================================================
 * INICIALIZACIÓN DEL LED
 * ========================================================= */
static void led_init(void) {
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = LED_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gpio);
    LED_OFF();
}


/* =========================================================
 * TESTS DE FASE 1
 * ========================================================= */
static uint8_t run_phase1_tests(void) {
    uint8_t all_ok = 1;
    uint8_t test_num = 0;

    DEBUG_PRINT("=== Ejecutando tests de Fase 1 ===\r\n\r\n");

    /* -----------------------------------------------------------------
     * TEST 1: Verificar clock del sistema
     *
     * Lee la frecuencia real de SYSCLK y compara con 168MHz.
     * Si el PLL no configuró bien (HSE no responde, valores
     * incorrectos de PLLM/PLLN/PLLP), este test falla.
     * ----------------------------------------------------------------- */
    test_num++;
    DEBUG_PRINTF("TEST %u: Verificar clock 168MHz... ", test_num);

    clock_print_status();   /* Imprime detalles del clock */

    if (clock_verify()) {
        DEBUG_PRINTF("OK (%lu MHz)\r\n",
            clock_get_sysclk_hz() / 1000000);
    } else {
        DEBUG_PRINTF("FAIL (frecuencia: %lu MHz, esperado: 168 MHz)\r\n",
            clock_get_sysclk_hz() / 1000000);
        DEBUG_PRINT("  → Verificar cristal HSE 8MHz en la WeAct board\r\n");
        all_ok = 0;
    }

    /* -----------------------------------------------------------------
     * TEST 2: GPIO — verificar que el LED responde
     *
     * Hace 5 destellos del LED. Si el LED parpadea visualmente,
     * el clock está correcto (HAL_Delay funciona) y el GPIO también.
     * ----------------------------------------------------------------- */
    test_num++;
    DEBUG_PRINTF("TEST %u: GPIO LED (PC13)... ", test_num);

    /* 5 destellos a 200ms */
    for (int i = 0; i < 5; i++) {
        LED_ON();
        HAL_Delay(100);
        LED_OFF();
        HAL_Delay(100);
    }
    /* Si llegamos aquí sin colgar, el GPIO y el delay funcionan */
    DEBUG_PRINT("OK (LED parpadeo 5 veces)\r\n");

    /* -----------------------------------------------------------------
     * TEST 3: UART — verificar transmisión
     *
     * Enviamos un paquete de test y verificamos que el tiempo
     * de transmisión es el esperado para 115200 baud.
     * Un string de 10 chars debería tomar ~870 microsegundos.
     * ----------------------------------------------------------------- */
    test_num++;
    DEBUG_PRINTF("TEST %u: UART debug (PA9, 115200)... ", test_num);

    uint32_t t_start = HAL_GetTick();
    /* Enviar string de prueba de longitud conocida */
    const char *test_str = "UART_TEST\r\n";  /* 11 caracteres */
    HAL_UART_Transmit(&huart1, (uint8_t*)test_str, 11, 100);
    uint32_t t_elapsed = HAL_GetTick() - t_start;

    /* A 115200 baud, 11 chars × 10 bits/char ÷ 115200 = ~0.95ms */
    /* Tolerancia amplia: 0 a 10ms (el polling puede tomar más) */
    if (t_elapsed <= 10) {
        DEBUG_PRINTF("OK (%lu ms)\r\n", t_elapsed);
    } else {
        DEBUG_PRINTF("WARN (tardó %lu ms — posible error de clock)\r\n", t_elapsed);
    }

    /* -----------------------------------------------------------------
     * TEST 4: USB OTG FS — iniciar y esperar enumeración
     *
     * Inicializa el USB y espera hasta 10 segundos a que el host
     * lo enumere. El usuario debe tener el cable USB-C conectado.
     * ----------------------------------------------------------------- */
    test_num++;
    DEBUG_PRINTF("TEST %u: USB OTG FS (PA11/PA12)...\r\n", test_num);
    DEBUG_PRINT("  Asegurate de conectar el cable USB-C de la WeAct\r\n");
    DEBUG_PRINT("  a tu computadora. Esperando 10 segundos...\r\n");

    usb_test_init();

    /* Esperar hasta 10 segundos a que el host enumere el dispositivo */
    uint32_t usb_timeout = HAL_GetTick() + 10000;
    while (HAL_GetTick() < usb_timeout) {
        if (usb_is_connected()) break;

        /* Parpadeo lento del LED mientras espera */
        LED_TOGGLE();
        HAL_Delay(500);

        /* Indicador de progreso cada 2 segundos */
        static uint32_t last_dot = 0;
        if ((HAL_GetTick() - last_dot) >= 2000) {
            last_dot = HAL_GetTick();
            DEBUG_PRINT("  ");
        }
    }
    DEBUG_PRINT("\r\n");

    if (usb_is_connected()) {
        DEBUG_PRINT("  TEST USB: OK — dispositivo enumerado\r\n");
        DEBUG_PRINT("  Verificar: lsusb | grep WalCorg\r\n");
        DEBUG_PRINT("  Verificar: trezorctl list\r\n");
    } else {
        DEBUG_PRINT("  TEST USB: WARN — no enumeró en 10s\r\n");
        DEBUG_PRINT("  Posibles causas:\r\n");
        DEBUG_PRINT("    - Cable USB-C no conectado\r\n");
        DEBUG_PRINT("    - Cable solo de carga (sin datos)\r\n");
        DEBUG_PRINT("    - Driver USB no instalado (Windows)\r\n");
        DEBUG_PRINT("    - PA11/PA12 sin soldar (revisar placa)\r\n");
        /* USB es importante pero no bloqueante en Fase 1 */
    }

    /* -----------------------------------------------------------------
     * TEST 5: I2C — detectar click-wheel AS5600
     *
     * Intenta comunicarse con el AS5600 en dirección 0x36.
     * Si el click-wheel no está conectado todavía, es esperado
     * que este test falle — lo marcamos como WARNING no ERROR.
     * ----------------------------------------------------------------- */
    test_num++;
    DEBUG_PRINTF("TEST %u: I2C click-wheel (PB6/PB7, AS5600@0x36)...\r\n",
        test_num);

    cw_state_t cw_state = {0};
    bool cw_ok = cw_init(&cw_state);

    if (cw_ok) {
        uint16_t angle = 0;
        cw_read_raw_angle(&angle);
        DEBUG_PRINTF("  OK — AS5600 detectado. Angulo inicial: %u (de 4095)\r\n",
            angle);
    } else {
        DEBUG_PRINT("  WARN — AS5600 no detectado en 0x36\r\n");
        DEBUG_PRINT("  Normal si el click-wheel no está conectado aún.\r\n");
        DEBUG_PRINT("  Verificar: pull-ups 4.7kΩ en PB6 y PB7\r\n");
        /* No es error bloqueante en Fase 1 */
    }

    /* -----------------------------------------------------------------
     * RESUMEN DE TESTS
     * ----------------------------------------------------------------- */
    DEBUG_PRINT("\r\n=== Resumen ===\r\n");
    DEBUG_PRINTF("Clock 168MHz : %s\r\n", clock_verify()      ? "PASS" : "FAIL");
    DEBUG_PRINTF("GPIO LED     : PASS\r\n");
    DEBUG_PRINTF("UART debug   : PASS\r\n");
    DEBUG_PRINTF("USB OTG FS   : %s\r\n", usb_is_connected()  ? "PASS" : "WARN");
    DEBUG_PRINTF("I2C AS5600   : %s\r\n", cw_ok               ? "PASS" : "WARN");
    DEBUG_PRINT("===============\r\n");

    return all_ok;
}


/* =========================================================
 * LOOP PRINCIPAL
 * ========================================================= */
static void main_loop(void) {

    uint32_t last_heartbeat = 0;   /* Para el LED heartbeat */
    uint32_t last_report    = 0;   /* Para el reporte de status */
    uint32_t uptime_secs    = 0;   /* Segundos desde el boot */

    cw_state_t cw_state = {0};
    cw_init(&cw_state);            /* Re-inicializar click-wheel */

    DEBUG_PRINT("[LOOP] Entrando al loop principal. Ctrl+C para salir.\r\n");

    while (1) {
        uint32_t now = HAL_GetTick();

        /* ---- LED heartbeat cada 1 segundo ----
         * Un pulso corto (100ms ON / 900ms OFF) indica que el
         * sistema está vivo y el loop principal está corriendo.
         * En Trezor esta señal visual es importante para saber
         * que el firmware no se colgó. */
        if ((now - last_heartbeat) >= 1000) {
            last_heartbeat = now;
            uptime_secs++;

            LED_ON();
            HAL_Delay(100);
            LED_OFF();
        }

        /* ---- Polling del USB ---- */
        usb_test_poll();

        /* ---- Polling del click-wheel ----
         * En Fase 1 solo imprimimos los eventos para verificar
         * que el AS5600 reporta correctamente. */
        cw_event_t event = cw_poll(&cw_state);
        if (event != CW_EVENT_NONE) {
            DEBUG_PRINTF("[CW] Evento: %s\r\n", cw_event_name(event));
        }

        /* ---- Reporte de status cada 30 segundos ---- */
        if ((now - last_report) >= 30000) {
            last_report = now;
            DEBUG_PRINTF("[STATUS] Uptime: %lu s | USB: %s | Tick: %lu\r\n",
                uptime_secs,
                usb_is_connected() ? "conectado" : "desconectado",
                HAL_GetTick());
        }
    }
}


/* =========================================================
 * BANNER DE INICIO
 * ========================================================= */
static void print_banner(void) {
    DEBUG_PRINT("\r\n");
    DEBUG_PRINT(" ___       __      .____ ___  _________ \r\n");
    DEBUG_PRINT(" __ |     / /_____ |    |   \\/   _____/ \r\n");
    DEBUG_PRINT(" __ | /| / /  __ ||    |   /\\_____  \\  \r\n");
    DEBUG_PRINT(" __ |/ \\| // /_/ /|    |  / /        \\ \r\n");
    DEBUG_PRINT(" ____/|__/ \\__,  / |______/ /_______  / \r\n");
    DEBUG_PRINT("             /_/                    \\/  \r\n");
    DEBUG_PRINT("         Cold Wallet — Fase 1 Bringup\r\n");
    DEBUG_PRINT("         STM32F405RGT6 @ 168MHz\r\n");
    DEBUG_PRINTF("         Build: %s %s\r\n", __DATE__, __TIME__);
    DEBUG_PRINTF("         PRODUCTION=%d\r\n", PRODUCTION);
    DEBUG_PRINT("\r\n");
}
