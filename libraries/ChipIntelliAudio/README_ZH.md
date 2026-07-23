# ChipIntelliAudio

`ChipIntelliAudio` 将 CI130X 官方 SDK 的提示音播放器封装成 Arduino API，
用于播放随固件烧录到 `voice.bin` 分区中的音频。

请直接使用全局对象 `ChipIntelliAudio`。SDK 播放器、完成回调钩子和静音钩子都是
芯片级单例，因此本类禁止再构造或复制第二个播放器实例。

它支持：

- 按语音 ID 播放；
- 按命令 ID、命令文本或语义 ID 查找并播放已配置的提示音；
- 中断当前提示音或排队播放；
- 停止播放、查询状态、调节音量及可靠地静音/恢复；
- 在 SDK 处理完提示音请求时执行回调。

本库不是 TTS 引擎。`playCommand(const char *)` 只查找资源包中已有的命令词，
不会把任意文本转换成语音；当前也不能直接播放 SD 卡中的 WAV 或 MP3 文件。

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
| `playVoice(voiceId, interruptCurrent)` | 播放 `voice.bin` 中的 16 位语音 ID |
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

语音、命令和语义 ID 必须与当前工程 `recursos/voice.bin` 及命令资源匹配；示例中的
ID `1` 不保证存在于自定义资源包中。

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
- `PromptControl`：通过 115200 波特率串口演示播放、停止、状态、音量和静音控制。
