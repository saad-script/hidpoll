#pragma once
#include <switch.h>
#include "hidpoll.h"

#define TARGET_MAX_MODULES 8

typedef struct {
    u64              pid;
    Handle           dbg;
    LoaderModuleInfo mods[TARGET_MAX_MODULES];
    s32              nmods;
} Target;

Result target_open(Target* t, u64 program_id);
void   target_close(Target* t);

Result target_read_u32 (Handle dbg, u64 addr, u32* out);
Result target_read_u64 (Handle dbg, u64 addr, u64* out);
Result target_write_u32(Handle dbg, u64 addr, u32 v);
Result target_write_u64(Handle dbg, u64 addr, u64 v);

/* Fill out[41] with the 40-hex-char uppercase build id (NUL-terminated). */
void target_build_id_str(const u8* build_id, char out[41]);

/* Shared scratch buffer used by pattern scanning; sized CHUNK+OVERLAP.
 * Allocated lazily by target_open(); NULL until first successful open. */
#define TARGET_SCAN_CHUNK   0x10000
#define TARGET_SCAN_OVERLAP 0x40
extern u8* g_scan_chunk;
