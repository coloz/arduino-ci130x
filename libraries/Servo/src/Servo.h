#pragma once

#include <Arduino.h>

#define Servo_VERSION 2

#define MIN_PULSE_WIDTH 544
#define MAX_PULSE_WIDTH 2400
#define DEFAULT_PULSE_WIDTH 1500
#define REFRESH_INTERVAL 20000

#define MAX_SERVOS 6
#define INVALID_SERVO 255

class Servo {
public:
  Servo();

  uint8_t attach(int pin);
  uint8_t attach(int pin, int minPulse, int maxPulse);
  void detach();

  void write(int value);
  void writeMicroseconds(int value);

  int read();
  int readMicroseconds();
  bool attached();

private:
  bool startPwm();
  void updatePwm();

  uint8_t _servoIndex;
  uint8_t _pin;
  int8_t _channel;
  uint16_t _minPulse;
  uint16_t _maxPulse;
  uint16_t _pulse;
  bool _attached;
};
