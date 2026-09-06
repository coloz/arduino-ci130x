// SPDX-License-Identifier: MIT
// Compile the real HCIESPLinkTransport.cpp against an injected RPC endpoint.
#include <ESPLink.h>
#include "../src/utility/HCIESPLinkTransport.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

static uint32_t nowMs = 0;
unsigned long millis() { return nowMs; }
void delay(unsigned long ms) { nowMs += uint32_t(ms); }
ESPLinkClass ESPLink;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
static uint16_t op(c3::Opcode v) { return static_cast<uint16_t>(v); }

int32_t ESPLinkClass::request(uint16_t opcode, const uint8_t* data,
    size_t size, uint8_t* response, size_t& responseSize, uint32_t timeoutMs) {
  CHECK(timeoutMs > 0);
  FakeCall call = {opcode, {}};
  if (size) { CHECK(data != nullptr); call.payload.assign(data, data + size); }
  calls.push_back(call);
  if (opcode == failOpcode) return failStatus;
  std::vector<uint8_t> reply;
  if (opcode == op(c3::Opcode::HciBegin) || opcode == op(c3::Opcode::HciEnd)) {
    CHECK(size == 0);
  } else if (opcode == op(c3::Opcode::HciStatus)) {
    CHECK(size == 0);
    reply.resize(12);
    reply[0] = enabled;
    c3::put32(reply.data() + 1, queued);
    c3::put32(reply.data() + 5, dropped);
    c3::put16(reply.data() + 9, maxPacket);
    reply[11] = fault;
  } else if (opcode == op(c3::Opcode::HciRead)) {
    CHECK(size == 2 && c3::get16(data) == 256);
    if (!reads.empty()) { reply = std::move(reads.front()); reads.pop_front(); }
    CHECK(reply.size() <= c3::get16(data));
    if (changeOnRead) ++currentSession;
  } else if (opcode == op(c3::Opcode::HciWrite)) {
    CHECK(size > 0);
    reply.resize(2);
    c3::put16(reply.data(), uint16_t(size - (shortAccept ? 1 : 0)));
  } else CHECK(false);
  if (malformedOpcode == opcode) {
    // Report a malformed length without writing past the caller's capacity.
    responseSize = responseSize + 1;
    return 0;
  }
  CHECK(reply.size() <= responseSize);
  if (!reply.empty()) { CHECK(response != nullptr); std::copy(reply.begin(), reply.end(), response); }
  responseSize = reply.size();
  return 0;
}

static void reset() {
  ESPLink = ESPLinkClass();
  nowMs = 0;
}
static void begin(HCIESPLinkTransportClass& t) {
  CHECK(t.begin() == 1);
  CHECK(t.healthy());
  CHECK(ESPLink.calls.size() == 2);
  CHECK(ESPLink.calls[0].opcode == op(c3::Opcode::HciBegin));
  CHECK(ESPLink.calls[1].opcode == op(c3::Opcode::HciStatus));
}
static size_t count(c3::Opcode opcode) {
  return std::count_if(ESPLink.calls.begin(), ESPLink.calls.end(),
    [opcode](const FakeCall& c) { return c.opcode == op(opcode); });
}

static void testLifecycle() {
  reset();
  HCIESPLinkTransportClass t;
  CHECK(!t.healthy() && t.read() == -1);
  ESPLink.isReady = false;
  begin(t);
  CHECK(ESPLink.begins == 1 && t.session() == 42);
  CHECK(ESPLink.polls > 0);
  CHECK(t.begin() == 1 && count(c3::Opcode::HciBegin) == 1);
  t.end();
  CHECK(!t.healthy() && count(c3::Opcode::HciEnd) == 1 && ESPLink.isReady);
  t.end();
  CHECK(count(c3::Opcode::HciEnd) == 1);
  CHECK(t.begin() == 1 && t.healthy());
  reset();
  HCIESPLinkTransportClass bad;
  ESPLink.isReady = false; ESPLink.beginOK = false;
  CHECK(bad.begin() == 0 && bad.lastError() == BLE_ESPLINK_NOT_STARTED && ESPLink.calls.empty());
}

static void testStream() {
  reset();
  HCIESPLinkTransportClass t; begin(t);
  // Two H4 events are deliberately split inside headers and coalesced in a reply.
  ESPLink.reads = {{4}, {0x0e, 4, 1, 3, 12}, {0, 4, 0x0f, 4, 0, 1, 5, 32}};
  const std::vector<uint8_t> expected = {4, 0x0e, 4, 1, 3, 12, 0, 4, 0x0f, 4, 0, 1, 5, 32};
  std::vector<uint8_t> received;
  CHECK(t.available() == 1 && t.peek() == 4 && t.peek() == 4);
  for (unsigned guard = 0; received.size() < expected.size() && guard < 30; ++guard) {
    if (t.available()) received.push_back(uint8_t(t.read()));
    else delay(1);
  }
  CHECK(received == expected);
  CHECK(count(c3::Opcode::HciRead) == 3);
  CHECK(t.read() == -1);
  std::vector<uint8_t> full(256);
  for (size_t i = 0; i < full.size(); ++i) full[i] = uint8_t(i);
  ESPLink.reads.push_back(full); delay(1);
  CHECK(t.available() == 256);
  for (size_t i = 0; i < full.size(); ++i) CHECK(t.read() == int(i));
  CHECK(t.read() == -1);
}

static void testWrites() {
  reset();
  HCIESPLinkTransportClass t; begin(t);
  const uint8_t cmd[] = {1, 3, 12, 0};
  const uint8_t acl[] = {2, 1, 0x20, 4, 0, 0, 0, 4, 0};
  CHECK(t.write(cmd, sizeof(cmd)) == sizeof(cmd));
  CHECK(t.write(acl, sizeof(acl)) == sizeof(acl));
  CHECK(ESPLink.calls.back().payload == std::vector<uint8_t>(acl, acl + sizeof(acl)));
  const size_t sent = count(c3::Opcode::HciWrite);
  CHECK(t.write(acl, sizeof(acl) - 1) == 0);
  CHECK(t.lastError() == BLE_ESPLINK_INVALID_PACKET);
  CHECK(t.write(cmd, sizeof(cmd)) == 0 && count(c3::Opcode::HciWrite) == sent);
  t.end();
  CHECK(t.begin() == 1);
  ESPLink.shortAccept = true;
  CHECK(t.write(cmd, sizeof(cmd)) == 0 && t.lastError() == BLE_ESPLINK_BAD_REPLY);
  reset();
  HCIESPLinkTransportClass failed; begin(failed);
  ESPLink.failOpcode = op(c3::Opcode::HciWrite);
  CHECK(failed.write(cmd, sizeof(cmd)) == 0);
  CHECK(failed.lastError() == c3::OutcomeUnknown);
  CHECK(failed.write(cmd, sizeof(cmd)) == 0 && count(c3::Opcode::HciWrite) == 1);
}

static void testSessionChange() {
  reset();
  HCIESPLinkTransportClass t; begin(t);
  ESPLink.reads.push_back({4, 0x0e, 0});
  CHECK(t.available() == 3);
  ++ESPLink.currentSession;
  CHECK(t.read() == -1 && !t.healthy());
  CHECK(t.lastError() == BLE_ESPLINK_SESSION_CHANGED);
  t.end();
  CHECK(count(c3::Opcode::HciEnd) == 0);
  CHECK(t.begin() == 1 && t.session() == 43);
  ESPLink.changeOnRead = true; ESPLink.reads.push_back({4});
  CHECK(t.available() == 0 && !t.healthy());
  reset();
  HCIESPLinkTransportClass disconnected; begin(disconnected);
  ESPLink.isReady = false;
  CHECK(disconnected.available() == 0 && disconnected.lastError() == BLE_ESPLINK_SESSION_CHANGED);
  reset();
  HCIESPLinkTransportClass cooperative; begin(cooperative);
  ESPLink.changeOnPoll = true;
  CHECK(!cooperative.healthy());
  CHECK(cooperative.lastError() == BLE_ESPLINK_SESSION_CHANGED);
  cooperative.end();
  CHECK(count(c3::Opcode::HciEnd) == 0);
}

static void testFaults() {
  for (int mode = 0; mode < 5; ++mode) {
    reset(); HCIESPLinkTransportClass t;
    if (mode == 0) ESPLink.enabled = false;
    if (mode == 1) ESPLink.fault = true;
    if (mode == 2) ESPLink.dropped = 1;
    if (mode == 3) ESPLink.maxPacket = 250;
    if (mode == 4) ESPLink.malformedOpcode = op(c3::Opcode::HciStatus);
    CHECK(t.begin() == 0 && !t.healthy());
    CHECK(t.lastError() == (mode == 4 ? BLE_ESPLINK_BAD_REPLY : BLE_ESPLINK_CONTROLLER_FAULT));
    t.end(); CHECK(count(c3::Opcode::HciEnd) == 1);
  }
  reset(); HCIESPLinkTransportClass t; begin(t);
  ESPLink.dropped = 7; ESPLink.queued = 999; delay(1000);
  CHECK(t.available() == 0 && t.controllerDropped() == 7 && t.queuedBytes() == 999);
  CHECK(t.lastError() == BLE_ESPLINK_CONTROLLER_FAULT);
  reset(); HCIESPLinkTransportClass r; begin(r);
  ESPLink.failOpcode = op(c3::Opcode::HciRead); ESPLink.failStatus = c3::LinkLost;
  CHECK(r.available() == 0 && r.lastError() == c3::LinkLost);
  CHECK(r.available() == 0 && count(c3::Opcode::HciRead) == 1);
  reset(); HCIESPLinkTransportClass malformed; begin(malformed);
  ESPLink.malformedOpcode = op(c3::Opcode::HciRead);
  CHECK(malformed.available() == 0 && malformed.lastError() == BLE_ESPLINK_BAD_REPLY);
}

static void testWait() {
  reset(); HCIESPLinkTransportClass t; begin(t);
  t.wait(5);
  CHECK(nowMs == 5 && t.healthy());
  CHECK(count(c3::Opcode::HciRead) <= 6);
  const size_t prior = count(c3::Opcode::HciRead);
  t.available(); t.available(); t.available();
  CHECK(count(c3::Opcode::HciRead) == prior);
  // Poll and timeout arithmetic must work across the 32-bit millis rollover.
  nowMs = UINT32_MAX - 2;
  t.wait(5);
  CHECK(nowMs == 2 && t.healthy());
  ESPLink.reads.push_back({4}); delay(1);
  t.wait(100);
  CHECK(nowMs == 3 && t.peek() == 4);
}

int main() {
  testLifecycle(); testStream(); testWrites(); testSessionChange(); testFaults(); testWait();
  std::cout << "BLE ESPLink transport: " << checks << " assertions passed\n";
}
