# OMX-27 host tools — SysEx remote control + MIDI QA

Drive the OMX-27 and see its screen from a computer, for hardware QA of the FORM sequencer
(or any mode). Pairs with the firmware's SysEx screen-mirror (`NornsLink`) and the input
injection command (`NL_CMD_INPUT`, 0x51).

## Requirements
```bash
pip3 install python-rtmidi pillow
```
The OMX must be connected over USB; it appears as a MIDI port named `omx-27-v3`.

## Scripts

### `omxctl.py` — remote control + screen capture
Importable library (and a CLI that saves a screenshot):
```bash
python3 omxctl.py screen.png      # capture the OLED to a PNG + print ASCII
```
```python
import omxctl
omxctl.tap(11)          # quick-tap key 11 (keys 0-26; 0 = AUX)
omxctl.hold(11, ms=300) # hold a key
omxctl.k_down(2); omxctl.tap(4); omxctl.k_up(2)   # chorded gesture (F2 + key4)
omxctl.enc(1)           # encoder +1 detent (negative = CCW)
omxctl.enc_click()      # encoder button
omxctl.pot(0, 100)      # set pot 0 to 100  (see caveat below)
omxctl.render(omxctl.capture()).save("s.png")     # grab the screen
```
Keys inject cleanly with real `quickClicked`/`held` timing. **Pot injection is unreliable** —
the firmware's pickup + smoothing logic fight a single injected value; use the physical knobs
for pickup-based edits. (The Step-view CC P-Lock uses a direct set, so it does respond.)

The panel is mounted 180°, which `render()` already accounts for.

### `midimon.py` — MIDI capture
```bash
python3 midimon.py capture.log     # logs note/CC/PC/transport + a BPM estimate; Ctrl-C to stop
```
Run it in the background while you drive the device. Restart it after each firmware upload
(reflashing re-enumerates the USB MIDI port).

### `analyze.py` — summarize a capture
```bash
python3 analyze.py capture.log [start_time]
```
Per-channel note counts + velocities, CC values seen, transport events, BPM, and an on/off
pairing check (stuck-note detection).

## SysEx protocol (device side)
- Enable screen mirror: `F0 7D 00 00 58 01 F7` (device streams `50`/`53` frame chunks).
- Inject input: `F0 7D 00 00 51 <sub> <args...> F7`
  - `00` KEY `[key(0-26)] [down] [held] [quickClicked] [clicks]`
  - `01` ENCODER `[dir 0=CCW/1=none/2=CW] [count] [speedup]`
  - `02` ENC BUTTON `[0=down 1=up 2=upLong]`
  - `03` POT `[pot(0-4)] [value(0-127)]`

See `src/midi/norns_link.*`, `src/midi/sysex.cpp`, and `omxInjectInput()` in the .ino.
