# ChipIntelliIR

`ChipIntelliIR` 是 CI1302、CI1303、CI1306 的 Arduino 红外库，直接封装芯片原厂
V2.7.14 SDK 的红外驱动与空调码库。

库提供三类能力：

- 固定 38 kHz 载波的通用原始波形收发，可用于学习和回放电视、风扇、灯具等遥控器；
- 标准 NEC 和扩展 NEC 发送；
- 原厂 36 品牌空调码库、型号搜索和空调状态命令。

原厂数据库只覆盖空调，并不是电视、风扇和灯具的通用码库。这些品类应使用 raw
学习/回放或已知的 NEC 地址与命令。

## 接线和初始化

默认配置与原厂示例一致：发送 `PA2`（Arduino 引脚 2 / PWM0）、接收 `PA4`
（Arduino 引脚 4）、`TIMER2`。红外发射 LED 应使用合适的限流与三极管驱动，接收端
应使用已解调的 38 kHz 红外接收头。

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

`sendRaw()` 的数组以 mark 开始，mark/space 交替，单位为微秒。每段至少 200 µs，
一次最多 1024 段。接收使用非阻塞状态机：

```cpp
static uint16_t learned[ChipIntelliIRClass::MaxRawEntries];

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

## 空调数据库

空调模式使用随库示例提供的原厂文件 `ir_data_2024_08_16.bin`，数据库版本 2.17，
大小 70,716 字节。它必须与平台中的 `libir_data.a` 成对使用。

```cpp
if (ChipIntelliIR.beginAirConditioner() &&
    ChipIntelliIR.selectAirBrand(ChipIntelliIRClass::AirBrand::Gree)) {
  ChipIntelliIR.power(true);
  ChipIntelliIR.setTemperature(26);
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

`selectAirBrand()` 会选择该品牌的首个码并重置空调状态。搜索得到的 code ID 是不透明
的 32 位值，应原样保存，再用 `selectAirCode()` 恢复。`sendAir()` 是异步入队：返回
`true` 表示原厂任务接受了命令，并不表示波形已经发完。

## 生命周期和限制

- raw 模式与空调数据库模式在一次启动中互斥；只能使用全局单例 `ChipIntelliIR`。
- 原厂驱动没有安全的反初始化接口，因此库成功初始化后会持有引脚、PWM 和 timer
  直到芯片复位，也不提供误导性的 `end()`。
- 原厂载波固定为 38 kHz、30% 占空比；不适合要求其他载波频率的协议。
- 原厂底层不是多线程安全的；本库用一个互斥锁串行化公开操作。空调搜索回调运行在
  原厂 IR 任务中，回调应尽快返回，不要阻塞，也不要从回调中再次调用本库。
- `readRaw()` 的公共持续时间类型为 `uint16_t`。大于 65,535 µs 的帧内间隔会报告
  `InvalidArgument`；驱动自动添加的 100 ms 帧尾间隔会被库移除。

完整示例见 `examples/RawSendReceive` 与 `examples/AirConditioner`。
