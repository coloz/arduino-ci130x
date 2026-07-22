# Arduino core for ChipIntelli CI130X

面向启英泰伦 CI13XX 系列语音芯片的 Arduino Core，当前基于
`CI13XX_SDK_ASR_ALG_V2.7.12`。项目沿用 Arduino 平台熟悉的目录、API 和示例组织方式，
同时保留原厂 SDK 已验证的启动流程、FreeRTOS、双核通信与离线语音任务。
Arduino 的 `setup()` 和 `loop()` 作为低优先级 FreeRTOS 任务接入原 SDK 调度器。

> [!IMPORTANT]
> 当前项目仍处于开发预览阶段。源码编译、链接、双核镜像后处理和本地
> Boards Manager 安装流程已经验证；CI1303 已完成实体烧录以及 I2C/SSD1306
> 运行测试，CI1302、CI1306 和音频/离线语音仍待实体回归。请勿直接用于量产固件。

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
| 当前开发版本 | `1.0.1` |
| Arduino IDE | Arduino IDE 2.x |
| Arduino CLI | 已使用 1.3.1 验证 |
| 主机系统 | Windows 10/11 x64 |
| 编译器 | Nuclei RISC-V GCC 9.2.0（`rv32imafc / ilp32f`） |
| 算法配置 | `USE_NULL=1` 基础离线 ASR profile |
| 公共 Boards Manager 发布 | `v1.0.1`（Windows x64） |
| 硬件运行验证 | CI1303：串口烧录、UART0、I2C/SSD1306 已通过；其余待验证 |

当前版本在编译前检查 sketch 根目录的 `recursos/`；缺少 `asr.bin`、`dnn.bin`、
`voice.bin` 或 `user_file.bin` 时，仅从 Arduino package 补齐缺失项，不覆盖项目文件。
原厂示例中可获得源码的 138 个编译单元随平台发布，并在 Arduino 首次构建时并行
编译为非 LTO `.o` 后直接链接；同一构建目录后续编译会复用缓存，不再使用
`libci13xx_sdk.a`。原厂 SDK 未提供源码的 ASR、TTS、BLE、FreeRTOS port、DSU 等
组件仍保留为二进制 `.a`。
链接完成后先生成双核 `user_code.bin`，再由 `citool-cli compose` 合成完整固件并执行
`inspect`；Arduino 上传阶段使用 `citool-cli flash` 从 Flash 地址 0 烧录该完整固件。
`citool-cli` 内置 CI130X FW_V2 Bootloader，合成时不再依赖完整固件模板。
它不是全部 CI13XX 型号、开发板和算法组合的通用实现。

## 文档

- [启英泰伦官方文档](https://document.chipintelli.com/)
- [Boards Manager 打包与发布说明](package/README.md)
- [Boards Manager 验证记录](package/VALIDATION.md)
- [Wire / I2C 说明](libraries/Wire/README.md)
- [SPI 说明](libraries/SPI/README.md)
- [Servo 说明](libraries/Servo/README.md)
- [EEPROM 说明](libraries/EEPROM/README.md)
- [离线语音识别结果接口](libraries/ChipIntelliASR/README.md)
- [提示音播放接口](libraries/ChipIntelliAudio/README.md)

## 安装

在 **Arduino IDE > 文件 > 首选项 > 其他开发板管理器地址** 中添加：

```text
https://raw.githubusercontent.com/coloz/arduino-ci130x/main/package/package_chipintelli_index.json
```

随后打开开发板管理器，搜索并安装 **ChipIntelli CI130X Arduino**。当前发布包仅
提供 Windows x64 编译器和 `citool-cli` 烧录工具。

固定版本的索引也随 GitHub Release 发布：

```text
https://github.com/coloz/arduino-ci130x/releases/download/v1.0.1/package_chipintelli_index.json
```

## 快速开始

1. 在 Arduino IDE 中选择对应开发板：
   **ChipIntelli CI1302**、**ChipIntelli CI1303** 或 **ChipIntelli CI1306**。
2. CI1302/CI1303 默认选择 **Internal RC (no crystal)**；只有板上确实安装
   12.288 MHz 晶振时才选择 **External 12.288 MHz crystal**。
3. PA4 接有 LED 时可打开 **文件 > 示例 > CI13XX > GPIO > PA4BlinkSerial**，
   并以 115200 波特率观察 UART0；其他接线可使用 **Blink** 并修改 LED 引脚。
4. 按照开发板原理图确认 LED 极性与限流电阻。
5. 执行验证/编译；平台会准备默认资源并生成经过校验的完整固件。

Arduino CLI 编译示例：

```powershell
arduino-cli compile --fqbn chipintelli:ci13xx:ci1306 `
  examples\CI13XXSmoke

arduino-cli compile --fqbn chipintelli:ci13xx:ci1302 `
  examples\CI13XXSmoke

arduino-cli compile --fqbn chipintelli:ci13xx:ci1303 `
  examples\CI13XXSmoke
```

第一次编译会在 sketch 根目录创建 `recursos/`，并复制 package 默认的四个分区文件。
需要定制模型、播报音或用户文件时，直接替换对应文件；后续编译不会覆盖它们。构建目录
同时保留 `<sketch>.user_code.bin` 和最终的 `<sketch>.bin`，Arduino IDE 导出的
`.firmware.bin` 是可直接烧录的完整固件。

## 支持的芯片

| 芯片 | 参考板卡 / 模组 | 封装与 Flash | FQBN | 当前验证 |
| --- | --- | --- | --- | --- |
| CI1302 | CI-D02GS02S | SSOP24 / 2 MB | `chipintelli:ci13xx:ci1302` | 编译、链接、后处理通过 |
| CI1303 | CI-D03GS02S | SSOP24 / 4 MB | `chipintelli:ci13xx:ci1303` | 编译、烧录、UART0 与 I2C/SSD1306 运行通过 |
| CI1306 | CI-D06GT01D | QFN40 / 4 MB | `chipintelli:ci13xx:ci1306` | 编译、链接、后处理通过 |

开发板或模组是否实际引出某个 PAD，应以对应硬件原理图为准。

## Arduino API 与库

| 类别 | API / 库 | 当前范围 |
| --- | --- | --- |
| Arduino 基础 | `setup()`、`loop()`、`String`、`Print`、`Stream`、`IPAddress` | C++17，无异常和 RTTI |
| 时间 | `millis()`、`micros()`、`delay()`、`delayMicroseconds()`、`yield()` | `micros()` 读取 64 位 mtime |
| GPIO | `pinMode()`、`digitalRead()`、`digitalWrite()`、`digitalToggle()` | 使用外部晶振时 PA0/PA1 不可用 |
| 中断 | `attachInterrupt()`、`attachInterruptArg()`、`detachInterrupt()` | PA/PB/PC 支持；PD 不支持 GPIO IRQ |
| ADC | `analogRead()`、读取分辨率 | 12 位；CI1302/1303 为 AIN2，CI1306 为 AIN2–AIN5 |
| PWM / Tone | `analogWrite()`、写分辨率/频率、`tone()`、`noTone()` | 6 个硬件通道 |
| 舵机 | [`Servo`](libraries/Servo/README.md) | 50 Hz 硬件 PWM、角度/微秒接口；CI1302/CI1303 最多 5 个通道，CI1306 最多 6 个通道 |
| 串口 | `Serial`、`Serial1`、`Serial2` | polling、8N1、原厂支持的固定波特率 |
| I2C | [`Wire`](libraries/Wire/README.md) | IIC0 master、32 B、10–400 kHz、支持安全地址探测和 repeated start |
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
| `Servo` | `Sweep` | 硬件 PWM 舵机角度扫描 |
| `Wire` | `MasterWrite`、`RegisterRead`、`Scanner` | IIC0 写入、repeated-start 寄存器读取和安全地址扫描 |
| `EEPROM` | `PersistentCounter` | NVDM 持久化计数器 |
| `ChipIntelliASR` | `ASRResults` | 读取离线语音识别结果 |
| `ChipIntelliAudio` | `PlayVoiceId`、`PromptControl` | 播放与控制已配置提示音 |

`examples/CI13XXSmoke` 是平台综合回归 sketch。

## 重要限制

### 外设与资源冲突

- CI1302/CI1303 默认使用内部 RC，系统主频为 200 MHz。选择外部 12.288 MHz
  晶振后主频为 246 MHz，且 PA0/PA1 被晶振占用。无晶振硬件若误选外部时钟，
  SDK 会在进入 Arduino `setup()` 前失去有效时钟，GPIO 和 UART 都不会运行。
- CI1302、CI1303、CI1306 均没有可供 Arduino 用户复用的通用硬件 SPI；片内
  `QSPI0` 用于启动、模型和用户 Flash，因此 `SPI` 是 GPIO software SPI，
  不支持 DMA、硬件片选或从机模式。
- `Servo`、`analogWrite()` 和 `tone()` 共用 PWM0–PWM5。Servo 只能连接具有
  PWM 能力的引脚；同一硬件通道不能被多个功能同时占用。
- CI-D03GS02S（CI1303）的原厂板级初始化使用 PC4 控制功放使能；当前 CI1303
  variant 同时将该管脚公开为 Arduino pin 20、`A0` 和 PC4 上的 PWM0 输出。
  使用原厂模块及音频基线时，不要把 pin 20 用作普通 GPIO、`analogRead(A0)`、
  `analogWrite()` 或 Servo，否则会改写功放控制状态并影响音频播放。只有确认
  自定义硬件未连接该功放控制电路且固件已释放 PC4 后，才能复用该管脚。
- `Wire` 只支持 IIC0 master。`Wire.probe()` 及空数据的 `endTransmission()` 使用
  专用地址探测事务：只发送 START 和地址、读取 ACK/NACK，并在 START 后的所有
  完成及错误路径发送 STOP；不会用可能改写未知设备寄存器的虚拟数据字节。
- `Serial` 使用 SDK 日志口 UART0（PB5/PB6），默认日志波特率为 921600；
  `Serial.begin()` 会重新初始化该端口。
- `HardwareSerial` 当前仅提供 polling 方式的 8N1；`Serial.begin(baud, config)`
  的 `config` 参数尚未生效。波特率必须使用原厂驱动支持的固定值，其他值会
  静默回退到 115200。
- `Wire` 与 `Serial1` 共用 PAD：CI1302/CI1303 为 PA2/PA3，CI1306 为 PB7/PC0，
  不能同时使用。当前 SDK profile 还将 UART1 TX 配置为开漏输出，使用
  `Serial1` 时必须提供与目标电平匹配的外部上拉电阻。
- `Serial2` 不会在 SDK 启动阶段自动占用 PAD。调用 `Serial2.begin()` 后才启用
  UART2 并切换复用功能；调用 `Serial2.end()` 后关闭 UART2，并将 TX/RX 释放为
  GPIO 输入。CI1302/CI1303 为 PA5/PA6，CI1306 为 PB1/PB2。
- software SPI 默认使用 `SCK=PA5`、`MISO=PA2`、`MOSI=PA4`、`SS=PA3`。
  PA4 同时是复位阶段的 `PG_EN` 检测脚，外设在复位期间不得主动驱动它。
- CI-D06GT01D 的 PD0 默认连接功放控制，改作普通 GPIO 会影响音频播放。
- `ChipIntelliAudio` 只能播放完整固件 `voice.bin` 中已经配置的提示音，不能读取
  任意 WAV/MP3 文件，也不会把文本实时转换为语音。
- 当前基线不提供 Wi-Fi、SD 卡或通用文件系统。

### 内存报告

代码、只读数据、读写数据、BSS、栈和两类运行时 heap 共用一段 `0x82000`
（532480 B）host SRAM。平台不再把最终 `user_code.bin` 固定限制为 `0x70000`：

- 链接器先为原厂 SDK 保留 3 KiB 栈和固定 100 KiB FreeRTOS heap；
- 代码、BSS 与 C/newlib heap 在剩余 417 KiB 中动态分配；默认至少保留 16 KiB
  C/newlib heap，也可在开发板菜单中选择 32 KiB 或 64 KiB；
- 后处理从 ELF 符号核对实际 SRAM 布局和剩余 heap，再生成双核容器；
- `citool-cli compose` 按五个最终 bin 的实际大小做 4 KiB 对齐和顺序排布，只有
  超过当前资源组合的 User Flash 布局上限时才报错。

因此最终 User 容器上限由 sketch 的 BSS、所选 heap 余量和其他 Flash 分区共同决定，
不是一个固定常数。Arduino CLI 报告的 program 与 dynamic memory 包含重叠的
`.data`，也不能当作两块可分别用满的内存。普通 `malloc`、Arduino `String` 和部分
ASR/音频解码器使用 C/newlib heap；大型 sketch 应根据运行负载选择更高的 heap 余量。
这里的放宽适用于 `citool-cli` 完整固件烧录流程；原厂 SDK 旧 OTA 头文件仍定义
448 KiB User 上限，启用该 OTA 路径前必须另行验证协议和升级端兼容性。

## 验证状态

当前验证基线：

- 原厂 SDK 示例的 138 个源文件使用 GCC 9.2.0 完整构建通过，并已验证 Arduino
  源码预构建、缓存和直接对象链接；三个变体的链接映射均不再引用
  `libci13xx_sdk.a`；
- CI1306 的 GPIO、中断、ADC、PWM、Serial、software SPI、Wire、EEPROM、ASR、
  提示音和综合冒烟等 16 个 sketch 已完成编译、链接与双镜像后处理；
- CI1302 与 CI1303 各完成 `CI13XXSmoke`、`PWMFade`、`Blink`、
  `DigitalInputPullup`、`GPIOInterrupt` 的编译、链接与后处理；
- `v1.0.0` Boards Manager 发布包已在隔离 Arduino CLI 环境完成安装；CI1306 的
  16 个安装后示例以及 CI1302/CI1303 的综合冒烟示例均编译通过，共 18/18；
- CI1302、CI1303 与 CI1306 已在隔离 Arduino CLI 环境验证资源准备、完整编译、
  `compose` 和 `inspect`；CI1303 已使用 `citool-cli` 完成实体板上传、固件 CRC、
  UART0 和 I2C/SSD1306 运行验证，CI1302、CI1306、音频和离线语音仍待验证。

详细环境、步骤与已知工具链问题见 [package/VALIDATION.md](package/VALIDATION.md)。

## 问题反馈

提交问题前请先搜索已有 [Issues](https://github.com/coloz/arduino-ci130x/issues)。
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
