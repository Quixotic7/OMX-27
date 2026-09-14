# ML Seq view v2 — step-hold editing (proposal)

Status: **BUILT & VERIFIED 2026-09-13**. NOTE, VEL and OCT modes all confirmed on a real M8
(see Build status at the end). Phrase view keeps the old keypad+phrase-slot layout; only Seq
view changed.

## Why

The current Seq view pins eight pitch pads on the black keys, so you get eight notes and no
velocity. The M8's own Launchpad sequencer works differently and better: you hold a step and the
grid becomes the editor for that step. This spec adopts that model, FORM-style, so the whole top
row is usable and we gain velocity and octave editing.

## Two hold orders (from hardware behaviour)

- **Modifier-first** — Clear / Duplicate: hold the modifier, then tap the step(s) it acts on.
- **Step-first** — Notes / Velocity / Octave: hold the step, then tap a note, a side-row ring
  CC (velocity), or an octave key.

The OMX tracks both: a held modifier (Clear/Dup) or a held step, one at a time.

## Modes (chosen while nothing is held)

The selected mode decides what a **held step** does. Selecting a mode never touches the step or
the AUX layer.

| Mode | Held-step top row does |
|---|---|
| NOTE (default) | keys 1-10 = ten consecutive in-scale notes → lock the note into the step |
| VEL | top row = the side-row ring CCs → set the step's velocity |
| OCT | top row = octave down / up → shift the held step's octave |

No step-edit gesture ever uses the AUX menu. AUX stays for view switch, transport and scroll,
used on their own.

## Key map

**White keys 11-26 = the 16 steps** of the current phrase (the M8's step block). Tap toggles a
step; hold to edit it in the selected mode.

**Top row (keys 1-10), nothing held (idle):**

| Key | Function |
|---|---|
| 1 | Clear (modifier: hold, then tap step(s) to clear) |
| 2 | Duplicate (modifier: hold, tap source step, tap destination) |
| 3 / 4 / 5 | Mode select: NOTE / VEL / OCT (active key bright) |
| 6-10 | Reserved for shortcuts (e.g. step mute, note-off, nudge) — TBD |

**Top row, a step held, per mode:**

- **NOTE:** keys 1-10 = ten consecutive in-scale notes, exactly the Notes-view mapping (K /
  auto-K). Keys 9-10 spill into grid row 2, which is fine — the M8's note-lock keyboard spans
  rows 1-4. Pressing a key locks that note into the held step.
- **VEL:** keys map to the side-row ring CCs (scene column). Pressing sets the step's velocity.
  Level count and exact CCs to confirm on hardware (expected 8: CC 19,29,…,89).
- **OCT:** keys send the M8 octave shift (LPP Up 80 / Down 70) applied to the held step.

## Hold logic (OMX state)

- `heldStep_` — the step pad currently held (0 = none). Set on white key-down when no modifier
  is held; its pad note is kept on; cleared and released on key-up.
- `heldModifier_` — Clear (60) or Dup (50) held from the idle top row; kept on until key-up.
- While `heldStep_` is set, the top row is remapped to the selected mode and each top-row tap is
  sent as that mode's pad/CC on top of the still-held step note.
- While `heldModifier_` is set, step taps are sent with the modifier held (modifier-first).
- One step and one modifier at a time; this matches the M8 and keeps the state small. Same shape
  as the existing `trackHeld_` / `clipMuteChord_` code.

## LEDs

- Steps 11-26 mirror the M8 (lit = active step, playhead as the M8 sends it).
- Idle top row: Clear/Dup fixed colours; mode selectors 3/4/5 dim, active one bright; shortcuts
  as added.
- Step held: top row shows the mode's palette — NOTE mirrors the M8 keyboard/roots, VEL a level
  ramp, OCT up/down. All driven from the mirrored M8 LEDs where possible.

## Hardware probe — DONE 2026-09-13 (see ML-M8-PROTOCOL-FINDINGS.md)

Confirmed on a real M8 over its DIN MIDI ports:

1. **Record must be on** first (send Rec = 10); without it grid pads only audition.
2. **Steps = the top-left 4×4** (pads 81-84 / 71-74 / 61-64 / 51-54), step 0 = R8C1,
   `step = (8-row)*4 + (col-1)`. Hold a step pad to select it (cursor follows, step lights).
3. **Note-lock** — hold a step, tap a **keyboard pad (rows 1-4, chromatic K5)**; the note writes
   to the step at default velocity 64. So the ten-note NOTE mode works.
4. **Velocity** — hold a step, tap the **side column (19 29 39 49 59 69 79)** = the level ramp.
5. **Octave** — hold a step, tap **Down 70 / Up 80**.
6. Buttons register as either note or CC; LED feedback is note-on, velocity = colour.

Our current Seq view already targets these exact step pads and the row-1 keyboard, so the pad
math is proven; v2 is the interaction layer on top.

## Build notes

- Reuse `notesPadForKey()` (with K / auto-K) for NOTE mode.
- Velocity: send the side-column ring CCs; add a small level→CC table once (2) is confirmed.
- Flash: Teensy 3.2 is at 97.8% (~5.8 KB free). Stub the hold logic and modes first and check
  the build fits before finishing; trim elsewhere if needed.
- Keep the current Seq view behind the change until the probe passes and the layout is approved.

## Build status (2026-09-13)

Implemented in `midimacro_m8v2.cpp`/`.h`: `seqMode_` (NOTE/VEL/OCT), `seqHeldStep_`,
`seqTopNotePad()`, the two hold orders, the mode-select top row, and the split from Phrase view.
Builds on all targets (teensy31 98.1%). Flashed and tested against the real M8:

- **NOTE mode — CONFIRMED.** Hold a step (white key) + tap a top-row note → the note locks into
  that phrase step. Verified: locked B-3 into step 2 on the M8.
- **VEL mode — CONFIRMED (2026-09-13, second probe).** Hold a step *that already holds a note*,
  then tap a top-row key (1-8 → side column CC 19,29,…,89) → the step's velocity changes. The
  side column is a ramp: key 1 (CC19, bottom) drives velocity low, key 8 (CC89, top) drives it
  high; taps near an end nudge toward the cap. Verified on step 2: V went 0x64→0x74→0x7F with
  key 8, then to 0x0A with key 1. **Key insight:** the M8 shows velocity in *hex* and its default
  is 0x64 (100). The earlier "no change" was reading hex as decimal and/or holding an empty step
  (velocity only edits a step that has a note).
- **OCT mode — CONFIRMED (2026-09-13, second probe).** Hold a step with a note, then tap a top-row
  key (1-5 → LPP Down 70, 6-10 → LPP Up 80) → the held step's octave shifts. Verified on step 4:
  F-3 → F-4 (up), F-4 → F-3 → F-2 (down).

**Integration nuance found:** the M8's Launchpad only enters step-edit mode for a phrase after it
receives the Seq button (97) *while the M8 is on that phrase*. If the M8 screen is on the phrase
but 97 was sent earlier (e.g. while on Song), the pad-holds select tracks instead of steps —
re-send Seq (switch the OMX away and back to Seq) to arm step editing. In normal use the
follow-view feature covers this (double-tap a pad → M8 jumps to its sequencer → OMX follows).
Record must be armed (AUX+8/9). CC vs NOTE for the ring did not matter once seq-edit mode was armed.
