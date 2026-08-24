#pragma once
#include "../../ClearUI/ClearUI_Input.h"
#include "../../hardware/omx_keypad.h"
#include "../../utils/param_manager.h"
#include "omni_structs.h"
#include "../omx_form_global.h"

namespace FormOmni
{
    // Singleton class for editing Omni Step Notes
    class OmniNoteEditor
    {
    public:
        OmniNoteEditor();
        ~OmniNoteEditor();

        uint8_t getSelStep();
        void setSelStep(uint8_t stepIndex);

        // Standard Updates
        // void onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta) override;
        // void onClockTick() override;
        // void loopUpdate() override;
        void updateLEDs(Track *track);
        // void onEncoderButtonDown() override;
        void onKeyUpdate(OMXKeypadEvent e, Track *track);
        void onKeyHeldUpdate(OMXKeypadEvent e, Track *track);
        void onDisplayUpdate(Track *track);

        void onEncoderChangedSelectParam(Encoder::Update enc, Track *track);
        void onEncoderChangedEditParam(Encoder::Update enc, Track *track);

    private:
        uint8_t selStep_ = 0;

        // uint8_t heldKeys[6];

        // static inline bool
		// compareArpNote(ArpNote a1, ArpNote a2)
		// {
		// 	return (a1.noteNumber < a2.noteNumber);
		// }
    };

    extern OmniNoteEditor omniNoteEditor;
}