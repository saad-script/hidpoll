#include "tester.hpp"

Tester T;
bool   T_have_result = false;

static Thread T_thread;
alignas(0x1000) static u8 T_stack[0x4000];

struct Sample { u64 sn; s32 lx, ly, rx, ry; };

static size_t read_states(HidNpadIdType id, u32 style, Sample* out, size_t max) {
    size_t n = 0;

    if (style & HidNpadStyleTag_NpadGc) {
        HidNpadGcState st[17];
        n = hidGetNpadStatesGc(id, st, 17);
        for (size_t i = 0; i < n && i < max; i++) {
            out[i] = { st[i].sampling_number,
                       st[i].analog_stick_l.x, st[i].analog_stick_l.y,
                       st[i].analog_stick_r.x, st[i].analog_stick_r.y };
        }
        return n < max ? n : max;
    }

    HidNpadCommonState st[17];
    if      (style & HidNpadStyleTag_NpadFullKey)   n = hidGetNpadStatesFullKey(id, st, 17);
    else if (style & HidNpadStyleTag_NpadHandheld)  n = hidGetNpadStatesHandheld(id, st, 17);
    else if (style & HidNpadStyleTag_NpadJoyDual)   n = hidGetNpadStatesJoyDual(id, st, 17);
    else if (style & HidNpadStyleTag_NpadJoyLeft)   n = hidGetNpadStatesJoyLeft(id, st, 17);
    else if (style & HidNpadStyleTag_NpadJoyRight)  n = hidGetNpadStatesJoyRight(id, st, 17);
    else if (style & HidNpadStyleTag_NpadSystemExt) n = hidGetNpadStatesSystemExt(id, (HidNpadSystemExtState*)st, 17);
    else if (style & HidNpadStyleTag_NpadSystem)    n = hidGetNpadStatesSystem(id, (HidNpadSystemState*)st, 17);

    for (size_t i = 0; i < n && i < max; i++) {
        out[i] = { st[i].sampling_number,
                   st[i].analog_stick_l.x, st[i].analog_stick_l.y,
                   st[i].analog_stick_r.x, st[i].analog_stick_r.y };
    }
    return n < max ? n : max;
}

static void sampler_thread(void*) {
    Sample buf[17];
    Sample prev{};
    u64    last_sn = 0;
    bool   have_prev = false;

    const u64 freq = armGetSystemTickFreq();

    size_t n = read_states(T.id, T.style, buf, 17);
    if (n) {
        last_sn   = buf[0].sn;
        prev      = buf[0];
        have_prev = true;
    }
    T.t_start = T.t_last = T.win_t0 = armGetSystemTick();

    while (!T.stop) {
        n = read_states(T.id, T.style, buf, 17);

        for (size_t i = n; i-- > 0;) {
            if (buf[i].sn <= last_sn) continue;

            if (have_prev) {
                if (buf[i].sn > last_sn + 1) {
                    T.missed += buf[i].sn - last_sn - 1;
                }
                T.samples     += buf[i].sn - last_sn;
                T.win_samples += buf[i].sn - last_sn;
                if (buf[i].lx != prev.lx || buf[i].ly != prev.ly ||
                    buf[i].rx != prev.rx || buf[i].ry != prev.ry) {
                    T.changes++;
                    T.win_changes++;
                }
            }
            T.observed++;
            prev      = buf[i];
            last_sn   = buf[i].sn;
            have_prev = true;
        }

        u64 now  = armGetSystemTick();
        T.t_last = now;

        if (now - T.win_t0 >= freq) {
            double dt = double(now - T.win_t0) / double(freq);
            T.inst_sample_hz = T.win_samples / dt;
            T.inst_change_hz = T.win_changes / dt;
            T.win_samples = T.win_changes = 0;
            T.win_t0 = now;
        }
        svcSleepThread(250000ull);
    }
    T.running = false;
}

bool test_start() {
    T = Tester{};

    T.id    = HidNpadIdType_No1;
    T.style = hidGetNpadStyleSet(HidNpadIdType_No1);
    if (!T.style) {
        T.id    = HidNpadIdType_Handheld;
        T.style = hidGetNpadStyleSet(HidNpadIdType_Handheld);
    }
    if (!T.style) return false;

    T.running = true;
    T.stop    = false;

    if (R_FAILED(threadCreate(&T_thread, sampler_thread, nullptr,
                              T_stack, sizeof(T_stack), 0x20, -2))) {
        T.running = false;
        return false;
    }
    if (R_FAILED(threadStart(&T_thread))) {
        threadClose(&T_thread);
        T.running = false;
        return false;
    }

    T_have_result = true;
    return true;
}

void test_stop() {
    if (!T.running && !T.stop) return;
    T.stop = true;
    threadWaitForExit(&T_thread);
    threadClose(&T_thread);
    T.running = false;
}

const char* style_name(u32 s) {
    if (s & HidNpadStyleTag_NpadGc)         return "GameCube";
    if (s & HidNpadStyleTag_NpadFullKey)    return "Pro Controller";
    if (s & HidNpadStyleTag_NpadHandheld)   return "Handheld";
    if (s & HidNpadStyleTag_NpadJoyDual)    return "Joy-Con (Dual)";
    if (s & HidNpadStyleTag_NpadJoyLeft)    return "Joy-Con (Left)";
    if (s & HidNpadStyleTag_NpadJoyRight)   return "Joy-Con (Right)";
    if (s & HidNpadStyleTag_NpadSystemExt)  return "System (Ext)";
    if (s & HidNpadStyleTag_NpadSystem)     return "System";
    return "?";
}
