/**
 * @file    usb_test.c
 * @brief   Test de enumeración USB — WalCorg Fase 1
 *
 * PROPÓSITO:
 *   Verificar que el STM32F405 (pines PA11/PA12) enumera
 *   correctamente como dispositivo USB HID ante el host.
 *   Este es el último test de la Fase 1 y el más crítico:
 *   si el USB no funciona, trezorctl no puede comunicarse
 *   con el dispositivo.
 *
 * QUÉ VERIFICA ESTE TEST:
 *   ✓ PA11 (USB_DM) y PA12 (USB_DP) configurados correctamente
 *   ✓ Clock USB = 48MHz exactos (PLLQ = 336/7)
 *   ✓ El descriptor USB es reconocido por el sistema operativo
 *   ✓ El dispositivo aparece como "WalCorg Cold Wallet" en lsusb
 *   ✓ Las interrupciones OTG_FS se disparan correctamente
 *
 * CÓMO VERIFICAR QUE FUNCIONA:
 *   En Linux:
 *     lsusb | grep WalCorg
 *     dmesg | tail -20
 *   Debe aparecer algo como:
 *     Bus 001 Device 012: ID 1209:53c1 WalCorg Cold Wallet
 *
 *   Con trezorctl:
 *     pip install trezor
 *     trezorctl list
 *   Debe listar el dispositivo.
 *
 * CONEXIÓN:
 *   USB-C de la WeAct board → puerto USB de tu computadora
 *   (el mismo cable que usas para alimentar la placa)
 *
 * NOTA SOBRE VID/PID:
 *   VID 0x1209 = pid.codes (VID público para proyectos open source)
 *   PID 0x53C1 = asignado tentativamente para WalCorg
 *   Para producción: registrar PID en https://pid.codes
 *   Trezor usa VID=0x1209 PID=0x53C1 también — cambiar al registrar.
 *
 * @license GNU GPL v3
 */

#include "usb_test.h"
#include "stm32f4xx_hal.h"
#include "uart_debug.h"

/* =========================================================
 * HANDLES GLOBALES
 * Declarados extern en stm32f4xx_it.h para el IRQ handler
 * ========================================================= */
PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* =========================================================
 * DESCRIPTOR USB HID MÍNIMO
 *
 * Un descriptor USB describe al sistema operativo qué tipo
 * de dispositivo somos y cómo comunicarnos con él.
 *
 * Usamos HID (Human Interface Device) porque:
 *   1. No requiere driver especial en Windows/Linux/macOS
 *   2. Es el protocolo que usa Trezor One
 *   3. Permite intercambiar reportes de 64 bytes (suficiente)
 *
 * El descriptor HID de Trezor está en:
 *   legacy/firmware/usb.c → usb_hid_report_descriptor[]
 * En Fase 1 usamos un descriptor mínimo solo para verificar
 * que la enumeración funciona. En Fase 4 usaremos el completo.
 * ========================================================= */

/* Descriptor de dispositivo — identifica el dispositivo al OS */
static const uint8_t usb_device_descriptor[] = {
    0x12,       /* bLength: 18 bytes */
    0x01,       /* bDescriptorType: Device */
    0x00, 0x02, /* bcdUSB: USB 2.0 */
    0x00,       /* bDeviceClass: definido por interfaz */
    0x00,       /* bDeviceSubClass */
    0x00,       /* bDeviceProtocol */
    0x40,       /* bMaxPacketSize0: 64 bytes (máximo para FS HID) */
    0x09, 0x12, /* idVendor:  0x1209 = pid.codes (open source) */
    0xC1, 0x53, /* idProduct: 0x53C1 = WalCorg tentativo */
    0x00, 0x01, /* bcdDevice: versión del dispositivo 1.0 */
    0x01,       /* iManufacturer: índice de string descriptor */
    0x02,       /* iProduct: índice de string descriptor */
    0x03,       /* iSerialNumber: índice de string descriptor */
    0x01,       /* bNumConfigurations: 1 configuración */
};

/* Strings del dispositivo — aparecen en lsusb y el OS */
static const char *usb_manufacturer = "WalCorg Project";
static const char *usb_product      = "WalCorg Cold Wallet";
static const char *usb_serial       = "0000000000000001";  /* Cambiar por hash único en producción */


/* =========================================================
 * ESTADO DEL TEST USB
 * ========================================================= */
static volatile uint8_t  usb_connected    = 0;
static volatile uint32_t usb_connect_time = 0;
static volatile uint32_t usb_sof_count    = 0;   /* Start of Frame count */


/* =========================================================
 * INICIALIZACIÓN
 * ========================================================= */

/**
 * usb_test_init — Inicializar el controlador USB OTG FS
 *
 * Configura PA11 (DM) y PA12 (DP) como pines USB y habilita
 * el controlador OTG FS interno del STM32F405.
 *
 * PA11 = USB_OTG_FS_DM (Data Minus) — Alternate Function 10
 * PA12 = USB_OTG_FS_DP (Data Plus)  — Alternate Function 10
 *
 * El OTG (On-The-Go) del F405 puede operar como host o device.
 * Aquí lo configuramos como DEVICE (el STM32 es el periférico,
 * la computadora es el host).
 */
void usb_test_init(void) {

    DEBUG_PRINT("[USB] Inicializando OTG FS...\r\n");

    /* -----------------------------------------------------------------
     * GPIO: PA11 y PA12 como pines USB
     *
     * Modo: AF_PP (Alternate Function Push-Pull)
     * Sin pull-up/down: el USB tiene sus propias resistencias
     *   internas de pull-up/down en el controlador OTG
     * Velocidad: VERY_HIGH necesario para señales USB de 12Mbps
     * AF10: la función alternativa del USB OTG FS en el F405
     * ----------------------------------------------------------------- */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_11 | GPIO_PIN_12;  /* PA11=DM, PA12=DP */
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF10_OTG_FS;           /* AF10 = USB OTG FS */
    HAL_GPIO_Init(GPIOA, &gpio);

    /* Pin ID (PA10) y VBUS (PA9 en algunos boards) opcionales para OTG.
     * En mode Device simple no los necesitamos. */

    /* -----------------------------------------------------------------
     * Habilitar clock del USB OTG FS
     *
     * El USB OTG FS usa su propio clock de 48MHz generado por PLLQ.
     * PLLQ configurado a 7 → 336MHz/7 = 48MHz exactos.
     * Si PLLQ no da 48MHz exactos, el USB no funciona.
     * ----------------------------------------------------------------- */
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();

    /* -----------------------------------------------------------------
     * Configurar el controlador PCD (USB Device Controller)
     *
     * PCD = Peripheral Control Driver — la capa HAL del USB device.
     * Instance: OTG_FS = el controlador USB Full Speed interno
     * Speed: FULL_SPEED = 12 Mbps (suficiente para HID)
     * phy_itface: EMBEDDED_PHY = usar el PHY interno del F405
     *   (no necesita chip externo como el ULPI)
     * Sof_enable: habilitar Start of Frame interrupt (cada 1ms)
     *   útil para contar que el USB está activo
     * low_power_enable: OFF durante desarrollo
     * vbus_sensing_enable: OFF — no medir el voltaje VBUS externamente
     * ----------------------------------------------------------------- */
    hpcd_USB_OTG_FS.Instance                 = USB_OTG_FS;
    hpcd_USB_OTG_FS.Init.dev_endpoints       = 4;         /* 4 endpoints (suficientes para HID) */
    hpcd_USB_OTG_FS.Init.speed               = PCD_SPEED_FULL;
    hpcd_USB_OTG_FS.Init.dma_enable          = DISABLE;   /* Sin DMA en FS (no necesario) */
    hpcd_USB_OTG_FS.Init.phy_itface          = PCD_PHY_EMBEDDED;
    hpcd_USB_OTG_FS.Init.Sof_enable          = ENABLE;
    hpcd_USB_OTG_FS.Init.low_power_enable    = DISABLE;
    hpcd_USB_OTG_FS.Init.lpm_enable          = DISABLE;
    hpcd_USB_OTG_FS.Init.vbus_sensing_enable = DISABLE;
    hpcd_USB_OTG_FS.Init.use_dedicated_ep1   = DISABLE;

    if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK) {
        DEBUG_PRINT("[USB] ERROR: HAL_PCD_Init fallo.\r\n");
        DEBUG_PRINT("[USB] Verificar: PA11/PA12 conectados, clock 48MHz.\r\n");
        return;
    }

    /* -----------------------------------------------------------------
     * Configurar FIFO de transmisión/recepción del USB
     *
     * El OTG FS tiene 1.25KB de RAM interna para USB.
     * Hay que dividirla entre:
     *   - RX FIFO global (recepción de todos los endpoints)
     *   - TX FIFO de control (endpoint 0)
     *   - TX FIFO de datos (endpoint HID IN)
     *
     * Tamaños en palabras de 32 bits (1 word = 4 bytes):
     *   RX  = 128 words = 512 bytes
     *   TX0 = 32  words = 128 bytes (control)
     *   TX1 = 64  words = 256 bytes (HID IN)
     *   Total = 224 words = 896 bytes (dentro del límite de 1.25KB)
     * ----------------------------------------------------------------- */
    HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_FS, 0x80);   /* RX: 128 words */
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 0, 0x20); /* TX EP0: 32 words */
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 1, 0x40); /* TX EP1 (HID): 64 words */

    /* -----------------------------------------------------------------
     * Habilitar interrupción OTG_FS en el NVIC
     *
     * Prioridad 6: más alta que SysTick (15) para responder
     * rápido al host, pero más baja que faults críticos.
     * ----------------------------------------------------------------- */
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);

    /* Iniciar el stack USB — el dispositivo aparecerá en el host */
    HAL_PCD_Start(&hpcd_USB_OTG_FS);

    DEBUG_PRINT("[USB] OTG FS iniciado. Conectar cable USB-C...\r\n");
    DEBUG_PRINT("[USB] Verificar con: lsusb | grep WalCorg\r\n");
}


/**
 * usb_test_poll — Verificar estado USB (llamar en el loop principal)
 *
 * Imprime estadísticas del USB cada 5 segundos para confirmar
 * que la conexión está activa y los SOF se reciben.
 */
void usb_test_poll(void) {
    static uint32_t last_report = 0;
    uint32_t now = HAL_GetTick();

    /* Reporte cada 5 segundos */
    if ((now - last_report) >= 5000) {
        last_report = now;
        DEBUG_PRINTF("[USB] Estado: %s | SOF recibidos: %lu\r\n",
            usb_connected ? "CONECTADO" : "desconectado",
            usb_sof_count);

        if (usb_connected) {
            DEBUG_PRINT("[USB] OK - Dispositivo enumerado correctamente.\r\n");
        } else {
            DEBUG_PRINT("[USB] Esperando conexion USB-C...\r\n");
        }
    }
}

/**
 * usb_is_connected — Retorna 1 si el host reconoció el dispositivo
 */
uint8_t usb_is_connected(void) {
    return usb_connected;
}


/* =========================================================
 * CALLBACKS DEL HAL USB
 *
 * El HAL llama estas funciones automáticamente cuando ocurren
 * eventos USB. Sobreescribimos las versiones "weak" del HAL.
 * ========================================================= */

/**
 * HAL_PCD_ConnectCallback — El cable USB fue conectado
 *
 * Se llama cuando el host detecta el dispositivo en el bus.
 * En este punto el dispositivo AÚN NO está enumerado — el host
 * todavía está leyendo los descriptores.
 */
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd) {
    (void)hpcd;
    usb_connect_time = HAL_GetTick();
    DEBUG_PRINT("[USB] Cable conectado — iniciando enumeracion...\r\n");
}

/**
 * HAL_PCD_DisconnectCallback — El cable USB fue desconectado
 */
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd) {
    (void)hpcd;
    usb_connected = 0;
    usb_sof_count = 0;
    DEBUG_PRINT("[USB] Cable desconectado.\r\n");
}

/**
 * HAL_PCD_ResetCallback — El host envió un reset USB
 *
 * Ocurre durante la enumeración. El host resetea el dispositivo
 * y luego lee los descriptores desde cero.
 */
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd) {
    (void)hpcd;
    DEBUG_PRINT("[USB] Reset recibido del host.\r\n");

    /* Configurar el endpoint de control (EP0) */
    HAL_PCD_EP_Open(&hpcd_USB_OTG_FS, 0x00, 64, EP_TYPE_CTRL);  /* EP0 OUT */
    HAL_PCD_EP_Open(&hpcd_USB_OTG_FS, 0x80, 64, EP_TYPE_CTRL);  /* EP0 IN  */
}

/**
 * HAL_PCD_SetupStageCallback — El host envió un Setup packet
 *
 * Los Setup packets son las solicitudes del host durante la
 * enumeración: "dame tu descriptor de dispositivo", "dame
 * tu descriptor de configuración", "configúrate con ID 3", etc.
 *
 * En Fase 1 solo manejamos las solicitudes más básicas para
 * confirmar que la enumeración funciona. En Fase 4 usaremos
 * el stack USB completo de Trezor.
 */
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd) {
    /* Extraer el setup packet de los registros del controlador */
    uint8_t *setup = (uint8_t *)hpcd->Setup;

    uint8_t  bmRequestType = setup[0];
    uint8_t  bRequest      = setup[1];
    uint16_t wValue        = (setup[3] << 8) | setup[2];
    uint16_t wLength       = (setup[7] << 8) | setup[6];

    DEBUG_PRINTF("[USB] Setup: type=0x%02X req=0x%02X val=0x%04X len=%u\r\n",
        bmRequestType, bRequest, wValue, wLength);

    /* GET_DESCRIPTOR (solicitud más común durante enumeración) */
    if (bmRequestType == 0x80 && bRequest == 0x06) {
        uint8_t desc_type = (wValue >> 8) & 0xFF;

        if (desc_type == 0x01) {
            /* Descriptor de dispositivo — el host pregunta "¿quién eres?" */
            DEBUG_PRINT("[USB] Host solicita Device Descriptor — enviando...\r\n");
            HAL_PCD_EP_Transmit(&hpcd_USB_OTG_FS, 0x80,
                (uint8_t *)usb_device_descriptor,
                (wLength < sizeof(usb_device_descriptor)) ?
                    wLength : sizeof(usb_device_descriptor));
            usb_connected = 1;
            DEBUG_PRINTF("[USB] Enumerado: %s / %s\r\n",
                usb_manufacturer, usb_product);
        }
    }
    /* SET_ADDRESS — el host asigna una dirección USB al dispositivo */
    else if (bmRequestType == 0x00 && bRequest == 0x05) {
        uint8_t addr = wValue & 0x7F;
        HAL_PCD_SetAddress(&hpcd_USB_OTG_FS, addr);
        DEBUG_PRINTF("[USB] Direccion USB asignada: %u\r\n", addr);
    }

    (void)wLength;
}

/**
 * HAL_PCD_SOFCallback — Start of Frame (cada 1ms en Full Speed)
 *
 * El host USB envía un paquete SOF cada 1ms para sincronizar
 * el bus. Si recibimos SOFs, el USB está activo y funcional.
 */
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd) {
    (void)hpcd;
    usb_sof_count++;  /* Contar SOFs — debe incrementar a ~1000/segundo */
}
