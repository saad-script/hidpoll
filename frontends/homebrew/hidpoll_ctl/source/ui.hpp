#pragma once
#include <string>
#include <switch.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include "theme.hpp"

struct Ui {
    SDL_Window*   win     = nullptr;
    SDL_Renderer* r       = nullptr;
    TTF_Font*     f_hero  = nullptr;
    TTF_Font*     f_head  = nullptr;
    TTF_Font*     f_body  = nullptr;
    TTF_Font*     f_small = nullptr;
    PlFontData    plfont{};
};
extern Ui g_ui;

bool ui_init();
void ui_shutdown();

enum class TextAlign { Left, Center, Right };

void set_color(Color c);
void fill_rect(int x, int y, int w, int h, Color c);
void fill_rounded(int x, int y, int w, int h, Color c, int rad = 12);
void draw_hline(int x1, int x2, int y, Color c);
void draw_text(TTF_Font* f, const char* text, int x, int y, Color color,
               TextAlign a = TextAlign::Left, int max_w = 0);
int  text_w(TTF_Font* f, const char* text);
std::string elide(TTF_Font* f, const std::string& in, int max_w);

std::string sfmt(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Ephemeral bottom-of-screen notification. Call ui_toast() after any user
// action; draw_toast() is invoked once per frame after the rest of the UI.
void ui_toast(const std::string& msg, bool error = false);
void draw_toast();
