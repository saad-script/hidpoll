#pragma once
#include <switch.h>
#include "target.h"

/* One instruction word to match: (word & mask) == value. */
typedef struct { u32 value, mask; } PatWord;

#define PW(v)          { (v), 0xFFFFFFFFu }
#define PW_M(v, m)     { (v), (m) }
#define PW_MOVZ_W(rd)  { 0x52800000u | (rd), 0xFFE0001Fu }
#define PW_MOVK_W(rd)  { 0x72A00000u | (rd), 0xFFE0001Fu }
#define PW_BL          { 0x94000000u, 0xFC000000u }

typedef struct {
    const char*    name;
    const PatWord* words;
    u32            nwords;
} Pattern;

#define PATTERN_MAX_MATCHES 8
typedef struct {
    u64 addr[PATTERN_MAX_MATCHES];
    u32 count;
} Matches;

/* Scan [base, base+size) of a debugged process for a pattern. Skips non-readable /
 * heap / TLS / MMIO / reserved regions. */
Result pattern_scan_module(Handle dbg, u64 base, u64 size,
                           const Pattern* p, Matches* out);

/* Find a pattern that occurs exactly once across all modules of the target. If
 * multiple occurrences exist, disambiguate by checking the movz/movk pair at
 * word `const_word` against any of the accepted constants. */
Result pattern_find_unique(Target* t, const Pattern* p,
                           u64* out_addr, int* out_mod_idx,
                           const u32* accept_consts, int naccept,
                           int const_word);
