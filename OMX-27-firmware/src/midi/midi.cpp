#include <MIDI.h>
#include "../globals.h"
#include "./midi.h"
#include "../consts/consts.h"
#include "../config.h"
#include "../utils/omx_util.h"
#include "../hardware/omx_disp.h"
#include "../utils/cvNote_util.h"
#include "../modes/omx_screensaver.h"
#include "../modes/omx_mode_interface.h"
#include "../modes/sequencer.h"
#include "sysex.h"

extern OmxModeInterface *activeOmxMode;
extern OmxScreensaver omxScreensaver;
extern SequencerState sequencer;


namespace
{

#if BOARDTYPE == OMX2040
	Adafruit_USBD_MIDI usb_midi;
	MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, usbMIDI);      // USBMIDI is USB MIDI
	MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, HWMIDI);           // HWMIDI is Hardware MIDI

#else
	using SerialMIDI = midi::SerialMIDI<HardwareSerial>;
	using MidiInterface = midi::MidiInterface<SerialMIDI>;
	SerialMIDI theSerialInstance(Serial1);
	MidiInterface HWMIDI(theSerialInstance);

#endif
}

namespace MM
{
	void begin()
	{
		#if BOARDTYPE == OMX2040
			HWMIDI.begin(MIDI_CHANNEL_OMNI);
			usbMIDI.begin(MIDI_CHANNEL_OMNI);

			HWMIDI.turnThruOff();
			usbMIDI.turnThruOff();

			// handlers / callbacks
			usbMIDI.setHandleNoteOn(handleNoteOn);
			usbMIDI.setHandleNoteOff(handleNoteOff);
			usbMIDI.setHandleClock(handleClock);
			usbMIDI.setHandleStart(handleStart);
			usbMIDI.setHandleStop(handleStop);
			usbMIDI.setHandleContinue(handleContinue);
			usbMIDI.setHandleControlChange(handleControlChange);
			usbMIDI.setHandleSystemExclusive(OnSysEx);

			HWMIDI.setHandleNoteOn(handleNoteOn);
			HWMIDI.setHandleNoteOff(handleNoteOff);
			HWMIDI.setHandleClock(handleClock);
			HWMIDI.setHandleStart(handleStart);
			HWMIDI.setHandleStop(handleStop);
			HWMIDI.setHandleContinue(handleContinue);
			HWMIDI.setHandleControlChange(handleControlChange);
			HWMIDI.setHandleSystemExclusive(OnSysExHW);
		#else
			// Teensy (3.x/4.x): usbMIDI is auto-initialised by the core (no begin()/
			// turnThruOff), but its input handlers still need registering - otherwise
			// USB MIDI input is read but never dispatched, so norns' screen-mirror
			// SysEx (NL_CMD_MIRROR_EN etc.) never reaches OnSysEx and mirroring never
			// turns on. Register the same handlers the RP2040 path uses.
			HWMIDI.begin(MIDI_CHANNEL_OMNI);
			HWMIDI.turnThruOff();

			usbMIDI.setHandleNoteOn(handleNoteOn);
			usbMIDI.setHandleNoteOff(handleNoteOff);
			usbMIDI.setHandleClock(handleClock);
			usbMIDI.setHandleStart(handleStart);
			usbMIDI.setHandleStop(handleStop);
			usbMIDI.setHandleContinue(handleContinue);
			usbMIDI.setHandleControlChange(handleControlChange);
			usbMIDI.setHandleSystemExclusive(OnSysEx);

			HWMIDI.setHandleNoteOn(handleNoteOn);
			HWMIDI.setHandleNoteOff(handleNoteOff);
			HWMIDI.setHandleClock(handleClock);
			HWMIDI.setHandleStart(handleStart);
			HWMIDI.setHandleStop(handleStop);
			HWMIDI.setHandleContinue(handleContinue);
			HWMIDI.setHandleControlChange(handleControlChange);
			HWMIDI.setHandleSystemExclusive(OnSysExHW);
		#endif
	}
	// #### Inbound MIDI callbacks

	// void onControlChange(byte channel, byte number, byte value){
	// 	// if bank select MSB (0)- set flag
	// 	// if flag, then look for next CC - LSB (32),
	// 	// then do bank change and reset flag
	// 	// or if not 32, reset flag
	// }

	void handleNoteOn(byte channel, byte note, byte velocity)
	{

		#if BOARDTYPE == OMX2040
			digitalWrite(BLUELED, HIGH);
		#endif

		if (midiSettings.midiSoftThru)
		{
			sendNoteOnHW(note, velocity, channel);
		}
		if (midiSettings.midiInToCV)
		{
			cvNoteUtil.cvNoteOn(note);
		}
		omxScreensaver.userActivity();
		activeOmxMode->inMidiNoteOn(channel, note, velocity);
	}

	void handleNoteOff(byte channel, byte note, byte velocity)
	{
		#if BOARDTYPE == OMX2040
			digitalWrite(BLUELED, LOW);
		#endif

		if (midiSettings.midiSoftThru)
		{
			sendNoteOffHW(note, velocity, channel);
		}
		if (midiSettings.midiInToCV)
		{
			cvNoteUtil.cvNoteOff(note);
		}
		activeOmxMode->inMidiNoteOff(channel, note, velocity);
	}

	void handleControlChange(byte channel, byte control, byte value)
	{
		// digitalWrite(REDLED, HIGH);
		if (midiSettings.midiSoftThru)
		{
			sendControlChangeHW(control, value, channel);
		}
		// change potbank on bank select
		if (control == 0){
			midiSettings.isBankSelect = true;
			potSettings.potbank = constrain(value, 0, NUM_CC_BANKS - 1);
			omxDisp.setDirty();
		// }else if (midiSettings.isBankSelect && control == 32){
		// 	midiSettings.isBankSelect = true;
		}else{
			midiSettings.isBankSelect = false;
		}

		// sendControlChange(control, value, channel);
	}

	// absolute_time_t last_ext_tick_at_ = 0;
	// void externalMidiClockTick(absolute_time_t timestamp) {
	// 	uint32_t delta = absolute_time_diff_us(last_ext_tick_at_, timestamp);
	// 	if ( delta > 0) {
	// 		clockConfig.ppqInterval = delta / 4 ;
	// 		clockConfig.clockbpm = (60000000 / clockConfig.ppqInterval) / PPQ;

	// 		last_ext_tick_at_ = timestamp;
	// 	}
	// }

	// FIXME: This is debug stuff for incoming midi clock average.
	// unsigned int cnt;
	// int cntmax = 24;
		void handleClock() {
		// start a rolling average clock
		// PPQN for MIDI is 24

		// bool clockSource;	// Internal clock (0), external clock (1)

		if (sequencer.clockSource == 1){ // external clock

		// 	absolute_time_t Now = time_us_32();
		// 	externalMidiClockTick(Now);
		// 	// omxDisp.setDirty();
		// }

/*
		if (cnt == cntmax)
		{
			Serial.print("BPM: ");
			Serial.print(clockstats.getBPM());
			Serial.print(" Last Interval: ");
			Serial.print(clockstats.getLastInterval());
			Serial.print(" Samples: ");
			Serial.println(clockstats.getSampleCount());
			cnt = 0;
		}
		*/
			clockstats.clockPulse(micros());
			clockConfig.clockbpm = clockstats.getBPM();
		}

		if (midiSettings.midiSoftThru){
			// sendClock();
		}
	}

	void handleStart() {

		#if BOARDTYPE == OMX2040
			digitalWrite(REDLED, HIGH);
		#endif
		clockstats.start();
		startTransport();
		if (midiSettings.midiSoftThru){
		}
	}

	void handleStop() {

		#if BOARDTYPE == OMX2040
			digitalWrite(REDLED, LOW);
		#endif
		clockstats.stop();
		stopTransport();
		if (midiSettings.midiSoftThru){
		}
	}

	void handleContinue() {

		#if BOARDTYPE == OMX2040
			digitalWrite(REDLED, HIGH);
		#endif
		continueTransport();
		if (midiSettings.midiSoftThru){
		}
	}

	void OnSysEx(byte *sysexData, unsigned length)
	{
		sysEx->processIncomingSysex(sysexData, length);
	}
	void OnSysExHW(byte* sysexData, unsigned length)
	{
		sendSysEx(length, sysexData, false);
	}

	void sendNoteOn(int note, int velocity, int channel)
	{
		HWMIDI.sendNoteOn(note, velocity, channel);
		usbMIDI.sendNoteOn(note, velocity, channel);
	}

	void sendNoteOnHW(int note, int velocity, int channel)
	{
		HWMIDI.sendNoteOn(note, velocity, channel);
	}

	void sendNoteOff(int note, int velocity, int channel)
	{
		HWMIDI.sendNoteOff(note, velocity, channel);
		usbMIDI.sendNoteOff(note, velocity, channel);
	}

	void sendNoteOffHW(int note, int velocity, int channel)
	{
		HWMIDI.sendNoteOff(note, velocity, channel);
	}

	void sendControlChange(int control, int value, int channel)
	{
		HWMIDI.sendControlChange(control, value, channel);
		usbMIDI.sendControlChange(control, value, channel);
	}

	void sendControlChangeHW(int control, int value, int channel)
	{
		HWMIDI.sendControlChange(control, value, channel);
	}

	void sendProgramChange(byte program, byte channel)
	{
		// Bank switch?
		HWMIDI.sendProgramChange(program, channel);
		usbMIDI.sendProgramChange(program, channel);
	}

	void sendSysEx(uint32_t length, const uint8_t *sysexData, bool hasBeginEnd)
	{
		usbMIDI.sendSysEx(length, sysexData, hasBeginEnd);
		HWMIDI.sendSysEx(length, sysexData, hasBeginEnd);
	}

	// USB-only SysEx. Used for the norns link (screen mirror / takeover) so we
	// don't flood the 31250-baud TRS DIN port with framebuffer data.
	//
	// Written directly to TinyUSB rather than via the MIDI library: the library
	// ignores tud_midi_stream_write's return value, so when the 128-byte TX FIFO
	// is short on room the TAIL of the message (incl. F7) is silently dropped --
	// which is exactly what was corrupting the norns screen stream. Here we keep
	// writing the remainder as the FIFO drains (TinyUSB's task runs from an IRQ
	// on RP2040), bounded by a short timeout so we can never stall the sequencer.
	void sendSysExUSB(uint32_t length, const uint8_t *sysexData, bool hasBeginEnd)
	{
#if BOARDTYPE == OMX2040
		uint8_t msg[160];
		uint32_t n = 0;
		if (!hasBeginEnd)
			msg[n++] = 0xF0;
		for (uint32_t i = 0; i < length && n < sizeof(msg) - 1; i++)
			msg[n++] = sysexData[i];
		if (!hasBeginEnd)
			msg[n++] = 0xF7;

		uint32_t sent = 0;
		uint32_t start = micros();
		while (sent < n)
		{
			sent += tud_midi_stream_write(0, msg + sent, n - sent);
			if (sent < n)
			{
				if ((uint32_t)(micros() - start) > 6000) // give up; norns will REQ a resend
					break;
				tud_task(); // help drain the FIFO while we wait
			}
		}
#else
		usbMIDI.sendSysEx(length, sysexData, hasBeginEnd);
		usbMIDI.send_now(); // flush now so norns receives chunks promptly (the RP2040 path drains its own FIFO)
#endif
	}

	// usbMIDI is a FortySevenEffects MidiInterface on RP2040 (has sendClock/Start/...),
	// but the Teensy core's usb_midi_class only has sendRealTime(type). Guard per platform.
	void sendClock()
	{
		if (sequencer.clockSource == 0){ // internal clock
#if BOARDTYPE == OMX2040
			usbMIDI.sendClock();
#else
			usbMIDI.sendRealTime(midi::Clock);
#endif
			HWMIDI.sendClock();
		}
	}

	void startTransport()
	{
#if BOARDTYPE == OMX2040
		usbMIDI.sendStart();
#else
		usbMIDI.sendRealTime(midi::Start);
#endif
		HWMIDI.sendStart();
	}

	void continueTransport()
	{
#if BOARDTYPE == OMX2040
		usbMIDI.sendContinue();
#else
		usbMIDI.sendRealTime(midi::Continue);
#endif
		HWMIDI.sendContinue();
	}

	void stopTransport()
	{
#if BOARDTYPE == OMX2040
		usbMIDI.sendStop();
#else
		usbMIDI.sendRealTime(midi::Stop);
#endif
		HWMIDI.sendStop();
	}

	// NEED SOMETHING FOR usbMIDI.read() / MIDI.read()

	bool usbMidiRead()
	{
#if BOARDTYPE == OMX2040
		// The Arduino MIDI library parses ONE byte per read(), and returns false
		// for every byte that doesn't complete a message — so a caller doing
		// `while (usbMidiRead())` would drain large SysEx (REMOTE-mode frames)
		// at ~one byte per main-loop pass. Parse everything buffered instead.
		while (usb_midi.available() > 0)
		{
			usbMIDI.read();
		}
		return false; // fully drained
#else
		return usbMIDI.read();
#endif
	}

	bool midiRead()
	{
		return HWMIDI.read();
	}
}
