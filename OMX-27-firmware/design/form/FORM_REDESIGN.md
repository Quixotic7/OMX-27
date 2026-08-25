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

_Decisions locked (Aug 2026): **single engine, 8 tracks** (no machine types); **4 views**
(Mix/Step/Transpose/Notes) on **AUX + 13–16**; Step view = **8 step-edit modes** on keys
3–10 (Note/Velocity/Step Length/Repeat/Chance/Math/Function/MIDI FX) with a 10-key value
palette; **no pot-bank shortcut** on AUX (bank set in the track menu); Notes reworked for
in-editor step nav; global 5-segment CC meter on the top OLED row._

---

## 1. What changes

| Area | Today | Proposed |
|---|---|---|
| Track engine | 8 slots, switchable machine types (OMNI/EUCL/…) | **one engine per track** (the OMNI step sequencer); **no machine types / no picker** |
| Knobs | Pot-mode matrix (K4 rate, K5 UI-mode, …) | **Pot bank** (5 knobs → 5 CCs), **per track**, like MI mode |
| Modes | K5 selects CONFIG/MIX/LENGTH/TRANSPOSE/STEP/NOTEEDIT | **5 views**: **Mix · Step · Transpose · Notes · Patterns** |
| View switch | AUX + 13–18 (and AUX sometimes behaves differently) | **AUX layer identical everywhere**, on **AUX + 13–17**; the **current view flashes** |
| Zoom/pages | K1 page × K2 zoom (1/2/4 bar), 16-of-64 slice | **Always 16 steps**; **4 fixed pages/track**, on keys 3–6 in Step view |
| Per-step edit | step-hold palette (funcs) | **8 step-edit modes** on keys 3–10 (Note / Velocity / Step Length / Repeat / Chance / Math / Function / MIDI FX); hold a step → 10-key value palette on keys 1–10; hold a mode key → set its default on keys 11–20 |
| CC locks | UI-only, never sent | **P-Locks**: hold a step + turn a knob → lock a CC, resolved through the track's pot bank |
| Step funcs | on the step-hold palette | back as the **Function** step-mode (no separate view) |
| Playback range / length | (n/a) | Step view **F3 (F1+F2)**: tap **pages** = which pages loop; tap **steps** = current page **length 1–16** |
| Patterns | (n/a) | **16 patterns** (whole-sequencer snapshots) — the **Patterns view**; tap to switch, queued to loop end (§4.5) |
| Live recording | (n/a) | **AUX + 7 = rec arm**; play the keyboard while running → notes quantize into the selected track (§7) |

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

## 4. The five views

The **top row (keys 3–10)** changes meaning per view; the **step row (11–26)** is always
the current 16 steps; **knobs are always the pot bank**; the **AUX layer is always the
same** (§6). The **current edit page is shared** across the editing views (Step /
Transpose / Notes) and selected on the AUX layer. The five views are **Mix · Step ·
Transpose · Notes · Patterns**.

### 4.1 Mix  _(≈ today's base view)_
Mix is for **mixing / performance**, not step editing.
- **Keys 3–10 = the 8 tracks** (per-track `seqColors`; selected WHITE, muted RED, empty
  dim, fired-this-step INDIGO). **Tap** = select · **double-click** = enter Step view ·
  **hold** = per-track controls on the low row (below).
- **Low row (11–26) = live keyboard:** tapping a key **plays its note** (audition / jam) —
  it does *not* open the note editor here. It also shows the selected track's pattern.
- **F1 = Mute, F2 = Solo** (modifiers): hold **F1 + tap a track** = mute; hold **F2 + tap a
  track** = solo. Hold **F1 + tap a step** = toggle that single step's mute.
- **F3 (hold F1+F2) = track rate:** top row 3–10 = rate options for the selected track;
  the low row mirrors the per-track controls.
- **Hold a track → per-track controls on the low row:** **11 = Mute · 12 = Solo · 14–18 =
  play mode** — 14 forward · 15 reverse · 16 forward-pong · 17 reverse-pong · **18 random-
  page** ("random-page" plays each *enabled* page of the pattern in random order). (13 free.)
- **Whole-track copy/paste** now needs a new gesture (F1/F2 became mute/solo) — see §10.

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
- **Quick-click a step (11–26) = clear that step** (Elektron-style).
- **Hold a step (11–26)** → the **top row keys 1–10 become the value palette** for the
  current mode; tap one to set that step's value. **Press AUX while holding = reset that
  step to the mode's default.** (Turning a pot-bank knob while holding still lays a CC
  P-Lock, §3.)
- **Hold a mode key (3–10)** → **keys 11–20 become the value palette for the mode's
  DEFAULT** value (what steps use until given an explicit value); tap one to set it.

**The value palette is 10 keys, contents per mode:**
- **Note** — 10 notes: **chromatic** = 10 notes from **C** in the current octave;
  **scale on** = the current scale's notes only.
- **Velocity / Step Length / Repeat / Chance** — the 10 keys span the value range.
- **Math (conditional trig)** — **key 1 = Fill · key 2 = !Fill**; **keys 3–6 = ratio A**
  (1–4) · **keys 7–10 = ratio B** (1–4). E.g. key 3 + key 10 = **1:4** (fires 1 of every 4
  loops). 
- **Function** — the 10 keys are the function list. **MIDI FX** — the MIDI-FX slots.

**Pages** are selected on the **AUX layer** (keys 3–6) now that the top row hosts modes;
**F3 (F1+F2) = structure layer** still sets playback range + page length (§5).

→ `form_redesign.json` **states 2, 3, 4** + `form_step.json` (Math + Velocity palettes).

### 4.3 Transpose
- Unchanged from today's transpose editor: step row = transpose pattern
  (`TZERO/THIGH/TLOW`), top keys = randomize/clear/copy shortcuts. Operates on the
  current page.

→ `form_redesign.json` **state 7**.

### 4.4 Notes  _(chord/note entry — reworked for in-editor step nav)_
Enter with a **double-click** on a step (Mix/Step view) — that stays. The rework lets
you **step through and edit without leaving the editor**:

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
- **Change page** while here: via the **AUX layer** (keys 3–6), like everywhere else (§6).
- **OLED:** above the keyboard render, a strip of **16 small boxes** (one **filled** =
  the step being edited) and, to its right, the **page number** (1–4).

→ `form_redesign.json` **state 6** (and `form_notes.json` for the detail).

### 4.5 Patterns
A **pattern** is a snapshot of the whole sequencer — all 8 tracks' step data plus their
settings (pot bank, play mode, pages/length, rate). You get **16 patterns** per project,
so you can build sections (verse / chorus / fill) and switch between them.

- **Low row (keys 11–26) = the 16 pattern slots.** Current pattern = WHITE; patterns with
  content = dim color; empty = off. **Tap a slot to switch** — the switch is **queued to
  the end of the current pattern's loop** (the queued slot blinks until it takes over) so
  it stays in time. _(Hold-tap = switch immediately — proposed.)_
- **Top row = pattern ops:** **F1 = copy · F2 = paste** a pattern; **hold + clear** a
  pattern. _(Pattern **chaining** / song mode is a later add — see §10.)_

→ `form_patterns.json`.

---

## 5. Pages, playback range & length (Step view)

- **4 pages per track, 16 steps each** (64 total), always one page on screen — no zoom.
- **Select the edit page** on the **AUX layer** (AUX + keys 3–6 = page 1–4) — §6.
- **Structure layer = hold F3 (F1+F2, keys 1 & 2):**
  - **Tap page keys (3–6) = which pages loop.** Tap one page ⇒ only it loops; tap two ⇒
    the **inclusive range** between them loops (tap page 1 & 4 ⇒ pages 1-2-3-4). Active
    pages light GREEN. _(Per-track **play mode** — forward / reverse / pong / **random-
    page** — is set in Mix, §4.1; random-page shuffles these enabled pages.)_
  - **Tap step keys (11–26) = the current page's length** (1–16). Steps beyond the length
    go dark. Each page can be a different length (polymeter).

→ `form_redesign.json` **state 5** (structure layer shows both at once).

---

## 6. The unified AUX layer (identical in every view)

Holding **AUX** always shows the same control layer, and **the current view's key
flashes**:

| AUX + key | Action |
|---|---|
| **1** | Play / Pause |
| **2** | Reset |
| **3 / 4 / 5 / 6** | Select edit **page 1–4** |
| **7** | **Record arm** (toggle; red when armed) — §7 |
| **8** | **Rec mode** — overdub / replace |
| **11 / 12** | Octave − / + |
| **13 / 14 / 15 / 16 / 17** | Switch view → **Mix / Step / Transpose / Notes / Patterns** (current **flashes**) |

Free: 9, 10, 18–26. No pot-bank shortcut here (bank is a menu setting, §2). **MIDI-FX is
no longer on AUX** — it's a Step-view mode now (§4.2). No view may reinterpret AUX
differently — fixes "sometimes holding AUX works differently."

→ `form_redesign.json` **state 8** + `form_aux.json` (fully-labeled layout).

---

## 7. Live note recording

Play notes into the sequencer while it runs.

- **Arm** with **AUX + 7** (latching; the key is red while armed). A REC indicator shows on
  the OLED.
- **While armed + playing,** notes you play — on the **Notes-view keyboard** or the
  **Mix low-row keyboard** — are **recorded into the selected track**, **quantized to the
  nearest step** on the current page. Multiple notes on one step = a chord.
- **Rec mode (AUX + 8): overdub** (default — adds to a step's existing notes) or
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
| Page: current edit page | WHITE | `#ffffff` |
| Page: exists, not current | LOWWHITE | `#202020` |
| Page: active for playback (F3) | GREEN | `#00ff00` |
| Step modes (Note/Vel/Len/Repeat/Chance/Math/Func/MFX), active bright / dim | ORANGE YELLOW GREEN CYAN BLUE PURPLE MAGENTA ROSE | `#ff8000 #ffff00 #00ff00 #00ffff #0000ff #7f00ff #ff00ff #ff0080` |
| Value palette: available / current-value / root | dim-blue / LTYELLOW / periwinkle | `#000090` / `#ffff80` / `#a2a2ff` |
| View (AUX): current (flashing) / other | WHITE / LOWWHITE | `#ffffff` / `#202020` |

---

## 10. Resolved decisions

1. **Single engine** → no machine types; 8 tracks, all the same sequencer (§0).
2. **View-select keys** → **AUX + 13–17** (Mix/Step/Transpose/Notes/Patterns); **no
   pot-bank shortcut** on AUX (bank set in the track menu).
3. **Per-page length** → **F3 (F1+F2) + tap a step** in Step view (§5).
4. **Notes step nav** → F1/F2 = copy/paste step; **11/12 = prev/next step**; 14 = clear;
   F3 = jump-to-step; **live MIDI audition** on key-hold when stopped (§4.5).
5. **CC meter** → 5-segment line meter on the top OLED row, every view (§2).
6. **Step view = 8 hold-modes** on keys 3–10 (Note / Velocity / Step Length / Repeat /
   Chance / Math / Function / MIDI FX). Hold step → value palette on keys 1–10; hold mode
   key → default on keys 11–20; AUX-while-holding-step = reset to default; **quick-click a
   step = clear it** (§4.2). **Function is back** as a mode.
7. **Math** = **conditional trig**: key 1 = Fill, key 2 = !Fill, keys 3–6 = ratio A (1–4),
   keys 7–10 = ratio B (1–4) → e.g. 3+10 = 1:4.
8. **Pages** → selected on the **AUX layer** (AUX + 3–6). **MIDI-FX removed from AUX**
   (it's a Step mode now).
9. **Mix = performance view** (§4.1): tap-play the low row; F1 = mute, F2 = solo, F3 =
   track rate; **hold a track** → mute/solo/**play mode** (fwd/rev/pong/rev-pong/random-
   page) on the low row; double-click a track = enter Step.
10. **Note mode & Notes view** → **keep both** (quick per-step palette vs full chord entry).
11. **Patterns** → a 5th view (AUX + 17); **16 whole-sequencer snapshots** on the low row,
    tap to switch (queued to loop end); F1/F2 copy/paste (§4.5).
12. **Live recording** → **AUX + 7 arm**, AUX + 8 overdub/replace; play the keyboard while
    running → notes quantize into the selected track (§7).

### Still open
- **Pattern switch timing** — queued-to-loop-end default; add hold-tap = instant? And
  **pattern chaining / song mode** (later).
- **Live-rec extras** — count-in / metronome; a **quantize-off** free-record option.
- **Whole-track copy/paste** — F1/F2 are now mute/solo in Mix, so track copy/paste needs a
  new gesture (a hold-track option? a Mix menu?).
- **Key 13** in the hold-track low row (11 mute, 12 solo, 14–18 play mode) — unused; assign
  or leave.
- **10-value granularity** — velocity/chance/length across only 10 keys is coarse; add
  encoder fine-tune while a step is held?
- **F1 tap-vs-hold** (Notes copy vs …), **P-Lock color MAGENTA** — as before.

---

## 11. Frames (files in this folder)

Load any of these into the LED designer. Each file holds its own states.

| File | Contents |
|---|---|
| `form_redesign.json` | Overview: Mix · Step (mode-selector / hold-step / hold-mode-default) · Step+F3 · Notes · Transpose · AUX |
| `form_mix.json` | Mix detail — hold-track controls, F3 track rate |
| `form_step.json` | Step detail — Math (conditional trig) & Velocity value palettes |
| `form_notes.json` | Notes detail — editing, F3 jump-to-step |
| `form_aux.json` | The AUX layer, fully labeled (transport · pages · rec · octave · views) |
| `form_patterns.json` | Patterns view — 16 pattern slots, switch/copy/paste |

(As-built current firmware is documented separately in `FORM_DESIGN.md` +
`form_container/omni/euclid.json`.)
