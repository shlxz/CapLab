// Sub-GHz 采集 / 解码 / 重放
//
// 采集：GDO0 边沿中断 + micros()，逐段记录「电平 + 持续时间」
// 解码：把时长归一化到基准 T，按 (短,长)/(长,短) 解 PWM，再拼成 24bit 或 12 位三态
// 重放：ESP32-S3 的 RMT 外设输出波形，比 bit-bang 精确得多（抖动 <1µs）
#pragma once
#include "cc1101_cap.h"

constexpr uint16_t CAP_MAX_SEG = 1400;
constexpr uint16_t CAP_DUR_CLAMP = 30000;   // RMT 单段上限 32767 ticks，留余量

struct CaptureData {
    uint16_t n = 0;
    uint16_t dur[CAP_MAX_SEG];   // 微秒
    uint8_t  lvl[CAP_MAX_SEG];   // 该段电平 1=高 0=低
    uint8_t  band = BAND_433;
};

struct Decoded {
    bool ok = false;
    bool tri = false;            // 含三态浮动位（PT2262 类）
    uint8_t bits = 0;            // ok 时有意义的位数
    uint32_t value = 0;
    uint16_t base_us = 0;        // 基准单位时长
    uint8_t repeats = 0;         // 同一帧在缓冲里重复了几次
    char text[16] = "--";
    char kind[12] = "raw";
};

namespace capture {

void begin(CapCC1101* drv);

void arm();
void disarm();
bool armed();
uint16_t liveEdges();
uint32_t rawEdges();     // 含被噪声门限丢弃的边沿总数
uint32_t armedMs();      // 本次监听已持续毫秒
bool bufferFull();

const CaptureData& data();
const Decoded& decoded();
bool hasData();
uint16_t minUs();
uint16_t maxUs();
uint32_t totalUs();

void clear();
void analyze();
uint16_t disarmCount();   // 诊断用

bool replay(bool allowTx);
const char* replayHint();

bool save();
bool load();

void selfTest();   // 合成一帧 EV1527 波形过一遍解码器，结果打到串口
void demoFill();   // 把合成波形留在缓冲区里，用于检查有数据时的界面
void loopbackTest();  // 硬件自环：RMT 发波形 -> GDO0 -> 中断采回 -> 解码

}  // namespace capture
