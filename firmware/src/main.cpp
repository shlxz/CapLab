// Cardputer-Adv + Cap CC1101 (U219)
// Sub-GHz 学习 / 重放 / 频谱 + NFC 读卡
//
// 交互（全中文界面，统一一套键）：
//   ENTER 确认/主操作   空格 开始/停止   ; . 上下选择   DEL 返回   1-6 菜单直选
//   各屏专用键都写在屏幕底部，帮助屏有完整对照表
// 串口：help 查看调试命令（snap 可把屏幕内容回传到电脑）

#include <M5Cardputer.h>
#include <Preferences.h>
#include <esp_log.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "cfg.h"
#include "ui.h"
#include "i18n.h"
#include "cc1101_cap.h"
#include "capture.h"
#include "nfc_reader.h"

CapCC1101 rf;

// ── 全局状态 ─────────────────────────────────────────────
enum Screen : uint8_t { SCR_MENU, SCR_CAPTURE, SCR_SCAN, SCR_NFC, SCR_EMU, SCR_INFO, SCR_HELP };
constexpr int MENU_N = 6;

static Screen  screen = SCR_MENU;
static Screen  helpFrom = SCR_MENU;   // 按 H 之前的屏，DEL / 再按 H 原路返回
static uint8_t menuSel = 0;
static CapBand band = BAND_433;
static bool    txAllowed = false;
static uint32_t lastFrame = 0;
static char g_lastKey[24] = "-";

// 频谱
static constexpr int SCAN_CH = 32;
static int8_t  scanVal[SCAN_CH];
static int8_t  scanPeak[SCAN_CH];
static int8_t  scanTop = -120;
static bool    scanRun = true;
static uint16_t scanIdx = 0;
static uint32_t scanAt = 0;

// NFC
static nfc::Card card;
enum NfcState : uint8_t { NFC_IDLE, NFC_SCANNING, NFC_FOUND, NFC_ERROR };
static NfcState nfcSt = NFC_IDLE;
static uint32_t nfcAt = 0;

// 收录模式：常驻等卡，不超时、不误报 no card。
// 主循环每 180ms 做一次「选卡 + 读全片」的单次握手，读到就自动收录。
// 用户不贴卡就一直等（贴卡找位置本来就要时间），DEL 随时退出。
static bool cloneArmed = false;
static uint32_t cloneScanAt = 0;
static uint32_t cloneTries = 0;
static uint8_t nfcMisses = 0;   // 连续扫描落空次数，用来触发芯片重初始化

// 卡库列表（EMULATE 屏按 L 打开）
static bool libOpen = false;
static int libSel = 0;
static int libTop = 0;

// 存枚举而不是字符串指针：全局数组只初始化一次，缓存指针会让切语言后菜单不更新。
static const i18n::Str MENU_LABEL[MENU_N] = {
    i18n::S_MENU_CAPTURE, i18n::S_MENU_SCAN, i18n::S_MENU_NFC,
    i18n::S_MENU_EMU,     i18n::S_MENU_INFO, i18n::S_MENU_HELP};
static const i18n::Str MENU_SUB[MENU_N] = {
    i18n::S_SUB_CAPTURE, i18n::S_SUB_SCAN, i18n::S_SUB_NFC,
    i18n::S_SUB_EMU,     i18n::S_SUB_INFO, i18n::S_SUB_HELP};

static int batPct() {
    int b = (int)M5.Power.getBatteryLevel();
    if (b < 0) b = 0;
    if (b > 100) b = 100;
    return b;
}

// ── 设置持久化 ───────────────────────────────────────────
static void loadSettings() {
    Preferences p;
    if (!p.begin("app", true)) return;
    band = (CapBand)p.getUChar("band", BAND_433);
    if (band > BAND_915) band = BAND_433;
    txAllowed = p.getBool("tx", false);
    i18n::setEn(p.getBool("en", false));   // 界面语言跨重启保留
    p.end();
}

static void saveSettings() {
    Preferences p;
    if (!p.begin("app", false)) return;
    p.putUChar("band", (uint8_t)band);
    p.putBool("tx", txAllowed);
    p.putBool("en", i18n::isEn());
    p.end();
}

// G：任意界面一键切中英。只改语言标志并立刻重绘，不碰 RF / 模拟 / 收录状态。
static void toggleLang() {
    i18n::setEn(!i18n::isEn());
    saveSettings();
    ui::toast(T(S_LANG_TOAST), cfg::ACCENT);
    // 不在这里强制 render：主循环 33ms 内会重绘，避免为前向声明绕一圈
}

// ── 启动画面 ─────────────────────────────────────────────
static void drawSplash(const char* status) {
    ui::clear(cfg::BG);
    ui::fill(0, 0, cfg::W, 3, cfg::ACCENT);
    ui::setFont(&fonts::Font4);
    ui::text(12, 36, cfg::TEXT, "CAP LAB");
    ui::setFont(&fonts::Font0);
    ui::text(14, 70, cfg::ACCENT, "CC1101 + ST25R3916");
    ui::text(14, 82, cfg::MUTED, "sub-GHz & NFC toolkit");
    ui::cn(14, 103, cfg::DIM, status);   // 中文状态也走中文字库，别用 Font0 画豆腐块
    ui::textfRight(228, 103, cfg::DIM, "v%s", cfg::FW_VERSION);  // 沿用 cn 设好的中文字库
    ui::commit();
}

// ── 菜单 ─────────────────────────────────────────────────
static void drawMenu() {
    ui::clear(cfg::BG);
    ui::statusBar(T(S_SB_MAIN), txAllowed ? T(S_TX_UNLOCKED) : T(S_TX_LOCKED), rf.present(), nfc::ready(), batPct());

    for (int i = 0; i < MENU_N; i++) {
        int y = 17 + i * 17;   // 6 项 × 17 = 102，最后一行正好停在提示条上沿
        bool sel = (i == menuSel);

        if (sel) {
            ui::panel(3, y, 234, 16, cfg::SURFACE2, cfg::LINE, 4);
            ui::fill(5, y + 3, 3, 10, cfg::ACCENT);
        }

        ui::canvas.fillRoundRect(13, y + 1, 14, 14, 3, sel ? cfg::ACCENT : cfg::SURFACE);
        if (!sel) ui::canvas.drawRoundRect(13, y + 1, 14, 14, 3, cfg::LINE);
        ui::setFont(&fonts::Font0);
        {
            char num[2] = {(char)('1' + i), 0};
            int w = ui::width(num);
            ui::text(13 + (14 - w) / 2, y + 4, sel ? cfg::INK : cfg::MUTED, num);
        }

        ui::cn(33, y + 1, sel ? cfg::TEXT : cfg::MUTED, TV(MENU_LABEL[i]));
        ui::cn(126, y + 2, sel ? cfg::ACCENT : cfg::DIM, TV(MENU_SUB[i]));
    }

    ui::hintPair(T(S_HINT_MENU_L), T(S_HINT_MENU_R));
}

// ── 操作帮助 ─────────────────────────────────────────────
// 用户反馈「快捷键繁杂看不懂」，所以把所有键收进一屏中文对照表，
// 并且全项目只保留一套通用规则 + 少量在底部标注过的专用键。
static void drawHelp() {
    ui::clear(cfg::BG);
    ui::statusBar(T(S_MENU_HELP), T(S_SB_KEYS), rf.present(), nfc::ready(), batPct());

    ui::cn(6, 19, cfg::ACCENT, T(S_COL_GLOBAL));
    ui::cn(124, 19, cfg::ACCENT, T(S_COL_APP));
    ui::canvas.drawFastHLine(6, 33, 228, cfg::LINE);

    // 两列各占 112px：键名一列、说明一列。12px 中文一字约 12px，
    // 说明必须压在 4~5 字以内，否则会串到隔壁列去。
    // 不能加 static：局部静态数组只初始化一次，切语言后键名/说明会是旧的。
    const char* kGen[6][2] = {
        {"H", T(S_H_HELP)}, {"ENTER", T(S_H_CONFIRM)}, {T(S_H_SPACE), T(S_H_ONOFF)},
        {";  .", T(S_H_UPDOWN)}, {"DEL", T(S_H_BACK)}, {"1 - 6", T(S_H_DIRECT)},
    };
    const char* kApp[6][2] = {
        {"L", T(S_H_LIB)}, {"D", T(S_H_DEL)}, {"B", T(S_H_BUILTIN)},
        {"N  U", T(S_H_CONTENT)}, {"R  C", T(S_H_REPLAY)}, {"T", T(S_H_TX)},
    };
    for (int i = 0; i < 6; i++) {
        const int y = 36 + i * 13;
        if (kGen[i][0][0]) {
            ui::cn(6, y, cfg::TEXT, kGen[i][0]);
            ui::cn(58, y, cfg::MUTED, kGen[i][1]);
        }
        if (kApp[i][0][0]) {
            ui::cn(124, y, cfg::TEXT, kApp[i][0]);
            ui::cn(160, y, cfg::MUTED, kApp[i][1]);
        }
    }

    ui::hintPair(T(S_HINT_HELP_L), T(S_HINT_HELP_R));
}

// ── 采集 ─────────────────────────────────────────────────
static void drawWave(int x, int y, int w, int h) {
    ui::panel(x, y, w, h, cfg::SURFACE, cfg::LINE, 3);
    const auto& d = capture::data();
    if (!d.n) {
        ui::setFont(&fonts::Font0);
        ui::cn(x + 8, y + h / 2 - 7, cfg::DIM, T(S_NO_SIGNAL));
        return;
    }

    uint16_t mx = 1;
    for (uint16_t i = 0; i < d.n; i++)
        if (d.dur[i] > mx) mx = d.dur[i];
    float lmx = logf((float)mx + 1.0f);

    int innerW = w - 8;
    int halfH  = h / 2 - 3;
    int midY   = y + h / 2;

    for (uint16_t i = 0; i < d.n; i++) {
        int px = x + 4 + (int)((int32_t)i * innerW / d.n);
        int nx = x + 4 + (int)((int32_t)(i + 1) * innerW / d.n);
        int bw = nx - px;
        if (bw < 1) bw = 1;

        float r = logf((float)d.dur[i] + 1.0f) / lmx;
        int bh = (int)(r * halfH);
        if (bh < 2) bh = 2;

        uint16_t col = d.lvl[i] ? cfg::ACCENT : cfg::BLUE;
        if (d.lvl[i]) ui::fill(px, midY - bh, bw, bh, col);
        else          ui::fill(px, midY, bw, bh, col);
    }
    ui::canvas.drawFastHLine(x + 3, midY, w - 6, cfg::LINE);
}

static void drawCapture() {
    ui::clear(cfg::BG);
    char mid[20];
    snprintf(mid, sizeof(mid), "%.2fMHz", CapCC1101::bandOf(band));
    ui::statusBar(T(S_MENU_CAPTURE), mid, rf.present(), nfc::ready(), batPct());

    // 频段选择
    for (int i = 0; i < BAND_COUNT; i++) {
        char lbl[6];
        snprintf(lbl, sizeof(lbl), "%s", CapCC1101::bandName((CapBand)i));
        ui::chip(4 + i * 59, 20, 54, 15, (i == band), lbl);
    }

    const auto& dec = capture::decoded();
    bool armed = capture::armed();

    // 主值
    ui::setFont(&fonts::Font4);
    ui::text(6, 38, armed ? cfg::DIM : (dec.ok ? cfg::ACCENT : cfg::DIM), dec.text);

    ui::setFont(&fonts::Font0);
    if (armed) {
        ui::cn(6, 64, cfg::DANGER, T(S_LISTENING));
    } else if (dec.ok) {
        ui::text(6, 66, cfg::MUTED, dec.kind);
        if (dec.repeats > 1) ui::textfRight(232, 66, cfg::ACCENT, "x%u repeat", dec.repeats);
    } else if (capture::hasData()) {
        ui::cn(6, 64, cfg::MUTED, capture::minUs() < 100 ? T(S_NOISE_ANT) : T(S_RAW_WAVE));
    } else {
        ui::cn(6, 64, cfg::DIM, T(S_NO_CAPTURE));
    }

    drawWave(4, 74, 232, 26);

    ui::setFont(&fonts::Font0);
    if (armed) {
        ui::textf(6, 103, cfg::DANGER, "REC  %u edges", capture::liveEdges());
    } else if (capture::hasData()) {
        ui::textf(6, 103, cfg::MUTED, "n=%u  T=%uus  %lu.%02lums", capture::data().n, dec.base_us,
                  (unsigned long)(capture::totalUs() / 1000), (unsigned long)((capture::totalUs() % 1000) / 10));
    } else {
        ui::cn(6, 101, cfg::DIM, T(S_PRESS_CAPTURE));
    }
    if (!txAllowed) ui::cn(160, 101, cfg::WARN, T(S_TX_LOCKED));

    ui::hintPair(armed ? T(S_HINT_CAP_STOP) : T(S_HINT_CAP_GO), T(S_HINT_BACK));
}

// ── 频谱 ─────────────────────────────────────────────────
static void sweepStep() {
    const float span = 3.0f;   // ±1.5MHz
    float center = CapCC1101::bandOf(band);
    float f = center - span / 2 + (float)scanIdx * span / (SCAN_CH - 1);

    // 必须真正进 RX 再读 RSSI。
    // 早先的写法是「改频 + CC_SCAL + 读 RSSI」，而 SCAL 是 IDLE 下的动作，
    // 芯片留在 IDLE 时 RSSI 寄存器不是有效测量值，整屏会恒定显示同一个假读数。
    // MCSM0=0x18 表示 IDLE->RX 会自动校准，所以这里只需要 SIDLE -> 改频 -> SRX。
    rf.idle();                    // SIDLE + 清 FIFO
    rf.setFrequencyMHz(f);
    rf.strobe(CC_SRX);            // IDLE -> RX，顺带自动校准

    // 等校准结束：MARCSTATE 变回 RX(0x0D) 之后 RSSI 才有意义
    uint32_t t0 = micros();
    while (rf.marcState() != 0x0D && (micros() - t0) < 4000) {}

    // 噪声会抖，取几次的最大值当主读数（等效峰值保持）
    int8_t v = -120;
    for (int k = 0; k < 3; k++) {
        delayMicroseconds(130);
        int8_t s = rf.rssi();
        if (s > v) v = s;
    }

    scanVal[scanIdx] = v;
    if (v > scanPeak[scanIdx]) scanPeak[scanIdx] = v;
    if (v > scanTop) scanTop = v;

    scanIdx = (scanIdx + 1) % SCAN_CH;
}

static void drawScan() {
    ui::clear(cfg::BG);
    char mid[20];
    snprintf(mid, sizeof(mid), "%.2fMHz", CapCC1101::bandOf(band));
    ui::statusBar(T(S_MENU_SCAN), mid, rf.present(), nfc::ready(), batPct());

    const int topDBm = -45, botDBm = -115;
    const int topY = 56, baseY = 100;
    auto yOf = [&](int dbm) -> int {
        if (dbm < botDBm) dbm = botDBm;
        if (dbm > topDBm) dbm = topDBm;
        return baseY - (dbm - botDBm) * (baseY - topY) / (topDBm - botDBm);
    };

    // 峰值读数
    char num[8];
    bool noData = scanTop <= -119;
    if (noData) snprintf(num, sizeof(num), "---");
    else snprintf(num, sizeof(num), "%d", scanTop);
    uint16_t col = noData ? cfg::DIM
                          : (scanTop > -60 ? cfg::DANGER : (scanTop > -80 ? cfg::WARN : cfg::ACCENT));

    ui::setFont(&fonts::Font0);
    ui::cn(6, 18, cfg::MUTED, T(S_PEAK));
    ui::setFont(&fonts::Font4);
    ui::text(6, 28, col, num);
    int nw = ui::canvas.textWidth(num);
    ui::setFont(&fonts::Font0);
    ui::text(10 + nw, 44, cfg::MUTED, "dBm");

    ui::cn(184, 18, scanRun ? cfg::OK : cfg::WARN, scanRun ? T(S_SCANNING) : T(S_PAUSED));
    ui::textfRight(232, 32, cfg::DIM, "span 3.0MHz");

    // 刻度线
    for (int g = -100; g <= -60; g += 20) {
        int gy = yOf(g);
        ui::canvas.drawFastHLine(4, gy, 232, cfg::LINE);
        ui::textfRight(234, gy - 8, cfg::DIM, "%d", g);
    }

    // 柱状图：32 通道铺满整条网格线（4..235），槽宽 7 / 柱宽 6
    const int plotX = 4, plotW = 232, slot = 7, barW = 6;
    const int x0 = plotX + (plotW - SCAN_CH * slot) / 2;
    ui::canvas.drawFastHLine(plotX, baseY, plotW, cfg::LINE);
    for (int i = 0; i < SCAN_CH; i++) {
        int x = x0 + i * slot;
        int v = scanVal[i];
        if (v <= -119) {                    // 还没扫到的通道：只留一条极淡底痕
            ui::fill(x, baseY - 1, barW, 1, cfg::LINE);
            continue;
        }
        int by = yOf(v);
        int hgt = baseY - by;
        if (hgt < 2) hgt = 2;               // 静噪底也要看得见，否则整屏像空的
        uint16_t c = (v > -60) ? cfg::DANGER : (v > -80 ? cfg::WARN : cfg::ACCENT);
        ui::fill(x, baseY - hgt, barW, hgt, c);

        int pk = scanPeak[i];
        if (pk > botDBm) {
            int py = yOf(pk);
            if (py < baseY - hgt - 2) ui::fill(x, py - 2, barW, 2, cfg::TEXT);
        }
    }

    // 频率刻度（底轴下方一行）
    ui::setFont(&fonts::Font0);
    const int fx = baseY + 3;
    ui::textf(6, fx, cfg::DIM, "%.2f", CapCC1101::bandOf(band) - 1.5f);
    ui::textfRight(234, fx, cfg::DIM, "%.2f", CapCC1101::bandOf(band) + 1.5f);
    {
        char c[16];
        snprintf(c, sizeof(c), "%.2f", CapCC1101::bandOf(band));
        int w = ui::width(c);
        ui::text((cfg::W - w) / 2, fx, cfg::MUTED, c);
    }

    ui::hint(T(S_HINT_SCAN));
}

// ── NFC ──────────────────────────────────────────────────
// 空状态的小图标：一张卡 + 几道电波（用折线画弧，避免不同版本 drawArc 角度约定差异）
static void drawTagGlyph() {
    ui::canvas.drawRoundRect(146, 44, 38, 30, 4, cfg::SURFACE2);
    ui::canvas.drawFastHLine(152, 53, 26, cfg::SURFACE2);
    ui::canvas.drawFastHLine(152, 62, 15, cfg::SURFACE2);

    for (int r = 9; r <= 21; r += 6) {
        int px = 0, py = 0;
        for (int a = -50; a <= 50; a += 10) {
            float rad = a * 0.0174533f;
            int x = 188 + (int)(r * cosf(rad));
            int y = 59 + (int)(r * sinf(rad));
            if (a > -50) ui::canvas.drawLine(px, py, x, y, cfg::ACCENT);
            px = x;
            py = y;
        }
    }
}

static void drawNfc() {
    ui::clear(cfg::BG);
    ui::statusBar(T(S_MENU_NFC), "13.56MHz", rf.present(), nfc::ready(), batPct());

    // 状态徽标
    const char* stTxt = T(S_READY);
    uint16_t stCol = cfg::MUTED;
    if (cloneArmed) {
        stTxt = T(S_SAVING); stCol = cfg::ACCENT;
    } else {
        switch (nfcSt) {
            case NFC_SCANNING: stTxt = T(S_SCANNING); stCol = cfg::WARN; break;
            case NFC_FOUND:    stTxt = T(S_READ_OK); stCol = cfg::OK;   break;
            case NFC_ERROR:    stTxt = T(S_ERROR);   stCol = cfg::DANGER; break;
            default:           stTxt = T(S_READY);   stCol = cfg::MUTED; break;
        }
    }
    ui::setFont(&fonts::efontCN_12);
    ui::textRight(234, 18, stCol, stTxt);

    if (!nfc::ready()) {
        ui::cnBig(10, 48, cfg::DANGER, T(S_NFC_OFF));
        ui::cn(10, 72, cfg::MUTED, nfc::lastError());
        ui::hintPair(T(S_HINT_BACK), "");
        return;
    }

    // 收录模式常驻显示，不受上一帧的 card.present 干扰。
    // 没有超时：贴卡找位置本来就要时间，等到用户按 DEL 为止。
    if (cloneArmed) {
        drawTagGlyph();
        ui::cnBig(10, 38, cfg::ACCENT, T(S_WAIT_CARD));
        ui::cn(10, 60, cfg::TEXT, T(S_PLACE_TAG));
        ui::cn(10, 76, cfg::MUTED, T(S_AUTO_SAVE));
        // 轮流亮的小方块，告诉用户固件没卡死、一直在找
        for (int i = 0; i < 4; i++) {
            const uint16_t c = (((cloneTries / 2) + i) % 4 == 0) ? cfg::ACCENT : cfg::LINE;
            ui::fill(10 + i * 11, 96, 7, 7, c);
        }
        ui::cnf(62, 94, cfg::DIM, T(S_TRIES), (unsigned)cloneTries);
        ui::hintPair(T(S_HINT_WAIT), T(S_NO_TIMEOUT));
        return;
    }

    if (!card.present) {
        drawTagGlyph();
        ui::cnBig(10, 40, cfg::MUTED, T(S_PLACE_TAG_BIG));
        ui::cn(10, 62, cfg::MUTED, T(S_BACK_SIDE));
        ui::cn(10, 82, cfg::DIM, "MIFARE / NTAG (NFC-A)");
        ui::hintPair(T(S_HINT_NFC), T(S_HINT_BACK));
        return;
    }

    ui::setFont(&fonts::Font4);
    ui::text(6, 30, cfg::ACCENT, card.uid);

    ui::setFont(&fonts::Font2);
    ui::text(6, 62, cfg::TEXT, card.type);

    ui::setFont(&fonts::Font0);
    ui::textf(6, 84, cfg::MUTED, "ATQA %04X   SAK %02X   %u blk", card.atqa, card.sak, card.blocks);

    uint16_t nc = card.ndefTried ? (card.ndefCapable ? cfg::TEXT : cfg::DIM) : cfg::DIM;
    ui::setFont(&fonts::efontCN_12);
    if (card.ndefTried) ui::cnf(6, 98, nc, "%.28s", card.ndef);
    else ui::cn(6, 98, nc, card.ndefCapable ? T(S_D_READ) : T(S_NO_NDEF));

    ui::hintPair(T(S_HINT_NFC_FOUND), T(S_HINT_BACK));
}

// ── 卡模拟 ───────────────────────────────────────────────
// 卡库列表：一张卡占一行，四个可见行，超出滚动。
// 只显示 UID 末三字节 —— 完整 7 字节塞不下，末三字节足够区分。
static void drawLib() {
    ui::clear(cfg::BG);
    char cnt[12];
    snprintf(cnt, sizeof(cnt), T(S_N_CARDS), nfc::libCount());
    ui::statusBar(T(S_H_LIB), cnt, rf.present(), nfc::ready(), batPct());

    const int n = nfc::libCount();
    if (n == 0) {
        ui::cnBig(10, 44, cfg::MUTED, T(S_NO_CARDS));
        ui::cn(10, 68, cfg::DIM, T(S_GO_NFC));
        ui::hint(T(S_HINT_BACK));
        return;
    }

    constexpr int kPer = 4;
    if (libSel < 0) libSel = 0;
    if (libSel >= n) libSel = n - 1;
    if (libSel < libTop) libTop = libSel;
    if (libSel >= libTop + kPer) libTop = libSel - kPer + 1;
    if (libTop < 0) libTop = 0;

    for (int row = 0; row < kPer; row++) {
        const int i = libTop + row;
        if (i >= n) break;
        const nfc::Tag* t = nfc::libGet(i);
        if (!t) continue;

        const int y = 17 + row * 22;
        const bool sel = (i == libSel);
        if (sel) ui::panel(4, y - 2, 232, 19, cfg::SURFACE2, cfg::ACCENT, 2);

        const uint16_t c = sel ? cfg::TEXT : cfg::MUTED;
        ui::textf(15, y, sel ? cfg::ACCENT : cfg::DIM, "%d", i + 1);
        ui::textf(30, y, c, "%s", nfc::tagTypeName(t->typeCode));

        const int from = t->uidLen > 3 ? t->uidLen - 3 : 0;
        ui::textf(118, y, sel ? cfg::TEXT : cfg::DIM, "%02X:%02X:%02X", t->uid[from], t->uid[from + 1],
                  t->uid[from + 2]);
        ui::textfRight(232, y, sel ? cfg::TEXT : cfg::DIM, "%uB", (unsigned)t->userLen);

        if (i == nfc::libCurrent()) {
            ui::text(4, y, cfg::OK, "*");   // 当前正在用的那张
        }
    }

    // 一行放得下，就别拆左右两栏挤在一起
    ui::hintPair(T(S_HINT_LIB), T(S_HINT_BACK));
}

// 模拟状态给中文，别让用户对着 "waiting for reader" 猜
static const char* emuStateCN(nfc::EmuState st) {
    switch (st) {
        case nfc::EmuState::Active: return T(S_EMU_ACTIVE);
        case nfc::EmuState::Ready:  return T(S_EMU_READY);
        case nfc::EmuState::Error:  return T(S_EMU_ERROR);
        default:                    return T(S_EMU_IDLE);
    }
}

static void drawEmu() {
    if (libOpen) { drawLib(); return; }

    ui::clear(cfg::BG);
    const bool on = nfc::emuRunning();
    const nfc::EmuState st = nfc::emuState();
    ui::statusBar(T(S_MENU_EMU), on ? T(S_EMU_ON) : T(S_EMU_OFF), rf.present(), nfc::ready(), batPct());

    if (!nfc::ready()) {
        ui::cnBig(10, 48, cfg::DANGER, T(S_NFC_OFF));
        ui::cn(10, 72, cfg::MUTED, nfc::lastError());
        ui::hint(T(S_HINT_BACK));
        return;
    }

    const uint16_t ink = on ? cfg::ACCENT : cfg::LINE;

    // 卡片轮廓：双线圆角，比单线更有实物感
    const int cx = 8, cy = 24, cw = 56, ch = 50;
    ui::canvas.drawRoundRect(cx, cy, cw, ch, 5, ink);
    ui::canvas.drawRoundRect(cx + 2, cy + 2, cw - 4, ch - 4, 4, ink);

    // 居中的芯片 + 两侧引脚（照真实卡片的摆法）
    const int chipX = cx + 18, chipW = 20, chipY = cy + 8, chipH = 14;
    ui::fill(chipX, chipY, chipW, chipH, ink);
    for (int i = 0; i < 3; i++) {
        const int py = chipY + 3 + i * 4;
        ui::canvas.drawFastHLine(cx + 7, py, chipX - (cx + 7), ink);
        ui::canvas.drawFastHLine(chipX + chipW, py, (cx + cw - 7) - (chipX + chipW), ink);
    }
    // 磁条
    ui::canvas.drawFastHLine(cx + 8, cy + 32, cw - 16, ink);
    ui::canvas.drawFastHLine(cx + 8, cy + 37, cw - 24, ink);

    // RF 波纹：只在模拟运行时亮，三道轮流呼吸
    if (on) {
        const uint32_t phase = millis() / 260;
        for (int i = 0; i < 3; i++) {
            const int r = 6 + i * 5;
            const uint16_t c = (((phase + i) % 3) == 0) ? cfg::ACCENT : cfg::LINE;
            int px = 0, py = 0;
            for (int a = -55; a <= 55; a += 11) {
                const float rad = (float)a * 0.0174533f;
                const int x = cx + cw + 4 + (int)(r * cosf(rad));
                const int y = cy + ch / 2 + (int)(r * sinf(rad));
                if (a > -55) ui::canvas.drawLine(px, py, x, y, c);
                px = x;
                py = y;
            }
        }
    }

    // 右侧信息
    const int tx = 92;
    const bool cloned = nfc::cloneActive();
    ui::setFont(&fonts::Font2);
    ui::text(tx, 25, on ? cfg::TEXT : cfg::MUTED,
             cloned ? nfc::tagTypeName(nfc::cloneCurrent().typeCode) : "NTAG213");

    ui::setFont(&fonts::Font0);
    ui::text(tx, 45, on ? cfg::TEXT : cfg::DIM, nfc::emuUidText());
    ui::cnf(tx, 58, cfg::DIM, T(S_HITS), (unsigned)nfc::emuHits());

    // 内容
    ui::cn(8, 76, cfg::MUTED, T(S_CONTENT));
    if (cloned) {
        // 克隆卡里是原始页数据，不是我们能读懂的 NDEF 字符串
        ui::cnf(8, 90, on ? cfg::TEXT : cfg::DIM, T(S_CLONE_BYTES),
                (unsigned)nfc::cloneCurrent().userLen);
    } else {
        ui::setFont(&fonts::Font0);
        ui::textf(8, 90, on ? cfg::TEXT : cfg::DIM, "%.34s", nfc::emuContentText());
    }

    // 状态点 + 文字
    uint16_t dot = cfg::DIM;
    if (on) {
        switch (st) {
            case nfc::EmuState::Active:  dot = cfg::OK; break;
            case nfc::EmuState::Ready:   dot = cfg::WARN; break;
            case nfc::EmuState::Error:   dot = cfg::DANGER; break;
            default:                     dot = cfg::ACCENT; break;
        }
    }
    // 内容行在 y=90（12px 字高到 102），状态行必须让开至少 13px：
    // 原来放 y=99 会和中文「克隆卡 504 字节」叠字，英文因为字形矮才没暴露。
    ui::canvas.fillCircle(12, 109, 3, dot);
    ui::cn(20, 103, on ? dot : cfg::DIM, emuStateCN(st));

    // 底部只写这一屏真正能用的键；不懂就去看帮助屏
    if (cloned || nfc::cloneStored()) {
        ui::hintPair(T(S_HINT_EMU_LIB), T(S_HINT_BACK));
    } else {
        ui::hintPair(T(S_HINT_EMU_DEF), T(S_HINT_BACK));
    }
}

// ── 信息 ─────────────────────────────────────────────────
static void infoRow(int y, const char* k, const char* v, uint16_t c) {
    ui::cn(10, y - 1, cfg::MUTED, k);
    ui::setFont(&fonts::efontCN_12);
    ui::textRight(232, y - 1, c, v);
}

// MARCSTATE 的十六进制对用户没意义，映射成状态名（不认识的留 0xNN）。
// 枚举值照 CC1101 datasheet 的 MARCSTATE[4:0]，注意 0x10 之后是
// TXRX_SWITCH / TX / TX_END / RXTX_SWITCH 这一段，很容易整体写错一位。
static const char* marcName(uint8_t m) {
    switch (m) {
        case 0x00: return "SLEEP";
        case 0x01: return "IDLE";
        case 0x02: return "XOFF";
        case 0x05: return "MAN CAL";
        case 0x06: return "VCO ON";
        case 0x07: return "REG ON";
        case 0x08: return "START CAL";
        case 0x0A: return "FS LOCK";
        case 0x0B: return "IFADCON";
        case 0x0C: return "CAL DONE";
        case 0x0D: return "RX";
        case 0x0E: return "RX END";
        case 0x0F: return "RX RESET";
        case 0x10: return "TXRX SWAP";
        case 0x11: return "TX";
        case 0x12: return "TX END";
        case 0x13: return "RXTX SWAP";
        case 0x14: return "RXTX SPARE";
        default:   return nullptr;
    }
}

static void drawInfo() {
    ui::clear(cfg::BG);
    ui::statusBar(T(S_MENU_INFO), txAllowed ? T(S_TX_UNLOCKED) : T(S_TX_LOCKED), rf.present(), nfc::ready(), batPct());

    char v[28];
    int y = 20;
    if (rf.present()) snprintf(v, sizeof(v), T(S_OK_VER), cfg::FW_VERSION);
    else              snprintf(v, sizeof(v), T(S_NOT_FOUND));
    infoRow(y, "CC1101", v, rf.present() ? cfg::OK : cfg::DANGER);
    y += 12;
    // 单次读偶发错位：曾抓到过 0x11(TX) 而设备根本没在发射。
    // 连读三次取多数，顺便让 RSSI 那行用同一个值判断，免得两行自相矛盾。
    uint8_t m = rf.marcState();
    {
        const uint8_t b = rf.marcState(), c = rf.marcState();
        if (b == c && b != m) m = b;
    }
    {
        const char* nm = marcName(m);
        if (nm) snprintf(v, sizeof(v), "%s", nm);
        else    snprintf(v, sizeof(v), "0x%02X", m);
        // IDLE/TX END 这类"静止"状态用灰色，RX 这种"在工作"的用主色
        uint16_t c = (m == 0x0D) ? cfg::ACCENT : (nm ? cfg::MUTED : cfg::WARN);
        infoRow(y, T(S_RF_STATE), v, c);
    }
    y += 12;
    // RSSI 只在 RX 状态下有效；IDLE 时读出来是钳位值，显示成 n/a 免得像故障
    if (m == 0x0D) {
        snprintf(v, sizeof(v), "%d dBm", (int)rf.rssi());
        infoRow(y, "RSSI", v, cfg::TEXT);
    } else {
        infoRow(y, "RSSI", T(S_IDLE), cfg::DIM);
    }
    y += 12;
    snprintf(v, sizeof(v), "%.2f MHz", CapCC1101::bandOf(band));
    infoRow(y, T(S_BAND), v, cfg::TEXT); y += 12;
    infoRow(y, T(S_H_TX), txAllowed ? T(S_ARMED) : T(S_LOCKED), txAllowed ? cfg::DANGER : cfg::OK);
    y += 12;
    infoRow(y, T(S_NFC_CHIP),
            !nfc::ready() ? T(S_ERROR) : (nfc::idOk() ? T(S_OK) : T(S_NO_ID)),
            !nfc::ready() ? cfg::DANGER : (nfc::idOk() ? cfg::OK : cfg::WARN));
    y += 12;
    snprintf(v, sizeof(v), "%.2fV  %d%%", M5.Power.getBatteryVoltage() / 1000.0f, batPct());
    infoRow(y, T(S_BATTERY), v, cfg::TEXT); y += 12;
    {
        uint32_t s = millis() / 1000;
        snprintf(v, sizeof(v), "%02lu:%02lu:%02lu", (unsigned long)(s / 3600),
                 (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
        infoRow(y, T(S_UPTIME), v, cfg::TEXT);
    }

    ui::hint(txAllowed ? T(S_HINT_INFO_LOCK) : T(S_HINT_INFO_UNLOCK));
}

// ── 渲染 ─────────────────────────────────────────────────
static void render(uint32_t now) {
    switch (screen) {
        case SCR_MENU:    drawMenu();    break;
        case SCR_CAPTURE: drawCapture(); break;
        case SCR_SCAN:    drawScan();    break;
        case SCR_NFC:     drawNfc();     break;
        case SCR_EMU:     drawEmu();     break;
        case SCR_INFO:    drawInfo();    break;
        case SCR_HELP:    drawHelp();    break;
    }
    ui::drawToast(now);
    ui::commit();
}

// ── 动作 ─────────────────────────────────────────────────
static void enterCapture() {
    screen = SCR_CAPTURE;
    // 保留上次的信号，从菜单来回切换不该丢掉
    if (capture::armed()) capture::disarm();
    rf.setBand(band);
    rf.idle();
}

static void enterScan() {
    screen = SCR_SCAN;
    if (capture::armed()) capture::disarm();
    rf.enterOokRx();
    memset(scanVal, -120, sizeof(scanVal));
    memset(scanPeak, -120, sizeof(scanPeak));
    scanTop = -120;
    scanIdx = 0;
    scanRun = true;
    scanAt = 0;
}

static void leaveScan() {
    scanRun = false;
}

static void enterNfc() {
    // 读卡和模拟共用同一颗芯片的两种模式，互斥。以前进读卡屏只在轮询条件里
    // 加了 !emuRunning()，模拟一开着屏幕就永远空白，看着像"扫不出卡"。
    // 直接停掉，并说清楚为什么。
    if (nfc::emuRunning()) {
        nfc::emuStop();
        ui::toast(T(S_STOPPED_FOR_NFC), cfg::WARN);
    }
    screen = SCR_NFC;
    card = nfc::Card{};
    nfcSt = nfc::ready() ? NFC_IDLE : NFC_ERROR;
    nfcAt = 0;
}

static void enterEmu() {
    libOpen = false;   // 重新进屏回到主视图，别留着上次的列表
    screen = SCR_EMU;
    if (capture::armed()) capture::disarm();
    rf.idle();
}

static void openScreen(uint8_t idx) {
    switch (idx) {
        case 0: enterCapture(); break;
        case 1: enterScan(); break;
        case 2: enterNfc(); break;
        case 3: enterEmu(); break;
        case 4: screen = SCR_INFO; break;
        case 5: helpFrom = SCR_MENU; screen = SCR_HELP; break;
    }
}

// H：任意界面一键帮助，再按 H 或 DEL 原路返回。帮助屏只是叠一层绘制，
// 不动 RF / 模拟 / 收录状态，所以返回时不需要重新进屏、也不能走 leaveScreen 的清理。
static void toggleHelp() {
    if (screen == SCR_HELP) screen = helpFrom;
    else { helpFrom = screen; screen = SCR_HELP; }
}

static void leaveScreen() {
    if (screen == SCR_HELP) { screen = helpFrom; return; }
    // 卡库列表是 EMULATE 屏里的一层，先关层而不是退屏
    if (libOpen) {
        libOpen = false;
        return;
    }
    if (screen == SCR_CAPTURE && capture::armed()) capture::disarm();
    if (screen == SCR_SCAN) leaveScan();
    if (screen == SCR_NFC) {
        card = nfc::Card{};
        cloneArmed = false;   // 离开就读卡屏，别再挂着等待态
    }
    // 模拟是独占 RF 的，离开必须停掉，否则 NFC 读卡页会失效
    if (screen == SCR_EMU) nfc::emuStop();
    screen = SCR_MENU;
    rf.setBand(band);
    rf.idle();
}

static void setBand(CapBand b) {
    band = b;
    if (capture::armed()) capture::disarm();
    rf.setBand(b);
    if (screen == SCR_SCAN) {
        memset(scanVal, -120, sizeof(scanVal));
        memset(scanPeak, -120, sizeof(scanPeak));
        scanTop = -120;
        scanIdx = 0;
        rf.enterOokRx();
    }
    saveSettings();
}

static void toggleLearn() {
    if (capture::armed()) {
        capture::disarm();
        const auto& d = capture::decoded();
        if (d.ok) {
            capture::save();
            ui::toast(T(S_CAPTURE_DONE), cfg::OK);
            M5.Speaker.tone(2200, 30);
        } else if (capture::hasData()) {
            ui::toast(T(S_GOT_RAW), cfg::WARN);
        } else {
            ui::toast(T(S_NO_SIG), cfg::DANGER);
        }
    } else {
        capture::clear();
        capture::arm();
        M5.Speaker.tone(1200, 20);
    }
}

static void doReplay() {
    if (!txAllowed) {
        ui::toast(T(S_TX_LOCK_HINT), cfg::WARN);
        return;
    }
    if (!capture::hasData()) {
        ui::toast(T(S_NOTHING_REPLAY), cfg::WARN);
        return;
    }
    bool ok = capture::replay(true);
    ui::toast(ok ? T(S_REPLAYED) : capture::replayHint(), ok ? cfg::OK : cfg::DANGER);
}

static void pollNfc() {
    nfcSt = NFC_SCANNING;
    bool found = nfc::scan(card);
    nfcSt = found ? NFC_FOUND : NFC_IDLE;
    if (found) {
        nfcMisses = 0;
        M5.Speaker.tone(2600, 25);
        return;
    }

    // 连续扫不到就怀疑芯片自己的状态，而不是"没卡"。
    // ST25R3916 被反复切模式、SPI 偶发错位后可能卡在发不出去的中间态，
    // 表现就是"NFC 有时失灵，重启才好"。这里每 8 次落空重初始化一次，
    // 几百毫秒的代价换掉一个重启。
    if (++nfcMisses >= 8) {
        nfcMisses = 0;
        nfc::recover();
    }
}

// 把手上这张卡整张抄下来，之后想模拟就模拟它。
// 读卡和模拟共用同一组 SPI、同一颗芯片，所以必须先停模拟才能读。
// 进入收录模式：常驻等卡，没有超时，按 DEL / ENTER / 空格随时退出。
// 读卡和模拟共用同一颗芯片，所以先停模拟。
static void enterCloneMode() {
    if (nfc::emuRunning()) nfc::emuStop();
    cloneArmed = true;
    cloneScanAt = 0;
    cloneTries = 0;
    card = nfc::Card{};
    nfcSt = NFC_SCANNING;
    nfcAt = 0;
    ui::toast(T(S_PLACE_AUTO), cfg::ACCENT);
}

static void cancelCloneMode() {
    cloneArmed = false;
    nfcSt = NFC_IDLE;
    nfcAt = millis() + 400;
    ui::toast(T(S_CANCELLED), cfg::MUTED);
}

// 主循环每 180ms 调一次：一次握手把卡整片抄下来。
// 没扫到卡是最正常的中间状态，不提示、不退出，让用户可以慢慢挪位置；
// 只有「卡在但读不了」这种硬错误才说清楚原因并收工。
static void cloneStep() {
    ++cloneTries;

    nfc::Tag t{};
    if (!nfc::clonePoll(t)) {
        const char* e = nfc::lastError();
        if (e && e[0]) {
            if (strstr(e, "not clonable")) {
                cloneArmed = false;
                ui::toast(T(S_MIFARE_NO), cfg::DANGER);
            } else if (strstr(e, "not supported")) {
                cloneArmed = false;
                ui::toast(T(S_TAG_UNSUPPORTED), cfg::DANGER);
            } else if (strstr(e, "read failed")) {
                cloneArmed = false;
                ui::toast(T(S_READ_FAIL), cfg::WARN);
            }
        }
        // 找了很久还没有，重初始化一次芯片，免得用户以为要按 RESET
        if (cloneTries % 60 == 0) nfc::recover();
        return;
    }

    if (!nfc::cloneApply(t)) {
        cloneArmed = false;
        ui::toast(nfc::lastError(), cfg::DANGER);
        return;
    }

    // cloneSave 内部会 libAdd + 把这张设为当前卡 + 写 NVS。
    // 别直接 libAdd：那样 cur 还是旧值，列表里没有 * 标记，开机也不会恢复。
    const bool saved = nfc::cloneSave();
    const int cur = nfc::libCurrent();

    cloneArmed = false;
    nfcSt = NFC_FOUND;
    nfcAt = millis();

    // 收录完把卡片信息留在屏幕上，用户一眼能看到收了什么
    card = nfc::Card{};
    card.present = true;
    snprintf(card.uid, sizeof(card.uid), "%s", nfc::emuUidText());
    snprintf(card.type, sizeof(card.type), "%s", nfc::tagTypeName(t.typeCode));
    card.atqa = t.atqa;
    card.sak = t.sak;
    card.blocks = (uint16_t)(t.userLen / 4 + 4);
    card.ndefCapable = false;

    if (!saved) {
        ui::toast(strstr(nfc::lastError(), "full") ? T(S_LIB_FULL) : T(S_SAVE_FAIL), cfg::WARN);
    } else {
        char msg[48];
        snprintf(msg, sizeof(msg), T(S_SAVED_N), cur + 1, nfc::libCount());
        ui::toast(msg, cfg::OK);
    }
    M5.Speaker.tone(3200, 45);
}

static void toggleEmu() {
    if (nfc::emuRunning()) {
        nfc::emuStop();
        ui::toast(T(S_EMU_STOPPED), cfg::MUTED);
    } else if (nfc::emuStart()) {
        ui::toast(T(S_EMU_STARTED), cfg::OK);
        M5.Speaker.tone(2000, 25);
    } else {
        ui::toast(nfc::lastError(), cfg::DANGER);
    }
}

// ── 按键 ─────────────────────────────────────────────────
static void onEnter() {
    if (libOpen) {
        // 列表里 ENTER = 用这张卡
        if (nfc::libSelect(libSel)) {
            const bool was = nfc::emuRunning();
            if (was) { nfc::emuStop(); nfc::emuStart(); }   // 换内存后要让芯片重读
            libOpen = false;
            char msg[24];
            snprintf(msg, sizeof(msg), T(S_USING_CARD), libSel + 1);
            ui::toast(msg, cfg::OK);
        } else {
            ui::toast(nfc::lastError(), cfg::DANGER);
        }
        return;
    }
    if (screen == SCR_MENU) {
        openScreen(menuSel);
    } else if (screen == SCR_CAPTURE) {
        toggleLearn();
    } else if (screen == SCR_NFC) {
        // 统一语义：ENTER 就是本屏主操作（收录 / 取消收录）
        if (cloneArmed) cancelCloneMode();
        else enterCloneMode();
    } else if (screen == SCR_EMU) {
        toggleEmu();
    } else if (screen == SCR_SCAN) {
        scanRun = !scanRun;
    }
}

static void onSpace() {
    switch (screen) {
        case SCR_MENU:    openScreen(menuSel); break;
        case SCR_CAPTURE: toggleLearn(); break;
        case SCR_SCAN:    scanRun = !scanRun; break;
        case SCR_NFC:     if (cloneArmed) cancelCloneMode(); else pollNfc(); break;
        case SCR_EMU:     if (!libOpen) toggleEmu(); break;
        default: break;
    }
}

static void onChar(char c) {
    if (c == ' ') return;

    // H 全局优先：任何界面都能一键看帮助，再按一次或 DEL 原路返回
    if (c == 'h' || c == 'H') { toggleHelp(); return; }

    // G 全局：中英界面一键切换，写 NVS，重启后保持
    if (c == 'g' || c == 'G') { toggleLang(); return; }

    // 数字键：菜单里是直选，其它界面是切频段
    if (c >= '1' && c <= '6') {
        if (screen == SCR_MENU) {
            menuSel = c - '1';
            openScreen(menuSel);
        } else if (screen == SCR_INFO || screen == SCR_HELP) {
            // 无效，保持安静
        } else if (screen != SCR_EMU) {
            // EMU 页跟 sub-GHz 无关，别把频段也改了
            if (c <= '4') setBand((CapBand)(c - '1'));
        }
        return;
    }

    switch (screen) {
        case SCR_MENU:
            if (c == ';') menuSel = (menuSel + MENU_N - 1) % MENU_N;
            else if (c == '.') menuSel = (menuSel + 1) % MENU_N;
            break;

        case SCR_CAPTURE:
            if (c == 'r' || c == 'R') doReplay();
            else if (c == 'c' || c == 'C') capture::clear();
            else if (c == ';') setBand((CapBand)((band + 3) % 4));
            else if (c == '.') setBand((CapBand)((band + 1) % 4));
            break;

        case SCR_SCAN:
            if (c == 'c' || c == 'C') {
                memset(scanPeak, -120, sizeof(scanPeak));
                scanTop = -120;
            } else if (c == ';') setBand((CapBand)((band + 3) % 4));
            else if (c == '.') setBand((CapBand)((band + 1) % 4));
            break;

        case SCR_NFC:
            if (c == 'd' || c == 'D') {
                if (card.present) {
                    nfc::readNdef(card);
                    M5.Speaker.tone(3000, 25);
                }
            } else if (c == 'k' || c == 'K') {
                // 兼容老习惯；界面上已经统一成 ENTER，不再宣传这个字母键
                if (cloneArmed) cancelCloneMode();
                else enterCloneMode();
            }
            break;

        case SCR_HELP:
            break;

        case SCR_EMU:
            // 列表打开时独占按键，别让 N/U/B 这些把状态搞乱
            if (libOpen) {
                const int n = nfc::libCount();
                if (n <= 0) {
                    if (c == 'l' || c == 'L') libOpen = false;
                    break;
                }
                if (c == ';') libSel = (libSel + n - 1) % n;
                else if (c == '.') libSel = (libSel + 1) % n;
                else if (c == 'l' || c == 'L') libOpen = false;
                else if (c == 'd' || c == 'D') {
                    const bool wasCur = (libSel == nfc::libCurrent());
                    nfc::libRemove(libSel);
                    nfc::libPersist();
                    if (libSel >= nfc::libCount() && libSel > 0) libSel--;
                    if (wasCur && nfc::emuRunning()) {   // 删掉的正是正在模拟的那张
                        nfc::emuStop();
                        nfc::emuStart();
                    }
                    ui::toast(T(S_DELETED), cfg::WARN);
                }
                break;
            }

            if (c == 'l' || c == 'L') {
                // 打开卡库列表，光标停在当前这张上
                libOpen = true;
                libSel = nfc::libCurrent() > 0 ? nfc::libCurrent() : 0;
                libTop = 0;
            } else if (c == 'n' || c == 'N') {
                // 克隆模式下内容来自真卡原始页，改内置 NDEF 不会生效，直接说清楚
                if (nfc::cloneActive()) {
                    ui::toast(T(S_CLONE_MODE), cfg::WARN);
                } else {
                    nfc::emuCycleContent();
                    ui::toast(nfc::emuContentText(), cfg::ACCENT);
                }
            } else if (c == 'u' || c == 'U') {
                if (nfc::cloneActive()) {
                    ui::toast(T(S_CLONE_MODE), cfg::WARN);
                } else {
                    nfc::emuRandomUid();   // 换一张"新卡"，免得两次读是同一个号
                    ui::toast(nfc::emuUidText(), cfg::ACCENT);
                }
            } else if (c == 'b' || c == 'B') {
                const bool was = nfc::emuRunning();
                nfc::cloneDisable();   // 回内置 NDEF；存档留着，L 能取回来
                if (was) {
                    nfc::emuStop();
                    nfc::emuStart();
                }
                ui::toast(T(S_BACK_BUILTIN), cfg::ACCENT);
            }
            break;

        case SCR_INFO:
            if (c == 't' || c == 'T') {
                txAllowed = !txAllowed;
                saveSettings();
                ui::toast(txAllowed ? T(S_TX_ARM_ANT) : T(S_TX_LOCKED), txAllowed ? cfg::WARN : cfg::OK);
            } else if (c == 'c' || c == 'C') {
                capture::clear();
                ui::toast(T(S_CAP_CLEARED), cfg::MUTED);
            }
            break;
    }
}

static void handleKeys() {
    if (!M5Cardputer.Keyboard.isChange()) return;
    if (!M5Cardputer.Keyboard.isPressed()) return;

    auto st = M5Cardputer.Keyboard.keysState();

    // 诊断：记录最近一次按键事件，串口 stat 里能看到
    snprintf(g_lastKey, sizeof(g_lastKey), "d%d s%d e%d t%d w%d", (int)st.del, (int)st.space,
             (int)st.enter, (int)st.tab, (int)st.word.size());

    if (st.del) {
        if (screen == SCR_MENU) {
            // 已在顶层，忽略
        } else {
            leaveScreen();
        }
        return;
    }
    if (st.enter) { onEnter(); return; }
    if (st.space) { onSpace(); return; }

    for (auto c : st.word) onChar(c);
}

// ── 串口调试协议 ─────────────────────────────────────────
static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static uint8_t* s_rle = nullptr;

static void cmdSnap() {
    render(millis());
    size_t n = 0;
    const uint8_t* raw = ui::framebuffer(n);
    const uint16_t* px = (const uint16_t*)raw;
    const size_t total = (size_t)cfg::W * cfg::H;

    // 波形密集时游程会很多，缓冲一次性分配 100KB（本机无 PSRAM，走内部 RAM）
    const size_t cap = 100000;
    if (!s_rle) {
        s_rle = (uint8_t*)malloc(cap);
        if (!s_rle) {
            Serial.println("ERR snap nomem");
            return;
        }
    }

    // RLE：界面大片纯色，压缩后只有 2~8KB，USB CDC 也不会丢
    size_t k = 0;
    size_t i = 0;
    while (i < total && k + 3 <= cap) {
        uint16_t c = px[i];
        size_t run = 1;
        while (i + run < total && px[i + run] == c && run < 255) run++;
        s_rle[k++] = (uint8_t)run;
        s_rle[k++] = (uint8_t)(c & 0xFF);
        s_rle[k++] = (uint8_t)(c >> 8);
        i += run;
    }

    Serial.printf("SNAP %d %d RLE %u\n", cfg::W, cfg::H, (unsigned)(k / 3));

    char out[180];
    size_t o = 0;
    for (size_t j = 0; j < k; j += 3) {
        uint32_t v = (uint32_t)s_rle[j] << 16;
        if (j + 1 < k) v |= (uint32_t)s_rle[j + 1] << 8;
        if (j + 2 < k) v |= (uint32_t)s_rle[j + 2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = B64[(v >> 6) & 63];
        out[o++] = B64[v & 63];
        if (o >= 176) {
            Serial.write((const uint8_t*)out, o);
            o = 0;
        }
    }
    if (o) Serial.write((const uint8_t*)out, o);
    Serial.flush();
    Serial.println();
}

static void cmdStat() {
    const auto& d = capture::data();
    const auto& dec = capture::decoded();
    Serial.printf("STAT fw=%s lang=%s screen=%d band=%s tx=%d cc=%d ver=0x%02X marc=0x%02X rssi=%d nfc=%d nfcid=%u/%u batt=%d%% %.2fV\n",
                  cfg::FW_VERSION, i18n::isEn() ? "en" : "zh", (int)screen, CapCC1101::bandName(band), (int)txAllowed, (int)rf.present(),
                  rf.chipVersion(),
                  rf.marcState(), (int)rf.rssi(), (int)nfc::ready(), nfc::idType(), nfc::idRev(), batPct(),
                  M5.Power.getBatteryVoltage() / 1000.0f);
    Serial.printf("CAP n=%u armed=%d ok=%d text=%s kind=%s T=%u rep=%u min=%u max=%u total=%lu\n",
                  d.n, (int)capture::armed(), (int)dec.ok, dec.text, dec.kind, dec.base_us, dec.repeats,
                  capture::minUs(), capture::maxUs(), (unsigned long)capture::totalUs());
    Serial.printf("DBG up=%lus key=[%s] dis=%u raw=%lu arm=%lums\n", (unsigned long)(millis() / 1000), g_lastKey,
                  capture::disarmCount(), (unsigned long)capture::rawEdges(), (unsigned long)capture::armedMs());
    if (card.present) {
        Serial.printf("CARD uid=%s type=%s atqa=%04X sak=%02X blk=%u ndef=%s\n", card.uid, card.type,
                      card.atqa, card.sak, card.blocks, card.ndef);
    }
}

static void cmdHelp() {
    Serial.println("CMDS: help stat snap ui <menu|capture|scan|nfc|emu|info|help> band <315|433|868|915>");
    Serial.println("      learn stop clear replay tx <on|off> nfc [def|v] emu [on|off|text <s>|uid <hex>]");
    Serial.println("      clone [run|off|list|use <n>|del <n>|load|stat|test|bak|unbak|fake]");
    Serial.println("      selftest demo loop scan save load key <c> lang [zh|en]");
}

static void cliExec(char* line) {
    // 不用 strtok：它只切出第一个词，"emu text hello world" 的 arg 会只剩 "text"，
    // 后面 strncmp(arg, "text ", 5) 永远匹配不上 —— 这个命令以前其实一直没生效过。
    char* cmd = line;
    while (*cmd == ' ' || *cmd == '\t') ++cmd;
    char* arg = cmd;
    while (*arg && *arg != ' ' && *arg != '\t') ++arg;
    if (*arg) {
        *arg++ = '\0';
        while (*arg == ' ' || *arg == '\t') ++arg;
        if (!*arg) arg = nullptr;
    } else {
        arg = nullptr;
    }
    if (!*cmd) return;

    if (!strcmp(cmd, "help")) {
        cmdHelp();
    } else if (!strcmp(cmd, "stat")) {
        cmdStat();
    } else if (!strcmp(cmd, "snap")) {
        cmdSnap();
    } else if (!strcmp(cmd, "ui")) {
        if (!arg) { Serial.println("ERR arg"); return; }
        if (!strcmp(arg, "menu")) { screen = SCR_MENU; }
        else if (!strcmp(arg, "capture")) { enterCapture(); }
        else if (!strcmp(arg, "scan")) { enterScan(); }
        else if (!strcmp(arg, "nfc")) { enterNfc(); }
        else if (!strcmp(arg, "emu")) { enterEmu(); }
        else if (!strcmp(arg, "info")) { screen = SCR_INFO; }
        else if (!strcmp(arg, "help")) { helpFrom = SCR_MENU; screen = SCR_HELP; }
        Serial.printf("OK ui=%s\n", arg);
    } else if (!strcmp(cmd, "lang")) {
        // 不带参数=查询，带 zh/en=设置；和 G 键共用同一份 NVS 设置
        if (arg) {
            if (!strcmp(arg, "zh")) i18n::setEn(false);
            else if (!strcmp(arg, "en")) i18n::setEn(true);
            else { Serial.println("ERR lang"); return; }
            saveSettings();
        }
        Serial.printf("OK lang=%s\n", i18n::isEn() ? "en" : "zh");
    } else if (!strcmp(cmd, "band")) {
        if (!arg) { Serial.println("ERR arg"); return; }
        int mhz = atoi(arg);
        CapBand b = mhz == 315 ? BAND_315 : (mhz == 433 ? BAND_433 : (mhz == 868 ? BAND_868 : BAND_915));
        setBand(b);
        Serial.printf("OK band=%s\n", CapCC1101::bandName(b));
    } else if (!strcmp(cmd, "learn")) {
        if (!capture::armed()) { capture::clear(); capture::arm(); }
        Serial.println("OK learning");
    } else if (!strcmp(cmd, "stop")) {
        capture::disarm();
        cmdStat();
    } else if (!strcmp(cmd, "clear")) {
        capture::clear();
        Serial.println("OK cleared");
    } else if (!strcmp(cmd, "replay")) {
        bool ok = capture::replay(txAllowed);
        Serial.printf("%s replay %s\n", ok ? "OK" : "ERR", ok ? "" : capture::replayHint());
    } else if (!strcmp(cmd, "tx")) {
        txAllowed = arg && !strcmp(arg, "on");
        saveSettings();
        Serial.printf("OK tx=%d\n", (int)txAllowed);
    } else if (!strcmp(cmd, "emu")) {
        const char* sub = arg;
        if (!sub || !*sub || !strcmp(sub, "on")) {
            bool ok = nfc::emuStart();
            if (ok) screen = SCR_EMU;   // 切到模拟页，屏幕上看得到状态
            // 克隆模式下 content 是内置 NDEF 的残留，真实来源是克隆摘要
            Serial.printf("%s emu on uid=%s src=%s err=%s\n", ok ? "OK" : "ERR", nfc::emuUidText(),
                          nfc::cloneActive() ? nfc::cloneSummary() : nfc::emuContentText(), nfc::lastError());
        } else if (!strcmp(sub, "off")) {
            nfc::emuStop();
            Serial.println("OK emu off");
        } else if (!strcmp(sub, "next")) {
            if (nfc::cloneActive()) {
                Serial.println("ERR emu next: clone mode (content follows the real tag, use 'clone off')");
            } else {
                nfc::emuCycleContent();
                Serial.printf("OK emu content=%s\n", nfc::emuContentText());
            }
        } else if (!strncmp(sub, "text ", 5)) {
            if (nfc::cloneActive()) {
                Serial.println("ERR emu text: clone mode (use 'clone off' for builtin NDEF)");
            } else {
                bool ok = nfc::emuSetContent(sub + 5);
                Serial.printf("%s emu content=%s\n", ok ? "OK" : "ERR", nfc::emuContentText());
            }
        } else if (!strncmp(sub, "uid ", 4)) {
            if (nfc::cloneActive()) {
                Serial.println("ERR emu uid: clone mode (UID comes from the real tag, use 'clone off')");
            } else {
                bool ok = nfc::emuSetUid(sub + 4);
                Serial.printf("%s emu uid=%s\n", ok ? "OK" : "ERR", nfc::emuUidText());
            }
        } else if (!strcmp(sub, "rnd")) {
            if (nfc::cloneActive()) {
                Serial.println("ERR emu rnd: clone mode (UID comes from the real tag, use 'clone off')");
            } else {
                nfc::emuRandomUid();
                Serial.printf("OK emu uid=%s\n", nfc::emuUidText());
            }
        } else if (!strcmp(sub, "dump")) {
            nfc::emuDump();
        } else {
            // 默认当查询。hits 是"被贴几次"，frames 是底层帧数，两个一起看才好判断
            Serial.printf("EMU running=%d state=%s hits=%u frames=%lu uid=%s src=%s\n",
                          (int)nfc::emuRunning(), nfc::emuStateName(), nfc::emuHits(),
                          (unsigned long)nfc::emuFrames(), nfc::emuUidText(),
                          nfc::cloneActive() ? nfc::cloneSummary() : nfc::emuContentText());
        }
    } else if (!strcmp(cmd, "clone")) {
        // clone          读手上这张卡并保存（之后 SPACE 模拟它）
        // clone run      读完立刻开始模拟
        // clone off      回到内置 NDEF
        // clone load     从 NVS 取出上次存的
        // clone stat     当前状态
        const char* sub = arg;
        if (!sub || !*sub) {
            const bool wasEmu = nfc::emuRunning();
            if (wasEmu) nfc::emuStop();
            nfc::Tag t{};
            // cloneRead 内部已有约 3 秒读卡窗口。只补一轮，覆盖"命令发完才去拿卡"。
            bool got = nfc::cloneRead(t);
            if (!got) got = nfc::cloneRead(t);
            if (!got) {
                Serial.printf("ERR clone read: %s\n", nfc::lastError());
            } else if (!nfc::cloneApply(t)) {
                Serial.printf("ERR clone apply: %s\n", nfc::lastError());
            } else {
                const bool saved = nfc::cloneSave();
                Serial.printf("OK clone type=%s uid=%s len=%u saved=%d md5=%02X%02X%02X%02X\n",
                              nfc::tagTypeName(t.typeCode), nfc::emuUidText(), (unsigned)t.userLen, (int)saved,
                              t.user[0], t.user[1], t.user[2], t.user[3]);
            }
        } else if (!strcmp(sub, "run")) {
            nfc::Tag t{};
            if (nfc::emuRunning()) nfc::emuStop();
            if (!nfc::cloneRead(t) || !nfc::cloneApply(t)) {
                Serial.printf("ERR clone: %s\n", nfc::lastError());
            } else {
                nfc::cloneSave();
                const bool ok = nfc::emuStart();
                screen = SCR_EMU;
                Serial.printf("%s clone run type=%s uid=%s len=%u\n", ok ? "OK" : "ERR",
                              nfc::tagTypeName(t.typeCode), nfc::emuUidText(), (unsigned)t.userLen);
            }
        } else if (!strcmp(sub, "off")) {
            nfc::cloneDisable();   // 退出克隆模式，但存档留着
            if (nfc::emuRunning()) {
                nfc::emuStop();
                nfc::emuStart();   // 重开一次才会回到内置 NDEF
            }
            Serial.printf("OK clone off mode=builtin uid=%s\n", nfc::emuUidText());
        } else if (!strcmp(sub, "load")) {
            const bool ok = nfc::cloneLoad();
            Serial.printf("%s clone load %s\n", ok ? "OK" : "ERR",
                          ok ? nfc::cloneSummary() : nfc::lastError());
        } else if (!strcmp(sub, "list")) {
            const int n = nfc::libCount();
            Serial.printf("LIB n=%d cur=%d\n", n, nfc::libCurrent() + 1);
            for (int i = 0; i < n; i++) {
                const nfc::Tag* t = nfc::libGet(i);
                if (!t) continue;
                const int from = t->uidLen > 4 ? t->uidLen - 4 : 0;
                Serial.printf("  %d%s %-10s %02X%02X%02X%02X %uB\n", i + 1,
                              (i == nfc::libCurrent()) ? "*" : " ", nfc::tagTypeName(t->typeCode),
                              t->uid[from], t->uid[from + 1], t->uid[from + 2], t->uid[from + 3],
                              (unsigned)t->userLen);
            }
        } else if (!strncmp(sub, "use ", 4)) {
            const int i = atoi(sub + 4) - 1;
            if (nfc::libSelect(i)) {
                nfc::libPersist();
                if (nfc::emuRunning()) {
                    nfc::emuStop();
                    nfc::emuStart();   // 换内存后芯片要重新读一次
                }
                Serial.printf("OK clone use #%d %s\n", i + 1, nfc::cloneSummary());
            } else {
                Serial.printf("ERR clone use: %s\n", nfc::lastError());
            }
        } else if (!strncmp(sub, "del ", 4)) {
            const int i = atoi(sub + 4) - 1;
            const bool wasCur = (i == nfc::libCurrent());
            if (!nfc::libRemove(i)) {
                Serial.printf("ERR clone del: %s\n", nfc::lastError());
            } else {
                nfc::libPersist();
                if (wasCur && nfc::emuRunning()) {
                    nfc::emuStop();
                    nfc::emuStart();
                }
                Serial.printf("OK clone del #%d left=%d\n", i + 1, nfc::libCount());
            }
        } else if (!strncmp(sub, "fake", 4)) {
            // 调试用：造几张合成卡塞进库，验证列表布局和滚动。
            // 真卡不够时用，`clone del <n>` 可以删掉。
            static const uint8_t kNdef[] = {0x03, 0x10, 0xD1, 0x01, 0x0C, 0x55, 0x04,
                                            'f',  'a',  'k',  'e',  '.',  'c',  'o',
                                            'm',  '/',  0x31, 0xFE};
            int added = 0;
            for (int k = 0; k < 3; k++) {
                nfc::Tag t{};
                t.valid = true;
                t.typeCode = nfc::typeCodeNTAG213();
                t.uidLen = 7;
                t.uid[0] = 0x04;
                t.uid[1] = 0xFA;
                t.uid[2] = 0xCE;
                t.uid[3] = 0x01;
                t.uid[4] = 0x02;
                t.uid[5] = 0x03;
                t.uid[6] = (uint8_t)(k + 1);
                t.atqa = 0x0044;
                memcpy(t.user, kNdef, sizeof(kNdef));
                t.userLen = 144;
                if (nfc::libAdd(t) >= 0) added++;
            }
            nfc::libPersist();
            Serial.printf("OK clone fake added=%d n=%d\n", added, nfc::libCount());
        } else if (!strcmp(sub, "bak")) {
            const bool ok = nfc::libBackupCurrent();
            Serial.printf("%s clone bak %s\n", ok ? "OK" : "ERR", ok ? "" : nfc::lastError());
        } else if (!strcmp(sub, "unbak")) {
            const bool ok = nfc::libRestoreBackup();
            Serial.printf("%s clone unbak %s n=%d\n", ok ? "OK" : "ERR",
                          ok ? nfc::cloneSummary() : nfc::lastError(), nfc::libCount());
        } else if (!strcmp(sub, "test")) {
            const bool ok = nfc::cloneSelfTest();
            Serial.printf("%s clone test\n", ok ? "OK" : "FAIL");
        } else {
            const nfc::Tag& c = nfc::cloneCurrent();
            Serial.printf("CLONE active=%d stored=%d type=%s uid=%s len=%u sum=%s\n",
                          (int)nfc::cloneActive(), (int)nfc::cloneStored(),
                          nfc::cloneActive() ? nfc::tagTypeName(c.typeCode) : "-",
                          nfc::cloneActive() ? nfc::emuUidText() : "-",
                          nfc::cloneActive() ? (unsigned)c.userLen : 0u, nfc::cloneSummary());

            // 只回前 64 字节：整张卡 504/888 字节一次打完，
            // 主机没在读的话会把 USB CDC 缓冲灌满，固件卡死在 printf 里。
            if (nfc::cloneActive() && c.userLen) {
                const uint16_t show = c.userLen > 64 ? 64 : c.userLen;
                Serial.print("  user area:");
                for (uint16_t i = 0; i < show; i++) {
                    if ((i % 16) == 0) Serial.printf("\n  %04X:", i);
                    Serial.printf(" %02X", c.user[i]);
                }
                if (c.userLen > show) Serial.printf(" ...(+%u)", (unsigned)(c.userLen - show));
                Serial.println();
            }
        }
    } else if (!strcmp(cmd, "nfc")) {
        // 模拟会占用芯片的被动目标模式，此时 scan 必然失败 —— 自动让位并说明
        if (nfc::emuRunning()) {
            nfc::emuStop();
            Serial.println("(emulation stopped for scan)");
        }
        const bool wantV = (arg && !strcmp(arg, "v"));
        const bool found = wantV ? nfc::scanV(card) : nfc::scan(card);
        Serial.printf("%s nfc present=%d uid=%s type=%s atqa=%04X sak=%02X blk=%u ndefcap=%d\n",
                      found ? "OK" : "ERR", (int)card.present, card.uid, card.type, card.atqa, card.sak,
                      card.blocks, (int)card.ndefCapable);
        if (found && !wantV && arg && !strcmp(arg, "def")) {
            nfc::readNdef(card);
            Serial.printf("OK ndef=%s\n", card.ndef);
        }
    } else if (!strcmp(cmd, "selftest")) {
        capture::selfTest();
    } else if (!strcmp(cmd, "demo")) {
        capture::demoFill();
        cmdStat();
    } else if (!strcmp(cmd, "loop")) {
        capture::loopbackTest();
    } else if (!strcmp(cmd, "scan")) {
        // 把 32 个通道的实测值打出来，用来确认扫描是逐通道独立测量的
        Serial.printf("SCAN ch=%d top=%d run=%d state=0x%02X\n", SCAN_CH, scanTop,
                      (int)scanRun, rf.marcState());
        for (int i = 0; i < SCAN_CH; i += 8) {
            Serial.print("  ");
            for (int j = 0; j < 8 && i + j < SCAN_CH; j++)
                Serial.printf("%5d", (int)scanVal[i + j]);
            Serial.println();
        }
    } else if (!strcmp(cmd, "save")) {
        Serial.printf("%s save\n", capture::save() ? "OK" : "ERR");
    } else if (!strcmp(cmd, "load")) {
        bool ok = capture::load();
        Serial.printf("%s load n=%u\n", ok ? "OK" : "ERR", capture::data().n);
    } else if (!strcmp(cmd, "key")) {
        if (!arg) { Serial.println("ERR arg"); return; }
        // 也接受特殊键名，方便脚本走完整套 UI 流程
        if (!strcmp(arg, "enter")) onEnter();
        else if (!strcmp(arg, "space")) onSpace();
        else if (!strcmp(arg, "del")) leaveScreen();
        else onChar(arg[0]);
        Serial.printf("OK key=%s\n", arg);
    } else {
        Serial.printf("ERR unknown %s\n", cmd);
    }
}

static void cliPoll() {
    static char buf[96];
    static uint8_t len = 0;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            buf[len] = 0;
            if (len) cliExec(buf);
            len = 0;
            continue;
        }
        if (len < sizeof(buf) - 1) buf[len++] = c;
    }
}

// ── 主流程 ───────────────────────────────────────────────
void setup() {
    // M5Unified 与 M5UnitUnified 各装一次 GPIO ISR 服务，第二次会打一条
    // 无害的 ESP_LOG 错误。屏蔽 gpio 这个 tag，保证串口输出只剩我们的协议文本。
    esp_log_level_set("gpio", ESP_LOG_NONE);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    if (M5Cardputer.Display.height() > M5Cardputer.Display.width()) {
        M5Cardputer.Display.setRotation(1);
    }
    M5Cardputer.Display.setBrightness(110);
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    Serial.begin(115200);
    delay(120);

    loadSettings();   // 先读回语言，启动画面也要跟着切换
    ui::begin();
    drawSplash(T(S_BOOT_POWER));

    Serial.printf("[boot] CAP LAB v%s lang=%s\n", cfg::FW_VERSION, i18n::isEn() ? "en" : "zh");
    rf.begin();
    Serial.printf("[boot] cc1101 ver=0x%02X ok=%d\n", rf.chipVersion(), (int)rf.present());

    bool nfcOk = nfc::begin();
    Serial.printf("[boot] nfc ready=%d id=%u/%u idok=%d err=%s\n", (int)nfcOk, nfc::idType(),
                  nfc::idRev(), (int)nfc::idOk(), nfc::lastError());

    rf.setBand(band);

    capture::begin(&rf);
    bool loaded = capture::load();
    Serial.printf("[boot] capture loaded=%d n=%u\n", (int)loaded, capture::data().n);

    // 克隆卡住在 NVS 里，重启后自动回到上次的模式（`clone off` 过的不会回来）
    if (nfc::cloneRestore()) {
        Serial.printf("[boot] clone restored type=%s uid=%s len=%u\n",
                      nfc::tagTypeName(nfc::cloneCurrent().typeCode), nfc::emuUidText(),
                      (unsigned)nfc::cloneCurrent().userLen);
    }

    drawSplash(nfcOk ? T(S_BOOT_READY) : T(S_BOOT_NFC_OFF));
    delay(350);

    screen = SCR_MENU;
    Serial.println("READY");
    cmdHelp();
}

// 模拟时手机贴在背面，用户看不见屏幕，所以用声音报告进度。
// 三种事件频率拉开距离，避免和之前那声混在一起听不清：
//   读卡器靠近     1400Hz 短促
//   完成一次读取   2800Hz 稍长（优先，会盖掉上面那声）
static void emuFeedback() {
    static uint8_t  prevSt = 255;
    static uint16_t prevHits = 0;
    static uint32_t prevSess = 0;

    const uint8_t  st = (uint8_t)nfc::emuState();
    const uint16_t hits = nfc::emuHits();
    const uint32_t sess = nfc::emuSession();

    // 新一轮模拟开始：先把基线对齐，否则上一轮的 hits 会让这一轮第一次读取不响
    if (sess != prevSess) {
        prevSess = sess;
        prevSt = st;
        prevHits = hits;
        return;
    }

    if (hits > prevHits) {
        M5.Speaker.tone(1400, 35);   // 读卡器来了
    } else if (st == (uint8_t)nfc::EmuState::Waiting && prevSt != (uint8_t)nfc::EmuState::Waiting) {
        M5.Speaker.tone(2800, 60);   // 读完 / 手机拿开了
    } else if (st == (uint8_t)nfc::EmuState::Error) {
        M5.Speaker.tone(600, 120);
    }

    prevSt = st;
    prevHits = hits;
}

void loop() {
    M5Cardputer.update();
    handleKeys();
    cliPoll();

    uint32_t now = millis();

    // 采集：静音 400ms 或缓冲满就自动收工
    {
        static uint16_t lastEdges = 0;
        static uint32_t lastEdgeAt = 0;
        if (capture::armed()) {
            uint16_t e = capture::liveEdges();
            if (e != lastEdges) {
                lastEdges = e;
                lastEdgeAt = now;
            }
            bool quiet = (e >= 8) && (now - lastEdgeAt > 400);

            // 没接天线时前端空载自激，边沿密到 27 万次/秒，但一条合格脉冲都没有：
            // 这种状态静音判断永远不成立，会一直烧 CPU。识别出来主动收工并给出提示。
            bool pureNoise = capture::rawEdges() > 120000 && e < 8 && capture::armedMs() > 1200;

            if (quiet || pureNoise || capture::bufferFull()) {
                capture::disarm();
                const auto& d = capture::decoded();
                if (d.ok) {
                    capture::save();
                    ui::toast(T(S_CAPTURE_DONE), cfg::OK);
                } else if (pureNoise) {
                    ui::toast(T(S_NOISE_ONLY_ANT), cfg::DANGER);
                } else if (capture::hasData()) {
                    ui::toast(capture::minUs() < 100 ? T(S_NOISE_ONLY) : T(S_RAW_WAVE), cfg::WARN);
                } else {
                    ui::toast(T(S_NO_SIG), cfg::DANGER);
                }
            }
        } else {
            lastEdges = 0;
        }
    }

    if (screen == SCR_SCAN && scanRun && now - scanAt >= 3) {
        scanAt = now;
        sweepStep();
    }

    if (screen == SCR_NFC && !nfc::emuRunning()) {
        if (cloneArmed) {
            // 常驻收录：每 180ms 做一次「选卡 + 读全片」的单次握手。
            // 没有超时，贴卡找位置要多久都行；读不到就 silently 继续等。
            if (now - cloneScanAt > 180) {
                cloneScanAt = now;
                cloneStep();
            }
        } else {
            // 扫到卡后会停在 NFC_FOUND 不再轮询，卡拿走了屏幕还挂着旧信息。
            // 停 4 秒后清空重扫：既不会刚显示就被抹掉，又能感知换卡/拿开。
            if (nfcSt == NFC_FOUND && now - nfcAt > 4000) {
                card = nfc::Card{};
                nfcSt = NFC_IDLE;
            }
            if (nfcSt == NFC_IDLE && now - nfcAt > 900) {
                nfcAt = now;
                pollNfc();
            }
        }
    }

    // 模拟状态机必须高频轮询：读卡器发完帧后留给卡回答的窗口很短。
    // 但动画不用跟着跑，还是 30fps。串口起的模拟也要能跑，所以不限定屏幕。
    if (nfc::emuRunning()) {
        nfc::emuUpdate();
        emuFeedback();
        if (now - lastFrame >= 33) {
            lastFrame = now;
            render(now);
        }
        delay(1);
        return;
    }

    if (now - lastFrame >= 33) {
        lastFrame = now;
        render(now);
    }

    delay(2);
}
