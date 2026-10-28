/**
 * @file    usb_hid.c
 * @brief   Stack USB HID — WALCORG Fase 5
 *
 * Implementa el protocolo de comunicación USB de Trezor:
 * mensajes protobuf fragmentados en chunks HID de 64 bytes.
 *
 * @license GNU GPL v3
 */

#include "usb_hid.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

/* =========================================================
 * DESCRIPTORES USB
 * ========================================================= */

/** Descriptor de dispositivo USB */
static const uint8_t _device_desc[] = {
    0x12,                   /* bLength */
    0x01,                   /* bDescriptorType: Device */
    0x00, 0x02,             /* bcdUSB: USB 2.0 */
    0x00,                   /* bDeviceClass: definido por interfaz */
    0x00,                   /* bDeviceSubClass */
    0x00,                   /* bDeviceProtocol */
    0x40,                   /* bMaxPacketSize0: 64 bytes */
    (USB_VID & 0xFF),
    (USB_VID >> 8),         /* idVendor: 0x1209 */
    (USB_PID & 0xFF),
    (USB_PID >> 8),         /* idProduct: 0x53C1 */
    0x01, 0x00,             /* bcdDevice: v1.0 */
    0x01,                   /* iManufacturer */
    0x02,                   /* iProduct */
    0x03,                   /* iSerialNumber */
    0x01,                   /* bNumConfigurations */
};

/** Descriptor de reporte HID (64 bytes IN/OUT) */
static const uint8_t _hid_report_desc[] = {
    0x06, 0x00, 0xFF,  /* Usage Page (Vendor Defined 0xFF00) */
    0x09, 0x01,        /* Usage (0x01) */
    0xA1, 0x01,        /* Collection (Application) */
    0x09, 0x20,        /*   Usage (Input Report) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00,  /*   Logical Maximum (255) */
    0x75, 0x08,        /*   Report Size (8 bits) */
    0x95, 0x40,        /*   Report Count (64 bytes) */
    0x81, 0x02,        /*   Input (Data, Variable, Absolute) */
    0x09, 0x21,        /*   Usage (Output Report) */
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x40,        /*   Report Count (64 bytes) */
    0x91, 0x02,        /*   Output (Data, Variable, Absolute) */
    0xC0               /* End Collection */
};

/* =========================================================
 * ESTADO INTERNO
 * ========================================================= */

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/** Cola simple de mensajes recibidos */
static usb_message_t _rx_queue[HID_QUEUE_DEPTH];
static uint8_t _rx_head = 0;
static uint8_t _rx_tail = 0;
static uint8_t _rx_count = 0;

/** Buffer de ensamblado del mensaje actual */
static struct {
    uint8_t  buf[HID_MAX_MSG_SIZE];  /* Buffer del mensaje completo */
    uint32_t expected_len;            /* Longitud esperada */
    uint32_t received_len;            /* Bytes recibidos hasta ahora */
    uint16_t msg_type;                /* Tipo del mensaje */
    bool     in_progress;             /* Ensamblando un mensaje */
} _assembly = {0};

/** Estado de conexión */
static bool _connected = false;
static bool _initialized = false;

/* Buffer de recepción HID (un chunk a la vez) */
static uint8_t _rx_buf[HID_REPORT_SIZE];

/* =========================================================
 * FUNCIONES PRIVADAS
 * ========================================================= */

/**
 * _queue_push — Añadir mensaje a la cola RX
 */
static void _queue_push(const usb_message_t *msg) {
    if (_rx_count >= HID_QUEUE_DEPTH) {
        /* Cola llena — descartar el más antiguo */
        _rx_head = (_rx_head + 1) % HID_QUEUE_DEPTH;
        _rx_count--;
    }
    memcpy(&_rx_queue[_rx_tail], msg, sizeof(usb_message_t));
    _rx_tail = (_rx_tail + 1) % HID_QUEUE_DEPTH;
    _rx_count++;
}

/**
 * _queue_pop — Extraer mensaje de la cola RX
 */
static bool _queue_pop(usb_message_t *msg) {
    if (_rx_count == 0) return false;
    memcpy(msg, &_rx_queue[_rx_head], sizeof(usb_message_t));
    _rx_head = (_rx_head + 1) % HID_QUEUE_DEPTH;
    _rx_count--;
    return true;
}

/**
 * _process_chunk — Procesar un chunk HID recibido
 *
 * Ensambla chunks sucesivos en un mensaje completo.
 * Cuando el mensaje está completo, lo añade a la cola RX.
 *
 * @param chunk  Buffer de 64 bytes del chunk recibido
 */
static void _process_chunk(const uint8_t *chunk) {
    /* El byte 0 de cada chunk debe ser '?' (0x3F) */
    if (chunk[0] != HID_MARKER) {
        DEBUG_PRINT("[USB] Chunk sin marcador '?' — descartando\r\n");
        return;
    }

    if (!_assembly.in_progress) {
        /* Primer chunk: verificar magic bytes */
        if (chunk[1] != HID_MAGIC_1 || chunk[2] != HID_MAGIC_2) {
            DEBUG_PRINT("[USB] Magic bytes inválidos — descartando\r\n");
            return;
        }

        /* Extraer tipo y longitud del mensaje (big-endian) */
        _assembly.msg_type     = ((uint16_t)chunk[3] << 8) | chunk[4];
        _assembly.expected_len = ((uint32_t)chunk[5] << 24) |
                                  ((uint32_t)chunk[6] << 16) |
                                  ((uint32_t)chunk[7] <<  8) |
                                   (uint32_t)chunk[8];

        if (_assembly.expected_len > HID_MAX_MSG_SIZE) {
            DEBUG_PRINTF("[USB] Mensaje demasiado largo: %lu bytes\r\n",
                _assembly.expected_len);
            return;
        }

        /* Copiar payload del primer chunk (bytes 9..63 = 55 bytes) */
        uint32_t first_payload = HID_REPORT_SIZE - 9;
        if (first_payload > _assembly.expected_len) {
            first_payload = _assembly.expected_len;
        }
        memcpy(_assembly.buf, chunk + 9, first_payload);
        _assembly.received_len = first_payload;
        _assembly.in_progress  = (_assembly.received_len < _assembly.expected_len);

        DEBUG_PRINTF("[USB] Nuevo mensaje tipo=%u len=%lu\r\n",
            _assembly.msg_type, _assembly.expected_len);

    } else {
        /* Chunk de continuación: bytes 1..63 = 63 bytes de payload */
        uint32_t remaining = _assembly.expected_len - _assembly.received_len;
        uint32_t chunk_payload = HID_CHUNK_PAYLOAD;
        if (chunk_payload > remaining) chunk_payload = remaining;

        memcpy(_assembly.buf + _assembly.received_len,
               chunk + 1, chunk_payload);
        _assembly.received_len += chunk_payload;
        _assembly.in_progress = (_assembly.received_len < _assembly.expected_len);
    }

    /* ¿Mensaje completo? */
    if (!_assembly.in_progress) {
        usb_message_t msg;
        msg.type   = _assembly.msg_type;
        msg.length = _assembly.received_len;
        memcpy(msg.payload, _assembly.buf, _assembly.received_len);
        msg.valid  = true;

        _queue_push(&msg);

        DEBUG_PRINTF("[USB] Mensaje %s completo (%lu bytes)\r\n",
            usb_msg_type_name((usb_msg_type_t)msg.type),
            msg.length);

        /* Limpiar el estado de ensamblado */
        memset(&_assembly, 0, sizeof(_assembly));
    }
}


/* =========================================================
 * CALLBACKS DEL HAL USB (sobreescriben los "weak" del HAL)
 * ========================================================= */

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd) {
    (void)hpcd;
    _connected = true;
    DEBUG_PRINT("[USB] Host conectado — iniciando enumeración\r\n");
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd) {
    (void)hpcd;
    _connected = false;
    memset(&_assembly, 0, sizeof(_assembly));
    DEBUG_PRINT("[USB] Host desconectado\r\n");
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd) {
    /* Configurar endpoint de control EP0 */
    HAL_PCD_EP_Open(hpcd, 0x00, 64, EP_TYPE_CTRL);
    HAL_PCD_EP_Open(hpcd, 0x80, 64, EP_TYPE_CTRL);
    /* Endpoint HID IN (host ← device) */
    HAL_PCD_EP_Open(hpcd, 0x81, HID_REPORT_SIZE, EP_TYPE_INTR);
    /* Endpoint HID OUT (host → device) */
    HAL_PCD_EP_Open(hpcd, 0x01, HID_REPORT_SIZE, EP_TYPE_INTR);
    /* Preparar la recepción del primer chunk */
    HAL_PCD_EP_Receive(hpcd, 0x01, _rx_buf, HID_REPORT_SIZE);
    DEBUG_PRINT("[USB] Reset USB — endpoints configurados\r\n");
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum) {
    if (epnum == 0x01) {
        /* Datos recibidos del host en EP1 OUT */
        _process_chunk(_rx_buf);
        /* Re-armar el endpoint para el siguiente chunk */
        HAL_PCD_EP_Receive(hpcd, 0x01, _rx_buf, HID_REPORT_SIZE);
    }
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd) {
    uint8_t *setup = (uint8_t *)hpcd->Setup;
    uint8_t  req_type = setup[0];
    uint8_t  request  = setup[1];
    uint16_t value    = (setup[3] << 8) | setup[2];
    uint16_t length   = (setup[7] << 8) | setup[6];

    /* Manejar solicitudes estándar de enumeración */
    if (req_type == 0x80 && request == 0x06) {
        /* GET_DESCRIPTOR */
        uint8_t desc_type = (value >> 8) & 0xFF;
        if (desc_type == 0x01) {
            /* Device Descriptor */
            uint16_t send_len = length < sizeof(_device_desc) ?
                                length : sizeof(_device_desc);
            HAL_PCD_EP_Transmit(hpcd, 0x80,
                (uint8_t *)_device_desc, send_len);
            _connected = true;
        } else if (desc_type == 0x22) {
            /* HID Report Descriptor */
            uint16_t send_len = length < sizeof(_hid_report_desc) ?
                                length : sizeof(_hid_report_desc);
            HAL_PCD_EP_Transmit(hpcd, 0x80,
                (uint8_t *)_hid_report_desc, send_len);
        } else {
            /* Descriptor no soportado — stall */
            HAL_PCD_EP_SetStall(hpcd, 0x80);
        }
    } else if (req_type == 0x00 && request == 0x05) {
        /* SET_ADDRESS */
        HAL_PCD_SetAddress(hpcd, value & 0x7F);
        HAL_PCD_EP_Transmit(hpcd, 0x80, NULL, 0);
    } else if (req_type == 0x00 && request == 0x09) {
        /* SET_CONFIGURATION — enumeración completa */
        HAL_PCD_EP_Transmit(hpcd, 0x80, NULL, 0);
        DEBUG_PRINT("[USB] Enumeración completada\r\n");
    } else {
        HAL_PCD_EP_Transmit(hpcd, 0x80, NULL, 0);
    }
    (void)length;
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd) {
    (void)hpcd;
    /* SOF cada 1ms confirma conexión activa */
}


/* =========================================================
 * API PÚBLICA
 * ========================================================= */

bool usb_hid_init(void) {
    DEBUG_PRINT("[USB] Inicializando HID stack...\r\n");

    /* Configurar GPIO PA11/PA12 como USB OTG FS */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF10_OTG_FS;
    HAL_GPIO_Init(GPIOA, &gpio);

    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();

    /* Configurar el controlador PCD */
    hpcd_USB_OTG_FS.Instance                 = USB_OTG_FS;
    hpcd_USB_OTG_FS.Init.dev_endpoints       = 4;
    hpcd_USB_OTG_FS.Init.speed               = PCD_SPEED_FULL;
    hpcd_USB_OTG_FS.Init.dma_enable          = DISABLE;
    hpcd_USB_OTG_FS.Init.phy_itface          = PCD_PHY_EMBEDDED;
    hpcd_USB_OTG_FS.Init.Sof_enable          = ENABLE;
    hpcd_USB_OTG_FS.Init.low_power_enable    = DISABLE;
    hpcd_USB_OTG_FS.Init.lpm_enable          = DISABLE;
    hpcd_USB_OTG_FS.Init.vbus_sensing_enable = DISABLE;
    hpcd_USB_OTG_FS.Init.use_dedicated_ep1   = DISABLE;

    if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK) {
        DEBUG_PRINT("[USB] ERROR: HAL_PCD_Init falló\r\n");
        return false;
    }

    /* Configurar FIFOs */
    HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_FS, 0x80);
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 0, 0x20);
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 1, 0x40);

    /* Habilitar interrupción */
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);

    HAL_PCD_Start(&hpcd_USB_OTG_FS);

    _initialized = true;
    DEBUG_PRINT("[USB] HID iniciado en PA11/PA12. Conectar cable USB-C.\r\n");
    DEBUG_PRINT("[USB] Verificar: trezorctl list\r\n");
    return true;
}

void usb_hid_poll(void) {
    /* El HAL USB es dirigido por interrupciones (OTG_FS_IRQHandler).
     * Este poll solo necesita manejar timeouts y estado de conexión. */
    (void)0;
}

bool usb_hid_has_message(void) {
    return (_rx_count > 0);
}

bool usb_hid_read_message(usb_message_t *msg) {
    return _queue_pop(msg);
}

bool usb_hid_send_message(usb_msg_type_t type,
                           const uint8_t *payload, uint32_t length) {
    if (!_initialized) return false;

    uint8_t chunk[HID_REPORT_SIZE];
    uint32_t offset = 0;
    bool first = true;

    while (first || offset < length) {
        memset(chunk, 0, sizeof(chunk));
        chunk[0] = HID_MARKER;  /* '?' */

        if (first) {
            /* Primer chunk con header del mensaje */
            chunk[1] = HID_MAGIC_1;
            chunk[2] = HID_MAGIC_2;
            chunk[3] = (uint8_t)(type >> 8);
            chunk[4] = (uint8_t)(type & 0xFF);
            chunk[5] = (uint8_t)(length >> 24);
            chunk[6] = (uint8_t)(length >> 16);
            chunk[7] = (uint8_t)(length >>  8);
            chunk[8] = (uint8_t)(length & 0xFF);

            /* Copiar primeros 55 bytes del payload */
            uint32_t copy = (length < 55) ? length : 55;
            if (payload && copy > 0) {
                memcpy(chunk + 9, payload + offset, copy);
                offset += copy;
            }
            first = false;
        } else {
            /* Chunks de continuación */
            uint32_t remaining = length - offset;
            uint32_t copy = (remaining < 63) ? remaining : 63;
            if (payload && copy > 0) {
                memcpy(chunk + 1, payload + offset, copy);
                offset += copy;
            }
        }

        /* Enviar el chunk por EP1 IN */
        HAL_StatusTypeDef st = HAL_PCD_EP_Transmit(
            &hpcd_USB_OTG_FS, 0x81, chunk, HID_REPORT_SIZE
        );
        if (st != HAL_OK) return false;

        /* Esperar a que el endpoint esté listo para el siguiente chunk */
        uint32_t timeout = HAL_GetTick() + 100;
        while (HAL_GetTick() < timeout) {
            if (hpcd_USB_OTG_FS.IN_ep[1].xfer_count >= HID_REPORT_SIZE) break;
        }

        if (offset >= length && !first) break;
    }

    return true;
}

bool usb_hid_send_success(const char *msg_text) {
    /* Mensaje protobuf Success mínimo:
     * field 1 (message) = string del mensaje */
    uint8_t payload[64] = {0};
    uint32_t len = 0;

    if (msg_text && msg_text[0]) {
        /* Protobuf field 1, wire type 2 (length-delimited) */
        uint8_t msg_len = (uint8_t)strlen(msg_text);
        payload[len++] = 0x0A;       /* field 1, type 2 */
        payload[len++] = msg_len;
        memcpy(payload + len, msg_text, msg_len);
        len += msg_len;
    }

    return usb_hid_send_message(MSG_SUCCESS, payload, len);
}

bool usb_hid_send_failure(uint32_t code, const char *msg_text) {
    uint8_t payload[128] = {0};
    uint32_t len = 0;

    /* field 1: code (varint) */
    payload[len++] = 0x08;  /* field 1, type 0 (varint) */
    payload[len++] = (uint8_t)(code & 0x7F);

    /* field 2: message (string) */
    if (msg_text && msg_text[0]) {
        uint8_t msg_len = (uint8_t)strlen(msg_text);
        payload[len++] = 0x12;  /* field 2, type 2 */
        payload[len++] = msg_len;
        memcpy(payload + len, msg_text, msg_len);
        len += msg_len;
    }

    return usb_hid_send_message(MSG_FAILURE, payload, len);
}

bool usb_hid_is_connected(void) {
    return _connected;
}

const char *usb_msg_type_name(usb_msg_type_t type) {
    switch (type) {
        case MSG_INITIALIZE:     return "Initialize";
        case MSG_PING:           return "Ping";
        case MSG_SUCCESS:        return "Success";
        case MSG_FAILURE:        return "Failure";
        case MSG_FEATURES:       return "Features";
        case MSG_GET_ADDRESS:    return "GetAddress";
        case MSG_ADDRESS:        return "Address";
        case MSG_SIGN_TX:        return "SignTx";
        case MSG_TX_REQUEST:     return "TxRequest";
        case MSG_TX_ACK:         return "TxAck";
        case MSG_BUTTON_REQUEST: return "ButtonRequest";
        case MSG_BUTTON_ACK:     return "ButtonAck";
        case MSG_PIN_REQUEST:    return "PinRequest";
        case MSG_PIN_ACK:        return "PinAck";
        case MSG_CANCEL:         return "Cancel";
        case MSG_WIPE_DEVICE:    return "WipeDevice";
        case MSG_RESET_DEVICE:   return "ResetDevice";
        default:                 return "Unknown";
    }
}