# OMX-27

Mechanical key switch midi keyboard and sequencer. Based on Teensy 3.2 and Cherry MX RGB key switches.

### Arduino Requirements

In Teensyduino Library Manager - check to be sure these are installed and on the most recent versions.  

__Libraries:__  
Adafruit_Keypad  
Adafruit_NeoPixel  
Adafruit_SSD1306  
Adafruit_GFX_Library  
U8g2_for_Adafruit_GFX
Adafruit_FRAM_I2C

Also check to be sure MIDI Library (by Francois Best / fortyseveneffects) is updated to 5.02  
I believe this is installed by default with Teensyduino 

Set the following for the Teensy under the Tools menu:  

__Board:  Teensy 3.2/3.1__  
__USB Type: MIDI__  
__CPU Speed: 120 MHz (overclock)__
  

### BOM

[Bill of Materials](<build/BOM.md>)

### Build

[Build Guide](<build/Build-Kit.md>)

### Docs

[Documentation](<Docs.md>)

# FAQ

Q: What key switches are recommended?  
A: Any RGB switches with a Cherry MX footprint can be used - I'm using Cherry MX RGB and these are linked in the [BOM](<BOM.md>). Different varieties are available (Red, Brown, etc.)  

Q: Can I use other key switches?  
A: Yes - as long as they have the same footprint as Cherry MX switches and a window/opening for the LED to shine through. Low profile keys like the Cherry Low Profile or Kailh Choc switches have a different footprint and will not work.  

Q: What about recommended Keycaps?  
A: Also listed in the [BOM](<BOM.md>). You want an MX stem cap, with translucency or a window for the LED to shine through. DSA profile caps work well.  

Q: Does this project require soldering?  
A: Yes. Thru-hole soldering is required along with some easy SMD (LEDs and jacks).  

Q: What's with these LEDs?  
A: This project uses SK6812-MINI-E reverse mount LEDs. They are somewhat hard to find, so I'll try to offer them included with kits. They are easy to solder, even if you've not done much SMD.  

Q: Can I get the Gerbers or order the pcbs myself?  
A: No. Not sure about open sourcing yet.  

Q: Can I get some of those windowed keycaps you're using?  
A: Yes. 
