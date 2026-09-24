#pragma once
#include <switch.h>
#include "hidpoll.h"
#include "patcher.h"

#define HP_CONFIG_DIR  "sdmc:/config/hidpoll"
#define HP_CONFIG_PATH HP_CONFIG_DIR "/config.ini"

extern u32         g_hid_hz;
extern u32         g_usb_hz;
extern PatchReport g_rep;

void config_load(void);
void config_save(void);

/* Clear the relevant flag bits then run the requested patcher(s). */
void apply_all(bool do_hid, bool do_usb);

/* Snapshot current state into the IPC-visible HidpollStatus. */
void fill_status(HidpollStatus* out);
