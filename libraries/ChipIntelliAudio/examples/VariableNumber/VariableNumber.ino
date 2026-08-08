#include <ChipIntelliAudio.h>

// The post-build resource generator converts these comments to voice.bin
// entries. Keep the IDs in sync with kNumberVoices below.
#define VOICE300 300  //零
#define VOICE301 301  //一
#define VOICE302 302  //二
#define VOICE303 303  //三
#define VOICE304 304  //四
#define VOICE305 305  //五
#define VOICE306 306  //六
#define VOICE307 307  //七
#define VOICE308 308  //八
#define VOICE309 309  //九
#define VOICE310 310  //十
#define VOICE311 311  //百
#define VOICE312 312  //千
#define VOICE313 313  //万
#define VOICE314 314  //亿
#define VOICE315 315  //负
#define VOICE316 316  //点

namespace {
volatile bool playbackFinished = false;
uint8_t nextNumber = 0;

void onPlaybackFinished(void *) {
  // Keep the SDK callback short. Start the next prompt from loop().
  playbackFinished = true;
}

void playNextNumber() {
  bool accepted = false;
  switch (nextNumber++) {
    case 0:
      Serial.println("Playing 300: 三百");
      accepted = ChipIntelliAudio.playVoice("300");
      break;
    case 1:
      Serial.println("Playing -1.5: 负一点五");
      accepted = ChipIntelliAudio.playVoice("-1.5");
      break;
    case 2:
      Serial.println("Playing 0.02: 零点零二");
      accepted = ChipIntelliAudio.playVoice("0.02");
      break;
    default:
      Serial.println("All variable-number prompts finished");
      return;
  }

  if (!accepted) {
    Serial.println("Variable prompt request failed");
    playbackFinished = true;
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  if (!ChipIntelliAudio.begin()) {
    Serial.println("Audio initialization failed");
    return;
  }

  ChipIntelliAudio.onFinished(onPlaybackFinished);
  playNextNumber();
}

void loop() {
  if (playbackFinished) {
    playbackFinished = false;
    delay(500);  // Make the three spoken values easier to distinguish.
    playNextNumber();
  }
  delay(1);
}
