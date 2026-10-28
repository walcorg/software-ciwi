/**
 * @file    usb_hid.h
 * @brief   Stack USB HID para trezorctl — WALCORG Fase 5
 *
 * Implementa el protocolo USB HID de Trezor:
 *   - Descriptores USB (Device, Configuration, HID, Report)
 *   - Endpoint HID IN/OUT de 64 bytes
 *   - Cola de mensajes protobuf entrantes/salientes
 *   - Framing de mensajes Trezor (header + chunks de 63 bytes)
 *
 * PROTOCOLO DE MENSAJES TREZOR:
 *
 *   Cada mensaje se divide en chunks de 63 bytes:
 *
 *   Chunk 1 (primer paquete):
 *     Byte 0:     '?' (0x3F) — marcador de primer chunk
 *     Byte 1-2:   '#' '#' (0x23 0x23) — magic
 *     Byte 3-4:   message_type (big-endian uint16)
 *     Byte 5-8:   message_length (big-endian uint32)
 *     Byte 9-63:  primeros 55 bytes del payload
 *
 *   Chunks siguientes:
 *     Byte 0:     '?' (0x3F) — marcador de continuación
 *     Byte 1-63:  siguientes 63 bytes del payload
 *
 *   Cada chunk se encapsula en un reporte HID de 64 bytes.
 *
 * TIPOS DE MENSAJE (subset de los que usa trezorctl):
 *   0x0001: Initialize
 *   0x0002: Ping
 *   0x0011: GetPublicKey
 *   0x001A: GetAddress
 *   0x0055: SignTx
 *   0x001C: Features (respuesta a Initialize)
 *   0x0002: Success
 *   0x0003: Failure
 *
 * @license GNU GPL v3
 */

#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"

/* =========================================================
 * CONSTANTES DEL PROTOCOLO
 * ========================================================= */

/** Tamaño de un reporte HID (fijo en el protocolo Trezor) */
#define HID_REPORT_SIZE         64

/** Tamaño útil por chunk (sin el byte '?') */
#define HID_CHUNK_PAYLOAD       63

/** Marcador de inicio/continuación de mensaje */
#define HID_MARKER              0x3F   /* '?' */

/** Magic bytes del protocolo Trezor */
#define HID_MAGIC_1             0x23   /* '#' */
#define HID_MAGIC_2             0x23   /* '#' */

/** Tamaño máximo de un mensaje protobuf completo */
#define HID_MAX_MSG_SIZE        2048

/** Profundidad de las colas de mensajes */
#define HID_QUEUE_DEPTH         4

/* VID/PID WALCORG (usar pid.codes para producción) */
#define USB_VID                 0x1209
#define USB_PID                 0x53C1

/* =========================================================
 * TIPOS DE MENSAJE TREZOR (subset)
 * ========================================================= */
typedef enum {
    MSG_INITIALIZE      = 0,
    MSG_PING            = 1,
    MSG_SUCCESS         = 2,
    MSG_FAILURE         = 3,
    MSG_FEATURES        = 17,   /* 0x11 */
    MSG_GET_PUBLIC_KEY  = 11,   /* 0x0B */
    MSG_PUBLIC_KEY      = 12,   /* 0x0C */
    MSG_GET_ADDRESS     = 29,   /* 0x1D */
    MSG_ADDRESS         = 30,   /* 0x1E */
    MSG_SIGN_TX         = 15,   /* 0x0F */
    MSG_TX_REQUEST      = 21,   /* 0x15 */
    MSG_TX_ACK          = 22,   /* 0x16 */
    MSG_BUTTON_REQUEST  = 26,   /* 0x1A */
    MSG_BUTTON_ACK      = 27,   /* 0x1B */
    MSG_PIN_REQUEST     = 18,   /* 0x12 */
    MSG_PIN_ACK         = 19,   /* 0x13 */
    MSG_CANCEL          = 20,   /* 0x14 */
    MSG_WIPE_DEVICE     = 5,    /* 0x05 */
    MSG_CHANGE_PIN      = 4,    /* 0x04 */
    MSG_RESET_DEVICE    = 14,   /* 0x0E */
    MSG_ENTROPY_REQUEST = 35,   /* 0x23 */
    MSG_ENTROPY_ACK     = 36,   /* 0x24 */
} usb_msg_type_t;

/* =========================================================
 * ESTRUCTURAS
 * ========================================================= */

/** Un mensaje recibido o a enviar */
typedef struct {
    uint16_t  type;                    /**< Tipo de mensaje      */
    uint32_t  length;                  /**< Longitud del payload */
    uint8_t   payload[HID_MAX_MSG_SIZE]; /**< Datos del mensaje  */
    bool      valid;                   /**< true si tiene datos  */
} usb_message_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Inicializa el stack USB HID.
 *
 * Configura los descriptores USB, registra el dispositivo
 * HID y habilita las interrupciones OTG_FS.
 *
 * @return true si el USB enumeró correctamente
 */
bool usb_hid_init(void);

/**
 * @brief Polling del USB — llamar en el loop principal.
 *
 * Procesa los datos recibidos del host y ensambla mensajes
 * completos de múltiples chunks HID.
 */
void usb_hid_poll(void);

/**
 * @brief Verifica si hay un mensaje completo recibido.
 * @return true si hay un mensaje en la cola de entrada
 */
bool usb_hid_has_message(void);

/**
 * @brief Lee el siguiente mensaje de la cola de entrada.
 *
 * @param msg  Buffer donde se copia el mensaje
 * @return     true si había un mensaje, false si la cola estaba vacía
 */
bool usb_hid_read_message(usb_message_t *msg);

/**
 * @brief Envía un mensaje al host.
 *
 * Divide el mensaje en chunks de 63 bytes y los envía
 * como reportes HID de 64 bytes.
 *
 * @param type     Tipo del mensaje de respuesta
 * @param payload  Datos del mensaje
 * @param length   Longitud del payload en bytes
 * @return         true si el envío fue exitoso
 */
bool usb_hid_send_message(usb_msg_type_t type,
                           const uint8_t *payload, uint32_t length);

/**
 * @brief Envía un mensaje de éxito simple.
 * @param msg_text  Texto del mensaje (puede ser NULL)
 */
bool usb_hid_send_success(const char *msg_text);

/**
 * @brief Envía un mensaje de error.
 * @param code      Código de error
 * @param msg_text  Descripción del error
 */
bool usb_hid_send_failure(uint32_t code, const char *msg_text);

/**
 * @brief Verifica si el host está conectado.
 * @return true si hay conexión USB activa
 */
bool usb_hid_is_connected(void);

/**
 * @brief Retorna el nombre del tipo de mensaje (debug).
 */
const char *usb_msg_type_name(usb_msg_type_t type);

/** Handle global del PCD — accedido desde stm32f4xx_it.c */
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

#endif /* USB_HID_H */