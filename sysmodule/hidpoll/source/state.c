#include "state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

u32         g_hid_hz = 1000;
u32         g_usb_hz = 1000;
PatchReport g_rep    = {0};

static void trim_leading_ws(char** p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

static void clamp_config(void) {
    if (g_hid_hz < HIDPOLL_HID_HZ_MIN || g_hid_hz > HIDPOLL_HID_HZ_MAX) g_hid_hz = 1000;
    if (g_usb_hz > HIDPOLL_USB_HZ_MAX)                                  g_usb_hz = HIDPOLL_USB_HZ_MAX;
}

void config_load(void) {
    FILE* f = fopen(HP_CONFIG_PATH, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;

        char* key = line;
        char* val = eq + 1;
        trim_leading_ws(&key);

        u32 v = (u32)strtoul(val, NULL, 10);
        if      (strncmp(key, "hid_hz", 6) == 0) g_hid_hz = v;
        else if (strncmp(key, "usb_hz", 6) == 0) g_usb_hz = v;
    }
    fclose(f);

    clamp_config();
}

void config_save(void) {
    FILE* f = fopen(HP_CONFIG_PATH, "w");
    if (!f) return;

    fprintf(f, "# hidpoll configuration\n");
    fprintf(f, "# hid_hz: Npad sampler + USB-HID poll task rate (%u..%u)\n",
            HIDPOLL_HID_HZ_MIN, HIDPOLL_HID_HZ_MAX);
    fprintf(f, "# usb_hz: forced xHCI full-speed interrupt rate (1..%u), "
               "0 = honor device bInterval\n", HIDPOLL_USB_HZ_MAX);
    fprintf(f, "hid_hz=%u\nusb_hz=%u\n", g_hid_hz, g_usb_hz);
    fclose(f);
}

void apply_all(bool do_hid, bool do_usb) {
    if (do_hid) {
        g_rep.flags &= ~(HidpollFlag_HidTextPatched | HidpollFlag_HidLivePoked |
                         HidpollFlag_HidFound       | HidpollFlag_HidBuildIdKnown |
                         HidpollFlag_HidLayoutRejected);
        g_rep.hid_live_tasks = 0;
        patcher_apply_hid(g_hid_hz, &g_rep);
    }
    if (do_usb) {
        g_rep.flags &= ~(HidpollFlag_UsbTextPatched | HidpollFlag_UsbFound);
        patcher_apply_usb(g_usb_hz, &g_rep);
    }
}

void fill_status(HidpollStatus* out) {
    memset(out, 0, sizeof(*out));
    out->api_version    = HIDPOLL_API_VERSION;
    out->hid_hz         = g_hid_hz;
    out->usb_hz         = g_usb_hz;
    out->flags          = g_rep.flags;
    out->last_hid_rc    = g_rep.hid_rc;
    out->last_usb_rc    = g_rep.usb_rc;
    out->hid_live_tasks = g_rep.hid_live_tasks;
}
