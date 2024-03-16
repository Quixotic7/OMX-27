#pragma once
#include "../../ClearUI/ClearUI_Input.h"
#include "../../hardware/omx_keypad.h"
#include "../../utils/param_manager.h"
#include "omni_structs.h"
#include "../omx_form_global.h"

namespace FormOmni
{
    // Singleton class for editing the transpose pattern
    class OmniTransposePattern
    {
    public:
        OmniTransposePattern();
        ~OmniTransposePattern();

        void reset();
        int16_t applyTranspPattern(int16_t noteNumber, TransposePattern *tPat);

        void advance(TransposePattern *tPat);

        int8_t getCurrentTranspose(TransposePattern *tPat);
        int8_t getTransposeAtStep(uint8_t step, TransposePattern *tPat);

        void onUIEnabled();

        bool getEncoderSelect();
        void loopUpdate();

        void updateLEDs(ParamManager *params, TransposePattern *tPat);
        void onKeyUpdate(OMXKeypadEvent e, ParamManager *params, TransposePattern *tPat);
        void onKeyHeldUpdate(OMXKeypadEvent e, TransposePattern *tPat);
        void onDisplayUpdate(ParamManager *params, TransposePattern *tPat, bool encoderSelect);

        void onEncoderChangedSelectParam(Encoder::Update enc, TransposePattern *tPat);
        void onEncoderChangedEditParam(Encoder::Update enc, uint8_t selParam, TransposePattern *tPat);

    private:
		uint8_t transpPos_ : 5;
		int8_t heldKey16_ : 5; // Key that is held

        uint8_t patShortcut_;

		int8_t transpCopyBuffer_;

		String headerMessage_;

		int messageTextTimer = 0;

        // bool funcKeyModLength_ = false;

        uint8_t getShortcutMode();

        void showMessage();

        void copyStep(uint8_t keyIndex, TransposePattern *tPat);
        void cutStep(uint8_t keyIndex, TransposePattern *tPat);
        void pasteStep(uint8_t keyIndex, TransposePattern *tPat);
    };
}