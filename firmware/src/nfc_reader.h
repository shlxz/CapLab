// NFC 读卡（Cap CC1101 的 ST25R3916 半边）
//
// 走 m5stack 官方 M5Unit-NFC：SPI mode 1、10MHz、CS=G6、IRQ=G4。
// 与 CC1101 共用同一组 SPI 线，所以只在 NFC 界面里动它，别和 Sub-GHz 同时跑。
#pragma once
#include <stdint.h>

namespace nfc {

struct Card {
    bool present = false;
    char uid[20] = "";
    char type[26] = "";
    uint16_t atqa = 0;
    uint8_t sak = 0;
    uint16_t blocks = 0;
    bool ndefCapable = false;
    bool ndefTried = false;
    char ndef[56] = "";
};

bool begin();          // 上电检测，只需一次
bool ready();
bool recover();        // 芯片卡死时重新初始化（保留已配置的 SPI 与参数）
const char* lastError();

uint8_t idType();      // ST25R3916 的 5bit IC 型号码，正常是 5
uint8_t idRev();       // 3bit 版本号，rev 3.1 时为 2
bool idOk();           // 型号码读对了才算芯片真的在线

bool scan(Card& c);    // 探测 + 识别（读完立即去激活）
bool scanA(Card& c);   // 只扫 NFC-A，不试 ISO15693；高频轮询用
bool scanV(Card& c);   // 手动扫 ISO15693（会临时切芯片模式再切回）
bool readNdef(Card& c);// 重新激活并读首条 NDEF

// ── 卡模拟（NFC-A / Type 2 Tag）──────────────────────────
// 让 Cardputer 装成一张 NTAG213，手机碰上去读到我们编的 NDEF。
//
// 能力边界（库的限制，不是硬件限制）：
//   支持 NTAG203/210/212/213/215/216、MIFARE Ultralight  ← NDEF 标签，手机可读
//   不支持 MIFARE Classic / DESFire / CPU 卡            ← 缺 Crypto1 与密钥认证，
//                                                         所以老门禁卡模拟不了
enum class EmuState : uint8_t { Off, Waiting, Ready, Active, Error };

bool emuStart();                 // 开始模拟（用当前的 UID / 内容）
void emuStop();                   // 停止并交还给读卡模式
void emuUpdate();                 // 主循环里必须调用
bool emuRunning();
EmuState emuState();
const char* emuStateName();
uint16_t emuHits();               // 被读卡器"贴"了几次（会话数，不是帧数）
uint32_t emuFrames();             // 累计收到的帧数，只用于调试
uint32_t emuSession();            // 每次 emuStart() 自增，用来判断"这是新一次模拟"
const char* emuUidText();         // "04:xx:xx:xx:xx:xx:xx"
const char* emuContentText();     // 当前写进标签的内容
bool emuSetContent(const char* text);   // 换内容（自动识别 URL / 纯文本）
void emuCycleContent();           // 在几套预设之间切换
bool emuSetUid(const char* hex);  // 14 位十六进制
void emuRandomUid();
void emuDump();                   // 把芯片里的 passive-target 内存读回来打印

// ── 标签克隆（读一张真卡 → 照原样模拟）─────────────────
// Type 2 Tag（NTAG21x / MIFARE Ultralight）的存储是扁平页，
// 用户区可以原样搬回来，配上同样的 UID 就是完整克隆。
//
// 做不到的：MIFARE Classic / DESFire / CPU 卡。
//   - Classic 缺 Crypto1，扇区数据根本读不出来；
//   - 而且模拟层 begin() 明确只接受 NTAG2 与 Ultralight。
struct Tag {
    bool valid = false;
    uint8_t typeCode = 0;       // m5::nfc::a::Type 的底层值
    uint8_t uidLen = 0;
    uint8_t uid[10] = {0};
    uint16_t atqa = 0;
    uint8_t sak = 0;
    uint16_t userLen = 0;       // 用户区字节数
    uint8_t user[888] = {0};    // 用户区原始数据（NTAG216 上限）
};

bool supported(const Tag& t);   // 这张卡的类型能不能被模拟
bool cloneRead(Tag& t);         // 读卡并导出完整内容（串口用，内部重试 6 轮）
bool clonePoll(Tag& t);         // 单次握手：选卡+读全片一步完成，失败立即返回
bool cloneApply(const Tag& t);  // 让模拟目标变成这张卡
const Tag& cloneCurrent();      // 当前正在模拟的克隆卡
bool cloneActive();             // 当前模拟目标是不是克隆卡（而非内置 NDEF）
void cloneClear();              // 回到内置 NDEF

bool cloneSave();               // 把当前克隆卡存进卡库并记为"当前"
bool cloneLoad();               // 从 NVS 恢复上次选中的那张
bool cloneStored();
void cloneDisable();            // 退出克隆模式但保留存档
bool cloneRestore();            // 开机时恢复上次的克隆模式

// ── 卡库：读过的卡都留着，随时切 ──────────────────────
// 每张 Tag 约 900 字节（用户区 888 B 占大头），8 张 ≈ 7.3 KB。
// NVS 分区 24 KB，装得下；RAM 也够（当前只用了 45 KB / 327 KB）。
constexpr int kLibMax = 8;

int libCount();
const Tag* libGet(int i);       // 越界或不存在的槽返回 nullptr
int libCurrent();               // 当前模拟的是第几张，-1 = 内置 NDEF
int libIndexOf(const Tag& t);   // 按 UID 找，没有返回 -1
int libAdd(const Tag& t);       // 同 UID 覆盖，否则追加；返回索引，满了返回 -1
bool libSelect(int i);          // 切到第 i 张（会重建内存）
bool libRemove(int i);
bool libPersist();              // 整个库写回 NVS
bool libLoad();                 // 从 NVS 读回，兼容旧的单卡存档

// 单卡快照：验证"删光重启"这类破坏性行为时用来兜底。
// 只存当前选中那一张（≈900 B），NVS 里放得下，比整个库备份省一半空间。
bool libBackupCurrent();        // 把当前选中那张存到备份槽
bool libRestoreBackup();        // 把备份槽写回卡库（同 UID 覆盖或追加）
bool libHasBackup();

const char* tagTypeName(uint8_t typeCode);
uint8_t typeCodeNTAG213();      // 外部造 Tag 时用，免得为了一个枚举去 include 库头
const char* cloneSummary();     // 给界面用的一行摘要
bool cloneSelfTest();           // 用合成数据把 apply→拼内存→模拟→NVS 全走一遍

}  // namespace nfc
