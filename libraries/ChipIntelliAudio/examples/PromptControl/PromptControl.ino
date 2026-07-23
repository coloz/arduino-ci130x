#include <ChipIntelliAudio.h>

// 示例：通过串口命令测试语音 ID、命令词 ID/文本、语义 ID、停止和音量控制。
// 串口监视器波特率为 115200，输入字符后发送；回车和换行会被忽略。
//
// 请把下面的值替换为草图 recursos 中模型/命令词包实际配置的 ID 和文本。
// 程序启动时不会自动播放，只有收到串口命令后才会发起播放请求。
constexpr uint16_t kVoiceId = 1;
constexpr uint16_t kCommandId = 1;
constexpr uint32_t kSemanticId = 1;
const char kCommandText[] = "replace with a configured command";

void printHelp() {
  Serial.println("v: voice ID, c: command ID, s: semantic ID");
  Serial.println("t: command text, x: stop, + / -: volume, m: mute");
}

void setup() {
  Serial.begin(115200);

  // begin() 已完成共享 SDK 和音频硬件的初始化等待，无需额外延时。
  if (!ChipIntelliAudio.begin()) {
    Serial.println("Audio initialization failed");
    return;
  }

  // 音量有效范围为 0～100。
  ChipIntelliAudio.setVolume(70);
  printHelp();
}

void loop() {
  // 没有串口输入时主动让出 CPU 时间给 SDK/FreeRTOS 任务。
  if (!Serial.available()) {
    delay(1);
    return;
  }

  char command = static_cast<char>(Serial.read());
  bool accepted = true;

  switch (command) {
    case 'v':
      // 按 voice.bin 中的语音 ID 播放。
      accepted = ChipIntelliAudio.playVoice(kVoiceId);
      break;
    case 'c':
      // 按命令词 ID 播放其关联提示音，optionIndex 默认使用 -1。
      accepted = ChipIntelliAudio.playCommand(kCommandId);
      break;
    case 's':
      // 按语义 ID 播放其关联提示音。
      accepted = ChipIntelliAudio.playSemantic(kSemanticId);
      break;
    case 't':
      // 这是已配置命令词的精确查找，不会将任意文本转换为语音。
      accepted = ChipIntelliAudio.playCommand(kCommandText);
      break;
    case 'x':
      // stop() 提交停止请求；SDK 不提供最终是否已进入空闲的状态码。
      accepted = ChipIntelliAudio.stop();
      break;
    case 'm':
      ChipIntelliAudio.setMuted(!ChipIntelliAudio.isMuted());
      break;
    case '+':
      // setVolume() 会把 100 以上的 uint8_t 值限制为 100。
      ChipIntelliAudio.setVolume(ChipIntelliAudio.volume() + 10U);
      break;
    case '-': {
      // 手动做下限保护，避免无符号整数减法下溢。
      uint8_t current = ChipIntelliAudio.volume();
      ChipIntelliAudio.setVolume(current > 10U ? current - 10U : 0U);
      break;
    }
    case '\r':
    case '\n':
      return;
    default:
      printHelp();
      return;
  }

  // accepted 只表示 SDK 是否接受请求；playing 是打印瞬间的播放状态。
  Serial.print(accepted ? "accepted" : "rejected");
  Serial.print("; playing=");
  Serial.print(ChipIntelliAudio.isPlaying() ? "yes" : "no");
  Serial.print("; volume=");
  Serial.print(ChipIntelliAudio.volume());
  Serial.print("; muted=");
  Serial.println(ChipIntelliAudio.isMuted() ? "yes" : "no");
}
