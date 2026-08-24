#include "form_machine_interface.h"
#include "../../hardware/omx_disp.h"

void FormMachineInterface::setEnabled(bool newEnabled)
{
	enabled_ = newEnabled;
	if (enabled_)
	{
		onEnabled();
	}
	else
	{
		onDisabled();
	}
}

void FormMachineInterface::setContext(void *context)
{
	context_ = context;
}

void FormMachineInterface::setNoteOnFptr(void (*fptr)(void *, MidiNoteGroup, uint8_t))
{
	noteOnFuncPtr = fptr;
}

void FormMachineInterface::setNoteOffFptr(void (*fptr)(void *, MidiNoteGroup, uint8_t))
{
	noteOffFuncPtr = fptr;
}

bool FormMachineInterface::isEnabled()
{
	return enabled_;
}

bool FormMachineInterface::getEncoderSelect()
{
	return omxFormGlobal.encoderSelect && !midiSettings.midiAUX;
}

void FormMachineInterface::onEncoderChanged(Encoder::Update enc)
{
	if (getEncoderSelect())
	{
		onEncoderChangedSelectParam(enc);
	}
	else
	{
		onEncoderChangedEditParam(enc);
	}
}

void FormMachineInterface::seqNoteOn(MidiNoteGroup noteGroup, uint8_t midiFx)
{
	if (context_ == nullptr || noteOnFuncPtr == nullptr)
		return;

	noteOnFuncPtr(context_, noteGroup, midiFx);
}

void FormMachineInterface::seqNoteOff(MidiNoteGroup noteGroup, uint8_t midiFx)
{
	if (context_ == nullptr || noteOffFuncPtr == nullptr)
		return;

	noteGroup.noteOff = true;

	noteOffFuncPtr(context_, noteGroup, midiFx);
}

void FormMachineInterface::onNoteOn(uint8_t channel, uint8_t noteNumber, uint8_t velocity, float stepLength, bool sendMidi, bool sendCV, uint32_t noteOnMicros)
{
	if (context_ == nullptr || noteOnFuncPtr == nullptr)
		return;

	MidiNoteGroup noteGroup;
	noteGroup.channel = channel;
	noteGroup.noteNumber = noteNumber;
	noteGroup.velocity = velocity;
	noteGroup.stepLength = stepLength;
	noteGroup.sendMidi = sendMidi;
	noteGroup.sendCV = sendCV;
	noteGroup.noteonMicros = noteOnMicros;

	// triggered_ = true;
	// triggerOffMicros_ = noteOnMicros + (stepLength * clockConfig.step_micros);

	noteOnFuncPtr(context_, noteGroup, 255);
}

// Handles selecting params using encoder
void FormMachineInterface::onEncoderChangedSelectParam(Encoder::Update enc)
{
	// params_.changeParam(enc.dir());
	omxDisp.setDirty();
}

void FormMachineInterface::onEncoderButtonDown()
{
	encoderSelect_ = !encoderSelect_;
	omxDisp.setDirty();
}
