// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// This package always uses the external ESP32-C3 through ESPLink, independent
// of the host Arduino board. Board-specific upstream backends remain vendored
// for provenance, but are not selected by this package.
#define BLE_USE_ESPLINK 1
