# M8V2 Macro Mode – Launchpad Pro emulation for the Dirtywave M8

Branch: `Q7-2026-M8LaunchpadMode`. Firmware version already bumped to 1.15.2 in the working tree.

## 1. What the M8 expects (protocol recap)

Sources: `schwung-m8/src/ui.js` (Ableton Move, tested against real M8), `m8cs.lua` (norns), Grahack/M8_LPP_recap, M8 changelog (LPP MK3 support since 4.0.0, Note/Seq/Beat-repeat views since 6.0.0).

### Handshake
- M8 sends a Universal Device Inquiry: `F0 7E 7F 06 01 F7` (repeats until answered).
- We answer as a Launchpad Pro MK3: `F0 7E 00 06 02 00 20 29 23 01 00 00 <4 version bytes> F7`.
  m8cs.lua answers with the Mini MK3 family (`13 01`) and schwung answers with zeros; the M8 only seems to check the Novation ID (`00 20 29`), so `23 01` (Pro MK3) is the safe choice.
- schwung also sends the identity proactively on start and every ~1 s until the M8 sends any LED message. We do the same, because the M8 may have sent its inquiry before the macro was entered.
- Any other SysEx from the M8 (e.g. programmer-mode select `F0 00 20 29 02 0E 0E 01 F7`) is ignored.

### Buttons we send (Note On/Off, channel 1, vel 127 / 0)
LPP MK3 programmer numbering, `note = row*10 + col`, row 1 = bottom, row 8 = top.

```
 90    91   92   93    94   95  96  97   98   99
Shift  T<   T>   Sess  Note  -   -   Seq  Prj  Logo
 80 Up      | 81 .. 88 |  89 row launch (row 8)
 70 Down    | 71 .. 78 |  79
 60 Clear   | 61 .. 68 |  69
 50 Dupe    | 51 .. 58 |  59
 40 -       | 41 .. 48 |  49
 30 -       | 31 .. 38 |  39
 20 Play    | 21 .. 28 |  29
 10 Edit/Rec| 11 .. 18 |  19 row launch (row 1)
             101 .. 108   Track buttons T1..T8
             1 SShot  2 Mute  3 Solo  4..8 -
```
Confirmed by both schwung's control map and m8cs' outer-ring table. There is no LPP button for the M8's **Option** key (see §4a).

### LEDs we receive
- Note On, `velocity = LPP palette index (0..127)`, channel 1 = static, channel 2 = flash, channel 3 = pulse (M8_LPP_recap, m8cs, schwung).
- Ring buttons (track buttons, scene buttons, Play, Mute, Solo…) also arrive as notes (schwung maps them that way). Accept CCs with the same number too, to be safe.
- We need a 128-entry LPP palette → RGB table (Launchpad MK3 programmer reference palette). ~384 bytes, put in flash.

### Legacy Control Map (still used for the nav cluster)
Notes on the macro channel `M-CH` (M8 "Control Map"): 0 Play, 1 Shift, 2 Edit, 3 Option, 4 Left, 5 Right, 6 Up, 7 Down. This is exactly what the existing `MidiMacroM8` control page sends.

## 2. Layout (from `OMX M8V2 layout.json`)

### Session (clip launch) view – default
| Key | Function | LED |
|---|---|---|
| 0 | AUX: hold = shortcut layer, double-click = exit macro (host mode handles this) | PURPLE |
| 1 / 2 | Scroll row down / up (which of the 8 grid rows keys 11-18 show) | dim purple, brighter when more rows in that direction |
| 3 | Pad mode: CLIP | MAGENTA (bright when active) |
| 4 | Pad mode: MUTE; press again while active toggles latch/momentary | RED |
| 5 | Pad mode: SOLO; same latch/momentary toggle | YELLOW |
| 8 / 21 / 22 / 23 | Up / Left / Down / Right (Control Map 6/4/7/5 on M-CH) | dim purple |
| 9 | Option (Control Map 3) | WHITE |
| 10 | Edit (Control Map 2) | RBLUE |
| 11-18 | Grid pads col 1-8 of the selected row (CLIP), or track 1-8 with Mute/Solo held (MUTE/SOLO) | colour mirrored from the M8 |
| 19 | Row launch for the selected row (`row*10+9`) | colour mirrored from the M8 scene button |
| 24 | Shift (Control Map 1) | GREEN |
| 26 | Play (Control Map 0) | WHITE, green when M8 reports Play lit |
| 6, 7, 20, 25 | unassigned in the design – proposals: 20 = Track < (91), 25 = Track > (92), 6 = Snapshot recall (1), 7 = Snapshot store (Shift+1) | |

### AUX shortcut layer (AUX held)
| Key | Function |
|---|---|
| 3 | Switch M8 to Session view (send 93) and OMX to Session layout |
| 4 | Switch M8 to Note view (send 94) and OMX to Note layout |
| 5 (proposal) | Seq view (97) |
| 1 / 2 (proposal) | LPP Up/Down (80/70) – octave shift in Note view |

Keys pressed while AUX is held never send pads.

### Note view (M8 "keyboard view")
The M8 decides the note layout on the 8x8 grid; we only forward pads and mirror LEDs (root / in-scale colours come from the M8).
- Keys 11-18 = grid row `row_` cols 1-8, keys 19-26 = row `row_+1`. Two grid rows = 16 white keys.
- Keys 1 / 2 keep scrolling rows (window of 2 rows over 8). Keys 8/9/10/21-24/26 keep the nav cluster so Play/Shift/Edit stay reachable. Keys 3/4/5 are dark (pad modes are Session-only).
- Alternative if the M8 layout turns out to be chromatic-per-row: map black keys 1-8 to a third row. Decide after seeing real LED feedback.

## 3. Code changes

### New files
- `src/midimacro/midimacro_m8v2.h / .cpp` – class `MidiMacroM8V2 : MidiMacroInterface`.
- `src/midimacro/lpp_palette.h` – `const uint8_t lppPalette[128][3] PROGMEM`.

### Interface additions (`midimacro_interface.h`)
- `virtual bool inMidiNoteOn(byte ch, byte note, byte vel) { return false; }`
- `virtual bool inMidiNoteOff(byte ch, byte note, byte vel) { return false; }`
- `virtual bool inSysEx(const uint8_t *data, unsigned len) { return false; }`
  Return true = consumed. Existing macros keep default behaviour.

### Registration
- `config.cpp`: `macromodes[] = {"Off","M8","NRN","DEL","M8V2"}`, `nummacromodes = 4`. Add an enum (`MACRO_OFF, MACRO_M8, MACRO_NORNS, MACRO_DELUGE, MACRO_M8V2`) to replace the magic numbers. The macro id is stored as one byte at `EEPROM_HEADER_ADDRESS + 30`, so value 4 persists with no storage format change.
- `getActiveMacro()` in `omx_mode_midi_keyboard.cpp`, `omx_mode_drum.cpp`, `omx_mode_chords.cpp`: add `case MACRO_M8V2`.
- Instantiate **one global** `MidiMacroM8V2` (the drum mode already carries a TODO to make macros global). One instance keeps the LED cache and link state shared across MI/DRUM/CH and avoids three copies of the state. Each mode still calls `setDoNoteOn/Off` and `setScale` on enable.

### Inbound MIDI routing (the part that does not exist yet)
- `midi.cpp handleNoteOn/Off` → `activeOmxMode->inMidiNoteOn`. In MI mode that handler **lights keys from incoming notes** (`inMidiNoteOn` at `omx_mode_midi_keyboard.cpp:1290`), which would fight the LED mirror. In all three modes, forward to the configured macro first (via `getActiveMacro()`, even when the macro is not active, so the LED cache is warm on entry); if consumed, return.
- `sysex.cpp processIncomingSysex` currently drops anything that is not `F0 7D 00 00`. Add an early hook: if `data[1]==0x7E && data[3]==0x06 && data[4]==0x01` (device inquiry) hand it to the configured macro (`inSysEx`). Reply regardless of whether the macro is active, as long as M8V2 is the selected macro.
- `midi.cpp handleControlChange` calls `stopTransport()` on **every** incoming CC. If the M8 ever sends ring LEDs as CCs, each LED update would stop the OMX transport. Route CCs to the macro first and skip the transport/bank-select logic when consumed.
- **Teensy builds**: `MM::begin()` registers the USB MIDI callbacks (`setHandleNoteOn`, `setHandleSystemExclusive`, …) only under `BOARDTYPE == OMX2040`. Verify the Teensy 3.2 / 4.0 path and add the `usbMIDI.setHandle*` registrations there, otherwise the handshake never fires on Teensy.
- SysEx size: the inquiry is 6 bytes, well under the MIDI library's 128-byte default.

### `MidiMacroM8V2` internals
State:
```
enum View { VIEW_SESSION, VIEW_NOTE };  enum PadMode { PAD_CLIP, PAD_MUTE, PAD_SOLO };
View view_; PadMode padMode_; bool muteLatch_, soloLatch_; uint8_t row_ = 8;
bool linked_; uint32_t lastIdentityMs_;
uint8_t ledColor_[110]; uint8_t ledMode_[110];   // by LPP note, mode 0 static / 1 flash / 2 pulse
uint8_t keyNoteSent_[27];                        // LPP note currently held per OMX key (0 = none)
bool auxHeld_;
```
Behaviour:
- `onEnabled`: send identity, `linked_=false`, send Session (93), redraw. `loopUpdate`: resend identity every 1000 ms until `linked_`.
- `inSysEx`: on inquiry → send identity immediately.
- `inMidiNoteOn/Off` (channels 1-3): update `ledColor_/ledMode_`, `linked_ = true`, `omxLeds.setDirty()`; consume. Notes on other channels are not consumed.
- Key down: resolve to an LPP note (or Control Map note on M-CH), send, remember in `keyNoteSent_[key]`. Key up: send off for `keyNoteSent_[key]`, not for a recomputed note, so scrolling while a pad is held cannot leave a stuck note.
- MUTE/SOLO pad modes: momentary = send Mute(2)/Solo(3) note-on while the mode is active and release it when leaving the mode; latch = the same but the modifier stays down until the mode key is pressed again. Track keys 11-18 send 101-108 while the modifier is held. **Needs hardware confirmation** that the M8 treats Mute/Solo as a held modifier (see §4b).
- `onDisabled`: release every held note (`keyNoteSent_`, Mute/Solo modifiers, nav keys), clear LEDs.
- `drawLEDs`: keys 11-18 (+19-26 in Note view) from the palette; flash → use `omxLeds.getBlinkState()`, pulse → alternate full/half brightness on the slow blink; key 19 from scene note `row_*10+9`; static colours for the rest per the table above; AUX purple; mode key of the active pad mode bright, others dim; key 26 green when LED cache for note 20 is non-zero.
- Display: `dispGenericModeLabel` style, e.g. `M8V2  SESS  R8  CLIP` and a status line `LPP: waiting…` / `linked`. Encoder turn = scroll row; press = toggle param select. Param page 2: `NAV` (M8 map / LPP), `MUTE` latch, `SOLO` latch, `BRT` LED brightness scale.
- Pots: unchanged from M8 v1 (`omxUtil.sendPots` on M-CH).

### Docs
- `Docs.md`: new "M8V2 Macro Mode" subsection under MIDI Macro Modes: M8 setup (MIDI Settings → CTRL SURFACE = Launchpad Pro, Control Map channel = `M-CH`), the two layouts, AUX shortcuts, the latch toggle. Note that it works over USB to the iOS app.

## 4. Open questions / decisions to confirm on hardware
a. **Option key**: there is no LPP equivalent, so the plan keeps the whole nav cluster on the legacy Control Map (M-CH). Requires the M8's Control Map channel to match `M-CH`, same as today. A `NAV=LPP` param can offer the LPP buttons instead (Up 80, Down 70, Left 91, Right 92, Edit 10, Shift 90, Play 20, Option → Clear 60).
b. **Mute / Solo semantics** on the LPP (held modifier + track button vs. toggled mode). Verify with LED feedback on notes 2/3 and 101-108.
c. **Note view grid layout** on the M8 (isomorphic vs chromatic rows) – decides whether black keys should map to a third row.
d. Whether ring LEDs arrive as notes, CCs, or both. The receiver accepts both.
e. Identity family bytes: `23 01` (Pro MK3) vs `13 01` (Mini MK3, used by m8cs). Start with `23 01`; fall back if the M8 ignores it.
f. Teensy 3.2 flash headroom: Deluge macro comments show ~61 % used; palette + class should add well under 8 KB. Check after phase 1.

## 5. Phases
1. **Skeleton + handshake**: class, registration in config and three modes, interface hooks, sysex/note routing, identity reply + retry, status on the display. Verify with `schwung-m8/tools/virtual_m8.py` pointed at the OMX USB port (it sends the inquiry and initial LEDs), then with the real M8/iOS app.
2. **Session view**: pads, row scroll, row launch, LED mirror with palette, flash/pulse, stuck-note protection.
3. **Nav cluster + AUX layer + pad modes**: Control Map keys, AUX+3/4 view switch, Mute/Solo with latch toggle, release-all on exit.
4. **Note view**.
5. **Polish**: param page (NAV / latch / brightness), unassigned keys, Docs.md, Teensy build check, RAM/flash report.

Testing aids: `virtual_m8.py` (needs `pip install mido python-rtmidi`; adapt the port name matching from "Move" to "OMX"), and the existing OMX SysEx remote-control harness for driving keys and mirroring the OLED during QA.
