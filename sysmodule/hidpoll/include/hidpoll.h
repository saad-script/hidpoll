/*
 * hidpoll - runtime HID/USB polling-rate patcher for the Nintendo Switch (Atmosphere).
 *
 * Service name: "hidpoll"   (plain cmif, no domains)
 *
 *  cmd 0  GetStatus()            -> HidpollStatus
 *  cmd 1  SetHidRate(u32 hz)     hid: Npad sampler + USB-HID (Ahid) poll tasks   (20..2000)
 *  cmd 2  SetUsbRate(u32 hz)     usb: xHCI full-speed interrupt interval (0 = honor bInterval)
 *  cmd 3  SetRate(u32 hz)        both of the above
 *  cmd 4  Reapply()              re-run the patcher with current settings
 *
 * All setters persist to sdmc:/config/hidpoll/config.ini and apply immediately.
 * usb changes only affect endpoints created afterwards (replug the device).
 */
#pragma once
#include <switch.h>

#define HIDPOLL_SERVICE_NAME  "hidpoll"
#define HIDPOLL_API_VERSION   1

#define HIDPOLL_HID_HZ_MIN    20
#define HIDPOLL_HID_HZ_MAX    2000
#define HIDPOLL_USB_HZ_MAX    1000   /* full-speed floor is 1 ms */

/* stock (unpatched) rates -- Npad sampler runs every 5ms (200 Hz) and usb
 * honors each endpoint's bInterval descriptor. Frontends use these as the
 * "reset to defaults" target. */
#define HIDPOLL_HID_HZ_DEFAULT 200
#define HIDPOLL_USB_HZ_DEFAULT 0

typedef enum {
    HidpollCmd_GetStatus  = 0,
    HidpollCmd_SetHidRate = 1,
    HidpollCmd_SetUsbRate = 2,
    HidpollCmd_SetRate    = 3,
    HidpollCmd_Reapply    = 4,
} HidpollCmd;

/* bit flags for HidpollStatus.flags */
enum {
    HidpollFlag_HidTextPatched   = 1u << 0,   /* Npad + Ahid interval constants rewritten */
    HidpollFlag_HidLivePoked     = 1u << 1,   /* running task periods updated (build-id known) */
    HidpollFlag_UsbTextPatched   = 1u << 2,   /* xHCI interval computation rewritten */
    HidpollFlag_HidBuildIdKnown  = 1u << 3,
    HidpollFlag_UsbFound         = 1u << 4,
    HidpollFlag_HidFound         = 1u << 5,
    HidpollFlag_HidLayoutRejected = 1u << 6,  /* build id known but live-task layout failed sanity check;
                                                 live poke skipped (offsets in HID_LAYOUTS are stale) */
};

typedef struct {
    u32 api_version;
    u32 hid_hz;          /* current hid target (Hz) */
    u32 usb_hz;          /* current usb target (Hz), 0 = honor descriptor */
    u32 flags;           /* HidpollFlag_* */
    Result last_hid_rc;  /* result of last hid patch attempt */
    Result last_usb_rc;  /* result of last usb patch attempt */
    u32 hid_live_tasks;  /* number of running task objects whose period was updated */
    u32 reserved;
} HidpollStatus;

/* Result module used for hidpoll-specific errors */
#define HIDPOLL_RESULT_MODULE  444
#define HIDPOLL_RC(desc)       MAKERESULT(HIDPOLL_RESULT_MODULE, (desc))
enum {
    HidpollErr_InvalidCommand   = 1,
    HidpollErr_InvalidArgument  = 2,
    HidpollErr_ProcessNotFound  = 3,
    HidpollErr_PatternNotFound  = 4,
    HidpollErr_PatternAmbiguous = 5,
    HidpollErr_DebugFailed      = 6,
};

/* ---------------------------------------------------------------------------
 * Client helpers (header-only). Link against libnx; call from an overlay/app:
 *
 *   Service s; hidpollInitialize(&s);
 *   hidpollSetRate(&s, 1000);
 *   HidpollStatus st; hidpollGetStatus(&s, &st);
 *   hidpollExit(&s);
 * ------------------------------------------------------------------------- */
#ifndef HIDPOLL_NO_CLIENT
NX_INLINE Result hidpollInitialize(Service* s) { return smGetService(s, HIDPOLL_SERVICE_NAME); }
NX_INLINE void   hidpollExit(Service* s)       { serviceClose(s); }
NX_INLINE Result hidpollGetStatus(Service* s, HidpollStatus* out) { return serviceDispatchOut(s, HidpollCmd_GetStatus, *out); }
NX_INLINE Result hidpollSetHidRate(Service* s, u32 hz) { return serviceDispatchIn(s, HidpollCmd_SetHidRate, hz); }
NX_INLINE Result hidpollSetUsbRate(Service* s, u32 hz) { return serviceDispatchIn(s, HidpollCmd_SetUsbRate, hz); }
NX_INLINE Result hidpollSetRate(Service* s, u32 hz)    { return serviceDispatchIn(s, HidpollCmd_SetRate, hz); }
NX_INLINE Result hidpollReapply(Service* s)            { return serviceDispatch(s, HidpollCmd_Reapply); }
#endif
