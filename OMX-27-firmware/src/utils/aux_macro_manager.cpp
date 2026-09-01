#include "aux_macro_manager.h"
#include "../globals.h" // sysSettings/potSettings/midiMacroConfig moved here in the q7/RP2040 restructure
#include "../utils/omx_util.h"
#include "../hardware/omx_disp.h"
#include "../hardware/omx_leds.h"
#include "../midimacro/midimacro_m8.h"
#include "../midimacro/midimacro_norns.h"
#include "../midimacro/midimacro_deluge.h"

// These are static and shared between different modes
midimacro::MidiMacroNorns nornsMarco_;
midimacro::MidiMacroM8 m8Macro_;
midimacro::MidiMacroDeluge delugeMacro_;

AuxMacroManager::AuxMacroManager()
{
}

bool AuxMacroManager::isMacroActive()
{
    return macroActive_;
}

bool AuxMacroManager::isMFXQuickEditEnabled()
{
    return mfxQuickEdit_;
}

bool AuxMacroManager::doesActiveMacroConsomeDisplay()
{
    if (macroActive_ && activeMacro_ != nullptr)
    {
        return activeMacro_->consumesDisplay();
    }
    return false;
}

bool AuxMacroManager::isMidiFXGroupIndexValid(uint8_t mfxIndex)
{
    return mfxIndex < NUM_MIDIFX_GROUPS;
}

bool AuxMacroManager::shouldBlockEncEdit()
{
    if (isSubmodeEnabled())
        return activeSubmode->shouldBlockEncEdit();

    if (isMacroActive())
        return true;

    return false;
}

void AuxMacroManager::onModeActivated()
{
    // Called when the OMX mode is activated, remap the macros to this macro manager
    m8Macro_.setDoNoteOn(&AuxMacroManager::doNoteOnForwarder, this);
    m8Macro_.setDoNoteOff(&AuxMacroManager::doNoteOffForwarder, this);
    nornsMarco_.setDoNoteOn(&AuxMacroManager::doNoteOnForwarder, this);
    nornsMarco_.setDoNoteOff(&AuxMacroManager::doNoteOffForwarder, this);
    delugeMacro_.setDoNoteOn(&AuxMacroManager::doNoteOnForwarder, this);
    delugeMacro_.setDoNoteOff(&AuxMacroManager::doNoteOffForwarder, this);
}
void AuxMacroManager::onModeDectivated()
{
}

void AuxMacroManager::setContext(void *context)
{
    context_ = context;
}

void AuxMacroManager::setMacroNoteOn(void (*fptr)(void *, uint8_t))
{
    doNoteOnFptr_ = fptr;
}

void AuxMacroManager::setMacroNoteOff(void (*fptr)(void *, uint8_t))
{
    doNoteOffFptr_ = fptr;
}

void AuxMacroManager::SetScale(MusicScales *scale)
{
    musicScale_ = scale;

    m8Macro_.setScale(scale);
    nornsMarco_.setScale(scale);
    delugeMacro_.setScale(scale);
}

bool AuxMacroManager::inMidiControlChange(byte channel, byte control, byte value)
{
    auto activeMacro = getActiveMacro();

    if (activeMacro != nullptr)
    {
        activeMacro->inMidiControlChange(channel, control, value);
        return true;
    }

    return false;
}

void AuxMacroManager::doMacroNoteOn(uint8_t keyIndex)
{
    if (context_ != nullptr)
    {
        doNoteOnFptr_(context_, keyIndex);
    }
}

void AuxMacroManager::doMacroNoteOff(uint8_t keyIndex)
{
    if (context_ != nullptr)
    {
        doNoteOffFptr_(context_, keyIndex);
    }
}

void AuxMacroManager::setSelectMidiFXFPTR(void (*fptr)(void *, uint8_t, bool))
{
    selectMidiFxFptr_ = fptr;
}

void AuxMacroManager::selectMidiFx(uint8_t mfxIndex, bool dispMsg)
{
    if(isMFXQuickEditEnabled())
	{
		// Change the MidiFX Group being edited
		if(isMidiFXGroupIndexValid(mfxIndex) && mfxIndex != quickEditMfxIndex_)
		{
			enableSubmode(&subModeMidiFx[mfxIndex]);
			subModeMidiFx[mfxIndex].enablePassthrough();
			quickEditMfxIndex_ = mfxIndex;
			dispMsg = false;
		}
		else if(mfxIndex >= NUM_MIDIFX_GROUPS)
		{
			disableSubmode();
		}
	}

    if (context_ != nullptr)
    {
        selectMidiFxFptr_(context_, mfxIndex, dispMsg);
    }
}

void AuxMacroManager::enableSubmode(SubmodeInterface *subMode)
{
    if (activeSubmode != nullptr)
    {
        activeSubmode->setEnabled(false);
    }

    activeSubmode = subMode;
    activeSubmode->setEnabled(true);
    omxDisp.setDirty();
}
void AuxMacroManager::disableSubmode()
{
    if (activeSubmode != nullptr)
    {
        activeSubmode->setEnabled(false);
    }

    midiSettings.midiAUX = false;
    mfxQuickEdit_ = false;
    activeSubmode = nullptr;
    omxDisp.setDirty();
}

bool AuxMacroManager::isSubmodeEnabled()
{
    if (activeSubmode == nullptr)
        return false;

    if (activeSubmode->isEnabled() == false)
    {
        disableSubmode();
        midiSettings.midiAUX = false;
        return false;
    }

    return true;
}

midimacro::MidiMacroInterface *AuxMacroManager::getActiveMacro()
{
    switch (midiMacroConfig.midiMacro)
    {
    case 1:
        return &m8Macro_;
    case 2:
        return &nornsMarco_;
    case 3:
        return &delugeMacro_;
    }
    return nullptr;
}

bool AuxMacroManager::onEncoderChanged(Encoder::Update enc)
{
    if (isSubmodeEnabled())
	{
		activeSubmode->onEncoderChanged(enc);
		return true;
	}

    if (doesActiveMacroConsomeDisplay())
    {
        activeMacro_->onEncoderChanged(enc);
        return true;
    }

    return false;
}

bool AuxMacroManager::onEncoderButtonDown()
{
    if (isSubmodeEnabled())
	{
		activeSubmode->onEncoderButtonDown();
		return true;
	}

    if (doesActiveMacroConsomeDisplay())
    {
        activeMacro_->onEncoderButtonDown();
        return true;
    }

    return false;
}

bool AuxMacroManager::onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta)
{
    if (isSubmodeEnabled() && activeSubmode->usesPots())
    {
        activeSubmode->onPotChanged(potIndex, prevValue, newValue, analogDelta);
        return true;
    }

    auto activeMacro = getActiveMacro();

    bool macroConsumesPots = false;
    if (macrosConsumePots_ && activeMacro != nullptr)
    {
        macroConsumesPots = activeMacro->consumesPots();
    }

    // Note, these get sent even if macro mode is not active
    if (macroConsumesPots)
    {
        activeMacro->onPotChanged(potIndex, prevValue, newValue, analogDelta);
        omxDisp.setDirty();
        return true;
    }

    return false;
}

bool AuxMacroManager::onKeyUpdate(OMXKeypadEvent e)
{
    if (isSubmodeEnabled())
    {
        if (activeSubmode->onKeyUpdate(e))
            return true; // Key consumed by submode

        return false;
    }

    if (midiMacroConfig.midiMacro == 0)
        return false;

    int thisKey = e.key();

    // Aux double click toggle macro
    if (!macroActive_)
    {
        // Enter M8 Mode
        if (!e.down() && thisKey == 0 && e.clicks() == 2)
        {
            midiSettings.midiAUX = false;

            activeMacro_ = getActiveMacro();
            if (activeMacro_ != nullptr)
            {
                macroActive_ = true;
                activeMacro_->setEnabled(true);
                // activeMacro_->setScale(musicScale); // Shouldn't be needed
                omxLeds.setDirty();
                omxDisp.setDirty();
                return true;
            }
            return false;
        }
    }
    else // Macro mode active
    {
        if (!e.down() && thisKey == 0 && e.clicks() == 2)
        {
            // exit macro mode
            if (activeMacro_ != nullptr)
            {
                activeMacro_->setEnabled(false);
                activeMacro_ = nullptr;
            }

            midiSettings.midiAUX = false;
            macroActive_ = false;
            omxLeds.setAllLEDS(0,0,0);
            omxLeds.setDirty();
            omxDisp.setDirty();
        }
        else
        {
            if (activeMacro_ != nullptr)
            {
                activeMacro_->onKeyUpdate(e);
            }
        }
        return true;
    }

    return false;
}

bool AuxMacroManager::onKeyHeldUpdate(OMXKeypadEvent e)
{
    if (isSubmodeEnabled())
	{
		activeSubmode->onKeyHeldUpdate(e);
		return true;
	}

    return false;
}

bool AuxMacroManager::onKeyUpdateAuxMFXShortcuts(OMXKeypadEvent e, uint8_t selMFXIndex)
{
    int thisKey = e.key();

    bool keyConsumed = false;

    if (!e.held())
    {
        // Double Click to edit midi fx
        if (!e.down() && e.clicks() == 2 && thisKey >= 6 && thisKey < 11)
        {
            if (midiSettings.midiAUX) // AUX Key is held
            {
                // Enter the full MidiFX editor
                enableSubmode(&subModeMidiFx[thisKey - 6]);
                keyConsumed = true;
            }
        }

        if (e.down() && thisKey != 0)
        {
            if (midiSettings.midiAUX) // Aux mode
            {
                if (mfxQuickEdit_ && thisKey == 1)
                {
                    keyConsumed = true;
                    subModeMidiFx[quickEditMfxIndex_].selectPrevMFXSlot();
                }
                else if (mfxQuickEdit_ && thisKey == 2)
                {
                    keyConsumed = true;
                    subModeMidiFx[quickEditMfxIndex_].selectNextMFXSlot();
                }
                else if (thisKey == 5)
                {
                    keyConsumed = true;
                    // Turn off midiFx
                    selectMidiFx(127, true);
                }
                else if (thisKey >= 6 && thisKey < 11)
                {
                    keyConsumed = true;
                    // Change active midiFx
                    selectMidiFx(thisKey - 6, true);
                }
                else if (thisKey == 20) // MidiFX Passthrough
                {
                    keyConsumed = true;
                    if (isMidiFXGroupIndexValid(selMFXIndex))
                    {
                        enableSubmode(&subModeMidiFx[selMFXIndex]);
                        subModeMidiFx[selMFXIndex].enablePassthrough();
                        mfxQuickEdit_ = true;
                        quickEditMfxIndex_ = selMFXIndex;
                        midiSettings.midiAUX = false;
                        omxDisp.displayMessageTimed("MFX PASS", 5);
                    }
                    else
                    {
                        omxDisp.displayMessage(mfxOffMsg);
                    }
                }
                else if (thisKey == 21) // Bypass-toggle the group's selected FX slot on/off
                {
                    keyConsumed = true;
                    if (isMidiFXGroupIndexValid(selMFXIndex))
                    {
                        subModeMidiFx[selMFXIndex].toggleSelectedFXBypass();
                    }
                    else
                    {
                        omxDisp.displayMessage(mfxOffMsg);
                    }
                }
                else if (thisKey == 22) // Goto arp params
                {
                    keyConsumed = true;
                    if (isMidiFXGroupIndexValid(selMFXIndex))
                    {
                        enableSubmode(&subModeMidiFx[selMFXIndex]);
                        subModeMidiFx[selMFXIndex].gotoArpParams();
                        midiSettings.midiAUX = false;
                    }
                    else
                    {
                        omxDisp.displayMessage(mfxOffMsg);
                    }
                }
                else if (thisKey == 23) // Next arp pattern
                {
                    keyConsumed = true;
                    if (isMidiFXGroupIndexValid(selMFXIndex))
                    {
                        subModeMidiFx[selMFXIndex].nextArpPattern();
                    }
                    else
                    {
                        omxDisp.displayMessage(mfxOffMsg);
                    }
                }
                else if (thisKey == 24) // Next arp octave
                {
                    keyConsumed = true;
                    if (isMidiFXGroupIndexValid(selMFXIndex))
                    {
                        subModeMidiFx[selMFXIndex].nextArpOctRange();
                    }
                    else
                    {
                        omxDisp.displayMessage(mfxOffMsg);
                    }
                }
                else if (thisKey == 25)
                {
                    keyConsumed = true;
                    if (isMidiFXGroupIndexValid(selMFXIndex))
                    {
                        subModeMidiFx[selMFXIndex].toggleArpHold();

                        if (subModeMidiFx[selMFXIndex].isArpHoldOn())
                        {
                            omxDisp.displayMessageTimed("Arp Hold: On", 5);
                        }
                        else
                        {
                            omxDisp.displayMessageTimed("Arp Hold: Off", 5);
                        }
                    }
                    else
                    {
                        omxDisp.displayMessage(mfxOffMsg);
                    }
                }
                else if (thisKey == 26)
                {
                    keyConsumed = true;
                    if (isMidiFXGroupIndexValid(selMFXIndex))
                    {
                        subModeMidiFx[selMFXIndex].toggleArp();

                        if (subModeMidiFx[selMFXIndex].isArpOn())
                        {
                            omxDisp.displayMessageTimed("Arp On", 5);
                        }
                        else
                        {
                            omxDisp.displayMessageTimed("Arp Off", 5);
                        }
                    }
                    else
                    {
                        omxDisp.displayMessage(mfxOffMsg);
                    }
                }
            }
        }
    }

    return keyConsumed;
}

bool AuxMacroManager::onKeyHeldAuxMFXShortcuts(OMXKeypadEvent e, uint8_t selMFXIndex)
{
    int thisKey = e.key();

	bool keyConsumed = false;

	if (midiSettings.midiAUX) // Aux mode
	{
		// Enter MidiFX mode
		if (thisKey >= 6 && thisKey < 11)
		{
			keyConsumed = true;
			enableSubmode(&subModeMidiFx[thisKey - 6]);
		}
	}

	return keyConsumed;
}

bool AuxMacroManager::updateLEDs()
{
    if (isSubmodeEnabled())
	{
        bool blinkStateSlow = omxLeds.getSlowBlinkState();

		auto auxColor = (blinkStateSlow ? RED : LEDOFF);
		strip.setPixelColor(0, auxColor);
        
		if (activeSubmode->updateLEDs())
			return true;
	}

    // Macros always consome LEDs
    if (macroActive_ && activeMacro_ != nullptr)
    {
        if (omxLeds.isDirty())
        {
            activeMacro_->drawLEDs();
        }
        return true;
    }

    return false;
}

bool AuxMacroManager::onDisplayUpdate()
{
    if (isSubmodeEnabled())
	{
		activeSubmode->onDisplayUpdate();
        return true;
	}

    if (doesActiveMacroConsomeDisplay())
    {
        activeMacro_->onDisplayUpdate();
        return true;
    }

    return false;
}

void AuxMacroManager::UpdateAUXLEDS(uint8_t selectedMidiFX)
{
    bool blinkState = omxLeds.getBlinkState();
    // bool slowBlink = omxLeds.getSlowBlinkState();

    // auto color1 = LIME;
    // auto color2 = MAGENTA;

    strip.setPixelColor(0, RED);
    strip.setPixelColor(1, LIME);
    strip.setPixelColor(2, MAGENTA);

    // Blink left/right keys for octave select indicators.
    omxLeds.drawOctaveKeys(11, 12, midiSettings.octave);

    // MidiFX off
    strip.setPixelColor(5, (selectedMidiFX >= NUM_MIDIFX_GROUPS ? colorConfig.selMidiFXGRPOffColor : colorConfig.midiFXGRPOffColor));

    for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
    {
        auto mfxColor = (i == selectedMidiFX) ? colorConfig.selMidiFXGRPColor : colorConfig.midiFXGRPColor;

        strip.setPixelColor(6 + i, mfxColor);
    }

    strip.setPixelColor(20, mfxQuickEdit_ && blinkState ? LEDOFF : colorConfig.mfxQuickEdit);
    strip.setPixelColor(22, colorConfig.gotoArpParams);
    strip.setPixelColor(23, colorConfig.nextArpPattern);

    SubModeMidiFxGroup *selMFXGroup = selectedMidiFX < NUM_MIDIFX_GROUPS ? &subModeMidiFx[selectedMidiFX] : nullptr;

    if (selectedMidiFX < NUM_MIDIFX_GROUPS && selMFXGroup != nullptr)
    {
        uint8_t octaveRange = selMFXGroup->getArpOctaveRange();
        if (octaveRange == 0)
        {
            strip.setPixelColor(24, colorConfig.nextArpOctave);
        }
        else
        {
            // Serial.println("Blink Octave: " + String(octaveRange));
            bool blinkOctave = omxLeds.getBlinkPattern(octaveRange);

            strip.setPixelColor(24, blinkOctave ? colorConfig.nextArpOctave : LEDOFF);
        }

        bool isOn = selMFXGroup->isArpOn() && blinkState;
        bool isHoldOn = selMFXGroup->isArpHoldOn();

        strip.setPixelColor(25, isHoldOn ? colorConfig.arpHoldOn : colorConfig.arpHoldOff);
        strip.setPixelColor(26, isOn ? colorConfig.arpOn : colorConfig.arpOff);
    }
    else
    {
        strip.setPixelColor(25, colorConfig.arpHoldOff);
        strip.setPixelColor(26, colorConfig.arpOff);
    }
}