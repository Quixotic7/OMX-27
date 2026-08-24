#include "omx_mode_config.h"
#include "../config.h"
#include "../globals.h"
#include "../consts/colors.h"
#include "../hardware/omx_disp.h"
#include "../hardware/omx_leds.h"
#include "../utils/omx_util.h"
#include "../utils/cvNote_util.h"
#include "sequencer.h"

// saveToStorage() is a free function defined in the main .ino (saves header +
// patterns). Forward-declared so the Save action can trigger a full save.
void saveToStorage(void);

enum ConfigPage
{
	CFGPAGE_CLOCK,
	CFGPAGE_MIDI,
	CFGPAGE_SCALE,
	CFGPAGE_CV,	   // In->CV, Trigger, -, Pots (launches pot config)
	CFGPAGE_DISPLAY, // LED brightness, Screensaver, Timeout
	CFGPAGE_SYSTEM, // Device ID, -, Save, Clear Storage
	CFGPAGE_VERSION, // full-screen version label, like MIDI mode's last page
	CFGPAGE_COUNT
};

OmxModeConfig::OmxModeConfig()
{
	params.addPage(4); // CLOCK  : Tempo, Source, Send, Quantize
	params.addPage(4); // MIDI   : Channel, Thru, Macro, Macro Ch
	params.addPage(4); // SCALE  : Root, Scale, Lock, Group
	params.addPage(4); // CV     : In->CV, Trigger, [gap], Pots
	params.addPage(3); // DISPLAY: LED Bright, Screensaver, Timeout
	params.addPage(4); // SYSTEM : Device ID, [gap], Save, Clear Storage
	params.addPage(1); // VERSION: rendered as a label
}

void OmxModeConfig::InitSetup()
{
	initSetup = true;
}

void OmxModeConfig::onModeActivated()
{
	if (!initSetup)
	{
		InitSetup();
	}
	disableSubmode();
	heldParam_ = -1;
	encoderSelect = true;
	midiSettings.midiAUX = false;
	params.setSelPageAndParam(0, 0);
	omxDisp.setDirty();
	omxLeds.setDirty();
}

void OmxModeConfig::onModeDeactivated()
{
	disableSubmode();
	midiSettings.midiAUX = false;
}

// ---------------------------------------------------------------------------
// Submode management (pot config / clear storage)
// ---------------------------------------------------------------------------
void OmxModeConfig::enableSubmode(SubmodeInterface *subMode)
{
	if (activeSubmode != nullptr)
		activeSubmode->setEnabled(false);
	activeSubmode = subMode;
	activeSubmode->setEnabled(true);
	omxDisp.setDirty();
	omxLeds.setDirty();
}

void OmxModeConfig::disableSubmode()
{
	if (activeSubmode != nullptr)
		activeSubmode->setEnabled(false);
	activeSubmode = nullptr;
}

bool OmxModeConfig::isSubmodeEnabled()
{
	if (activeSubmode == nullptr)
		return false;
	if (activeSubmode->isEnabled() == false)
	{
		disableSubmode();
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Params
// ---------------------------------------------------------------------------
bool OmxModeConfig::inEditMode()
{
	return heldParam_ >= 0 || midiSettings.midiAUX || !encoderSelect;
}

bool OmxModeConfig::isActionParam(int8_t page, int8_t param)
{
	if (page == CFGPAGE_CV && param == 3)
		return true; // Pots -> launch pot config
	if (page == CFGPAGE_SYSTEM && (param == 2 || param == 3))
		return true; // Save / Clear Storage
	return false;
}

bool OmxModeConfig::isGapParam(int8_t page, int8_t param)
{
	if (page == CFGPAGE_CV && param == 2)
		return true;
	if (page == CFGPAGE_SYSTEM && param == 1)
		return true;
	return false;
}

void OmxModeConfig::navParam(int8_t dir)
{
	if (dir == 0)
		dir = 1;
	for (uint8_t i = 0; i < CFGPAGE_COUNT * 4 + 1; i++)
	{
		params.changeParam(dir);
		if (!isGapParam(params.getSelPage(), params.getSelParam()))
			break;
	}
	omxDisp.setDirty();
	omxLeds.setDirty();
}

void OmxModeConfig::doAction(int8_t page, int8_t param)
{
	if (page == CFGPAGE_CV && param == 3) // Pot config
	{
		enableSubmode(&omxUtil.subModePotConfig);
	}
	else if (page == CFGPAGE_SYSTEM && param == 2) // Save (blocks for a bit)
	{
		omxDisp.displayMessage("Saving");
		omxDisp.forceShowDisplay(); // push "Saving" to the OLED before the blocking save
		saveToStorage();
		omxDisp.displayMessage("Saved");
	}
	else if (page == CFGPAGE_SYSTEM && param == 3) // Clear Storage
	{
		enableSubmode(&omxUtil.subModeClearStorage);
	}
	omxDisp.setDirty();
	omxLeds.setDirty();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
void OmxModeConfig::onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta)
{
	if (isSubmodeEnabled() && activeSubmode->usesPots())
	{
		activeSubmode->onPotChanged(potIndex, prevValue, newValue, analogDelta);
		return;
	}
}

void OmxModeConfig::loopUpdate(Micros elapsedTime)
{
	if (isSubmodeEnabled())
	{
		activeSubmode->loopUpdate();
	}
}

void OmxModeConfig::onEncoderChanged(Encoder::Update enc)
{
	if (isSubmodeEnabled())
	{
		activeSubmode->onEncoderChanged(enc);
		return;
	}

	if (!inEditMode())
	{
		navParam(enc.dir());
		return;
	}

	onEncoderChangedEditParam(enc);
	omxDisp.setDirty();
}

void OmxModeConfig::onEncoderChangedEditParam(Encoder::Update enc)
{
	int8_t amt = enc.accel(5);
	int8_t page = params.getSelPage();
	int8_t param = params.getSelParam();

	switch (page)
	{
	case CFGPAGE_CLOCK:
		if (param == 0) // Tempo
		{
			clockConfig.newtempo = constrain(clockConfig.clockbpm + amt, 40, 300);
			if (clockConfig.newtempo != clockConfig.clockbpm)
			{
				clockConfig.clockbpm = clockConfig.newtempo;
				omxUtil.resetClocks();
			}
		}
		else if (param == 1) // Clock source
			sequencer.clockSource = constrain(sequencer.clockSource + amt, 0, 1);
		else if (param == 2) // Send always
			clockConfig.send_always = constrain(clockConfig.send_always + amt, 0, 1);
		else if (param == 3) // Quantize
			clockConfig.globalQuantizeStepIndex = constrain(clockConfig.globalQuantizeStepIndex + amt, 0, kNumArpRates - 1);
		break;
	case CFGPAGE_MIDI:
		if (param == 0)
			sysSettings.midiChannel = constrain(sysSettings.midiChannel + amt, 1, 16);
		else if (param == 1)
			midiSettings.midiSoftThru = constrain(midiSettings.midiSoftThru + amt, 0, 1);
		else if (param == 2)
			midiMacroConfig.midiMacro = constrain(midiMacroConfig.midiMacro + amt, 0, nummacromodes);
		else if (param == 3)
			midiMacroConfig.midiMacroChan = constrain(midiMacroConfig.midiMacroChan + amt, 1, 16);
		break;
	case CFGPAGE_SCALE:
		if (param == 0)
		{
			int prevRoot = scaleConfig.scaleRoot;
			scaleConfig.scaleRoot = constrain(scaleConfig.scaleRoot + amt, 0, 12 - 1);
			if (musicScale_ != nullptr && prevRoot != scaleConfig.scaleRoot)
				musicScale_->calculateScale(scaleConfig.scaleRoot, scaleConfig.scalePattern);
		}
		else if (param == 1)
		{
			int prevPat = scaleConfig.scalePattern;
			int maxPat = musicScale_ != nullptr ? musicScale_->getNumScales() - 1 : 0;
			scaleConfig.scalePattern = constrain(scaleConfig.scalePattern + amt, -1, maxPat);
			if (prevPat != scaleConfig.scalePattern)
			{
				if (musicScale_ != nullptr)
					musicScale_->calculateScale(scaleConfig.scaleRoot, scaleConfig.scalePattern);
				if (scaleConfig.scalePattern < 0)
				{
					if (prevPat >= 0)
					{
						scaleConfig.lockedState = scaleConfig.lockScale;
						scaleConfig.groupedState = scaleConfig.group16;
					}
					scaleConfig.lockScale = false;
					scaleConfig.group16 = false;
				}
				else if (prevPat < 0)
				{
					scaleConfig.lockScale = scaleConfig.lockedState;
					scaleConfig.group16 = scaleConfig.groupedState;
				}
			}
		}
		else if (param == 2)
		{
			if (scaleConfig.scalePattern >= 0)
				scaleConfig.lockScale = constrain(scaleConfig.lockScale + amt, 0, 1);
		}
		else if (param == 3)
		{
			if (scaleConfig.scalePattern >= 0)
				scaleConfig.group16 = constrain(scaleConfig.group16 + amt, 0, 1);
		}
		break;
	case CFGPAGE_CV:
		if (param == 0)
			midiSettings.midiInToCV = constrain(midiSettings.midiInToCV + amt, 0, 1);
		else if (param == 1)
			cvNoteUtil.triggerMode = constrain(cvNoteUtil.triggerMode + amt, 0, 1);
		break;
	case CFGPAGE_DISPLAY:
		if (param == 0) // LED brightness
		{
			ledBrightness = constrain(ledBrightness + amt, 5, 255);
			strip.setBrightness(ledBrightness);
			omxLeds.setDirty();
		}
		else if (param == 1) // Screensaver on/off
			screensaverEnabled = constrain(screensaverEnabled + amt, 0, 1);
		else if (param == 2) // Screensaver timeout (seconds)
			screensaverTimeoutSec = constrain(screensaverTimeoutSec + amt * 5, 5, 3600);
		break;
	case CFGPAGE_SYSTEM:
		if (param == 0) // Device ID
			deviceID = constrain(deviceID + amt, 0, 127);
		break;
	default:
		break;
	}
}

void OmxModeConfig::onEncoderButtonDown()
{
	if (isSubmodeEnabled())
	{
		activeSubmode->onEncoderButtonDown();
		return;
	}

	int8_t page = params.getSelPage();
	int8_t param = params.getSelParam();
	if (isActionParam(page, param))
	{
		doAction(page, param);
		return;
	}

	encoderSelect = !encoderSelect;
	omxDisp.setDirty();
}

bool OmxModeConfig::shouldBlockEncEdit()
{
	if (isSubmodeEnabled())
		return activeSubmode->shouldBlockEncEdit();
	return false;
}

void OmxModeConfig::onKeyUpdate(OMXKeypadEvent e)
{
	if (isSubmodeEnabled())
	{
		if (activeSubmode->onKeyUpdate(e))
			return;
	}

	uint8_t thisKey = e.key();

	if (thisKey == 0) // AUX
	{
		midiSettings.midiAUX = e.down();
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}

	if (e.down())
	{
		if (thisKey == 1)
		{
			navParam(-1);
		}
		else if (thisKey == 2)
		{
			navParam(1);
		}
		else if (thisKey >= 19 && thisKey <= 22)
		{
			int8_t p = (int8_t)(thisKey - 19);
			int8_t page = params.getSelPage();
			if (p < (int8_t)params.getNumOfParamsForPage(page) && !isGapParam(page, p))
			{
				if (isActionParam(page, p))
				{
					doAction(page, p);
				}
				else
				{
					params.setSelParam(p);
					heldParam_ = p;
				}
			}
		}
	}
	else // key up
	{
		if (thisKey >= 19 && thisKey <= 22 && heldParam_ == (int8_t)(thisKey - 19))
		{
			heldParam_ = -1;
		}
	}

	omxDisp.setDirty();
	omxLeds.setDirty();
}

void OmxModeConfig::onKeyHeldUpdate(OMXKeypadEvent e)
{
	if (isSubmodeEnabled())
	{
		activeSubmode->onKeyHeldUpdate(e);
	}
}

// ---------------------------------------------------------------------------
// LEDs
// ---------------------------------------------------------------------------
void OmxModeConfig::updateLEDs()
{
	if (isSubmodeEnabled())
	{
		if (activeSubmode->updateLEDs())
			return;
	}

	omxLeds.setAllLEDS(0, 0, 0);

	strip.setPixelColor(0, midiSettings.midiAUX ? RED : LOWWHITE); // AUX
	strip.setPixelColor(1, LOWWHITE);							  // prev param
	strip.setPixelColor(2, LOWWHITE);							  // next param

	int8_t page = params.getSelPage();
	int8_t sel = params.getSelParam();
	if (page != CFGPAGE_VERSION)
	{
		uint8_t np = params.getNumOfParamsForPage(page);
		for (uint8_t p = 0; p < np && p < 4; p++)
		{
			if (isGapParam(page, p))
				continue; // leave gap key dark
			uint32_t col = isActionParam(page, p) ? ORANGE : (p == sel ? WHITE : BLUE);
			strip.setPixelColor(19 + p, col);
		}
	}
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
void OmxModeConfig::onDisplayUpdate()
{
	// Submode (pot config / clear storage) drives its own screen + LEDs.
	if (isSubmodeEnabled())
	{
		activeSubmode->updateLEDs();
		activeSubmode->onDisplayUpdate();
		return;
	}

	// Modes paint their own LEDs from onDisplayUpdate (the loop only pushes the strip).
	if (omxLeds.isDirty())
	{
		updateLEDs();
	}

	if (encoderConfig.enc_edit)
		return; // mode-select menu is showing

	if (!omxDisp.isDirty())
		return;

	int8_t page = params.getSelPage();

	// Version page: full-screen label, like the last page of MIDI mode.
	if (page == CFGPAGE_VERSION)
	{
		tempString = "v" + String(MAJOR_VERSION) + "." + String(MINOR_VERSION) + "." + String(POINT_VERSION);
		omxDisp.dispGenericModeLabel(tempString.c_str(), params.getNumPages(), page);
		return;
	}

	omxDisp.clearLegends();

	switch (page)
	{
	case CFGPAGE_CLOCK:
		omxDisp.setLegend(0, "BPM", (int)clockConfig.clockbpm);
		omxDisp.setLegend(1, "CLK", sequencer.clockSource ? "Ext" : "Int");
		omxDisp.setLegend(2, "SEND", clockConfig.send_always ? "ON" : "OFF");
		omxDisp.setLegend(3, "QNT", "1/" + String(kArpRates[clockConfig.globalQuantizeStepIndex]));
		break;
	case CFGPAGE_MIDI:
		omxDisp.setLegend(0, "CH", (int)sysSettings.midiChannel);
		omxDisp.setLegend(1, "THRU", midiSettings.midiSoftThru ? "ON" : "OFF");
		omxDisp.setLegend(2, "MCRO", macromodes[midiMacroConfig.midiMacro]);
		omxDisp.setLegend(3, "M-CH", midiMacroConfig.midiMacroChan);
		break;
	case CFGPAGE_SCALE:
		omxDisp.setLegend(0, "ROOT", musicScale_ != nullptr ? musicScale_->getNoteName(scaleConfig.scaleRoot) : "-");
		omxDisp.setLegend(1, "SCALE", (scaleConfig.scalePattern < 0 || musicScale_ == nullptr) ? "Off" : musicScale_->getScaleName(scaleConfig.scalePattern));
		omxDisp.setLegend(2, "LOCK", scaleConfig.lockScale ? "ON" : "OFF");
		omxDisp.setLegend(3, "GRP", scaleConfig.group16 ? "ON" : "OFF");
		break;
	case CFGPAGE_CV:
		omxDisp.setLegend(0, "InCV", midiSettings.midiInToCV ? "ON" : "OFF");
		omxDisp.setLegend(1, "TRIG", cvNoteUtil.getTriggerModeDispName());
		omxDisp.setLegend(3, "POTS", "Edit");
		break;
	case CFGPAGE_DISPLAY:
		omxDisp.setLegend(0, "BRHT", (int)ledBrightness);
		omxDisp.setLegend(1, "SAVR", screensaverEnabled ? "ON" : "OFF");
		omxDisp.setLegend(2, "TIME", (int)screensaverTimeoutSec);
		break;
	case CFGPAGE_SYSTEM:
		omxDisp.setLegend(0, "DEV", (int)deviceID);
		omxDisp.setLegend(2, "SAVE", "SAV");
		omxDisp.setLegend(3, "CLR", "STOR");
		break;
	default:
		break;
	}

	omxDisp.dispGenericMode2(params.getNumPages(), page, params.getSelParam(), !inEditMode());
}
