# CapLab v1.5.1 — WebBurner 上架表单（可直接复制）

填写地址：<https://burner.m5stack.com> → 右上角登录 M5Stack 社区账号 → **Developer ▸ Upload firmware**

| 字段                | 填入内容                                          |
| ----------------- | --------------------------------------------- |
| **Name**          | `CapLab: Sub-GHz + NFC Lab for Cardputer ADV` |
| **Version**       | `1.5.1`                                       |
| **Device**        | **Cardputer ADV**（不要选 `Cardputer`，两者是不同分类）    |
| **Categories**    | 选站上最接近的 Tools / Utilities 或 RF 类（以实际下拉项为准）    |
| **Github**        | 你的公开仓库地址，形如 `https://github.com/<用户名>/CapLab` |
| **FirmWare**      | `CapLab-1.5.1-full.bin`（**合并镜像**，烧录地址 `0x0`）  |
| **Cover**         | `CapLab-1.5.1-cover.png`（1600×900，16:9）       |
| **Visibility**    | `Public`（提交后状态为 `Pending`，M5Stack 审核通过才公开）    |
| **Version notes** | 见下方 Changelog                                 |

> **不要用 `CapLab-1.5.1-app.bin` 上传 M5Burner。** 那个是 app-only 镜像，烧录地址是 `0x10000`，  
> 只用于 M5Launcher 从 SD 卡安装。M5Burner 从 `0x0` 写入整片，传它会导致  
> `Invalid image block, can't boot`。`full.bin` 已含 bootloader + 分区表 + otadata + app 四段。

## Description（英文，粘贴用）

```
CapLab - Sub-GHz and NFC lab firmware for M5Stack Cardputer ADV with the
Cap CC1101 & NFC cap (U219).

Built for the U219 cap, which brings a CC1101 sub-GHz transceiver and an
ST25R3916 NFC front end on one board. Bruce does not initialise this cap
(POWER_EN and RF_SW are missing), so the driver and UI were written from
scratch.

Features
- Sub-GHz capture and replay on 315 / 433 / 868 / 915 MHz, with PWM / raw
  decoding and a 32-channel RSSI spectrum sweep.
- NFC-A tag reading, NDEF extraction, and Type 2 Tag (NTAG213/215/216,
  Ultralight) full-dump cloning into an 8-slot card library saved to NVS.
- Card emulation: replay a cloned tag with its real UID, or use the built-in
  NDEF tag. Verified with a phone reading https://www.m5stack.com.
- Persistent capture mode: hold a tag near the reader and it is cloned
  automatically, with no timeout and no false "no card" errors.
- Bilingual UI (Simplified Chinese / English), toggled instantly with the G
  key from any screen; the choice persists across reboots (NVS).
- Single Help key (H) available from any screen.

Keys: G language toggle, H help, ENTER confirm / primary action,
SPACE start-stop, ; and . move selection, DEL back, 1-6 menu shortcuts.

Note: the UI defaults to Simplified Chinese and switches to full English
with the G key. Technical values (UID, ATQA, dBm, chip names) use standard
notation.

Hardware required: Cardputer ADV + Cap CC1101 & NFC (U219).
MIFARE Classic cloning is not supported (no Crypto1).

For authorised testing and your own cards / remotes only. Replaying
sub-GHz transmissions may be regulated in your region; fit an antenna
and transmit only where permitted.
```

## Version notes / Changelog（粘贴用）

```
v1.5.1
- Fixed "clear capture" not erasing the stored copy in NVS: after clearing,
  a previously saved capture reappeared on screen after reboot.

v1.5.0
- Bilingual UI: press G in any screen to switch Simplified Chinese /
  English; the choice is stored in NVS and survives reboots.
- Language badge (中 / EN) in the status bar; serial command "lang [zh|en]"
  to query or switch, "stat" now reports lang=.
- Fixed overlapping text on the Emulate screen (clone-bytes line vs status
  line) present since 1.4.1.
- All UI strings moved into a single bilingual string table (i18n.h).

v1.4.1
- H is now a global one-key help shortcut, available from any screen.
  Press H again or DEL to return to the previous screen.
- Help screen lists H; menu item 6 subtitle now reads "Press H to view".
- Serial commands: added "key h" and "ui help" for scripted UI checks.

v1.4.0
- Fully Chinese UI (efontCN_12), seven screens, unified key layout.
- Persistent clone capture: single-handshake clonePoll(), no timeout,
  no spurious "no card" errors.
- Fixed cloned cards reappearing after deletion and reboot.
- Removed automatic ISO15693 probing that periodically stalled NFC.

v1.3.x
- 8-slot card library with NVS persistence and per-card selection.
- Boot-time restore of the active cloned card.
```

## 提交后

1. 条目先是 `Pending`，M5Stack 人工审核通过后才变成公开。
2. LauncherHub 镜像官方目录，通常在**一天内**同步。
3. 验证是否已被收录（把 `CapLab` 换成实际名字）：
   ```bash
   curl "https://api.launcherhub.net/firmwares?category=cardputer&q=CapLab"
   ```
   返回 `total: 1` 且 `items[0].name` 匹配即为成功。
4. 真机验证：Launcher → **OTA** → 搜索 `CapLab` → 安装一次。

> LauncherHub 本身**没有公开上传接口**（`api.launcherhub.net` 只读）。  
> 上传是 WebBurner 上绑定账号的手工步骤，无法用脚本自动化。

## 本地文件清单

| 文件                       | 用途                                                     |
| ------------------------ | ------------------------------------------------------ |
| `CapLab-1.5.1-app.bin`   | M5Launcher SD 卡安装，写 `0x10000`                          |
| `CapLab-1.5.1-full.bin`  | **M5Burner / WebBurner 上传**，写 `0x0`                    |
| `CapLab-1.5.1-cover.png` | 上架封面 1600×900                                          |
| `SHA256SUMS`             | 三个发布文件的校验值                                             |
| `m5burner/`              | 走官方 `M5Stack-Firmware` 仓库 PR 路线时的分文件 + `m5burner.json` |
