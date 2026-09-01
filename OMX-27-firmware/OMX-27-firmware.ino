// OMX-27 MIDI KEYBOARD / SEQUENCER

//	v1.15.3 — First feature rev of Form Sequencer complete
//	Last update: April 2026
//
//	Original concept and initial code by Steven Noreyko
//  Additional code contributions:
// 		Matt Boone, Steven Zydek,
// 		Chris Atkins, Will Winder,
// 		Michael P Jones
//
//	Big thanks to:
//	John Park and Gerald Stevens for initial testing and feature ideas
//	mzero for immense amounts of code coaching/assistance
//	drjohn for support
//

#include <functional>
#include "src/consts/consts.h"
#include "src/globals.h"
#include "src/config.h"
#include <ResponsiveAnalogRead.h>
#include "src/midi/midi.h"
#include "src/consts/colors.h"
#include "src/ClearUI/ClearUI.h"
// sequencer.h declares the `sequencer` object (clockSource + timing config) which
// is used globally even when the old S1/S2 sequencer MODE is compiled out.
#include "src/modes/sequencer.h"
#include "src/midi/noteoffs.h"
#include "src/hardware/storage.h"
#include "src/midi/sysex.h"
#include "src/hardware/omx_keypad.h"
#include "src/utils/omx_util.h"
#include "src/utils/cvNote_util.h"
#include "src/hardware/omx_disp.h"
#include "src/modes/omx_mode_midi_keyboard.h"
#include "src/modes/omx_mode_drum.h"
#ifdef OMXMODESEQ
#include "src/modes/omx_mode_sequencer.h"
#endif
#ifdef OMXMODEGRIDS
#include "src/modes/omx_mode_grids.h"
#endif
#include "src/modes/omx_mode_euclidean.h"
#include "src/modes/omx_mode_chords.h"
#include "src/form/omx_mode_form.h"
#include "src/modes/omx_mode_config.h"
#include "src/modes/omx_mode_remote.h"
#include "src/modes/omx_screensaver.h"
#include "src/utils/music_scales.h"
#include "src/hardware/omx_leds.h"
#include "src/midi/MIDIClockStats.h"
#include "src/midi/norns_link.h"

// Allows code to compile with smallest code LTO

#if BOARDTYPE != OMX2040
extern "C"
{
	int _getpid() { return -1; }
	int _kill(int pid, int sig) { return -1; }
	int _write(int file, char *ptr, int len) { return -1; }
}
#endif

// #define RAM_MONITOR
// #ifdef RAM_MONITOR
// #include "src/utils/RamMonitor.h"
// #endif

OmxModeMidiKeyboard omxModeMidi;
OmxModeDrum omxModeDrum;
#ifdef OMXMODESEQ
OmxModeSequencer omxModeSeq;
#endif
#ifdef OMXMODEGRIDS
OmxModeGrids omxModeGrids;
#endif
OmxModeEuclidean omxModeEuclid;
OmxModeChords omxModeChords;
OmxModeForm omxModeForm;
OmxModeConfig omxModeConfig;
OmxModeRemote omxModeRemote;

OmxModeInterface *activeOmxMode;

// SysEx remote-control injection (NL_CMD_INPUT 0x51). Called synchronously from the SysEx
// handler at the end of loop(), after all physical input has been processed this frame — safe,
// no reentrancy. sysexData[4]=0x51, [5]=subcmd, [6..]=args. Mirrors the physical dispatch in
// loop() (incl. midiSettings.keyState[] bookkeeping) so modes behave identically to real input.
//   0x00 KEY:  [6]=key(0-26) [7]=down [8]=held [9]=quickClicked [10]=clicks
//   0x01 ENC:  [6]=dir(0=CCW,1=none,2=CW) [7]=count [8]=speedup
//   0x02 EBTN: [6]=action(0=down,1=up,2=upLong)
//   0x03 POT:  [6]=pot(0-4) [7]=value(0-127)
extern OmxScreensaver omxScreensaver; // defined below
void saveToStorage(void);   // defined below

// Host->OMX REMOTE-mode data (LEDs / screen). Ignored unless REMOTE is active.
void omxRemoteSysex(const uint8_t *d, unsigned n)
{
	if (sysSettings.omxMode == MODE_REMOTE)
		omxModeRemote.onSysex(d, n);
}

void omxInjectInput(const uint8_t *d, unsigned n)
{
	if (activeOmxMode == nullptr || n < 6)
		return;
	// Injected input counts as user activity: without this the screensaver blanks the
	// OLED mid-QA while injected events keep silently mutating mode state underneath.
	omxScreensaver.userActivity();
	sysSettings.screenSaverMode = false;
	switch (d[5])
	{
	case 0x00: // KEY
		if (n >= 11 && d[6] <= 26)
		{
			uint8_t key = d[6];
			bool down = d[7] != 0, held = d[8] != 0, quick = d[9] != 0;
			if (down)
				midiSettings.keyState[key] = true;
			OMXKeypadEvent e(key, d[10], held, down, quick);
			activeOmxMode->onKeyUpdate(e);
			if (!down)
				midiSettings.keyState[key] = false;
			if (held)
				activeOmxMode->onKeyHeldUpdate(e);
		}
		break;
	case 0x01: // ENCODER turn
		if (n >= 8)
		{
			int16_t dir = (d[6] == 0) ? -1 : (d[6] == 2 ? 1 : 0);
			int16_t speedup = (n >= 9) ? (int16_t)d[8] : 0;
			for (uint8_t i = 0; i < d[7]; i++)
				activeOmxMode->onEncoderChanged(Encoder::makeUpdate(dir, speedup));
		}
		break;
	case 0x02: // ENCODER button
		if (n >= 7)
		{
			if (d[6] == 0)
				activeOmxMode->onEncoderButtonDown();
			else if (d[6] == 1)
				activeOmxMode->onEncoderButtonUp();
			else if (d[6] == 2)
				activeOmxMode->onEncoderButtonUpLong();
		}
		break;
	case 0x05: // MODE — switch the active OMX mode (QA: injection can't reach the
	           // hardware-only enc-DownLong mode-select path)
		if (n >= 7 && d[6] < NUM_OMX_MODES)
		{
			changeOmxMode((OMXMode)d[6]);
			omxDisp.setDirty();
			omxLeds.setDirty();
		}
		break;
	case 0x04: // SAVE — persist state exactly like the enc-edit + AUX gesture
	{
		omxDisp.displayMessage("Saving...");
		saveToStorage();
		omxDisp.displayMessage("Saved State");
		break;
	}
	case 0x03: // POT
		if (n >= 8 && d[6] < 5)
		{
			uint8_t k = d[6], val = d[7] & 0x7F;
			int prev = potSettings.analogValues[k];
			potSettings.analogValues[k] = val;
			potSettings.hiResPotVal[k] = (uint16_t)val << 7;
			activeOmxMode->onPotChanged(k, prev, val, abs((int)val - prev));
		}
		break;
	}
}

OmxScreensaver omxScreensaver;

MusicScales globalScale;

MIDIClockStats clockstats;

// storage of pot values; current is in the main loop; last value is for midi output
int volatile currentValue[NUM_CC_POTS];
int lastMidiValue[NUM_CC_POTS];

int temp;

Micros lastProcessTime;

uint8_t RES;
uint16_t AMAX;
int V_scale;

// ENCODER
#if BOARDTYPE == OMX2040
	Encoder myEncoder(25, 26); // encoder pins on hardware
	const int buttonPin = 20;
#else
	Encoder myEncoder(12, 11); // encoder pins on hardware
	const int buttonPin = 0;
#endif
int buttonState = 1;
Button encButton(buttonPin);

// long newPosition = 0;
// long oldPosition = -999;

#if BOARDTYPE == OMX2040
	char mfgstr[32] = "denki-oto";
	char prodstr[32] = "omx-27-v3";
	// MUX config
	const int muxMapping[5] = {2,3,0,1,4}; //{A2, A3, A0, A1, A4};
	const int mux_common_pin = 29;
	const int mux1 = 23;
	const int mux2 = 24;
	const int mux3 = 22;
	using namespace admux;
	Mux mux(Pin(mux_common_pin, INPUT, PinType::Analog), Pinset(mux1, mux2, mux3));

	// USB WebUSB object
	// Landing Page: scheme (0: http, 1: https), url
	// Page source can be found at https://github.com/hathach/tinyusb-webusb-page/tree/main/webusb-rgb
	Adafruit_USBD_WebUSB usb_web;
	WEBUSB_URL_DEF(landingPage, 1 /*https*/, "okyeron.github.io/web-editor/index.html");

#endif

// KEYPAD
// initialize an instance of custom Keypad class
unsigned long longPressInterval = 800;
unsigned long clickWindow = 200;
OMXKeypad keypad(longPressInterval, clickWindow, makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// setup EEPROM/FRAM storage
// Storage *storage;
// SysEx *sysEx;

#ifdef RAM_MONITOR
RamMonitor ram;
uint32_t reporttime;

void report_ram_stat(const char *aname, uint32_t avalue)
{
	Serial.print(aname);
	Serial.print(": ");
	Serial.print((avalue + 512) / 1024);
	Serial.print(" Kb (");
	Serial.print((((float)avalue) / ram.total()) * 100, 1);
	Serial.println("%)");
};

void report_profile_time(const char *aname, uint32_t avalue)
{
	Serial.print(aname);
	Serial.print(": ");
	Serial.print(avalue);
	Serial.println("\n");
};

void report_ram()
{
	bool lowmem;
	bool crash;

	Serial.println("==== memory report ====");

	report_ram_stat("free", ram.adj_free());
	report_ram_stat("stack", ram.stack_total());
	report_ram_stat("heap", ram.heap_total());

	lowmem = ram.warning_lowmem();
	crash = ram.warning_crash();
	if (lowmem || crash)
	{
		Serial.println();

		if (crash)
			Serial.println("**warning: stack and heap crash possible");
		else if (lowmem)
			Serial.println("**warning: unallocated memory running low");
	};

	Serial.println();
};
#endif

// ####### SEQUENCER LEDS #######

void changeOmxMode(OMXMode newOmxmode)
{
	//	Serial.println((String)"NewMode: " + newOmxmode);
	sysSettings.omxMode = newOmxmode;
	sysSettings.newmode = newOmxmode;

	if (activeOmxMode != nullptr)
	{
		activeOmxMode->onModeDeactivated();
	}

	switch (newOmxmode)
	{
	case MODE_MIDI:
		omxModeMidi.setMidiMode();
		activeOmxMode = &omxModeMidi;
		break;
	case MODE_DRUM:
		activeOmxMode = &omxModeDrum;
		break;
	case MODE_CHORDS:
		activeOmxMode = &omxModeChords;
		break;
	case MODE_FORM:
		activeOmxMode = &omxModeForm;
		break;
	case MODE_S1:
#ifdef OMXMODESEQ
		omxModeSeq.setSeq1Mode();
		activeOmxMode = &omxModeSeq;
#endif
		break;
	case MODE_S2:
#ifdef OMXMODESEQ
		omxModeSeq.setSeq2Mode();
		activeOmxMode = &omxModeSeq;
#endif
		break;
	case MODE_OM:
		omxModeMidi.setOrganelleMode();
		activeOmxMode = &omxModeMidi;
		break;
	case MODE_GRIDS:
#ifdef OMXMODEGRIDS
		activeOmxMode = &omxModeGrids;
#endif
		break;
	case MODE_EUCLID:
		activeOmxMode = &omxModeEuclid;
		break;
	case MODE_CONFIG:
		activeOmxMode = &omxModeConfig;
		break;
	case MODE_REMOTE:
		activeOmxMode = &omxModeRemote;
		break;
	default:
		omxModeMidi.setMidiMode();
		activeOmxMode = &omxModeMidi;
		break;
	}

	activeOmxMode->onModeActivated();

	omxLeds.setDirty();
	omxDisp.setDirty();
}

// ####### END LEDS

// ####### POTENTIOMETERS #######
void readPotentimeters()
{
	for (int k = 0; k < potCount; k++)
	{
		int prevValue = potSettings.analogValues[k];
		int prevAnalog = potSettings.analog[k]->getValue();
#if BOARDTYPE == OMX2040
		temp = mux.read(muxMapping[k]);
// 		temp = 0;
#else
		temp = analogRead(analogPins[k]);
#endif
		potSettings.analog[k]->update(temp);
		// read from the smoother, constrain (to account for tolerances), and map it
		temp = potSettings.analog[k]->getValue();
		temp = constrain(temp, potMinVal, potMaxVal);
		temp = map(temp, potMinVal, potMaxVal, 0, 16383);
		potSettings.hiResPotVal[k] = temp;

		// map and update the value
		potSettings.analogValues[k] = temp >> 7;

		int newAnalog = potSettings.analog[k]->getValue();

		// delta is way smaller on T4 - what to do??
		int analogDelta = abs(newAnalog - prevAnalog);

		// if (k == 1)
		// {
		// 	Serial.print(analogPins[k]);
		// 	Serial.print(" ");
		// 	Serial.print(temp);
		// 	Serial.print(" ");
		// 	Serial.print(potSettings.analogValues[k]);
		// 	Serial.print("\n");
		// }

		if (potSettings.analog[k]->hasChanged())
		{
			nornsLink.markActivity();
			// do stuff
			if (sysSettings.screenSaverMode)
			{
				omxScreensaver.onPotChanged(k, prevValue, potSettings.analogValues[k], analogDelta);
			}
			// don't send pots in screensaver
			else
			{
				activeOmxMode->onPotChanged(k, prevValue, potSettings.analogValues[k], analogDelta);
			}
		}

	}
}
// ####### END POTENTIOMETERS #######


void saveHeader()
{
	// 1 byte for EEPROM version
	storage->write(EEPROM_HEADER_ADDRESS + 0, EEPROM_VERSION);

	// 1 byte for mode
	storage->write(EEPROM_HEADER_ADDRESS + 1, (uint8_t)sysSettings.omxMode);

	// 1 byte for the active pattern
#ifdef OMXMODESEQ
	storage->write(EEPROM_HEADER_ADDRESS + 2, (uint8_t)sequencer.playingPattern);
	#endif

	// 1 byte for Midi channel
	uint8_t unMidiChannel = (uint8_t)(sysSettings.midiChannel - 1);
	storage->write(EEPROM_HEADER_ADDRESS + 3, unMidiChannel);

	for (int b = 0; b < NUM_CC_BANKS; b++)
	{
		for (int i = 0; i < NUM_CC_POTS; i++)
		{
			storage->write(EEPROM_HEADER_ADDRESS + 4 + i + (5 * b), pots[b][i]);
		}
	}
	// Last is 28

	uint8_t midiMacroChan = (uint8_t)(midiMacroConfig.midiMacroChan - 1);
	storage->write(EEPROM_HEADER_ADDRESS + 29, midiMacroChan);

	uint8_t midiMacroId = (uint8_t)midiMacroConfig.midiMacro;
	storage->write(EEPROM_HEADER_ADDRESS + 30, midiMacroId);

	uint8_t scaleRoot = (uint8_t)scaleConfig.scaleRoot;
	storage->write(EEPROM_HEADER_ADDRESS + 31, scaleRoot);

	uint8_t scalePattern = (uint8_t)scaleConfig.scalePattern;
	storage->write(EEPROM_HEADER_ADDRESS + 32, scalePattern);

	uint8_t lockScale = (uint8_t)scaleConfig.lockScale;
	storage->write(EEPROM_HEADER_ADDRESS + 33, lockScale);

	uint8_t scaleGrp16 = (uint8_t)scaleConfig.group16;
	storage->write(EEPROM_HEADER_ADDRESS + 34, scaleGrp16);

	storage->write(EEPROM_HEADER_ADDRESS + 35, midiSettings.defaultVelocity);

	storage->write(EEPROM_HEADER_ADDRESS + 36, clockConfig.globalQuantizeStepIndex);

	storage->write(EEPROM_HEADER_ADDRESS + 37, cvNoteUtil.triggerMode);

	storage->write(EEPROM_HEADER_ADDRESS + 38, potSettings.potbank);

	// CONFIG-mode global settings (offsets 40-50; the 40-63 range is a free gap
	// between the header and EEPROM_PATTERN_ADDRESS at 64).
	uint16_t bpm = (uint16_t)clockConfig.clockbpm;
	storage->write(EEPROM_HEADER_ADDRESS + 40, (uint8_t)(bpm & 0xFF));
	storage->write(EEPROM_HEADER_ADDRESS + 41, (uint8_t)((bpm >> 8) & 0xFF));
	storage->write(EEPROM_HEADER_ADDRESS + 42, (uint8_t)sequencer.clockSource);
	storage->write(EEPROM_HEADER_ADDRESS + 43, (uint8_t)clockConfig.send_always);
	storage->write(EEPROM_HEADER_ADDRESS + 44, (uint8_t)midiSettings.midiSoftThru);
	storage->write(EEPROM_HEADER_ADDRESS + 45, (uint8_t)midiSettings.midiInToCV);
	storage->write(EEPROM_HEADER_ADDRESS + 46, deviceID);
	storage->write(EEPROM_HEADER_ADDRESS + 47, ledBrightness);
	storage->write(EEPROM_HEADER_ADDRESS + 48, (uint8_t)screensaverEnabled);
	storage->write(EEPROM_HEADER_ADDRESS + 49, (uint8_t)(screensaverTimeoutSec & 0xFF));
	storage->write(EEPROM_HEADER_ADDRESS + 50, (uint8_t)((screensaverTimeoutSec >> 8) & 0xFF));
	storage->write(EEPROM_HEADER_ADDRESS + 51, (uint8_t)(colorConfig.midiBg_Hue & 0xFF));
	storage->write(EEPROM_HEADER_ADDRESS + 52, (uint8_t)((colorConfig.midiBg_Hue >> 8) & 0xFF));
	uint16_t ssHue = (uint16_t)min(colorConfig.screensaverColor, (uint32_t)65024);
	storage->write(EEPROM_HEADER_ADDRESS + 53, (uint8_t)(ssHue & 0xFF));
	storage->write(EEPROM_HEADER_ADDRESS + 54, (uint8_t)((ssHue >> 8) & 0xFF));
}

// returns true if the header contained initialized data
// false means we shouldn't attempt to load any further information
bool loadHeader(void)
{
	uint8_t version = storage->read(EEPROM_HEADER_ADDRESS + 0);
	// A transient read glitch here (e.g. FRAM/I2C not settled right after a reboot)
	// used to look like "uninitialized" and trigger a full reinit + re-save, wiping
	// every saved setting AND the FORM pattern bank. Re-read before believing it.
	if (version == 0xFF || version != EEPROM_VERSION)
	{
		delay(10);
		version = storage->read(EEPROM_HEADER_ADDRESS + 0);
	}

	char buf[64];
	snprintf(buf, sizeof(buf), "EEPROM Header Version is %d\n", version);
	// Serial.print(buf);

	// Uninitalized EEPROM memory is filled with 0xFF
	if (version == 0xFF)
	{
		// EEPROM was uninitialized
		// Serial.println("version was 0xFF");
		return false;
	}

	if (version != EEPROM_VERSION)
	{
		// write an adapter if we ever need to increment the EEPROM version and also save the existing patterns
		// for now, return false will essentially reset the state
		// Serial.println("version not matched");
		return false;
	}

	sysSettings.omxMode = (OMXMode)storage->read(EEPROM_HEADER_ADDRESS + 1);

#ifdef OMXMODESEQ
	sequencer.playingPattern = storage->read(EEPROM_HEADER_ADDRESS + 2);
	sysSettings.playingPattern = sequencer.playingPattern;
#endif

	uint8_t unMidiChannel = storage->read(EEPROM_HEADER_ADDRESS + 3);
	sysSettings.midiChannel = unMidiChannel + 1;

	// Serial.println("Loading banks");
	for (int b = 0; b < NUM_CC_BANKS; b++)
	{
		for (int i = 0; i < NUM_CC_POTS; i++)
		{
			pots[b][i] = storage->read(EEPROM_HEADER_ADDRESS + 4 + i + (5 * b));
		}
	}

	uint8_t midiMacroChannel = storage->read(EEPROM_HEADER_ADDRESS + 29);
	midiMacroConfig.midiMacroChan = midiMacroChannel + 1;

	uint8_t midiMacro = storage->read(EEPROM_HEADER_ADDRESS + 30);
	midiMacroConfig.midiMacro = midiMacro;

	uint8_t scaleRoot = storage->read(EEPROM_HEADER_ADDRESS + 31);
	scaleConfig.scaleRoot = scaleRoot;

	int8_t scalePattern = (int8_t)storage->read(EEPROM_HEADER_ADDRESS + 32);
	scaleConfig.scalePattern = scalePattern;

	bool lockScale = (bool)storage->read(EEPROM_HEADER_ADDRESS + 33);
	scaleConfig.lockScale = lockScale;

	bool scaleGrp16 = (bool)storage->read(EEPROM_HEADER_ADDRESS + 34);
	scaleConfig.group16 = scaleGrp16;

	globalScale.calculateScale(scaleConfig.scaleRoot, scaleConfig.scalePattern);

	midiSettings.defaultVelocity = storage->read(EEPROM_HEADER_ADDRESS + 35);

	clockConfig.globalQuantizeStepIndex = constrain(storage->read(EEPROM_HEADER_ADDRESS + 36), 0, kNumArpRates - 1);

	cvNoteUtil.triggerMode = constrain(storage->read(EEPROM_HEADER_ADDRESS + 37), 0, 1);

	potSettings.potbank = constrain(storage->read(EEPROM_HEADER_ADDRESS + 38), 0, NUM_CC_BANKS-1);

	// CONFIG-mode global settings
	uint16_t bpm = (uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 40) | ((uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 41) << 8);
	clockConfig.clockbpm = constrain((int)bpm, 40, 300);
	omxUtil.resetClocks(); // apply the loaded tempo
	sequencer.clockSource = (bool)storage->read(EEPROM_HEADER_ADDRESS + 42);
	clockConfig.send_always = (bool)storage->read(EEPROM_HEADER_ADDRESS + 43);
	midiSettings.midiSoftThru = (bool)storage->read(EEPROM_HEADER_ADDRESS + 44);
	midiSettings.midiInToCV = (bool)storage->read(EEPROM_HEADER_ADDRESS + 45);
	deviceID = constrain((int)storage->read(EEPROM_HEADER_ADDRESS + 46), 0, 127);
	ledBrightness = constrain((int)storage->read(EEPROM_HEADER_ADDRESS + 47), 5, 255);
	strip.setBrightness(ledBrightness);
	screensaverEnabled = (bool)storage->read(EEPROM_HEADER_ADDRESS + 48);
	uint16_t ssTimeout = (uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 49) | ((uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 50) << 8);
	screensaverTimeoutSec = constrain((int)ssTimeout, 5, 3600);
	// LED hues (0xFFFF = written by an older save that lacked these bytes -> keep defaults)
	uint16_t keyBgHue = (uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 51) | ((uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 52) << 8);
	if (keyBgHue != 0xFFFF)
		colorConfig.midiBg_Hue = keyBgHue;
	uint16_t ssHue = (uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 53) | ((uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 54) << 8);
	if (ssHue != 0xFFFF)
		colorConfig.screensaverColor = ssHue;

	// digitalWrite(BLUELED, HIGH);
	return true;
}

void savePatterns(void)
{
	bool isEeprom = storage->isEeprom();

	int nLocalAddress = EEPROM_PATTERN_ADDRESS;

	int patternSize = 0;

#ifdef OMXMODESEQ
	patternSize = serializedPatternSize(isEeprom);

	// Serial.println((String)"Seq patternSize: " + patternSize);
	int seqPatternNum = isEeprom ? NUM_SEQ_PATTERNS_EEPROM : NUM_SEQ_PATTERNS;

	for (int i = 0; i < seqPatternNum; i++)
	{
		auto pattern = (byte *)sequencer.getPattern(i);
		for (int j = 0; j < patternSize; j++)
		{
			storage->write(nLocalAddress + j, *pattern++);
		}

		nLocalAddress += patternSize;
	}
#endif
	if(isEeprom)
	{
		return;
	}
	// Serial.println((String)"nLocalAddress: " + nLocalAddress); // 5784

#ifndef OMXMODESEQ
	Serial.println("Saving FORM");
	Serial.println((String)"nLocalAddress: " + nLocalAddress); 
	nLocalAddress = omxModeForm.saveToDisk(nLocalAddress, storage);
	Serial.println((String)"nLocalAddress: " + nLocalAddress); 
#endif

#ifdef OMXMODEGRIDS
	// Serial.println("Saving Grids");

	// Grids patterns
	patternSize = OmxModeGrids::serializedPatternSize(isEeprom);
	int numPatterns = OmxModeGrids::getNumPatterns();

	// Serial.println((String)"OmxModeGrids patternSize: " + patternSize);
	// Serial.println((String)"numPatterns: " + numPatterns);

	for (int i = 0; i < numPatterns; i++)
	{
		auto pattern = (byte *)omxModeGrids.getPattern(i);
		for (int j = 0; j < patternSize; j++)
		{
			storage->write(nLocalAddress + j, *pattern++);
		}

		nLocalAddress += patternSize;
	}
	// Serial.println((String)"nLocalAddress: " + nLocalAddress); // 6008
#endif

	// Serial.println("Saving Euclidean");
	nLocalAddress = omxModeEuclid.saveToDisk(nLocalAddress, storage);
	// Serial.println((String)"nLocalAddress: " + nLocalAddress); // 7433

	// Serial.println("Saving Chords");
	nLocalAddress = omxModeChords.saveToDisk(nLocalAddress, storage);
	// Serial.println((String)"nLocalAddress: " + nLocalAddress); // 10505

	// Serial.println("Saving Drums");
	nLocalAddress = omxModeDrum.saveToDisk(nLocalAddress, storage);
	// Serial.println((String)"nLocalAddress: " + nLocalAddress); // 11545

	// Serial.println("Saving MidiFX");
	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		nLocalAddress = subModeMidiFx[i].saveToDisk(nLocalAddress, storage);
		// Serial.println((String)"Saved: " + i);
		// Serial.println((String)"nLocalAddress: " + nLocalAddress);
	}
	// Serial.println((String)"nLocalAddress: " + nLocalAddress); // 11585

	// Starting 11545
	// MidiFX with nothing 11585
	// 1 MidiFX full ARPS 11913
	//
	// OMX Frooze/Ran out of memory after creating 4 x 8 - 3 = 29  ARPs
	// Maybe build in a limit of 2 or one arps per MidiFX, or just recommend users not to
	// create 29 ARPs.

	// Seq patternSize: 715
	// nLocalAddress: 5752
	// size of patterns: 5720
	// OmxModeGrids patternSize: 23
	// numPatterns: 8
	// nLocalAddress: 5936
	// size of grids: 184
}

void loadPatterns(void)
{
	bool isEeprom = storage->isEeprom();

	int patternSize = 0;
	int nLocalAddress = EEPROM_PATTERN_ADDRESS;

#ifdef OMXMODESEQ
	patternSize = serializedPatternSize(isEeprom);

	Serial.print("Seq patterns - nLocalAddress: ");
	Serial.println(nLocalAddress);

	int seqPatternNum = isEeprom ? NUM_SEQ_PATTERNS_EEPROM : NUM_SEQ_PATTERNS;

	for (int i = 0; i < seqPatternNum; i++)
	{
		auto pattern = Pattern{};
		auto current = (byte *)&pattern;
		for (int j = 0; j < patternSize; j++)
		{
			*current = storage->read(nLocalAddress + j);
			current++;
		}
		sequencer.patterns[i] = pattern;

		nLocalAddress += patternSize;
	}
#endif

	if (isEeprom)
	{
		return;
	}

#ifndef OMXMODESEQ
	Serial.print("Loading FORM");
	Serial.println((String) "nLocalAddress: " + nLocalAddress); // 5988
	nLocalAddress = omxModeForm.loadFromDisk(nLocalAddress, storage);
	Serial.println((String) "nLocalAddress: " + nLocalAddress); // 5988
#endif

	Serial.print("Grids patterns - nLocalAddress: ");
	Serial.println(nLocalAddress);
	// 332 - eeprom size
	// 332 * 8 = 2656

	// Grids patterns
#ifdef OMXMODEGRIDS
	patternSize = OmxModeGrids::serializedPatternSize(isEeprom);
	int numPatterns = OmxModeGrids::getNumPatterns();

	for (int i = 0; i < numPatterns; i++)
	{
		auto pattern = grids::SnapShotSettings{};
		auto current = (byte *)&pattern;
		for (int j = 0; j < patternSize; j++)
		{
			*current = storage->read(nLocalAddress + j);
			current++;
		}
		omxModeGrids.setPattern(i, pattern);
		nLocalAddress += patternSize;
	}
#endif

	// Serial.print("Pattern size: ");
	// Serial.print(patternSize);

	// Serial.print(" - nLocalAddress: ");
	// Serial.println(nLocalAddress);

	// Serial.print("Loading Euclidean - ");
	nLocalAddress = omxModeEuclid.loadFromDisk(nLocalAddress, storage);
	// Serial.println((String) "nLocalAddress: " + nLocalAddress); // 5988

	// Serial.print("Loading Chords - ");
	nLocalAddress = omxModeChords.loadFromDisk(nLocalAddress, storage);
	// Serial.println((String)"nLocalAddress: " + nLocalAddress); // 5988

	// Serial.print("Loading Drums - ");
	nLocalAddress = omxModeDrum.loadFromDisk(nLocalAddress, storage);
	// Serial.println((String)"nLocalAddress: " + nLocalAddress); // 5988

	// Serial.println((String)"nLocalAddress: " + nLocalAddress); // 5968

	// Serial.print("Loading MidiFX - ");
	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		nLocalAddress = subModeMidiFx[i].loadFromDisk(nLocalAddress, storage);
		// Serial.println((String)"Loaded: " + i);
		// Serial.println((String)"nLocalAddress: " + nLocalAddress);
	}
	// Serial.println((String) "nLocalAddress: " + nLocalAddress); // 5988

	// with 8 note chords, 10929

	// Pattern size = 715
	// Pattern size eprom = 332
	// Total size of patterns = 5720
	// Total storage size = 5749
	// Fram = 32000 = 26251 available
	// Eeprom = 2048
	// Eeprom rom can save 6 patterns, plus 56 bytes

	// 2832 - size of 16 euclid patterns of 16 euclids

	// no arps = 9905, 5 arps = 10105, 25 arps = 11505

	// no arps = 10929, 5 arps = 11129, 25 arps = 12529
	//
}

// currently saves everything ( mode + patterns )
void saveToStorage(void)
{
	// Serial.println("Saving to Storage...");
	saveHeader();
	savePatterns();
}

// currently loads everything ( mode + patterns )
bool loadFromStorage(void)
{
	// This load can happen soon after Serial.begin
	// - enable this 'wait for Serial' if you need to Serial.print during loading
	// while( !Serial );

	// Serial.println("Read the header");
	bool bContainedData = loadHeader();

	if (bContainedData)
	{
		// Serial.println("Loading patterns");
		loadPatterns();
		changeOmxMode(sysSettings.omxMode);

		omxDisp.isDirty();
		omxLeds.isDirty();
		return true;
	}

	// Serial.println("-- Failed to load --");

	omxDisp.isDirty();
	omxLeds.isDirty();

	return false;
}

// ############## MAIN LOOP ##############

void loop()
{
	//	customKeypad.tick();
	keypad.tick();
	// clksTimer = 0; // TODO - didn't see this used anywhere

	Micros now = micros();
	Micros passed = now - lastProcessTime;
	lastProcessTime = now;

	sysSettings.timeElasped = passed;

	seqConfig.currentFrameMicros = micros();
	// Micros timeStart = micros();
	activeOmxMode->loopUpdate(passed);
	cvNoteUtil.loopUpdate(passed);

	if (passed > 0) // This should always be true
	{
		bool seqPlaying = false;

#ifdef OMXMODESEQ
		seqPlaying = sequencer.playing;
#endif
		if (seqPlaying || omxUtil.areClocksRunning())
		{
			omxScreensaver.resetCounter(); // screenSaverCounter = 0;
		}
		omxUtil.advanceClock(activeOmxMode, passed);
		omxUtil.advanceSteps(passed);
	}

	// DISPLAY SETUP -- why is this display. instead of omxDisp. ??
	display.clearDisplay();

	// ############### SLEEP MODE ###############
	//
	//	Serial.println(screenSaverCounter);
	// Keep the OLED awake (don't blank) while the norns screen mirror is active,
	// and force a periodic repaint so a static screen still streams a frame
	// (initial frame after enable + steady self-heal of any dropped frames).
	if (nornsLink.mirrorEnabled())
	{
		omxScreensaver.resetCounter();
		static uint32_t lastMirrorPush = 0;
		if ((uint32_t)(millis() - lastMirrorPush) > 500)
		{
			lastMirrorPush = millis();
			omxDisp.setDirty();
		}
	}
	omxScreensaver.updateScreenSaverState();
	sysSettings.screenSaverMode = omxScreensaver.shouldShowScreenSaver();

	// ############### POTS ###############
	//
	readPotentimeters();

	bool omxModeChangedThisFrame = false;

	// ############### EXTERNAL MODE CHANGE / SYSEX ###############
	if ((!encoderConfig.enc_edit && (sysSettings.omxMode != sysSettings.newmode)) || sysSettings.refresh)
	{
		sysSettings.newmode = sysSettings.omxMode;
		changeOmxMode(sysSettings.omxMode);
		omxModeChangedThisFrame = true;

#ifdef OMXMODESEQ
		sequencer.playingPattern = sysSettings.playingPattern;
#endif
		omxDisp.setDirty();
		omxLeds.setAllLEDS(0, 0, 0);
		omxLeds.setDirty();
		sysSettings.refresh = false;
	}

	// ############### ENCODER ###############
	//
	auto u = myEncoder.update();
// 	Serial.println("Encoder update");
	if (u.active())
	{
		auto amt = u.accel(1);		   // where 5 is the acceleration factor if you want it, 0 if you don't)
		omxScreensaver.userActivity(); // screenSaverCounter = 0;
		nornsLink.markActivity();
									   //    	Serial.println(u.dir() < 0 ? "ccw " : "cw ");
									   //    	Serial.println(amt);

		// Change Mode
		if (encoderConfig.enc_edit)
		{
			// set mode
			//			int modesize = NUM_OMX_MODES;
			int newMode = constrain((int)sysSettings.newmode + amt, 0, NUM_OMX_MODES - 1);
#ifndef OMXMODESEQ
			// The S1/S2 sequencers are compiled out (kept behind OMXMODESEQ), so skip
			// their slots in the mode rotation instead of landing on a dead no-op.
			int skipDir = (amt < 0) ? -1 : 1;
			while ((newMode == MODE_S1 || newMode == MODE_S2) && newMode > 0 && newMode < (NUM_OMX_MODES - 1))
			{
				newMode += skipDir;
			}
#endif
			sysSettings.newmode = (OMXMode)newMode;
			// omxDisp.dispMode();
			// omxDisp.bumpDisplayTimer();
			omxDisp.setDirty();
			omxLeds.setDirty();
		}
		else
		{
			activeOmxMode->onEncoderChanged(u);
		}
	}
	// END ENCODER

	// ############### ENCODER BUTTON ###############
	//
	auto s = encButton.update();
	switch (s)
	{
	// SHORT PRESS
	case Button::Down:				   // Serial.println("Button down");
		omxScreensaver.userActivity(); // screenSaverCounter = 0;
		nornsLink.markActivity();

		// what page are we on?
		if (sysSettings.newmode != sysSettings.omxMode && encoderConfig.enc_edit)
		{
			changeOmxMode(sysSettings.newmode);
			omxModeChangedThisFrame = true;
#ifdef OMXMODESEQ
			seqStop();
#endif
			omxLeds.setAllLEDS(0, 0, 0);
			encoderConfig.enc_edit = false;
			// omxDisp.dispMode();
			omxDisp.setDirty();
		}
		else if (encoderConfig.enc_edit)
		{
			encoderConfig.enc_edit = false;
		}

		// Prevents toggling encoder select when entering mode
		if (!omxModeChangedThisFrame)
		{
			activeOmxMode->onEncoderButtonDown();
		}

		omxDisp.setDirty();
		break;

	// LONG PRESS
	case Button::DownLong: // Serial.println("Button downlong");
		if (activeOmxMode->shouldBlockEncEdit())
		{
			activeOmxMode->onEncoderButtonDown();
		}
		else
		{
			// Enter mode change
			encoderConfig.enc_edit = true;
			sysSettings.newmode = sysSettings.omxMode;
			omxLeds.setAllLEDS(0, 0, 0);
			omxDisp.setDirty();
			// omxDisp.dispMode();
		}

		omxDisp.setDirty();
		break;
	case Button::Up: // Serial.println("Button up");
		activeOmxMode->onEncoderButtonUp();
		break;
	case Button::UpLong: // Serial.println("Button uplong");
		activeOmxMode->onEncoderButtonUpLong();
		break;
	default:
		break;
	}
	// END ENCODER BUTTON

	// ############### KEY HANDLING ###############
	//
	while (keypad.available())
	{
// 		Serial.println("keypad");
		auto e = keypad.next();
		int thisKey = e.key();
		bool keyConsumed = false;
		// int keyPos = thisKey - 11;
		// int seqKey = keyPos + (sequencer.patternPage[sequencer.playingPattern] * NUM_STEPKEYS);

		if (e.down())
		{
			omxScreensaver.userActivity(); // screenSaverCounter = 0;
			nornsLink.markActivity();
			midiSettings.keyState[thisKey] = true;
		}

		// !e.held(): only a fresh AUX press saves — an AUX that was already held
		// when enc_edit opened (e.g. the REMOTE-mode AUX+enc exit chord) gets
		// re-delivered as a held event and must not trigger the blocking save.
		if (e.down() && !e.held() && thisKey == 0 && encoderConfig.enc_edit)
		{
			// temp - save whenever the 0 key is pressed in encoder edit mode
			omxDisp.displayMessage("Saving...");
			omxDisp.isDirty();
			omxDisp.showDisplay();
			saveToStorage();
			//	Serial.println("EEPROM saved");
			omxDisp.displayMessage("Saved State");
			encoderConfig.enc_edit = false;
			omxLeds.setAllLEDS(0, 0, 0);
			activeOmxMode->onModeActivated();
			omxDisp.isDirty();
			omxLeds.isDirty();
			keyConsumed = true;
		}

		if (!keyConsumed)
		{
			activeOmxMode->onKeyUpdate(e);
		}

		// END MODE SWITCH

		if (!e.down())
		{
			midiSettings.keyState[thisKey] = false;
		}

		// ### LONG KEY SWITCH PRESS
		if (e.held() && !keyConsumed)
		{
			// DO LONG PRESS THINGS
			activeOmxMode->onKeyHeldUpdate(e); // Only the sequencer uses this, could probably be handled in onKeyUpdate() but keyStates are modified before this stuff happens.
		}									   // END IF HELD

	} // END KEYS WHILE

	// Drain USB MIDI before the display/LED push: display.display() stalls the
	// loop for several ms and the TinyUSB RX FIFO is only 128 bytes — going into
	// the stall full makes the host back up (REMOTE mode is the heavy case).
	while (MM::usbMidiRead())
	{
	}

	if (!sysSettings.screenSaverMode)
	{
		omxLeds.updateBlinkStates();
		omxDisp.UpdateMessageTextTimer();

		if (encoderConfig.enc_edit)
		{
			omxDisp.dispMode();
		}
		else
		{
			activeOmxMode->onDisplayUpdate();
		}
	}
	else
	{ // if screenSaverMode
		omxScreensaver.onDisplayUpdate();
	}

	// DISPLAY at end of loop
	omxDisp.showDisplay();
	omxLeds.showLeds();

	// Pace the norns screen-mirror page sends (one page per loop iteration) so
	// the 4 SysEx pages of a frame don't overflow the USB TX FIFO in one burst.
	nornsLink.pump();

	while (MM::usbMidiRead())
	{
		// incoming messages - see handlers
	}
	while (MM::midiRead())
	{
		// incoming messages - see handlers
	}

	// Serial.println(clockstats.getBPM());

	// Micros elapsed = micros() - timeStart;
	// if ((timeStart - reporttime) > 2000)
	// {
	// 	report_profile_time("Elapsed", elapsed);
	// 	reporttime = timeStart;
	// 	// report_ram();
	// };

#ifdef RAM_MONITOR
	uint32_t time = millis();

	if ((time - reporttime) > 2000)
	{
		reporttime = time;
		report_ram();
	};

	ram.run();
#endif

} // ######## END MAIN LOOP ########



// ####### SETUP #######

void setup()
{

#if BOARDTYPE == TEENSY4
// 	Serial.println("Teensy 4.0");
// 	Serial.println("DAC Start!");
	dac.begin(DAC_ADDR);

#elif BOARDTYPE == OMX2040
// 	Serial.println("RP2040");
	TinyUSBDevice.setManufacturerDescriptor(mfgstr);
	TinyUSBDevice.setProductDescriptor(prodstr);

	pinMode(REDLED, OUTPUT);	// RED LED
	pinMode(BLUELED, OUTPUT);	// BLUE LED
	digitalWrite(REDLED, LOW); 	// digitalWrite(REDLED, LOW);
	digitalWrite(BLUELED, HIGH);

	pinMode(FIVEVEN, OUTPUT); 		// 5v enable Pin
	digitalWrite(FIVEVEN, HIGH);	// Turn 5v enable ON

	pinMode(TXLED, OUTPUT); 	// TX
	pinMode(RXLED, INPUT); 	// RX

	digitalWrite(TXLED, LOW);
	digitalWrite(RXLED, LOW);
	Wire1.setSDA(I2C_SDA);		// i2c1 SDA
	Wire1.setSCL(I2C_SCL);		// i2c1 SCL

// 	Serial1.setRX(RXLED);
// 	Serial1.setTX(TXLED);

	dac.begin(DAC_ADDR, &Wire1);

	// Initialize WebUSB for connection notification, etc
	// TEMP: landing page disabled so the browser doesn't auto-open the web editor on
	// reconnect. Re-enable by uncommenting the setLandingPage line below.
 	// usb_web.setLandingPage(&landingPage);
	usb_web.begin();

#else
// 	Serial.println("Teensy 3.2");
#endif

	// HW MIDI
	MM::begin();

	// CV GATE pin
	pinMode(CVGATE_PIN, OUTPUT);
	// ENCODER BUTTON pin
	pinMode(buttonPin, INPUT_PULLUP);

	// Storage - FIX?
	storage = Storage::initStorage();
	sysEx = new SysEx(storage, &sysSettings);
	// Serial.println( "initStorage" );

#ifdef RAM_MONITOR
	ram.initialize();
#endif

	// clksTimer = 0; // TODO - didn't see this used anywhere
	omxScreensaver.resetCounter();
	// ssstep = 0;

	lastProcessTime = micros();
	omxUtil.restartClocks();
	omxUtil.subModeClearStorage.setStoragePtr(storage);


// #if BOARDTYPE == OMX2040
	// while (!TinyUSBDevice.mounted()){
	// 	delay(100);
	// }
// #endif

	// Serial
	Serial.begin(115200);
	delay(100);


	// SET ANALOG READ resolution to teensy's 13 usable bits
#if BOARDTYPE == TEENSY4
	randomSeed(analogRead(13));
	srand(analogRead(13));
	analogReadResolution(10); // Teensy 4 = 10 bits
#elif BOARDTYPE == OMX2040
// 	randomSeed(analogRead(29));
// 	srand(analogRead(29));
	analogReadResolution(10); // MUX = 10 bits
#else
	randomSeed(analogRead(13));
	srand(analogRead(13));
	analogReadResolution(13); // Teensy 3.x = 13 bits
#endif


	// initialize ANALOG INPUTS and ResponsiveAnalogRead
	for (int i = 0; i < potCount; i++)
	{
// 		potSettings.analog[i] = new ResponsiveAnalogRead(0, true, .001);
// 		potSettings.analog[i]->setAnalogResolution(1 << 13);

#if BOARDTYPE == TEENSY4
		pinMode(analogPins[i], INPUT);
		potSettings.analog[i] = new ResponsiveAnalogRead(analogPins[i], true, .001);
// 		potSettings.analog[i]->setAnalogResolution(10);
//		potSettings.analog[i]->setActivityThreshold(8);
#elif BOARDTYPE == OMX2040
		potSettings.analog[i] = new ResponsiveAnalogRead(mux_common_pin, true, .001);

#else
		pinMode(analogPins[i], INPUT);
		potSettings.analog[i] = new ResponsiveAnalogRead(analogPins[i], true, .001);
		potSettings.analog[i]->setAnalogResolution(1 << 13);
		potSettings.analog[i]->setActivityThreshold(32);
#endif

		currentValue[i] = 0;
		lastMidiValue[i] = 0;
	}

	// set DAC Resolution CV/GATE
	RES = 12;
	AMAX = pow(2, RES);
	V_scale = 64; // pow(2,(RES-7)); 4095 max

#if BOARDTYPE == TEENSY4
	dac.setVoltage(0, false);
#elif BOARDTYPE == OMX2040
	dac.setVoltage(0, false);
#else
	analogWriteResolution(RES); // set resolution for DAC
	analogWrite(CVPITCH_PIN, 0);
#endif

	globalScale.calculateScale(scaleConfig.scaleRoot, scaleConfig.scalePattern);
	omxModeMidi.SetScale(&globalScale);
	omxModeDrum.SetScale(&globalScale);
#ifdef OMXMODESEQ
	omxModeSeq.SetScale(&globalScale);
#endif
#ifdef OMXMODEGRIDS
	omxModeGrids.SetScale(&globalScale);
#endif
	omxModeEuclid.SetScale(&globalScale);
	omxModeChords.SetScale(&globalScale);
	omxModeForm.SetScale(&globalScale);
	omxModeConfig.SetScale(&globalScale);

	// Keypad
	//	customKeypad.begin();
	keypad.begin();
	// Serial.println( "Init keypad" );

	// Init Display
	omxDisp.setup();

	// Startup screen
	omxDisp.drawStartupScreen();

	// LEDs
	omxLeds.initSetup();
	// Serial.println( "Init LEDs" );


	// Load settings from EEPROM
	// bool bLoaded = false; // loadFromStorage();
	// Serial.println( "Load from EEPROM" );
	bool bLoaded = loadFromStorage();

	if (!bLoaded)
	{
		// Serial.println( "Init load fail. Reinitializing" );

		// Failed to load due to initialized EEPROM or version mismatch
		// defaults
		// sysSettings.omxMode = DEFAULT_MODE;

#ifdef OMXMODESEQ
		sequencer.playingPattern = 0;
		#endif
		sysSettings.playingPattern = 0;
		sysSettings.midiChannel = 1;
		pots[0][0] = CC1;
		pots[0][1] = CC2;
		pots[0][2] = CC3;
		pots[0][3] = CC4;
		pots[0][4] = CC5;

#ifdef OMXMODESEQ
		omxModeSeq.initPatterns();
		#endif

		changeOmxMode(DEFAULT_MODE);
		// initPatterns();
		// The FORM pattern bank lives in LittleFS and survives an FRAM wipe — pull it
		// back BEFORE the reinit save below, so saveToStorage() re-persists the real
		// bank instead of overwriting the flash file with empty defaults.
		omxModeForm.restoreBankFromFS();
		saveToStorage();
	}


#ifdef RAM_MONITOR
	reporttime = millis();
#endif


}

// ####### END SETUP #######
