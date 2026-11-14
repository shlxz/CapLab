# CapLab v1.5.1 — M5Burner 官方仓库 PR 用

这个目录是走 `m5stack/M5Stack-Firmware` 仓库 PR 路线时提交的内容。
走 WebBurner 网页上传（推荐、更快）**不需要**这个目录，只要 `CapLab-1.5.1-full.bin` + 封面。

## 提交前必改

`m5burner.json` 里的 `repository` 已填好公开仓库地址
`https://github.com/shlxz/CapLab`。
WebBurner 表单的 `Github` 字段填同一个地址。

## 目录规则（官方要求）

- 根目录必须有 `m5burner.json` 和 `README.md`（本文件）。
- `firmware/` 下的文件名必须是 `名称_0x烧录地址.bin`。
- 四个分文件对应 ESP32-S3 的标准烧录地址：

| 文件 | 地址 | 内容 |
|---|---|---|
| `CapLab_0x0.bin` | `0x0` | bootloader |
| `CapLab_0x8000.bin` | `0x8000` | 分区表（huge_app：nvs / otadata / app0 3 MB / spiffs / coredump） |
| `CapLab_0xe000.bin` | `0xe000` | otadata（boot_app0） |
| `CapLab_0x10000.bin` | `0x10000` | 应用程序 |

## 提交步骤

1. Fork `m5stack/M5Stack-Firmware`。
2. 把本目录整个放进你的仓库，在 `firmware-repo.list` 里登记仓库地址。
3. 提 PR，M5Stack 审核通过后会出现在 M5Burner 目录。
4. 之后更新固件只需推新文件，M5Burner 会自动拉取，不用重新 PR。

> 固件含 NFC 克隆与 sub-GHz 重放功能，PR 描述里请写明仅限自有卡片 / 授权测试。
