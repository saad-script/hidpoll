#include "state.hpp"

const char* const TAB_NAMES[TAB_COUNT] = { "Settings", "Poll Rate Test", "Status" };

const u32 HID_PRESETS[] = { 125, 200, 250, 500, 1000 };
const u32 USB_PRESETS[] = { 0,   125, 250, 500, 1000 };
const int HID_PRESET_COUNT = sizeof(HID_PRESETS) / sizeof(HID_PRESETS[0]);
const int USB_PRESET_COUNT = sizeof(USB_PRESETS) / sizeof(USB_PRESETS[0]);

int nearest_idx(const u32* arr, int n, u32 v) {
    int best = 0;
    u32 best_d = (u32)-1;
    for (int i = 0; i < n; i++) {
        u32 d = arr[i] > v ? arr[i] - v : v - arr[i];
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

AppState A;
