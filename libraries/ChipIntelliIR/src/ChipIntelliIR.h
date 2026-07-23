#pragma once

#include <stddef.h>
#include <stdint.h>

class ChipIntelliIRFactory;

class ChipIntelliIRClass {
public:
  static constexpr uint8_t DefaultTransmitPin = 2;
  static constexpr uint8_t DefaultReceivePin = 4;
  static constexpr uint8_t DefaultTimer = 2;
  static constexpr uint16_t DefaultDatabaseResourceId = 50000;
  static constexpr uint32_t OfficialDatabaseSize = 70716;
  static constexpr size_t MaxRawEntries = 1024;

  enum class Mode : uint8_t {
    None = 0,
    Raw,
    AirConditioner,
  };

  enum class Error : uint8_t {
    None = 0,
    AlreadyBegun,
    WrongMode,
    InvalidPin,
    InvalidTimer,
    InvalidArgument,
    ResourceBusy,
    Busy,
    NotReady,
    BufferTooSmall,
    DriverFailure,
    SdkStartFailed,
    FlashTimeout,
    DatabaseMissing,
    AliasBusy,
    AllocationFailed,
    AirCodeNotSelected,
  };

  enum class ReceiveStatus : uint8_t {
    Idle = 0,
    Receiving,
    Ready,
    Timeout,
    Error,
  };

  // Values deliberately match the official SDK eAirBrand enumeration.
  enum class AirBrand : uint8_t {
    LG = 0,
    Gree,
    Midea,
    Aux,
    Haier,
    Changhong,
    Chigo,
    TCL,
    Hisense,
    Panasonic,
    GreeMideaAuxHaierChanghong,
    Hitachi,
    Daikin,
    Mitsubishi,
    Xiaomi,
    Whirlpool,
    Galanz,
    Fujitsu,
    Sanshui,
    York,
    Skyworth,
    Shinco,
    Chunlan,
    Cheblo,
    Samsung,
    Aucma,
    Xinfei,
    Toshiba,
    Sampo,
    Yuetu,
    Yair,
    Amoi,
    Sharp,
    Konka,
    Rongshida,
    Toyo,
  };

  // Commands are the numeric interface documented by the official database.
  // The vendor SDK keeps ElectricHeatOn and EnergySavingOn at legacy values
  // 102 and 103. Values 207 and 209 have no documented public meaning.
  enum class AirCommand : uint16_t {
    PowerOn = 5,
    PowerOff = 6,
    FanHigh = 7,
    FanMedium = 8,
    FanLow = 9,
    FanAuto = 10,
    SwingStop = 11,
    SwingStart = 12,
    Temperature19 = 13,
    Temperature20 = 14,
    Temperature21 = 15,
    Temperature22 = 16,
    Temperature23 = 17,
    Temperature24 = 18,
    Temperature25 = 19,
    Temperature26 = 20,
    Temperature27 = 21,
    Temperature28 = 22,
    Temperature29 = 23,
    Temperature30 = 24,
    ModeCool = 25,
    ModeHeat = 26,
    ModeFan = 27,
    ModeDry = 28,
    ModeAuto = 29,
    Temperature16 = 30,
    Temperature17 = 31,
    Temperature18 = 32,
    SwingVertical = 68,
    SwingHorizontal = 69,
    SwingVerticalStop = 70,
    SwingHorizontalStop = 71,
    TemperatureUp = 72,
    TemperatureDown = 73,
    FanUp = 74,
    FanDown = 75,
    ElectricHeatOn = 102,
    EnergySavingOn = 103,
    SleepOff = 200,
    SleepMode1 = 201,
    SleepMode2 = 202,
    SleepMode3 = 203,
    AirCleanOff = 204,
    AirCleanOn = 205,
    ElectricHeatOff = 206,
    EnergySavingOff = 208,
    HealthOff = 210,
    HealthOn = 211,
    DisplayOff = 212,
    DisplayOn = 213,
    MuteOff = 214,
    MuteOn = 215,
    PowerfulOff = 216,
    PowerfulOn = 217,
    FollowMeOff = 218,
    FollowMeOn = 219,
    FreshAirOff = 220,
    FreshAirOn = 221,
    WindToPersonOff = 222,
    WindToPersonOn = 223,
  };

  enum class AirSearchType : uint8_t {
    AllBrands = 0,
    CurrentBrandModels,
  };

  enum class AirSearchEvent : uint8_t {
    CodeSent = 0,
    Completed,
    Stopped,
  };

  using AirSearchCallback = void (*)(AirSearchEvent event, int32_t codeId,
                                     void *context);

  ChipIntelliIRClass(const ChipIntelliIRClass &) = delete;
  ChipIntelliIRClass &operator=(const ChipIntelliIRClass &) = delete;

  // Raw mode and database mode are mutually exclusive for the lifetime of a
  // boot. Calling begin again with the same successful configuration is safe.
  // TIMER3 is valid silicon but reserved by the enabled SDK BLE stack; use
  // TIMER0 through TIMER2 in Arduino sketches.
  bool begin(uint8_t transmitPin = DefaultTransmitPin,
             uint8_t receivePin = DefaultReceivePin,
             uint8_t timer = DefaultTimer);
  bool beginAirConditioner(
      uint8_t transmitPin = DefaultTransmitPin,
      uint8_t receivePin = DefaultReceivePin,
      uint8_t timer = DefaultTimer,
      uint16_t resourceId = DefaultDatabaseResourceId);

  // Raw durations alternate mark/space and always begin with a mark. The
  // official driver currently uses a fixed 38 kHz carrier.
  bool sendRaw(const uint16_t *durationsUs, size_t count);
  bool sendNEC(uint8_t address, uint8_t command, uint8_t repeats = 0);
  bool sendExtendedNEC(uint16_t address, uint8_t command,
                       uint8_t repeats = 0);
  bool startReceive(uint32_t timeoutMs = 5000);
  bool stopReceive();
  ReceiveStatus receiveStatus();
  bool readRaw(uint16_t *durationsUs, size_t capacity, size_t &count);
  bool isBusy() const;

  bool selectAirBrand(AirBrand brand);
  bool selectAirCode(uint32_t codeId);
  uint32_t airCode() const;
  bool sendAir(AirCommand command);
  bool setTemperature(uint8_t celsius);
  bool power(bool on);
  bool startAirSearch(AirSearchType type, AirSearchCallback callback,
                      void *context = nullptr, uint8_t sendCount = 3,
                      uint32_t intervalMs = 3000);
  bool stopAirSearch();

  Mode mode() const;
  Error lastError() const;
  const char *errorString() const;
  static const char *errorString(Error error);

private:
  friend class ChipIntelliIRFactory;
  ChipIntelliIRClass();
  ~ChipIntelliIRClass() = default;

  bool beginConfiguration(uint8_t transmitPin, uint8_t receivePin,
                          uint8_t timer);
  bool ensureMutex();
  bool configurationMatches(uint8_t transmitPin, uint8_t receivePin,
                            uint8_t timer, uint16_t resourceId) const;
  ReceiveStatus pollReceiveStatus();
  bool sendAirUnlocked(AirCommand command);
  bool requireMode(Mode expected);
  void setError(Error error);

  Mode _mode;
  Error _lastError;
  ReceiveStatus _receiveStatus;
  uint8_t _transmitPin;
  uint8_t _receivePin;
  uint8_t _timer;
  uint16_t _resourceId;
  uint32_t _airCode;
  void *_mutex;
  bool _ready;
  bool _airCodeSelected;
  bool _airInitAttempted;
};

extern ChipIntelliIRClass &ChipIntelliIR;
