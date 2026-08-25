# FORM — Redesign Proposal (v2 interaction model)

A proposed rework of FORM's interaction model, replacing the confusing **knob-mode
matrix** with a small set of **views** and **pot banks** (like MI mode). This is a
*design draft* to iterate on — nothing here is built yet. Companion frames:
[`form_redesign.json`](form_redesign.json). Current as-built behaviour lives in
[`FORM_DESIGN.md`](FORM_DESIGN.md).

_Decisions locked in this revision (Aug 2026): step-functions get their own **Func
view**; views live on **AUX + 13–17**; **no pot-bank shortcut** on AUX (bank is set in
the track menu); **per-page length** is set with **F3 + tap step**._

---

## 1. What changes

| Area | Today | Proposed |
|---|---|---|
| Knobs | Pot-mode matrix (K4 rate, K5 UI-mode, …) | **Pot bank** (5 knobs → 5 CCs), **per track**, like MI mode |
| Modes | K5 selects CONFIG/MIX/LENGTH/TRANSPOSE/STEP/NOTEEDIT | **5 views**: **Mix · Step · Func · Transpose · Notes** |
| View switch | AUX + 13–18 (and AUX sometimes behaves differently) | **AUX layer identical everywhere**, on **AUX + 13–17**; the **current view flashes** |
| Zoom/pages | K1 page × K2 zoom (1/2/4 bar), 16-of-64 slice | **Always 16 steps**; **4 fixed pages/track**, on keys 3–6 in Step view |
| CC locks | UI-only, never sent | **P-Locks**: hold a step + turn a knob → lock a CC, resolved through the track's pot bank |
| Step funcs | on the step-hold palette | **their own Func view** (hold-step is now P-Lock) |
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
same** (§6). The **current edit page is shared** across the editing views (Step / Func /
Transpose / Notes) — you pick the page on keys 3–6 in **Step** view and the others
inherit it.

### 4.1 Mix  _(≈ today's base view)_
- **Keys 3–10 = the 8 tracks.** Tap = select · double-tap = mute · hold = machine-type
  picker (unchanged).
- Step row = selected track's pattern (OMNI: LTBLUE has-notes / DKBLUE empty / WHITE
  playhead). "Arrange / pick a track."

→ `form_redesign.json` **state 1**.

### 4.2 Step  _(per-track step editing, paged)_
- **Keys 3–6 = pages 1–4** (7–10 unused). Tap to edit that page. Current page = WHITE,
  others dim.
- Step row = current page's 16 steps. **Hold step + turn knob = P-Lock** (§3); locked
  steps show MAGENTA.
- **Hold F3 (F1+F2)** = the structure layer (§5): tap pages = playback range, tap steps
  = page length.

→ `form_redesign.json` **states 2, 4, 5**.

### 4.3 Func  _(per-step functions — NEW)_
- Dedicated view for step functions (was the step-hold palette). **Top keys 1–7 = the
  function palette**: `-- RSET << >> <> J? ???`
  (RED / ORANGE / DKYELLOW / GREEN / MAGENTA / ROSE / DIMORANGE).
- Step row shows each step colored by its assigned function (no-func = DKBLUE). Tap a
  step to select it (WHITE); the palette key of its current function blinks; tap a
  palette key to assign. For **jump-to-step**, pick `J?` then tap the target step.

→ `form_redesign.json` **state 3**.

### 4.4 Transpose
- Unchanged from today's transpose editor: step row = transpose pattern
  (`TZERO/THIGH/TLOW`), top keys = randomize/clear/copy shortcuts. Operates on the
  current page.

→ `form_redesign.json` **state 7**.

### 4.5 Notes  _(chord/note entry — reworked for in-editor step nav)_
Enter with a **double-click** on a step (Mix/Step view) — that stays. The rework lets
you **step through and edit without leaving the editor**:

- **Keyboard starts at F4 (key 15).** The playable piano is now **keys 3–10 (sharps) +
  15–26 (naturals)** = F4→C6 (~1.5 octaves). The sharps 3–10 line up as the black keys
  for the naturals 15–26, so it reads as a real keyboard. (Octave shift = AUX + 11/12.)
- **Keys 1 / 2 = F1 / F2 = prev / next step**, **one step per click** (no auto-repeat).
- **F3 (hold F1+F2) = jump-to-step:** while held, the low row (11–26) becomes a 16-step
  selector for the current page — the current step flashes; tap a key to jump there.
- **Keys 11–14 = per-step actions:** **11 = clear step · 12 = copy step · 13 = paste
  step · 14 = tie / hold**.
- **Encoder still changes the step** too (unchanged).
- **Step-change feedback:** no more persistent blinking cursor. On any step change
  (F1/F2 or encoder) the new step's stored notes **flash briefly, then settle** to the
  normal keyboard — a quick preview of what's on the step you landed on.
- **OLED:** above the keyboard render, a strip of **16 small boxes** (one **filled** =
  the step being edited) and, to its right, the **page number** (1–4). This is the step
  position readout now that the LED cursor is gone.

→ `form_redesign.json` **state 6**.

---

## 5. Pages, playback range & length (Step view)

- **4 pages per track, 16 steps each** (64 total), always one page on screen — no zoom.
- **Structure layer = hold F3 (F1+F2, keys 1 & 2):**
  - **Tap page keys (3–6) = which pages loop.** Tap one page ⇒ only it loops; tap two ⇒
    the **inclusive range** between them loops (tap page 1 & 4 ⇒ pages 1-2-3-4). Active
    pages light GREEN.
  - **Tap step keys (11–26) = the current page's length** (1–16). Steps beyond the length
    go dark. Each page can be a different length (polymeter).

→ `form_redesign.json` **state 4** (structure layer shows both at once).

---

## 6. The unified AUX layer (identical in every view)

Holding **AUX** always shows the same control layer, and **the current view's key
flashes**:

| AUX + key | Action |
|---|---|
| **1** | Play / Pause |
| **2** | Reset |
| **11 / 12** | Octave − / + |
| **13 / 14 / 15 / 16 / 17** | Switch view → **Mix / Step / Func / Transpose / Notes** (current **flashes**) |

No pot-bank shortcut here (bank is a menu setting, §2). No view may reinterpret AUX
differently — fixes "sometimes holding AUX works differently."

→ `form_redesign.json` **state 8**.

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
| Func palette `-- RSET << >> <> J? ???` | RED ORANGE DKYELLOW GREEN MAGENTA ROSE DIMORANGE | `#ff0000 #ff8000 #4c4d00 #00ff00 #ff00ff #ff0080 #9f8060` |
| View (AUX): current (flashing) / other | WHITE / LOWWHITE | `#ffffff` / `#202020` |

---

## 9. Resolved decisions

1. **Step functions** → their own **Func view** (§4.3).
2. **View-select keys** → **AUX + 13–17**; **no pot-bank shortcut** on AUX (bank set in
   the track menu).
3. **Per-page length** → **F3 (F1+F2) + tap a step** in Step view (§5).

### Still open
- **Changing page while in Func/Transpose/Notes:** current plan inherits the page picked
  in Step view. Add page-nav to those views, or is "switch to Step to change page" fine?
- **Func-view jump-to-step gesture:** pick `J?` then tap target — confirm.
- **P-Lock step color** MAGENTA `#ff00ff` — ok?

---

## 10. Frames in `form_redesign.json`

| State | View / moment |
|---|---|
| 1 | **Mix** — tracks on 3–10, selected track's pattern |
| 2 | **Step** — pages on 3–6, one page's steps (with a P-locked step) |
| 3 | **Func** — per-step function palette + function map |
| 4 | **Step + F3** — structure layer: playback pages (GREEN) + page length boundary |
| 5 | **Step — hold step + knob** = P-Lock being dialed in |
| 6 | **Notes** — scale keyboard / chord entry |
| 7 | **Transpose** — per-step transpose lane |
| 8 | **AUX layer** — 5 views + transport + octave, current view flashing |
