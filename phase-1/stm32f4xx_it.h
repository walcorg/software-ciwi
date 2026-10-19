/**
 * @file    stm32f4xx_it.h
 * @brief   Declaraciones de los manejadores de interrupción — WalCorg Fase 1
 *
 * En STM32, cada interrupción tiene un nombre fijo definido en la tabla
 * de vectores (startup_stm32f405xx.s). Si defines una función con ese
 * nombre exacto, el hardware la llama automáticamente cuando ocurre
 * la interrupción correspondiente.
 *
 * Este header declara los handlers que implementamos en stm32f4xx_it.c
 * Los handlers NO declarados aquí siguen siendo llamados si ocurre
 * la interrupción — pero ejecutan el handler "weak" del startup que
 * por defecto es un loop infinito (útil para detectar interrupciones
 * no manejadas durante debug).
 *
 * @license GNU GPL v3
 */

#ifndef STM32F4XX_IT_H
#define STM32F4XX_IT_H

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * EXCEPCIONES DEL NÚCLEO CORTEX-M4
 *
 * Estas son excepciones del procesador ARM, no periféricos STM32.
 * Siempre deben estar definidas — son la red de seguridad del sistema.
 * ========================================================= */

/**
 * NMI = Non-Maskable Interrupt
 * Interrupción que NO se puede deshabilitar con __disable_irq().
 * En STM32F4 la genera el reloj (CSS) si el cristal externo falla.
 * Usamos esto para detectar si el HSE (cristal 8MHz) falló durante
 * la inicialización del clock.
 */
void NMI_Handler(void);

/**
 * HardFault = Error grave del procesador
 * Se dispara cuando el CPU encuentra una instrucción ilegal,
 * acceso a memoria inválido, división por cero, etc.
 * En desarrollo: llegar aquí significa que hay un bug serio.
 * En producción: debe triggear un reset del sistema.
 */
void HardFault_Handler(void);

/**
 * MemManage = Fallo del Memory Protection Unit (MPU)
 * Se dispara cuando el código intenta acceder a una región
 * de memoria protegida. Trezor usa la MPU para proteger las
 * claves privadas — cualquier acceso no autorizado llega aquí.
 */
void MemManage_Handler(void);

/**
 * BusFault = Error en el bus AHB/APB
 * Acceso a una dirección de memoria que no existe o periférico
 * que no está habilitado. Común cuando se olvida habilitar el
 * clock de un periférico antes de usarlo.
 */
void BusFault_Handler(void);

/**
 * UsageFault = Error de uso del CPU
 * División por cero, instrucción no alineada, instrucción
 * Thumb inválida, etc.
 */
void UsageFault_Handler(void);

/**
 * SVC = SuperVisor Call
 * Llamada explícita al sistema mediante la instrucción SVC.
 * En Trezor se usa para aislar el bootloader del firmware.
 * En Fase 1 no lo usamos, pero debe estar declarado.
 */
void SVC_Handler(void);

/**
 * DebugMon = Debug Monitor
 * Maneja breakpoints en modo monitor (sin halt del CPU).
 * Relevante cuando GDB está conectado.
 */
void DebugMon_Handler(void);

/**
 * PendSV = Pendable Service Call
 * En sistemas con RTOS: usado para cambio de contexto.
 * En bare-metal: raramente usado, pero debe estar definido.
 */
void PendSV_Handler(void);

/**
 * SysTick = Temporizador del sistema (cada 1ms)
 * LA interrupción más importante en Fase 1.
 * HAL_GetTick() y HAL_Delay() dependen de que este handler
 * incremente el contador cada 1ms exactamente.
 */
void SysTick_Handler(void);

/* =========================================================
 * INTERRUPCIONES DE PERIFÉRICOS STM32F405
 * (solo las que usamos en Fase 1)
 * ========================================================= */

/**
 * USART1 = Interrupción de la UART de debug (PA9/PA10)
 * Necesaria para recibir comandos por UART durante desarrollo.
 * En Fase 1 solo usamos TX (transmitir), pero declaramos la IRQ
 * por si necesitamos RX más adelante.
 */
void USART1_IRQHandler(void);

/**
 * OTG_FS = USB Full Speed (PA11/PA12)
 * Manejador de interrupciones del controlador USB.
 * En Fase 1 lo usamos para verificar que el chip enumera como
 * dispositivo USB correctamente.
 */
void OTG_FS_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32F4XX_IT_H */
