# M8V2 Macro Mode – Launchpad Pro emulation for the Dirtywave M8

Branch: `Q7-2026-M8LaunchpadMode` (q7-2026-3 merged in at `57e7cb9`). Firmware 1.15.x.

## 0. What the q7-2026-3 merge changed (delta from the first plan)

| First plan assumed | Merged tree | Effect |
|---|---|---|
| Macros instantiated per mode, `getActiveMacro()` switch in MI/DRUM/CH | `AuxMacroManager` (`src/utils/aux_macro_manager.*`) owns **one static instance** of each macro and the `getActiveMacro()` switch; MI and FORM use it. DRUM and CHORDS still carry their own instances and their own switch. | Registration is 3 places (manager, drum, chords), not a new global. |
| Add a new "M8V2" entry to `macromodes[]` | **Decision: replace M8 in place.** Slot 1 ("M8") becomes the new class; the old class stays in the tree behind `#ifdef OMX_M8_MACRO_LEGACY`. | No EEPROM, CONFIG-mode, FORM param or Docs list changes. `nummacromodes` stays 3. |
| USB MIDI callbacks not registered on Teensy | Registered for both targets in `MM::begin()` | Concern gone. |
| `handleControlChange()` calls `stopTransport()` on every CC | Removed | Concern gone. |
| `usbMidiRead()` parses one byte per loop | RP2040 drains the whole TinyUSB FIFO per call | LED bursts from the M8 (80+ notes on connect) are handled in one loop pass. |
| No SysEx dispatcher | `sysex.cpp` is an opcode dispatcher with the NornsLink / REMOTE commands, still gated on `F0 7D 00 00` | Add the universal-inquiry hook **before** the 7D gate. |
| No QA tooling | `OMX-27-firmware/tools/` has `omxctl.py` (inject keys/enc, capture OLED, read LED state), `midimon.py`, `analyze.py`; `SYSEX_SPEC.md` documents the protocol | Hardware QA can be scripted end to end; add `virtual_m8.py` next to them. |
| FORM mode did not exist | FORM uses the manager, forwards CCs to the macro, ignores incoming notes, and can disable macro pot capture via `setMacrosConsumePots` | Macro is reachable from MI, DRUM, CH and FORM. Note routing must go through the manager so FORM gets it for free. |
| MI mode `inMidiNoteOn` lights keys from incoming notes | Still true (`omx_mode_midi_keyboard.cpp:716`) | Must intercept before it. |
| RAM budget unknown | Teensy 3.2 after manager integration: RAM 54.5 %, flash 72 % (comment in `aux_macro_manager.h`) | Fine for this class; keep the palette in flash. |

## 1. What the M8 expects (protocol recap)

Sources: `schwung-m8/src/ui.js` (Ableton Move, tested against real M8), `m8cs.lua` (norns), Grahack/M8_LPP_recap, M8 changelog (LPP MK3 support since 4.0.0; Note/Seq/Beat-repeat views since 6.0.0).

### Handshake
- M8 sends a Universal Device Inquiry: `F0 7E 7F 06 01 F7` (repeats until answered).
- We answer as a Launchpad Pro MK3: `F0 7E 00 06 02 00 20 29 23 01 00 00 <4 version bytes> F7`. m8cs answers with the Mini MK3 family (`13 01`), schwung with zeros; the M8 only seems to check the Novation ID (`00 20 29`), so `23 01` (Pro MK3) is the safe choice.
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
- Note On, `velocity = LPP palette index (0..127)`, channel 1 = static, channel 2 = flash, channel 3 = pulse.
- Ring buttons (track, scene, Play, Mute, Solo…) also arrive as notes (schwung maps them that way). Accept CCs with the same number too, to be safe.
- 128-entry LPP palette → RGB table (Launchpad MK3 programmer reference palette), ~384 bytes in flash.

### Legacy Control Map (not used for now)
Notes on the macro channel `M-CH`: 0 Play, 1 Shift, 2 Edit, 3 Option, 4 Left, 5 Right, 6 Up, 7 Down (what the old `MidiMacroM8` sends). **Decision (2026-09-09): the nav cluster uses the LPP buttons only.** Option is pencilled in as a no-op key; a later `NAV` param may bring the Control Map back for it.

## 2. Layout (from `OMX M8V2 layout.json`)

### Session (clip launch) view – default
| Key | Function | LED |
|---|---|---|
| 0 | AUX: hold = shortcut layer, double-click = exit macro (manager handles this) | PURPLE |
| 1 / 2 | Scroll row down / up (which of the 8 grid rows keys 11-18 show) | dim purple, brighter when more rows in that direction |
| 3 | Pad mode: CLIP | MAGENTA (bright when active) |
| 4 | Pad mode: MUTE; press again while active toggles latch/momentary | RED |
| 5 | Pad mode: SOLO; same latch/momentary toggle | YELLOW |
| 8 / 21 / 22 / 23 | Up / Left / Down / Right → LPP Up 80 / Track< 91 / Down 70 / Track> 92 | dim purple |
| 9 | Option – **pencilled in, no-op for now** (no LPP equivalent; lit so the key exists in the layout) | WHITE |
| 10 | Edit → LPP Edit/Rec 10 | RBLUE |
| 11-18 | Grid pads col 1-8 of the selected row (CLIP), or track 1-8 with Mute/Solo held (MUTE/SOLO) | colour mirrored from the M8 |
| 19 | Row launch for the selected row (`row*10+9`) | colour mirrored from the M8 scene button |
| 24 | Shift → LPP Shift 90 | GREEN |
| 26 | Play → LPP Play 20 | WHITE, green when M8 reports Play lit |
| 6, 7, 20, 25 | unassigned in the design – proposals: 20 = Track < (91), 25 = Track > (92), 6 = Snapshot recall (1), 7 = Snapshot store (Shift+1) | |

### AUX shortcut layer (AUX held)
| Key | Function |
|---|---|
| 3 | Switch M8 to Session view (send 93) and OMX to Session layout |
| 4 | Switch M8 to Note view (send 94) and OMX to Note layout |
| 5 (proposal) | Seq view (97) |
| 1 / 2 (proposal) | LPP Up/Down (80/70) – octave shift in Note view |

Keys pressed while AUX is held never send pads. AUX-held detection: the manager passes every key event to the macro while active, so the macro tracks key 0 down/up itself (`auxHeld_`), the same way the Deluge macro does with `auxDown_`.

### Note view (M8 "keyboard view")
The M8 decides the note layout on the 8x8 grid; we forward pads and mirror LEDs (root / in-scale colours come from the M8).
- Keys 11-18 = grid row `row_` cols 1-8, keys 19-26 = row `row_+1`. Two grid rows = 16 white keys.
- Keys 1 / 2 keep scrolling rows (window of 2 rows over 8). Keys 8/9/10/21-24/26 keep the nav cluster so Play/Shift/Edit stay reachable. Keys 3/4/5 are dark (pad modes are Session-only).
- Alternative if the M8 layout turns out to be chromatic-per-row: map black keys 1-8 to a third row. Decide after seeing real LED feedback.

## 3. Code changes

### Replace-in-place with a legacy switch
- `src/config.h` (near the other feature defines) : `// #define OMX_M8_MACRO_LEGACY` – define to build the old mute/solo + control-page macro.
- `src/midimacro/midimacro_m8.h/.cpp`: wrap the existing class in `#ifdef OMX_M8_MACRO_LEGACY … #endif`; rename nothing, so the legacy build is byte-for-byte the current one.
- New `src/midimacro/midimacro_m8v2.h/.cpp`: `class MidiMacroM8V2`. Header exposes it under `#ifndef OMX_M8_MACRO_LEGACY`.
- A small alias header (or a block in `midimacro_m8.h`) selects the type: `#ifdef OMX_M8_MACRO_LEGACY using MidiMacroM8Type = MidiMacroM8; #else using MidiMacroM8Type = MidiMacroM8V2; #endif`. The three owners declare `MidiMacroM8Type m8Macro_` so nothing else changes:
  - `src/utils/aux_macro_manager.cpp` (static instance + `getActiveMacro()` case 1) – covers MI and FORM.
  - `src/modes/omx_mode_drum.h/.cpp` and `omx_mode_chords.h/.cpp` (own instances, own switch).
- `macromodes[]` stays `{"Off","M8","NRN","DEL"}`; `getName()` returns `"M8"` in both builds. Docs describe the new behaviour and mention the legacy define.

### Interface additions (`midimacro_interface.h`)
- `virtual bool inMidiNoteOn(byte ch, byte note, byte vel) { return false; }`
- `virtual bool inMidiNoteOff(byte ch, byte note, byte vel) { return false; }`
- `virtual bool inSysEx(const uint8_t *data, unsigned len) { return false; }`
  Return true = consumed. Existing macros keep the defaults.

### Inbound routing
- `AuxMacroManager`: add `bool inMidiNoteOn/Off(...)` mirroring the existing `inMidiControlChange` (forward to `getActiveMacro()` even when not active, so the LED cache is warm on entry; return consumed).
- `OmxModeMidiKeyboard::inMidiNoteOn/Off`: call the manager first and return if consumed, **before** the key-lighting code. `OmxModeForm::inMidiNoteOn/Off`: same call (currently no-ops). DRUM: same via its own `getActiveMacro()`. CHORDS has no note-in handlers; add the forwarders.
- `sysex.cpp processIncomingSysex`: before the `F0 7D 00 00` gate, `if (size >= 5 && d[1]==0x7E && d[3]==0x06 && d[4]==0x01)` → `activeOmxMode->inSysEx(...)`? The mode interface has no SysEx hook and adding one to every mode is noise. Simpler: a free function `midimacro::onDeviceInquiry()` in `midimacro_m8v2.cpp` that replies when `midiMacroConfig.midiMacro == 1` (the M8 slot), independent of which OMX mode is active. Also handle an incoming 7E inquiry from any *other* device the same way; only the M8 asks in practice. Document under a new `## Universal Device Inquiry (0x7E)` heading in `SYSEX_SPEC.md`.
- `handleControlChange` already forwards to the mode, which forwards to the manager; extend `MidiMacroM8V2::inMidiControlChange` to treat CC number = LPP note for ring LEDs.
- MIDI thru: incoming M8 LED notes would be echoed to TRS if `midiSoftThru` is on; acceptable (it's user-selected), note in docs.

### `MidiMacroM8V2` internals
State:
```
enum View { VIEW_SESSION, VIEW_NOTE };  enum PadMode { PAD_CLIP, PAD_MUTE, PAD_SOLO };
View view_; PadMode padMode_; bool muteLatch_, soloLatch_; uint8_t row_ = 8;
bool linked_; uint32_t lastIdentityMs_; bool auxHeld_;
uint8_t ledColor_[110]; uint8_t ledMode_[110];   // by LPP note; mode 0 static / 1 flash / 2 pulse
uint8_t keyNoteSent_[27];                        // LPP note held per OMX key (0 = none)
```
Behaviour:
- `onEnabled`: send identity, `linked_=false`, send Session (93), redraw. `loopUpdate`: resend identity every 1000 ms until `linked_`.
- `onDeviceInquiry` (from sysex.cpp): reply immediately, regardless of enabled state, whenever slot 1 is selected.
- `inMidiNoteOn/Off` (channels 1-3): update `ledColor_/ledMode_`, `linked_=true`, `omxLeds.setDirty()`; consume. Other channels: not consumed.
- Key down: resolve to an LPP note (ch 1), send, remember in `keyNoteSent_`. Key 9 (Option) resolves to nothing: lit, consumed, sends nothing. Key up: send off for what was sent, never a recomputed note, so scrolling while a pad is held cannot leave a stuck note.
- MUTE/SOLO pad modes: momentary = hold Mute(2)/Solo(3) while the mode is active and release on leaving; latch = the modifier stays down until the mode key is pressed again. Track keys 11-18 send 101-108 while the modifier is held. **Needs hardware confirmation** (§4b).
- `onDisabled`: release every held note (`keyNoteSent_`, Mute/Solo modifiers), clear LEDs.
- `drawLEDs`: keys 11-18 (+19-26 in Note view) from the palette; flash → `omxLeds.getBlinkState()`, pulse → alternate full/half on the slow blink; key 19 from scene note `row_*10+9`; static colours per the tables; AUX purple; active pad-mode key bright; key 26 green when note 20's cached colour is non-zero. Respect `ledBrightness` (global strip brightness already applies).
- Display: `dispGenericModeLabel` style, e.g. `M8  SESS  R8  CLIP`, status `LPP: waiting…` / `linked`. Encoder turn = scroll row; press = toggle param select. Param page 2: `MUTE` latch, `SOLO` latch (a `NAV` param is deferred).
- Pots: unchanged from the old macro (`omxUtil.sendPots` on M-CH); FORM's `setMacrosConsumePots(false)` still applies.

### Docs
- `Docs.md` "M8 Macro Mode" section rewritten: M8 setup (MIDI Settings → CTRL SURFACE = Launchpad Pro, Control Map channel = `M-CH`), the two layouts, AUX shortcuts, latch toggle, iOS over USB, and a one-liner on `OMX_M8_MACRO_LEGACY`.
- `SYSEX_SPEC.md`: Universal Device Inquiry reply.
- `OMX-27-firmware/tools/README.md`: `virtual_m8.py` usage.

## 4. Open questions / decisions to confirm on hardware
a. **Option key**: no LPP equivalent. Decision: nav cluster is LPP-only (Up 80, Down 70, Left 91, Right 92, Edit 10, Shift 90, Play 20); Option is a lit no-op placeholder until we pick a behaviour (Control Map note 3 on M-CH, or LPP Clear 60).
b. **Mute / Solo semantics** on the LPP (held modifier + track button vs. toggled mode). Verify with LED feedback on notes 2/3 and 101-108.
c. **Note view grid layout** on the M8 (isomorphic vs chromatic rows) – decides whether black keys should map to a third row.
d. Ring LEDs as notes, CCs, or both. The receiver accepts both.
e. Identity family bytes: `23 01` (Pro MK3) vs `13 01` (Mini MK3, used by m8cs). Start with `23 01`.
f. Teensy 3.2 flash: 72 % used after the manager work; palette + class should add well under 8 KB and the legacy class drops out. Check after phase 1 on all three targets.

## 5. Phases
1. **Skeleton + handshake**: legacy define, new class wired into the manager/drum/chords, interface hooks, note + SysEx routing, identity reply + retry, status on the display. Verify with `virtual_m8.py` (copied into `tools/`, port match changed to `omx-27-v3`) then the real M8 / iOS app. Build all three targets.
2. **Session view**: pads, row scroll, row launch, LED mirror with palette, flash/pulse, stuck-note protection. QA with `omxctl.py leds` + `midimon.py`.
3. **Nav cluster + AUX layer + pad modes**: LPP nav keys (Option placeholder), AUX+3/4 view switch, Mute/Solo with latch toggle, release-all on exit.
4. **Note view**.
5. **Polish**: param page (latch), unassigned keys, Docs.md, SYSEX_SPEC.md, tools README, flash/RAM report.
