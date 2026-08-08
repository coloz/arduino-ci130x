# ChipIntelliAudio

`ChipIntelliAudio` 将 CI130X 官方 SDK 的提示音播放器封装成 Arduino API，
用于播放随固件烧录到 `voice.bin` 分区中的音频。

请直接使用全局对象 `ChipIntelliAudio`。SDK 播放器、完成回调钩子和静音钩子都是
芯片级单例，因此本类禁止再构造或复制第二个播放器实例。

它支持：

- 按语音 ID 播放；
- 将 1～24 个语音 ID 作为一条连续提示音播放；
- 直接用 `playVoice("数字字符串")` 和 ID 300～316 的基础音频拼接整数、小数；
- 连续播放 1～16 声内置“滴”提示音；
- 按命令 ID、命令文本或语义 ID 查找并播放已配置的提示音；
- 中断当前提示音或排队播放；
- 停止播放、查询状态、调节音量及可靠地静音/恢复；
- 在 SDK 处理完提示音请求时执行回调。

本库不是 TTS 引擎。`playCommand(const char *)` 只查找资源包中已有的命令词，
不会把任意文本转换成语音；变量数字接口只拼接 `voice.bin` 中预先生成的短音频。
当前也不能直接播放 SD 卡中的 WAV 或 MP3 文件。

## 快速开始

```cpp
#include <ChipIntelliAudio.h>

void setup() {
  Serial.begin(115200);

  if (!ChipIntelliAudio.begin()) {
    Serial.println("Audio initialization failed");
    return;
  }

  ChipIntelliAudio.setVolume(70);
  if (!ChipIntelliAudio.playVoice(1)) {
    Serial.println("Playback request failed");
  }
}

void loop() {}
```

`begin()` 会按需启动与 `ChipIntelliASR` 等库共用的 SDK，并等待 Flash 资源、
音频任务、Codec 和功放就绪，最长等待 10 秒。成功初始化后再次调用会直接返回
`true`。`end()` 会清除完成回调、提交停止请求并恢复静音前的音量，但不会关闭共享
SDK；与 `stop()` 一样，它无法确认 SDK 是否在有限等待结束前真正进入空闲状态。

## 播放接口

| API | 作用 |
| --- | --- |
| `playVoice(voiceId, interruptCurrent)` | 数字参数：播放 `voice.bin` 中的 16 位语音 ID |
| `playVoice(numberText, interruptCurrent)` | 字符串参数：解析并播报十进制数字 |
| `playVoiceSequence(voiceIds, count, interruptCurrent)` | 连续播放 1～24 个语音 ID；整组只回调一次 |
| `playNumber(value, voiceIds)` | 拼接并播放 32 位有符号中文整数 |
| `playFixedPoint(value, fractionalDigits, voiceIds)` | 按定点数方式拼接并播放小数 |
| `buildNumberVoiceSequence(...)` | 生成整数语音 ID 序列，便于检查或调整结果 |
| `buildFixedPointVoiceSequence(...)` | 生成定点小数语音 ID 序列，便于检查或调整结果 |
| `playBeep(count)` | 连续播放 1～16 声内置“滴”提示音 |
| `playCommand(commandId, optionIndex, interruptCurrent)` | 播放命令 ID 对应的提示音 |
| `playCommand(commandText, optionIndex, interruptCurrent)` | 按已配置的命令文本查找提示音 |
| `playSemantic(semanticId, optionIndex, interruptCurrent)` | 播放 32 位语义 ID 对应的提示音 |
| `stop()` | 提交停止请求；未初始化时视为已停止 |
| `isPlaying()` | 查询当前是否正在播放 |
| `isReady()` | 查询 `begin()` 是否已成功完成 |

播放函数返回 `true` 只表示 SDK 外层提示音接口接受了请求，不代表之后的资源查找或
播放一定成功。参数可由本封装判定为无效、尚未初始化或 SDK 立即拒绝时会返回
`false`。`stop()` 也无法判断 SDK 是否在其内部有限等待结束前真正进入空闲状态。

`interruptCurrent` 默认为 `true`，会中断当前提示音；传入 `false` 会将新请求加入
SDK 提示音队列。`optionIndex` 默认为 `-1`，表示使用资源包配置的默认选项。

`playBeep()` 默认播放 1 声，传入 `1`～`16` 可设置连续播放次数，例如
`ChipIntelliAudio.playBeep(3)`。它使用资源包的特殊命令 `<beep>`，会中断当前提示音，
并在整组“滴”声结束后触发一次完成回调。标准资源包包含该提示音；自定义资源包若
缺少 `<beep>`，函数会返回 `false`。`count` 为 `0` 或大于 `16` 时也返回 `false`。

语音、命令和语义 ID 必须与当前工程 `recursos/voice.bin` 及命令资源匹配；示例中的
ID `1` 不保证存在于自定义资源包中。

## 变量数字播报

变量数字播报使用 17 条可复用的短音频：`零、一、二、三、四、五、六、七、八、九、十、百、千、万、亿、负、点`，
固定使用 ID 300～316。当最终程序链接了字符串版 `playVoice()` 时，Arduino 构建后处理会
自动让 `citool-cli generate` 把这 17 条内容加入 TTS 请求，程序中不需要声明资源宏。
用户可以直接传入字符串：

```cpp
ChipIntelliAudio.playVoice("300");   // 三百
ChipIntelliAudio.playVoice("-1.5");  // 负一点五
ChipIntelliAudio.playVoice("0.02");  // 零点零二
ChipIntelliAudio.playVoice("0.02", false);  // 将整组数字加入播放队列
```

`interruptCurrent` 默认为 `true`；传入 `false` 时，整组数字会排在当前提示音之后。
这个参数只影响数字第一段音频如何开始，后续各段始终连续播放，整组完成后只回调一次。
整数参数仍保持原有含义：`playVoice(300)` 是直接播放语音 ID 300，
而 `playVoice("300")` 才是播报数值“三百”。

字符串允许首尾 ASCII 空白、可选的 `+`/`-` 符号和一个小数点，不接受科学计数法。
小数部分逐位播报并保留零；非法字符串、整数部分超过 `uint32_t` 或最终超过 24 个词元时
返回 `false`。字符串 `-0` 和 `-0.00` 按数值零播报，不读“负”。

需要使用另一套语音 ID 时，可以把对应 ID 填入 `NumberVoiceIds`，再调用底层数字接口：

```cpp
const ChipIntelliAudioClass::NumberVoiceIds numberVoices = {
    {300, 301, 302, 303, 304, 305, 306, 307, 308, 309},
    310,  // 十
    311,  // 百
    312,  // 千
    313,  // 万
    314,  // 亿
    315,  // 负
    316,  // 点
};

ChipIntelliAudio.playNumber(-10010, numberVoices);       // 负一万零一十
ChipIntelliAudio.playFixedPoint(235, 1, numberVoices);   // 二十三点五
```

`playFixedPoint()` 使用整数表示定点值，不使用浮点运算。例如 `(235, 1)` 表示 `23.5`，
`(2300, 2)` 表示 `23.00`，小数末尾的零会保留；小数位数范围为 `0`～`9`。

如需先检查或调整拆分结果，可用 `buildNumberVoiceSequence()` 或
`buildFixedPointVoiceSequence()` 取得语音 ID 数组，再调用 `playVoiceSequence()`。

每组最多包含 24 段音频；可选的 `interruptCurrent` 参数只影响整组如何开始，SDK 将它
作为一个逻辑播放请求处理，整组完成后只触发一次完成回调。详细的必选、按需词表及录音建议见
[`VARIABLE_VOICE_TEXTS_ZH.md`](VARIABLE_VOICE_TEXTS_ZH.md)。

## 音量和静音

```cpp
ChipIntelliAudio.setVolume(70);  // 0～100
ChipIntelliAudio.mute();
ChipIntelliAudio.unmute();       // 恢复到 70
```

`setVolume()` 接受 `0`～`100`，更大的 `uint8_t` 值会限制为 `100`。Codec 会把
非零值量化为有限的 DAC 增益档位，因此该数值不是线性的声学响度百分比。

`mute()` 会保存当前请求的音量并将输出增益设为零；静音期间调用 `setVolume()` 只
更新之后要恢复的音量。Arduino SDK 钩子还会把 SDK 内部发起的音量变化强制保持为
零，语音或串口音量命令不会意外解除静音。SDK V2.7.14 中
`audio_play_set_mute()` 没有实际控制 Codec，所以本库使用输出增益实现可靠静音。

SDK 虽然声明了暂停、继续和变速接口，但默认提示音播放器不能可靠地恢复播放，
标准 ASR 配置也没有编译变速模块，因此本库不暴露这些会产生虚假成功状态的 API。

## 播放完成回调

```cpp
volatile bool playbackFinished = false;

void onPlaybackFinished(void *) {
  playbackFinished = true;
}

void setup() {
  Serial.begin(115200);
  if (!ChipIntelliAudio.begin()) {
    return;
  }
  ChipIntelliAudio.onFinished(onPlaybackFinished);
  ChipIntelliAudio.playVoice(1);
}

void loop() {
  if (playbackFinished) {
    playbackFinished = false;
    // 在这里执行打印、通信等耗时工作。
  }
}
```

SDK 原本会在持有提示音互斥锁时触发完成通知。Arduino 集成会先记录该事件，再由 SDK
解锁后的钩子调用用户回调，因此不会在持有提示音锁时重入播放接口。回调没有固定的
任务上下文：立即拒绝的请求可能在调用它的 sketch 任务中通知，异步完成通常在 SDK
音频任务中通知。回调仍应保持简短，不要调用 `delay()`、播放控制、等待锁、执行
Flash 写入或大量打印。建议只设置 `volatile` 标志或发送非阻塞队列消息，再由
`loop()` 处理。该通知表示 SDK 已处理完请求，但没有播放成功/失败状态；被中断或后续
资源查找失败也可能产生通知。调用 `onFinished(nullptr)` 可清除回调。

## 示例

- `PlayVoiceId`：初始化播放器，按语音 ID 播放并安全处理完成事件；
- `VariableNumber`：用 ID 300～316 的 17 个基础数字词元拼接整数和小数；
- `PromptControl`：通过 115200 波特率串口演示提示音、“滴”声、停止、状态、音量和静音控制。
