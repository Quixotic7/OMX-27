# FORM Sequencer — User Guide

_For OMX-27 owners who know the OMX but have never touched the FORM sequencer._

FORM is an **8-track polyphonic step sequencer** that lives as one of the OMX modes. Each
track has its own notes, timing, length, MIDI channel, and CC knobs, and you build patterns
by programming steps or by playing them in live. This guide walks you from "how do I even
get in" to the details of every screen.

---

## 1. Getting in and out

FORM is a normal OMX mode, so you reach it the usual way:

1. **Long-press the encoder** to open the OMX mode picker.
2. **Turn** the encoder until **FORM** is selected.
3. **Click** the encoder to enter it.

To leave, long-press the encoder again and pick another mode.

The two leftmost keys and the encoder are your main controls; the rest of the keybed is the
**16-step row** (bottom) and the **top row** of 8 keys.

---

## 2. The one idea that unlocks everything: the encoder has two states

Almost every screen in FORM is driven by the encoder, which is always in one of two states:

- **SELECT** (the default): turning the encoder **moves a cursor** — between params, pages,
  tracks, or menu items. Nothing changes value.
- **EDIT**: turning the encoder **changes the selected value**.

Two ways to switch:

- **Click the encoder** to toggle SELECT ⇄ EDIT (it stays until you click again).
- **Hold AUX** for *temporary* EDIT — turn while holding AUX to change a value, release AUX
  and you're back in SELECT. (This is the exact behavior of the main MIDI-keyboard mode.)

A selected cell/param is drawn **boxed**; while you're editing it, it's **inverted**.

**Cell glyphs** (used across FORM's param grids):
- ☐ *(empty checkbox)* = **off** · ☑ *(checked)* = **on** — for toggles like Triplet, Mono,
  Mute, Solo, Lock.
- **↗-box** (`@`) = clicking this cell **opens a submenu** (Pot Config, Quantize).
- **✕** (`µ`) = clicking this cell is a **destructive action** (Clear track).
- A **checkerboard-dimmed** cell is **inactive** — it doesn't apply in the current state and
  editing it does nothing (e.g. ROOT/SCALE on a chromatic track, LOCK/GROUP with no scale
  on).

**Groups:** views organize their pages into named groups — crossing into a group with the
encoder pops its name once (e.g. "TRACK" in Mix, "TRACK SETUP" / "STEP" in Seq, "STEP
LOCKS" / "ACTIONS" in Notes).

---

## 3. Seven views, switched from the AUX layer

FORM has seven screens ("views"). Switch by **holding AUX** and tapping a key on the **bottom
step row** — the view name previews while AUX is held and commits when you **release AUX**:

| Hold AUX + key | View | What it's for |
|---|---|---|
| **13** | **MIX** | Levels, mutes/solos, per-track CC knobs, track params |
| **14** | **STEP** (SEQ) | Program the steps of a track |
| **15** | **TRANSPOSE** | A per-step transpose lane |
| **16** | **NOTES** | A focused chord/notes editor |
| **17** | **PATTERNS** | Pick/queue patterns, set switch style, copy/paste |
| **18** | **MI** | A playable keyboard + live recording |
| **19** | **TOOLS** | One-shot pattern tools (rotate, euclid, grids, …) |

(Keys 13–19 are the 3rd–9th keys of the bottom row.)

Views **remember their menu position** across switches. **Double-tap** a view key (still
holding AUX) to also send that view **back to its first page** — "HOME" pops to confirm.

---

## 4. The transport (AUX layer)

Hold **AUX** and use the two function keys / rec key:

- **AUX + F1 (key 1)** — Play / Pause (pause keeps the playhead where it is)
- **AUX + F2 (key 2)** — Reset the playhead to the start
- **AUX + F1 + F2 together** — **STOP** (stop *and* reset), in any order
- **STOP again while already stopped** — **KILL**: flushes every sounding note and sends
  All-Notes-Off / All-Sound-Off on every track's channel. The panic gesture — if anything
  is ever stuck ringing on a synth, stop twice.
- **AUX + F1 (hold) while stopped** — STOP (reset after a pause, without the chord)
- **AUX + key 3 (tap)** — toggle **Rec Arm** (records while playing)
- **AUX + key 3 (hold)** — open the **Clear** confirm (clears the current track's pattern)
- **AUX + key 4** — toggle record mode: **Overdub** vs **Replace**
- **AUX + keys 11 / 12** — Octave down / up

Stop is a pause, so the page playhead stays visible on screen even when stopped. Play,
Reset and Rec-Arm fire when you **release** the key (that's what lets the STOP chord never
misfire a Play on the way in).

---

## 5. Function keys inside a view (F1 / F2 / F3)

Without AUX, the two leftmost playable keys are momentary shortcuts *within the current view*
(**except MI**, where the whole keybed plays notes — no F-keys there):

- **F1** = hold **key 1**
- **F2** = hold **key 2**
- **F3** = hold **key 1 + key 2** together

What they do is view-specific, but the pattern is consistent across the Seq/Notes/Tools views:

- **Hold F1** — page tools: shows the pages; **double-tap** a page key to solo it, or **hold
  two** page keys to set a loop range.
- **Hold F2** — **top row** = track select (tap a key 3–10 to jump tracks); in the Seq view
  the **low row** is the step **pick-up/drop tool** (see §8).
- **Hold F3** — **rate & length**: set the track's step rate and the active page's length.
  Also: **F3 + turn the encoder = tempo** and **F3 + tap AUX = tap tempo**, from any view
  with F-keys — the BPM pops as you turn or tap. (Both also live in the Tools → BPM tool.)

---

## 6. How a track is built

Each of the **8 tracks** is an independent polyphonic step sequencer:

- **Steps** — up to **64** per track, shown 16 at a time.
- **Pages** — a track has up to **4 pages** of 16 steps. Pages can be **different lengths**
  (polymeter), and you can enable/disable them. Set page length with **hold F3**.
- A **step** holds: up to **6 notes** (chords), **velocity**, **nudge** (micro-timing, early
  or late within the step), **length** (fractional step up to several bars), **ratchet**
  (repeat — retrigger the step 2–4× within its slot), **probability / conditions**, and **5
  P-Lock slots** (per-step CC values).
- Each track also has a **scale mode**, set on any SCALE page's **MODE** cell:
  - **GL** (global) — follows the shared global scale (the default).
  - **CH** (chromatic) — ignores scales entirely; ROOT/SCALE render dimmed and are inert.
    Perfect for drum tracks.
  - **LO** (local) — the track carries its **own root and scale**, independent of the
    global one. Melodic tracks can each live in their own key.

  Mix freely: drums chromatic, a lead on a local scale, everything else global. The scale
  mode is **saved with the pattern**, so each of the 16 pattern slots remembers its own
  setup. (LOCK and GROUP remain **global** settings — they shape the live keyboard for
  whichever scale is in effect.)

---

## 7. MI view — play and record

MI is a **playable keyboard** (the step row plays scale notes) plus **live recording**. Notes
play on the **selected track's** MIDI channel and velocity.

### Recording

1. **Arm** recording (**AUX + key 3**, tap). The rec key glows red.
2. **Play**. If the transport is stopped, the first note you play **starts** playback and
   recording together.
3. Notes are captured **musically** — the sequencer resolves the nearest step, records the
   **nudge** (how early/late you played) and the **length** (how long you held).
4. **Overdub** layers onto existing steps; **Replace** (AUX + key 4) clears a step the first
   time you record into it on a pass.
5. Up to **8 notes can be held at once** while recording; past that the extras can't be
   captured — the screen pops **REC FULL** and the AUX key flashes red so a lost note is
   never silent.
6. Stop (AUX + F1 + F2) — rec-arm turns itself off.

### The MI menu (turn the encoder in SELECT to move through it)

- **Keyboard (page 0)** — in EDIT, the encoder **changes the selected track**. Along the
  bottom, small bars show each active page with a moving playhead.
- **SCALE** — the shared 5-cell scale page: **Mode / Root / Scale / Lock / Group**. MODE is
  this track's scale mode (GL / CH / LO — see §6); ROOT and SCALE edit the track's own
  scale when it's LOCAL, otherwise the global one. The same page appears in the Seq view,
  the Notes view, and the Mix machine menu, and behaves identically everywhere.
- **MIDI** — **Chan / Vel / Oct**. Velocity is the track's default (it also governs live
  play).
- **MACROS** — **Mcro** selects an AUX macro (Off / M8 / NRN / DEL) and **MPot** is a
  toggle (default **off**): whether a *selected* macro may take the pots in FORM — leave
  it off to keep the knobs on the track's CC bank (see the Macros note at the end).
- **ACTIONS** — **Quant** (@) morphs recorded nudges toward the grid live, click again to
  apply; **Clear** (µ) wipes the track's pattern (Yes/No); **Pots** (@) opens the
  CC-number editor.
- **CC** (last) — the track's 5 pot-bank knobs as bars, plus the **bank number**. Turning
  a knob sends its CC live; the **"CC" title** is selectable — click it to edit which CC
  number each knob sends (see §10).

---

## 8. STEP (SEQ) view — program the steps

This is the classic step programmer for the selected track.

- The **16-step row** is the current page. A lit step has notes; the green step is the
  playhead.
- **Hold one or more steps**, then use the **top row** as a value palette for the current
  **edit mode**: **Note, Velocity, Length, Repeat (ratchet), Chance, Math, Function,
  MIDI FX**. Tap a top-row key while no step is held to switch which edit mode is active.
- In **Note** mode, holding a step and tapping keys 1–10 builds its chord (auditioned while
  stopped).
- **Hold a step and turn the encoder** to fine-edit the current mode's value directly
  (velocity/chance sweep with acceleration; in Note mode the turn shifts the chord by
  semitones).
- **Turn the encoder** (nothing held) to walk the menu, organized in two groups. The
  **STEP group**: the step **param pages** (a 4-cell grid: Vel/Nudge/Len/MFX and
  Prob/Cond/Func/Accum), the **CC page** (5 CC bars + bank — hold a step + turn the
  encoder on a slot to write that step's CC P-Lock, click to clear it, click the "CC"
  title to edit CC numbers), and the per-step **Notes editor**. Then the **TRACK SETUP
  group** (its name pops as you cross): the **SCALE page** (Mode / Root / Scale / Lock /
  Group — see §6) and an **ACTIONS page** (Quant @ / Clear µ / Pots @ / **NTRY**).
- **NTRY** picks the note-entry feel for step programming (here and in Notes):
  **PR (Pressed)** — a fresh press *replaces* the held step's notes with the keys you're
  holding (hold several to write a chord). **TG (Toggle)** — each tap *adds* the note, or
  *removes* it if the step already has it (drum-style on/off). The setting is global and
  saved.
- On the **SCALE page** (Seq only), the top row is a **value palette** for the selected
  cell: **MODE** on keys 3/4/5 (GL/CH/LO) · **ROOT** on keys 3–9 (the seven natural notes
  C D E F G A B) · **SCALE** on keys 3–10 (the first 8 scale patterns) · **LOCK/GROUP** on
  keys 6 = off, 7 = on. The LEDs light the current value.
- On a **param page**, the top row becomes the **selected param's value palette** — every
  param has one, including Nudge (9 keys, zero in the middle) and Accum (5 keys). With
  steps **held** it sets those steps (and P-Locks them); with nothing held it sets the
  **track default**. Keys 1/2 join in via a *quick tap* (holding them stays F1/F2). The
  top-row **LEDs mirror the palette**, lighting up to the current value.
- Holding a step on a param page and turning the encoder writes that step's **P-Lock**;
  clicking the encoder clears it.

**Moving steps — the F2 pick-up/drop tool.** Hold **F2** and the low row becomes a
grab-and-place tool for whole steps (notes + all their locks):

- Your **first press** (nothing in hand) **CUTs** that step — you're now carrying it. This
  is the only press that will cut an *empty* step (so you can deliberately pick up
  "nothing" to use as an eraser).
- Carrying something, pressing an **empty** step **PASTEs** into it — grab here, drop
  there.
- Carrying something, pressing a **filled** step alternates **PASTE / CUT**: the first
  press **drops onto it** (replacing what was there), the next press on a filled step
  **picks that one up** — so you can keep grabbing and dropping in one F2 hold.
- **F1 + step** copies it into the same hand (non-destructive), ready to F2-drop copies
  anywhere. Releasing F2 empties your hand.

---

## 9. MIX view — the mixer and track controls

MIX is track-level control. **Hold a track key (3–10)** to reveal its low-row controls:
**Mute · Solo · play direction/mode · colour**. Turn a held track + knob 5 to set its colour.

**Track copy:** hold the source track's key, press **key 18** to arm — once = **"COPY
PAT TO?"** (pattern only: steps, pages, play mode, step defaults), twice = **"COPY ALL
TO?"** (settings and colour too). While still holding the track, **tap a destination
track key** to copy ("TRK n > m"); tap more keys to copy to several tracks. Release the
held track to cancel. Destructive on the destination, like the tools.

Turn the encoder (SELECT) to move through the MIX pages, in two groups ("MIX" / "TRACK"
pop as you cross between them):

- **MIX group — Overview** (EDIT-turn selects the track) and **LEVELS**, an 8-bar
  per-track **velocity mixer**; editing a bar sets that track's default velocity (pushed
  to every step without its own velocity lock).
- **TRACK group — TRACK grid** (Mute / Solo / Gate / Rate for the selected track) and the
  machine's full param menu: Length / MFX routing / an **FX ↗ cell** (click it to open the
  routed MidiFX group's editor — the menu front door to the chord/arp engine; the AUX-hold
  shortcuts still work too), then modes, MIDI, timings, scale, and an **ACTIONS**
  page (Quant @ / Clear µ / Pots @ / NTRY). Global **BPM** lives on the TIMINGS page. (The
  transpose params live in the **Transpose view**; the CC page lives in the **Seq view**,
  where steps can be held for P-Locks.)

---

## 10. Pot knobs, CC, and Pot Config

In FORM the **5 knobs are the selected track's pot bank** — each sends a mapped CC on the
track's channel. A track can use any of the banks; switch banks on the **CC page** (the big
bank number).

- The **CC page** (in Seq and MI) bars show each knob's last-sent value. Turn a knob to
  send + move its bar.
- **Which CC number** each knob sends is set in **Pot Config**. Open it from the **Pots @**
  cell on any ACTIONS page (Mix, Seq, Notes, MI) or by clicking the selectable **"CC"
  title** on the CC page. Editing there changes what the current bank's knobs transmit;
  press AUX or the Exit page to return.
- **P-Locks** (Seq): hold a step and move a knob (or the encoder on a CC slot) to lock a
  per-step CC value; it fires when that step plays.

---

## 11. PATTERNS, TRANSPOSE, NOTES, TOOLS

**PATTERNS** — 16 slots, each a snapshot of the **whole sequencer** (all 8 tracks' steps
and settings) — switch patterns to build song sections. Tap a slot to switch/queue it. **Switch style**
(keys 3–6) decides *when* a queued switch happens: **Finish Loop**, **Next Bar**, **Instant**,
or **Chained** (build a chain of patterns). A progress bar shows when the switch will land.
Slot operations (holding the modifier shows what the slots will do): **F1 + slot = copy**
it to the buffer · **F2 + slot = cut** a filled slot / **paste** into an empty one ·
**F3 + slot = clear** it. A *quick tap* of key 1/2 still copies/pastes the **active**
pattern.

**TRANSPOSE** — a 16-slot lane that transposes the track's notes per step (hold a step, pick a
value; the encoder covers the full ±48 range while the palette keys are quick 0–9 shortcuts).
Top-row extras: **key 8** randomizes the **values only** (keeps the lane length), **key 9**
randomizes **everything** (values *and* a new length — happy accidents), **key 10** clears
the lane.
Turning the encoder **past the lane's end** opens the track's **live-transpose params**:
**TPOS** (track transpose amount), **TYPE** (semitones, or scale-degree intervals that stay
in key), and **TPAT** (apply this lane to playback). Back off the first cell — or press any
key — to return to the lane. Per-step **Accum** (Seq param page 2) makes individual steps
*walk* through this lane over successive loops for evolving, Metropolis-style lines.

**NOTES** — a focused chord editor: pick a step, edit its up-to-6 notes as names or numbers,
with the scale params, the full step params ("STEP LOCKS" group), and an ACTIONS page on
the same encoder walk. With **record armed**, the view becomes a live instrument: the note
keys **play and record like the MI keyboard** (start-on-note included) instead of editing
the selected step; the keyboard and LEDs highlight the keys you're **holding** (white)
rather than the selected step's chord, and the bottom strip swaps the step markers for
the MI-style **page/playhead bars**. Disarm to go back to step editing.

**TOOLS** — **13 one-shot pattern operations** on the selected track. Turn the encoder to move
through the tools (a tool's name pops as you cross into it) — or jump straight to one:
**hold key 3** (it glows dimly to say it's live) and the low row lights up as a **tool map**;
tap a low-row key to jump to that tool. Each tool's page shows its params and **on-screen
action buttons** — fire a button with an **encoder click** on it, or with its key. Actions
are silent: the step row (and the step **LEDs**, which here light *only* actual triggers)
show the result immediately. F1/F2/F3 behave exactly like the Seq view.

**Shared conventions:**
- **SCOPE** (on most tools) — act on the **active page** or the **whole loop**. **Key 9
  toggles** page ↔ track (bright LED = track); the SCOPE cell on the page shows it too.
- **UNDO — key 10, in every tool.** Each destructive tool action snapshots the track
  first; key 10 restores it, and pressing again brings the action back (**redo**). One
  slot, last action only. The key glows while a restore is available; the slot dies when
  you switch patterns.
- **Key 7** fires the single action button (ROTATE uses **6 = left, 7 = right**; TRANSPOSE
  uses **5–8**).
- **Hold a low-row step** and the top row becomes the Seq view's editor for that tool —
  chord entry for most tools, the **velocity palette** in VEL RANDOM, the **chance palette**
  in CHANCE RND (the encoder edits the held value too).

**The tools**, in order:
- **ROTATE / MIRROR** — shift or reverse the steps (scoped).
- **PAGE** — a clipboard for whole 16-step pages: **COPY / CUT / PASTE** buttons on keys
  **6 / 7 / 8**, acting on the page F1 has selected. The buffer survives page and track
  switches, so it's how you move a page anywhere — even to another track. (COPY is
  deliberately the first button, so a stray encoder click never destroys anything.)
- **BPM** — the global tempo: the encoder edits it (40–300) and **TAP** (key 7, or a click
  on the button) is a tap tempo — the button flashes on each tap.
- **SHUFFLE** — randomly permute the steps (scoped).
- **HUMANIZE** — random micro-timing (nudge) within an amount %, on triggering steps.
- **QUANTIZE** — pull every nudge toward the grid by a % (the destructive twin of the MI
  menu's live quantize morph).
- **TRANSPOSE** — **OCT− / OCT+ / SEMI− / SEMI+** buttons (keys 5–8) on the scope's notes.
- **SCALE SNAP** — snap the scope's notes to the **track's effective scale** (chromatic
  tracks are left alone). Root/Scale are editable right on the page — they edit the same
  scale the snap will use, local or global.
- **VEL RANDOM / CHANCE RND** — a graphical page: a **min/max range bar** on top and **16
  per-step value bars** below, all selectable and encoder-editable. Filled bar = a step with
  notes, **outlined bar = a ghost trigger**, baseline tick = empty (editing an empty slot
  *creates* a ghost). Key 7 randomizes every step within the range.
- **EUCLID** — pulses / rotation / scope, with a **live pattern preview** (the Euclidean
  mode's own rendering) that updates as you turn — key 7 applies exactly what you see.
- **GRIDS** — a topographic drum generator (instrument BD/SD/HH/AC, X, Y, density), same
  live preview, hit velocities derived from the drum-map levels.

Generators stamp a middle-C on new on-steps and clear off-steps, so you get an audible pattern
to edit.

---

## 12. Saving

Save with the device's normal save gesture. What persists:

- **Every board** — the active pattern's 8 tracks, including each track's **scale mode /
  root / scale** (they're part of the pattern data), plus the **NTRY** note-entry
  preference.
- **RP2040 (V3)** — additionally the **whole 16-pattern bank** (each pattern remembering
  its own per-track scales), the switch style, quantize amount, and track colours, all to
  flash. The bank reloads at boot.
- **Teensy builds** — only the **active pattern** is saved (no filesystem for the bank).

---

## 13. Quick reference

**Encoder:** click = toggle Select/Edit · hold AUX = temporary Edit · Select turns navigate,
Edit turns change value.

**Views:** hold AUX + step key **13** MIX · **14** STEP · **15** TRANSPOSE · **16** NOTES ·
**17** PATTERNS · **18** MI · **19** TOOLS.

**Transport (hold AUX):** F1 play/pause · F2 reset · F1+F2 stop · **stop again = KILL (all
notes off)** · key 3 tap = rec-arm, hold = clear · key 4 = overdub/replace · keys 11/12 =
octave.

**In a view:** hold F1 (key 1) = pages (Seq: F1+step = copy) · hold F2 (key 2) = track select
on the top row (Seq: low row = step pick-up/drop) · hold F3 (keys 1+2) = rate & length.

**Tools:** hold key 3 + low row = **jump to a tool** · key 7 = apply (ROTATE 6/7 =
left/right · PAGE 6/7/8 = Copy/Cut/Paste · TRANSPOSE 5–8 = Oct−/Oct+/Semi−/Semi+) ·
key 9 = scope toggle · **key 10 = undo/redo** · encoder click on a button also fires it.

**Anywhere:** hold F3 + turn the encoder = **tempo** · hold F3 + tap AUX = **tap tempo**.

> **Heads-up on Macros:** an AUX macro (M8/NRN/DEL) can take over the pots. In FORM this is
> **off by default** — a selected macro leaves the knobs on the track's CC bank. If you *want*
> the macro to drive the knobs, turn **MPOT = On** (on the MI MACROS page). With
> MPOT off, the CC-page bars track the knobs normally even while a macro is selected.
