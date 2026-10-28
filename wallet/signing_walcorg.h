/**
 * @file    signing_walcorg.h
 * @brief   Protocolo interactivo de firma de TX Bitcoin — WALCORG Fase 5
 *
 * API pública del módulo de firma. El FSM llama estas funciones
 * en respuesta a los mensajes USB de trezorctl:
 *
 *   MSG_SIGN_TX  → signing_init()
 *   MSG_TX_ACK   → signing_txack()
 *   MSG_CANCEL   → signing_cancel()
 *
 * @license GNU GPL v3
 */

#ifndef SIGNING_WALCORG_H
#define SIGNING_WALCORG_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Inicializa el proceso de firma al recibir SignTx.
 *
 * Parsea el número de inputs/outputs de la TX y envía
 * el primer TxRequest al host pidiendo el input 0.
 *
 * @param payload  Payload del mensaje MSG_SIGN_TX
 * @param length   Longitud del payload
 */
void signing_init(const uint8_t *payload, uint32_t length);

/**
 * @brief Procesa un TxAck del host (datos de input u output).
 *
 * Acumula los datos de la TX, hashea cada elemento,
 * y cuando los tiene todos muestra la confirmación de UI,
 * pide el PIN y ejecuta la firma ECDSA.
 *
 * @param payload  Payload del mensaje MSG_TX_ACK
 * @param length   Longitud del payload
 */
void signing_txack(const uint8_t *payload, uint32_t length);

/**
 * @brief Cancela el proceso de firma en curso.
 *
 * Limpia el estado interno y envía Failure al host.
 */
void signing_cancel(void);

/**
 * @brief Verifica si hay un proceso de firma activo en este momento.
 * @return true si signing_init() fue llamado y aún no terminó
 */
bool signing_is_active(void);

#endif /* SIGNING_WALCORG_H */
