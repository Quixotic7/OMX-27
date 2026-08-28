# FORM — Redesign Proposal (v2 interaction model)

A rework of FORM's interaction model, replacing the confusing **knob-mode matrix** with a
small set of **views** and **pot banks** (like MI mode). Much of this is now **built** — the
shell, Mix, and Step views ship, with sections marked _**built**_ describing the actual
firmware (see §4.0–4.2); the rest is still design to iterate on. Companion frames:
[`form_redesign.json`](form_redesign.json). Current as-built (v1) behaviour lives in
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

## 3. P-Locks (per-step CC locks) — _**built** Aug 2026_

- **Gesture (Step view):** hold a step key (11–26), then **turn a knob (K1–K5)**. That
  knob-slot's CC is **locked** on every held step, set directly to the knob's value (no
  pickup). The gesture also arms the step's trig so a CC-only (note-less) step still fires.
- **Resolution at playback:** when the step fires, each locked knob-slot sends
  `CC = pots[trackBank][slot]` with the locked value on the track's channel. The *slot* is
  locked, the *CC number* follows the track's current pot bank.
- The OLED confirms `CC<n> <value>` while you turn. A step carrying any P-Lock is tinted
  **MAGENTA `#ff00ff`** in the step row _(planned; not yet drawn)_.
- Drives the `Step::potVals[5]` model via `setStepPotLock()` → sent in `triggerStep`.

> Note: the keyboard's **Velocity / Length / Chance / Math** are set with the **key palettes**
> (hold 11 / 12 / 13 / 11+12 in Notes; the step-hold palette in Seq), so the pots are free for
> CC P-Locks. The old machine path that edited Vel/Len with the pots is retired.

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

### 4.0 The track page & view selector  _(shared page-1 overview — **built** Aug 2026)_

Both **Mix** and **Step** open on the same **page-1 track overview** (`dispSeqTrackPage`), a
single at-a-glance status screen. Top to bottom / left to right:

- **8 track-state squares** (top-left): a filled 4×4 square = an **unmuted** track, an
  outline = a **muted** one; the **selected** track carries an underline. Solo overrides:
  if any track is soloed, every non-soloed track reads as muted.
- **Transport widget** (top-middle): **play ▸ / stop ▪ / record ●**, the active state filled
  and the others outlined. (Record is drawn but not yet wired — always shows play/stop.)
- **View tag** (top, right of transport): **"MIX"** or **"SEQ"** — the name of the current
  view. This is also the **encoder view selector** (below); while selecting it inverts to a
  filled box with black text and shows the live view (`MIX / SEQ / TRSP / NOTE / PTRN / MI`).
- **BPM** (top-right).
- **Name row** (chunky font): **"TRK n"** — or **"MUTE"** while F1 is held in Mix — plus the
  **play-mode icon** (fwd / rev / fwd-pong / rev-pong / random) and the **rate** (`1:n`).
- **4 page icons** (right): filled = enabled page, outline = disabled; the active page is
  underlined.
- **16-step row** (bottom): each step is one of — **empty** (outline) · **has-notes** (solid)
  · **ghost** = on but no notes (inset-top box + two dots) · **muted-ghost** (inset block +
  top corners + punched dot-row) · **muted-note** (inset 4×4 block). Steps past the page
  **length** shrink to short boxes; the **playhead** draws a tick beneath the current step.

**Encoder = view selector (page 1).** The encoder has no other job on page 1, so it drives
view switching:
- **Click** the encoder → enter the selector (the view tag boxes/inverts). **Turn** →
  **switch views instantly** (live), wrapping through all six. **Click again** (from
  anywhere) → leave the selector.
- **Holding AUX** also makes the selector live — turn the encoder while AUX is held to switch
  views — in addition to the AUX + 13–18 key selector (§6). Releasing AUX keeps the view you
  landed on.
- The selector never steals a normal Step-overview turn (that still pages into the param
  menu); it only intercepts the encoder while actively selecting.

The under-step playhead marker and the LED playhead both advance on **every step** (the loop
marks the display+LEDs dirty the instant the selected track's step changes); the **LED
playhead is a steady bright GREEN**, no blink.

→ `seq-track-page-*.txt` / `mix-track-page-*.txt` frames (`design/form/UI/`).

### 4.1 Mix  _(performance / mixing — **built** Aug 2026)_
Mix is for **mixing / performance**, not step editing. Opens on the shared track page (§4.0)
with the **"MIX"** tag.
- **Keys 3–10 = the 8 tracks** (per-track hue; selected WHITE / SALMON-if-muted, muted RED,
  fired-this-step INDIGO; soloed tracks flash). **Tap** = select · **hold** = per-track
  controls on the low row (below). _(Double-click-to-open-Step was dropped; use the view
  selector.)_
- **Low row (11–26) = the selected track's pattern.** Tapping a step **auditions that step's
  programmed notes** (note-on while held) — you're jamming the *sequence*, not a chromatic
  keyboard. (For live chromatic play, use **MI**, §4.6.)
- **F1 = Mute:** the page name flips to **"MUTE"**; **F1 + tap a track** = mute (no popup —
  the track squares show it); **F1 + tap a step** = toggle that step's mute (shown by the
  muted-step glyphs, §4.0).
- **F2 = Solo / Fill:** **F2 + tap a track** = solo; **holding F2** also arms momentary
  **FILL** on all tracks (steps with a Fill condition play). The F2 display is a split view:
  top row = per-track solo cells, bottom = "Fill".
- **F3 (hold F1+F2) = LEN | RATE:** shows the selected track's rate on top and its page
  length as a 16-cell bar; top row 3–10 pick a rate.
- **Hold a track → per-track controls on the low row:** **11 = Mute · 12 = Solo · 13–17 =
  play mode** (fwd / rev / fwd-pong / rev-pong / **random**) · **19–26 = 8 track-colour
  presets** (or hold the track + turn **K5** for a continuous hue). While a track is held the
  page name boxes and the bottom labels **"MUTE / PLAY MODE"** (same look as Step's F2+track).

→ `form_redesign.json` **state 1** + `form_mix.json` (hold-track & rate detail).

### 4.2 Step  _(per-track step editing — 8 hold-modes — **built** Aug 2026)_
Locked to one track; opens on the shared track page (§4.0) with the **"SEQ"** tag. The
**top row (keys 3–10) selects one of 8 step-edit modes**; the active mode is lit bright, the
others dim:

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
- **Quick-tap a step (11–26) = toggle it.** An empty step is **created** (stamped with the
  last chord entered, default middle C); a step with content is **cleared** (into the copy
  buffer, so **F2/paste undoes it**, §3.5). _(No double-click — it was axed to remove the
  click-vs-hold collision.)_
- **Hold a step (11–26)** → the **top row becomes the value palette** for the current mode
  (keys **1–10**, or **5–10** for MIDI-FX); tap to set the held step(s)' value. Several steps
  can be held at once and edited together. **Press AUX while holding = reset that step to the
  mode's default** (AUX has no other job while a step is held). Turning a pot-bank knob while
  holding still lays a CC P-Lock (§3). A short (~150 ms) delay keeps quick taps from flashing
  the hold UI.
- **Fine values via encoder:** the palette keys are *coarse*. For a precise value (and for
  **microtiming**, which has no key palette), navigate to that param page with the encoder and
  **turn the encoder while holding the step** (param pages 1–2 are P-Lockable: Vel/Nudge/Len/
  MFX and Prob/Cond/Func/Accum; the encoder click clears a lock).
- **Hold a mode key (3–10)** → the value palette sets the mode's **DEFAULT** (what steps use
  until given an explicit value).
- **F1 + step = copy / paste the step; F2 + step = cut / paste** (§3.5). The full chord editor
  is the **Notes view**.

**Modifiers (steps not held — a held step freezes F1/F2 into the palette):**
- **F1 + top row 3–6 = the 4 pages:** single-click selects the edit page; **double-click
  solos** that page; **hold one page + press another** enables that **range/loop**. (This is
  where page select/solo/loop live in Step view — not on the AUX layer.)
- **F2 + top row 3–10 = select the track;** holding one exposes its Mix hold-track controls
  (mute/solo/play-mode/colour) on the low row, with the name boxed + **"MUTE / PLAY MODE"**.
- **F3 + top row 3–10 = rate;** **F3 + a low-row step = set the active page's length** (1–16;
  steps beyond go dark — per-page length gives polymeter).

**The value palette (keys 1–10) per mode:**
- **Note** — chord entry works exactly like today's note editor: while holding the step,
  the **currently-held note keys define the step's notes** — hold several for a **chord**,
  and a **fresh press (from no keys held) replaces**. (C-major example: hold step, hold
  key 1 = C, add 3 = E, add 5 = G → a C-major chord; release all, press 3 alone → the step
  becomes just E.) 10 notes: chromatic = 10 from **C** in the octave, or scale-on = the
  scale's notes.
- **Velocity** — 10 linear levels (key 10 = 127, key 1 ≈ 12; menu+encoder for exact).
- **Step Length** — **0.5 · 0.75 · 1 · 2 · 4 · 6 · 8 · 16 · 32 · 64** (menu+encoder for
  precise).
- **Chance** — 10 probability levels (menu+encoder for exact).
- **Repeat** — **1 · 2 · 3 (quick triplet) · 4** (ratchet count).
- **Math (conditional trig)** — **key 1 = Fill · key 2 = !Fill**; **keys 3–6 = ratio A**
  (1–4) · **keys 7–10 = ratio B** (1–4). E.g. key 3 + key 10 = **1:4** (fires 1 of every 4
  loops).
- **Function** — the **current OMNI step functions** (RSET / reverse / forward / pong /
  rand-jump / rand / jump-to-step).
- **MIDI FX** — **Off + 5 FX slots** (same MIDI-FX as elsewhere on the OMX). Per-step, so
  different steps can route through different FX slots (and it's P-lockable — true weirdness).

**Pages** in Step view are on **F1 + top row 3–6** (select / solo / loop-range, above);
**page length** is **F3 + a low-row step**. _(The original plan put pages on the AUX layer;
in the build they live under F1 so the AUX layer stays view-switch + transport only.)_

→ `form_redesign.json` **states 2, 3, 4** + `form_step.json` (Math + Velocity palettes).

### 4.3 Transpose
- Unchanged from today's transpose editor: step row = transpose pattern
  (`TZERO/THIGH/TLOW`), top keys = randomize/clear/copy shortcuts. Operates on the
  current page.

→ `form_redesign.json` **state 7**.

### 4.4 Notes  _(chord/note entry with in-editor step nav — **built** Aug 2026)_
Enter via the **AUX view selector (AUX + 16)** or the page-1 encoder view tag. There is always
one **selected step**; the keys play/edit it, the encoder pages through richer editors.

- **Keyboard starts at F4 (key 15).** The playable piano is **keys 3–10 (sharps) + 15–26
  (naturals)** = F4→C6. Held keys build the selected step's **chord** (a single press replaces).
  Scale-aware colours like MI mode (root periwinkle / in-scale dim blue / off-scale dark), with
  a chromatic fallback when no scale is set. **Live audition** while transport is stopped
  (note-on on key-hold, note-off on release). (Octave = AUX + 11/12.)
- **Keys 1 / 2 = quick-tap copy / paste step; hold = the F1 / F2 modifiers** (below).
- **Keys 11 / 12 = quick-tap prev / next step; hold = a param palette** (below).
- **Key 13 = hold for the Chance palette.** **Key 14 = quick-tap clear step (into the buffer);
  hold = clear the P-Lock on the encoder-selected step param.**

**Hold-key param palettes** (top row 1–10 sets the value; a ~150 ms delay keeps a quick tap
from flashing the popup — same as the Seq step-hold):
- **Hold 11 = Velocity · hold 12 = Length · hold 11+12 = Math · hold 13 = Chance.**

**F1 / F2 / F3 modifiers** (mirror the Seq view):
- **Hold F1** → **jump-to-step** on the low row (11–26) + **pages** on the top row (3–6:
  select / double-solo / hold-range). OLED shows "JUMP" + the page-1 page icons.
- **Hold F2 + top row 3–10** → select the track.
- **F3 (F1+F2)** → **rate** (top 3–10) + **page length** (low row) — the exact Seq LEN|RATE screen.

**Encoder = pages** (flat cursor, click toggles **select** ↔ **edit**, like the rest of the UI):
`0` keyboard · `1–6` the six note slots + `7` the names/numbers switch (the same **STEPNOTES**
page the Seq view shows) · `8–11` **Scale** (Root / Scale / Lock / Group — global) · `12–15`
step params A (Vel / Nudge / Len / MFX) · `16–19` step params B (Prob / Cond / Func / Accum).
Edit-mode turn changes the selected step (keyboard), the six note values (STEPNOTES), or the
param on the selected step. Keys (piano / nav / palettes / F-mods) work the same on every page.

→ `form_notes.json` (and the Seq STEPNOTES page it reuses).

### 4.5 Patterns
A **pattern** is a snapshot of the whole sequencer — all 8 tracks' step data plus their
settings (pot bank, play mode, pages/length, rate). Switch between patterns to build
sections (verse / chorus / fill). **One project only** (the device has no room for
multiple), and the **pattern count is RAM-limited** — up to 16 on the low row, but may end
up fewer. Counts are likely **platform-dependent**: **OMX-27 V3** has the most RAM;
**Teensy 3.1 / 4** are tighter, so patterns and/or the number of **pages / tracks** may be
trimmed per build to fit (see §10). We won't know the real ceilings until we try.

- **Low row (keys 11–26) = the pattern slots.** Current pattern = WHITE; patterns with
  content = dim color; queued = blinking; empty = off. **Tap a slot to switch.**
- **Top row keys 3–6 = switch style:** **3 Finish Loop · 4 Next Bar · 5 Instant · 6
  Chained** (active style lit bright). This governs *when* a tapped pattern takes over —
  at loop end, at the next bar, immediately, or appended to a chain.
- **F1 / F2 = copy / paste** a pattern (the §3.5 buffer — cut also clears, so it's undo).
  _(Pattern **chaining** / song mode builds on the "Chained" style — see §10.)_

→ `form_patterns.json`.

### 4.6 MI  _(live-play keyboard — **built** Aug 2026)_
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
- **Count-in:** if you start recording from a **stopped** transport, a **1-bar count-in**
  plays first. Togglable in the **track menu**.
- **Quantize-off (free-timed) record** rides on **per-step microtiming** — each recorded
  note keeps a micro-offset from its step. Microtiming has no key palette; it's edited by
  holding the step + the **menu param + encoder** (§4.2).

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
| Playhead (LED, both views) | **GREEN, steady** (no blink) | `#00ff00` |
| Muted step (LED) | DKRED | `#4d0000` |
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
17. **Note-mode chords** = the current note-editor logic: held note keys define the step;
    hold several = chord, a fresh press replaces (§4.2).
18. **Value granularity** → 10 keys are coarse; **menu param + encoder while holding the
    step** gives exact values. Velocity = 10 linear (10→127); Step Length = 0.5/0.75/1/2/4/
    6/8/16/32/64. **Microtiming** is menu+encoder only (no keys) → enables quantize-off (§7).
19. **MIDI FX** = **Off + 5 slots**, per-step and P-lockable (§4.2).
20. **Count-in** = 1 bar when starting from stopped; menu toggle (§7).
21. **Per-track MIDI channel** → defaults to the **track index + 1 (channels 1–8)**, changeable
    in the track menu (SEQMIDI page). Applied at construction, in `FormPattern()`, and
    re-applied in `loadFromDisk` (which re-creates the machines); a saved channel overrides.
22. **Default step velocity** → **100** (was 127).
23. **CC P-Lock** → **hold step + turn a pot** in the Step view (§3), direct-set; pots are no
    longer used for Vel/Len (those are key palettes).
24. **Display refresh** → FORM renders the OLED buffer **every loop like Euclidean** (dropped the
    `canShowDisplay()` gate) so the per-loop-cleared buffer is always populated; the physical
    flush stays throttled to ~60 ms. Fixes the SysEx screen-mirror capturing blank frames.
25. **SysEx remote control** → the firmware accepts injected key/encoder/pot events
    (`NL_CMD_INPUT` 0x51) and mirrors its screen, for host-driven QA (host tools in `tools/`).

26. **Live recording** (§7) → **built**: AUX+3 arm (transport widget shows the record icon),
    AUX+4 overdub/replace. A note played (MI or Notes keyboard) while armed records into the selected
    track's nearest step — but keeps its **micro-timing** (the step's NUDGE is set from how far off
    the grid it was played) and its **length** (note-on→note-off sets the step's LEN), so takes feel
    musical rather than hard-locked. **Start-on-note**: armed + stopped, the first note starts the
    transport + recording on the downbeat. **QUANTIZE strength** (0-100%, MI menu cursor 9) scales the
    recorded nudge live (100 = hard snap); **long-press the encoder in MI = quantize now** (pull the
    selected track's timing toward the grid by that amount). Count-in dropped (MIDI-only device).
27. **Pot banks + CC meter** (§2) → **built**: a plain knob turn sends `CC = pots[track.potBank]
    [slot]` on the track's channel (per-track bank, all 5 knobs); a 5-segment top-row CC meter shows
    the live knob values on the track page.
28. **MI view** (§4.6) → **built**: keys 1-26 play scale-aware notes on the selected track's
    channel (octave AUX+11/12); records when armed (§7, now with nudge/length/quantize); does not
    touch the pattern. Page 0 shows up to 4 rectangles along the bottom — one per enabled page, width
    proportional to its length — with a filled playhead cell moving through the playing page.
29. **Pattern switch styles** (§4.5) → **built**: top row 3-6 = Finish Loop / Next Bar / Instant /
    Chained; a tapped slot commits at the chosen boundary (loop end / bar / immediate), Chained
    builds an auto-advancing chain. The transient CC meter (decision 27) shows only while a knob moves.
30. **Patterns view UI** (§4.5) → **built**: TENFAT "Pn" (no "/N" count), the switch-style name
    top-right, a ">Pq"/"CHn" tag, and a bottom **switch-progress bar** (loop position, or bar
    position under Next Bar). **Quick-tap F1 = copy** the whole pattern, **quick-tap F2 = paste**
    (buffer is a lazily heap-allocated FormPattern). **Hold F1 + tap a slot = instant jump**,
    overriding the switch style in any mode. **Hold F2 + tap a slot = cut/paste**: a filled slot is
    cut to the buffer + cleared, an empty slot receives it (move a pattern between slots). The
    Step-view P-Lock no longer pops a "CC" message — the top CC meter already conveys the value.

### Still open
- **RAM ceilings / per-platform trims** — real pattern / page / track counts per build
  (V3 fullest; Teensy 3.1 / 4 tighter). Decide the trims once we measure.
- **Pattern extras** — full **song mode** (edit/save chains, not just live chaining).
- **Count-in** (§7) — dropped: this is a MIDI-only device, so there is no click to count
  against. (Transpose view is built; needs a functional MIDI QA pass.)
- **P-Lock step tint (MAGENTA)**, the **4-line page indicator**, and the **CC meter on non-track
  views** — not drawn yet.

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
