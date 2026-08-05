# Arduino core for ChipIntelli CI130X

面向启英泰伦 CI13XX 系列语音芯片的 Arduino Core，当前基于
`CI130X_SDK_ALG_V2.7.14`。项目沿用 Arduino 平台熟悉的目录、API 和示例组织方式，
同时保留原厂 SDK 已验证的启动流程、FreeRTOS、双核通信与离线语音任务。
Arduino 的 `setup()` 和 `loop()` 作为低优先级 FreeRTOS 任务接入原 SDK 调度器。

> [!IMPORTANT]
> 当前项目仍处于开发预览阶段。源码编译、链接、双核镜像后处理和本地
> Boards Manager 安装流程已经验证；CI1303 已完成 Standard/CWSL 启动、音频提示
> 回调、核心外设以及 I2C/SSD1306 实体测试。CI1302、CI1306 和受控说词识别仍待
> 实体回归；AEC/语音打断目前完成构建验证，仍待带参考回采硬件的声学实测。请勿
> 直接用于量产固件。

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
| 当前开发版本 | `1.0.6` |
| Arduino IDE | Arduino IDE 2.x |
| Arduino CLI | 已使用 1.5.0 验证 |
| 主机系统 | 完整固件流程：Windows 10/11 x64、macOS 15+ Apple Silicon；Linux x86_64 已通过编译/链接验证，完整回归待补充 |
| 编译器 | Nuclei RISC-V GCC 9.2.0（`rv32imafc / ilp32f`）；macOS 由官方 `nuclei_9.2_fixjalr_forhw` 源码构建 |
| 算法配置 | 默认标准离线 ASR + AEC/语音打断；可选无 AEC、CWSL+AEC 或 CWSL profile |
| 公共 Boards Manager 发布 | `v1.0.6`（Windows x64、Linux x86_64、macOS Apple Silicon） |
| 硬件运行验证 | CI1303：Windows/macOS Standard 启动和音频、CWSL 启动、核心外设、I2C/SSD1306 已通过；其余待验证 |

当前版本在编译前检查 sketch 根目录的 profile 资源；标准 ASR/AEC 使用
`recursos/*.bin`，CWSL/CWSL+AEC 使用 `recursos/cwsl/*.bin`。缺少分区时从
Arduino package 的对应 profile 补齐。
精确匹配 1.0.3 及更早版本已知不兼容 Standard 四文件哈希的完整集合会安全升级；
任何自定义、修改过或混合的资源集都不会被覆盖，并会给出兼容性警告。
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
- [CI13XX SDK UART 外设 API](https://document.chipintelli.com/软件开发/SDK/CI13XX芯片SDK/CI-SDK-ALG/CI13XX_SDK_ASR_ALG_V2.6.3/API参考/外设API/uart/)
- [CI13XX SDK IIC 外设 API](https://document.chipintelli.com/软件开发/SDK/CI13XX芯片SDK/CI-SDK-ALG/CI13XX_SDK_ASR_ALG_V2.6.3/API参考/外设API/iic/)
- [ArduinoCore-API](https://github.com/arduino/ArduinoCore-API)
- [Boards Manager 打包与发布说明](package/README.md)
- [Boards Manager 验证记录](package/VALIDATION.md)
- [Wire / I2C 说明](libraries/Wire/README.md)
- [SPI 说明](libraries/SPI/README.md)
- [Servo 说明](libraries/Servo/README.md)
- [EEPROM 说明](libraries/EEPROM/README.md)
- [Preferences 说明](libraries/Preferences/README.md)
- [硬件 Timer / Ticker 说明](libraries/ChipIntelliTimer/README.md)
- [Watchdog 说明](libraries/ChipIntelliWatchdog/README.md)
- [离线语音识别结果接口](libraries/ChipIntelliASR/README.md)
- [命令词自学习接口](libraries/ChipIntelliCWSL/README.md)
- [提示音播放接口](libraries/ChipIntelliAudio/README.md)
- [红外收发与空调码库](libraries/ChipIntelliIR/README.md)

## 安装

在 **Arduino IDE > 文件 > 首选项 > 其他开发板管理器地址** 中添加：

```text
https://raw.githubusercontent.com/coloz/arduino-ci130x/main/package/package_chipintelli_index.json
```

随后打开开发板管理器，搜索并安装 **ChipIntelli CI130X Arduino**。发布流程会附带
Windows x64、macOS Universal 和 Linux x86_64 的 `citool-cli`，以及 Windows x64、
Linux x86_64、macOS 15+ Apple Silicon 的 Nuclei GCC 9.2.0。macOS 编译器在实体
Apple Silicon Mac 上由 Nuclei 官方 `nuclei_9.2_fixjalr_forhw` 固定提交源码构建，
并验证版本、目标、20 组 multilib 以及 C/C++ 链接；目前不提供 Intel Mac 编译器。
Windows 使用 PowerShell 和厂家 `ci-tool-kit.exe`，Linux/macOS 使用等价的 Python
构建钩子以及逐字节兼容的双核容器生成器。macOS Apple Silicon 已完成 CI1303
内部晶振配置的编译、CH343 串口烧录和三段提示音实机验证；Linux GCC 已通过真实
CI1306 编译和链接，完整后处理与实体板回归仍待补充。

固定版本的索引也随 GitHub Release 发布：

```text
https://github.com/coloz/arduino-ci130x/releases/download/v1.0.6/package_chipintelli_index.json
```

## 快速开始

1. 在 Arduino IDE 中选择对应开发板：
   **ChipIntelli CI1302**、**ChipIntelli CI1303** 或 **ChipIntelli CI1306**。
2. 默认使用 **标准离线 ASR + AEC/语音打断**；需要命令词自学习时选择
   **命令词自学习 CWSL + AEC/语音打断**。兼容旧硬件或没有 AEC 参考回采线路时，
   可选择名称中标有“无 AEC”的 profile。该选项会一起切换编译宏、链接脚本和
   第二核算法镜像。
3. CI1302/CI1303 默认选择 **Internal RC (no crystal)**；只有板上确实安装
   12.288 MHz 晶振时才选择 **External 12.288 MHz crystal**。
4. PA4 接有 LED 时可打开 **文件 > 示例 > CI13XX > GPIO > PA4BlinkSerial**，
   并以 115200 波特率观察 UART0；其他接线可使用 **Blink** 并修改 LED 引脚。
5. 按照开发板原理图确认 LED 极性与限流电阻。
6. 执行验证/编译；平台会准备默认资源并生成经过校验的完整固件。

Arduino CLI 编译示例：

```powershell
arduino-cli compile --fqbn chipintelli:ci13xx:ci1306 `
  examples\CI13XXSmoke

arduino-cli compile --fqbn chipintelli:ci13xx:ci1302 `
  examples\CI13XXSmoke

arduino-cli compile --fqbn chipintelli:ci13xx:ci1303 `
  examples\CI13XXSmoke
```

第一次编译会在 sketch 根目录创建所选 profile 的资源目录：标准 ASR/AEC 为
`recursos/`，CWSL/CWSL+AEC 为 `recursos/cwsl/`，并复制该 profile 的四个分区文件
和 package-managed 清单。
需要定制模型、播报音或用户文件时，直接替换对应文件；检测到修改后，后续编译不会
覆盖它们。构建目录
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
| 定时任务 | [`ChipIntelliTimer`](libraries/ChipIntelliTimer/README.md)、`Ticker` | TIMER0–2 微秒级中断；TIMER3 保留给 BLE；软件定时器为 2 ms 分辨率 |
| 看门狗 | [`ChipIntelliWatchdog`](libraries/ChipIntelliWatchdog/README.md) | 官方 IWDG、毫秒配置、喂狗和全系统复位路由 |
| GPIO | `pinMode()`、`digitalRead()`、`digitalWrite()`、`digitalToggle()` | 使用外部晶振时 PA0/PA1 不可用 |
| 中断 | `attachInterrupt()`、`attachInterruptArg()`、`detachInterrupt()` | PA/PB/PC 支持；PD 不支持 GPIO IRQ |
| ADC | `analogRead()`、读取分辨率 | 12 位；CI1302/1303 为 AIN2，CI1306 为 AIN2–AIN5 |
| PWM / Tone | `analogWrite()`、写分辨率/频率、`tone()`、`noTone()` | 6 个硬件通道 |
| 舵机 | [`Servo`](libraries/Servo/README.md) | 50 Hz 硬件 PWM、角度/微秒接口；CI1302/CI1303 最多 5 个通道，CI1306 最多 6 个通道 |
| 串口 | `Serial`、`Serial1`、`Serial2` | 中断收发、128 B RX/TX 环形缓冲、5–8 数据位、奇偶校验和 1/1.5/2 停止位 |
| I2C | [`Wire`](libraries/Wire/README.md) | IIC0 controller/peripheral、64 B、10–400 kHz、repeated start、从机回调和 25 ms 可恢复超时 |
| SPI | [`SPI`](libraries/SPI/README.md)、`SPISettings` | GPIO software master、模式 0–3、MSB/LSB，最高 500 kHz |
| SD 卡 | [`SD`](libraries/SD/README.md)、`File` | software SPI、SD/SDHC、FAT16/FAT32、8.3 短文件名 |
| 持久化 | [`EEPROM`](libraries/EEPROM/README.md)、[`Preferences`](libraries/Preferences/README.md) | 基于 NVDM；EEPROM 缓冲提交，Preferences 提供 namespace/typed key-value |
| 语音识别 | [`ChipIntelliASR`](libraries/ChipIntelliASR/README.md) | OneButton 风格的启动/唤醒/超时/命令/语义事件、512 项处理表及 `tick()`；提示音由 sketch 决定，默认 AEC 和语音打断 |
| 命令词自学习 | [`ChipIntelliCWSL`](libraries/ChipIntelliCWSL/README.md) | 命令词/唤醒词学习、模板删除与计数、异步状态和识别事件 |
| 提示音 | [`ChipIntelliAudio`](libraries/ChipIntelliAudio/README.md) | 播放 `voice.bin` 中已有的提示音，支持队列、停止、音量、静音和完成回调 |
| 红外 | [`ChipIntelliIR`](libraries/ChipIntelliIR/README.md) | 38 kHz raw 收发、NEC、学习回放，以及官方 36 品牌空调码库和码组搜索 |
| 资源管理 | `PeripheralManager` | 原子申请引脚与 UART/IIC/SPI/PWM/Timer 资源，冲突查询和安全释放 |
| 兼容接口 | `pulseIn()`、`shiftIn()`、`shiftOut()`、`random()`、`map()`、`PROGMEM` | 软件实现或统一地址空间兼容 |

资源冲突不会再静默覆盖管脚复用。返回 `bool` 的 `Wire.begin()`、`SPI.begin()`、
`Servo.attach()` 应检查结果；`HardwareSerial::begin()` 保持 Arduino 官方的 `void`
签名，通过 `operator bool()` 和 `lastError()` 检查。详细冲突可以这样读取：

```cpp
#include <PeripheralManager.h>

PeripheralConflict conflict = PeripheralManager.lastConflict();
Serial.print(PeripheralManager.ownerName(conflict.currentOwner));
```

公共 `pinMode()` 对正在由外设占用的管脚不会改写复用寄存器。应先调用对应的
`end()`、`detach()` 或 `noTone()`；普通 GPIO 所有权可以被外设 `begin()` 安全接管。

## 示例

Arduino IDE 的 **文件 > 示例** 菜单中包含：

| 菜单 | 示例 | 用途 |
| --- | --- | --- |
| `CI13XX > GPIO` | `Blink`、`DigitalInputPullup`、`GPIOInterrupt` | GPIO 输出、上拉输入和外部中断 |
| `CI13XX > Analog` | `AnalogReadSerial`、`PWMFade` | 12 位 ADC 和硬件 PWM |
| `CI13XX > Serial` | `SerialEcho`、`Serial1Bridge`、`BufferedSerial` | UART 回显、桥接、帧格式和错误计数 |
| `CI13XX > Peripherals` | `ResourceOwnership` | 检测 Wire/Serial1 引脚复用冲突并安全切换 |
| `SPI` | `SoftwareSPILoopback` | GPIO software SPI 回环 |
| `SD` | `CardInfo`、`ReadWrite`、`Datalogger` 等 | SD 卡诊断、FAT 文件与目录读写 |
| `Servo` | `Sweep` | 硬件 PWM 舵机角度扫描 |
| `Wire` | `MasterWrite`、`RegisterRead`、`Scanner`、`PeripheralCallbacks` | IIC0 主机传输、地址扫描、超时和从机回调 |
| `EEPROM` | `PersistentCounter` | NVDM 持久化计数器 |
| `Preferences` | `StartCounter` | namespace、typed key-value 与立即持久化 |
| `ChipIntelliTimer` | `HardwareTimer`、`Ticker` | 硬件中断定时和 FreeRTOS 软件定时任务 |
| `ChipIntelliWatchdog` | `BasicWatchdog` | 配置、喂养并测试芯片级 IWDG |
| `ChipIntelliASR` | `SimpleCommandPlayback`、`ASRResults`、`WakeCommandWindow`、`BargeIn` | 用生命周期/命令/语义回调和 `tick()` 处理事件，由 sketch 异步播放提示音，实现连续命令窗口，并测试播报中语音打断 |
| `ChipIntelliCWSL` | `BasicLearning`、`SerialLearning` | 最小命令词学习，以及通过串口学习/删除命令词和唤醒词并观察异步事件 |
| `ChipIntelliAudio` | `PlayVoiceId`、`PromptControl` | 播放与控制已配置提示音 |
| `ChipIntelliIR` | `RawSendReceive`、`AirConditioner` | 原始波形/NEC 收发，以及使用官方码库控制和匹配空调 |

`examples/CI13XXSmoke` 是平台综合回归 sketch。

## 重要限制

### 算法 Profile、AEC 与 CWSL

- 默认 **标准离线 ASR + AEC/语音打断** 使用 `USE_AEC_MODULE=1`、
  `AEC_INTERRUPT_TYPE=2`、AEC 专用链接脚本和第二核镜像；播报时保留麦克风输入和
  ASR 任务，唤醒词及普通命令词均可打断。**CWSL+AEC** 在此基础上增加
  `USE_CWSL=1`。两个名称中标有“无 AEC”的兼容 profile 分别使用原 NULL/CWSL
  镜像，播报时会静音录音并暂停 ASR，因而不支持语音打断。
- AEC 是单麦 + 一路参考信号算法：内部 Codec 左通道接麦克风，右通道必须接功放前
  的线电平播放参考。不能把功放或扬声器输出直接送入 Codec。参考通道未连接、削顶、
  极性/增益或延时不合适时，固件虽然显示 AEC 已启用，实际回声消除和打断可靠性仍会
  很差；具体接线和衰减应以模块原理图及硬件设计为准。
- 四个 profile 的分区均来自 V2.7.14 官方 `offline_asr_alg_pro_sample`；AEC 与无 AEC
  标准档共用 Standard ASR/DNN/Voice/UserFile，CWSL+AEC 与 CWSL 共用 CWSL 资源。
  这是编译期选择，同一固件不能在运行时切换；非 CWSL profile 中
  `ChipIntelliCWSL.begin()` 会返回 `false`。CWSL `cmd_info` 和
  提示音同时保留原厂 ID 199～208 的语音控制流程；Arduino API 可直接发起操作，
  无需依赖这些控制词。为避免程序操作与语音控制互相触发，程序学习接口拒绝将
  ID 199～208 作为目标命令。
- `learnCommand()` / `learnWakeWord()` 接收的 command ID 必须已存在于当前
  `cmd_info` 资源中，并分别匹配普通命令/唤醒词属性，且不得超过 65535；group ID
  不得超过 255。自学习只建立新语音模板到已有命令的映射，不会创建命令元数据或
  显示文本；ID 不存在、类型不匹配或越界时均返回 `false`。
- 模板由原厂 NVDATA 管理并在复位后保留。当前 profile 预留 16 个模板；普通
  command/group 只允许一个模板，而官方支持两个唤醒词模板同时映射到 ID 1、
  group 0。此 SDK 的 CWSL 不支持 `MULT_INTENT > 1`。
- Arduino 接口采用异步事件，不依赖原厂语音引导提示，可由 sketch 使用串口、LED、
  显示屏或 `ChipIntelliAudio` 提供反馈。回调运行在 SDK task 中，不能阻塞；耗时
  工作应在 `loop()` 中从事件队列读取后处理。所有 CWSL API 只能从 task 上下文
  调用，不可从 ISR 或硬件定时器回调直接调用。
- 原厂 record-end 回调没有会话 ID 或最终排空确认，因此 `cancelLearning()` 只在
  `CWSLLearningStarted` 前受理。录音请求入队后的强制休眠/复位、官方语音流程在
  record-end 回调内退出，或默认命令冲突触发原厂双 end 路径时，本次开机会保守地
  禁止后续学习；识别和删除仍可用，重启 MCU 后恢复学习。该隔离避免旧 type6
  回调落入新的学习任务。

### 外设与资源冲突

- CI1302/CI1303 默认使用内部 RC，系统主频为 200 MHz。选择外部 12.288 MHz
  晶振后主频为 246 MHz，且 PA0/PA1 被晶振占用。无晶振硬件若误选外部时钟，
  SDK 会在进入 Arduino `setup()` 前失去有效时钟，GPIO 和 UART 都不会运行。
- CI1302、CI1303、CI1306 均没有可供 Arduino 用户复用的通用硬件 SPI；片内
  `QSPI0` 用于启动、模型和用户 Flash，因此 `SPI` 是 GPIO software SPI，
  不支持 DMA、硬件片选或从机模式。
- `Servo`、`analogWrite()` 和 `tone()` 共用 PWM0–PWM5。资源管理器会拒绝同一
  通道或管脚的并发占用；释放原功能后才能切换。Servo 只能连接具有 PWM
  能力的引脚。
- CI-D03GS02S（CI1303）的原厂板级初始化使用 PC4 控制功放使能；当前 CI1303
  variant 同时将该管脚公开为 Arduino pin 20、`A0` 和 PC4 上的 PWM0 输出。
  使用原厂模块及音频基线时，不要把 pin 20 用作普通 GPIO、`analogRead(A0)`、
  `analogWrite()` 或 Servo，否则会改写功放控制状态并影响音频播放。只有确认
  自定义硬件未连接该功放控制电路且固件已释放 PC4 后，才能复用该管脚。
- `Wire` 使用唯一的 IIC0，可选择 controller/master 或 peripheral/slave 模式。
  `Wire.probe()` 及空数据的 `endTransmission()` 使用
  专用地址探测事务：只发送 START 和地址、读取 ACK/NACK，并在 START 后的所有
  完成及错误路径发送 STOP；不会用可能改写未知设备寄存器的虚拟数据字节。
- `Serial` 使用 SDK 日志口 UART0（PB5/PB6），默认日志波特率为 921600；
  `Serial.begin()` 会重新初始化该端口。
- `HardwareSerial` 使用 128 B 可配置 RX/TX 环形缓冲和 UART 中断。帧格式支持
  5–8 数据位、无/奇/偶校验及 1/1.5/2 停止位；CI130X 不支持 mark/space
  校验。波特率必须是原厂驱动列出的固定值；不支持的参数会保留原端口状态并
  通过 `lastError()` 报错，不再静默回退。
- `Wire` 与 `Serial1` 共用 PAD：CI1302/CI1303 为 PA2/PA3，CI1306 为 PB7/PC0。
  后初始化者会在改写复用寄存器前失败，可通过 `PeripheralManager.lastConflict()`
  查询占用者；先调用当前外设的 `end()` 才能安全切换。当前 SDK profile 还将
  UART1 TX 配置为开漏输出，使用
  `Serial1` 时必须提供与目标电平匹配的外部上拉电阻。
- `Serial2` 不会在 SDK 启动阶段自动占用 PAD。调用 `Serial2.begin()` 后才启用
  UART2 并切换复用功能；调用 `Serial2.end()` 后关闭 UART2，并将 TX/RX 释放为
  GPIO 输入。CI1302/CI1303 为 PA5/PA6，CI1306 为 PB1/PB2。
- software SPI 默认使用 `SCK=PA5`、`MISO=PA2`、`MOSI=PA4`、`SS=PA3`，
  `SPI.begin()` 会原子申请四个引脚和 software-SPI 资源；冲突时返回 `false`。
  PA4 同时是复位阶段的 `PG_EN` 检测脚，外设在复位期间不得主动驱动它。
- `SD` 使用上述 software SPI 接线，仅支持 FAT16/FAT32 和 8.3 短文件名；不支持
  exFAT、长文件名、DMA 或 SDIO。CI1302/CI1303 上 PA2/PA3 与 `Wire`/`Serial1`
  冲突，SD 文件系统和 SPI 总线也需要由应用保证跨 task 互斥。
- CI-D06GT01D 的 PD0 默认连接功放控制，改作普通 GPIO 会影响音频播放。
- `ChipIntelliIR` 默认占用 pin 2/PWM0、pin 4 和 TIMER2，载波固定为 38 kHz。
  原厂驱动和空调任务没有完整反初始化接口，因此成功启动后会持有这些资源直到
  复位；raw 模式与空调码库模式不能在同一次启动中切换。
- 官方数据库只覆盖空调。电视、风扇、灯具等设备通过 raw 学习/保存/回放或 NEC
  控制；空调数据库以 sketch-local user-file ID `50000` 叠加，不占用 TTS 的 ID `0`。
- `ChipIntelliTimer` 会通过资源管理器申请 TIMER0–2；当前 SDK 的 BLE 射频驱动固定
  占用 TIMER3，资源管理器将其标记为系统保留，Timer 和 IR 都不能覆盖。硬件回调
  运行在中断中，不能阻塞、访问 Flash 或调用 `Serial`。`Ticker` 不占硬件 Timer，
  但回调运行在共享的 FreeRTOS timer-service task，耗时操作会延迟 SDK 的其他软件定时器。
- `ChipIntelliWatchdog` 和原厂音频任务共用唯一 IWDG；音频输入任务也会喂狗，
  因此它用于检测系统/音频管线整体停滞，不能严格只监视 Arduino `loop()`。
- `Preferences` 每个 namespace 最多 16 项且整个记录最多 240 B；它保留 NVDM
  ID `0xE0000000`–`0xEFFFFFFF`，与 IR 等 user-file 资源 ID 属于不同地址空间。
- `ChipIntelliAudio` 只能播放完整固件 `voice.bin` 中已经配置的提示音，不能读取
  任意 WAV/MP3 文件，也不会把文本实时转换为语音。
- 当前基线不提供 Wi-Fi；除 `SD` 库的 FAT16/FAT32 外，不提供通用文件系统。

### 内存报告

代码、只读数据、读写数据、BSS、栈和运行时 heap 共用一段 `0x82000`
（532480 B）host SRAM，代码在启动时从 Flash 加载到 SRAM。根据芯片厂家的确认，
Arduino 保留原厂 SDK 的最终 `user_code.bin <= 0x70000`（448 KiB）硬限制；超过时
后处理立即报错，不会生成或烧录完整固件。AEC/CWSL+AEC 第二核镜像分别为
76,584/76,648 B，菜单相应收紧 host program 上限。CI1302 选择官方 CWSL 四件套时还受
2 MB Flash 排布限制，菜单会把双核 `user_code.bin` 上限进一步收紧为 `0x39000`
（233472 B），Arduino 显示的保守 host program 上限为 157233 B；CI1303/CI1306
CWSL 仍使用 `0x70000` 上限。

`citool-cli compose` 仍按 User、ASR、DNN、Voice、UserFile 五个最终 bin 的实际大小
进行 4 KiB 对齐并顺序计算 Flash 地址，但这只优化 Flash 排布，不会放宽 User 的
SRAM/加载限制以及 CI1302 CWSL 的 Flash 排布限制。Arduino CLI 报告的 program 与
dynamic memory 包含重叠的
`.data`，不能当作两块可分别用满的内存；最终以链接器和 `user_code.bin` 后处理检查
为准。

## 验证状态

当前验证基线：

- 原厂 SDK 示例的 138 个源文件使用 GCC 9.2.0 完整构建通过，并已验证 Arduino
  源码预构建、缓存和直接对象链接；三个变体的链接映射均不再引用
  `libci13xx_sdk.a`；
- CI1306 的 GPIO、中断、ADC、PWM、Serial、software SPI、Wire、EEPROM、ASR、
  提示音和综合冒烟等 16 个 sketch 已完成编译、链接与双镜像后处理；
- CI1302 与 CI1303 各完成 `CI13XXSmoke`、`PWMFade`、`Blink`、
  `DigitalInputPullup`、`GPIOInterrupt` 的编译、链接与后处理；
- `SD` 的 7 个示例已分配到 CI1302、CI1303、CI1306 完成编译、链接和完整固件
  后处理；实体 SD 卡的初始化、FAT16/FAT32 读写和吞吐量仍待硬件验证；
- `ChipIntelliWatchdog`、`ChipIntelliTimer`/`Ticker`、`Preferences` 与
  `ChipIntelliAudio` 已在 CI1302、CI1303、CI1306 完成单库及合并编译、链接和完整
  固件后处理；CI1303 已通过 Timer/Ticker、Watchdog 控制、EEPROM/Preferences 跨复位
  保持、音频初始化和提示音完成回调，Watchdog 实际超时复位及其余芯片仍待验证；
- Windows 上的 `ChipIntelliCWSL/SerialLearning` 已在 CI1302、CI1303、CI1306
  的标准离线 ASR 与 CWSL 两种 profile 完成 6/6 编译、链接、双核合并、`compose`
  和 `inspect`。macOS/Linux 使用函数级 section 的非 LTO 厂家库，程序会比 Windows
  LTO 构建稍大；CI1302 CWSL 应使用最小的 `BasicLearning` 起步，完整
  `SerialLearning` 适用于 CI1303/CI1306。CI1303 CWSL 已完成实体启动和模板容量
  查询，自学习录音、模板持久化和识别仍待受控说词验证；
- `v1.0.0` Boards Manager 发布包已在隔离 Arduino CLI 环境完成安装；CI1306 的
  16 个安装后示例以及 CI1302/CI1303 的综合冒烟示例均编译通过，共 18/18；
- CI1302、CI1303 与 CI1306 已在隔离 Arduino CLI 环境验证资源准备、完整编译、
  `compose` 和 `inspect`；CI1303 已使用 `citool-cli` 完成实体板上传、固件 CRC、
  Standard/CWSL SDK 启动、UART0、音频提示回调和 I2C/SSD1306 运行验证，CI1302、
  CI1306 与受控说词识别仍待验证。macOS Apple Silicon 的内部晶振 Standard 构建
  还通过了 CH343 上传、双核启动以及 voice ID 1/2/3 顺序播放。

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
