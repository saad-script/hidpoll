#include "service.h"
#include "state.h"
#include "log.h"
#include "hidpoll.h"

#include <switch.h>
#include <string.h>

#define CMIF_IN_MAGIC  0x49434653u /* 'SFCI' */
#define CMIF_OUT_MAGIC 0x4F434653u /* 'SFCO' */

// ---------- reply ----------

static void write_response(Result rc, const void* out, size_t out_size) {
    void* tls = armGetTls();

    HipcRequest r = hipcMakeRequestInline(tls,
        .type           = 0,
        .num_data_words = (u32)((16 + sizeof(CmifOutHeader) + out_size + 3) / 4));

    CmifOutHeader* h = (CmifOutHeader*)cmifGetAlignedDataStart(r.data_words, tls);
    h->magic   = CMIF_OUT_MAGIC;
    h->version = 0;
    h->result  = rc;
    h->token   = 0;

    if (out && out_size) memcpy(h + 1, out, out_size);
}

// ---------- per-command handlers ----------

static Result cmd_set_hid_rate(const u32* args) {
    u32 hz = args[0];
    if (hz < HIDPOLL_HID_HZ_MIN || hz > HIDPOLL_HID_HZ_MAX) {
        return HIDPOLL_RC(HidpollErr_InvalidArgument);
    }
    g_hid_hz = hz;
    config_save();
    apply_all(true, false);
    return g_rep.hid_rc;
}

static Result cmd_set_usb_rate(const u32* args) {
    u32 hz = args[0];
    if (hz > HIDPOLL_USB_HZ_MAX) return HIDPOLL_RC(HidpollErr_InvalidArgument);

    g_usb_hz = hz;
    config_save();
    apply_all(false, true);
    return g_rep.usb_rc;
}

static Result cmd_set_rate(const u32* args) {
    u32 hz = args[0];
    if (hz < HIDPOLL_HID_HZ_MIN || hz > HIDPOLL_HID_HZ_MAX) {
        return HIDPOLL_RC(HidpollErr_InvalidArgument);
    }
    g_hid_hz = hz;
    g_usb_hz = hz > HIDPOLL_USB_HZ_MAX ? HIDPOLL_USB_HZ_MAX : hz;
    config_save();
    apply_all(true, true);
    return R_FAILED(g_rep.hid_rc) ? g_rep.hid_rc : g_rep.usb_rc;
}

static Result cmd_reapply(void) {
    apply_all(true, true);
    return R_FAILED(g_rep.hid_rc) ? g_rep.hid_rc : g_rep.usb_rc;
}

// ---------- request dispatch ----------

// Returns true if the caller should close the session (Close message).
static bool handle_request(void) {
    void*             tls = armGetTls();
    HipcParsedRequest req = hipcParseRequest(tls);

    if (req.meta.type == CmifCommandType_Close) return true;

    if (req.meta.type != CmifCommandType_Request &&
        req.meta.type != CmifCommandType_RequestWithContext) {
        // Control / domain messages are not supported.
        write_response(HIDPOLL_RC(HidpollErr_InvalidCommand), NULL, 0);
        return false;
    }

    const CmifInHeader* in = (const CmifInHeader*)cmifGetAlignedDataStart(req.data.data_words, tls);
    if (in->magic != CMIF_IN_MAGIC) {
        write_response(HIDPOLL_RC(HidpollErr_InvalidCommand), NULL, 0);
        return false;
    }

    const u32* args = (const u32*)(in + 1);
    Result     rc   = 0;

    switch (in->command_id) {
        case HidpollCmd_GetStatus: {
            HidpollStatus st;
            fill_status(&st);
            write_response(0, &st, sizeof(st));
            return false;
        }
        case HidpollCmd_SetHidRate: rc = cmd_set_hid_rate(args); break;
        case HidpollCmd_SetUsbRate: rc = cmd_set_usb_rate(args); break;
        case HidpollCmd_SetRate:    rc = cmd_set_rate(args);     break;
        case HidpollCmd_Reapply:    rc = cmd_reapply();          break;
        default:                    rc = HIDPOLL_RC(HidpollErr_InvalidCommand); break;
    }

    write_response(rc, NULL, 0);
    return false;
}

// ---------- session table ----------

static void handles_remove(Handle* handles, s32* count, s32 idx) {
    svcCloseHandle(handles[idx]);
    for (s32 i = idx; i < *count - 1; i++) handles[i] = handles[i + 1];
    (*count)--;
}

// ---------- main loop ----------

void service_run(void) {
    Handle port;
    Result rc = smRegisterService(&port, smEncodeName(HIDPOLL_SERVICE_NAME), false, HP_MAX_SESSIONS);
    if (R_FAILED(rc)) {
        hp_log("smRegisterService failed: %#x", rc);
        return;
    }
    hp_log("service '%s' registered", HIDPOLL_SERVICE_NAME);

    Handle handles[1 + HP_MAX_SESSIONS];
    s32    count        = 1;
    handles[0]          = port;
    Handle reply_target = INVALID_HANDLE;

    for (;;) {
        s32 idx = -1;
        rc = svcReplyAndReceive(&idx, handles, count, reply_target, UINT64_MAX);
        reply_target = INVALID_HANDLE;

        if (R_FAILED(rc)) {
            if (rc == KERNELRESULT(ConnectionClosed) && idx > 0 && idx < count) {
                handles_remove(handles, &count, idx);
            }
            continue;
        }

        // Incoming connection on the port.
        if (idx == 0) {
            Handle sess;
            if (R_SUCCEEDED(svcAcceptSession(&sess, port))) {
                if (count < 1 + HP_MAX_SESSIONS) handles[count++] = sess;
                else                             svcCloseHandle(sess);
            }
            continue;
        }

        // Message on an established session.
        if (handle_request()) {
            handles_remove(handles, &count, idx);
            continue;
        }
        reply_target = handles[idx];
    }
}
