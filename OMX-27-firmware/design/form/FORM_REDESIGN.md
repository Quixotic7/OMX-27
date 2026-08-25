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
| Modes | K5 selects CONFIG/MIX/LENGTH/TRANSPOSE/STEP/NOTEEDIT | **4 views**: **Mix · Step · Transpose · Notes** |
| View switch | AUX + 13–18 (and AUX sometimes behaves differently) | **AUX layer identical everywhere**, on **AUX + 13–16**; the **current view flashes** |
| Zoom/pages | K1 page × K2 zoom (1/2/4 bar), 16-of-64 slice | **Always 16 steps**; **4 fixed pages/track**, on keys 3–6 in Step view |
| Per-step edit | step-hold palette (funcs) | **8 step-edit modes** on keys 3–10 (Note / Velocity / Step Length / Repeat / Chance / Math / Function / MIDI FX); hold a step → 10-key value palette on keys 1–10; hold a mode key → set its default on keys 11–20 |
| CC locks | UI-only, never sent | **P-Locks**: hold a step + turn a knob → lock a CC, resolved through the track's pot bank |
| Step funcs | on the step-hold palette | back as the **Function** step-mode (no separate view) |
| Playback range / length | (n/a) | Step view **F3 (F1+F2)**: tap **pages** = which pages loop; tap **steps** = current page **length 1–16** |

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

## 4. The four views

The **top row (keys 3–10)** changes meaning per view; the **step row (11–26)** is always
the current 16 steps; **knobs are always the pot bank**; the **AUX layer is always the
same** (§6). The **current edit page is shared** across the editing views (Step /
Transpose / Notes) — you pick the page on keys 3–6 in **Step** view and the others
inherit it.

### 4.1 Mix  _(≈ today's base view)_
- **Keys 3–10 = the 8 tracks**, each shown in its own **track color** (from `seqColors`).
  Tap = select · **double-click = enter that track's Step view**. Selected = WHITE,
  muted = RED, fired-this-step = INDIGO flash, empty = dim.
- Step row = selected track's pattern (LTBLUE has-notes / DKBLUE empty / WHITE playhead).
  "Arrange / pick a track."
- **Mute = F3 (hold F1+F2) + tap a track.**
- **Copy/paste a whole track** with F1/F2 + tap a track (copy the entire OMNI track to
  another slot).
- Hold-a-track is now **free** (was the machine-type picker, which is gone — see §0).

→ `form_redesign.json` **state 1**.

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
| 8 | **Math** | (conditional / value math — TBD, see §9) |
| 9 | **Function** | step function (RSET / jump / reverse / …) |
| 10 | **MIDI FX** | per-step MIDI-FX routing |

**Editing in the active mode:**
- **Hold a step (11–26)** → the **top row keys 1–10 become the value palette** for the
  current mode; tap one to set that step's value. **Press AUX while holding = reset that
  step to the mode's default.** (Turning a pot-bank knob while holding still lays a CC
  P-Lock, §3.)
- **Hold a mode key (3–10)** → **keys 11–20 become the value palette for the mode's
  DEFAULT** value (what steps use until given an explicit value); tap one to set it.

**The value palette is 10 keys, contents per mode.** For **Note** mode the 10 keys are 10
notes from the current note settings: **chromatic** = 10 chromatic notes from **C** in the
current octave; **scale on** = the notes of the current scale only. (Velocity / Chance /
etc. map their 10 keys across the value range; Function's 10 keys are the function list.)

**Structure & pages** (unchanged):
- **Hold F3 (F1+F2) = structure layer** (§5): tap **pages** = playback range, tap **steps**
  = page length. _(The top row now hosts the 8 modes, so page selection lives here / on the
  AUX layer — see §9.)_

→ `form_redesign.json` **states 2, 3, 4**.

### 4.3 Transpose
- Unchanged from today's transpose editor: step row = transpose pattern
  (`TZERO/THIGH/TLOW`), top keys = randomize/clear/copy shortcuts. Operates on the
  current page.

→ `form_redesign.json` **state 6**.

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
- **[proposed] Per-step length (gate):** hold **F3** and use the **top row** to set the
  current step's length. (See §9 — needs to reconcile with F3's low-row jump.)
- **[proposed] Change page from the editor:** hold **F1** and tap a **page key (3–6)** to
  jump the shared page without leaving Notes. (See §9 — F1 is also tap=copy.)
- **OLED:** above the keyboard render, a strip of **16 small boxes** (one **filled** =
  the step being edited) and, to its right, the **page number** (1–4).

→ `form_redesign.json` **state 5** (and `form_notes.json` for the detail).

---

## 5. Pages, playback range & length (Step view)

- **4 pages per track, 16 steps each** (64 total), always one page on screen — no zoom.
- **Structure layer = hold F3 (F1+F2, keys 1 & 2):**
  - **Tap page keys (3–6) = which pages loop.** Tap one page ⇒ only it loops; tap two ⇒
    the **inclusive range** between them loops (tap page 1 & 4 ⇒ pages 1-2-3-4). Active
    pages light GREEN.
  - **Tap step keys (11–26) = the current page's length** (1–16). Steps beyond the length
    go dark. Each page can be a different length (polymeter).

→ `form_redesign.json` **state 3** (structure layer shows both at once).

---

## 6. The unified AUX layer (identical in every view)

Holding **AUX** always shows the same control layer, and **the current view's key
flashes**:

| AUX + key | Action |
|---|---|
| **1** | Play / Pause |
| **2** | Reset |
| **11 / 12** | Octave − / + |
| **13 / 14 / 15 / 16** | Switch view → **Mix / Step / Transpose / Notes** (current **flashes**) |

No pot-bank shortcut here (bank is a menu setting, §2). No view may reinterpret AUX
differently — fixes "sometimes holding AUX works differently."

→ `form_redesign.json` **state 7**.

---

## 7. Always-16-step view

The K1/K2 page×zoom slice is gone. The grid always shows exactly 16 steps = one page.
Longer material comes from the 4 pages (§5), not from zooming a 64-step lane.

---

## 8. Color proposals

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

## 9. Resolved decisions

1. **Single engine** → no machine types; 8 tracks, all the same sequencer (§0).
2. **View-select keys** → **AUX + 13–16** (Mix/Step/Transpose/Notes); **no pot-bank
   shortcut** on AUX (bank set in the track menu).
3. **Per-page length** → **F3 (F1+F2) + tap a step** in Step view (§5).
4. **Notes step nav** → F1/F2 = copy/paste step; **11/12 = prev/next step**; 14 = clear;
   F3 = jump-to-step; **live MIDI audition** on key-hold when stopped (§4.5).
5. **Mix double-click** → enters the track's Step view (was mute); **mute = F3 + tap**.
6. **CC meter** → 5-segment line meter on the top OLED row, every view (§2).
7. **Step view = 8 hold-modes** on keys 3–10 (Note / Velocity / Step Length / Repeat /
   Chance / Math / Function / MIDI FX). Hold step → value palette on keys 1–10; hold mode
   key → default on keys 11–20; AUX-while-holding-step = reset to default (§4.2).
   **Function is back** as a mode (no separate view).

### Still open
- **Where do pages go?** The top row (3–10) is now the 8 modes, so pages can't live on
  3–6 anymore. Proposal: page selection happens in the **F3 structure layer** (tap a page
  to make it the edit page) and/or **AUX**. Confirm.
- **"Math" mode** — what is it exactly? (Conditional trigs A:B? per-step transpose/accum
  math? something else?)
- **Note mode vs Notes view** — Note mode gives a quick 10-note-per-step palette; the
  Notes view is full chord entry. Keep both (quick vs deep), or does the Note mode make
  the Notes view redundant for single-note tracks?
- **10-value granularity** — velocity/chance/length across only 10 keys is coarse; add
  encoder fine-tune while a step is held?
- **F1 tap-vs-hold**, **P-Lock color MAGENTA** — as before.

---

## 10. Frames in `form_redesign.json`

| State | View / moment |
|---|---|
| 1 | **Mix** — 8 tracks (per-track colors), selected track's pattern |
| 2 | **Step — mode selector** — 8 step-edit modes on keys 3–10 (Note active) |
| 3 | **Step — hold a step** — value palette on keys 1–10 (Note = 10 notes) |
| 4 | **Step — hold a mode key** — set that mode's default on keys 11–20 |
| 5 | **Step + F3** — structure layer: playback pages (GREEN) + page length |
| 6 | **Notes** — full chord entry (F4 start, F1/F2 copy/paste) |
| 7 | **Transpose** — per-step transpose lane |
| 8 | **AUX layer** — 4 views + transport + octave, current view flashing |

Notes view detail (edit + F3 jump-to-step) is in `form_notes.json`.
