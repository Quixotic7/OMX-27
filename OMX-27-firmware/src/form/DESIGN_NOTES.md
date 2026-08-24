# FORM Sequencer — Design Notes & Status

_A review of the intended design, what shipped, where it stands, what's missing,
and how it could be improved. Sources: the spec comments in
[`omx_mode_form.h`](omx_mode_form.h), the v1.14–1.15 release notes, and a
line-by-line read of the implementation (Aug 2026)._

---

## 1. The vision

FORM was conceived as a **host for pluggable sequencers** ("machines"), Elektron /
Digitakt in spirit but OMX-shaped. The `omx_mode_form.h` header is effectively the
spec:

- **8 machine slots** (top 8 keys select a machine). Hold a machine key + press a
  bottom key to **switch that slot's sequencer TYPE**; changing type replaces the
  sequencer (with F1 undo).
- **Four machine types** were planned:
  - **OMNI** — a powerful polyphonic step sequencer
  - **Euclidean**
  - **Grids** (Mutable-style drum generator)
  - **Tambola** — bouncing balls in a rotating polygon
- **Machine + step copy / paste / cut / undo** (F1 / F2 / F1+F2).
- Designed for **samplers / drum machines** — each key configurable (note/vel/chan).

### Intended OMNI UI (Pot-driven, layered)
- **Pot 1** — page (1–4) · **Pot 2** — zoom (1/2/4 bar) · **Pot 3** — cross-page apply
  · **Pot 4** — rate/play-mode · **Pot 5** — UI mode.
- **Pot 5 UI modes:** SEQ · MIDI-Keyboard (record notes in) · MIDI-Keyboard-Transpose
  · Transpose-Pattern · Note-Editor · Machine-Config.
- **Pot 4 SEQ sub-modes:** MIX · EDIT FUNC · NOTE-LENGTH · NUDGE · MFX · ACCUM ·
  CHANCE · CONDITIONS · **CHORD** (8 configurable chord keys) · **DRUM** (8 drum keys).
- Per-step **CC/pot locks** (4 CCs on the last page), note nudging, conditional trigs,
  step functions, dual transpose (global + per-step accumulative), triplets/swing.
- "Make sequencer keys light up as notes are triggered."

---

## 2. Architecture (as built)

- **`OmxModeForm`** — the container mode. Holds `machines_[8]` of
  `FormMachineInterface*`, a selected-machine index, plus `copyBuffer_`/`undoBuffer_`
  for machine cut/copy/paste/undo. Renders the top row (machine select) and delegates
  keys/LEDs/display/pots to the selected machine.
- **`FormMachineInterface`** ([`machines/form_machine_interface.h`](machines/form_machine_interface.h))
  — the plugin contract: `getType()`, `getClone()`, `doesConsume{Keys,LEDs,Display,Pots}()`,
  note-out callbacks, `saveToDisk`/`loadFromDisk`. Clean and extensible.
- **`FormMachineNull`** — empty slot. **`FormMachineOmni`** — the one real machine.

This architecture is sound and is the project's biggest asset — adding a new machine
type is "implement the interface + register it."

---

## 3. Current state — what's implemented

### Container
- 8 machine slots; select, cut/copy/paste/undo of whole machines (`getClone()`).
- Machine **type switch** exists but `kMachineNames = {"NONE","OMNI"}` — only NULL and
  OMNI are registered, so in practice a slot is either **empty or OMNI**.

### OMNI machine (end-to-end working)
- **Per-step:** up to 6 notes, velocity, gate length (sub-step 0.25–0.75 + multi-bar),
  micro-nudge (±60), probability, **conditional trigs** (A:B ratios, PRE/NEI/1ST +
  negations), **step functions** (restart/rev/fwd/pong/rand-jump/rand/jump-to-N),
  transpose-pattern accumulation, per-step MIDI-FX routing, per-step mute, copy/cut/paste.
- **Per-track:** length, swing (+ 16th/8th division), triplet mode, play direction,
  play modes (pong / rand / rand-no-dupe / shuffle / shuffle-hold), rate, gate,
  transpose modes (global-interval / semitone / local-interval), track MIDI-FX.
- **Global:** MIDI channel, sendMIDI, sendCV, BPM, scale (root/pattern/lock/group).
- **Editors:** the **note editor** and the **transpose-pattern editor** are both real and
  fully wired (LEDs, keys, display, copy/paste, randomize).
- **Pages + zoom** (1/2/4 bar), **save/load**, **mute/solo**, **MIDI + CV output**, and
  the "keys light up as notes trigger" feedback.

The core sequencer is genuinely capable and mostly complete.

---

## 4. What's missing / incomplete

### 4a. The other machine types (the biggest gap vs the vision)
**Euclidean, Grids, and Tambola were never built.** The whole point of FORM — mixing
different sequencer types across the 8 slots — isn't reachable; every slot is OMNI or
empty. The plugin interface is ready for them; they just don't exist.

### 4b. Features that exist in the model/UI but do nothing in playback ("dead" features)
These are editable (and often displayed) but never acted on when the sequencer plays —
the most misleading kind of gap:

- **Per-step CC / pot locks** (`Step::potVals[5]`, `OmniSeq::potBank`, `potMode`
  CC-Step/CC-Fade) — you can edit and see them, but **no CC is ever sent**. `triggerStep`
  emits notes only. This is a headline OMNI feature that's UI-only.
- **Monophonic mode** (`OmniSeq::monoPhonic`) — the toggle exists and displays, but
  playback always plays all 6 notes; the flag is never read.
- **Track start step** (`Track::startstep`) — stored, initialised to 0, **never
  referenced**. `getRestartPos()` only uses play direction. (Comment hints at an
  intended "-1 = random start.")
- **FILL conditions** — `fillActive_` is read by `evaluateTrig` but **never set true**
  anywhere, so `FILL` trigs never fire and `!FILL` is always true. The fill trigger
  source (a key/AUX gesture) was never wired.

### 4c. UI collapsed vs the spec
The rich Pot-4/Pot-5 matrix in the spec did not materialise as distinct modes:
- `OmniUIMode` shipped as CONFIG / MIX / LENGTH / TRANSPOSE / STEP / NOTEEDIT.
- **CONFIG and MIX are treated identically** everywhere (the enum comment admits it).
- **LENGTH is nearly a no-op** — empty cases in LEDs/keys/held/quick-click; only the
  encoder path reaches the transpose editor, seemingly by accident.
- **MIDI-Keyboard record**, **MIDI-Keyboard-Transpose**, and the SEQ sub-modes
  **CHORD** and **DRUM** (8 configurable chord/drum keys per the spec) aren't present.
- `onAUXFunc()` is empty — the documented AUX+Top2 Reset / Top3 flip / Top4 mode live
  (partially) in `onKeyUpdate` instead, and only the direction flip is implemented.

### 4d. Single-track
OMNI is **single-track**: `OmniSeq::tracks[1]` (comment: _"possibly more in future if
mem permits"_), everything hardcoded to `tracks[0]`. The abstraction anticipates
multi-track but there's no track-select UI or playback loop. (You still get 8 parallel
OMNI *machines*, so 8 lanes total — but each machine is one track.)

### 4e. Debug scaffolding left in
`setTest()` (hardcoded C-E-G-B arp) and large commented-out blocks (`getParams` body,
old page-name messages, shuffle experiments, nudge/triplet re-derivations) remain.

---

## 5. Bugs & rough edges

| # | Issue | Where | Effect |
|---|---|---|---|
| 1 | `Step::CopyFrom` copies only `potVals[0..3]` (loop `< 4`, array is 5) | `omni_structs.h:217` | Copy/paste/cut silently drops the 5th CC lock (latent; moot until CC output exists) |
| 2 | `random(0, track->len)` in play modes uses `len` not `len+1` | `form_machine_omni.cpp:670,688,811` | RAND / RAND-JUMP / RAND-NO-DUPE can **never land on the last step** |
| 3 | `calculateShuffle` uses `random(0, size-1)` (exclusive max) | `:744` | Last element never chosen → biased shuffle; degenerate when size==1 |
| 4 | A:B conditions constrained 0–35 but only ≤ ~7:7 reachable | `:2071` vs `kTrigConditionsAB` | Upper ratio rows (1:8…8:8) can't be selected despite table + field supporting them |
| 5 | Save/load is a **raw struct blit** of `OmniSeq` (bitfields, no version tag) | `:2541-2574` | Any field reorder/resize silently corrupts saved patterns; no migration |
| 6 | `std::vector` churn every clock tick on RP2040 | `form_machine_omni.h:146-173` | Heap fragmentation risk; only `noteOns_` is capped (16) |
| 7 | Dead statement after `return` in `getStepLenString` | `:1632-1633` | Harmless, but a sign of unfinished editing |
| 8 | Trigger-feedback flags reset across the nudge `while` loop | `:1282-1439` | LED "triggered" feedback may misreport on heavily-nudged steps |

---

## 6. Improvement roadmap (suggested priority)

**Tier 1 — make what's there honest & correct (small, high value)**
1. **Wire or remove the dead features.** Either implement per-step CC output
   (`potVals` → `MM::sendControlChange` in `triggerStep`, with the Step/Fade `potMode`),
   monophonic playback (respect `monoPhonic` in `triggerStep`), `startstep`, and a FILL
   source (an AUX/key "fill" gesture that sets `fillActive_`) — or strip them from the
   UI so the sequencer doesn't advertise features it doesn't have.
2. **Fix the correctness bugs** #1–#4 above (one-line each), so shuffle/random cover the
   full track and copy/paste doesn't lose a lock.
3. **Version the save format** (#5): prefix a `formatVersion` byte and gate `loadFromDisk`
   on it (mirrors the header/EEPROM_VERSION pattern already used elsewhere). This unblocks
   *any* future data-model change without bricking saves.

**Tier 2 — deliver the vision's headline (medium)**
4. **A second machine type.** Euclidean is the cheapest to add and immediately proves the
   multi-machine promise (mix an OMNI melody lane with a Euclidean hat lane). Grids next,
   Tambola last (most novel/most work). Each is "implement `FormMachineInterface` + add to
   the `FormMachineType` enum + `kMachineNames`/`kMachineColors` + the `changeMachineAtIndex`
   switch."
5. **Finish the OMNI edit modes** the spec promised — especially **CHORD** and **DRUM**
   step entry (reuse the existing chord-mode and drum-mode key configs) and
   **MIDI-keyboard record**, and make CONFIG/MIX/LENGTH real distinct modes.

**Tier 3 — depth (larger)**
6. **Multi-track OMNI** (grow `tracks[1]` → N with a track-select gesture and a playback
   loop) — but weigh it against just using more machine slots; parallel machines may be
   the simpler answer.
7. **RAM hardening** (#6): replace the per-tick `std::vector`s with fixed-size arrays
   (max steps is known, notes cap at 6, shuffle length ≤ 64) to remove heap churn on the
   RP2040 / Teensy 3.1.
8. Remove `setTest()` and the dead commented blocks.

---

_Bottom line: the **architecture and the OMNI core are strong and ~80% there**; the gap
between it and the original vision is (a) the missing machine types, (b) a handful of
"looks done but does nothing" features, and (c) the collapsed UI-mode matrix. Tier-1 is a
day or two and makes it trustworthy; Tier-2 is what turns "a good step sequencer" into
"the FORM you designed."_
