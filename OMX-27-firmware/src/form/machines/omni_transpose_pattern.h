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

        void updateLEDs(ParamManager *params, TransposePattern *tPat);
        void onKeyUpdate(OMXKeypadEvent e, ParamManager *params, TransposePattern *tPat);
        void onKeyHeldUpdate(OMXKeypadEvent e, TransposePattern *tPat);
        void onDisplayUpdate(ParamManager *params, TransposePattern *tPat, bool encoderSelect);

        void onEncoderChangedSelectParam(Encoder::Update enc, TransposePattern *tPat);
        void onEncoderChangedEditParam(Encoder::Update enc, uint8_t selParam, TransposePattern *tPat);

    private:
		uint8_t transpPos_ : 5;
		int8_t heldKey16_ : 5; // Key that is held

		int8_t transpCopyBuffer_;

        bool funcKeyModLength_ = false;
    };
}