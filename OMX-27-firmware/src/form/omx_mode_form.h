#pragma once

#include "../modes/omx_mode_interface.h"
#include "../utils/aux_macro_manager.h"
#include "../utils/param_manager.h"
#include "../modes/submodes/submode_midifxgroup.h"
#include "../modes/submodes/submode_potconfig.h"
#include "../modes/submodes/submode_preset.h"
#include "../midifx/midifx_interface.h"
#include "machines/form_machine_interface.h"

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

private:
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

	// uint8_t copiedMachineIndex_;

	void changeFormMode(uint8_t newFormMode);

	FormMachineInterface *machines_[kNumMachines];

	FormMachineInterface *copyBuffer_; // Machine for cut/copy/paste and undo
	FormMachineInterface *undoBuffer_; // Machine for cut/copy/paste and undo

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
