# SD for CI13XX

本库为 CI1302、CI1303 和 CI1306 提供 Arduino 标准 `SD`/`File` API，支持
SD、SDHC 卡上的 FAT16/FAT32 文件系统。文件系统和上层 API 基于 Arduino 官方
[SD 1.3.0](https://github.com/arduino-libraries/SD/tree/32f14bd592b4af168a5bf165cccdf63b13031d96)，
底层通过本平台的 GPIO software SPI 访问存储卡。

CI130X SDK ALG V2.7.14 的播放器代码预留了 `AUDIO_PLAY_USE_SD_CARD`、`ff.h`
和 `f_open()` 等调用点，但公开 SDK 包没有附带 FatFs、磁盘 I/O 层或可供用户
复用的 SDIO 控制器。本库因此不启用该不完整路径，也不复用承载启动、模型和
用户数据的片内 `QSPI0`。

## 接线

所有 CI13XX variant 的默认接线如下：

| SD 模块 | Arduino 引脚 | CI130X PAD |
| --- | ---: | --- |
| CLK / SCK | `SCK` / 5 | PA5 |
| DO / MISO | `MISO` / 2 | PA2 |
| DI / MOSI | `MOSI` / 4 | PA4 |
| CS | `SS` / 3 | PA3 |
| VCC | 3.3 V | 3.3 V |
| GND | GND | GND |

只可使用 3.3 V 信号。没有电平转换的 5 V SD 模块可能损坏芯片。PA4 也是复位
阶段的 `PG_EN` 检测脚，外设在 CI13XX 复位时不得主动驱动该引脚。

## 用法

```cpp
#include <SD.h>

void setup() {
  Serial.begin(115200);
  if (!SD.begin(SS)) {
    Serial.println("SD init failed");
    return;
  }

  File file = SD.open("hello.txt", FILE_WRITE);
  if (file) {
    file.println("hello CI130X");
    file.close();
  }
}

void loop() {}
```

支持 `SD.begin()`、`open()`、`exists()`、`mkdir()`、`remove()`、`rmdir()`，以及
`File` 的读写、定位、目录遍历和 `Print` API。`SD.begin(clock, cs)` 可请求运行
时钟，但 CI13XX software SPI 会把高于 500 kHz 的值限制为 500 kHz。

## 限制

- 仅支持 FAT16/FAT32 和 8.3 短文件名，不支持 exFAT 与长文件名。
- GPIO software SPI 无 DMA，吞吐量显著低于硬件 SPI/SDIO。
- 默认 PA2/PA3 在 CI1302/CI1303 上也用于 `Wire`/`Serial1`，不能同时使用。
- 默认四根 SPI 信号位于 IIS 引脚组；若更改原厂 SDK profile 启用相应 IIS
  输入/输出，需要先解决引脚冲突。
- 文件系统对象和 SPI 总线没有跨 FreeRTOS task 的互斥保护；应由同一个 task
  使用，或由应用自行加锁。

`CardInfo` 示例可用于区分接线、卡初始化和 FAT 分区问题，其余示例展示常用的
文件与目录操作。
