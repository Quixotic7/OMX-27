# FORM — Redesign Proposal (v2 interaction model)

A proposed rework of FORM's interaction model, replacing the confusing **knob-mode
matrix** with a small set of **views** and **pot banks** (like MI mode). This is a
*design draft* to iterate on — nothing here is built yet. Companion frames:
[`form_redesign.json`](form_redesign.json). Current as-built behaviour lives in
[`FORM_DESIGN.md`](FORM_DESIGN.md).

## 0. FORM is now a single-engine, 8-track sequencer

The **switchable machine-type** concept is **dropped**. FORM is just an **8-track
sequencer**; **every track is the same engine** (the polyphonic step sequencer formerly
called "OMNI"). There is no machine picker, no NULL/EUCL/Grids/Tambola types, and no
type-switch gesture. This removes a whole layer of state and vocabulary (slot vs machine
vs type) — a track is a track.

> Implementation note: this retires `FormMachineType`/`kMachineNames`/`changeMachineAtIndex`,
> the SELECTMACHINE mode, and the EUCL machine — the `FormMachineInterface` indirection can
> collapse to one concrete sequencer. (The current firmware still has all of it; that's what
> `FORM_DESIGN.md` and `form_container/omni/euclid.json` document.)

_Decisions locked (Aug 2026): **single engine, 8 tracks** (no machine types); **6 views**
(Mix/Step/Transpose/Notes/Patterns/MI) on **AUX + 13–18**; Step view = **8 step-edit
modes** on keys 3–10 with a 10-key value palette; **pages / rec / transport on the AUX
layer**; **16 patterns**; **live recording**; **no pot-bank shortcut** on AUX (bank set in
the track menu); global 5-segment CC meter on the top OLED row._

---

## 1. What changes

| Area | Today | Proposed |
|---|---|---|
| Track engine | 8 slots, switchable machine types (OMNI/EUCL/…) | **one engine per track** (the OMNI step sequencer); **no machine types / no picker** |
| Knobs | Pot-mode matrix (K4 rate, K5 UI-mode, …) | **Pot bank** (5 knobs → 5 CCs), **per track**, like MI mode |
| Modes | K5 selects CONFIG/MIX/LENGTH/TRANSPOSE/STEP/NOTEEDIT | **6 views**: **Mix · Step · Transpose · Notes · Patterns · MI** |
| View switch | AUX + 13–18 (and AUX sometimes behaves differently) | **AUX layer identical everywhere**, on **AUX + 13–18**; the **current view flashes** |
| Zoom/pages | K1 page × K2 zoom (1/2/4 bar), 16-of-64 slice | **Always 16 steps**; **4 pages/track**, on the **AUX layer (keys 6–9)** |
| Per-step edit | step-hold palette (funcs) | **8 step-edit modes** on keys 3–10 (Note / Velocity / Step Length / Repeat / Chance / Math / Function / MIDI FX); hold a step → 10-key value palette on keys 1–10; hold a mode key → set its default on keys 11–20 |
| Copy / paste | buffer (steps only) | **F1 = Copy, F2 = Paste/Cut** — the same buffer logic in every view except Mix, at **step / track / pattern** scope; doubles as **undo** (§3.5) |
| CC locks | UI-only, never sent | **P-Locks**: hold a step + turn a knob → lock a CC, resolved through the track's pot bank |
| Step funcs | on the step-hold palette | back as the **Function** step-mode — same functions as the current OMNI |
| Playback range | (n/a) | **AUX page keys (6–9)**: click = select · double-click = solo one page · hold+press = loop range (§6); page **length** via F3 (§5) |
| Patterns | (n/a) | **patterns** (whole-sequencer snapshots) — the **Patterns view**; tap to switch, style-selectable (Finish Loop / Next Bar / Instant / Chained). One project, count RAM-limited (§4.5) |
| Live recording | (n/a) | **AUX + 3 = rec arm**; play the keyboard while running → notes quantize into the selected track (§7) |
| Live play | (n/a) | **MI view** — the MI-mode keyboard, as a 6th view, for live playing (§4.6) |
| Config | knob-mode matrix | **Track menu** (encoder): per-track settings up front, global (**BPM / clock / scale / root / swing / groove**) in back; first page a 2-octave mini-keyboard (§2.5) |

---

## 2. Knobs = pot banks (per track)

- The 5 knobs behave like MI mode: each **pot bank** maps K1–K5 to 5 CC numbers
  (`pots[bank][0..4]`, `NUM_CC_BANKS = 5`). Turning a knob sends that CC live.
- **Each track stores its own pot-bank index.** Selecting a different track (Mix view)
  remaps the knobs to that track's bank.
- **The pot bank is chosen in the track's parameter menu** (encoder), **not** on the AUX
  layer — AUX stays free of pot-bank shortcuts for now.
- No more knob-driven modes. Knobs only ever = pot bank.
- **CC meter (all views):** the **top 1-pixel row** of the OLED is split into **5
  segments**, one per knob, each drawing a horizontal line whose length = that knob's
  current CC value (0–127). The active bank's five live CC values are therefore always on
  screen — this is the persistent knob feedback (addresses "the knobs are invisible").
- **Page indicator (all views):** a persistent row of **4 short lines** near the top of
  the OLED — one per page — **1 px tall for a normal page, 2 px for the active page**. So
  page state is always visible, not only while holding AUX (addresses "page state is
  AUX-only").

---

## 2.5 The track menu — all config lives here

The **encoder** opens/drives a **menu of param pages** (the same param model the current
FORM uses). It is the config surface for everything that isn't a per-step edit:

- **Front pages = the selected track's settings** shown up front (MIDI channel, output,
  pot bank, rate, etc.). The **first page is a 2-octave mini-keyboard**.
- **Back pages = global settings:** **BPM**, **clock** (internal / external), **scale**,
  **root**, **swing**, **groove**.
- Layout mirrors the current build of FORM (track params front, global in back).

This replaces the old knob-mode matrix as the home for BPM/scale/etc., and is what "the
track's parameter menu" (§2) refers to.

---

## 3. P-Locks (per-step CC locks)

- **Gesture:** hold a step key (11–26), then **turn a knob**. That knob-slot's value is
  **locked** on that step.
- **Resolution at playback:** when the step fires, each locked knob-slot sends
  `CC = pots[trackBank][slot]` with the locked value. The *slot* is locked, the *CC
  number* follows the track's current pot bank:
  - Bank 1 maps K1→CC26 ⇒ a lock on K1 sends **CC26**.
  - Switch the track to Bank 2 (K1→CC33) ⇒ the same lock now sends **CC33**.
- A step carrying any P-Lock is tinted **MAGENTA `#ff00ff`** in the step row so locks are
  visible. While the step is held, the OLED lists the locked slots/values (resolved to
  their current CC #s).
- Finally makes the long-dead `Step::potVals[5]` model do something
  (`DESIGN_NOTES.md §4b`).

---

## 3.5 Copy / paste / undo (one buffer model, everywhere)

**F1 = Copy, F2 = Paste/Cut** — the same logic the current OMNI already runs, applied
identically to **steps, tracks, and patterns** (and it's the only thing F1/F2 do outside
Mix, §1). One buffer:

- **F1 copies** the selected item into the buffer (non-destructive).
- **F2 pastes** the buffer into the selected item. Right after a copy, F2 always pastes.
- Selecting an item that **has content** and hitting F2 **cuts** it (moves it to the
  buffer, clearing the source); the **next** item you select + F2 **pastes**; hitting it
  again **cuts**. It toggles cut ⇄ paste as you move around.
- **Empty items are never loaded into the buffer** — so once something is in the buffer it
  stays until you copy/cut something else. (Example: only step 1 has notes → cut step 1 →
  paste it back into step 1 → now keep F2-pasting it into the other 15 empty steps.)
- **This is also the undo:** any **clear/cut goes into the buffer**, so **F2 pastes it
  back**. There is no separate undo stack.
- Scope follows the view: **Notes** = step copy/paste between steps; **Step** = step
  clear/cut/paste; **Patterns** = pattern copy/paste; **Mix** = whole-track (F1/F2 are
  mute/solo in Mix, so track copy/paste is **hold a track → 25 = copy · 26 = paste**).

---

## 4. The six views

The **top row (keys 3–10)** changes meaning per view; the **step row (11–26)** is always
the current 16 steps; **knobs are always the pot bank**; the **AUX layer is always the
same** (§6). The **current edit page is shared** across the editing views (Step /
Transpose / Notes) and selected on the AUX layer. The six views are **Mix · Step ·
Transpose · Notes · Patterns · MI**.

### 4.1 Mix  _(≈ today's base view)_
Mix is for **mixing / performance**, not step editing.
- **Keys 3–10 = the 8 tracks** (per-track `seqColors`; selected WHITE, muted RED, empty
  dim, fired-this-step INDIGO). **Tap** = select · **double-click** = enter Step view ·
  **hold** = per-track controls on the low row (below).
- **Low row (11–26) = the selected track's pattern.** Tapping a step **plays that step's
  programmed notes** — you're jamming the *sequence* (triggering the steps you wrote), not
  a chromatic keyboard. It does not open the note editor. (For live chromatic playing, use
  the **MI view**, §4.6.)
- **F1 = Mute, F2 = Solo** (modifiers): hold **F1 + tap a track** = mute; hold **F2 + tap a
  track** = solo. Hold **F1 + tap a step** = toggle that single step's mute.
- **F3 (hold F1+F2) = track rate:** top row 3–10 = rate options for the selected track;
  the low row mirrors the per-track controls.
- **Hold a track → per-track controls on the low row:** **11 = Mute · 12 = Solo · 14–18 =
  play mode** — 14 forward · 15 reverse · 16 forward-pong · 17 reverse-pong · **18 random-
  page** ("random-page" plays each *enabled* page of the pattern in random order) · **25 =
  copy track · 26 = paste track** (the §3.5 buffer). (13 free.)

→ `form_redesign.json` **state 1** + `form_mix.json` (hold-track & rate detail).

### 4.2 Step  _(per-track step editing — 8 hold-modes)_
Locked to one track. The **top row (keys 3–10) selects one of 8 step-edit modes**; the
active mode is lit bright, the others dim:

| Key | Mode | Edits, per step |
|---|---|---|
| 3 | **Note** | the note(s) |
| 4 | **Velocity** | hit velocity |
| 5 | **Step Length** | gate length |
| 6 | **Repeat** | retrigger / ratchet count |
| 7 | **Chance** | trigger probability |
| 8 | **Math** | **conditional trig** (A:B ratio + Fill/!Fill) |
| 9 | **Function** | step function (RSET / jump / reverse / …) |
| 10 | **MIDI FX** | per-step MIDI-FX routing |

**Editing in the active mode:**
- **Single-click a step (11–26) = clear it.** The cleared step goes to the copy buffer, so
  **F2 pastes it back** if you didn't mean it (§3.5). _(Double-click is no longer used —
  it was axed to remove the click-vs-hold collision.)_
- **Hold a step (11–26)** → the **top row keys 1–10 become the value palette** for the
  current mode; tap one to set that step's value. **Press AUX while holding = reset that
  step to the mode's default** (AUX has no other job while a step is held). Turning a
  pot-bank knob while holding still lays a CC P-Lock (§3).
- **Hold a mode key (3–10)** → **keys 11–20 become the value palette for the mode's
  DEFAULT** value (what steps use until given an explicit value); tap one to set it.
- **F1 / F2 = copy / paste the selected step** (§3.5). To reach the Notes chord editor,
  switch to the **Notes view** (AUX + 16).

**The value palette (keys 1–10) per mode:**
- **Note** — 10 notes: **chromatic** = 10 notes from **C** in the current octave;
  **scale on** = the current scale's notes only.
- **Velocity / Step Length / Chance** — the 10 keys span the value range.
- **Repeat** — **1 · 2 · 3 (quick triplet) · 4** (ratchet count).
- **Math (conditional trig)** — **key 1 = Fill · key 2 = !Fill**; **keys 3–6 = ratio A**
  (1–4) · **keys 7–10 = ratio B** (1–4). E.g. key 3 + key 10 = **1:4** (fires 1 of every 4
  loops).
- **Function** — the **current OMNI step functions** (RSET / reverse / forward / pong /
  rand-jump / rand / jump-to-step). **MIDI FX** — the track's MIDI-FX slots.

**Pages** are on the **AUX layer** (keys 6–9 — select / solo / loop-range, §6);
**F3 (F1+F2) = structure layer** sets **page length** (tap steps, §5).

→ `form_redesign.json` **states 2, 3, 4** + `form_step.json` (Math + Velocity palettes).

### 4.3 Transpose
- Unchanged from today's transpose editor: step row = transpose pattern
  (`TZERO/THIGH/TLOW`), top keys = randomize/clear/copy shortcuts. Operates on the
  current page.

→ `form_redesign.json` **state 7**.

### 4.4 Notes  _(chord/note entry — reworked for in-editor step nav)_
Enter via the **AUX view selector (AUX + 16)** — the double-click-a-step shortcut is gone
with the Step-view double-click. The view lets you **step through and edit without leaving
the editor**:

- **Keyboard starts at F4 (key 15).** The playable piano is now **keys 3–10 (sharps) +
  15–26 (naturals)** = F4→C6 (~1.5 octaves). The sharps 3–10 line up as the black keys
  for the naturals 15–26, so it reads as a real keyboard. (Octave shift = AUX + 11/12.)
- **Keys 1 / 2 = F1 / F2 = copy step / paste step.**
- **Keys 11 / 12 = prev / next step**, **one step per click** (no auto-repeat).
  **Key 13 = (unused for now)** · **Key 14 = clear step**.
- **F3 (hold F1+F2) = jump-to-step:** while held, the low row (11–26) becomes a 16-step
  selector for the current page — the current step flashes; tap a key to jump there.
- **Live audition:** when transport is **stopped**, pressing/holding the piano keys
  **sends MIDI note-on while held** (note-off on release) so you hear what you're
  entering. (While playing, key presses edit the step without extra audition.)
- **Encoder still changes the step** too (unchanged).
- **Step-change feedback:** no persistent cursor. On any step change (11/12, F3-jump, or
  encoder) the new step's stored notes **flash briefly, then settle** — a quick preview
  of what's on the step you landed on.
- **Change page** while here: via the **AUX layer** (keys 6–9), like everywhere else (§6).
- **OLED:** above the keyboard render, a strip of **16 small boxes** (one **filled** =
  the step being edited) and, to its right, the **page number** (1–4). (Plus the global
  CC meter + page indicator, §2.)

→ `form_redesign.json` **state 6** (and `form_notes.json` for the detail).

### 4.5 Patterns
A **pattern** is a snapshot of the whole sequencer — all 8 tracks' step data plus their
settings (pot bank, play mode, pages/length, rate). Switch between patterns to build
sections (verse / chorus / fill). **One project only** (the device has no room for
multiple), and the **pattern count is RAM-limited** — up to 16 on the low row, but may end
up fewer depending on what fits (see §10).

- **Low row (keys 11–26) = the pattern slots.** Current pattern = WHITE; patterns with
  content = dim color; queued = blinking; empty = off. **Tap a slot to switch.**
- **Top row keys 3–6 = switch style:** **3 Finish Loop · 4 Next Bar · 5 Instant · 6
  Chained** (active style lit bright). This governs *when* a tapped pattern takes over —
  at loop end, at the next bar, immediately, or appended to a chain.
- **F1 / F2 = copy / paste** a pattern (the §3.5 buffer — cut also clears, so it's undo).
  _(Pattern **chaining** / song mode builds on the "Chained" style — see §10.)_

→ `form_patterns.json`.

### 4.6 MI  _(live-play keyboard)_
The same **keyboard layout as the standalone MI OMX mode**, brought in as a view purely
for **live playing** — jam over the running sequencer without touching the pattern.

- Full piano across keys **1–26** (scale-aware colors: roots periwinkle, in-scale dim
  blue, out-of-scale off; pressed keys white). Octave = AUX + 11/12.
- Plays the **selected track's** channel/pot-bank; with **REC armed** (§7) it also records.
- No step editing here — it's the performance keyboard. It's the **only chromatic
  keyboard for live play**: Notes-view is for chord *entry* into steps, and the Mix low-row
  triggers the *programmed steps* (not chromatic).

→ `form_mi.json`.

---

## 5. Pages, playback range & length

- **4 pages per track, 16 steps each** (64 total), always one page on screen — no zoom.
- **Pages live on the AUX layer (keys 6–9)** — select / solo / loop-range and the full
  color scheme are in **§6**. Single-click = select the edit page; double-click = solo/loop
  just that page; hold page A + press page B = loop the range A–B. _(Per-track **play
  mode** — forward / reverse / pong / **random-page** — is set in Mix, §4.1; random-page
  shuffles the enabled pages.)_
- **Page length (1–16)** is set in the **F3 structure layer** (hold F1+F2 in Step view,
  tap step keys). Steps beyond the length go dark; each page can be a different length
  (polymeter).

→ `form_redesign.json` **state 5** (F3 length) + `form_aux.json` (page keys).

---

## 6. The unified AUX layer (identical in every view)

Holding **AUX** always shows the same control layer, and **the current view's key
flashes**:

| AUX + key | Action |
|---|---|
| **1** | Play / Pause |
| **2** | Reset |
| **3** | **Record arm** (toggle; red when armed) — §7 |
| **4** | **Rec mode** — overdub / replace |
| **6 / 7 / 8 / 9** | **Pages 1–4** (select / solo / loop-range — see below) |
| **11 / 12** | Octave − / + |
| **13 / 14 / 15 / 16 / 17 / 18** | Switch view → **Mix / Step / Transpose / Notes / Patterns / MI** (current **flashes**) |

Free: 5, 10, 19–26. No pot-bank shortcut here (bank is a menu setting, §2). **MIDI-FX is
no longer on AUX** — it's a Step-view mode now (§4.2).

**Page keys (6–9) — select / solo / loop-range** (there is **no separate page-mute** — a
page is "muted" simply by not being in the enabled set):
- **Single-click** a page = make it the **active edit page** (what the views show/edit);
  doesn't change what plays.
- **Double-click** a page = **enable only that page** — it plays, the others go muted.
  (e.g. double-click page 2 → only page 2 plays.)
- **Hold page A, press page B** = **enable the range A–B**, the rest muted; the **first
  (held) page is the active** one. (e.g. hold page 2, press page 4 → pages 2–4 play, page
  1 muted.)

**Page-key colors:**
| State | Color |
|---|---|
| Active/selected page | **GREEN** (bright) · **RED** if that page is muted |
| Page enabled in the playback loop | **BLUE** |
| Muted page | **very low brightness** |
| Currently-playing page | **flashing YELLOW** (flashes green/red if it's also the selected page) |

→ `form_redesign.json` **state 8** + `form_aux.json` (fully-labeled layout).

---

## 7. Live note recording

Play notes into the sequencer while it runs.

- **Arm** with **AUX + 3** (latching; the key is red while armed). A REC indicator shows on
  the OLED.
- **While armed + playing,** notes you play — on the **MI view** or the **Notes-view
  keyboard** — are **recorded into the selected track**, **quantized to the nearest step**
  on the current page. Multiple notes on one step = a chord.
- **Rec mode (AUX + 4): overdub** (default — adds to a step's existing notes) or
  **replace** (overwrites the step).
- Live audition still sounds each note as you play it (§4.5 audition also works when
  stopped, for auditioning without recording).
- Recorded **velocity** = the track's current default (the OMX keys aren't velocity-
  sensitive) — tweak later in Velocity mode.
- Open: **count-in / metronome**, and whether a **quantize-off** (free-timed) record is
  wanted — see §10.

---

## 8. Always-16-step view

The K1/K2 page×zoom slice is gone. The grid always shows exactly 16 steps = one page.
Longer material comes from the 4 pages (§5), not from zooming a 64-step lane.

---

## 9. Color proposals

| Meaning | Color | Hex |
|---|---|---|
| Step has notes | LTBLUE | `#a8a8ff` |
| Step empty (in length) | DKBLUE | `#00004d` |
| Step has a P-Lock | MAGENTA | `#ff00ff` |
| Playhead (firing / silent) | WHITE / RED | `#ffffff` / `#ff0000` |
| Beyond page length | off | `#000000` |
| Step modes (Note/Vel/Len/Repeat/Chance/Math/Func/MFX), active bright / dim | ORANGE YELLOW GREEN CYAN BLUE PURPLE MAGENTA ROSE | `#ff8000 #ffff00 #00ff00 #00ffff #0000ff #7f00ff #ff00ff #ff0080` |
| Value palette: available / current-value / root | dim-blue / LTYELLOW / periwinkle | `#000090` / `#ffff80` / `#a2a2ff` |
| View (AUX): current (flashing) / other | WHITE / LOWWHITE | `#ffffff` / `#202020` |
| AUX page key: selected / in-loop / muted / playing | GREEN / BLUE / very-dim / YELLOW(flash) | `#00ff00` / `#0000ff` / `#101010` / `#ffff00` (RED `#ff0000` if selected+muted) |

---

## 10. Resolved decisions

1. **Single engine** → no machine types; 8 tracks, all the same sequencer (§0).
2. **View-select keys** → **AUX + 13–18** (Mix/Step/Transpose/Notes/Patterns/MI); **no
   pot-bank shortcut** on AUX (bank set in the track menu).
3. **Per-page length** → **F3 (F1+F2) + tap a step** in Step view (§5).
4. **Notes step nav** → F1/F2 = copy/paste step; **11/12 = prev/next step**; 14 = clear;
   F3 = jump-to-step; **live MIDI audition** on key-hold when stopped (§4.5).
5. **CC meter** → 5-segment line meter on the top OLED row, every view (§2).
6. **Step view = 8 hold-modes** on keys 3–10 (Note / Velocity / Step Length / Repeat /
   Chance / Math / Function / MIDI FX). Hold step → value palette 1–10; hold mode key →
   default 11–20; AUX-while-holding-step = reset to default; **single-click a step = clear
   it** (→ buffer); **double-click removed** (§4.2). Repeat = 1/2/3-triplet/4; Function =
   the current OMNI functions.
7. **Math** = **conditional trig**: key 1 = Fill, key 2 = !Fill, keys 3–6 = ratio A (1–4),
   keys 7–10 = ratio B (1–4) → e.g. 3+10 = 1:4.
8. **Pages** → on the **AUX layer** (AUX + 6–9): click = select · double-click = solo one
   page · hold+press = loop range; **muting is implicit** (out-of-loop = muted). Colors
   green/blue/very-dim/yellow (§6).
9. **Mix = performance view** (§4.1): low row triggers the programmed steps; F1 = mute, F2
   = solo, F3 = track rate; **hold a track** → mute/solo/play-mode + **25 copy / 26 paste
   track** on the low row; double-click a track = enter Step.
10. **Copy / paste / undo** → one buffer, **F1 = Copy, F2 = Paste/Cut** everywhere but Mix,
    at step/track/pattern scope; empty items not buffered; clear/cut → buffer = undo (§3.5).
11. **Track menu** (encoder) = all config: per-track up front (first page a 2-oct
    mini-keyboard), global (BPM / clock / scale / root / swing / groove) in back (§2.5).
12. **Note mode & Notes view** → **keep both** (quick per-step palette vs full chord entry).
13. **Patterns** → a view (AUX + 17); snapshots on the low row; **top row 3–6 = switch
    style** (Finish Loop / Next Bar / Instant / Chained). **One project, count RAM-limited**
    (§4.5).
14. **Live recording** → **AUX + 3 arm**, AUX + 4 overdub/replace; keyboard notes quantize
    into the selected track (§7).
15. **MI view** → a 6th view (AUX + 18); the MI-mode keyboard for live playing (§4.6).
16. **OLED** → CC meter + a **4-line page indicator** (2 px = active page), every view (§2).

### Still open
- **Pattern count** — how many patterns actually fit in RAM (target 16). And **switch-style
  default** + **chaining / song mode** (builds on "Chained").
- **Live-rec extras** — count-in / metronome; a **quantize-off** free-record option.
- **Note-mode chords** — a step is polyphonic; does the 10-note palette add or replace, and
  how does it build a chord vs. the Notes view?
- **10-value granularity** — velocity/chance/length across only 10 keys is coarse; add
  encoder fine-tune while a step is held?
- **Repeat/MIDI-FX ranges** — MIDI-FX slot count; **P-Lock color MAGENTA** — confirm.

---

## 11. Frames (files in this folder)

Load any of these into the LED designer. Each file holds its own states.

| File | Contents |
|---|---|
| `form_redesign.json` | Overview: Mix · Step (mode-selector / hold-step / hold-mode-default) · Step+F3 · Notes · Transpose · AUX |
| `form_mix.json` | Mix detail — hold-track controls, F3 track rate |
| `form_step.json` | Step detail — Math (conditional trig) & Velocity value palettes |
| `form_notes.json` | Notes detail — editing, F3 jump-to-step |
| `form_aux.json` | The AUX layer, fully labeled (transport · rec · pages · octave · 6 views) |
| `form_patterns.json` | Patterns view — 16 slots + switch-style row |
| `form_mi.json` | MI view — the live-play keyboard |

(As-built current firmware is documented separately in `FORM_DESIGN.md` +
`form_container/omni/euclid.json`.)
