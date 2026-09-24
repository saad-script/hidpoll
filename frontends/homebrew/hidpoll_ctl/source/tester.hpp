#pragma once
#include <switch.h>

struct Tester {
    volatile bool running = false;
    volatile bool stop    = false;

    HidNpadIdType id{};
    u32           style = 0;

    volatile u64 samples  = 0;
    volatile u64 observed = 0;
    volatile u64 changes  = 0;
    volatile u64 missed   = 0;

    volatile u64 t_start     = 0;
    volatile u64 t_last      = 0;
    volatile u64 win_t0      = 0;
    volatile u64 win_samples = 0;
    volatile u64 win_changes = 0;

    volatile double inst_sample_hz = 0.0;
    volatile double inst_change_hz = 0.0;
};

extern Tester T;
extern bool   T_have_result;

bool test_start();
void test_stop();

const char* style_name(u32 s);
