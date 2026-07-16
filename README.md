# Arduino core for ChipIntelli CI13XX

这是基于 `CI13XX_SDK_ASR_ALG_V2.7.12` 的 Arduino 平台适配。它参考
`arduino-esp32` 的包结构，但不会替换启英泰伦 SDK 已验证的启动代码、
FreeRTOS、双核通信和离线语音任务；Arduino 的 `setup()` / `loop()` 作为一个
低优先级 FreeRTOS 任务接入原 SDK 调度器。

> 当前是已完成编译、链接和 `user_code.bin` 生成验证的首版基线。串口更新
> 只有在开发板先写入下文定义的 Arduino 完整固件分区基线后才成立；本轮尚未
> 做实体板烧录验证。它不是全部 CI13XX 型号和算法组合的泛化版本。

## 当前支持范围

- Windows 10/11 x64 主机（SDK 后处理工具 `ci-tool-kit.exe` 为 x64）；
- CI-D06GT01D 开发板、CI1306、4 MB Flash；
- SDK `USE_NULL=1` 基础离线 ASR 算法配置；
- 厂商 Nuclei RISC-V GCC 9.2.0，`rv32imafc / ilp32f`；
- Arduino CLI / Arduino IDE 2.x 的平台目录结构；
- 生成双镜像 `user_code.bin`，并用厂商 `code_program.exe` 更新 user-code 分区。

已实现的 Arduino 接口：

| 类别 | 接口 / 库 | 当前边界 |
| --- | --- | --- |
| 基础 | `setup/loop`、`String`、`Print`、`Stream`、`IPAddress` | C++17，无异常和 RTTI；浮点 `String` 不依赖 printf-float |
| 时间 | `millis`、`micros`、`delay`、`delayMicroseconds`、`yield` | `micros` 直接读取 64 位 mtime |
| GPIO | `pinMode`、`digitalRead/Write/Toggle` | PA0/PA1 为外部晶振保留 |
| 中断 | `attachInterrupt`、`attachInterruptArg`、`detachInterrupt` | PA/PB/PC；PD 不支持 GPIO IRQ |
| 模拟 | `analogRead`、读分辨率 | AIN2–AIN5，硬件 12 位 |
| PWM | `analogWrite`、写分辨率/频率、`tone/noTone` | 6 个硬件通道，同通道引脚互斥 |
| 串口 | `Serial`、`Serial1`、`Serial2` | polling、8N1、厂商支持的固定波特率 |
| I2C | `Wire` | IIC0 master、32 B、10–400 kHz、支持寄存器重复起始读 |
| SPI | `SPI`、`SPISettings` | GPIO software master、模式 0–3、MSB/LSB、事务/缓冲传输，上限 500 kHz |
| 持久化 | `EEPROM` | 基于 NVDM，单实例 1–240 B，需要 `commit()` |
| 语音 | `ChipIntelliASR` | 命令 ID、语义 ID、得分、帧数和文本队列/回调 |
| 兼容 | `pulseIn`、`shiftIn/shiftOut`、`random`、`map`、`PROGMEM/pgmspace` | 软件或统一地址空间兼容实现 |

CI1306 没有面向用户外设的通用硬件 SPI 控制器；片内 `QSPI0` 是启动、模型和
用户 Flash 总线，不能复用。本版 `SPI` 因此是明确标注的 GPIO software SPI，
不冒充硬件 SPI，也不提供 DMA、硬件片选或从机模式。Wi-Fi、SD 和通用文件系统
不属于此芯片/SDK 基线。

## 构建准备

1. 获取并解压厂商 `riscv-nuclei-elf-gcc-9.2.0`，确认目录中存在
   `gcc_fix_raissrc/bin/riscv-nuclei-elf-g++.exe`。
2. 在 PowerShell 中生成与当前板型/算法严格绑定的 SDK payload：

```powershell
cd arduino-chipintelli
.\tools\rebuild_sdk.ps1 `
  -SdkPath ..\CI13XX_SDK_ASR_ALG_V2.7.12 `
  -ToolchainBin C:\path\to\gcc_fix_raissrc\bin
```

3. 正式发布时，填充 `package/package_chipintelli_index.template.json`，通过
   Boards Manager 安装平台和工具链。源码开发时，也可把本目录链接到
   `{sketchbook}/hardware/chipintelli/ci13xx`，复制
   `platform.local.txt.example` 为 `platform.local.txt` 并填写本机编译器路径。

Arduino CLI 编译示例：

```powershell
arduino-cli compile --fqbn chipintelli:ci13xx:ci_d06gt01d `
  examples\CI13XXSmoke
```

## 基础示例

Arduino IDE 的 **文件 > 示例** 菜单会显示以下平台/库示例：

| 菜单 | 示例 | 用途 |
| --- | --- | --- |
| `CI13XX > GPIO` | `Blink`、`DigitalInputPullup`、`GPIOInterrupt` | 外接 LED、上拉按键、GPIO 中断 |
| `CI13XX > Analog` | `AnalogReadSerial`、`PWMFade` | 12 位 ADC、硬件 PWM 渐变 |
| `CI13XX > Serial` | `SerialEcho`、`Serial1Bridge` | UART0 回显、UART0/UART1 双向桥接 |
| `SPI` | `SoftwareSPILoopback` | PA4/MOSI 接 PA2/MISO 的软件 SPI 回环 |
| `Wire` | `MasterWrite`、`RegisterRead` | IIC0 寄存器写、重复起始寄存器读 |
| `EEPROM` / `ChipIntelliASR` | `PersistentCounter` / `ASRResults` | NVDM 持久化、离线识别结果 |

`examples/CI13XXSmoke` 保留为平台综合回归 sketch。`Wire` 不提供传统 I2C
Scanner：厂商 polling API 的零字节地址写不会安全结束，而写入虚拟数据可能改动
未知从设备；应使用器件数据手册给出的已知地址和寄存器。

## 引脚编号

逻辑编号按端口连续排列：

| Arduino 编号 | 芯片 PAD | 说明 |
| --- | --- | --- |
| 0–1 | PA0–PA1 | 外部晶振占用，不可作为 GPIO |
| 2–7 | PA2–PA7 | GPIO / PWM |
| 8–15 | PB0–PB7 | GPIO / PWM；13/14 为 UART0，15 为 UART1 TX / SDA |
| 16–21 | PC0–PC5 | GPIO；16 为 UART1 RX / SCL；17–20 支持 ADC/PWM |
| 22–27 | PD0–PD5 | GPIO，无 GPIO 中断；PD0 默认连接功放控制 |

别名：`A0=PC4/AIN2`、`A1=PC3/AIN3`、`A2=PC2/AIN4`、
`A3=PC1/AIN5`、`SDA=PB7`、`SCL=PC0`；software SPI 默认
`SCK=PA5`、`MISO=PA2`、`MOSI=PA4`、`SS=PA3`。

引脚表来自芯片 PAD 复用表；开发板/模组是否把某个 PAD 实际引出，仍应以
对应硬件原理图为准。

## 资源冲突

- `Serial` 是 SDK 日志口 UART0（PB5/PB6），默认日志波特率 921600；调用
  `Serial.begin()` 会重新初始化该口。
- `Serial2` 的 PB1/PB2 默认被 SDK 语音模块协议占用；重新初始化会接管协议口。
- `Wire` 与 `Serial1` 共用 PB7/PC0，不能同时使用。
- software SPI 默认使用 PA2–PA5；如果改为启用 IIS 采音/录放的 SDK profile，
  必须改用其他 GPIO。PA4 同时是复位阶段的 `PG_EN` 检测脚，外设在复位时不得
  主动驱动该脚；作为主机 MOSI 连接普通从机输入可在启动后使用。
- PD0 是 CI-D06GT01D 功放控制脚，改作普通 GPIO 会影响语音播放。
- 多个 PAD 可映射到同一个 PWM 通道；同一时刻一个通道只能维持一组配置。
- Arduino 任务与 SDK 语音任务并行运行；ASR 回调运行在 SDK 消息任务中，回调
  应保持很短，耗时处理放到 `loop()` 中完成。

## 固件与上传边界

CI13XX Arduino 产物不是可写入地址 0 的普通裸 bin。后处理会：

1. 将 sketch ELF 转成 `[0]code.bin`；
2. 加入匹配 `USE_NULL` 的第二核 `[1]code.bin`；
3. 用 `ci-tool-kit merge user-file` 生成最终 Arduino `.bin`（即
   `user_code.bin` 容器）。

“上传”只更新 user-code 分区。原 SDK 打包脚本按当时 `user_code.bin` 的实际
大小分区（原示例约 `0x26000`），不能容纳当前 Arduino 产物。因此本开发预览
采用 448 KiB（`0x70000`）作为固定 Arduino baseline 策略；数值取自 V2.7.12
的 `USERCODE_MAX_SIZE`，但厂商打包器并未强制使用它，仍须用 GUI 和实体板确认。
平台在后处理阶段按此策略对合并容器做硬检查。`boards.txt` 中的
382577 B 程序上限按当前第二核镜像及容器最坏对齐开销计算；最终仍以后处理对
完整容器的精确检查为准。

首次使用必须：

1. 在原 SDK 的 `projects/offline_asr_alg_pro_sample/firmware` 中运行原厂分区
   合并步骤，生成 `asr.bin`、`dnn.bin`、`voice.bin` 和 `user_file.bin`；
2. 先编译任一 Arduino 示例得到其合并后 `.bin`；
3. 生成输入文件哈希和分区预留清单，并按需启动原厂 GUI：

```powershell
.\tools\prepare_provisioning.ps1 `
  -FirmwareDirectory ..\CI13XX_SDK_ASR_ALG_V2.7.12\projects\offline_asr_alg_pro_sample\firmware `
  -UserCode C:\path\to\CI13XXSmoke.ino.bin `
  -OutputDirectory ..\ci13xx-provisioning `
  -LaunchTool
```

4. 在 `PACK_UPDATE_TOOL.exe` 中选择 CI130X 系列、CI1306、FW_V2、4 MB、
   Code2 关闭；填写 manifest 中的软硬件版本、ID、16 KiB NV data 等全部
   metadata，并为五个文件分别填写 `reservedSize`（User 固定为 `0x70000`）；
   打包时若提示地址冲突必须重新调整非 User 分区并复核容量；
5. 打包后记录完整固件的 SHA-256，再用工具的固件升级页把该产物首次写入
   CI-D06GT01D。以后 Arduino “上传”才可只更新 user-code 分区。

输出目录应放在 `arduino-chipintelli` 平台树之外，避免把临时清单或完整固件
误打进平台 ZIP。准备脚本不替代原厂 GUI，也不执行首次烧录。SDK 自带命令行
`ci-tool-kit make-firmware` 对 CI1306/CI130X 会报 `chip name error`，所以这里
没有用未经验证的 CI1303 映射绕过；完整固件必须走官方支持的
`PACK_UPDATE_TOOL.exe`。仓库也不分发预制完整固件；正式发布必须提供经实机
验证的固定产物、SHA-256、下载地址和首次烧录说明。不能把 Arduino 输出当作
地址 0 的完整量产固件。更多细节见
[README-SDK-INTEGRATION.md](README-SDK-INTEGRATION.md)。

## 内存计量

CI1306 链接脚本把代码、只读数据、读写数据、BSS、栈和 100 KiB FreeRTOS heap
放在同一段 `0x82000`（532480 B）host SRAM。Arduino CLI 显示的 “program” 和
“dynamic memory” 含有重叠的 `.data`，不是两块可分别用满的内存；其“剩余局部
变量空间”提示也不适用于此布局。最终是否溢出以链接器对这段统一 SRAM 的检查
为准，百分比只用于横向比较。

## 验证记录

- 原 SDK 示例：138 个源文件使用官方 GCC 9.2.0 完整构建通过；
- Arduino CLI 1.5.1：GPIO、外部中断、ADC、PWM、Serial、Serial1、software
  SPI、Wire、EEPROM、ASR 和综合冒烟共 14 个 sketch 均完成交叉编译、最终
  链接与后处理；`arduino-cli lib examples` 也已识别 `CI13XX`、`SPI`、`Wire`
  的全部新增示例；综合冒烟为 137083 B program、119024 B dynamic memory；
- 最终 ELF 已确认包含 `__wrap_vTaskStartScheduler`、
  `__wrap_sys_asr_result_hook`、Arduino 任务和 sketch 字符串，排除了
  SDK LTO 绕过 Arduino/ASR 接入钩子的情况；
- 后处理：冒烟产物 `[0]code.bin` 为 136820 B，合并后的双核
  `user_code.bin` 为 212992 B；已验证超过 448 KiB 时构建会失败；
- 首次固件：分区清单/哈希准备脚本和官方 GUI 流程已接入；SDK 命令行打包器
  的 CI1306 参数不兼容已显式拦开，未伪装成可自动打包；
- 尚未在本轮执行实体 CI-D06GT01D 的串口烧录和硬件 I/O/语音回归测试。

## 许可提醒

Arduino 兼容 core 中继承的 LGPL-2.1+ 源码已附
[许可证全文](LICENSE-ARDUINO-CORE.md)；具体归属和其余发布边界见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。上游 SDK README 含“未经允许
不得使用或修改”的限制，且没有统一的顶层开源许可证。公开发布 Boards
Manager 包之前，仍必须取得启英泰伦对 SDK 对象、算法库、第二核镜像、编译器
和 Windows 工具的再分发许可，并完成逐文件第三方声明。因此 package index
目前只是模板。
