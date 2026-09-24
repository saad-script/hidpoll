/*
 * hidpoll sysmodule: applies the hid/usb polling-rate patches at boot and
 * serves the "hidpoll" IPC service so overlays/apps can change the rate at runtime.
 */
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <switch.h>

#include "state.h"
#include "service.h"
#include "log.h"

// ---------- libnx integration ----------

#define INNER_HEAP_SIZE 0x20000

u32 __nx_applet_type     = AppletType_None;
u32 __nx_fs_num_sessions = 1;

void __libnx_initheap(void) {
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void* fake_heap_start;
    extern void* fake_heap_end;
    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

void __appInit(void) {
    Result rc = smInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) {
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        }
        setsysExit();
    }

    rc = fsInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
    fsdevMountSdmc();

    rc = pmdmntInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(rc);

    rc = ldrDmntInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(rc);

    // Keep sm open: we register a service.
}

void __appExit(void) {
    ldrDmntExit();
    pmdmntExit();
    fsdevUnmountAll();
    fsExit();
    smExit();
}

// ---------- entry ----------

static void ensure_config(void) {
    mkdir("sdmc:/config", 0777);
    mkdir(HP_CONFIG_DIR, 0777);
    remove(HP_CONFIG_DIR "/log.txt");

    hp_log("hidpoll starting");
    config_load();
    if (access(HP_CONFIG_PATH, F_OK) != 0) config_save();
    hp_log("config: hid_hz=%u usb_hz=%u", g_hid_hz, g_usb_hz);
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    ensure_config();

    // hid is launched after boot2
    // usb is launched by boot2 before launching sysmodules
    apply_all(true, true);

    service_run();
    return 0;
}
