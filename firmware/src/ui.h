// 绘图层：离屏画布 + 统一视觉语言
//
// 用一整张 240x135 的精灵做离屏渲染，再一次性推屏。
// 好处：不闪、可以先算好再画、还能把缓冲区导出成截图（串口 snap 命令）。
#pragma once
#include <M5Cardputer.h>
#include "cfg.h"

namespace ui {

extern M5Canvas canvas;

bool begin();
void commit();
void clear(uint16_t c = cfg::BG);

void setFont(const lgfx::IFont* f);
void text(int x, int y, uint16_t col, const char* s);
void textf(int x, int y, uint16_t col, const char* fmt, ...);
void textRight(int xRight, int y, uint16_t col, const char* s);
void textfRight(int xRight, int y, uint16_t col, const char* fmt, ...);
int  width(const char* s);

// 中文：M5GFX 自带的 efontCN 字库同时含 ASCII，中英混排不会断字。
// 12px 用于提示/说明，16px 用于主标题，24px 用于收录模式的大字。
void cn(int x, int y, uint16_t col, const char* s);
void cnf(int x, int y, uint16_t col, const char* fmt, ...);
void cnBig(int x, int y, uint16_t col, const char* s);
int  cnWidth(const char* s);
const lgfx::IFont* cnFont();

void fill(int x, int y, int w, int h, uint16_t col);
void panel(int x, int y, int w, int h, uint16_t fillCol, uint16_t border, int radius = 3);
void chip(int x, int y, int w, int h, bool on, const char* label);

void statusBar(const char* mode, const char* mid, bool ccOk, bool nfcOk, int bat);
void hint(const char* s);
void hintPair(const char* left, const char* right);

void toast(const char* msg, uint16_t col);
void drawToast(uint32_t now);

// 截图：返回画布缓冲区指针（RGB565，W*H*2 字节）
const uint8_t* framebuffer(size_t& bytes);

}  // namespace ui
