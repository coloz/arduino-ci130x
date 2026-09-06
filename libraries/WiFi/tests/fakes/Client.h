#pragma once
#include "Arduino.h"
class Client:public Stream{public:
using Stream::read;
virtual int connect(IPAddress,uint16_t)=0;virtual int connect(const char*,uint16_t)=0;
virtual int read(uint8_t*,size_t)=0;virtual void stop()=0;virtual uint8_t connected()=0;virtual operator bool()=0;
};
