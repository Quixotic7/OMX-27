#pragma once
#include "../../ClearUI/ClearUI_Input.h"
#include "../../hardware/omx_keypad.h"
#include "../../utils/param_manager.h"
#include "../../config.h"
#include "../omx_form_global.h"

enum FormMachineType
{
	FORMMACH_NULL,
	FORMMACH_OMNI,
	FORMMACH_COUNT
};

// defines interface for a submode, a mode within a mode
class FormMachineInterface
{
public:
	FormMachineInterface() {}
	virtual ~FormMachineInterface() {}

	virtual FormMachineType getType() = 0;
	virtual FormMachineInterface *getClone() { return nullptr; }

    // Getters
	virtual bool isEnabled();
	virtual bool usesPots() { return false; } // return true if submode uses pots
	virtual bool shouldBlockEncEdit() { return false; }
	virtual bool getEncoderSelect();

	virtual bool doesConsumePots() { return false; }
	virtual bool doesConsumeDisplay() { return false; }

	// consumeKeys and consumeLEDs if consumes all keys and LEDs
	// by design, form machines can use the lower 16 keys
	virtual bool doesConsumeKeys() {return false; }
	virtual bool doesConsumeLEDs() {return false; }

	virtual const char* getF3shortcutName() {return ""; }

    // Setters
	virtual void setEnabled(bool newEnabled);

	virtual void playBackStateChanged(bool newIsPlaying) {}
	virtual void resetPlayback() {}

	// Callbacks
	void setContext(void *context);
	void setNoteOnFptr(void (*fptr)(void *, MidiNoteGroup, uint8_t));
	void setNoteOffFptr(void (*fptr)(void *, MidiNoteGroup, uint8_t));

    // Standard Updates
	virtual void onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta) {}
	virtual void onClockTick() {}
	virtual void loopUpdate() {}
	virtual bool updateLEDs() { return true; }
	virtual void onEncoderChanged(Encoder::Update enc);
	virtual void onEncoderButtonDown();
	virtual bool onKeyUpdate(OMXKeypadEvent e) { return true; }
	virtual bool onKeyHeldUpdate(OMXKeypadEvent e) { return true; }
	virtual bool onKeyQuickClicked(OMXKeypadEvent e) { return false; }

	virtual void onDisplayUpdate() {}

	virtual void selectMidiFx(uint8_t mfxIndex, bool dispMsg) {};
	virtual uint8_t getSelectedMidiFX() { return 255; }

	virtual void setTest(){};

	// Selected Machine in form
	virtual void onSelected() {}

    // AUX + Top 1 = Play Stop
    // For Omni:
    // AUX + Top 2 = Reset
    // AUX + Top 3 = Flip play direction if forward or reverse
    // AUX + Top 4 = Increment play direction mode
	virtual void onAUXFunc(uint8_t funcKey) {}

	virtual int saveToDisk(int startingAddress, Storage *storage) { return startingAddress; }
	virtual int loadFromDisk(int startingAddress, Storage *storage) { return startingAddress; }

protected:
	bool enabled_;
	bool encoderSelect_;

	void *context_;
	void (*noteOnFuncPtr)(void *, MidiNoteGroup, uint8_t);
	void (*noteOffFuncPtr)(void *, MidiNoteGroup, uint8_t);

	virtual void seqNoteOn(MidiNoteGroup noteGroup, uint8_t midiFx);
	virtual void seqNoteOff(MidiNoteGroup noteGroup, uint8_t midiFx);
	virtual void onNoteOn(uint8_t channel, uint8_t noteNumber, uint8_t velocity, float stepLength, bool sendMidi, bool sendCV, uint32_t noteOnMicros);
	

	virtual void onEnabled() {}	 // Called whenever entering mode
	virtual void onDisabled() {} // Called whenever exiting mode

	virtual void onEncoderChangedSelectParam(Encoder::Update enc);
	virtual void onEncoderChangedEditParam(Encoder::Update enc) = 0;
};
