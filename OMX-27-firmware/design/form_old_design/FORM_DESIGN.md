# FORM — Full Design Reference

A complete, as-built reference for the **FORM** mode: the hardware model, every
on-screen state, and the exact **LED / key / pot / encoder / screen** behaviour of
the container and each machine. This is the companion to the LED-designer files in
this folder and to the design review in
[`../../src/form/DESIGN_NOTES.md`](../../src/form/DESIGN_NOTES.md).

- **This file** = *how FORM works today* (mechanics + exact colors).
- **`DESIGN_NOTES.md`** = *the vision, what's done, what's missing, roadmap*.
- **`form_*.json`** = LED-designer frames you can load into `index.html` (the OMX-27 LED
  designer). See [`FORMAT.md`](FORMAT.md) for the JSON schema.

> Line references point at the current tree (`src/form/…`). Colors are the named
> constants in [`src/consts/colors.h`](../../src/consts/colors.h).

---

## 1. Hardware model

27 keys, each with an RGB LED (`strip.setPixelColor(index, 0xRRGGBB)`):

| Index | Role | Notes |
|---|---|---|
| **0** | AUX / function key | stands alone, top-left |
| **1–10** | black keys (top row) | also **F1 (1)**, **F2 (2)**, and **machine slots 0–7 on 3–10** |
| **11–26** | white keys (bottom row) | the **16-step** sequencer row (key 11 = step 0) |

Plus **5 pots (K1–K5)**, a **push encoder** (turn + press), and a **128×32 OLED**.

---

## 2. Concept

FORM is a **host for pluggable sequencer "machines"** (Elektron-ish, OMX-shaped).
Up to **8 machines** live in the 8 top-row slots (keys 3–10); each slot runs one
machine of a selectable **type**. Two types are built today:

| Type | Enum | Slot color | Status |
|---|---|---|---|
| **NONE** (empty) | `FORMMACH_NULL` (0) | off | — |
| **OMNI** | `FORMMACH_OMNI` (1) | `ORANGE #ff8000` | full polyphonic step sequencer |
| **EUCL** | `FORMMACH_EUCLID` (2) | `CYAN #00ffff` | Euclidean rhythm generator |

Planned but not built: **Grids**, **Tambola** (see `DESIGN_NOTES.md §4a`).

The container paints the **top row** (machine picker) and delegates the **bottom 16
keys**, the **pots**, the **display**, and note output to the selected machine.

---

## 3. Global color legend

| Meaning | Color | Hex |
|---|---|---|
| Selected machine slot | WHITE | `#ffffff` |
| Selected **and** muted slot | SALMON | `#ff8080` |
| Muted machine slot | RED | `#ff0000` |
| Machine fired this step (flash) | INDIGO | `#4b0082` |
| OMNI type / EUCL hit / (SEQ playhead=white) | ORANGE | `#ff8000` |
| EUCL type | CYAN | `#00ffff` |
| OMNI step has notes | LTBLUE | `#a8a8ff` |
| OMNI step empty (in length) | DKBLUE | `#00004d` |
| Playhead on a firing step | WHITE | `#ffffff` |
| Playhead on a silent step | RED | `#ff0000` |
| Jump-to-step target | LTYELLOW | `#ffff80` |
| EUCL rest | LOWWHITE | `#202020` |
| Note-editor: scale root | (periwinkle) | `#a2a2ff` |
| Note-editor: in-scale | (dim blue) | `#000090` |
| Note-editor: stored chord note | LTYELLOW / ORANGE | `#ffff80` / `#ff8000` |
| Note-editor: REST key (key 11) | DKRED | `#800000` |
| Cursor (blink) | HALFWHITE | `#808080` |
| Transpose 0 / up / down | TZERO / THIGH / TLOW | `#0000ff` / `#8080ff` / `#000020` |
| Step-function palette | RED,ORANGE,DKYELLOW,GREEN,MAGENTA,ROSE,DIMORANGE | `#ff0000 #ff8000 #4c4d00 #00ff00 #ff00ff #ff0080 #9f8060` |

---

## 4. Container states

### 4.1 `FORMMODE_BASE` — the main FORM screen
`OmxModeForm::updateLEDs()` `omx_mode_form.cpp:881-942`.

- All LEDs cleared, then:
  - If **AUX held** (`midiSettings.midiAUX`) → the AUX macro layer paints everything
    and the container returns (`:885-890`).
  - If the selected machine **consumes LEDs** → it paints the whole grid (`:894-898`).
    (OMNI consumes LEDs while a step is held or in an editor; EUCL never does.)
  - Otherwise the **top row (keys 3–10)** shows the 8 machine slots and the machine
    paints keys **11–26**.
- **Top-row slot colors** (`:906-922`): type color (off/ORANGE/CYAN) · muted = RED ·
  selected = WHITE (SALMON if muted) · fired-this-step = INDIGO (overrides).
- Keys **0/1/2** are unlit in base (AUX/F1/F2 are gestures, not indicators).

**Keys** (`onKeyUpdate` `:586-824`):

| Key | Action |
|---|---|
| 0 (AUX) | hold → AUX shortcut layer (octave on 11/12, play on AUX+1, reset on AUX+2) |
| 1 (F1) | hold → **copy machine**; tap a slot to copy, tap again to paste |
| 2 (F2) | hold → **cut/paste machine** |
| 1+2 (F3) | machine-defined shortcut overlay (top row hidden) |
| 3–10 | **tap** = select slot · **double-tap** = mute · **hold** = open type picker |
| 11–26 | forwarded to the selected machine |

→ Designer frames: `form_container.json` states 1 (stopped) & 2 (playing).

### 4.2 `FORMMODE_SELECTMACHINE` — machine-type picker
Reached by **holding** a slot key. Keys **11 / 12 / 13** = **NONE / OMNI / EUCL**
(colors off / ORANGE / CYAN); the slot's current type blinks (NONE blinks `DKRED`).
Press to set the type; release the held slot key to exit. `:804-822`, `:925-937`.

→ Designer frame: `form_container.json` state 3.

### 4.3 Shortcut overlays
`updateShortcutMode()` `:229-268`, driven purely by which of keys 0/1/2 are held:
`AUX`, `F1` (Copy), `F2` (Cut/Paste), `F3` (1+2). Feedback is mostly on the **screen**
(“Copy”/“Cut”/“Paste”/`getF3shortcutName()`); F3 also blanks the top row.

---

## 5. OMNI machine

`FormMachineOmni`, `form_machine_omni.cpp`. A single-track (`tracks[0]`) polyphonic
step sequencer, up to **64 steps** viewed 16 at a time through **page** (K1) × **zoom**
(K2, 1/2/4 bar). **UI mode** is chosen with **K5** or **AUX + keys 13–18**:
CONFIG · MIX · LENGTH · TRANSPOSE · STEP · NOTEEDIT.

> STEP and NOTEEDIT are identical. CONFIG and MIX are treated identically. LENGTH is a
> near-noop (track length is actually set via the F3 shortcut in CONFIG/MIX). See
> `DESIGN_NOTES.md §4c`.

### 5.1 SEQ view (CONFIG / MIX) — `updateLEDs()` `:1727-1783`
Step row: **LTBLUE** = step has notes, **DKBLUE** = in-length but empty, **off** =
muted or beyond track length. Playhead (while playing) = **WHITE** on a firing step,
**RED** on a silent one. `key16toStep()` applies page/zoom, so the 16 keys are a slice
of the 64-step track.

Keys: tap step = mute/unmute · double-tap = enter note editor · **hold** a step = arm
the **function overlay** (top keys 1–7 = `-- RSET << >> <> J? ???`, the current
function blinks; a second step key sets **jump-to-step N**, shown LTYELLOW).

→ `form_omni.json` states 1 (SEQ) & 2 (step-held).

### 5.2 Note editor (STEP / NOTEEDIT) — `omni_note_editor.cpp:41-146`
A piano/scale keyboard: roots `#a2a2ff`, in-scale `#000090`, out-of-scale off (red
tint when the step is muted). Notes on the selected step light **LTYELLOW** (in view)
or **ORANGE** (folded out of view). **Key 11 = REST** (DKRED) clears the step. Cursor
pulses **HALFWHITE**. Hold multiple keys to enter a chord (≤ 6 notes). Encoder moves
the selected step 0–63.

→ `form_omni.json` state 3.

### 5.3 Transpose pattern editor (TRANSPOSE) — `omni_transpose_pattern.cpp:100-380`
Per-step transpose lane within the pattern length: **TZERO** `#0000ff` (0), **THIGH**
`#8080ff` (up), **TLOW** `#000020` (down), off beyond length. Shortcuts: key 3 =
random-arm, key 9 = randomize, key 10 = clear, keys 1/2 = F1/F2 copy/cut. Holding a
step turns keys 1–10 into a value bar.

→ `form_omni.json` state 4.

### 5.4 What OMNI can do (model)
Per-step: ≤6 notes, velocity, gate length, micro-nudge (±60), probability, conditional
trigs (A:B, PRE/NEI/1ST + negation), step functions, transpose accumulation, per-step
MIDI-FX, per-step mute. Per-track: length, swing (+division), triplets, play direction
& modes (pong/rand/shuffle…), rate, gate, transpose modes. Global: MIDI channel,
send-MIDI, send-CV, BPM, scale.

> **Advertised but currently inert:** per-step CC/pot locks, monophonic mode,
> `startstep`, FILL trigs (see `DESIGN_NOTES.md §4b`). Some were wired in Tier-1 — check
> the notes file for current status.

---

## 6. EUCL machine

`FormMachineEuclid`, `form_machine_euclid.cpp`. Wraps the existing
`EuclideanSequencer`: distribute **Hits** over **Steps** with **Rotation**, play one
note per hit. **Does not** consume the grid — the container keeps the top-row picker and
EUCL paints only keys 11–26.

- **Step row** `updateLEDs()`: **ORANGE** = hit, **LOWWHITE** = rest, **WHITE** =
  playhead, **off** = beyond the step count.
- **Keys 11–26**: tap a step to **rotate** the pattern to start there.
- **Params** (encoder; hold AUX or press to edit): **RHYTHM** page = Steps / Hits /
  Rotation / Note · **NOTE** page = Vel / Chan / Length / Swing.
- Per-machine versioned save/load.

→ `form_euclid.json` states 1 (E(5,16)) & 2 (E(4,12), short length).

---

## 7. Pots & encoder (summary)

| Control | OMNI | EUCL |
|---|---|---|
| **K1** | Page (1–4) | — |
| **K2** | Zoom (1/2/4 bar) | — |
| **K3** | cross-page apply | — |
| **K4** | rate / play-mode | — |
| **K5** | UI mode | — |
| **Encoder turn** | select parameter | select parameter |
| **Encoder + AUX (or press-toggle)** | edit value | edit value |

> Encoder select vs edit is gated by the shared `omxFormGlobal.encoderSelect` flag
> (turn = select by default; hold AUX or click to edit). See the Aug-2026 fix for the
> static-init bug that had OMNI's param pages empty.

---

## 8. Machine interface (extension point)

Adding a machine = implement `FormMachineInterface`
([`machines/form_machine_interface.h`](../../src/form/machines/form_machine_interface.h))
and register it:

1. add a value to `enum FormMachineType`;
2. add a name to `kMachineNames[]` and a color to `kMachineColors[]`
   (`omx_mode_form.cpp:28-30`);
3. add a `case` in `changeMachineAtIndex()` that `new`s it.

Key contract methods: `getType()`, `getClone()`,
`doesConsume{Keys,LEDs,Display,Pots}()` (return **false** unless you need the whole
grid — return false to keep the container's top-row navigation working),
`updateLEDs()`, `onKeyUpdate()`, `onDisplayUpdate()`, `onEncoderChanged*()`,
`loopUpdate()`, `saveToDisk()/loadFromDisk()`, and the `seqNoteOn/seqNoteOff`
callbacks. EUCL (`form_machine_euclid.*`) is the smallest complete example.

---

## 9. Files in this folder

| File | What it is |
|---|---|
| `FORMAT.md` | the LED-designer JSON schema |
| `FORM_DESIGN.md` | this reference |
| `form_container.json` | container frames: base (stopped / playing), type picker |
| `form_omni.json` | OMNI frames: SEQ, step-held, note editor, transpose |
| `form_euclid.json` | EUCL frames: E(5,16), E(4,12) short length |

Load any `form_*.json` into the LED designer (`index.html`) to view/edit the frames.
