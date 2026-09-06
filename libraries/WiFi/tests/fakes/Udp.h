#pragma once
#include "Arduino.h"
class UDP:public Stream{public:
using Stream::read;
virtual uint8_t begin(uint16_t)=0;virtual uint8_t beginMulticast(IPAddress,uint16_t)=0;virtual void stop()=0;
virtual int beginPacket(IPAddress,uint16_t)=0;virtual int beginPacket(const char*,uint16_t)=0;virtual int endPacket()=0;
virtual int parsePacket()=0;virtual int read(uint8_t*,size_t)=0;virtual int read(char*p,size_t n){return read((uint8_t*)p,n);}
virtual IPAddress remoteIP()=0;virtual uint16_t remotePort()=0;
};
