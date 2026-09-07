// SPDX-License-Identifier: MIT
#pragma once
// Override these for the entire build, including separately compiled libraries.
// ESPLINK_DEFAULT_SERIAL may name a board-provided hardware serial object.
// Without it, the implementation chooses the last available hardware UART.
#ifndef ESPLINK_DEFAULT_BAUD
#define ESPLINK_DEFAULT_BAUD 921600UL
#endif
static_assert(ESPLINK_DEFAULT_BAUD > 0, "Default UART baud must be positive");
// The wire protocol is platform independent. This flag only selects scheduling
// and an optional UART buffer optimization for the existing CI13XX core.
#ifndef ESPLINK_CI13XX_RTOS
#if defined(ARDUINO_ARCH_CI13XX)
#define ESPLINK_CI13XX_RTOS 1
#else
#define ESPLINK_CI13XX_RTOS 0
#endif
#endif
// Override with build flags for ALL translation units, not only the sketch.
#ifndef ESPLINK_CI13XX_UART_BULK_RX
#define ESPLINK_CI13XX_UART_BULK_RX 1
#endif
#ifndef ESPLINK_CI13XX_TX_DMA
#define ESPLINK_CI13XX_TX_DMA 1
#endif
#ifndef ESPLINK_CI13XX_TX_DRAIN
#define ESPLINK_CI13XX_TX_DRAIN 1
#endif
#ifndef ESPLINK_CI13XX_TASK_NOTIFY
#define ESPLINK_CI13XX_TASK_NOTIFY 1
#endif
#ifndef ESPLINK_CI13XX_RX_BUFFER_SIZE
#define ESPLINK_CI13XX_RX_BUFFER_SIZE 4096
#endif
#ifndef ESPLINK_CI13XX_RX_CHUNK_SIZE
#define ESPLINK_CI13XX_RX_CHUNK_SIZE 64
#endif
#ifndef ESPLINK_CI13XX_DMA_THRESHOLD
#define ESPLINK_CI13XX_DMA_THRESHOLD 64
#endif
#ifndef ESPLINK_CI13XX_TASK_STACK_WORDS
#define ESPLINK_CI13XX_TASK_STACK_WORDS 2048
#endif
#ifndef ESPLINK_CI13XX_TASK_PRIORITY
#define ESPLINK_CI13XX_TASK_PRIORITY 3
#endif
#ifndef ESPLINK_CI13XX_IDLE_WAIT_MS
#define ESPLINK_CI13XX_IDLE_WAIT_MS 1000
#endif
#ifndef ESPLINK_CI13XX_POLL_MS
#define ESPLINK_CI13XX_POLL_MS 2
#endif
#ifndef ESPLINK_RX_PUMP_BUDGET
#define ESPLINK_RX_PUMP_BUDGET 4096
#endif
#if ESPLINK_CI13XX_RTOS
static_assert(ESPLINK_CI13XX_TX_DRAIN == 0 || ESPLINK_CI13XX_TX_DRAIN == 1,
  "CI frame drain must be 0 or 1");
static_assert(ESPLINK_CI13XX_RX_BUFFER_SIZE >= 2048 && ESPLINK_CI13XX_RX_BUFFER_SIZE <= 32768 &&
  (ESPLINK_CI13XX_RX_BUFFER_SIZE & (ESPLINK_CI13XX_RX_BUFFER_SIZE - 1)) == 0,
  "CI RX storage must be a power of two between 2048 and 32768 bytes");
static_assert(ESPLINK_CI13XX_RX_CHUNK_SIZE > 0 && ESPLINK_CI13XX_RX_CHUNK_SIZE <= 64,
  "CI RX chunk must be 1..64 bytes");
static_assert(ESPLINK_CI13XX_DMA_THRESHOLD > 0, "DMA threshold must be positive");
static_assert(ESPLINK_CI13XX_TASK_STACK_WORDS >= 1024 && ESPLINK_CI13XX_TASK_STACK_WORDS <= 65535,
  "CI worker stack must be 1024..65535 words; measure hardware headroom before reducing");
static_assert(ESPLINK_CI13XX_TASK_PRIORITY > 0, "CI worker cannot use idle priority");
static_assert(ESPLINK_CI13XX_IDLE_WAIT_MS > 0 && ESPLINK_CI13XX_IDLE_WAIT_MS <= 1000 &&
  ESPLINK_CI13XX_POLL_MS > 0 && ESPLINK_CI13XX_POLL_MS <= 1000,
  "CI waits must be 1..1000 milliseconds");
#endif
static_assert(ESPLINK_RX_PUMP_BUDGET >= 64 && ESPLINK_RX_PUMP_BUDGET <= 65536,
  "RX pump budget must be 64..65536 bytes");
