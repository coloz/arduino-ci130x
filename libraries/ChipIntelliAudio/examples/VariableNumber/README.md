# VariableNumber

本示例通过 `CHIPINTELLI_LANGUAGE` 选择中文，使用 ID 300～316 预生成
17 个中文数字基础词元，再依次调用
`playVoice("300")`、`playVoice("-1.5")` 和 `playVoice("0.02")`，分别播报
“三百”“负一点五”和“零点零二”。示例等待一次播放完成后才从 `loop()` 发起下一次播放，
避免后一条提示音中断前一条。

语言宏必须位于 `#include <ChipIntelliAudio.h>` 之前。可选值为 `_ZH`、`_EN`、`_JA`、
`_KO`、`_RU`、`_ES`、`_TH`、`_DE`、`_ID`、`_VI`、`_FR`、`_PT`、`_FA`、`_TR`、
`_AR`；未声明时默认 `_ZH`。示例不需要声明数字语音资源宏。构建后处理会从最终 ELF
中识别字符串版 `playVoice()`，自动让 `citool-cli generate` 在 TTS 请求中加入所选
语言从 ID 300 开始的完整词表，并生成匹配的 `voice.bin`。

首次生成需要能够访问 `platform.txt` 中配置的 `ci-service`。如果使用已经制作好的
资源包，仍需确保从 ID 300 开始的内容与所选语言词表一致。

十五套完整词表及拼接规则见
[`../../VARIABLE_VOICE_TEXTS.md`](../../VARIABLE_VOICE_TEXTS.md)。
