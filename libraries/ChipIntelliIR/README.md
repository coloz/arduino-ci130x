# ChipIntelliIR

`ChipIntelliIR` 是 CI13XX 的 Arduino 红外库，直接封装芯片原厂 V2.7.14 SDK 的
红外驱动与空调码库。

库提供三类能力：

- 固定 38 kHz 载波的通用原始波形收发，可用于学习和回放电视、风扇、灯具等遥控器；
- 标准 NEC 和扩展 NEC 发送、接收解码与重复帧识别；
- 原厂 36 品牌空调码库、型号搜索和空调状态命令。

原厂数据库只覆盖空调，并不是电视、风扇和灯具的通用码库。这些品类应使用 raw
学习/回放或已知的 NEC 地址与命令。

## 支持矩阵

| 目标 | Raw/NEC | 空调码库 | 默认引脚 |
| --- | --- | --- | --- |
| CI1302（2 MB） | 支持 | 标准 Arduino 资源布局空间不足，不支持 | PA2 / PA4 |
| CI1303（4 MB） | 支持 | 支持 | PA2 / PA4 |
| CI1306（4 MB） | 支持 | 支持 | PA2 / PA4 |
| CI-D06GT01D | 支持 | 支持 | 板载 PA2 / PA4 |
| easyVoice 1306 dev | 支持 | 支持 | PA2 / PA3；PA4 未引出 |

“支持”表示源码、链接和固件打包路径已经验证。红外 LED 驱动强度、接收头极性、
载波误差及具体电器兼容性仍应按本文末尾的实物检查表在目标硬件上验收。

## 接线和初始化

默认配置按开发板 variant 提供。CI1302、CI1303、CI1306 和 CI-D06GT01D 与原厂
示例一致：发送 `PA2`（Arduino 引脚 2 / PWM0）、接收 `PA4`（Arduino 引脚 4）、
`TIMER2`。easyVoice 1306 dev 的 PA4 未引出，因此默认接收改用 PA3。红外发射 LED
应使用合适的限流与三极管驱动，接收端应使用已解调的 38 kHz 红外接收头。

```cpp
#include <ChipIntelliIR.h>

void setup() {
  Serial.begin(115200);
  if (!ChipIntelliIR.begin()) {
    Serial.println(ChipIntelliIR.errorString());
  }
}
```

也可以给 `begin(txPin, rxPin, timer)` 指定其他引脚。TX 必须支持 PWM，RX 必须支持
GPIO 中断。当前 Arduino SDK 的 BLE 射频驱动固定占用 `TIMER3`，因此可选 timer 为
0 到 2；显式请求 3 会按资源冲突拒绝。库会原子占用两个引脚、对应 PWM 和 timer；冲突时
初始化失败，不会改写正在使用的外设。成功接管 RX 后，库会清除该引脚原有的
`attachInterrupt()` 回调。

## Raw 与 NEC

`sendRaw()` 的数组以 mark 开始，mark/space 交替，单位为微秒。一次最多 1024 段；
`uint32_t` 接口的单段范围是 200 到 131,070 µs，旧版 `uint16_t` 接口继续兼容。
接收使用非阻塞状态机：

```cpp
static uint32_t learned[ChipIntelliIRClass::MaxRawEntries];

ChipIntelliIR.startReceive(5000);
if (ChipIntelliIR.receiveStatus() ==
    ChipIntelliIRClass::ReceiveStatus::Ready) {
  size_t count;
  if (ChipIntelliIR.readRaw(learned,
                            ChipIntelliIRClass::MaxRawEntries, count)) {
    ChipIntelliIR.sendRaw(learned, count);
  }
}
```

标准 NEC 自动生成地址和命令反码：

```cpp
ChipIntelliIR.sendNEC(0x10, 0x20);
ChipIntelliIR.sendExtendedNEC(0x34AB, 0x20);
```

学习完成后可直接识别标准 NEC、16 位扩展地址 NEC 以及 repeat。解码会校验命令反码，
默认容差为 25%，不分配动态内存：

```cpp
ChipIntelliIRClass::NECDecodeResult decoded;
if (ChipIntelliIRClass::decodeNEC(learned, count, decoded)) {
  // decoded.type / address / command / repeatCount
}
```

## 空调数据库

空调模式使用原厂 V2.7.14 SDK 包中的 `ir_data_2024_08_16.bin`，大小 70,716 字节。
它必须与平台中的 `libir_data.a` 成对使用。

草图通过 `user_file_entries` 添加或合并 Arduino 保留 ID 50000 时，构建工具会校验
数据库长度和 SHA-256；损坏或与当前 `libir_data.a` 不配套的文件会在打包前被拒绝。
官方文件的 SHA-256 为
`F7E3680B45F9ABE56C6D3E16D1BBFFD7336E286D0B0DABED20E1D2B4854B97D0`。设备启动时还会
分块读取 Flash，并用 SDK 的 CRC32 核对 `0x07BCB65F`，以发现打包后或存储介质上的损坏。

```cpp
if (ChipIntelliIR.beginAirConditioner() &&
    ChipIntelliIR.selectAirBrand(ChipIntelliIRClass::AirBrand::Gree)) {
  if (ChipIntelliIR.power(true)) {
    ChipIntelliIR.waitUntilIdle();
  }
}
```

编译 `AirConditioner` 示例时，构建系统会将
`recursos/user_file_entries/[50000]ir_data_2024_08_16.bin` 叠加进最终固件。物理 ID
50000 永久留给 Arduino IR 数据库；默认 TTS 字典继续使用 ID0。初始化空调库时，
仅当前任务在一次原厂 `ir_init()` 调用期间临时看到 `0 -> 50000`，因此 TTS 和其他
任务不会受到影响。高级用户若使用原厂“IR 数据库直接位于 ID0”的旧固件布局，可
显式传 `beginAirConditioner(tx, rx, timer, 0)`。

新建自己的空调草图时，需要把示例中的数据库文件复制到草图目录；仅写
`#include <ChipIntelliIR.h>` 不会自动增加这个可选的 70 KB 资源：

```text
MyAirSketch/
  MyAirSketch.ino
  recursos/
    user_file_entries/
      [50000]ir_data_2024_08_16.bin
```

不要复制或替换示例构建时自动生成的 `asr.bin`、`dnn.bin`、`voice.bin` 和
`user_file.bin`；平台会在缺少它们时补齐默认版本。

CI1302 的 2 MB Flash 无法同时容纳标准 Arduino 语音资源、原厂空调静态库和这份
数据库。构建工具会给出针对性错误；请改用 CI1303/CI1306。若产品必须使用 CI1302，
需要另行制作精简的专用固件资源布局，不能只靠更换本库解决。

`selectAirBrand()` 会选择该品牌的首个码并重置空调状态。搜索得到的 code ID 是不透明
的 32 位值，应原样保存，再用 `selectAirCode()` 恢复；库会校验其编码校验和与官方索引
范围，避免无效值进入二进制解析器。`sendAir()` 是异步入队：返回 `true` 表示原厂任务
接受了命令，并不表示波形已经发完。`airSendStatus()` 可观察 `Queued`、`Sending`、
`Settling` 和 `Failed`；`waitUntilIdle()` 可阻塞等待完成并报告超时或异步错误。

官方私有队列没有“整条命令完成”事件，少数码型会在 600 ms 后发送第二帧。因此库在最后
一次驱动 `IR_SEND_END` 后保留 1 秒静默窗口，再判定为 `Idle`。这会使连续空调命令之间
至少间隔约 1 秒，是当前 V2.7.14 下避免漏判第二帧的保守策略。

## 生命周期和限制

- raw 模式与空调数据库模式在一次启动中互斥；只能使用全局单例 `ChipIntelliIR`。
- 原厂驱动没有安全的反初始化接口，因此库成功初始化后会持有引脚、PWM 和 timer
  直到芯片复位，也不提供误导性的 `end()`。
- 原厂载波固定为 38 kHz、30% 占空比；不适合要求其他载波频率的协议。
- 原厂底层不是多线程安全的；本库用一个互斥锁串行化公开操作。空调搜索回调运行在
  原厂 IR 任务中，回调应尽快返回，不要阻塞，也不要从回调中再次调用本库。
- 空调模式会占用原厂驱动唯一的 IR 事件回调来跟踪异步发送；不要另行调用底层
  `registe_ir_remote_callback()` 覆盖它。
- V2.7.14 的 `ir_init()` 在少数内部内存、队列、任务或定时器创建失败路径上只打印日志，
  仍可能返回成功，而且 SDK 没有公开初始化状态查询。极端低内存下应保留原厂日志；
  `waitUntilIdle()` 可发现命令未开始或异步失败，但无法消除该原厂限制。
- 学习或回放可能包含超过 65,535 µs 的间隔时应使用 `uint32_t` 版 `readRaw()` 和
  `sendRaw()`；旧 `uint16_t` 版读取遇到长间隔会报告 `InvalidArgument`。驱动自动添加的
  100 ms 帧尾间隔会被库移除。

完整示例见 `examples/RawSendReceive` 与 `examples/AirConditioner`。

## 实物验收建议

发布产品前至少完成以下检查，并保留串口日志和示波器/逻辑分析仪截图：

1. 测量连续 100 个 mark，确认载波接近 38 kHz、占空比接近 30%。
2. 分别发送标准 NEC、扩展 NEC 和带两个以上 repeat 的长按帧，核对地址、命令和时序。
3. 对同一遥控器执行至少 100 次接收、读取、回放，统计失败和截断次数。
4. 在 CI1303、CI1306 上各完成一次空调品牌搜索、开关、16/26/30 ℃和模式切换。
5. 验证无信号超时、主动停止、错误数据库以及 PWM/Timer/引脚冲突均返回预期错误。
6. 与 ASR、音频播放和串口同时运行至少 2 小时，确认无死锁、复位和持续 busy。
