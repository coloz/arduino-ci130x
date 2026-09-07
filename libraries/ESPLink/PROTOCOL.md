# Arduino Host / ESP32-C3 协议 v1

本文件与 `src/C3Protocol.h` 是两端唯一 wire 契约。两端首版已实现并进行软件验证；编译和原生测试不能替代实物验收。

## 编码与可靠性

所有整数 little-endian。字符串为 u16 字节数 + UTF-8，不含 NUL。`addr` 固定 18 字节：u8 family (0=unspecified,4,6)、u8 zone、16 字节网络序地址；IPv4 放前四字节，余下十二字节为零。

帧为 `00 COBS(header + payload + crc32c) 00`。Header 共20字节：C1 C3、version=1、type u8、session u32、id u32、opcode u16、length u16、status i32。负载最多1024字节。CRC32C覆盖头部和负载，反射多项式82F63B78、初值/末异或FFFFFFFF，123456789测试向量E3069283。

Type: HELLO=1、HELLO_REPLY=2、REQUEST=3、RESPONSE=4、ACK=5、PING=6、PONG=7。

v1采用**单个应用RPC在途（接收额度1）+主机按需拉取**；不是已经实现了多帧滑动窗口。所有重发保持session/id/opcode/payload相同。设备缓存最近请求的指纹、执行状态和完整响应。重复执行中请求返回ACK，重复已完成请求重放响应；旧id返回ResultExpired，绝不重新执行。不同内容复用同id返回ProtocolError。新请求隐含确认前一响应。ACK仅表示受理/接收，不表示外部操作完成。

HELLO的session=0、id=主机非零nonce。尚未执行应用请求时，相同nonce重发返回同session；新nonce或已经执行过业务后的HELLO在服务线程安全清理旧资源后生成新session。这样CI重启后即便nonce重复，也不会复活旧对象。HELLO_REPLY负载：u16 maxPayload、u32 features、u32 bootID、u8 maxSockets、u8 maxBLEConnections、u16 maxHciPacket、string firmwareVersion。握手完成前应用RPC不执行。新会话使全部socket/证书/HCI状态失效。

请求每300ms可重发，受操作总deadline约束。超过deadline后主机使会话失效；已发送操作返回OutcomeUnknown，不能自动重放。PING/PONG为空负载，id=0，由串口任务处理，不经过可能阻塞的网络worker。无硬复位线；C3看门狗负责自身故障恢复。v1默认固定921600；双方显式配置同一115200/2M/3M可以测试，但**动态波特率协商尚不属于v1**。

status: 0 OK; -1 InvalidArgument; -2 Unsupported; -3 NoMemory; -4 Busy; -5 Timeout; -6 NotConnected; -7 IoError; -8 ProtocolError; -9 OutcomeUnknown; -10 StaleHandle; -11 BufferTooSmall; -12 ResultExpired; -13 SecurityError; -14 WouldBlock; -15 LinkLost; -16 NotFound。

未注明的响应为空。所有处理器必须校验完整请求长度，拒绝尾随垃圾；错误响应可以为空。所有handle为含generation的u32，0表示无对象；类型与session必须校验。

## 系统与Wi-Fi

| Opcode | 请求负载 | 成功响应负载 |
|---|---|---|
| 0001 Echo | 任意bytes | 相同bytes |
| 0002 SystemInfo | 空 | string fw,u32 features,u32 freeHeap,u32 minimumHeap,u32 uptimeMs |
| 0003 Restart | 空 | 空；响应发送后软件重启 |
| 0100 WifiMode | u8 mode(0=OFF,1=STA,2=AP,3=APSTA) | 空 |
| 0101 WifiBegin | string ssid,string password,i32 channel,u8 hasBssid,bytes bssid[6] | 空；仅启动连接，状态另查 |
| 0102 WifiDisconnect | u8 eraseCredentials,u8 turnOff | 空 |
| 0103 WifiStatus | 空 | u8 ArduinoWLStatus,u8 mode,i32 rssi,u8 channel,bytes mac[6],bytes bssid[6],string ssid,string hostname,addr local,addr gateway,addr subnet,addr dns1,addr dns2 |
| 0104 WifiConfig | addr local,addr gateway,addr subnet,addr dns1,addr dns2 | 空；IPv4全零恢复DHCP；不支持的family明确报错 |
| 0105 WifiScanStart | u8 showHidden,u8 passive,u16 maxMsPerChannel,u8 channel | 空；非阻塞启动 |
| 0106 WifiScanStatus | 空 | i32 count(-1运行中,-2失败/无结果) |
| 0107 WifiScanResult | u16 index | string ssid,i32 rssi,u8 auth,u8 channel,bytes bssid[6],u8 hidden |
| 0108 WifiScanDelete | 空 | 空 |
| 0110 WifiAPBegin | string ssid,string password,u8 channel,u8 hidden,u8 maxClients | 空 |
| 0111 WifiAPConfig | addr local,addr gateway,addr subnet | 空 |
| 0112 WifiAPStop | u8 turnOff | 空 |
| 0113 WifiAPStatus | 空 | u8 stations,string ssid,bytes mac[6],addr local |
| 0114 WifiHostname | string name | 空 |
| 0115 WifiReconnect | 空 | 空 |
| 0116 WifiAutoReconnect | u8 enabled | 空 |
| 0117 WifiSetOption | u8 option(1=sleep,2=txpower,3=autoreconnect),i32 value | 空；txpower以Arduino wifi_power_t四分之一dBm数值为单位 |
| 0118 WifiDNS | addr dns1,addr dns2 | empty; update DNS without changing DHCP/static-IP mode |
| 0119 WifiGetOption | u8 option (same as 0117) | i32 currentValue |
| 0120 DnsResolve | string hostname,u8 family(0/4/6) | addr result |
| 0121 TimeConfig | i32 gmtOffsetSeconds,i32 daylightOffsetSeconds,string server1,string server2 | 空 |
| 0122 TimeGet | 空 | u32 unixTimeLow,u32 unixTimeHigh |

Wi-Fi事件由主机状态监测生成并在Arduino任务分发；v1无无限制异步串口推送。瞬时状态可能在轮询之间合并，不能声称完整逐事件Espressif事件语义。

## Socket、UDP、TLS

| Opcode | 请求负载 | 成功响应负载 |
|---|---|---|
| 0200 SocketOpen | u8 kind(0=TCP,1=TLS,2=UDP,3=server) | u32 handle |
| 0201 SocketConnect | u32 handle,string host,u16 port,u32 timeoutMs | 空 |
| 0202 SocketRead | u32 handle,u16 maximum | u16 count,bytes data[count]；count不超过maximum及1022 |
| 0203 SocketWrite | u32 handle,bytes data(全部剩余负载) | u32 accepted；允许部分写入，不能以ERROR抹掉已发送数量 |
| 0204 SocketStatus | u32 handle | u8 connected,u32 available,u32 writable,addr remote,u16 remotePort,u16 localPort,addr local (49 bytes total) |
| 0205 SocketClose | u32 handle | 空 |
| 0206 SocketListen | u32 handle,u16 port,u8 backlog | 空 |
| 0207 SocketAccept | u32 serverHandle | u32 childHandle；0=无待接入连接 |
| 0208 SocketOption | u32 handle,u8 option,u32 value | 空；1=NoDelay,2=TimeoutMs,3=KeepAliveIdle,4=KeepAliveInterval,5=KeepAliveCount |
| 0209 SocketClearRx | u32 handle | 空；明确丢弃RX，不称为flush |
| 0220 UdpBind | u32 handle,u16 localPort,addr multicastGroup | 空；family0=普通bind |
| 0221 UdpTxBegin | u32 handle,string host,u16 port | 空；丢弃该对象上一个未提交发送数据报 |
| 0222 UdpTxData | u32 handle,bytes data | u16 accepted；单个完整数据报最大1472，超限明确报错 |
| 0223 UdpTxEnd | u32 handle | 空；仅代表本地sendto成功，不保证对端收到 |
| 0224 UdpRxBegin | u32 handle | u16 packetSize,addr remote,u16 remotePort；0=无新数据报，非零先丢弃上个未读完数据报 |
| 0225 UdpRead | u32 handle,u16 maximum | u16 count,bytes data[count]；最大1022；不跨数据报 |
| 0240 CertBegin | u8 kind(0=CA,1=client cert,2=private key),u32 totalBytes | u32 certHandle；单项最大8192 |
| 0241 CertWrite | u32 certHandle,u32 offset,bytes data | 空；严格连续offset；重发由RPC层去重 |
| 0242 CertCommit | u32 certHandle | 空；已完整上传，固件追加NUL，内容在TLS配置/握手中校验 |
| 0243 CertDelete | u32 certHandle | 空；正在被socket使用的对象返回Busy |
| 0244 SocketTLS | u32 socketHandle,u32 caHandle,u32 clientCertHandle,u32 keyHandle,u8 insecure,u32 handshakeTimeoutMs | 空；caHandle0=默认受信CA bundle，cert/key0=不用mTLS；insecure必须显式开启 |

SocketConnect timeoutMs limits the TCP phase (1..30000 ms); SocketTLS handshakeTimeoutMs independently limits TLS (1..30000 ms). DNS has a 10000 ms service deadline. Host SocketConnect RPC budget is 10000 + TCP timeout + TLS handshake timeout (TLS only) + 3000 ms link margin. Defaults: TCP 23000 ms, TLS 38000 ms; maxima: TCP 43000 ms, TLS 73000 ms. DnsResolve defaults to a 13000 ms RPC budget; an explicitly supplied shorter host deadline is honored and may expire with OutcomeUnknown. UdpTxBegin uses a 13000 ms RPC budget. A DNS job that outlives its service deadline holds the dedicated DNS worker until completion; later DNS requests return Busy during that interval.

证书为会话临时对象。证书delete只有不被socket持有才成功；主机close socket后删除上传的证书。证书持久化与原子更新属于后续扩展。`Client.flush()`等待本地同步write完成，不清除RX，也不承诺远端应用已处理。

## BLE HCI

| Opcode | 请求负载 | 成功响应负载 |
|---|---|---|
| 0300 HciBegin | 空 | 空；初始化Controller，不运行C3 NimBLE/Bluedroid Host |
| 0301 HciEnd | 空 | 空；释放Controller会话状态 |
| 0302 HciWrite | 完整H4 packet bytes | u16 accepted；要么完整入队，要么0/错误；不接受半个packet |
| 0303 HciRead | u16 maximum | raw H4字节流，可为空/分段/合并；以响应length为准 |
| 0304 HciStatus | 空 | u8 enabled,u32 queuedBytes,u32 dropped,u16 maxH4Packet,u8 fault |

HCI队列有界；溢出必须fault并要求BLE重新初始化，不能静默继续。HCIRead请求不超过1024。ArduinoBLE主机接收H4长度受其自身实现约束，控制器事件/ACL不能无检查地塞入258字节主机缓冲。协议可靠性与蓝牙控制器credit分别处理。
