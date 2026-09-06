#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <arpa/inet.h>
using byte = uint8_t;
class String {
 std::string value;
public:
 String() = default;
 String(const char *s) : value(s ? s : "") {}
 String(const std::string &s) : value(s) {}
 const char *c_str() const { return value.c_str(); }
};
#if !defined(WIFI_TEST_IPV4_ONLY)
enum IPType { IPv4, IPv6 };
#endif
class IPAddress {
#if defined(WIFI_TEST_IPV4_ONLY)
 uint8_t data[4] = {};
#else
 uint8_t data[16] = {}; IPType family = IPv4;
#if !defined(WIFI_TEST_IPV6_NO_ZONE)
 uint8_t scope = 0;
#endif
#endif
public:
 IPAddress() = default;
 IPAddress(uint8_t a,uint8_t b,uint8_t c,uint8_t d) : data{a,b,c,d} {}
#if !defined(WIFI_TEST_IPV4_ONLY)
 IPAddress(IPType f) : family(f) {}
#if defined(WIFI_TEST_IPV6_NO_ZONE)
 IPAddress(IPType f,const uint8_t *p) : family(f) { memcpy(data,p,f==IPv6?16:4); }
#else
 IPAddress(IPType f,const uint8_t *p,uint8_t z=0) : family(f),scope(z) { memcpy(data,p,f==IPv6?16:4); }
 uint8_t zone() const { return scope; }
#endif
 IPType type() const { return family; }
#endif
 uint8_t operator[](int i) const { return data[i]; }
 operator uint32_t() const {
#if !defined(WIFI_TEST_IPV4_ONLY)
  if(family!=IPv4) return 0;
#endif
  uint32_t n;memcpy(&n,data,4);return n;
 }
 bool fromString(const char *s) {
  uint8_t b[16]={};
  if(inet_pton(AF_INET,s,b)==1){memcpy(data,b,4);
#if !defined(WIFI_TEST_IPV4_ONLY)
   family=IPv4;
#endif
   return true;
  }
#if !defined(WIFI_TEST_IPV4_ONLY)
  if(inet_pton(AF_INET6,s,b)==1){memcpy(data,b,16);family=IPv6;return true;}
#endif
  return false;
 }
 String toString() const {
  char b[64];int af=AF_INET;
#if !defined(WIFI_TEST_IPV4_ONLY)
  if(family==IPv6) af=AF_INET6;
#endif
  inet_ntop(af,data,b,sizeof(b));return String(b);
 }
};
class Print {
 int error=0;
protected: void setWriteError(int e=1){error=e;}
public:
 ~Print()=default;
 virtual size_t write(uint8_t)=0;
 virtual size_t write(const uint8_t*p,size_t n){size_t done=0;while(done<n&&write(p[done]))++done;return done;}
 size_t write(const char *p){return write((const uint8_t*)p,strlen(p));}
#if !defined(WIFI_TEST_IPV4_ONLY) && !defined(WIFI_TEST_IPV6_NO_ZONE)
 virtual int availableForWrite(){return 0;}
#endif
 virtual void flush(){}
 int getWriteError(){return error;}
};
class Stream:public Print {
protected: unsigned long _timeout=1000;
#if !defined(WIFI_TEST_IPV4_ONLY) && !defined(WIFI_TEST_IPV6_NO_ZONE)
 virtual bool waitForData(unsigned long){return false;}
#endif
public:
 virtual int available()=0;virtual int read()=0;virtual int peek()=0;
 virtual size_t readBytes(char *p,size_t n){size_t i=0;for(;i<n;++i){int v=read();if(v<0)break;p[i]=v;}return i;}
};
struct HardwareSerial {};

unsigned long millis();void delay(unsigned long);
