# M8 Launchpad Pro control-surface — hardware findings

Probed 2026-09-13 against a **real Dirtywave M8** over its DIN MIDI ports (a "USB Midi" USB-MIDI
interface), driven from Web MIDI in the browser. The Launchpad control surface does **not** work
over the M8's own USB, only the hardware MIDI ports. These confirm the protocol our ML macro
implements and match what the iOS M8 app should do.

## Handshake

- Send the Launchpad Pro MK3 identity reply and the M8 begins streaming LED feedback:
  `F0 7E 00 06 02 00 20 29 23 01 00 00 00 01 00 00 F7`.
- Re-sending the identity **resets the M8 back to its Session view** — don't use it as a "refresh."
- The generic vlpp page uses a shorter identity (`… 20 29 00 00 …`); the MK3 form above works too.

## LED feedback (M8 → surface)

- Sent as **Note On**, channel 1, `note` = the button/pad, `velocity` = colour index; off is
  velocity 0 (or Note Off). This is exactly what our macro already caches and mirrors.
- **Ring buttons arrive as notes**, not CC: Shift 90, Session 93, Note 94, Seq 97, Project 98,
  Logo 99, the scene/side column 19 29 39 49 59 69 79 89, and track buttons 101-108.
- Grid pads are `row*10 + col`, row 1 bottom, col 1 left (11-88).
- Root/keyboard highlight colour observed = velocity **3**.

## Button presses (surface → M8)

- The M8 accepts a button press as **either a Note On or a CC** on the same number — tested with
  the Note view button (94): both a note press and a CC press switched the M8 to Note view.
- So our OMX sending the ring as CC (RING = CC default) is compatible. Grid pads we send as notes.

## Note view keyboard (button 94) — CONFIRMS K5 for chromatic

- With the M8 on **D Chromatic**, the keyboard lights roots (velocity 3) at grid pads:
  R8C3, R7C8, R6C1, R5C6, R3C4, R1C2.
- Solving `step = (r-1)*K + (c-1)` with roots a constant mod N: **K = 5, N = 12**. Each row up is
  a perfect fourth (5 semitones); roots recur every 12 steps. This matches the K5 we derived from
  the played-note report, on hardware, and means the auto-detect (root colour → lattice → K) will
  read K5 correctly here.

## Sequencer step editor — CONFIRMED on hardware

Verified 2026-09-13 with the M8 on a PHRASE screen (m8.run shows the display; its keyboard control
navigates the M8: arrows move, Left-Shift = the M8 "select" button, Space = Play, Z = Option,
X = Edit; screen-to-screen is select + a direction). The Launchpad Seq view (button 97) overlays
the phrase; the M8's own screen must already be on that phrase.

**Record must be ON.** Send the Rec button (10). Record lit = step editing is armed; Clear (60)
and Duplicate (50) also light while armed. Without Record, grid pads just audition notes.

**Regions of the 8×8 in the Seq view:**
- **Steps = the top-left 4×4** (rows 5-8, cols 1-4): pads 81 82 83 84 / 71-74 / 61-64 / 51-54.
  Order is top-to-bottom then left-to-right: `step = (8 - row)*4 + (col - 1)`, so step 0 = R8C1
  (pad 81) … step 15 = R5C4 (pad 54). A step that holds a note lights dim.
- **Keyboard = rows 1-4**, chromatic: col +1 = +1 semitone, row +1 = a perfect fourth (K5).
  Pad 11 (R1C1) is the lowest note. The currently selected note highlights.

**Gestures (Record on):**
- **Lock a note:** hold a step pad, then tap a keyboard pad → the note is written to that step at
  default velocity 64 (verified: step 9 → B-3 then C#4; step 12 → C#4). Holding the step also
  moves the M8's phrase cursor to it.
- **Velocity:** hold the step, tap the **side/scene column** (19 29 39 49 59 69 79) → those light
  up as the velocity ramp while the step is held.
- **Octave:** hold the step, tap **Down (70) / Up (80)** → both light while a step is held.
- Shift (90) also lights while a step is held (instrument-pool / other combos).

This matches the Seq v2 spec's model and maps cleanly onto the OMX: the 16 white keys = the 16
step pads (51-84 in that order), the top row = the keyboard (rows 1-4, K5), velocity mode = the
side column, octave = Up/Down, and the macro must send Rec first. Note our **current** Seq view
already targets these same step pads (top-left 4×4) and row-1 keyboard, so the pad math is proven.

Test residue: chain 11 / phrase 10 now holds C#4 at steps 9 and 12 (created during probing) —
clear or ignore as you like.
