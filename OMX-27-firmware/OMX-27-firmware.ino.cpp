# 1 "/var/folders/s9/jr6440vj5hq_rk4tm6djbj5r0000gn/T/tmpj64ha68c"
#include <Arduino.h>
# 1 "/Volumes/Q7Media-2025/Projects/Norns/github/OMX-27-Q7/OMX-27-firmware/OMX-27-firmware.ino"
# 18 "/Volumes/Q7Media-2025/Projects/Norns/github/OMX-27-Q7/OMX-27-firmware/OMX-27-firmware.ino"
#include <functional>
#include "src/consts/consts.h"
#include "src/globals.h"
#include "src/config.h"
#include <ResponsiveAnalogRead.h>
#include "src/midi/midi.h"
#include "src/consts/colors.h"
#include "src/ClearUI/ClearUI.h"


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
#include "src/modes/omx_screensaver.h"
#include "src/utils/music_scales.h"
#include "src/hardware/omx_leds.h"
#include "src/midi/MIDIClockStats.h"
#include "src/midi/norns_link.h"



#if BOARDTYPE != OMX2040
extern "C"
{
 int _getpid() { return -1; }
 int _kill(int pid, int sig) { return -1; }
 int _write(int file, char *ptr, int len) { return -1; }
}
#endif






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

OmxModeInterface *activeOmxMode;
# 93 "/Volumes/Q7Media-2025/Projects/Norns/github/OMX-27-Q7/OMX-27-firmware/OMX-27-firmware.ino"
void omxInjectInput(const uint8_t *d, unsigned n);
void report_ram_stat(const char *aname, uint32_t avalue);
void report_profile_time(const char *aname, uint32_t avalue);
void report_ram();
void changeOmxMode(OMXMode newOmxmode);
void readPotentimeters();
void saveHeader();
bool loadHeader(void);
void savePatterns(void);
void loadPatterns(void);
void saveToStorage(void);
bool loadFromStorage(void);
void loop();
void setup();
#line 93 "/Volumes/Q7Media-2025/Projects/Norns/github/OMX-27-Q7/OMX-27-firmware/OMX-27-firmware.ino"
void omxInjectInput(const uint8_t *d, unsigned n)
{
 if (activeOmxMode == nullptr || n < 6)
  return;
 switch (d[5])
 {
 case 0x00:
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
 case 0x01:
  if (n >= 8)
  {
   int16_t dir = (d[6] == 0) ? -1 : (d[6] == 2 ? 1 : 0);
   int16_t speedup = (n >= 9) ? (int16_t)d[8] : 0;
   for (uint8_t i = 0; i < d[7]; i++)
    activeOmxMode->onEncoderChanged(Encoder::makeUpdate(dir, speedup));
  }
  break;
 case 0x02:
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
 case 0x03:
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


int volatile currentValue[NUM_CC_POTS];
int lastMidiValue[NUM_CC_POTS];

int temp;

Micros lastProcessTime;

uint8_t RES;
uint16_t AMAX;
int V_scale;


#if BOARDTYPE == OMX2040
 Encoder myEncoder(25, 26);
 const int buttonPin = 20;
#else
 Encoder myEncoder(12, 11);
 const int buttonPin = 0;
#endif
int buttonState = 1;
Button encButton(buttonPin);




#if BOARDTYPE == OMX2040
 char mfgstr[32] = "denki-oto";
 char prodstr[32] = "omx-27-v3";

 const int muxMapping[5] = {2,3,0,1,4};
 const int mux_common_pin = 29;
 const int mux1 = 23;
 const int mux2 = 24;
 const int mux3 = 22;
 using namespace admux;
 Mux mux(Pin(mux_common_pin, INPUT, PinType::Analog), Pinset(mux1, mux2, mux3));




 Adafruit_USBD_WebUSB usb_web;
 WEBUSB_URL_DEF(landingPage, 1 , "okyeron.github.io/web-editor/index.html");

#endif



unsigned long longPressInterval = 800;
unsigned long clickWindow = 200;
OMXKeypad keypad(longPressInterval, clickWindow, makeKeymap(keys), rowPins, colPins, ROWS, COLS);





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



void changeOmxMode(OMXMode newOmxmode)
{

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
 default:
  omxModeMidi.setMidiMode();
  activeOmxMode = &omxModeMidi;
  break;
 }

 activeOmxMode->onModeActivated();

 omxLeds.setDirty();
 omxDisp.setDirty();
}




void readPotentimeters()
{
 for (int k = 0; k < potCount; k++)
 {
  int prevValue = potSettings.analogValues[k];
  int prevAnalog = potSettings.analog[k]->getValue();
#if BOARDTYPE == OMX2040
  temp = mux.read(muxMapping[k]);

#else
  temp = analogRead(analogPins[k]);
#endif
  potSettings.analog[k]->update(temp);

  temp = potSettings.analog[k]->getValue();
  temp = constrain(temp, potMinVal, potMaxVal);
  temp = map(temp, potMinVal, potMaxVal, 0, 16383);
  potSettings.hiResPotVal[k] = temp;


  potSettings.analogValues[k] = temp >> 7;

  int newAnalog = potSettings.analog[k]->getValue();


  int analogDelta = abs(newAnalog - prevAnalog);
# 365 "/Volumes/Q7Media-2025/Projects/Norns/github/OMX-27-Q7/OMX-27-firmware/OMX-27-firmware.ino"
  if (potSettings.analog[k]->hasChanged())
  {
   nornsLink.markActivity();

   if (sysSettings.screenSaverMode)
   {
    omxScreensaver.onPotChanged(k, prevValue, potSettings.analogValues[k], analogDelta);
   }

   else
   {
    activeOmxMode->onPotChanged(k, prevValue, potSettings.analogValues[k], analogDelta);
   }
  }

 }
}



void saveHeader()
{

 storage->write(EEPROM_HEADER_ADDRESS + 0, EEPROM_VERSION);


 storage->write(EEPROM_HEADER_ADDRESS + 1, (uint8_t)sysSettings.omxMode);


#ifdef OMXMODESEQ
 storage->write(EEPROM_HEADER_ADDRESS + 2, (uint8_t)sequencer.playingPattern);
 #endif


 uint8_t unMidiChannel = (uint8_t)(sysSettings.midiChannel - 1);
 storage->write(EEPROM_HEADER_ADDRESS + 3, unMidiChannel);

 for (int b = 0; b < NUM_CC_BANKS; b++)
 {
  for (int i = 0; i < NUM_CC_POTS; i++)
  {
   storage->write(EEPROM_HEADER_ADDRESS + 4 + i + (5 * b), pots[b][i]);
  }
 }


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



bool loadHeader(void)
{
 uint8_t version = storage->read(EEPROM_HEADER_ADDRESS + 0);

 char buf[64];
 snprintf(buf, sizeof(buf), "EEPROM Header Version is %d\n", version);



 if (version == 0xFF)
 {


  return false;
 }

 if (version != EEPROM_VERSION)
 {



  return false;
 }

 sysSettings.omxMode = (OMXMode)storage->read(EEPROM_HEADER_ADDRESS + 1);

#ifdef OMXMODESEQ
 sequencer.playingPattern = storage->read(EEPROM_HEADER_ADDRESS + 2);
 sysSettings.playingPattern = sequencer.playingPattern;
#endif

 uint8_t unMidiChannel = storage->read(EEPROM_HEADER_ADDRESS + 3);
 sysSettings.midiChannel = unMidiChannel + 1;


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


 uint16_t bpm = (uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 40) | ((uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 41) << 8);
 clockConfig.clockbpm = constrain((int)bpm, 40, 300);
 omxUtil.resetClocks();
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

 uint16_t keyBgHue = (uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 51) | ((uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 52) << 8);
 if (keyBgHue != 0xFFFF)
  colorConfig.midiBg_Hue = keyBgHue;
 uint16_t ssHue = (uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 53) | ((uint16_t)storage->read(EEPROM_HEADER_ADDRESS + 54) << 8);
 if (ssHue != 0xFFFF)
  colorConfig.screensaverColor = ssHue;


 return true;
}

void savePatterns(void)
{
 bool isEeprom = storage->isEeprom();

 int nLocalAddress = EEPROM_PATTERN_ADDRESS;

 int patternSize = 0;

#ifdef OMXMODESEQ
 patternSize = serializedPatternSize(isEeprom);


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


#ifndef OMXMODESEQ
 Serial.println("Saving FORM");
 Serial.println((String)"nLocalAddress: " + nLocalAddress);
 nLocalAddress = omxModeForm.saveToDisk(nLocalAddress, storage);
 Serial.println((String)"nLocalAddress: " + nLocalAddress);
#endif

#ifdef OMXMODEGRIDS



 patternSize = OmxModeGrids::serializedPatternSize(isEeprom);
 int numPatterns = OmxModeGrids::getNumPatterns();




 for (int i = 0; i < numPatterns; i++)
 {
  auto pattern = (byte *)omxModeGrids.getPattern(i);
  for (int j = 0; j < patternSize; j++)
  {
   storage->write(nLocalAddress + j, *pattern++);
  }

  nLocalAddress += patternSize;
 }

#endif


 nLocalAddress = omxModeEuclid.saveToDisk(nLocalAddress, storage);



 nLocalAddress = omxModeChords.saveToDisk(nLocalAddress, storage);



 nLocalAddress = omxModeDrum.saveToDisk(nLocalAddress, storage);



 for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
 {
  nLocalAddress = subModeMidiFx[i].saveToDisk(nLocalAddress, storage);


 }
# 654 "/Volumes/Q7Media-2025/Projects/Norns/github/OMX-27-Q7/OMX-27-firmware/OMX-27-firmware.ino"
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
 Serial.println((String) "nLocalAddress: " + nLocalAddress);
 nLocalAddress = omxModeForm.loadFromDisk(nLocalAddress, storage);
 Serial.println((String) "nLocalAddress: " + nLocalAddress);
#endif

 Serial.print("Grids patterns - nLocalAddress: ");
 Serial.println(nLocalAddress);




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
# 729 "/Volumes/Q7Media-2025/Projects/Norns/github/OMX-27-Q7/OMX-27-firmware/OMX-27-firmware.ino"
 nLocalAddress = omxModeEuclid.loadFromDisk(nLocalAddress, storage);



 nLocalAddress = omxModeChords.loadFromDisk(nLocalAddress, storage);



 nLocalAddress = omxModeDrum.loadFromDisk(nLocalAddress, storage);





 for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
 {
  nLocalAddress = subModeMidiFx[i].loadFromDisk(nLocalAddress, storage);


 }
# 767 "/Volumes/Q7Media-2025/Projects/Norns/github/OMX-27-Q7/OMX-27-firmware/OMX-27-firmware.ino"
}


void saveToStorage(void)
{

 saveHeader();
 savePatterns();
}


bool loadFromStorage(void)
{





 bool bContainedData = loadHeader();

 if (bContainedData)
 {

  loadPatterns();
  changeOmxMode(sysSettings.omxMode);

  omxDisp.isDirty();
  omxLeds.isDirty();
  return true;
 }



 omxDisp.isDirty();
 omxLeds.isDirty();

 return false;
}



void loop()
{

 keypad.tick();


 Micros now = micros();
 Micros passed = now - lastProcessTime;
 lastProcessTime = now;

 sysSettings.timeElasped = passed;

 seqConfig.currentFrameMicros = micros();

 activeOmxMode->loopUpdate(passed);
 cvNoteUtil.loopUpdate(passed);

 if (passed > 0)
 {
  bool seqPlaying = false;

#ifdef OMXMODESEQ
  seqPlaying = sequencer.playing;
#endif
  if (seqPlaying || omxUtil.areClocksRunning())
  {
   { static unsigned long t=0; if (millis()-t>500){t=millis(); Serial.println("SSDBG rst:clocks");} }
   omxScreensaver.resetCounter();
  }
  omxUtil.advanceClock(activeOmxMode, passed);
  omxUtil.advanceSteps(passed);
 }


 display.clearDisplay();







 if (nornsLink.mirrorEnabled())
 {
  { static unsigned long t=0; if (millis()-t>500){t=millis(); Serial.println("SSDBG rst:mirror");} }
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



 readPotentimeters();

 bool omxModeChangedThisFrame = false;


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



 auto u = myEncoder.update();

 if (u.active())
 {
  auto amt = u.accel(1);
  { static unsigned long t=0; if (millis()-t>500){t=millis(); Serial.println("SSDBG rst:enc");} }
  omxScreensaver.resetCounter();
  nornsLink.markActivity();




  if (encoderConfig.enc_edit)
  {


   int newMode = constrain((int)sysSettings.newmode + amt, 0, NUM_OMX_MODES - 1);
#ifndef OMXMODESEQ


   int skipDir = (amt < 0) ? -1 : 1;
   while ((newMode == MODE_S1 || newMode == MODE_S2) && newMode > 0 && newMode < (NUM_OMX_MODES - 1))
   {
    newMode += skipDir;
   }
#endif
   sysSettings.newmode = (OMXMode)newMode;


   omxDisp.setDirty();
   omxLeds.setDirty();
  }
  else
  {
   activeOmxMode->onEncoderChanged(u);
  }
 }




 auto s = encButton.update();
 switch (s)
 {

 case Button::Down:
  omxScreensaver.resetCounter();
  nornsLink.markActivity();


  if (sysSettings.newmode != sysSettings.omxMode && encoderConfig.enc_edit)
  {
   changeOmxMode(sysSettings.newmode);
   omxModeChangedThisFrame = true;
#ifdef OMXMODESEQ
   seqStop();
#endif
   omxLeds.setAllLEDS(0, 0, 0);
   encoderConfig.enc_edit = false;

   omxDisp.setDirty();
  }
  else if (encoderConfig.enc_edit)
  {
   encoderConfig.enc_edit = false;
  }


  if (!omxModeChangedThisFrame)
  {
   activeOmxMode->onEncoderButtonDown();
  }

  omxDisp.setDirty();
  break;


 case Button::DownLong:
  if (activeOmxMode->shouldBlockEncEdit())
  {
   activeOmxMode->onEncoderButtonDown();
  }
  else
  {

   encoderConfig.enc_edit = true;
   sysSettings.newmode = sysSettings.omxMode;
   omxLeds.setAllLEDS(0, 0, 0);
   omxDisp.setDirty();

  }

  omxDisp.setDirty();
  break;
 case Button::Up:
  activeOmxMode->onEncoderButtonUp();
  break;
 case Button::UpLong:
  activeOmxMode->onEncoderButtonUpLong();
  break;
 default:
  break;
 }




 while (keypad.available())
 {

  auto e = keypad.next();
  int thisKey = e.key();
  bool keyConsumed = false;



  if (e.down())
  {
   omxScreensaver.resetCounter();
   nornsLink.markActivity();
   midiSettings.keyState[thisKey] = true;
  }

  if (e.down() && thisKey == 0 && encoderConfig.enc_edit)
  {

   omxDisp.displayMessage("Saving...");
   omxDisp.isDirty();
   omxDisp.showDisplay();
   saveToStorage();

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



  if (!e.down())
  {
   midiSettings.keyState[thisKey] = false;
  }


  if (e.held() && !keyConsumed)
  {

   activeOmxMode->onKeyHeldUpdate(e);
  }

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
 {
  omxScreensaver.onDisplayUpdate();
 }


 omxDisp.showDisplay();
 omxLeds.showLeds();



 nornsLink.pump();

 while (MM::usbMidiRead())
 {

 }
 while (MM::midiRead())
 {

 }
# 1095 "/Volumes/Q7Media-2025/Projects/Norns/github/OMX-27-Q7/OMX-27-firmware/OMX-27-firmware.ino"
#ifdef RAM_MONITOR
 uint32_t time = millis();

 if ((time - reporttime) > 2000)
 {
  reporttime = time;
  report_ram();
 };

 ram.run();
#endif

}





void setup()
{

#if BOARDTYPE == TEENSY4


 dac.begin(DAC_ADDR);

#elif BOARDTYPE == OMX2040

 TinyUSBDevice.setManufacturerDescriptor(mfgstr);
 TinyUSBDevice.setProductDescriptor(prodstr);

 pinMode(REDLED, OUTPUT);
 pinMode(BLUELED, OUTPUT);
 digitalWrite(REDLED, LOW);
 digitalWrite(BLUELED, HIGH);

 pinMode(FIVEVEN, OUTPUT);
 digitalWrite(FIVEVEN, HIGH);

 pinMode(TXLED, OUTPUT);
 pinMode(RXLED, INPUT);

 digitalWrite(TXLED, LOW);
 digitalWrite(RXLED, LOW);
 Wire1.setSDA(I2C_SDA);
 Wire1.setSCL(I2C_SCL);




 dac.begin(DAC_ADDR, &Wire1);





 usb_web.begin();

#else

#endif


 MM::begin();


 pinMode(CVGATE_PIN, OUTPUT);

 pinMode(buttonPin, INPUT_PULLUP);


 storage = Storage::initStorage();
 sysEx = new SysEx(storage, &sysSettings);


#ifdef RAM_MONITOR
 ram.initialize();
#endif


 omxScreensaver.resetCounter();


 lastProcessTime = micros();
 omxUtil.restartClocks();
 omxUtil.subModeClearStorage.setStoragePtr(storage);
# 1190 "/Volumes/Q7Media-2025/Projects/Norns/github/OMX-27-Q7/OMX-27-firmware/OMX-27-firmware.ino"
 Serial.begin(115200);
 delay(100);



#if BOARDTYPE == TEENSY4
 randomSeed(analogRead(13));
 srand(analogRead(13));
 analogReadResolution(10);
#elif BOARDTYPE == OMX2040


 analogReadResolution(10);
#else
 randomSeed(analogRead(13));
 srand(analogRead(13));
 analogReadResolution(13);
#endif



 for (int i = 0; i < potCount; i++)
 {



#if BOARDTYPE == TEENSY4
  pinMode(analogPins[i], INPUT);
  potSettings.analog[i] = new ResponsiveAnalogRead(analogPins[i], true, .001);


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


 RES = 12;
 AMAX = pow(2, RES);
 V_scale = 64;

#if BOARDTYPE == TEENSY4
 dac.setVoltage(0, false);
#elif BOARDTYPE == OMX2040
 dac.setVoltage(0, false);
#else
 analogWriteResolution(RES);
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



 keypad.begin();



 omxDisp.setup();


 omxDisp.drawStartupScreen();


 omxLeds.initSetup();






 bool bLoaded = loadFromStorage();

 if (!bLoaded)
 {






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

  saveToStorage();
 }


#ifdef RAM_MONITOR
 reporttime = millis();
#endif


}