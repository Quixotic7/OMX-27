# omx27-led-design JSON format

Interchange format for OMX-27 LED designs, produced and consumed by
[index.html](index.html) (the OMX-27 LED designer). Hand this file (or just the JSON) to Claude
when asking it to read or generate a design.

## Hardware conventions

The [OMX-27](https://github.com/okyeron/OMX-27) is a 27-key MIDI keyboard / sequencer with an
RGB LED under every key, 5 potentiometers, one push encoder, and a 128×32 OLED screen.

- **27 RGB LEDs, addressed by key index 0–26**, exactly matching the firmware call
  `strip.setPixelColor(index, 0xRRGGBB)` (Adafruit NeoPixel, `NEO_GRB`). The index order is:
  - **key 0** = the **AUX** / function key (stands alone, top-left)
  - **keys 1–10** = the **10 black keys** (top row) — sharps `C#4 D#4 F#4 G#4 A#4 C#5 D#5 F#5 G#5 A#5`
  - **keys 11–26** = the **16 white keys** (bottom row) — naturals `B3 C4 D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 A5 B5 C6`
- The keys form a real **piano layout**: the 16 white keys sit in a row, and the 10 black keys
  sit above them in their true piano positions (with the usual gaps at E–F and B–C).
- Each LED is a colour **`"#RRGGBB"`**; **`"#000000"` means the LED is off**. (An RGB LED encodes
  its own brightness — a dim colour is a dark colour; off is black.)
- Note numbers are standard MIDI (middle C = 60 = `C4`) and come straight from the firmware
  `notes[]` array. See `keyMap` below for the full index → note table.
- The other controls — **5 pots (K1–K5)**, the **encoder** (turn + press), and the **screen** —
  carry free-text notes per state (and pots an optional 0–127 value, the screen optional display text).

## Schema (version 1)

A design holds **one or more states** — alternative pages/modes of the same hardware (e.g.
"MIDI mode" vs "Sequencer"). Every state describes all 27 LEDs plus the controls.

```json
{
  "format": "omx27-led-design",
  "version": 1,
  "device": "OMX-27",
  "name": "my design",
  "ledCount": 27,
  "convention": "(human-readable restatement of the conventions above)",
  "keyMap": [
    { "key": 0,  "role": "aux",   "note": null, "name": "AUX" },
    { "key": 1,  "role": "black", "note": 61,   "name": "C#4" },
    { "key": 11, "role": "white", "note": 59,   "name": "B3"  }
  ],
  "notes": "global notes: overall intent, how the states relate, animation behaviour",
  "states": [
    {
      "state": 1,
      "name": "MIDI mode",
      "export": true,
      "notes": "free-text notes about this state",
      "leds": [
        "#ff0000", "#000000", "... exactly 27 colours, one per key index 0-26 ..."
      ],
      "keyNotes": { "0": "AUX — hold for shortcuts", "12": "middle C" },
      "pots": [
        { "pot": 1, "label": "K1", "note": "Cutoff / CC 21", "value": 100 },
        { "pot": 2, "label": "K2", "note": "", "value": null }
      ],
      "encoder": { "note": "scroll octave", "press": "open menu" },
      "screen":  { "note": "idle MIDI screen", "text": "MIDI  C4\nOct:0 Ch:1" }
    }
  ]
}
```

- `states` contains 1–8 entries; `state` is its 1-based position. Each state has its own `name`
  (shown in the editor and png export), `notes`, `leds`, `keyNotes`, `pots`, `encoder` and `screen`.
- `export` marks whether the state is included in the png export (editor setting; defaults to true
  and is irrelevant to consumers of the JSON).
- **`leds`** is an array of **exactly 27** colour strings; **`leds[i]` is the colour of key `i`** — so
  `leds[0]` is AUX, `leds[11]` is the first white key (B3). `"#000000"` = off.
- **`keyNotes`** labels semantically meaningful keys (root note, play button, playhead, mute…),
  keyed by key index (as a string). Labels only; the LED colour lives in `leds`.
- **`pots`** has 5 entries (K1–K5). Each has a `note` (what the knob does, e.g. `"Cutoff / CC 21"`)
  and an optional `value` (0–127, or `null`) which draws the knob's indicator position.
- **`encoder`** has `note` (turn) and `press` (push) strings.
- **`screen`** has `note` (a description) and `text` (up to ~3 short lines rendered on the OLED mock-up).
- `keyMap` is a **static reference** (the same for every OMX-27) mapping each key index to its
  role and MIDI note; the designer emits it so a reader always knows what each key is.
- The loader is forgiving: missing fields default sensibly; colours may be `"#RRGGBB"`, `"RRGGBB"`,
  a `0xRRGGBB` number, or `[r,g,b]`; `keys` is accepted as an alias for `leds`; `keyNotes` may
  alternatively be an array of `{ key, text }`; a single-state file (top-level `leds`) loads as a
  one-state design.

## Tips for generating designs (for Claude)

- Emit the full 27-entry `leds` array. Keep AUX (index 0) in mind — it is a real LED.
- Colours map 1:1 to the firmware's named constants (`src/consts/colors.h`): e.g. `RED #FF0000`,
  `ORANGE #FF8000`, `CYAN #00FFFF`, `BLUE #0000FF`, `PURPLE #7F00FF`, `WHITE #FFFFFF`,
  `LOWWHITE #202020`, dims like `DKBLUE #00004D`. Prefer these so a design is easy to port to firmware.
- Common idioms: a dim colour (e.g. `#202020` LOWWHITE) for background/playable keys; a saturated
  colour for active/selected keys; `#FFFFFF` for a cursor/playhead; `#FF0000` on AUX.
- Use multiple `states` to show alternative pages: mode A / mode B, a page with a key held,
  before/after a press, animation keyframes. Each state is one static frame.
- Use `keyNotes`, the control notes, and `notes` to state intent (what each region/knob does,
  how it animates).
- The white keys (11–26) are a natural place for step sequencers (16 steps); the black keys (1–10)
  double as the firmware's F1/F2 + P1–P8 function/pattern keys.
