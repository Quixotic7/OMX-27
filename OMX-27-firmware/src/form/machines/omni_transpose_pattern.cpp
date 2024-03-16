#include "omni_transpose_pattern.h"
#include "../../config.h"
#include "../../consts/colors.h"
#include "../../utils/omx_util.h"
#include "../../hardware/omx_disp.h"
#include "../../hardware/omx_leds.h"
#include <algorithm>

namespace FormOmni
{
    enum TPatShortcut
    {
        TPATSHORT_NONE,
        TPATSHORT_RAND,
        TPATSHORT_HELDSTEP
    };

    OmniTransposePattern::OmniTransposePattern()
    {
        heldKey16_ = -1;
    }

    OmniTransposePattern::~OmniTransposePattern()
    {
    }

    void OmniTransposePattern::onUIEnabled()
    {
        heldKey16_ = -1;
        patShortcut_ = TPATSHORT_NONE;
        messageTextTimer = 0;
    }

    void OmniTransposePattern::reset()
    {
        transpPos_ = 0;
    }

    void OmniTransposePattern::advance(TransposePattern *tPat)
    {
        transpPos_ = (transpPos_ + 1) % (tPat->len + 1);
    }

    void OmniTransposePattern::copyStep(uint8_t keyIndex, TransposePattern *tPat)
    {
        if(keyIndex < 0 || keyIndex >= 16) return;

        transpCopyBuffer_ = tPat->pat[keyIndex];
    }
    void OmniTransposePattern::cutStep(uint8_t keyIndex, TransposePattern *tPat)
    {
        if(keyIndex < 0 || keyIndex >= 16) return;

        copyStep(keyIndex, tPat);
        tPat->pat[keyIndex] = 0;
    }
    void OmniTransposePattern::pasteStep(uint8_t keyIndex, TransposePattern *tPat)
    {
        if (keyIndex < 0 || keyIndex >= 16)
            return;

        tPat->pat[keyIndex] = transpCopyBuffer_;
    }

    bool OmniTransposePattern::getEncoderSelect()
    {
        return heldKey16_ < 0;
    }

    int8_t OmniTransposePattern::getCurrentTranspose(TransposePattern *tPat)
    {
        transpPos_ = transpPos_ % (tPat->len + 1);
        return tPat->pat[transpPos_];
    }

    int8_t OmniTransposePattern::getTransposeAtStep(uint8_t step, TransposePattern *tPat)
    {
        uint8_t stepIndex = step % (tPat->len + 1);
        return tPat->pat[stepIndex];
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

        uint8_t shortcutMode = getShortcutMode();

        // if(omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE && patShortcut_ == TPATSHORT_NONE || patShortcut_ == TPATSHORT_NONE)
        // {

        // }
        // if(omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE && patShortcut_ == TPATSHORT_HELDSTEP)
        // {

        // }
        // if(omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE && patShortcut_ == TPATSHORT_NONE)
        // {

        // }

        if (patShortcut_ != TPATSHORT_HELDSTEP)
        {
            auto randShortColor = (patShortcut_ == TPATSHORT_RAND && blinkState) ? LEDOFF : FUNKONE;
            strip.setPixelColor(3, randShortColor);

            // Clear pattern
            strip.setPixelColor(10, RED);

            // Function Keys
            if (shortcutMode == FORMSHORTCUT_F3)
            {
                auto f3Color = blinkState ? LEDOFF : FUNKTHREE;
                strip.setPixelColor(1, f3Color);
                strip.setPixelColor(2, f3Color);
            }
            else
            {
                auto f1Color = (shortcutMode == FORMSHORTCUT_F1 && blinkState) ? LEDOFF : FUNKONE;
                strip.setPixelColor(1, f1Color);

                auto f2Color = (shortcutMode == FORMSHORTCUT_F2 && blinkState) ? LEDOFF : FUNKTWO;
                strip.setPixelColor(2, f2Color);
            }
        }
        else if(patShortcut_ == TPATSHORT_HELDSTEP)// Key 16 is held, quick change value
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
        // auto param = params->getSelParam();

        uint8_t shortcutMode = getShortcutMode();

        // Update the shortcuts
        switch (shortcutMode)
        {
        case FORMSHORTCUT_NONE:
        {
            switch (patShortcut_)
            {
            case TPATSHORT_NONE:
                if (e.down() && thisKey == 3)
                {
                    patShortcut_ = TPATSHORT_RAND;
                }
                else if (thisKey == 10)
                {
                    tPat->Reinit();
                    headerMessage_ = "Clear Pat";
                    showMessage();
                }
                if (e.down() && thisKey >= 11)
                {
                    params->setSelParam(thisKey - 11);
                    heldKey16_ = thisKey - 11;
                    patShortcut_ = TPATSHORT_HELDSTEP;
                }
                break;
            case TPATSHORT_RAND:
            {
                if (e.down() && thisKey >= 11)
                {
                    tPat->pat[thisKey - 11] = rand() % 12;
                    params->setSelParam(thisKey - 11);
                    heldKey16_ = thisKey - 11;
                }
                if (!e.down() && thisKey == 3)
                    patShortcut_ = TPATSHORT_NONE;
            }
            break;
            case TPATSHORT_HELDSTEP:
            {
                // Select the transpose value while key is held
                if (heldKey16_ >= 0 && thisKey > 0 && thisKey < 11)
                {
                    tPat->pat[heldKey16_] = thisKey - 1;
                }
                else if (e.down() && thisKey >= 11)
                {
                    params->setSelParam(thisKey - 11);
                    heldKey16_ = thisKey - 11;
                }
                else if (!e.down() && thisKey >= 11 && thisKey - 11 == heldKey16_)
                {
                    patShortcut_ = TPATSHORT_NONE;
                    heldKey16_ = -1;
                }
            }
            break;
            }

            if (!e.down() && thisKey >= 11 && thisKey - 11 == heldKey16_)
            {
                heldKey16_ = -1;
            }
        }
        break;
        case FORMSHORTCUT_F1:
        case FORMSHORTCUT_F2:
        case FORMSHORTCUT_F3:
        {
            heldKey16_ = -1;
            patShortcut_ = TPATSHORT_NONE;
        }
        break;
        }
        
        if (shortcutMode == FORMSHORTCUT_F1)
        {
            if (e.down())
            {
                if (thisKey >= 11)
                {
                    // Copy then paste
                    if (omxFormGlobal.shortcutPaste == false)
                    {
                        copyStep(thisKey - 11, tPat);
                        omxFormGlobal.shortcutPaste = true;
                        // headerMessage_ = "Copied: " + String(thisKey - 11 + 1);
                    }
                    else
                    {
                        pasteStep(thisKey - 11, tPat);
                        // headerMessage_ = "Pasted: " + String(thisKey - 11 + 1);
                    }


                    // tPat->pat[thisKey - 11] = 0;
                    // transpCopyBuffer_ = 0;

                    params->setSelParam(thisKey - 11);

                    // headerMessage_ = "Reset: " + String(thisKey - 11 + 1);
                    // showMessage();
                }
            }
        }
        else if (shortcutMode == FORMSHORTCUT_F2)
        {
            if (e.down())
            {
                // Cut then paste
                if (thisKey >= 11)
                {
                    if (omxFormGlobal.shortcutPaste == false)
                    {
                        cutStep(thisKey - 11, tPat);
                        omxFormGlobal.shortcutPaste = true;
                        // headerMessage_ = "Cut: " + String(thisKey - 11 + 1);
                    }
                    else
                    {
                        pasteStep(thisKey - 11, tPat);
                        // headerMessage_ = "Pasted: " + String(thisKey - 11 + 1);
                    }

                    // tPat->pat[thisKey - 11] = transpCopyBuffer_;

                    params->setSelParam(thisKey - 11);

                    // headerMessage_ = "Pasted: " + String(thisKey - 11 + 1);
                    // showMessage();
                }
            }
        }
        else if (shortcutMode == FORMSHORTCUT_F3)
        {
            if (e.down())
            {
                // Set length
                if (thisKey >= 11)
                {
                    tPat->len = thisKey - 11;
                    heldKey16_ = -1;

                    headerMessage_ = "Length: " + String(tPat->len + 1);
                    showMessage();
                }
            }
        }
        // else if (shortcutMode == FORMSHORTCUT_F3)
        // {
        //     if (e.down())
        //     {
        //         // Set length
        //         if (thisKey >= 11)
        //         {
        //             tPat->len = thisKey - 11;
        //             heldKey16_ = -1;

        //             // transpCopyBuffer_ = rand() % 12;
        //             // tPat->pat[thisKey - 11] = transpCopyBuffer_;

        //             // params->setSelParam(thisKey - 11);

        //             headerMessage_ = "Legnth: " + String(tPat->len + 1);
        //             showMessage();
        //         }
        //     }
        // }
    }

    void OmniTransposePattern::onKeyHeldUpdate(OMXKeypadEvent e, TransposePattern *tPat)
    {
    }

    void OmniTransposePattern::loopUpdate()
    {
        if (messageTextTimer > 0)
        {
            messageTextTimer -= sysSettings.timeElasped;
            if (messageTextTimer <= 0)
            {
                omxDisp.setDirty();
                omxLeds.setDirty();
                messageTextTimer = 0;
            }
        }
    }

    uint8_t OmniTransposePattern::getShortcutMode()
    {
        uint8_t shortcutMode = omxFormGlobal.shortcutMode;

        // don't switch shortcut while holding step
        if (patShortcut_ == TPATSHORT_HELDSTEP)
        {
            shortcutMode = FORMSHORTCUT_NONE;
        }

        return shortcutMode;
    }

    void OmniTransposePattern::showMessage()
    {
        const uint8_t secs = 5;
        messageTextTimer = secs * 100000;
        omxDisp.setDirty();
    }

    void OmniTransposePattern::onDisplayUpdate(ParamManager *params, TransposePattern *tPat, bool encoderSelect)
    {
        bool useLabelHeader = false;

        if (messageTextTimer > 0)
        {
            tempStrings[0] = headerMessage_;
            useLabelHeader = true;
        }

        uint8_t shortcutMode = getShortcutMode();

        if (!useLabelHeader)
        {
            if (shortcutMode == FORMSHORTCUT_NONE && patShortcut_ == TPATSHORT_RAND)
            {
                useLabelHeader = true;
                tempStrings[0] = "Random";
            }
            // else if (shortcutMode == FORMSHORTCUT_NONE && patShortcut_ == TPATSHORT_HELDSTEP)
            // {
            //     useLabelHeader = true;
            //     tempStrings[0] = "Transpose";
            // }
            else if (shortcutMode == FORMSHORTCUT_F1)
            {
                useLabelHeader = true;
                tempStrings[0] = omxFormGlobal.shortcutPaste ? "Paste" : "Copy";
            }
            else if (shortcutMode == FORMSHORTCUT_F2)
            {
                useLabelHeader = true;
                tempStrings[0] = omxFormGlobal.shortcutPaste ? "Paste" : "Cut";
            }
            else if (shortcutMode == FORMSHORTCUT_F3)
            {
                useLabelHeader = true;
                tempStrings[0] = "Set Length";
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