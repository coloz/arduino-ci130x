#include <ChipIntelliAudio.h>

// 示例：通过串口命令测试语音 ID、命令词 ID/文本、语义 ID、停止和音量控制。
// 串口监视器波特率为 115200，输入字符后发送；回车和换行会被忽略。
//
// 下面的值与本示例 recursos 中的空调命令词和提示音资源一致。ID 16 是一段
// 较长的测试音，便于用 x 命令观察停止播放。
constexpr uint16_t kTestVoiceId = 16;
constexpr uint16_t kAirConditionerOnCommandId = 2;
constexpr uint32_t kAirConditionerOnSemanticId = 0x01E41943UL;
const char kAirConditionerOnText[] = "打开空调";

void printHelp() {
  Serial.println("v: voice ID, b: one beep, B: three beeps");
  Serial.println("c: command ID, s: semantic ID");
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
      accepted = ChipIntelliAudio.playVoice(kTestVoiceId);
      break;
    case 'b':
      accepted = ChipIntelliAudio.playBeep();
      break;
    case 'B':
      accepted = ChipIntelliAudio.playBeep(3);
      break;
    case 'c':
      // 按命令词 ID 播放其关联提示音，optionIndex 默认使用 -1。
      accepted = ChipIntelliAudio.playCommand(kAirConditionerOnCommandId);
      break;
    case 's':
      // 按语义 ID 播放其关联提示音。
      accepted = ChipIntelliAudio.playSemantic(kAirConditionerOnSemanticId);
      break;
    case 't':
      // 这是已配置命令词的精确查找，不会将任意文本转换为语音。
      accepted = ChipIntelliAudio.playCommand(kAirConditionerOnText);
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
