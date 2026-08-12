#include <Ticker.h>

Ticker statusTicker;
volatile uint32_t tickerCount = 0;

void onTicker() {
  // The timer-service task posts this to the Arduino event dispatcher.
  ++tickerCount;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  if (!statusTicker.attach_ms(1000, onTicker)) {
    Serial.print("Ticker start failed: ");
    Serial.println(Ticker::errorString(statusTicker.lastError()));
  }
}

void loop() {
  static uint32_t printedCount = 0;
  if (printedCount != tickerCount) {
    printedCount = tickerCount;
    Serial.print("ticker ");
    Serial.println(printedCount);
  }
}
