#include "ui.hpp"
#include <cstdarg>
#include <cstdio>

Ui g_ui;

static SDL_Color to_sdl(Color c) { return SDL_Color{c.r, c.g, c.b, c.a}; }

void set_color(Color c) {
    SDL_SetRenderDrawColor(g_ui.r, c.r, c.g, c.b, c.a);
}

void fill_rect(int x, int y, int w, int h, Color c) {
    set_color(c);
    SDL_Rect r{x, y, w, h};
    SDL_RenderFillRect(g_ui.r, &r);
}

void fill_rounded(int x, int y, int w, int h, Color c, int rad) {
    fill_rect(x + rad, y,       w - 2 * rad, h,           c);
    fill_rect(x,       y + rad, w,           h - 2 * rad, c);
    fill_rect(x + 2,   y + 2,   w - 4,       h - 4,       c);
}

void draw_hline(int x1, int x2, int y, Color c) {
    set_color(c);
    SDL_RenderDrawLine(g_ui.r, x1, y, x2, y);
}

void draw_text(TTF_Font* f, const char* text, int x, int y, Color color, TextAlign a, int max_w) {
    if (!text || !*text) return;

    SDL_Surface* s = TTF_RenderUTF8_Blended(f, text, to_sdl(color));
    if (!s) return;

    SDL_Texture* t = SDL_CreateTextureFromSurface(g_ui.r, s);
    int tw = s->w, th = s->h;
    SDL_FreeSurface(s);
    if (!t) return;

    if      (a == TextAlign::Center) x -= tw / 2;
    else if (a == TextAlign::Right)  x -= tw;

    if (max_w > 0 && tw > max_w) tw = max_w;

    SDL_Rect dst{x, y, tw, th};
    SDL_RenderCopy(g_ui.r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

int text_w(TTF_Font* f, const char* text) {
    int w = 0, h = 0;
    TTF_SizeUTF8(f, text, &w, &h);
    return w;
}

std::string elide(TTF_Font* f, const std::string& in, int max_w) {
    if (max_w <= 0) return in;
    if (text_w(f, in.c_str()) <= max_w) return in;

    std::string base = in;
    while (base.size() > 1) {
        base.pop_back();
        std::string s = base + "\xE2\x80\xA6"; // horizontal ellipsis
        if (text_w(f, s.c_str()) <= max_w) return s;
    }
    return "\xE2\x80\xA6";
}

// ---------- toast ----------

namespace {
constexpr Uint32 TOAST_VISIBLE_MS = 2200;   // solid on-screen time
constexpr Uint32 TOAST_FADE_MS    = 400;    // trailing fade-out

struct Toast {
    std::string msg;
    bool        error   = false;
    Uint32      expires = 0;   // SDL_GetTicks() value at which the toast is fully gone
} g_toast;
} // namespace

void ui_toast(const std::string& msg, bool error) {
    g_toast.msg     = msg;
    g_toast.error   = error;
    g_toast.expires = SDL_GetTicks() + TOAST_VISIBLE_MS + TOAST_FADE_MS;
}

void draw_toast() {
    if (g_toast.expires == 0) return;
    Uint32 now = SDL_GetTicks();
    if (now >= g_toast.expires) { g_toast.expires = 0; return; }

    // Compute fade alpha for the trailing FADE portion.
    Uint32 remaining = g_toast.expires - now;
    Uint8  alpha     = 0xff;
    if (remaining < TOAST_FADE_MS)
        alpha = (Uint8)((remaining * 255) / TOAST_FADE_MS);

    const int pad_x = 24;
    const int pad_y = 14;
    int tw = text_w(g_ui.f_body, g_toast.msg.c_str());
    int w  = tw + pad_x * 2;
    int h  = 48;
    int x  = (theme::W - w) / 2;
    int y  = theme::H - 40 - h - 16;   // sit above the hint bar

    Color bg = g_toast.error
        ? Color{0x3a, 0x1b, 0x1b, alpha}   // CHIP_BAD tint
        : Color{0x1b, 0x3a, 0x27, alpha};  // CHIP_OK tint
    Color fg = g_toast.error ? Color{0xff, 0x6b, 0x6b, alpha}
                             : Color{0xea, 0xef, 0xf6, alpha};

    fill_rect(x, y, w, h, bg);
    fill_rect(x, y, 4, h, g_toast.error
              ? Color{0xff, 0x6b, 0x6b, alpha}
              : Color{0x4c, 0xd9, 0x64, alpha});
    draw_text(g_ui.f_body, g_toast.msg.c_str(), x + pad_x, y + pad_y, fg);
}

std::string sfmt(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

bool ui_init() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
    if (TTF_Init() != 0)               return false;

    g_ui.win = SDL_CreateWindow("hidpoll", 0, 0, theme::W, theme::H, 0);
    if (!g_ui.win) return false;

    g_ui.r = SDL_CreateRenderer(g_ui.win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ui.r) return false;
    SDL_SetRenderDrawBlendMode(g_ui.r, SDL_BLENDMODE_BLEND);

    if (R_FAILED(plInitialize(PlServiceType_User)))                            return false;
    if (R_FAILED(plGetSharedFontByType(&g_ui.plfont, PlSharedFontType_Standard))) return false;

    auto load = [&](int pt) {
        SDL_RWops* rw = SDL_RWFromConstMem(g_ui.plfont.address, g_ui.plfont.size);
        return TTF_OpenFontRW(rw, 1, pt);
    };
    g_ui.f_hero  = load(56);
    g_ui.f_head  = load(32);
    g_ui.f_body  = load(22);
    g_ui.f_small = load(18);

    return g_ui.f_hero && g_ui.f_head && g_ui.f_body && g_ui.f_small;
}

void ui_shutdown() {
    if (g_ui.f_hero)  TTF_CloseFont(g_ui.f_hero);
    if (g_ui.f_head)  TTF_CloseFont(g_ui.f_head);
    if (g_ui.f_body)  TTF_CloseFont(g_ui.f_body);
    if (g_ui.f_small) TTF_CloseFont(g_ui.f_small);
    plExit();
    if (g_ui.r)   SDL_DestroyRenderer(g_ui.r);
    if (g_ui.win) SDL_DestroyWindow(g_ui.win);
    TTF_Quit();
    SDL_Quit();
}
