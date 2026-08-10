# VariableNumber

本示例通过 `CHIPINTELLI_LANGUAGE` 选择中文，使用从 ID 300 开始的数字基础词元，
把 `int16_t` 数值 `300`、`double` 数值 `3.1415926` 和 `float` 数值 `-123.5`
连续转换为字符串，再用 `playVoice(..., false)` 一次性加入有序播放队列。
`ChipIntelliAudio` 的 FreeRTOS 任务会完整播放一个数值后再播放下一个，`loop()`
无需等待，仍可继续处理主程序逻辑。

`String(float/double)` 默认只保留两位小数；示例用 `String(piValue, 7)` 和
`String(negativeValue, 1)` 显式指定需要播报的小数位。若把小数变量错误声明成
`int16_t`，赋值时小数部分就已经丢失，之后 `String()` 无法恢复。

语言宏必须位于 `#include <ChipIntelliAudio.h>` 之前。可选值为 `_ZH`、`_EN`、`_JA`、
`_KO`、`_RU`、`_ES`、`_TH`、`_DE`、`_ID`、`_VI`、`_FR`、`_PT`、`_FA`、`_TR`、
`_AR`；未声明时默认 `_ZH`。示例不需要声明数字语音资源宏。构建后处理会从最终 ELF
中识别字符串版 `playVoice()`，自动让 `citool-cli generate` 在 TTS 请求中加入所选
语言从 ID 300 开始的完整词表，并生成匹配的 `voice.bin`。

首次生成需要能够访问 `platform.txt` 中配置的 `ci-service`。如果使用已经制作好的
资源包，仍需确保从 ID 300 开始的内容与所选语言词表一致。

十五套完整词表及拼接规则见
[`../../VARIABLE_VOICE_TEXTS.md`](../../VARIABLE_VOICE_TEXTS.md)。
