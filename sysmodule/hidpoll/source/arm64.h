#pragma once
#include <switch.h>

/* ARM64 A64 instruction encode/decode helpers used by the runtime patcher. */

static inline u32 arm64_enc_movz_w(u32 rd, u32 imm16) {
    return 0x52800000u | (imm16 << 5) | rd;
}

static inline u32 arm64_enc_movk_w_lsl16(u32 rd, u32 imm16) {
    return 0x72A00000u | (imm16 << 5) | rd;
}

static inline bool arm64_is_movz_w(u32 w, u32 rd) {
    return (w & 0xFFE0001Fu) == (0x52800000u | rd);
}

static inline bool arm64_is_movk_w_lsl16(u32 w, u32 rd) {
    return (w & 0xFFE0001Fu) == (0x72A00000u | rd);
}

/* Reassemble a 32-bit immediate from a movz(low16)+movk-lsl16(high16) pair. */
static inline u32 arm64_imm_of_movz_movk_pair(u32 movz, u32 movk) {
    return ((movz >> 5) & 0xFFFF) | (((movk >> 5) & 0xFFFF) << 16);
}

static inline bool arm64_is_bl(u32 w) {
    return (w & 0xFC000000u) == 0x94000000u;
}

static inline u64 arm64_bl_target(u64 pc, u32 w) {
    s64 imm = (s64)(w & 0x03FFFFFF);
    if (imm & 0x02000000) imm -= 0x04000000; // sign extend 26-bit
    return pc + imm * 4;
}
