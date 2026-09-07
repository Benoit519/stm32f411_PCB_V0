/**
  ******************************************************************************
  * @file    usbd_midi.c
  * @brief   USB-MIDI 1.0 class driver (USB Audio Class 1.0 / MIDIStreaming 1.0).
  *
  *          Topology:
  *            MIDI IN Jack (Embedded, ID=1)  --source-->  MIDI OUT Jack (Embedded, ID=2) --> Bulk IN EP
  *            Bulk OUT EP --> MIDI IN Jack (Embedded, ID=3)   (received data is discarded)
  *          The accordion only ever produces notes (Bulk IN); the Bulk OUT
  *          side exists purely because Windows' native usbaudio.sys driver
  *          fails to start (Code 10) on a MIDIStreaming interface that only
  *          declares one direction.
  ******************************************************************************
  */
#include "usbd_midi.h"
#include "usbd_ctlreq.h"

static uint8_t USBD_MIDI_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_MIDI_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_MIDI_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_MIDI_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_MIDI_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t *USBD_MIDI_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_MIDI_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_MIDI_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_MIDI_GetDeviceQualifierDesc(uint16_t *length);

typedef struct
{
    uint8_t tx_buf[4];
    __IO uint8_t tx_busy;
    uint8_t rx_buf[MIDI_EPOUT_SIZE];
} USBD_MIDI_HandleTypeDef;

static USBD_MIDI_HandleTypeDef hmidi;

USBD_ClassTypeDef USBD_MIDI = {
    USBD_MIDI_Init,
    USBD_MIDI_DeInit,
    USBD_MIDI_Setup,
    NULL,                       /* EP0_TxSent */
    NULL,                       /* EP0_RxReady */
    USBD_MIDI_DataIn,
    USBD_MIDI_DataOut,
    NULL,                       /* SOF */
    NULL,                       /* IsoINIncomplete */
    NULL,                       /* IsoOUTIncomplete */
    USBD_MIDI_GetHSCfgDesc,
    USBD_MIDI_GetFSCfgDesc,
    USBD_MIDI_GetOtherSpeedCfgDesc,
    USBD_MIDI_GetDeviceQualifierDesc,
};

#if defined(__ICCARM__)
#pragma data_alignment=4
#endif
__ALIGN_BEGIN static uint8_t USBD_MIDI_CfgDesc[USB_MIDI_CONFIG_DESC_SIZ] __ALIGN_END =
{
    /* ---- Configuration Descriptor ---- */
    0x09, USB_DESC_TYPE_CONFIGURATION,
    LOBYTE(USB_MIDI_CONFIG_DESC_SIZ), HIBYTE(USB_MIDI_CONFIG_DESC_SIZ),
    0x02,                       /* bNumInterfaces: AudioControl + MIDIStreaming */
    0x01,                       /* bConfigurationValue */
    0x00,                       /* iConfiguration */
    0x80,                       /* bmAttributes: bus powered */
    0x32,                       /* bMaxPower: 100 mA */

    /* ---- Interface 0: Standard Audio Control ---- */
    0x09, USB_DESC_TYPE_INTERFACE,
    0x00,                       /* bInterfaceNumber */
    0x00,                       /* bAlternateSetting */
    0x00,                       /* bNumEndpoints */
    0x01,                       /* bInterfaceClass: AUDIO */
    0x01,                       /* bInterfaceSubClass: AUDIOCONTROL */
    0x00,                       /* bInterfaceProtocol */
    0x00,                       /* iInterface */

    /* ---- Class-specific AC Interface Header ---- */
    0x09, 0x24, 0x01,           /* CS_INTERFACE, HEADER */
    0x00, 0x01,                 /* bcdADC 1.00 */
    0x09, 0x00,                 /* wTotalLength (AC-only) = 9 */
    0x01,                       /* bInCollection: 1 streaming interface */
    0x01,                       /* baInterfaceNr(1) = interface 1 */

    /* ---- Interface 1: Standard MIDIStreaming ---- */
    0x09, USB_DESC_TYPE_INTERFACE,
    0x01,                       /* bInterfaceNumber */
    0x00,                       /* bAlternateSetting */
    0x02,                       /* bNumEndpoints: Bulk IN + Bulk OUT */
    0x01,                       /* bInterfaceClass: AUDIO */
    0x03,                       /* bInterfaceSubClass: MIDISTREAMING */
    0x00,                       /* bInterfaceProtocol */
    0x00,                       /* iInterface */

    /* ---- Class-specific MS Interface Header ---- */
    0x07, 0x24, 0x01,           /* CS_INTERFACE, MS_HEADER */
    0x00, 0x01,                 /* bcdMSC 1.00 */
    0x26, 0x00,                 /* wTotalLength (MS-specific) = 38 */

    /* MIDI IN Jack (Embedded), ID=1 - represents the firmware's note generator */
    0x06, 0x24, 0x02,           /* CS_INTERFACE, MIDI_IN_JACK */
    0x01,                       /* bJackType: EMBEDDED */
    0x01,                       /* bJackID */
    0x00,                       /* iJack */

    /* MIDI OUT Jack (Embedded), ID=2, fed by Jack 1 - feeds the Bulk IN endpoint */
    0x09, 0x24, 0x03,           /* CS_INTERFACE, MIDI_OUT_JACK */
    0x01,                       /* bJackType: EMBEDDED */
    0x02,                       /* bJackID */
    0x01,                       /* bNrInputPins */
    0x01,                       /* BaSourceID(1) = Jack 1 */
    0x01,                       /* BaSourcePin(1) */
    0x00,                       /* iJack */

    /* MIDI IN Jack (Embedded), ID=3 - fed by the Bulk OUT endpoint. Nothing
       downstream reads this (no MIDI OUT jack sourced from it) - Windows
       doesn't require one, it just needs the endpoint <-> jack association. */
    0x06, 0x24, 0x02,           /* CS_INTERFACE, MIDI_IN_JACK */
    0x01,                       /* bJackType: EMBEDDED */
    0x03,                       /* bJackID */
    0x00,                       /* iJack */

    /* ---- Standard Bulk IN Endpoint (bLength=7, NOT 9 - unlike interface
       descriptors, a standard endpoint descriptor has no iInterface-style
       trailing byte) ---- */
    0x07, USB_DESC_TYPE_ENDPOINT,
    MIDI_EPIN_ADDR,             /* bEndpointAddress */
    0x02,                       /* bmAttributes: BULK */
    LOBYTE(MIDI_EPIN_SIZE), HIBYTE(MIDI_EPIN_SIZE),
    0x00,                       /* bInterval */

    /* ---- Class-specific MS Bulk Data Endpoint Descriptor (IN) ---- */
    0x05, 0x25, 0x01,           /* CS_ENDPOINT, MS_GENERAL */
    0x01,                       /* bNumEmbMIDIJack */
    0x02,                       /* BaAssocJackID(1) = Jack 2 (the OUT jack) */

    /* ---- Standard Bulk OUT Endpoint ---- */
    0x07, USB_DESC_TYPE_ENDPOINT,
    MIDI_EPOUT_ADDR,            /* bEndpointAddress */
    0x02,                       /* bmAttributes: BULK */
    LOBYTE(MIDI_EPOUT_SIZE), HIBYTE(MIDI_EPOUT_SIZE),
    0x00,                       /* bInterval */

    /* ---- Class-specific MS Bulk Data Endpoint Descriptor (OUT) ---- */
    0x05, 0x25, 0x01,           /* CS_ENDPOINT, MS_GENERAL */
    0x01,                       /* bNumEmbMIDIJack */
    0x03,                       /* BaAssocJackID(1) = Jack 3 (the IN jack fed by this EP) */
};

#if defined(__ICCARM__)
#pragma data_alignment=4
#endif
__ALIGN_BEGIN static uint8_t USBD_MIDI_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
    USB_LEN_DEV_QUALIFIER_DESC,
    USB_DESC_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02,
    0x00, 0x00, 0x00,
    0x40,
    0x01,
    0x00,
};

static uint8_t USBD_MIDI_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    UNUSED(cfgidx);

    printf("USB-MIDI: SetConfiguration recu, endpoints 0x%02X/0x%02X ouverts\r\n",
           MIDI_EPIN_ADDR, MIDI_EPOUT_ADDR);

    USBD_LL_OpenEP(pdev, MIDI_EPIN_ADDR, USBD_EP_TYPE_BULK, MIDI_EPIN_SIZE);
    pdev->ep_in[MIDI_EPIN_ADDR & 0xFU].is_used = 1U;

    USBD_LL_OpenEP(pdev, MIDI_EPOUT_ADDR, USBD_EP_TYPE_BULK, MIDI_EPOUT_SIZE);
    pdev->ep_out[MIDI_EPOUT_ADDR & 0xFU].is_used = 1U;

    hmidi.tx_busy = 0U;
    pdev->pClassData = &hmidi;

    /* Arme la reception, meme si les donnees reçues sont ignorees (voir DataOut) */
    USBD_LL_PrepareReceive(pdev, MIDI_EPOUT_ADDR, hmidi.rx_buf, MIDI_EPOUT_SIZE);

    return (uint8_t)USBD_OK;
}

static uint8_t USBD_MIDI_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    UNUSED(cfgidx);

    USBD_LL_CloseEP(pdev, MIDI_EPIN_ADDR);
    pdev->ep_in[MIDI_EPIN_ADDR & 0xFU].is_used = 0U;

    USBD_LL_CloseEP(pdev, MIDI_EPOUT_ADDR);
    pdev->ep_out[MIDI_EPOUT_ADDR & 0xFU].is_used = 0U;

    pdev->pClassData = NULL;

    return (uint8_t)USBD_OK;
}

/* No class-specific control requests are needed: our Audio Control interface
   exposes no controls (no feature/selector unit) and MIDIStreaming has none
   either - only the standard requests already handled by USBD_Core apply. */
static uint8_t USBD_MIDI_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    switch (req->bmRequest & USB_REQ_TYPE_MASK)
    {
    case USB_REQ_TYPE_CLASS:
    case USB_REQ_TYPE_STANDARD:
        return (uint8_t)USBD_OK;

    default:
        USBD_CtlError(pdev, req);
        return (uint8_t)USBD_FAIL;
    }
}

static uint8_t USBD_MIDI_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    UNUSED(pdev);
    UNUSED(epnum);

    hmidi.tx_busy = 0U;
    return (uint8_t)USBD_OK;
}

static uint8_t USBD_MIDI_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    UNUSED(epnum);

    /* Rien n'est fait des donnees recues du host - on rearme juste la
       reception pour ne jamais bloquer l'endpoint OUT. */
    USBD_LL_PrepareReceive(pdev, MIDI_EPOUT_ADDR, hmidi.rx_buf, MIDI_EPOUT_SIZE);
    return (uint8_t)USBD_OK;
}

static uint8_t *USBD_MIDI_GetFSCfgDesc(uint16_t *length)
{
    *length = (uint16_t)sizeof(USBD_MIDI_CfgDesc);
    return USBD_MIDI_CfgDesc;
}

static uint8_t *USBD_MIDI_GetHSCfgDesc(uint16_t *length)
{
    /* Hardware is full-speed only (OTG_FS); return the same descriptor. */
    *length = (uint16_t)sizeof(USBD_MIDI_CfgDesc);
    return USBD_MIDI_CfgDesc;
}

static uint8_t *USBD_MIDI_GetOtherSpeedCfgDesc(uint16_t *length)
{
    *length = (uint16_t)sizeof(USBD_MIDI_CfgDesc);
    return USBD_MIDI_CfgDesc;
}

static uint8_t *USBD_MIDI_GetDeviceQualifierDesc(uint16_t *length)
{
    *length = (uint16_t)sizeof(USBD_MIDI_DeviceQualifierDesc);
    return USBD_MIDI_DeviceQualifierDesc;
}

/**
  * @brief  Sends one 4-byte USB-MIDI event packet on the Bulk IN endpoint.
  * @param  cable: virtual cable number (0-15) - always 0, we expose one MIDI port
  * @param  cin:   Code Index Number (e.g. MIDI_CIN_NOTE_ON / MIDI_CIN_NOTE_OFF)
  * @param  b1..b3: the raw MIDI message bytes (status|channel, data1, data2)
  * @retval USBD_OK, USBD_BUSY if a previous packet is still in flight (the
  *         event is then dropped - acceptable for sparse button events, and
  *         avoids blocking the caller if the host isn't reading fast enough).
  */
USBD_StatusTypeDef USBD_MIDI_SendPacket(USBD_HandleTypeDef *pdev, uint8_t cable,
                                         uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    if (pdev->dev_state != USBD_STATE_CONFIGURED)
    {
        return USBD_FAIL;
    }

    if (hmidi.tx_busy)
    {
        return USBD_BUSY;
    }

    hmidi.tx_busy = 1U;
    hmidi.tx_buf[0] = (uint8_t)((cable << 4) | (cin & 0x0FU));
    hmidi.tx_buf[1] = b1;
    hmidi.tx_buf[2] = b2;
    hmidi.tx_buf[3] = b3;

    return USBD_LL_Transmit(pdev, MIDI_EPIN_ADDR, hmidi.tx_buf, 4U);
}
