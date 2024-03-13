#include "form_machine_omni.h"
#include "../../config.h"
#include "../../consts/colors.h"
#include "../../utils/omx_util.h"
#include "../../hardware/omx_disp.h"
#include "../../hardware/omx_leds.h"
#include "omni_note_editor.h"

namespace FormOmni
{
    enum OmniPage
    {
        OMNIPAGE_STEP1, // Vel, Nudge, Length, MFX
        OMNIPAGE_STEPCONDITION, // Prob, Condition, Func, Accum
        OMNIPAGE_STEPNOTES,
        OMNIPAGE_STEPPOTS, 
        OMNIPAGE_GBL1, // BPM
        OMNIPAGE_1,    // Velocity, Channel, Rate, Gate
        OMNIPAGE_2,    // Transpose, TransposeMode
        OMNIPAGE_3,    // SendMidi, SendCV
        OMNIPAGE_TPAT, // SendMidi, SendCV
        OMNIPAGE_COUNT
    };

    enum OmniStepPage
    {
        OSTEPPAGE_1,
        OSTEPAGE_COUNT
    };

    const char* kUIModeMsg[] = {"CONFIG", "MIX", "LENGTH", "TRANSPOSE", "STEP", "NOTE EDIT"};

    // kSeqRates[] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64};
    // 1, 2, 3, 4, 8, 16, 32, 64
    const uint8_t kRateShortcuts[] = {0, 1, 2, 3, 6, 9, 12, 15};

    const uint8_t kZoomMults[] = {1,2,4};
    const uint8_t kPageMax[] = {4,2,1};

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

    const char* kConditionModes[9] = {"--", "FILL", "!FILL", "PRE", "!PRE", "NEI", "!NEI", "1ST", "!1ST"};

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

    const char *kStepFuncs[7] = {"--", "1", ">>", "<<", "<>", "#?", "?"};

    // Global param management so pages are same across machines
    ParamManager trackParams_;
    ParamManager noteParams_;

    bool paramsInit_ = false;
    bool neighborPrevTrigWasTrue_ = false;

    uint8_t omniUiMode_ = 0;

    FormMachineOmni::FormMachineOmni()
    {
        if (!paramsInit_)
        {
            trackParams_.addPages(OMNIPAGE_COUNT);
        }

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

    void FormMachineOmni::onSelected()
    {
        setPotPickups(OMNIPAGE_GBL1);
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
        case OMNIPAGE_GBL1:
        {
            omxFormGlobal.potPickups[0].SetValRemap(activePage_, 0, kPageMax[zoomLevel_] - 1);
            omxFormGlobal.potPickups[1].SetValRemap(zoomLevel_, 0, 2);
            omxFormGlobal.potPickups[4].SetValRemap(omniUiMode_, 0, OMNIUIMODE_COUNT - 1);
        }
        break;
        case OMNIPAGE_1:
            break;
        case OMNIPAGE_2:
            break;
        case OMNIPAGE_3:
            break;
        case OMNIPAGE_TPAT:
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
        case OMNIUIMODE_COUNT:
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

    const char *FormMachineOmni::getF3shortcutName()
    {
        return "LEN | RATE";
    }

    void FormMachineOmni::setTest()
    {
        auto track = getTrack();

        track->len = 3;
        track->steps[0].notes[0] = 60;
        track->steps[1].notes[0] = 64;
        track->steps[2].notes[0] = 67;
        track->steps[3].notes[0] = 71;
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

            // Calculate first step
        }
        else
        {
            for(auto n : noteOns_)
            {
                seqNoteOff(n, 255);
            }
            noteOns_.clear();
        }
    }

    void FormMachineOmni::resetPlayback()
    {
        auto track = getTrack();

        // nextStepTime_ = seqConfig.lastClockMicros + ;
        playingStep_ = track->playDirection == TRACKDIRECTION_FORWARD ? 0 : track->getLength() - 1;

        grooveCounter_ = 0;
        playRateCounter_ = 0;
        loopCounter_ = 0;
        loopCount_ = 0;
        firstLoop_ = true;
        prevCondWasTrue_ = false;
        neighborPrevTrigWasTrue_ = false;

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


    Track *FormMachineOmni::getTrack()
    {
        return &seq_.tracks[0];
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
                setPotPickups(OMNIPAGE_GBL1);
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

    MidiNoteGroup FormMachineOmni::step2NoteGroup(uint8_t noteIndex, Step *step)
    {
        MidiNoteGroup noteGroup;

        noteGroup.channel = seq_.channel;

        if(noteIndex >= 6)
        {
            noteGroup.noteNumber = 255;
        }
        else
        {
            noteGroup.noteNumber = step->notes[noteIndex];
        }
        noteGroup.velocity = step->vel;

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

    void FormMachineOmni::triggerStep(Step *step)
    {
        if(context_ == nullptr || noteOnFuncPtr == nullptr)
            return;

        if((bool)step->mute) return;

        // Micros now = micros();

        for(int8_t i = 0; i < 6; i++)
        {
            int8_t noteNumber = step->notes[i];

            if(noteNumber >= 0 && noteNumber <= 127)
            {
                // Serial.println("triggerStep: " + String(noteNumber));

                auto noteGroup = step2NoteGroup(i, step);

                bool noteTriggeredOnSameStep = false; 

                // With nudge, two steps could fire at once, 
                // If a step already triggered a note, 
                // don't trigger same note again to avoid
                // overlapping note ons
                for(auto n : triggeredNotes_)
                {
                    if(n.noteNumber == noteNumber)
                    {
                        noteTriggeredOnSameStep = true;
                        break;
                    }
                }

                if (!noteTriggeredOnSameStep)
                {
                    // bool foundNoteToRemove = false;
                    auto it = noteOns_.begin();
                    while (it != noteOns_.end())
                    {
                        // remove matching note numbers
                        if (it->noteNumber == noteNumber)
                        {
                            seqNoteOff(*it, 255);
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

                if(!noteTriggeredOnSameStep && noteOns_.size() < 16)
                {
                    noteGroup.noteonMicros = seqConfig.lastClockMicros;
                    seqNoteOn(noteGroup, 255);
                    triggeredNotes_.push_back(noteGroup);
                    noteOns_.push_back(noteGroup);
                }
            }
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
            int8_t prevPage = trackParams_.getSelPage();

            trackParams_.changeParam(enc.dir());

            if (trackParams_.getSelPage() != prevPage)
            {
                switch (trackParams_.getSelPage())
                {
                case OMNIPAGE_STEP1:
                    omxDisp.displayMessage("Step 1");
                    break;
                case OMNIPAGE_STEPCONDITION:
                    omxDisp.displayMessage("Step Cond");
                    break;
                case OMNIPAGE_STEPNOTES:
                    omxDisp.displayMessage("Step Notes");
                    break;
                case OMNIPAGE_STEPPOTS:
                    omxDisp.displayMessage("Step Pots");
                    break;
                case OMNIPAGE_GBL1:
                    omxDisp.displayMessage("Track 1");
                    break;
                case OMNIPAGE_1:
                    // omxDisp.displayMessage("Step 1");
                    break;
                case OMNIPAGE_2:
                    // omxDisp.displayMessage("Step 1");
                    break;
                case OMNIPAGE_3:
                    // omxDisp.displayMessage("Step 1");
                    break;
                case OMNIPAGE_TPAT:
                    // omxDisp.displayMessage("Step 1");
                    break;
                }
            }
        }
        break;
        case OMNIUIMODE_TRANSPOSE:
        case OMNIUIMODE_STEP:
        case OMNIUIMODE_NOTEEDIT:
            break;
        }

        omxDisp.setDirty();
    }
    void FormMachineOmni::onEncoderChangedEditParam(Encoder::Update enc)
    {
        int amtSlow = enc.accel(1);
        int amtFast = enc.accel(5);

        int8_t selPage = trackParams_.getSelPage();
        int8_t selParam = trackParams_.getSelParam();

        switch (omniUiMode_)
        {
        case OMNIUIMODE_CONFIG:
        case OMNIUIMODE_MIX:
        {
            switch (selPage)
            {
            case OMNIPAGE_STEP1:
                editPage(OMNIPAGE_STEP1, selParam, amtSlow, amtFast);
                break;
            case OMNIPAGE_STEPCONDITION:
                editPage(OMNIPAGE_STEPCONDITION, selParam, amtSlow, amtFast);
                break;
            case OMNIPAGE_STEPNOTES:
                editPage(OMNIPAGE_STEPNOTES, selParam, amtSlow, amtFast);
                break;
            case OMNIPAGE_STEPPOTS:
                editPage(OMNIPAGE_STEPPOTS, selParam, amtSlow, amtFast);
                break;
            case OMNIPAGE_GBL1: // BPM
                editPage(OMNIPAGE_GBL1, selParam, amtSlow, amtFast);
                break;
            case OMNIPAGE_1: // Velocity, Channel, Rate, Gate
                editPage(OMNIPAGE_1, selParam, amtSlow, amtFast);
                break;
            case OMNIPAGE_2: // Transpose, TransposeMode
                editPage(OMNIPAGE_2, selParam, amtSlow, amtFast);
                break;
            case OMNIPAGE_3: // SendMidi, SendCV
                editPage(OMNIPAGE_3, selParam, amtSlow, amtFast);
                break;
            case OMNIPAGE_TPAT: // SendMidi, SendCV
                editPage(OMNIPAGE_TPAT, selParam, amtSlow, amtFast);
                break;
            }
        }
        break;
        case OMNIUIMODE_LENGTH:
        case OMNIUIMODE_TRANSPOSE:
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
        }
    }

    void FormMachineOmni::onUIModeChanged(uint8_t prevMode, uint8_t newMode)
    {
        // Tell Note editor it's been started for step mode
    }

    void FormMachineOmni::onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta)
    {
        // Serial.println("onPotChanged: " + String(potIndex) + " " + String(prevValue) + " " + String(newValue));

        // This guy is always setting the mode
        if (potIndex == 4)
        {
            uint8_t newUIMode = omxFormGlobal.potPickups[4].UpdatePotGetMappedValue(prevValue, newValue, 0, OMNIUIMODE_COUNT - 1);
            changeUIMode(newUIMode, true);
            omxFormGlobal.potPickups[4].DisplayLabel(kUIModeMsg[omniUiMode_]);
            return;
        }

        if (stepHeld_)
        {
            auto page = trackParams_.getSelPage();

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
            case OMNIPAGE_GBL1:
                break;
            case OMNIPAGE_1:
                break;
            case OMNIPAGE_2:
                break;
            case OMNIPAGE_3:
                break;
            case OMNIPAGE_TPAT:
                break;
            };
        }
        else
        {
            if (potIndex == 0)
            {
                activePage_ = omxFormGlobal.potPickups[0].UpdatePotGetMappedValue(prevValue, newValue, 0, kPageMax[zoomLevel_] - 1);
                omxFormGlobal.potPickups[0].DisplayValue("Page", activePage_ + 1);
            }
            else if (potIndex == 1)
            {
                zoomLevel_ = omxFormGlobal.potPickups[1].UpdatePotGetMappedValue(prevValue, newValue, 0, 2);
                tempString = "Zoom " + String(kZoomMults[zoomLevel_]) + "x";
                omxFormGlobal.potPickups[1].DisplayLabel(tempString.c_str());
            }
        }

        
    }

    void FormMachineOmni::onClockTick()
    {
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
                    seqNoteOff(*it, 255);
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
        }

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

            int8_t directionIncrement = track->playDirection == TRACKDIRECTION_FORWARD ? 1 : -1;

            uint8_t nextStepIndex = (playingStep_ + length + directionIncrement) % length;

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

            if(evaluateTrig(playingStep_, currentStep))
            {
                triggerStep(currentStep);
                lastTriggeredStepState_ = true;
            }
            else
            {
                lastTriggeredStepState_ = false;
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

            loopCounter_ = (loopCounter_ + 1) % track->getLength();
            if(loopCounter_ == 0)
            {
                // 840 is evenly divisible by 8,7,6,5,4,3,2,1
                loopCount_ = (loopCount_ + 1) % 840;
                firstLoop_ = false;
            }
            playingStep_ = nextStepIndex;
        }

        ticksTilNextTrigger_--;
        ticksTilNext16Trigger_--;
        ticksTilNextTriggerRate_--;

        // if (seqConfig.lastClockMicros >= nextStepTime_)
        // {
        //     // 96 ppq

        //     auto track = getTrack();

        //     uint8_t length = track->len + 1;


        //     // uint8_t nextStepIndex = playingStep_ + 1 % length;

        //     auto playingStep = &track->steps[playingStep_];
        //     // auto nextStep = &track->steps[nextStepIndex];

        //     triggerStep(playingStep);

        //     nextStepTime_ = nextStepTime_ + clockConfig.ppqInterval * ticksPerStep_;
        //     playingStep_ = (playingStep_ + 1) % length;
        // }

        // seqConfig.lastClockMicros = ;
        // if(seqConfig.currentClockTick % 96 == 0)
        // {

        // }
        // seqConfig.currentClockTick

        // clockConfig.ppqInterval
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

    float FormMachineOmni::getGateMult(uint8_t gate)
    {
        return max(gate / 100.f * 2, 0.01f);
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
    }
    bool FormMachineOmni::updateLEDs()
    {
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

                strip.setPixelColor(11 + playingStepKey, lastTriggeredStepState_ ? WHITE : RED);
            }
        }
        break;
        case OMNIUIMODE_LENGTH:
        break;
        case OMNIUIMODE_TRANSPOSE:
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
        if (e.held())
            return false;

        omxDisp.setDirty();
        omxLeds.setDirty();

        uint8_t thisKey = e.key();

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
                    setPotPickups(OMNIPAGE_GBL1);
                    return true;
                }
            }
        }

        switch (omniUiMode_)
        {
        case OMNIUIMODE_CONFIG:
        case OMNIUIMODE_MIX:
        {
            switch (omxFormGlobal.shortcutMode)
            {
            case FORMSHORTCUT_NONE:
            {
                if (thisKey >= 11 && thisKey < 27)
                {
                    if (e.quickClicked())
                    {
                        auto track = getTrack();

                        selStep(thisKey - 11);
                        uint8_t stepIndex = key16toStep(thisKey - 11);
                        track->steps[stepIndex].mute = !track->steps[stepIndex].mute;
                    }
                    else if (e.down())
                    {
                        selStep(thisKey - 11);
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
                // Copy Paste
                if (e.down() && thisKey >= 11 && thisKey < 27)
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
                if(e.down() && thisKey >= 3 && thisKey <= 10)
                {
                    seq_.rate = kRateShortcuts[thisKey - 3];
                    omxDisp.displayMessage("RATE 1:" + String(kSeqRates[seq_.rate]));
                    onRateChanged();
                }
                if (e.down() && thisKey >= 11 && thisKey < 27)
                {
                    auto track = getTrack();
                    track->len = (thisKey - 11) * kZoomMults[zoomLevel_] + (kZoomMults[zoomLevel_] - 1);

                }
                break;
            }
        }
        break;
        case OMNIUIMODE_LENGTH:
        break;
        case OMNIUIMODE_TRANSPOSE:
        break;
        case OMNIUIMODE_STEP:
        case OMNIUIMODE_NOTEEDIT:
            omniNoteEditor.onKeyUpdate(e, getTrack());
            break;
        }
        return false;
    }
    bool FormMachineOmni::onKeyHeldUpdate(OMXKeypadEvent e)
    {
        uint8_t thisKey = e.key();

        switch (omniUiMode_)
        {
        case OMNIUIMODE_CONFIG:
        case OMNIUIMODE_MIX:
        {
            switch (omxFormGlobal.shortcutMode)
            {
            case FORMSHORTCUT_NONE:
            {
                if (thisKey >= 11 && thisKey < 27)
                {
                    stepHeld(thisKey - 11);
                }
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
        break;
        case OMNIUIMODE_STEP:
        case OMNIUIMODE_NOTEEDIT:
            omniNoteEditor.onKeyHeldUpdate(e, getTrack());
            break;
        }

        return true;
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
                selStep->mfxIndex = constrain(selStep->mfxIndex + amtSlow, 0, 6);
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
                selStep->condition = constrain(selStep->condition + amtSlow, 0, 35);
            }
            else if (param == 2)
            {
                selStep->func = constrain(selStep->func + amtSlow, 0, 22);
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
            selStep->notes[param] = constrain(selStep->notes[param] + amtFast, -1, 127);
        }
        break;
        case OMNIPAGE_STEPPOTS:
        {
            auto selStep = getSelStep();

            selStep->potVals[param] = constrain(selStep->potVals[param] + amtFast, -1, 127);
        }
        break;
        case OMNIPAGE_GBL1:
        {
            auto track = getTrack();

            if (param == 0)
            {
                track->swing = constrain(track->swing + amtFast, -100, 100);
            }
            else if (param == 1)
            {
                track->swingDivision = constrain(track->swingDivision + amtSlow, 0, 1);
            }
            else if (param == 2)
            {
            }
            else if (param == 3)
            {
            }
        }
        break;
        case OMNIPAGE_1:
        {
            auto track = getTrack();

            if (param == 0)
            {
                track->len = constrain(track->len + amtSlow, 0, 63);
            }
            else if (param == 1)
            {
                seq_.channel = constrain(seq_.channel + amtSlow, 0, 15);
            }
            else if (param == 2)
            {
                seq_.rate = constrain(seq_.rate + amtSlow, 0, kNumSeqRates - 1);
                onRateChanged();
            }
            else if (param == 3)
            {
                seq_.gate = constrain(seq_.gate + amtFast, 0, 100);
            }
        }
        break;
        case OMNIPAGE_2:
        {
            if (param == 0)
            {
                seq_.transpose = constrain(seq_.transpose + amtSlow, -64, 64);
            }
            else if (param == 1)
            {
                seq_.transposeMode = constrain(seq_.transposeMode + amtSlow, 0, 1);
            }
            else if (param == 2)
            {
                auto track = getTrack();
                track->tripletMode = constrain(track->tripletMode + amtSlow, 0, 1);
                onRateChanged();
            }
            else if (param == 3)
            {
            }
        }
        break;
        case OMNIPAGE_3:
        {
            if (param == 0)
            {
                seq_.sendMidi = constrain(seq_.sendMidi + amtSlow, 0, 1);
            }
            else if (param == 1)
            {
                seq_.sendCV = constrain(seq_.sendCV + amtSlow, 0, 1);
            }
            else if (param == 2)
            {
            }
            else if (param == 3)
            {
            }
        }
        break;
        case OMNIPAGE_TPAT: // SendMidi, SendCV
        {
        }
        break;
        }
    }



    bool FormMachineOmni::drawPage(uint8_t page)
    {
        switch (page)
        {
        case OMNIPAGE_STEP1:  // Vel, Nudge, Length, MFX
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

            if (selStep->func >= 7)
            {
                omxDisp.setLegend(2, "FUNC", "J" + String(selStep->func - 7 + 1));
            }
            else
            {
                omxDisp.setLegend(2, "FUNC", kStepFuncs[selStep->func]);
            }

            omxDisp.setLegend(3, "ACUM", selStep->accumTPat == 0, selStep->accumTPat);
        }
        return true;
        case OMNIPAGE_STEPNOTES:
        break;
        case OMNIPAGE_STEPPOTS:
        {
            omxDisp.clearLegends();

            auto selStep = getSelStep();
            // auto bank = pots[potSettings.potbank];

            omxDisp.setLegend(0, "CC1", selStep->potVals[0] < 0, selStep->potVals[0]);
            omxDisp.setLegend(1, "CC2", selStep->potVals[1] < 0, selStep->potVals[1]);
            omxDisp.setLegend(2, "CC3", selStep->potVals[2] < 0, selStep->potVals[2]);
            omxDisp.setLegend(3, "CC4", selStep->potVals[3] < 0, selStep->potVals[3]);
        }
        return true;
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

            bool drawGeneric = true;

            switch (selPage)
            {
            case OMNIPAGE_STEP1:
                drawGeneric = drawPage(OMNIPAGE_STEP1);
                break;
            case OMNIPAGE_STEPCONDITION:
                drawGeneric = drawPage(OMNIPAGE_STEPCONDITION);
                break;
            case OMNIPAGE_STEPNOTES:
                drawGeneric = drawPage(OMNIPAGE_STEPNOTES);
                break;
            case OMNIPAGE_STEPPOTS:
                drawGeneric = drawPage(OMNIPAGE_STEPPOTS);
                break;
            case OMNIPAGE_GBL1: // BPM
            {
                // auto selStep = getSelStep();

                // int8_t nudgePerc = (selStep->nudge / 60.0f) * 100;
                // omxDisp.setLegend(0, "NUDG", nudgePerc);

                // float stepLenMult = getStepLenMult(selStep->len);

                // if(stepLenMult < 1.0f)
                // {
                //     omxDisp.setLegend(1, "LEN", String(stepLenMult,2));
                // }
                // else if(stepLenMult > 16)
                // {
                //     uint8_t bar = stepLenMult / 16.0f;
                //     omxDisp.setLegend(1, "LEN", String(bar)+"br");
                // }
                // else
                // {
                //     omxDisp.setLegend(1, "LEN", String(stepLenMult, 0));
                // }

                auto track = getTrack();

                omxDisp.setLegend(0, "SWNG", track->swing);
                omxDisp.setLegend(1, "S-DV", track->swingDivision == 0 ? "16th" : "8th");
            }
            break;
            case OMNIPAGE_1: // Velocity, Channel, Rate, Gate
            {
                auto track = getTrack();
                omxDisp.setLegend(0, "LEN", track->len + 1);
                omxDisp.setLegend(1, "CHAN", seq_.channel + 1);
                omxDisp.setLegend(2, "RATE", "1/" + String(kSeqRates[seq_.rate]));
                uint8_t gateMult = getGateMult(seq_.gate) * 100;
                omxDisp.setLegend(3, "GATE", gateMult);
            }
            break;
            case OMNIPAGE_2: // Transpose, TransposeMode
            {
                auto track = getTrack();

                omxDisp.setLegend(0, "TPOS", 100);
                omxDisp.setLegend(1, "TYPE", seq_.transposeMode == 0 ? "INTR" : "SEMI");
                omxDisp.setLegend(2, "TRIP", track->tripletMode == 0 ? "OFF" : "ON");
            }
            break;
            case OMNIPAGE_3: // SendMidi, SendCV
            {
                omxDisp.setLegend(0, "MIDI", seq_.sendMidi ? "SEND" : "OFF");
                omxDisp.setLegend(1, "CV", seq_.sendCV ? "SEND" : "OFF");
            }
            break;
            case OMNIPAGE_TPAT: // SendMidi, SendCV
            {
            }
            break;
            }

            if(drawGeneric)
            {
                omxDisp.dispGenericMode2(trackParams_.getNumPages(), trackParams_.getSelPage(), trackParams_.getSelParam(), getEncoderSelect());
            }
        }
        break;
        case OMNIUIMODE_TRANSPOSE:
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
}