#include "panels.hpp"
#include "theme.hpp"
#include "ui.hpp"
#include "state.hpp"
#include "tester.hpp"

using namespace theme;

// ---------- layout helpers ----------

static SDL_Rect card_rect(int idx, int rows_on_screen = 3, int top = 140) {
    int panel_x = SIDEBAR_W + 24;
    int panel_w = W - SIDEBAR_W - 48;
    int gap     = 16;

    int h = (H - top - 90 - gap * (rows_on_screen - 1)) / rows_on_screen;
    if (h > 130) h = 130;

    return SDL_Rect{ panel_x, top + idx * (h + gap), panel_w, h };
}

static void draw_card(SDL_Rect r, bool sel) {
    fill_rect(r.x, r.y, r.w, r.h, sel ? CARD_SEL : CARD);
    if (sel) fill_rect(r.x, r.y, 4, r.h, ACCENT);
}

static void draw_chip(int x, int y, const char* text, Color bg, Color fg) {
    int pad = 12;
    int tw  = text_w(g_ui.f_small, text);
    int h   = 26;
    int w   = tw + pad * 2;
    fill_rect(x, y, w, h, bg);
    draw_text(g_ui.f_small, text, x + pad, y + 4, fg);
}

static void draw_titlebar(const char* title, const char* subtitle) {
    int x = SIDEBAR_W + 40;
    draw_text(g_ui.f_hero, title, x, 26, TEXT);
    if (subtitle) draw_text(g_ui.f_small, subtitle, x, 82, TEXT_DIM);
    draw_hline(SIDEBAR_W + 24, W - 24, 116, DIVIDER);
}

static void draw_preset_dots(int x, int y, int count, int selected) {
    for (int i = 0; i < count; i++) {
        Color c = (i == selected) ? ACCENT : DOT_OFF;
        fill_rect(x, y, 10, 10, c);
        x += 18;
    }
}

// ---------- sidebar & hints ----------

void draw_sidebar() {
    fill_rect(0,         0, SIDEBAR_W, H, SIDEBAR);
    fill_rect(SIDEBAR_W, 0, 1,         H, DIVIDER);

    draw_text(g_ui.f_head,  "hidpoll",                  24, 22, TEXT);
    draw_text(g_ui.f_small, "Runtime HID/USB Polling",  24, 60, TEXT_DIM);

    int y = 120;
    for (int i = 0; i < TAB_COUNT; i++) {
        SDL_Rect r{ 16, y, SIDEBAR_W - 32, 64 };
        if (i == A.tab) {
            fill_rect(r.x, r.y, r.w, r.h, CARD_SEL);
            fill_rect(r.x, r.y, 4,   r.h, ACCENT);
        }
        draw_text(g_ui.f_body, TAB_NAMES[i], r.x + 22, r.y + 20,
                  i == A.tab ? TEXT : TEXT_DIM);
        y += 72;
    }

    // status footer pill
    int fy = H - 96;
    fill_rect(16, fy, SIDEBAR_W - 32, 80, CARD);
    fill_rect(16, fy, 4,              80, A.svc_ok ? GOOD : BAD);
    draw_text(g_ui.f_small, "sysmodule", 32, fy + 10, TEXT_DIM);
    draw_text(g_ui.f_body,
              A.svc_ok ? "connected" : "not found",
              32, fy + 32,
              A.svc_ok ? GOOD : BAD);

    if (A.svc_ok) draw_text(g_ui.f_small, sfmt("api v%u", A.st.api_version).c_str(), 32, fy + 60, TEXT_DIM);
    else          draw_text(g_ui.f_small, sfmt("rc=%#x",  A.svc_rc).c_str(),        32, fy + 60, TEXT_DIM);
}

void draw_hints() {
    fill_rect(0, H - 40, W, 40, SIDEBAR);
    draw_hline(0, W, H - 40, DIVIDER);

    const char* hints = "";
    switch (A.tab) {
        case TAB_SETTINGS:
            hints = "L/R: tab   D-Pad: navigate   Left/Right: change   A: apply   Y: apply both   X: reapply   +: exit";
            break;
        case TAB_TEST:
            hints = T.running ? "B: stop test   +: exit"
                              : "A: start test   L/R: tab   +: exit";
            break;
        case TAB_STATUS:
            hints = "L/R: tab   X: reapply patcher   +: exit";
            break;
    }
    draw_text(g_ui.f_small, hints, 24, H - 30, TEXT_DIM);
}

// ---------- settings panel ----------

static void draw_rate_card(int row_idx, bool selected, const char* subtitle,
                           const std::string& value_text,
                           const std::string& current_text,
                           int preset_count, int preset_i) {
    auto r = card_rect(row_idx);
    draw_card(r, selected);

    draw_text(g_ui.f_small, subtitle,           r.x + 24, r.y + 16, TEXT_DIM);
    draw_text(g_ui.f_head,  value_text.c_str(), r.x + 24, r.y + 40, TEXT);

    if (A.svc_ok) {
        int cx = r.x + r.w - text_w(g_ui.f_small, current_text.c_str()) - 24;
        draw_text(g_ui.f_small, current_text.c_str(), cx, r.y + 16, TEXT_DIM);
    }

    draw_preset_dots(r.x + 24, r.y + r.h - 22, preset_count, preset_i);
}

void panel_settings() {
    draw_titlebar("Settings",
                  "hid rate applies live; usb rate applies to devices attached afterwards");

    // Row 0: HID rate
    draw_rate_card(
        0, A.row == 0,
        "HID sampler + USB-HID poll",
        sfmt("%u Hz", HID_PRESETS[A.hid_i]),
        A.svc_ok ? sfmt("current: %u Hz", A.st.hid_hz) : std::string(),
        HID_PRESET_COUNT, A.hid_i
    );

    // Row 1: USB rate
    std::string usb_val = USB_PRESETS[A.usb_i] == 0
        ? "honor bInterval"
        : sfmt("%u Hz", USB_PRESETS[A.usb_i]);
    std::string usb_cur;
    if (A.svc_ok) {
        usb_cur = A.st.usb_hz == 0
            ? std::string("current: honor bInterval")
            : sfmt("current: %u Hz", A.st.usb_hz);
    }
    draw_rate_card(
        1, A.row == 1,
        "USB xHCI FS-interrupt (forced)",
        usb_val, usb_cur,
        USB_PRESET_COUNT, A.usb_i
    );

    // Row 2: reset-to-defaults action card
    auto r = card_rect(2, 3);
    draw_card(r, A.row == 2);
    draw_text(g_ui.f_small, "Restore stock rates", r.x + 24, r.y + 16, TEXT_DIM);
    draw_text(g_ui.f_head,
              sfmt("Reset to defaults  (%u Hz / honor bInterval)",
                   HIDPOLL_HID_HZ_DEFAULT).c_str(),
              r.x + 24, r.y + 40, TEXT);
    const char* hint = A.svc_ok ? "press A to apply" : "sysmodule not connected";
    draw_text(g_ui.f_small, hint, r.x + 24, r.y + r.h - 26,
              A.svc_ok ? TEXT_DIM : BAD);
}

// ---------- test panel ----------

static void draw_big_metric_card(int x, int y, int w, int h,
                                 const char* label, double now, double avg,
                                 Color accent) {
    fill_rect(x, y, w, h, CARD);
    fill_rect(x, y, 4, h, accent);

    draw_text(g_ui.f_small, label, x + 24, y + 16, TEXT_DIM);

    auto now_s = sfmt("%.0f", now);
    draw_text(g_ui.f_hero, now_s.c_str(), x + 24, y + 40, TEXT);
    int wpx = text_w(g_ui.f_hero, now_s.c_str());
    draw_text(g_ui.f_body, "Hz", x + 24 + wpx + 10, y + 74, TEXT_DIM);

    draw_text(g_ui.f_small, sfmt("avg %.1f Hz", avg).c_str(),
              x + 24, y + h - 34, TEXT_DIM);
    draw_text(g_ui.f_small, "(last second)",
              x + w - text_w(g_ui.f_small, "(last second)") - 24, y + h - 34, TEXT_DIM);
}

void panel_test() {
    draw_titlebar("Poll Rate Test",
                  "Spin BOTH analog sticks continuously to measure the real update rate reaching games.");

    // Status pill
    int px = SIDEBAR_W + 24, py = 140;
    Color       pill_bg = T.running ? CHIP_RUN : (T_have_result ? CHIP_IDLE : CHIP_WARN);
    const char* pill_t  = T.running ? "TESTING" : (T_have_result ? "STOPPED" : "READY");
    Color       pill_fg = T.running ? GOOD     : (T_have_result ? TEXT      : WARN_FG);
    draw_chip(px, py, pill_t, pill_bg, pill_fg);

    if (T.style) {
        auto s = sfmt("%s   -   %s",
                      T.id == HidNpadIdType_Handheld ? "Handheld" : "Player 1",
                      style_name(T.style));
        draw_text(g_ui.f_body, s.c_str(), px + 130, py + 2, TEXT_DIM);
    }

    if (!T.running && !T_have_result) {
        int y = 240;
        draw_text(g_ui.f_body, "Press A to start.",              SIDEBAR_W + 24, y,      TEXT);
        draw_text(g_ui.f_body, "Then spin BOTH analog sticks continuously.", SIDEBAR_W + 24, y + 40, TEXT_DIM);
        draw_text(g_ui.f_body, "Press B to stop.",                           SIDEBAR_W + 24, y + 80, TEXT_DIM);
        return;
    }

    const u64 freq    = armGetSystemTickFreq();
    double    elapsed = double(T.t_last - T.t_start) / double(freq);
    if (elapsed < 0.001) elapsed = 0.001;
    double sample_avg = T.samples / elapsed;
    double change_avg = T.changes / elapsed;

    int panel_x = SIDEBAR_W + 24;
    int panel_w = W - SIDEBAR_W - 48;
    int top = 190, gap = 16;
    int cw = (panel_w - gap) / 2;
    int ch = 220;

    draw_big_metric_card(panel_x,            top, cw, ch,
                         "SAMPLER  (hid ticks / s)", T.inst_sample_hz, sample_avg, BLUE);
    draw_big_metric_card(panel_x + cw + gap, top, cw, ch,
                         "STICK RATE  (unique / s)", T.inst_change_hz, change_avg, ACCENT);

    // Details card
    int sy = top + ch + gap;
    int sh = 130;
    fill_rect(panel_x, sy, panel_w, sh, CARD);

    draw_text(g_ui.f_small, "Details", panel_x + 24, sy + 14, TEXT_DIM);

    auto details = sfmt("elapsed %.1fs   observed %llu   changed %llu   missed %llu",
                        elapsed,
                        (unsigned long long)T.observed,
                        (unsigned long long)T.changes,
                        (unsigned long long)T.missed);
    draw_text(g_ui.f_body, details.c_str(), panel_x + 24, sy + 40, TEXT);

    if (T.missed > 0) {
        draw_text(g_ui.f_small, "missed > 0 means the reader fell behind (LIFO overrun)",
                  panel_x + 24, sy + 78, BAD);
    } else {
        draw_text(g_ui.f_small, "no LIFO overruns",
                  panel_x + 24, sy + 78, TEXT_DIM);
    }
}

// ---------- status panel ----------

static void draw_flag_row(int i, const char* name, bool ok, const std::string& note) {
    SDL_Rect r = card_rect(i, 6, 140);
    r.h = 72;

    fill_rect(r.x, r.y, r.w, r.h, CARD);
    fill_rect(r.x, r.y, 6,   r.h, ok ? GOOD : BAD);

    const char* badge = ok ? "OK" : "-";
    int pad = 14;
    int bw  = text_w(g_ui.f_small, badge) + pad * 2;
    int bh  = 26;
    int bx  = r.x + r.w - bw - 20;
    int by  = r.y + (r.h - bh) / 2;
    fill_rect(bx, by, bw, bh, ok ? CHIP_OK : CHIP_BAD);
    draw_text(g_ui.f_small, badge, bx + pad, by + 4, ok ? GOOD : BAD);

    int text_left  = r.x + 22;
    int text_right = bx - 16;
    int avail      = text_right - text_left;
    draw_text(g_ui.f_body,  elide(g_ui.f_body,  name,        avail).c_str(), text_left, r.y + 14, TEXT);
    draw_text(g_ui.f_small, elide(g_ui.f_small, note.c_str(), avail).c_str(), text_left, r.y + 44, TEXT_DIM);
}

void panel_status() {
    draw_titlebar("Status",
                  "What the hidpoll sysmodule reports about its last patch attempt");

    if (!A.svc_ok) {
        draw_text(g_ui.f_body,  "sysmodule not connected", SIDEBAR_W + 24, 170, BAD);
        draw_text(g_ui.f_small, "check atmosphere/contents/420000000048504C/{exefs.nsp, flags/boot2.flag}",
                  SIDEBAR_W + 24, 210, TEXT_DIM);
        return;
    }

    u32 f = A.st.flags;
    int i = 0;
    draw_flag_row(i++, "hid process attached", f & HidpollFlag_HidFound,       "svcDebugActiveProcess(hid)");
    draw_flag_row(i++, "hid text patched",     f & HidpollFlag_HidTextPatched, "Npad + Ahid SetInterval constants rewritten");
    draw_flag_row(i++, "hid live tasks poked", f & HidpollFlag_HidLivePoked,
                  (f & HidpollFlag_HidLayoutRejected)
                      ? std::string("layout sanity check FAILED - HID_LAYOUTS offsets are stale for this firmware")
                      : sfmt("%u running task periods updated", A.st.hid_live_tasks));
    draw_flag_row(i++, "hid build id known",   f & HidpollFlag_HidBuildIdKnown,"live task offsets available for this firmware");
    draw_flag_row(i++, "usb process attached", f & HidpollFlag_UsbFound,       "svcDebugActiveProcess(usb)");
    draw_flag_row(i++, "usb text patched",     f & HidpollFlag_UsbTextPatched, "xHCI FS-interrupt interval computation rewritten");

    int y = 140 + 6 * (72 + 16) + 12;
    draw_text(g_ui.f_small,
              sfmt("last hid rc = %#x    last usb rc = %#x", A.st.last_hid_rc, A.st.last_usb_rc).c_str(),
              SIDEBAR_W + 24, y, TEXT_DIM);
}
