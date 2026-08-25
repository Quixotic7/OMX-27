#pragma once
// FORM v2 — per-platform capacity caps (Phase 1). Device-only (pulls BOARDTYPE).
// Measured (host g++, GNU bitfield ABI == arm-gcc): sizeof(Pattern) = 9296 B.
//   16 patterns fully-resident = ~145 KB.
//   RP2040 V3 (264 KB RAM) and Teensy 4 (512 KB) both hold 16 in RAM — no streaming.
//   Teensy 3.1 (64 KB) cannot; it gets a reduced count.

#include <Arduino.h>          // pin macros (A14 etc.) that consts.h references on Teensy
#include "form2_data.h"
#include "../consts/consts.h" // BOARDTYPE / OMX2040 / TEENSY32 / TEENSY4

#if BOARDTYPE == TEENSY32
// Teensy 3.1 — 64 KB total; baseline firmware already ~34 KB. ~3-4 patterns of headroom.
// Start conservative; tune against the real build size line.
#define FORM_NUM_PATTERNS 4
#else
// RP2040 (V3) and Teensy 4 — 16 patterns fit in RAM (~145 KB).
#define FORM_NUM_PATTERNS 16
#endif

// Phase 1: all patterns are resident in RAM (measured to fit). If a future platform
// needs streaming, only this and the store back-end change.
#define FORM_PATTERNS_RESIDENT FORM_NUM_PATTERNS
