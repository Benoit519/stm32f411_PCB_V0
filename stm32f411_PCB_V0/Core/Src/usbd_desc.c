/**
  ******************************************************************************
  * @file    usbd_desc.c
  * @brief   USB-MIDI device descriptors (device / string) - based on ST's
  *          standard USBD_DescriptorsTypeDef pattern.
  ******************************************************************************
  */
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"

/* Private define ------------------------------------------------------------ */
/* VID reused from STMicroelectronics's own demo range (hobby/private-use board,
   not a commercial product - if this ever ships commercially, get a real VID/PID). */
#define USBD_VID                      0x0483
#define USBD_PID                      0x5779
#define USBD_LANGID_STRING            0x409
#define USBD_MANUFACTURER_STRING      "DIY Accordion"
#define USBD_PRODUCT_STRING           "Accordion MIDI"
#define USBD_CONFIGURATION_STRING     "MIDI Config"
#define USBD_INTERFACE_STRING         "MIDI Interface"

/* Private function prototypes ------------------------------------------------ */
static uint8_t *USBD_MIDI_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *USBD_MIDI_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *USBD_MIDI_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *USBD_MIDI_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *USBD_MIDI_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *USBD_MIDI_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *USBD_MIDI_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);

USBD_DescriptorsTypeDef MIDI_Desc = {
    USBD_MIDI_DeviceDescriptor,
    USBD_MIDI_LangIDStrDescriptor,
    USBD_MIDI_ManufacturerStrDescriptor,
    USBD_MIDI_ProductStrDescriptor,
    USBD_MIDI_SerialStrDescriptor,
    USBD_MIDI_ConfigStrDescriptor,
    USBD_MIDI_InterfaceStrDescriptor,
};

#if defined(__ICCARM__)
#pragma data_alignment=4
#endif
__ALIGN_BEGIN uint8_t USBD_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END = {
    0x12,                       /* bLength */
    USB_DESC_TYPE_DEVICE,       /* bDescriptorType */
    0x00, 0x02,                 /* bcdUSB = 2.00 */
    0x00,                       /* bDeviceClass    (defined at interface level for Audio class) */
    0x00,                       /* bDeviceSubClass */
    0x00,                       /* bDeviceProtocol */
    USB_MAX_EP0_SIZE,           /* bMaxPacketSize0 */
    LOBYTE(USBD_VID), HIBYTE(USBD_VID),
    LOBYTE(USBD_PID), HIBYTE(USBD_PID),
    0x00, 0x02,                 /* bcdDevice = 2.00 */
    USBD_IDX_MFC_STR,           /* Index of manufacturer string */
    USBD_IDX_PRODUCT_STR,       /* Index of product string */
    USBD_IDX_SERIAL_STR,        /* Index of serial number string */
    USBD_MAX_NUM_CONFIGURATION  /* bNumConfigurations */
};

#if defined(__ICCARM__)
#pragma data_alignment=4
#endif
__ALIGN_BEGIN uint8_t USBD_LangIDDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
    USB_LEN_LANGID_STR_DESC,
    USB_DESC_TYPE_STRING,
    LOBYTE(USBD_LANGID_STRING), HIBYTE(USBD_LANGID_STRING),
};

#if defined(__ICCARM__)
#pragma data_alignment=4
#endif
__ALIGN_BEGIN uint8_t USBD_StringSerial[USB_SIZ_STRING_SERIAL] __ALIGN_END = {
    USB_SIZ_STRING_SERIAL,
    USB_DESC_TYPE_STRING,
};

#if defined(__ICCARM__)
#pragma data_alignment=4
#endif
__ALIGN_BEGIN uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;

static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len);
static void Get_SerialNum(void);

static uint8_t *USBD_MIDI_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    *length = sizeof(USBD_DeviceDesc);
    return (uint8_t *)USBD_DeviceDesc;
}

static uint8_t *USBD_MIDI_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    *length = sizeof(USBD_LangIDDesc);
    return (uint8_t *)USBD_LangIDDesc;
}

static uint8_t *USBD_MIDI_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

static uint8_t *USBD_MIDI_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    USBD_GetString((uint8_t *)USBD_PRODUCT_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

static uint8_t *USBD_MIDI_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    *length = USB_SIZ_STRING_SERIAL;
    Get_SerialNum();
    return (uint8_t *)USBD_StringSerial;
}

static uint8_t *USBD_MIDI_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

static uint8_t *USBD_MIDI_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    USBD_GetString((uint8_t *)USBD_INTERFACE_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

static void Get_SerialNum(void)
{
    uint32_t deviceserial0, deviceserial1, deviceserial2;

    deviceserial0 = *(uint32_t *)DEVICE_ID1;
    deviceserial1 = *(uint32_t *)DEVICE_ID2;
    deviceserial2 = *(uint32_t *)DEVICE_ID3;

    deviceserial0 += deviceserial2;

    if (deviceserial0 != 0)
    {
        IntToUnicode(deviceserial0, &USBD_StringSerial[2], 8);
        IntToUnicode(deviceserial1, &USBD_StringSerial[18], 4);
    }
}

static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len)
{
    for (uint8_t idx = 0; idx < len; idx++)
    {
        uint8_t nibble = (value >> 28) & 0xF;
        pbuf[2 * idx] = (nibble < 0xA) ? (nibble + '0') : (nibble + 'A' - 10);
        value <<= 4;
        pbuf[2 * idx + 1] = 0;
    }
}
