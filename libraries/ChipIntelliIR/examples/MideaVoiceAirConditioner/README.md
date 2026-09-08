# 美的空调离线语音遥控

目标开发板：**easyVoice 1306 dev / CI1306 / 4 MB**。
使用 ChipIntelliIR 原厂美的码库、ChipIntelliASR 离线识别、ChipIntelliAudio 提示音和 Preferences 码组保存。
附带仓库 SimpleCommandPlayback 已生成的完整语音资源，无需登录或联网生成。

## 接线

- 红外发射模块信号接 **PA2**，与开发板共地；使用适合 3.3 V 信号的带驱动发射模块，或限流电阻加三极管驱动红外 LED。
- 库占用 **PA3** 作为红外接收脚，使用 TIMER2；本例发射和码组搜索不需要外接接收头。
- 使用开发板麦克风及扬声器。默认编译配置为模拟单麦、AEC、外部 12.288 MHz 晶振。
- 发射头朝向空调接收窗。上电不自动发送开机命令。

## 语音指令

先说 **“小智小智”**，听到“我在，请说”后说下列指令。每次识别续期 15 秒，超时后重新唤醒。

| 命令 ID | 指令 | 操作 |
| --- | --- | --- |
| 2 / 3 | 打开空调 / 关闭空调 | 开机 / 关机 |
| 4 / 5 | 开启制冷 / 开启制热 | 制冷 / 制热 |
| 6 / 7 | 调高温度 / 调低温度 | 调整温度 |
| 8 | 设置二十六度 | 26℃ |
| 9 / 10 | 风速调高 / 风速调低 | 调整风速 |
| 11 | 开启自动风 | 自动风速 |
| 12 | 开启睡眠模式 | 睡眠模式 1 |
| 13 / 14 | 定时一小时 / 取消定时 | 本地一小时关机定时 / 取消 |

完成红外发送后响一声“滴”；忙碌、错误等响两声，详情见串口。
提示音只说明本机发送状态，红外没有空调接收确认。不同美的机型不一定支持所有功能。
使用原遥控器后，码库中的状态可能与空调实际状态不同。

一小时定时由开发板计时，到期发送关机指令；开发板须持续供电，重启会取消定时。
若到期时正在匹配或发射，关机指令会等到空闲后发送；发送失败会报告错误。

## 匹配不同美的机型

初次默认选择美的品牌第一个码组。如果空调无反应，打开 **COM30，115200 / 8N1** 串口工具：

1. 发送 `s` 开始匹配，发射头持续对准空调。匹配会实际发送空调测试信号。
2. 程序逐个尝试美的码组，每个码组发送 3 次，间隔参数为 5 秒。
3. 空调发出提示声或有动作后，立即发送 `y`。串口出现 `Saved Midea code=...` 表示已保存到 Flash。
4. 再测试开关机、制冷和温度；若仅部分功能正常，重新匹配。
5. 发送 `x` 取消；取消或搜索自然结束时恢复之前的码组，不保存未确认码组。

本例资源不含语音匹配词，匹配使用串口。断电后保留已确认码组。
串口还支持：`?` 查询状态、`1` 开机、`0` 关机、`c` 制冷、`h` 制热、`6` 设置 26℃。
状态查询不发射红外，其余控制命令会实际发射。

## 编译与烧录

使用 **1.0.17 或更新兼容版本**的 ChipIntelli Arduino 核心及配套 SDK。
主草图已声明 `#define CHIPINTELLI_IR_DATABASE 1`，构建钩子自动准备并校验官方
空调数据库，使用公共 `recursos/user_file_entries` 路径，与算法配置无关：

```powershell
arduino-cli compile --fqbn chipintelli:ci13xx:easyvoice_1306_dev --build-path .build/midea libraries/ChipIntelliIR/examples/MideaVoiceAirConditioner
arduino-cli upload --fqbn chipintelli:ci13xx:easyvoice_1306_dev --port COM30 --input-dir .build/midea libraries/ChipIntelliIR/examples/MideaVoiceAirConditioner
```

保留 recursos 下四个语音资源文件，不要替换为平台通用资源。示例已有的
`recursos/user_file_entries/[50000]ir_data_2024_08_16.bin` 会校验后直接使用；新建自己的
工程时无需另行复制数据库，保留主 `.ino` 中的顶层字面量宏即可。`0` 或未定义关闭
自动准备，只清理由钩子自动创建且未修改的数据库，保留示例原有或用户修改的资源。
aily 工程会同时准备 `src/recursos/user_file_entries` 与本轮
`.temp/sketch/recursos/user_file_entries`。宏格式及文件托管规则见库 README。
ASR/DNN 来自仓库 `ChipIntelliASR/examples/SimpleCommandPlayback` 的 CI1306 资源，命令 ID 与上表逐项对应。
TTS 保留其中的启动、唤醒、定时反馈和蜂鸣音。
