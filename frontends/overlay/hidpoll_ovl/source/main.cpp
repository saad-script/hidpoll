/*
 * hidpoll_ovl - Ultrahand (libultrahand) overlay front-end for the hidpoll
 * sysmodule. Mirrors hidpoll_ctl's feature set:
 *
 *   Main page  - HID + USB rate trackbars, apply / reapply, links to sub-pages.
 *   Test page  - live poll-rate test using the Npad LIFO sampler thread.
 *   Status     - flags reported by the sysmodule (patched / not, live-poked, etc).
 *
 * Install:  sd:/switch/.overlays/hidpoll_ovl.ovl
 */
#define TESLA_INIT_IMPL
#include <exception_wrap.hpp>
#include <tesla.hpp>

#include <cstdio>
#include <string>

extern "C" {
#include "hidpoll.h"
}

#include "tester.hpp"

// ---------- shared state --------------------------------------------------

static Service       g_svc{};
static bool          g_svc_ok = false;
static Result        g_svc_rc = 0;
static HidpollStatus g_st{};



static const u32 HID_PRESETS[] = { 125, 200, 250, 500, 1000 };
static const u32 USB_PRESETS[] = { 0,   125, 250, 500, 1000 };
static constexpr int HID_PRESET_COUNT = sizeof(HID_PRESETS) / sizeof(HID_PRESETS[0]);
static constexpr int USB_PRESET_COUNT = sizeof(USB_PRESETS) / sizeof(USB_PRESETS[0]);

static int nearest_idx(const u32* arr, int n, u32 v) {
    int best = 0;
    u32 best_d = (u32)-1;
    for (int i = 0; i < n; i++) {
        u32 d = arr[i] > v ? arr[i] - v : v - arr[i];
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

static std::string sfmt(const char* fmt, ...) {
    char buf[192];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

static void refresh_status() {
    if (g_svc_ok) hidpollGetStatus(&g_svc, &g_st);
}

static void notify(const std::string& s) {
    if (tsl::notification) tsl::notification->showNow(ult::NOTIFY_HEADER + s, 22);
}

static void op_set_hid(int i) {
    Result rc = hidpollSetHidRate(&g_svc, HID_PRESETS[i]);
    refresh_status();
    notify(R_SUCCEEDED(rc)
           ? sfmt("hid = %u Hz", HID_PRESETS[i])
           : sfmt("hid failed: %#x", rc));
}

static void op_set_usb(int i) {
    Result rc = hidpollSetUsbRate(&g_svc, USB_PRESETS[i]);
    refresh_status();
    if (!R_SUCCEEDED(rc))
        notify(sfmt("usb failed: %#x", rc));
    else if (USB_PRESETS[i] == 0)
        notify("usb = honor bInterval (replug device)");
    else
        notify(sfmt("usb = %u Hz (replug device)", USB_PRESETS[i]));
}

static void op_set_both(int hid_i) {
    Result rc = hidpollSetRate(&g_svc, HID_PRESETS[hid_i]);
    refresh_status();
    notify(R_SUCCEEDED(rc)
           ? sfmt("hid+usb = %u Hz", HID_PRESETS[hid_i])
           : sfmt("SetRate failed: %#x", rc));
}

static void op_reapply() {
    Result rc = hidpollReapply(&g_svc);
    refresh_status();
    notify(R_SUCCEEDED(rc) ? "reapplied" : sfmt("reapply failed: %#x", rc));
}

// Restore the stock (unpatched) rates: HID sampler back to 200 Hz and usb
// endpoints back to honoring each device's bInterval descriptor.
static void op_reset_defaults() {
    Result rc = hidpollSetHidRate(&g_svc, HIDPOLL_HID_HZ_DEFAULT);
    if (R_SUCCEEDED(rc))
        rc = hidpollSetUsbRate(&g_svc, HIDPOLL_USB_HZ_DEFAULT);
    refresh_status();
    notify(R_SUCCEEDED(rc)
           ? "restored stock defaults (replug usb devices)"
           : sfmt("reset failed: %#x", rc));
}

// ---------- helpers -------------------------------------------------------

static std::vector<std::string> hid_labels() {
    std::vector<std::string> v;
    for (int i = 0; i < HID_PRESET_COUNT; i++) v.push_back(sfmt("%u", HID_PRESETS[i]));
    return v;
}
static std::vector<std::string> usb_labels() {
    std::vector<std::string> v;
    for (int i = 0; i < USB_PRESET_COUNT; i++)
        v.push_back(USB_PRESETS[i] == 0 ? std::string("desc") : sfmt("%u", USB_PRESETS[i]));
    return v;
}

// libultrahand's NamedStepTrackBar takes std::initializer_list<std::string> in
// its ctor -- allocate variants inline to sidestep that constraint.
static tsl::elm::NamedStepTrackBar* make_named_trackbar(
        const char* icon, const std::vector<std::string>& names, const std::string& label) {
    switch (names.size()) {
        case 1: return new tsl::elm::NamedStepTrackBar(icon, {names[0]}, true, label);
        case 2: return new tsl::elm::NamedStepTrackBar(icon, {names[0], names[1]}, true, label);
        case 3: return new tsl::elm::NamedStepTrackBar(icon, {names[0], names[1], names[2]}, true, label);
        case 4: return new tsl::elm::NamedStepTrackBar(icon, {names[0], names[1], names[2], names[3]}, true, label);
        case 5: return new tsl::elm::NamedStepTrackBar(icon, {names[0], names[1], names[2], names[3], names[4]}, true, label);
        default: return new tsl::elm::NamedStepTrackBar(icon, {names[0], names[1], names[2], names[3], names[4], names[5]}, true, label);
    }
}

// ---------- Status page ---------------------------------------------------

class StatusGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        refresh_status();
        auto* frame = new tsl::elm::OverlayFrame("hidpoll", "Status");
        auto* list  = new tsl::elm::List();

        if (!g_svc_ok) {
            list->addItem(new tsl::elm::CategoryHeader("Sysmodule not connected"));
            auto* it = new tsl::elm::ListItem(sfmt("rc = %#x", g_svc_rc));
            list->addItem(it);
            list->addItem(new tsl::elm::CategoryHeader("Check that boot2.flag exists"));
            frame->setContent(list);
            return frame;
        }

        u32 f = g_st.flags;
        auto add = [&](const char* name, bool ok, const std::string& note){
            auto* it = new tsl::elm::ListItem(name);
            it->setValue(ok ? "OK" : "-", !ok);
            list->addItem(it);
            if (!note.empty()) {
                list->addItem(new tsl::elm::CustomDrawer(
                    [note](tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, s32 h){
                        (void)w;(void)h;
                        r->drawString(note.c_str(), false, x + 20, y + 22, 14,
                                      r->a(tsl::style::color::ColorDescription));
                    }), 40);
            }
        };

        list->addItem(new tsl::elm::CategoryHeader("hid"));
        add("process attached",  f & HidpollFlag_HidFound,        "svcDebugActiveProcess(hid)");
        add("text patched",      f & HidpollFlag_HidTextPatched,  "Npad + Ahid intervals rewritten");
        add("live tasks poked",  f & HidpollFlag_HidLivePoked,
            (f & HidpollFlag_HidLayoutRejected)
                ? std::string("layout check FAILED - offsets stale")
                : sfmt("%u task periods updated", g_st.hid_live_tasks));
        add("build id known",    f & HidpollFlag_HidBuildIdKnown, "live task offsets available");

        list->addItem(new tsl::elm::CategoryHeader("usb"));
        add("process attached",  f & HidpollFlag_UsbFound,        "svcDebugActiveProcess(usb)");
        add("text patched",      f & HidpollFlag_UsbTextPatched,  "xHCI FS-int computation rewritten");

        list->addItem(new tsl::elm::CategoryHeader("last results"));
        auto* rc = new tsl::elm::ListItem("hid rc");
        rc->setValue(sfmt("%#x", g_st.last_hid_rc));
        list->addItem(rc);
        auto* rc2 = new tsl::elm::ListItem("usb rc");
        rc2->setValue(sfmt("%#x", g_st.last_usb_rc));
        list->addItem(rc2);

        auto* reap = new tsl::elm::ListItem("Reapply patcher");
        reap->setClickListener([](u64 k){
            if ((k & KEY_A) && g_svc_ok) { op_reapply(); return true; }
            return false;
        });
        list->addItem(reap);

        frame->setContent(list);
        return frame;
    }
};

// ---------- Test page -----------------------------------------------------

class TestGui : public tsl::Gui {
    tsl::elm::ListItem* m_startStop = nullptr;
    bool                m_last_running = false;

    static const char* label_for_state() {
        return T.running ? "running" : (T_have_result ? "stopped" : "-");
    }

public:
    ~TestGui() override { if (T.running) test_stop(); }

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("hidpoll", "Poll Rate Test");
        auto* list  = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("Spin BOTH sticks continuously"));

        // Live metrics drawer -- reads T fields every frame.
        list->addItem(new tsl::elm::CustomDrawer(
            [](tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, s32 h){
                (void)w; (void)h;
                const u64 freq = armGetSystemTickFreq();
                double elapsed = T.t_last > T.t_start
                    ? double(T.t_last - T.t_start) / double(freq) : 0.0;
                if (elapsed < 0.001) elapsed = 0.001;
                double s_avg = T.samples / elapsed;
                double c_avg = T.changes / elapsed;

                auto col_hi = r->a(tsl::style::color::ColorText);
                auto col_lo = r->a(tsl::style::color::ColorDescription);

                const char* stateStr = T.running ? "TESTING"
                                     : (T_have_result ? "STOPPED" : "READY");
                r->drawString(stateStr, false, x + 20, y + 24, 18, col_hi);

                if (T.style) {
                    auto s = sfmt("%s / %s",
                                  T.id == HidNpadIdType_Handheld ? "Handheld" : "Player 1",
                                  style_name(T.style));
                    r->drawString(s.c_str(), false, x + 130, y + 24, 14, col_lo);
                }

                if (!T.running && !T_have_result) {
                    r->drawString("Press A to start.\nThen spin both analog sticks.\nPress B to stop.",
                                  false, x + 20, y + 60, 15, col_lo);
                    return;
                }

                auto sampler_now = sfmt("SAMPLER   %.0f Hz", (double)T.inst_sample_hz);
                auto sampler_avg = sfmt("avg %.1f Hz  (hid ticks/s)", s_avg);
                r->drawString(sampler_now.c_str(), false, x + 20, y + 60,  20, col_hi);
                r->drawString(sampler_avg.c_str(), false, x + 20, y + 86,  14, col_lo);

                auto change_now = sfmt("STICKS    %.0f Hz", (double)T.inst_change_hz);
                auto change_avg = sfmt("avg %.1f Hz  (unique samples/s)", c_avg);
                r->drawString(change_now.c_str(), false, x + 20, y + 120, 20, col_hi);
                r->drawString(change_avg.c_str(), false, x + 20, y + 146, 14, col_lo);

                auto details = sfmt("elapsed %.1fs   observed %llu",
                                    elapsed, (unsigned long long)T.observed);
                r->drawString(details.c_str(), false, x + 20, y + 180, 13, col_lo);

                auto miss = sfmt("changed %llu   missed %llu",
                                 (unsigned long long)T.changes,
                                 (unsigned long long)T.missed);
                r->drawString(miss.c_str(), false, x + 20, y + 200, 13,
                              T.missed ? r->a(tsl::style::color::ColorHighlight) : col_lo);
            }), 240);

        m_startStop = new tsl::elm::ListItem("Start / Stop");
        m_startStop->setValue(label_for_state());
        m_startStop->setClickListener([this](u64 k){
            if (!(k & KEY_A)) return false;
            if (T.running)         { test_stop(); }
            else if (test_start()) { /* running */ }
            else                   { m_startStop->setValue("no controller", true); return true; }
            m_startStop->setValue(label_for_state());
            m_last_running = T.running;
            return true;
        });
        list->addItem(m_startStop);

        m_last_running = T.running;
        frame->setContent(list);
        return frame;
    }

    // Keep the Start/Stop label in sync with T.running -- e.g. if the sampler
    // was stopped by Overlay::onHide() while the overlay was dismissed, the
    // label needs to flip from "running" to "stopped" the next time the page
    // becomes visible.
    void update() override {
        if (m_startStop && T.running != m_last_running) {
            m_startStop->setValue(label_for_state());
            m_last_running = T.running;
        }
    }

    // B (goBack) tears the page down which triggers the destructor's test_stop.
};

// ---------- Main (Settings) page ------------------------------------------

class MainGui : public tsl::Gui {
    int m_hid_i = 0;
    int m_usb_i = 0;

public:
    MainGui() {
        refresh_status();
        m_hid_i = nearest_idx(HID_PRESETS, HID_PRESET_COUNT, g_st.hid_hz ? g_st.hid_hz : 1000);
        m_usb_i = nearest_idx(USB_PRESETS, USB_PRESET_COUNT, g_st.usb_hz);
    }

    // Live-updated ListItems (setValue()'d after every op).
    tsl::elm::ListItem*           m_hidCur = nullptr;
    tsl::elm::ListItem*           m_usbCur = nullptr;
    tsl::elm::NamedStepTrackBar*  m_hidBar = nullptr;
    tsl::elm::NamedStepTrackBar*  m_usbBar = nullptr;

    void refresh_labels() {
        if (m_hidCur) {
            m_hidCur->setValue(g_svc_ok ? sfmt("%u Hz", g_st.hid_hz) : "-");
        }
        if (m_usbCur) {
            m_usbCur->setValue(g_svc_ok
                ? (g_st.usb_hz == 0 ? std::string("honor bInterval") : sfmt("%u Hz", g_st.usb_hz))
                : std::string("-"));
        }
    }

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("hidpoll", "v" APP_VERSION);
        auto* list = new tsl::elm::List();

        // Sysmodule status row
        auto* svcItem = new tsl::elm::ListItem("sysmodule");
        if (g_svc_ok) svcItem->setValue(sfmt("connected  api v%u", g_st.api_version));
        else          svcItem->setValue(sfmt("not found  rc=%#x", g_svc_rc), true);
        list->addItem(svcItem);

        // ---- HID rate ----
        list->addItem(new tsl::elm::CategoryHeader(
            "HID rate  " + ult::DIVIDER_SYMBOL + "  \uE0E0 apply  " + ult::DIVIDER_SYMBOL + "  \uE0E3 apply both"));

        // No leading glyph -- shared-font icons for "polling rate" don't exist,
        // and the label ("Hz") + step name ("125", "500", ...) already convey it.
        m_hidBar = make_named_trackbar("", hid_labels(), "Hz");
        m_hidBar->setProgress(m_hid_i);
        m_hidBar->setValueChangedListener([this](u8 v){ m_hid_i = v; });
        m_hidBar->setClickListener([this](u64 k){
            if (!g_svc_ok) return false;
            if (k & KEY_A) { m_hidBar->triggerClickAnimation(); op_set_hid(m_hid_i);  refresh_labels(); return true; }
            if (k & KEY_Y) { m_hidBar->triggerClickAnimation(); op_set_both(m_hid_i); refresh_labels(); return true; }
            return false;
        });
        list->addItem(m_hidBar);

        m_hidCur = new tsl::elm::ListItem("current");
        list->addItem(m_hidCur);

        // ---- USB rate ----
        list->addItem(new tsl::elm::CategoryHeader(
            "USB rate  " + ult::DIVIDER_SYMBOL + "  \uE0E0 apply  (replug device)"));

        m_usbBar = make_named_trackbar("", usb_labels(), "Hz");
        m_usbBar->setProgress(m_usb_i);
        m_usbBar->setValueChangedListener([this](u8 v){ m_usb_i = v; });
        m_usbBar->setClickListener([this](u64 k){
            if (!g_svc_ok) return false;
            if (k & KEY_A) { m_usbBar->triggerClickAnimation(); op_set_usb(m_usb_i); refresh_labels(); return true; }
            return false;
        });
        list->addItem(m_usbBar);

        m_usbCur = new tsl::elm::ListItem("current");
        list->addItem(m_usbCur);

        // ---- actions ----
        list->addItem(new tsl::elm::CategoryHeader("actions"));

        auto* reap = new tsl::elm::ListItem("Reapply patcher");
        reap->setClickListener([this](u64 k){
            if ((k & KEY_A) && g_svc_ok) { op_reapply(); refresh_labels(); return true; }
            return false;
        });
        list->addItem(reap);

        auto* reset = new tsl::elm::ListItem("Reset to defaults");
        reset->setValue(sfmt("%u / %s",
                             HIDPOLL_HID_HZ_DEFAULT,
                             HIDPOLL_USB_HZ_DEFAULT == 0 ? "desc" : "Hz"));
        reset->setClickListener([this](u64 k){
            if ((k & KEY_A) && g_svc_ok) {
                op_reset_defaults();
                m_hid_i = nearest_idx(HID_PRESETS, HID_PRESET_COUNT, g_st.hid_hz);
                m_usb_i = nearest_idx(USB_PRESETS, USB_PRESET_COUNT, g_st.usb_hz);
                if (m_hidBar) m_hidBar->setProgress(m_hid_i);
                if (m_usbBar) m_usbBar->setProgress(m_usb_i);
                refresh_labels();
                return true;
            }
            return false;
        });
        list->addItem(reset);

        auto* test = new tsl::elm::ListItem("Poll Rate Test");
        test->setValue("\u25B6");
        test->setClickListener([](u64 k){
            if (k & KEY_A) { tsl::changeTo<TestGui>(); return true; }
            return false;
        });
        list->addItem(test);

        auto* status = new tsl::elm::ListItem("Status");
        status->setValue("\u25B6");
        status->setClickListener([](u64 k){
            if (k & KEY_A) { tsl::changeTo<StatusGui>(); return true; }
            return false;
        });
        list->addItem(status);

        refresh_labels();  // populate initial values
        frame->setContent(list);
        return frame;
    }

    // Re-sync current/last-op labels every time we return to the Main page
    // (e.g. back from Status after pressing Reapply there).
    void update() override { refresh_labels(); }
};

// ---------- Overlay boilerplate ------------------------------------------

class HidpollOverlay : public tsl::Overlay {
public:
    void initServices() override {
        g_svc_rc = hidpollInitialize(&g_svc);
        g_svc_ok = R_SUCCEEDED(g_svc_rc);
        if (g_svc_ok) refresh_status();
    }
    void exitServices() override {
        if (T.running) test_stop();
        if (g_svc_ok) hidpollExit(&g_svc);
    }

    // The overlay process is kept alive in the background between summons, so
    // the sampler thread would otherwise keep polling hid forever after the
    // user dismisses the overlay. Stop it whenever we go invisible.
    void onHide() override {
        if (T.running) test_stop();
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainGui>();
    }
};

int main(int argc, char** argv) {
    return tsl::loop<HidpollOverlay>(argc, argv);
}
