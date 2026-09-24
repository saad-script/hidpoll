#pragma once
#include <string>
#include <switch.h>

extern "C" {
#include "hidpoll.h"
}

enum Tab { TAB_SETTINGS = 0, TAB_TEST = 1, TAB_STATUS = 2, TAB_COUNT };

extern const char* const TAB_NAMES[TAB_COUNT];

extern const u32 HID_PRESETS[];
extern const u32 USB_PRESETS[];
extern const int HID_PRESET_COUNT;
extern const int USB_PRESET_COUNT;

int nearest_idx(const u32* arr, int n, u32 v);

struct AppState {
    Service     svc{};
    bool        svc_ok = false;
    Result      svc_rc = 0;

    HidpollStatus st{};
    int hid_i = 0;
    int usb_i = 0;

    int tab = TAB_SETTINGS;
    int row = 0;
};
extern AppState A;
