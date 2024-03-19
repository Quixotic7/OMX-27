#include "omx_mode_form.h"
#include "../config.h"
#include "../consts/colors.h"
#include "../utils/omx_util.h"
#include "../utils/cvNote_util.h"
#include "../hardware/omx_disp.h"
#include "../hardware/omx_leds.h"
#include "../midi/midi.h"
#include "../utils/music_scales.h"
#include "../midi/noteoffs.h"
#include "machines/form_machine_null.h"
#include "machines/form_machine_omni.h"
#include "omx_form_global.h"

enum FormModePage
{
	FORMPAGE_INSPECT,		// Sent Pot CC, Last Note, Last Vel, Last Chan, Not editable, just FYI
	FORMPAGE_DRUMKEY,		// Note, Chan, Vel, MidiFX
	FORMPAGE_DRUMKEY2,		// Hue, RND Hue, Copy, Paste
	FORMPAGE_SCALES,		// Hue,
	FORMPAGE_POTSANDMACROS, // PotBank, Thru, Macro, Macro Channel
	FORMPAGE_CFG,
	FORMPAGE_NUMPAGES
};

const char *kMachineNames[FORMMACH_COUNT] = {"NONE", "OMNI"};

const int kMachineColors[FORMMACH_COUNT] = {LEDOFF, ORANGE};

OmxModeForm::OmxModeForm()
{
	params.addPages(FORMPAGE_NUMPAGES);

	auxMacroManager_.setContext(this);
	auxMacroManager_.setMacroNoteOn(&OmxModeForm::doNoteOnForwarder);
	auxMacroManager_.setMacroNoteOff(&OmxModeForm::doNoteOffForwarder);
	auxMacroManager_.setSelectMidiFXFPTR(&OmxModeForm::selectMidiFXForwarder);

	presetManager.setContextPtr(this);
	presetManager.setDoSaveFunc(&OmxModeForm::doSaveKitForwarder);
	presetManager.setDoLoadFunc(&OmxModeForm::doLoadKitForwarder);

	stopped_ = true;

	// Setup default machines
	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		machines_[i] = new FormOmni::FormMachineOmni();
		machines_[i]->setContext(this);
		machines_[i]->setNoteOnFptr(&OmxModeForm::seqNoteOnForwarder);
		machines_[i]->setNoteOffFptr(&OmxModeForm::seqNoteOffForwarder);
	}

	machines_[0]->setTest();

	selectMachine(0);

	ledUpdateTime_ = 0;

	// machines_[0]->setContext(this);
	// machines_[0]->setNoteOnFptr(&OmxModeForm::seqNoteOnForwarder);
	// machines_[0]->setNoteOffFptr(&OmxModeForm::seqNoteOffForwarder);



	// char foo[sizeof(OmxModeForm)]
}

OmxModeForm::~OmxModeForm()
{
	omxLeds.setBlinkAutoRefresh(true);
	cleanup();
}

void OmxModeForm::cleanup()
{
	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		if(machines_[i] != nullptr)
		{
			delete machines_[i];
			machines_[i] = nullptr;
		}
	}

	if(copyBuffer_ != nullptr)
	{
		delete copyBuffer_;
		copyBuffer_ = nullptr;
	}

	if(undoBuffer_ != nullptr)
	{
		delete undoBuffer_;
		undoBuffer_ = nullptr;
	}
}

void OmxModeForm::changeFormMode(uint8_t newFormMode)
{
	if (newFormMode < FORMMODE_COUNT)
	{
		omxFormGlobal.formMode = newFormMode;
	}
}

bool OmxModeForm::isMachineValid(uint8_t machineIndex)
{
	return machineIndex < kNumMachines;
}

const char *OmxModeForm::getMachineName(uint8_t machineIndex)
{
	if (isMachineValid(machineIndex) == false)
		return "";

	uint8_t mType = static_cast<uint8_t>(machines_[machineIndex]->getType());

	return kMachineNames[mType];
}

int OmxModeForm::getMachineColor(uint8_t machineIndex)
{
	if (isMachineValid(machineIndex) == false)
		return LEDOFF;

	uint8_t mType = static_cast<uint8_t>(machines_[machineIndex]->getType());

	return kMachineColors[mType];
}

void OmxModeForm::selectMachine(uint8_t machineIndex)
{
	if (isMachineValid(machineIndex) == false)
		return;

	selectedMachine_ = machineIndex;
	machines_[machineIndex]->onSelected();
}

FormMachineInterface *OmxModeForm::getSelectedMachine()
{
	return machines_[selectedMachine_];
}

void OmxModeForm::changeMachineAtIndex(uint8_t machineIndex, uint8_t machineType)
{
	if (isMachineValid(machineIndex) == false)
		return;

	switch (machineType)
	{
	case FORMMACH_COUNT:
	case FORMMACH_NULL:
	{
		// Serial.println("Setting machine at " + String(machineIndex) + " to FormMachineNull");
		setMachineTo(machineIndex, new FormMachineNull());
	}
	break;
	case FORMMACH_OMNI:
	{
		// Serial.println("Setting machine at " + String(machineIndex) + " to FormMachineOmni");
		setMachineTo(machineIndex, new FormOmni::FormMachineOmni());
	}
	break;
	}
}

void OmxModeForm::cutMachineAt(uint8_t machineIndex)
{
	if (isMachineValid(machineIndex) == false)
		return;

	copyMachineAt(machineIndex);
	setMachineTo(machineIndex, new FormMachineNull());
}

void OmxModeForm::copyMachineAt(uint8_t machineIndex)
{
	if (isMachineValid(machineIndex) == false)
		return;

	if (copyBuffer_ != nullptr)
	{
		delete copyBuffer_;
		copyBuffer_ = nullptr;
	}

	copyBuffer_ = machines_[machineIndex]->getClone();
}

void OmxModeForm::pasteMachineTo(uint8_t machineIndex)
{
	if (isMachineValid(machineIndex) == false)
		return;

	if (copyBuffer_ != nullptr)
	{
		setMachineTo(machineIndex, copyBuffer_->getClone());
	}
}

void OmxModeForm::setMachineTo(uint8_t machineIndex, FormMachineInterface *ptr)
{
	if (isMachineValid(machineIndex) == false)
		return;

	if (undoBuffer_ != nullptr)
	{
		delete undoBuffer_;
	}

	undoBuffer_ = machines_[machineIndex];

	machines_[machineIndex] = ptr;
	machines_[machineIndex]->setContext(this);
	machines_[machineIndex]->setMachineIndex(machineIndex);
	machines_[machineIndex]->setNoteOnFptr(&OmxModeForm::seqNoteOnForwarder);
	machines_[machineIndex]->setNoteOffFptr(&OmxModeForm::seqNoteOffForwarder);
}

void OmxModeForm::updateShortcutMode()
{
	if (omxFormGlobal.auxBlock && midiSettings.keyState[0] == false)
	{
		omxFormGlobal.auxBlock = false;
		omxDisp.setDirty();
		omxLeds.setDirty();
	}

	uint8_t prevMode = omxFormGlobal.shortcutMode;

	if (omxFormGlobal.shortcutMode != FORMSHORTCUT_AUX && midiSettings.keyState[1] && midiSettings.keyState[2])
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_F3;
	}
	else if (omxFormGlobal.shortcutMode != FORMSHORTCUT_AUX && midiSettings.keyState[1])
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_F1;
	}
	else if (omxFormGlobal.shortcutMode != FORMSHORTCUT_AUX && midiSettings.keyState[2])
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_F2;
	}
	else if (midiSettings.keyState[0])
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_AUX;
	}
	else
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_NONE;
	}

	if (prevMode != omxFormGlobal.shortcutMode)
	{
		omxFormGlobal.shortcutPaste = false;

		omxDisp.setDirty();
		omxLeds.setDirty();
	}
}

void OmxModeForm::InitSetup()
{
	initSetup = true;
}

void OmxModeForm::onModeActivated()
{
	// auto init when activated
	if (!initSetup)
	{
		InitSetup();
	}

	// sequencer.playing = false;
	stopSequencers();

	omxLeds.setDirty();
	omxDisp.setDirty();

	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		subModeMidiFx[i].setEnabled(true);
		subModeMidiFx[i].onModeChanged();
		subModeMidiFx[i].setNoteOutputFunc(&OmxModeForm::onNotePostFXForwarder, this);
	}

	pendingNoteOffs.setNoteOffFunction(&OmxModeForm::onPendingNoteOffForwarder, this);

	params.setSelPageAndParam(0, 0);
	omxFormGlobal.encoderSelect = true;

	// Serial.println("AuxMacroActivated");
	auxMacroManager_.onModeActivated();
	// Serial.println("onModeActivated complete");


	// activeDrumKit.CopyFrom(drumKits[selDrumKit]);

	// selectMidiFx(mfxIndex_, false);
}

void OmxModeForm::onModeDeactivated()
{
	stopSequencers();

	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		subModeMidiFx[i].setEnabled(false);
		subModeMidiFx[i].onModeChanged();
	}

	auxMacroManager_.onModeDectivated();
}

void OmxModeForm::stopSequencers()
{
	omxUtil.stopClocks();
	pendingNoteOffs.allOff();
}

void OmxModeForm::selectMidiFx(uint8_t mfxIndex, bool dispMsg)
{
	getSelectedMachine()->selectMidiFx(mfxIndex, dispMsg);

	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		subModeMidiFx[i].setSelected(i == mfxIndex);
	}

	// uint8_t prevMidiFX = activeDrumKit.drumKeys[selDrumKey].midifx;

	// if(mfxIndex != prevMidiFX && prevMidiFX < NUM_MIDIFX_GROUPS)
	// {
	//     drumKeyUp(selDrumKey + 1);
	// }

	// activeDrumKit.drumKeys[selDrumKey].midifx = mfxIndex;

	// if(mfxQuickEdit_)
	// {
	// 	// Change the MidiFX Group being edited
	// 	if(mfxIndex < NUM_MIDIFX_GROUPS && mfxIndex != quickEditMfxIndex_)
	// 	{
	// 		enableSubmode(&subModeMidiFx[mfxIndex]);
	// 		subModeMidiFx[mfxIndex].enablePassthrough();
	// 		quickEditMfxIndex_ = mfxIndex;
	// 		dispMsg = false;
	// 	}
	// 	else if(mfxIndex >= NUM_MIDIFX_GROUPS)
	// 	{
	// 		disableSubmode();
	// 	}
	// }

	

	// if (dispMsg)
	// {
	// 	if (mfxIndex < NUM_MIDIFX_GROUPS)
	// 	{
	// 		omxDisp.displayMessageTimed("MidiFX " + String(mfxIndex + 1), 5);
	// 	}
	// 	else
	// 	{
	// 		omxDisp.displayMessageTimed("MidiFX Off", 5);
	// 	}
	// }
}

void OmxModeForm::onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta)
{
	if (auxMacroManager_.onPotChanged(potIndex, prevValue, newValue, analogDelta))
		return;

	auto selMachine = getSelectedMachine();

	if(selMachine->doesConsumePots())
	{
		selMachine->onPotChanged(potIndex, prevValue, newValue, analogDelta);
		return;
	}

	omxUtil.sendPots(potIndex, sysSettings.midiChannel);
	omxDisp.setDirty();
}

void OmxModeForm::onClockTick()
{
	for(auto machine : machines_)
	{
		machine->onClockTick();
	}

	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		// Lets them do things in background
		subModeMidiFx[i].onClockTick();
	}
}

void OmxModeForm::loopUpdate(Micros elapsedTime)
{
	// Serial.println("LoopUpdate");

	for(auto machine : machines_)
	{
		machine->loopUpdate();
	}

	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		// Lets them do things in background
		subModeMidiFx[i].loopUpdate();
	}

	// Can be modified by scale MidiFX
	omxFormGlobal.musicScale->calculateScaleIfModified(scaleConfig.scaleRoot, scaleConfig.scalePattern);

	if (omxFormGlobal.isPlaying)
	{
		uint32_t stepmicros = seqConfig.currentFrameMicros;

		if (stepmicros >= ledUpdateTime_)
		{
			ledUpdateTime_ = stepmicros + clockConfig.ppqInterval * 12;
			omxLeds.setDirty();
		}
	}

	// Serial.println("LoopUpdate complete");
}

bool OmxModeForm::getEncoderSelect()
{
	// return encoderSelect && !midiSettings.midiAUX && !isDrumKeyHeld();

	return omxFormGlobal.encoderSelect && !midiSettings.midiAUX;
}

void OmxModeForm::onEncoderChanged(Encoder::Update enc)
{
	if (auxMacroManager_.onEncoderChanged(enc))
		return;

	auto selMachine = getSelectedMachine();
	selMachine->onEncoderChanged(enc);

	// if (getEncoderSelect())
	// {
	// 	// onEncoderChangedSelectParam(enc);
	// 	params.changeParam(enc.dir());
	// 	omxDisp.setDirty();
	// 	return;
	// }

	// auto amt = enc.accel(5); // where 5 is the acceleration factor if you want it, 0 if you don't)

	// int8_t selPage = params.getSelPage();
	// int8_t selParam = params.getSelParam() + 1; // Add one for readability

	// if (selPage == FORMPAGE_POTSANDMACROS)
	// {
	// 	if (selParam == 1)
	// 	{
	// 		potSettings.potbank = constrain(potSettings.potbank + amt, 0, NUM_CC_BANKS - 1);
	// 	}
	// 	if (selParam == 2)
	// 	{
	// 		midiSettings.midiSoftThru = constrain(midiSettings.midiSoftThru + amt, 0, 1);
	// 	}
	// 	if (selParam == 3)
	// 	{
	// 		midiMacroConfig.midiMacro = constrain(midiMacroConfig.midiMacro + amt, 0, nummacromodes);
	// 	}
	// 	if (selParam == 4)
	// 	{
	// 		midiMacroConfig.midiMacroChan = constrain(midiMacroConfig.midiMacroChan + amt, 1, 16);
	// 	}
	// }
	// else if (selPage == FORMPAGE_SCALES)
	// {
	// 	if (selParam == 1)
	// 	{
	// 		int prevRoot = scaleConfig.scaleRoot;
	// 		scaleConfig.scaleRoot = constrain(scaleConfig.scaleRoot + amt, 0, 12 - 1);
	// 		if (prevRoot != scaleConfig.scaleRoot)
	// 		{
	// 			omxFormGlobal.musicScale->calculateScale(scaleConfig.scaleRoot, scaleConfig.scalePattern);
	// 		}
	// 	}
	// 	if (selParam == 2)
	// 	{
	// 		int prevPat = scaleConfig.scalePattern;
	// 		scaleConfig.scalePattern = constrain(scaleConfig.scalePattern + amt, -1, omxFormGlobal.musicScale->getNumScales() - 1);
	// 		if (prevPat != scaleConfig.scalePattern)
	// 		{
	// 			omxDisp.displayMessage(omxFormGlobal.musicScale->getScaleName(scaleConfig.scalePattern));
	// 			omxFormGlobal.musicScale->calculateScale(scaleConfig.scaleRoot, scaleConfig.scalePattern);
	// 		}
	// 	}
	// 	if (selParam == 3)
	// 	{
	// 		scaleConfig.lockScale = constrain(scaleConfig.lockScale + amt, 0, 1);
	// 	}
	// 	if (selParam == 4)
	// 	{
	// 		scaleConfig.group16 = constrain(scaleConfig.group16 + amt, 0, 1);
	// 	}
	// }
	// else if (selPage == FORMPAGE_CFG)
	// {
	// 	if (selParam == 3)
	// 	{
	// 		clockConfig.globalQuantizeStepIndex = constrain(clockConfig.globalQuantizeStepIndex + amt, 0, kNumArpRates - 1);
	// 	}
	// 	else if (selParam == 4)
	// 	{
	// 		cvNoteUtil.triggerMode = constrain(cvNoteUtil.triggerMode + amt, 0, 1);
	// 	}
	// }

	// omxDisp.setDirty();
}

void OmxModeForm::onEncoderButtonDown()
{
	if (auxMacroManager_.onEncoderButtonDown())
		return;

	auto selMachine = getSelectedMachine();
	selMachine->onEncoderButtonDown();

	// if (params.isPageAndParam(FORMPAGE_CFG, 0))
	// {
	// 	auxMacroManager_.enableSubmode(&omxUtil.subModePotConfig);
	// 	omxDisp.isDirty();
	// 	return;
	// }

	omxFormGlobal.encoderSelect = !omxFormGlobal.encoderSelect;
	omxDisp.isDirty();
}

void OmxModeForm::onEncoderButtonUp()
{
}

void OmxModeForm::onEncoderButtonDownLong()
{
}

bool OmxModeForm::shouldBlockEncEdit()
{
	if (auxMacroManager_.shouldBlockEncEdit())
		return true;

	return false;
}

void OmxModeForm::saveKit(uint8_t saveIndex)
{
	// drumKits[saveIndex].CopyFrom(activeDrumKit);
	// selDrumKit = saveIndex;
}

void OmxModeForm::loadKit(uint8_t loadIndex)
{
	// activeDrumKit.CopyFrom(drumKits[loadIndex]);
	// selDrumKit = loadIndex;
}

void OmxModeForm::onKeyUpdate(OMXKeypadEvent e)
{
	omxDisp.setDirty();
	omxLeds.setDirty();

	auto selMachine = machines_[selectedMachine_];

	updateShortcutMode();

	if (auxMacroManager_.onKeyUpdate(e))
		return; // Key consumed by macro

	// Enables quick key aux exit
	if(e.quickClicked() && selMachine->onKeyQuickClicked(e))
		return;

	if (omxFormGlobal.auxBlock)
		return;

	if (onKeyUpdateSelMidiFX(e))
		return;

	// if (omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE || omxFormGlobal.shortcutMode == FORMSHORTCUT_AUX)
	// {
	// 	if (auxMacroManager_.onKeyUpdate(e))
	// 		return; // Key consumed by macro

	// 	if (onKeyUpdateSelMidiFX(e))
	// 		return;
	// }


	int thisKey = e.key();
	// AUX KEY

	// Don't go into aux mode if shortcuts F1 or F2 are being used
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE || omxFormGlobal.shortcutMode == FORMSHORTCUT_AUX)
	{
		if (thisKey == 0)
		{
			midiSettings.midiAUX = e.down();
			return;
		}
	}

	bool keyConsumed = false;

	// Aux shortcuts for all modes
	if(omxFormGlobal.shortcutMode == FORMSHORTCUT_AUX)
	{
		if (!e.held() && e.down())
		{
			if (thisKey == 11 || thisKey == 12) // Change Octave
			{
				int amt = thisKey == 11 ? -1 : 1;
				midiSettings.octave = constrain(midiSettings.octave + amt, -5, 4);
				omxDisp.displayMessage("Octave: " + String(midiSettings.octave));
				keyConsumed = true;
			}
			else if (auxMacroManager_.isMFXQuickEditEnabled() == false && (thisKey == 1 || thisKey == 2)) // Change Param selection
			{
				if (thisKey == 1)
				{
					togglePlayback();
					// params.decrementParam();
					omxDisp.displayMessage(omxFormGlobal.isPlaying ? "PLAY" : "PAUSE");
				}
				else if (thisKey == 2)
				{
					resetPlayback();
					omxDisp.displayMessage("RESET");
				}
				keyConsumed = true;
			}
		}
		else if(e.held())
		{
			if (auxMacroManager_.isMFXQuickEditEnabled() == false && thisKey == 1 ) // Change Param selection
			{
				// Shortcut to do a reset after pausing
				if(omxFormGlobal.isPlaying == false)
				{
					resetPlayback();
					omxDisp.displayMessage("STOP");
					keyConsumed = true;
				}
			}
		}
	}


	if(selMachine->doesConsumeKeys())
	{
		// if(omxFormGlobal.shortcutMode != FORMSHORTCUT_AUX)
		// {
		// 	selMachine->onKeyUpdate(e);
		// }

		if(!keyConsumed)
		{
			keyConsumed = selMachine->onKeyUpdate(e);
		}

		return;
	}

	if(keyConsumed)
	return;

	switch (omxFormGlobal.formMode)
	{
	case FORMMODE_BASE:
	{
		switch (omxFormGlobal.shortcutMode)
		{
		case FORMSHORTCUT_AUX:
		break;
		case FORMSHORTCUT_F1: // Copy
		{
			if (!e.held() && e.down())
			{
				if(thisKey == 0)
				{
					omxDisp.displayMessage("AUX F1");
				}
				else if (thisKey >= 3 && thisKey < 11)
				{
					if (omxFormGlobal.shortcutPaste == false)
					{
						copyMachineAt(thisKey - 3);
						omxFormGlobal.shortcutPaste = true;
						keyConsumed = true;
					}
					else
					{
						pasteMachineTo(thisKey - 3);
						keyConsumed = true;
					}
				}
			}
		}
		break;
		case FORMSHORTCUT_F2: // Cut
		{
			if (!e.held() && e.down())
			{
				if (thisKey == 0)
				{
					omxDisp.displayMessage("AUX F2");
				}
				else if (thisKey >= 3 && thisKey < 11)
				{
					if (omxFormGlobal.shortcutPaste == false)
					{
						cutMachineAt(thisKey - 3);
						omxFormGlobal.shortcutPaste = true;
						keyConsumed = true;
					}
					else
					{
						pasteMachineTo(thisKey - 3);
						keyConsumed = true;
					}
				}
			}
		}
		break;
		case FORMSHORTCUT_F3: // Sequencer shortcut
		{
			if (thisKey == 0)
			{
				omxDisp.displayMessage("AUX F3");
			}
			// if (!e.held() && e.down())
			// {
			// 	if (thisKey >= 3 && thisKey < 11)
			// 	{
			// 		cutMachineAt(thisKey - 3);
			// 		keyConsumed = true;
			// 	}
			// }
		}
		break;
		default:
		{
			// Toggle Mute
			if(!e.down() && e.clicks() == 2)
			{
				if (thisKey >= 3 && thisKey < 11)
				{
					selectMachine(thisKey - 3);
					keyConsumed = true;
					auto m = getSelectedMachine();
					m->setMute(!m->getMute());

					omxDisp.displayMessage(m->getMute() ? "MUTE" : "UNMUTE");
					// selectMachineMode_ = true;
				}
			}
			else if (!e.held() && e.down())
			{
				if (thisKey >= 3 && thisKey < 11)
				{
					selectMachine(thisKey - 3);
					keyConsumed = true;
					// selectMachineMode_ = true;
				}
			}
			// Key released
			else if (!e.held() && !e.down())
			{
			}

		}
		break;
		}

		if(!keyConsumed)
		{
			selMachine->onKeyUpdate(e);
		}
	}
	break;
	case FORMMODE_SELECTMACHINE:
	{
		if (!e.held() && e.down())
		{
			if (thisKey >= 11 && thisKey < 27)
			{
				changeMachineAtIndex(selectedMachine_, thisKey - 11);
			}
		}
		else if (!e.held() && !e.down())
		{
			// Exit machine select
			if (thisKey >= 3 && thisKey - 3 == selectedMachine_)
			{
				changeFormMode(FORMMODE_BASE);
			}
		}
	}
	break;
	}
}

bool OmxModeForm::onKeyUpdateSelMidiFX(OMXKeypadEvent e)
{
	if (auxMacroManager_.onKeyUpdateAuxMFXShortcuts(e, omxFormGlobal.selMidiFX))
		return true;

	return false;
}

bool OmxModeForm::onKeyHeldSelMidiFX(OMXKeypadEvent e)
{
	if (auxMacroManager_.onKeyHeldAuxMFXShortcuts(e, omxFormGlobal.selMidiFX))
		return true;

	return false;
}

void OmxModeForm::onKeyHeldUpdate(OMXKeypadEvent e)
{
	if (auxMacroManager_.onKeyHeldUpdate(e))
		return;

	if (onKeyHeldSelMidiFX(e))
		return;

	auto selMachine = getSelectedMachine();

	if(selMachine->doesConsumeKeys())
	{
		selMachine->onKeyHeldUpdate(e);
		return;
	}

	int thisKey = e.key();

	switch (omxFormGlobal.formMode)
	{
	case FORMMODE_BASE:
	{
		if (thisKey >= 3 && thisKey < 11)
		{
			selectMachine(thisKey - 3);
			changeFormMode(FORMMODE_SELECTMACHINE);
			return;
		}
		selMachine->onKeyHeldUpdate(e);
	}
	break;
	case FORMMODE_SELECTMACHINE:
	{

	}
		break;
	}
}

void OmxModeForm::updateLEDs()
{
	omxLeds.setAllLEDS(0, 0, 0);

	if (midiSettings.midiAUX)
	{
		uint8_t selMFXIndex = getSelectedMachine()->getSelectedMidiFX();
		auxMacroManager_.UpdateAUXLEDS(selMFXIndex);
		return;
	}

	auto selMachine = getSelectedMachine();

	if(selMachine->doesConsumeLEDs())
	{
		selMachine->updateLEDs();
		return;
	}

	bool blinkState = omxLeds.getBlinkState();
	// bool slowBlink = omxLeds.getSlowBlinkState();

	// F3 machine might use these keys for shortcuts
	if (omxFormGlobal.shortcutMode != FORMSHORTCUT_F3)
	{
		for (uint8_t i = 0; i < kNumMachines; i++)
		{
			bool isMuted = machines_[i]->getMute();
			int color = isMuted ? RED : getMachineColor(i);

			if(i == selectedMachine_)
			{
				color = isMuted ? SALMON : WHITE;
			}

			if(machines_[i]->didTriggerThisStep())
			{
				color = INDIGO;
			}

			strip.setPixelColor(i + 3, color);
		}
	}

	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE && omxFormGlobal.formMode == FORMMODE_SELECTMACHINE)
	{
		for (uint8_t i = 0; i < FORMMACH_COUNT; i++)
		{
			bool typeIsSelected = i == machines_[selectedMachine_]->getType();

			int blinkColor = i == 0 ? DKRED : LEDOFF;

			int color = (typeIsSelected && blinkState) ? blinkColor : kMachineColors[i];

			strip.setPixelColor(i + 11, color);
		}
	}
	else if(omxFormGlobal.formMode == FORMMODE_BASE)
	{
		selMachine->updateLEDs();
	}
}

void OmxModeForm::onDisplayUpdate()
{
	updateShortcutMode();

	if (auxMacroManager_.updateLEDs() == false && omxLeds.isDirty())
	{
		// Macro or submode is off, update our LEDs
		updateLEDs();
	}

	// If true, macro or submode is on and consuming display
	if (auxMacroManager_.onDisplayUpdate())
		return;

	// If this is true we are in mode selection menu
	if (encoderConfig.enc_edit)
		return;

	if (omxDisp.canShowDisplay() == false)
		return;

	auto selMachine = getSelectedMachine();

	if(selMachine->doesConsumeDisplay())
	{
		selMachine->onDisplayUpdate();
		return;
	}

	bool dispLabel = false;
	bool dispParams = true;

	switch (omxFormGlobal.shortcutMode)
	{
	// case FORMSHORTCUT_AUX:
	// 	// tempString = "Aux";
	// 	break;
	case FORMSHORTCUT_F1:
		tempString = omxFormGlobal.shortcutPaste ? "Paste" : "Copy";
		dispLabel = true;
		break;
	case FORMSHORTCUT_F2:
		tempString = omxFormGlobal.shortcutPaste ? "Paste" : "Cut";
		dispLabel = true;
		break;
	case FORMSHORTCUT_F3:
		tempString = selMachine->getF3shortcutName();
		dispLabel = true;
		break;
	default:
	{
		if(omxFormGlobal.formMode == FORMMODE_SELECTMACHINE)
		{
			// tempString = String(selectedMachine_ + 1) + " TYPE";
			omxDisp.dispGenericModeLabelDoubleLine("Machine Type", getMachineName(selectedMachine_), 0, 0);
			return;
			// dispParams = false;
		}
		else if(omxFormGlobal.formMode == FORMMODE_BASE)
		{
			selMachine->onDisplayUpdate();
			return;
		}
	}
		break;
	}

	if(dispLabel){
		omxDisp.dispGenericModeLabel(tempString.c_str(), 0, 0);
	}
	else if(dispParams)
	{
		// omxDisp.clearLegends();

		// switch (params.getSelPage())
		// {
		// case FORMPAGE_INSPECT:
		// {
		// 	omxDisp.setLegend(0, "P CC", potSettings.potCC);
		// 	omxDisp.setLegend(1, "P VAL", potSettings.potCC);
		// 	omxDisp.setLegend(2, "NOTE", potSettings.potCC);
		// 	omxDisp.setLegend(3, "VEL", potSettings.potCC);
		// }
		// break;

		// default:
		// 	break;
		// }

		// omxDisp.dispGenericMode2(params.getNumPages(), params.getSelPage(), params.getSelParam(), getEncoderSelect());
		// // omxDisp.dispGenericModeLabelDoubleLine

	}
	// if (params.getSelPage() == FORMPAGE_INSPECT)
	// {
	// 	omxDisp.clearLegends();

	// 	omxDisp.legends[0] = "P CC";
	// 	omxDisp.legends[1] = "P VAL";
	// 	omxDisp.legends[2] = "NOTE";
	// 	omxDisp.legends[3] = "VEL";
	// 	omxDisp.legendVals[0] = potSettings.potCC;
	// 	omxDisp.legendVals[1] = potSettings.potVal;
	// 	omxDisp.legendVals[2] = midiSettings.midiLastNote;
	// 	omxDisp.legendVals[3] = midiSettings.midiLastVel;
	// }
	// else if (params.getSelPage() == FORMPAGE_POTSANDMACROS) // SUBMODE_MIDI3
	// {
	// 	omxDisp.clearLegends();

	// 	omxDisp.legends[0] = "PBNK"; // Potentiometer Banks
	// 	omxDisp.legends[1] = "THRU"; // MIDI thru (usb to hardware)
	// 	omxDisp.legends[2] = "MCRO"; // Macro mode
	// 	omxDisp.legends[3] = "M-CH";
	// 	omxDisp.legendVals[0] = potSettings.potbank + 1;
	// 	omxDisp.legendText[1] = midiSettings.midiSoftThru ? "On" : "Off";
	// 	omxDisp.legendText[2] = macromodes[midiMacroConfig.midiMacro];
	// 	omxDisp.legendVals[3] = midiMacroConfig.midiMacroChan;
	// }
	// else if (params.getSelPage() == FORMPAGE_SCALES) // SCALES
	// {
	// 	omxDisp.clearLegends();
	// 	omxDisp.legends[0] = "ROOT";
	// 	omxDisp.legends[1] = "SCALE";
	// 	omxDisp.legends[2] = "LOCK";
	// 	omxDisp.legends[3] = "GROUP";
	// 	omxDisp.legendVals[0] = -127;
	// 	if (scaleConfig.scalePattern < 0)
	// 	{
	// 		omxDisp.legendVals[1] = -127;
	// 		omxDisp.legendText[1] = "Off";
	// 	}
	// 	else
	// 	{
	// 		omxDisp.legendVals[1] = scaleConfig.scalePattern;
	// 	}

	// 	omxDisp.legendVals[2] = -127;
	// 	omxDisp.legendVals[3] = -127;

	// 	omxDisp.legendText[0] = musicScale->getNoteName(scaleConfig.scaleRoot);
	// 	omxDisp.legendText[2] = scaleConfig.lockScale ? "On" : "Off";
	// 	omxDisp.legendText[3] = scaleConfig.group16 ? "On" : "Off";
	// }
	// else if (params.getSelPage() == FORMPAGE_CFG) // CONFIG
	// {
	// 	omxDisp.clearLegends();
	// 	omxDisp.setLegend(0, "P CC", "CFG");
	// 	omxDisp.setLegend(1, "CLR", "STOR");
	// 	omxDisp.setLegend(2, "QUANT", "1/" + String(kArpRates[clockConfig.globalQuantizeStepIndex]));
	// 	omxDisp.setLegend(3, "CV M", cvNoteUtil.getTriggerModeDispName());
	// }

	// omxDisp.dispGenericMode2(params.getNumPages(), params.getSelPage(), params.getSelParam(), getEncoderSelect());
}

// void onDisplayUpdateLoadKit()
// {

// }

// incoming midi note on
void OmxModeForm::inMidiNoteOn(byte channel, byte note, byte velocity)
{
	// midiSettings.midiLastNote = note;
	// midiSettings.midiLastVel = velocity;
	// int whatoct = (note / 12);
	// int thisKey;
	// uint32_t keyColor = MIDINOTEON;

	// if ((whatoct % 2) == 0)
	// {
	// 	thisKey = note - (12 * whatoct);
	// }
	// else
	// {
	// 	thisKey = note - (12 * whatoct) + 12;
	// }
	// if (whatoct == 0)
	// { // ORANGE,YELLOW,GREEN,MAGENTA,CYAN,BLUE,LIME,LTPURPLE
	// }
	// else if (whatoct == 1)
	// {
	// 	keyColor = ORANGE;
	// }
	// else if (whatoct == 2)
	// {
	// 	keyColor = YELLOW;
	// }
	// else if (whatoct == 3)
	// {
	// 	keyColor = GREEN;
	// }
	// else if (whatoct == 4)
	// {
	// 	keyColor = MAGENTA;
	// }
	// else if (whatoct == 5)
	// {
	// 	keyColor = CYAN;
	// }
	// else if (whatoct == 6)
	// {
	// 	keyColor = LIME;
	// }
	// else if (whatoct == 7)
	// {
	// 	keyColor = CYAN;
	// }
	// strip.setPixelColor(midiKeyMap[thisKey], keyColor); //  Set pixel's color (in RAM)
	// 													//	dirtyPixels = true;
	// strip.show();
	// omxDisp.setDirty();
}

void OmxModeForm::inMidiNoteOff(byte channel, byte note, byte velocity)
{
	// int whatoct = (note / 12);
	// int thisKey;
	// if ((whatoct % 2) == 0)
	// {
	// 	thisKey = note - (12 * whatoct);
	// }
	// else
	// {
	// 	thisKey = note - (12 * whatoct) + 12;
	// }
	// strip.setPixelColor(midiKeyMap[thisKey], LEDOFF); //  Set pixel's color (in RAM)
	// 												  //	dirtyPixels = true;
	// strip.show();
	// omxDisp.setDirty();
}

void OmxModeForm::inMidiControlChange(byte channel, byte control, byte value)
{
	if (auxMacroManager_.inMidiControlChange(channel, control, value))
		return;
}

void OmxModeForm::SetScale(MusicScales *scale)
{
	omxFormGlobal.musicScale = scale;
	auxMacroManager_.SetScale(scale);
}

// void OmxModeForm::drumKeyDown(uint8_t keyIndex)
// {
//     auto drumKey = activeDrumKit.drumKeys[keyIndex - 1];

//     MidiNoteGroup noteGroup = omxUtil.midiDrumNoteOn(keyIndex, drumKey.noteNum, drumKey.vel, drumKey.chan);

// 	if (noteGroup.noteNumber == 255)
// 		return;

//     selDrumKey = keyIndex - 1;

// 	noteGroup.unknownLength = true;
// 	noteGroup.prevNoteNumber = noteGroup.noteNumber;

// 	if (drumKey.midifx < NUM_MIDIFX_GROUPS)
// 	{
// 		subModeMidiFx[drumKey.midifx].noteInput(noteGroup);
// 	}
// 	else
// 	{
// 		onNotePostFX(noteGroup);
// 	}
// }

// void OmxModeForm::drumKeyUp(uint8_t keyIndex)
// {
//     MidiNoteGroup noteGroup = omxUtil.midiDrumNoteOff(keyIndex);

// 	if (noteGroup.noteNumber == 255)
// 		return;

//     auto drumKey = activeDrumKit.drumKeys[keyIndex - 1];

// 	noteGroup.unknownLength = true;
// 	noteGroup.prevNoteNumber = noteGroup.noteNumber;

// 	if (drumKey.midifx < NUM_MIDIFX_GROUPS)
// 	{
// 		subModeMidiFx[drumKey.midifx].noteInput(noteGroup);
// 	}
// 	else
// 	{
// 		onNotePostFX(noteGroup);
// 	}
// }

void OmxModeForm::seqNoteOn(MidiNoteGroup noteGroup, uint8_t midifx)
{
	// Serial.println("seqNoteOn: " + String(noteGroup.noteNumber) + " " + String(midifx));
	// onNotePostFX(noteGroup);


	// MidiNoteGroup noteGroup = omxUtil.midiNoteOn2(musicScale, keyIndex, midiSettings.defaultVelocity, sysSettings.midiChannel);

	if (noteGroup.noteNumber == 255)
		return;

	// Serial.println("doNoteOn: " + String(noteGroup.noteNumber));

	// noteGroup.unknownLength = true;
	noteGroup.prevNoteNumber = noteGroup.noteNumber;

	if (midifx < NUM_MIDIFX_GROUPS)
	{
		// Serial.println("seqNoteOn Send to midifx");
		// noteGroup.Print();
		subModeMidiFx[midifx].noteInput(noteGroup);
		// subModeMidiFx.noteInput(noteGroup);
	}
	else
	{
		// Serial.println("Send to post");
		onNotePostFX(noteGroup);
	}
}

// Called via doNoteOnForwarder
void OmxModeForm::seqNoteOff(MidiNoteGroup noteGroup, uint8_t midifx)
{
	// Serial.println("seqNoteOff: " + String(noteGroup.noteNumber) + " " + String(midifx));

	// Serial.println("seqNoteOff: " + String(noteGroup.noteNumber));

	// onNotePostFX(noteGroup);

	// MidiNoteGroup noteGroup = omxUtil.midiNoteOff2(keyIndex, sysSettings.midiChannel);

	if (noteGroup.noteNumber == 255)
		return;

	// Serial.println("doNoteOff: " + String(noteGroup.noteNumber));

	// noteGroup.unknownLength = true;
	noteGroup.prevNoteNumber = noteGroup.noteNumber;

	if (midifx < NUM_MIDIFX_GROUPS)
	{
		// Serial.println("seqNoteOff Send to midifx");
		// noteGroup.Print();
		subModeMidiFx[midifx].noteInput(noteGroup);
		// subModeMidiFx.noteInput(noteGroup);
	}
	else
	{
		// Serial.println("Send to post");
		onNotePostFX(noteGroup);
	}
}

// Called via doNoteOnForwarder
void OmxModeForm::doNoteOn(uint8_t keyIndex)
{
	MidiNoteGroup noteGroup = omxUtil.midiNoteOn2(omxFormGlobal.musicScale, keyIndex, midiSettings.defaultVelocity, sysSettings.midiChannel);

	if (noteGroup.noteNumber == 255)
		return;

	// Serial.println("doNoteOn: " + String(noteGroup.noteNumber));

	noteGroup.unknownLength = true;
	noteGroup.prevNoteNumber = noteGroup.noteNumber;

	onNotePostFX(noteGroup);

	// if (mfxIndex_ < NUM_MIDIFX_GROUPS)
	// {
	// 	subModeMidiFx[mfxIndex_].noteInput(noteGroup);
	// 	// subModeMidiFx.noteInput(noteGroup);
	// }
	// else
	// {
	// 	onNotePostFX(noteGroup);
	// }
}

// Called via doNoteOnForwarder
void OmxModeForm::doNoteOff(uint8_t keyIndex)
{
	MidiNoteGroup noteGroup = omxUtil.midiNoteOff2(keyIndex, sysSettings.midiChannel);

	if (noteGroup.noteNumber == 255)
		return;

	// Serial.println("doNoteOff: " + String(noteGroup.noteNumber));

	noteGroup.unknownLength = true;
	noteGroup.prevNoteNumber = noteGroup.noteNumber;

	onNotePostFX(noteGroup);

	// if (mfxIndex_ < NUM_MIDIFX_GROUPS)
	// {
	// 	subModeMidiFx[mfxIndex_].noteInput(noteGroup);
	// 	// subModeMidiFx.noteInput(noteGroup);
	// }
	// else
	// {
	// 	onNotePostFX(noteGroup);
	// }
}

// Called by the midiFX group when a note exits it's FX Pedalboard
void OmxModeForm::onNotePostFX(MidiNoteGroup note)
{
	if (note.noteOff)
	{
		// Serial.println("OmxModeForm::onNotePostFX noteOff: " + String(note.noteNumber));

		if (note.sendMidi)
		{
			MM::sendNoteOff(note.noteNumber, note.velocity, note.channel);
		}
		if (note.sendCV)
		{
			cvNoteUtil.cvNoteOff(note.noteNumber);
		}
	}
	else
	{
		if (note.unknownLength == false)
		{
			uint32_t noteOnMicros = note.noteonMicros; // TODO Might need to be set to current micros
			pendingNoteOns.insert(note.noteNumber, note.velocity, note.channel, noteOnMicros, note.sendCV);

			// Serial.println("StepLength: " + String(note.stepLength));

			uint32_t noteOffMicros = noteOnMicros + (note.stepLength * clockConfig.step_micros);
			pendingNoteOffs.insert(note.noteNumber, note.channel, noteOffMicros, note.sendCV);

			// Serial.println("noteOnMicros: " + String(noteOnMicros));
			// Serial.println("noteOffMicros: " + String(noteOffMicros));
		}
		else
		{
			// Serial.println("OmxModeForm::onNotePostFX noteOn: " + String(note.noteNumber));

			if (note.sendMidi)
			{
				midiSettings.midiLastNote = note.noteNumber;
				midiSettings.midiLastVel = note.velocity;
				MM::sendNoteOn(note.noteNumber, note.velocity, note.channel);
			}
			if (note.sendCV)
			{
				cvNoteUtil.cvNoteOn(note.noteNumber);
			}
		}
	}

	// uint32_t noteOnMicros = note.noteonMicros; // TODO Might need to be set to current micros
	// pendingNoteOns.insert(note.noteNumber, note.velocity, note.channel, noteOnMicros, note.sendCV);

	// uint32_t noteOffMicros = noteOnMicros + (note.stepLength * clockConfig.step_micros);
	// pendingNoteOffs.insert(note.noteNumber, note.channel, noteOffMicros, note.sendCV);
}

void OmxModeForm::onPendingNoteOff(int note, int channel)
{
	// Serial.println("OmxModeEuclidean::onPendingNoteOff " + String(note) + " " + String(channel));
	// subModeMidiFx.onPendingNoteOff(note, channel);

	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		subModeMidiFx[i].onPendingNoteOff(note, channel);
	}
}

void OmxModeForm::togglePlayback()
{
	omxFormGlobal.isPlaying = !omxFormGlobal.isPlaying;

	if(omxFormGlobal.isPlaying)
	{
		if(stopped_)
		{
			seqConfig.currentClockTick = 0;
			omxUtil.startClocks();
			stopped_ = false;
		}
		else
		{
			omxUtil.resumeClocks();
		}

		omxLeds.setBlinkAutoRefresh(false);
	}
	else
	{
		// Tell all the MidiFX to stop
		for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
		{
			subModeMidiFx[i].resync();
		}

		omxUtil.stopClocks();
		omxLeds.setBlinkAutoRefresh(true);
	}

	for(auto m : machines_)
	{
		m->playBackStateChanged(omxFormGlobal.isPlaying);
	}
}

void OmxModeForm::resetPlayback()
{
	if(omxFormGlobal.isPlaying)
	{
		// Send midi start so external sequencers reset to start
		omxUtil.startClocks();
	}
	else
	{
		// Means start will send a start instead of resume
		stopped_ = true;
	}

	for(auto m : machines_)
	{
		m->resetPlayback();
	}
}

int OmxModeForm::saveToDisk(int startingAddress, Storage *storage)
{
	int initStart = startingAddress;

	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		// Serial.println((String)"startingAddress: " + startingAddress);

		auto machine = machines_[i];

		if(machine == nullptr || machine->getType() == FORMMACH_NULL)
		{
			Serial.println("machine is null");
			storage->write(startingAddress, FORMMACH_NULL);
			startingAddress++;
		}
		else
		{
			uint8_t machineType = machine->getType();

			// Serial.println("machine type is: " + String(machineType));

			storage->write(startingAddress, machineType);
			startingAddress++;

			startingAddress = machine->saveToDisk(startingAddress, storage);
		}
	}

	int totalSize = startingAddress - initStart;

	Serial.println("FORM Size = " + String(totalSize));

	return startingAddress;
}

int OmxModeForm::loadFromDisk(int startingAddress, Storage *storage)
{
	// Serial.println((String)"startingAddress: " + startingAddress);

	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		uint8_t machineType = storage->read(startingAddress);

		// Serial.println("machine type is: " + String(machineType));
		startingAddress++;

		changeMachineAtIndex(i, machineType);

		auto machine = machines_[i];

		if (machine != nullptr)
		{
			// uint8_t newMachType = machine->getType();

			// Serial.println("Machine at " + String(i) + " is " + String(newMachType));
			// Serial.println("Machine.getMachineIndex() = " + String(machine->getMachineIndex()));

			startingAddress = machine->loadFromDisk(startingAddress, storage);
		}
		else
		{
			Serial.println("machine is null");
		}

		// Serial.println((String)"startingAddress: " + startingAddress);
	}

	return startingAddress;
}