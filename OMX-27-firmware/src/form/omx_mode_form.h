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
// six views on the AUX layer, patterns, live recording. Design: design/form/FORM_DESIGN.md
// (the v1 machine-type spec and the v2 redesign proposal live in git history).

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
	FormOmni::FormMachineOmni *getSelectedMachine();
	// Snapshot the current pattern, then make `index` active and load it into the machines.
	void switchPattern(uint8_t index);
	void clearPattern(uint8_t index);
	bool patternHasContent(uint8_t index); // any track has any step with notes (snapshots if active)
	// Pattern-bank persistence. The bank (~165 KB) exceeds FRAM (32 KB), so on the
	// RP2040 (V3) it lives in the LittleFS flash filesystem; Teensy builds persist only
	// the active pattern (via the Storage blit) — a known per-platform limitation.
	void saveBankToFS();     // no-op off-RP2040
	bool loadBankFromFS();   // false when absent/mismatched/off-RP2040
public:
	// Recover the bank from flash after an FRAM header failure (the .ino reinit path):
	// loads the bank file and brings its active pattern live in the machines.
	void restoreBankFromFS();
private:

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
	// Shell wrapper: render the 5-cell scale page for the selected track. Delegates to the
	// machine's modular FormMachineOmni::drawScalePage5 (the single source of the scale look).
	void dispScalePage5(uint8_t sel, bool editing);
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
	// F1/F2 used as a modifier this hold (suppresses the param-page quick-tap palette
	// on their release — keyState is still true during a key's own release event).
	bool stepF1Used_ = false;
	bool stepF2Used_ = false;
	bool pageGestureDone_ = false;  // a range gesture consumed this F1+page press group
	void onKeyUpdateStep(OMXKeypadEvent e);
	bool onEncoderStep(Encoder::Update enc);   // returns true if the Step view consumed the encoder
	// F1 + page keys (3-6): shared select / solo / loop-range gesture (Seq + Notes views)
	void handlePageGesture(FormOmni::FormMachineOmni *omni, uint8_t p, OMXKeypadEvent e);
	bool onEncoderTranspose(int dir);          // Transpose params page; false = machine's pattern editor
	bool onEncoderButtonStep();                // returns true if consumed
	void onDisplayStepMenu();                  // render a param page (held step values / defaults)
	// Render the page-1 track overview (+ F1/F2 overlay). keyboardMode (MI view) hides the page
	// icons + step row and suppresses the F1/F2 overlays.
	void onDisplaySeqTrackPage(bool keyboardMode = false);
	void stepApplyToHeld(uint8_t paletteIndex); // set the palette value on every held step
	void updateStepLEDs();
	// Paint the low row (11-26) with the pattern step colours + playhead. Shared by the
	// Step overview and the F1/F2 layers so the step row reads identically under those holds.
	void paintStepRow(FormOmni::FormMachineOmni *omni);
	void onDisplayStep();
	void onKeyUpdateMix(OMXKeypadEvent e);     // Mix-view track keys (mute/solo/select)
	bool onKeyUpdateMixRoute(OMXKeypadEvent e); // Mix key routing; false = machine F3 fall-through
	void onKeyUpdateMixHold(OMXKeypadEvent e); // low-row per-track controls while holding a track
	void onKeyUpdateMixStep(OMXKeypadEvent e);     // low-row taps audition the selected track's steps
	void onKeyUpdateMixStepMute(OMXKeypadEvent e); // F1 + low-row toggles the selected track's step mutes
	void updateMixHoldLEDs();                  // paint those controls on the low row
	int8_t heldTrackKey_ = -1; // track key held right now in Mix (for K5 hue), -1 = none
	uint8_t mixCopyMode_ = 0;  // armed track copy while holding a track: 0 off, 1 pattern, 2 all
	// Quant/Clear submenu round-trip (they render in MI): view + menu position to restore.
	void submenuSetReturn();
	void submenuReturn();
	uint8_t subRetMixCursor_ = 0, subRetNotesCursor_ = 0;
	// Mix encoder pages (flat cursor): 0 = track overview, 1-8 = LEVELS (per-track
	// default-velocity mixer), 9-12 = TRACK (Mute/Solo/Gate/Rate). Click = select/edit.
	uint8_t mixCursor_ = 0;
	// Last CC value sent, per track x pot bank x slot (by a knob turn or the Mix CC
	// page's encoder). Per-track-per-bank so every track's banks remember their own
	// values — switching track or bank shows that combination's last-sent state.
	// (potSettings.analogValues can't hold encoder edits: the pot scan rewrites it.)
	uint8_t ccLastSent_[FORM_NUM_TRACKS][NUM_CC_BANKS][5] = {};
	// The selected track's current bank row of that table.
	uint8_t *ccBankRow() { return ccLastSent_[selectedMachine_][getSelectedMachine()->getPotBank()]; }
	// Low-row steps held in Mix (audition): while held, the CC page shows/edits that
	// step's CC P-Locks (knob turns lock too, like the Step view's hold-step gesture).
	uint16_t mixHeldStepMask_ = 0;
	int8_t mixHeldStepKey_ = -1; // most recent held step (focus for the lock display)
	bool onEncoderMix(int dir); // encoder turn in the Mix view. consumed?
	void onDisplayMixView();    // Mix display routing: F1/F2/F3 screens, else the encoder pages
	void onDisplayCCPage(uint8_t sel, uint16_t heldMask, int8_t heldKey); // shared CC page (Seq/MI)
	void onDisplayMix();        // render the Mix view's encoder pages
	// Shared CC-page edit (Mix + MI): cell 0-4 = a pot-bank CC slot (live value + send),
	// cell 5 = the track's pot bank. The P-Lock-on-held-step gesture stays Mix-only.
	void editCCPage(uint8_t cell, int dir);
	// Reusable pot-config action (Mix/Seq/Notes/MI POTS item): open the shared pot-config
	// submode on the selected track's current bank.
	void openPotConfig();
	// Fill out[16] with the display state of the active page's 16 steps: 0 empty, 1 has-notes,
	// 2 on, 3 on+muted, 4 has-notes+muted. Shared by the step-row renderers.
	void fillStepStates(FormOmni::FormMachineOmni *omni, uint8_t out[16]);
	// The gamma-corrected RGB for a track's colour (trackHue_[idx]).
	uint32_t trackHueColor(uint8_t idx);
	// The shared F3 rate|length screen ("1:<rate>" over a length bar of activeCount steps).
	void dispF3RateLength(FormOmni::FormMachineOmni *omni, uint8_t activeCount);
	// ---- Tools view (AUX+19): each menu page is a tool; keys 3-10 are that tool's
	// action buttons; the low row auditions steps (like Mix). All tools act on the
	// selected track. The encoder walks (toolIndex_, toolCell_): the cursor only stops on
	// a tool's real cells (kToolCells[]) and crossing a tool boundary pops its name.
	uint8_t toolIndex_ = 0;
	uint8_t toolCell_ = 0;
	// Transpose view's second encoder page: the track's live-transpose params
	// (TPOS/TYPE/TPAT), entered by turning past the pattern editor's end.
	bool transParamsPage_ = false;
	uint8_t transSel_ = 0;
	// Tool params (persist while in the mode):
	bool toolScopeAll_ = false;              // ROTATE: whole loop vs active page
	// PAGE tool clipboard: a whole 16-step page (steps + length). Persists across pages and
	// tracks so you can copy a page and paste it wherever (workflow: F1-select a page, COPY,
	// F1-select another page, PASTE). Lazily "loaded" by the first cut/copy.
	FormOmni::Step pageBuffer_[16];
	uint8_t pageBufferLen_ = 16;
	bool pageBufferLoaded_ = false;
	uint8_t toolVelMin_ = 64, toolVelMax_ = 127;
	uint8_t toolEucPulses_ = 4, toolEucRot_ = 0;
	uint8_t toolGridsInst_ = 0, toolGridsX_ = 128, toolGridsY_ = 128, toolGridsDens_ = 128;
	uint8_t toolHumAmt_ = 15;                // HUMANIZE: % of max nudge
	uint8_t toolChanceMin_ = 50, toolChanceMax_ = 100; // CHANCE RND: probability range
	uint8_t toolQuantAmt_ = 100;             // QUANTIZE: % pull toward the grid
	void toolAction(uint8_t tool, uint8_t action); // fire a tool's action button (keys + encoder click)
	bool onEncoderButtonTools();                   // click on a button cell fires it. consumed?
	void onKeyUpdateTools(OMXKeypadEvent e);
	bool onEncoderTools(int dir);
	void updateToolsLEDs();
	void onDisplayTools();

	// One machine per track. Follows FORM_NUM_TRACKS so the whole shell (machines_, trackHue_,
	// every kNumMachines loop, patterns_) scales with the per-platform track count.
	static const uint8_t kNumMachines = FORM_NUM_TRACKS;

	// Mix-view encoder cursor map, parameterized by the track count so it scales with
	// kNumMachines: 0 overview · 1..kNumMachines LEVELS · then 5 CC slots + bank + "CC" title ·
	// then the 4-cell TRACK grid · then the machine param menu. (For 8 tracks these are the
	// original 9-13/14/15/16-19/20.)
	// Mix cursor map (menu-map §4): 0 overview · 1-8 LEVELS · TRACK grid · machine menu.
	// (The CC page moved to the Seq view — Seq owns step P-Locks.)
	static const uint8_t kMixTrack   = kNumMachines + 1; // first of the 4 TRACK-grid cursors
	static const uint8_t kMixMenu    = kNumMachines + 5; // machine param menu cursor (last)

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

	// ---- AUX-layer transport / key tracking ----
	// Keys whose DOWN the AUX layer consumed: their releases belong to the AUX layer too
	// (transport / rec-arm act on release) and must never leak into the active view —
	// even when AUX itself was released first (the old pass-through fired quick-tap
	// copy/paste/palette actions and phantom F1/F2 states).
	uint32_t auxSwallowMask_ = 0;
	bool aux1Used_ = false; // transport key consumed by a chord/hold this press
	bool aux2Used_ = false;
	// STOP (chord) while playing; a second STOP while already stopped = KILL all notes.
	void doStopOrKill();
	void killAllNotes();
	// Tap tempo helper (rolling average), driven by the BPM tool's TAP button.
	uint32_t lastTapMs_ = 0;
	uint8_t tapCount_ = 0;
	float tapAvgMs_ = 0;
	void tapTempo();
	// When the TAP button was last hit — the BPM tool flashes it "pressed" briefly instead
	// of popping a message, so each tap gives visual feedback on the button itself.
	uint32_t bpmTapFlashMs_ = 0;
	// Seq F2 = a pick-up / drop tool.
	//  - The FIRST press with nothing loaded (seqF2Loaded_ = false) CUTS/grabs that step —
	//    even an empty one — into the buffer. This is the ONLY time an empty step is cut.
	//  - Once loaded, NON-empty steps alternate cut/paste (seqF2Holding_): holding -> paste,
	//    empty-handed -> cut; so one step pressed repeatedly cuts, pastes, cuts...
	//  - Once loaded, an EMPTY step is ALWAYS a paste (never cut).
	// F1 copy loads the buffer (holding). Releasing F2 (or a view/track change) resets both
	// flags, so the next F2 hold begins with a fresh grab.
	bool seqF2Loaded_ = false;  // has this F2 hold grabbed/copied anything yet?
	bool seqF2Holding_ = false; // holding content ready to drop (drives the non-empty alternation)
	// Audibility tracking (mute/solo): notes are flushed when a track goes inaudible.
	bool trackAudible_[FORM_NUM_TRACKS];
	// The selected track's keyboard scale (null = chromatic — see FormMachineOmni).
	MusicScales *kbScale();

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
