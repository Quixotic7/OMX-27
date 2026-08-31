#include "form_machine_omni.h"
#include "../../config.h"
#include "../../globals.h" // sysSettings/potSettings/midiMacroConfig moved here in the q7/RP2040 restructure
#include "../../consts/colors.h"
#include "../../consts/consts.h"
#include "../../utils/omx_util.h"
#include "../../midi/midi.h" // MM::sendControlChange for per-step CC locks
#include "../../hardware/omx_disp.h"
#include "../../hardware/omx_leds.h"
#include "omni_note_editor.h"
#include "../../modes/euclidean_sequencer.h" // EuclideanMath (static helpers) for the Euclid tool
#include "../../modes/retro_grids.h"         // grids::GridsChannel for the Grids tool
#include <U8g2_for_Adafruit_GFX.h>
// #include <algorithm>

namespace FormOmni
{
    // The machine menu (walked with the encoder from the Step machine-menu and from Mix).
    // Opens on the specialized editors (STEPNOTES / STEPPOTS / TPAT — kept as-is per the
    // FORM_V2_REVIEW.md §4 decisions); the seven param pages after them render in the
    // shared dispStepParams grid. The old STEP1/STEPCONDITION pages are gone — the shell's
    // Step param pages 1-2 (P-Lockable) cover those fields.
    // The machine menu. The SEQ view walks only OMNIPAGE_STEPNOTES (its notes editor);
    // the MIX view walks OMNIPAGE_TRACK..SCALE (track/global params) — Seq is for
    // programming steps, Mix is for track-level control (FORM_V2_REVIEW.md).
    // TPAT is not a menu page: it's the Transpose view's renderer id.
    enum OmniPage
    {
        OMNIPAGE_STEPNOTES,  // per-step notes editor (Seq menu)
        OMNIPAGE_TRACK,      // Length, MidiFX          (Mix menu from here on)
        OMNIPAGE_TRACKMODES, // Triplet, Direction, Mode
        OMNIPAGE_SEQTPOSE,   // Transpose, Mode, Apply TPat
        OMNIPAGE_SEQMIDI,    // Chan, Mono, SendMidi, SendCV
        OMNIPAGE_TIMINGS,    // BPM, Rate, Swing, Swing Div
        OMNIPAGE_SCALE,      // Root, Scale, Lock, Group
        OMNIPAGE_COUNT,
        OMNIPAGE_TPAT = OMNIPAGE_COUNT // Transpose-view render id (not in trackParams_)
    };

    // 2-char cell codes for the play modes (full kTrackModeMsg name pops while turning —
    // §4 label rule: a dispStepParams cell fits <=3 digits of number or <=2 chars of text).
    const char* kTrackModeShort[] = {"--", "PG", "RD", "R2", "SF", "SH"};
    const char* kTranspModeShort[] = {"GI", "SE", "LI"};

    enum OmniStepPage
    {
        OSTEPPAGE_1,
        OSTEPAGE_COUNT
    };

    const char* kUIModeMsg[] = {"CONFIG", "MIX", "LENGTH", "TRANSPOSE", "STEP", "NOTE EDIT"};

    const char* kPotMode[] = {"CC Step", "CC Fade"};

    const char* kTranspModeMsg[] = {"GINT", "SEMI", "LINT"};
    const char* kTranspModeLongMsg[] = {"GBL INTERVAL", "SEMITONES", "LOC INTERVAL"};

    const char* kTrackModeMsg[] = {"NONE", "PONG", "RAND", "RND2", "SHUF", "SHLD"};

    // kSeqRates[] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64};
    // 1, 2, 3, 4, 8, 16, 32, 64
    const uint8_t kRateShortcuts[] = {0, 1, 2, 3, 6, 9, 12, 15};

    const uint8_t kZoomMults[] = {1,2,4};
    const uint8_t kPageMax[] = {4,2,1};

    // v2: fixed 4 pages of 16 steps (zoom retired; zoomLevel_ stays 0).
    static const uint8_t kFormNumPages = 4;

    // 0, 0.33333, 0.66666, 0.83333
    // 0, 0.25, 0.5, 0.75, 1.0
    // 0, 0.083333, 0.166666, 0.083333
    // Percent, how much to nudge note forward to become a triplet note
    const float kTripletNudge[] = {0.0f, 1.0f/3.0f, 1.0f/3.0f * 2.0f, 0.0f};

    // Mod to use for swing
    // 16th is 2
    // 0S0S0S
    // 00S00
    // 10001000100010001000
    const uint8_t kSwingDivisionMod[] = {2,4};

    // const char *kTrigConditions[36] = {
	// "1:1",
	// "1:2", "2:2",
	// "1:3", "2:3", "3:3",
	// "1:4", "2:4", "3:4", "4:4",
	// "1:5", "2:5", "3:5", "4:5", "5:5",
	// "1:6", "2:6", "3:6", "4:6", "5:6", "6:6",
	// "1:7", "2:7", "3:7", "4:7", "5:7", "6:7", "7:7",
	// "1:8", "2:8", "3:8", "4:8", "5:8", "6:8", "7:8", "8:8"};

    const char* kConditionModes[9] = {"--", "FILL", "!FIL", "PRE", "!PRE", "NEI", "!NEI", "1ST", "!1ST"};

    // Must be a quick way to calculate this
    uint8_t kTrigConditionsAB[35][2] = {
	{1, 2},	{2, 2},
	{1, 3},	{2, 3},	{3, 3},
	{1, 4},	{2, 4},	{3, 4},	{4, 4},
	{1, 5},	{2, 5},	{3, 5},	{4, 5},	{5, 5},
	{1, 6},	{2, 6},	{3, 6},	{4, 6},	{5, 6},	{6, 6},	
    {1, 7},	{2, 7},	{3, 7},	{4, 7},	{5, 7},	{6, 7},	{7, 7},
	{1, 8},	{2, 8},	{3, 8},	{4, 8},	{5, 8},	{6, 8},	{7, 8},	{8, 8}};

    // int sizeArray[sizeof(kTrigConditions)];

    // Off, Reset, Forward, Reverse, Jump Rand, Rand, Jump to step
    const char *kStepFuncs[7] = {"--", "RSET", "<<", ">>", "<>", "J?", "???"};

    const int kStepFuncColors[7] = {RED, ORANGE, DKYELLOW, GREEN, MAGENTA, ROSE, DIMORANGE};

    // v2 Step value palettes: len index for 0.5·0.75·1·2·4·6·8·16·32·64, and mfxIndex for Off + FX 1-5.
    static const uint8_t kLenPalette[10] = {2, 3, 4, 5, 7, 9, 11, 19, 20, 22};
    static const uint8_t kMfxPalette[6] = {0, 2, 3, 4, 5, 6};

    // Global param management so pages are same across machines
    ParamManager trackParams_;
    ParamManager tPatParams_;

    bool paramsInit_ = false;
    bool neighborPrevTrigWasTrue_ = false;

    uint8_t omniUiMode_ = 0;

    // The ParamManagers above are shared namespace globals. They must be populated
    // AT RUNTIME, not from the constructor: omxModeForm is a global object, so its
    // constructor (which news these OMNI machines) can run during static init BEFORE
    // these ParamManager globals are themselves constructed -- in which case the
    // ParamManager constructor later wipes any pages the OMNI ctor had added, leaving
    // 0 pages and a param selector stuck on param 1. Keying on getNumPages() makes
    // this idempotent and self-healing: loopUpdate() (runtime) rebuilds if wiped.
    void FormMachineOmni::ensureParamsInit()
    {
        if (trackParams_.getNumPages() > 0)
            return;

        trackParams_.addPage(7);  // OMNIPAGE_STEPNOTES
        trackParams_.addPage(2);  // OMNIPAGE_TRACK: Length, MidiFX
        trackParams_.addPage(3);  // OMNIPAGE_TRACKMODES: Triplet, Direction, Mode
        trackParams_.addPage(3);  // OMNIPAGE_SEQTPOSE: Transpose, Mode, Apply TPat
        trackParams_.addPage(4);  // OMNIPAGE_SEQMIDI: Chan, Mono, SendMidi, SendCV
        trackParams_.addPage(4);  // OMNIPAGE_TIMINGS: BPM, Rate, Swing, Swing Div
        trackParams_.addPage(4);  // OMNIPAGE_SCALE: Root, Scale, Lock, Group

        tPatParams_.addPage(17);

        paramsInit_ = true;
    }

    FormMachineOmni::FormMachineOmni()
    {
        ensureParamsInit();

        for (uint8_t k = 0; k < 16; k++)
            for (uint8_t i = 0; i < 6; i++)
                auditionNotes_[k][i] = -1;

        resetPlayback();

        onRateChanged();
    }
    FormMachineOmni::~FormMachineOmni()
    {
    }

    ParamManager *FormMachineOmni::getParams()
    {
        return &trackParams_;
    }

    void FormMachineOmni::onSelected()
    {
    }

    bool FormMachineOmni::doesConsumePots()
    {
        return true;
        // switch (omniUiMode_)
        // {
        // case OMNIUIMODE_CONFIG:
        // case OMNIUIMODE_MIX:
        // case OMNIUIMODE_LENGTH:
        // case OMNIUIMODE_TRANSPOSE:
        // case OMNIUIMODE_COUNT:
        //     return false;
        // case OMNIUIMODE_STEP:
        // case OMNIUIMODE_NOTEEDIT:
        //     return true;
        // }
        // return false;
    }

    bool FormMachineOmni::doesConsumeDisplay()
    {
        switch (omniUiMode_)
        {
        case OMNIUIMODE_CONFIG:
        case OMNIUIMODE_MIX:
        case OMNIUIMODE_LENGTH:
            return false;
        case OMNIUIMODE_TRANSPOSE:
        case OMNIUIMODE_STEP:
        case OMNIUIMODE_NOTEEDIT:
            return true;
        }
        return false;
    }

    bool FormMachineOmni::doesConsumeKeys()
    {
        switch (omniUiMode_)
        {
        case OMNIUIMODE_CONFIG:
        case OMNIUIMODE_MIX:
        case OMNIUIMODE_LENGTH:
            return false;
        case OMNIUIMODE_TRANSPOSE:
        case OMNIUIMODE_STEP:
        case OMNIUIMODE_NOTEEDIT:
            return true;
        }
        return false;
    }
    bool FormMachineOmni::doesConsumeLEDs()
    {
        return doesConsumeKeys();
    }

    void FormMachineOmni::onEncoderChanged(Encoder::Update enc)
    {
        if (getEncoderSelect())
            onEncoderChangedSelectParam(enc);
        else
            onEncoderChangedEditParam(enc);
    }

    void FormMachineOmni::seqNoteOn(MidiNoteGroup noteGroup, uint8_t midiFx)
    {
        if (context_ == nullptr || noteOnFuncPtr == nullptr)
            return;
        noteOnFuncPtr(context_, noteGroup, midiFx);
    }

    void FormMachineOmni::seqNoteOff(MidiNoteGroup noteGroup, uint8_t midiFx)
    {
        if (context_ == nullptr || noteOffFuncPtr == nullptr)
            return;
        noteGroup.noteOff = true;
        noteOffFuncPtr(context_, noteGroup, midiFx);
    }

    bool FormMachineOmni::getEncoderSelect()
    {
        bool shouldSelect = omxFormGlobal.encoderSelect;

        if(omniUiMode_ == OMNIUIMODE_TRANSPOSE)
        {
            shouldSelect = transpPat_.getEncoderSelect();
        }

	    return omxFormGlobal.encoderSelect && !midiSettings.midiAUX && shouldSelect;
    }

    void FormMachineOmni::playBackStateChanged(bool newIsPlaying)
    {
        if(newIsPlaying)
        {
            noteOns_.clear();

            ticksTilNext16Trigger_ = 0;
            
            // nextStepTime_ = seqConfig.lastClockMicros;
            // playingStep_ = 0;
            // ticksTilNextTrigger_ = 0;
            // ticksTilNextTriggerRate_ = 0;

            playRateCounter_ = playingStep_;

            onRateChanged();

            didNotesPlayThisStep_ = false;

            // Calculate first step
        }
        else
        {
            for(auto n : noteOns_)
            {
                auto noteGroup = n.toMidiNoteGroup();
                noteGroup.noteOff = true;
                noteGroup.unknownLength = true;
                seqNoteOff(noteGroup, n.getMidifFXIndex());
            }
            noteOns_.clear();

            ratchetDivs_ = 0; // don't leave a ratchet pending across stop
            ratchetStepIdx_ = -1;

            didNotesPlayThisStep_ = false;
        }
    }

    void FormMachineOmni::resetPlayback()
    {
        resetPlayback(true);
    }

    void FormMachineOmni::selectMidiFx(uint8_t mfxIndex, bool dispMsg)
    {
        auto track = getTrack();

        if (mfxIndex >= NUM_MIDIFX_GROUPS)
        {
            track->midiFx = 0;
        }
        else
        {
            track->midiFx = mfxIndex + 1;
        }

        if (dispMsg)
        {
            if (mfxIndex < NUM_MIDIFX_GROUPS)
            {
                omxDisp.displayMessageTimed("TRK MFX " + String(mfxIndex + 1), 5);
            }
            else
            {
                omxDisp.displayMessageTimed("TRK MFX Off", 5);
            }
        }
    }

    uint8_t FormMachineOmni::getSelectedMidiFX()
    {
         auto track = getTrack();

        if (track->midiFx == 0)
        {
            return 255;
        }

        return track->midiFx - 1;
    }

    void FormMachineOmni::resetPlayback(bool resetTickCounters)
    {
        // nextStepTime_ = seqConfig.lastClockMicros + ;
        playingStep_ = getRestartPos();
        pongDir_ = getTrack()->playDirection == TRACKDIRECTION_FORWARD ? 1 : -1; // pong starts in the set direction

        grooveCounter_ = 0;
        playRateCounter_ = 0;
        loopCounter_ = 0;
        loopCount_ = 0;
        firstLoop_ = true;
        prevCondWasTrue_ = false;
        neighborPrevTrigWasTrue_ = false;

        transpPat_.reset();
        seqDynamic_.Reset();

        calculateShuffle();

        if (resetTickCounters)
        {

            if (omxFormGlobal.isPlaying)
            {
                ticksTilNext16Trigger_ = 0;
                ticksTilNextTrigger_ = ticksTilNext16Trigger_;
                ticksTilNextTriggerRate_ = ticksTilNext16Trigger_;
            }
            else
            {
                ticksTilNextTrigger_ = 0;
                ticksTilNext16Trigger_ = 0;
                ticksTilNextTriggerRate_ = 0;
            }

            onRateChanged();
        }
    }

    Track *FormMachineOmni::getTrack()
    {
        return &seq_.tracks[0];
    }

    TrackDynamic *FormMachineOmni::getDynamicTrack()
    {
        return &seqDynamic_.tracks[0];
    }

    Step *FormMachineOmni::getSelStep()
    {
        return &getTrack()->steps[selStep_];
    }

    // Clamp seq_ fields a raw pattern blit can't guarantee. Called from setSeq (bank load +
    // pattern switch) and loadFromDisk (FRAM). Only the two fields whose valid range is
    // narrower than their bitfield width can go bad: potBank (:3 = 0-7, but NUM_CC_BANKS
    // pots banks) indexes past pots[][]; rate (:5 = 0-31, but kNumSeqRates entries) indexes
    // past kSeqRates[] and feeds a `/ rate` divide. channel (:4) and potMode (:1) already
    // can't exceed their valid range, so they need no clamp.
    void FormMachineOmni::sanitizeSeq()
    {
        if (seq_.potBank >= NUM_CC_BANKS)
            seq_.potBank = 0;
        if (seq_.rate >= kNumSeqRates)
            seq_.rate = 9; // 1:16 default
    }

    // ---- Tools view operations ----

    // Gather the absolute step indices of a tool's scope in play order. The whole-loop scope is
    // NOT contiguous in steps[]: pages live at p*16 and disabled pages / short-page tails are
    // skipped, so it must map each loop position through positionToStep. The active-page scope is
    // the page's own contiguous block. idx must hold up to 64 entries; returns the count.
    uint8_t FormMachineOmni::toolScopeIndices(bool wholeTrack, uint8_t *idx)
    {
        auto track = getTrack();
        if (wholeTrack)
        {
            uint8_t len = (uint8_t)track->getLength(); // <= 64
            for (uint8_t i = 0; i < len; i++)
                idx[i] = track->positionToStep(i);
            return len;
        }
        uint8_t start = (uint8_t)(activePage_ * 16);
        uint8_t len = getPageLen(activePage_);
        for (uint8_t i = 0; i < len; i++)
            idx[i] = start + i;
        return len;
    }

    // Shift steps by one position. wholeTrack = the whole playing loop; otherwise just the active
    // page. Operates over the scope's mapped indices so polymeter / disabled pages stay correct.
    void FormMachineOmni::toolRotate(int8_t dir, bool wholeTrack)
    {
        auto track = getTrack();
        uint8_t idx[64];
        uint8_t len = toolScopeIndices(wholeTrack, idx);
        if (len < 2 || dir == 0)
            return;
        if (dir > 0) // shift right: last -> first
        {
            Step tmp = track->steps[idx[len - 1]];
            for (uint8_t i = len - 1; i > 0; i--)
                track->steps[idx[i]] = track->steps[idx[i - 1]];
            track->steps[idx[0]] = tmp;
        }
        else // shift left: first -> last
        {
            Step tmp = track->steps[idx[0]];
            for (uint8_t i = 0; i < (uint8_t)(len - 1); i++)
                track->steps[idx[i]] = track->steps[idx[i + 1]];
            track->steps[idx[len - 1]] = tmp;
        }
    }

    void FormMachineOmni::toolMirror(bool wholeTrack)
    {
        auto track = getTrack();
        uint8_t idx[64];
        uint8_t len = toolScopeIndices(wholeTrack, idx);
        for (uint8_t i = 0; i < len / 2; i++)
        {
            Step tmp = track->steps[idx[i]];
            track->steps[idx[i]] = track->steps[idx[len - 1 - i]];
            track->steps[idx[len - 1 - i]] = tmp;
        }
    }

    void FormMachineOmni::toolShuffle(bool wholeTrack)
    {
        auto track = getTrack();
        uint8_t idx[64];
        uint8_t len = toolScopeIndices(wholeTrack, idx);
        for (uint8_t i = len; i > 1; i--) // Fisher-Yates
        {
            uint8_t j = (uint8_t)random(0, i); // 0..i-1
            Step tmp = track->steps[idx[i - 1]];
            track->steps[idx[i - 1]] = track->steps[idx[j]];
            track->steps[idx[j]] = tmp;
        }
    }

    void FormMachineOmni::toolScaleRemap(bool wholeTrack)
    {
        if (omxFormGlobal.musicScale == nullptr)
            return;
        auto track = getTrack();
        uint8_t idx[64];
        uint8_t len = toolScopeIndices(wholeTrack, idx);
        for (uint8_t i = 0; i < len; i++)
            for (uint8_t n = 0; n < 6; n++)
            {
                int8_t note = track->steps[idx[i]].notes[n];
                if (note >= 0)
                    track->steps[idx[i]].notes[n] = omxFormGlobal.musicScale->remapNoteToScale((uint8_t)note);
            }
    }

    void FormMachineOmni::toolQuantize(uint8_t amtPct, bool wholeTrack)
    {
        amtPct = constrain(amtPct, 0, 100);
        auto track = getTrack();
        uint8_t idx[64];
        uint8_t len = toolScopeIndices(wholeTrack, idx);
        for (uint8_t i = 0; i < len; i++)
            track->steps[idx[i]].nudge = (int8_t)((int)track->steps[idx[i]].nudge * (100 - amtPct) / 100);
    }

    void FormMachineOmni::toolChanceRnd(uint8_t pmin, uint8_t pmax)
    {
        if (pmin > pmax)
        {
            uint8_t t = pmin; pmin = pmax; pmax = t;
        }
        auto track = getTrack();
        for (uint8_t st = 0; st < 64; st++)
            if (track->steps[st].isOn())
                track->steps[st].prob = random(pmin, pmax + 1);
    }

    void FormMachineOmni::toolTranspose(int8_t semis, bool wholeTrack)
    {
        auto track = getTrack();
        uint8_t idx[64];
        uint8_t len = toolScopeIndices(wholeTrack, idx);
        for (uint8_t i = 0; i < len; i++)
            for (uint8_t n = 0; n < 6; n++)
            {
                int8_t note = track->steps[idx[i]].notes[n];
                if (note >= 0)
                    track->steps[idx[i]].notes[n] = (int8_t)constrain(note + semis, 0, 127);
            }
    }

    void FormMachineOmni::toolRandomVel(uint8_t vmin, uint8_t vmax)
    {
        if (vmin > vmax)
        {
            uint8_t t = vmin; vmin = vmax; vmax = t;
        }
        auto track = getTrack();
        for (uint8_t st = 0; st < 64; st++)
            if (track->steps[st].hasNotes())
                track->steps[st].vel = random(vmin, vmax + 1);
    }

    void FormMachineOmni::toolHumanize(uint8_t amtPct, bool wholeTrack)
    {
        int range = (int)(60L * constrain(amtPct, 0, 100) / 100); // nudge is +/-60
        auto track = getTrack();
        uint8_t idx[64];
        uint8_t len = toolScopeIndices(wholeTrack, idx);
        for (uint8_t i = 0; i < len; i++)
            if (track->steps[idx[i]].isOn())
                track->steps[idx[i]].nudge = (int8_t)random(-range, range + 1);
    }

    // Apply a boolean rhythm over the current scope's steps (page or whole loop, via
    // toolScopeIndices): on-steps trigger (empty ones are stamped with middle C),
    // off-steps are silenced (notes + trig cleared).
    void FormMachineOmni::applyRhythmScope(const bool *pattern, const uint8_t *vels, bool wholeTrack)
    {
        auto track = getTrack();
        uint8_t idx[64];
        uint8_t len = toolScopeIndices(wholeTrack, idx);
        for (uint8_t i = 0; i < len; i++)
        {
            Step *s = &track->steps[idx[i]];
            if (pattern[i])
            {
                s->trig = 1;
                if (!s->hasNotes())
                    s->notes[0] = 60;
                if (vels != nullptr)
                    s->vel = vels[i];
            }
            else
            {
                for (uint8_t n = 0; n < 6; n++)
                    s->notes[n] = -1;
                s->trig = 0;
            }
        }
    }

    // Build the euclidean rhythm for the current scope. Fills pattern[0..len-1] (len is the
    // scope length, <= 64; generation caps at EuclideanMath's 32 and tiles beyond). Also the
    // shell's live preview source, so what applies is exactly what previews.
    uint8_t FormMachineOmni::buildEuclidPattern(uint8_t pulses, uint8_t rot, bool wholeTrack, bool *pattern)
    {
        uint8_t idx[64];
        uint8_t len = toolScopeIndices(wholeTrack, idx);
        uint8_t genLen = len > euclidean::EuclideanMath::kPatternSize ? euclidean::EuclideanMath::kPatternSize : len;
        bool gen[euclidean::EuclideanMath::kPatternSize];
        euclidean::EuclideanMath::clearPattern(gen);
        if (genLen > 0)
        {
            euclidean::EuclideanMath::generateEuclidPattern(gen, pulses > genLen ? genLen : pulses, genLen);
            uint8_t r = rot % genLen;
            if (r > 0)
                euclidean::EuclideanMath::rotatePattern(gen, genLen, r);
        }
        for (uint8_t i = 0; i < len; i++)
            pattern[i] = genLen > 0 ? gen[i % genLen] : false;
        return len;
    }

    // Build the grids rhythm (+ level-derived velocities) for the current scope.
    uint8_t FormMachineOmni::buildGridsPattern(uint8_t inst, uint8_t x, uint8_t y, uint8_t density, bool wholeTrack, bool *pattern, uint8_t *vels)
    {
        uint8_t idx[64];
        uint8_t len = toolScopeIndices(wholeTrack, idx);
        grids::GridsChannel channel;
        const uint8_t threshold = (uint8_t)~density;
        for (uint8_t i = 0; i < len; i++)
        {
            channel.setStep((uint8_t)((i * 2) % 32)); // 32-step drum map at 8th resolution
            uint8_t level = channel.level(inst, x, y);
            pattern[i] = level > threshold;
            // Velocity from how far above the threshold the level sits (like GridsWrapper).
            vels[i] = pattern[i] ? (uint8_t)constrain(32 + (int)(95.f * float(level - threshold) / float(256 - threshold)), 32, 127) : 100;
        }
        return len;
    }

    void FormMachineOmni::toolEuclid(uint8_t pulses, uint8_t rot, bool wholeTrack)
    {
        bool pattern[64];
        buildEuclidPattern(pulses, rot, wholeTrack, pattern);
        applyRhythmScope(pattern, nullptr, wholeTrack);
    }

    void FormMachineOmni::toolGrids(uint8_t inst, uint8_t x, uint8_t y, uint8_t density, bool wholeTrack)
    {
        bool pattern[64];
        uint8_t vels[64];
        buildGridsPattern(inst, x, y, density, wholeTrack, pattern, vels);
        applyRhythmScope(pattern, vels, wholeTrack);
    }

    uint8_t FormMachineOmni::potLockCC(uint8_t slot)
    {
        return (slot < NUM_CC_POTS) ? pots[seq_.potBank][slot] : 0;
    }

    // Send one pot-bank slot's CC live on the track's channel (the Mix CC page's
    // encoder edits go through here — same path a physical knob turn takes).
    void FormMachineOmni::sendPotCC(uint8_t slot, uint8_t val)
    {
        if (slot >= NUM_CC_POTS || !seq_.sendMidi)
            return;
        MM::sendControlChange(pots[seq_.potBank][slot], val, seq_.channel + 1);
    }

    void FormMachineOmni::recordNoteToStep(uint8_t absStep, int8_t note)
    {
        if (note < 0 || note > 127 || absStep >= 64)
            return;
        auto track = getTrack();
        Step *s = &track->steps[absStep];
        bool wasEmpty = !s->hasNotes();
        for (uint8_t i = 0; i < 6; i++)
            if (s->notes[i] == note)
                return; // already there
        for (uint8_t i = 0; i < 6; i++)
            if (s->notes[i] < 0)
            {
                s->notes[i] = note;
                if (wasEmpty)
                    s->vel = (uint8_t)track->paramDefaults[0]; // recorded velocity = track default
                return;
            }
    }

    uint8_t FormMachineOmni::recordResolveStep(uint8_t quantizePct, int8_t &nudgeOut)
    {
        nudgeOut = 0;
        auto track = getTrack();
        uint16_t total = track->totalLen();
        if (total == 0)
            return 255;
        // How far into the current step we are (0 at the step start, ->1 approaching the next).
        float frac = (ticksPerStep_ > 0) ? (1.0f - (float)ticksTilNextTriggerRate_ / (float)ticksPerStep_) : 0.0f;
        uint16_t pos = playingStep_;
        float nudgeFrac; // signed offset from the chosen step, in [-0.5, +0.5] of a step
        if (frac < 0.5f)
            nudgeFrac = frac; // played late on the current step -> positive (delay) nudge
        else
        {
            pos = (uint16_t)((playingStep_ + 1) % total); // round up to the next step
            nudgeFrac = -(1.0f - frac);                    // played early -> negative nudge
        }
        // Nudge scaled by the quantize strength: 100% => 0 (hard snap), 0% => full played offset.
        if (quantizePct > 100)
            quantizePct = 100;
        int16_t nudge = (int16_t)lroundf(nudgeFrac * 60.0f * (float)(100 - quantizePct) / 100.0f);
        nudgeOut = (int8_t)constrain((int)nudge, -60, 60);
        return track->positionToStep(pos);
    }

    // Map a held-note duration (in steps) to the nearest LEN value (getStepLenMult is the inverse).
    static uint8_t lenFromSteps(float steps)
    {
        if (steps <= 0.1875f) return 0; // 0.125
        if (steps <= 0.375f) return 1;  // 0.25
        if (steps <= 0.625f) return 2;  // 0.5
        if (steps <= 0.875f) return 3;  // 0.75
        if (steps <= 16.5f)             // len 4..19 => 1..16 steps (len-3)
        {
            int v = (int)lroundf(steps) + 3;
            return (uint8_t)(v < 4 ? 4 : v);
        }
        // Long notes snap to the palette's bar values: 16 / 32 / 48 / 64 steps.
        if (steps <= 24.0f) return 19; // 16 steps (1 bar)
        if (steps <= 40.0f) return 20; // 32 (2 bar)
        if (steps <= 56.0f) return 21; // 48 (3 bar)
        return 22;                     // 64 (4 bar)
    }

    void FormMachineOmni::recordNoteLen(uint8_t absStep, float durationSteps)
    {
        if (absStep >= 64 || durationSteps <= 0.0f)
            return;
        getTrack()->steps[absStep].len = lenFromSteps(durationSteps);
    }

    uint8_t FormMachineOmni::key16toStep(uint8_t key16)
    {
        uint8_t zoomMult = kZoomMults[zoomLevel_];

        uint8_t view = 16 * zoomMult;

        uint8_t page = min(activePage_, kPageMax[zoomLevel_]);

        uint8_t stepIndex = (view * page) + (key16 * zoomMult);

        return stepIndex;
    }

    void FormMachineOmni::copyStep(uint8_t keyIndex)
    {
        if(keyIndex < 0 || keyIndex >= 16) return;

        uint8_t stepIndex = key16toStep(keyIndex);

        auto track = getTrack();
        bufferedStep_.CopyFrom(&track->steps[stepIndex]);
    }
    void FormMachineOmni::cutStep(uint8_t keyIndex)
    {
        if(keyIndex < 0 || keyIndex >= 16) return;

        uint8_t stepIndex = key16toStep(keyIndex);

        copyStep(keyIndex);
        getTrack()->steps[stepIndex].setToInit();
    }
    void FormMachineOmni::pasteStep(uint8_t keyIndex)
    {
        if(keyIndex < 0 || keyIndex >= 16) return;

        uint8_t stepIndex = key16toStep(keyIndex);

        getTrack()->steps[stepIndex].CopyFrom(&bufferedStep_);
    }

    // ---- v2 Step value palettes ----

    // v2 nudge palette: 9 keys spanning -60..+60 with a true zero on key 5.
    static const int8_t kNudgePalette[9] = {-60, -45, -30, -15, 0, 15, 30, 45, 60};

    // Which P-Lock bit a Step-view edit mode owns (-1 = none, e.g. Note).
    // Modes 8/9 are the param-page pseudo-modes for Nudge / Accum (no StepMode of
    // their own — they only exist as palettes on the Seq param pages).
    static int8_t stepModeToLock(uint8_t mode)
    {
        switch (mode)
        {
        case 1: return SLOCK_VEL;
        case 2: return SLOCK_LEN;
        case 3: return SLOCK_REPEAT;
        case 4: return SLOCK_PROB;
        case 5: return SLOCK_COND;
        case 6: return SLOCK_FUNC;
        case 7: return SLOCK_MFX;
        case 8: return SLOCK_NUDGE;
        case 9: return SLOCK_ACCUM;
        default: return -1;
        }
    }

    uint8_t FormMachineOmni::stepPaletteCount(uint8_t mode)
    {
        switch (mode)
        {
        case 1: return 10;             // velocity
        case 2: return 10;             // length
        case 3: return 4;              // repeat
        case 4: return 10;             // chance
        case 5: return 10;             // math (1 Fill, 2 !Fill, 3-6 ratio A, 7-10 ratio B)
        case 6: return STEPFUNC_COUNT; // function
        case 7: return 6;              // midi fx (Off + FX 1-5)
        case 8: return 9;              // nudge (-60..+60, zero on key 5)
        case 9: return 5;              // accum (0-4)
        default: return 0;             // note = handled elsewhere
        }
    }

    void FormMachineOmni::setStepPalette(uint8_t key16, uint8_t mode, uint8_t p)
    {
        if (key16 >= 16) return;
        Step *s = &getTrack()->steps[key16toStep(key16)];
        s->trig = 1; // locking a value turns the step on (ghost step if it has no notes)
        int8_t lock = stepModeToLock(mode);
        if (lock >= 0) s->setLock(lock); // setting a value P-Locks that param
        switch (mode)
        {
        case 1: s->vel = constrain(((int)(p + 1) * 127) / 10, 1, 127); break;
        case 2: if (p < 10) s->len = kLenPalette[p]; break;
        case 3: if (p < 4) s->repeat = p; break;
        case 4: s->prob = constrain((int)(p + 1) * 10, 1, 100); break;
        case 5:
        {
            if (p == 0) { s->condition = 1; break; } // Fill
            if (p == 1) { s->condition = 2; break; } // !Fill
            uint8_t a = 1, b = 1;
            if (s->condition >= 9) { a = kTrigConditionsAB[s->condition - 9][0]; b = kTrigConditionsAB[s->condition - 9][1]; }
            if (p >= 2 && p <= 5) { a = (p - 2) + 1; if (b < a) b = a; }
            else if (p >= 6 && p <= 9) { b = (p - 6) + 1; if (a > b) a = b; }
            for (uint8_t i = 0; i < 35; i++)
                if (kTrigConditionsAB[i][0] == a && kTrigConditionsAB[i][1] == b) { s->condition = 9 + i; break; }
            break;
        }
        case 6: if (p < STEPFUNC_COUNT) s->func = p; break;
        case 7: if (p < 6) s->mfxIndex = kMfxPalette[p]; break;
        case 8: if (p < 9) s->nudge = kNudgePalette[p]; break;
        case 9: if (p < 5) s->accumTPat = p; break;
        }
    }

    // Set a param DEFAULT from a value-palette key (same mapping as setStepPalette),
    // pushing it to every step without its own lock (via editParamDefault's logic).
    void FormMachineOmni::setParamDefaultPalette(uint8_t mode, uint8_t p)
    {
        Track *t = getTrack();
        int pid = -1, v = 0;
        switch (mode)
        {
        case 1: pid = 0; v = constrain(((int)(p + 1) * 127) / 10, 1, 127); break;
        case 2: if (p < 10) { pid = 2; v = kLenPalette[p]; } break;
        case 4: pid = 4; v = constrain((int)(p + 1) * 10, 1, 100); break;
        case 5:
        {
            pid = 5;
            uint8_t cur = (uint8_t)t->paramDefaults[5];
            if (p == 0) { v = 1; break; }      // Fill
            if (p == 1) { v = 2; break; }      // !Fill
            uint8_t a = 1, b = 1;
            if (cur >= 9) { a = kTrigConditionsAB[cur - 9][0]; b = kTrigConditionsAB[cur - 9][1]; }
            if (p >= 2 && p <= 5) { a = (p - 2) + 1; if (b < a) b = a; }
            else if (p >= 6 && p <= 9) { b = (p - 6) + 1; if (a > b) a = b; }
            v = cur;
            for (uint8_t i = 0; i < 35; i++)
                if (kTrigConditionsAB[i][0] == a && kTrigConditionsAB[i][1] == b) { v = 9 + i; break; }
            break;
        }
        case 6: if (p < STEPFUNC_COUNT) { pid = 6; v = p; } break;
        case 7: if (p < 6) { pid = 3; v = kMfxPalette[p]; } break;
        case 8: if (p < 9) { pid = 1; v = kNudgePalette[p]; } break;
        case 9: if (p < 5) { pid = 7; v = p; } break;
        }
        if (pid < 0)
            return;
        editParamDefault((uint8_t)pid, v - (int)t->paramDefaults[pid]);
    }

    int16_t FormMachineOmni::stepPaletteSelected(uint8_t key16, uint8_t mode)
    {
        if (key16 >= 16) return -1;
        Step *s = &getTrack()->steps[key16toStep(key16)];
        switch (mode)
        {
        case 1: return constrain(((int)s->vel * 10 + 63) / 127 - 1, 0, 9); // rounded inverse of the vel palette
        case 2: for (uint8_t i = 0; i < 10; i++) if (kLenPalette[i] == s->len) return i; return -1;
        case 3: return s->repeat;
        case 4: return constrain(((int)s->prob / 10) - 1, 0, 9);
        case 6: return (s->func < STEPFUNC_COUNT) ? s->func : -1;
        case 7: for (uint8_t i = 0; i < 6; i++) if (kMfxPalette[i] == s->mfxIndex) return i; return -1;
        case 8: for (uint8_t i = 0; i < 9; i++) if (kNudgePalette[i] == s->nudge) return i; return -1;
        case 9: return s->accumTPat;
        default: return -1; // math via stepMathInfo
        }
    }

    // The DEFAULT's lit palette key for a mode (-1 = none) — mirrors stepPaletteSelected
    // but reads the track's paramDefaults (for the param pages' no-hold palette LEDs).
    int16_t FormMachineOmni::defaultPaletteSelected(uint8_t mode)
    {
        Track *t = getTrack();
        switch (mode)
        {
        case 1: return constrain(((int)t->paramDefaults[0] * 10 + 63) / 127 - 1, 0, 9);
        case 2: for (uint8_t i = 0; i < 10; i++) if (kLenPalette[i] == (uint8_t)t->paramDefaults[2]) return i; return -1;
        case 4: return constrain(((int)t->paramDefaults[4] / 10) - 1, 0, 9);
        case 6: return (t->paramDefaults[6] < STEPFUNC_COUNT) ? t->paramDefaults[6] : -1;
        case 7: for (uint8_t i = 0; i < 6; i++) if (kMfxPalette[i] == (uint8_t)t->paramDefaults[3]) return i; return -1;
        case 8: for (uint8_t i = 0; i < 9; i++) if (kNudgePalette[i] == t->paramDefaults[1]) return i; return -1;
        case 9: return t->paramDefaults[7];
        default: return -1;
        }
    }

    void FormMachineOmni::setRateShortcut(uint8_t topKeyIndex)
    {
        if (topKeyIndex >= 8) return;
        seq_.rate = kRateShortcuts[topKeyIndex];
        omxDisp.displayMessage("RATE 1:" + String(kSeqRates[seq_.rate]));
        onRateChanged();
    }

    int8_t FormMachineOmni::rateShortcutSel()
    {
        for (uint8_t i = 0; i < 8; i++)
            if (kRateShortcuts[i] == seq_.rate)
                return (int8_t)i;
        return -1;
    }

    void FormMachineOmni::editGate(int amt)
    {
        seq_.gate = constrain((int)seq_.gate + amt, 0, 100);
    }

    void FormMachineOmni::editRate(int amt)
    {
        seq_.rate = constrain((int)seq_.rate + amt, 0, kNumSeqRates - 1);
        onRateChanged();
    }

    String FormMachineOmni::gateBox()
    {
        return String((uint16_t)(getGateMult(seq_.gate) * 100));
    }

    void FormMachineOmni::seqMenuEnter()
    {
        trackParams_.setSelPageAndParam(OMNIPAGE_STEPNOTES, 0);
    }
    bool FormMachineOmni::seqMenuAtStart()
    {
        return trackParams_.getSelPage() == OMNIPAGE_STEPNOTES && trackParams_.getSelParam() == 0;
    }
    bool FormMachineOmni::seqMenuAtEnd() // the SEQ menu is the notes page only
    {
        return trackParams_.getSelPage() == OMNIPAGE_STEPNOTES && trackParams_.getSelParam() == 6;
    }
    bool FormMachineOmni::transMenuAtEnd()
    {
        return tPatParams_.getSelParam() >= 16;
    }
    void FormMachineOmni::transParamsDraw(uint8_t sel)
    {
        drawPage(OMNIPAGE_SEQTPOSE, sel);
    }
    void FormMachineOmni::transParamsEdit(uint8_t sel, int dir)
    {
        editPage(OMNIPAGE_SEQTPOSE, sel, (int8_t)dir, (int8_t)dir);
    }

    void FormMachineOmni::mixMenuEnter() // the MIX menu = the track/global pages
    {
        trackParams_.setSelPageAndParam(OMNIPAGE_TRACK, 0);
    }
    bool FormMachineOmni::mixMenuAtStart()
    {
        return trackParams_.getSelPage() == OMNIPAGE_TRACK && trackParams_.getSelParam() == 0;
    }
    void FormMachineOmni::setSelStepByKey(uint8_t key16)
    {
        if (key16 < 16)
            selStep_ = key16toStep(key16);
    }

    // ---- v2 Step menu params (P-Lockable) ----
    static const uint8_t kStepParamLockBits[8] = {
        SLOCK_VEL, SLOCK_NUDGE, SLOCK_LEN, SLOCK_MFX, SLOCK_PROB, SLOCK_COND, SLOCK_FUNC, SLOCK_ACCUM};
    static const char *kStepParamLabels[8] = {"VEL", "NUDG", "LEN", "MFX", "PROB", "COND", "FUNC", "ACUM"};

    const char *FormMachineOmni::stepParamLabel(uint8_t pid)
    {
        return pid < 8 ? kStepParamLabels[pid] : "";
    }

    String FormMachineOmni::stepParamValueString2(uint8_t key16, uint8_t pid)
    {
        if (key16 >= 16) return "";
        Step *s = &getTrack()->steps[key16toStep(key16)];
        switch (pid)
        {
        case 0: return String(s->vel);
        case 1: return String(s->nudge);
        case 2: return getStepLenString(s->len);
        case 3: return s->mfxIndex == 0 ? String("OFF") : (s->mfxIndex == 1 ? String("TRK") : ("FX" + String(s->mfxIndex - 1)));
        case 4: return String(s->prob);
        case 5: return s->condition == 2 ? String("!FILL") : (s->condition == 1 ? String("FILL") : String(getCondChar(s->condition)));
        case 6:
            if (s->func >= STEPFUNC_COUNT) return "J" + String(s->func - STEPFUNC_COUNT + 1);
            if (s->func == STEPFUNC_RANDJUMP) return String("J?");
            return String(kStepFuncs[s->func]);
        case 7: return String(s->accumTPat);
        default: return "";
        }
    }

    // Compact value for the param cell (<= ~4 chars). Floats < 1 drop the leading zero (".75").
    String FormMachineOmni::formatParamBox(uint8_t pid, int v)
    {
        switch (pid)
        {
        case 0: return String(v);
        case 1: return String(v);
        case 2:
        {
            float m = getStepLenMult(v);
            if (m < 1.0f) { String s = String(m, 2); return s.substring(1); } // "0.75" -> ".75"
            if (m == (float)(int)m) return String((int)m);                    // whole: "1","16"
            return String(m, 1);                                             // "1.2"
        }
        case 3: return v == 0 ? String("--") : (v == 1 ? String("T") : String(v - 1)); // -- / T / 1..5
        case 4: return String(v);
        case 5:
        {
            static const char *kCondBox[9] = {"--", "F", "!F", "P", "!P", "N", "!N", "1", "!1"};
            if (v < 9) return String(kCondBox[v]);
            return String(getCondChar(v)); // ratios "A:B"
        }
        case 6:
            if (v >= STEPFUNC_COUNT) return "J" + String(v - STEPFUNC_COUNT + 1);
            if (v == STEPFUNC_RESTART) return String("RT"); // "RSET" -> "RT"
            return String(kStepFuncs[v]);
        case 7: return String(v);
        default: return "";
        }
    }

    static int stepParamRawValue(FormOmni::Step *s, uint8_t pid)
    {
        switch (pid)
        {
        case 0: return s->vel;
        case 1: return s->nudge;
        case 2: return s->len;
        case 3: return s->mfxIndex;
        case 4: return s->prob;
        case 5: return s->condition;
        case 6: return s->func;
        case 7: return s->accumTPat;
        default: return 0;
        }
    }

    int FormMachineOmni::stepParamValue(uint8_t key16, uint8_t pid)
    {
        if (key16 >= 16)
            return 0;
        Step *st = &getTrack()->steps[key16toStep(key16)];
        return stepParamRawValue(st, pid);
    }

    String FormMachineOmni::stepParamBox(uint8_t key16, uint8_t pid)
    {
        if (key16 >= 16) return "";
        Step *s = &getTrack()->steps[key16toStep(key16)];
        return formatParamBox(pid, stepParamRawValue(s, pid));
    }

    String FormMachineOmni::paramDefaultBox(uint8_t pid)
    {
        if (pid >= 8) return "";
        return formatParamBox(pid, getTrack()->paramDefaults[pid]);
    }

    // Edit a track default and push it to every step that hasn't locked that param.
    void FormMachineOmni::editParamDefault(uint8_t pid, int delta)
    {
        if (pid >= 8) return;
        Track *t = getTrack();
        static const int lo[8] = {0, -60, 0, 0, 0, 0, 0, 0};
        static const int hi[8] = {127, 60, 22, NUM_MIDIFX_GROUPS + 2 - 1, 100, 9 + 35 - 1, STEPFUNC_COUNT + 64 - 1, 4};
        int v = constrain((int)t->paramDefaults[pid] + delta, lo[pid], hi[pid]);
        t->paramDefaults[pid] = (int8_t)v;
        uint8_t bit = kStepParamLockBits[pid];
        for (uint8_t i = 0; i < 64; i++)
        {
            Step *s = &t->steps[i];
            if (s->isLocked(bit)) continue;
            switch (pid)
            {
            case 0: s->vel = v; break;
            case 1: s->nudge = v; break;
            case 2: s->len = v; break;
            case 3: s->mfxIndex = v; break;
            case 4: s->prob = v; break;
            case 5: s->condition = v; break;
            case 6: s->func = v; break;
            case 7: s->accumTPat = v; break;
            }
        }
    }

    bool FormMachineOmni::stepParamWide(uint8_t pid)
    {
        return pid == 5 || pid == 6; // Cond / Func can read long -> show the value popup while editing
    }

    void FormMachineOmni::editStepParam(uint8_t key16, uint8_t pid, int delta)
    {
        if (key16 >= 16 || pid >= 8) return;
        Step *s = &getTrack()->steps[key16toStep(key16)];
        switch (pid)
        {
        case 0: s->vel = constrain(s->vel + delta, 0, 127); break;
        case 1: s->nudge = constrain(s->nudge + delta, -60, 60); break;
        case 2: s->len = constrain(s->len + delta, 0, 22); break;
        case 3: s->mfxIndex = constrain(s->mfxIndex + delta, 0, NUM_MIDIFX_GROUPS + 2 - 1); break;
        case 4: s->prob = constrain(s->prob + delta, 0, 100); break;
        case 5: s->condition = constrain(s->condition + delta, 0, 9 + 35 - 1); break;
        case 6: s->func = constrain(s->func + delta, 0, STEPFUNC_COUNT + 64 - 1); break;
        case 7: s->accumTPat = constrain(s->accumTPat + delta, 0, 4); break;
        }
        s->trig = 1;
        s->setLock(kStepParamLockBits[pid]);
    }

    bool FormMachineOmni::stepParamLocked(uint8_t key16, uint8_t pid)
    {
        if (key16 >= 16 || pid >= 8) return false;
        return getTrack()->steps[key16toStep(key16)].isLocked(kStepParamLockBits[pid]);
    }

    void FormMachineOmni::clearStepParamLock(uint8_t key16, uint8_t pid)
    {
        if (key16 >= 16 || pid >= 8) return;
        Step *s = &getTrack()->steps[key16toStep(key16)];
        s->clearLock(kStepParamLockBits[pid]);
        int v = getTrack()->paramDefaults[pid]; // revert to the track default
        switch (pid)
        {
        case 0: s->vel = v; break;
        case 1: s->nudge = v; break;
        case 2: s->len = v; break;
        case 3: s->mfxIndex = v; break;
        case 4: s->prob = v; break;
        case 5: s->condition = v; break;
        case 6: s->func = v; break;
        case 7: s->accumTPat = v; break;
        }
    }

    uint8_t FormMachineOmni::stepMathInfo(uint8_t key16, uint8_t &a, uint8_t &b)
    {
        a = b = 0;
        if (key16 >= 16) return 0;
        Step *s = &getTrack()->steps[key16toStep(key16)];
        if (s->condition == 1) return 1; // Fill
        if (s->condition == 2) return 2; // !Fill
        if (s->condition >= 9)
        {
            a = kTrigConditionsAB[s->condition - 9][0];
            b = kTrigConditionsAB[s->condition - 9][1];
            return 3;
        }
        return 0;
    }

    String FormMachineOmni::stepValueString(uint8_t key16, uint8_t mode)
    {
        if (key16 >= 16) return "";
        Step *s = &getTrack()->steps[key16toStep(key16)];
        switch (mode)
        {
        case 1: return "VEL " + String(s->vel);
        case 2: return getStepLenString(s->len);
        case 3: return "x" + String(s->repeat + 1);
        case 4: return String(s->prob) + "%";
        case 5: return s->condition == 2 ? String("!FILL") : (s->condition == 1 ? String("FILL") : String(getCondChar(s->condition)));
        case 6:
            if (s->func >= STEPFUNC_COUNT) return "JMP > " + String(s->func - STEPFUNC_COUNT + 1);   // jump to step (1-based)
            if (s->func == STEPFUNC_RANDJUMP) return String("JMP > ?");                               // random jump
            return String(kStepFuncs[s->func]);
        case 7: return s->mfxIndex == 0 ? String("OFF") : (s->mfxIndex == 1 ? String("TRK") : ("FX" + String(s->mfxIndex - 1)));
        default: return "";
        }
    }

    // Step-view edit mode -> menu param id (-1 = none, e.g. Note / Repeat which has no track default).
    static int8_t stepModeToPid(uint8_t mode)
    {
        switch (mode)
        {
        case 1: return 0; // Vel
        case 2: return 2; // Len
        case 4: return 4; // Chance -> prob
        case 5: return 5; // Math -> cond
        case 6: return 6; // Func
        case 7: return 3; // MFX
        default: return -1;
        }
    }

    void FormMachineOmni::resetStepValue(uint8_t key16, uint8_t mode)
    {
        if (key16 >= 16) return;
        // Params with a track default: clear the lock and revert to that default.
        int8_t pid = stepModeToPid(mode);
        if (pid >= 0)
        {
            clearStepParamLock(key16, pid);
            return;
        }
        // Repeat has no track default: clear its lock and reset to the built-in default.
        if (mode == 3)
        {
            Step *s = &getTrack()->steps[key16toStep(key16)];
            s->clearLock(SLOCK_REPEAT);
            s->repeat = 0;
        }
    }

    void FormMachineOmni::previewNote(int8_t note, bool on)
    {
        if (context_ == nullptr || note < 0 || note > 127) return;
        MidiNoteGroup ng;
        ng.channel = seq_.channel + 1;
        ng.noteNumber = note;
        ng.prevNoteNumber = note;
        ng.velocity = on ? (uint8_t)getTrack()->paramDefaults[0] : 0; // track default velocity
        ng.sendMidi = (bool)seq_.sendMidi;
        ng.sendCV = (bool)seq_.sendCV;
        ng.unknownLength = true; // send/stop directly — do NOT schedule a pending note-off
        if (on)
        {
            ng.noteonMicros = seqConfig.lastClockMicros;
            seqNoteOn(ng, 255);
        }
        else
        {
            seqNoteOff(ng, 255);
        }
    }

    MidiNoteGroup FormMachineOmni::step2NoteGroup(uint8_t noteIndex, Step *step)
    {
        MidiNoteGroup noteGroup;

        noteGroup.channel = seq_.channel + 1;

        if(noteIndex >= 6)
        {
            noteGroup.noteNumber = 255;
        }
        else
        {
            noteGroup.noteNumber = step->notes[noteIndex];
        }
        noteGroup.velocity = step->vel;
        noteGroup.prevNoteNumber = noteGroup.noteNumber;

        // noteGroup.stepLength = getStepLenMult(step->len) * stepLengthMult_ * getGateMult(seq_.gate);

        float lenMult = getStepLenMult(step->len);
        // if(lenMult <= 1.0f)
        // {
        //     lenMult *= getGateMult(seq_.gate);
        // }
        noteGroup.stepLength = lenMult * getGateMult(seq_.gate);

        noteGroup.sendMidi = (bool)seq_.sendMidi;
        noteGroup.sendCV = (bool)seq_.sendCV;
        noteGroup.unknownLength = true;

        return noteGroup;
    }

    bool FormMachineOmni::getMute()
    {
        return seq_.mute == 1;
    }
    bool FormMachineOmni::getSolo()
    {
        return seq_.solo == 1;
    }
    void FormMachineOmni::setMute(bool isMuted)
    {
        seq_.mute = isMuted ? 1 : 0;
    }
    void FormMachineOmni::setSolo(bool isSoloed)
    {
        seq_.solo = isSoloed ? 1 : 0;
    }

    bool FormMachineOmni::didTriggerThisStep() 
    {
        return omxFormGlobal.isPlaying && didNotesPlayThisStep_;
    }

    bool FormMachineOmni::evaluateTrig(uint8_t stepIndex, Step *step)
    {
        if(step->mute == 1) return false;
        if(step->prob == 100 && step->condition == 0)
        {
            // An unconditional fire counts as "previous trig was true" for PRE/!PRE, the
            // same as a passing probability roll below — otherwise PRE semantics differ
            // between a prob-100 step and a prob-99 step that happened to pass.
            prevCondWasTrue_ = true;
            return true;
        }

        // random(100) is 0-99, so >= makes prob N fire exactly N% of the time
        // (with > , prob 50 fired 51% and prob 99 fired always).
        if (step->prob != 100 && (step->prob == 0 || random(100) >= step->prob))
        {
            prevCondWasTrue_ = false;
			return false;
        }

        // Fill
        if(step->condition == 1 || step->condition == 2)
        {
            if((step->condition == 1 && !fillActive_) || (step->condition == 2 && fillActive_))
            {
                prevCondWasTrue_ = false;
                return false;
            }
        }
        // Pre
        else if(step->condition == 3 || step->condition == 4)
        {
            if((step->condition == 3 && !prevCondWasTrue_) || (step->condition == 4 && prevCondWasTrue_))
            {
                prevCondWasTrue_ = false;
                return false;
            }
        }
        // NEI
        else if(step->condition == 5 || step->condition == 6)
        {
            if((step->condition == 5 && !neighborPrevTrigWasTrue_) || (step->condition == 6 && neighborPrevTrigWasTrue_))
            {
                prevCondWasTrue_ = false;
                return false;
            }
        }
        // 1st
        else if(step->condition == 7 || step->condition == 8)
        {
            if((step->condition == 7 && !firstLoop_) || (step->condition == 8 && firstLoop_))
            {
                prevCondWasTrue_ = false;
                return false;
            }
        }
        else if(step->condition >= 9)
        {
            uint8_t abIndex = step->condition - 9;

            uint8_t evalA = kTrigConditionsAB[abIndex][0];
            uint8_t evalB = kTrigConditionsAB[abIndex][1];

            uint8_t loopPos = loopCount_ % evalB;

            if (loopPos + 1 != evalA)
            {
                prevCondWasTrue_ = false;
                return false;
            }
        }

        // 5
        // 7, 14, 21, 28, 35 42 49 56 63 70   112 / 8 = 14  280  420   840

        prevCondWasTrue_ = true;
        return true;
    }

    int8_t FormMachineOmni::processPlayMode(uint8_t currentStepIndex, uint8_t playmodeIndex)
    {
        switch (playmodeIndex)
        {
        case TRACKMODE_NONE:
        return -1;
        // Steps move forward and reverse on last step
        case TRACKMODE_PONG:
        {
            // Bounce a runtime direction so the track's set playDirection (the FWD/REV-pong
            // intent) is not overwritten by playback.
            auto track = getTrack();

            if(currentStepIndex == 0)
            {
                pongDir_ = 1;
            }
            else if(currentStepIndex == track->len)
            {
                pongDir_ = -1;
            }
        }
        return -1;
        // Each step is randomly selected        
        case TRACKMODE_RAND:
        {
            auto track = getTrack();
            int8_t jumpstep = random(0, track->getLength());
            return jumpstep;
        }
        break;
        // Steps are randomly selected but won't play the same step twice
        case TRACKMODE_RANDNODUPE:
        {
            auto track = getTrack();

            if (track->len < 2)
            {
                return -1;
            }

            int8_t jumpstep = currentStepIndex;

            while (jumpstep == currentStepIndex)
            {
                jumpstep = random(0, track->getLength());
            }

            return jumpstep;
        }
        break;
        // Steps are randomly shuffled each time the pattern loops
        case TRACKMODE_SHUFFLE:
        // Steps are shuffled once when playback starts.
        // For shuffle modes, steps will be reshuffled if the track length is changed
        case TRACKMODE_SHUFFLE_HOLD:
        {
            auto track = getTrack();

            if(track->getLength() != shuffleVec.size())
            {
                calculateShuffle();
            }

            // // This shouldn't happen
            // if(currentStepIndex >= shuffleVec.size())
            // {
            //     return -1;
            // }

            int8_t jumpStep = shuffleVec[shuffleCounter_];

            // Increment the shuffle counter
            int8_t directionIncrement = track->playDirection == TRACKDIRECTION_FORWARD ? 1 : -1;
            uint8_t trackLen = track->getLength();
            shuffleCounter_ = (shuffleCounter_ + trackLen + directionIncrement) % trackLen;

            return jumpStep;
        }
        break;
        }

        return -1;
    }

    void FormMachineOmni::calculateShuffle()
    {
        shuffleVec.clear();
        tempShuffleVec.clear();

        auto track = getTrack();

        // Serial.println("calculateShuffle");

        for(uint8_t i = 0; i < track->getLength(); i++)
        {
            tempShuffleVec.push_back(i);
        }

        while (tempShuffleVec.size() > 0)
        {
            uint8_t randIndex = random(0, tempShuffleVec.size()); // exclusive max: cover the last index
            shuffleVec.push_back(tempShuffleVec[randIndex]);
            tempShuffleVec.erase(tempShuffleVec.begin() + randIndex);
        }

        tempShuffleVec.clear();

        // // randomly sort
        // std::sort(shuffleVec.begin(), shuffleVec.end(), &FormMachineOmni::shuffleSortFunc);

        // for(uint8_t step : shuffleVec)
        // {
        //     Serial.print(step);
        //     Serial.print(" ");
        // }
        // Serial.print("\n");

        shuffleCounter_ = 0;
    }

    int8_t FormMachineOmni::processStepFunction(uint8_t functionIndex)
    {
        if(functionIndex >= STEPFUNC_COUNT)
        {
            auto track = getTrack();

            // The jump target is an absolute step index; convert it to a playback position.
            uint8_t jumpStep = functionIndex - STEPFUNC_COUNT;
            return (int8_t)track->stepToPosition(jumpStep);
        }

        switch (functionIndex)
        {
            // No Function
        case STEPFUNC_NONE:
            return -1;
        // Restarts to start step
        case STEPFUNC_RESTART:
            return -2;
        // Sets track to play reverse
        case STEPFUNC_REV:
        {
            auto track = getTrack();
            track->playDirection = TRACKDIRECTION_REVERSE;
        }
            return -1;
        // Sets track to play forward
        case STEPFUNC_FWD:
        {
            auto track = getTrack();
            track->playDirection = TRACKDIRECTION_FORWARD;
        }
            return -1;
        // Reverses current direction of track
        case STEPFUNC_PONG:
        {
            auto track = getTrack();
            track->playDirection = track->playDirection == TRACKDIRECTION_FORWARD ? TRACKDIRECTION_REVERSE : TRACKDIRECTION_FORWARD;
        }
            return -1;
        // Randomly jumps to a step
        case STEPFUNC_RANDJUMP:
        {
            auto track = getTrack();
            int8_t jumpstep = random(0, track->getLength());
            return jumpstep;
        }
        // Randomly does a function or NONE
        case STEPFUNC_RAND:
        {
            uint8_t randFunc = random(0, STEPFUNC_RANDJUMP);
            return processStepFunction(randFunc);
        }
        }

        return -1;
    }

    int8_t FormMachineOmni::applyTranspose(int noteNumber, Step *step, StepDynamic *stepDynamic)
    {
        // Apply global transpose
        int intervalMod = seq_.transpose;

        // Apply transpose pattern
        if (seq_.applyTransPat == 1)
        {
            int8_t transp = transpPat_.getCurrentTranspose(&seq_.transposePattern);

            intervalMod = intervalMod + transp;
        }

        // Apply step transpose
        if (step->accumTPat > 0)
        {
            int8_t stepTransp = transpPat_.getTransposeAtStep(stepDynamic->tPatPos, &seq_.transposePattern);

            intervalMod = intervalMod + stepTransp;
        }

        if (seq_.transposeMode == TRANPOSEMODE_SEMITONE || scaleConfig.scalePattern < 0)
        {
            noteNumber = noteNumber + intervalMod;
        }
        else if (seq_.transposeMode == TRANPOSEMODE_INTERVAL)
        {
            noteNumber = omxFormGlobal.musicScale->offsetNoteByIntervalInScale(noteNumber, intervalMod);
        }
        else if (seq_.transposeMode == TRANPOSEMODE_LOCALINTERVAL)
        {
            noteNumber = omxFormGlobal.musicScale->offsetNoteByInterval(noteNumber, intervalMod);
        }

        if(noteNumber < 0 || noteNumber > 127)
        {
            return -1;
        }

        return noteNumber;
    }

    void FormMachineOmni::triggerStep(Step *step, StepDynamic *stepDynamic, bool ratchetHit)
    {
        if(context_ == nullptr || noteOnFuncPtr == nullptr)
            return;

        // handled in evaluateStep()
        // if((bool)step->mute) return;

        // Micros now = micros();

        if (seq_.mute == 0)
        {
            // Per-step CC locks: send this step's pot values on the machine's pot-bank CCs.
            // TODO: "CC Fade" (potMode 1) should interpolate over the step; for now both
            // modes send the value once on trigger like "CC Step".
            if (seq_.sendMidi)
            {
                for (uint8_t p = 0; p < NUM_CC_POTS; p++)
                {
                    int8_t v = step->potVals[p];
                    if (v >= 0)
                        MM::sendControlChange(pots[seq_.potBank][p], v, seq_.channel + 1);
                }
            }

            // Monophonic: play only the last set note of the step.
            int8_t monoNoteIdx = -1;
            if (seq_.monoPhonic)
            {
                for (int8_t i = 0; i < 6; i++)
                    if (step->notes[i] >= 0 && step->notes[i] <= 127)
                        monoNoteIdx = i;
            }

            for (int8_t i = 0; i < 6; i++)
            {
                if (seq_.monoPhonic && i != monoNoteIdx)
                    continue;

                int8_t noteNumber = step->notes[i];

                if (noteNumber >= 0 && noteNumber <= 127)
                {
                    // Serial.println("triggerStep: " + String(noteNumber));

                    noteNumber = applyTranspose(noteNumber, step, stepDynamic);

                    if (noteNumber < 0)
                        continue;

                    auto noteGroup = step2NoteGroup(i, step);
                    noteGroup.noteNumber = noteNumber;
                    noteGroup.prevNoteNumber = noteNumber;

                    bool noteTriggeredOnSameStep = false;

                    // With nudge, two steps could fire at once,
                    // If a step already triggered a note,
                    // don't trigger same note again to avoid
                    // overlapping note ons. Ratchet sub-hits are the exception: they intentionally
                    // re-fire the same note within the step, so skip this de-dup for them.
                    for (auto n : triggeredNotes_)
                    {
                        if (!ratchetHit && n.noteNumber == noteNumber)
                        {
                            noteTriggeredOnSameStep = true;
                            break;
                        }
                    }

                    uint8_t mfxIndex = 255;

                    // Use the track midiFX, default
                    if (step->mfxIndex == 1)
                    {
                        uint8_t trackMFX = getTrack()->midiFx;
                        mfxIndex = trackMFX == 0 ? 255 : trackMFX - 1;
                    }
                    // Use the step's midiFX
                    else if (step->mfxIndex >= 2)
                    {
                        mfxIndex = step->mfxIndex - 2;
                    }
                    // If step's mfxIndex is 0, mfxIndex will be 255 for off

                    if (!noteTriggeredOnSameStep)
                    {
                        // bool foundNoteToRemove = false;
                        auto it = noteOns_.begin();
                        while (it != noteOns_.end())
                        {
                            // remove matching note numbers
                            if (it->noteNumber == noteNumber)
                            {
                                auto noteGroup = it->toMidiNoteGroup();
                                seqNoteOff(noteGroup, it->getMidifFXIndex());
                                // `erase()` invalidates the iterator, use returned iterator
                                it = noteOns_.erase(it);
                                // foundNoteToRemove = true;
                            }
                            else
                            {
                                ++it;
                            }
                        }
                    }

                    if (!noteTriggeredOnSameStep && noteOns_.size() < 16)
                    {
                        didNotesPlayThisStep_ = true;
                        noteGroup.noteonMicros = seqConfig.lastClockMicros;
                        seqNoteOn(noteGroup, mfxIndex);
                        OmniTriggeredNoteTracker triggeredTracker;
                        triggeredTracker.noteNumber = noteGroup.noteNumber;
                        triggeredNotes_.push_back(triggeredTracker);
                        OmniNoteTracker trackedNote;
                        trackedNote.setFromNoteGroup(noteGroup);
                        trackedNote.setMidiFXIndex(mfxIndex);
                        noteOns_.push_back(trackedNote);

                        omxLeds.setDirty();
                    }
                }
            }
        }

        // Increment steps tPat position (once per step, not on each ratchet sub-hit).
        if (!ratchetHit)
            stepDynamic->tPatPos = (stepDynamic->tPatPos + step->accumTPat) % (seq_.transposePattern.len + 1);
    }

    // Preview a step's programmed notes on a manual key press (Mix low-row audition).
    // Plays the raw stored notes (no transpose / MIDI FX) so the note-off on key release
    // reliably matches the note-on. key16 is the physical low-row key 0-15.
    void FormMachineOmni::auditionStep(uint8_t key16, bool on)
    {
        if (context_ == nullptr || noteOnFuncPtr == nullptr || key16 >= 16)
            return;

        // Always release anything currently sounding on this key first.
        for (int8_t i = 0; i < 6; i++)
        {
            int8_t n = auditionNotes_[key16][i];
            if (n < 0)
                continue;
            MidiNoteGroup off;
            off.channel = seq_.channel + 1;
            off.noteNumber = n;
            off.prevNoteNumber = n;
            off.velocity = 0;
            off.sendMidi = (bool)seq_.sendMidi;
            off.sendCV = (bool)seq_.sendCV;
            seqNoteOff(off, 255);
            auditionNotes_[key16][i] = -1;
        }

        if (!on)
            return;

        uint8_t stepIndex = key16toStep(key16);
        Step *step = &getTrack()->steps[stepIndex];

        // Monophonic: only the last set note sounds.
        int8_t monoNoteIdx = -1;
        if (seq_.monoPhonic)
            for (int8_t i = 0; i < 6; i++)
                if (step->notes[i] >= 0 && step->notes[i] <= 127)
                    monoNoteIdx = i;

        for (int8_t i = 0; i < 6; i++)
        {
            if (seq_.monoPhonic && i != monoNoteIdx)
                continue;

            int8_t noteNumber = step->notes[i];
            if (noteNumber < 0 || noteNumber > 127)
                continue;

            auto noteGroup = step2NoteGroup(i, step);
            noteGroup.noteNumber = noteNumber;
            noteGroup.prevNoteNumber = noteNumber;
            noteGroup.noteOff = false;
            noteGroup.noteonMicros = seqConfig.lastClockMicros;
            seqNoteOn(noteGroup, 255);
            auditionNotes_[key16][i] = noteNumber;
        }
    }

    void FormMachineOmni::onEnabled()
    {
    }
    void FormMachineOmni::onDisabled()
    {
    }

    void FormMachineOmni::onEncoderChangedSelectParam(Encoder::Update enc)
    {
        switch (omniUiMode_)
        {
        case OMNIUIMODE_CONFIG:
        case OMNIUIMODE_MIX:
        case OMNIUIMODE_LENGTH:
        {
            {
                trackParams_.changeParam(enc.dir());

                // if (trackParams_.getSelPage() != prevPage)
                // {
                //     switch (trackParams_.getSelPage())
                //     {
                //     case OMNIPAGE_STEP1:
                //         omxDisp.displayMessage("Step 1");
                //         break;
                //     case OMNIPAGE_STEPCONDITION:
                //         omxDisp.displayMessage("Step Cond");
                //         break;
                //     case OMNIPAGE_STEPNOTES:
                //         omxDisp.displayMessage("Step Notes");
                //         break;
                //     case OMNIPAGE_STEPPOTS:
                //         omxDisp.displayMessage("Step Pots");
                //         break;
                //     case OMNIPAGE_GBL1:
                //         omxDisp.displayMessage("Track 1");
                //         break;
                //     case OMNIPAGE_1:
                //         // omxDisp.displayMessage("Step 1");
                //         break;
                //     case OMNIPAGE_2:
                //         // omxDisp.displayMessage("Step 1");
                //         break;
                //     case OMNIPAGE_3:
                //         // omxDisp.displayMessage("Step 1");
                //         break;
                //     case OMNIPAGE_TPAT:
                //         transpPat_.onUIEnabled();
                //         // omxDisp.displayMessage("Step 1");
                //         break;
                //     }
                // }
            }
        }
        break;
        case OMNIUIMODE_TRANSPOSE:
        {
            tPatParams_.changeParam(enc.dir());
        }
        break;
        case OMNIUIMODE_STEP:
        case OMNIUIMODE_NOTEEDIT:
        {
            omniNoteEditor.onEncoderChangedSelectParam(enc, getTrack());
        }
            break;
        }

        omxDisp.setDirty();
    }
    void FormMachineOmni::onEncoderChangedEditParam(Encoder::Update enc)
    {
        int amtSlow = enc.accel(1);
        int amtFast = enc.accel(5);

        auto params = getParams();

        int8_t selPage = params->getSelPage();
        int8_t selParam = params->getSelParam();

        switch (omniUiMode_)
        {
        case OMNIUIMODE_CONFIG:
        case OMNIUIMODE_MIX:
        {
            editPage(selPage, selParam, amtSlow, amtFast);
        }
        break;
        case OMNIUIMODE_LENGTH:
        case OMNIUIMODE_TRANSPOSE:
            selParam = tPatParams_.getSelParam();
            transpPat_.onEncoderChangedEditParam(enc, selParam, &seq_.transposePattern);
            break;
        case OMNIUIMODE_STEP:
        case OMNIUIMODE_NOTEEDIT:
        {
            omniNoteEditor.onEncoderChangedEditParam(enc, getTrack());
        }
        break;
        }

        omxDisp.setDirty();
    }

    void FormMachineOmni::changeUIMode(uint8_t newMode, bool silent)
    {
        if (newMode >= OMNIUIMODE_COUNT)
            return;

        if (newMode != omniUiMode_)
        {
            uint8_t prevMode = omniUiMode_;
            omniUiMode_ = newMode;
            onUIModeChanged(prevMode, newMode);

            if (!silent)
            {
                omxDisp.displayMessage(kUIModeMsg[omniUiMode_]);
            }

            omxLeds.setDirty();
            omxDisp.setDirty();

		    omxFormGlobal.shortcutMode = FORMSHORTCUT_NONE;
			midiSettings.midiAUX = false;
            setDirtyOnceMessageClears_ = true;
        }
    }

    void FormMachineOmni::onUIModeChanged(uint8_t prevMode, uint8_t newMode)
    {
        if(newMode == OMNIUIMODE_TRANSPOSE)
        {
            transpPat_.onUIEnabled();
        }

    }

    void FormMachineOmni::onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta)
    {
        // Serial.println("onPotChanged: " + String(potIndex) + " " + String(prevValue) + " " + String(newValue));

        // v2: K5 no longer selects the UI mode — views live on the AUX layer (AUX+13-18).
        // Knobs are the track's pot bank (§2): all 5 send their mapped CC live (below).

        // The 5 knobs are the track's pot bank — send the mapped CC live on the track's own
        // channel. The CC number comes from pots[bank][slot] (§2); the P-Lock path (hold step
        // + pot, shell-side) locks the same slot per-step (§3). The v1 step-held pot editing
        // that lived here is gone (velocity/length/etc. are key palettes now).
        if (seq_.sendMidi && potIndex < NUM_CC_POTS)
        {
            MM::sendControlChange(pots[seq_.potBank][potIndex], newValue, seq_.channel + 1);
            omxDisp.setDirty(); // the top-row CC meter reflects the value; no popup
        }
    }

    void FormMachineOmni::onClockTick()
    {
        if(setDirtyOnceMessageClears_ && omxDisp.isMessageActive() == false)
        {
            omxDisp.setDirty();
            omxLeds.setDirty();
            setDirtyOnceMessageClears_ = false;
        }

        if(omxFormGlobal.isPlaying == false) return;

        // Send note offs
        if (noteOns_.size() > 0)
        {
            auto it = noteOns_.begin();
            while (it != noteOns_.end())
            {
                Micros noteOffMicros = it->noteonMicros + (stepMicros_ * it->stepLength);
                // remove matching note numbers
                if (seqConfig.lastClockMicros >= noteOffMicros)
                {
                    auto noteGroup = it->toMidiNoteGroup();
                    noteGroup.unknownLength = true;
                    seqNoteOff(noteGroup, it->getMidifFXIndex());
                    it = noteOns_.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        // currentClockTick goes up to 1 bar, 96 * 4 = 384

        // ticksPerStep_
        // 1 = 384, 3 = 192, 4 = 128, 4 = 96, 5 = 76.8, 6 = 64, 8 = 48, 10 = 38.4, 12 = 32, 16 = 24, 20 = 19.2, 24 = 16, 32 = 12, 40 = 9.6, 48 = 8, 64 = 6

        // int16_t maxTick = PPQ * 4; // 384

        // int16_t maxTick = ticksPerStep_ * 16; // 384
        // 

        // if(seqConfig.currentClockTick % 384 == 0)
        // {

        //     omniTick_++;
        // }

        // 1xxxxx1xxxxx1xxxxx1xxxxx1xxxxx1xxxxx1xxxxx

        // 0543210543210
        // 1xxxxx1xxxxx1

        if(ticksTilNext16Trigger_ <= 0)
        {
            ticksTilNext16Trigger_ = 24;
            didNotesPlayThisStep_ = false;

            // auto track = getTrack();

            // int8_t directionIncrement = track->playDirection == TRACKDIRECTION_FORWARD ? 1 : -1;
            // playRateCounter_ = (playRateCounter_ + 16 + directionIncrement) % 16;

            // omxLeds.setDirty();
            // omxDisp.setDirty();
        }

        bool onRate = false;

        if(ticksTilNextTriggerRate_ <= 0)
        {
            ticksTilNextTriggerRate_ = ticksPerStep_;
            onRate = true;
            didNotesPlayThisStep_ = false;

            // auto track = getTrack();
            // uint8_t length = track->len + 1;

            // int8_t directionIncrement = track->playDirection == TRACKDIRECTION_FORWARD ? 1 : -1;
            // playRateCounter_ = (playRateCounter_ + length + directionIncrement) % length;

            // omxLeds.setDirty();
            // omxDisp.setDirty();
        }

        // 1xxxxx1xxxxx1xxxxx1xxxxx1xxxxx1xxxxx
        // 0543210543210
        // 1xx1xxxxxxxx1xxxxx1
        // 0210876543210543210
        // 5-3, 3 + 6 - 1

        // uint8_t loop = 0;

        // ticksTilNextTrigger_ = 100;

        if(ticksTilNextTrigger_ <= 0)
        {
            triggeredNotes_.clear();
            didNotesPlayThisStep_ = false;
        }

        bool resetAfterThisTrig = false;

        // can trigger twice in once clock if note is fully nudged
        while(ticksTilNextTrigger_ <= 0)
        {
            auto track = getTrack();
            uint8_t length = track->len + 1;

            // playingStep_ is a position (0..length-1); map it to the absolute step index.
            auto currentStep = &track->steps[track->positionToStep(playingStep_)];

            bool shouldBeOnRate = currentStep->nudge == 0;

            bool isSwingStep = false;
            bool isNextSwingStep = false;

            if(track->swingDivision == 0) // 16th
            {
                // 1SxS2SxS3SxS4SxS
                isSwingStep = grooveCounter_ % 2 == 1; // Swing every other 16th. Basically every even 16th
                isNextSwingStep = (grooveCounter_ + 1) % 2 == 1;
            }
            else if(track->swingDivision == 1) // 8th
            {
                // 1xSx2xSx3xSx4xSx
                isSwingStep = grooveCounter_ % 4 == 2; // Swing ever other 8th note
                isNextSwingStep = (grooveCounter_ + 1) % 4 == 2;
            }

            if(isSwingStep)
            {
                shouldBeOnRate = false;
            }

            float swingPerc = isSwingStep ? track->swing / 100.0f : 0.0f;
            float nextSwingPerc = isNextSwingStep ? track->swing / 100.0f : 0.0f;

            // if(track->tripletMode == 1 && grooveCounter_ % 4 != 0)
            // {
            //     shouldBeOnRate = false;
            // }

            // Step should be on rate, delay until on rate
            if(shouldBeOnRate && !onRate)
            {
                ticksTilNextTrigger_ = ticksTilNextTriggerRate_;
                break;
            }

            bool shouldTriggerStep = evaluateTrig(playingStep_, currentStep);

            int8_t playmodeStep = processPlayMode(playingStep_, track->playMode);

            int8_t functionStep = -1;

            if(shouldTriggerStep)
            {
                functionStep = processStepFunction(currentStep->func);
            }

            int8_t directionIncrement = (track->playMode == TRACKMODE_PONG)
                                            ? pongDir_
                                            : (track->playDirection == TRACKDIRECTION_FORWARD ? 1 : -1);

            uint8_t nextStepIndex;

            // -2 means reset next step
            if(functionStep == -2)
            {
                resetAfterThisTrig = true;
                nextStepIndex = getRestartPos();
            }
            // -1 means normal advance
            else if (functionStep == -1)
            {
                // Step functions supercede playmode functions
                if (playmodeStep < 0)
                {
                    nextStepIndex = (playingStep_ + length + directionIncrement) % length;
                }
                else
                {
                    nextStepIndex = playmodeStep;
                }
            }
            // Step function is changing the next step
            else
            {
                nextStepIndex = functionStep;
            }

            // Skip every 4th step
            if(track->tripletMode == 1)
            {
                if(nextStepIndex % 4 == 3)
                {
                    // increment again
                    nextStepIndex = (nextStepIndex + length + directionIncrement) % length;
                }
            }
            // uint8_t nextStepIndex = (playingStep_ + directionIncrement) % length;
            auto nextStep = &track->steps[track->positionToStep(nextStepIndex)];

            if(shouldTriggerStep)
            {
                auto trackDynamic = getDynamicTrack();
                auto dynamicStep = &trackDynamic->steps[track->positionToStep(playingStep_)];

                triggerStep(currentStep, dynamicStep);

                // Step repeat (ratchet): split the step into (repeat+1) even sub-hits.
                if (currentStep->repeat > 0 && ticksPerStep_ > 1)
                {
                    ratchetStepIdx_ = (int8_t)track->positionToStep(playingStep_);
                    ratchetDivs_ = currentStep->repeat + 1;
                    ratchetHitIndex_ = 1;
                    ratchetTotalTicks_ = ticksPerStep_;
                    ratchetElapsed_ = 0;
                }
                else
                {
                    ratchetDivs_ = 0;
                }

                lastTriggeredStepState_ = true;
            }
            else
            {
                ratchetDivs_ = 0; // a non-triggering step clears any pending ratchet
                lastTriggeredStepState_ = false;
                didNotesPlayThisStep_ = false;
            }
            lastTriggeredStepIndex_ = playingStep_;

            // int currentNudgeTicks = abs(currentStep->nudge)

            // Reverse the nudges when flipping directions
            int8_t nudgeCurrent = currentStep->nudge * directionIncrement;
            int8_t nudgeNext = nextStep->nudge * directionIncrement;

            // float nudgePerc = abs(currentStep->nudge) / 60.0f * (currentStep->nudge < 0 ? -1 : 1);
            int nudgeTicks = constrain((nudgeCurrent / 60.0f) + swingPerc, -1.0f, 1.0f) * ticksPerStep_;
            // float nextNudgePerc = abs(nextStep->nudge) / 60.0f * (nextStep->nudge < 0 ? -1 : 1);
            int nextNudgeTicks = constrain((nudgeNext / 60.0f) + nextSwingPerc, -1.0f, 1.0f) * ticksPerStep_;

            // Apply Swing
            // By using the nudge system
            // nudgeTicks = isSwingStep ? (nudgeTicks + (swingPerc * ticksPerStep_)) : nudgeTicks;
            // nextNudgeTicks = isNextSwingStep ? (nextNudgeTicks + (swingPerc * ticksPerStep_)) : nextNudgeTicks;

            // if(track->tripletMode == 1)
            // {
            //     uint8_t modPos = grooveCounter_ % 4;
            //     uint8_t nextModPos = (grooveCounter_ + 1) % 4;
            //     // nudgeTicks = nudgeTicks + (kTripletNudge[modPos] * ticksPerStep_);
            //     // nextNudgeTicks = nextNudgeTicks + (kTripletNudge[nextModPos] * ticksPerStep_);


            //     nudgeTicks = kTripletNudge[modPos] * ticksPerStep_;
            //     nextNudgeTicks = kTripletNudge[nextModPos] * ticksPerStep_;
            // }

            // nudgeTicks = constrain(nudgeTicks, -ticksPerStep_, ticksPerStep_);
            // nextNudgeTicks = constrain(nextNudgeTicks, -ticksPerStep_, ticksPerStep_);

            if(!onRate && nextNudgeTicks == 0)
            {
                ticksTilNextTrigger_ = ticksTilNextTriggerRate_;
            }
            else
            {
                ticksTilNextTrigger_ = ticksPerStep_ + nextNudgeTicks - nudgeTicks;
            }

            if(nudgeTicks < 0 && !onRate)
            {
                ticksTilNextTrigger_ += ticksPerStep_;
            }


            // if(currentStep->nudge >=0 && nextStep->nudge < 0)
            // {
            //     if (onRate)
            //     {
            //         ticksTilNextTrigger_ = ticksPerStep_ + nextNudgeTicks - nudgeTicks;
            //     }
            //     else
            //     {
            //         ticksTilNextTrigger_ = ticksTilNextTriggerRate_ + nextNudgeTicks;
            //     }
            // }

            // if (onRate)
            // {
            //     ticksTilNextTrigger_ = ticksPerStep_ + nextNudgeTicks - nudgeTicks;
            // }
            // else
            // {
            //     ticksTilNextTrigger_ = ticksTilNextTriggerRate_ + nextNudgeTicks;
            //     // if (currentStep->nudge < 0)
            //     // {
            //     //     ticksTilNextTrigger_ = ticksTilNextTriggerRate_ + ticksPerStep_ + nextNudgeTicks - 1;
            //     // }
            //     // else
            //     // {
            //     //     ticksTilNextTrigger_ = ticksTilNextTriggerRate_ + nextNudgeTicks;
            //     // }
            // }

                // 24 + 16 + 16
            // ticksTilNextTrigger_ = ticksPerStep_ + nextNudgeTicks - nudgeTicks;
            // ticksTilNextTrigger_ = ticksTilNextTriggerRate_ + nextNudgeTicks;
            // ticksTilNextTrigger_ = ticksPerStep_;

            // loop++;

            // Machines get updated left to right
            // This variable is global
            // Thus if this is true, it means the machine to the left
            // Evaluated to true. 
            neighborPrevTrigWasTrue_ = prevCondWasTrue_;

            // always counts forward
            grooveCounter_ = (grooveCounter_ + 1) % 16;

            // Advance transpose pattern
            transpPat_.advance(&seq_.transposePattern);

            loopCounter_ = (loopCounter_ + 1) % track->getLength();
            if(loopCounter_ == 0)
            {
                // Reshuffle every loop
                if(track->playMode == TRACKMODE_SHUFFLE)
                {
                    calculateShuffle();
                }

                // 840 is evenly divisible by 8,7,6,5,4,3,2,1
                loopCount_ = (loopCount_ + 1) % 840;
                firstLoop_ = false;
            }
            playingStep_ = nextStepIndex;

            if (resetAfterThisTrig)
            {
                resetPlayback(false);
            }
        }

        // Step repeat (ratchet): fire the queued sub-hits at round(k*total/divs) ticks into the
        // step. Re-triggering the step note-offs the prior hit and note-ons again.
        if (ratchetDivs_ > 1 && ratchetStepIdx_ >= 0 && ratchetHitIndex_ < ratchetDivs_)
        {
            ratchetElapsed_++;
            int16_t target = (int16_t)(((int32_t)ratchetHitIndex_ * ratchetTotalTicks_) / ratchetDivs_);
            if (ratchetElapsed_ >= target)
            {
                // Resolve the step from its index each hit — edits during playback may have
                // re-initialised the step the original pointers referenced.
                triggerStep(&getTrack()->steps[ratchetStepIdx_],
                            &getDynamicTrack()->steps[ratchetStepIdx_], true);
                ratchetHitIndex_++;
            }
        }

        ticksTilNextTrigger_--;
        ticksTilNext16Trigger_--;
        ticksTilNextTriggerRate_--;
    }

    void FormMachineOmni::onRateChanged()
    {
        int8_t rate = kSeqRates[seq_.rate];

        auto track = getTrack();

        ticksPerStep_ = roundf((PPQ * 4) / (float)rate);

        if(track->tripletMode == 1)
        {
            ticksPerStep_ = ticksPerStep_ * 4 / 3.0f;
        }

        // ticksTilNextTrigger_ = 0; // Should we reset this?

        ticksTilNextTrigger_ = ticksTilNext16Trigger_;
        ticksTilNextTriggerRate_ = ticksTilNext16Trigger_;

        stepLengthMult_ = 16.0f / rate;

        stepMicros_ = clockConfig.step_micros * 16 / rate;

        if(track->tripletMode == 1)
        {
            stepMicros_ = stepMicros_ * 4 / 3;
        }
    }

    float FormMachineOmni::getStepLenMult(uint8_t len)
    {
        float lenMult = 1.0f;

        switch (len)
        {
        case 0:
            lenMult = 0.125f;
            break;
        case 1:
            lenMult = 0.25f;
            break;
        case 2:
            lenMult = 0.5f;
            break;
        case 3:
            lenMult = 0.75f;
            break;
        case 20: // 2 bar
            lenMult = 16 * 2;
            break;
        case 21:
            lenMult = 16 * 3;
            break;
        case 22: // 4 bar
            lenMult = 16 * 4;
            break;
        default:
            lenMult = len - 3;
            break;
        }

        return lenMult;
    }

    String FormMachineOmni::getStepLenString(uint8_t len)
    {
        float stepLenMult = getStepLenMult(len);

        if (stepLenMult < 1.0f)
        {
            return String(stepLenMult, 2);
        }
        else if (stepLenMult > 16)
        {
            uint8_t bar = stepLenMult / 16.0f;
            return String(bar) + "br";
        }
        return String(stepLenMult, 0);
    }

    void FormMachineOmni::onTrackLengthChanged()
    {
        auto track = getTrack();

        if(track->playMode == TRACKMODE_SHUFFLE || track->playMode == TRACKMODE_SHUFFLE_HOLD)
        {
            calculateShuffle();
        }
    }

    float FormMachineOmni::getGateMult(uint8_t gate)
    {
        return max(gate / 100.f * 2, 0.01f);
    }

    uint8_t FormMachineOmni::getRestartPos()
    {
        auto track = getTrack();
        return track->playDirection == TRACKDIRECTION_FORWARD ? 0 : track->getLength() - 1;
    }

    const char *FormMachineOmni::getCondChar(uint8_t condIndex)
    {
        if(condIndex < 9)
        {
            return kConditionModes[condIndex];
        }

        uint8_t abIndex = condIndex - 9;

        tempString = String(kTrigConditionsAB[abIndex][0]) + ":" + String(kTrigConditionsAB[abIndex][1]);
        return tempString.c_str();
    }

    void FormMachineOmni::loopUpdate()
    {
        ensureParamsInit(); // self-heal if pages were wiped by static-init ordering
        transpPat_.loopUpdate();
    }

    bool FormMachineOmni::updateLEDs()
    {
        bool blinkState = omxLeds.getBlinkState();

        switch (omniUiMode_)
        {
        case OMNIUIMODE_CONFIG:
        case OMNIUIMODE_MIX:
        {
            auto track = getTrack();

            for (uint8_t i = 0; i < 16; i++)
            {
                uint8_t stepIndex = key16toStep(i);
                bool isInLen = stepIndex <= track->len;
                auto step = &track->steps[stepIndex];
                int keyColor = (step->mute || !isInLen) ? LEDOFF : (step->hasNotes() ? LTBLUE : DKBLUE);

                strip.setPixelColor(11 + i, keyColor);
            }

            if(omxFormGlobal.isPlaying)
            {
                // auto track = getTrack();
                // uint8_t playingStepKey = playRateCounter_ % (16 * kZoomMults[zoomLevel_]);

                // playingStepKey = map(playingStepKey, 0, 16 * kZoomMults[zoomLevel_], 0, 15);

                // strip.setPixelColor(11 + playingStepKey, WHITE);

                uint8_t playingStepKey = lastTriggeredStepIndex_ % (16 * kZoomMults[zoomLevel_]);

                if (kZoomMults[zoomLevel_] > 1)
                {
                    playingStepKey = map(playingStepKey, 0, 16 * kZoomMults[zoomLevel_], 0, 15);
                }

                strip.setPixelColor(11 + playingStepKey, GREEN); // playhead: steady bright green
            }
        }
        break;
        case OMNIUIMODE_LENGTH:
        break;
        case OMNIUIMODE_TRANSPOSE:
        {
            transpPat_.updateLEDs(&tPatParams_, &seq_.transposePattern);
        }
        break;
        case OMNIUIMODE_STEP:
        case OMNIUIMODE_NOTEEDIT:
            omniNoteEditor.updateLEDs(getTrack());
            break;
        }

        return true;
    }
    void FormMachineOmni::onEncoderButtonDown()
    {
    }
    bool FormMachineOmni::onKeyUpdate(OMXKeypadEvent e)
    {
        uint8_t thisKey = e.key();

        if (e.held())
            return false;

        omxDisp.setDirty();
        omxLeds.setDirty();

        // v2: the shell owns AUX, the step row, and the top row in every view. The only
        // key events that still reach the machine are the Mix-view F3 fall-through
        // (rate + flat track length) and the Transpose view's delegation. The old v1
        // layers that lived here (AUX uiMode switch, step-hold mute/func/jump palette,
        // F1/F2 step copy-cut/paste, double-click into the note editor) are gone.
        switch (omniUiMode_)
        {
        case OMNIUIMODE_CONFIG:
        case OMNIUIMODE_MIX:
        {
            if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3)
            {
                if (e.down() && thisKey >= 3 && thisKey <= 10)
                {
                    seq_.rate = kRateShortcuts[thisKey - 3];
                    omxDisp.displayMessage("RATE 1:" + String(kSeqRates[seq_.rate]));
                    onRateChanged();
                }
                if (e.down() && thisKey >= 11 && thisKey < 27)
                {
                    uint8_t flatLen = (thisKey - 11) + (16 * min(activePage_, kPageMax[zoomLevel_] - 1));
                    setTrackLen(flatLen); // rebuild the page structure from a flat length
                    omxDisp.displayMessage("LENGTH " + String(getTrack()->getLength()));
                }
            }
        }
        break;
        case OMNIUIMODE_TRANSPOSE:
        {
            transpPat_.onKeyUpdate(e, &tPatParams_, &seq_.transposePattern);
        }
        break;
        default:
            break;
        }
        return false;
    }
    bool FormMachineOmni::onKeyHeldUpdate(OMXKeypadEvent e)
    {
        if (omniUiMode_ == OMNIUIMODE_TRANSPOSE)
        {
            transpPat_.onKeyHeldUpdate(e, &seq_.transposePattern);
        }
        return true;
    }

    void FormMachineOmni::editPage(uint8_t page, uint8_t param, int8_t amtSlow, int8_t amtFast)
    {
        switch (page)
        {
        case OMNIPAGE_STEPNOTES:
        {
            auto selStep = getSelStep();
            if (param == 6)
            {
                omxFormGlobal.useNoteNumbers = (bool)constrain(omxFormGlobal.useNoteNumbers + amtSlow, 0, 1);
            }
            else
            {
                selStep->notes[param] = constrain(selStep->notes[param] + amtFast, -1, 127);
            }
        }
        break;
        // Length, MidiFX
        case OMNIPAGE_TRACK:
        {
            auto track = getTrack();

            if (param == 0)
            {
                // Through setTrackLen so enabledPages/pageLen are rebuilt too — writing
                // track->len directly desyncs them and the next syncLen() reverts the edit.
                setTrackLen(constrain(track->len + amtSlow, 0, 63));
            }
            else if (param == 1)
            {
                track->midiFx = constrain(track->midiFx + amtSlow, 0, NUM_MIDIFX_GROUPS + 1 - 1);
            }
        }
        break;
        // Triplet Mode, Direction, Mode
        case OMNIPAGE_TRACKMODES:
        {
            auto track = getTrack();

            if (param == 0)
            {
                track->tripletMode = constrain(track->tripletMode + amtSlow, 0, 1);
                onRateChanged();
            }
            else if (param == 1)
            {
                // Reverse encoder direction since forward makes more sense to the right.
                track->playDirection = constrain(track->playDirection - amtSlow, 0, 1);
            }
            else if (param == 2)
            {
                uint8_t prevMode = track->playMode;
                track->playMode = constrain(track->playMode + amtSlow, 0, TRACKMODE_COUNT - 1);

                if(prevMode != track->playMode && track->playMode >= TRACKMODE_SHUFFLE)
                {
                    calculateShuffle();
                }
                // The cell only fits a 2-char code — the full name shows while turning (§4 rule).
                omxDisp.displayMessage(kTrackModeMsg[track->playMode]);
            }
        }
        break;
        // Transpose, Transpose Mode, Apply Transpose Pat
        case OMNIPAGE_SEQTPOSE:
        {
            if (param == 0)
            {
                seq_.transpose = constrain(seq_.transpose + amtSlow, -64, 64);
            }
            else if (param == 1)
            {
                seq_.transposeMode = constrain(seq_.transposeMode + amtSlow, 0, TRANPOSEMODE_COUNT - 1);
                omxDisp.displayMessage(kTranspModeLongMsg[seq_.transposeMode]);
            }
            else if (param == 2)
            {
                seq_.applyTransPat = constrain(seq_.applyTransPat + amtSlow, 0, 1);
            }
        }
        break;
        // Midi Chan, MonoPhonic, SendMidi, SendCV
        case OMNIPAGE_SEQMIDI:
        {
            if (param == 0)
            {
                seq_.channel = constrain(seq_.channel + amtSlow, 0, 15);
            }
            else if (param == 1)
            {
                seq_.monoPhonic = constrain(seq_.monoPhonic + amtSlow, 0, 1);
            }
            else if (param == 2)
            {
                seq_.sendMidi = constrain(seq_.sendMidi + amtSlow, 0, 1);
            }
            else if (param == 3)
            {
                seq_.sendCV = constrain(seq_.sendCV + amtSlow, 0, 1);
            }
        }
        break;
        // BPM, Rate, Swing, Swing Division
        case OMNIPAGE_TIMINGS:
        {
            auto track = getTrack();

            if (param == 0)
            {
                clockConfig.newtempo = constrain(clockConfig.clockbpm + amtFast, 40, 300);
                if (clockConfig.newtempo != clockConfig.clockbpm)
                {
                    // SET TEMPO HERE
                    clockConfig.clockbpm = clockConfig.newtempo;
                    omxUtil.resetClocks();
                }
            }
            else if (param == 1)
            {
                seq_.rate = constrain(seq_.rate + amtSlow, 0, kNumSeqRates - 1);
                onRateChanged();
                omxDisp.displayMessage("RATE 1:" + String(kSeqRates[seq_.rate])); // §4: full form while turning
            }
            else if (param == 2)
            {
                track->swing = constrain(track->swing + amtFast, -100, 100);
            }
            else if (param == 3)
            {
                track->swingDivision = constrain(track->swingDivision + amtSlow, 0, 1);
                omxDisp.displayMessage(track->swingDivision == 0 ? "SWING 16th" : "SWING 8th");
            }
        }
        break;
        case OMNIPAGE_SCALE:
        {
            if (param == 0)
            {
                int prevRoot = scaleConfig.scaleRoot;
                scaleConfig.scaleRoot = constrain(scaleConfig.scaleRoot + amtSlow, 0, 12 - 1);
                if (prevRoot != scaleConfig.scaleRoot)
                {
                    omxFormGlobal.musicScale->calculateScale(scaleConfig.scaleRoot, scaleConfig.scalePattern);
                }
            }
            if (param == 1)
            {
                int prevPat = scaleConfig.scalePattern;
                scaleConfig.scalePattern = constrain(scaleConfig.scalePattern + amtSlow, -1, omxFormGlobal.musicScale->getNumScales() - 1);

                if (prevPat != scaleConfig.scalePattern)
                {
                    omxDisp.displayMessage(omxFormGlobal.musicScale->getScaleName(scaleConfig.scalePattern));
                    omxFormGlobal.musicScale->calculateScale(scaleConfig.scaleRoot, scaleConfig.scalePattern);

                    if (scaleConfig.scalePattern < 0)
                    {
                        // record locked and grouped states, then set the current lockScale and group16 to off
                        if (prevPat >= 0)
                        {
                            scaleConfig.lockedState = scaleConfig.lockScale;
                            scaleConfig.groupedState = scaleConfig.group16;
                        }
                        scaleConfig.lockScale = false;
                        scaleConfig.group16 = false;
                    }
                    else
                    {
                        // restore locked and grouped states if the scale was previously set to off
                        if (prevPat < 0)
                        {
                            scaleConfig.lockScale = scaleConfig.lockedState;
                            scaleConfig.group16 = scaleConfig.groupedState;
                        }
                    }
                }
            }
            if (param == 2)
            {
                if (scaleConfig.scalePattern >= 0)
                {
                    scaleConfig.lockScale = constrain(scaleConfig.lockScale + amtSlow, 0, 1);
                }
            }
            if (param == 3)
            {
                if (scaleConfig.scalePattern >= 0)
                {
                    scaleConfig.group16 = constrain(scaleConfig.group16 + amtSlow, 0, 1);
                }
            }
        }
        break;
        }
    }

    // Render one 4-cell param page in the shared shell look (dispStepParams). Values obey
    // the FORM_V2_REVIEW.md §4 label rules: numerics up to 3 digits inline, text at most
    // 2 chars; longer names show transiently while the encoder edits (see editPage).
    void FormMachineOmni::dispParamGrid(const char *labels[4], const String vals[4], uint8_t selParam)
    {
        const char *values[4];
        for (uint8_t i = 0; i < 4; i++)
            values[i] = vals[i].c_str();
        const bool locked[4] = {false, false, false, false};
        omxDisp.dispStepParams(labels, values, locked, selParam, !getEncoderSelect());
    }

    bool FormMachineOmni::drawPage(uint8_t page, uint8_t selParam)
    {
        switch (page)
        {
        case OMNIPAGE_STEPNOTES:
        {
            const char *labels[6];
            const char *headers[1];
            headers[0] = omxFormGlobal.useNoteNumbers ? "Note Numbers" : "Notes";

            auto step = getSelStep();

            for (uint8_t i = 0; i < 6; i++)
            {
                int note = step->notes[i];

                if (note >= 0 && note <= 127)
                {
                    tempStrings[i] = omxFormGlobal.useNoteNumbers ? String(note) : omxFormGlobal.musicScale->getFullNoteName(note);
                    labels[i] = tempStrings[i].c_str();
                }
                else
                {
                    labels[i] = "-";
                }
            }

            omxDisp.dispCenteredSlots(FONT_LABELS, labels, 6, selParam, getEncoderSelect(), true, true, headers, 1);
        }
            return false;
        // The seven param pages render in the shared 4-cell grid — same look as the
        // shell's Step/Notes/MI param pages (FORM_V2_REVIEW.md §4 decision 2).
        case OMNIPAGE_TRACK:
        {
            auto track = getTrack();
            const char *labels[4] = {"LEN", "MFX", "", ""};
            String vals[4];
            vals[0] = String(track->len + 1);
            vals[1] = track->midiFx == 0 ? "--" : String(track->midiFx);
            dispParamGrid(labels, vals, selParam);
        }
            return false;
        case OMNIPAGE_TRACKMODES:
        {
            auto track = getTrack();
            const char *labels[4] = {"TRIP", "DIR", "MODE", ""};
            String vals[4];
            vals[0] = track->tripletMode ? "On" : "--";
            vals[1] = track->playDirection == TRACKDIRECTION_FORWARD ? ">>" : "<<";
            vals[2] = kTrackModeShort[track->playMode];
            dispParamGrid(labels, vals, selParam);
        }
            return false;
        case OMNIPAGE_SEQTPOSE:
        {
            const char *labels[4] = {"TPOS", "TYPE", "TPAT", ""};
            String vals[4];
            vals[0] = String(seq_.transpose);
            vals[1] = kTranspModeShort[seq_.transposeMode];
            vals[2] = seq_.applyTransPat == 1 ? "On" : "--";
            dispParamGrid(labels, vals, selParam);
        }
            return false;
        case OMNIPAGE_SEQMIDI:
        {
            const char *labels[4] = {"CHAN", "MONO", "MIDI", "CV"};
            String vals[4];
            vals[0] = String(seq_.channel + 1);
            vals[1] = seq_.monoPhonic == 1 ? "On" : "--";
            vals[2] = seq_.sendMidi ? "On" : "--";
            vals[3] = seq_.sendCV ? "On" : "--";
            dispParamGrid(labels, vals, selParam);
        }
            return false;
        case OMNIPAGE_TIMINGS:
        {
            auto track = getTrack();
            const char *labels[4] = {"BPM", "RATE", "SWNG", "S-DV"};
            String vals[4];
            vals[0] = String((uint16_t)clockConfig.clockbpm);
            vals[1] = String(kSeqRates[seq_.rate]); // full "1:n" pops while turning
            vals[2] = String(track->swing);
            vals[3] = track->swingDivision == 0 ? "16" : "8"; // "16th"/"8th" pops while turning
            dispParamGrid(labels, vals, selParam);
        }
            return false;
        case OMNIPAGE_SCALE:
        {
            const char *labels[4] = {"ROOT", "SCALE", "LOCK", "GROUP"};
            String vals[4];
            vals[0] = omxFormGlobal.musicScale->getNoteName(scaleConfig.scaleRoot);
            vals[1] = scaleConfig.scalePattern < 0 ? "--" : String(scaleConfig.scalePattern);
            vals[2] = scaleConfig.lockScale ? "On" : "--";
            vals[3] = scaleConfig.group16 ? "On" : "--";
            dispParamGrid(labels, vals, selParam);
        }
            return false;
        case OMNIPAGE_TPAT:
        {
            transpPat_.onDisplayUpdate(&tPatParams_, &seq_.transposePattern, getEncoderSelect());
        }
            return false;
        }

        return false;
    }

    void FormMachineOmni::onDisplayUpdate()
    {
        omxDisp.clearLegends();

        switch (omniUiMode_)
        {
        case OMNIUIMODE_CONFIG:
        case OMNIUIMODE_MIX:
        case OMNIUIMODE_LENGTH:
        {
            // Every page renders itself (the specialized editors, or the shared
            // dispParamGrid look) — the legacy dispGenericMode2 legend fallback is retired.
            drawPage(trackParams_.getSelPage(), trackParams_.getSelParam());
        }
        break;
        case OMNIUIMODE_TRANSPOSE:
        {
            int8_t selParam = tPatParams_.getSelParam();
            drawPage(OMNIPAGE_TPAT, selParam);
        }
        break;
        case OMNIUIMODE_STEP:
        case OMNIUIMODE_NOTEEDIT:
            omniNoteEditor.onDisplayUpdate(getTrack());
            break;
        }
    }

    // AUX + Top 1 = Play Stop
    // For Omni:
    // AUX + Top 2 = Reset
    // AUX + Top 3 = Flip play direction if forward or reverse
    // AUX + Top 4 = Increment play direction mode
    int FormMachineOmni::saveToDisk(int startingAddress, Storage *storage)
	{
        int saveSize = sizeof(OmniSeq);

        // Serial.println("Omni Save Size = " + String(saveSize));

        storage->write(startingAddress, kOmniSaveVersion);
        startingAddress += 1;

        auto saveBytesPtr = (byte *)(&seq_);
        for (int j = 0; j < saveSize; j++)
        {
            storage->write(startingAddress + j, *saveBytesPtr++);
        }

        startingAddress += saveSize;

        return startingAddress;
	}

	int FormMachineOmni::loadFromDisk(int startingAddress, Storage *storage)
	{
		int saveSize = sizeof(OmniSeq);

        uint8_t ver = storage->read(startingAddress);
        startingAddress += 1;

        // Only blit if the on-disk layout matches; otherwise leave seq_ at defaults
        // but still advance the address so later machines/patterns stay aligned.
        if (ver == kOmniSaveVersion)
        {
            auto current = (byte *)&seq_;
            for (int j = 0; j < saveSize; j++)
            {
                *current = storage->read(startingAddress + j);
                current++;
            }
            // Sanitize ranges the blit can't guarantee (older saves within the same
            // version have shipped out-of-range values, e.g. potBank 7 -> "Bank 8").
            sanitizeSeq();
        }
        startingAddress += saveSize;

        resetPlayback(true);
        onEnabled();

        return startingAddress;
	}
}

