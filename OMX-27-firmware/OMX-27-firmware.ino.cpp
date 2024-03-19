# 1 "/var/folders/s9/jr6440vj5hq_rk4tm6djbj5r0000gn/T/tmpux3g3suw"
#include <Arduino.h>
# 1 "/Users/quixotic7mini/Documents/GitHub/OMX-27/OMX-27-firmware/OMX-27-firmware.ino"
# 18 "/Users/quixotic7mini/Documents/GitHub/OMX-27/OMX-27-firmware/OMX-27-firmware.ino"
#include <functional>
#include <ResponsiveAnalogRead.h>
#include "src/consts/consts.h"
#include "src/config.h"
#include "src/consts/colors.h"
#include "src/midi/midi.h"
#include "src/ClearUI/ClearUI.h"
#ifdef OMXMODESEQ
#include "src/modes/sequencer.h"
#endif
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
#include "src/modes/omx_screensaver.h"
#include "src/hardware/omx_leds.h"
#include "src/utils/music_scales.h"


extern "C"
{
 int _getpid() { return -1; }
 int _kill(int pid, int sig) { return -1; }
 int _write() { return -1; }
}






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

OmxModeInterface *activeOmxMode;

OmxScreensaver omxScreensaver;

MusicScales globalScale;


int volatile currentValue[NUM_CC_POTS];
int lastMidiValue[NUM_CC_POTS];

int temp;

Micros lastProcessTime;

uint8_t RES;
uint16_t AMAX;
int V_scale;


Encoder myEncoder(12, 11);
const int buttonPin = 0;
int buttonState = 1;
Button encButton(buttonPin);






unsigned long longPressInterval = 800;
unsigned long clickWindow = 200;
OMXKeypad keypad(longPressInterval, clickWindow, makeKeymap(keys), rowPins, colPins, ROWS, COLS);


Storage *storage;
SysEx *sysEx;

#ifdef RAM_MONITOR
RamMonitor ram;
uint32_t reporttime;
void report_ram_stat(const char *aname, uint32_t avalue);
void report_profile_time(const char *aname, uint32_t avalue);
void report_ram();
void changeOmxMode(OMXMode newOmxmode);
void readPotentimeters();
void handleNoteOn(byte channel, byte note, byte velocity);
void handleNoteOff(byte channel, byte note, byte velocity);
void handleControlChange(byte channel, byte control, byte value);
void OnNoteOn(byte channel, byte note, byte velocity);
void OnNoteOff(byte channel, byte note, byte velocity);
void OnControlChange(byte channel, byte control, byte value);
void OnSysEx(const uint8_t *data, uint16_t length, bool complete);
void saveHeader();
bool loadHeader(void);
void savePatterns(void);
void loadPatterns(void);
void saveToStorage(void);
bool loadFromStorage(void);
void loop();
void setup();
#line 116 "/Users/quixotic7mini/Documents/GitHub/OMX-27/OMX-27-firmware/OMX-27-firmware.ino"
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

  temp = analogRead(analogPins[k]);
  potSettings.analog[k]->update(temp);


  temp = potSettings.analog[k]->getValue();
  temp = constrain(temp, potMinVal, potMaxVal);
  temp = map(temp, potMinVal, potMaxVal, 0, 16383);
  potSettings.hiResPotVal[k] = temp;


  potSettings.analogValues[k] = temp >> 7;

  int newAnalog = potSettings.analog[k]->getValue();


  int analogDelta = abs(newAnalog - prevAnalog);
# 262 "/Users/quixotic7mini/Documents/GitHub/OMX-27/OMX-27-firmware/OMX-27-firmware.ino"
  if (potSettings.analog[k]->hasChanged())
  {

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



void handleNoteOn(byte channel, byte note, byte velocity)
{
 if (midiSettings.midiSoftThru)
 {
  MM::sendNoteOnHW(note, velocity, channel);
 }
 if (midiSettings.midiInToCV)
 {
  cvNoteUtil.cvNoteOn(note);
 }

 omxScreensaver.resetCounter();

 activeOmxMode->inMidiNoteOn(channel, note, velocity);
}

void handleNoteOff(byte channel, byte note, byte velocity)
{
 if (midiSettings.midiSoftThru)
 {
  MM::sendNoteOffHW(note, velocity, channel);
 }

 if (midiSettings.midiInToCV)
 {
  cvNoteUtil.cvNoteOff(note);
 }

 activeOmxMode->inMidiNoteOff(channel, note, velocity);
}

void handleControlChange(byte channel, byte control, byte value)
{
 if (midiSettings.midiSoftThru)
 {
  MM::sendControlChangeHW(control, value, channel);
 }

 activeOmxMode->inMidiControlChange(channel, control, value);
}


void OnNoteOn(byte channel, byte note, byte velocity)
{
 handleNoteOn(channel, note, velocity);
}
void OnNoteOff(byte channel, byte note, byte velocity)
{
 handleNoteOff(channel, note, velocity);
}
void OnControlChange(byte channel, byte control, byte value)
{
 handleControlChange(channel, control, value);
}

void OnSysEx(const uint8_t *data, uint16_t length, bool complete)
{
 sysEx->processIncomingSysex(data, length);
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


}



bool loadHeader(void)
{
 uint8_t version = storage->read(EEPROM_HEADER_ADDRESS + 0);

 char buf[64];
 snprintf(buf, sizeof(buf), "EEPROM Header Version is %d\n", version);
 Serial.print(buf);


 if (version == 0xFF)
 {

  Serial.println("version was 0xFF");
  return false;
 }

 if (version != EEPROM_VERSION)
 {


  Serial.println("version not matched");
  return false;
 }

 sysSettings.omxMode = (OMXMode)storage->read(EEPROM_HEADER_ADDRESS + 1);

#ifdef OMXMODESEQ
 sequencer.playingPattern = storage->read(EEPROM_HEADER_ADDRESS + 2);
 sysSettings.playingPattern = sequencer.playingPattern;
#endif

 uint8_t unMidiChannel = storage->read(EEPROM_HEADER_ADDRESS + 3);
 sysSettings.midiChannel = unMidiChannel + 1;

 Serial.println("Loading banks");
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
 Serial.println((String)"nLocalAddress: " + nLocalAddress);

#ifndef OMXMODESEQ
 Serial.println("Saving FORM");
 Serial.println((String)"nLocalAddress: " + nLocalAddress);
 nLocalAddress = omxModeForm.saveToDisk(nLocalAddress, storage);
 Serial.println((String)"nLocalAddress: " + nLocalAddress);
#endif

#ifdef OMXMODEGRIDS
 Serial.println("Saving Grids");


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
 Serial.println((String)"nLocalAddress: " + nLocalAddress);
#endif

 Serial.println("Saving Euclidean");
 nLocalAddress = omxModeEuclid.saveToDisk(nLocalAddress, storage);
 Serial.println((String)"nLocalAddress: " + nLocalAddress);

 Serial.println("Saving Chords");
 nLocalAddress = omxModeChords.saveToDisk(nLocalAddress, storage);
 Serial.println((String)"nLocalAddress: " + nLocalAddress);

 Serial.println("Saving Drums");
 nLocalAddress = omxModeDrum.saveToDisk(nLocalAddress, storage);
 Serial.println((String)"nLocalAddress: " + nLocalAddress);

 Serial.println("Saving MidiFX");
 for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
 {
  nLocalAddress = subModeMidiFx[i].saveToDisk(nLocalAddress, storage);


 }
 Serial.println((String)"nLocalAddress: " + nLocalAddress);
# 564 "/Users/quixotic7mini/Documents/GitHub/OMX-27/OMX-27-firmware/OMX-27-firmware.ino"
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

 Serial.print("Pattern size: ");
 Serial.print(patternSize);

 Serial.print(" - nLocalAddress: ");
 Serial.println(nLocalAddress);

 Serial.print("Loading Euclidean - ");
 nLocalAddress = omxModeEuclid.loadFromDisk(nLocalAddress, storage);
 Serial.println((String) "nLocalAddress: " + nLocalAddress);

 Serial.print("Loading Chords - ");
 nLocalAddress = omxModeChords.loadFromDisk(nLocalAddress, storage);
 Serial.println((String)"nLocalAddress: " + nLocalAddress);

 Serial.print("Loading Drums - ");
 nLocalAddress = omxModeDrum.loadFromDisk(nLocalAddress, storage);
 Serial.println((String)"nLocalAddress: " + nLocalAddress);



 Serial.print("Loading MidiFX - ");
 for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
 {
  nLocalAddress = subModeMidiFx[i].loadFromDisk(nLocalAddress, storage);


 }
 Serial.println((String) "nLocalAddress: " + nLocalAddress);
# 677 "/Users/quixotic7mini/Documents/GitHub/OMX-27/OMX-27-firmware/OMX-27-firmware.ino"
}


void saveToStorage(void)
{
 Serial.println("Saving to Storage...");
 saveHeader();
 savePatterns();
}


bool loadFromStorage(void)
{



 Serial.println("Read the header");
 bool bContainedData = loadHeader();

 if (bContainedData)
 {
  Serial.println("Loading patterns");
  loadPatterns();
  changeOmxMode(sysSettings.omxMode);

  omxDisp.isDirty();
  omxLeds.isDirty();
  return true;
 }

 Serial.println("-- Failed to load --");

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
   omxScreensaver.resetCounter();
  }
  omxUtil.advanceClock(activeOmxMode, passed);
  omxUtil.advanceSteps(passed);
 }


 display.clearDisplay();




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
  omxScreensaver.resetCounter();




  if (encoderConfig.enc_edit)
  {


   sysSettings.newmode = (OMXMode)constrain(sysSettings.newmode + amt, 0, NUM_OMX_MODES - 1);


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

 while (MM::usbMidiRead())
 {

 }
 while (MM::midiRead())
 {

 }
# 968 "/Users/quixotic7mini/Documents/GitHub/OMX-27/OMX-27-firmware/OMX-27-firmware.ino"
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
 Serial.begin(115200);

#if T4
 Serial.println("Teensy 4.0");

 dac.begin(DAC_ADDR);
#else
 Serial.println("Teensy 3.2");
#endif

 omxDisp.setup();


 omxDisp.drawStartupScreen();


 storage = Storage::initStorage();
 sysEx = new SysEx(storage, &sysSettings);

#ifdef RAM_MONITOR
 ram.initialize();
#endif


 usbMIDI.setHandleNoteOff(OnNoteOff);
 usbMIDI.setHandleNoteOn(OnNoteOn);
 usbMIDI.setHandleControlChange(OnControlChange);
 usbMIDI.setHandleSystemExclusive(OnSysEx);


 omxScreensaver.resetCounter();


 lastProcessTime = micros();
 omxUtil.resetClocks();
 omxUtil.subModeClearStorage.setStoragePtr(storage);


 MM::begin();

 randomSeed(analogRead(13));
 srand(analogRead(13));


#if T4
 analogReadResolution(10);
#else
 analogReadResolution(13);
#endif


 pinMode(CVGATE_PIN, OUTPUT);

 pinMode(buttonPin, INPUT_PULLUP);


 for (int i = 0; i < potCount; i++)
 {


  pinMode(analogPins[i], INPUT);
  potSettings.analog[i] = new ResponsiveAnalogRead(analogPins[i], true, .001);

#if T4


#else
  potSettings.analog[i]->setAnalogResolution(1 << 13);
  potSettings.analog[i]->setActivityThreshold(32);
#endif

  currentValue[i] = 0;
  lastMidiValue[i] = 0;
 }


 RES = 12;
 AMAX = pow(2, RES);
 V_scale = 64;

#if T4
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


 bool bLoaded = loadFromStorage();
 if (!bLoaded)
 {
  Serial.println( "Init load fail. Reinitializing" );





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



 keypad.begin();


 omxLeds.initSetup();


#ifdef RAM_MONITOR
 reporttime = millis();
#endif
}