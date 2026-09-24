#include "pattern.h"
#include "arm64.h"
#include "log.h"
#include <string.h>

static bool region_is_scannable(const MemoryInfo* mi) {
    if (!(mi->perm & Perm_R))            return false;
    if (mi->type == MemType_Unmapped)    return false;
    if (mi->type == MemType_Reserved)    return false;
    if (mi->type == MemType_Io)          return false;
    if (mi->type == MemType_Heap)        return false;
    if (mi->type == MemType_ThreadLocal) return false;
    return true;
}

static bool pat_match(const Pattern* p, const u8* buf) {
    for (u32 i = 0; i < p->nwords; i++) {
        u32 w;
        memcpy(&w, buf + i * 4, 4);
        if ((w & p->words[i].mask) != p->words[i].value) return false;
    }
    return true;
}

Result pattern_scan_module(Handle dbg, u64 base, u64 size,
                           const Pattern* p, Matches* out) {
    out->count = 0;

    const u64 patlen = (u64)p->nwords * 4;
    const u64 end    = base + size;

    u64 cur = base;
    while (cur < end) {
        MemoryInfo mi;
        u32        pi;
        Result rc = svcQueryDebugProcessMemory(&mi, &pi, dbg, cur);
        if (R_FAILED(rc)) {
            hp_log("  query %#lx failed: %#x", cur, rc);
            return rc;
        }

        u64 rstart = mi.addr > cur ? mi.addr : cur;
        u64 rend   = mi.addr + mi.size;
        if (rend > end) rend = end;

        if (rend <= rstart) {
            u64 next = mi.addr + mi.size;
            if (next <= rstart) break;
            cur = next;
            continue;
        }

        if (!region_is_scannable(&mi)) {
            hp_log("  skip region %#lx+%#lx type %u perm %u attr %u",
                   mi.addr, mi.size, mi.type, mi.perm, mi.attr);
            cur = rend;
            continue;
        }

        u64 off = rstart;
        while (off < rend) {
            u64 want = rend - off;
            if (want > TARGET_SCAN_CHUNK) want = TARGET_SCAN_CHUNK;

            u64 rd = want + TARGET_SCAN_OVERLAP;
            if (off + rd > rend) rd = rend - off;

            rc = svcReadDebugProcessMemory(g_scan_chunk, dbg, off, rd);
            if (R_FAILED(rc)) {
                hp_log("  read %#lx+%#lx failed: %#x (region %#lx+%#lx type %u perm %u attr %u)",
                       off, rd, rc, mi.addr, mi.size, mi.type, mi.perm, mi.attr);
                return rc;
            }

            if (rd >= patlen) {
                u64 limit = rd - patlen;
                for (u64 i = 0; i <= limit && i < want; i += 4) {
                    if (pat_match(p, g_scan_chunk + i)) {
                        if (out->count < PATTERN_MAX_MATCHES) out->addr[out->count] = off + i;
                        out->count++;
                    }
                }
            }
            off += want;
        }
        cur = rend;
    }
    return 0;
}

Result pattern_find_unique(Target* t, const Pattern* p,
                           u64* out_addr, int* out_mod_idx,
                           const u32* accept_consts, int naccept,
                           int const_word) {
    Matches all      = {0};
    int     all_mod[PATTERN_MAX_MATCHES];

    for (s32 mi = 0; mi < t->nmods; mi++) {
        Matches m;
        Result  rc = pattern_scan_module(t->dbg, t->mods[mi].base_address, t->mods[mi].size, p, &m);
        if (R_FAILED(rc)) return rc;

        for (u32 i = 0; i < m.count && i < PATTERN_MAX_MATCHES && all.count < PATTERN_MAX_MATCHES; i++) {
            all_mod[all.count]  = mi;
            all.addr[all.count] = m.addr[i];
            all.count++;
        }
    }

    if (all.count == 0) {
        hp_log("  pattern '%s': not found", p->name);
        return HIDPOLL_RC(HidpollErr_PatternNotFound);
    }
    if (all.count == 1) {
        *out_addr    = all.addr[0];
        *out_mod_idx = all_mod[0];
        return 0;
    }

    // Ambiguous: try to disambiguate via the movz/movk pair at const_word.
    if (accept_consts && naccept > 0 && const_word >= 0) {
        int keep = -1, nkeep = 0;
        for (u32 i = 0; i < all.count; i++) {
            u32 z, k;
            if (R_FAILED(target_read_u32(t->dbg, all.addr[i] + const_word * 4,     &z))) continue;
            if (R_FAILED(target_read_u32(t->dbg, all.addr[i] + const_word * 4 + 4, &k))) continue;
            u32 c = arm64_imm_of_movz_movk_pair(z, k);

            for (int a = 0; a < naccept; a++) {
                if (c == accept_consts[a]) { keep = (int)i; nkeep++; break; }
            }
        }
        if (nkeep == 1) {
            *out_addr    = all.addr[keep];
            *out_mod_idx = all_mod[keep];
            return 0;
        }
    }

    hp_log("  pattern '%s': ambiguous (%u matches)", p->name, all.count);
    return HIDPOLL_RC(HidpollErr_PatternAmbiguous);
}
