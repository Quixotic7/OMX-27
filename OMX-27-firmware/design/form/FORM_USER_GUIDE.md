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

---

## 4. The transport (AUX layer)

Hold **AUX** and use the two function keys / rec key:

- **AUX + F1 (key 1)** — Play / Pause (pause keeps the playhead where it is)
- **AUX + F2 (key 2)** — Reset the playhead to the start
- **AUX + F1 + F2 together** — **STOP** (stop *and* reset), in any order
- **AUX + key 3 (tap)** — toggle **Rec Arm** (records while playing)
- **AUX + key 3 (hold)** — open the **Clear** confirm (clears the current track's pattern)
- **AUX + key 4** — toggle record mode: **Overdub** vs **Replace**
- **AUX + keys 11 / 12** — Octave down / up

Stop is a pause, so the page playhead stays visible on screen even when stopped.

---

## 5. Function keys inside a view (F1 / F2 / F3)

Without AUX, the two leftmost playable keys are momentary shortcuts *within the current view*:

- **F1** = hold **key 1**
- **F2** = hold **key 2**
- **F3** = hold **key 1 + key 2** together

What they do is view-specific, but the pattern is consistent across the Seq/Notes/Tools views:

- **Hold F1** — page tools: shows the pages; **double-tap** a page key to solo it, or **hold
  two** page keys to set a loop range.
- **Hold F2** — track select / cut-paste: tap a top-row key to jump tracks.
- **Hold F3** — **rate & length**: set the track's step rate and the active page's length.

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
5. Stop (AUX + F1 + F2) — rec-arm turns itself off.

### The MI menu (turn the encoder in SELECT to move through it)

- **Keyboard (page 0)** — in EDIT, the encoder **changes the selected track**. Along the
  bottom, small bars show each active page with a moving playhead.
- **SCALE** — Root / Scale / Lock / Group (shared global scale).
- **MIDI** — **Chan / Vel / Oct / Macro**. Velocity is the track's default (it also governs
  live play). **Macro** selects an AUX macro (Off / M8 / NRN / DEL) — see the note in §11.
- **CC** — the track's 5 pot-bank knobs as bars, plus the **bank number**. Turning a knob
  sends its CC live; the **"CC" title** is selectable — click it to edit which CC number each
  knob sends (see §10).
- **QUANT / CLEAR / POTS / MPOT** — the first three are actions (click to open):
  **Quantize** pulls recorded nudges toward the grid by an amount you morph live; **Clear**
  wipes the track's pattern (Yes/No); **Pots** opens the CC-number editor. **MPOT** is a
  toggle (default **off**): whether a *selected* AUX macro is allowed to take the pots in
  FORM — leave it off to keep the knobs on the track's CC bank (see §11).

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
- **Turn the encoder** (nothing held) to walk the menu: the step **param pages** (a 4-cell
  grid: Vel/Nudge/Len/MFX and Prob/Cond/Func/Accum), then the per-step **Notes editor**,
  and finally a **POTS** action (click to open the CC-number editor).
- On a **param page**, the top row becomes the **selected param's value palette** — every
  param has one, including Nudge (9 keys, zero in the middle) and Accum (5 keys). With
  steps **held** it sets those steps (and P-Locks them); with nothing held it sets the
  **track default**. Keys 1/2 join in via a *quick tap* (holding them stays F1/F2). The
  top-row **LEDs mirror the palette**, lighting up to the current value.
- Holding a step on a param page and turning the encoder writes that step's **P-Lock**;
  clicking the encoder clears it.

---

## 9. MIX view — the mixer and track controls

MIX is track-level control. **Hold a track key (3–10)** to reveal its low-row controls:
**Mute · Solo · play direction/mode · colour**. Turn a held track + knob 5 to set its colour.

**Track copy:** hold the source track's key, press **key 18** to arm — once = **"COPY
PAT TO?"** (pattern only: steps, pages, play mode, step defaults), twice = **"COPY ALL
TO?"** (settings and colour too). While still holding the track, **tap a destination
track key** to copy ("TRK n > m"); tap more keys to copy to several tracks. Release the
held track to cancel. Destructive on the destination, like the tools.

Turn the encoder (SELECT) to move through the MIX pages:

- **Overview** (EDIT-turn selects the track).
- **LEVELS** — an 8-bar per-track **velocity mixer**; editing a bar sets that track's default
  velocity (pushed to every step without its own velocity lock).
- **CC** — the same 5-knob CC page as MI, **plus per-step P-Locks**: hold a low-row step and
  turn a knob (or the encoder on a slot) to lock that step's CC; click a slot to clear it. The
  **"CC" title** is selectable → click to edit the bank's CC numbers.
- **TRACK** — Mute / Solo / Gate / Rate for the selected track.
- **TRACK PARAMS** — the machine's full param menu (Length/MFX, modes, transpose, MIDI,
  timings, scale, and a **POTS** action). Global **BPM** lives on the TIMINGS page.

---

## 10. Pot knobs, CC, and Pot Config

In FORM the **5 knobs are the selected track's pot bank** — each sends a mapped CC on the
track's channel. A track can use any of the banks; switch banks on the **CC page** (the big
bank number).

- The **CC page** bars show each knob's last-sent value. Turn a knob to send + move its bar.
- **Which CC number** each knob sends is set in **Pot Config**. Open it from the **POTS** menu
  item (present in Mix, Seq, Notes, and MI) or by clicking the selectable **"CC" title** on
  the CC page. Editing there changes what the current bank's knobs transmit; press AUX or the
  Exit page to return.
- **P-Locks** (Mix/Seq): hold a step and move a knob (or the encoder on a CC slot) to lock a
  per-step CC value; it fires when that step plays.

---

## 11. PATTERNS, TRANSPOSE, NOTES, TOOLS

**PATTERNS** — 16 slots, each a snapshot of the **whole sequencer** (all 8 tracks' steps
and settings) — switch patterns to build song sections. Tap a slot to switch/queue it. **Switch style**
(keys 3–6) decides *when* a queued switch happens: **Finish Loop**, **Next Bar**, **Instant**,
or **Chained** (build a chain of patterns). A progress bar shows when the switch will land.
Copy/paste with the F1/F2 quick-tap + hold idiom.

**TRANSPOSE** — a 16-slot lane that transposes the track's notes per step (hold a step, pick a
value; the encoder covers the full ±48 range while the palette keys are quick 0–9 shortcuts).
Turning the encoder **past the lane's end** opens the track's **live-transpose params**:
**TPOS** (track transpose amount), **TYPE** (semitones, or scale-degree intervals that stay
in key), and **TPAT** (apply this lane to playback). Back off the first cell — or press any
key — to return to the lane. Per-step **Accum** (Seq param page 2) makes individual steps
*walk* through this lane over successive loops for evolving, Metropolis-style lines.

**NOTES** — a focused chord editor: pick a step, edit its up-to-6 notes as names or numbers,
with the scale params and full step params on the same encoder walk.

**TOOLS** — **11 one-shot pattern operations** on the selected track. Turn the encoder to move
through the tools (a tool's name pops as you cross into it); each tool's page shows its params
and **on-screen action buttons** — fire a button with an **encoder click** on it, or with its
key. Actions are silent: the step row (and the step **LEDs**, which here light *only* actual
triggers) show the result immediately. F1/F2/F3 behave exactly like the Seq view.

**Shared conventions:**
- **SCOPE** (on most tools) — act on the **active page** or the **whole loop**. Keys **9 =
  page, 10 = track**, everywhere; the 9/10 LEDs show the current setting.
- **Key 7** fires the single action button (ROTATE uses **6 = left, 7 = right**; TRANSPOSE
  uses **5–8**).
- **Hold a low-row step** and the top row becomes the Seq view's editor for that tool —
  chord entry for most tools, the **velocity palette** in VEL RANDOM, the **chance palette**
  in CHANCE RND (the encoder edits the held value too).

**The tools**, in order:
- **ROTATE / MIRROR / SHUFFLE** — shift, reverse, or randomly permute the steps (scoped).
- **HUMANIZE** — random micro-timing (nudge) within an amount %, on triggering steps.
- **QUANTIZE** — pull every nudge toward the grid by a % (the destructive twin of the MI
  menu's live quantize morph).
- **TRANSPOSE** — **OCT− / OCT+ / SEMI− / SEMI+** buttons (keys 5–8) on the scope's notes.
- **SCALE SNAP** — snap the scope's notes to the current scale (Root/Scale editable right on
  the page; the scale name pops as you change it).
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

Save with the device's normal save gesture. On the **RP2040 (V3)** the **whole pattern bank**
(all 16 patterns), the switch style, quantize amount, and track colours persist to flash; on
Teensy builds only the **active pattern** is saved. The bank reloads at boot.

---

## 13. Quick reference

**Encoder:** click = toggle Select/Edit · hold AUX = temporary Edit · Select turns navigate,
Edit turns change value.

**Views:** hold AUX + step key **13** MIX · **14** STEP · **15** TRANSPOSE · **16** NOTES ·
**17** PATTERNS · **18** MI · **19** TOOLS.

**Transport (hold AUX):** F1 play/pause · F2 reset · F1+F2 stop · key 3 tap = rec-arm, hold =
clear · key 4 = overdub/replace · keys 11/12 = octave.

**In a view:** hold F1 (key 1) = pages · hold F2 (key 2) = track select · hold F3 (keys 1+2) =
rate & length.

**Tools:** key 7 = apply (ROTATE 6/7 = left/right · TRANSPOSE 5–8 = Oct−/Oct+/Semi−/Semi+) ·
keys 9/10 = scope page/track · encoder click on a button also fires it.

> **Heads-up on Macros:** an AUX macro (M8/NRN/DEL) can take over the pots. In FORM this is
> **off by default** — a selected macro leaves the knobs on the track's CC bank. If you *want*
> the macro to drive the knobs, turn **MPOT = On** (the 4th cell on the MI actions page). With
> MPOT off, the CC-page bars track the knobs normally even while a macro is selected.
