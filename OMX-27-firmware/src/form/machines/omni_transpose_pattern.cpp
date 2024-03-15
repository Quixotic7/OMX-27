#include "omni_transpose_pattern.h"
#include "../../config.h"
#include "../../consts/colors.h"
#include "../../utils/omx_util.h"
#include "../../hardware/omx_disp.h"
#include "../../hardware/omx_leds.h"
#include <algorithm>

namespace FormOmni
{
    enum NoteEditorPage
    {
        NEDITPAGE_1, // BPM
        OMNIPAGE_COUNT
    };

    const int stepOnRootColor = 0xA2A2FF;
    const int stepOnScaleColor = 0x000090;

    const int stepOffRootColor = RED;
    const int stepOffScaleColor = DKRED;

    OmniTransposePattern::OmniTransposePattern()
    {
        heldKey16_ = -1;

    }

    OmniTransposePattern::~OmniTransposePattern()
    {
    }

    void OmniTransposePattern::reset()
    {
        transpPos_ = 0;
    }

    int16_t OmniTransposePattern::applyTranspPattern(int16_t noteNumber, TransposePattern *tPat)
    {
        // Simple
        int16_t newNote = noteNumber + tPat->pat[transpPos_];

        return newNote;
    }

    void OmniTransposePattern::updateLEDs(ParamManager *params, TransposePattern *tPat)
    {
        bool blinkState = omxLeds.getBlinkState();

        // auto page = params->getSelPage();
        auto param = params->getSelParam();

        if (heldKey16_ < 0)
        {
            auto modLengthColor = (funcKeyModLength_ && blinkState) ? LEDOFF : FUNKONE;
            strip.setPixelColor(3, modLengthColor);

            // Function Keys
            if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3)
            {
                auto f3Color = blinkState ? LEDOFF : FUNKTHREE;
                strip.setPixelColor(1, f3Color);
                strip.setPixelColor(2, f3Color);
            }
            else
            {
                auto f1Color = (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 && blinkState) ? LEDOFF : FUNKONE;
                strip.setPixelColor(1, f1Color);

                auto f2Color = (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2 && blinkState) ? LEDOFF : FUNKTWO;
                strip.setPixelColor(2, f2Color);
            }
        }
        else // Key 16 is held, quick change value
        {
            const uint32_t vcolor = 0x101010;
            const uint32_t vcolor2 = 0xD0D0D0;

            for (uint8_t i = 0; i < 10; i++)
            {
                if (i <= tPat->pat[heldKey16_])
                {
                    strip.setPixelColor(i + 1, vcolor2);
                }
                else
                {
                    strip.setPixelColor(i + 1, vcolor);
                }
            }
        }

        const uint32_t TZERO = 0x0000FF;
        const uint32_t THIGH = 0x8080FF;
        const uint32_t TLOW = 0x000020;

        for (uint8_t i = 0; i < 16; i++)
        {
            if (param == i && blinkState) // Selected
            {
                // strip.setPixelColor(11 + i, TSEL);
            }
            else
            {
                if (i < tPat->len + 1)
                {
                    if (tPat->pat[i] == 0)
                    {
                        strip.setPixelColor(11 + i, TZERO);
                    }
                    else if (tPat->pat[i] > 0)
                    {
                        strip.setPixelColor(11 + i, THIGH);
                    }
                    else
                    {
                        strip.setPixelColor(11 + i, TLOW);
                    }
                }
            }
        }
    }

    void OmniTransposePattern::onKeyUpdate(OMXKeypadEvent e, ParamManager *params, TransposePattern *tPat)
    {
        if (e.held())
            return;

        int thisKey = e.key();
        auto param = params->getSelParam();

        if (e.down() && heldKey16_ < 0 && thisKey == 3)
        {
            funcKeyModLength_ = true;
        }

        if (!e.down() && thisKey == 3)
        {
            funcKeyModLength_ = false;
        }

        if (omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE || heldKey16_ >= 0)
        {
            if (e.down())
            {
                // Select the transpose value while key is held
                if (heldKey16_ >= 0 && thisKey > 0 && thisKey < 11)
                {
                    tPat->pat[heldKey16_] = thisKey - 1;
                    transpCopyBuffer_ = thisKey - 1;
                }
                // Select step
                if (thisKey >= 11)
                {
                    if (param == 16 || funcKeyModLength_)
                    {
                        tPat->len = thisKey - 11;

                        heldKey16_ = -1;
                    }
                    else
                    {
                        transpCopyBuffer_ = tPat->pat[thisKey - 11];

                        params->setSelParam(thisKey - 11);
                        heldKey16_ = thisKey - 11;
                    }
                }
            }
            else
            {
                if (thisKey >= 11 && thisKey - 11 == heldKey16_)
                {
                    heldKey16_ = -1;
                }
            }
        }
        else if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1)
        {
            if (e.down())
            {
                if (thisKey >= 11)
                {
                    tPat->pat[thisKey - 11] = 0;
                    transpCopyBuffer_ = 0;

                    params->setSelParam(thisKey - 11);

                    omxDisp.displayMessage("Reset: " + String(thisKey - 11 + 1));
                }
            }
        }
        else if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2)
        {
            if (e.down())
            {
                if (thisKey >= 11)
                {
                    tPat->pat[thisKey - 11] = transpCopyBuffer_;

                    params->setSelParam(thisKey - 11);

                    omxDisp.displayMessage("Pasted: " + String(thisKey - 11 + 1));
                }
            }
        }
        else if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3)
        {
            if (e.down())
            {
                if (thisKey >= 11)
                {
                    transpCopyBuffer_ = rand() % 12;
                    tPat->pat[thisKey - 11] = transpCopyBuffer_;

                    params->setSelParam(thisKey - 11);

                    omxDisp.displayMessage("Random: " + String(thisKey - 11 + 1));
                }
            }
        }
    }

    void OmniTransposePattern::onKeyHeldUpdate(OMXKeypadEvent e, TransposePattern *tPat)
    {
    }
    void OmniTransposePattern::onDisplayUpdate(ParamManager *params, TransposePattern *tPat, bool encoderSelect)
    {
        bool useLabelHeader = false;

        // if (messageTextTimer > 0)
        // {
        //     tempStrings[0] = headerMessage_;
        //     useLabelHeader = true;
        // }

        if (!useLabelHeader && omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE)
        {
            if (funcKeyModLength_)
            {
                useLabelHeader = true;

                tempStrings[0] = "Set Length";
            }
        }

        if (!useLabelHeader && omxFormGlobal.shortcutMode != FORMSHORTCUT_NONE)
        {
            useLabelHeader = true;

            if (omxFormGlobal.shortcutMode  == FORMSHORTCUT_F1)
            {
                tempStrings[0] = "Reset";
                // omxDisp.dispGenericModeLabel("Reset", params_.getNumPages(), params_.getSelPage());
            }
            else if (omxFormGlobal.shortcutMode  == FORMSHORTCUT_F2)
            {
                tempStrings[0] = "Paste";
                // omxDisp.dispGenericModeLabel("Paste", params_.getNumPages(), params_.getSelPage());
            }
            else if (omxFormGlobal.shortcutMode  == FORMSHORTCUT_F3)
            {
                tempStrings[0] = "Random";
                // omxDisp.dispGenericModeLabel("Random", params_.getNumPages(), params_.getSelPage());
            }
        }

        if (useLabelHeader)
        {
            const char *labels[1];
            labels[0] = tempStrings[0].c_str();

            omxDisp.dispValues16(tPat->pat, tPat->len + 1, -10, 10, true, constrain(params->getSelParam(), 0, 15), params->getNumPages(), params->getSelPage(), encoderSelect, true, labels, 1);
        }
        else
        {
            const char *labels[3];

            tempStrings[0] = "LEN: " + String(tPat->len + 1);

            if (params->getSelParam() < 16)
            {
                tempStrings[1] = "SEL: " + String(params->getSelParam() + 1);
                tempStrings[2] = "OFS: " + String(tPat->pat[params->getSelParam()]);
            }
            else
            {
                tempStrings[1] = "SEL: -";
                tempStrings[2] = "OFS: -";
            }

            labels[0] = tempStrings[0].c_str();
            labels[1] = tempStrings[1].c_str();
            labels[2] = tempStrings[2].c_str();

            omxDisp.dispValues16(tPat->pat, tPat->len + 1, -10, 10, true, params->getSelParam(), params->getNumPages(), params->getSelPage(), encoderSelect, true, labels, 3);
        }
    }

    void OmniTransposePattern::onEncoderChangedSelectParam(Encoder::Update enc, TransposePattern *tPat)
    {
    }
    void OmniTransposePattern::onEncoderChangedEditParam(Encoder::Update enc, uint8_t selParam, TransposePattern *tPat)
    {
        int8_t amt = enc.accel(1);

        if (selParam < 16)
        {
            tPat->pat[selParam] = constrain(tPat->pat[selParam] + amt, -48, 48);
            // transpPattern_[param] = constrain(transpPattern_[param] + amtSlow, 0, 127);
        }
        else
        {
            tPat->len = constrain(tPat->len + amt, 0, 15);
        }
    }
}