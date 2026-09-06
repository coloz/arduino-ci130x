// SPDX-License-Identifier: MIT
// Compile the real production ESPLink.cpp without CI13XX/FreeRTOS declarations.
#include <ESPLink.h>
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <vector>

static uint32_t clockMs;
static unsigned checks;
static std::function<void()> yieldHook;
unsigned long millis() { return clockMs; }
unsigned long micros() { return uint32_t(clockMs * 1000U); }
void yield() { ++clockMs; if (yieldHook) yieldHook(); }
#define CHECK(test) do { ++checks; if (!(test)) { std::cerr << __FILE__ << ':' << __LINE__ << ": " #test "\n"; std::exit(1); } } while (0)

class FakeSerial final : public Stream {
 public:
  unsigned begins = 0, ends = 0, writes = 0, hellos = 0, pings = 0, acks = 0;
  unsigned executions = 0, requestFrames = 0, duplicateFrames = 0;
  unsigned dropRequests = 0, dropResponses = 0, corruptResponses = 0, dropHelloReplies = 0;
  uint16_t helloMaxPayload = c3::MaxPayload;
  bool stallAck = false;
  uint32_t baud = 0, remoteSession = 41, helloNonce = 0;
  size_t maxWrite = std::numeric_limits<size_t>::max();
  unsigned zeroWrites = 0;
  bool offline = false, discardReplies = false, replyPings = true;
  bool closeLink = false, staleResponseFirst = false;
  int32_t responseStatus = c3::Ok;
  std::function<void()> writeHook;
  std::deque<uint8_t> receiveBytes;
  std::vector<c3::Frame> submitted;
  c3::Frame cachedRequest, cachedResponse;
  bool haveCache = false;
  c3::Decoder parser;

  void begin(uint32_t speed) { ++begins; baud = speed; }
  void end() { ++ends; }
  int available() override { return int(receiveBytes.size()); }
  int read() override {
    if (receiveBytes.empty()) return -1;
    const int b = receiveBytes.front(); receiveBytes.pop_front(); return b;
  }
  size_t write(const uint8_t* data, size_t size) override {
    ++writes;
    if (writeHook) writeHook();
    if (zeroWrites) { --zeroWrites; return 0; }
    const size_t accepted = std::min(size, maxWrite);
    for (size_t i = 0; i < accepted; ++i) {
      c3::Frame frame;
      if (parser.feed(data[i], frame)) process(frame);
    }
    return accepted;
  }
  void enqueue(const c3::Frame& frame, bool corrupt = false) {
    uint8_t wire[c3::MaxWire];
    size_t size;
    if (corrupt) {
      uint8_t encoded[c3::MaxWire], raw[c3::MaxDecoded];
      const size_t encodedSize = c3::encode(frame, encoded, sizeof(encoded));
      CHECK(encodedSize > 4);
      // Preserve the COBS syntax and damage only the checksum.
      const size_t rawSize = c3::cobsDecode(encoded + 1, encodedSize - 2, raw, sizeof(raw));
      CHECK(rawSize > 4);
      raw[rawSize - 1] ^= 0x80;
      wire[0] = 0;
      const size_t body = c3::cobsEncode(raw, rawSize, wire + 1, sizeof(wire) - 2);
      CHECK(body != 0);
      wire[body + 1] = 0; size = body + 2;
    } else size = c3::encode(frame, wire, sizeof(wire));
    CHECK(size != 0);
    receiveBytes.insert(receiveBytes.end(), wire, wire + size);
  }
  void process(const c3::Frame& frame) {
    submitted.push_back(frame);
    if (offline) return;
    if (frame.type == c3::Type::Hello) {
      ++hellos;
      if (frame.id != helloNonce) { helloNonce = frame.id; ++remoteSession; haveCache = false; }
      c3::Frame reply;
      reply.type = c3::Type::HelloReply; reply.id = frame.id; reply.session = remoteSession;
      c3::Writer out(reply.payload, sizeof(reply.payload));
      out.u16(helloMaxPayload); out.u32(c3::WiFi | c3::HCI);
      out.u32(77); out.u8(6); out.u8(1); out.u16(512); out.string("fake-c3-portable");
      CHECK(out.ok()); reply.size = uint16_t(out.size());
      if (dropHelloReplies) --dropHelloReplies; else enqueue(reply);
      return;
    }
    if (frame.type == c3::Type::Ack) { ++acks; return; }
    if (frame.type == c3::Type::Ping) {
      ++pings;
      if (replyPings && frame.session == remoteSession) {
        c3::Frame reply; reply.type = c3::Type::Pong; reply.session = remoteSession; enqueue(reply);
      }
      return;
    }
    CHECK(frame.type == c3::Type::Request);
    ++requestFrames;
    if (dropRequests) { --dropRequests; return; }
    CHECK(frame.session == remoteSession);
    if (haveCache && frame.id == cachedRequest.id) {
      ++duplicateFrames;
      CHECK(frame.opcode == cachedRequest.opcode && frame.size == cachedRequest.size);
      CHECK(!std::memcmp(frame.payload, cachedRequest.payload, frame.size));
    } else {
      CHECK(!haveCache || frame.id > cachedRequest.id);
      cachedRequest = frame; cachedResponse = frame;
      cachedResponse.type = c3::Type::Response;
      cachedResponse.status = closeLink ? c3::LinkLost : responseStatus;
      if (cachedResponse.status != c3::Ok) cachedResponse.size = 0;
      haveCache = true; ++executions;
    }
    if (discardReplies) return;
    if (dropResponses) { --dropResponses; return; }
    if (staleResponseFirst) {
      staleResponseFirst = false;
      auto stale = cachedResponse; --stale.session; enqueue(stale);
    }
    const bool corrupt = corruptResponses != 0;
    if (corrupt) --corruptResponses;
    enqueue(cachedResponse, corrupt);
    if (stallAck) maxWrite = 0;
  }
};

static int32_t echo(ESPLinkClass& link, const std::vector<uint8_t>& data,
                    std::vector<uint8_t>& response, uint32_t timeout = 1500) {
  size_t size = response.size();
  const int32_t result = link.request(c3::Opcode::Echo, data.data(), data.size(),
                                    response.data(), size, timeout);
  if (size <= response.size()) response.resize(size);
  return result;
}
static void start(ESPLinkClass& link, FakeSerial& serial) {
  CHECK(link.begin(serial, 115200, 1000));
  CHECK(link.ready() && link.connected() && link.session() == serial.remoteSession);
  CHECK(serial.begins == 1 && serial.baud == 115200 && serial.ends == 0);
}

static void testNoImplicitPortAndLifecycle() {
  clockMs = 0;
  ESPLinkClass link;
  CHECK(!link.begin() && !link.ready() && link.session() == 0);
  CHECK(link.lastError() == c3::NotConnected);
  link.poll(); link.end();
  FakeSerial serial;
  start(link, serial);
  const auto caps = link.capabilities();
  CHECK(caps.bootID == 77 && caps.maxSockets == 6 && caps.maxBLEConnections == 1);
  CHECK(caps.maxPayload == 1024 && caps.maxHciPacket == 512);
  CHECK(!std::strcmp(caps.firmwareVersion, "fake-c3-portable"));
  CHECK(link.begin() && serial.begins == 1 && serial.hellos == 1);
  link.end();
  CHECK(!link.ready() && serial.ends == 1 && link.session() == 0);
  link.end(); CHECK(serial.ends == 1);
  CHECK(link.begin() && serial.begins == 2 && serial.hellos == 2);
  CHECK(link.stats().sessionChanges == 2);
  FakeSerial another;
  CHECK(link.begin(another, 921600, 1000));
  CHECK(serial.ends == 2 && another.begins == 1 && another.baud == 921600);
  link.end(); CHECK(another.ends == 1);
}

static void testDestructionOwnership() {
  clockMs = 0;
  FakeSerial managed;
  {
    ESPLinkClass link;
    start(link, managed);
    CHECK(link.ping());
  }
  CHECK(managed.ends == 1);
  FakeSerial borrowed;
  borrowed.begin(230400);
  {
    ESPLinkClass link;
    CHECK(link.begin(static_cast<Stream&>(borrowed), 1000));
    CHECK(link.ping());
  }
  CHECK(borrowed.begins == 1 && borrowed.ends == 0);
}

static void testHandshakeRecovery() {
  clockMs = 0;
  ESPLinkClass link; FakeSerial serial;
  serial.dropHelloReplies = 1;
  start(link, serial);
  CHECK(serial.hellos == 2 && serial.remoteSession == 42);
  CHECK(link.stats().retries == 1 && link.stats().sessionChanges == 1);
  link.end();
  serial.helloMaxPayload = 512;
  CHECK(!link.begin());
  CHECK(link.lastError() == c3::ProtocolError && !link.ready());
  serial.helloMaxPayload = c3::MaxPayload;
  CHECK(link.begin() && link.ready());
  link.end();
  serial.offline = true;
  const uint32_t began = clockMs;
  CHECK(!link.begin(serial, 115200, 20));
  CHECK(clockMs - began == 20 && link.lastError() == c3::Timeout);
  serial.offline = false;
  CHECK(link.begin());
  link.end();
}

static void testBorrowedPort() {
  clockMs = 0;
  ESPLinkClass link; FakeSerial serial;
  serial.begin(230400);
  CHECK(link.begin(static_cast<Stream&>(serial), 1000));
  CHECK(serial.begins == 1 && serial.ends == 0 && serial.baud == 230400);
  CHECK(link.ping());
  link.end(); CHECK(serial.ends == 0);
  CHECK(link.begin()); CHECK(serial.begins == 1);
  link.end(); CHECK(serial.ends == 0);
}

static void testPartialWritesAndFullFrame() {
  clockMs = 0;
  ESPLinkClass link; FakeSerial serial;
  serial.maxWrite = 7; serial.zeroWrites = 3;
  start(link, serial);
  std::vector<uint8_t> data(c3::MaxPayload), response(c3::MaxPayload);
  for (size_t i = 0; i < data.size(); ++i) data[i] = uint8_t(i * 17);
  CHECK(echo(link, data, response) == c3::Ok && data == response);
  CHECK(serial.executions == 1 && serial.duplicateFrames == 0);
  CHECK(serial.parser.crcErrors == 0 && serial.parser.malformed == 0);
  CHECK(serial.writes > 150 && serial.acks == 1);
  CHECK(link.stats().transmitted >= 3);
  link.end();
}

static void testRetryCorruptionAndDedup() {
  for (int mode = 0; mode < 3; ++mode) {
    clockMs = 0;
    ESPLinkClass link; FakeSerial serial; start(link, serial);
    serial.dropRequests = mode == 0;
    serial.dropResponses = mode == 1;
    serial.corruptResponses = mode == 2;
    std::vector<uint8_t> data = {0, 1, 2, 255, 0}, response(20);
    const uint32_t before = clockMs;
    CHECK(echo(link, data, response) == c3::Ok && data == response);
    CHECK(clockMs - before >= c3::RetryMs);
    CHECK(serial.requestFrames == 2 && serial.executions == 1);
    CHECK(serial.duplicateFrames == unsigned(mode != 0));
    CHECK(link.stats().retries == 1);
    CHECK(link.stats().crcErrors == unsigned(mode == 2));
    CHECK(link.ready()); link.end();
  }
}

static void testOutcomeUnknownAndRecovery() {
  clockMs = 0;
  ESPLinkClass link; FakeSerial serial; start(link, serial);
  const uint32_t originalSession = link.session();
  serial.discardReplies = true;
  std::vector<uint8_t> data = {7, 8}, response(20);
  CHECK(echo(link, data, response, 750) == c3::OutcomeUnknown);
  CHECK(!link.ready() && link.session() == 0 && response.empty());
  CHECK(serial.executions == 1 && serial.requestFrames == 3 && serial.duplicateFrames == 2);
  const unsigned before = serial.requestFrames;
  response.resize(20); CHECK(echo(link, data, response) == c3::LinkLost);
  CHECK(serial.requestFrames == before);
  serial.discardReplies = false;
  CHECK(link.begin() && link.session() != originalSession);
  CHECK(serial.begins == 1 && serial.ends == 0);
  response.resize(20); CHECK(echo(link, data, response) == c3::Ok && data == response);
  CHECK(serial.executions == 2);
  link.end();
}

static void testStaleSessionAndRemoteLoss() {
  clockMs = 0;
  ESPLinkClass link; FakeSerial serial; start(link, serial);
  serial.staleResponseFirst = true;
  std::vector<uint8_t> data = {3, 2, 1}, response(10);
  CHECK(echo(link, data, response) == c3::Ok && data == response);
  CHECK(link.stats().ignored == 1 && link.ready());
  serial.closeLink = true;
  response.resize(10); CHECK(echo(link, data, response) == c3::LinkLost);
  CHECK(!link.ready() && link.session() == 0);
  link.end();
}

static void testBuffersAndArguments() {
  clockMs = 0;
  ESPLinkClass link; FakeSerial serial; start(link, serial);
  const uint8_t data[5] = {1, 2, 3, 4, 5};
  uint8_t response[2] = {}; size_t size = sizeof(response);
  CHECK(link.request(c3::Opcode::Echo, data, sizeof(data), response, size) == c3::BufferTooSmall);
  CHECK(size == 5 && link.ready() && serial.executions == 1);
  size = 2;
  CHECK(link.request(c3::Opcode::Echo, nullptr, 1, response, size) == c3::InvalidArgument && size == 0);
  size = 2;
  CHECK(link.request(c3::Opcode::Echo, data, sizeof(data), nullptr, size) == c3::InvalidArgument && size == 0);
  size = 2;
  CHECK(link.request(c3::Opcode::Echo, data, sizeof(data), response, size, 0) == c3::InvalidArgument && size == 0);
  size = 2;
  CHECK(link.request(c3::Opcode::Echo, data, c3::MaxPayload + 1, response, size) == c3::InvalidArgument && size == 0);
  CHECK(serial.executions == 1);
  serial.responseStatus = c3::Unsupported;
  size = 2;
  CHECK(link.request(c3::Opcode::Echo, nullptr, 0, response, size) == c3::Unsupported);
  CHECK(size == 0 && link.ready());
  link.end();
}

static void testReentrantDuringTransaction() {
  clockMs = 0;
  ESPLinkClass link; FakeSerial serial; start(link, serial);
  bool invoked = false;
  serial.writeHook = [&]() {
    if (invoked) return;
    invoked = true;
    uint8_t reply[2]; size_t size = sizeof(reply);
    CHECK(link.request(c3::Opcode::Echo, nullptr, 0, reply, size, 50) == c3::Busy && size == 0);
    link.end(); CHECK(link.ready() && serial.ends == 0 && link.lastError() == c3::Busy);
    CHECK(!link.begin(serial, 230400, 50));
    CHECK(serial.begins == 1 && serial.baud == 115200);
    link.poll();
  };
  CHECK(link.ping() && invoked && link.ready());
  CHECK(link.lastError() == c3::Ok);
  serial.writeHook = {}; link.end();
}

static void testHeartbeatAndRollover() {
  clockMs = UINT32_MAX - 1500;
  ESPLinkClass link; FakeSerial serial; start(link, serial);
  const unsigned initial = serial.writes;
  clockMs += 1001;
  CHECK(serial.writes == initial); // Cooperative backend performs no hidden work.
  link.poll(); CHECK(serial.pings == 1); link.poll();
  clockMs += 1001; link.poll(); link.poll(); CHECK(serial.pings == 2 && link.ready());
  serial.replyPings = false;
  for (int i = 0; i < 6; ++i) { clockMs += 1000; link.poll(); }
  CHECK(!link.ready() && link.lastError() == c3::LinkLost && link.session() == 0);
  link.end();
}

static void testShortWriteDeadline() {
  clockMs = 0;
  ESPLinkClass link; FakeSerial serial; start(link, serial);
  serial.maxWrite = 0;
  const uint32_t began = clockMs;
  uint8_t reply[4]; size_t size = sizeof(reply);
  CHECK(link.request(c3::Opcode::Echo, nullptr, 0, reply, size, 10) == c3::OutcomeUnknown);
  CHECK(clockMs - began <= 11);
  CHECK(serial.executions == 0 && !link.ready());
  link.end();
}

static void testResponseAckDeadline() {
  clockMs = 0;
  ESPLinkClass link; FakeSerial serial; start(link, serial);
  serial.stallAck = true;
  const uint32_t began = clockMs;
  std::vector<uint8_t> data = {1, 2}, response(2);
  CHECK(echo(link, data, response, 10) == c3::Ok && data == response);
  CHECK(clockMs - began <= 11);
  CHECK(serial.executions == 1 && link.ready());
  serial.maxWrite = 99; serial.stallAck = false;
  CHECK(link.ping());
  link.end();
}

static void testReentrantDuringIdlePoll() {
  clockMs = 0;
  ESPLinkClass link; FakeSerial serial; start(link, serial);
  bool invoked = false;
  serial.maxWrite = 7;
  serial.writeHook = [&]() {
    if (invoked) return;
    invoked = true;
    uint8_t reply[4]; size_t size = sizeof(reply);
    CHECK(link.request(c3::Opcode::Echo, nullptr, 0, reply, size, 20) == c3::Busy);
    CHECK(!link.begin(serial, 230400, 20));
    link.end(); CHECK(link.ready() && serial.ends == 0 && link.lastError() == c3::Busy);
  };
  clockMs += 1001; link.poll();
  CHECK(invoked && serial.pings == 1 && serial.begins == 1);
  serial.writeHook = {}; link.poll(); CHECK(link.ready()); link.end();
}

int main() {
  testNoImplicitPortAndLifecycle(); testDestructionOwnership(); testHandshakeRecovery(); testBorrowedPort(); testPartialWritesAndFullFrame();
  testRetryCorruptionAndDedup(); testOutcomeUnknownAndRecovery();
  testStaleSessionAndRemoteLoss(); testBuffersAndArguments();
  testReentrantDuringTransaction(); testHeartbeatAndRollover();
  testShortWriteDeadline(); testResponseAckDeadline(); testReentrantDuringIdlePoll();
  std::cout << "ESPLink cooperative production runtime: " << checks << " assertions passed\n";
}
