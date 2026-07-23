#include <ChipIntelliAudio.h>

// 示例：播放 voice.bin 中指定 ID 的提示音，并在播放完成后打印消息。
//
// 将这里替换为草图 recursos/voice.bin 中实际存在的语音 ID。Arduino 的正常
// 编译和上传会把 recursos 中的音频资源一起打包到完整固件中。
constexpr uint16_t kVoiceId = 1;

// 完成通知只会在 SDK 释放提示音锁后执行；任务上下文并不固定。
// volatile 标志用于把事件交给 loop()。
volatile bool playbackFinished = false;

void onPlaybackFinished(void *context) {
  (void)context;
  // 回调中不要 delay、控制播放、大量打印或执行耗时操作。
  playbackFinished = true;
}

void setup() {
  // 示例的日志波特率；串口监视器需要使用相同设置。
  Serial.begin(115200);

  // begin() 会启动共享 SDK 并等待音频资源、Codec、功放和播放任务就绪。
  // 不需要额外调用 ChipIntelliASR.begin()，也不需要固定延时。
  if (!ChipIntelliAudio.begin()) {
    Serial.println("Audio initialization failed");
    return;
  }

  // 音量范围为 0～100，其中 0 静音、100 最大。
  ChipIntelliAudio.setVolume(70);

  // 回调必须在 playVoice() 之前注册；也可以传入 context 指针作为第二参数。
  ChipIntelliAudio.onFinished(onPlaybackFinished);

  // 默认 interruptCurrent=true，会中断正在播放的提示音。
  // 返回 true 仅表示请求已被 SDK 接受，不表示播放已经结束。
  if (ChipIntelliAudio.playVoice(kVoiceId)) {
    Serial.println("Prompt request accepted");
  } else {
    Serial.println("Prompt playback request failed");
  }
}

void loop() {
  // 在 Arduino 主循环中处理回调事件，可以安全地打印或执行后续业务。
  if (playbackFinished) {
    playbackFinished = false;
    Serial.println("Prompt request completed");
  }
  delay(1);
}
