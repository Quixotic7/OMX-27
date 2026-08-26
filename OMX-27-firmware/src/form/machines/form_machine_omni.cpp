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
#include <U8g2_for_Adafruit_GFX.h>
// #include <algorithm>

namespace FormOmni
{
    enum OmniPage
    {
        OMNIPAGE_STEP1, // Vel, Nudge, Length, MFX
        OMNIPAGE_STEPCONDITION, // Prob, Condition, Func, Accum
        OMNIPAGE_STEPNOTES,
        OMNIPAGE_STEPPOTS,
        OMNIPAGE_TPAT, // Transpose pattern — sits right after CC
        OMNIPAGE_TRACK, // Length, MidiFX
        OMNIPAGE_TRACKMODES, // Triplet Mode, Direction, Mode,
        OMNIPAGE_SEQMIX, // Mute, Solo, Gate
        OMNIPAGE_SEQTPOSE, // Transpose, Transpose Mode, Apply Transpose Pat,
        OMNIPAGE_SEQMIDI, // Midi Chan, MonoPhonic, SendMidi, SendCV
        OMNIPAGE_TIMINGS, // BPM, Rate, Swing, Swing Division
        OMNIPAGE_SCALE,
        OMNIPAGE_NAV,  // Page, Zoom, UI Mode
        OMNIPAGE_COUNT
    };

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
    ParamManager stepParams_;
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

        trackParams_.addPage(4); // OMNIPAGE_STEP1, // Vel, Nudge, Length, MFX
        trackParams_.addPage(4); // OMNIPAGE_STEPCONDITION, // Prob, Condition, Func, Accum
        trackParams_.addPage(7); // OMNIPAGE_STEPNOTES,
        trackParams_.addPage(7); // OMNIPAGE_STEPPOTS,
        trackParams_.addPage(17); // OMNIPAGE_TPAT, transpose pattern (moved after CC)
        trackParams_.addPage(4); // OMNIPAGE_TRACK, // Length, MidiFX
        trackParams_.addPage(4); // OMNIPAGE_TRACKMODES, // Triplet Mode, Direction, Mode,
        trackParams_.addPage(4); // OMNIPAGE_SEQMIX, // Mute, Solo, Gate
        trackParams_.addPage(4); // OMNIPAGE_SEQTPOSE, // Transpose, Transpose Mode, Apply Transpose Pat,
        trackParams_.addPage(4); // OMNIPAGE_SEQMIDI, // Midi Chan, MonoPhonic, SendMidi, SendCV
        trackParams_.addPage(4); // OMNIPAGE_TIMINGS, // BPM, Rate, Swing, Swing Division
        trackParams_.addPage(4); // OMNIPAGE_SCALE,

        stepParams_.addPage(4); // OMNIPAGE_STEP1, // Vel, Nudge, Length, MFX
        stepParams_.addPage(4); // OMNIPAGE_STEPCONDITION, // Prob, Condition, Func, Accum
        stepParams_.addPage(7); // OMNIPAGE_STEPNOTES,
        stepParams_.addPage(7); // OMNIPAGE_STEPPOTS,

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

    FormMachineInterface *FormMachineOmni::getClone()
    {
        auto clone = new FormMachineOmni();
        return clone;
    }

    ParamManager *FormMachineOmni::getParams()
    {
        return &trackParams_;

        // switch (omniUiMode_)
        // {
        // case OMNIUIMODE_CONFIG:
        // case OMNIUIMODE_MIX:
        // {
        //     if(stepHeld_)
        //     {
        //         return &stepParams_;
        //     }
        //     else
        //     {
        //         return &trackParams_;
        //     }
        // }
        // break;
        // case OMNIUIMODE_LENGTH:
        // break;
        // case OMNIUIMODE_TRANSPOSE:
        //     return &tPatParams_;
        // case OMNIUIMODE_STEP:
        // case OMNIUIMODE_NOTEEDIT:
        // break;
        // }

        // return &trackParams_;
    }

    void FormMachineOmni::onSelected()
    {
        setPotPickups(OMNIPAGE_NAV);
    }

    void FormMachineOmni::setPotPickups(uint8_t page)
    {
        switch (page)
        {
        case OMNIPAGE_STEP1:
        {
            auto selStep = getSelStep();

            omxFormGlobal.potPickups[0].SetValRemap(selStep->vel, 0, 127);
            omxFormGlobal.potPickups[1].SetValRemap(selStep->nudge, -60, 60);
            omxFormGlobal.potPickups[2].SetValRemap(selStep->len, 0, 22);
            omxFormGlobal.potPickups[3].SetValRemap(selStep->mfxIndex, 0, 6);
        }
        break;
        case OMNIPAGE_STEPCONDITION:
            break;
        case OMNIPAGE_STEPNOTES:
            break;
        case OMNIPAGE_STEPPOTS:
            break;
        case OMNIPAGE_NAV:
        {
            omxFormGlobal.potPickups[0].SetValRemap(activePage_, 0, kFormNumPages - 1);
            omxFormGlobal.potPickups[1].SetValRemap(zoomLevel_, 0, 2); // zoom retired (unused)
            omxFormGlobal.potPickups[4].SetValRemap(omniUiMode_, 0, OMNIUIMODE_COUNT - 1);
        }
        break;
        };
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
            if (stepHeld_)
            {
                return true;
            }
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
        {
            if(stepHeld_)
            {
                return true;
            }
        }
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

    const char *FormMachineOmni::getF3shortcutName()
    {
        return "LEN | RATE";
    }

    bool FormMachineOmni::getEncoderSelect()
    {
        bool shouldSelect = omxFormGlobal.encoderSelect;

        if(omniUiMode_ == OMNIUIMODE_TRANSPOSE)
        {
            shouldSelect = transpPat_.getEncoderSelect();
        }
        else if (omniUiMode_ <= OMNIUIMODE_MIX)
        {
            shouldSelect = !stepHeld_;
        }

	    return omxFormGlobal.encoderSelect && !midiSettings.midiAUX && shouldSelect;
    }

    void FormMachineOmni::setTest()
    {
        auto track = getTrack();

        track->len = 3;
        track->steps[0].notes[0] = 60;
        track->steps[1].notes[0] = 64;
        track->steps[2].notes[0] = 67;
        track->steps[3].notes[0] = 71;
        onTrackLengthChanged();
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

    uint8_t FormMachineOmni::key16toStep(uint8_t key16)
    {
        uint8_t zoomMult = kZoomMults[zoomLevel_];

        uint8_t view = 16 * zoomMult;

        uint8_t page = min(activePage_, kPageMax[zoomLevel_]);

        uint8_t stepIndex = (view * page) + (key16 * zoomMult);

        return stepIndex;
    }

    void FormMachineOmni::selStep(uint8_t stepIndex)
    {
        if (stepHeld_)
        {
            // setPotPickups(OMNIPAGE_GBL1);
            // Update pickups to new step
            setPotPickups(trackParams_.getSelPage());
            // stepHeld_ = false;
        }
        selStep_ = key16toStep(stepIndex);

        omniNoteEditor.setSelStep(selStep_);
    }

    void FormMachineOmni::stepHeld(uint8_t key16Index)
    {
        if(!stepHeld_)
        {
            selStep(key16Index);
            setPotPickups(trackParams_.getSelPage());
            stepHeld_ = true;
        }
    }

    void FormMachineOmni::stepReleased(uint8_t key16Index)
    {
        if(stepHeld_)
        {
            uint8_t stepIndex = key16toStep(key16Index);
            if(selStep_ == stepIndex)
            {
                setPotPickups(OMNIPAGE_NAV);
                stepHeld_ = false;
            }
        }
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

    // Which P-Lock bit a Step-view edit mode owns (-1 = none, e.g. Note).
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
        }
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
        default: return -1; // math via stepMathInfo
        }
    }

    void FormMachineOmni::seqMenuEnter()
    {
        trackParams_.setSelPageAndParam(OMNIPAGE_STEPNOTES, 0);
    }
    bool FormMachineOmni::seqMenuAtStart()
    {
        return trackParams_.getSelPage() == OMNIPAGE_STEPNOTES && trackParams_.getSelParam() == 0;
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

    void FormMachineOmni::resetStepValue(uint8_t key16, uint8_t mode)
    {
        if (key16 >= 16) return;
        Step *s = &getTrack()->steps[key16toStep(key16)];
        int8_t lock = stepModeToLock(mode);
        if (lock >= 0) s->clearLock(lock); // resetting to default clears the P-Lock
        switch (mode)
        {
        case 1: s->vel = 127; break;
        case 2: s->len = 3; break;
        case 3: s->repeat = 0; break;
        case 4: s->prob = 100; break;
        case 5: s->condition = 0; break;
        case 6: s->func = 0; break;
        case 7: s->mfxIndex = 1; break;
        }
    }

    void FormMachineOmni::previewNote(int8_t note, bool on)
    {
        if (context_ == nullptr || note < 0 || note > 127) return;
        MidiNoteGroup ng;
        ng.channel = seq_.channel + 1;
        ng.noteNumber = note;
        ng.prevNoteNumber = note;
        ng.velocity = on ? 100 : 0;
        ng.sendMidi = (bool)seq_.sendMidi;
        ng.sendCV = (bool)seq_.sendCV;
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
        if(step->prob == 100 && step->condition == 0) return true;

        if (step->prob != 100 && (step->prob == 0 || random(100) > step->prob))
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
            auto track = getTrack();

            if(currentStepIndex == 0)
            {
                track->playDirection = TRACKDIRECTION_FORWARD;
            }
            else if(currentStepIndex == track->len)
            {
                track->playDirection = TRACKDIRECTION_REVERSE;
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

            uint8_t jumpStep = functionIndex - STEPFUNC_COUNT;

            // Keep jump inside length
            jumpStep = jumpStep % (track->len + 1);

            return jumpStep;
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

    void FormMachineOmni::triggerStep(Step *step, StepDynamic *stepDynamic)
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
                    // overlapping note ons
                    for (auto n : triggeredNotes_)
                    {
                        if (n.noteNumber == noteNumber)
                        {
                            noteTriggeredOnSameStep = true;
                            continue;
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

        // Increment steps tPat position
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
        stepHeld_ = false;
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
            if (stepHeld_)
            {
                stepParams_.changeParam(enc.dir());
            }
            else
            {
                int8_t prevPage = trackParams_.getSelPage();

                trackParams_.changeParam(enc.dir());

                int8_t newPage = trackParams_.getSelPage();

                if (prevPage == OMNIPAGE_TPAT && newPage == OMNIPAGE_TRACK)
                {
                    omxDisp.displayMessage("Track Params");
                }
                else if (prevPage == OMNIPAGE_TRACK && newPage == OMNIPAGE_TPAT)
                {
                    omxDisp.displayMessage("Step Params");
                }

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
            switch (selPage)
            {
            case OMNIPAGE_TPAT: // SendMidi, SendCV
                transpPat_.onEncoderChangedEditParam(enc, selParam, &seq_.transposePattern);
                break;
            default:
                editPage(selPage, selParam, amtSlow, amtFast);
                break;
            }
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
            encoderSelect_ = true;

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

        stepHeld_ = false;
        // Tell Note editor it's been started for step mode
    }

    void FormMachineOmni::onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta)
    {
        // Serial.println("onPotChanged: " + String(potIndex) + " " + String(prevValue) + " " + String(newValue));

        // v2: K5 no longer selects the UI mode — views live on the AUX layer (AUX+13-18).
        // (Knobs become pot banks in a later step.)
        if (potIndex == 4)
        {
            return;
        }

        if (stepHeld_)
        {
            auto page = stepParams_.getSelPage();

            switch (page)
            {
            case OMNIPAGE_STEP1:
            {
                auto selStep = getSelStep();

                if (potIndex == 0)
                {
                    selStep->vel = omxFormGlobal.potPickups[0].UpdatePotGetMappedValue(prevValue, newValue, 0, 127);
                    omxFormGlobal.potPickups[0].DisplayValue("Velocity", selStep->vel);
                }
                else if (potIndex == 1)
                {
                    selStep->nudge = omxFormGlobal.potPickups[1].UpdatePotGetMappedValue(prevValue, newValue, -60, 60);
                    int8_t nudgePerc = selStep->nudge / 60.0f * 100;
                    tempString = "Nudge " + String(nudgePerc) + "%";
                    omxFormGlobal.potPickups[1].DisplayLabel(tempString.c_str());
                }
                else if (potIndex == 2)
                {
                    selStep->len = omxFormGlobal.potPickups[2].UpdatePotGetMappedValue(prevValue, newValue, 0, 22);
                    tempString = "Nt Length " + getStepLenString(selStep->len);
                    omxFormGlobal.potPickups[2].DisplayLabel(tempString.c_str());
                }
                else if (potIndex == 3)
                {
                    selStep->mfxIndex = omxFormGlobal.potPickups[3].UpdatePotGetMappedValue(prevValue, newValue, 0, 6);
                    tempString = "MidiFX " + (selStep->mfxIndex == 0 ? "Off" : selStep->mfxIndex == 1 ? "Track"
                                                                                                      : String(selStep->mfxIndex - 1));
                    omxFormGlobal.potPickups[3].DisplayLabel(tempString.c_str());
                }
            }
            break;
            case OMNIPAGE_STEPCONDITION:
                break;
            case OMNIPAGE_STEPNOTES:
                break;
            case OMNIPAGE_STEPPOTS:
                break;
            // case OMNIPAGE_GBL1:
            //     break;
            // case OMNIPAGE_1:
            //     break;
            // case OMNIPAGE_2:
            //     break;
            // case OMNIPAGE_3:
            //     break;
            // case OMNIPAGE_TPAT:
            //     break;
            };
        }
        else
        {
            if (potIndex == 0)
            {
                // v2: fixed 4 pages of 16 (zoom retired) — page 0-3.
                activePage_ = omxFormGlobal.potPickups[0].UpdatePotGetMappedValue(prevValue, newValue, 0, kFormNumPages - 1);
                omxFormGlobal.potPickups[0].DisplayValue("Page", activePage_ + 1);
            }
            // potIndex == 1 (was Zoom) is retired in v2 — the grid is always 16 steps.
            // zoomLevel_ stays 0, so kZoomMults[0]=1 / kPageMax[0]=4 everywhere.
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

            auto currentStep = &track->steps[playingStep_];

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

            int8_t directionIncrement = track->playDirection == TRACKDIRECTION_FORWARD ? 1 : -1;

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
            auto nextStep = &track->steps[nextStepIndex];

            if(shouldTriggerStep)
            {
                auto trackDynamic = getDynamicTrack();
                auto dynamicStep = &trackDynamic->steps[playingStep_];

                triggerStep(currentStep, dynamicStep);

                lastTriggeredStepState_ = true;
            }
            else
            {
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
            omxDisp.setLegend(2, "LEN", String(stepLenMult, 2));
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
            auto heldStep = getSelStep();

            if(stepHeld_)
            {
                for (uint8_t i = 0; i < STEPFUNC_COUNT; i++)
                {
                    int keyColor = kStepFuncColors[i];

                    if(i == heldStep->func)
                    {
                        keyColor = blinkState ? LEDOFF : keyColor;
                    }

                    strip.setPixelColor(1 + i, keyColor);
                }
            }

            auto track = getTrack();

            for (uint8_t i = 0; i < 16; i++)
            {
                uint8_t stepIndex = key16toStep(i);
                bool isInLen = stepIndex <= track->len;
                auto step = &track->steps[stepIndex];
                int keyColor = (step->mute || !isInLen) ? LEDOFF : (step->hasNotes() ? LTBLUE : DKBLUE);

                if (stepHeld_ && heldStep->func >= STEPFUNC_COUNT && (heldStep->func - STEPFUNC_COUNT) == stepIndex)
                {
                    keyColor = LTYELLOW;
                }

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

                strip.setPixelColor(11 + playingStepKey, lastTriggeredStepState_ ? WHITE : RED);
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

        if (omxFormGlobal.shortcutMode == FORMSHORTCUT_AUX)
        {
            if(e.down())
            {
                if(thisKey == 3)
                {
                    auto track = getTrack();
                    track->playDirection = track->playDirection == TRACKDIRECTION_FORWARD ? TRACKDIRECTION_REVERSE : TRACKDIRECTION_FORWARD;
                    omxDisp.displayMessage(track->playDirection == TRACKDIRECTION_FORWARD ? ">>" : "<<");
                }
                else if (thisKey >= 13 && thisKey < 19)
                {
                    changeUIMode(thisKey - 13, false);
                    setPotPickups(OMNIPAGE_NAV);
                    return true;
                }
            }
        }

        switch (omniUiMode_)
        {
        case OMNIUIMODE_CONFIG:
        case OMNIUIMODE_MIX:
        {
            // Double click to enter note edit
            if (!e.down() && thisKey >= 11 && e.clicks() == 2)
            {
                auto track = getTrack();

                selStep(thisKey - 11);
                stepReleased(thisKey - 11);
                uint8_t stepIndex = key16toStep(thisKey - 11);
                // reverse effects of quick click mute
                track->steps[stepIndex].mute = !track->steps[stepIndex].mute;

                changeUIMode(OMNIUIMODE_NOTEEDIT, false);
                omxFormGlobal.auxBlock = true;
                return true;
            }

            if (stepHeld_)
            {
                // Shortcut to set mute
                if (e.quickClicked() && thisKey >= 11)
                {
                    auto track = getTrack();

                    selStep(thisKey - 11);
                    stepReleased(thisKey - 11);
                    uint8_t stepIndex = key16toStep(thisKey - 11);
                    track->steps[stepIndex].mute = !track->steps[stepIndex].mute;
                }
                else if (e.down() && thisKey >= 1 && thisKey <= STEPFUNC_COUNT)
                {
                    auto step = getSelStep();

                    step->func = thisKey - 1;
                    omxDisp.displayMessage(kStepFuncs[step->func]);
                }
                // Shortcut to jump to another step
                else if (e.down() && thisKey >= 11)
                {
                    uint8_t jumpStep = key16toStep(thisKey - 11);

                    // Conditional might be unneeded, this should never be false
                    if (jumpStep != selStep_)
                    {
                        auto step = getSelStep();

                        step->func = jumpStep + STEPFUNC_COUNT;

                        omxDisp.displayMessage("Jump to " + String(jumpStep + 1));
                    }
                }
                // Release the held step key
                if (!e.down() && thisKey >= 11)
                {
                    stepReleased(thisKey - 11);
                }
            }
            else
            {
                switch (omxFormGlobal.shortcutMode)
                {
                case FORMSHORTCUT_NONE:
                {
                    if (thisKey >= 11 && thisKey < 27)
                    {
                        // Shortcut to set mute, not sure this is needed here
                        if (e.quickClicked())
                        {
                            auto track = getTrack();

                            selStep(thisKey - 11);
                            stepReleased(thisKey - 11);
                            uint8_t stepIndex = key16toStep(thisKey - 11);
                            track->steps[stepIndex].mute = !track->steps[stepIndex].mute;
                        }
                        else if (e.down())
                        {
                            selStep(thisKey - 11);
                            stepHeld(thisKey - 11);
                        }
                        else
                        {
                            stepReleased(thisKey - 11);
                        }
                    }
                }
                break;
                case FORMSHORTCUT_AUX:
                    break;
                case FORMSHORTCUT_F1:
                    if (e.down() && thisKey == 0)
                    {
                        changeUIMode(OMNIUIMODE_NOTEEDIT, false);
                        omxFormGlobal.auxBlock = true;
                        return true;
                    }
                    // Copy Paste
                    else if (e.down() && thisKey >= 11 && thisKey < 27)
                    {
                        if (omxFormGlobal.shortcutPaste == false)
                        {
                            copyStep(thisKey - 11);
                            omxFormGlobal.shortcutPaste = true;
                        }
                        else
                        {
                            pasteStep(thisKey - 11);
                        }
                    }
                    break;
                case FORMSHORTCUT_F2:
                    // Cut Paste
                    if (e.down() && thisKey >= 11 && thisKey < 27)
                    {
                        if (omxFormGlobal.shortcutPaste == false)
                        {
                            cutStep(thisKey - 11);
                            omxFormGlobal.shortcutPaste = true;
                        }
                        else
                        {
                            pasteStep(thisKey - 11);
                        }
                    }
                    break;
                case FORMSHORTCUT_F3:
                    // Set track length
                    if (e.down() && thisKey >= 3 && thisKey <= 10)
                    {
                        seq_.rate = kRateShortcuts[thisKey - 3];
                        omxDisp.displayMessage("RATE 1:" + String(kSeqRates[seq_.rate]));
                        onRateChanged();
                    }
                    if (e.down() && thisKey >= 11 && thisKey < 27)
                    {
                        auto track = getTrack();

                        uint8_t pageKey = (thisKey - 11) + (16 * min(activePage_, kPageMax[zoomLevel_] - 1));
                        track->len = pageKey * kZoomMults[zoomLevel_] + (kZoomMults[zoomLevel_] - 1);
                        onTrackLengthChanged();
                        omxDisp.displayMessage("LENGTH " + String(track->getLength()));
                    }
                    break;
                }
            }
        }
        break;
        case OMNIUIMODE_LENGTH:
        break;
        case OMNIUIMODE_TRANSPOSE:
        {
            transpPat_.onKeyUpdate(e, &tPatParams_, &seq_.transposePattern);
        }
        break;
        case OMNIUIMODE_STEP:
        case OMNIUIMODE_NOTEEDIT:
            // if (e.down() && thisKey == 0)
            // {
            //     changeUIMode(OMNIUIMODE_MIX, false);
            //     return true;
            // }
            omniNoteEditor.onKeyUpdate(e, getTrack());
            break;
        }
        return false;
    }
    bool FormMachineOmni::onKeyHeldUpdate(OMXKeypadEvent e)
    {
        // uint8_t thisKey = e.key();

        switch (omniUiMode_)
        {
        case OMNIUIMODE_CONFIG:
        case OMNIUIMODE_MIX:
        {
            switch (omxFormGlobal.shortcutMode)
            {
            case FORMSHORTCUT_NONE:
            {
                // if (thisKey >= 11 && thisKey < 27)
                // {
                //     stepHeld(thisKey - 11);
                // }
            }
            break;
            case FORMSHORTCUT_AUX:
                break;
            case FORMSHORTCUT_F1:
                break;
            case FORMSHORTCUT_F2:
                break;
            case FORMSHORTCUT_F3:
                break;
            }
        }
        break;
        case OMNIUIMODE_LENGTH:
        break;
        case OMNIUIMODE_TRANSPOSE:
        {
            transpPat_.onKeyHeldUpdate(e, &seq_.transposePattern);
        }
        break;
        case OMNIUIMODE_STEP:
        case OMNIUIMODE_NOTEEDIT:
            omniNoteEditor.onKeyHeldUpdate(e, getTrack());
            break;
        }

        return true;
    }

    bool FormMachineOmni::onKeyQuickClicked(OMXKeypadEvent e)
    {
        uint8_t thisKey = e.key();

        switch (omniUiMode_)
        {
        case OMNIUIMODE_CONFIG:
        case OMNIUIMODE_MIX:
        break;
        case OMNIUIMODE_LENGTH:
        break;
        case OMNIUIMODE_TRANSPOSE:
        break;
        case OMNIUIMODE_STEP:
        case OMNIUIMODE_NOTEEDIT:
            if (omxFormGlobal.auxBlock == false && thisKey == 0)
            {
                changeUIMode(OMNIUIMODE_MIX, false);
                omxFormGlobal.auxBlock = true;
                return true;
            }
            break;
        }

        return false;
    }

    void FormMachineOmni::editPage(uint8_t page, uint8_t param, int8_t amtSlow, int8_t amtFast)
    {
        switch (page)
        {
        case OMNIPAGE_STEP1: // Vel, Nudge, Length, MFX
        {
            auto selStep = getSelStep();

            if (param == 0)
            {
                selStep->vel = constrain(selStep->vel + amtFast, 0, 127);
            }
            else if (param == 1)
            {
                selStep->nudge = constrain(selStep->nudge + amtSlow, -60, 60);
            }
            else if (param == 2)
            {
                selStep->len = constrain(selStep->len + amtSlow, 0, 22);
            }
            else if (param == 3)
            {
                selStep->mfxIndex = constrain(selStep->mfxIndex + amtSlow, 0, NUM_MIDIFX_GROUPS + 2 - 1);
            }
        }
        break;
        case OMNIPAGE_STEPCONDITION: // Prob, Condition, Func, Accum
        {
            auto selStep = getSelStep();

            if (param == 0)
            {
                selStep->prob = constrain(selStep->prob + amtFast, 0, 100);
            }
            else if (param == 1)
            {
                // 0-8 are the special conditions; 9-43 map to the 35 A:B ratio rows (abIndex = condition-9)
                selStep->condition = constrain(selStep->condition + amtSlow, 0, 9 + 35 - 1);
            }
            else if (param == 2)
            {
                selStep->func = constrain(selStep->func + amtSlow, 0, STEPFUNC_COUNT + 64 - 1);
            }
            else if (param == 3)
            {
                selStep->accumTPat = constrain(selStep->accumTPat + amtSlow, 0, 4);
            }
        }
        break;
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
        case OMNIPAGE_STEPPOTS:
        {
            auto selStep = getSelStep();

            if (param == 5)
            {
                seq_.potMode = constrain(seq_.potMode + amtSlow, 0, 1);
            }
            else if (param == 6)
            {
                seq_.potBank = constrain(seq_.potBank + amtSlow, 0, NUM_CC_BANKS - 1);
            }
            else
            {
                selStep->potVals[param] = constrain(selStep->potVals[param] + amtFast, -1, 127);
            }
        }
        break;
        // Length, MidiFX
        case OMNIPAGE_TRACK:
        {
            auto track = getTrack();

            if (param == 0)
            {
                track->len = constrain(track->len + amtSlow, 0, 63);
                onTrackLengthChanged();
            }
            else if (param == 1)
            {
                track->midiFx = constrain(track->midiFx + amtSlow, 0, NUM_MIDIFX_GROUPS + 1 - 1);
            }
        }
        break;
        // Triplet Mode, Direction, Mode,
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
            }
        }
        break;
        // Mute, Solo, Gate
        case OMNIPAGE_SEQMIX:
        {
            if (param == 0)
            {
                seq_.mute = constrain(seq_.mute + amtSlow, 0, 1);
            }
            else if (param == 1)
            {
                seq_.solo = constrain(seq_.solo + amtSlow, 0, 1);
            }
            else if (param == 2)
            {
                seq_.gate = constrain(seq_.gate + amtFast, 0, 100);
            }
        }
        break;
        // Transpose, Transpose Mode, Apply Transpose Pat,
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
            }
            else if (param == 2)
            {
                track->swing = constrain(track->swing + amtFast, -100, 100);
            }
            else if (param == 3)
            {
                track->swingDivision = constrain(track->swingDivision + amtSlow, 0, 1);
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
        case OMNIPAGE_TPAT: // SendMidi, SendCV
        {
        }
        break;
        }
    }

    bool FormMachineOmni::drawPage(uint8_t page, uint8_t selParam)
    {
        switch (page)
        {
        case OMNIPAGE_STEP1: // Vel, Nudge, Length, MFX
        {
            omxDisp.clearLegends();

            auto selStep = getSelStep();

            omxDisp.setLegend(0, "VEL", selStep->vel);

            int8_t nudgePerc = (selStep->nudge / 60.0f) * 100;
            omxDisp.setLegend(1, "NUDG", nudgePerc);
            omxDisp.setLegend(2, "LEN", getStepLenString(selStep->len));
            omxDisp.setLegend(3, "MFX", selStep->mfxIndex == 0, selStep->mfxIndex == 1 ? "TRK" : String(selStep->mfxIndex - 1));
        }
            return true;
        case OMNIPAGE_STEPCONDITION: // Prob, Condition, Func, Accum
        {
            omxDisp.clearLegends();

            auto selStep = getSelStep();

            omxDisp.setLegend(0, "CHC%", selStep->prob);
            omxDisp.setLegend(1, "COND", getCondChar(selStep->condition));

            if (selStep->func >= STEPFUNC_COUNT)
            {
                omxDisp.setLegend(2, "FUNC", "J" + String(selStep->func - STEPFUNC_COUNT + 1));
            }
            else
            {
                omxDisp.setLegend(2, "FUNC", kStepFuncs[selStep->func]);
            }

            omxDisp.setLegend(3, "ACUM", selStep->accumTPat == 0, selStep->accumTPat);
        }
            return true;
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
                    // note = note + (5 * 12); // get rid of negative octaves since we can only disp 3 chars per note
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
        case OMNIPAGE_STEPPOTS:
        {
            const char *labels[5];
            const char *headers[2];
            headers[0] = kPotMode[seq_.potMode];
            tempStrings[5] = "Bank " + String(seq_.potBank + 1);
            headers[1] = tempStrings[5].c_str();

            auto step = getSelStep();

            for (uint8_t i = 0; i < 5; i++)
            {
                int pVal = step->potVals[i];

                if (pVal >= 0 && pVal <= 127)
                {
                    tempStrings[i] = String(pVal);
                    labels[i] = tempStrings[i].c_str();
                }
                else
                {
                    labels[i] = "-";
                }
            }

            omxDisp.dispCenteredSlots(labels, 5, selParam, getEncoderSelect(), true, true, headers, 2);

            // omxDisp.clearLegends();

            // auto selStep = getSelStep();
            // // auto bank = pots[potSettings.potbank];

            // omxDisp.setLegend(0, "CC1", selStep->potVals[0] < 0, selStep->potVals[0]);
            // omxDisp.setLegend(1, "CC2", selStep->potVals[1] < 0, selStep->potVals[1]);
            // omxDisp.setLegend(2, "CC3", selStep->potVals[2] < 0, selStep->potVals[2]);
            // omxDisp.setLegend(3, "CC4", selStep->potVals[3] < 0, selStep->potVals[3]);
        }
            return false;
        // Length, MidiFX
        case OMNIPAGE_TRACK:
        {
            auto track = getTrack();

            omxDisp.setLegend(0, "LEN", track->len + 1);
            omxDisp.setLegend(1, "MFX", track->midiFx == 0, track->midiFx);
        }
            return true;
        // Triplet Mode, Direction, Mode,
        case OMNIPAGE_TRACKMODES:
        {
            auto track = getTrack();

            omxDisp.setLegend(0, "TRIP", bool2lightswitchMsg[track->tripletMode]);
            omxDisp.setLegend(1, "DIR", track->playDirection == TRACKDIRECTION_FORWARD ? ">>" : "<<");
            omxDisp.setLegend(2, "MODE", kTrackModeMsg[track->playMode]);
        }
            return true;
        // Mute, Solo, Gate
        case OMNIPAGE_SEQMIX:
        {
            omxDisp.setLegend(0, "MUTE", seq_.mute == 1);
            omxDisp.setLegend(1, "SOLO", seq_.solo == 1);
            uint8_t gateMult = getGateMult(seq_.gate) * 100;
            omxDisp.setLegend(2, "GATE", gateMult);
        }
            return true;
        // Transpose, Transpose Mode, Apply Transpose Pat,
        case OMNIPAGE_SEQTPOSE:
        {
            omxDisp.setLegend(0, "TPOS", seq_.transpose);
            omxDisp.setLegend(1, "TYPE", kTranspModeMsg[seq_.transposeMode]);
            omxDisp.setLegend(2, "TPAT", seq_.applyTransPat == 1);
        }
            return true;
        // Midi Chan, MonoPhonic, SendMidi, SendCV
        case OMNIPAGE_SEQMIDI:
        {
            omxDisp.setLegend(0, "CHAN", seq_.channel + 1);
            omxDisp.setLegend(1, "MONO", seq_.monoPhonic == 1 ? "MONO" : "POLY");
            omxDisp.setLegend(2, "MIDI", seq_.sendMidi ? "SEND" : paramOffMsg);
            omxDisp.setLegend(3, "CV", seq_.sendCV ? "SEND" : paramOffMsg);
        }
            return true;
        // BPM, Rate, Swing, Swing Division
        case OMNIPAGE_TIMINGS:
        {
            auto track = getTrack();

            omxDisp.setLegend(0, "BPM", (uint16_t)clockConfig.clockbpm);
            omxDisp.setLegend(1, "RATE", "1/" + String(kSeqRates[seq_.rate]));
            omxDisp.setLegend(2, "SWNG", track->swing);
            omxDisp.setLegend(3, "S-DV", track->swingDivision == 0 ? "16th" : "8th");
        }
            return true;
        case OMNIPAGE_SCALE:
        {
            omxDisp.setLegend(0, "ROOT", omxFormGlobal.musicScale->getNoteName(scaleConfig.scaleRoot));
            omxDisp.setLegend(1, "SCALE", scaleConfig.scalePattern < 0, scaleConfig.scalePattern);
            omxDisp.setLegend(2, "LOCK", scaleConfig.lockScale);
            omxDisp.setLegend(3, "GROUP", scaleConfig.group16);
        }
            return true;
        case OMNIPAGE_TPAT:
        {
            transpPat_.onDisplayUpdate(omniUiMode_ == OMNIUIMODE_TRANSPOSE ? &tPatParams_ : &trackParams_, &seq_.transposePattern, getEncoderSelect());
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
            int8_t selPage = trackParams_.getSelPage();
            int8_t selParam = trackParams_.getSelParam();

            // bool drawGeneric = true;

            // switch (selPage)
            // {
            // case OMNIPAGE_STEP1:
            // case OMNIPAGE_STEPCONDITION:
            // case OMNIPAGE_STEPNOTES:
            // case OMNIPAGE_STEPPOTS:
            // case OMNIPAGE_TPAT: // SendMidi, SendCV
            //     drawGeneric = drawPage(selPage, selParam);
            //     break;
            // }

            bool drawGeneric = drawPage(selPage, selParam);

            if(drawGeneric)
            {
                omxDisp.dispGenericMode2(trackParams_.getNumPages(), trackParams_.getSelPage(), trackParams_.getSelParam(), getEncoderSelect());
            }
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
    void FormMachineOmni::onAUXFunc(uint8_t funcKey) {}

    // Bump whenever the OmniSeq layout changes so old saves are skipped rather than
    // blitted into a mismatched struct. (The global EEPROM_VERSION also gates loads,
    // but this makes an OmniSeq change safe on its own.)
    static const uint8_t kOmniSaveVersion = 4; // v4: added Track::paramDefaults

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
        }
        startingAddress += saveSize;

        resetPlayback(true);
        onEnabled();

        return startingAddress;
	}
}

