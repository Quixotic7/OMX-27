#pragma once
// FORM — per-platform capacity caps + fixed geometry. Device-only (pulls BOARDTYPE).
// (Formerly src/form2/form2_config.h; the rest of form2/ was retired.)
// NOTE: the live model is FormPattern (OmniSeq x 8) — sizeof measured ~10.6 KB/pattern,
//   so 16 patterns fully-resident = ~165 KB. Re-measure before raising any cap.
//   RP2040 V3 (264 KB RAM) and Teensy 4 (512 KB) both hold 16 in RAM — no streaming.
//   Teensy 3.1 (64 KB) cannot; it gets a reduced count.

#include <Arduino.h>          // pin macros (A14 etc.) that consts.h references on Teensy
#include "../consts/consts.h" // BOARDTYPE / OMX2040 / TEENSY32 / TEENSY4

// ---- Fixed geometry (board-independent). Overridable, but these are the v2 spec. ----
// (Moved from the retired form2_data.h scaffolding — the shipping data model is the
// OmniSeq-based FormPattern in src/form/form_patterns.h.)
#ifndef FORM_NUM_TRACKS
#if BOARDTYPE == TEENSY32
// Teensy 3.1 (64 KB): 4 tracks instead of 8. Each track is a heap machine (~1.6 KB) plus an
// OmniSeq in the resident pattern (~1.3 KB), so halving the count frees ~11.8 KB — enough
// headroom for MidiFX. Pages stay at 4 (steps-per-track unchanged). RP2040/Teensy 4 keep 8.
#define FORM_NUM_TRACKS 4
#else
#define FORM_NUM_TRACKS 8
#endif
#endif
#ifndef FORM_NUM_PAGES
#define FORM_NUM_PAGES 4
#endif
#ifndef FORM_STEPS_PER_PAGE
#define FORM_STEPS_PER_PAGE 16
#endif
#ifndef FORM_STEPS_PER_TRACK
#define FORM_STEPS_PER_TRACK (FORM_NUM_PAGES * FORM_STEPS_PER_PAGE) // 64
#endif

#if BOARDTYPE == TEENSY32
// Teensy 3.1 — only 64 KB RAM. The resident bank `patterns_[N]` is the single biggest static
// object (~10.6 KB/pattern), and the 8 track machines are heap-allocated at boot on top of it;
// 2 patterns left too little free RAM for that allocation, so the device wouldn't start. Keep
// exactly ONE resident pattern here (no in-RAM pattern switching on Teensy 3.1) to leave heap
// headroom. RP2040/Teensy 4 keep the full 16-pattern bank below.
#define FORM_NUM_PATTERNS 1
#else
// RP2040 (V3) and Teensy 4 — 16 patterns fit in RAM (~145 KB).
#define FORM_NUM_PATTERNS 16
#endif

// Phase 1: all patterns are resident in RAM (measured to fit). If a future platform
// needs streaming, only this and the store back-end change.
#define FORM_PATTERNS_RESIDENT FORM_NUM_PATTERNS
