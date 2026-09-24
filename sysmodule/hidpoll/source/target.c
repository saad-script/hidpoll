#include "target.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

u8* g_scan_chunk = NULL;

Result target_read_u32 (Handle dbg, u64 addr, u32* out) { return svcReadDebugProcessMemory (out, dbg, addr, 4); }
Result target_read_u64 (Handle dbg, u64 addr, u64* out) { return svcReadDebugProcessMemory (out, dbg, addr, 8); }
Result target_write_u32(Handle dbg, u64 addr, u32 v)    { return svcWriteDebugProcessMemory(dbg, &v, addr, 4); }
Result target_write_u64(Handle dbg, u64 addr, u64 v)    { return svcWriteDebugProcessMemory(dbg, &v, addr, 8); }

void target_build_id_str(const u8* build_id, char out[41]) {
    for (int i = 0; i < 20; i++) sprintf(out + i * 2, "%02X", build_id[i]);
    out[40] = 0;
}

Result target_open(Target* t, u64 program_id) {
    if (!g_scan_chunk) {
        g_scan_chunk = (u8*)malloc(TARGET_SCAN_CHUNK + TARGET_SCAN_OVERLAP);
        if (!g_scan_chunk) return HIDPOLL_RC(HidpollErr_DebugFailed);
    }

    memset(t, 0, sizeof(*t));
    t->dbg = INVALID_HANDLE;

    Result rc = pmdmntGetProcessId(&t->pid, program_id);
    if (R_FAILED(rc)) {
        hp_log("pmdmntGetProcessId(%016lx) failed: %#x", program_id, rc);
        return HIDPOLL_RC(HidpollErr_ProcessNotFound);
    }

    rc = ldrDmntGetProcessModuleInfo(t->pid, t->mods, TARGET_MAX_MODULES, &t->nmods);
    if (R_FAILED(rc)) {
        hp_log("ldrDmntGetProcessModuleInfo(pid %lu) failed: %#x", t->pid, rc);
        return rc;
    }

    rc = svcDebugActiveProcess(&t->dbg, t->pid);
    if (R_FAILED(rc)) {
        hp_log("svcDebugActiveProcess(pid %lu) failed: %#x", t->pid, rc);
        return HIDPOLL_RC(HidpollErr_DebugFailed);
    }

    for (s32 i = 0; i < t->nmods; i++) {
        char bid[41];
        target_build_id_str(t->mods[i].build_id, bid);
        hp_log("  module %d: base %#lx size %#lx bid %s",
               i, t->mods[i].base_address, t->mods[i].size, bid);
    }
    return 0;
}

void target_close(Target* t) {
    if (t->dbg != INVALID_HANDLE) {
        svcCloseHandle(t->dbg);
        t->dbg = INVALID_HANDLE;
    }
}
