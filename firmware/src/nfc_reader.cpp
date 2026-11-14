#include "nfc_reader.h"
#include <string.h>
#include <esp_random.h>
#include <Preferences.h>
#include <M5Cardputer.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedNFC.h>
#include <M5Utility.h>
#include <wiring/m5_unit_unified_wiring.hpp>

using m5::nfc::EmulationLayerA;

static m5::unit::UnitUnified   s_units;
static m5::unit::CapCC1101NFC  s_unit{};
static m5::nfc::NFCLayerA      s_layer{s_unit};
static m5::nfc::NFCLayerV      s_layer_v{s_unit};   // ISO15693 标签备份通道

static bool s_ready = false;
static char s_err[40] = "";
static uint8_t s_idType = 0, s_idRev = 0;
static bool s_idOk = false;

namespace nfc {

bool begin() {
    if (s_ready) return true;
    s_err[0] = 0;

    // Cap 的 3.3V 由 CC1101 驱动那边的 POWER_EN 负责，这里只管 SPI
    // SPI 已被 CC1101 初始化过，addSPI 会复用它，不会重复 begin
    if (!m5::unit::wiring::addSPI(s_units, s_unit, 10000000, 1)) {
        strcpy(s_err, "addSPI failed");
        return false;
    }
    if (!s_units.begin()) {
        strcpy(s_err, "unit begin failed");
        return false;
    }

    // begin() 成功只代表没报错，再读一次 IC 型号码才算真的通上了：
    // ST25R3916/7 的 IC_TYPE 固定是 00101(5)，读到别的值说明 SPI 没通。
    s_idOk = s_unit.readICIdentity(s_idType, s_idRev);
    if (s_idOk) s_idOk = (s_idType == 0x05);

    s_ready = true;
    return true;
}

bool ready() { return s_ready; }
const char* lastError() { return s_err; }
uint8_t idType() { return s_idType; }
uint8_t idRev() { return s_idRev; }
bool idOk() { return s_idOk; }

// 芯片只有一套射频前端，模式（A/B/F/V、读卡/模拟）是全局寄存器状态。
// 任何一次模式切换都会 STOP_ALL_ACTIVITIES 并重写一串寄存器，切完要等场重新
// 起振才能发帧。这函数把"现在是 NFC-A 读卡模式"这件事落实到位，
// 读卡前调用，比"假设上一步已经切好了"可靠得多。
static bool ensurePollMode() {
    if (!s_ready) return false;

    const auto c = s_unit.config();
    // emulation=true 说明还停在被动目标模式；mode 不是 A 说明芯片被切去过 15693 之类
    if (!c.emulation && c.mode == m5::nfc::NFC::A) return true;

    s_layer.deactivate();
    s_layer_v.deactivate();

    auto nc = c;
    nc.emulation = false;
    nc.mode = m5::nfc::NFC::A;
    s_unit.config(nc);
    if (!s_unit.configureNFCMode(m5::nfc::NFC::A)) return false;

    // 写完 CMD_STOP_ALL_ACTIVITIES 之后场要重新建立，立刻 detect 会白跑
    m5::utility::delay(12);
    return true;
}

static bool activate(m5::nfc::a::PICC& picc) {
    if (!s_layer.detect(picc, 60)) return false;
    if (!s_layer.identify(picc)) {
        s_layer.deactivate();
        return false;
    }
    if (!s_layer.reactivate(picc)) {
        s_layer.deactivate();
        return false;
    }
    return true;
}

static void fill(Card& c, const m5::nfc::a::PICC& picc) {
    c.present = true;
    strncpy(c.uid, picc.uidAsString().c_str(), sizeof(c.uid) - 1);
    strncpy(c.type, picc.typeAsString().c_str(), sizeof(c.type) - 1);
    c.atqa = picc.atqa;
    c.sak = picc.sak;
    c.blocks = picc.blocks;

    // 类型字符串偶尔带很长的后缀，界面上放不下就截断
    if (strlen(c.type) > 22) c.type[22] = 0;
}

// 只扫 NFC-A。克隆等待时每 200ms 调一次，不能顺手去试 ISO15693 ——
// 那要切一次芯片模式再切回来（几次寄存器写 + 等场起振），
// 高频轮询下纯属浪费，还可能让射频状态不稳。
bool scanA(Card& c) {
    c = Card{};
    if (!s_ready) {
        strcpy(s_err, "not ready");
        return false;
    }
    ensurePollMode();

    m5::nfc::a::PICC picc{};
    if (!activate(picc)) return false;

    fill(c, picc);
    c.ndefCapable = picc.supportsNDEF();
    s_layer.deactivate();
    return true;
}

// V 卡的探测没有并进自动轮询。试过两条路都不划算：
//   1) 不切模式直接发 15693 帧 —— 芯片还按 A 的调制发，V 卡根本不应答，
//      还把射频状态弄乱，这是早期"NFC 用着用着就失灵"的一个来源；
//   2) 每次空扫都切到 V 再切回 A —— 要写两遍寄存器、等两次场起振，
//      实测每 5 秒一次仍会让某次扫描卡住 4 秒以上（串口命令直接超时）。
// 本项目实际用的是 NTAG21x / MIFARE（都是 NFC-A），自动 V 探测收益为零、
// 稳定性代价很大，所以只保留轻量的 A 扫描。真要读 15693 卡，走 nfc v。
bool scan(Card& c) { return scanA(c); }

bool scanV(Card& c) {
    c = Card{};
    if (!s_ready) {
        strcpy(s_err, "not ready");
        return false;
    }
    if (!s_unit.configureNFCMode(m5::nfc::NFC::V)) {
        strcpy(s_err, "NFC-V config failed");
        return false;
    }
    m5::nfc::v::PICC pv{};
    const bool got = s_layer_v.detect(pv, 40);
    s_layer_v.deactivate();
    s_unit.configureNFCMode(m5::nfc::NFC::A);   // 无条件切回
    m5::utility::delay(8);
    if (!got) {
        strcpy(s_err, "no NFC-V tag");
        return false;
    }
    c.present = true;
    strncpy(c.uid, pv.uidAsString().c_str(), sizeof(c.uid) - 1);
    strncpy(c.type, "ISO15693 (NFC-V)", sizeof(c.type) - 1);
    c.blocks = pv.blocks;
    return true;
}

bool readNdef(Card& c) {
    if (!s_ready) return false;
    m5::nfc::a::PICC picc{};
    if (!activate(picc)) return false;

    c.ndefTried = true;
    c.ndef[0] = 0;
    c.ndefCapable = picc.supportsNDEF();

    if (c.ndefCapable) {
        bool valid = false;
        if (s_layer.ndefIsValidFormat(valid) && valid) {
            m5::nfc::ndef::TLV msg{};
            if (s_layer.ndefRead(msg) && msg.isMessageTLV()) {
                size_t used = 0;
                for (auto&& rec : msg.records()) {
                    std::string payload = rec.payloadAsString();
                    const char* tp = rec.type();
                    int w = snprintf(c.ndef + used, sizeof(c.ndef) - used, "%s%s:%s",
                                     used ? " | " : "", tp ? tp : "?", payload.c_str());
                    if (w <= 0 || (size_t)w >= sizeof(c.ndef) - used) break;
                    used += (size_t)w;
                }
                if (!used) strcpy(c.ndef, "(empty)");
            } else {
                strcpy(c.ndef, "(read failed)");
            }
        } else {
            strcpy(c.ndef, "(not NDEF)");
        }
    } else {
        strcpy(c.ndef, "(no NDEF)");
    }

    s_layer.deactivate();
    return true;
}

// ── 卡模拟 ────────────────────────────────────────────────
// 装成一张 NTAG213（Type 2 Tag），把 NDEF 写在虚拟内存里给读卡器读。
// 芯片侧只需要被动负载调制，不需要自己发载波，所以不需要额外供电。
namespace {

// 状态机是长生命周期的，必须比一帧活得更久，所以放在文件作用域
class EmuLayer : public EmulationLayerA {
public:
    using EmulationLayerA::EmulationLayerA;

    // hits 是"被贴了几次"，由 emuUpdate() 按状态跃迁计数。
    // 别在 receive_callback 里加：手机读一次 NTAG213 要发十几帧
    //（WUPA → SELECT CL1 → SELECT CL2 → RATS → READ×N），逐帧计数会瞬间跳到十几。
    uint16_t hits = 0;
    uint32_t frames = 0;   // 收到的总帧数，只给串口调试看

    State receive_callback(const uint8_t* rx, const uint32_t rx_len) override {
        ++frames;
        return EmulationLayerA::receive_callback(rx, rx_len);
    }
};

EmuLayer s_emu{s_unit};

// 上一次观测到的底层状态，用来识别"读卡器来了 / 走了"
static EmulationLayerA::State s_lastState = EmulationLayerA::State::Off;

// 模拟用的页内存，按最大的 NTAG216 留（231 页 × 4 = 924）。
// 只是内存，不占 flash：RAM 也就是多 744 字节。
uint8_t s_mem[924]{};
uint8_t s_emuType = 0;          // m5::nfc::a::Type 的底层值
uint16_t s_memLen = 180;        // 实际有效的字节数，随类型变

// NTAG 的 UID 首字节 0x04 是 NXP 厂商码。
// 缓冲开到 10 字节：克隆时真卡可能是 4/7/10 字节 UID，长度跟着 s_uidLen 走。
uint8_t s_uid[10] = {0x04, 0xA2, 0x31, 0x5C, 0x7E, 0x19, 0x88};
uint8_t s_uidLen = 7;
char s_uidText[24] = "";

char s_content[48] = "https://m5stack.com";
bool s_running = false;
uint32_t s_session = 0;   // 每次 emuStart() 自增，主循环用它判断"新一轮模拟"

// 预设内容，N 键轮换
constexpr const char* PRESETS[] = {
    "https://m5stack.com",
    "https://bmorcelli.github.io/Launcher/",
    "CAP LAB Cardputer ADV",
};
constexpr uint8_t PRESET_N = sizeof(PRESETS) / sizeof(PRESETS[0]);
uint8_t s_presetIdx = 0;

}  // namespace

// 芯片级重初始化。读卡器"用着用着就不灵了、重启才好"这种情况，
// 十有八九是 ST25R3916 卡在了发不出去的中间态（残留 RF/收发使能、
// 振荡器没起振），寄存器里看不出异常，但帧就是发不出去。
// unit.begin() 内部会 STOP_ALL_ACTIVITIES → SET_DEFAULT → 重新起振，
// 正是需要的动作；它本身是幂等的，重跑安全。
bool recover() {
    if (!s_ready) return begin();
    if (s_running) return false;   // 正在模拟别乱动，会打断场

    s_layer.deactivate();
    s_layer_v.deactivate();

    if (!s_unit.begin()) {
        strcpy(s_err, "chip reinit failed");
        return false;
    }
    m5::utility::delay(10);
    return true;
}

namespace {

const char* urlPrefix(const char* s, uint8_t& code) {
    if (!strncmp(s, "https://", 8)) { code = 0x04; return s + 8; }
    if (!strncmp(s, "http://", 7))  { code = 0x03; return s + 7; }
    if (!strncmp(s, "mailto:", 7))  { code = 0x06; return s + 7; }
    code = 0x00;
    return s;
}

// page 3 的 Capability Container 随容量变，写错了读卡器只会读前 144 字节。
// 数值取 NXP 数据手册的出厂 CC。
static uint8_t ccByteFor(uint8_t typeCode) {
    switch ((m5::nfc::a::Type)typeCode) {
        case m5::nfc::a::Type::NTAG_215: return 0x3E;   // 504 B
        case m5::nfc::a::Type::NTAG_216: return 0x6D;   // 888 B
        case m5::nfc::a::Type::NTAG_212: return 0x1A;   // 208 B
        case m5::nfc::a::Type::NTAG_210:
        case m5::nfc::a::Type::NTAG_210u:
        case m5::nfc::a::Type::MIFARE_Ultralight: return 0x06;   // 48 B
        default: return 0x12;                            // 213 / 203，144 B
    }
}

static void writeHeader() {
    // page 0-1：UID + BCC。4 字节 UID 与 7 字节 UID 的页布局不同，
    // 混着写会让读卡器算校验失败、直接不认这张卡。
    const bool longUid = (s_uidLen == 7 || s_uidLen == 10);

    memcpy(s_mem, s_uid, 3);   // UID0-2 两种情况都在 page 0 的前三字节

    if (longUid) {
        // 7 字节：CT=0x88 标记级联，BCC0 要把它异或进去
        s_mem[3] = (uint8_t)(0x88 ^ s_uid[0] ^ s_uid[1] ^ s_uid[2]);
        memcpy(s_mem + 4, s_uid + 3, 4);                    // page 1 = UID3..6
        s_mem[8] = (uint8_t)(s_uid[3] ^ s_uid[4] ^ s_uid[5] ^ s_uid[6]);   // BCC1
        s_mem[9] = 0x48;                                    // Internal（NXP 出厂值）
    } else {
        // 4 字节：无级联标记，BCC0 只异或 3 字节；page 1 只有 1 字节 UID
        s_mem[3] = (uint8_t)(s_uid[0] ^ s_uid[1] ^ s_uid[2]);
        s_mem[4] = s_uid[3];
        s_mem[5] = s_uid[3];        // BCC1，4 字节 UID 时就是 UID3 本身
        s_mem[6] = 0x48;            // Internal
        // s_mem[7] 是 Lock0
    }
    // Lock 字节保持全 0：真卡可能已经锁死，但我们不需要锁，
    // 未锁状态下读卡器才肯写，也才读得全。

    // page 3：Capability Container
    s_mem[12] = 0xE1;
    s_mem[13] = 0x10;
    s_mem[14] = ccByteFor(s_emuType);
    s_mem[15] = 0x00;
}

void buildMemory() {
    memset(s_mem, 0, sizeof(s_mem));

    const auto t = (m5::nfc::a::Type)s_emuType;
    const uint16_t total = (uint16_t)(m5::nfc::a::get_number_of_blocks(t) * 4);
    s_memLen = total ? total : 180;

    writeHeader();

    // 克隆模式：用户区（page 4 起）原样搬真卡数据，不再生成 NDEF
    if (cloneActive()) {
        const Tag& c = cloneCurrent();
        const uint16_t room = (s_memLen > 16) ? (uint16_t)(s_memLen - 16) : 0;
        uint16_t n = c.userLen < room ? c.userLen : room;
        if (n) memcpy(s_mem + 16, c.user, n);
        return;
    }
    // 下面继续写内置 NDEF

    // page 4 起：NDEF TLV
    uint8_t* tlv = s_mem + 16;
    // 用实际容量而不是缓冲大小：缓冲留到 NTAG216，但内置模式仍是 213
    const uint16_t room = (s_memLen > 17) ? (uint16_t)(s_memLen - 16 - 1) : 0;   // 末尾留一个 Terminator

    uint8_t prefix = 0;
    // 有协议前缀就按 URI 记录写，否则当纯文本
    const bool isUrl = (strstr(s_content, "://") || !strncmp(s_content, "mailto:", 7));
    const char* body = isUrl ? urlPrefix(s_content, prefix) : s_content;

    // 记录头固定 4 字节（D1 01 len type），URL 还要 1 字节前缀 / 文本要 1 字节状态
    const uint16_t overhead = isUrl ? 5 : 5;
    uint16_t bodyLen = (uint16_t)strnlen(body, sizeof(s_content));
    if (bodyLen + overhead + 2 + 1 > room) bodyLen = room - overhead - 2 - 1;

    const uint16_t payloadLen = bodyLen + 1;
    tlv[0] = 0x03;                                // NDEF Message TLV
    tlv[1] = (uint8_t)(payloadLen + 4);           // D1,01,len,type 共 4 字节
    tlv[2] = 0xD1;                                // MB|ME|SR|TNF=well-known
    tlv[3] = 0x01;
    tlv[4] = (uint8_t)payloadLen;
    if (isUrl) {
        tlv[5] = 'U';
        tlv[6] = prefix;
        memcpy(tlv + 7, body, bodyLen);
        tlv[7 + bodyLen] = 0xFE;
    } else {
        tlv[5] = 'T';
        tlv[6] = 0x02;                            // UTF-8、语言码长度 0
        memcpy(tlv + 7, body, bodyLen);
        tlv[7 + bodyLen] = 0xFE;
    }
}

EmuState mapState() {
    switch (s_emu.state()) {
        case EmulationLayerA::State::Ready:
        case EmulationLayerA::State::Halt:
            return EmuState::Ready;
        case EmulationLayerA::State::Active:
            return EmuState::Active;
        case EmulationLayerA::State::None:
            return EmuState::Error;
        default:
            // Off/Idle 都是"没被任何读卡器抓走"，对用户就是等着
            return EmuState::Waiting;
    }
}

void refreshUidText() {
    char* p = s_uidText;
    size_t left = sizeof(s_uidText);
    const uint8_t n = (s_uidLen == 4 || s_uidLen == 7 || s_uidLen == 10) ? s_uidLen : 7;
    for (uint8_t i = 0; i < n && left > 3; ++i) {
        const int w = snprintf(p, left, i ? ":%02X" : "%02X", s_uid[i]);
        if (w <= 0 || (size_t)w >= left) break;
        p += w;
        left -= (size_t)w;
    }
}

}  // namespace

bool emuStart() {
    if (!s_ready) {
        strcpy(s_err, "not ready");
        return false;
    }
    if (s_running) return true;

    // 读卡层可能还握着芯片，先放掉
    s_layer.deactivate();
    s_layer_v.deactivate();

    // configureEmulationMode() 会检查 cfg.emulation，所以必须先改配置再切
    auto c = s_unit.config();
    c.emulation = true;
    c.mode = m5::nfc::NFC::A;
    s_unit.config(c);

    if (!s_unit.configureEmulationMode(m5::nfc::NFC::A)) {
        strcpy(s_err, "emulation config failed");
        return false;
    }

    // 克隆模式下 UID 和类型都跟着真卡走，不再是写死的 NTAG213
    if (cloneActive()) {
        const Tag& c = cloneCurrent();
        memcpy(s_uid, c.uid, sizeof(s_uid));
        s_uidLen = c.uidLen;
        s_emuType = c.typeCode;
    } else {
        s_emuType = (uint8_t)m5::nfc::a::Type::NTAG_213;
    }

    buildMemory();
    refreshUidText();

    m5::nfc::a::PICC picc{};
    if (!picc.emulate((m5::nfc::a::Type)s_emuType, s_uid, s_uidLen)) {
        strcpy(s_err, "picc setting failed");
        return false;
    }
    if (!s_emu.begin(picc, s_mem, s_memLen)) {
        strcpy(s_err, "emulation begin failed");
        return false;
    }

    // 库默认 10 秒才算"读卡器走了"。缩到 2 秒：手一拿开就回到 Off，
    // 下次贴上来才算新的一次，界面上的 reads 才符合直觉。
    // 正常读取期间每帧都会刷新 _activity_at，不会被这个超时打断。
    s_emu.setExpiredTime(2000);

    s_emu.hits = 0;
    s_emu.frames = 0;
    s_lastState = EmulationLayerA::State::Off;
    s_running = true;
    ++s_session;
    s_err[0] = 0;
    return true;
}

void emuStop() {
    if (!s_running) return;
    s_emu.end();

    // 交还给读卡模式，不然 NFC 扫描页会失效。三件事都要做：
    // 1) 让两个 layer 松手，别让它们以为还握着已激活的卡
    // 2) 写配置 + 重配寄存器（STOP_ALL_ACTIVITIES 会掐断射频）
    // 3) 等场重新起振，否则紧接着的 detect 一定失败
    s_layer.deactivate();
    s_layer_v.deactivate();

    auto c = s_unit.config();
    c.emulation = false;
    c.mode = m5::nfc::NFC::A;
    s_unit.config(c);
    s_unit.configureNFCMode(m5::nfc::NFC::A);
    m5::utility::delay(12);

    s_running = false;
}

void emuUpdate() {
    s_units.update();
    if (!s_running) return;

    s_emu.update();

    // 一次"被贴"= 从"没有场"进到任何活动状态。
    // 手机在同一场里会反复轮询（每秒好几次），但它一直没回到 Off，
    // 所以不会重复计数 —— 这才是用户看到的"读了几次"。
    const auto st = s_emu.state();
    const bool wasOff = (s_lastState == EmulationLayerA::State::Off ||
                         s_lastState == EmulationLayerA::State::None);
    const bool isOff = (st == EmulationLayerA::State::Off || st == EmulationLayerA::State::None);
    if (wasOff && !isOff) ++s_emu.hits;
    s_lastState = st;
}

bool emuRunning() { return s_running; }

EmuState emuState() { return s_running ? mapState() : EmuState::Off; }

const char* emuStateName() {
    switch (emuState()) {
        case EmuState::Waiting: return "waiting for reader";
        case EmuState::Ready:   return "reader found";
        case EmuState::Active:  return "reading";
        case EmuState::Error:   return "chip error";
        default:                return "stopped";
    }
}

uint16_t emuHits() { return s_emu.hits; }
uint32_t emuFrames() { return s_emu.frames; }
uint32_t emuSession() { return s_session; }
const char* emuUidText() {
    // 还没启动过模拟时 s_uidText 是空的，界面上会显示一行空白，这里补上
    if (!s_uidText[0]) refreshUidText();
    return s_uidText;
}
const char* emuContentText() { return s_content; }

bool emuSetContent(const char* text) {
    if (!text || !*text) return false;
    strncpy(s_content, text, sizeof(s_content) - 1);
    s_content[sizeof(s_content) - 1] = 0;
    if (s_running) buildMemory();   // 模拟中改内容立即生效
    return true;
}

void emuCycleContent() {
    s_presetIdx = (uint8_t)((s_presetIdx + 1) % PRESET_N);
    emuSetContent(PRESETS[s_presetIdx]);
}

bool emuSetUid(const char* hex) {
    if (!hex) return false;
    uint8_t out[10]{};
    int n = 0;
    for (const char* p = hex; *p && n < 10; ++p) {
        if (*p == ':' || *p == '-' || *p == ' ') continue;
        int hi = -1, lo = -1;
        auto val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        hi = val(*p);
        if (hi < 0) return false;
        if (!p[1]) return false;
        lo = val(p[1]);
        if (lo < 0) return false;
        out[n++] = (uint8_t)((hi << 4) | lo);
        ++p;
    }
    if (n != 4 && n != 7 && n != 10) return false;
    memset(s_uid, 0, sizeof(s_uid));
    memcpy(s_uid, out, (size_t)n);
    s_uidLen = (uint8_t)n;
    refreshUidText();
    if (s_running) buildMemory();
    return true;
}

void emuRandomUid() {
    // 换 UID 就回到内置的 7 字节形态，别继承上一张克隆卡的 4/10 字节长度
    memset(s_uid, 0, sizeof(s_uid));
    s_uidLen = 7;
    uint32_t r = esp_random();
    s_uid[0] = 0x04;
    for (int i = 1; i < 7; ++i) {
        s_uid[i] = (uint8_t)(r >> ((i - 1) * 4));
    }
    refreshUidText();
    if (s_running) buildMemory();
}

// 回读配置寄存器，证明芯片真的被切成了"被动目标"而不是还在当读卡器。
// 注意 PT_MEM 里的 UID/ATQA 只能在被读卡器选到 Ready 状态后才读得出来，
// 单机没有第二个读卡器时这条路走不通，所以查 Mode Definition。
//
// 顺带记一个读库源码才看懂的坑：configure_emulation_a() 写 PT_DEF=0x5C 时注释
// 写的是 "enable d_106_ac"，而 0x5C 的 bit0 恰恰是 0；goto_off() 更是明确 clear
// 这一位。这不是笔误而是设计：Off/Idle 下由固件软件应答 WUPA，只有进到 Active
// 才置位交给硬件自动应答。所以"bit0=0"是正常态，别当故障去改。
void emuDump() {
    uint8_t ptd = 0, md = 0, ptm = 0;
    const bool okA = s_unit.readNFCIP1PassiveTargetDefinition(ptd);
    const bool okB = s_unit.readModeDefinition(md);
    const bool okC = s_unit.readPassiveTargetModulation(ptm);

    Serial.printf("EMUDUMP running=%d state=%s md=0x%02X ptd=0x%02X ptm=0x%02X hits=%u frames=%lu\n",
                  (int)s_running, emuStateName(), okB ? md : 0, okA ? ptd : 0, okC ? ptm : 0,
                  s_emu.hits, (unsigned long)s_emu.frames);

    // Mode Definition 的 bit7 是 target 位：置位说明在当卡，不是当读卡器
    const bool isTarget = okB && (md & 0x80);
    Serial.printf("  target=%s  d_106_ac_a=%u (%s)\n", isTarget ? "yes (PICC)" : "NO - still reader mode",
                  (unsigned)(ptd & 1),
                  (ptd & 1) ? "hardware auto-answer" : "software answer, normal while waiting");
}

// ── 标签克隆 ─────────────────────────────────────────────
// Type 2 Tag 的存储是扁平页：page 0-3 是 UID/BCC/Lock/CC，page 4 起是用户区。
// 用户区能原样读回来，page 0-3 里的 UID 我们有、BCC 可以算、CC 按类型给标准值，
// 所以拼出来的内存和真卡等价 —— 这不是"仿造内容"，是真的克隆。
//
// 硬边界：MIFARE Classic 的扇区数据要 Crypto1 认证，库没实现，读不出来；
// 而且模拟层 begin() 只接受 NTAG2 与 Ultralight，Classic 连装都装不了。
static Tag  s_clone{};
static bool s_cloneOn = false;
static char s_cloneSum[44] = "";

// ── 卡库 ─────────────────────────────────────────────────
// 一开始只存一张"上次克隆的卡"，用过就知道不够：换一张就得把旧的顶掉。
// 这里做成定长数组 + NVS 逐槽存储，随时能切回去。
static Tag s_lib[kLibMax];
static int s_libCount = 0;
static int s_curIdx = -1;   // 当前模拟的是库里第几张，-1 = 内置 NDEF

static void libSlotKey(char* buf, size_t n, int i) { snprintf(buf, n, "c%d", i); }

int libCount() { return s_libCount; }

const Tag* libGet(int i) {
    if (i < 0 || i >= s_libCount) return nullptr;
    return &s_lib[i];
}

int libCurrent() { return s_curIdx; }

int libIndexOf(const Tag& t) {
    for (int i = 0; i < s_libCount; i++) {
        if (s_lib[i].uidLen == t.uidLen && memcmp(s_lib[i].uid, t.uid, t.uidLen) == 0) return i;
    }
    return -1;
}

int libAdd(const Tag& t) {
    if (!t.valid) return -1;
    const int at = libIndexOf(t);
    if (at >= 0) {          // 同一张卡再读一次：刷新内容，不占新槽
        s_lib[at] = t;
        return at;
    }
    if (s_libCount >= kLibMax) return -1;
    s_lib[s_libCount] = t;
    return s_libCount++;
}

bool libSelect(int i) {
    if (i < 0 || i >= s_libCount) {
        strcpy(s_err, "no such entry");
        return false;
    }
    if (!cloneApply(s_lib[i])) return false;
    s_curIdx = i;
    return true;
}

bool libRemove(int i) {
    if (i < 0 || i >= s_libCount) {
        strcpy(s_err, "no such entry");
        return false;
    }
    for (int k = i; k < s_libCount - 1; k++) s_lib[k] = s_lib[k + 1];
    s_libCount--;
    s_lib[s_libCount] = Tag{};

    // 删的可能就是当前这张，或者排在它前面的
    if (s_curIdx == i) {
        cloneClear();
        s_curIdx = -1;
    } else if (s_curIdx > i) {
        s_curIdx--;
    }
    return true;
}

bool libPersist() {
    Preferences p;
    if (!p.begin("caplab", false)) {
        strcpy(s_err, "nvs open failed");
        return false;
    }
    p.putUChar("cnt", (uint8_t)s_libCount);
    p.putChar("cur", (int8_t)s_curIdx);
    for (int i = 0; i < kLibMax; i++) {
        char k[6];
        libSlotKey(k, sizeof(k), i);
        if (i < s_libCount) {
            p.putBytes(k, &s_lib[i], sizeof(Tag));
        } else {
            p.remove(k);
        }
    }
    // 1.2.x 的单卡键必须清掉。它跟卡库是两套存储，只要还留着，
    // libLoad() 在库为空时就会把它当"没迁移过"再读回来 —— 这就是
    // 用户删光卡、重启又冒出来的原因。
    p.remove("tag");
    p.end();
    return true;
}

bool libLoad() {
    s_libCount = 0;
    s_curIdx = -1;
    for (int i = 0; i < kLibMax; i++) s_lib[i] = Tag{};

    Preferences p;
    // 读写模式：迁移完要顺手把这个老键删掉
    if (!p.begin("caplab", false)) return false;

    // "cnt" 存在 = 卡库体系已经生效过（哪怕是被删空的 cnt=0）。
    // 只有从没写过 cnt 的老固件才需要从单卡键迁移 —— 否则一旦删光卡，
    // 重启就会把老 tag 又读回来，用户看到的就是"删掉的卡自己回来了"。
    const bool libInitialized = p.isKey("cnt");

    const uint8_t cnt = p.getUChar("cnt", 0);
    for (int i = 0; i < kLibMax && s_libCount < kLibMax; i++) {
        if (i >= cnt) break;
        char k[6];
        libSlotKey(k, sizeof(k), i);
        if (p.getBytesLength(k) == sizeof(Tag)) {
            Tag t{};
            if (p.getBytes(k, &t, sizeof(Tag)) == sizeof(Tag) && t.valid) s_lib[s_libCount++] = t;
        }
    }

    // 兼容 1.2.1 及以前的单卡存档：只迁一次，迁完就删，避免反复复活
    if (!libInitialized && p.getBytesLength("tag") == sizeof(Tag)) {
        Tag t{};
        if (p.getBytes("tag", &t, sizeof(Tag)) == sizeof(Tag) && t.valid) {
            s_lib[s_libCount++] = t;
            p.putUChar("cnt", (uint8_t)s_libCount);
        }
        p.remove("tag");
    }

    const int8_t cur = p.getChar("cur", s_libCount > 0 ? 0 : -1);
    p.end();

    if (cur >= 0 && cur < s_libCount) s_curIdx = cur;
    else s_curIdx = -1;
    return s_libCount > 0;
}

bool libHasBackup() {
    Preferences p;
    if (!p.begin("caplab_bak", true)) return false;
    const bool has = (p.getBytesLength("one") == sizeof(Tag));
    p.end();
    return has;
}

bool libBackupCurrent() {
    if (s_curIdx < 0 || s_curIdx >= s_libCount) {
        strcpy(s_err, "no card selected");
        return false;
    }
    Preferences p;
    if (!p.begin("caplab_bak", false)) {
        strcpy(s_err, "nvs open failed");
        return false;
    }
    const size_t n = p.putBytes("one", &s_lib[s_curIdx], sizeof(Tag));
    p.end();
    if (n != sizeof(Tag)) {
        strcpy(s_err, "backup write failed");
        return false;
    }
    return true;
}

bool libRestoreBackup() {
    Preferences p;
    if (!p.begin("caplab_bak", true)) {
        strcpy(s_err, "nvs open failed");
        return false;
    }
    Tag t{};
    const bool ok = (p.getBytesLength("one") == sizeof(Tag)) &&
                    (p.getBytes("one", &t, sizeof(Tag)) == sizeof(Tag)) && t.valid;
    p.end();
    if (!ok) {
        strcpy(s_err, "no backup");
        return false;
    }
    const int at = libAdd(t);
    if (at < 0) {
        strcpy(s_err, "library full");
        return false;
    }
    s_curIdx = at;
    if (!libPersist()) {
        strcpy(s_err, "nvs write failed");
        return false;
    }
    return cloneApply(t);
}

const char* tagTypeName(uint8_t typeCode) {
    static char buf[26] = "?";
    m5::nfc::a::PICC p{};
    p.type = (m5::nfc::a::Type)typeCode;
    p.size = 7;   // 库里按 size 非 0 才查名字表，不设会一律返回 Unknown
    const std::string s = p.typeAsString();
    snprintf(buf, sizeof(buf), "%s", s.empty() ? "unknown" : s.c_str());
    return buf;
}

uint8_t typeCodeNTAG213() { return (uint8_t)m5::nfc::a::Type::NTAG_213; }

bool supported(const Tag& t) {
    const auto ty = (m5::nfc::a::Type)t.typeCode;
    return m5::nfc::a::is_ntag2(ty) || ty == m5::nfc::a::Type::MIFARE_Ultralight ||
           ty == m5::nfc::a::Type::MIFARE_Ultralight_EV1_1 || ty == m5::nfc::a::Type::MIFARE_Ultralight_EV1_2;
}

// 已经 activate 成功，把这张卡整片抄进 Tag。成功失败都会 deactivate。
static bool copyActiveTag(Tag& t, const m5::nfc::a::PICC& picc) {
    const auto ty = picc.type;
    t.typeCode = (uint8_t)ty;
    t.uidLen = picc.size;
    memcpy(t.uid, picc.uid, picc.size > sizeof(t.uid) ? sizeof(t.uid) : picc.size);
    t.atqa = picc.atqa;
    t.sak = picc.sak;

    if (!supported(t)) {
        s_layer.deactivate();
        // 单独给 Classic 一句人话，不然用户以为固件坏了
        strcpy(s_err, m5::nfc::a::is_mifare(ty) ? "MIFARE Classic not clonable" : "type not supported");
        return false;
    }

    uint16_t area = picc.userAreaSize();
    if (area > sizeof(t.user)) area = sizeof(t.user);
    uint16_t len = area;
    const uint8_t first = (uint8_t)m5::nfc::a::get_first_user_block(ty);
    const bool ok = s_layer.read(t.user, len, first);
    s_layer.deactivate();

    if (!ok || !len) {
        strcpy(s_err, "read failed");
        return false;
    }

    t.userLen = len;
    t.valid = true;

    bool nd = false;
    if (len > 2) s_layer.ndefIsValidFormat(nd);   // 复用接口，失败不影响克隆
    (void)nd;

    snprintf(s_cloneSum, sizeof(s_cloneSum), "%s %uB", tagTypeName(t.typeCode), (unsigned)t.userLen);
    return true;
}

// 单次尝试：一次握手完成「选卡 + 读全片」。失败立即返回，不重试不延时。
//
// 以前 UI 的收录流程是 scanA() 先 activate 一次再 deactivate，然后 cloneRead()
// 又重新 activate 最多 6 次 —— 等于要求同一张卡连续握手成功两次。场重新起振、
// 手稍微一抖、卡片位置略偏，第二次就全落空，于是「明明贴着卡却报 no card」。
// 现在把检测和读取合成一步，UI 每 180ms 调一次，扫到就能收。
bool clonePoll(Tag& t) {
    t = Tag{};
    s_cloneSum[0] = 0;
    s_err[0] = 0;
    if (!s_ready) { strcpy(s_err, "not ready"); return false; }
    if (s_running) { strcpy(s_err, "stop emulation first"); return false; }

    ensurePollMode();
    m5::nfc::a::PICC picc{};
    if (!activate(picc)) {
        s_layer.deactivate();
        return false;          // 没卡是常态，不算错误，UI 会继续等
    }
    return copyActiveTag(t, picc);
}

// 串口 clone 命令用：一次不成再给几轮机会。UI 不走这里。
bool cloneRead(Tag& t) {
    t = Tag{};
    s_cloneSum[0] = 0;
    if (!s_ready) { strcpy(s_err, "not ready"); return false; }
    if (s_running) { strcpy(s_err, "stop emulation first"); return false; }

    for (int i = 0; i < 6; i++) {
        if (clonePoll(t)) return true;
        // 卡在但读不了（Classic / 不支持 / 读失败）是硬错误，重试也没用
        if (strstr(s_err, "not supported") || strstr(s_err, "not clonable") ||
            strstr(s_err, "read failed")) return false;
        if (i && i % 3 == 0) ensurePollMode();
        m5::utility::delay(60);
    }
    strcpy(s_err, "no card");
    return false;
}

bool cloneApply(const Tag& t) {
    if (!t.valid) return false;
    if (!supported(t)) {
        strcpy(s_err, "type not supported");
        return false;
    }
    s_clone = t;
    s_cloneOn = true;

    // 立刻同步到当前 UID / 类型，界面下一帧就能反映
    memset(s_uid, 0, sizeof(s_uid));
    memcpy(s_uid, t.uid, t.uidLen > sizeof(s_uid) ? sizeof(s_uid) : t.uidLen);
    s_uidLen = t.uidLen;
    s_emuType = t.typeCode;
    refreshUidText();
    snprintf(s_cloneSum, sizeof(s_cloneSum), "%s %uB", tagTypeName(t.typeCode), (unsigned)t.userLen);
    return true;
}

void cloneClear() {
    s_cloneOn = false;
    s_cloneSum[0] = 0;
}

bool cloneActive() { return s_cloneOn && s_clone.valid; }
const Tag& cloneCurrent() { return s_clone; }
const char* cloneSummary() { return s_cloneSum; }

bool cloneSave() {
    if (!s_clone.valid) {
        strcpy(s_err, "nothing to save");
        return false;
    }
    const int at = libAdd(s_clone);
    if (at < 0) {
        strcpy(s_err, "library full");
        return false;
    }
    s_curIdx = at;
    if (!libPersist()) {
        strcpy(s_err, "nvs write failed");
        return false;
    }
    return true;
}

bool cloneLoad() {
    if (s_libCount == 0) {
        strcpy(s_err, "no saved tag");
        return false;
    }
    if (s_curIdx < 0 || s_curIdx >= s_libCount) s_curIdx = 0;
    if (!cloneApply(s_lib[s_curIdx])) return false;
    libPersist();   // 记住这次的选择，下次开机还回到这张
    return true;
}

bool cloneStored() { return s_libCount > 0; }

// 关掉克隆模式但不删存档：下次 `clone load` 还能拿回来。
void cloneDisable() {
    cloneClear();
    s_curIdx = -1;

    // UID 和类型也得一起复位。否则退回内置 NDEF 后，UID 还挂着真卡那个，
    // 会造出一张"真卡 UID + 内置内容"的怪卡，两种模式都不对。
    static const uint8_t kDefaultUid[7] = {0x04, 0xA2, 0x31, 0x5C, 0x7E, 0x19, 0x88};
    memset(s_uid, 0, sizeof(s_uid));
    memcpy(s_uid, kDefaultUid, sizeof(kDefaultUid));
    s_uidLen = 7;
    s_emuType = (uint8_t)m5::nfc::a::Type::NTAG_213;
    refreshUidText();
    buildMemory();

    libPersist();   // cur = -1，开机不会自作聪明恢复
}

// 开机恢复：拔插 USB / 按 RESET 都会重启，克隆卡不该因此丢掉。
// libLoad 会把整个库读进来，cur >= 0 才重新套用上次那张。
bool cloneRestore() {
    libLoad();
    if (s_curIdx < 0 || s_curIdx >= s_libCount) return false;
    return cloneApply(s_lib[s_curIdx]);
}

// 没有第二张真卡时，唯一能把"克隆通路"验到底的办法：
// 先用内置 NDEF 生成一份已知正确的用户区，把它当成"刚从真卡读回来的数据"，
// 再走 apply → buildMemory，逐字节比对。这样读卡那一段之外的链路全被覆盖。
bool cloneSelfTest() {
    if (s_running) emuStop();

    // 自检会往 NVS 写合成卡，结束后必须原样还原，
    // 否则用户辛苦克隆的真卡会被一张 04:DE:AD:... 的假卡顶掉。
    // 现在存的是整个卡库，备份就是内存快照（8×912 B 太大，别放栈上）。
    static Tag libBackup[kLibMax];
    const int cntBackup = s_libCount;
    const int curBackup = s_curIdx;
    for (int i = 0; i < kLibMax; i++) libBackup[i] = s_lib[i];

    const bool prevOn = s_cloneOn;
    const Tag prevClone = s_clone;
    const bool hadBackup = (cntBackup > 0);

    // 自检还会动 UID / 类型 / NDEF 内容，这几个都是全局状态。
    // 只还原卡库的话，跑完自检"内置卡"会变成 04:DE:AD:... 的合成卡。
    uint8_t uidBackup[10], uidLenBackup = s_uidLen, typeBackup = s_emuType;
    char contentBackup[48];
    memcpy(uidBackup, s_uid, sizeof(s_uid));
    memcpy(contentBackup, s_content, sizeof(s_content));

    const uint8_t fakeUid[7] = {0x04, 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    const char* fakeUrl = "https://example.com/clone";

    // 1) 造"真卡"：用现成的 NDEF 生成器产出一份标准 NTAG213 内存
    cloneClear();
    s_emuType = (uint8_t)m5::nfc::a::Type::NTAG_213;
    s_uidLen = 7;
    memcpy(s_uid, fakeUid, 7);
    emuSetContent(fakeUrl);
    buildMemory();

    Tag t{};
    t.valid = true;
    t.typeCode = (uint8_t)m5::nfc::a::Type::NTAG_213;
    t.uidLen = 7;
    memcpy(t.uid, fakeUid, 7);
    t.atqa = 0x0044;
    t.sak = 0x00;
    t.userLen = 144;
    memcpy(t.user, s_mem + 16, t.userLen);

    // 2) 走克隆：应用 → 重新拼内存 → 跟"读到的数据"比
    const bool applied = cloneApply(t);
    buildMemory();
    const bool same = (memcmp(s_mem + 16, t.user, t.userLen) == 0);

    // 3) NVS 往返：存完再读出来，字段必须一致（现在落到卡库的第 cur 槽）
    const bool saved = cloneSave();
    Tag back{};
    bool nvsOk = false;
    {
        Preferences p;
        if (p.begin("caplab", true)) {
            char k[6];
            libSlotKey(k, sizeof(k), s_curIdx);
            if (p.getBytesLength(k) == sizeof(Tag)) {
                nvsOk = (p.getBytes(k, &back, sizeof(Tag)) == sizeof(Tag)) &&
                        back.valid && back.typeCode == t.typeCode && back.uidLen == t.uidLen &&
                        back.userLen == t.userLen && memcmp(back.uid, t.uid, t.uidLen) == 0 &&
                        memcmp(back.user, t.user, t.userLen) == 0;
            }
            p.end();
        }
    }

    // 4) 起模拟，看芯片真的进了被动目标模式
    const bool started = emuStart();

    Serial.printf("[clone-test] apply=%d bytes_match=%d save=%d nvs_roundtrip=%d emu_start=%d\n",
                  (int)applied, (int)same, (int)saved, (int)nvsOk, (int)started);
    Serial.printf("[clone-test] type=%s uid=%s userlen=%u\n", tagTypeName(t.typeCode), emuUidText(),
                  (unsigned)t.userLen);
    emuDump();

    // 还原现场：停掉合成卡的模拟，把卡库和内存都恢复成自检之前的样子
    if (s_running) emuStop();
    s_libCount = cntBackup;
    s_curIdx = curBackup;
    for (int i = 0; i < kLibMax; i++) s_lib[i] = libBackup[i];
    libPersist();

    s_clone = prevClone;
    s_cloneOn = prevOn;

    // 先把自检动过的全局参数无条件还原，再决定要不要套回原来的克隆卡
    memcpy(s_uid, uidBackup, sizeof(s_uid));
    s_uidLen = uidLenBackup;
    s_emuType = typeBackup;
    memcpy(s_content, contentBackup, sizeof(s_content));
    s_cloneSum[0] = 0;

    if (prevOn && prevClone.valid) {
        memcpy(s_uid, prevClone.uid, sizeof(s_uid));
        s_uidLen = prevClone.uidLen;
        s_emuType = prevClone.typeCode;
        snprintf(s_cloneSum, sizeof(s_cloneSum), "%s %uB", tagTypeName(prevClone.typeCode),
                 (unsigned)prevClone.userLen);
    }
    refreshUidText();
    buildMemory();
    Serial.printf("[clone-test] restored=%d lib=%d cur=%d type=%s\n", (int)hadBackup, s_libCount,
                  s_curIdx, (hadBackup && prevOn) ? tagTypeName(prevClone.typeCode) : "builtin");

    // 通过的条件：四步全绿 + 用户区逐字节一致
    return applied && same && saved && nvsOk && started;
}

}  // namespace nfc
