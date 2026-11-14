#include "ui.h"
#include "i18n.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace ui {

M5Canvas canvas(&M5.Display);

static char s_toast[80];
static uint16_t s_toastCol = cfg::ACCENT;
static uint32_t s_toastUntil = 0;

// 所有中文走同一个字库，避免每处重复 setFont 后忘了切回来
const lgfx::IFont* cnFont() { return &fonts::efontCN_12; }

void cn(int x, int y, uint16_t col, const char* s) {
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextColor(col);
    canvas.drawString(s, x, y);
}

void cnf(int x, int y, uint16_t col, const char* fmt, ...) {
    char b[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    cn(x, y, col, b);
}

void cnBig(int x, int y, uint16_t col, const char* s) {
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(col);
    canvas.drawString(s, x, y);
}

int cnWidth(const char* s) {
    canvas.setFont(&fonts::efontCN_12);
    return canvas.textWidth(s);
}

bool begin() {
    // Cardputer-Adv 是 ESP32-S3FN8：8MB flash / 无 PSRAM，
    // 240x135x16bpp = 64.8KB 直接走内部 RAM，够用且更快
    canvas.setPsram(false);
    canvas.setColorDepth(16);
    if (!canvas.createSprite(cfg::W, cfg::H)) return false;
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextWrap(false);
    canvas.fillScreen(cfg::BG);
    return true;
}

void commit() { canvas.pushSprite(0, 0); }

void clear(uint16_t c) { canvas.fillScreen(c); }

void setFont(const lgfx::IFont* f) { canvas.setFont(f); }

void text(int x, int y, uint16_t col, const char* s) {
    canvas.setTextColor(col);
    canvas.drawString(s, x, y);
}

void textf(int x, int y, uint16_t col, const char* fmt, ...) {
    char b[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    canvas.setTextColor(col);
    canvas.drawString(b, x, y);
}

void textRight(int xRight, int y, uint16_t col, const char* s) {
    canvas.setTextColor(col);
    canvas.drawString(s, xRight - canvas.textWidth(s), y);
}

void textfRight(int xRight, int y, uint16_t col, const char* fmt, ...) {
    char b[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    textRight(xRight, y, col, b);
}

int width(const char* s) {
    canvas.setFont(&fonts::Font0);
    return canvas.textWidth(s);
}

void fill(int x, int y, int w, int h, uint16_t col) { canvas.fillRect(x, y, w, h, col); }

void panel(int x, int y, int w, int h, uint16_t fillCol, uint16_t border, int radius) {
    canvas.fillRoundRect(x, y, w, h, radius, fillCol);
    if (border != fillCol) canvas.drawRoundRect(x, y, w, h, radius, border);
}

void chip(int x, int y, int w, int h, bool on, const char* label) {
    if (on) {
        canvas.fillRoundRect(x, y, w, h, 3, cfg::ACCENT);
    } else {
        canvas.fillRoundRect(x, y, w, h, 3, cfg::SURFACE);
        canvas.drawRoundRect(x, y, w, h, 3, cfg::LINE);
    }
    canvas.setFont(&fonts::Font0);
    int tw = canvas.textWidth(label);
    canvas.setTextColor(on ? cfg::INK : cfg::MUTED);
    canvas.drawString(label, x + (w - tw) / 2, y + (h - 8) / 2);
}

void statusBar(const char* mode, const char* mid, bool ccOk, bool nfcOk, int bat) {
    canvas.fillRect(0, 0, cfg::W, cfg::STATUS_H, cfg::SURFACE);
    canvas.drawFastHLine(0, cfg::STATUS_H - 1, cfg::W, cfg::LINE);

    // 状态条里会出现中文模式名，必须用带中文字形的字体
    canvas.setFont(&fonts::efontCN_12);
    cn(6, 2, cfg::ACCENT, mode);
    // 英文模式名有长有短，mid 跟着右移，别和主标题挤在一起
    int midX = 6 + cnWidth(mode) + 8;
    if (midX < 66) midX = 66;
    if (mid && *mid) cn(midX, 2, cfg::MUTED, mid);

    // 电池
    char b[8];
    if (bat < 0) bat = 0;
    snprintf(b, sizeof(b), "%d%%", bat);
    int bw = canvas.textWidth(b);
    uint16_t bc = bat > 50 ? cfg::OK : (bat > 20 ? cfg::WARN : cfg::DANGER);
    if (M5.Power.isCharging()) bc = cfg::BLUE;
    text(cfg::W - 6 - bw, 2, bc, b);

    // CC / NFC 指示灯
    int x = cfg::W - 6 - bw - 40;
    text(x, 2, ccOk ? cfg::OK : cfg::DANGER, "CC");
    text(x + 16, 2, nfcOk ? cfg::OK : cfg::DIM, "NFC");

    // 语言徽标：任何屏都能一眼看到当前是「中」还是 EN，按 G 可切
    canvas.setFont(&fonts::efontCN_12);
    cn(x - 26, 2, cfg::ACCENT, i18n::tag());
}

void hint(const char* s) {
    canvas.fillRect(0, cfg::HINT_Y, cfg::W, cfg::HINT_H, cfg::SURFACE);
    canvas.drawFastHLine(0, cfg::HINT_Y, cfg::W, cfg::LINE);
    canvas.setFont(&fonts::efontCN_12);   // 中文提示（也兼容 ASCII 键名）
    text(6, cfg::HINT_Y + 2, cfg::MUTED, s);
}

void hintPair(const char* left, const char* right) {
    hint(left);
    canvas.setFont(&fonts::efontCN_12);
    textRight(cfg::W - 6, cfg::HINT_Y + 2, cfg::DIM, right);
}

void toast(const char* msg, uint16_t col) {
    strncpy(s_toast, msg, sizeof(s_toast) - 1);
    s_toast[sizeof(s_toast) - 1] = 0;
    s_toastCol = col;
    s_toastUntil = millis() + 1800;
}

void drawToast(uint32_t now) {
    if (!s_toast[0] || (int32_t)(now - s_toastUntil) > 0) return;
    canvas.setFont(&fonts::efontCN_12);
    int w = canvas.textWidth(s_toast) + 16;
    if (w > cfg::W - 8) w = cfg::W - 8;
    int x = (cfg::W - w) / 2;
    int y = cfg::HINT_Y - 28;
    canvas.fillRoundRect(x, y, w, 22, 4, cfg::SURFACE2);
    canvas.drawRoundRect(x, y, w, 22, 4, s_toastCol);
    int tw = canvas.textWidth(s_toast);
    canvas.setTextColor(s_toastCol);
    canvas.drawString(s_toast, x + (w - tw) / 2, y + 4);
}

const uint8_t* framebuffer(size_t& bytes) {
    bytes = (size_t)cfg::W * cfg::H * 2;
    return (const uint8_t*)canvas.getBuffer();
}

}  // namespace ui
