/**
 * @file    backlight.h / backlight.c
 * @brief   Control de brillo del display via PWM — WALCORG Fase 4
 *
 * Controla el backlight del TFT via TIM3_CH4 en PB1.
 *
 * HARDWARE:
 *   PB1 → TIM3_CH4 (Alternate Function 2)
 *   Frecuencia PWM: 1kHz (por encima de la percepción visual)
 *   Duty cycle: 0% = apagado, 100% = máximo brillo
 *
 * CARACTERÍSTICAS:
 *   - Fade suave al encender/apagar (evita parpadeo)
 *   - Auto-dim después de inactividad configurable
 *   - Auto-off después de inactividad prolongada
 *   - Control de brillo 0-255 (mapeado a 0-100% duty)
 *
 * INTEGRACIÓN CON DISPLAY:
 *   En Fase 2, backlight era ON/OFF via GPIO.
 *   En Fase 4, se usa PWM para control fino de brillo.
 *   La función oledSetBrightness() en display.c ahora
 *   llama a backlight_set() en vez de GPIO directo.
 *
 * @license GNU GPL v3
 */

#ifndef BACKLIGHT_H
#define BACKLIGHT_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"

/* =========================================================
 * CONFIGURACIÓN
 * ========================================================= */

/** Frecuencia del PWM en Hz (1kHz = sin parpadeo visible) */
#define BL_PWM_FREQ_HZ          1000

/** Brillo predeterminado al encender (0-255) */
#define BL_DEFAULT_BRIGHTNESS   200

/** Brillo de modo dim — bajo uso (0-255) */
#define BL_DIM_BRIGHTNESS       60

/** Segundos de inactividad antes del dim automático */
#define BL_DIM_TIMEOUT_S        30

/** Segundos de inactividad antes de apagar el backlight */
#define BL_OFF_TIMEOUT_S        120

/** Duración del fade de encendido/apagado en ms */
#define BL_FADE_DURATION_MS     300

/** Pasos del fade (mayor = más suave, más lento) */
#define BL_FADE_STEPS           30

/* Handle global del timer — accedido desde display.c */
extern TIM_HandleTypeDef htim3;


/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Inicializa el PWM del backlight en TIM3_CH4 (PB1).
 *
 * Configura el timer TIM3 con frecuencia de 1kHz,
 * inicia el canal PWM y enciende el backlight al
 * brillo por defecto con un fade suave.
 *
 * @return true si la inicialización fue correcta
 */
bool backlight_init(void);

/**
 * @brief Establece el brillo inmediatamente (sin fade).
 * @param brightness  Brillo de 0 (apagado) a 255 (máximo)
 */
void backlight_set(uint8_t brightness);

/**
 * @brief Establece el brillo con transición suave (fade).
 * @param brightness    Brillo destino (0-255)
 * @param duration_ms   Duración de la transición en ms
 */
void backlight_fade_to(uint8_t brightness, uint32_t duration_ms);

/**
 * @brief Enciende el backlight con fade desde apagado.
 * Usa BL_DEFAULT_BRIGHTNESS y BL_FADE_DURATION_MS.
 */
void backlight_on(void);

/**
 * @brief Apaga el backlight con fade hasta cero.
 */
void backlight_off(void);

/**
 * @brief Retorna el brillo actual (0-255).
 */
uint8_t backlight_get(void);

/**
 * @brief Notifica actividad del usuario — resetea el timer de auto-dim.
 * Llamar en cada evento de input_manager.
 */
void backlight_activity(void);

/**
 * @brief Polling del auto-dim — llamar en el loop principal.
 * Maneja el dim y apagado automático por inactividad.
 */
void backlight_poll(void);

#endif /* BACKLIGHT_H */
