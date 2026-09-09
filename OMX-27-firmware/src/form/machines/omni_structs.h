#pragma once
#include "../../ClearUI/ClearUI_Input.h"
#include "../../config.h"

namespace FormOmni
{

    enum TransposeMode
    {
        TRANPOSEMODE_INTERVAL,      // Transpose patterns and values are in intervals of current scale and root
        TRANPOSEMODE_SEMITONE,      // Transpose patterns and values are in semitones
        TRANPOSEMODE_LOCALINTERVAL,  // Transpose patterns and values are in intervals of current scale, the step note is the root
        TRANPOSEMODE_COUNT
    };

    enum OmniUIMode
    {
        OMNIUIMODE_CONFIG,   // the machine's param menu (Step view + Mix menu)
        OMNIUIMODE_MIX,      // Mix/Tools track overview
        OMNIUIMODE_TRANSPOSE,// Transpose editor
        OMNIUIMODE_NOTEEDIT, // Notes view (shell-rendered; the machine no-ops in this mode)
        OMNIUIMODE_COUNT
    };

    enum PlayDirection
    {
        TRACKDIRECTION_FORWARD, // Steps move forward
        TRACKDIRECTION_REVERSE, // Steps move backward
        TRACKDIRECTION_COUNT
    };

    enum PlayMode
    {
        TRACKMODE_NONE,
        TRACKMODE_PONG,         // Steps move forward and reverse on last step
        TRACKMODE_RAND,         // Each step is randomly selected
        TRACKMODE_RANDNODUPE,   // Steps are randomly selected but won't play the same step twice
        TRACKMODE_SHUFFLE,      // Steps are randomly shuffled each time the pattern loops
        TRACKMODE_SHUFFLE_HOLD, // Steps are shuffled once when playback starts.
                                // For shuffle modes, steps will be reshuffled if the track length is changed
        TRACKMODE_COUNT
    };

    enum StepFunc
    {
        STEPFUNC_NONE,     // No Function
        STEPFUNC_RESTART,  // Restarts to start step
        STEPFUNC_REV,      // Sets track to play backward
        STEPFUNC_FWD,      // Sets track to play forward
        STEPFUNC_PONG,     // Reverses current direction of track
        STEPFUNC_RANDJUMP, // Randomly jumps to a step
        STEPFUNC_RAND,     // Randomly does a function or NONE
        STEPFUNC_COUNT
    };

    struct OmniTriggeredNoteTracker
    {
        uint8_t noteNumber = 0;
    };

    struct OmniNoteTracker
    {
        uint8_t channel : 4;
        uint8_t midiFXIndex : 3;        // MidiFX index, 0 for off, 1-5 for MidiFX Groups 1-5
        uint8_t noteNumber = 0;
        // uint8_t keyIndex = 0; // use if t
        // uint8_t prevNoteNumber = 0; // note number before being modified by midiFX
        // uint8_t velocity = 100;
        float stepLength = 0; // fraction or multiplier of clockConfig.step_micros, 1 == 1 step
        uint8_t sendMidi : 1;
        uint8_t sendCV : 1;
        uint32_t noteonMicros = 0;
        uint8_t unknownLength : 1;
        // bool noteOff = false; // Set true if note off, corresponding note on should have stepLength of 0

        OmniNoteTracker()
        {
            midiFXIndex = 0;
            channel = 0;
            midiFXIndex = 0;
            sendMidi = 1;
            sendCV = 1;
            unknownLength = 0;
        }

        // Set with full note non-bitmasked midifx version where 0-4 map to a MidiFX group and 255 is off
        void setMidiFXIndex(uint8_t midiFX)
        {
            if(midiFX >= NUM_MIDIFX_GROUPS)
            {
                midiFXIndex = 0;
            }
            else
            {
                midiFXIndex = midiFX + 1;
            }
        }

        // Returns full note non-bitmasked midifx version where 0-4 map to a MidiFX group and 255 is off
        uint8_t getMidifFXIndex()
        {
            if(midiFXIndex == 0)
            {
                return 255;
            }
            return midiFXIndex - 1;
        }

        void setFromNoteGroup(MidiNoteGroup noteGroup)
        {
            channel = noteGroup.channel - 1;
            noteNumber = noteGroup.noteNumber;
            stepLength = noteGroup.stepLength;
            sendMidi = (uint8_t)noteGroup.sendMidi;
            sendCV = (uint8_t)noteGroup.sendCV;
            noteonMicros = noteGroup.noteonMicros;
            unknownLength = (uint8_t)noteGroup.unknownLength;
        }

        MidiNoteGroup toMidiNoteGroup()
        {
            MidiNoteGroup noteGroup;

            noteGroup.channel = channel + 1;
            noteGroup.velocity = 100;
            noteGroup.noteNumber = noteNumber;
            noteGroup.prevNoteNumber = noteNumber;
            noteGroup.stepLength = stepLength;
            noteGroup.sendMidi = sendMidi == 1;
            noteGroup.sendCV = sendCV == 1;
            noteGroup.noteonMicros = noteonMicros;
            noteGroup.unknownLength = unknownLength == 1;
            return noteGroup;
        }
    };

    // 96 pulse per quarter note
    // 24 pules per 16th note`

    // Data for a step that is not saved
    struct StepDynamic
    {
        uint8_t tPatPos : 4; // Position in transpose pattern for accumulating

        StepDynamic()
        {
            ResetPositions();
        }

        void ResetPositions()
        {
            tPatPos = 0;
        }
    };

    // Which per-step parameters can be P-Locked (each has a bit in Step::locks).
    enum StepLockBit
    {
        SLOCK_VEL = 0,
        SLOCK_NUDGE,
        SLOCK_LEN,
        SLOCK_MFX,
        SLOCK_PROB,
        SLOCK_COND,
        SLOCK_FUNC,
        SLOCK_ACCUM,
        SLOCK_REPEAT,
        SLOCK_COUNT
    };

    struct Step
    {
        // Packed into one 2-byte unit (14 bits) so the lock flags cost almost nothing.
        uint16_t mute : 1;   // bool for mute
        uint16_t repeat : 3; // ratchet count index: 0 = 1x (off), 1 = 2x, 2 = 3x (triplet), 3 = 4x
        uint16_t trig : 1;   // "ghost" trigger: step is on with no notes (e.g. a locked value/CC)
        uint16_t locks : 9;  // P-Lock flags: bit set = that param is explicitly locked (StepLockBit)
        int8_t notes[6];     // 0 - 127, -1 for off
        int8_t potVals[5];     // 0 -> 127, -1 for off
        uint8_t vel : 7;       // 0 - 127
        int8_t nudge : 7;      // Nudge note back or forward. Range is +- 60, displayed as -100% to +100%, , displ
        uint8_t len : 5;       // [0]0.25 - 64th note, [1]0.5 - 32nd note, [2]0.75, [3]1 - 16 steps
        uint8_t func : 7;      // Off, Reset, Forward, Reverse, Jump Rand, Rand, Jump to specific step + 16 max 23
        uint8_t prob : 7;      // 0 - 100% Chance
        uint8_t condition : 6; // 0 - 36, 1:2, 2:2, etc, add Fill, !Fill, Pre, !Pre, Nei, !Nei, 1st, !1st, 
        uint8_t accumTPat : 3; // 0 for off, 1-4 to accumulate the transpose pattern, max 4
        uint8_t mfxIndex : 3;  // 0 = Off, 1 = Track, 2 - 7 = MidiFX Group 1-5, Max 7

        // Does not need to be saved
        // Moved to StepPositions struct
        // uint8_t tPatPos : 4;    // Position in transpose pattern, this gets added to the track position mod 16

        Step()
        {
            setToInit();
        }

        void setToInit()
        {
            // Set defaults
            mute = 0;
            repeat = 0;
            trig = 0;
            locks = 0;
            for (uint8_t i = 0; i < 6; i++)
                notes[i] = -1;
            for (uint8_t i = 0; i < 5; i++)
                potVals[i] = -1;
            vel = 100;
            nudge = 0;
            len = 3;
            func = 0;
            prob = 100;
            condition = 0;
            accumTPat = 0;
            mfxIndex = 1;
            // tPatPos = 0;
        }

        bool hasNotes()
        {
            for(uint8_t i = 0; i < 6; i++)
            {
                if(notes[i] >= 0 && notes[i] <= 127)
                {
                    return true;
                }
            }
            return false;
        }

        // "On" for the Step-view grid: has notes, or is an intentional ghost trigger.
        bool isOn() { return trig || hasNotes(); }

        void CopyFrom(Step *other)
        {
            mute = other->mute;
            repeat = other->repeat;
            trig = other->trig;
            locks = other->locks;
            for (uint8_t i = 0; i < 6; i++)
                notes[i] = other->notes[i];
            for (uint8_t i = 0; i < 5; i++)
                potVals[i] = other->potVals[i];
            vel = other->vel;
            nudge = other->nudge;
            len = other->len;
            func = other->func;
            prob = other->prob;
            condition = other->condition;
            accumTPat = other->accumTPat;
            mfxIndex = other->mfxIndex;

            // tPatPos = other->tPatPos;
        }

        bool isLocked(uint8_t bit) { return (locks & (1 << bit)) != 0; }
        void setLock(uint8_t bit) { locks |= (1 << bit); }
        void clearLock(uint8_t bit) { locks &= ~(1 << bit); }
    };

    struct TrackDynamic
    {
        StepDynamic steps[64];

        void Reset()
        {
            for (uint8_t i = 0; i < 64; i++)
            {
                steps[i].ResetPositions();
            }
        }
    };

    struct Track
    {
        Step steps[64];

        uint8_t len : 6; // Max 63, Length of track, 0 - 63, maps to 1 - 64
        uint8_t startstep : 6;     // RESERVED — never read; kept for save-format layout (v8)
        int8_t swing : 8;         // Amount of swing, + or minus 100. Shifts off notes forward back, similar to nudge, but applies to whole track. 
        uint8_t swingDivision : 1; // 16th or 8th note swing
        uint8_t tripletMode : 1;   // automatically nudges every 2nd and 3rd step to become a triplet
        uint8_t playDirection : 1; // Forward or back
        uint8_t playMode : 3;      // Shuffles and randomizes
        uint8_t midiFx : 3;        // MidiFX index, 0 for off, 1-5 for MidiFX Groups 1-5

        uint8_t enabledPages : 4; // bitmask of pages in the playback loop (muted = bit clear)
        uint8_t pageLen[4];       // steps per page (1-16), for polymeter

        // Per-track defaults for the P-Lockable step params (pid 0-7: Vel/Nudge/Len/MFX/Prob/
        // Cond/Func/Accum). Unlocked steps track these; editing a default pushes it to them.
        int8_t paramDefaults[8];

        Track()
        {
            len = 15;
            startstep = 0;
            swing = 0;
            swingDivision = 0;
            tripletMode = 0;
            playDirection = TRACKDIRECTION_FORWARD;
            playMode = TRACKMODE_NONE;
            midiFx = 0;
            enabledPages = 0b0001; // only page 1 in the loop by default
            for (uint8_t i = 0; i < 4; i++)
                pageLen[i] = 16;
            initParamDefaults();
            syncLen();
        }

        // ---- Polymeter: playback runs in "position space" (0 .. totalLen-1) over the ENABLED
        // pages, in ascending order; each position maps to an absolute step index. `len` is
        // kept = totalLen-1 for the rest of the engine.
        uint8_t pageStepLen(uint8_t p) { return pageLen[p] == 0 ? 1 : pageLen[p]; }
        uint16_t totalLen()
        {
            uint16_t t = 0;
            for (uint8_t p = 0; p < 4; p++)
                if (enabledPages & (1 << p))
                    t += pageStepLen(p);
            return t == 0 ? 1 : t;
        }
        void syncLen()
        {
            if ((enabledPages & 0x0F) == 0)
                enabledPages = 0b0001; // never let the loop be empty
            uint16_t t = totalLen();
            len = (t > 64 ? 64 : t) - 1;
        }
        uint8_t positionToStep(uint16_t pos)
        {
            for (uint8_t p = 0; p < 4; p++)
            {
                if (!(enabledPages & (1 << p)))
                    continue;
                uint8_t pl = pageStepLen(p);
                if (pos < pl)
                    return p * 16 + pos;
                pos -= pl;
            }
            return 0;
        }
        uint16_t stepToPosition(uint8_t absIdx)
        {
            uint8_t page = absIdx / 16, sp = absIdx % 16;
            if (!(enabledPages & (1 << page)))
                return 0; // disabled page -> loop start
            uint16_t pos = 0;
            for (uint8_t p = 0; p < page; p++)
                if (enabledPages & (1 << p))
                    pos += pageStepLen(p);
            uint8_t maxsp = pageStepLen(page) - 1;
            pos += (sp > maxsp ? maxsp : sp);
            return pos;
        }

        void initParamDefaults()
        {
            paramDefaults[0] = 100; // vel
            paramDefaults[1] = 0;   // nudge
            paramDefaults[2] = 3;   // len (0.75)
            paramDefaults[3] = 1;   // mfx (track)
            paramDefaults[4] = 100; // prob
            paramDefaults[5] = 0;   // cond
            paramDefaults[6] = 0;   // func
            paramDefaults[7] = 0;   // accum
        }

        uint8_t getLength()
        {
            return len + 1;
        }

        bool isStepOn(uint8_t stepIndex)
        {
            if(stepIndex > len) return false;

            return !steps[stepIndex].mute;
        }
    };

    struct TransposePattern
    {
        int8_t pat[16]; // second pattern for transposing notes
        uint8_t len : 4;    // Length of transpose pattern

        TransposePattern()
        {
            Reinit();
        }

        void Reinit()
        {
            len = 15;

            for (uint8_t i = 0; i < 16; i++)
            {
                pat[i] = 0;
            }
        }
    };

    struct OmniSeqDynamic
    {
        TrackDynamic tracks[1];

        void Reset()
        {
            for(uint8_t i = 0; i < 1; i++)
            {
                tracks[i].Reset();
            }
        }
    };

    // Saved sequencer variables
    struct OmniSeq
    {
        Track tracks[1]; // Only one track per seq, possibly more in future if mem permits

        int8_t transpose : 8; // +- 64, in intervals or semitones depending on transposeMode
        uint8_t rate : 5;
        uint8_t transposeMode : 2; // Max 2, Intervals, semitones, or step intervals
        uint8_t channel : 4;       // 0 - 15 , maps to channels 1 - 16
        uint8_t monoPhonic : 1;    // bool
        uint8_t mute : 1;          // bool
        uint8_t solo : 1;          // bool
        uint8_t sendMidi : 1;      // bool
        uint8_t sendCV : 1;        // bool
        uint8_t applyTransPat : 1; // bool
        uint8_t gate : 7;          // 0-100 mapping to 0-200% for quick legato
        uint8_t potBank : 3;
        uint8_t potMode : 1;       // RESERVED — never read; kept for save-format layout (v8)

        TransposePattern transposePattern;

        // Per-track scale (v9): mode GLOBAL/CHROMATIC/LOCAL, plus the track's own root and
        // pattern for LOCAL mode (-1 = off). Living inside OmniSeq means the scale saves on
        // every board through the normal kOmniSaveVersion gate and travels with the pattern
        // (per-pattern scale) instead of the old RP2040-only bank-file tail.
        uint8_t scaleMode;    // TrackScaleMode: 0 GLOBAL, 1 CHROMATIC, 2 LOCAL
        uint8_t localRoot;    // 0-11
        int8_t localPattern;  // -1 = off/chromatic

        OmniSeq()
        {
            transpose = 0;
            rate = 9;
            transposeMode = 0;
            channel = 0;
            monoPhonic = false;
            mute = false;
            solo = false;
            applyTransPat = false;
            sendMidi = true;
            sendCV = true;
            gate = 40;
            scaleMode = 0;
            localRoot = 0;
            localPattern = 0;
        }
    };
}
