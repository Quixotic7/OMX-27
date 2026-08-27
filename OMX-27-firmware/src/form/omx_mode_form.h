#pragma once

#include "../modes/omx_mode_interface.h"
#include "../utils/aux_macro_manager.h"
#include "../utils/param_manager.h"
#include "../modes/submodes/submode_midifxgroup.h"
#include "../modes/submodes/submode_potconfig.h"
#include "../modes/submodes/submode_preset.h"
#include "../midifx/midifx_interface.h"
#include "machines/form_machine_interface.h"
#include "form_patterns.h"

// AUX View - Rendered by form
// Familiar shortcuts as MI Modes

// AUX + Top 1 = Play/Stop
// For Omni:
// AUX + Top 2 = Reset
// AUX + Top 3 = Flip play direction if forward or reverse
// AUX + Top 4 = Increment rand/shuffle mode

// Main view, F1, F2, 8 Sequencer machines - Top keys, rendered by Form
// Lower portion rendered by the sequencer

// Top 8 - Select a machine
// Hold top 8, press bottom 16 to select a sequencer type
// Changing sequencer type will get rid of current sequencer.
// Maybe keep this in ram and offer undo with F1?

// Machines:
// OMNI - Powerful step sequencer
// Euclidean
// Grids
// Tambola - Bouncing balls in rotating polygon

// OMNI
// Pot 1 - Pickup off, Selects Page: 1 - 4
// Pot 2 - Pickup on, Selects Zoom: 1 Bar, 2 Bar, 4 Bar, Steps faster than zoom are hidden.
// Pot 3 - Pickup on, Cross Page: Applies changes to step on all bars if zoom level 1 bar,
// Pot 4 - Pickup on, Sets track rate, maybe play mode instead since there are now F3 rate shortcuts

// Pot 5 - Pickup On, default is mix. Change behaviour of keys, also on UI page


// Pot 5 - UI Mode:
// 	SEQ - Edit steps, change active machine
//	MIDI KEYBOARD - Play the keyboard, record notes into active machine
//	MIDI KEYBOARD TRANSPOSE - Play the keyboard, transpose the current machine
//	TRANSPOSE PATTERN - Edit the transpose pattern
//	NOTE EDITOR - Edit the notes
//	MACHINE CONFIG - Edit the machine configuration (Cut, copy, paste machines) (Load different machine types)

// Pot 4 - SEQ
//	MIX - Quickclick steps to toggle mute, hold step to edit velocity for held steps on top row, Double Click keys to edit notes, F1, F2 shortcuts for muting and soloing F1 + F2 to change length
//  EDIT MODES: In these modes F1 and F2 do cut, copy, paste, track length, quickclick mute toggle is off?
//	EDIT FUNC - Hold step to edit function or jump to specific step. 
//  EDIT NOTE LENGTH - Hold steps to edit their note lengths(Top row keys)
//	EDIT NUDGE - Hold steps to edit their nudge(Top row keys)
//	EDIT MFX - Hold steps and use top keys to set midifx
//	EDIT ACCUM - Hold steps and use top keys to set step accum values
//	EDIT CHANCE - Hold steps and use top keys to set step chance values
//	EDIT CONDITIONS - Hold steps and use top keys to set step condition values
//	EDIT CHORD - Hold steps and use the top row keys to set a chord, 8 Chord keys configurable like in chord mode. 
//  EDIT DRUM - Hold steps and use the top row keys to set drum keys, 8 drum keys configurable like drum mode

// Pot 4 - MIDI KEYBOARD
//	RECORD OFF
//	RECORD ON - Record notes to pattern

// MIDI KEYBOARD
//	Menu option to clear the pattern


// Mix - Press keys to mute/unmute, hold to enter note editor
//      Mix note editor here shows full note params
//      Pots will set params of current page using pot pickup
//      Sequencers can also be muted with a click, or soloed by holding down the sequencer key

// Transpose - Changes keys to keyboard view, select a key to set transpose value

// Step - Enters note editor, pressing keys sets notes for step, auto advances to next step when releasing notes

// Note Edit - Enters note editor, pressing keys toggles notes on and off, advance to next step by turning encoder
//      OMNI can be set to monophonic, in this case, Note Edit sets note to latest key, only one note

// Params - Hold key to quickly set the parameters for step of current page using the pots or encoder.
//      If using pots, pot pickup is used
//      4 Pot CC's can be set on last page
//      If holding a step then pressing another step it will set that steps length

// Track Length - Set the start and end steps for the track length, behaviour is changed by selecting highlighted start or end step than selecting non highlighted step or using the encoder

// Function - Hold key then press top keys to set the step function

// Transpose Pattern - Edit the transpose pattern using keys like in arp editor

// Configure - Use this mode to change sequencers
//      Hold sequencer and select type below
//      Global config params also available in menu here
//

// Macro modes - Available and accessible just like in MI mode by double pressing AUX.

// Menu Pages
// Transpose Pattern - Editable in menu unless pot 5 mode is set to transpose pattern.

// F1 = Copy / Undo cut or undo changing a machine
// F2 = Paste
// F1 + F2 = Cut

// Other features
// - Make sequencer keys light up as notes are triggered by them

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
	FORMVIEW_MI,        // AUX+18 (container-rendered; stub for now)
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

// This mode is designed to be used with samplers or drum machines
// Each key can be configured to whatever Note, Vel, Midi Chan you want.
// This class is very similar to the midi keyboard, maybe we merge or inherit.
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
	uint8_t getPatternCount() const { return FORM_NUM_PATTERNS; }
	// Snapshot the current pattern, then make `index` active and load it into the machines.
	void switchPattern(uint8_t index);
	void copyPatternTo(uint8_t from, uint8_t to);
	void clearPattern(uint8_t index);

private:
	// ---- v2 shell: view router ----
	uint8_t formView_ = FORMVIEW_MIX;
	// While AUX is held, tapping a view key selects a pending view (preview + message);
	// the switch is committed on AUX release so the AUX overlay stays up while you browse.
	uint8_t pendingView_ = FORMVIEW_MIX;
	// Page-1 (track page) encoder view selector: click the encoder to enter edit mode (the view
	// tag boxes/inverts), turn to browse pendingView_, click again to commit. See isTrackPage().
	bool viewSelectEdit_ = false;
	// silent = live switch from the selector: no popup, keep the selector open.
	void setFormView(uint8_t view, bool silent = false);
	bool isTrackPage();      // true when the MIX/SEQ page-1 track overview owns the encoder
	bool viewEditActive();   // view selector is live (encoder latch OR AUX held)
	bool onEncoderTrackPage(int dir); // encoder turn: live-switch views while editing. consumed?
	bool onEncoderButtonTrackPage();  // encoder click: enter/exit the view selector. consumed?
	void updateAuxViewLEDs(); // paint the view selector (keys 13-18) on the AUX overlay
	// Container-rendered views:
	void updatePatternsLEDs();
	void onKeyUpdatePatterns(OMXKeypadEvent e);
	void onDisplayPatterns();
	// MI view — a live-play keyboard over the running sequencer (§4.6). The encoder navigates a
	// small menu: 0 = keyboard · 1-4 = Scale (root/scale/lock/group) · 5-8 = Track (chan/vel/
	// bank/oct). Click toggles select (turn = move) vs edit (turn = change value).
	uint8_t miCursor_ = 0;
	bool miEncEdit_ = false;
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
	bool notesEncEdit_ = false;
	bool onEncoderNotes(int dir);       // encoder turn in the Notes view. consumed?
	bool onEncoderButtonNotes();        // encoder click in the Notes view. consumed?
	void notesEditScaleParam(uint8_t param, int dir); // 0 root · 1 scale · 2 lock · 3 group
	void onKeyUpdateNotes(OMXKeypadEvent e);
	void updateNotesLEDs();
	void onDisplayNotes();
	void notesSetChordFromHeld(); // build the selected step's chord from the held piano keys
	// Live recording: quantize a played note into the selected track's nearest playing step.
	uint64_t recClearedMask_ = 0; // steps cleared this record pass (replace mode); reset on loop wrap
	void recordPlayedNote(int8_t note);
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
	bool stepTrackSelEdit_ = false; // overview: encoder-click armed to change the track
	bool stepDefaultEdit_ = false;  // param page (no step held): encoder-click armed to edit the default
	bool stepPasteArmed_ = false;   // F2 buffer state: next F2 pastes (after a copy/cut)
	uint8_t heldPageMask_ = 0;      // F1 + page keys currently held (for loop-range gesture)
	bool pageGestureDone_ = false;  // a range gesture consumed this F1+page press group
	void onKeyUpdateStep(OMXKeypadEvent e);
	bool onEncoderStep(Encoder::Update enc);   // returns true if the Step view consumed the encoder
	bool onEncoderButtonStep();                // returns true if consumed
	void onDisplayStepMenu();                  // render a param page (held step values / defaults)
	void onDisplaySeqTrackPage();              // render the page-1 track overview (+ F1/F2 overlay)
	void stepApplyToHeld(uint8_t paletteIndex); // set the palette value on every held step
	void updateStepLEDs();
	void onDisplayStep();
	void onKeyUpdateMix(OMXKeypadEvent e);     // Mix-view track keys (mute/solo/select)
	void onKeyUpdateMixHold(OMXKeypadEvent e); // low-row per-track controls while holding a track
	void onKeyUpdateMixStep(OMXKeypadEvent e);     // low-row taps audition the selected track's steps
	void onKeyUpdateMixStepMute(OMXKeypadEvent e); // F1 + low-row toggles the selected track's step mutes
	void updateMixHoldLEDs();                  // paint those controls on the low row
	int8_t heldTrackKey_ = -1; // track key held right now in Mix (for K5 hue), -1 = none

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

	void changeFormMode(uint8_t newFormMode);

	FormMachineInterface *machines_[kNumMachines];

	// Per-track colour (hue): hold a track + turn K5 in Mix. Container-level for now
	// (not yet persisted with the pattern).
	uint8_t trackHue_[kNumMachines];

	FormMachineInterface *copyBuffer_; // Machine for cut/copy/paste and undo
	FormMachineInterface *undoBuffer_; // Machine for cut/copy/paste and undo

	// v2 pattern data layer: the bank of whole-sequencer snapshots. The active pattern is
	// live in the machines; the others sit here. See form_patterns.h + FORM_IMPLEMENTATION.md.
	FormPattern patterns_[FORM_NUM_PATTERNS];
	uint8_t activePattern_ = 0;

	// Copy the 8 machines' live seq data into patterns_[activePattern_].
	void snapshotActivePattern();
	// Load patterns_[index] into the 8 machines.
	void loadPatternIntoMachines(uint8_t index);

	bool isMachineValid(uint8_t machineIndex);

	const char *getMachineName(uint8_t machineIndex);
	int getMachineColor(uint8_t machineIndex);

	void selectMachine(uint8_t machineIndex);

	FormMachineInterface* getSelectedMachine();

	void changeMachineAtIndex(uint8_t machineIndex, uint8_t machineType);

	void cutMachineAt(uint8_t machineIndex);
	void copyMachineAt(uint8_t machineIndex);
	void pasteMachineTo(uint8_t machineIndex);
	void setMachineTo(uint8_t machineIndex, FormMachineInterface *ptr);

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
