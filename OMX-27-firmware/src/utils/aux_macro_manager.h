#pragma once
#include "../ClearUI/ClearUI_Input.h"
#include "../hardware/omx_keypad.h"
#include "../config.h"
#include "../modes/submodes/submode_midifxgroup.h"
#include "../utils/music_scales.h"
#include "../midimacro/midimacro_interface.h"


/// This class manages enabling/disabling macros, submodes, and aux shortcuts
/// So that your OMX Mode can focus on what's special to that OMX Mode
/// And leave the rerouting to the AuxMacroManager
///
/// This will also reduce RAM usage by not having multiple instances of
/// Macros and Submodes within each OMX mode. 
///
/// Before integration:
/// RAM:   [======    ]  58.0% (used 37988 bytes from 65536 bytes)
/// Flash: [=======   ]  72.0% (used 188792 bytes from 262144 bytes)
///
/// After Midi Keyboard integration
/// Ram reduced
/// RAM:   [=====     ]  54.5% (used 35704 bytes from 65536 bytes)
/// Flash: [=======   ]  72.0% (used 188752 bytes from 262144 bytes)
class AuxMacroManager
{
public:
    AuxMacroManager();

    bool isMacroActive();
    bool isMFXQuickEditEnabled();
    bool doesActiveMacroConsomeDisplay();
    bool isMidiFXGroupIndexValid(uint8_t mfxIndex);
    bool shouldBlockEncEdit();

    // Must be called for macros to work
    void onModeActivated();
    void onModeDectivated();

    void setContext(void *context);
    void setMacroNoteOn(void (*fptr)(void *, uint8_t));
    void setMacroNoteOff(void (*fptr)(void *, uint8_t));

    // Modes implement midifx in their own ways
    void setSelectMidiFXFPTR(void (*fptr)(void *, uint8_t, bool));

    void SetScale(MusicScales *scale);

    // Returns true if consumed
    bool inMidiControlChange(byte channel, byte control, byte value);

    // If this returns true, macro is consuming display and using encoder values
    bool onEncoderChanged(Encoder::Update enc);
    // If this returns true, macro is consuming display and using encoder values
    bool onEncoderButtonDown();
    bool onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta);
    // If this returns true, macro is consuming key events
    bool onKeyUpdate(OMXKeypadEvent e);
    bool onKeyHeldUpdate(OMXKeypadEvent e);

    // If this returns true, key was used for midifx shortcut
    bool onKeyUpdateAuxMFXShortcuts(OMXKeypadEvent e, uint8_t selMFXIndex);
    bool onKeyHeldAuxMFXShortcuts(OMXKeypadEvent e, uint8_t selMFXIndex);

    // If this returns true, macro is active and consumes LEDs
    bool updateLEDs();
    // If this returns true, macro is consuming the display
    bool onDisplayUpdate();

    // Modes keep track of the selected MidiFX which varies between modes
    // In Drum and Chord modes, each drum or chord key has it's own midifx param
    void UpdateAUXLEDS(uint8_t selectedMidiFX);

    // SubModes
	void enableSubmode(SubmodeInterface *subMode);
    void disableSubmode();
	bool isSubmodeEnabled();

    midimacro::MidiMacroInterface *getActiveMacro();

private:
    bool macroActive_;
	bool mfxQuickEdit_ = false;
	uint8_t quickEditMfxIndex_ = 0;

	MusicScales *musicScale_;

    void *context_;
    void (*doNoteOnFptr_)(void *, uint8_t);
    void (*doNoteOffFptr_)(void *, uint8_t);

    
    midimacro::MidiMacroInterface *activeMacro_;

    // Static glue to link a pointer to a member function
	static void doNoteOnForwarder(void *context, uint8_t keyIndex)
	{
		static_cast<AuxMacroManager *>(context)->doMacroNoteOn(keyIndex);
	}

	// Static glue to link a pointer to a member function
	static void doNoteOffForwarder(void *context, uint8_t keyIndex)
	{
		static_cast<AuxMacroManager *>(context)->doMacroNoteOff(keyIndex);
	}

    void doMacroNoteOn(uint8_t keyIndex);
    void doMacroNoteOff(uint8_t keyIndex);

	SubmodeInterface *activeSubmode = nullptr;

    void (*selectMidiFxFptr_)(void *, uint8_t, bool);
    void selectMidiFx(uint8_t mfxIndex, bool dispMsg);
};

    // char foo[sizeof(midimacro::MidiMacroNorns)]



