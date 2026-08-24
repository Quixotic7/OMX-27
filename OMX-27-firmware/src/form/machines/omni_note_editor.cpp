#include "omni_note_editor.h"
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

    OmniNoteEditor::OmniNoteEditor()
    {
    }

    OmniNoteEditor::~OmniNoteEditor()
    {
    }

    uint8_t OmniNoteEditor::getSelStep()
    {
        return selStep_;
    }

    void OmniNoteEditor::setSelStep(uint8_t stepIndex)
    {
        selStep_ = stepIndex;
    }

    void OmniNoteEditor::updateLEDs(Track *track)
    {
        bool isStepOn = track->isStepOn(selStep_);
        int rootColor = isStepOn ? stepOnRootColor : stepOffRootColor;
        int scaleColor = isStepOn ? stepOnScaleColor : stepOffScaleColor;

        if(scaleConfig.scalePattern < 0 && !isStepOn)
        {
            omxLeds.setAllLEDS(stepOffScaleColor);
        }
        else
        {
            omxLeds.drawKeyboardScaleLEDs(omxFormGlobal.musicScale, rootColor, scaleColor, LEDOFF);
        }

        bool blinkState = omxLeds.getSlowBlinkState();

        auto stepNotes = track->steps[selStep_].notes;

        int viewStartNote = notes[11] + (midiSettings.octave * 12); // Which note is key 11?
        int viewEndNote = notes[26] + (midiSettings.octave * 12);

        for(uint8_t i = 0; i < 6; i++)
        {
            if(stepNotes[i] >= 0 && stepNotes[i] <= 127)
            {
                int8_t noteNumber = stepNotes[i];

                bool noteInView = noteNumber >= viewStartNote && noteNumber <= viewEndNote;

                int8_t keyIndex = omxUtil.noteNumberToKeyNumber(noteNumber);

                if(keyIndex >= 0)
                {
                    auto color = noteInView ? LTYELLOW : ORANGE;
                    strip.setPixelColor(keyIndex, color);
                }
            }
        }

        strip.setPixelColor(11, DKRED);

        if(blinkState)
        {
            uint8_t sel16 = selStep_ % 16;
            strip.setPixelColor(11 + sel16, HALFWHITE);
        }
    }
    void OmniNoteEditor::onKeyUpdate(OMXKeypadEvent e, Track *track)
    {
        if(e.held()) return;

        uint8_t thisKey = e.key();

        if(thisKey == 0) return;

        if(e.down())
        {
            // Set rest, no notes
            if(thisKey == 11)
            {
                for (uint8_t i = 0; i < 6; i++)
                {
                    track->steps[selStep_].notes[i] = -1;
                }
                return;
            }

            std::vector<uint8_t> heldKeys;

            for(uint8_t i = 1; i < 27; i++)
            {
                if(midiSettings.keyState[i])
                {
                    // Serial.println("HeldKey: " + String(i));
                    heldKeys.push_back(i);
                }
            }

            std::vector<uint8_t> noteNumbers;

            for (uint8_t i = 0; i < heldKeys.size(); i++)
            {
                int8_t noteNumber = omxUtil.getNoteNumber(heldKeys[i], omxFormGlobal.musicScale);

                if (noteNumber >= 0 && noteNumber <= 127)
                {
                    noteNumbers.push_back(noteNumber);
                }
            }

            std::sort(noteNumbers.begin(), noteNumbers.end());

            for(uint8_t i = 0; i < 6; i++)
            {
                if(i < noteNumbers.size())
                {
                    track->steps[selStep_].notes[i] = noteNumbers[i];
                }
                else
                {
                    track->steps[selStep_].notes[i] = -1;
                }
            }
        }
    }

    void OmniNoteEditor::onKeyHeldUpdate(OMXKeypadEvent e, Track *track)
    {
    }
    void OmniNoteEditor::onDisplayUpdate(Track *track)
    {
        auto trackNotes = track->steps[selStep_].notes;

        // omxDisp.setLegend(0, "NT 1", notes[0]);
        // omxDisp.setLegend(0, "NT 2", notes[0]);
        // omxDisp.setLegend(0, "NT 3", notes[0]);
        // omxDisp.setLegend(0, "NT 4", notes[0]);

        tempString = "";
        int8_t noteKeys[6];

        for (uint8_t i = 0; i < 6; i++)
        {
            int8_t note = trackNotes[i];

            if (note >= 0 && note <= 127)
            {
                noteKeys[i] = omxUtil.noteNumberToKeyNumber(note);
                tempString += omxFormGlobal.musicScale->getFullNoteName(note);
            }
            else
            {
                noteKeys[i] = -1;
            }
        }

        uint8_t step16 = selStep_ % 16 + 1;
        uint8_t bar = (selStep_) / 16 + 1;

        tempStrings[0] = String(step16) + ":" + String(bar);

        const char *labels[2];
        labels[0] = tempStrings[0].c_str();
        labels[1] = tempString.c_str();

        omxDisp.dispSeqKeyboard(noteKeys, true, labels, 2);

        // omxDisp.dispGenericModeLabel("Note Editor");
    }
    void OmniNoteEditor::onEncoderChangedSelectParam(Encoder::Update enc, Track *track)
    {
    }
    void OmniNoteEditor::onEncoderChangedEditParam(Encoder::Update enc, Track *track)
    {
        int8_t amt = enc.accel(1);
        selStep_ = constrain(selStep_ + amt, 0, 64-1);
    }

    OmniNoteEditor omniNoteEditor;
}