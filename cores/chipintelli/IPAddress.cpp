/*
  IPAddress.cpp - Base class that provides IPAddress
  Copyright (c) 2011 Adrian McEwen.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "IPAddress.h"
#include "Print.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace {

class StringPrint : public Print {
public:
  explicit StringPrint(String &output) : _output(output) {}

  size_t write(uint8_t value) override {
    return _output.concat(static_cast<char>(value)) ? 1U : 0U;
  }

private:
  String &_output;
};

int hexDigit(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

size_t printHex16(Print &printer, unsigned int value) {
  char buffer[5];
  char *cursor = &buffer[sizeof(buffer)];
  *--cursor = '\0';
  do {
    const unsigned int digit = value & 0x0fU;
    *--cursor = digit < 10U ? static_cast<char>('0' + digit) : static_cast<char>('a' + digit - 10U);
    value >>= 4;
  } while (value != 0);
  return printer.print(cursor);
}

}  // namespace

IPAddress::IPAddress() : IPAddress(IPv4) {}

IPAddress::IPAddress(IPType ip_type) : _type(ip_type), _zone(0) {
  memset(_address.bytes, 0, sizeof(_address.bytes));
}

IPAddress::IPAddress(uint8_t first_octet, uint8_t second_octet, uint8_t third_octet, uint8_t fourth_octet)
  : IPAddress(IPv4) {
  _address.bytes[IPADDRESS_V4_BYTES_INDEX] = first_octet;
  _address.bytes[IPADDRESS_V4_BYTES_INDEX + 1] = second_octet;
  _address.bytes[IPADDRESS_V4_BYTES_INDEX + 2] = third_octet;
  _address.bytes[IPADDRESS_V4_BYTES_INDEX + 3] = fourth_octet;
}

IPAddress::IPAddress(
  uint8_t o1, uint8_t o2, uint8_t o3, uint8_t o4, uint8_t o5, uint8_t o6, uint8_t o7, uint8_t o8,
  uint8_t o9, uint8_t o10, uint8_t o11, uint8_t o12, uint8_t o13, uint8_t o14, uint8_t o15,
  uint8_t o16, uint8_t z
) : _type(IPv6), _zone(z) {
  const uint8_t bytes[16] = {o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12, o13, o14, o15, o16};
  memcpy(_address.bytes, bytes, sizeof(_address.bytes));
}

IPAddress::IPAddress(uint32_t address) : IPAddress(IPv4) {
  _address.dword[IPADDRESS_V4_DWORD_INDEX] = address;
}

IPAddress::IPAddress(const uint8_t *address) : IPAddress(IPv4, address) {}

IPAddress::IPAddress(IPType ip_type, const uint8_t *address, uint8_t z) : IPAddress(ip_type) {
  if (address == nullptr) {
    return;
  }
  if (ip_type == IPv4) {
    memcpy(&_address.bytes[IPADDRESS_V4_BYTES_INDEX], address, sizeof(uint32_t));
  } else {
    memcpy(_address.bytes, address, sizeof(_address.bytes));
    _zone = z;
  }
}

IPAddress::IPAddress(const char *address) : IPAddress(IPv4) {
  fromString(address);
}

IPAddress::IPAddress(const IPAddress &address) {
  *this = address;
}

bool IPAddress::fromString(const char *address) {
  if (address == nullptr) {
    return false;
  }

  IPAddress parsed(IPv4);
  if (!parsed.fromString4(address) && !parsed.fromString6(address)) {
    return false;
  }
  *this = parsed;
  return true;
}

bool IPAddress::fromString4(const char *address) {
  uint8_t bytes[4] = {0, 0, 0, 0};
  int accumulator = -1;
  uint8_t dots = 0;

  if (address == nullptr || *address == '\0') {
    return false;
  }

  while (*address != '\0') {
    const char value = *address++;
    if (value >= '0' && value <= '9') {
      accumulator = accumulator < 0 ? value - '0' : accumulator * 10 + value - '0';
      if (accumulator > 255) {
        return false;
      }
    } else if (value == '.') {
      if (dots >= 3 || accumulator < 0) {
        return false;
      }
      bytes[dots++] = static_cast<uint8_t>(accumulator);
      accumulator = -1;
    } else {
      return false;
    }
  }

  if (dots != 3 || accumulator < 0) {
    return false;
  }
  bytes[3] = static_cast<uint8_t>(accumulator);

  memset(_address.bytes, 0, sizeof(_address.bytes));
  memcpy(&_address.bytes[IPADDRESS_V4_BYTES_INDEX], bytes, sizeof(bytes));
  _type = IPv4;
  _zone = 0;
  return true;
}

bool IPAddress::fromString6(const char *address) {
  uint8_t bytes[16] = {0};
  int groups = 0;
  int compressedAt = -1;
  uint8_t zone = 0;

  if (address == nullptr || *address == '\0') {
    return false;
  }

  const char *cursor = address;
  if (*cursor == ':') {
    if (*(cursor + 1) != ':') {
      return false;
    }
    compressedAt = 0;
    cursor += 2;
  }

  while (*cursor != '\0' && *cursor != '%') {
    if (groups >= 8) {
      return false;
    }

    unsigned int field = 0;
    int digits = 0;
    while (*cursor != '\0' && *cursor != ':' && *cursor != '%') {
      const int digit = hexDigit(*cursor++);
      if (digit < 0 || ++digits > 4) {
        return false;
      }
      field = field * 16U + static_cast<unsigned int>(digit);
    }
    if (digits == 0) {
      return false;
    }

    bytes[groups * 2] = static_cast<uint8_t>(field >> 8);
    bytes[groups * 2 + 1] = static_cast<uint8_t>(field);
    ++groups;

    if (*cursor != ':') {
      break;
    }
    ++cursor;
    if (*cursor == ':') {
      if (compressedAt >= 0) {
        return false;
      }
      compressedAt = groups;
      ++cursor;
    } else if (*cursor == '\0' || *cursor == '%') {
      return false;
    }
  }

  if (compressedAt >= 0) {
    const int missingGroups = 8 - groups;
    if (missingGroups < 1) {
      return false;
    }
    memmove(
      &bytes[(compressedAt + missingGroups) * 2],
      &bytes[compressedAt * 2],
      static_cast<size_t>(groups - compressedAt) * 2U
    );
    memset(&bytes[compressedAt * 2], 0, static_cast<size_t>(missingGroups) * 2U);
    groups = 8;
  }

  if (groups != 8) {
    return false;
  }

  if (*cursor == '%') {
    ++cursor;
    if (*cursor == '\0') {
      return false;
    }
    char *end = nullptr;
    const unsigned long parsedZone = strtoul(cursor, &end, 10);
    if (*end != '\0' || parsedZone > 254UL) {
      return false;
    }
    zone = static_cast<uint8_t>(parsedZone + 1UL);
  } else if (*cursor != '\0') {
    return false;
  }

  memcpy(_address.bytes, bytes, sizeof(_address.bytes));
  _type = IPv6;
  _zone = zone;
  return true;
}

IPAddress &IPAddress::operator=(const uint8_t *address) {
  _type = IPv4;
  _zone = 0;
  memset(_address.bytes, 0, sizeof(_address.bytes));
  if (address != nullptr) {
    memcpy(&_address.bytes[IPADDRESS_V4_BYTES_INDEX], address, sizeof(uint32_t));
  }
  return *this;
}

IPAddress &IPAddress::operator=(const char *address) {
  fromString(address);
  return *this;
}

IPAddress &IPAddress::operator=(uint32_t address) {
  _type = IPv4;
  _zone = 0;
  memset(_address.bytes, 0, sizeof(_address.bytes));
  _address.dword[IPADDRESS_V4_DWORD_INDEX] = address;
  return *this;
}

IPAddress &IPAddress::operator=(const IPAddress &address) {
  if (this != &address) {
    _type = address._type;
    _zone = address._zone;
    memcpy(_address.bytes, address._address.bytes, sizeof(_address.bytes));
  }
  return *this;
}

bool IPAddress::operator==(const IPAddress &address) const {
  return _type == address._type
         && memcmp(_address.bytes, address._address.bytes, sizeof(_address.bytes)) == 0;
}

bool IPAddress::operator==(const uint8_t *address) const {
  return address != nullptr
         && _type == IPv4
         && memcmp(address, &_address.bytes[IPADDRESS_V4_BYTES_INDEX], sizeof(uint32_t)) == 0;
}

uint8_t IPAddress::operator[](int index) const {
  return _type == IPv4 ? _address.bytes[IPADDRESS_V4_BYTES_INDEX + index] : _address.bytes[index];
}

uint8_t &IPAddress::operator[](int index) {
  return _type == IPv4 ? _address.bytes[IPADDRESS_V4_BYTES_INDEX + index] : _address.bytes[index];
}

String IPAddress::toString(bool includeZone) const {
  String output;
  StringPrint printer(output);
  printTo(printer, includeZone);
  return output;
}

size_t IPAddress::printTo(Print &printer) const {
  return printTo(printer, false);
}

size_t IPAddress::printTo(Print &printer, bool includeZone) const {
  size_t written = 0;

  if (_type == IPv4) {
    for (int index = 0; index < 4; ++index) {
      if (index != 0) {
        written += printer.print('.');
      }
      written += printer.print(_address.bytes[IPADDRESS_V4_BYTES_INDEX + index], DEC);
    }
    return written;
  }

  int longestStart = -1;
  int longestLength = 1;
  int currentStart = -1;
  int currentLength = 0;
  for (int field = 0; field < 8; ++field) {
    if (_address.bytes[field * 2] == 0 && _address.bytes[field * 2 + 1] == 0) {
      if (currentStart < 0) {
        currentStart = field;
        currentLength = 1;
      } else {
        ++currentLength;
      }
      if (currentLength > longestLength) {
        longestStart = currentStart;
        longestLength = currentLength;
      }
    } else {
      currentStart = -1;
      currentLength = 0;
    }
  }

  for (int field = 0; field < 8; ++field) {
    if (field == longestStart) {
      if (field == 0) {
        written += printer.print(':');
      }
      written += printer.print(':');
      field += longestLength - 1;
      continue;
    }

    const unsigned int value =
      (static_cast<unsigned int>(_address.bytes[field * 2]) << 8)
      | _address.bytes[field * 2 + 1];
    written += printHex16(printer, value);
    if (field < 7) {
      written += printer.print(':');
    }
  }

  if (_zone > 0 && includeZone) {
    written += printer.print('%');
    written += printer.print(_zone - 1, DEC);
  }
  return written;
}

const IPAddress IN6ADDR_ANY(IPv6);
const IPAddress INADDR_NONE(0, 0, 0, 0);
