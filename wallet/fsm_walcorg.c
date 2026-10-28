/**
 * @file    fsm_walcorg.c
 * @brief   FSM del wallet con protocolo SignTx interactivo — WALCORG Fase 5
 *
 * CAMBIOS RESPECTO A LA VERSIÓN ANTERIOR DE fsm_walcorg.c:
 *
 *   [MOD-F1] _handle_sign_tx() ahora inicia el protocolo interactivo
 *            real via signing_init() en vez de la versión simplificada
 *            de demostración.
 *
 *   [MOD-F2] MSG_TX_ACK se despacha a signing_txack() para continuar
 *            el diálogo TxRequest/TxAck con el host.
 *
 *   [MOD-F3] Añadido _handle_get_public_key() — trezorctl lo usa para
 *            verificar que el dispositivo tiene la clave correcta antes
 *            de construir la TX.
 *
 *   [MOD-F4] Añadido _handle_change_pin() — permite cambiar el PIN
 *            via trezorctl además de hacerlo localmente en el menú.
 *
 *   [MOD-F5] Añadido _handle_entropy_request() — responde a solicitudes
 *            de entropía adicional del host durante ResetDevice.
 *
 *   [MOD-F6] signing_is_active() se verifica al inicio del dispatch para
 *            redirigir MSG_TX_ACK y MSG_CANCEL al módulo de firma.
 *
 * @license GNU GPL v3
 */

#include "signing_walcorg.h"
#include "usb_hid.h"
#include "wallet_core.h"
#include "storage_hal.h"
#include "ui_screens.h"
#include "ui_components.h"
#include "ui_theme.h"
#include "display_tft.h"
#include "input_manager.h"
#include "rng_test.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

/* =========================================================
 * PROTOBUF MÍNIMO (mismas helpers que en signing_walcorg.c)
 * ========================================================= */

static int _pb_write_varint(uint8_t *buf, uint64_t val) {
    int i = 0;
    do {
        buf[i] = (uint8_t)(val & 0x7F);
        val >>= 7;
        if (val) buf[i] |= 0x80;
        i++;
    } while (val && i < 10);
    return i;
}

static int _pb_read_varint(const uint8_t *buf, uint32_t len, uint64_t *val) {
    *val = 0;
    int shift = 0;
    for (uint32_t i = 0; i < len && i < 10; i++) {
        *val |= ((uint64_t)(buf[i] & 0x7F)) << shift;
        shift += 7;
        if (!(buf[i] & 0x80)) return (int)(i + 1);
    }
    return -1;
}

static int _pb_write_string(uint8_t *buf, uint8_t field, const char *str) {
    if (!str) return 0;
    uint32_t len = (uint32_t)strlen(str);
    int pos = 0;
    pos += _pb_write_varint(buf + pos, ((uint64_t)field << 3) | 2);
    pos += _pb_write_varint(buf + pos, len);
    memcpy(buf + pos, str, len);
    return pos + (int)len;
}

static int _pb_write_uint32(uint8_t *buf, uint8_t field, uint32_t val) {
    int pos = 0;
    pos += _pb_write_varint(buf + pos, ((uint64_t)field << 3) | 0);
    pos += _pb_write_varint(buf + pos, val);
    return pos;
}

static int _pb_write_bool(uint8_t *buf, uint8_t field, bool val) {
    return _pb_write_uint32(buf, field, val ? 1 : 0);
}

static int _pb_write_bytes(uint8_t *buf, uint8_t field,
                            const uint8_t *data, uint32_t len) {
    int pos = 0;
    pos += _pb_write_varint(buf + pos, ((uint64_t)field << 3) | 2);
    pos += _pb_write_varint(buf + pos, len);
    memcpy(buf + pos, data, len);
    return pos + (int)len;
}


/* =========================================================
 * HANDLERS DE MENSAJES
 * ========================================================= */

/**
 * _handle_initialize — Retornar Features del dispositivo
 */
static void _handle_initialize(void) {
    DEBUG_PRINT("[FSM] Initialize → Features\r\n");

    uint8_t resp[256];
    int pos = 0;

    pos += _pb_write_string(resp + pos, 1,  "walcorg.com");
    pos += _pb_write_uint32(resp + pos, 2,  1);    /* major_version */
    pos += _pb_write_uint32(resp + pos, 3,  0);    /* minor_version */
    pos += _pb_write_uint32(resp + pos, 4,  0);    /* patch_version */
    pos += _pb_write_bool  (resp + pos, 5,  false);/* bootloader_mode */
    pos += _pb_write_string(resp + pos, 11, "WALCORG");
    pos += _pb_write_bool  (resp + pos, 12, true); /* firmware_present */
    pos += _pb_write_bool  (resp + pos, 13, wallet_is_initialized());
    pos += _pb_write_bool  (resp + pos, 20, true); /* pin_protection */
    pos += _pb_write_bool  (resp + pos, 25, false);/* passphrase_protection */

    /* Label del dispositivo */
    char label[STORAGE_LABEL_MAX_LEN];
    wallet_get_info(label, sizeof(label));
    if (label[0]) pos += _pb_write_string(resp + pos, 9, label);

    usb_hid_send_message(MSG_FEATURES, resp, (uint32_t)pos);
}

/**
 * _handle_ping — Eco del mensaje
 */
static void _handle_ping(const usb_message_t *msg) {
    DEBUG_PRINT("[FSM] Ping\r\n");

    char ping_msg[64] = {0};
    const uint8_t *p = msg->payload;
    uint32_t rem = msg->length;

    while (rem > 0) {
        uint64_t tag;
        int c = _pb_read_varint(p, rem, &tag);
        if (c < 0) break;
        p += c; rem -= c;
        if ((tag >> 3) == 1 && (tag & 7) == 2) {
            uint64_t slen;
            c = _pb_read_varint(p, rem, &slen);
            if (c < 0) break;
            p += c; rem -= c;
            uint32_t copy = (uint32_t)slen < sizeof(ping_msg)-1 ?
                            (uint32_t)slen : sizeof(ping_msg)-1;
            memcpy(ping_msg, p, copy);
            ping_msg[copy] = '\0';
            break;
        } else break;
    }

    usb_hid_send_success(ping_msg[0] ? ping_msg : "pong");
}

/**
 * _handle_get_public_key — Derivar y retornar clave pública
 *
 * [MOD-F3] Nuevo handler. trezorctl usa GetPublicKey para:
 *   - Verificar que el dispositivo tiene la clave correcta
 *   - Obtener xpub para generar direcciones offline
 *   - Verificar la cuenta antes de construir una TX
 *
 * PublicKey response fields:
 *   field 1: node.public_key (bytes, 33 bytes comprimida)
 *   field 2: xpub (string) — en formato xpub... de BIP32
 */
static void _handle_get_public_key(const usb_message_t *msg) {
    DEBUG_PRINT("[FSM] GetPublicKey\r\n");

    /* Extraer key_index del address_n */
    uint32_t key_index = 0;
    const uint8_t *p = msg->payload;
    uint32_t rem = msg->length;
    while (rem > 0) {
        uint64_t tag;
        int c = _pb_read_varint(p, rem, &tag);
        if (c < 0) break;
        p += c; rem -= c;
        if ((tag >> 3) == 1 && (tag & 7) == 2) {
            uint64_t alen;
            c = _pb_read_varint(p, rem, &alen);
            if (c < 0) break;
            p += c; rem -= c;
            const uint8_t *aend = p + alen;
            while (p < aend) {
                uint64_t elem;
                c = _pb_read_varint(p, (uint32_t)(aend-p), &elem);
                if (c < 0) break;
                key_index = (uint32_t)(elem & 0x7FFFFFFF);
                p += c;
            }
            rem -= (uint32_t)alen;
            break;
        } else break;
    }

    /* Pedir PIN */
    char pin[10] = {0};
    if (screen_pin_entry("PIN para GetPublicKey", pin, sizeof(pin))
            != SCREEN_RESULT_OK) {
        usb_hid_send_failure(4, "Action cancelled");
        return;
    }

    /* Derivar dirección para obtener la clave pública */
    wallet_address_t addr = {0};
    wallet_result_t r = wallet_get_address(pin, key_index,
                                            WALLET_COIN_BITCOIN, &addr);
    memset(pin, 0, sizeof(pin));

    if (r != WALLET_OK) {
        usb_hid_send_failure(1, wallet_result_str(r));
        return;
    }

    /* Serializar respuesta PublicKey */
    uint8_t resp[128];
    int pos = 0;

    /* node embedded message (field 1) */
    uint8_t node_buf[64];
    int npos = 0;
    /* node.public_key = field 3, bytes 33 */
    npos += _pb_write_bytes(node_buf + npos, 3, addr.public_key, 33);
    /* node.chain_code = field 4, bytes 32 (placeholder) */
    uint8_t chain_code[32] = {0};
    npos += _pb_write_bytes(node_buf + npos, 4, chain_code, 32);
    pos += _pb_write_bytes(resp + pos, 1, node_buf, (uint32_t)npos);

    /* xpub placeholder (en producción: serializar el nodo HD completo) */
    pos += _pb_write_string(resp + pos, 2, "xpub_placeholder");

    usb_hid_send_message(MSG_PUBLIC_KEY, resp, (uint32_t)pos);
    DEBUG_PRINTF("[FSM] PublicKey enviada para índice %lu\r\n",
        (unsigned long)key_index);
}

/**
 * _handle_get_address — Derivar y retornar dirección Bitcoin
 */
static void _handle_get_address(const usb_message_t *msg) {
    DEBUG_PRINT("[FSM] GetAddress\r\n");

    uint32_t addr_index = 0;
    bool show_display   = true;

    const uint8_t *p = msg->payload;
    uint32_t rem = msg->length;
    while (rem > 0) {
        uint64_t tag;
        int c = _pb_read_varint(p, rem, &tag);
        if (c < 0) break;
        p += c; rem -= c;
        uint8_t field = (uint8_t)(tag >> 3);
        uint8_t wtype = (uint8_t)(tag & 7);
        if (field == 1 && wtype == 2) {
            uint64_t alen;
            c = _pb_read_varint(p, rem, &alen);
            if (c < 0) break;
            p += c; rem -= c;
            const uint8_t *aend = p + alen;
            while (p < aend) {
                uint64_t elem;
                c = _pb_read_varint(p, (uint32_t)(aend-p), &elem);
                if (c < 0) break;
                addr_index = (uint32_t)(elem & 0x7FFFFFFF);
                p += c;
            }
            rem -= (uint32_t)alen;
        } else if (field == 3 && wtype == 0) {
            uint64_t val;
            c = _pb_read_varint(p, rem, &val);
            if (c > 0) { show_display = (val != 0); p += c; rem -= c; }
        } else break;
    }

    char pin[10] = {0};
    if (screen_pin_entry("PIN para GetAddress", pin, sizeof(pin))
            != SCREEN_RESULT_OK) {
        usb_hid_send_failure(4, "Action cancelled");
        return;
    }

    wallet_address_t addr = {0};
    wallet_result_t r = wallet_get_address(pin, addr_index,
                                            WALLET_COIN_BITCOIN, &addr);
    memset(pin, 0, sizeof(pin));

    if (r != WALLET_OK) {
        usb_hid_send_failure(1, wallet_result_str(r));
        return;
    }

    if (show_display) screen_receive(addr.address_bech32);

    uint8_t resp[128];
    int pos = 0;
    pos += _pb_write_string(resp + pos, 1, addr.address_bech32);
    usb_hid_send_message(MSG_ADDRESS, resp, (uint32_t)pos);
    DEBUG_PRINTF("[FSM] Address: %s\r\n", addr.address_bech32);
}

/**
 * _handle_sign_tx — Iniciar protocolo interactivo de firma
 *
 * [MOD-F1] Reemplaza la versión simplificada por signing_init()
 * que inicia el diálogo TxRequest/TxAck con el host.
 */
static void _handle_sign_tx(const usb_message_t *msg) {
    DEBUG_PRINT("[FSM] SignTx → iniciando protocolo interactivo\r\n");

    if (!wallet_is_initialized()) {
        usb_hid_send_failure(2, "Wallet not initialized");
        return;
    }

    /* [MOD-F1] Delegar al módulo de firma interactivo */
    signing_init(msg->payload, msg->length);
    /* El resto del protocolo se maneja en signing_txack()
     * conforme llegan los MSG_TX_ACK del host */
}

/**
 * _handle_change_pin — Cambiar PIN via trezorctl
 *
 * [MOD-F4] Nuevo handler. Flujo:
 *   1. Solicitar PIN actual en pantalla
 *   2. Verificar PIN actual
 *   3. Solicitar nuevo PIN
 *   4. Solicitar nuevo PIN (confirmación)
 *   5. Si ambos coinciden → guardar en Flash
 */
static void _handle_change_pin(void) {
    DEBUG_PRINT("[FSM] ChangePin\r\n");

    /* PIN actual */
    char old_pin[10] = {0};
    if (screen_pin_entry("PIN actual", old_pin, sizeof(old_pin))
            != SCREEN_RESULT_OK) {
        usb_hid_send_failure(4, "Action cancelled");
        return;
    }

    if (!storage_verify_pin(old_pin)) {
        memset(old_pin, 0, sizeof(old_pin));
        usb_hid_send_failure(4, "PIN incorrecto");
        screen_error("PIN incorrecto", "No se pudo cambiar el PIN.");
        return;
    }
    memset(old_pin, 0, sizeof(old_pin));

    /* Nuevo PIN */
    char new_pin[10] = {0}, confirm_pin[10] = {0};
    if (screen_pin_entry("Nuevo PIN (min 4 digitos)",
                          new_pin, sizeof(new_pin)) != SCREEN_RESULT_OK) {
        usb_hid_send_failure(4, "Action cancelled");
        return;
    }

    if (screen_pin_entry("Confirma el nuevo PIN",
                          confirm_pin, sizeof(confirm_pin)) != SCREEN_RESULT_OK
            || strcmp(new_pin, confirm_pin) != 0) {
        memset(new_pin,     0, sizeof(new_pin));
        memset(confirm_pin, 0, sizeof(confirm_pin));
        usb_hid_send_failure(4, "PINs do not match");
        screen_error("Error", "Los PINs no coinciden.");
        return;
    }
    memset(confirm_pin, 0, sizeof(confirm_pin));

    if (!storage_set_pin(new_pin)) {
        memset(new_pin, 0, sizeof(new_pin));
        usb_hid_send_failure(1, "Failed to update PIN");
        return;
    }
    memset(new_pin, 0, sizeof(new_pin));

    usb_hid_send_success("PIN updated");
    screen_success("PIN actualizado", 1500);
    DEBUG_PRINT("[FSM] PIN cambiado exitosamente.\r\n");
}

/**
 * _handle_wipe_device — Borrar wallet con confirmación
 */
static void _handle_wipe_device(void) {
    DEBUG_PRINT("[FSM] WipeDevice\r\n");

    tft_clear(THEME_BG);
    ui_topbar("Borrar Wallet", 0, THEME_ERROR_DIM);
    tft_draw_string_centered(100,
        "BORRADO TOTAL — IRREVERSIBLE",
        THEME_ERROR, THEME_BG, FONT_BODY);
    tft_draw_string_centered(130,
        "Todos tus datos seran eliminados.",
        THEME_TEXT_PRIMARY, THEME_BG, FONT_SMALL);
    tft_draw_string_centered(150,
        "Sin semilla de respaldo = fondos perdidos.",
        THEME_WARNING, THEME_BG, FONT_SMALL);
    ui_confirm_buttons("Cancelar", "Borrar", false);

    input_flush();
    input_action_t act = input_wait(0);
    if (act != INPUT_CONFIRM) {
        usb_hid_send_failure(4, "Action cancelled");
        return;
    }

    char pin[10] = {0};
    if (screen_pin_entry("PIN para autorizar borrado",
                          pin, sizeof(pin)) != SCREEN_RESULT_OK) {
        usb_hid_send_failure(4, "Action cancelled");
        return;
    }

    wallet_result_t r = wallet_wipe(pin);
    memset(pin, 0, sizeof(pin));

    if (r == WALLET_OK) {
        usb_hid_send_success("Device wiped");
        screen_success("Wallet borrado", 2000);
        DEBUG_PRINT("[FSM] Wallet borrado.\r\n");
    } else {
        usb_hid_send_failure(1, wallet_result_str(r));
    }
}

/**
 * _handle_reset_device — Crear nuevo wallet via trezorctl
 */
static void _handle_reset_device(void) {
    DEBUG_PRINT("[FSM] ResetDevice\r\n");

    if (wallet_is_initialized()) {
        bool ok = ui_dialog("Ya existe un wallet",
                            "Crear uno nuevo borrara el actual.",
                            "Continuar", "Cancelar");
        if (!ok) {
            usb_hid_send_failure(4, "Action cancelled");
            return;
        }
    }

    char pin[10] = {0}, pin2[10] = {0};
    if (screen_pin_entry("Crea un PIN nuevo",
                          pin, sizeof(pin)) != SCREEN_RESULT_OK) {
        usb_hid_send_failure(4, "Cancelled");
        return;
    }
    if (screen_pin_entry("Confirma el PIN",
                          pin2, sizeof(pin2)) != SCREEN_RESULT_OK
            || strcmp(pin, pin2) != 0) {
        memset(pin, 0, sizeof(pin));
        memset(pin2, 0, sizeof(pin2));
        usb_hid_send_failure(4, "PIN mismatch");
        return;
    }
    memset(pin2, 0, sizeof(pin2));

    if (!storage_set_pin(pin)) {
        memset(pin, 0, sizeof(pin));
        usb_hid_send_failure(1, "Failed to set PIN");
        return;
    }

    ui_loading_start("Generando semilla BIP39...", 0);
    char mnemonic[WALLET_MNEMONIC_MAX_LEN];
    wallet_result_t r = wallet_generate_seed(24, pin, mnemonic, sizeof(mnemonic));
    memset(pin, 0, sizeof(pin));
    ui_loading_end(r == WALLET_OK, NULL);

    if (r != WALLET_OK) {
        usb_hid_send_failure(1, wallet_result_str(r));
        memset(mnemonic, 0, sizeof(mnemonic));
        return;
    }

    /* Mostrar semilla al usuario */
    const char *words[24]; int wc = 0;
    char mc[WALLET_MNEMONIC_MAX_LEN];
    strncpy(mc, mnemonic, sizeof(mc) - 1);
    memset(mnemonic, 0, sizeof(mnemonic));
    char *tok = strtok(mc, " ");
    while (tok && wc < 24) { words[wc++] = tok; tok = strtok(NULL, " "); }
    screen_seed_view((const char *const *)words, (uint8_t)wc);
    memset(mc, 0, sizeof(mc));

    /* [MOD-F5] Enviar EntropyRequest para que el host contribuya entropía */
    uint8_t ent_req[4];
    int pos = _pb_write_uint32(ent_req, 1, 32);  /* size = 32 bytes */
    usb_hid_send_message(MSG_ENTROPY_REQUEST, ent_req, (uint32_t)pos);
    DEBUG_PRINT("[FSM] EntropyRequest enviado (esperando EntropyAck).\r\n");

    /* La respuesta EntropyAck se procesa en el dispatch */
    usb_hid_send_success("Device reset successfully");
    screen_success("Wallet creado", 2000);
}

/**
 * _handle_entropy_ack — Procesar entropía adicional del host
 *
 * [MOD-F5] Nuevo handler. Durante ResetDevice, el host puede
 * contribuir entropía adicional mezclándola con la del RNG hardware.
 * Aquí la recibimos y la XOR-eamos con un nuevo valor del RNG.
 *
 * En producción: usar la entropía del host para re-sembrar el RNG
 * antes de generar la semilla final.
 */
static void _handle_entropy_ack(const usb_message_t *msg) {
    DEBUG_PRINT("[FSM] EntropyAck recibido\r\n");

    if (msg->length >= 32) {
        /* XOR con entropía del hardware RNG del dispositivo */
        uint8_t device_entropy[32];
        for (int i = 0; i < 8; i++) {
            uint32_t r = 0;
            rng_get32(&r);   /* [v0.2] the rng_test.h API is rng_get32() */
            memcpy(device_entropy + i * 4, &r, 4);
        }

        /* En producción: pasar la entropía mezclada a la función de
         * generación de semilla. Aquí solo la registramos. */
        uint8_t mixed[32];
        for (int i = 0; i < 32; i++) {
            mixed[i] = msg->payload[i] ^ device_entropy[i];
        }

        DEBUG_PRINT("[FSM] Entropía del host mezclada con RNG hardware.\r\n");
        memset(device_entropy, 0, sizeof(device_entropy));
        memset(mixed, 0, sizeof(mixed));
    }

    usb_hid_send_success("Entropy acknowledged");
}


/* =========================================================
 * DISPATCH PRINCIPAL
 * ========================================================= */

/**
 * fsm_dispatch — Procesar un mensaje USB y ejecutar la acción
 *
 * [MOD-F6] Verifica signing_is_active() primero para redirigir
 * MSG_TX_ACK y MSG_CANCEL al módulo de firma cuando hay una
 * transacción en curso.
 *
 * @param msg  Mensaje recibido del host via USB HID
 */
void fsm_dispatch(const usb_message_t *msg) {
    if (!msg || !msg->valid) return;

    DEBUG_PRINTF("[FSM] %s (tipo=%u, len=%lu)\r\n",
        usb_msg_type_name((usb_msg_type_t)msg->type),
        msg->type, msg->length);

    /* [MOD-F6] Si hay firma en curso, redirigir TxAck y Cancel */
    if (signing_is_active()) {
        if ((usb_msg_type_t)msg->type == MSG_TX_ACK) {
            signing_txack(msg->payload, msg->length);
            return;
        }
        if ((usb_msg_type_t)msg->type == MSG_CANCEL) {
            signing_cancel();
            return;
        }
    }

    switch ((usb_msg_type_t)msg->type) {

        case MSG_INITIALIZE:
            _handle_initialize();
            break;

        case MSG_PING:
            _handle_ping(msg);
            break;

        case MSG_GET_PUBLIC_KEY:        /* [MOD-F3] */
            _handle_get_public_key(msg);
            break;

        case MSG_GET_ADDRESS:
            _handle_get_address(msg);
            break;

        case MSG_SIGN_TX:               /* [MOD-F1] */
            _handle_sign_tx(msg);
            break;

        case MSG_TX_ACK:                /* [MOD-F2] */
            /* Si llegamos aquí, no hay firma en curso */
            usb_hid_send_failure(2, "Not in signing mode");
            break;

        case MSG_CHANGE_PIN:            /* [MOD-F4] */
            _handle_change_pin();
            break;

        case MSG_WIPE_DEVICE:
            _handle_wipe_device();
            break;

        case MSG_RESET_DEVICE:
            _handle_reset_device();
            break;

        case MSG_ENTROPY_ACK:           /* [MOD-F5] */
            _handle_entropy_ack(msg);
            break;

        case MSG_BUTTON_ACK:
            DEBUG_PRINT("[FSM] ButtonAck — OK\r\n");
            break;

        case MSG_PIN_ACK:
            /* PIN siempre se ingresa en el dispositivo — no via host */
            usb_hid_send_failure(4, "PIN entry on device only");
            break;

        case MSG_CANCEL:
            usb_hid_send_failure(4, "Action cancelled");
            break;

        default:
            DEBUG_PRINTF("[FSM] Mensaje no implementado: %u\r\n", msg->type);
            usb_hid_send_failure(6, "Not implemented");
            break;
    }
}
