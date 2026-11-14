// 全局布局与配色（240x135 横屏仪表盘风格）
#pragma once
#include <stdint.h>

namespace cfg {

// 出包前改这里：菜单页、boot 日志、INFO 页共用同一个版本号
constexpr const char* FW_VERSION = "1.5.1";

constexpr int W = 240;
constexpr int H = 135;

constexpr int STATUS_H = 16;   // 顶部状态条
constexpr int BODY_Y   = 18;   // 内容区起点
constexpr int HINT_Y   = 118;  // 底部提示条
constexpr int HINT_H   = 17;

constexpr int BODY_H = HINT_Y - BODY_Y;

// RGB888 -> RGB565
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t BG       = rgb(10, 14, 19);     // 深底
constexpr uint16_t SURFACE  = rgb(19, 26, 34);     // 卡片
constexpr uint16_t SURFACE2 = rgb(30, 41, 52);     // 选中/次级卡片
constexpr uint16_t LINE     = rgb(41, 53, 67);     // 分隔线
constexpr uint16_t TEXT     = rgb(230, 237, 243);
constexpr uint16_t MUTED    = rgb(139, 152, 165);
constexpr uint16_t DIM      = rgb(84, 96, 110);
constexpr uint16_t ACCENT   = rgb(53, 208, 186);   // 青绿主色
constexpr uint16_t BLUE     = rgb(78, 168, 255);
constexpr uint16_t WARN     = rgb(255, 180, 84);
constexpr uint16_t DANGER   = rgb(255, 92, 92);
constexpr uint16_t OK       = rgb(82, 210, 115);
constexpr uint16_t INK      = rgb(6, 10, 14);      // 反白文字

}  // namespace cfg
