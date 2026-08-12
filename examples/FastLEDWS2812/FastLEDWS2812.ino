// FastLED library dependency / FastLED 库依赖：
// This example requires the CI130X-enabled FastLED library from:
// 本示例需要使用已适配 CI130X 的 FastLED 库，请从以下仓库下载或克隆：
// https://github.com/coloz/FastLED
// In Arduino IDE, install the downloaded ZIP with:
// Sketch -> Include Library -> Add .ZIP Library...
// Arduino IDE 中可通过“项目 -> 加载库 -> 添加 .ZIP 库...”完成安装。

#include <FastLED.h>

// Wiring (CI-D06GT01D):
//   PD0 -> 330 ohm resistor -> WS2812 DIN
//   GND -> strip GND (the controller and strip must share ground)
//   External regulated 5 V -> strip 5 V; do not power a long strip from USB.
// A 1000 uF capacitor across strip 5 V/GND is recommended. A 74AHCT125 or
// 74HCT14 level shifter improves margin when a 5 V strip rejects 3.3 V data.
// PD0 is normally the onboard power-amplifier enable signal; this test
// intentionally repurposes it as the LED data output.
constexpr uint8_t DATA_PIN = PD0;
constexpr uint16_t NUM_LEDS = 8;
constexpr uint8_t BRIGHTNESS = 32;

CRGB leds[NUM_LEDS];

static void showSolid(const CRGB &color, const char *name) {
  fill_solid(leds, NUM_LEDS, color);
  FastLED.show();
  Serial.println(name);
  delay(700);
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS)
      .setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  Serial.println("CI130X FastLED WS2812 test");
  Serial.print("data pin: PD0, LEDs: ");
  Serial.println(NUM_LEDS);

  showSolid(CRGB::Red, "red");
  showSolid(CRGB::Green, "green");
  showSolid(CRGB::Blue, "blue");
  showSolid(CRGB::White, "white");
  FastLED.clear(true);
}

void loop() {
  static uint8_t hue = 0;
  fill_rainbow(leds, NUM_LEDS, hue++, 255 / NUM_LEDS);
  FastLED.show();
  delay(20);
}
