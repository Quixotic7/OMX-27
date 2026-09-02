#pragma once
#include "../../ClearUI/ClearUI_Input.h"
#include "../../hardware/omx_keypad.h"
#include "../../utils/param_manager.h"
#include "../../config.h"
#include "../omx_form_global.h"
#include "../../hardware/storage.h"
#include "omni_structs.h"
#include "omni_transpose_pattern.h"

namespace FormOmni
{
    // Bump whenever the OmniSeq layout changes so old saves are skipped rather than
    // blitted into a mismatched struct. Also stamps the V3 pattern-bank file.
    constexpr uint8_t kOmniSaveVersion = 8; // v8: default MIDI ch 1-8 + default velocity 100


    // FORM's one (and only) track engine — the polyphonic step sequencer. The old
    // FormMachineInterface machine abstraction is collapsed into this concrete class
    // (FORM_V2_REVIEW.md §1.1): the shell owns 8 of these directly.
    class FormMachineOmni
    {
    public:
        FormMachineOmni();
        ~FormMachineOmni();

	    void onSelected();

        // Note-out callbacks into the shell (kept from the old interface).
        void setContext(void *context) { context_ = context; }
        void setNoteOnFptr(void (*fptr)(void *, MidiNoteGroup, uint8_t)) { noteOnFuncPtr = fptr; }
        void setNoteOffFptr(void (*fptr)(void *, MidiNoteGroup, uint8_t)) { noteOffFuncPtr = fptr; }

        bool getMute();
        bool getSolo();
        void setMute(bool isMuted);
        void setSolo(bool isSoloed);
	    bool didTriggerThisStep();

        // Audibility: muted tracks — and, while any track is soloed, non-soloed tracks —
        // don't send notes (they keep advancing so unmute/unsolo re-enters in time).
        bool isAudible() { return seq_.mute == 0 && (!omxFormGlobal.anySolo || seq_.solo == 1); }
        // Send note-offs for every note this track currently has sounding (mute/solo/kill).
        void flushNotes();


        // Encoder turn: dispatch to select (navigate) or edit per getEncoderSelect().
        void onEncoderChanged(Encoder::Update enc);

        bool doesConsumePots();
        bool doesConsumeDisplay();
        bool doesConsumeKeys(); 
        bool doesConsumeLEDs(); 

	    bool getEncoderSelect();



	    void playBackStateChanged(bool newIsPlaying);
	    void resetPlayback();

	    void selectMidiFx(uint8_t mfxIndex, bool dispMsg);
	    uint8_t getSelectedMidiFX();


        // Standard Updates
        void onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta);
        void onClockTick();
        void loopUpdate();
        bool updateLEDs();
        void onEncoderButtonDown();
        bool onKeyUpdate(OMXKeypadEvent e);
        bool onKeyHeldUpdate(OMXKeypadEvent e);

        void onDisplayUpdate();

        int saveToDisk(int startingAddress, Storage *storage);
	    int loadFromDisk(int startingAddress, Storage *storage);

        // v2 shell: let the container drive the editor UI mode (silent — the container
        // shows the v2 view name).
        void setUiMode(uint8_t mode) { changeUIMode(mode, true); }

        // v2: expose the track (play mode / direction / length etc.) to the container.
        Track *trackPtr() { return getTrack(); }

        // v2 Mix F3: which 16-step page is active (for the length bar window).
        uint8_t activePage() const { return activePage_; }
        void setActivePage(uint8_t page) { activePage_ = page > 3 ? 3 : page; }

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

        // v2 length / polymeter. setTrackLen takes a flat 0-63 length and rebuilds pages;
        // setPageLen sets one page's length (1-16); setEnabledPages sets the loop bitmask.
        void setTrackLen(uint8_t len) // flat 0-63 -> enable pages 0..lastPage, rebuild lengths
        {
            Track *t = getTrack();
            if (len > 63) len = 63;
            uint8_t lastPage = len / 16;
            t->enabledPages = 0;
            for (uint8_t p = 0; p <= lastPage; p++)
            {
                t->enabledPages |= (1 << p);
                t->pageLen[p] = (p == lastPage) ? (len % 16) + 1 : 16;
            }
            t->syncLen();
            onTrackLengthChanged();
        }
        void setPageLen(uint8_t page, uint8_t len)
        {
            if (page >= 4) return;
            Track *t = getTrack();
            t->pageLen[page] = len < 1 ? 1 : (len > 16 ? 16 : len);
            t->enabledPages |= (1 << page); // setting a page's length puts it in the loop
            t->syncLen();
            onTrackLengthChanged();
        }
        void setEnabledPages(uint8_t mask)
        {
            getTrack()->enabledPages = mask & 0x0F;
            getTrack()->syncLen();
            onTrackLengthChanged();
        }
        uint8_t getEnabledPages() { return getTrack()->enabledPages; }
        uint8_t getPageLen(uint8_t page) { return page < 4 ? getTrack()->pageLen[page] : 16; }
        // v2 Mix encoder pages: gate / rate accessors (values live on the OmniSeq).
        uint8_t getGate() { return seq_.gate; }
        void editGate(int amt);
        uint8_t getRate() { return seq_.rate; }
        void editRate(int amt);       // clamps + onRateChanged
        String gateBox();             // gate as a percent string for a param cell
        // v2 Step F3: set the rate from a top-row key (0-7), like Mix F3.
        void setRateShortcut(uint8_t topKeyIndex);
        int8_t rateShortcutSel(); // which top-row key (0-7) matches the current rate, -1 if none

        // v2 Step view: read step content + playhead, and the copy/paste buffer (key16 = 0-15).
        bool stepHasNotes(uint8_t key16) { return getTrack()->steps[key16toStep(key16)].hasNotes(); }
        bool stepIsOn(uint8_t key16) { return getTrack()->steps[key16toStep(key16)].isOn(); }
        uint8_t playingStepIndex() { return getTrack()->positionToStep(playingStep_); } // absolute step
        // 0..1 fraction through the track's loop (for the Patterns switch-progress bar).
        float loopProgress()
        {
            uint16_t t = getTrack()->totalLen();
            return t > 0 ? (float)playingStep_ / (float)t : 0.0f;
        }
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
        void setParamDefaultPalette(uint8_t mode, uint8_t paletteIndex); // palette -> track default
        int16_t stepPaletteSelected(uint8_t key16, uint8_t mode);
        int16_t defaultPaletteSelected(uint8_t mode); // the DEFAULT's lit palette key (-1 = none)   // lit palette index, -1 = none
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
            if (seq_.monoPhonic) // mono tracks hold one note per step: replace, don't add
            {
                for (uint8_t i = 0; i < 6; i++) s->notes[i] = -1;
                s->notes[0] = note;
                return;
            }
            for (uint8_t i = 0; i < 6; i++) if (s->notes[i] == note) return; // dedup
            for (uint8_t i = 0; i < 6; i++) if (s->notes[i] < 0) { s->notes[i] = note; return; }
        }
        // Toggle-entry: remove one note from a step (keeps the rest).
        void stepRemoveNote(uint8_t key16, int8_t note)
        {
            Step *s = &getTrack()->steps[key16toStep(key16)];
            for (uint8_t i = 0; i < 6; i++) if (s->notes[i] == note) s->notes[i] = -1;
        }
        bool isMono() { return seq_.monoPhonic == 1; }
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

        // v2 SEQ menu handoff: after the container's custom Vel/.../Accum pages, delegate to
        // this machine's native param menu (Notes/CC/Transpose/track params, same as MIX view).
        void seqMenuEnter();       // position the menu at the first delegated page (Notes)
        void seqMenuEnterEnd();    // position it at that page's last param (entering from the right)
        bool seqMenuAtStart();     // true when on that first page/param (for the back boundary)
        // Transpose view's second page: the track's live-transpose params (SEQTPOSE).
        bool transMenuAtEnd(); // at the pattern editor's last cell (LEN)
        void transParamsDraw(uint8_t sel);      // render the SEQTPOSE grid (TPOS/TYPE/TPAT)
        void transParamsEdit(uint8_t sel, int dir);
        bool seqMenuAtEnd();       // true at the notes page's last param (Seq's forward fence)
        void mixMenuEnter();       // position the menu at the track/global pages (Mix)
        bool mixMenuAtStart();     // true at the Mix menu's first page/param (back boundary)
        // Set when the POTS machine-menu cell is clicked; the shell consumes it to open Pot Config.
        int8_t takeActionRequest() { int8_t r = actionRequested_; actionRequested_ = -1; return r; }
        void setSelStepByKey(uint8_t key16); // point step-scoped menu edits at this step

        // v2 Step menu (P-Lockable params). pid: 0 Vel,1 Nudge,2 Len,3 MFX,4 Prob,5 Cond,6 Func,7 Accum.
        static const uint8_t kStepMenuParamCount = 8;
        const char *stepParamLabel(uint8_t pid);
        String stepParamValueString2(uint8_t key16, uint8_t pid); // full value (for the popup)
        String stepParamBox(uint8_t key16, uint8_t pid);          // compact value (for the cell)
        bool stepParamWide(uint8_t pid);                          // true = show the popup while editing
        String formatParamBox(uint8_t pid, int value);           // compact formatter for a raw value
        // Track defaults (unlocked steps track these):
        String paramDefaultBox(uint8_t pid);
        void editParamDefault(uint8_t pid, int delta); // edit + push to unlocked steps
        void editStepParam(uint8_t key16, uint8_t pid, int delta); // edits value, sets its lock
        void editStepRepeat(uint8_t key16, int delta)               // ratchet count (no lock bit)
        {
            Step *s = &getTrack()->steps[key16toStep(key16)];
            s->repeat = (uint8_t)constrain((int)s->repeat + delta, 0, 3);
        }
        bool stepParamLocked(uint8_t key16, uint8_t pid);
        void clearStepParamLock(uint8_t key16, uint8_t pid); // clears the lock and resets to default

        void setChannel(uint8_t ch) { seq_.channel = ch & 0x0F; } // 0-15 -> MIDI ch 1-16
        void setPotBank(uint8_t b) { seq_.potBank = b % NUM_CC_BANKS; }
        uint8_t getPotBank() { return seq_.potBank; }
        uint8_t getChannel() { return seq_.channel; }

        // Per-step CC P-Lock: lock pot-slot `slot` (0-4) to `value` (0-127) on this step; -1 =
        // unlocked. When the step fires it sends CC potLockCC(slot) with this value.
        void setStepPotLock(uint8_t key16, uint8_t slot, int8_t value)
        {
            if (slot >= 5) return;
            Step *s = &getTrack()->steps[key16toStep(key16)];
            s->potVals[slot] = value;
            if (value >= 0) s->trig = 1; // ensure the step fires so its locked CC is sent
        }
        int8_t getStepPotLock(uint8_t key16, uint8_t slot)
        {
            return (slot < 5) ? getTrack()->steps[key16toStep(key16)].potVals[slot] : (int8_t)-1;
        }
        // ---- Tools view operations (FORM_V2_REVIEW.md; destructive, act on this track) ----
        void toolRotate(int8_t dir, bool wholeTrack);           // shift steps left/right (page or whole loop)
        void toolMirror(bool wholeTrack);                       // reverse step order (page or whole loop)
        void toolShuffle(bool wholeTrack);                      // random permutation of steps (page or whole loop)
        void toolScaleRemap(bool wholeTrack);                   // snap the scope's notes to the current scale
        void toolQuantize(uint8_t amtPct, bool wholeTrack);     // pull the scope's nudges toward the grid by amt%
        void toolChanceRnd(uint8_t pmin, uint8_t pmax);         // randomize step probability of on-steps
        void toolTranspose(int8_t semis, bool wholeTrack);      // transpose the scope's notes, clamped 0-127
        void toolRandomVel(uint8_t vmin, uint8_t vmax);         // randomize velocity of note steps
        void toolHumanize(uint8_t amtPct, bool wholeTrack);     // random nudge within +/- amt% of max
        void toolEuclid(uint8_t pulses, uint8_t rot, bool wholeTrack);       // euclidean rhythm onto the scope
        void toolGrids(uint8_t inst, uint8_t x, uint8_t y, uint8_t density, bool wholeTrack); // grids rhythm onto the scope
        // Pattern builders shared with the Tools view's live preview (return the scope length).
        uint8_t buildEuclidPattern(uint8_t pulses, uint8_t rot, bool wholeTrack, bool *pattern);
        uint8_t buildGridsPattern(uint8_t inst, uint8_t x, uint8_t y, uint8_t density, bool wholeTrack, bool *pattern, uint8_t *vels);
        int stepParamValue(uint8_t key16, uint8_t pid); // numeric step param (pid as editStepParam)

        uint8_t potLockCC(uint8_t slot); // the CC number a pot slot maps to (current pot bank)
        void sendPotCC(uint8_t slot, uint8_t val); // live CC send on the track's bank/channel

        // Live recording: the absolute step (0-63) to record a played note into — the playing
        // step, quantized to nearest (past the step's midpoint rounds up to the next step).
        void recordNoteToStep(uint8_t absStep, int8_t note); // add (dedup); fresh step -> default vel
        // Resolve where a note played *now* should record: the nearest step, plus that step's NUDGE
        // from how far off the grid it was, scaled by (100-quantizePct)/100 (100 = hard snap, 0 =
        // full micro-timing). Returns the abs step (255 if none) and the nudge via nudgeOut. Writes
        // nothing — the note is committed on release so the sequencer can't replay it mid-hold.
        uint8_t recordResolveStep(uint8_t quantizePct, int8_t &nudgeOut);
        void setStepNudge(uint8_t absStep, int8_t nudge)
        {
            if (absStep < 64)
                getTrack()->steps[absStep].nudge = (int8_t)constrain((int)nudge, -60, 60);
        }
        // Set a recorded step's LEN from how long the note was actually held (in steps).
        void recordNoteLen(uint8_t absStep, float durationSteps);
        float stepMicros() { return stepMicros_; } // micros per step at the current rate
        void clearStepNotesAbs(uint8_t absStep)
        {
            if (absStep < 64)
            {
                Step *s = &getTrack()->steps[absStep];
                for (uint8_t i = 0; i < 6; i++) s->notes[i] = -1;
            }
        }
        // Clear this track's pattern: reset every step to empty (keeps channel / rate / length etc).
        void clearTrackSteps()
        {
            Track *t = getTrack();
            for (uint8_t s = 0; s < 64; s++)
                t->steps[s].setToInit();
        }

        // ---- Per-track scale mode ----
        // 0 GLOBAL: the track follows the global scale settings (default).
        // 1 CHROMATIC: the track ignores scales entirely (keyboard chromatic, transpose in
        //   semitones, no scale colours).
        // 2 LOCAL: the track has its own root + scale (localScale_), independent of global.
        // Runtime + bank-file persisted (not in OmniSeq, so the save layout is unchanged).
        enum TrackScaleMode { TRACKSCALE_GLOBAL = 0, TRACKSCALE_CHROMATIC, TRACKSCALE_LOCAL, TRACKSCALE_COUNT };
        uint8_t getScaleMode() { return scaleMode_; }
        uint8_t getLocalRoot() { return localRoot_; }
        int8_t getLocalPattern() { return localPattern_; }
        void setScaleConfig(uint8_t mode, uint8_t root, int8_t pattern)
        {
            scaleMode_ = mode >= TRACKSCALE_COUNT ? TRACKSCALE_GLOBAL : mode;
            localRoot_ = root > 11 ? 0 : root;
            localPattern_ = pattern < -1 ? -1 : pattern;
            recalcLocalScale();
        }
        void editScaleMode(int amt);   // cycles GLOBAL/CHROMATIC/LOCAL (pops the name)
        void editScaleRoot(int amt);   // LOCAL: local root; else the global root
        void editScalePattern(int amt);// LOCAL: local pattern; else the global pattern
        // Effective scale for interval math / note palettes (never null; chromatic tracks
        // get the local instance calculated with pattern -1 = chromatic degrees).
        MusicScales *paletteScale()
        {
            return scaleMode_ == TRACKSCALE_GLOBAL ? omxFormGlobal.musicScale : &localScale_;
        }
        // Effective scale for the live keyboard (null = plain chromatic mapping, no lock/group).
        MusicScales *keyboardScale()
        {
            if (scaleMode_ == TRACKSCALE_CHROMATIC) return nullptr;
            return paletteScale();
        }
        // True when this track plays chromatically (mode chromatic, or its scale is off).
        bool scaleIsChromatic()
        {
            if (scaleMode_ == TRACKSCALE_CHROMATIC) return true;
            if (scaleMode_ == TRACKSCALE_LOCAL) return localPattern_ < 0;
            return scaleConfig.scalePattern < 0;
        }

        // Tools PAGE clipboard: read / write / clear one 16-step page (steps + page length).
        // The buffer itself lives in the shell so it can move a page between pages or tracks.
        void copyPageOut(uint8_t page, Step dst[16], uint8_t &lenOut)
        {
            Track *t = getTrack();
            uint8_t p = page > 3 ? 3 : page;
            for (uint8_t i = 0; i < 16; i++) dst[i].CopyFrom(&t->steps[p * 16 + i]);
            lenOut = t->pageLen[p];
        }
        void pastePageIn(uint8_t page, Step src[16], uint8_t len)
        {
            if (page >= 4) return;
            Track *t = getTrack();
            for (uint8_t i = 0; i < 16; i++) t->steps[page * 16 + i].CopyFrom(&src[i]);
            t->pageLen[page] = len < 1 ? 1 : (len > 16 ? 16 : len);
            t->enabledPages |= (1 << page);
            t->syncLen();
            onTrackLengthChanged();
        }
        void clearPageSteps(uint8_t page)
        {
            if (page >= 4) return;
            Track *t = getTrack();
            for (uint8_t i = 0; i < 16; i++) t->steps[page * 16 + i].setToInit();
        }

        // Live recording: the loop position that fired most recently (for nearest-step resolve).
        uint16_t lastPlayedPos() { return lastTriggeredStepIndex_; }

        // Transpose view: the pattern position currently applied (playhead).
        uint8_t transposePos() { return transpPat_.position(&seq_.transposePattern); }

        // v2 pattern data layer: snapshot / restore this track's sequencer data.
        const OmniSeq &getSeq() const { return seq_; }
        // Mix track-copy "Copy Pat": replace only the pattern (steps/pages/play mode/step
        // defaults), keeping this track's settings (channel, rate, gate, transpose, banks…).
        void setTrackData(const Track &t)
        {
            seq_.tracks[0] = t;
            sanitizeSeq();
            seqDynamic_.Reset();
            ratchetDivs_ = 0;
            ratchetStepIdx_ = -1;
            onRateChanged(); // the copied pattern's timing must not run on stale tick/micros
        }
        void setSeq(const OmniSeq &s)
        {
            seq_ = s;
            sanitizeSeq();        // clamp ranges a raw pattern blit can't guarantee (bank/switch load)
            seqDynamic_.Reset();  // loaded pattern starts from a clean playback state
            ratchetDivs_ = 0;     // and no ratchet carried over from the old pattern
            ratchetStepIdx_ = -1;
            onRateChanged();      // pattern switches used to leave ticksPerStep_/stepMicros_ at the
                                  // OLD pattern's rate — wrong step rate AND note lengths until the
                                  // rate was next edited.
        }

    private:
        // Clamp seq_ fields that a raw blit (FRAM load, LittleFS bank, pattern switch) can't
        // guarantee in range — an out-of-range rate/potBank OOB-reads kSeqRates[]/pots[].
        void sanitizeSeq();
        // Tools: gather the absolute step indices of a tool's scope (whole played loop or the
        // active page) in play order, into idx[<=64]. Returns the count. The whole-loop scope is
        // non-contiguous (disabled pages / short-page tails are skipped) — must go via positionToStep.
        uint8_t toolScopeIndices(bool wholeTrack, uint8_t *idx);
        // Tools: shared rhythm-apply for the Euclid/Grids generators (scope-mapped).
        void applyRhythmScope(const bool *pattern, const uint8_t *vels, bool wholeTrack);

        // Note-out into the shell (kept from the old interface).
        void seqNoteOn(MidiNoteGroup noteGroup, uint8_t midiFx);
        void seqNoteOff(MidiNoteGroup noteGroup, uint8_t midiFx);

        void *context_ = nullptr;
        void (*noteOnFuncPtr)(void *, MidiNoteGroup, uint8_t) = nullptr;
        void (*noteOffFuncPtr)(void *, MidiNoteGroup, uint8_t) = nullptr;

        OmniSeq seq_;
        OmniSeqDynamic seqDynamic_;

        uint8_t selStep_ = 0;

        // Notes currently sounding from a Mix step audition, per low-row key (0-15). -1 = none.
        int8_t auditionNotes_[16][6];

        uint8_t activePage_ = 0;

        // Per-track scale mode (see TrackScaleMode). localScale_ is calculated whenever the
        // mode/root/pattern change; chromatic mode keeps it calculated with pattern -1.
        uint8_t scaleMode_ = 0;    // TRACKSCALE_GLOBAL
        uint8_t localRoot_ = 0;    // 0-11
        int8_t localPattern_ = 0;  // -1 = off/chromatic
        MusicScales localScale_;
        void recalcLocalScale();

        OmniTransposePattern transpPat_;

        void onEnabled();

        void onEncoderChangedSelectParam(Encoder::Update enc);
        void onEncoderChangedEditParam(Encoder::Update enc);

        void changeUIMode(uint8_t newMode, bool silent);
        void onUIModeChanged(uint8_t prevMode, uint8_t newMode);


        void resetPlayback(bool resetTickCounters);

        // returns true if should draw generic page
        void editPage(uint8_t page, uint8_t param, int8_t amtSlow, int8_t amtFast);
        bool drawPage(uint8_t page, uint8_t selParam);
        // Shared 4-cell param-grid renderer for the menu's plain param pages (§4 label rules).
        void dispParamGrid(const char *labels[4], const String vals[4], uint8_t selParam);

        Track *getTrack();
        Step *getSelStep();

        ParamManager *getParams();

        void ensureParamsInit(); // populates shared param pages at runtime (static-init-safe)

        TrackDynamic *getDynamicTrack();

        uint8_t key16toStep(uint8_t key16);


        Step bufferedStep_; 

        // Key index is 0-15
        void copyStep(uint8_t keyIndex);
        void cutStep(uint8_t keyIndex);
        void pasteStep(uint8_t keyIndex);

        uint8_t playingStep_;

        int8_t pongDir_ = 1; // runtime pong direction (+1/-1); keeps track->playDirection as the set intent

        bool prevCondWasTrue_ = false;
        bool fillActive_ = false;
        int8_t actionRequested_ = -1; // ACTIONS menu cell clicked (0 QNT / 1 CLR / 2 POTS); -1 = none
        bool firstLoop_ = false;

        bool setDirtyOnceMessageClears_ = false;

        // Counts from 0 to 16 during playback to determine groove
        uint8_t grooveCounter_ = 0;

        // Counts from 0 to track length to determine when the track has looped
        uint8_t loopCounter_ = 0;

        // Starts on playingStep_ but then counts from 0 to track length or in reverse depending on play direction
        uint8_t shuffleCounter_ = 0;

        // Increments everytime track loops
        uint16_t loopCount_ = 0;

        uint8_t lastTriggeredStepIndex_ = 0;
        bool didNotesPlayThisStep_ = false;

        std::vector<uint8_t> shuffleVec;
        std::vector<uint8_t> tempShuffleVec;

        Micros stepMicros_;

        uint16_t ticksPerStep_ = 24;

        int16_t ticksTilNextTrigger_ = 0;

        int16_t ticksTilNext16Trigger_ = 0; // Keeps track of ticks to quantized next 16th

        int16_t ticksTilNextTriggerRate_ = 0;

        // Step repeat (ratchet): after a step with repeat>0 triggers, re-fire it so the step is
        // split into `ratchetDivs_` (= repeat+1) even sub-hits. Each sub-hit's tick is recomputed
        // as round(k * total / divs) so integer rounding doesn't drift the later hits.
        // Stored as an absolute step index (not pointers into seq_): a clear/paste/pattern
        // switch during playback re-initialises steps in place, which would leave a raw
        // pointer aimed at reused memory mid-ratchet.
        int8_t ratchetStepIdx_ = -1;   // absolute step (0-63), -1 = none
        uint8_t ratchetDivs_ = 0;      // subdivisions (repeat+1); 0/1 = inactive
        uint8_t ratchetHitIndex_ = 0;  // next sub-hit to fire (1 .. divs-1)
        int16_t ratchetTotalTicks_ = 0;
        int16_t ratchetElapsed_ = 0;

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
        void triggerStep(Step *step, StepDynamic *stepDynamic, bool ratchetHit = false);

        // char foo[sizeof(PotPickupUtil)]
    };
}