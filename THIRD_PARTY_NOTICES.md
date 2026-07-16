# Third-party notices and release status

## Arduino compatibility core

Files in `cores/chipintelli` derived from the Arduino and Arduino-ESP32
compatibility cores retain their individual copyright headers and are provided
under the GNU Lesser General Public License, version 2.1 or (where stated by the
source file) any later version. A copy of LGPL 2.1 is included in
`LICENSE-ARDUINO-CORE.md`.

## ChipIntelli CI13XX SDK

`tools/sdk` is generated from `CI13XX_SDK_ASR_ALG_V2.7.12`. The upstream SDK
README restricts use or modification without permission and does not provide a
single top-level open-source licence. The payload also contains proprietary
algorithm archives, a second-core image, Windows tools, and third-party
components with their own terms.

This repository therefore does **not** assert that the generated SDK payload is
redistributable. Obtain written permission from ChipIntelli and complete a
file-level licence/notice inventory before publishing a platform archive.

## Nuclei GCC toolchain

The compiler is not stored in this platform directory. Any Boards Manager
repack of the vendor GCC distribution must preserve its licence texts and meet
the applicable source-code and runtime-library exception obligations.

The package index remains a template until these release conditions are met.
