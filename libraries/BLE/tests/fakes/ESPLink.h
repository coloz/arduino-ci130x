// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>
#include <C3Protocol.h>
#include <deque>
#include <vector>
struct FakeCall { uint16_t opcode; std::vector<uint8_t> payload; };
class ESPLinkClass {
public:
  bool isReady = true, beginOK = true;
  uint32_t currentSession = 42;
  unsigned begins = 0, polls = 0;
  bool changeOnPoll = false;
  uint16_t failOpcode = 0;
  int32_t failStatus = c3::OutcomeUnknown;
  uint16_t malformedOpcode = 0;
  bool enabled = true, fault = false, shortAccept = false, changeOnRead = false;
  uint16_t maxPacket = 512;
  uint32_t dropped = 0, queued = 0;
  std::deque<std::vector<uint8_t>> reads;
  std::vector<FakeCall> calls;
  bool begin() { ++begins; isReady = beginOK; return beginOK; }
  void poll() { ++polls; if (changeOnPoll) { ++currentSession; changeOnPoll = false; } }
  bool ready() const { return isReady; }
  uint32_t session() const { return currentSession; }
  int32_t request(uint16_t opcode, const uint8_t* data, size_t size,
                  uint8_t* response, size_t& responseSize, uint32_t timeoutMs);
};
extern ESPLinkClass ESPLink;
