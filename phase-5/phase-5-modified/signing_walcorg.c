/**
 * @file    signing_walcorg.c
 * @brief   Protocolo interactivo SignTx — WalCorg Fase 5
 *
 * [MOD-SIGN1] UI via ui_screens.c en vez de pantallas OLED Trezor
 * [MOD-SIGN2] Firma via wallet_core.c (incluye tamper check y MPU)
 * [MOD-SIGN3] Hash de TX via hasher de Trezor sin cambios
 *
 * Protocolo TxRequest/TxAck:
 *   SignTx → TxRequest(INPUT 0) → TxAck → TxRequest(INPUT 1) → TxAck
 *         → TxRequest(OUTPUT 0) → TxAck → ... → TxRequest(FINISHED+firma)
 *
 * @license GNU GPL v3
 */

#include "signing_walcorg.h"
#include "wallet_core.h"
#include "usb_hid.h"
#include "ui_screens.h"
#include "ui_components.h"
#include "ui_theme.h"
#include "display_tft.h"
#include "tamper_detect.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"

#include "sha2.h"
#include "hasher.h"
#include "ecdsa.h"
#include "curves.h"

#include <string.h>
#include <stdio.h>

/* =========================================================
 * CONSTANTES
 * ========================================================= */
#define SIGN_MAX_INPUTS             20
#define SIGN_MAX_OUTPUTS            20
#define SIGN_HIGH_FEE_THRESHOLD_PCT  5

#define TXREQUEST_TXINPUT    0
#define TXREQUEST_TXOUTPUT   1
#define TXREQUEST_TXFINISHED 3

/* =========================================================
 * TIPOS
 * ========================================================= */
typedef struct {
    uint8_t  prev_hash[32];
    uint32_t prev_index;
    uint32_t amount;
    uint32_t key_index;
    bool     valid;
} sign_input_t;

typedef struct {
    char     address[80];
    uint64_t amount;
    bool     is_change;
    bool     valid;
} sign_output_t;

typedef struct {
    sign_input_t  inputs[SIGN_MAX_INPUTS];
    sign_output_t outputs[SIGN_MAX_OUTPUTS];
    uint8_t       num_inputs;
    uint8_t       num_outputs;
    uint8_t       inputs_received;
    uint8_t       outputs_received;
    uint64_t      total_in_satoshi;
    uint64_t      total_out_satoshi;
    uint64_t      fee_satoshi;
    uint8_t       tx_hash[32];
    Hasher        hasher;
    char          pin[10];
    bool          initialized;
    bool          signing_complete;
} sign_state_t;

static sign_state_t _sign = {0};

/* =========================================================
 * PROTOBUF MÍNIMO
 * ========================================================= */
static int _pb_w_varint(uint8_t *b, uint64_t v) {
    int i = 0;
    do { b[i]=(uint8_t)(v&0x7F); v>>=7; if(v) b[i]|=0x80; i++; } while(v&&i<10);
    return i;
}
static int _pb_r_varint(const uint8_t *b, uint32_t l, uint64_t *v) {
    *v=0; int sh=0;
    for(uint32_t i=0;i<l&&i<10;i++){
        *v|=((uint64_t)(b[i]&0x7F))<<sh; sh+=7;
        if(!(b[i]&0x80)) return (int)(i+1);
    }
    return -1;
}
static int _pb_w_u32(uint8_t *b, uint8_t f, uint32_t v) {
    int p=0; p+=_pb_w_varint(b+p,((uint64_t)f<<3)|0); p+=_pb_w_varint(b+p,v); return p;
}
static int _pb_w_bytes(uint8_t *b, uint8_t f, const uint8_t *d, uint32_t l) {
    int p=0; p+=_pb_w_varint(b+p,((uint64_t)f<<3)|2); p+=_pb_w_varint(b+p,l);
    memcpy(b+p,d,l); return p+(int)l;
}

/* =========================================================
 * FORMATEO DE CANTIDADES
 * ========================================================= */
static void _satoshi_to_str(uint64_t sat, char *buf, size_t len) {
    snprintf(buf, len, "%llu.%08llu BTC",
             (unsigned long long)(sat/100000000ULL),
             (unsigned long long)(sat%100000000ULL));
}

/* =========================================================
 * ENVÍO DE TxRequest
 * ========================================================= */
static void _send_tx_request_input(uint8_t idx) {
    uint8_t pl[32]; int p=0;
    p += _pb_w_u32(pl+p, 1, TXREQUEST_TXINPUT);
    uint8_t det[8]; int dl=_pb_w_u32(det,1,idx);
    p += _pb_w_bytes(pl+p, 2, det, (uint32_t)dl);
    usb_hid_send_message(MSG_TX_REQUEST, pl, (uint32_t)p);
    DEBUG_PRINTF("[SIGN] → TxRequest INPUT[%d]\r\n", idx);
}

static void _send_tx_request_output(uint8_t idx) {
    uint8_t pl[32]; int p=0;
    p += _pb_w_u32(pl+p, 1, TXREQUEST_TXOUTPUT);
    uint8_t det[8]; int dl=_pb_w_u32(det,1,idx);
    p += _pb_w_bytes(pl+p, 2, det, (uint32_t)dl);
    usb_hid_send_message(MSG_TX_REQUEST, pl, (uint32_t)p);
    DEBUG_PRINTF("[SIGN] → TxRequest OUTPUT[%d]\r\n", idx);
}

static void _send_tx_request_finished(const uint8_t *sig, uint8_t pby) {
    uint8_t pl[128]; int p=0;
    p += _pb_w_u32(pl+p, 1, TXREQUEST_TXFINISHED);

    if (sig) {
        /* Convertir firma r||s (64 bytes) a DER + SIGHASH_ALL */
        uint8_t der[72]; uint8_t der_len = 0;
        ecdsa_sig_to_der(sig, der, &der_len);
        der[der_len++] = 0x01; /* SIGHASH_ALL */

        uint8_t ser[80];
        int slen = _pb_w_bytes(ser, 1, der, der_len);
        p += _pb_w_bytes(pl+p, 3, ser, (uint32_t)slen);
    }

    usb_hid_send_message(MSG_TX_REQUEST, pl, (uint32_t)p);
    DEBUG_PRINT("[SIGN] → TxRequest FINISHED\r\n");
    (void)pby;
}

/* =========================================================
 * PARSEO DE TxAck
 * ========================================================= */
static bool _parse_input(const uint8_t *pl, uint32_t len, sign_input_t *inp) {
    memset(inp, 0, sizeof(*inp));
    const uint8_t *p=pl; uint32_t r=len;
    while(r>0){
        uint64_t tag; int c=_pb_r_varint(p,r,&tag); if(c<0) break;
        p+=c; r-=c;
        uint8_t f=(uint8_t)(tag>>3), w=(uint8_t)(tag&7);
        if(f==1&&w==2){
            uint64_t al; c=_pb_r_varint(p,r,&al); if(c<0) break;
            p+=c; r-=c;
            const uint8_t *ae=p+al;
            while(p<ae){ uint64_t e; c=_pb_r_varint(p,(uint32_t)(ae-p),&e);
                if(c<0) break; inp->key_index=(uint32_t)(e&0x7FFFFFFF); p+=c; }
            r-=(uint32_t)al;
        } else if(f==2&&w==2){
            uint64_t bl; c=_pb_r_varint(p,r,&bl); if(c<0) break;
            p+=c; r-=c;
            if(bl==32) memcpy(inp->prev_hash,p,32);
            p+=bl; r-=(uint32_t)bl;
        } else if(f==3&&w==0){
            uint64_t v; c=_pb_r_varint(p,r,&v);
            if(c>0){inp->prev_index=(uint32_t)v; p+=c; r-=c;}
        } else if(f==6&&w==0){
            uint64_t v; c=_pb_r_varint(p,r,&v);
            if(c>0){inp->amount=(uint32_t)v; p+=c; r-=c;}
        } else break;
    }
    inp->valid=true; return true;
}

static bool _parse_output(const uint8_t *pl, uint32_t len, sign_output_t *out) {
    memset(out, 0, sizeof(*out));
    const uint8_t *p=pl; uint32_t r=len;
    while(r>0){
        uint64_t tag; int c=_pb_r_varint(p,r,&tag); if(c<0) break;
        p+=c; r-=c;
        uint8_t f=(uint8_t)(tag>>3), w=(uint8_t)(tag&7);
        if(f==1&&w==2){
            uint64_t sl; c=_pb_r_varint(p,r,&sl); if(c<0) break;
            p+=c; r-=c;
            uint32_t cp=(uint32_t)sl<sizeof(out->address)-1?(uint32_t)sl:sizeof(out->address)-1;
            memcpy(out->address,p,cp); out->address[cp]='\0';
            p+=sl; r-=(uint32_t)sl;
        } else if(f==5&&w==0){
            uint64_t v; c=_pb_r_varint(p,r,&v);
            if(c>0){out->amount=v; p+=c; r-=c;}
        } else break;
    }
    out->valid=true; return true;
}

/* =========================================================
 * CONFIRMACIÓN DE UI
 * ========================================================= */
static bool _show_tx_summary(void) {
    _sign.fee_satoshi = _sign.total_in_satoshi - _sign.total_out_satoshi;

    char amt_str[32], fee_str[32];
    _satoshi_to_str(_sign.total_out_satoshi, amt_str, sizeof(amt_str));
    _satoshi_to_str(_sign.fee_satoshi,       fee_str, sizeof(fee_str));

    const char *dest = NULL;
    for(int i=0;i<_sign.num_outputs;i++){
        if(!_sign.outputs[i].is_change && _sign.outputs[i].valid){
            dest=_sign.outputs[i].address; break;
        }
    }

    /* Advertencia de fee alta */
    if(_sign.total_in_satoshi > 0){
        uint64_t pct=(_sign.fee_satoshi*100)/_sign.total_in_satoshi;
        if(pct > SIGN_HIGH_FEE_THRESHOLD_PCT){
            tft_clear(THEME_BG);
            ui_topbar("! Comision Alta !", 0, THEME_ERROR_DIM);
            char ws[64]; snprintf(ws,sizeof(ws),"Fee: %llu%% del total",(unsigned long long)pct);
            tft_draw_string_centered(120, ws, THEME_ERROR, THEME_BG, FONT_BODY);
            tft_draw_string_centered(150, fee_str, THEME_WARNING, THEME_BG, FONT_BODY);
            ui_confirm_buttons("Cancelar","Continuar",false);
            input_flush();
            if(input_wait(0) != INPUT_CONFIRM) return false;
        }
    }

    /* [MOD-SIGN1] Pantalla de confirmación via ui_screens.c */
    return screen_send_confirm(amt_str, fee_str,
                               dest ? dest : "multiple outputs")
           == SCREEN_RESULT_OK;
}

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

void signing_init(const uint8_t *payload, uint32_t length) {
    DEBUG_PRINT("\r\n[SIGN] === Iniciando protocolo SignTx ===\r\n");

    if (!tamper_check_safe_to_proceed()) {
        usb_hid_send_failure(99, "Security check failed");
        return;
    }

    memset(&_sign, 0, sizeof(_sign));

    const uint8_t *p=payload; uint32_t r=length;
    while(r>0){
        uint64_t tag; int c=_pb_r_varint(p,r,&tag); if(c<0) break;
        p+=c; r-=c;
        uint8_t f=(uint8_t)(tag>>3), w=(uint8_t)(tag&7);
        if(w==0){
            uint64_t v; c=_pb_r_varint(p,r,&v); if(c<0) break;
            if(f==1) _sign.num_inputs=(uint8_t)v;
            if(f==2) _sign.num_outputs=(uint8_t)v;
            p+=c; r-=c;
        } else break;
    }

    if(!_sign.num_inputs || !_sign.num_outputs ||
       _sign.num_inputs  > SIGN_MAX_INPUTS ||
       _sign.num_outputs > SIGN_MAX_OUTPUTS){
        usb_hid_send_failure(2,"Invalid transaction structure");
        return;
    }

    DEBUG_PRINTF("[SIGN] %d inputs, %d outputs\r\n",
        _sign.num_inputs, _sign.num_outputs);

    hasher_Init(&_sign.hasher, HASHER_SHA2D);
    _sign.initialized = true;
    _send_tx_request_input(0);
}

void signing_txack(const uint8_t *payload, uint32_t length) {
    if(!_sign.initialized){
        usb_hid_send_failure(2,"Not in signing mode"); return;
    }

    if(_sign.inputs_received < _sign.num_inputs){
        /* Recibiendo un input */
        uint8_t idx = _sign.inputs_received;
        if(!_parse_input(payload, length, &_sign.inputs[idx])){
            usb_hid_send_failure(2,"Failed to parse input"); return;
        }
        _sign.total_in_satoshi += _sign.inputs[idx].amount;
        hasher_Update(&_sign.hasher, _sign.inputs[idx].prev_hash, 32);
        hasher_Update(&_sign.hasher, (uint8_t*)&_sign.inputs[idx].prev_index, 4);
        _sign.inputs_received++;
        DEBUG_PRINTF("[SIGN] Input[%d]: %lu sats\r\n", idx,
            (unsigned long)_sign.inputs[idx].amount);

        if(_sign.inputs_received < _sign.num_inputs){
            _send_tx_request_input(_sign.inputs_received);
        } else {
            _send_tx_request_output(0);
        }

    } else if(_sign.outputs_received < _sign.num_outputs){
        /* Recibiendo un output */
        uint8_t idx = _sign.outputs_received;
        if(!_parse_output(payload, length, &_sign.outputs[idx])){
            usb_hid_send_failure(2,"Failed to parse output"); return;
        }
        _sign.total_out_satoshi += _sign.outputs[idx].amount;
        hasher_Update(&_sign.hasher, (uint8_t*)&_sign.outputs[idx].amount, 8);
        hasher_Update(&_sign.hasher, (uint8_t*)_sign.outputs[idx].address,
                      strlen(_sign.outputs[idx].address));
        _sign.outputs_received++;
        DEBUG_PRINTF("[SIGN] Output[%d]: %s = %llu sats\r\n", idx,
            _sign.outputs[idx].address,
            (unsigned long long)_sign.outputs[idx].amount);

        if(_sign.outputs_received < _sign.num_outputs){
            _send_tx_request_output(_sign.outputs_received);
            return;
        }

        /* ===== TODOS LOS DATOS RECIBIDOS ===== */
        hasher_Final(&_sign.hasher, _sign.tx_hash);
        DEBUG_PRINT("[SIGN] Hash TX completado. Iniciando confirmación.\r\n");

        /* [MOD-SIGN1] Confirmación via UI */
        if(!_show_tx_summary()){
            usb_hid_send_failure(4,"Action cancelled");
            memset(&_sign,0,sizeof(_sign)); return;
        }

        /* Pedir PIN */
        if(screen_pin_entry("PIN para firmar TX",
                            _sign.pin, sizeof(_sign.pin)) != SCREEN_RESULT_OK){
            usb_hid_send_failure(4,"Action cancelled");
            memset(&_sign,0,sizeof(_sign)); return;
        }

        /* [MOD-SIGN2] Firmar via wallet_core */
        uint8_t signature[64]; uint8_t pby=0;
        wallet_result_t res = wallet_sign_tx(
            _sign.pin, _sign.tx_hash,
            _sign.inputs[0].key_index,
            WALLET_COIN_BITCOIN, signature, &pby);

        memset(_sign.pin, 0, sizeof(_sign.pin));

        if(res != WALLET_OK){
            usb_hid_send_failure(1, wallet_result_str(res));
            memset(&_sign,0,sizeof(_sign)); return;
        }

        DEBUG_PRINT("[SIGN] TX firmada. Enviando TxRequest FINISHED.\r\n");
        _send_tx_request_finished(signature, pby);
        screen_success("Transaccion firmada", 1500);

        _sign.signing_complete = true;
        memset(&_sign, 0, sizeof(_sign));
    }
}

void signing_cancel(void) {
    if(_sign.initialized){
        DEBUG_PRINT("[SIGN] Firma cancelada.\r\n");
        memset(&_sign, 0, sizeof(_sign));
        usb_hid_send_failure(4,"Action cancelled");
    }
}

bool signing_is_active(void) {
    return _sign.initialized && !_sign.signing_complete;
}
