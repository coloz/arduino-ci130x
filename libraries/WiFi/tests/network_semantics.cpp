#include <WiFi.h>
#include <C3Protocol.h>
#include <WiFiTransport.h>
#include <cassert>
#include <iostream>
#include <map>
#include <vector>
#include <deque>


ESPLinkClass ESPLink;
static unsigned long ticks=1000;
unsigned long millis(){return ticks;}
void delay(unsigned long ms){ticks+=ms;}
bool ESPLinkClass::begin(HardwareSerial&,uint32_t,uint32_t){active=true;return true;}
struct Socket { uint32_t handshakeTimeout=0; bool open=true; std::vector<uint8_t> rx,tx;size_t pos=0;std::deque<std::vector<uint8_t>> packets; };
static std::map<uint32_t,Socket> sockets;
static std::map<uint32_t,std::vector<uint8_t>> certs;
static uint32_t nextHandle=1,lastHandle=0;
static size_t writeLimit=99999,writeRequests=0,closeRequests=0,udpCommits=0;
static size_t failWrite=0;static bool failConnect=false;
static std::vector<uint8_t> nextRx;
static uint8_t wifiStatus=WL_DISCONNECTED;
static int eventCount=0;
static bool localIPv6=false, dnsIPv6=false;
static uint8_t lastDnsFamily=255;
static uint32_t simulatedDns=0,simulatedTcp=0,simulatedTls=0,lastTcpTimeout=0,lastHandshakeTimeout=0;
static void putAddress(c3::Writer &w){uint8_t b[18]={4,0,192,168,1,1};w.bytes(b,18);}
int32_t ESPLinkClass::request(uint16_t opcode,const uint8_t *request,size_t length,uint8_t *reply,size_t &replyLength,uint32_t rpcTimeout) {
 c3::Reader r(request,length);c3::Writer w(reply,replyLength);auto op=static_cast<c3::Opcode>(opcode);
 if(op==c3::Opcode::SocketOpen){r.u8();lastHandle=nextHandle++;sockets[lastHandle].rx=nextRx;nextRx.clear();w.u32(lastHandle);}
 else if(op==c3::Opcode::WifiStatus){w.u8(wifiStatus);w.u8(WIFI_STA);w.i32(-40);w.u8(6);uint8_t mac[6]={};w.bytes(mac,6);w.bytes(mac,6);w.string("test");w.string("test-host");for(int i=0;i<5;++i)putAddress(w);}
 else if(op==c3::Opcode::SocketConnect){uint32_t h=r.u32();char host[256];r.string(host,sizeof(host));r.u16();lastTcpTimeout=r.u32();assert(sockets.count(h));lastHandshakeTimeout=sockets.at(h).handshakeTimeout;uint32_t elapsed=simulatedDns+simulatedTcp+simulatedTls;ticks+=elapsed;if(elapsed>rpcTimeout){replyLength=0;return c3::OutcomeUnknown;}if(simulatedTcp>lastTcpTimeout||simulatedTls>lastHandshakeTimeout){replyLength=0;return c3::Timeout;}if(failConnect){replyLength=0;return c3::SecurityError;}}
 else if(op==c3::Opcode::DnsResolve){char host[256];r.string(host,sizeof(host));lastDnsFamily=r.u8();if(dnsIPv6){uint8_t address[18]={6,0,0x20,1};address[17]=7;w.bytes(address,18);}else putAddress(w);}
 else if(op==c3::Opcode::SocketListen){assert(sockets.count(r.u32()));assert(r.u16()==8080);r.u8();}
 else if(op==c3::Opcode::SocketAccept){assert(sockets.count(r.u32()));lastHandle=nextHandle++;sockets[lastHandle]=Socket();w.u32(lastHandle);}
 else if(op==c3::Opcode::SocketStatus){uint32_t h=r.u32();assert(sockets.count(h));auto&s=sockets[h];w.u8(s.open);w.u32(s.rx.size()-s.pos);w.u32(768);putAddress(w);w.u16(80);w.u16(4567);uint8_t local[18]={4,0,10,20,30,40};if(localIPv6){local[0]=6;local[1]=2;local[2]=0x20;local[3]=1;local[17]=7;}w.bytes(local,18);}
 else if(op==c3::Opcode::SocketRead||op==c3::Opcode::UdpRead){uint32_t h=r.u32();size_t maximum=r.u16();auto&s=sockets.at(h);size_t count=std::min(maximum,s.rx.size()-s.pos);w.u16(count);w.bytes(s.rx.data()+s.pos,count);s.pos+=count;}
 else if(op==c3::Opcode::SocketWrite||op==c3::Opcode::UdpTxData){uint32_t h=r.u32();auto&s=sockets.at(h);size_t size=r.remaining();if(op==c3::Opcode::SocketWrite&&failWrite&&writeRequests+1==failWrite){++writeRequests;active=false;replyLength=0;return c3::OutcomeUnknown;}size_t count=std::min(size,writeLimit);s.tx.insert(s.tx.end(),r.current(),r.current()+count);std::vector<uint8_t> discard(size);r.bytes(discard.data(),size);if(op==c3::Opcode::SocketWrite){++writeRequests;w.u32(count);}else w.u16(count);}
 else if(op==c3::Opcode::SocketClose){uint32_t h=r.u32();++closeRequests;sockets.erase(h);}
 else if(op==c3::Opcode::SocketClearRx){auto&s=sockets.at(r.u32());s.rx.clear();s.pos=0;}
 else if(op==c3::Opcode::UdpBind){uint32_t h=r.u32();assert(sockets.count(h));r.u16();uint8_t b[18];r.bytes(b,18);}
 else if(op==c3::Opcode::UdpTxBegin){auto&s=sockets.at(r.u32());s.tx.clear();char host[256];r.string(host,sizeof(host));r.u16();}
 else if(op==c3::Opcode::UdpTxEnd){assert(sockets.count(r.u32()));++udpCommits;}
 else if(op==c3::Opcode::UdpRxBegin){auto&s=sockets.at(r.u32());s.rx.clear();s.pos=0;if(!s.packets.empty()){s.rx=s.packets.front();s.packets.pop_front();}w.u16(s.rx.size());putAddress(w);w.u16(9000);}
 else if(op==c3::Opcode::CertBegin){r.u8();r.u32();uint32_t h=nextHandle++;certs[h]={};w.u32(h);}
 else if(op==c3::Opcode::CertWrite){uint32_t h=r.u32(),offset=r.u32();auto&cert=certs.at(h);assert(offset==cert.size());size_t n=r.remaining();cert.insert(cert.end(),r.current(),r.current()+n);std::vector<uint8_t>b(n);r.bytes(b.data(),n);}
 else if(op==c3::Opcode::CertCommit){assert(certs.count(r.u32()));}
 else if(op==c3::Opcode::CertDelete){assert(certs.erase(r.u32())==1);}
 else if(op==c3::Opcode::SocketTLS){uint32_t socket=r.u32();assert(sockets.count(socket));for(int i=0;i<3;++i){uint32_t h=r.u32();assert(!h||certs.count(h));}r.u8();sockets.at(socket).handshakeTimeout=r.u32();}
 else {std::cerr<<"Unexpected opcode "<<opcode<<"\n";assert(false);}
 assert(r.done());assert(w.ok());replyLength=w.size();return c3::Ok;
}
static void event(WiFiEvent_t e){if(e==ARDUINO_EVENT_WIFI_STA_GOT_IP)++eventCount;}
int main(){
 {WiFiClient a;nextRx={'a','b','c'};assert(a.connect("example.com",80));uint32_t h=lastHandle;WiFiClient b=a;
  assert(a.localIP()==IPAddress(10,20,30,40));assert(a.localPort()==4567);assert(a.peek()=='a');assert(b.read()=='a');assert(a.read()=='b');assert(b.read()=='c');
  writeLimit=4;uint8_t data[10]={0,1,2,3,4,5,6,7,8,9};size_t before=writeRequests;assert(a.write(data,10)==4);assert(writeRequests==before+1);assert(sockets[h].tx==std::vector<uint8_t>(data,data+4));
  b.stop();assert(!a.connected());assert(!sockets.count(h));}
 writeLimit=99999;
 {localIPv6=true;WiFiClient a;
#if defined(WIFI_TEST_IPV4_ONLY) || defined(WIFI_TEST_IPV6_NO_ZONE)
  assert(!a.connect("2001::7",80));assert(a.lastError()==c3::Unsupported);
#else
  assert(a.connect("2001::7",80));auto ip=a.localIP();assert(ip.type()==IPv6&&ip.zone()==2&&ip[0]==0x20&&ip[1]==1&&ip[15]==7);
#endif
  localIPv6=false;}
 {uint8_t payload[18]={6,0,0x20,1};payload[17]=7;c3::Reader r(payload,sizeof(payload));int32_t error=c3::Ok;auto ip=espwifi::address(r,error);assert(r.done());
#if defined(WIFI_TEST_IPV4_ONLY)
  assert(error==c3::Unsupported);assert(uint32_t(ip)==0);assert(espwifi::dnsFamily(ip)==4);
  IPAddress resolved(1,2,3,4);assert(!WiFi.hostByName("2001::7",resolved));assert(WiFi.lastError()==c3::Unsupported);assert(resolved==IPAddress(1,2,3,4));
#else
  assert(error==c3::Ok&&espwifi::isIPv6(ip)&&ip[0]==0x20&&ip[15]==7);
  assert(std::string(espwifi::addressString(ip).c_str())=="2001:0:0:0:0:0:0:7");
#endif
 }
 {uint8_t payload[18]={9};c3::Reader r(payload,sizeof(payload));int32_t error=c3::Ok;espwifi::address(r,error);assert(error==c3::ProtocolError);}
 {IPAddress ip;assert(WiFi.hostByName("test.example",ip));assert(ip==IPAddress(192,168,1,1));
#if defined(WIFI_TEST_IPV4_ONLY)
  assert(lastDnsFamily==4);
#else
  assert(lastDnsFamily==0);
#endif
  dnsIPv6=true;
#if defined(WIFI_TEST_IPV4_ONLY)
  assert(!WiFi.hostByName("ipv6.example",ip));assert(WiFi.lastError()==c3::Unsupported);assert(ip==IPAddress(192,168,1,1));
#else
  assert(WiFi.hostByName("ipv6.example",ip));assert(espwifi::isIPv6(ip)&&ip[15]==7);
#endif
  dnsIPv6=false;}
 {WiFiServer server(8080);server.begin();assert(server);localIPv6=true;WiFiClient child=server.accept();
#if defined(WIFI_TEST_IPV4_ONLY) || defined(WIFI_TEST_IPV6_NO_ZONE)
  assert(server.lastError()==c3::Unsupported);assert(!child);assert(!sockets.count(lastHandle));
#else
  assert(server.lastError()==c3::Ok);assert(child);assert(espwifi::isIPv6(child.localIP()));
#endif
  localIPv6=false;}
 {WiFiClient a;assert(a.connect("example.com",80));size_t closes=closeRequests;nextRx={'x'};ESPLink.token++;assert(a.available()==0);a.stop();assert(closeRequests==closes);}
 nextRx.clear();sockets.clear();
 {WiFiClient a;std::vector<uint8_t> data(2000,42);assert(a.connect("example.com",80));uint32_t h=lastHandle;assert(a.write(data.data(),data.size())==data.size());assert(sockets[h].tx==data);}
 {WiFiClient a;assert(a.connect("example.com",80));std::vector<uint8_t>data(1600,5);failWrite=writeRequests+2;assert(a.write(data.data(),data.size())==768);assert(a.lastError()==c3::OutcomeUnknown);failWrite=0;ESPLink.token++;ESPLink.active=true;}
 sockets.clear();
 {WiFiClient a;nextRx={'z'};assert(a.connect("example.com",80));sockets[lastHandle].open=false;assert(a.available()==1);a.flush();assert(a.connected());assert(a.read()=='z');assert(!a.connected());}
 {WiFiUDP udp;assert(udp.begin(9000));uint32_t h=lastHandle;sockets[h].packets={{'a','b','c'},{'d','e'}};
  assert(udp.parsePacket()==3);assert(udp.peek()=='a');assert(udp.read()=='a');assert(udp.parsePacket()==2);assert(udp.read()=='d');assert(udp.read()=='e');assert(udp.read()==-1);
  assert(udp.beginPacket(IPAddress(1,2,3,4),9000));std::vector<uint8_t> big(1473,7);assert(udp.write(big.data(),big.size())==0);size_t before=udpCommits;assert(!udp.endPacket());assert(udpCommits==before);
  assert(udp.beginPacket("example.com",9000));assert(udp.write(big.data(),1472)==1472);assert(udp.endPacket());assert(sockets[h].tx.size()==1472);}
 {WiFiClientSecure tls;std::string pem(2000,'A');tls.setCACert(pem.c_str());tls.setCertificate("CERT");tls.setPrivateKey("KEY");assert(tls.connect("example.com",443));assert(certs.size()==3);WiFiClientSecure copy=tls;copy.stop();assert(certs.empty());assert(!tls.connected());}
 {WiFiClientSecure tls;tls.setCACert("CA");failConnect=true;assert(!tls.connect("example.com",443));failConnect=false;assert(certs.empty());assert(sockets.empty());}
 {WiFiClientSecure tls;tls.setHandshakeTimeout(5);simulatedDns=8000;simulatedTcp=18000;simulatedTls=4000;assert(tls.connect("slow.example",443,20000));assert(lastTcpTimeout==20000&&lastHandshakeTimeout==5000);simulatedDns=simulatedTcp=simulatedTls=0;}
 WiFi.onEvent(event);wifiStatus=WL_CONNECTED;ticks+=300;assert(WiFi.status()==WL_CONNECTED);WiFi.poll();assert(eventCount==1);ticks+=300;WiFi.poll();assert(eventCount==1);
 std::cout<<"network semantics: partial writes, shared close, stale session, bulk transfer, EOF, UDP boundaries, TLS lifecycle, socket-local addresses, phased deadlines, events PASS\n";
}
