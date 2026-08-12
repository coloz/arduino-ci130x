#include <Servo.h>

#if defined(CI_CHIP_CI1302) || defined(CI_CHIP_CI1303)
// PC4 / PWM0 on the SSOP24 profiles.
constexpr uint8_t kServoPin = PC4;
#else
// PB3 / PWM4 on CI1306.
constexpr uint8_t kServoPin = PB3;
#endif

Servo servo;

void setup() {
  if (servo.attach(kServoPin) == INVALID_SERVO) {
    while (true) {
      delay(1000);
    }
  }
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
