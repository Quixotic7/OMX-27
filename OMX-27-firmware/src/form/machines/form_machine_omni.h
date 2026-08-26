#pragma once
#include "form_machine_interface.h"
#include "omni_structs.h"
#include "omni_transpose_pattern.h"

namespace FormOmni
{

    // Very powerful step sequencer
    class FormMachineOmni : public FormMachineInterface
    {
    public:
        FormMachineOmni();
        ~FormMachineOmni();

	    void onSelected();

        FormMachineType getType() { return FORMMACH_OMNI; }
        FormMachineInterface *getClone() override;

        bool getMute() override;
        bool getSolo() override;
        void setMute(bool isMuted) override;
        void setSolo(bool isSoloed) override;
	    bool didTriggerThisStep() override;


        bool doesConsumePots() override;
        bool doesConsumeDisplay() override;
        bool doesConsumeKeys() override; 
        bool doesConsumeLEDs() override; 

	    const char* getF3shortcutName() override;
	    bool getEncoderSelect() override;


        void setTest() override;

	    void playBackStateChanged(bool newIsPlaying) override;
	    void resetPlayback() override;

	    void selectMidiFx(uint8_t mfxIndex, bool dispMsg) override;
	    uint8_t getSelectedMidiFX() override;


        // Standard Updates
        void onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta) override;
        void onClockTick() override;
        void loopUpdate() override;
        bool updateLEDs() override;
        void onEncoderButtonDown() override;
        bool onKeyUpdate(OMXKeypadEvent e) override;
        bool onKeyHeldUpdate(OMXKeypadEvent e) override;
	    bool onKeyQuickClicked(OMXKeypadEvent e) override;

        void onDisplayUpdate() override;

        // AUX + Top 1 = Play Stop
        // For Omni:
        // AUX + Top 2 = Reset
        // AUX + Top 3 = Flip play direction if forward or reverse
        // AUX + Top 4 = Increment play direction mode
        void onAUXFunc(uint8_t funcKey) override;

        int saveToDisk(int startingAddress, Storage *storage) override;
	    int loadFromDisk(int startingAddress, Storage *storage) override;

        // v2 shell: let the container drive the editor UI mode (silent — the container
        // shows the v2 view name).
        void setUiMode(uint8_t mode) { changeUIMode(mode, true); }

        // v2: expose the track (play mode / direction / length etc.) to the container.
        Track *trackPtr() { return getTrack(); }

        // v2 Mix F3: which 16-step page is active (for the length bar window).
        uint8_t activePage() const { return activePage_; }

        // v2 Mix: audition (preview) a step's programmed notes. key16 = 0-15 (mapped to the
        // active page's step). on=true = note-on, on=false = note-off.
        void auditionStep(uint8_t key16, bool on);

        // v2 Mix (F1 + low row): toggle / read a step's mute. key16 = 0-15 (active page).
        void toggleStepMute(uint8_t key16)
        {
            uint8_t s = key16toStep(key16);
            getTrack()->steps[s].mute = !getTrack()->steps[s].mute;
        }
        bool getStepMute(uint8_t key16) { return getTrack()->steps[key16toStep(key16)].mute; }

        // v2 Mix (hold F2): momentary FILL — steps with a Fill condition play while active.
        void setFill(bool on) { fillActive_ = on; }

        // v2 Step view: read step content + playhead, and the copy/paste buffer (key16 = 0-15).
        bool stepHasNotes(uint8_t key16) { return getTrack()->steps[key16toStep(key16)].hasNotes(); }
        bool stepIsOn(uint8_t key16) { return getTrack()->steps[key16toStep(key16)].isOn(); }
        uint8_t playingStepIndex() { return playingStep_; }
        // Function mode: is the step currently a jump (random J? or a specific jump target)?
        bool stepIsJump(uint8_t key16)
        {
            Step *s = &getTrack()->steps[key16toStep(key16)];
            return s->func == STEPFUNC_RANDJUMP || s->func >= STEPFUNC_COUNT;
        }
        // Function mode: point the held step's jump function at another step (key16 = 0-15).
        void setStepJumpTarget(uint8_t key16, uint8_t targetKey16)
        {
            Step *s = &getTrack()->steps[key16toStep(key16)];
            s->func = STEPFUNC_COUNT + key16toStep(targetKey16);
            s->trig = 1;
            s->setLock(SLOCK_FUNC);
        }
        void stepCopy(uint8_t key16) { copyStep(key16); }
        void stepCut(uint8_t key16) { cutStep(key16); }
        void stepPaste(uint8_t key16) { pasteStep(key16); }

        // v2 Step value palettes. `mode` matches StepMode in omx_mode_form.h
        // (1 Vel, 2 Length, 3 Repeat, 4 Chance, 5 Math, 6 Func, 7 MFX; 0 Note = elsewhere).
        uint8_t stepPaletteCount(uint8_t mode);                     // palette keys used (0 = n/a)
        void setStepPalette(uint8_t key16, uint8_t mode, uint8_t paletteIndex);
        int16_t stepPaletteSelected(uint8_t key16, uint8_t mode);   // lit palette index, -1 = none
        String stepValueString(uint8_t key16, uint8_t mode);        // current value, for the OLED
        void resetStepValue(uint8_t key16, uint8_t mode);           // AUX = reset to default
        // Note mode: strip a step's notes but keep it on as a ghost trigger.
        void stepNotesToGhost(uint8_t key16)
        {
            Step *s = &getTrack()->steps[key16toStep(key16)];
            for (uint8_t i = 0; i < 6; i++) s->notes[i] = -1;
            s->trig = 1;
        }

        // Note-mode chord entry helpers (key16 = 0-15).
        void stepClearNotes(uint8_t key16)
        {
            Step *s = &getTrack()->steps[key16toStep(key16)];
            for (uint8_t i = 0; i < 6; i++) s->notes[i] = -1;
        }
        void stepAddNote(uint8_t key16, int8_t note)
        {
            if (note < 0 || note > 127) return;
            Step *s = &getTrack()->steps[key16toStep(key16)];
            for (uint8_t i = 0; i < 6; i++) if (s->notes[i] == note) return; // dedup
            for (uint8_t i = 0; i < 6; i++) if (s->notes[i] < 0) { s->notes[i] = note; return; }
        }
        void stepSetNotes(uint8_t key16, const int8_t src[6])
        {
            Step *s = &getTrack()->steps[key16toStep(key16)];
            for (uint8_t i = 0; i < 6; i++) s->notes[i] = src[i];
        }
        void getStepNotes(uint8_t key16, int8_t dst[6])
        {
            Step *s = &getTrack()->steps[key16toStep(key16)];
            for (uint8_t i = 0; i < 6; i++) dst[i] = s->notes[i];
        }
        bool stepHasNote(uint8_t key16, int8_t note)
        {
            Step *s = &getTrack()->steps[key16toStep(key16)];
            for (uint8_t i = 0; i < 6; i++) if (s->notes[i] == note) return true;
            return false;
        }
        // Audition a single note (chord-entry preview): on = note-on, off = note-off.
        void previewNote(int8_t note, bool on);
        // Math introspection for LEDs: returns kind (0 other, 1 Fill, 2 !Fill, 3 ratio) and,
        // for a ratio, the A/B components (1-8) via the out params.
        uint8_t stepMathInfo(uint8_t key16, uint8_t &a, uint8_t &b);

        // v2 Step menu (P-Lockable params). pid: 0 Vel,1 Nudge,2 Len,3 MFX,4 Prob,5 Cond,6 Func,7 Accum.
        static const uint8_t kStepMenuParamCount = 8;
        const char *stepParamLabel(uint8_t pid);
        String stepParamValueString2(uint8_t key16, uint8_t pid); // full value (for the popup)
        String stepParamBox(uint8_t key16, uint8_t pid);          // compact value (for the cell)
        bool stepParamWide(uint8_t pid);                          // true = show the popup while editing
        void editStepParam(uint8_t key16, uint8_t pid, int delta); // edits value, sets its lock
        bool stepParamLocked(uint8_t key16, uint8_t pid);
        void clearStepParamLock(uint8_t key16, uint8_t pid); // clears the lock and resets to default

        // v2 pattern data layer: snapshot / restore this track's sequencer data.
        const OmniSeq &getSeq() const { return seq_; }
        void setSeq(const OmniSeq &s)
        {
            seq_ = s;
            seqDynamic_.Reset(); // loaded pattern starts from a clean playback state
        }

    private:
        OmniSeq seq_;
        OmniSeqDynamic seqDynamic_;

        uint8_t selStep_ = 0;
        bool stepHeld_ = false;

        // Notes currently sounding from a Mix step audition, per low-row key (0-15). -1 = none.
        int8_t auditionNotes_[16][6];

        uint8_t activePage_ = 0;
        uint8_t zoomLevel_ = 0;

        OmniTransposePattern transpPat_;

        void onEnabled();
        void onDisabled();

        void onEncoderChangedSelectParam(Encoder::Update enc);
        void onEncoderChangedEditParam(Encoder::Update enc);

        void changeUIMode(uint8_t newMode, bool silent);
        void onUIModeChanged(uint8_t prevMode, uint8_t newMode);

        void setPotPickups(uint8_t page);

        void resetPlayback(bool resetTickCounters);

        // returns true if should draw generic page
        void editPage(uint8_t page, uint8_t param, int8_t amtSlow, int8_t amtFast);
        bool drawPage(uint8_t page, uint8_t selParam);

        Track *getTrack();
        Step *getSelStep();

        ParamManager *getParams();

        void ensureParamsInit(); // populates shared param pages at runtime (static-init-safe)

        TrackDynamic *getDynamicTrack();

        uint8_t key16toStep(uint8_t key16);

        void selStep(uint8_t stepIndex); // 0-15
        void stepHeld(uint8_t key16Index); // 0-15
        void stepReleased(uint8_t key16Index);

        Step bufferedStep_; 

        // Key index is 0-15
        void copyStep(uint8_t keyIndex);
        void cutStep(uint8_t keyIndex);
        void pasteStep(uint8_t keyIndex);

        uint8_t playingStep_;

        bool prevCondWasTrue_ = false;
        bool fillActive_ = false;
        bool firstLoop_ = false;

        bool setDirtyOnceMessageClears_ = false;

        // Counts from 0 to 16 during playback to determine groove
        uint8_t grooveCounter_ = 0;

        uint8_t playRateCounter_ = 0;

        // Counts from 0 to track length to determine when the track has looped
        uint8_t loopCounter_ = 0;

        // Starts on playingStep_ but then counts from 0 to track length or in reverse depending on play direction
        uint8_t shuffleCounter_ = 0;

        // Increments everytime track loops
        uint16_t loopCount_ = 0;

        uint8_t lastTriggeredStepIndex_ = 0;
        bool lastTriggeredStepState_ = false;
        bool didNotesPlayThisStep_ = false;

        // Each slot points to a step index
        // uint8_t shufflePattern[64];

        std::vector<uint8_t> shuffleVec;
        std::vector<uint8_t> tempShuffleVec;

        // static inline bool
		// shuffleSortFunc(uint8_t a1, uint8_t a2)
		// {
		// 	return (rand() % 100) > 50;
		// }

        Micros nextStepTime_;

        Micros stepMicros_;

        uint16_t ticksPerStep_ = 24;

        uint16_t omniTick_ = 0;

        int16_t ticksTilNextTrigger_ = 0;

        int16_t ticksTilNext16Trigger_ = 0; // Keeps track of ticks to quantized next 16th

        int16_t ticksTilNextTriggerRate_ = 0;

        float stepLengthMult_ = 1.0f; // 1 is a 16th note, 0.5 a 32nd note length, recalculated with the rate

        std::vector<OmniTriggeredNoteTracker> triggeredNotes_;

        std::vector<OmniNoteTracker> noteOns_;

        void onRateChanged();
        void onTrackLengthChanged();

        float getStepLenMult(uint8_t len);
        String getStepLenString(uint8_t len);

        float getGateMult(uint8_t gate);
        uint8_t getRestartPos();
        const char* getCondChar(uint8_t condIndex);


        MidiNoteGroup step2NoteGroup(uint8_t noteIndex, Step *step);
        bool evaluateTrig(uint8_t stepIndex, Step *step);

        int8_t processPlayMode(uint8_t currentStepIndex, uint8_t playmodeIndex);
        void calculateShuffle();

        // returns index of next step
        int8_t processStepFunction(uint8_t functionIndex);

        int8_t applyTranspose(int noteNumber, Step *step, StepDynamic *stepDynamic);
        void triggerStep(Step *step, StepDynamic *stepDynamic);

        // char foo[sizeof(PotPickupUtil)]
    };
}