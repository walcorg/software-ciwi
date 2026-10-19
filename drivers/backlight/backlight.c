/**
 * @file    backlight.c
 * @brief   Control PWM de backlight — WalCorg Fase 4
 *
 * TIM3_CH4 en PB1 genera la señal PWM.
 *
 * CÁLCULO DEL TIMER:
 *   PCLK1 = 42MHz → TIM3 clock = 84MHz (×2 por el APB1 timer multiplier)
 *   Para PWM de 1kHz con resolución de 256 pasos:
 *     Prescaler = 84MHz / (1kHz × 256) - 1 = 328 - 1 = 327
 *     Period    = 255 (ARR = 255 → 256 pasos de 0 a 255)
 *     CCR4      = duty cycle (0 = apagado, 255 = máximo)
 *
 * @license GNU GPL v3
 */

#include "backlight.h"
#include "uart_debug.h"
#include <stdlib.h>   /* abs() */

/* =========================================================
 * ESTADO INTERNO
 * ========================================================= */

TIM_HandleTypeDef htim3;       /* Handle global del TIM3            */

static uint8_t  _brightness   = 0;    /* Brillo actual (0-255)       */
static bool     _initialized  = false;
static uint32_t _last_activity = 0;   /* Timestamp de última actividad */
static bool     _dimmed        = false; /* true si estamos en modo dim */
static bool     _off_by_timeout = false;/* true si apagado por timeout */


/* =========================================================
 * FUNCIONES PRIVADAS
 * ========================================================= */

/**
 * _set_pwm_duty — Escribir el duty cycle directamente al registro del timer
 *
 * CCR4 (Capture/Compare Register 4) controla el duty cycle:
 *   CCR4 = 0   → 0%   duty → backlight apagado
 *   CCR4 = 255 → 100% duty → backlight máximo
 *
 * Usamos acceso directo al registro para máxima velocidad —
 * más eficiente que HAL_TIM_PWM_Start() en el loop del fade.
 */
static void _set_pwm_duty(uint8_t duty) {
    if (!_initialized) return;
    /* TIM3->CCR4 controla el duty cycle del canal 4 */
    htim3.Instance->CCR4 = duty;
}


/* =========================================================
 * INICIALIZACIÓN
 * ========================================================= */

/**
 * backlight_init — Configurar TIM3_CH4 como PWM en PB1
 *
 * Secuencia:
 *   1. Habilitar clock de GPIOB y TIM3
 *   2. Configurar PB1 como AF2 (TIM3_CH4)
 *   3. Configurar TIM3 en modo PWM con 1kHz y 256 pasos
 *   4. Iniciar el canal PWM
 *   5. Encender el backlight con fade suave
 */
bool backlight_init(void) {
    DEBUG_PRINT("[BL] Inicializando backlight PWM (TIM3_CH4, PB1)...\r\n");

    /* ---- 1. GPIO: PB1 como TIM3_CH4 (AF2) ---- */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_1;
    gpio.Mode      = GPIO_MODE_AF_PP;   /* Alternate Function Push-Pull */
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_LOW;  /* 1kHz no necesita alta velocidad */
    gpio.Alternate = GPIO_AF2_TIM3;        /* AF2 = TIM3 en STM32F405 */
    HAL_GPIO_Init(GPIOB, &gpio);

    /* ---- 2. TIM3: configurar el timer base ---- */
    __HAL_RCC_TIM3_CLK_ENABLE();

    /* TIM3 clock = PCLK1 × 2 = 42MHz × 2 = 84MHz
     * (cuando el divisor APB1 > 1, los timers en APB1 reciben ×2)
     *
     * Para PWM de 1kHz con 256 pasos:
     *   Prescaler = 84,000,000 / (1,000 × 256) - 1 = 327
     *   Period    = 255 (ARR, el período del contador)
     *   Frecuencia real = 84MHz / (328 × 256) = 1001.5Hz ≈ 1kHz ✓ */
    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 327;   /* 84MHz / 328 = 256kHz tick */
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 255;   /* 256kHz / 256 = 1kHz PWM */
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) {
        DEBUG_PRINT("[BL] ERROR: HAL_TIM_PWM_Init falló.\r\n");
        return false;
    }

    /* ---- 3. Configurar el canal 4 en modo PWM ---- */
    TIM_OC_InitTypeDef oc_config = {0};
    oc_config.OCMode      = TIM_OCMODE_PWM1;
    /* PWM1: el canal está activo mientras el contador < CCR4
     * duty% = CCR4 / (Period+1) × 100%
     * CCR4=0   → 0%   → apagado
     * CCR4=255 → 100% → máximo brillo */
    oc_config.Pulse       = 0;          /* Empezar apagado */
    oc_config.OCPolarity  = TIM_OCPOLARITY_HIGH;  /* Activo en HIGH */
    oc_config.OCFastMode  = TIM_OCFAST_DISABLE;

    if (HAL_TIM_PWM_ConfigChannel(&htim3, &oc_config, TIM_CHANNEL_4) != HAL_OK) {
        DEBUG_PRINT("[BL] ERROR: HAL_TIM_PWM_ConfigChannel falló.\r\n");
        return false;
    }

    /* ---- 4. Iniciar el canal PWM ---- */
    if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4) != HAL_OK) {
        DEBUG_PRINT("[BL] ERROR: HAL_TIM_PWM_Start falló.\r\n");
        return false;
    }

    _initialized    = true;
    _brightness     = 0;
    _last_activity  = HAL_GetTick();
    _dimmed         = false;
    _off_by_timeout = false;

    /* ---- 5. Fade de encendido inicial ---- */
    backlight_fade_to(BL_DEFAULT_BRIGHTNESS, BL_FADE_DURATION_MS);

    DEBUG_PRINTF("[BL] Inicializado. Brillo: %d/255. PWM: 1kHz en TIM3_CH4\r\n",
        _brightness);
    return true;
}


/* =========================================================
 * CONTROL DE BRILLO
 * ========================================================= */

/**
 * backlight_set — Establecer brillo instantáneo
 *
 * Escribe directamente al CCR4 del TIM3 sin fade.
 * @param brightness  0=apagado, 255=máximo
 */
void backlight_set(uint8_t brightness) {
    _brightness = brightness;
    _set_pwm_duty(brightness);
}

/**
 * backlight_fade_to — Transición suave de brillo
 *
 * Interpola linealmente desde el brillo actual hasta
 * el brillo destino en la duración especificada.
 *
 * El delay entre pasos = duration_ms / BL_FADE_STEPS.
 * Con 30 pasos y 300ms → 10ms por paso → suave y visible.
 *
 * @param brightness    Brillo destino (0-255)
 * @param duration_ms   Duración de la transición en ms
 */
void backlight_fade_to(uint8_t brightness, uint32_t duration_ms) {
    if (!_initialized) return;

    int start  = (int)_brightness;
    int end    = (int)brightness;
    int steps  = BL_FADE_STEPS;
    int delay  = (int)(duration_ms / steps);

    if (delay < 1) delay = 1;

    for (int i = 0; i <= steps; i++) {
        /* Interpolación lineal: start + (end - start) × i / steps */
        int current = start + (end - start) * i / steps;
        if (current < 0)   current = 0;
        if (current > 255) current = 255;

        _set_pwm_duty((uint8_t)current);
        HAL_Delay((uint32_t)delay);
    }

    _brightness = brightness;
    _set_pwm_duty(brightness);  /* Asegurar valor final exacto */
}

/**
 * backlight_on — Encender con fade suave
 */
void backlight_on(void) {
    if (_brightness >= BL_DEFAULT_BRIGHTNESS) return;
    backlight_fade_to(BL_DEFAULT_BRIGHTNESS, BL_FADE_DURATION_MS);
    _dimmed         = false;
    _off_by_timeout = false;
}

/**
 * backlight_off — Apagar con fade suave
 */
void backlight_off(void) {
    if (_brightness == 0) return;
    backlight_fade_to(0, BL_FADE_DURATION_MS);
}

/**
 * backlight_get — Retornar el brillo actual
 */
uint8_t backlight_get(void) {
    return _brightness;
}


/* =========================================================
 * AUTO-DIM POR INACTIVIDAD
 * ========================================================= */

/**
 * backlight_activity — Notificar actividad del usuario
 *
 * Llamar en cada evento de input_manager (NAV, CLICK, etc.)
 * para resetear los timers de auto-dim y auto-off.
 *
 * Si el backlight estaba dimmed o apagado, lo restaura
 * con un fade suave al brillo por defecto.
 */
void backlight_activity(void) {
    _last_activity = HAL_GetTick();

    if (_off_by_timeout) {
        /* Restaurar desde apagado por timeout */
        backlight_fade_to(BL_DEFAULT_BRIGHTNESS, BL_FADE_DURATION_MS / 2);
        _off_by_timeout = false;
        _dimmed         = false;
    } else if (_dimmed) {
        /* Restaurar desde dim */
        backlight_fade_to(BL_DEFAULT_BRIGHTNESS, BL_FADE_DURATION_MS / 3);
        _dimmed = false;
    }
}

/**
 * backlight_poll — Verificar timers de inactividad
 *
 * Llamar en el loop principal, fuera del menú.
 * El loop del menú consume tiempo bloqueando — el auto-dim
 * se verifica aquí cuando el menú no está activo.
 *
 * Lógica:
 *   t < DIM_TIMEOUT  → brillo normal
 *   DIM_TIMEOUT ≤ t < OFF_TIMEOUT → dim (BL_DIM_BRIGHTNESS)
 *   t ≥ OFF_TIMEOUT  → apagado (0)
 */
void backlight_poll(void) {
    if (!_initialized) return;

    uint32_t elapsed_s = (HAL_GetTick() - _last_activity) / 1000;

    if (elapsed_s >= BL_OFF_TIMEOUT_S && !_off_by_timeout) {
        /* Auto-off: apagar completamente por inactividad prolongada */
        DEBUG_PRINTF("[BL] Auto-off (%lu s de inactividad)\r\n", elapsed_s);
        backlight_fade_to(0, BL_FADE_DURATION_MS);
        _off_by_timeout = true;
        _dimmed         = false;

    } else if (elapsed_s >= BL_DIM_TIMEOUT_S && !_dimmed && !_off_by_timeout) {
        /* Auto-dim: reducir brillo por inactividad */
        DEBUG_PRINTF("[BL] Auto-dim (%lu s de inactividad)\r\n", elapsed_s);
        backlight_fade_to(BL_DIM_BRIGHTNESS, BL_FADE_DURATION_MS * 2);
        _dimmed = true;
    }
}
