/*
 * usb (nn.usb, program 0100000000000006)
 *
 *   XhciDriver::GetEndpointInterval, full-speed interrupt branch:
 *       cbz  w0, <assert>
 *       lsl  w0, w0, #3        <-- original (Interval = fls(bInterval*8) - 1)
 *       bl   fls
 *       sub  w0, w0, #1
 *
 *   We overwrite word 1 with `movz w0, #(8 * period_ms)` so Interval becomes fixed
 *   regardless of the device's bInterval descriptor. usb_hz==0 restores the original
 *   `lsl w0, w0, #3` instruction.
 */
#include "patcher.h"
#include "target.h"
#include "pattern.h"
#include "arm64.h"
#include "log.h"

#define PID_USB 0x0100000000000006ull

#define USB_ORIG_LSL_W0_W0_3   0x531D7000u   // original instruction we may need to restore

// The FS branch. Word 1 is either the original lsl or our replacement movz — the
// mask on word 1 is loose so we validate it by hand after matching.
static const PatWord P_USB_WORDS[] = {
    PW_M(0x34000000u, 0xFF00001Fu),   // cbz w0, ...
    PW_M(0x50000000u, 0xD0000000u),   // lsl w0,w0,#3  OR  movz w0,#imm
    PW_BL,                            // bl fls
    PW(0x51000400u),                  // sub w0, w0, #1
};
static const Pattern P_USB = { "usb.fs_int_interval", P_USB_WORDS, 4 };

// Word 1 must be either the original lsl or a movz-into-w0.
// (The low-speed sibling uses `lsl w0, w8, #3` = 0x531D7100 which is rejected here.)
static bool usb_word1_ok(u32 w) {
    return w == USB_ORIG_LSL_W0_W0_3 || arm64_is_movz_w(w, 0);
}

// FS interrupt intervals are 2^k ms; pick the largest 2^k <= (1000/hz) ms, clamped to [1..128].
static u32 pick_fs_period_ms(u32 usb_hz) {
    u32 ms = 1000 / usb_hz;
    if (ms < 1) ms = 1;

    u32 p = 1;
    while ((p << 1) <= ms && p < 128) p <<= 1;
    return p;
}

static Result find_usb_site(Target* t, u64* out_site) {
    u64 site = 0;
    int hits = 0;

    for (s32 mi = 0; mi < t->nmods; mi++) {
        Matches m;
        Result  rc = pattern_scan_module(t->dbg, t->mods[mi].base_address, t->mods[mi].size, &P_USB, &m);
        if (R_FAILED(rc)) return rc;

        for (u32 i = 0; i < m.count && i < PATTERN_MAX_MATCHES; i++) {
            u32 w1;
            if (R_FAILED(target_read_u32(t->dbg, m.addr[i] + 4, &w1))) continue;
            if (!usb_word1_ok(w1)) continue;

            site = m.addr[i];
            hits++;
        }
    }

    if (hits == 0) { hp_log("  usb pattern not found");           return HIDPOLL_RC(HidpollErr_PatternNotFound); }
    if (hits >  1) { hp_log("  usb pattern ambiguous (%d)", hits); return HIDPOLL_RC(HidpollErr_PatternAmbiguous); }

    *out_site = site;
    return 0;
}

Result patcher_apply_usb(u32 usb_hz, PatchReport* rep) {
    if (usb_hz > HIDPOLL_USB_HZ_MAX) usb_hz = HIDPOLL_USB_HZ_MAX;

    Target t;
    Result rc = target_open(&t, PID_USB);
    if (R_FAILED(rc)) { rep->usb_rc = rc; return rc; }

    rep->flags |= HidpollFlag_UsbFound;
    hp_log("usb: pid %lu, %d modules, target %u Hz", t.pid, t.nmods, usb_hz);

    u64 site = 0;
    rc = find_usb_site(&t, &site);
    if (R_FAILED(rc)) goto out;

    u32 newword;
    if (usb_hz == 0) {
        newword = USB_ORIG_LSL_W0_W0_3;
    } else {
        u32 p = pick_fs_period_ms(usb_hz);
        newword = arm64_enc_movz_w(0, 8 * p);
        hp_log("  usb interval -> %u ms (mov w0,#%u)", p, 8 * p);
    }

    u32 cur;
    rc = target_read_u32(t.dbg, site + 4, &cur); if (R_FAILED(rc)) goto out;

    if (cur != newword) {
        rc = target_write_u32(t.dbg, site + 4, newword); if (R_FAILED(rc)) goto out;
        hp_log("  usb @%#lx: %08x -> %08x", site + 4, cur, newword);
    } else {
        hp_log("  usb @%#lx already %08x", site + 4, cur);
    }

    rep->flags |= HidpollFlag_UsbTextPatched;
    rc = 0;

out:
    target_close(&t);
    rep->usb_rc = rc;
    hp_log("usb: done rc=%#x", rc);
    return rc;
}
