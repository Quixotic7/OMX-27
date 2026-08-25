#pragma once
// FORM v2 — portable data model (Phase 1).
// Dependency-free on purpose so it compiles on-device AND on a host g++ for
// size / round-trip measurement. No Arduino headers here.
//
// Mirrors the packed OMNI `Step` (omni_structs.h) — which already carries almost
// every v2 per-step field — and adds the one genuinely new field: `repeat`.
// See design/form/FORM_IMPLEMENTATION.md §A/§B.

#include <cstdint>

// ---- Fixed geometry (board-independent). Overridable, but these are the v2 spec. ----
#ifndef FORM_NUM_TRACKS
#define FORM_NUM_TRACKS 8
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

namespace form2
{
    static const uint8_t kMaxNotesPerStep = 6; // polyphony per step (chord)
    static const uint8_t kNumPotLocks = 5;     // P-lock slots = the 5 pot-bank knobs

    // One step. All 8 edit-mode values live here (Note/Vel/Len/Repeat/Chance/Math/
    // Function/MIDI-FX) plus microtiming (nudge) and CC P-locks (potVals).
    struct Step
    {
        int8_t notes[kMaxNotesPerStep]; // MIDI notes, -1 = empty slot
        int8_t potVals[kNumPotLocks];   // CC P-locks, -1 = unlocked (0-127 = locked value)

        uint8_t mute : 1;      // step muted
        uint8_t vel : 7;       // Velocity 0-127
        int8_t nudge : 7;      // Microtiming, +/-60 (enables quantize-off record)
        uint8_t len : 5;       // Step Length: index into the length table
        uint8_t repeat : 3;    // NEW: Repeat/ratchet index (1 / 2 / 3-triplet / 4)
        uint8_t func : 7;      // Function: OMNI step functions (RSET/rev/fwd/pong/jump/…)
        uint8_t prob : 7;      // Chance 0-100%
        uint8_t condition : 6; // Math: conditional trig (A:B ratio + Fill/!Fill), 0-36
        uint8_t accumTPat : 3; // transpose-pattern accumulate (0=off, 1-4)
        uint8_t mfxIndex : 3;  // MIDI FX: 0=Off, 1-5 = FX slot (per-step, P-lockable)

        Step() { clear(); }

        void clear()
        {
            for (uint8_t i = 0; i < kMaxNotesPerStep; i++)
                notes[i] = -1;
            for (uint8_t i = 0; i < kNumPotLocks; i++)
                potVals[i] = -1;
            mute = 0;
            vel = 100;
            nudge = 0;
            len = 3; // 1 step (see length table, index 3)
            repeat = 0;
            func = 0;
            prob = 100;
            condition = 0;
            accumTPat = 0;
            mfxIndex = 0;
        }

        bool hasNotes() const
        {
            for (uint8_t i = 0; i < kMaxNotesPerStep; i++)
                if (notes[i] >= 0 && notes[i] <= 127)
                    return true;
            return false;
        }
    };

    // Play direction / order for a track.
    enum PlayMode : uint8_t
    {
        PLAY_FORWARD = 0,
        PLAY_REVERSE,
        PLAY_FWD_PONG,
        PLAY_REV_PONG,
        PLAY_RANDOM_PAGE, // plays each enabled page in random order
        PLAY_MODE_COUNT
    };

    // One track = 64 steps (4 pages x 16) + its settings.
    struct Track
    {
        Step steps[FORM_STEPS_PER_TRACK];

        uint8_t pageLen[FORM_NUM_PAGES]; // per-page length 1-16 (polymeter); stored as len-1
        uint8_t enabledPages;            // bitmask: which pages are in the playback loop

        int8_t swing;              // per-track swing, +/-100
        uint8_t rate;              // rate index
        uint8_t channel : 4;       // MIDI channel 1-16 (stored 0-15)
        uint8_t potBank : 3;       // pot-bank index 0-4
        uint8_t midiFx : 3;        // track-level MIDI-FX slot (0=off, 1-5)
        uint8_t playMode : 3;      // PlayMode
        uint8_t swingDivision : 1; // 16th / 8th
        uint8_t tripletMode : 1;
        uint8_t muted : 1;
        uint8_t soloed : 1;
        uint8_t sendMidi : 1;
        uint8_t sendCV : 1;

        Track() { init(); }

        void init()
        {
            for (uint8_t p = 0; p < FORM_NUM_PAGES; p++)
                pageLen[p] = FORM_STEPS_PER_PAGE - 1; // full 16-step page
            enabledPages = 0x01;                      // page 1 in the loop
            swing = 0;
            rate = 0;
            channel = 0;
            potBank = 0;
            midiFx = 0;
            playMode = PLAY_FORWARD;
            swingDivision = 0;
            tripletMode = 0;
            muted = 0;
            soloed = 0;
            sendMidi = 1;
            sendCV = 0;
        }
    };

    // One pattern = a whole-sequencer snapshot: all tracks + their settings.
    struct Pattern
    {
        Track tracks[FORM_NUM_TRACKS];
    };

    // Project-level globals (one project only — the device has no room for more).
    struct Globals
    {
        uint16_t bpmX10;      // BPM * 10 (e.g. 1200 = 120.0)
        uint8_t clockSource;  // internal / external
        uint8_t scaleRoot;    // 0-11
        int8_t scalePattern;  // -1 = chromatic, else scale index
        int8_t swing;         // global swing
        uint8_t groove;       // groove template index
        uint8_t curPattern;   // active pattern index
        uint8_t flags;        // bit0 = count-in, …
    };

    // Compile-time floor checks (bounds get tightened once measured on host).
    static_assert(sizeof(Step) <= 24, "form2::Step larger than expected");
    static_assert(sizeof(Track) <= sizeof(Step) * FORM_STEPS_PER_TRACK + 16,
                  "form2::Track header larger than expected");
} // namespace form2
