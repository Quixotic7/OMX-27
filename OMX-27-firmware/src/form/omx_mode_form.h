#pragma once

#include "../modes/omx_mode_interface.h"
#include "../utils/aux_macro_manager.h"
#include "../utils/param_manager.h"
#include "../modes/submodes/submode_midifxgroup.h"
#include "../modes/submodes/submode_potconfig.h"
#include "../modes/submodes/submode_preset.h"
#include "../midifx/midifx_interface.h"
#include "machines/form_machine_omni.h"
#include "form_patterns.h"

// FORM v2: an 8-track polyphonic step sequencer (single engine — the OMNI machine),
// six views on the AUX layer, patterns, live recording. Design: design/form/FORM_REDESIGN.md
// (v1 machine-type spec history lives in git and design/form/FORM_DESIGN.md).

// v2 shell: the six top-level views, switched on the AUX layer (AUX + keys 13-18).
// The four editor views map to the OMNI machine's UI modes; Patterns + MI are rendered
// by the container itself.
enum FormView
{
	FORMVIEW_MIX,       // AUX+13
	FORMVIEW_STEP,      // AUX+14
	FORMVIEW_TRANSPOSE, // AUX+15
	FORMVIEW_NOTES,     // AUX+16
	FORMVIEW_PATTERNS,  // AUX+17 (container-rendered)
	FORMVIEW_MI,        // AUX+18 (container-rendered live-play keyboard)
	FORMVIEW_TOOLS,     // AUX+19 (container-rendered pattern tools: rotate/transpose/
	                    //         vel-randomize/humanize/euclid/grids generators)
	FORMVIEW_COUNT
};

// Step-view edit modes: top row keys 3-10 select one. Each edits one Step field.
enum StepMode
{
	STEPMODE_NOTE,   // notes[6]      (chord entry)
	STEPMODE_VEL,    // vel
	STEPMODE_LENGTH, // len
	STEPMODE_REPEAT, // repeat (ratchet)
	STEPMODE_CHANCE, // prob
	STEPMODE_MATH,   // condition (conditional trig)
	STEPMODE_FUNC,   // func
	STEPMODE_MFX,    // mfxIndex
	STEPMODE_COUNT
};

class OmxModeForm : public OmxModeInterface
{
public:
	OmxModeForm();
	~OmxModeForm();

	void InitSetup() override;
	void onModeActivated() override;
	void onModeDeactivated() override;

	void onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta) override;
	void loopUpdate(Micros elapsedTime) override;
	void onClockTick() override;

	void updateLEDs() override;

	void onEncoderChanged(Encoder::Update enc) override;
	void onEncoderButtonDown() override;
	void onEncoderButtonUp() override;

	void onEncoderButtonDownLong() override;
	void onEncoderButtonUpLong() override;

	bool shouldBlockEncEdit() override;

	void onKeyUpdate(OMXKeypadEvent e) override;
	void onKeyHeldUpdate(OMXKeypadEvent e) override;

	void onDisplayUpdate() override;
	void inMidiNoteOn(byte channel, byte note, byte velocity) override;
	void inMidiNoteOff(byte channel, byte note, byte velocity) override;
	void inMidiControlChange(byte channel, byte control, byte value) override;

	void SetScale(MusicScales *scale);

	int saveToDisk(int startingAddress, Storage *storage);
	int loadFromDisk(int startingAddress, Storage *storage);

	// ---- Patterns (v2 data layer) ----
	uint8_t getActivePattern() const { return activePattern_; }
	FormOmni::FormMachineOmni *getSelectedMachine();
	uint8_t getPatternCount() const { return FORM_NUM_PATTERNS; }
	// Snapshot the current pattern, then make `index` active and load it into the machines.
	void switchPattern(uint8_t index);
	void copyPatternTo(uint8_t from, uint8_t to);
	void clearPattern(uint8_t index);
	bool patternHasContent(uint8_t index); // any track has any step with notes (snapshots if active)
	// Pattern-bank persistence. The bank (~165 KB) exceeds FRAM (32 KB), so on the
	// RP2040 (V3) it lives in the LittleFS flash filesystem; Teensy builds persist only
	// the active pattern (via the Storage blit) — a known per-platform limitation.
	void saveBankToFS();     // no-op off-RP2040
	bool loadBankFromFS();   // false when absent/mismatched/off-RP2040

private:
	// ---- v2 shell: view router ----
	uint8_t formView_ = FORMVIEW_MIX;
	// While AUX is held, tapping a view key selects a pending view (preview + message);
	// the switch is committed on AUX release so the AUX overlay stays up while you browse.
	uint8_t pendingView_ = FORMVIEW_MIX;
	// silent = live switch (e.g. the CLEAR submenu returning): no popup.
	void setFormView(uint8_t view, bool silent = false);
	bool viewEditActive();   // AUX held = view browsing live (the view tag boxes)
	void updateAuxViewLEDs(); // paint the view selector (keys 13-18) on the AUX overlay
	// Container-rendered views:
	// Patterns view: switch style (top row 3-6) governs WHEN a tapped slot takes over.
	// 0 Finish Loop (at the selected track's loop end) · 1 Next Bar · 2 Instant · 3 Chained.
	uint8_t switchStyle_ = 2;      // default Instant
	int8_t queuedPattern_ = -1;    // pattern queued to switch at the boundary (-1 = none)
	uint16_t lastBarTick_ = 0;     // for detecting the bar boundary (currentClockTick wrap)
	uint8_t chain_[16] = {0};      // Chained mode: the pattern sequence
	uint8_t chainLen_ = 0;
	uint8_t chainPos_ = 0;
	FormPattern *patternBuffer_ = nullptr; // F1 copy / F2 paste buffer (lazily heap-allocated so
	                                       // the ~pattern-sized block stays out of .bss on Teensy)
	bool patF1Used_ = false;       // F1 used as a modifier (jump) this hold -> no quick-copy
	bool patF2Used_ = false;
	void updatePatternsLEDs();
	void onKeyUpdatePatterns(OMXKeypadEvent e);
	void onDisplayPatterns();
	// MI view — a live-play keyboard over the running sequencer (§4.6). The encoder navigates a
	// small menu: 0 = keyboard · 1-4 = Scale (root/scale/lock/group) · 5-8 = Track (chan/vel/
	// bank/oct). Click toggles select (turn = move) vs edit (turn = change value).
	uint8_t miCursor_ = 0;
	// QUANTIZE submenu (cursor 9): click enters, turn morphs the amount + previews it live on the
	// track (from a snapshot of the original nudges), click applies, AUX exits (restores original).
	bool miQuantSub_ = false;
	uint8_t quantWork_ = 0;         // the amount being scrubbed in the submenu
	int8_t quantOrigNudges_[64] = {}; // snapshot to morph from / restore on cancel
	void quantEnterSubmenu();
	void quantMorphPreview();       // apply quantWork_ to the track from the snapshot (preview)
	void quantExitSubmenu(bool apply);
	bool miClearSub_ = false;       // CLEAR (cursor 10) confirm submenu (reuses the Yes/No combo)
	uint8_t clearSel_ = 0;          // 0 = NO, 1 = YES
	int8_t clearReturnView_ = -1;   // view to restore when the CLEAR submenu was opened from elsewhere
	void closeClearSub();           // close the submenu, returning to the view it was opened from
	void onKeyUpdateMI(OMXKeypadEvent e);
	void updateMILEDs();
	void onDisplayMI();
	bool onEncoderMI(int dir);      // encoder turn in the MI view. consumed?
	bool onEncoderButtonMI();       // encoder click in the MI view. consumed?
	// ---- Notes view (container-rendered chord editor with in-editor step nav) ----
	uint8_t notesSelStep_ = 0;   // step being edited (0-15, active-page-relative)
	// Param-palette holds (no F-key): hold 11 = velocity, 12 = length, 11+12 = math, 13 = chance;
	// top row 1-10 sets the value. A quick tap of 11/12 navigates prev/next instead.
	bool notesPaletteEngaged_ = false; // a palette hold (11/12/13, no F-key) is active
	uint8_t notesHoldMask_ = 0;        // explicit held-bits for 11/12/13 (keyState is stale on release)
	bool notesModalHeld_ = false;      // any modal key (1/2/11/12/13) is held (drives the popup delay)
	uint32_t notesHoldStartMs_ = 0;    // when the modal hold began (for the popup delay)
	bool notesHoldUIShown_ = false;    // the hold popup has engaged (past the delay or edited)
	bool notesSuppressPrev_ = false;   // key 11 was used as a palette hold (suppress its nav)
	bool notesSuppressNext_ = false;   // key 12 was used as a palette hold (suppress its nav)
	bool notesF1Used_ = false;         // F1 was used as a modifier this hold (suppresses quick-copy)
	bool notesF2Used_ = false;         // F2 was used as a modifier this hold (suppresses quick-paste)
	int8_t notesPaletteMode();         // active palette STEPMODE from held 11/12/13, -1 = none
	// Encoder-navigated pages (flat cursor): 0 = keyboard, 1-6 = the 6 note slots + 7 = names/
	// numbers switch (Seq STEPNOTES page), 8-11 = scale (root/scale/lock/group), 12-15 = step
	// params A (vel/nudge/len/mfx), 16-19 = step params B (prob/cond/func/accum). Click toggles
	// select (turn = move cursor) vs edit (turn = change value).
	uint8_t notesCursor_ = 0;
	bool onEncoderNotes(int dir);       // encoder turn in the Notes view. consumed?
	bool onEncoderButtonNotes();        // encoder click in the Notes view. consumed?
	void notesEditScaleParam(uint8_t param, int dir); // 0 root · 1 scale · 2 lock · 3 group
	void onKeyUpdateNotes(OMXKeypadEvent e);
	void updateNotesLEDs();
	void onDisplayNotes();
	void notesSetChordFromHeld(); // build the selected step's chord from the held piano keys
	// Live recording: record a played note into the selected track's nearest playing step, keeping
	// its micro-timing (nudge) and length. recQuantize_ (0-100%) pulls timing toward the grid.
	uint64_t recClearedMask_ = 0; // steps cleared this record pass (replace mode); reset on loop wrap
	uint8_t recQuantize_ = 0;     // 0 = keep played timing, 100 = hard snap to the grid
	void recordPlayedNote(int8_t note);
	void recordNoteReleased(int8_t note); // commit the held note (step/nudge/length) on release
	void flushRecHeld();                  // commit any still-held recording notes (on stop)
	// Notes being held while recording. The note is committed to its step ON RELEASE (with the
	// step/nudge — and the track — resolved at press time), so the sequencer can't replay it and
	// cut the live note, and switching tracks mid-hold can't re-target the write.
	struct RecHeld { int8_t note; uint8_t step; int8_t nudge; uint8_t track; uint32_t onMicros; };
	RecHeld recHeld_[8] = {};
	uint8_t recHeldCount_ = 0;
	void commitRecHeld(const RecHeld &h); // write one held note (note/nudge/length) into its step
	// Preview-note bookkeeping, per key: which note (and machine) a key's preview note-on started,
	// so the matching note-off always goes out — even if AUX/a modifier swallows the key's release
	// event for the view, or the octave/track/view changed while the key was held.
	int8_t previewNote_[27];
	uint8_t previewMach_[27];
	void previewKeyOn(uint8_t key, int8_t note); // note-on on the selected track, remembered per key
	int8_t previewKeyOff(uint8_t key);           // send the pending note-off (if any); returns the note or -1
	// CC meter is transient: show it only briefly after a knob moves.
	uint32_t lastPotMs_ = 0;
	bool ccMeterWasActive_ = false;
	bool ccMeterActive() { return lastPotMs_ != 0 && (millis() - lastPotMs_) < 1000; }
	// ---- Step view (container-rendered v2 editor) ----
	uint8_t stepEditMode_ = STEPMODE_NOTE; // which of the 8 modes is active (top row 3-10)
	uint16_t heldStepMask_ = 0;            // bitmask of step keys (0-15) held right now
	int8_t heldStepKey_ = -1;              // most-recently-pressed held step (focus for display), -1 = none
	bool stepEdited_ = false;              // a palette value was set during this hold (suppresses clear)
	uint32_t stepHoldStartMs_ = 0;         // when the current step-hold began (for the display delay)
	bool stepHoldUIShown_ = false;         // the hold-step UI has engaged (past the delay or edited)
	uint16_t heldNoteKeys_ = 0;            // Note mode: note-palette keys (degree 0-9) held right now
	int8_t lastNotes_[6] = {60, -1, -1, -1, -1, -1}; // last chord entered; defaults to middle C
	// Step menu cursor: page 0 = overview (palettes on hold); pages 1-2 = P-Lockable param
	// pages (STEP: Vel/Nudge/Len/MFX, TRIG: Prob/Cond/Func/Accum), selMenu = param 0-3.
	uint8_t stepMenuPage_ = 0;
	uint8_t stepMenuSel_ = 0;
	uint8_t heldPageMask_ = 0;      // F1 + page keys currently held (for loop-range gesture)
	bool pageGestureDone_ = false;  // a range gesture consumed this F1+page press group
	void onKeyUpdateStep(OMXKeypadEvent e);
	bool onEncoderStep(Encoder::Update enc);   // returns true if the Step view consumed the encoder
	bool onEncoderButtonStep();                // returns true if consumed
	void onDisplayStepMenu();                  // render a param page (held step values / defaults)
	// Render the page-1 track overview (+ F1/F2 overlay). keyboardMode (MI view) hides the page
	// icons + step row and suppresses the F1/F2 overlays.
	void onDisplaySeqTrackPage(bool keyboardMode = false);
	void stepApplyToHeld(uint8_t paletteIndex); // set the palette value on every held step
	void updateStepLEDs();
	void onDisplayStep();
	void onKeyUpdateMix(OMXKeypadEvent e);     // Mix-view track keys (mute/solo/select)
	void onKeyUpdateMixHold(OMXKeypadEvent e); // low-row per-track controls while holding a track
	void onKeyUpdateMixStep(OMXKeypadEvent e);     // low-row taps audition the selected track's steps
	void onKeyUpdateMixStepMute(OMXKeypadEvent e); // F1 + low-row toggles the selected track's step mutes
	void updateMixHoldLEDs();                  // paint those controls on the low row
	int8_t heldTrackKey_ = -1; // track key held right now in Mix (for K5 hue), -1 = none
	// Mix encoder pages (flat cursor): 0 = track overview, 1-8 = LEVELS (per-track
	// default-velocity mixer), 9-12 = TRACK (Mute/Solo/Gate/Rate). Click = select/edit.
	uint8_t mixCursor_ = 0;
	// Last CC value sent per pot slot (by a knob turn or the Mix CC page's encoder).
	// The CC page edits/shows this — potSettings.analogValues can't hold an encoder
	// edit because the physical pot scan rewrites it every loop.
	uint8_t ccLastSent_[5] = {0, 0, 0, 0, 0};
	// Low-row steps held in Mix (audition): while held, the CC page shows/edits that
	// step's CC P-Locks (knob turns lock too, like the Step view's hold-step gesture).
	uint16_t mixHeldStepMask_ = 0;
	int8_t mixHeldStepKey_ = -1; // most recent held step (focus for the lock display)
	bool onEncoderMix(int dir); // encoder turn in the Mix view. consumed?
	void onDisplayMix();        // render the Mix view's encoder pages
	// ---- Tools view (AUX+19): each menu page is a tool; keys 3-10 are that tool's
	// action buttons; the low row auditions steps (like Mix). All tools act on the
	// selected track. The encoder walks (toolIndex_, toolCell_): the cursor only stops on
	// a tool's real cells (kToolCells[]) and crossing a tool boundary pops its name.
	uint8_t toolIndex_ = 0;
	uint8_t toolCell_ = 0;
	// Tool params (persist while in the mode):
	bool toolScopeAll_ = false;              // ROTATE: whole loop vs active page
	uint8_t toolVelMin_ = 64, toolVelMax_ = 127;
	uint8_t toolEucPulses_ = 4, toolEucRot_ = 0;
	uint8_t toolGridsInst_ = 0, toolGridsX_ = 128, toolGridsY_ = 128, toolGridsDens_ = 128;
	uint8_t toolHumAmt_ = 15;                // HUMANIZE: % of max nudge
	uint8_t toolChanceMin_ = 50, toolChanceMax_ = 100; // CHANCE RND: probability range
	uint8_t toolQuantAmt_ = 100;             // QUANTIZE: % pull toward the grid
	void onKeyUpdateTools(OMXKeypadEvent e);
	bool onEncoderTools(int dir);
	void updateToolsLEDs();
	void onDisplayTools();

	static const uint8_t kNumMachines = 8;

	SubModePreset presetManager;

	bool initSetup = false;

	bool stopped_ = true;

	// If true, encoder selects param rather than modifies value
	// bool encoderSelect = false;
	// void onEncoderChangedSelectParam(Encoder::Update enc);
	ParamManager params;

	AuxMacroManager auxMacroManager_;

	uint8_t selectedMachine_;

	Micros ledUpdateTime_;
	int16_t lastPlayheadStep_ = -1; // selected track's last-rendered playing step (for refresh-on-advance)

	// uint8_t copiedMachineIndex_;


	FormOmni::FormMachineOmni *machines_[kNumMachines];

	// Per-track colour (hue): hold a track + turn K5 in Mix. Container-level for now
	// (not yet persisted with the pattern).
	uint8_t trackHue_[kNumMachines];


	// v2 pattern data layer: the bank of whole-sequencer snapshots. The active pattern is
	// live in the machines; the others sit here. See form_patterns.h + FORM_IMPLEMENTATION.md.
	FormPattern patterns_[FORM_NUM_PATTERNS];
	uint8_t activePattern_ = 0;

	// Copy the 8 machines' live seq data into patterns_[activePattern_].
	void snapshotActivePattern();
	// Load patterns_[index] into the 8 machines.
	void loadPatternIntoMachines(uint8_t index);

	bool isMachineValid(uint8_t machineIndex);


	void selectMachine(uint8_t machineIndex);




	// char foo[sizeof(auxMacroManager_)]

	// bool macroActive_ = false;
	// bool mfxQuickEdit_ = false;
	// uint8_t quickEditMfxIndex_ = 0;

	void cleanup();

	void updateShortcutMode();

	bool getEncoderSelect();

	// SubModes
	// SubmodeInterface *activeSubmode = nullptr;
	// SubModePotConfig subModePotConfig_;

	// void enableSubmode(SubmodeInterface *subMode);
	// void disableSubmode();
	// bool isSubmodeEnabled();

	bool onKeyUpdateSelMidiFX(OMXKeypadEvent e);
	bool onKeyHeldSelMidiFX(OMXKeypadEvent e);

	// Index 0-4 = MidiFX 1 - 5, >= 5 is off
    void seqNoteOn(MidiNoteGroup noteGroup, uint8_t midifx);
	// Index 0-4 = MidiFX 1 - 5, >= 5 is off
    void seqNoteOff(MidiNoteGroup noteGroup, uint8_t midifx);

	static void seqNoteOnForwarder(void *context, MidiNoteGroup note, uint8_t midifx)
	{
		static_cast<OmxModeForm *>(context)->seqNoteOn(note, midifx);
	}

	static void seqNoteOffForwarder(void *context, MidiNoteGroup note, uint8_t midifx)
	{
		static_cast<OmxModeForm *>(context)->seqNoteOff(note, midifx);
	}

    void doNoteOn(uint8_t keyIndex);
    void doNoteOff(uint8_t keyIndex);

	// Static glue to link a pointer to a member function
	static void onNotePostFXForwarder(void *context, MidiNoteGroup note)
	{
		// Serial.println("onNotePostFXForwarder " + String(note.noteNumber));
		static_cast<OmxModeForm *>(context)->onNotePostFX(note);
	}

	void onNotePostFX(MidiNoteGroup note);

	// Static glue to link a pointer to a member function
	static void onPendingNoteOffForwarder(void *context, int note, int channel)
	{
		static_cast<OmxModeForm *>(context)->onPendingNoteOff(note, channel);
	}

	void onPendingNoteOff(int note, int channel);

	void togglePlayback();
	void resetPlayback();

	void stopSequencers();

	void selectMidiFx(uint8_t mfxIndex, bool dispMsg);

	// uint8_t mfxIndex_ = 0;

	void saveKit(uint8_t saveIndex);
	void loadKit(uint8_t loadIndex);

	static void doSaveKitForwarder(void *context, uint8_t kitIndex)
	{
		static_cast<OmxModeForm *>(context)->saveKit(kitIndex);
	}

	static void doLoadKitForwarder(void *context, uint8_t kitIndex)
	{
		static_cast<OmxModeForm *>(context)->loadKit(kitIndex);
	}

	// Static glue to link a pointer to a member function
	static void doNoteOnForwarder(void *context, uint8_t keyIndex)
	{
		static_cast<OmxModeForm *>(context)->doNoteOn(keyIndex);
	}

	// Static glue to link a pointer to a member function
	static void doNoteOffForwarder(void *context, uint8_t keyIndex)
	{
		static_cast<OmxModeForm *>(context)->doNoteOff(keyIndex);
	}

	static void selectMidiFXForwarder(void *context, uint8_t keyIndex, bool dispMsg)
	{
		static_cast<OmxModeForm *>(context)->selectMidiFx(keyIndex, dispMsg);
	}
};
