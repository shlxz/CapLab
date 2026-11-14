// CAP LAB 双语言字符串表 —— 所有界面文案的唯一来源
// 加文案时只改这里；中英两列必须一一对应，编译期 static_assert 会校验长度。
#pragma once
#include <stdint.h>

#define CAPLAB_STRINGS(X) \
    X(S_MENU_CAPTURE, "采集遥控", "Capture") \
    X(S_MENU_SCAN, "频谱扫描", "Spectrum") \
    X(S_MENU_NFC, "读卡收录", "NFC Read") \
    X(S_MENU_EMU, "卡片模拟", "Emulate") \
    X(S_MENU_INFO, "系统信息", "System") \
    X(S_MENU_HELP, "操作帮助", "Help") \
    X(S_SUB_CAPTURE, "学习/重放", "Learn / Replay") \
    X(S_SUB_SCAN, "信号强度", "RSSI level") \
    X(S_SUB_NFC, "NFC 标签", "NFC tags") \
    X(S_SUB_EMU, "变成一张卡", "Act as a tag") \
    X(S_SUB_INFO, "状态诊断", "Diagnostics") \
    X(S_SUB_HELP, "按 H 查看", "Press H") \
    X(S_SB_MAIN, "主控", "Home") \
    X(S_TX_UNLOCKED, "发射已解锁", "TX armed") \
    X(S_TX_LOCKED, "发射已锁定", "TX locked") \
    X(S_HINT_MENU_L, "ENTER 进入    ; . 上下", "ENTER Open    ; . Move") \
    X(S_HINT_MENU_R, "1-6 直选", "1-6 Go") \
    X(S_SB_KEYS, "按键说明", "Keys") \
    X(S_COL_GLOBAL, "通用键", "Global") \
    X(S_COL_APP, "专用键", "Screen") \
    X(S_H_HELP, "随时帮助", "Help") \
    X(S_H_CONFIRM, "确认", "Confirm") \
    X(S_H_SPACE, "空格", "Space") \
    X(S_H_ONOFF, "开 / 停", "On / Off") \
    X(S_H_UPDOWN, "上下选", "Up/Down") \
    X(S_H_BACK, "返回", "Back") \
    X(S_H_DIRECT, "直选/频段", "Menu/Band") \
    X(S_H_LIB, "卡库", "Library") \
    X(S_H_DEL, "删除", "Delete") \
    X(S_H_BUILTIN, "内置卡", "Built-in") \
    X(S_H_CONTENT, "内容 / UID", "Text / UID") \
    X(S_H_REPLAY, "重放 / 清空", "Replay/Clr") \
    X(S_H_TX, "发射开关", "TX toggle") \
    X(S_HINT_HELP_L, "G 切换中英", "G Language") \
    X(S_HINT_HELP_R, "H / DEL 返回", "H / DEL Back") \
    X(S_NO_SIGNAL, "暂无信号", "No signal") \
    X(S_LISTENING, "正在监听", "Listening") \
    X(S_NOISE_ANT, "只有噪声·检查天线", "Noise - check antenna") \
    X(S_RAW_WAVE, "原始波形", "Raw waveform") \
    X(S_NO_CAPTURE, "还没有采集", "No capture yet") \
    X(S_PRESS_CAPTURE, "按 ENTER 开始采集", "Press ENTER to capture") \
    X(S_HINT_CAP_STOP, "ENTER 停止   R 重放  C 清空", "ENTER Stop  R Play  C Clr") \
    X(S_HINT_CAP_GO, "ENTER 采集   R 重放  C 清空", "ENTER Rec   R Play  C Clr") \
    X(S_HINT_BACK, "DEL 返回", "DEL Back") \
    X(S_PEAK, "峰值", "Peak") \
    X(S_SCANNING, "扫描中", "Scanning") \
    X(S_PAUSED, "已暂停", "Paused") \
    X(S_HINT_SCAN, "ENTER 暂停  C 清峰值  DEL 返回", "ENTER Pause  C Clr Peak  DEL Back") \
    X(S_READY, "就绪", "Ready") \
    X(S_SAVING, "收录中", "Saving") \
    X(S_READ_OK, "已读到", "Read OK") \
    X(S_ERROR, "故障", "Error") \
    X(S_NFC_OFF, "NFC 未就绪", "NFC offline") \
    X(S_WAIT_CARD, "正在等卡", "Waiting for card") \
    X(S_PLACE_TAG, "把卡贴在读卡区", "Place tag on back") \
    X(S_AUTO_SAVE, "读到会自动收录", "Auto-saves on read") \
    X(S_TRIES, "已找 %u 次", "Tries: %u") \
    X(S_HINT_WAIT, "DEL 返回    ENTER 取消", "DEL Back   ENTER Cancel") \
    X(S_NO_TIMEOUT, "不贴就一直等", "No timeout") \
    X(S_PLACE_TAG_BIG, "请贴卡", "Place tag") \
    X(S_BACK_SIDE, "放到背面读卡区", "Back of device") \
    X(S_HINT_NFC, "ENTER 收录    空格 重扫", "ENTER Save   SPACE Rescan") \
    X(S_D_READ, "D 读取内容", "D Read NDEF") \
    X(S_NO_NDEF, "无 NDEF 内容", "No NDEF") \
    X(S_HINT_NFC_FOUND, "ENTER 收录此卡    D 读内容", "ENTER Save   D Read NDEF") \
    X(S_N_CARDS, "%d 张", "%d cards") \
    X(S_NO_CARDS, "还没有卡片", "No cards yet") \
    X(S_GO_NFC, "去读卡屏，贴卡后按 ENTER 收录", "NFC Read screen, press ENTER") \
    X(S_HINT_LIB, "ENTER 使用  D 删除  ; . 上下", "ENTER Use  D Del  ; . Move") \
    X(S_EMU_ACTIVE, "正在被读取", "Being read") \
    X(S_EMU_READY, "读卡器已靠近", "Reader near") \
    X(S_EMU_ERROR, "模拟出错", "Emu error") \
    X(S_EMU_IDLE, "等待手机靠近", "Waiting for phone") \
    X(S_EMU_ON, "模拟中", "Emulating") \
    X(S_EMU_OFF, "已停止", "Stopped") \
    X(S_HITS, "已读 %u 次", "Read %u times") \
    X(S_CONTENT, "内容", "Content") \
    X(S_CLONE_BYTES, "克隆卡  %u 字节", "Clone  %u bytes") \
    X(S_HINT_EMU_LIB, "ENTER 开关  L 卡库  B 内置", "ENTER On/Off  L Lib  B Def") \
    X(S_HINT_EMU_DEF, "ENTER 开关  N 内容  U 换号", "ENTER On/Off  N Text U UID") \
    X(S_OK_VER, "正常  v%s", "OK  v%s") \
    X(S_NOT_FOUND, "未检测到", "Not found") \
    X(S_RF_STATE, "射频状态", "RF state") \
    X(S_IDLE, "未接收", "Idle") \
    X(S_BAND, "频段", "Band") \
    X(S_ARMED, "已解锁", "Armed") \
    X(S_LOCKED, "已锁定", "Locked") \
    X(S_NFC_CHIP, "NFC 芯片", "NFC chip") \
    X(S_OK, "正常", "OK") \
    X(S_NO_ID, "无 ID", "No ID") \
    X(S_BATTERY, "电池", "Battery") \
    X(S_UPTIME, "运行时间", "Uptime") \
    X(S_HINT_INFO_LOCK, "T 锁定发射    DEL 返回", "T Lock TX    DEL Back") \
    X(S_HINT_INFO_UNLOCK, "T 解锁发射    DEL 返回", "T Unlock TX    DEL Back") \
    X(S_STOPPED_FOR_NFC, "已停止模拟，切到读卡", "Emu stopped for NFC") \
    X(S_CAPTURE_DONE, "采集完成", "Capture done") \
    X(S_GOT_RAW, "采到原始波形", "Got raw waveform") \
    X(S_NO_SIG, "没收到信号", "No signal") \
    X(S_TX_LOCK_HINT, "发射已锁定 · 信息屏按 T", "TX locked - press T in Info") \
    X(S_NOTHING_REPLAY, "没有可重放的数据", "Nothing to replay") \
    X(S_REPLAYED, "已重放", "Replayed") \
    X(S_PLACE_AUTO, "把卡贴在读卡区，读到自动收录", "Place tag - auto-saves") \
    X(S_CANCELLED, "已取消收录", "Cancelled") \
    X(S_MIFARE_NO, "MIFARE Classic 无法克隆", "MIFARE Classic: no clone") \
    X(S_TAG_UNSUPPORTED, "这种卡暂不支持", "Tag not supported") \
    X(S_READ_FAIL, "读取失败，请把卡贴稳", "Read failed, hold still") \
    X(S_LIB_FULL, "卡库已满（最多 8 张）", "Library full (max 8)") \
    X(S_SAVE_FAIL, "已读到但保存失败", "Read OK, save failed") \
    X(S_SAVED_N, "已收录第 %d 张 · 共 %d 张", "Saved #%d - total %d") \
    X(S_EMU_STOPPED, "已停止模拟", "Emu stopped") \
    X(S_EMU_STARTED, "模拟已开 · 手机贴上来", "Emu on - tap phone") \
    X(S_USING_CARD, "正在用第 %d 张", "Using card #%d") \
    X(S_DELETED, "已删除", "Deleted") \
    X(S_CLONE_MODE, "克隆模式下请按 B 回内置卡", "Clone mode: press B") \
    X(S_BACK_BUILTIN, "已切回内置卡", "Back to built-in") \
    X(S_TX_ARM_ANT, "发射已解锁 · 装好天线", "TX armed - fit antenna") \
    X(S_CAP_CLEARED, "已清空采集", "Capture cleared") \
    X(S_NOISE_ONLY_ANT, "只有底噪 · 检查天线", "Noise only - check antenna") \
    X(S_NOISE_ONLY, "只有噪声", "Noise only") \
    X(S_TX_LOCKED_HINT, "TX 已锁定", "TX locked") \
    X(S_NO_DATA, "没有数据", "No data") \
    X(S_RMT_FAIL, "RMT 失败", "RMT failed") \
    X(S_SEND_FAIL, "发送失败", "Send failed") \
    X(S_LANG_TOAST, "界面已切换为中文", "Language: English") \
    X(S_BOOT_POWER, "正在启动射频...", "powering cap...") \
    X(S_BOOT_READY, "就绪", "ready") \
    X(S_BOOT_NFC_OFF, "就绪（NFC 离线）", "ready (nfc offline)")

namespace i18n {
enum Str : uint16_t {
#define CAPLAB_ROW(id, zh, en) id,
    CAPLAB_STRINGS(CAPLAB_ROW)
#undef CAPLAB_ROW
    S_COUNT
};

extern bool english;              // true = English
const char* t(Str s);            // 取当前语言的文案
inline bool isEn() { return english; }
inline void setEn(bool e) { english = e; }
inline const char* tag() { return english ? "EN" : "中"; }
}  // namespace i18n

#define T(id)  i18n::t(i18n::id)   // 传枚举名
#define TV(e)  i18n::t(e)         // 传枚举变量
