// TinyUSB host for usbinput.c: keyboards and mice, through a hub (the dock)
#pragma once
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_PICO
#endif
#define CFG_TUH_ENABLED             1
#define CFG_TUH_MAX_SPEED           OPT_MODE_FULL_SPEED
#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_HUB                 3
#define CFG_TUH_DEVICE_MAX          8
#define CFG_TUH_ENDPOINT_MAX        8
#define CFG_TUH_INTERFACE_MAX       8
#define CFG_TUH_HID                 6       // HID interfaces, all devices together
#define CFG_TUH_HID_EPIN_BUFSIZE    64
#define CFG_TUH_HID_EPOUT_BUFSIZE   64

// ...and a MIDI device (usbmidi_dev.c): with a computer on the port, the
// engine's MIDI goes to it. One role at a time, chosen at the start.
#define CFG_TUD_ENABLED             1
#define CFG_TUD_MAX_SPEED           OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE      64
#define CFG_TUD_MIDI                1
#define CFG_TUD_MIDI_RX_BUFSIZE     64
#define CFG_TUD_MIDI_TX_BUFSIZE     1024
