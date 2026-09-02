/*
  CI1306 SD card read/write verification with custom software-SPI pins.

  Wiring:
    SCK  - PA5
    MISO - PA2
    MOSI - PA3
    CS   - PA6
*/

#include <SPI.h>
#include <SD.h>

static const uint8_t kSckPin = PA5;
static const uint8_t kMisoPin = PA2;
static const uint8_t kMosiPin = PA3;
static const uint8_t kCsPin = PA6;
static const char kTestFile[] = "rwtest.bin";
static const uint32_t kTestSize = 4096;
static const size_t kChunkSize = 128;

static const char *testResult = "RESULT:RUNNING";

static uint8_t patternByte(uint32_t position) {
  return (uint8_t)((position * 37U + 11U) & 0xFFU);
}

static bool fail(const char *result) {
  testResult = result;
  Serial.println(result);
  SD.end();
  return false;
}

static bool runReadWriteTest() {
  uint8_t buffer[kChunkSize];

  Serial.println("STEP:SPI_ROUTE_CHECK");
  if (!SPI.begin(kSckPin, kMisoPin, kMosiPin, kCsPin)) {
    testResult = "RESULT:FAIL:SPI_BEGIN";
    Serial.println(testResult);
    return false;
  }
  Serial.print("SPI_PINS:");
  Serial.print(SPI.pinSCK());
  Serial.print(',');
  Serial.print(SPI.pinMISO());
  Serial.print(',');
  Serial.print(SPI.pinMOSI());
  Serial.print(',');
  Serial.println(SPI.pinSS());

  Serial.println("STEP:LOW_LEVEL_CARD_INIT");
  Sd2Card diagnosticCard;
  if (!diagnosticCard.init(SPI_HALF_SPEED, kCsPin)) {
    Serial.print("SD_ERROR_CODE:");
    Serial.println(diagnosticCard.errorCode());
    Serial.print("SD_ERROR_DATA:");
    Serial.println(diagnosticCard.errorData());
    diagnosticCard.end();
    testResult = "RESULT:FAIL:LOW_LEVEL_CARD_INIT";
    Serial.println(testResult);
    return false;
  }
  Serial.print("SD_CARD_TYPE:");
  Serial.println(diagnosticCard.type());
  diagnosticCard.end();

  Serial.println("STEP:SD_BEGIN");
  if (!SD.begin(kSckPin, kMisoPin, kMosiPin, kCsPin)) {
    return fail("RESULT:FAIL:SD_BEGIN");
  }

  if (SD.exists(kTestFile) && !SD.remove(kTestFile)) {
    return fail("RESULT:FAIL:REMOVE_OLD");
  }

  Serial.println("STEP:WRITE_4096");
  File output = SD.open(kTestFile, FILE_WRITE);
  if (!output) {
    Serial.print("SD_ERROR_CODE:");
    Serial.println(SD.cardErrorCode());
    Serial.print("SD_ERROR_DATA:");
    Serial.println(SD.cardErrorData());
    return fail("RESULT:FAIL:OPEN_WRITE");
  }

  for (uint32_t offset = 0; offset < kTestSize; offset += kChunkSize) {
    for (size_t i = 0; i < kChunkSize; ++i) {
      buffer[i] = patternByte(offset + i);
    }
    if (output.write(buffer, kChunkSize) != kChunkSize) {
      output.close();
      return fail("RESULT:FAIL:WRITE");
    }
  }
  output.flush();
  output.close();

  Serial.println("STEP:READ_VERIFY_4096");
  File input = SD.open(kTestFile, FILE_READ);
  if (!input) {
    return fail("RESULT:FAIL:OPEN_READ");
  }
  if (input.size() != kTestSize) {
    input.close();
    return fail("RESULT:FAIL:SIZE");
  }

  for (uint32_t offset = 0; offset < kTestSize; offset += kChunkSize) {
    const int count = input.read(buffer, kChunkSize);
    if (count != (int)kChunkSize) {
      input.close();
      return fail("RESULT:FAIL:READ");
    }
    for (size_t i = 0; i < kChunkSize; ++i) {
      if (buffer[i] != patternByte(offset + i)) {
        input.close();
        return fail("RESULT:FAIL:DATA_MISMATCH");
      }
    }
  }
  input.close();

  Serial.println("STEP:REMOVE_AND_END");
  if (!SD.remove(kTestFile) || SD.exists(kTestFile)) {
    return fail("RESULT:FAIL:REMOVE");
  }

  SD.end();
  if (SPI.pinSCK() != -1 || SPI.pinMISO() != -1 ||
      SPI.pinMOSI() != -1 || SPI.pinSS() != -1) {
    testResult = "RESULT:FAIL:SPI_NOT_RELEASED";
    Serial.println(testResult);
    return false;
  }

  testResult = "RESULT:PASS:4096_BYTES_VERIFIED";
  Serial.println(testResult);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("CI1306_SD_CUSTOM_PINS_TEST");
  Serial.println("PINS:SCK=PA5,MISO=PA2,MOSI=PA3,CS=PA6");
  runReadWriteTest();
}

void loop() {
  delay(2000);
  Serial.println(testResult);
}
