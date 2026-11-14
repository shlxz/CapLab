# CAP LAB v1.5.1 — 固件包

Cardputer-Adv + Cap CC1101 (U219) 的 sub-GHz 采集/重放 + NFC 读写 / **卡模拟 / 真卡克隆**固件。

| 文件 | 用途 | 大小 |
|---|---|---|
| `CapLab-1.5.1-app.bin` | **给 M5Launcher 用**，只含应用程序，写 `0x10000` | 1223 KB |
| `CapLab-1.5.1-full.bin` | 整片镜像（bootloader + 分区表 + otadata + app），写 `0x0`；**也是上架 M5Burner 要传的文件** | 1287 KB |
| `CapLab-1.5.1-cover.png` | 上架封面，1600×900（16:9） | — |
| `listing.md` | WebBurner 上架表单逐字段内容 + 英文描述 / 更新日志 | — |
| `SHA256SUMS` | 三个发布文件的校验值 | — |
| `m5burner/` | 走官方仓库 PR 路线时的分文件 + `m5burner.json` | — |

两个 bin 都是从同一份源码 `firmware/` 编出来的，功能完全一致，区别只在烧录地址。

## 发布到 LauncherHub（在线目录）

**结论：LauncherHub 没有直传入口。** `api.launcherhub.net` 是只读的固件目录，
它镜像 M5Stack 官方固件库（M5Burner / WebBurner）的数据。个人固件要先传到
WebBurner，M5Stack 审核通过后约一天才会出现在 LauncherHub，之后 Launcher 的
`OTA` 菜单才能搜到。上传本身是**绑定账号的手工步骤，没有公开 API，不能脚本化**。

### 路线 A：WebBurner 开发者上传（公开发布，推荐）

1. 准备一个 **M5Stack 社区账号**和一个**公开 GitHub 仓库**（表单里 `Github` 字段必填）。
   本目录 `m5burner/m5burner.json` 里的 `repository` 是占位符，发布前改成你的真实地址。
2. 打开 <https://burner.m5stack.com> → 右上角登录 → **Developer ▸ Upload firmware**。
3. 按 `listing.md` 的表逐字段填。**Firmware 传 `CapLab-1.5.1-full.bin`（烧录地址 `0x0`）**，
   Cover 传 `CapLab-1.5.1-cover.png`，Device 选 **Cardputer ADV**。
4. Visibility 选 `Public`。提交后是 `Pending`，M5Stack 人工审核通过才公开。
5. 一天后用下面的命令确认已同步，并在 Launcher → `OTA` → 搜 `CapLab` 试装一次：

```bash
curl "https://api.launcherhub.net/firmwares?category=cardputer&q=CapLab"
# 返回 total:1 且 items[0].name 匹配即成功
```

> 2026-11-14 实测基线：`q=CapLab` 返回 `total:0`（尚未上架），
> 对照 `q=nucleoos` 返回 `total:1`，确认这个查询方式有效。
> 桌面版 M5Burner 走 `USER CUSTOM ▸ Publish`，字段一样；官方宣布 2027-01-01 停止服务，
> 之后统一用 WebBurner。桌面版还能拿 `Share Code` 小范围分享，不用等审核。

### 路线 B：官方仓库 PR（长期维护）

Fork `m5stack/M5Stack-Firmware`，把本目录 `m5burner/`（`m5burner.json` + `firmware/`
下四个按地址命名的分文件 + 一份 README）放进仓库，在 `firmware-repo.list` 里登记后提 PR。
审核通过后 M5Burner 会自动拉取后续更新，不用每次重新上传。文件名规则是
`名称_0x烧录地址.bin`（`0x0` / `0x8000` / `0xe000` / `0x10000` 四段）。

### 路线 C：自己用，不上传（最快）

不需要账号、不需要审核，也不会公开：

- **SD 卡**：把 `CapLab-1.5.1-app.bin` 拷进 FAT32 卡 → Launcher → `SD` → `Install`（详见下文）。
- **WebUI**：Launcher → `WUI` → 浏览器上传 bin。
- **Favorites 直链**：把 bin 放到任何设备能直接下载的 URL（如 GitHub Release），
  在 Launcher 的 `config.conf` 里加一条 `{"name":"CapLab","fid":"","link":"<直链>"}`。

### 两个必须避开的坑

- **别把 `app.bin` 传给 M5Burner。** 它是 app-only 镜像，属于 `0x10000`；
  M5Burner 从 `0x0` 写入整片，传它会报 `Invalid image block, can't boot`。
  `full.bin` 已含四段，静态校验：`0x0` 引导头 `0xE9`、`0x8000` 分区表魔数 `AA50`、
  `0xE000` otadata、`0x10000` app 头 `0xE9`，且 app 段与 `app.bin` 字节完全一致。
- **固件含 NFC 克隆与 sub-GHz 重放功能**，公开上架审核可能要求说明用途。
  `listing.md` 的英文描述里已经写明「仅限自有卡片 / 授权测试，遵守当地无线电规定」，别删掉。

## 1.5.1：修「清空采集」不清存储

采集屏按 `C` 清空原来只清内存，NVS 里还留着——之前测试存进去的演示数据（`123456`）重启后又会出现在默认界面。现在清空连 NVS 一起擦；开机自动恢复真实采集的特性保留（换电池不丢数据），正反回归均通过。

## 1.5.0：中英双语界面

任意界面按 `G` 一键切换 中文 / English，语言写入 NVS 跨重启保留；状态栏显示 `中` / `EN` 徽标；串口新增 `lang [zh|en]` 查询与切换，`stat` 增加 `lang=` 字段。全部 155 条界面文案集中到 `i18n.h` 双语字符串表。顺手修了模拟屏「克隆卡 504 字节」与状态行的叠字（1.4.1 就有）。

## 1.4.1：H 一键帮助

在任何界面按 `H` 都能打开中文操作帮助，再按一次 `H` 或按 `DEL` 原路返回刚才那一屏，不用先退到菜单再翻第 6 项。菜单第 6 项副标题也改成「按 H 查看」，首页就能看见这个键。

帮助屏通用键首行加了 `H 随时帮助`，两列各 6 行仍不串列；底部提示改成左右分栏 `ENTER 收录 1-4 频段` / `H / DEL 返回`。串口同步支持 `key h` 与 `ui help`，脚本能直接切到帮助屏抓图。

真机验证：`fw=1.4.1`；菜单按 `H` → 帮助屏(6) → 再按 `H` 回菜单(0)；读卡屏按 `H` → 帮助屏 → `DEL` 原路回读卡屏(3)；卡库 `LIB n=1` 与真卡 `NTAG 215 AD892681 504B` 保留。帮助屏只是叠一层绘制，不碰 RF / 模拟 / 收录状态，所以收录等卡时看一眼帮助再回来，等待态不会丢。

## 1.4.0：全中文界面 + 收录不再报错 / 不再超时

三个真机反馈：贴卡报 `no card`、不贴卡会超时、快捷键繁杂看不懂。

**界面全中文。** 用 M5GFX 自带的 `fonts::efontCN_12`（同时含 ASCII，中英混排不断字），状态栏、底部提示、状态徽标、toast、菜单全部汉化；技术数值（UID、ATQA、dBm、芯片型号）保留通用写法。字库让固件从 713 KB 涨到 1.25 MB，仍在 3 MB app 分区内（39.7%）。七个屏逐张截图核对过排版，底部提示统一改成左右分栏，不再出现 `DEL 返回` 被截断。

**按键收成一套，另加中文帮助屏。**

| 键 | 作用 |
|---|---|
| `H` | **任意界面一键帮助**，再按一次或 `DEL` 原路返回（1.4.1 新增） |
| `G` | **中英界面切换**，语言跨重启保留（1.5.0 新增） |
| `ENTER` | 确认 / 本屏主操作（收录、开始停止、使用卡片、进菜单） |
| `空格` | 开始 / 停止（多数屏与 `ENTER` 等价） |
| `;` `.` | 上下选择 |
| `DEL` | 返回上一层 |
| `1`-`6` | 菜单直选；其它屏 `1`-`4` 切频段 |

字母键只剩 `H L D B N U R C T`，且都写在对应屏的底部或帮助屏里。菜单第 6 项「操作帮助」是一屏中文对照表。老习惯的 `K` 仍兼容，但界面不再宣传它。

**收录改成常驻模式，`no card` 和超时一起消失。**

根因是**双次握手**：等待循环先用 `scanA()` 激活一次再去激活，紧接着 `cloneRead()` 又重新 `activate()` 最多 6 次，等于要求同一张卡连续握手成功两次。场重新起振、手一抖、位置略偏，第二次就全落空 → 报 `no card`。

现在 `clonePoll()` 把「选卡 + 读全片」合成一次握手，`activate()` 成功后直接在同一个激活态里读完所有用户页。主循环每 180 ms 试一次，**读不到不算错误，继续等**；只有 MIFARE Classic / 类型不支持 / 读取失败这类硬错误才说清原因并收工。20 秒超时整个删掉：不贴卡就一直等，屏幕显示「正在等卡 / 已找 N 次」加流动小方块，`DEL` 返回、`ENTER` 或空格取消。实测 26 秒后仍是「已找 160 次」，`stat` 正常响应。

收录成功走 `cloneSave()`，把新卡设为当前（卡库里有 `*`）并写 NVS，开机自动恢复；直接 `libAdd` 会漏掉这步。串口 `clone` 仍用 `cloneRead()`（内部循环 `clonePoll`），无卡约 1.5 秒返回 `no card`，行为不变。

**验证过的**：`clone test` 五项全绿且内置卡恢复 `04:A2:31:5C:7E:19:88`；`emu text hello world` 解析正常；`clone use/del/list`、删光后硬复位 `LIB n=0`；emu 反复开关后芯片正常；串口 `key` 新增 `enter` / `space` / `del`，脚本能走完整套 UI。

> 注：下面 1.3.1 里「20 秒超时 / 按 K 克隆」的描述是当时的实现，已被 1.4.0 / 1.4.1 取代，留作变更记录。

## 1.3.1 修了什么

三个真机反馈的问题：

**删掉的卡重启又回来了。** 1.2.x 的单卡存档键 `tag` 从来没被删过，而 `libLoad()` 在卡库为空时会把它当成"还没迁移过"重新读进来 —— 于是删光卡、重启，那张卡就复活了。现在 `libPersist()` 无条件删 `tag`，`libLoad()` 只在 `cnt` 键不存在（真正没用过卡库的老固件）时才迁移，迁完立刻删。实测：造 3 张 → 全删 → 硬复位 → `LIB n=0`。

**克隆报 `no card`。** 按 `K` 时固件同步死等读卡（内部只重试 6 次、约 0.5 秒），手还没把卡放稳就判死了，屏幕还冻着。现在改成非阻塞等待态：按 `K` 立刻返回，屏幕显示 `cloning / hold it still`，主循环每 200 ms 扫一次，扫到自动克隆，20 秒超时，再按 `K` 或 `SPACE` 取消。`cloneRead()` 也加了 `ensurePollMode()`，防止刚从模拟模式切回来时芯片还停在被动目标态。

**NFC 有时失灵。** 根因是自动轮询里的 ISO15693 探测：工程从来没调用过 `configureNFCMode(NFC::V)`，却用 NFC-A 的硬件配置发 15693 帧，既读不到 V 卡又会把射频状态弄乱。改成显式切模式再切回之后，实测仍会周期性让某次扫描卡住 4 秒以上（切两遍寄存器 + 等两次场起振）。本项目用的 NTAG21x / MIFARE 都是 NFC-A，所以把自动 V 探测整个去掉，只留轻量的 A 扫描；真要读 15693 卡用串口 `nfc v` 手动触发。单次扫描约 0.08 秒。另外加了 `recover()`：连续 8 次扫空就重新初始化一次芯片（STOP_ALL → SET_DEFAULT → 重新起振），不用再手动 RESET。

顺带修的：

- `clone test` 只还原了卡库，没还原它改过的 UID / NDEF 内容 → 跑完自检"内置卡"变成 `04:DE:AD:...` 的合成卡。现在连 `s_uid` / `s_content` / `s_emuType` 一起快照还原，实测自检后 `emu on` 回到 `04:A2:31:5C:7E:19:88` + `https://m5stack.com`。
- 新增 `clone bak` / `clone unbak`（单卡快照到独立 NVS 命名空间），做破坏性验证时不会再把真卡弄丢。
- 屏幕空闲提示从 `MIFARE / NTAG / ISO15693` 改成 `MIFARE / NTAG (NFC-A)`，跟实际探测范围一致。

## 1.3.0：NFC 卡库

以前只能存一张卡，换一张就把旧的顶掉。现在有 8 个槽位，按 UID 去重。

EMULATE 屏按 `L` 打开列表，`;` `.` 选，`ENTER` 换过去，`D` 删，`L`/`DEL` 关。行首 `*` 是当前正在模拟的那张。

串口对应 `clone list` / `clone use <n>` / `clone del <n>`（序号从 1 开始，和界面一致）。

1.2.2 及以前的单卡存档会自动迁移到 `c0`，不用重新贴卡。

## 1.3.0 同批修的

**串口 `emu text <内容>` 从来没生效过。** 命令解析用 `strtok` 只切出第一个词，`emu text hello world` 拿到的 arg 是 `"text"`，后面 `strncmp(arg, "text ", 5)` 永远匹配不上 —— 一直掉进默认查询分支。改成手写分词、把命令名之后整段都当参数，现在 `emu text hello world` 能真正写进内置 NDEF。

**`clone off` 之后 UID 还是真卡的。** 只清了克隆标志，没复位 `s_uid`，退回内置 NDEF 时会造出一张「真卡 UID + 内置内容」的怪卡，两种模式都对不上。现在 `cloneDisable()` 连 UID / 类型 / 内存一起复位到内置 NTAG213。

**克隆模式下按 N/U 静默失效。** 内容跟着真卡走，改内置 NDEF 不会生效，但按键和串口都不吭声。现在按键弹 `clone mode - B builtin`，串口回 `ERR emu text: clone mode (...)`，底部提示也不再摆 `N content`。

**串口 `emu stat` 的 `content=` 在克隆模式下是假的。** 它打的是内置 NDEF 的残留字符串（`https://example.com/clone`），而实际广播的是真卡那 504 字节。字段改名为 `src=`，克隆时显示 `NTAG 215 504B`。屏幕那边本来就已经分开处理了（显示 `cloned tag 504 B`），只有串口没跟上。

### 更早那批（原定 1.2.1，也没单独发过包）

**重启后克隆卡会丢。** 存档本来就在 NVS 里，但开机没人去读它，拔插一次 USB 就得重新贴卡。现在用 `cur` 记录当前索引（-1 = 内置 NDEF），开机 `cloneRestore()` 先 `libLoad()`，`cur >= 0` 就自动套用。实测 esptool 硬复位后 `clone stat` 直接是 `active=1`。

**`clone test` 会冲掉真卡存档。** 自检往 NVS 写的是合成卡 `04:DE:AD:...`，跑一次就把用户克隆的真卡顶掉。现在开头把整个卡库快照下来、结尾写回（含内存 UID 和模拟状态），末尾打 `restored=1 lib=1 cur=0` 让你确认。踩过的坑：这次实测确实被冲掉过一次，只能重新贴卡。

**按 `K` 克隆时误报 `no card`。** 屏幕上 900 ms 才轮询一次，卡刚贴上去那一刻 `card.present` 还是 0，直接判死就报错。当时改成 `doClone()` 先主动补扫一次、`cloneRead()` 内部重试 6 次；1.3.1 又进一步改成非阻塞等待态（见上）。

**进 NFC 屏扫不出卡。** 模拟会把芯片切到被动目标模式，之前只在轮询条件里加了 `!emuRunning()`，模拟开着时屏幕永远空白，看着像坏了。现在 `enterNfc()` 直接停掉模拟并弹一句 `emulation stopped`；串口 `nfc` 同样先让位。顺带修了扫到卡后停在 `NFC_FOUND` 不再轮询的问题 —— 卡拿走了屏幕还挂着旧信息。

**串口工具不再复位板子。** `tools/dev.py` 的 `open_port()` 在 `open()` **之前**就把 `_dtr_state` / `_rts_state` 置成 False。之前是在 `open()` 之后设，那时 DTR/RTS 已经拉高、ESP32 的自动复位电路已经把芯片重启了，所以每跑一条命令都看到一遍 boot log。

**`clone stat` 不再卡死固件。** 一次把 504 字节 hex 全打出来，主机没在读的话 USB CDC 缓冲会被灌满，固件卡在下一次 `Serial.write` 里 —— 屏幕冻结、按键无响应，只能按 RESET。现在超过 64 字节就截断，末尾标 `...(+440)`。

## 1.2.0 多了什么

**真卡克隆。** 把一张真标签贴在设备上读一遍，之后就能原样"冒充"它，不用再带原卡：

```bash
clone          # 读手上这张卡 → 存 NVS → 下次 SPACE 模拟它
clone run      # 读完立刻开始模拟
clone load     # 从 NVS 取回上次存的
clone off      # 回到内置 NDEF（存档保留）
clone stat     # 当前状态 + 用户区前 64 字节
clone test     # 合成卡自检
```

原理是**整片搬运**，不是解析 NDEF：Type 2 Tag 存储是扁平页，`NFCLayerA::read()` 免认证就能从 page 4 读到底，存原始字节、模拟时原样铺回内存。这样厂商自定义数据区也能一起带过来。

实测那张是 **NTAG 215**，UID `04:9F:54:AD:89:26:81`，504 字节，内容是 `03 10 D1 01 0C 55 02 "m5stack.com" FE` —— M5Stack 官方的 `https://www.m5stack.com` 卡。模拟时 UID 换成真卡的，手机读到的是同一张卡。

**卡模拟支持全系列 NTAG。** 不再写死 213：容量跟着类型算，CC 字节按 48 / 144 / 504 / 888 B 分别给 `0x06` / `0x12` / `0x3E` / `0x6D`。

**MIFARE Classic 克隆不了。** `begin()` 阶段就被库拒绝 —— 没有 Crypto1，连认证都过不去。命令会单独回一句 `MIFARE Classic not clonable`，不是"固件坏了"。

## 用 M5Launcher 安装（推荐，不动 Launcher 本体）

1. SD 卡格式化为 **FAT32**（256GB 的卡如果格式化不成 FAT32，换张小卡，exFAT 不认）
2. 把 `CapLab-1.5.1-app.bin` 拷进 SD 卡任意目录（根目录或 `/downloads` 都行）
3. 卡插回 Cardputer，开机进 M5Launcher
4. 主界面选 **SD** → 找到 `CapLab-1.5.1-app.bin` → **Install**
5. 装完自动重启进入 CAP LAB

在文件菜单里还能顺手按 **Bind to key**，绑到某个字母键，以后在主界面按一下就切过来，不用每次翻 SD。

> **为什么给的是 app-only 而不是合并镜像**
> Launcher 装 app 时只往 app 分区（0x10000）写，分区表沿用 Launcher 自己的。
> app 镜像 1,220 KB，Launcher 的 OTA app 分区装得下。
> 合并镜像也能被 Launcher 识别（它会自己拆出 app 段），但多绕一层没必要。
> 注意反过来不成立：M5Burner 从 `0x0` 写整片时**必须**用 `full.bin`（见「发布到 LauncherHub」）。

## 用 esptool 整片烧（会盖掉 M5Launcher）

```bash
esptool.py --chip esp32s3 -p COM7 -b 921600 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 CapLab-1.5.1-full.bin
```

只写 app、保留现有引导区：

```bash
esptool.py --chip esp32s3 -p COM7 -b 921600 \
  write_flash --flash_size detect 0x10000 CapLab-1.5.1-app.bin
```

## 用 PlatformIO 从源码烧（开发用）

```bash
cd firmware
python -m platformio run -j 1 -t upload --upload-port COM7
```

## 回到 M5Launcher

本包不含 Launcher 本体。想回去就用 M5Burner 搜 `M5Launcher` 刷一遍，
或者把当初的 Launcher bin 用上面的 esptool 命令烧到 `0x0`。

## 启动时如果看到 `esp_core_dump_flash: Incorrect size`

那是 coredump 分区（`0x3F0000`，64 KB）里残留了别的固件的数据，不影响使用。想消掉它：

```bash
esptool.py --chip esp32s3 -p COM7 erase_region 0x3F0000 0x10000
```

## 分区与 NVS

| 项 | 值 |
|---|---|
| Flash | 8 MB（ESP32-S3**FN8**，无 PSRAM） |
| 本包分区表 | `huge_app.csv`：nvs 0x9000 / app 0x10000 3 MB / coredump 0x3F0000 |
| Launcher 分区表 | nvs 同样是 0x9000 |

两边的 nvs 都从 `0x9000` 开始，所以频段、TX 授权、上次采集的波形、**以及克隆卡存档**在两种安装方式之间都能互相带过去，不会因为换安装方式丢设置。

克隆卡存在 `caplab` 命名空间：`tag` 是整份 `Tag` 结构（约 640 B，含 512 B 用户区），`on` 记录是否处于克隆模式。
