#include "config.h"
#include "consts/consts.h"

const OMXMode DEFAULT_MODE = MODE_MIDI;
// v39 - merge of FormSequencer (FORM mode + shared improvements) into the q7-2026 line.
// Both branches independently used v38 for divergent layouts; bumped to 39 so existing
// saves re-initialize cleanly instead of being misread against the merged layout.
#if BOARDTYPE == TEENSY32
// Teensy 3.1 reduced FORM to 4 tracks, which shrinks FORM's slice of the sequential save
// stream and shifts every later mode's offset. A distinct version forces a one-time storage
// re-init so old 8-track saves aren't misread. Keep this one ahead of the shared version so a
// future base bump still re-inits the T3.1.
const uint8_t EEPROM_VERSION = 42;
#else
const uint8_t EEPROM_VERSION = 41;
#endif

const char* VERSION_STRING = "ALPHA";

uint8_t deviceID = DEVICE_ID; // runtime device id, editable in CONFIG mode

// v30 - adds storage to header for velocity
// v31 - adds storage for drums
// v32 - adds mfx chord saves
// v33 - adds mfx selector saves
// v34 - adds mfx repeat saves
// v35 - adds quantize rate to arps, added global quant rate to header
// v36 - adds stuff to randomizer

// DEFINE CC NUMBERS FOR POTS // CCS mapped to Organelle Defaults
const int CC1 = 21;
const int CC2 = 22;
const int CC3 = 23;
const int CC4 = 24;
const int CC5 = 7; // change to 25 for EYESY Knob 5

const int CC_AUX = 25; // Mother mode - AUX key
const int CC_OM1 = 26; // Mother mode - enc switch
const int CC_OM2 = 28; // Mother mode - enc turn

#if BOARDTYPE == OMX2040
	const int LED_BRIGHTNESS = 90;
#else
	const int LED_BRIGHTNESS = 50; // Teensy boards
#endif

uint8_t ledBrightness = LED_BRIGHTNESS;      // runtime, editable in CONFIG mode
bool screensaverEnabled = true;              // CONFIG: screensaver on/off
uint16_t screensaverTimeoutSec = 60 * 3;     // CONFIG: 3 min default

// DONT CHANGE ANYTHING BELOW HERE
const int LED_COUNT = 27;

#if BOARDTYPE == OMX2040
	const int LED_PIN = 19;
#else
	const int LED_PIN = 14;
#endif

#if DEV
	const int analogPins[] = {23, 22, 21, 20, 16}; // DEV/beta boards
	const byte DAC_ADDR = 0x62;
#elif MIDIONLY
	const int analogPins[] = {23, 22, 21, 20, 16}; // on MIDI only boards - {23,A10,21,20,16} on Bodged MIDI boards
	const byte DAC_ADDR = 0x60;
#elif BOARDTYPE == TEENSY4
	const int analogPins[] = {23, 22, 21, 20, 16}; // on 2.0
	const byte DAC_ADDR = 0x60;
#elif BOARDTYPE == OMX2040
	const int analogPins[] = {2, 3 ,0, 1, 4};	// mux pin numbers
	const byte DAC_ADDR = 0x60;
#else
	const int analogPins[] = {34, 22, 21, 20, 16}; // on 1.0
	const byte DAC_ADDR = 0x60;
#endif

const int potCount = NUM_CC_POTS;

int pots[NUM_CC_BANKS][NUM_CC_POTS] = {
	{CC1, CC2, CC3, CC4, CC5},
	{29, 30, 31, 32, 33},
	{34, 35, 36, 37, 38},
	{39, 40, 41, 42, 43},
	{91, 93, 103, 104, 7}}; // the MIDI CC (continuous controller) for each analog input

int potMinVal = 0;
#if BOARDTYPE == TEENSY4
	int potMaxVal = 1019; // T4 = 1019 // T3.2 = 8190;
#elif BOARDTYPE == OMX2040
	int potMaxVal = 1018;
#else
	int potMaxVal = 8191; // T4 = 1019 // T3.2 = 8191;
#endif

const int gridh = 32;
const int gridw = 128;
const int PPQ = 96; // Pulses Per Quarter note

const uint32_t secs2micros = 1000000;


const char *mfxOffMsg = "MidiFX are Off";
const char *mfxArpEditMsg = "Arp Edit";
const char *mfxPassthroughEditMsg = "MFX Quickedit";
const char *exitMsg = "Exit";
const char *paramOffMsg = "OFF";
const char *paramOnMsg = "ON";
const char *bool2lightswitchMsg[] = {"OFF", "ON"};
const char *bool2Msg[] = {"TRUE", "FALS"};

const char *modes[] = {"MI", "DRUM", "CH", "FORM", "S1", "S2", "GR", "EL", "OM", "CFG"};
const char *macromodes[] = {"Off", "M8", "NRN", "DEL"};
const int nummacromodes = 3;

float multValues[] = {.25, .5, 1, 2, 4, 8, 16};
const char *mdivs[] = {"1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "W"};

const float kNoteLengths[] = {0.10, 0.25, 0.5, 0.75, 1, 1.5, 2, 4, 8, 16};
const uint8_t kNumNoteLengths = 10;

const uint8_t kArpRates[] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64};
const uint8_t kNumArpRates = 16;

String tempString = "12345";
String tempStrings[8] = {"12345", "12345", "12345", "12345", "12345", "12345", "12345", "12345"};

// KEY SWITCH ROWS/COLS

// Map the keys
char keys[ROWS][COLS] = {
	{0, 1, 2, 3, 4, 5},
	{6, 7, 8, 9, 10, 26},
	{11, 12, 13, 14, 15, 24},
	{16, 17, 18, 19, 20, 25},
	{22, 23, 21}};
#if BOARDTYPE == OMX2040
	byte rowPins[ROWS] = {28, 14, 13, 12, 6};		// row pins for key switches
	byte colPins[COLS] = {10, 9, 4, 5, 8, 11}; // column pins for key switches
#else
	byte rowPins[ROWS] = {6, 4, 3, 5, 2};		// row pins for key switches
	byte colPins[COLS] = {7, 8, 10, 9, 15, 17}; // column pins for key switches
#endif

// KEYBOARD MIDI NOTE LAYOUT
const int notes[] = {0,
					 61, 63, 66, 68, 70, 73, 75, 78, 80, 82,
					 59, 60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79, 81, 83, 84};

// I'm not sure this is actually used? Just burning up memory.
const int steps[] = {0,
					 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
					 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};

// Maps note numbers starting at B to key numbers
// If using octaves, lowest midi note 0 is a C, so add 1. 
const int midiKeyMap[] = {11, 12, 1, 13, 2, 14, 15, 3, 16, 4, 17, 5, 18, 19, 6, 20, 7, 21, 22, 8, 23, 9, 24, 10, 25, 26};

Adafruit_MCP4725 dac;
MidiConfig midiSettings;
EncoderConfig encoderConfig;
ClockConfig clockConfig;
SequencerConfig seqConfig;
ColorConfig colorConfig;
ScaleConfig scaleConfig;

// MidiPage midiPageParams;
// SequencerPage seqPageParams;
