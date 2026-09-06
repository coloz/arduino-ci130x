#pragma once
#include "Arduino.h"
#include "C3Protocol.h"
class ESPLinkClass {
public:
 uint32_t token=1;bool active=true;
 bool begin(HardwareSerial&,uint32_t=115200,uint32_t=10000);
 bool begin(){active=true;return true;}
 void poll(){}
 bool ready()const{return active;}uint32_t session()const{return active?token:0;}
 int32_t request(uint16_t,const uint8_t*,size_t,uint8_t*,size_t&,uint32_t=5000);
};
extern ESPLinkClass ESPLink;
