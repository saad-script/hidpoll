/*
 * hid (nn.hid, program 0100000000000013)
 *
 *   - ResourceManager::Activate  : SetInterval(&npad_task,  5'000'000 ns) -> requested_hz
 *   - Ahid device attach         : SetInterval(&mode1_task, 8'000'000 ns) -> requested_hz
 *                                  SetInterval(&mode2_task, 8'000'000 ns) -> requested_hz
 *
 *   Live poke: for the known hid build id, write task->period (task+0x30) on the running
 *   Npad task and up to 17 AhidSampler tasks so the change takes effect immediately.
 */
#include "patcher.h"
#include "target.h"
#include "pattern.h"
#include "arm64.h"
#include "log.h"
#include <string.h>

#define PID_HID          0x0100000000000013ull
#define TASK_SCHED_OFF   0x00   // PeriodicTask+0x00: owning scheduler (points into hid .bss)
#define TASK_PERIOD_OFF  0x30   // PeriodicTask+0x30: period in ns, 0 = stopped

// A period is plausible if it corresponds to a rate we could ever have set, or stock.
#define PERIOD_NS_MIN    ((u64)1000000000u / HIDPOLL_HID_HZ_MAX)
#define PERIOD_NS_MAX    ((u64)1000000000u / HIDPOLL_HID_HZ_MIN)
#define NPAD_STOCK_NS    ((u64)5000000u)
#define AHID_STOCK_NS    ((u64)8000000u)

// ---------- patterns ----------

// Npad activate:
//   add x0, x19, #0x498 ; movz w1, #lo ; movk w1, #hi, lsl #16 ; bl SetInterval
static const PatWord P_NPAD_WORDS[] = {
    PW(0x91126260u),
    PW_MOVZ_W(1),
    PW_MOVK_W(1),
    PW_BL,
};

// Ahid device attach, mode-1 and mode-2 tasks back-to-back (mode-2 alone is ambiguous):
//   ldr x0, [x12]     ; movz w1 ; movk w1 ; mov x19, x12 ; bl SetInterval    (mode-1: words 0..4)
//   ldr x0, [x19, #8] ; movz w1 ; movk w1 ; bl SetInterval                   (mode-2: words 5..8)
static const PatWord P_AHID_WORDS[] = {
    PW(0xF9400180u),
    PW_MOVZ_W(1),
    PW_MOVK_W(1),
    PW(0xAA0C03F3u),
    PW_BL,
    PW(0xF9400660u),
    PW_MOVZ_W(1),
    PW_MOVK_W(1),
    PW_BL,
};

static const Pattern P_NPAD = { "hid.npad.activate", P_NPAD_WORDS, 4 };
static const Pattern P_AHID = { "hid.ahid.attach",   P_AHID_WORDS, 9 };

// ---------- live task layouts (keyed by hid main-module build id) ----------

typedef struct {
    const char* build_id;      // 40 uppercase hex chars
    u64         rm_off;        // ResourceManager singleton in .bss (module base + rm_off)
    u64         npad_task_off; // RM + this = Npad periodic task
    u64         ahid_base_off; // RM + this = first AhidSampler thread object
    u64         ahid_stride;
    u32         ahid_count;
    u64         ahid_task_off; // thread object + this = periodic task
} HidLayout;

static const HidLayout HID_LAYOUTS[] = {
    { "C3030310E47B3841417518902AFFB2301F0033FC", 0x26c000, 0x498, 0xbb0, 0x368, 17, 0x320 }, // 22.5.0
    { "C9636704F7726BF77CEBBC53FFB14A80D8F789A1", 0x27e000, 0x498, 0xbc0, 0x368, 17, 0x320 }, // 23.0.X
};

static const HidLayout* find_hid_layout(const char* bid) {
    for (size_t i = 0; i < sizeof(HID_LAYOUTS) / sizeof(HID_LAYOUTS[0]); i++) {
        if (strcmp(HID_LAYOUTS[i].build_id, bid) == 0) return &HID_LAYOUTS[i];
    }
    return NULL;
}

// ---------- helpers ----------

// Track the last-applied hid rate so we can recognize our own patched constants
// as "acceptable" when disambiguating pattern matches on subsequent applies.
static u32 g_last_hid_hz = 0;

static Result patch_movz_movk_pair(Handle dbg, u64 site, int word,
                                   u32 new_value, const char* what) {
    u32 z, k;
    Result rc = target_read_u32(dbg, site + word * 4,     &z); if (R_FAILED(rc)) return rc;
    rc        = target_read_u32(dbg, site + word * 4 + 4, &k); if (R_FAILED(rc)) return rc;

    u32 old = arm64_imm_of_movz_movk_pair(z, k);
    u32 nz  = arm64_enc_movz_w(1, new_value & 0xFFFF);
    u32 nk  = arm64_enc_movk_w_lsl16(1, new_value >> 16);

    if (z == nz && k == nk) {
        hp_log("  %s @%#lx already %u ns", what, site, new_value);
        return 0;
    }

    rc = target_write_u32(dbg, site + word * 4,     nz); if (R_FAILED(rc)) return rc;
    rc = target_write_u32(dbg, site + word * 4 + 4, nk); if (R_FAILED(rc)) return rc;

    hp_log("  %s @%#lx: %u -> %u ns", what, site, old, new_value);
    return 0;
}

// Verify all three call sites actually dispatch to the same function (Task::SetInterval).
static Result verify_setinterval_targets(Handle dbg, u64 a_npad, u64 a_ahid1, u64 a_ahid2) {
    // BL is at word 3 for npad, word 4 for ahid1, word 3 for ahid2 sub-site.
    u32 b0, b1, b2;
    if (R_FAILED(target_read_u32(dbg, a_npad  + 12, &b0))) return 0; // best-effort: allow soft-pass
    if (R_FAILED(target_read_u32(dbg, a_ahid1 + 16, &b1))) return 0;
    if (R_FAILED(target_read_u32(dbg, a_ahid2 + 12, &b2))) return 0;
    if (!arm64_is_bl(b0) || !arm64_is_bl(b1) || !arm64_is_bl(b2)) return 0;

    u64 t0 = arm64_bl_target(a_npad  + 12, b0);
    u64 t1 = arm64_bl_target(a_ahid1 + 16, b1);
    u64 t2 = arm64_bl_target(a_ahid2 + 12, b2);

    if (t0 != t1 || t1 != t2) {
        hp_log("  SetInterval targets differ (%#lx %#lx %#lx) - refusing", t0, t1, t2);
        return HIDPOLL_RC(HidpollErr_PatternAmbiguous);
    }
    return 0;
}

static bool period_plausible(u64 ns) {
    return ns >= PERIOD_NS_MIN && ns <= PERIOD_NS_MAX;
}

// Sanity-check a HidLayout against the live process before trusting it.
//
// A stale rm_off / npad_task_off still lands inside hid's .bss on a new firmware, so
// the debug reads succeed and return whatever happens to live there (usually 0). Without
// this check that is indistinguishable from "task idle" and the poke silently no-ops.
//
// The Npad task is activated unconditionally in hid's main(), so on a correct layout:
//   - task+0x30 (period) is stock 5 ms, or whatever we last wrote -- never 0, never junk
//   - task+0x00 (scheduler*) points at the CommonSampler's scheduler inside hid's .bss
static bool validate_layout(Handle dbg, const HidLayout* L, const LoaderModuleInfo* mod) {
    const u64 mod_lo = mod->base_address;
    const u64 mod_hi = mod->base_address + mod->size;
    const u64 task   = mod_lo + L->rm_off + L->npad_task_off;

    u64 period = 0, sched = 0;
    Result rc = target_read_u64(dbg, task + TASK_PERIOD_OFF, &period);
    if (R_FAILED(rc)) {
        hp_log("  LAYOUT REJECTED: cannot read npad task period @%#lx (rc=%#x)", task, rc);
        return false;
    }
    rc = target_read_u64(dbg, task + TASK_SCHED_OFF, &sched);
    if (R_FAILED(rc)) {
        hp_log("  LAYOUT REJECTED: cannot read npad task scheduler @%#lx (rc=%#x)", task, rc);
        return false;
    }

    bool ok = true;
    if (period != NPAD_STOCK_NS && !period_plausible(period)) {
        hp_log("  LAYOUT REJECTED: npad task @%#lx period %lu ns is not stock (%lu) nor in [%lu, %lu]",
               task, period, NPAD_STOCK_NS, PERIOD_NS_MIN, PERIOD_NS_MAX);
        ok = false;
    }
    if (sched < mod_lo || sched >= mod_hi) {
        hp_log("  LAYOUT REJECTED: npad task @%#lx scheduler %#lx outside hid module [%#lx, %#lx)",
               task, sched, mod_lo, mod_hi);
        ok = false;
    }
    if (!ok) {
        hp_log("  -> HID_LAYOUTS entry for this build id is stale; live tasks NOT updated. "
               "Re-derive rm_off/npad_task_off/ahid_* for this firmware.");
    }
    return ok;
}

static u32 poke_live_tasks(Handle dbg, const HidLayout* L, u64 module_base, u32 period_ns) {
    u64 rm = module_base + L->rm_off;
    u32 poked = 0;

    // Npad task (already validated non-zero and plausible by validate_layout)
    {
        u64 task = rm + L->npad_task_off;
        u64 cur  = 0;
        if (R_SUCCEEDED(target_read_u64(dbg, task + TASK_PERIOD_OFF, &cur)) && cur != 0) {
            if (cur != period_ns && R_SUCCEEDED(target_write_u64(dbg, task + TASK_PERIOD_OFF, period_ns))) {
                poked++;
            }
            hp_log("  live npad task @%#lx: %lu -> %u", task, cur, period_ns);
        } else {
            hp_log("  live npad task @%#lx: inactive (%lu)", task, cur);
        }
    }

    // Per-slot AhidSampler tasks. A slot with no device attached legitimately reads 0.
    for (u32 i = 0; i < L->ahid_count; i++) {
        u64 task = rm + L->ahid_base_off + (u64)i * L->ahid_stride + L->ahid_task_off;
        u64 cur  = 0;
        if (R_FAILED(target_read_u64(dbg, task + TASK_PERIOD_OFF, &cur))) continue;
        if (cur == 0)          continue; // slot has no device attached
        if (cur == period_ns)  continue; // already at target
        if (cur != AHID_STOCK_NS && !period_plausible(cur)) {
            // Not a period we (or stock) could have produced: ahid_base_off/stride/task_off
            // are probably wrong for this build. Never write into memory we don't understand.
            hp_log("  live ahid[%u] @%#lx: implausible period %lu ns - skipping (ahid_* offsets stale?)",
                   i, task, cur);
            continue;
        }
        if (R_SUCCEEDED(target_write_u64(dbg, task + TASK_PERIOD_OFF, period_ns))) {
            poked++;
            hp_log("  live ahid[%u] @%#lx: %lu -> %u", i, task, cur, period_ns);
        }
    }
    return poked;
}

// ---------- entry point ----------

Result patcher_apply_hid(u32 hid_hz, PatchReport* rep) {
    if (hid_hz < HIDPOLL_HID_HZ_MIN || hid_hz > HIDPOLL_HID_HZ_MAX) {
        return HIDPOLL_RC(HidpollErr_InvalidArgument);
    }
    const u32 period_ns = (u32)(1000000000ull / hid_hz);

    Target t;
    Result rc = target_open(&t, PID_HID);
    if (R_FAILED(rc)) { rep->hid_rc = rc; return rc; }

    rep->flags |= HidpollFlag_HidFound;
    hp_log("hid: pid %lu, %d modules, target %u Hz (%u ns)", t.pid, t.nmods, hid_hz, period_ns);

    // Accepted constants when disambiguating: the original stock value, or whatever
    // we wrote last time (in case this is a re-apply).
    const u32 accept_npad[] = { 5000000, g_last_hid_hz ? 1000000000u / g_last_hid_hz : 0 };
    const u32 accept_ahid[] = { 8000000, g_last_hid_hz ? 1000000000u / g_last_hid_hz : 0 };

    u64 a_npad = 0, a_ahid = 0;
    int mod_npad = 0, mod_ahid = 0;

    rc = pattern_find_unique(&t, &P_NPAD, &a_npad, &mod_npad, accept_npad, 2, 1);
    if (R_FAILED(rc)) goto out;
    rc = pattern_find_unique(&t, &P_AHID, &a_ahid, &mod_ahid, accept_ahid, 2, 1);
    if (R_FAILED(rc)) goto out;

    const u64 a_ahid1 = a_ahid;
    const u64 a_ahid2 = a_ahid + 20; // mode-2 sub-site starts at word 5

    rc = verify_setinterval_targets(t.dbg, a_npad, a_ahid1, a_ahid2);
    if (R_FAILED(rc)) goto out;

    rc = patch_movz_movk_pair(t.dbg, a_npad,  1, period_ns, "npad.activate"); if (R_FAILED(rc)) goto out;
    rc = patch_movz_movk_pair(t.dbg, a_ahid1, 1, period_ns, "ahid.mode1");    if (R_FAILED(rc)) goto out;
    rc = patch_movz_movk_pair(t.dbg, a_ahid2, 1, period_ns, "ahid.mode2");    if (R_FAILED(rc)) goto out;
    rep->flags |= HidpollFlag_HidTextPatched;

    // Live poke (build-id keyed).
    char bid[41];
    target_build_id_str(t.mods[mod_npad].build_id, bid);
    const HidLayout* L = find_hid_layout(bid);
    if (!L) {
        hp_log("  hid build id %s unknown: text patched, live tasks NOT updated "
               "(takes effect on next activate/attach)", bid);
    } else {
        rep->flags |= HidpollFlag_HidBuildIdKnown;
        const LoaderModuleInfo* mod = &t.mods[mod_npad];
        if (!validate_layout(t.dbg, L, mod)) {
            rep->flags |= HidpollFlag_HidLayoutRejected;
        } else {
            rep->hid_live_tasks = poke_live_tasks(t.dbg, L, mod->base_address, period_ns);
            rep->flags |= HidpollFlag_HidLivePoked;
        }
    }

    g_last_hid_hz = hid_hz;
    rc = 0;

out:
    target_close(&t);
    rep->hid_rc = rc;
    hp_log("hid: done rc=%#x", rc);
    return rc;
}
