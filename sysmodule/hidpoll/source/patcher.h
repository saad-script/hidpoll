#pragma once
#include <switch.h>
#include "hidpoll.h"

typedef struct {
    u32    flags;            // HidpollFlag_*
    Result hid_rc;
    Result usb_rc;
    u32    hid_live_tasks;
} PatchReport;

/* Apply hid patches: rewrite the 3 SetInterval constants (Npad activate, Ahid mode-1/mode-2)
 * and, if the hid build id is known, poke the periods of the running task objects. */
Result patcher_apply_hid(u32 hid_hz, PatchReport* rep);

/* Apply usb patch: force full-speed interrupt endpoint interval.
 * usb_hz == 0 restores the original instruction (honor bInterval). */
Result patcher_apply_usb(u32 usb_hz, PatchReport* rep);
