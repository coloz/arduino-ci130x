# VariableNumber

本示例使用 ID 300～316 预生成 17 个中文数字基础词元，再依次调用
`playVoice("300")`、`playVoice("-1.5")` 和 `playVoice("0.02")`，分别播报
“三百”“负一点五”和“零点零二”。示例等待一次播放完成后才从 `loop()` 发起下一次播放，
避免后一条提示音中断前一条。主 `.ino` 中的
`VOICE<n>` 宏会在构建后处理阶段交给
`citool-cli generate`，生成与示例 ID 一致的 `voice.bin`。

首次生成需要能够访问 `platform.txt` 中配置的 `ci-service`。如果使用已经制作好的
资源包，可以删除这些 `VOICE<n>` 宏，将 ID 改为现有 `voice.bin` 中的实际 ID。
