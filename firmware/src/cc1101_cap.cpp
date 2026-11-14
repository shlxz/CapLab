#include "cc1101_cap.h"
#include <SPI.h>

// 异步 OOK 预设：寄存器值照抄 Flipper Zero 生产固件的
// subghz_device_cc1101_preset_ook_270khz_async_regs
// 270.833kHz 接收带宽 / 3.79 kBaud / ASK-OOK / 无前导码与同步字
static const uint8_t OOK_PROFILE[][2] = {
    {CC_IOCFG1,   0x2E},   // GDO1 未用，静态低
    {CC_IOCFG0,   0x0D},   // GDO0 = 异步串行数据
    {CC_FIFOTHR,  0x47},
    {CC_PKTCTRL0, 0x32},   // 异步串行 + 连续模式
    {CC_FSCTRL1,  0x06},
    {CC_FSCTRL0,  0x00},
    {CC_MDMCFG4,  0x67},
    {CC_MDMCFG3,  0x32},
    {CC_MDMCFG2,  0x30},   // MOD_FORMAT = ASK/OOK
    {CC_MDMCFG1,  0x00},
    {CC_MDMCFG0,  0x00},
    {CC_MCSM0,    0x18},
    {CC_FOCCFG,   0x18},
    {CC_BSCFG,    0x6C},
    {CC_AGCCTRL2, 0x03},
    {CC_AGCCTRL1, 0x00},
    {CC_AGCCTRL0, 0x40},
    {CC_WORCTRL,  0xFB},
    {CC_FREND1,   0xB6},
    {CC_FREND0,   0x11},   // PA_POWER 索引 1 -> PATABLE[1]
    {CC_FSCAL3,   0xE9},
    {CC_FSCAL2,   0x2A},
    {CC_FSCAL1,   0x00},
    {CC_FSCAL0,   0x1F},
    {CC_TEST2,    0x81},
    {CC_TEST1,    0x35},
    {CC_TEST0,    0x09},
};

static SPISettings s_spi(2000000, MSBFIRST, SPI_MODE0);

// CS 拉低后 MISO 会变低表示芯片就绪；芯片不在时不能死等
static inline bool waitReady() {
    uint32_t t0 = micros();
    while (digitalRead(CAP_PIN_MISO) == HIGH) {
        if (micros() - t0 > 2000) return false;
    }
    return true;
}

void CapCC1101::writeReg(uint8_t addr, uint8_t val) {
    SPI.beginTransaction(s_spi);
    digitalWrite(CAP_PIN_CC_CS, LOW);
    waitReady();
    SPI.transfer(addr & 0x3F);
    SPI.transfer(val);
    digitalWrite(CAP_PIN_CC_CS, HIGH);
    SPI.endTransaction();
}

uint8_t CapCC1101::readReg(uint8_t addr) {
    // 0x30..0x3D 为状态寄存器，读访问要置 burst 位
    uint8_t a = (addr >= 0x30 && addr <= 0x3D) ? (addr | 0xC0) : (addr | 0x80);

    SPI.beginTransaction(s_spi);
    digitalWrite(CAP_PIN_CC_CS, LOW);
    if (!waitReady()) {
        digitalWrite(CAP_PIN_CC_CS, HIGH);
        SPI.endTransaction();
        return 0;
    }
    SPI.transfer(a);
    uint8_t v = SPI.transfer(0x00);
    digitalWrite(CAP_PIN_CC_CS, HIGH);
    SPI.endTransaction();
    return v;
}

void CapCC1101::writeBurst(uint8_t addr, const uint8_t* data, uint8_t len) {
    SPI.beginTransaction(s_spi);
    digitalWrite(CAP_PIN_CC_CS, LOW);
    waitReady();
    SPI.transfer(addr | 0x40);
    for (uint8_t i = 0; i < len; i++) SPI.transfer(data[i]);
    digitalWrite(CAP_PIN_CC_CS, HIGH);
    SPI.endTransaction();
}

void CapCC1101::strobe(uint8_t cmd) {
    SPI.beginTransaction(s_spi);
    digitalWrite(CAP_PIN_CC_CS, LOW);
    waitReady();
    SPI.transfer(cmd);
    digitalWrite(CAP_PIN_CC_CS, HIGH);
    SPI.endTransaction();
}

void CapCC1101::resetChip() {
    digitalWrite(CAP_PIN_CC_CS, HIGH);
    delayMicroseconds(5);
    digitalWrite(CAP_PIN_CC_CS, LOW);
    delayMicroseconds(10);
    digitalWrite(CAP_PIN_CC_CS, HIGH);
    delayMicroseconds(45);
    strobe(CC_SRES);
    delay(10);
}

bool CapCC1101::begin() {
    // 1. 先给整卡上电：POWER_EN 不拉高，后面全是读 0xFF
    pinMode(CAP_PIN_POWER_EN, OUTPUT);
    digitalWrite(CAP_PIN_POWER_EN, HIGH);
    delay(120);

    // 2. 无关片选顶高，避免它们在总线上乱应答
    pinMode(CAP_PIN_NFC_CS, OUTPUT);
    digitalWrite(CAP_PIN_NFC_CS, HIGH);
    pinMode(CAP_PIN_SD_CS, OUTPUT);
    digitalWrite(CAP_PIN_SD_CS, HIGH);
    pinMode(CAP_PIN_CC_CS, OUTPUT);
    digitalWrite(CAP_PIN_CC_CS, HIGH);
    pinMode(CAP_PIN_NFC_IRQ, INPUT_PULLUP);

    pinMode(CAP_PIN_RF_SW0, OUTPUT);
    digitalWrite(CAP_PIN_RF_SW0, LOW);

    pinMode(CAP_PIN_GDO0, INPUT);

    // 3. SPI 与机身 SD 卡同线，CS 交给驱动自己管
    SPI.begin(CAP_PIN_SCK, CAP_PIN_MISO, CAP_PIN_MOSI, CAP_PIN_CC_CS);

    resetChip();

    _version = readReg(CC_VERSION);
    _ok = (_version == 0x14);

    if (_ok) {
        setBand(BAND_433);
        applyOokProfile();
        strobe(CC_SIDLE);
        strobe(CC_SFRX);
        strobe(CC_SFTX);
    }
    return _ok;
}

float CapCC1101::bandOf(CapBand b) {
    switch (b) {
        case BAND_315: return 315.0f;
        case BAND_433: return 433.92f;
        case BAND_868: return 868.35f;
        case BAND_915: return 915.0f;
        default:       return 433.92f;
    }
}

const char* CapCC1101::bandName(CapBand b) {
    switch (b) {
        case BAND_315: return "315";
        case BAND_433: return "433";
        case BAND_868: return "868";
        case BAND_915: return "915";
        default:       return "???";
    }
}

float CapCC1101::bandMHz() const { return bandOf(_band); }

void CapCC1101::setBand(CapBand b) {
    _band = b;

    // 官方真值表：315=0/0  433=0/1  868|915=1/1
    uint8_t sw0 = 0, sw1 = 0;
    switch (b) {
        case BAND_315: sw0 = 0; sw1 = 0; break;
        case BAND_433: sw0 = 0; sw1 = 1; break;
        case BAND_868:
        case BAND_915: sw0 = 1; sw1 = 1; break;
    }

    digitalWrite(CAP_PIN_RF_SW0, sw0 ? HIGH : LOW);

    // RF_SW1 不在主机引脚上，挂在 CC1101 的 GDO2：只能写 IOCFG2 设成静态电平
    writeReg(CC_IOCFG2, sw1 ? CC_GDO_HW1 : CC_GDO_HW0);

    setFrequencyMHz(bandOf(b));
}

void CapCC1101::setFrequencyMHz(float mhz) {
    // f = FREQ/2^16 * 26MHz
    uint32_t f = (uint32_t)((mhz * 65536.0f) / 26.0f + 0.5f);
    writeReg(CC_FREQ2, (f >> 16) & 0xFF);
    writeReg(CC_FREQ1, (f >> 8) & 0xFF);
    writeReg(CC_FREQ0, f & 0xFF);
}

void CapCC1101::applyOokProfile() {
    for (uint8_t i = 0; i < sizeof(OOK_PROFILE) / sizeof(OOK_PROFILE[0]); i++) {
        writeReg(OOK_PROFILE[i][0], OOK_PROFILE[i][1]);
    }
    const uint8_t pa[2] = {0x00, 0xC0};   // index1 ≈ +10dBm
    writeBurst(CC_PATABLE, pa, 2);
}

void CapCC1101::enterOokRx() {
    applyOokProfile();
    setBand(_band);        // profile 之后再落一次频段，防 IOCFG2 被覆盖
    strobe(CC_SIDLE);
    strobe(CC_SFRX);
    strobe(CC_SRX);
}

void CapCC1101::enterOokTx() {
    applyOokProfile();
    setBand(_band);
    strobe(CC_SIDLE);
    strobe(CC_SFTX);
    strobe(CC_STX);
}

void CapCC1101::idle() {
    strobe(CC_SIDLE);
    strobe(CC_SFRX);   // 顺手清 FIFO，否则下次进 RX 会带着溢出标志
}

void CapCC1101::sleep() {
    strobe(CC_SIDLE);
    strobe(CC_SXOFF);
    strobe(CC_SPWD);
}

int8_t CapCC1101::rssi() {
    uint8_t raw = readReg(CC_RSSI);
    int16_t v = (raw >= 128) ? ((int16_t)raw - 256) : (int16_t)raw;
    int32_t dbm = (int32_t)(v / 2) - 74;
    // IDLE 时 RSSI 寄存器无效，会算出离谱的值，钳住
    if (dbm > 0) dbm = 0;
    if (dbm < -120) dbm = -120;
    return (int8_t)dbm;
}

uint8_t CapCC1101::marcState() { return readReg(CC_MARCSTATE) & 0x1F; }
