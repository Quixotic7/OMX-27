#pragma once

#include <Arduino.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <ResponsiveAnalogRead.h>

// #include <cstdarg>

//const int OMX_VERSION = 1.6.0;

/* * firmware metadata  */
// OMX_VERSION = 1.6.0
const int MAJOR_VERSION = 1;
const int MINOR_VERSION = 6;
const int POINT_VERSION = 0;

const int DEVICE_ID     = 2;

enum OMXMode
{
	MODE_MIDI = 0,
	MODE_S1,
	MODE_S2,
	MODE_OM,

	NUM_OMX_MODES
};

extern const OMXMode DEFAULT_MODE;

// Increment this when data layout in EEPROM changes. May need to write version upgrade readers when this changes.
extern const uint8_t EEPROM_VERSION;

#define EEPROM_HEADER_ADDRESS            0
#define EEPROM_HEADER_SIZE               32
#define EEPROM_PATTERN_ADDRESS           32

// next address 1104 (was 1096 before clock)

// DEFINE CC NUMBERS FOR POTS // CCS mapped to Organelle Defaults
extern const int CC1;
extern const int CC2;
extern const int CC3;
extern const int CC4;
extern const int CC5;       // change to 25 for EYESY Knob 5

extern const int CC_AUX;    // Mother mode - AUX key
extern const int CC_OM1;    // Mother mode - enc switch
extern const int CC_OM2;    // Mother mode - enc turn

extern const int LED_BRIGHTNESS;

// DONT CHANGE ANYTHING BELOW HERE

extern const int LED_PIN;
extern const int LED_COUNT;

// POTS/ANALOG INPUTS - teensy pins for analog inputs
extern const int analogPins[];

#define NUM_CC_BANKS 5
#define NUM_CC_POTS 5
extern int pots[NUM_CC_BANKS][NUM_CC_POTS];         // the MIDI CC (continuous controller) for each analog input

struct SysSettings {
	OMXMode omxMode = DEFAULT_MODE;
	OMXMode newmode = DEFAULT_MODE;
	uint8_t midiChannel = 0;
	int playingPattern;
	bool refresh = false;
	bool screenSaverMode = false;
	Micros timeElasped;
};

SysSettings sysSettings;

const int potCount = NUM_CC_POTS;

struct PotSettings
{
	ResponsiveAnalogRead *analog[NUM_CC_POTS];

	// ANALOGS
	int potbank = 0;
	int analogValues[NUM_CC_POTS] = {0, 0, 0, 0, 0}; // default values
	int potValues[NUM_CC_POTS] = {0, 0, 0, 0, 0};
	int potCC = pots[potbank][0];
	int potVal = analogValues[0];
	int potNum = 0;
	bool plockDirty[NUM_CC_POTS] = {false, false, false, false, false};
	int prevPlock[NUM_CC_POTS] = {0, 0, 0, 0, 0};
};
// Put in global struct to share across classes
PotSettings potSettings;

struct MidiConfig
{
    int defaultVelocity = 100;
    int octave = 0; // default C4 is 0 - range is -4 to +5
    int newoctave = octave;
    int transpose = 0;
    int rotationAmt = 0;

    uint8_t swing = 0;
    const int maxswing = 100;
    // int swing_values[maxswing] = {0, 1, 3, 5, 52, 66, 70, 72, 80, 99 }; // 0 = off, <50 early swing , >50 late swing, 99=drunken swing

    bool keyState[27] = {false};
    int midiKeyState[27] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    int midiChannelState[27] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    int rrChannel = 0;
    bool midiRoundRobin = false;
    int midiRRChannelOffset = 0;
    int midiRRChannelCount = 1;
    uint8_t midiLastNote = 0;
    int currpgm = 0;
    int currbank = 0;
    bool midiInToCV = true;
    bool midiSoftThru = false;
};

MidiConfig midiSettings;

struct MidiMacroConfig {
	bool midiAUX = false;
	int midiMacro = 0;
	bool m8AUX = false;
	int midiMacroChan = 10;
};

MidiMacroConfig midiMacroConfig;

struct EncoderConfig {
	bool enc_edit = false;
};

EncoderConfig encoderConfig;

struct ClockConfig {
	float clockbpm = 120;
	float newtempo = clockbpm;
	unsigned long tempoStartTime;
	unsigned long tempoEndTime;
	float step_delay;
};

ClockConfig clockConfig;

struct ColorConfig
{
	uint32_t screensaverColor = 0xFF0000;
	uint32_t stepColor = 0x000000;
	uint32_t muteColor = 0x000000;
	uint16_t midiBg_Hue = 0;
	uint8_t midiBg_Sat = 255;
	uint8_t midiBg_Brightness = 255;
};

ColorConfig colorConfig;

#define NUM_DISP_PARAMS 5

extern const int gridh;
extern const int gridw;
extern const int PPQ;

extern const char* modes[];
extern const char* macromodes[];
extern const int nummacromodes;
extern const char* infoDialogText[];

enum multDiv
{
	MD_QUART = 0,
	MD_HALF,
	MD_ONE,
	MD_TWO,
	MD_FOUR,
	MD_EIGHT,
	MD_SIXTEEN,

	NUM_MULTDIVS
};

extern float multValues[];
extern const char* mdivs[];

enum Dialogs{
	COPY = 0,
	PASTE,
	CLEAR,
	RESET,
	FWD,
	REV,
	SAVED,
	SAVE,

	NUM_DIALOGS
};
struct InfoDialogs {
	const char*  text;
	bool state;
};

extern InfoDialogs infoDialog[NUM_DIALOGS];

enum SubModes
{
	SUBMODE_MIDI = 0,
	SUBMODE_SEQ,
	SUBMODE_SEQ2,
	SUBMODE_NOTESEL,
	SUBMODE_NOTESEL2,
	SUBMODE_NOTESEL3,
	SUBMODE_PATTPARAMS,
	SUBMODE_PATTPARAMS2,
	SUBMODE_PATTPARAMS3,
	SUBMODE_STEPREC,
	SUBMODE_MIDI2,
	SUBMODE_MIDI3,
	SUBMODE_MIDI4,

	SUBMODES_COUNT
};

// KEY SWITCH ROWS/COLS
#define ROWS 5 //five rows
#define COLS 6 //six columns

// Map the keys
extern char keys[ROWS][COLS];
extern byte rowPins[ROWS]; // row pins for key switches
extern byte colPins[COLS]; // column pins for key switches

// KEYBOARD MIDI NOTE LAYOUT
extern const int notes[];
extern const int steps[];
extern const int midiKeyMap[];
