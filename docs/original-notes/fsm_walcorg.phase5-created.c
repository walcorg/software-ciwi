/**
 * @file    fsm_walcorg.c
 * @brief   Máquina de estados del wallet — WalCorg Fase 5
 *
 * ARCHIVO ORIGINAL: legacy/firmware/fsm.c del repo trezor-firmware
 *
 * CONTEXTO:
 *   fsm.c es el corazón del firmware de Trezor. Recibe mensajes
 *   protobuf de trezorctl via USB, los decodifica, interactúa con
 *   el usuario via las pantallas de UI, llama a las funciones crypto
 *   y retorna las respuestas al host.
 *
 * ESTRATEGIA DE ADAPTACIÓN:
 *   En vez de modificar el fsm.c original (que es complejo y tiene
 *   muchas dependencias protobuf), creamos una capa de dispatch
 *   propia que:
 *     1. Recibe mensajes de usb_hid.c
 *     2. Los interpreta sin protobuf completo (solo los campos necesarios)
 *     3. Llama a wallet_core.c para operaciones crypto
 *     4. Muestra confirmación en UI via ui_screens.c
 *     5. Retorna la respuesta al host via usb_hid.c
 *
 * MENSAJES IMPLEMENTADOS:
 *   Initialize    → Retorna Features (info del dispositivo)
 *   Ping          → Retorna Success con el mismo mensaje
 *   GetAddress    → Deriva y retorna dirección Bitcoin
 *   SignTx        → Solicita confirmación, firma y retorna TX
 *   WipeDevice    → Pide confirmación, borra el wallet
 *   ResetDevice   → Inicia el proceso de creación de nuevo wallet
 *
 * @license GNU GPL v3
 */

#include "usb_hid.h"
#include "wallet_core.h"
#include "storage_hal.h"
#include "ui_screens.h"
#include "ui_components.h"
#include "ui_theme.h"
#include "display_tft.h"
#include "input_manager.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

/* =========================================================
 * PROTOBUF MÍNIMO
 *
 * En vez de incluir el generador protobuf completo de Trezor,
 * implementamos solo el parsing/serialización de los campos
 * que necesitamos. Esto reduce la complejidad enormemente.
 *
 * Wire types protobuf:
 *   0 = varint
 *   1 = 64-bit
 *   2 = length-delimited (string, bytes, embedded messages)
 *   5 = 32-bit
 * ========================================================= */

/**
 * _pb_read_varint — Leer un varint protobuf
 * @return Número de bytes consumidos, -1 si error
 */
static int _pb_read_varint(const uint8_t *buf, uint32_t len,
                            uint64_t *val_out) {
    uint64_t val = 0;
    int shift = 0;
    for (uint32_t i = 0; i < len && i < 10; i++) {
        val |= ((uint64_t)(buf[i] & 0x7F)) << shift;
        shift += 7;
        if (!(buf[i] & 0x80)) {
            *val_out = val;
            return (int)(i + 1);
        }
    }
    return -1;
}

/**
 * _pb_write_varint — Escribir un varint protobuf
 * @return Número de bytes escritos
 */
static int _pb_write_varint(uint8_t *buf, uint64_t val) {
    int i = 0;
    do {
        buf[i] = (uint8_t)(val & 0x7F);
        val >>= 7;
        if (val) buf[i] |= 0x80;
        i++;
    } while (val);
    return i;
}

/**
 * _pb_write_string — Escribir campo string protobuf (field, type=2)
 * @return Bytes escritos
 */
static int _pb_write_string(uint8_t *buf, uint8_t field_num,
                              const char *str) {
    if (!str || !str[0]) return 0;
    int pos = 0;
    uint32_t str_len = strlen(str);
    /* Tag: (field_num << 3) | 2 */
    pos += _pb_write_varint(buf + pos, ((uint64_t)field_num << 3) | 2);
    pos += _pb_write_varint(buf + pos, str_len);
    memcpy(buf + pos, str, str_len);
    pos += str_len;
    return pos;
}

/**
 * _pb_write_bool — Escribir campo bool protobuf
 */
static int _pb_write_bool(uint8_t *buf, uint8_t field_num, bool val) {
    int pos = 0;
    pos += _pb_write_varint(buf + pos, ((uint64_t)field_num << 3) | 0);
    pos += _pb_write_varint(buf + pos, val ? 1 : 0);
    return pos;
}

/**
 * _pb_write_uint32 — Escribir campo uint32 protobuf
 */
static int _pb_write_uint32(uint8_t *buf, uint8_t field_num, uint32_t val) {
    int pos = 0;
    pos += _pb_write_varint(buf + pos, ((uint64_t)field_num << 3) | 0);
    pos += _pb_write_varint(buf + pos, val);
    return pos;
}


/* =========================================================
 * HANDLERS DE MENSAJES
 * ========================================================= */

/**
 * _handle_initialize — Responder a Initialize con Features
 *
 * trezorctl envía Initialize al conectarse para obtener info
 * del dispositivo. Respondemos con un mensaje Features que
 * incluye el modelo, versión y capacidades del wallet.
 *
 * Features fields usados:
 *   field 1:  vendor (string)         = "walcorg.com"
 *   field 2:  major_version (uint32)  = 1
 *   field 3:  minor_version (uint32)  = 0
 *   field 4:  patch_version (uint32)  = 0
 *   field 5:  bootloader_mode (bool)  = false
 *   field 11: model (string)          = "WalCorg"
 *   field 12: firmware_present (bool) = true
 *   field 13: initialized (bool)      = wallet_is_initialized()
 */
static void _handle_initialize(void) {
    DEBUG_PRINT("[FSM] Initialize → Features\r\n");

    uint8_t resp[256];
    int pos = 0;

    pos += _pb_write_string(resp + pos, 1,  "walcorg.com");
    pos += _pb_write_uint32(resp + pos, 2,  1);   /* major */
    pos += _pb_write_uint32(resp + pos, 3,  0);   /* minor */
    pos += _pb_write_uint32(resp + pos, 4,  0);   /* patch */
    pos += _pb_write_bool  (resp + pos, 5,  false); /* bootloader_mode */
    pos += _pb_write_string(resp + pos, 11, "WalCorg");
    pos += _pb_write_bool  (resp + pos, 12, true);  /* firmware_present */
    pos += _pb_write_bool  (resp + pos, 13, wallet_is_initialized());

    /* Añadir label del dispositivo */
    char label[STORAGE_LABEL_MAX_LEN];
    wallet_get_info(label, sizeof(label));
    if (label[0]) pos += _pb_write_string(resp + pos, 9, label);

    usb_hid_send_message(MSG_FEATURES, resp, (uint32_t)pos);
}

/**
 * _handle_ping — Responder a Ping con Success
 *
 * Ping puede incluir un mensaje que debe ser devuelto.
 * También puede solicitar confirmación de botón.
 */
static void _handle_ping(const usb_message_t *msg) {
    DEBUG_PRINT("[FSM] Ping → Success\r\n");

    /* Extraer el campo message del Ping (field 1, string) */
    char ping_msg[64] = {0};
    const uint8_t *p = msg->payload;
    uint32_t remaining = msg->length;

    while (remaining > 0) {
        uint64_t tag;
        int consumed = _pb_read_varint(p, remaining, &tag);
        if (consumed < 0) break;
        p += consumed; remaining -= consumed;

        uint8_t field = (uint8_t)(tag >> 3);
        uint8_t wtype = (uint8_t)(tag & 0x07);

        if (field == 1 && wtype == 2) {
            /* Campo message (string) */
            uint64_t str_len;
            consumed = _pb_read_varint(p, remaining, &str_len);
            if (consumed < 0) break;
            p += consumed; remaining -= consumed;
            if (str_len < sizeof(ping_msg)) {
                memcpy(ping_msg, p, str_len);
                ping_msg[str_len] = '\0';
            }
            p += str_len;
            remaining -= (uint32_t)str_len;
        } else {
            /* Skip campo desconocido */
            break;
        }
    }

    DEBUG_PRINTF("[FSM] Ping message: '%s'\r\n", ping_msg);
    usb_hid_send_success(ping_msg[0] ? ping_msg : "pong");
}

/**
 * _handle_get_address — Derivar y retornar dirección Bitcoin
 *
 * GetAddress fields:
 *   field 1: address_n (repeated uint32) = ruta de derivación
 *   field 2: coin_name (string)          = "Bitcoin"
 *   field 3: show_display (bool)         = mostrar en pantalla
 *   field 4: script_type (uint32)        = 0=P2PKH, 4=P2WPKH
 */
static void _handle_get_address(const usb_message_t *msg) {
    DEBUG_PRINT("[FSM] GetAddress\r\n");

    /* Extraer índice de la ruta (simplificado: solo el último nivel) */
    uint32_t addr_index = 0;
    bool show_display   = true;

    /* Parsear payload del mensaje */
    const uint8_t *p = msg->payload;
    uint32_t remaining = msg->length;
    while (remaining > 0) {
        uint64_t tag;
        int consumed = _pb_read_varint(p, remaining, &tag);
        if (consumed < 0) break;
        p += consumed; remaining -= consumed;

        uint8_t field = (uint8_t)(tag >> 3);
        uint8_t wtype = (uint8_t)(tag & 0x07);

        if (field == 1 && wtype == 2) {
            /* address_n: array de uint32 en formato packed */
            uint64_t arr_len;
            consumed = _pb_read_varint(p, remaining, &arr_len);
            if (consumed < 0) break;
            p += consumed; remaining -= consumed;
            /* El último elemento de la ruta es el índice de dirección */
            const uint8_t *arr_end = p + arr_len;
            while (p < arr_end) {
                uint64_t path_elem;
                consumed = _pb_read_varint(p, (uint32_t)(arr_end - p), &path_elem);
                if (consumed < 0) break;
                addr_index = (uint32_t)(path_elem & 0x7FFFFFFF);
                p += consumed;
            }
            remaining -= (uint32_t)arr_len;
        } else if (field == 3 && wtype == 0) {
            uint64_t val;
            consumed = _pb_read_varint(p, remaining, &val);
            if (consumed > 0) { show_display = (val != 0); p += consumed; remaining -= consumed; }
        } else {
            break;
        }
    }

    /* Pedir PIN al usuario si el wallet está inicializado */
    char pin_buf[10] = {0};
    if (storage_is_initialized()) {
        screen_result_t r = screen_pin_entry("PIN para GetAddress",
                                             pin_buf, sizeof(pin_buf));
        if (r != SCREEN_RESULT_OK) {
            usb_hid_send_failure(4, "Action cancelled");
            memset(pin_buf, 0, sizeof(pin_buf));
            return;
        }
    }

    /* Derivar dirección */
    wallet_address_t addr = {0};
    wallet_result_t result = wallet_get_address(
        pin_buf[0] ? pin_buf : "0000",
        addr_index, WALLET_COIN_BITCOIN, &addr
    );
    memset(pin_buf, 0, sizeof(pin_buf));

    if (result != WALLET_OK) {
        usb_hid_send_failure(1, wallet_result_str(result));
        return;
    }

    /* Mostrar dirección en pantalla si se pidió */
    if (show_display) {
        screen_receive(addr.address_bech32);
    }

    /* Serializar respuesta Address */
    uint8_t resp[128];
    int pos = 0;
    pos += _pb_write_string(resp + pos, 1, addr.address_bech32);

    usb_hid_send_message(MSG_ADDRESS, resp, (uint32_t)pos);
    DEBUG_PRINTF("[FSM] Dirección enviada: %s\r\n", addr.address_bech32);
}

/**
 * _handle_sign_tx — Confirmar y firmar transacción Bitcoin
 *
 * Flujo simplificado:
 *   1. Recibir SignTx con detalles de la TX
 *   2. Mostrar los detalles en pantalla para confirmación
 *   3. Si el usuario confirma, solicitar PIN
 *   4. Firmar el hash de la TX
 *   5. Retornar la firma al host
 *
 * NOTA: El protocolo completo de SignTx es interactivo
 * (múltiples TxRequest/TxAck). Aquí implementamos la versión
 * simplificada para demostración. El protocolo completo
 * se implementa en la Fase 6.
 */
static void _handle_sign_tx(const usb_message_t *msg) {
    DEBUG_PRINT("[FSM] SignTx\r\n");
    (void)msg;

    /* Mostrar pantalla de confirmación */
    screen_result_t r = screen_send_confirm(
        "0.001 BTC",
        "0.00001 BTC",
        "bc1q..." /* En producción: extraer del payload */
    );

    if (r != SCREEN_RESULT_OK) {
        usb_hid_send_failure(4, "Action cancelled");
        return;
    }

    /* Pedir PIN */
    char pin_buf[10] = {0};
    r = screen_pin_entry("PIN para firmar TX", pin_buf, sizeof(pin_buf));
    if (r != SCREEN_RESULT_OK) {
        usb_hid_send_failure(4, "Action cancelled");
        memset(pin_buf, 0, sizeof(pin_buf));
        return;
    }

    /* Hash de TX de prueba (en producción: derivado de la TX real) */
    uint8_t tx_hash[32];
    memset(tx_hash, 0xAB, sizeof(tx_hash));

    uint8_t signature[64];
    uint8_t pby = 0;
    wallet_result_t result = wallet_sign_tx(
        pin_buf, tx_hash, 0, WALLET_COIN_BITCOIN,
        signature, &pby
    );
    memset(pin_buf, 0, sizeof(pin_buf));

    if (result != WALLET_OK) {
        usb_hid_send_failure(1, wallet_result_str(result));
        return;
    }

    /* Retornar firma */
    uint8_t resp[80];
    int pos = 0;
    /* field 2: signature (bytes) */
    pos += _pb_write_varint(resp + pos, (2 << 3) | 2);
    pos += _pb_write_varint(resp + pos, 64);
    memcpy(resp + pos, signature, 64);
    pos += 64;

    usb_hid_send_message(MSG_TX_REQUEST, resp, (uint32_t)pos);
    DEBUG_PRINT("[FSM] TX firmada y enviada\r\n");

    screen_success("Transaccion firmada", 1500);
}

/**
 * _handle_wipe_device — Borrar el wallet
 */
static void _handle_wipe_device(void) {
    DEBUG_PRINT("[FSM] WipeDevice\r\n");

    /* Confirmación del usuario en pantalla */
    tft_clear(THEME_BG);
    ui_topbar("Borrar Wallet", 0, THEME_ERROR_DIM);
    tft_draw_string_centered(120,
        "Esto borrara TODOS tus datos.", THEME_ERROR, THEME_BG, 2);
    tft_draw_string_centered(148,
        "Sin tu semilla de respaldo,", THEME_TEXT_PRIMARY, THEME_BG, 1);
    tft_draw_string_centered(162,
        "perderás acceso a tus fondos.", THEME_TEXT_PRIMARY, THEME_BG, 1);
    ui_confirm_buttons("Cancelar", "Borrar todo", false);

    input_flush();
    input_action_t action = input_wait(0);
    if (action != INPUT_CONFIRM) {
        usb_hid_send_failure(4, "Action cancelled");
        return;
    }

    /* Pedir PIN para autorizar */
    char pin_buf[10] = {0};
    screen_result_t r = screen_pin_entry("PIN para autorizar borrado",
                                          pin_buf, sizeof(pin_buf));
    if (r != SCREEN_RESULT_OK) {
        usb_hid_send_failure(4, "Action cancelled");
        return;
    }

    wallet_result_t result = wallet_wipe(pin_buf);
    memset(pin_buf, 0, sizeof(pin_buf));

    if (result == WALLET_OK) {
        usb_hid_send_success("Device wiped");
        screen_success("Wallet borrado", 2000);
    } else {
        usb_hid_send_failure(1, wallet_result_str(result));
    }
}

/**
 * _handle_reset_device — Crear nuevo wallet
 *
 * Flujo:
 *   1. Verificar que no hay wallet activo (o pedir confirmación)
 *   2. Pedir PIN nuevo al usuario
 *   3. Generar semilla BIP39 con hardware RNG
 *   4. Mostrar semilla al usuario para anotarla
 *   5. Verificar que el usuario la anotó (test de palabras)
 *   6. Almacenar semilla cifrada en Flash
 */
static void _handle_reset_device(void) {
    DEBUG_PRINT("[FSM] ResetDevice — Creando nuevo wallet\r\n");

    if (wallet_is_initialized()) {
        /* Ya existe un wallet — pedir confirmación */
        bool ok = ui_dialog(
            "Ya existe un wallet",
            "Crear uno nuevo borrará el actual.",
            "Continuar", "Cancelar"
        );
        if (!ok) {
            usb_hid_send_failure(4, "Action cancelled");
            return;
        }
    }

    /* Pedir nuevo PIN */
    char pin_buf[10] = {0};
    screen_result_t r = screen_pin_entry("Crea un nuevo PIN (min 4 digitos)",
                                          pin_buf, sizeof(pin_buf));
    if (r != SCREEN_RESULT_OK) {
        usb_hid_send_failure(4, "Cancelled");
        return;
    }

    /* Verificar PIN (pedir dos veces) */
    char pin_confirm[10] = {0};
    r = screen_pin_entry("Confirma tu PIN nuevo", pin_confirm, sizeof(pin_confirm));
    if (r != SCREEN_RESULT_OK || strcmp(pin_buf, pin_confirm) != 0) {
        memset(pin_buf,     0, sizeof(pin_buf));
        memset(pin_confirm, 0, sizeof(pin_confirm));
        usb_hid_send_failure(4, "PIN mismatch");
        screen_error("Error", "Los PINs no coinciden.");
        return;
    }
    memset(pin_confirm, 0, sizeof(pin_confirm));

    /* Configurar el PIN primero */
    if (!storage_set_pin(pin_buf)) {
        memset(pin_buf, 0, sizeof(pin_buf));
        usb_hid_send_failure(1, "Failed to set PIN");
        return;
    }

    /* Mostrar pantalla de "generando semilla" */
    ui_loading_start("Generando semilla BIP39...", 0);

    /* Generar semilla con hardware RNG */
    char mnemonic[WALLET_MNEMONIC_MAX_LEN];
    wallet_result_t result = wallet_generate_seed(
        24, pin_buf, mnemonic, sizeof(mnemonic)
    );
    memset(pin_buf, 0, sizeof(pin_buf));

    ui_loading_end(result == WALLET_OK, NULL);

    if (result != WALLET_OK) {
        usb_hid_send_failure(1, wallet_result_str(result));
        memset(mnemonic, 0, sizeof(mnemonic));
        screen_error("Error", "No se pudo generar la semilla.");
        return;
    }

    /* Separar el mnemónico en palabras para mostrar */
    const char *words[24];
    int word_count = 0;
    char mnemonic_copy[WALLET_MNEMONIC_MAX_LEN];
    strncpy(mnemonic_copy, mnemonic, sizeof(mnemonic_copy) - 1);

    char *token = strtok(mnemonic_copy, " ");
    while (token && word_count < 24) {
        words[word_count++] = token;
        token = strtok(NULL, " ");
    }

    /* Mostrar semilla al usuario */
    screen_seed_view((const char *const *)words, (uint8_t)word_count);

    /* Limpiar datos sensibles */
    memset(mnemonic,      0, sizeof(mnemonic));
    memset(mnemonic_copy, 0, sizeof(mnemonic_copy));

    /* Notificar éxito al host */
    usb_hid_send_success("Device reset successfully");
    screen_success("Wallet creado", 2000);

    DEBUG_PRINT("[FSM] Nuevo wallet creado y almacenado.\r\n");
}


/* =========================================================
 * DISPATCH PRINCIPAL
 * ========================================================= */

/**
 * fsm_dispatch — Procesar un mensaje USB y ejecutar la acción
 *
 * Punto de entrada del FSM. Llamar para cada mensaje recibido.
 *
 * @param msg  Mensaje recibido del host via USB HID
 */
void fsm_dispatch(const usb_message_t *msg) {
    if (!msg || !msg->valid) return;

    DEBUG_PRINTF("[FSM] Mensaje recibido: %s (tipo=%u, len=%lu)\r\n",
        usb_msg_type_name((usb_msg_type_t)msg->type),
        msg->type, msg->length);

    switch ((usb_msg_type_t)msg->type) {

        case MSG_INITIALIZE:
            _handle_initialize();
            break;

        case MSG_PING:
            _handle_ping(msg);
            break;

        case MSG_GET_ADDRESS:
            _handle_get_address(msg);
            break;

        case MSG_SIGN_TX:
            _handle_sign_tx(msg);
            break;

        case MSG_WIPE_DEVICE:
            _handle_wipe_device();
            break;

        case MSG_RESET_DEVICE:
            _handle_reset_device();
            break;

        case MSG_CANCEL:
            DEBUG_PRINT("[FSM] Cancel recibido\r\n");
            usb_hid_send_failure(4, "Action cancelled");
            break;

        case MSG_BUTTON_ACK:
            /* El host confirma que recibió el ButtonRequest */
            DEBUG_PRINT("[FSM] ButtonAck recibido\r\n");
            break;

        case MSG_PIN_ACK:
            /* El host provee el PIN (no lo usamos — PIN se ingresa en el device) */
            DEBUG_PRINT("[FSM] PinAck — ignorado (PIN ingresado en device)\r\n");
            usb_hid_send_failure(4, "PIN entry on device only");
            break;

        default:
            DEBUG_PRINTF("[FSM] Mensaje no implementado: %u\r\n", msg->type);
            usb_hid_send_failure(6, "Not implemented");
            break;
    }
}