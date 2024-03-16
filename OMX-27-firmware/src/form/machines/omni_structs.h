#pragma once
#include "../../ClearUI/ClearUI_Input.h"

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
        OMNIUIMODE_CONFIG,      // Should be same thing as OMNIUIMODE_PARAMS
        OMNIUIMODE_MIX,         // 
        OMNIUIMODE_LENGTH,
        OMNIUIMODE_TRANSPOSE,
        OMNIUIMODE_STEP,
        OMNIUIMODE_NOTEEDIT,
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
        STEPFUNC_FWD,      // Sets track to play forward
        STEPFUNC_REV,      // Sets track to play backward
        STEPFUNC_PONG,     // Reverses current direction of track
        STEPFUNC_RANDJUMP, // Randomly jumps to a step
        STEPFUNC_RAND,     // Randomly does a function or NONE
        STEPFUNC_COUNT
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

    struct Step
    {
        uint8_t mute : 1;      // bool for mute
        int8_t notes[6];       // 0 - 127, -1 for off
        int8_t potVals[5];     // 0 -> 127, -1 for off
        uint8_t vel : 7;       // 0 - 127
        int8_t nudge : 7;      // Nudge note back or forward. Range is +- 60, displayed as -100% to +100%, , displ
        uint8_t len : 5;       // [0]0.25 - 64th note, [1]0.5 - 32nd note, [2]0.75, [3]1 - 16 steps
        uint8_t func : 5;      // StepFunc 7 or jump to specific step + 16 max 23
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
            for (uint8_t i = 0; i < 6; i++)
                notes[i] = -1;
            for (uint8_t i = 0; i < 5; i++)
                potVals[i] = -1;
            vel = 127;
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

        void CopyFrom(Step *other)
        {
            mute = other->mute;
            for (uint8_t i = 0; i < 6; i++)
                notes[i] = other->notes[i];
            for (uint8_t i = 0; i < 4; i++)
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
        // This is rot in current Seq, just going to make this a utility function that moves everything
        uint8_t startstep : 6;     // Max 63,  Step that track starts on, -1 for random?
        int8_t swing : 8;         // Amount of swing, + or minus 100. Shifts off notes forward back, similar to nudge, but applies to whole track. 
        uint8_t swingDivision : 1; // 16th or 8th note swing
        uint8_t tripletMode : 1;   // automatically nudges every 2nd and 3rd step to become a triplet
        uint8_t playDirection : 1; // Forward or back
        uint8_t playMode : 3;      // Shuffles and randomizes
        uint8_t midiFx : 3;        // MidiFX index, 0 for off, 1-5 for MidiFX Groups 1-5

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
        uint8_t potMode : 1;

        TransposePattern transposePattern;

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
        }
    };
}
