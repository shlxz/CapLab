#include "capture.h"
#include "i18n.h"
#include <Preferences.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <driver/rmt.h>

namespace capture {

static CapCC1101* s_rf = nullptr;

// ── 中断采集缓冲（放 DRAM，ISR 里只做赋值，绝不调用函数）─────
static volatile uint16_t isr_cnt = 0;
static volatile uint16_t isr_dur[CAP_MAX_SEG];
static volatile uint8_t  isr_lvl[CAP_MAX_SEG];
static volatile uint32_t isr_last = 0;
static volatile uint8_t  isr_cur = 0;
static volatile bool     isr_full = false;
static volatile uint32_t isr_raw = 0;      // 所有边沿（含被丢弃的噪声），用于评估中断负载
static uint32_t          isr_armAt = 0;

static CaptureData s_data;
static Decoded     s_dec;
static bool        s_armed = false;
static uint16_t    s_disarmCount = 0;

static uint16_t s_min = 0, s_max = 0;
static uint32_t s_total = 0;

static char s_hint[40] = "";

static void IRAM_ATTR gdoISR() {
    uint32_t now = micros();
    uint32_t d = now - isr_last;
    isr_last = now;
    isr_raw++;

    // 噪声尖峰：没接天线时前端会自激出 ~250kHz 方波（脉宽仅几微秒），
    // 真实遥控脉冲都在 100us 以上，所以这里直接丢，只跟电平不记账。
    if (d < 90) {
        isr_cur = (uint8_t)gpio_get_level((gpio_num_t)CAP_PIN_GDO0);
        return;
    }

    uint16_t i = isr_cnt;
    if (i < CAP_MAX_SEG) {
        isr_dur[i] = (d > CAP_DUR_CLAMP) ? CAP_DUR_CLAMP : (uint16_t)d;
        isr_lvl[i] = isr_cur;
        isr_cur = (uint8_t)gpio_get_level((gpio_num_t)CAP_PIN_GDO0);
        isr_cnt = i + 1;
    } else {
        isr_full = true;
    }
}

// ── 采集后处理 ───────────────────────────────────────────

// 丢弃第 0 段（attach 到首个边沿之间的空档没有意义），并把 <24us 的毛刺并进下一段
static void harvest() {
    uint16_t n = isr_cnt;
    if (n > CAP_MAX_SEG) n = CAP_MAX_SEG;

    static uint16_t td[CAP_MAX_SEG];
    static uint8_t  tl[CAP_MAX_SEG];
    uint16_t m = 0;
    for (uint16_t i = 1; i < n && m < CAP_MAX_SEG; i++) {
        td[m] = isr_dur[i];
        tl[m] = isr_lvl[i];
        m++;
    }

    uint16_t k = 0;
    for (uint16_t i = 0; i < m; i++) {
        if (i + 1 < m && td[i] < 24) {
            uint32_t sum = (uint32_t)td[i + 1] + td[i];
            td[i + 1] = (sum > CAP_DUR_CLAMP) ? CAP_DUR_CLAMP : (uint16_t)sum;
            continue;
        }
        s_data.dur[k] = td[i];
        s_data.lvl[k] = tl[i];
        k++;
    }
    s_data.n = k;
    s_data.band = s_rf ? (uint8_t)s_rf->band() : (uint8_t)BAND_433;
}

static int cmpU16(const void* a, const void* b) {
    return (int)(*(const uint16_t*)a) - (int)(*(const uint16_t*)b);
}

// 把时长换算成符号：0=短 1=长 2=超长(帧间隔)
static inline int clsOf(uint16_t d, uint16_t T) {
    if (d < (uint32_t)T * 18 / 10) return 0;
    if (d < (uint32_t)T * 50 / 10) return 1;
    return 2;
}

void analyze() {
    s_dec = Decoded{};
    s_min = 0;
    s_max = 0;
    s_total = 0;

    const uint16_t n = s_data.n;
    if (n < 4) {
        strcpy(s_dec.kind, "empty");
        return;
    }

    for (uint16_t i = 0; i < n; i++) {
        uint16_t d = s_data.dur[i];
        s_total += d;
        if (s_min == 0 || d < s_min) s_min = d;
        if (d > s_max) s_max = d;
    }

    // 基准 T：取 60..2500us 的时长，排序后 15% 分位，再用邻域均值精修
    static uint16_t v[CAP_MAX_SEG];
    uint16_t c = 0;
    for (uint16_t i = 0; i < n; i++) {
        uint16_t d = s_data.dur[i];
        if (d >= 60 && d <= 2500) v[c++] = d;
    }
    if (c < 6) {
        strcpy(s_dec.kind, "raw");
        return;
    }
    qsort(v, c, sizeof(uint16_t), cmpU16);

    uint16_t T = v[(uint32_t)c * 15 / 100];
    if (T < 60) T = 60;
    uint32_t sum = 0;
    uint16_t cnt = 0;
    for (uint16_t i = 0; i < c; i++) {
        if (v[i] > (uint32_t)T * 85 / 100 && v[i] < (uint32_t)T * 115 / 100) {
            sum += v[i];
            cnt++;
        }
    }
    if (cnt) T = (uint16_t)(sum / cnt);
    s_dec.base_us = T;

    // 扫出最长的一段「高-低」配对序列
    uint8_t  best[64];
    uint16_t bestLen = 0;
    uint16_t i = 0;
    while (i + 1 < n) {
        if (s_data.lvl[i] != 1) { i++; continue; }
        int a = clsOf(s_data.dur[i], T);
        int b = clsOf(s_data.dur[i + 1], T);
        if (a == 2 || b == 2) { i++; continue; }

        uint8_t syms[64];
        uint16_t len = 0, j = i;
        while (j + 1 < n && len < 64) {
            int x = clsOf(s_data.dur[j], T);
            int y = clsOf(s_data.dur[j + 1], T);
            if (x == 2 || y == 2) break;
            if (x != y) {
                syms[len++] = (x == 0) ? 0 : 1;          // 短长=0 长短=1
            } else if (x == 0) {
                syms[len++] = 2;                          // 短短=浮动位
            } else {
                break;                                    // 长长=非法，帧结束
            }
            j += 2;
        }
        if (len > bestLen) {
            bestLen = len;
            memcpy(best, syms, len);
        }
        i = (j > i) ? j : i + 1;
    }

    // 全同符号说明只是噪声在重复，不是真码，宁可认「raw」也别骗人
    if (bestLen >= 12) {
        uint8_t n0 = (bestLen >= 24) ? 24 : 12;
        bool uniform = true;
        for (uint8_t k = 1; k < n0; k++) {
            if (best[k] != best[0]) {
                uniform = false;
                break;
            }
        }
        if (uniform) bestLen = 0;
    }

    if (bestLen >= 24) {
        // 24bit PWM（EV1527 / PT2262 的纯二进制帧）
        uint32_t val = 0;
        bool clean = true;
        for (uint8_t k = 0; k < 24; k++) {
            if (best[k] == 2) clean = false;
            val = (val << 1) | (best[k] == 1 ? 1u : 0u);
        }
        s_dec.value = val;
        s_dec.bits = 24;
        s_dec.ok = true;
        s_dec.tri = !clean;
        s_dec.repeats = bestLen / 24;
        if (clean) {
            snprintf(s_dec.text, sizeof(s_dec.text), "%06X", (unsigned)val);
            strcpy(s_dec.kind, "PWM24");
        } else {
            // 含浮动位就按 12 位三态显示
            for (uint8_t k = 0; k < 12; k++) {
                s_dec.text[k] = (best[k] == 0) ? '0' : (best[k] == 1 ? '1' : 'F');
            }
            s_dec.text[12] = 0;
            s_dec.bits = 12;
            strcpy(s_dec.kind, "TRI12");
        }
    } else if (bestLen >= 12) {
        for (uint8_t k = 0; k < 12; k++) {
            s_dec.text[k] = (best[k] == 0) ? '0' : (best[k] == 1 ? '1' : 'F');
        }
        s_dec.text[12] = 0;
        s_dec.bits = 12;
        s_dec.ok = true;
        s_dec.repeats = bestLen / 12;
        strcpy(s_dec.kind, "TRI12");
    } else {
        strcpy(s_dec.kind, s_max > (uint32_t)s_min * 6 ? "framed" : "raw");
    }

    // 重复次数：帧与帧之间有同步间隔，不能只看连续段，得全缓冲扫一遍
    if (s_dec.ok) {
        const uint8_t N = (s_dec.bits == 24) ? 24 : 12;
        uint8_t reps = 0;
        for (uint16_t s = 0; (uint32_t)s + (uint32_t)N * 2 <= n; s++) {
            if (s_data.lvl[s] != 1) continue;
            bool match = true;
            for (uint8_t k = 0; k < N && match; k++) {
                int x = clsOf(s_data.dur[s + k * 2], T);
                int y = clsOf(s_data.dur[s + k * 2 + 1], T);
                if (x == 2 || y == 2) {
                    match = false;
                    break;
                }
                uint8_t sym = (x != y) ? (uint8_t)(x == 0 ? 0 : 1) : (uint8_t)(x == 0 ? 2 : 3);
                if (sym != best[k]) match = false;
            }
            if (match) reps++;
        }
        s_dec.repeats = reps ? reps : 1;
    }
}

// ── 对外 ─────────────────────────────────────────────────

void begin(CapCC1101* drv) {
    s_rf = drv;
    s_data.n = 0;
    s_dec = Decoded{};
}

void arm() {
    if (!s_rf || s_armed) return;
    isr_cnt = 0;
    isr_full = false;
    isr_raw = 0;
    isr_last = micros();
    isr_armAt = isr_last;
    isr_cur = (uint8_t)digitalRead(CAP_PIN_GDO0);

    s_rf->enterOokRx();
    attachInterrupt(digitalPinToInterrupt(CAP_PIN_GDO0), gdoISR, CHANGE);
    s_armed = true;
}

void disarm() {
    if (!s_armed) return;
    s_disarmCount++;
    detachInterrupt(digitalPinToInterrupt(CAP_PIN_GDO0));
    s_armed = false;
    if (s_rf) s_rf->idle();
    harvest();
    analyze();
}

bool armed() { return s_armed; }
uint16_t disarmCount() { return s_disarmCount; }
uint16_t liveEdges() { return isr_cnt; }
uint32_t rawEdges() { return isr_raw; }
uint32_t armedMs() { return s_armed ? (micros() - isr_armAt) / 1000 : 0; }
bool bufferFull() { return isr_full; }

const CaptureData& data() { return s_data; }
const Decoded& decoded() { return s_dec; }
bool hasData() { return s_data.n >= 4; }
uint16_t minUs() { return s_min; }
uint16_t maxUs() { return s_max; }
uint32_t totalUs() { return s_total; }

void clear() {
    if (s_armed) disarm();
    s_data.n = 0;
    s_dec = Decoded{};
    s_min = s_max = 0;
    s_total = 0;
    // 清空要连 NVS 一起擦：开机自动恢复读的是 "cap" 命名空间，
    // 只清内存的话重启后上次采的（或演示数据）又会回来。
    Preferences p;
    if (p.begin("cap", false)) p.clear();
}

// ── RMT 精确重放 ─────────────────────────────────────────
// 这个内核里没有新版 rmt_tx 驱动，只能用旧版 rmt.h；硬件定时一样准。
// ESP32-S3 的旧版 RMT 驱动只有 0-3 号是发送通道
static const rmt_channel_t RMT_CH = RMT_CHANNEL_0;
static bool            s_rmtInstalled = false;
static rmt_item32_t    s_sym[CAP_MAX_SEG / 2 + 2];

static bool rmtEnsure() {
    if (s_rmtInstalled) return true;
    rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX((gpio_num_t)CAP_PIN_GDO0, RMT_CH);
    cfg.clk_div = 80;   // 80MHz / 80 = 1MHz -> 1 tick = 1us
    if (rmt_config(&cfg) != ESP_OK) return false;
    if (rmt_driver_install(RMT_CH, 0, 0) != ESP_OK) return false;
    s_rmtInstalled = true;
    return true;
}

static void rmtRelease() {
    if (s_rmtInstalled) {
        rmt_driver_uninstall(RMT_CH);
        s_rmtInstalled = false;
    }
    pinMode(CAP_PIN_GDO0, INPUT);
}

// 把采集段打包成 RMT 项（每项两个边沿）
static size_t buildSymbols(const CaptureData& src, rmt_item32_t* out, size_t cap) {
    size_t cnt = 0;
    uint16_t i = 0;
    while (i < src.n && cnt < cap) {
        uint16_t d0 = src.dur[i];
        uint8_t  l0 = src.lvl[i];
        i++;
        uint16_t d1 = 0;
        uint8_t  l1 = 0;
        if (i < src.n) {
            d1 = src.dur[i];
            l1 = src.lvl[i];
            i++;
        }
        out[cnt].level0 = l0;
        out[cnt].duration0 = d0 ? d0 : 1;
        out[cnt].level1 = l1;
        out[cnt].duration1 = d1;
        cnt++;
    }
    return cnt;
}

bool replay(bool allowTx) {
    s_hint[0] = 0;
    if (!allowTx) {
        strcpy(s_hint, T(S_TX_LOCKED_HINT));
        return false;
    }
    if (!s_rf || s_data.n < 6) {
        strcpy(s_hint, T(S_NO_DATA));
        return false;
    }
    if (s_armed) disarm();

    // 异步发送模式下 GDO0 变成芯片的输入脚，波形由主机灌进去
    s_rf->enterOokTx();
    if (!rmtEnsure()) {
        strcpy(s_hint, T(S_RMT_FAIL));
        return false;
    }

    size_t cnt = buildSymbols(s_data, s_sym, CAP_MAX_SEG / 2 + 1);

    esp_err_t e = rmt_write_items(RMT_CH, s_sym, cnt, true);
    bool ok = (e == ESP_OK);
    rmtRelease();

    if (s_rf) {
        s_rf->enterOokRx();
        s_rf->idle();
    }
    if (!ok) strcpy(s_hint, T(S_SEND_FAIL));
    return ok;
}

const char* replayHint() { return s_hint; }

// ── 掉电保存（只存一份，重启后还能重放）──────────────────
static const uint16_t SAVE_MAX = 1000;

bool save() {
    if (s_data.n < 6) return false;
    uint16_t n = s_data.n > SAVE_MAX ? SAVE_MAX : s_data.n;
    Preferences p;
    if (!p.begin("cap", false)) return false;
    p.putUShort("n", n);
    p.putUChar("band", s_data.band);
    p.putBytes("dur", s_data.dur, n * sizeof(uint16_t));
    p.putBytes("lvl", s_data.lvl, n);
    p.end();
    return true;
}

bool load() {
    Preferences p;
    if (!p.begin("cap", true)) return false;
    uint16_t n = p.getUShort("n", 0);
    if (n < 6 || n > SAVE_MAX) {
        p.end();
        return false;
    }
    uint8_t band = p.getUChar("band", BAND_433);
    size_t got = p.getBytes("dur", s_data.dur, n * sizeof(uint16_t));
    size_t got2 = p.getBytes("lvl", s_data.lvl, n);
    p.end();
    if (got != n * sizeof(uint16_t) || got2 != n) {
        s_data.n = 0;
        return false;
    }
    s_data.n = n;
    s_data.band = band;
    analyze();
    return true;
}

// ── 自检：合成一帧 EV1527（0x123456，T=300us，重复两次）────
static void buildSynthetic() {
    s_data.n = 0;
    uint16_t k = 0;
    for (int rep = 0; rep < 2; rep++) {
        s_data.dur[k] = 300;  s_data.lvl[k] = 1; k++;   // 同步头
        s_data.dur[k] = 9300; s_data.lvl[k] = 0; k++;
        uint32_t val = 0x123456;
        for (int b = 23; b >= 0; b--) {
            bool one = (val >> b) & 1;
            s_data.dur[k] = one ? 900 : 300; s_data.lvl[k] = 1; k++;
            s_data.dur[k] = one ? 300 : 900; s_data.lvl[k] = 0; k++;
        }
    }
    s_data.n = k;
    s_data.band = s_rf ? (uint8_t)s_rf->band() : (uint8_t)BAND_433;
}

void selfTest() {
    CaptureData bak = s_data;
    Decoded bakd = s_dec;

    buildSynthetic();
    analyze();
    Serial.printf("[selftest] n=%u T=%u text=%s bits=%u ok=%d rep=%u kind=%s (expect 123456/24/ok/2/PWM24)\n",
                  s_data.n, s_dec.base_us, s_dec.text, s_dec.bits, (int)s_dec.ok, s_dec.repeats, s_dec.kind);

    s_data = bak;
    s_dec = bakd;
}

void demoFill() {
    if (s_armed) disarm();
    buildSynthetic();
    analyze();
}

// 硬件自环：芯片切到异步发送模式后 GDO0 变成输入（内部高阻），
// 这时用 RMT 驱动它同时用中断采回来，等于把「采集→解码」整条链路跑通。
// 为了不空载发射，先把发射功率压到最低。
void loopbackTest() {
    if (!s_rf) return;
    if (s_armed) disarm();

    buildSynthetic();
    CaptureData expect = s_data;

    // 芯片切异步发送模式：GDO0 变成它的输入脚（高阻），主机可以自由驱动
    s_rf->enterOokTx();
    s_rf->writeReg(CC_PATABLE, 0x00);   // 最小功率，避免空载发射
    s_rf->writeReg(CC_FREND0, 0x00);

    // ── A：验证 RMT 真的在按 1us 分辨率输出
    // 引脚被 RMT 占用后输入缓冲是关的，读不到电平，所以改测「阻塞发送耗时」：
    // 这段波形总长 76.8ms，若 RMT 没真跑，耗时会明显对不上。
    uint32_t rmtUs = 0;
    if (rmtEnsure()) {
        size_t cnt = buildSymbols(expect, s_sym, CAP_MAX_SEG / 2 + 1);
        uint32_t t0 = micros();
        bool ok = (rmt_write_items(RMT_CH, s_sym, cnt, true) == ESP_OK);
        rmtUs = ok ? (micros() - t0) : 0;
        rmtRelease();
    }

    // ── B：主机自己翻转引脚，让中断采集一遍
    isr_cnt = 0;
    isr_full = false;
    isr_last = micros();
    isr_cur = 0;

    pinMode(CAP_PIN_GDO0, OUTPUT);
    digitalWrite(CAP_PIN_GDO0, expect.lvl[0] ? HIGH : LOW);
    attachInterrupt(digitalPinToInterrupt(CAP_PIN_GDO0), gdoISR, CHANGE);

    for (uint16_t i = 0; i < expect.n; i++) {
        digitalWrite(CAP_PIN_GDO0, expect.lvl[i] ? HIGH : LOW);
        delayMicroseconds(expect.dur[i]);
    }
    delayMicroseconds(200);

    detachInterrupt(digitalPinToInterrupt(CAP_PIN_GDO0));
    digitalWrite(CAP_PIN_GDO0, LOW);
    pinMode(CAP_PIN_GDO0, INPUT);

    harvest();
    analyze();

    Serial.printf("[loop] rmt=%luus(exp 76800) cap_edges=%u text=%s ok=%d T=%u kind=%s (expect 123456/ok)\n",
                  (unsigned long)rmtUs, s_data.n, s_dec.text, (int)s_dec.ok, s_dec.base_us, s_dec.kind);

    s_rf->enterOokRx();
    s_rf->idle();
}

}  // namespace capture
