// Cap CC1101 (M5Stack U219) —— 最小但完整的 CC1101 驱动
//
// 只做本固件需要的事：上电时序、频段切换（含 RF_SW1 的 GDO2 玄机）、
// 异步 OOK 收发、RSSI、快速跳频。
//
// 硬件事实（来自官方 PinMap，别改）：
//   POWER_EN = G3   整卡 3.3V 总闸，必须拉高，否则 SPI 上读不到任何东西
//   RF_SW0   = G13  主机直驱；RF_SW1 接 CC1101 自己的 GDO2，只能写 IOCFG2
//   CC_CS    = G5   GDO0 = G15（RX 时是输出，TX 时是输入）
//   NFC_CS   = G6   NFC_IRQ = G4（同一个 SPI 总线，片选必须互斥）
//   SPI      = SCK 40 / MOSI 14 / MISO 39（与机身 SD 卡共用）

#pragma once
#include <Arduino.h>

#define CAP_PIN_POWER_EN 3
#define CAP_PIN_RF_SW0   13
#define CAP_PIN_CC_CS     5
#define CAP_PIN_GDO0     15
#define CAP_PIN_NFC_CS    6
#define CAP_PIN_NFC_IRQ   4
#define CAP_PIN_SD_CS    12

#define CAP_PIN_SCK      40
#define CAP_PIN_MOSI     14
#define CAP_PIN_MISO     39

// ── CC1101 寄存器 ──────────────────────────────────────────
#define CC_IOCFG2    0x00
#define CC_IOCFG1    0x01
#define CC_IOCFG0    0x02
#define CC_FIFOTHR   0x03
#define CC_PKTLEN    0x06
#define CC_PKTCTRL1  0x07
#define CC_PKTCTRL0  0x08
#define CC_ADDR      0x09
#define CC_CHANNR    0x0A
#define CC_FSCTRL1   0x0B
#define CC_FSCTRL0   0x0C
#define CC_FREQ2     0x0D
#define CC_FREQ1     0x0E
#define CC_FREQ0     0x0F
#define CC_MDMCFG4   0x10
#define CC_MDMCFG3   0x11
#define CC_MDMCFG2   0x12
#define CC_MDMCFG1   0x13
#define CC_MDMCFG0   0x14
#define CC_DEVIATN   0x15
#define CC_MCSM2     0x16
#define CC_MCSM1     0x17
#define CC_MCSM0     0x18
#define CC_FOCCFG    0x19
#define CC_BSCFG     0x1A
#define CC_AGCCTRL2  0x1B
#define CC_AGCCTRL1  0x1C
#define CC_AGCCTRL0  0x1D
#define CC_WORCTRL   0x20
#define CC_FREND1    0x21
#define CC_FREND0    0x22
#define CC_FSCAL3    0x23
#define CC_FSCAL2    0x24
#define CC_FSCAL1    0x25
#define CC_FSCAL0    0x26
#define CC_TEST2     0x2E
#define CC_TEST1     0x2F
#define CC_TEST0     0x30
#define CC_PATABLE   0x3E

#define CC_RSSI      0x34
#define CC_MARCSTATE 0x35
#define CC_VERSION   0x31   // 必须是 0x14

// ── 命令选通 ──────────────────────────────────────────────
#define CC_SRES      0x30
#define CC_SFSTXON   0x31
#define CC_SXOFF     0x32
#define CC_SCAL      0x33
#define CC_SRX       0x34
#define CC_STX       0x35
#define CC_SIDLE     0x36
#define CC_SWOR      0x38
#define CC_SPWD      0x39
#define CC_SFRX      0x3A
#define CC_SFTX      0x3B

#define CC_GDO_HW0   0x2E   // 静态低
#define CC_GDO_HW1   0x2F   // 静态高

enum CapBand : uint8_t { BAND_315 = 0, BAND_433 = 1, BAND_868 = 2, BAND_915 = 3 };
constexpr uint8_t BAND_COUNT = 4;

class CapCC1101 {
public:
    bool begin();
    bool present() const { return _ok; }
    uint8_t chipVersion() const { return _version; }

    void setBand(CapBand b);
    CapBand band() const { return _band; }
    float bandMHz() const;
    static float bandOf(CapBand b);
    static const char* bandName(CapBand b);

    void setFrequencyMHz(float mhz);   // 频谱扫描用，纯改频率 + 重校准

    void enterOokRx();
    void enterOokTx();
    void idle();
    void sleep();

    int8_t rssi();                     // dBm，未校准时约 -120
    uint8_t marcState();

    uint8_t readReg(uint8_t addr);
    void writeReg(uint8_t addr, uint8_t val);
    void strobe(uint8_t cmd);

private:
    void resetChip();
    void applyOokProfile();
    void writeBurst(uint8_t addr, const uint8_t* data, uint8_t len);

    bool _ok = false;
    uint8_t _version = 0;
    CapBand _band = BAND_433;
};
