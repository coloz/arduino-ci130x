// Servo library dependency / Servo 库依赖：
// This example requires the CI130X-enabled Servo library from:
// 本示例需要使用已适配 CI130X 的 Servo 库，请从以下仓库下载或克隆：
// https://github.com/coloz/Servo
// In Arduino IDE, install the downloaded ZIP with:
// Sketch -> Include Library -> Add .ZIP Library...
// Arduino IDE 中可通过“项目 -> 加载库 -> 添加 .ZIP 库...”完成安装。

#include <Servo.h>

// Wiring / 接线：
//   CI1302/CI1303: PA5 -> servo signal
//   CI1306:        PB3 -> servo signal
//   External regulated supply -> servo V+; supply GND -> servo GND and board GND
//   CI1302/CI1303：PA5 接舵机信号线
//   CI1306：PB3 接舵机信号线
// 舵机应使用合适的外部稳压电源，并确保电源、舵机和开发板共地。
// Do not power the servo motor from a GPIO pin.
// 不要使用 GPIO 引脚为舵机供电。

#if defined(CI_CHIP_CI1302) || defined(CI_CHIP_CI1303)
constexpr uint8_t SERVO_PIN = PA5;
#else
constexpr uint8_t SERVO_PIN = PB3;
#endif

Servo servo;

void setup() {
  Serial.begin(115200);

  if (servo.attach(SERVO_PIN) == INVALID_SERVO) {
    Serial.println("Servo attach failed: check the selected pin and PWM resources.");
    while (true) {
      delay(1000);
    }
  }

  servo.write(90);
  Serial.println("CI130X Servo sweep test started");
  delay(1000);
}

void loop() {
  for (int angle = 0; angle <= 180; ++angle) {
    servo.write(angle);
    delay(15);
  }

  for (int angle = 180; angle >= 0; --angle) {
    servo.write(angle);
    delay(15);
  }
}
