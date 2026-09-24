/*
 * hidpoll_ctl - SDL2 front-end for the hidpoll sysmodule.
 *
 * UI:
 *   Left sidebar : Settings | Test | Status
 *   Right panel  : cards for the selected tab
 *   Bottom bar   : contextual button hints
 *
 * Buttons:
 *   L / R                : previous / next tab
 *   D-Pad Up/Down        : select card in tab
 *   D-Pad Left/Right     : change value (Settings)
 *   A                    : apply selected row (Settings) / start Test
 *   Y                    : apply both (Settings)
 *   X                    : Reapply patcher
 *   B                    : stop test (in Test tab)
 *   +                    : exit
 */
#include <switch.h>
#include <SDL.h>

#include "theme.hpp"
#include "ui.hpp"
#include "state.hpp"
#include "tester.hpp"
#include "panels.hpp"

// ---------- service ops ----------

static void refresh_status() {
    hidpollGetStatus(&A.svc, &A.st);
}

// Convert a Result from a setter call into a user-facing toast. `ok_msg` is
// shown on success; failures always render as red with the rc code.
static void toast_result(Result rc, const std::string& ok_msg) {
    if (R_SUCCEEDED(rc)) ui_toast(ok_msg);
    else                 ui_toast(sfmt("failed: rc = %#x", rc), true);
}

static void op_set_hid() {
    Result rc = hidpollSetHidRate(&A.svc, HID_PRESETS[A.hid_i]);
    refresh_status();
    toast_result(rc, sfmt("hid rate set to %u Hz", HID_PRESETS[A.hid_i]));
}

static void op_set_usb() {
    Result rc = hidpollSetUsbRate(&A.svc, USB_PRESETS[A.usb_i]);
    refresh_status();
    toast_result(rc,
        USB_PRESETS[A.usb_i] == 0
            ? std::string("usb rate: honor bInterval (replug device)")
            : sfmt("usb rate set to %u Hz (replug device)", USB_PRESETS[A.usb_i]));
}

static void op_set_both() {
    Result rc = hidpollSetRate(&A.svc, HID_PRESETS[A.hid_i]);
    refresh_status();
    A.usb_i = nearest_idx(USB_PRESETS, USB_PRESET_COUNT, A.st.usb_hz);
    toast_result(rc, sfmt("hid + usb set to %u Hz", HID_PRESETS[A.hid_i]));
}

static void op_reapply() {
    Result rc = hidpollReapply(&A.svc);
    refresh_status();
    toast_result(rc, "patcher reapplied");
}

static void op_reset_defaults() {
    // Restore the stock (unpatched) rates.
    Result rc = hidpollSetHidRate(&A.svc, HIDPOLL_HID_HZ_DEFAULT);
    if (R_SUCCEEDED(rc))
        rc = hidpollSetUsbRate(&A.svc, HIDPOLL_USB_HZ_DEFAULT);
    refresh_status();
    A.hid_i = nearest_idx(HID_PRESETS, HID_PRESET_COUNT, A.st.hid_hz);
    A.usb_i = nearest_idx(USB_PRESETS, USB_PRESET_COUNT, A.st.usb_hz);
    toast_result(rc, "restored stock defaults");
}

// ---------- input ----------

static void switch_tab(int new_tab) {
    if (new_tab == A.tab) return;
    if (T.running) test_stop();
    A.tab = new_tab;
    A.row = 0;
}

static void handle_settings_input(u64 k) {
    constexpr int SETTINGS_ROWS = 3;   // 0=hid, 1=usb, 2=reset-to-defaults
    if (k & HidNpadButton_Up)   A.row = (A.row + SETTINGS_ROWS - 1) % SETTINGS_ROWS;
    if (k & HidNpadButton_Down) A.row = (A.row + 1) % SETTINGS_ROWS;

    // Left/Right only meaningful on the two preset rows.
    if (k & HidNpadButton_Left) {
        if      (A.row == 0) A.hid_i = (A.hid_i + HID_PRESET_COUNT - 1) % HID_PRESET_COUNT;
        else if (A.row == 1) A.usb_i = (A.usb_i + USB_PRESET_COUNT - 1) % USB_PRESET_COUNT;
    }
    if (k & HidNpadButton_Right) {
        if      (A.row == 0) A.hid_i = (A.hid_i + 1) % HID_PRESET_COUNT;
        else if (A.row == 1) A.usb_i = (A.usb_i + 1) % USB_PRESET_COUNT;
    }

    if (!A.svc_ok) return;

    if (k & HidNpadButton_A) {
        switch (A.row) {
            case 0: op_set_hid();        break;
            case 1: op_set_usb();        break;
            case 2: op_reset_defaults(); break;
        }
    }
    if (k & HidNpadButton_Y) op_set_both();
    if (k & HidNpadButton_X) op_reapply();
}

static void handle_test_input(u64 k) {
    if ((k & HidNpadButton_A) && !T.running) test_start();
    if (k & HidNpadButton_B)                 test_stop();
}

static void handle_status_input(u64 k) {
    if (A.svc_ok && (k & HidNpadButton_X)) op_reapply();
}

// Returns false if the app should exit.
static bool handle_input(PadState& pad) {
    padUpdate(&pad);
    u64 k = padGetButtonsDown(&pad);

    if (k & HidNpadButton_Plus) return false;

    if (k & HidNpadButton_L) switch_tab((A.tab + TAB_COUNT - 1) % TAB_COUNT);
    if (k & HidNpadButton_R) switch_tab((A.tab + 1) % TAB_COUNT);

    switch (A.tab) {
        case TAB_SETTINGS: handle_settings_input(k); break;
        case TAB_TEST:     handle_test_input(k);     break;
        case TAB_STATUS:   handle_status_input(k);   break;
    }
    return true;
}

// ---------- render ----------

static void render_frame() {
    set_color(theme::BG);
    SDL_RenderClear(g_ui.r);

    draw_sidebar();
    switch (A.tab) {
        case TAB_SETTINGS: panel_settings(); break;
        case TAB_TEST:     panel_test();     break;
        case TAB_STATUS:   panel_status();   break;
    }
    draw_hints();
    draw_toast();

    SDL_RenderPresent(g_ui.r);
}

// ---------- entry ----------

int main(int, char**) {
    if (!ui_init()) return 1;

    padConfigureInput(1, HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadGc);
    PadState pad;
    padInitializeDefault(&pad);

    A.svc_rc = hidpollInitialize(&A.svc);
    A.svc_ok = R_SUCCEEDED(A.svc_rc);
    if (A.svc_ok) refresh_status();

    A.hid_i = nearest_idx(HID_PRESETS, HID_PRESET_COUNT, A.st.hid_hz ? A.st.hid_hz : 1000);
    A.usb_i = nearest_idx(USB_PRESETS, USB_PRESET_COUNT, A.st.usb_hz);

    // Pressing HOME keeps the applet alive in the background (qlaunch is
    // overlaid, but appletMainLoop() keeps returning true). Auto-stop any
    // running test the moment we lose focus so the sampler thread isn't
    // hammering hid while the user is off doing something else.
    AppletFocusState prev_focus = AppletFocusState_InFocus;
    while (appletMainLoop()) {
        AppletFocusState focus = appletGetFocusState();
        if (focus != AppletFocusState_InFocus &&
            prev_focus == AppletFocusState_InFocus &&
            T.running) {
            test_stop();
        }
        prev_focus = focus;

        if (!handle_input(pad)) break;
        render_frame();
    }

    if (T.running)  test_stop();
    if (A.svc_ok)   hidpollExit(&A.svc);
    ui_shutdown();
    return 0;
}
