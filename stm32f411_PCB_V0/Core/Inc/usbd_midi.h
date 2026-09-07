/**
  ******************************************************************************
  * @file    usbd_midi.h
  * @brief   USB-MIDI 1.0 class driver (Audio Class 1.0 / MIDIStreaming 1.0).
  *          Functionally the accordion only ever sends notes (Bulk IN); the
  *          Bulk OUT endpoint is wired up but its data is simply discarded -
  *          Windows' native usbaudio.sys driver reliably fails to start
  *          (Code 10) on a MIDIStreaming interface that only has one
  *          direction, so both are exposed for compatibility.
  ******************************************************************************
  */
#ifndef __USBD_MIDI_H
#define __USBD_MIDI_H

#include "usbd_ioreq.h"

#define MIDI_EPIN_ADDR                 0x81U
#define MIDI_EPOUT_ADDR                0x01U
#define MIDI_EPIN_SIZE                 0x40U   /* 64 B, full-speed bulk max packet */
#define MIDI_EPOUT_SIZE                0x40U

#define USB_MIDI_CONFIG_DESC_SIZ       96U

/* USB-MIDI Code Index Number (CIN) values used by the accordion */
#define MIDI_CIN_NOTE_OFF              0x08U
#define MIDI_CIN_NOTE_ON               0x09U

/* MIDI status bytes (channel added in the low nibble by the caller) */
#define MIDI_STATUS_NOTE_OFF           0x80U
#define MIDI_STATUS_NOTE_ON            0x90U

extern USBD_ClassTypeDef USBD_MIDI;
#define USBD_MIDI_CLASS  (&USBD_MIDI)

USBD_StatusTypeDef USBD_MIDI_SendPacket(USBD_HandleTypeDef *pdev, uint8_t cable,
                                         uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3);

#endif /* __USBD_MIDI_H */
