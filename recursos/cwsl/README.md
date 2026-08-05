# CWSL firmware resources

These four profile resources are exact outputs of the official
`CI130X_SDK_ALG_V2.7.14/projects/offline_asr_alg_pro_sample/firmware` pipeline.
They are selected when the Arduino **Algorithm** menu is set to either
**Command-word self-learning CWSL + AEC/voice interruption** or its without-AEC
compatibility profile.

Run `tools/generate_cwsl_package_resources.ps1` beside the official SDK to
reproduce them. The script merges the sample's two V00874 ASR files, G3 NLP PRO
V00874 DNN, precompiled command-info entry 60000 and all 130 voice sources with
the same vendor tools and settings as `合成分区bin文件.bat`. It refuses to copy
an output unless its size and SHA-256 match this manifest.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `asr.bin` | 20,038 | `08624F47C63858A4F57F31BA7E2CFA3C9E981B03C868047817FFDA6522082B3E` |
| `dnn.bin` | 1,410,376 | `18F90809B641B02D3F2E9E10DC3D496FFE720D370410EC048B89691B56C19991` |
| `voice.bin` | 379,741 | `C1948B068DEEBB58A455A27CA4B9941D331E613DAF2BAEB99422587EF9D58318` |
| `user_file.bin` | 12,321 | `6359ADF0DA19A774BBE1E094BC2AC94E67272096A8C10FA9B0565514127661FD` |

The command-info and voice resources retain the vendor sample's spoken CWSL
control flow (IDs 199 through 208) and learning prompts. The Arduino C++ API can
start operations directly, so sketches do not need to invoke those spoken
controls. Resource generation and firmware composition are reproducible checks;
microphone capture, persistence, wake behavior and learned-word recognition
still require physical-board tests.

The Standard profile currently uses independently generated copies of the same
V2.7.14 vendor-sample resource payloads. Profile selection remains meaningful:
it chooses separate compile-time features, linker memory limits and second-core
algorithm images, while separate directories keep future model revisions and
sketch-local overrides isolated.
