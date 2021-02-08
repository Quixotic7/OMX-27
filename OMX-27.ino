// OMX-27 MIDI KEYBOARD / SEQUENCER
// 
// Steven Noreyko, January 2021

#include <Adafruit_Keypad.h>
#include <Adafruit_NeoPixel.h>
#include <ResponsiveAnalogRead.h>
#include <MIDI.h>
MIDI_CREATE_DEFAULT_INSTANCE();
//MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

#include "ClearUI.h"
#include "OMX-27.h"
#include "sequencer.h"

const int potCount = 5;
ResponsiveAnalogRead *analog[potCount];

// storage of pot values; current is in the main loop; last value is for midi output
int volatile currentValue[potCount];
int lastMidiValue[potCount];
int potMin = 0;
int potMax = 8190;
int temp;

// Timers and such
elapsedMillis msec = 0;
elapsedMillis pots_msec = 0;
elapsedMillis checktime1 = 0;
elapsedMicros clksTimer = 0;
unsigned long clksDelay;
elapsedMillis step_interval[8] = {0,0,0,0,0,0,0,0};
unsigned long lastStepTime[8] = {0,0,0,0,0,0,0,0};
elapsedMillis keyPressTime[27] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};


// ENCODER
Encoder myEncoder(12, 11); 	// encoder pins
Button encButton(0);		// encoder button pin
long newPosition = 0;
long oldPosition = -999;


//initialize an instance of class Keypad
Adafruit_Keypad customKeypad = Adafruit_Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS); 

// Declare NeoPixel strip object
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);



// ####### POTENTIMETERS #######

void sendPots(int val){
	usbMIDI.sendControlChange(pots[val], analogValues[val], midiChannel);
	MIDI.sendControlChange(pots[val], analogValues[val], midiChannel);
	potCC = pots[val];
	potVal = analogValues[val];
	potValues[val] = potVal;
}

void readPotentimeters(){
	for(int k=0; k<potCount; k++) {
		temp = analogRead(analogPins[k]);
		analog[k]->update(temp);
		
	    // read from the smoother, constrain (to account for tolerances), and map it
    	temp = analog[k]->getValue();
    	temp = constrain(temp, potMin, potMax);
		temp = map(temp, potMin, potMax, 0, 16383);
		// map and update the value
    	analogValues[k] = temp >> 7;
   	
    	if(analog[k]->hasChanged()) {
       		// do stuff		   	
			switch(mode) { 
				case 3:
					// fall through
				case 0: // MIDI
//					usbMIDI.sendControlChange(pots[k], analogValues[k], midiChannel);
//					MIDI.sendControlChange(pots[k], analogValues[k], midiChannel);
//					potCC = pots[k];
//					potVal = analogValues[k];
					sendPots(k);
					dirtyDisplay = true;
					break;    	
				case 1: // SEQ1
					if (noteSelect && noteSelection){ // note selection - do P-Locks
						potNum = k;
						potCC = pots[k];
						potVal = analogValues[k];
								// stepNoteP[8] {notenum,vel,len,p1,p2,p3,p4,p5}
						if (k < 4){ // only store p-lock value for first 4 knobs
							stepNoteP[playingPattern][selectedStep][k+3] = analogValues[k];
						}
						sendPots(k);
						dirtyDisplay = true;
						
					} else if (!noteSelect){
						sendPots(k);
					}
					
					break;    	
				case 2: // SEQ2
					break;    	
    		}
    	}
	}
}

// ####### SETUP #######

void setup() {
	Serial.begin(115200);
	checktime1 = 0;
	clksTimer = 0;
	resetClocks();
	
	// set analog read resolution to teensy's 13 usable bits
	analogReadResolution(13);
	
	// initialize ResponsiveAnalogRead
	for (int i = 0; i < potCount; i++){
		analog[i] = new ResponsiveAnalogRead(0, true, .001);
		analog[i]->setAnalogResolution(1 << 13);

		// ResponsiveAnalogRead is designed for 10-bit ADCs
		// meanining its threshold defaults to 4. Let's bump that for 
		// our 13-bit adc by setting it to 4 << (13-10)
		analog[i]->setActivityThreshold(32);

		currentValue[i] = 0;
		lastMidiValue[i] = 0;
	}

	// hardware midi
	MIDI.begin();
		
	//CV gate pin
	pinMode(CVGATE_PIN, OUTPUT); 
		
  	// set DAC Resolution CV/GATE
    RES = 12;
    analogWriteResolution(RES); // set resolution for DAC
    AMAX = pow(2,RES);
    V_scale = 64; // pow(2,(RES-7)); 4095 max
    analogWrite(CVPITCH_PIN, 0);

  	// Init Display
	initializeDisplay();
	
	// Startup screen		
	display.clearDisplay();
	testdrawrect();
	delay(200);
	display.clearDisplay();
	display.setCursor(16,4);
	defaultText(2);
	display.setTextColor(SSD1306_WHITE);
	display.println("OMX-27");
	display.display();

	// Keypad
	customKeypad.begin();


		// Handle incoming MIDI events
		//MIDI.setHandleClock(handleExtClock);
		//MIDI.setHandleStart(handleExtStart);
		//MIDI.setHandleContinue(handleExtContinue);
		//MIDI.setHandleStop(handleExtStop);
		//MIDI.setHandleNoteOn(HandleNoteOn);  // Put only the name of the function
		//usbMIDI.setHandleNoteOn(HandleNoteOn); 
		//MIDI.setHandleControlChange(HandleControlChange);
		//usbMIDI.setHandleControlChange(HandleControlChange);
		//MIDI.setHandleNoteOff(HandleNoteOff);
		//usbMIDI.setHandleNoteOff(HandleNoteOff);

	//LEDs
	strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
	strip.show();            // Turn OFF all pixels ASAP
	strip.setBrightness(50); // Set BRIGHTNESS to about 1/5 (max = 255)

	for(int i=0; i<LED_COUNT; i++) { // For each pixel...
		strip.setPixelColor(i, HALFWHITE);
		strip.show();   // Send the updated pixel colors to the hardware.
		delay(5); // Pause before next pass through loop
	}
	rainbow(5); // rainbow startup pattern
  
	delay(100);
	strip.fill(0, 0, LED_COUNT);
	strip.show();            // Turn OFF all pixels ASAP

	delay(100);


	// Clear display and show default mode
	display.clearDisplay();
//	display.setTextSize(1);
//	display.setCursor(96, 0);
//	display.print(modes[newmode]);
//	//dispTempo();
	display.display();			

	dirtyDisplay = true;

	//Serial.println(" loading... ");
}

// ####### END SETUP #######



// ####### MIDI LEDS #######

void midi_leds() {
	if (midiAUX){
		strip.setPixelColor(0, MEDRED);
	} else {
		strip.setPixelColor(0, LEDOFF);
	}
	dirtyPixels = true;
}

// ####### SEQUENCER LEDS #######

void show_current_step(int patternNum) {
	blinkInterval = step_delay*2;
	
	if (msec >= blinkInterval){
		blinkState = !blinkState;
		msec = 0;
	}
															// AUX KEY
	if (playing && blinkState){
		strip.setPixelColor(0, HALFWHITE);
	} else if (noteSelect && blinkState){
		strip.setPixelColor(0, NOTESEL);
	} else if (patternParams && blinkState){
		strip.setPixelColor(0, PATTSEL);
	} else {
		strip.setPixelColor(0, LEDOFF);
	}
	
	if (patternMute[patternNum]){
		stepColor = muteColors[patternNum];
	} else {
		stepColor = seqColors[patternNum];
	}

	if (noteSelect && noteSelection) {
		for(int j = 1; j < NUM_STEPS+11; j++){
			if (j < patternLength[patternNum]+11){
				if (j == selectedNote){
					strip.setPixelColor(j, HALFWHITE);
				} else if (j == selectedStep+11){
					strip.setPixelColor(j, SEQSTEP);
				} else{
					strip.setPixelColor(j, LEDOFF);
				}
			} else {
				strip.setPixelColor(j, LEDOFF);
			}
		}
		
	} else {
		for(int j = 1; j < NUM_STEPS+11; j++){		
			if (j < patternLength[patternNum]+11){
				if (j == 1) {								
					strip.setPixelColor(j, FUNKONE);
// NOTE SELECT
//					if (noteSelect){
//						if (noteSelect && blinkState){
//							strip.setPixelColor(j, NOTESEL);
//						} else {
//							strip.setPixelColor(j, LEDOFF);
//						}
//					} else {
//						strip.setPixelColor(j, NOTESEL);
//					}
					
				} else if (j == 2) {
					strip.setPixelColor(j, FUNKTWO);
// PATTERN PARAMS
//					if (patternParams){
//						if (patternParams && blinkState){
//							strip.setPixelColor(j, PATTSEL);
//						} else {
//							strip.setPixelColor(j, LEDOFF);
//						}
//					} else {
//						strip.setPixelColor(j, PATTSEL);
//					}

					
				} else if (j == patternNum+3){  			// PATTERN SELECT
					strip.setPixelColor(patternNum+3, stepColor);

				} else {
					strip.setPixelColor(j, LEDOFF);
				}
			} else {
				strip.setPixelColor(j, LEDOFF);
			}
		}

		for(int i = 0; i < NUM_STEPS; i++){
			if (i < patternLength[patternNum]){
				if(i % 4 == 0){ // mark groups of 4
					if(i == seqPos[patternNum]){
						if (playing){
							strip.setPixelColor(i+11, SEQCHASE); // step chase
						} else if (stepPlay[patternNum][i] == 1){
							strip.setPixelColor(i+11, stepColor); // step on color
						} else {
							strip.setPixelColor(i+11, SEQMARKER); 
						}
//						strip.setPixelColor(i+11, SEQCHASE); // step chase
					} else if (stepPlay[patternNum][i] == 1){
						strip.setPixelColor(i+11, stepColor); // step on color
					} else {
						strip.setPixelColor(i+11, SEQMARKER); 
					}
				} else if (i == seqPos[patternNum]){
					if (playing){
						strip.setPixelColor(i+11, SEQCHASE); // step chase
					} else if (stepPlay[patternNum][i] == 1){
						strip.setPixelColor(i+11, stepColor); // step on color
					} else {
						strip.setPixelColor(i+11, LEDOFF); 
					}
//					strip.setPixelColor(i+11, SEQCHASE); // step chase

				} else if (stepPlay[patternNum][i] == 1){
					strip.setPixelColor(i+11, stepColor); // step on color

				} else {
					strip.setPixelColor(i+11, LEDOFF);
				}
			}
		}
	}
	dirtyPixels = true;
//	strip.show();
}
// ####### END LEDS


// ####### SEQENCER FUNCTIONS

void step_ahead(int patternNum) {
  // step ahead one place
    seqPos[patternNum]++;
    if (seqPos[patternNum] >= patternLength[patternNum])
      seqPos[patternNum] = 0;
}

void step_on(int patternNum){
	//	Serial.print(g);
	//	Serial.println(" step on");

}

void step_off(int patternNum, int position){
	//	Serial.print(seqPos[patternNum]);
	//	Serial.println(" step off");
      usbMIDI.sendNoteOff(lastNote[patternNum][position], 0, midiChannel);
      MIDI.sendNoteOff(lastNote[patternNum][position], 0, midiChannel);
      lastNote[patternNum][position] = 0;
      analogWrite(CVPITCH_PIN, 0);
      digitalWrite(CVGATE_PIN, LOW);
}



// FIGURE OUT WHAT TO DO WITH CLOCK FOR NOW ???

// ####### CLOCK/TIMING #######

void sendClock(){
	usbMIDI.sendRealTime(usbMIDI.Clock);
//	MIDI.sendClock();
}
void startClock(){
	usbMIDI.sendRealTime(usbMIDI.Start);
//	MIDI.sendStart();
}
void stopClock(){
	usbMIDI.sendRealTime(usbMIDI.Stop);
//	MIDI.sendStop();
}
void resetClocks(){
	// BPM tempo to step-delay calculation
	step_delay = 60000 / clockbpm / 4; // 16th notes
	// BPM to clock pulses
	clksDelay = (60000000 / clockbpm) / 24;
}



// ####### DISPLAY FUNCTIONS #######


void dispPatt(){
//	display.setTextSize(1);
//	display.setCursor(0, 0);
//	display.print("PTN: ");
//	display.setCursor(32, 0);
//	display.print(playingPattern+1);
		display.setCursor(0, 0);
		display.setTextSize(1);
		display.print("PTN");		
		display.setCursor(30, 0);
		display.setTextSize(2);
		display.print(playingPattern+1);
}

void dispTempo(){
//	display.setTextSize(1);
//	display.setCursor(0, 24);
//	display.print("BPM:");
//	display.setCursor(32, 24);
//	display.print((int)clockbpm);
		display.setCursor(65, 19);
		display.setTextSize(1);
		display.print("BPM");		
		display.setCursor(92, 18);
		display.setTextSize(2);
		display.print((int)clockbpm);
}

void dispPots(){
	display.setTextSize(1);
	display.setCursor(0, 0);
	display.print("CC");
	display.print(potCC);
	display.print(": ");
	display.setCursor(30, 0);
	display.print(potVal);
}

void dispOctave(){
	display.setTextSize(1);
	display.setCursor(0, 24);
	display.print("Octave:");
	display.print((int)octave+4);
}

void dispNotes(){
	display.setTextSize(1);
	display.setCursor(0, 12);
	display.print("NOTE:");
	display.print(lastNote[playingPattern][seqPos[playingPattern]]);
}

void dispNoteSelect(){
	if (!noteSelection){

		display.setCursor(0, 0);
		display.setTextSize(1);
		display.print("PTN");		
		display.setCursor(30, 0);
		display.setTextSize(2);
		display.print(playingPattern+1);

		display.fillRect(66, 0, 58, 32, WHITE);
		display.setTextColor(INVERSE);
		display.setTextSize(4);
		display.setCursor(74, 1);
		display.print("NS");
	}else{
		int tempOffset = 27;
		switch(nsmode){
			case 0:
				display.fillRect(-2, 0, 22, 12, WHITE);
				display.setTextColor(INVERSE);
				//display.drawRect(0, 0, 24, 12, WHITE);
				break;
			case 1: 
				display.fillRect(tempOffset, 0, 22, 12, WHITE);
				display.setTextColor(INVERSE);
				break;
			case 2: 
				display.fillRect(tempOffset*2, 0, 22, 12, WHITE);
				display.setTextColor(INVERSE);
				break;
			case 3: 
				display.fillRect(tempOffset*3, 0, 22, 12, WHITE);
				display.setTextColor(INVERSE);
				//display.drawRect(96, 0, 22, 12, WHITE);
				break;
			case 4: 
				display.fillRect(tempOffset, 16, 38, 16, WHITE);
				display.setTextColor(INVERSE);
				break;
			case 5: 
				display.fillRect(90, 16, 38, 16, WHITE);
				display.setTextColor(INVERSE);
				break;
			case 6: 
				display.fillRect(tempOffset*4, 0, 20, 12, WHITE);
				display.setTextColor(INVERSE);
				break;
		}

		display.setCursor(2, 2);
		for (int j=0; j<4; j++){
			display.setTextSize(1);
			display.setCursor(j*tempOffset, 2);
			if (stepNoteP[playingPattern][selectedStep][j+3] > 0){
				display.print(stepNoteP[playingPattern][selectedStep][j+3]);
			} else {
				display.print("---");
			}
			if (j != 3){
				display.setCursor(j*tempOffset+19, 2);
				display.print("/");
			}
		}

		display.setTextSize(1);
		display.setCursor(tempOffset*4+2, 1);
		display.print("NS");
		display.print(noteSelectPage+1);
		
		
		display.setCursor(1, 19);
		display.setTextSize(1);
		display.print("NOTE");		
		display.setCursor(29, 18);
		display.setTextSize(2);
		display.print(stepNoteP[playingPattern][selectedStep][0]);

		display.setCursor(65, 19);
		display.setTextSize(1);
		display.print("VEL");		
		display.setCursor(92, 18);
		display.setTextSize(2);
		display.print(stepNoteP[playingPattern][selectedStep][1]);
	}
}

void dispPattLen(){
		display.setCursor(1, 19);
		display.setTextSize(1);
		display.print("LEN");	
		display.setCursor(29, 18);
		display.setTextSize(2);
		display.print(pattLen[playingPattern]);
}
void dispPattStrt(){
		display.setCursor(1, 19);
		display.setTextSize(1);
		display.print("SRT");	
		display.setCursor(29, 18);
		display.setTextSize(2);
		display.print(pattLen[playingPattern]);
}

void dispPatternParams(){
	if (patternParams){

		int tempOffset = 27;
		switch(ptmode){
			case 0:  // LEN
				display.fillRect(tempOffset, 16, 38, 16, WHITE);
				display.setTextColor(INVERSE);
				break;
			case 1: 	// ROTATE
				display.fillRect(63, 16, 28, 16, WHITE);
				display.setTextColor(INVERSE);
				break;
			case 2: 
				display.fillRect(100, 0, 28, 16, WHITE);
				display.setTextColor(INVERSE);
				break;
		}

		display.setCursor(0, 0);
		display.setTextSize(1);
		display.print("PTN");		
		display.setCursor(30, 0);
		display.setTextSize(2);
		display.print(playingPattern+1);

		display.setCursor(1, 19);
		display.setTextSize(1);
		display.print("LEN");		
		display.setCursor(29, 18);
		display.setTextSize(2);
		display.print(pattLen[playingPattern]);

		display.setCursor(65, 19);
		display.setTextSize(1);
		display.print("ROT");		
		display.setCursor(65, 18);
		display.setTextSize(2);

//		display.fillRect(100, 0, 20, 16, WHITE);
//		display.setTextColor(INVERSE);
		display.setTextSize(2);
		display.setCursor(102, 1);
		display.print("PT");
	}else{

	}
}

void dispMode(){
	display.setTextSize(4);
	display.setCursor(74, 0);

	if (newmode != mode && enc_edit) {
		display.print(modes[newmode]);
	} else if (enc_edit) {
		display.print(modes[mode]);
	}
}


// ############## MAIN LOOP ##############

void loop() {
	customKeypad.tick();
	checktime1 = 0;
	
//	resetClocks();
	if (clksTimer > clksDelay ) {
		// SEND CLOCK
	  clksTimer = 0;
	}


	// DISPLAY SETUP
	display.clearDisplay();
			
				
	// ############### POTS ###############
	//
	readPotentimeters();
	

	// ############### ENCODER ###############
	// 
	auto u = myEncoder.update();
	if (u.active()) {
    	auto amt = u.accel(5); // where 5 is the acceleration factor if you want it, 0 if you don't)
//    	Serial.println(u.dir() < 0 ? "ccw " : "cw ");
//    	Serial.println(amt);
    	
		// Change Mode
    	if (enc_edit) {
			// set mode
			int modesize = numModes-1;
//			Serial.println(modesize);
	    	newmode = constrain(newmode + amt, 0, modesize);
	    	dispMode();
	    	dirtyDisplay = true;

		} else if (!noteSelect && !patternParams){  
			switch(mode) { 
				case 3: // Organelle Mother
					if(u.dir() < 0){									// if turn ccw
						usbMIDI.sendControlChange(CC_OM2,0,midiChannel);
					} else if (u.dir() > 0){							// if turn cw
						usbMIDI.sendControlChange(CC_OM2,127,midiChannel);
					}      
					break;
				case 0: // MIDI
					// set octave 
					newoctave = constrain(octave + amt, -5, 4);
					if (newoctave != octave){
						octave = newoctave;
						dirtyDisplay = true;
					}
				case 1: // SEQ 1
					newtempo = constrain(clockbpm + amt, 40, 300);
					if (newtempo != clockbpm){
						// SET TEMPO HERE
						clockbpm = newtempo;
						resetClocks();
						dirtyDisplay = true;
					}
					break;
				case 2: // SEQ 2
					newtempo = constrain(clockbpm + amt, 40, 300);
					if (newtempo != clockbpm){
						// SET TEMPO HERE
						clockbpm = newtempo;
						resetClocks();
						dirtyDisplay = true;
					}
					break;
			}

		} else if (noteSelect || patternParams) {  
			switch(mode) { // process encoder input depending on mode
				case 0: // MIDI
					break;
				case 1: // SEQ 1
					if (patternParams && !enc_edit){ // sequence edit mode
						//
						if (ptmode == 0) { // set length
							pattLen[playingPattern] = constrain(patternLength[playingPattern] + amt, 1, 16);
							patternLength[playingPattern] = pattLen[playingPattern];
						}	
						if (ptmode == 1) { // set rotation						
							//int N = sizeof(stepNoteP[playingPattern]) / sizeof(int);
							rotatePattern(stepPlay[playingPattern], patternLength[playingPattern], constrain(amt, -patternLength[playingPattern]-1, patternLength[playingPattern]-1));
						}	
						
						dirtyDisplay = true;
					} else if (noteSelect && noteSelection && !enc_edit){
						// {notenum,vel,len,p1,p2,p3,p4,p5}
						if (nsmode >= 0 && nsmode < 4){
							if(u.dir() < 0){			// reset plock if turn ccw
								stepNoteP[playingPattern][selectedStep][nsmode+3] = -1;
								dirtyDisplay = true;
							}
						}
						if (nsmode == 4) { // set note
							int tempNote = stepNoteP[playingPattern][selectedStep][0];
							stepNoteP[playingPattern][selectedStep][0] = constrain(tempNote + amt, 0, 127);
							dirtyDisplay = true;
						}	
						if (nsmode == 5) { // set velocity
							int tempVel = stepNoteP[playingPattern][selectedStep][1];
							stepNoteP[playingPattern][selectedStep][1] = constrain(tempVel + amt, 0, 127);
							dirtyDisplay = true;
						}	
						if (nsmode == 6) { // change page

						}	
//						Serial.println("NS");

					} else {
						newtempo = constrain(clockbpm + amt, 40, 300);
						if (newtempo != clockbpm){
							// SET TEMPO HERE
							clockbpm = newtempo;
							resetClocks();
							dirtyDisplay = true;
						}
					}
					break;
				case 2: // SEQ 2
					if (noteSelect && !enc_edit){ // sequence edit mode
						// 
						pattLen[playingPattern] = constrain(patternLength[playingPattern] + amt, 1, 16);
						patternLength[playingPattern] = pattLen[playingPattern];
						dirtyDisplay = true;
					} else {
						newtempo = constrain(clockbpm + amt, 40, 300);
						if (newtempo != clockbpm){
							// SET TEMPO HERE
							clockbpm = newtempo;
							resetClocks();
							dirtyDisplay = true;
						}
					}		
					break;
				case 3: // Organelle Mother
					break;
			}		

		}
	}
	
	// ############### ENCODER BUTTON ###############
	//
	auto s = encButton.update();
	switch (s) {
		case Button::Down: //Serial.println("Button down"); 
			// what page are we on?
			if (newmode != mode && enc_edit) {
				mode = newmode;
				playing = 0;
				setAllLEDS(0,0,0);
				enc_edit = false;
				dispMode();
				dirtyDisplay = true;
			} else if (enc_edit){
				enc_edit = false;
				dirtyDisplay = true;
			}

			if(mode == 3) {
				usbMIDI.sendControlChange(CC_OM1,100,midiChannel);					
			}
			if(mode == 1) {
				if (noteSelect && noteSelection) {
					// increment nsmode
					nsmode = (nsmode + 1 ) % 7;
					Serial.println(nsmode);
					dirtyDisplay = true;
				}
				if (patternParams) {
					// increment ptmode
					ptmode = (ptmode + 1 ) % 3;
					Serial.println(ptmode);
					dirtyDisplay = true;
				}
			}
			break;
		case Button::DownLong: //Serial.println("Button downlong"); 
			enc_edit = true;			
			dispMode();
			dirtyDisplay = true;
			break;
		case Button::Up: //Serial.println("Button up"); 
			if(mode == 3) {
				usbMIDI.sendControlChange(CC_OM1,0,midiChannel);						
			}
			break;
		case Button::UpLong: //Serial.println("Button uplong"); 
			break;
		default:
			break;		
	}
				

	// ############### KEY HANDLING ###############
	
	while(customKeypad.available()){
		keypadEvent e = customKeypad.read();
		int thisKey = e.bit.KEY;
		int keyPos = thisKey - 11;

		if (e.bit.EVENT == KEY_JUST_PRESSED){
			keyState[thisKey] = true;
		}
		
		switch(mode) {
			case 3: // Organelle
				// Fall Through		
			case 0: // MIDI CONTROLLER
		
				// ### KEY PRESS EVENTS
				if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey != 0) {
					//Serial.println(" pressed");
					noteOn(thisKey, noteon_velocity, playingPattern);
					
				} else if(e.bit.EVENT == KEY_JUST_RELEASED && thisKey != 0) {
					//Serial.println(" released");
					noteOff(thisKey);
				}
				
				// AUX KEY
				if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey == 0) {

					// Hard coded Organelle stuff
					usbMIDI.sendControlChange(CC_AUX, 100, midiChannel);
					MIDI.sendControlChange(CC_AUX, 100, midiChannel);
					if (midiAUX) {
						// STOP CLOCK
					} else {
						// START CLOCK
					}
					midiAUX = !midiAUX;
					
				} else if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey == 0) { 
					// Hard coded Organelle stuff
					usbMIDI.sendControlChange(CC_AUX, 0, midiChannel);
					MIDI.sendControlChange(CC_AUX, 0, midiChannel);
//					midiAUX = false;
				}					
				break;

			case 1: // SEQUENCER 1
				// fall through
			case 2: // SEQUENCER 2
				// Sequencer row keys
					
				// ### KEY PRESS EVENTS
				
				if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey != 0) {
					// set key timer to zero
					keyPressTime[thisKey] = 0;
					
					// are we noteSelect ?
					if (noteSelect){
						if (noteSelection) {		// set note
							stepSelect = false;
							selectedNote = thisKey;
							stepNoteP[playingPattern][selectedStep][0] = notes[thisKey];
							if (!playing){
								noteOn(thisKey, noteon_velocity, playingPattern);
							}
							// see RELEASE events for more
							dirtyDisplay = true;
														
						} else if (thisKey == 1) { 
//							noteSelect = !noteSelect; // toggle noteSelect on/off
//							patternParams = false;
//							dirtyDisplay = true;

						} else if (thisKey == 2) { 

						} else if (thisKey > 2 && thisKey < 11) { // Pattern select keys
							playingPattern = thisKey-3;
							dirtyDisplay = true;

						} else if ( thisKey > 10 ) {
							selectedStep = keyPos; // set noteSelection to this step
							stepSelect = true;
							noteSelection = true;
							dirtyDisplay = true;							
						}
						
					// are we patternParams ?
					} else if (patternParams){

						if (thisKey == 1) { 

						} else if (thisKey == 2) { 
//							patternParams = !patternParams; // toggle patternParams on/off
//							noteSelect = false;
//							dirtyDisplay = true;
						} else if (thisKey > 2 && thisKey < 11) { // Pattern select keys
							playingPattern = thisKey-3;
							dirtyDisplay = true;
						} else if ( thisKey > 10 ) {

						}
					

					} else {					
						if (thisKey == 1) {		
//							if (noteSelection){					// Note Select
//								noteSelection = !noteSelection; // toggle noteSelection on/off
//							}else{
//								noteSelect = !noteSelect; 		// toggle noteSelect on/off
//							}
//							dirtyDisplay = true;
						
						} else if (thisKey == 2) { 			
							seqReset(); 					// reset all sequences to step 1
															// patternParams
//							patternParams = !patternParams; // toggle patternParams on/off
//							dirtyDisplay = true;

						// BLACK KEYS
						} else if (thisKey > 2 && thisKey < 11) { // Pattern select
						
							if (keyState[2]){ 		// If KEY 2 is down + pattern = mute
//								Serial.print("mute ");
//								Serial.println(thisKey);
//								Serial.println(patternMute[thisKey]);
								patternMute[thisKey-3] = !patternMute[thisKey-3];
							} else {
								playingPattern = thisKey-3;
								dirtyDisplay = true;
							}
							
						} else if (thisKey > 10) { // SEQUENCE 1-16 KEYS
							if (stepPlay[playingPattern][keyPos] == 1){ // toggle note on
								stepPlay[playingPattern][keyPos] = 0;
							} else if (stepPlay[playingPattern][keyPos] == 0){ // toggle note off
								stepPlay[playingPattern][keyPos] = 1;
								//stepNote[playingPattern][keyPos] = notes[thisKey];
							}
						}
					}
				}
				
				// ### KEY RELEASE EVENTS
				
				if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey != 0) {
					
				}
				
				if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey != 0 && noteSelection && selectedNote > 0) {
					if (!playing){
						noteOff(thisKey);
					}
//					noteSelection = false;
//					noteSelect = false;
//					selectedStep = 0;
//					selectedNote = 0;
				}

				// AUX KEY PRESS EVENTS
				
				if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey == 0) {
//				Serial.print("noteSelect:");
//				Serial.println(noteSelect);
//				Serial.print("noteSelection:");
//				Serial.println(noteSelection);
					
					if (noteSelect){
						if (noteSelection){
							selectedStep = 0;
							selectedNote = 0;
							
						} else {
							
						}
						noteSelection = false;
						noteSelect = !noteSelect;
						dirtyDisplay = true;

					} else if (patternParams){
						patternParams = !patternParams;
						dirtyDisplay = true;

					} else {
						if (playing){
							playing = 0;
							allNotesOff();
						} else {
							playing = 1;
						}
					}

				// AUX KEY RELEASE EVENTS

				} else if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey == 0) {
				
				}

//				strip.show();
				break;
		} // END MODE SWITCH

		if (e.bit.EVENT == KEY_JUST_RELEASED){
			keyState[thisKey] = false;
			keyPressTime[thisKey] = 0;
		}

	} // END KEYS WHILE
	
	
	// ### LONG KEY PRESS
	for (int j=0; j<LED_COUNT; j++){
		if (keyState[j]){
			if (keyPressTime[j] >= longPressInterval && keyPressTime[j] < 9999){

				// DO LONG PRESS THINGS
				switch (mode){
					case 0:
						break;
					case 1:
						// fall through
					case 2:
						if (j > 2 && j < 11){ // skip AUX key, get pattern keys
							patternParams = true;
							dirtyDisplay = true;
						
						} else if (j > 10){
							selectedStep = j - 11; // set noteSelection to this step
							noteSelect = true;
							stepSelect = true;
							noteSelection = true;
							dirtyDisplay = true;
						}
						break;
						
				}
//				Serial.print("long press:");
//				Serial.println(j);

				keyPressTime[j] = 9999;	
			}
		}
	}
	


	// ############### MODES LOGIC ##############

	switch(mode){
		case 3: 						// ############## ORGANELLE MODE
			// Fall through

		case 0:							// ############## MIDI KEYBOARD
			midi_leds();				// SHOW LEDS

			if (dirtyDisplay){			// DISPLAY
				dispPots();
				//dispTempo();
				dispNotes();
				dispOctave();
			}
			break;

		case 1: 						// ############## SEQUENCER 1
			if (dirtyDisplay){			// DISPLAY
				if (!enc_edit){
					if (!noteSelect and !patternParams){
						dispPatt();
						dispPattLen();
						dispTempo();
					}				
					if (noteSelect) {
						dispNoteSelect();
					}
					if (patternParams) {
						dispPatternParams();
					}
				}
			}
			if(playing == true) {
				// step timing
				if(step_interval[playingPattern] >= step_delay){
					
					// Do stuff
					
					// turn previous note off
					int lastPos = (seqPos[playingPattern]+15) % 16;
//					Serial.println(lastPos);
					if (lastNote[playingPattern][lastPos] > 0){
						step_off(playingPattern, lastPos);
					}
					lastStepTime[playingPattern] = step_interval[playingPattern];
					
					step_on(playingPattern);
					playNote(playingPattern);
					show_current_step(playingPattern);
					step_ahead(playingPattern);
					step_interval[playingPattern] = 0;
				}
			} else {
				show_current_step(playingPattern);
			}
			if (playing) {
				for (int z=0; z<16; z++){
				}
			}
			break;

		case 2: 						// ############## SEQUENCER 2
			if (dirtyDisplay){			// DISPLAY
				if (noteSelect) {
					dispNoteSelect();
				} else {
					dispPattLen();
					dispTempo();		
				}
			}
			if(playing == true) {
				for (int j=0; j<8; j++){
//					Serial.print("pattern:");
//					Serial.println(j);
					
					if (!patternMute[j]) {
					
						if(step_interval[j] >= step_delay){
							int lastPos = (seqPos[j]+15) % 16;
							if (lastNote[j][lastPos] > 0){
								step_off(j, lastPos);
							}
							lastStepTime[j] = step_interval[j];
							step_on(j);
							playNote(j);
							show_current_step(playingPattern);
							step_ahead(j);
							step_interval[j] = 0;
	//					} else if(step_interval[playingPattern] >= step_delay / 2){
	//						step_off(j, lastPos);
	//						show_current_step(playingPattern);
						}
					}
				}				
			} else {
				show_current_step(playingPattern);
			}
			break;
	}

	//	Serial.print("one:");
	//	Serial.println(checktime1);

	// DISPLAY at end of loop

	if (dirtyDisplay){
		display.display();
		dirtyDisplay = false;
	}
	
//	Serial.print("two:");
//	Serial.println(checktime1);

	// are pixels dirty
	if (dirtyPixels){
		strip.show();	
		dirtyPixels = false;
	}

	while (usbMIDI.read()) {
		// ignore incoming messages
	}
	while (MIDI.read()) {
		// ignore incoming messages
	}
	
} // ######## END MAIN LOOP ########


void seqReset(){
	for (int k=0; k<8; k++){
		seqPos[k] = 0;
	}
}

// #### MIDI Mode note on/off
void noteOn(int notenum, int velocity, int patternNum){
	int adjnote = notes[notenum] + (octave * 12); // adjust key for octave range
	if (adjnote>=0 && adjnote <128){
		usbMIDI.sendNoteOn(adjnote, velocity, midiChannel);
		lastNote[patternNum][seqPos[patternNum]] = adjnote;
		MIDI.sendNoteOn(adjnote, velocity, midiChannel);	
		// CV
		pitchCV = map (adjnote, 35, 90, 0, 4096);
		digitalWrite(CVGATE_PIN, HIGH);
		analogWrite(CVPITCH_PIN, pitchCV);
	}

	strip.setPixelColor(notenum, MIDINOTEON);         //  Set pixel's color (in RAM)
	dirtyPixels = true;	
	dirtyDisplay = true;
}
void noteOff(int notenum){
	int adjnote = notes[notenum] + (octave * 12); // adjust key for octave range
	if (adjnote>=0 && adjnote <128){
		usbMIDI.sendNoteOff(adjnote, 0, midiChannel);
		MIDI.sendNoteOff(adjnote, 0, midiChannel);
		// CV
		digitalWrite(CVGATE_PIN, LOW);
		analogWrite(CVPITCH_PIN, 0);
	}
	
	strip.setPixelColor(notenum, LEDOFF); 
	dirtyPixels = true;
	dirtyDisplay = true;
}

void rotatePattern(int a[], int size, int rot ){
	int arr[size];	
	//rot = rot % size;
	rot = (rot + size) % size;
	
	for (int d = rot, s = 0; s < size; d = (d+1) % size, ++s)
		arr[d] = a[s];
	for (int i = 0; i < size; ++i)
		a[i] = arr[i];
    
//  	
//	// add last rot elements
//	for (int i = 0; i < rot; i++){
//		arr[i] = a[size - rot + i];
//	}
//	// add remaining elements
//	for (int i = rot; i < size; i++){
//		arr[i] = a[i - rot];
//	}
//	for (int i = 0; i < size; i++){
//        a[i] = arr[i];
//   }
}

// #### LED STUFF
// Rainbow cycle along whole strip. Pass delay time (in ms) between frames.
void rainbow(int wait) {
  // Hue of first pixel runs 5 complete loops through the color wheel.
  // Color wheel has a range of 65536 but it's OK if we roll over, so
  // just count from 0 to 5*65536. Adding 256 to firstPixelHue each time
  // means we'll make 5*65536/256 = 1280 passes through this outer loop:
  for(long firstPixelHue = 0; firstPixelHue < 1*65536; firstPixelHue += 256) {
    for(int i=0; i<strip.numPixels(); i++) { // For each pixel in strip...
      // Offset pixel hue by an amount to make one full revolution of the
      // color wheel (range of 65536) along the length of the strip
      // (strip.numPixels() steps):
      int pixelHue = firstPixelHue + (i * 65536L / strip.numPixels());

      // strip.ColorHSV() can take 1 or 3 arguments: a hue (0 to 65535) or
      // optionally add saturation and value (brightness) (each 0 to 255).
      // Here we're using just the single-argument hue variant. The result
      // is passed through strip.gamma32() to provide 'truer' colors
      // before assigning to each pixel:
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue)));
    }
    strip.show(); // Update strip with new contents
    delay(wait);  // Pause for a moment
  }
}
void setAllLEDS(int R, int G, int B) {
	for(int i=0; i<LED_COUNT; i++) { // For each pixel...
		strip.setPixelColor(i, strip.Color(R, G, B));
	}
	dirtyPixels = true;
}

// #### OLED STUFF
void testdrawrect(void) {
  display.clearDisplay();

  for(int16_t i=0; i<display.height()/2; i+=2) {
    display.drawRect(i, i, display.width()-2*i, display.height()-2*i, SSD1306_WHITE);
    display.display(); // Update screen with each newly-drawn rectangle
    delay(1);
  }

  delay(500);
}