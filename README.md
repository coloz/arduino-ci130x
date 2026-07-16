# Arduino core for ChipIntelli CI13XX

面向启英泰伦 CI13XX 系列语音芯片的 Arduino Core，当前基于
`CI13XX_SDK_ASR_ALG_V2.7.12`。项目沿用 Arduino 平台熟悉的目录、API 和示例组织方式，
同时保留原厂 SDK 已验证的启动流程、FreeRTOS、双核通信与离线语音任务。
Arduino 的 `setup()` 和 `loop()` 作为低优先级 FreeRTOS 任务接入原 SDK 调度器。

> [!IMPORTANT]
> 当前项目仍处于开发预览阶段。源码编译、链接、双核镜像后处理和本地
> Boards Manager 安装流程已经验证，但尚未完成 CI1302、CI1303、CI1306 的
> 实体板烧录与硬件 I/O/语音回归测试。请勿直接用于量产固件。

## 目录

- [开发状态](#开发状态)
- [文档](#文档)
- [安装](#安装)
- [快速开始](#快速开始)
- [支持的芯片](#支持的芯片)
- [Arduino API 与库](#arduino-api-与库)
- [示例](#示例)
- [重要限制](#重要限制)
- [验证状态](#验证状态)
- [问题反馈](#问题反馈)
- [参与贡献](#参与贡献)
- [许可证](#许可证)

## 开发状态

| 项目 | 状态 |
| --- | --- |
| 当前开发版本 | `0.0.1` |
| Arduino IDE | Arduino IDE 2.x |
| Arduino CLI | 已使用 1.3.1 验证 |
| 主机系统 | Windows 10/11 x64 |
| 编译器 | Nuclei RISC-V GCC 9.2.0（`rv32imafc / ilp32f`） |
| 算法配置 | `USE_NULL=1` 基础离线 ASR profile |
| 公共 Boards Manager 发布 | 尚未发布；当前使用本地安装流程 |
| 硬件运行验证 | 尚未完成 |

当前版本生成双核 `user_code.bin` 容器，并通过原厂 `code_program.exe` 更新
user-code 分区。它不是全部 CI13XX 型号、开发板和算法组合的通用实现。

## 文档

- [启英泰伦官方文档](https://document.chipintelli.com/)
- [Boards Manager 打包与发布说明](package/README.md)
- [Boards Manager 验证记录](package/VALIDATION.md)
- [Wire / I2C 说明](libraries/Wire/README.md)
- [SPI 说明](libraries/SPI/README.md)
- [EEPROM 说明](libraries/EEPROM/README.md)
- [离线语音识别结果接口](libraries/ChipIntelliASR/README.md)
- [提示音播放接口](libraries/ChipIntelliAudio/README.md)

## 安装

### 使用本地 Boards Manager 仓库

当前公共 Release 资产尚未发布。可在 Windows PowerShell 中生成并启动本地
Boards Manager 仓库：

```powershell
git clone https://github.com/coloz/arduino-chipintelli.git
cd arduino-chipintelli

.\package\build_package.ps1 `
  -ToolchainRoot C:\path\to\riscv-nuclei-elf-gcc-9.2.0

.\package\serve_package.ps1
```

在 **Arduino IDE > 文件 > 首选项 > 其他开发板管理器地址** 中添加：

```text
http://127.0.0.1:8765/package_chipintelli_index.json
```

随后打开开发板管理器，搜索并安装 **ChipIntelli CI13XX Arduino**。安装期间需保持
本地服务器运行；安装完成后可关闭服务器。

> [!NOTE]
> `package/package_chipintelli_index.json` 只有在对应 Release ZIP 已发布且 URL、
> 大小和 SHA-256 均匹配时才能作为公共索引使用。不要把指向本机地址或未发布
> Release 的索引当作可公开安装的版本。

### 从源码开发

将本仓库放到 Arduino sketchbook 的
`hardware/chipintelli/ci13xx` 目录，复制
`platform.local.txt.example` 为 `platform.local.txt`，并填写本机编译器路径。

如果需要从原厂 SDK 重新生成各芯片的静态库：

```powershell
.\tools\rebuild_sdk.ps1 -Variant ci1306 `
  -SdkPath ..\CI13XX_SDK_ASR_ALG_V2.7.12 `
  -ToolchainBin C:\path\to\gcc_fix_raissrc\bin

.\tools\rebuild_sdk.ps1 -Variant ci1302 `
  -SdkPath ..\CI13XX_SDK_ASR_ALG_V2.7.12 `
  -ToolchainBin C:\path\to\gcc_fix_raissrc\bin

.\tools\rebuild_sdk.ps1 -Variant ci1303 `
  -SdkPath ..\CI13XX_SDK_ASR_ALG_V2.7.12 `
  -ToolchainBin C:\path\to\gcc_fix_raissrc\bin
```

每个 variant 的 SDK 静态库与芯片和算法 profile 严格绑定，不能交叉复用。

## 快速开始

1. 在 Arduino IDE 中选择对应开发板：
   **ChipIntelli CI1302**、**ChipIntelli CI1303** 或 **ChipIntelli CI1306**。
2. 打开 **文件 > 示例 > CI13XX > GPIO > Blink**。
3. 按照开发板原理图修改 LED 引脚。
4. 先执行验证/编译，再按[首次固件要求](#固件与首次烧录)准备开发板。

Arduino CLI 编译示例：

```powershell
arduino-cli compile --fqbn chipintelli:ci13xx:ci1306 `
  examples\CI13XXSmoke

arduino-cli compile --fqbn chipintelli:ci13xx:ci1302 `
  examples\CI13XXSmoke

arduino-cli compile --fqbn chipintelli:ci13xx:ci1303 `
  examples\CI13XXSmoke
```

## 支持的芯片

| 芯片 | 参考板卡 / 模组 | 封装与 Flash | FQBN | 当前验证 |
| --- | --- | --- | --- | --- |
| CI1302 | CI-D02GS02S | SSOP24 / 2 MB | `chipintelli:ci13xx:ci1302` | 编译、链接、后处理通过 |
| CI1303 | CI-D03GS02S | SSOP24 / 4 MB | `chipintelli:ci13xx:ci1303` | 编译、链接、后处理通过 |
| CI1306 | CI-D06GT01D | QFN40 / 4 MB | `chipintelli:ci13xx:ci1306` | 编译、链接、后处理通过 |

开发板或模组是否实际引出某个 PAD，应以对应硬件原理图为准。

## Arduino API 与库

| 类别 | API / 库 | 当前范围 |
| --- | --- | --- |
| Arduino 基础 | `setup()`、`loop()`、`String`、`Print`、`Stream`、`IPAddress` | C++17，无异常和 RTTI |
| 时间 | `millis()`、`micros()`、`delay()`、`delayMicroseconds()`、`yield()` | `micros()` 读取 64 位 mtime |
| GPIO | `pinMode()`、`digitalRead()`、`digitalWrite()`、`digitalToggle()` | PA0/PA1 为外部晶振保留 |
| 中断 | `attachInterrupt()`、`attachInterruptArg()`、`detachInterrupt()` | PA/PB/PC 支持；PD 不支持 GPIO IRQ |
| ADC | `analogRead()`、读取分辨率 | 12 位；CI1302/1303 为 AIN2，CI1306 为 AIN2–AIN5 |
| PWM / Tone | `analogWrite()`、写分辨率/频率、`tone()`、`noTone()` | 6 个硬件通道 |
| 串口 | `Serial`、`Serial1`、`Serial2` | polling、8N1、原厂支持的固定波特率 |
| I2C | [`Wire`](libraries/Wire/README.md) | IIC0 master、32 B、10–400 kHz、支持 repeated start |
| SPI | [`SPI`](libraries/SPI/README.md)、`SPISettings` | GPIO software master、模式 0–3、MSB/LSB，最高 500 kHz |
| 持久化 | [`EEPROM`](libraries/EEPROM/README.md) | 基于 NVDM，单实例 1–240 B，需要 `commit()` |
| 语音识别 | [`ChipIntelliASR`](libraries/ChipIntelliASR/README.md) | 命令 ID、语义 ID、得分、帧数、文本队列与回调 |
| 提示音 | [`ChipIntelliAudio`](libraries/ChipIntelliAudio/README.md) | 播放 `voice.bin` 中已有的提示音，支持队列、停止、音量和完成回调 |
| 兼容接口 | `pulseIn()`、`shiftIn()`、`shiftOut()`、`random()`、`map()`、`PROGMEM` | 软件实现或统一地址空间兼容 |

## 示例

Arduino IDE 的 **文件 > 示例** 菜单中包含：

| 菜单 | 示例 | 用途 |
| --- | --- | --- |
| `CI13XX > GPIO` | `Blink`、`DigitalInputPullup`、`GPIOInterrupt` | GPIO 输出、上拉输入和外部中断 |
| `CI13XX > Analog` | `AnalogReadSerial`、`PWMFade` | 12 位 ADC 和硬件 PWM |
| `CI13XX > Serial` | `SerialEcho`、`Serial1Bridge` | UART0 回显和 UART0/UART1 桥接 |
| `SPI` | `SoftwareSPILoopback` | GPIO software SPI 回环 |
| `Wire` | `MasterWrite`、`RegisterRead` | IIC0 写入和 repeated-start 寄存器读取 |
| `EEPROM` | `PersistentCounter` | NVDM 持久化计数器 |
| `ChipIntelliASR` | `ASRResults` | 读取离线语音识别结果 |
| `ChipIntelliAudio` | `PlayVoiceId`、`PromptControl` | 播放与控制已配置提示音 |

`examples/CI13XXSmoke` 是平台综合回归 sketch。

## 重要限制

### 外设与资源冲突

- CI1302、CI1303、CI1306 均没有可供 Arduino 用户复用的通用硬件 SPI；片内
  `QSPI0` 用于启动、模型和用户 Flash，因此 `SPI` 是 GPIO software SPI，
  不支持 DMA、硬件片选或从机模式。
- `Wire` 只支持 IIC0 master。项目不提供传统 I2C Scanner，以避免原厂 polling
  API 对未知设备执行不安全的探测写入。
- `Serial` 使用 SDK 日志口 UART0（PB5/PB6），默认日志波特率为 921600；
  `Serial.begin()` 会重新初始化该端口。
- `Wire` 与 `Serial1` 共用 PAD：CI1302/CI1303 为 PA2/PA3，CI1306 为 PB7/PC0，
  不能同时使用。
- `Serial2` 默认用于 SDK 语音模块协议：CI1302/CI1303 为 PA5/PA6，CI1306 为
  PB1/PB2。重新初始化会接管该协议端口。
- software SPI 默认使用 `SCK=PA5`、`MISO=PA2`、`MOSI=PA4`、`SS=PA3`。
  PA4 同时是复位阶段的 `PG_EN` 检测脚，外设在复位期间不得主动驱动它。
- CI-D06GT01D 的 PD0 默认连接功放控制，改作普通 GPIO 会影响音频播放。
- `ChipIntelliAudio` 只能播放完整固件 `voice.bin` 中已经配置的提示音，不能读取
  任意 WAV/MP3 文件，也不会把文本实时转换为语音。
- 当前基线不提供 Wi-Fi、SD 卡或通用文件系统。

### 内存报告

代码、只读数据、读写数据、BSS、栈和 100 KiB FreeRTOS heap 共用一段
`0x82000`（532480 B）host SRAM。Arduino CLI 报告的 program 与 dynamic memory
包含重叠的 `.data`，不能当作两块可分别用满的内存；最终容量以链接器和双镜像
后处理检查为准。

## 验证状态

当前验证基线：

- 原厂 SDK 示例的 138 个源文件使用 GCC 9.2.0 完整构建通过；
- CI1306 的 GPIO、中断、ADC、PWM、Serial、software SPI、Wire、EEPROM、ASR
  和综合冒烟等 14 个 sketch 已完成编译、链接与双镜像后处理；
- CI1302 与 CI1303 各完成 `CI13XXSmoke`、`PWMFade`、`Blink`、
  `DigitalInputPullup`、`GPIOInterrupt` 的编译、链接与后处理；
- 本地 Boards Manager 仓库已在隔离 Arduino CLI 环境完成安装和 14/14 示例
  编译验证；
- 尚未完成公共 v0.0.1 Release 的全新安装回归，也尚未完成实体板上传、GPIO、
  音频和离线语音运行验证。

详细环境、步骤与已知工具链问题见 [package/VALIDATION.md](package/VALIDATION.md)。

## 问题反馈

提交问题前请先搜索已有 [Issues](https://github.com/coloz/arduino-chipintelli/issues)。
新问题请至少包含：

- 平台版本和 Arduino IDE / CLI 版本；
- 芯片、开发板和完整 FQBN；
- 使用的 SDK 与算法 profile；
- 可复现的最小 sketch；
- 完整编译、后处理或上传日志；
- 如涉及上传，开发板当前完整固件和 User 分区布局。

## 参与贡献

欢迎提交 Issue 和 Pull Request。涉及 API、variant、库或构建流程的修改应：

1. 保持原厂 SDK 启动、双核和语音任务边界；
2. 为受影响的芯片 profile 提供最小示例或回归 sketch；
3. 完成编译、链接和 `user_code.bin` 后处理验证；
4. 同步更新 README、库说明和 Boards Manager 元数据；
5. 明确区分编译验证、实体板验证和量产验证。

## 许可证

Arduino 兼容 core 中继承的代码按 [LGPL-2.1 或更高版本]提供。  
启英泰伦 SDK、算法库、第二核镜像、编译器和 Windows 工具仍受各自许可与再分发条款约束。  
