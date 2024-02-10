```
// Memory
// Clean slate no MidiFX
```
==== memory report ====
free: 29 Kb (45.9%)
stack: 1 Kb (1.6%)
heap: 2 Kb (2.9%)
``

// 25 Arps
```
==== memory report ====
free: 17 Kb (26.7%)
stack: 2 Kb (3.1%)
heap: 13 Kb (20.7%)
```

// 5 Arps
==== memory report ====
free: 26 Kb (40.3%)
stack: 2 Kb (3.1%)
heap: 13 Kb (20.7%)

```
==== memory report ====
free: 27 Kb (42.3%)
stack: 2 Kb (3.1%)
heap: 13 Kb (20.7%)
```


Bugs:
	Endus: "@Quixotic7 So, had some free time for a couple of days, noticed a bug with the saves in 'Chord Mode'.

Selected 'Save Slot 1', used the first 4 keys from the left for 'Custom Chords', then saved in 'Save Slot 1'.  Then loaded 'Save Slot 2' to start from scratch, created 4 new 'Custom Chords', again, using the first 4 keys from the left.  Then saved in 'Save Slot 2'.

Now, when I load 'Save Slot 1', the root note, and key octave is how I saved it, but the '+' and '-' notes of the custom chords are how I set them in 'Save Slot 2'.  Never noticed this before, 'cause with each FW update, saves were erased.
Also, changes I make to the custom chord '+ -' notes in 'Save Slot 1', will show in 'Save Slot 2'."

"In 'Save Slot 1' all 4 keys each use a custom chord, with added custom notes.

For 'Save Slot 2', the first key is a 'Fifth' chord, the second and third keys are 'Forths' chords, and the forth key is a 'Custom Chord' with custom notes.

When I edit the 'Custom Notes' in the 'Custom Chord' on the forth key on 'Save Slot 2', I'll see (in the custom chord note param), and hear those changes when I load 'Save Slot 1', and press on the forth key."




Arps don't work in MidiModeception

What's next:

- Chord Split Mode
- Midi Channels For Chords
- Hold Chord Turn set chord
- Chord Keyboard Display
- Default chords to something playable. 


Adafruit DMA neopixel 1.3.0
Adafruit NeoPixel 1.10.5

/Applications/Teensyduino.app/Contents/Java/hardware/tools/arm/bin/../lib/gcc/arm-none-eabi/5.4.1/../../../../arm-none-eabi/lib/armv7e-m/libc_nano.a(lib_a-signalr.o): In function `_kill_r':
Multiple libraries were found for "Adafruit_NeoPixel.h"
signalr.c:(.text._kill_r+0xe): undefined reference to `_kill'
 Used: /Users/quixotic7mini/Documents/Arduino/libraries/Adafruit_NeoPixel
/Applications/Teensyduino.app/Contents/Java/hardware/tools/arm/bin/../lib/gcc/arm-none-eabi/5.4.1/../../../../arm-none-eabi/lib/armv7e-m/libc_nano.a(lib_a-signalr.o): In function `_getpid_r':
 Not used: /Applications/Teensyduino.app/Contents/Java/hardware/teensy/avr/libraries/Adafruit_NeoPixel
Multiple libraries were found for "MIDI.h"
 Used: /Applications/Teensyduino.app/Contents/Java/hardware/teensy/avr/libraries/MIDI
signalr.c:(.text._getpid_r+0x0): undefined reference to `_getpid'
 Not used: /Users/quixotic7mini/Documents/Arduino/libraries/MIDI_Library
Multiple libraries were found for "Adafruit_SSD1306.h"
 Used: /Users/quixotic7mini/Documents/Arduino/libraries/Adafruit_SSD1306
collect2: error: ld returned 1 exit status
 Not used: /Users/quixotic7mini/Documents/Arduino/libraries/Adafruit_SSD1306_Wemos_Mini_OLED
 

Ideas:
Aux + Encoder to quickly switch modes. Modular setup in which You can combine sequencers, keyboards etc. 


"Was messing around with the Grids mode, and thought it would be nice if the four buttons that blink showing the different 'hits' could also act as mutes."

- DONE - Move MidiFX easily rather than cut/paste

- DONE -Quickkeys for arps. 

- DONE - Way to adjust midifx and play keyboard at same time. 

DONE - "feedback dialogs when selecting FXgroup or FX-off might be nice"

- Save / recall arp settings. 







- S1/S2 - Active step mode
    This would add an additional view in which steps can be turned on or off. 
    Steps that are off get ignored as if they did not exist. 
    Allows for fun performance manipulation of sequence without changing it. 

- S1/S2 pattern gate feature

- DONE - Arpegiator



- Add MidiFX to Seq, Chord, and Grids Modes

- Chord LED feedback

- Auto strum - Does Arp MidiFX Accomplish this? Maybe a arp submode? 

- Earthsea sequencer, add into midi and chord modes, simple records what you play then plays back in natural time or quantized. 

- Pot CC Presets
    These would be templates for quickly setting cc's to work with a specific synth like a mm2 or digitone. 

    Might be easier to just have multiple banks of pot presets. 

    A key shortcut for changing banks would be nice. 


- Try and make UI more consistent between modes

- Show something on screen to indicate you're in a submode and can press aux to exit. 

- Save single mode to sysex and load from sysex

- Add midi channels to chords



- Full manual chord note input






Waldorf Arp notes


The arpeggiator uses a so-called note list that can store up to 16 notes. 

Sort Order is set to Num Lo>Hi, the list is rearranged so that the lowest note is placed at the first position, the second lowest note at the next


Mode
	off
	on
	one shot
	hold
	
Step Len
	if Length is set to legato, all arpeggio notes are played without pauses between each step and Arp Steplen therefore has no effect.
	
	
Range
	octaves
	
Patterns


x-xxx-xxx-xxx-xx
x-x-x--xx-x-x--x
x-x-x-xxx-x-x-xx
x-xxx-x-x-xxx-x-
x-x-xx-xx-x-xx-x
xx-x-xx-xx-x-xx-
x-x-x-x-xx-x-x-x
x-x-x-xx-x-xx-x-
xxx-xxx-xxx-xxx-
xx-xx-xx-xx-xxx-
xx-xx-xx-xx-x-x-
xx-xx-x-xx-xx-x-
x-x-x-x-xx-x-xxx
x--x--x--x--x--x
x-x-x-x-x--xx-x-

Max Notes

Step length

Direction

	up
	down
	alt up
	alt down
	
Sort Order
	as played 
	reversed 
	Num Lo>Hi 
	Num Hi>Lo 
	Vel Lo>Hi 
	Vel Hi>Lo
	
Velocity
	randomize like grids?
	
Swing?

Same Note Overlap

Pattern Reset
	With Pattern Reset, you can decide if the note list is also restarted from the beginning when the rhythm pattern is reset.
	
	If Off is selected, the note list is not restarted, so that there is no synchronization between rhythm and note list. E.g., when you have a pattern where four steps are set and you play three notes, the pattern and the note list are repeated differently.
	
	If On is selected, the note list will be restarted as soon as the rhythm pattern is restarted.
	

Arpeggiator Edit Menu Step Data
	
Arp Accent 

Arp Glide

Arp Step

	• If * is selected (asterisk symbol), the Arpeggiator plays the step unaltered. The note list is advanced beforehand, except when you press a new chord.

	• If `off` is selected (empty space), the Arpeggiator plays nothing at this step position. When Length or Steplen is set to legato, the previous step that isn’t set to Off is still held to create the legato effect. The note list is not advanced.

	• If - is selected, the Arpeggiator plays the same note as it had to play in the previous step that was set to * or ˆ. With this setting, you can repeat a particular note of the note list several times. The note list is not advanced.

	• If < is selected, the Arpeggiator plays the very first note of the note list. This might be interesting if you want to only play the "root note" of a chord in a bass sound. The note list is not advanced.

	• If > is selected, the Arpeggiator plays the very last note of the note list. The note list is not advanced.

	• If <> is selected, the Arpeggiator plays a chord with two notes, the first and the last one of the note list. This means that you have to play at least two notes to hear the effect. Otherwise, you would hear only one note anyway. The note list is not advanced.

	• If (notes) is selected (notes symbol), the Arpeggiator plays a chord with all notes from the note list. This means that you have to play at least two notes to hear the effect. The note list is not advanced.

	• If ? is selected, the Arpeggiator plays a random note from the note list. This doesn’t mean that it creates any random note, it only uses one note of the note list at will. The note list is not advanced.


Humanize Chord feature

	Enduss - "A few days ago I had an idea for 'Chord Mode', a way to make chord playing have an organic feel to it.  It's basically an option to have each note, after the root note, to have their own 'Delayed Note', and/or 'Chance of Delayed Note' setting, like as if you were rolling the notes on the keys (with the right hand) from thumb, to pinky.  The settings could use those 'oblong characters' you created (or different characters) to adjust the delay for each note, and also a setting for 'Chance of Delay' if a user doesn't always want the delay to happen, and just wants all note in the chord to play at once."





