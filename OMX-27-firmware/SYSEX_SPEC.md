# OMX Sysex spec

The OMX-27 interfaces with its editor via MIDI Sysex. This document describes the supported messages.

_Work in progress, porting from 16n faderbank editor_

## `0x1F` - "1nFo"

Request for OMX-27 to transmit current state via sysex. No other payload.

## `0x0F` - "c0nFig"

"Here is my current config." Only sent by OMX-27 as an outbound message, in response to `0x1F`. Payload of 32 bytes, describing current EEPROM state.

## `0x0E` - "c0nfig Edit"

~~"Here is a new complete configuration for you". Payload (other than mfg header, top/tail, etc) of 80 bytes to go straight into EEPROM, according to the memory map described in `README.md`.~~ not implemented

## `0x0D` - "c0nfig edit (Device options)"

"Here is a new set of device options for you". Payload (other than mfg header, top/tail, etc) of 32 bytes to go straight into appropriate locations of EEPROM, according to the following map:
```
	//  64 bytes of data:
	//  0 - EEPROM VERSION
	//  1 - Current MODE
	//  2 - Sequencer PlayingPattern
	//  3 - MIDI mode MidiChannel 
	//  4 - 28 - Pots (x25 - 5 banks of 5 pots)
	//  29 - MIDI Macro Channel
	//  30 - MIDI Macro Type
	//  31 - Scale Root
	//  32 - Scale Pattern, -1 for chromatic
	//  33 - Lock Scale - Bool
	//  34 - Scale Group 16 - Bool
	//  35 - midiSettings.defaultVelocity
	//  36 - clockConfig.globalQuantizeStepIndex
	//  37 - cvNoteUtil.triggerMode
	// 	38 - actvie pot bank
	
	//  XX - 63 - Not yet used

```
Example: 
`F0 7D 00 00 0D 09 00 00 00 15 16 17 18 07 1D 1E 1F 20 21 22 23 24 25 26 27 28 29 2A 2B 5B 5D 67 68 69 00 00 00 F7`

## `0x0C` - "c0nfig edit (usb options)"

~~"Here is a new set of USB options for you". Payload (other than mfg header, top/tail, etc) of 32 bytes to go straight into appropriate locations of EEPROM, according to the memory map described in `README.md`.~~ not implemented

## `0x0B` - "c0nfig edit (trs options)"

~~"Here is a new set of TRS options for you". Payload (other than mfg header, top/tail, etc) of 32 bytes to go straight into appropriate locations of EEPROM, according to the memory map described in `README.md`.~~ not implemented

## REMOTE mode (`0x51`, `0x52`, `0x59`-`0x5D`)

In REMOTE mode (mode "RMT") the OMX is a dumb terminal for a host script
(monome norns lua, or `tools/remote_test.py`): the host owns all 27 LEDs and
the 128x32 screen, and every input event is reported over SysEx. Normal MIDI
output (notes/CCs) is suppressed. Hold AUX + click the encoder to open mode
select (the encoder long-press is left free for scripts).

All messages: `F0 7D 00 00 <cmd> <payload...> F7`.

### OMX -> host

- `0x51 0x00 [key z]` — key event, key 0-26 (0 = AUX), z 1/0
- `0x51 0x01 [dir amt]` — encoder turn, dir 0=CCW 2=CW, amt >= 1
- `0x51 0x02 [z]` — encoder button, z 1/0 (host does its own hold timing)
- `0x51 0x03 [pot v7 hi lo]` — pot 0-4, v7 0-127, plus 14-bit hi-res `(hi<<7)|lo`
  (jitter-suppressed: sent on a 7-bit change or a hi-res move >= 32)
- `0x52 0x02` — REMOTE mode entered; `0x52 0x03` — REMOTE mode left

### host -> OMX (ignored unless REMOTE mode is active)

- `0x59 [idx r g b]` — stage LED idx, channels 0-127 (scaled x2 to 8-bit)
- `0x5A [start count r g b ...]` — stage a run of LEDs
- `0x5B` — latch staged LEDs (like grid:refresh())
- `0x5C [chunk enc37]` — stage screen chunk 0-15: 32 bytes of the SSD1306
  buffer, 7-bit encoded exactly like the mirror's `0x50` FRAME chunks
- `0x5D` — latch the staged frame to the screen (also keeps the screensaver away)

The screen buffer is SSD1306 page layout (`byte = buf[x + (y/8)*128]`, bit
`y%8`), stored 180deg rotated vs the physical panel — same orientation the
mirror streams out, see `tools/remote_test.py::Omx._pack()`.
