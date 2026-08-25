# FORM — Redesign Proposal (v2 interaction model)

A proposed rework of FORM's interaction model, replacing the confusing **knob-mode
matrix** with a small set of **views** and **pot banks** (like MI mode). This is a
*design draft* to iterate on — nothing here is built yet. Companion frames:
[`form_redesign.json`](form_redesign.json). Current as-built behaviour lives in
[`FORM_DESIGN.md`](FORM_DESIGN.md).

> **Assumptions I made** are called out inline as **[proposed]** and collected in
> §9. Correct any of them and I'll redraw.

---

## 1. What changes

| Area | Today | Proposed |
|---|---|---|
| Knobs | Pot-mode matrix (K4 rate, K5 UI-mode, …) | **Pot bank** (5 knobs → 5 CCs), **per track**, exactly like MI mode |
| Modes | K5 selects CONFIG/MIX/LENGTH/TRANSPOSE/STEP/NOTEEDIT | **4 views**: **Mix · Step · Transpose · Notes** |
| View switch | AUX + keys 13–18 (and AUX sometimes behaves differently) | **AUX layer is identical everywhere**; the **current view flashes** while AUX is held |
| Zoom/pages | K1 page × K2 zoom (1/2/4 bar), 16-of-64 slice | **Always 16 steps on screen**; **4 fixed pages/track**, selected on keys 3–6 in Step view |
| CC locks | UI-only, never sent | **P-Locks**: hold a step + turn a knob → lock a CC, resolved through the track's pot bank |
| Playback range | (n/a) | In Step view, **F1+F2** + page keys pick which pages play; each page has its own **length 1–16** |

---

## 2. Knobs = pot banks (per track)

- The 5 knobs behave like MI mode: each **pot bank** maps K1–K5 to 5 CC numbers
  (`pots[bank][0..4]`, `NUM_CC_BANKS = 5`). Turning a knob sends that CC live.
- **Each track stores its own pot-bank index.** Selecting a different track (Mix view)
  remaps the knobs to that track's bank.
- Change the current track's bank on the **AUX layer, keys 13/14** (prev/next, wraps) —
  the same gesture MI mode already uses, with the bank-color flash on 13/14.
- No more knob-driven modes. Knobs only ever = pot bank.

---

## 3. P-Locks (per-step CC locks)

- **Gesture:** hold a step key (11–26), then **turn a knob**. That knob-slot's value is
  **locked** on that step.
- **Resolution at playback:** when the step fires, each locked knob-slot sends
  `CC = pots[trackBank][slot]` with the locked value. So the *slot* is locked, the *CC
  number* follows the track's current pot bank:
  - Bank 1 maps K1→CC26 ⇒ a lock on K1 sends **CC26**.
  - Switch the track to Bank 2 (K1→CC33) ⇒ the same lock now sends **CC33**.
- A step that carries any P-Lock is tinted **MAGENTA `#ff00ff`** in the step row
  **[proposed color]** so locks are visible at a glance. While the step is held, the
  OLED lists the locked slots/values (resolved to their current CC #s).
- This finally makes the long-dead `Step::potVals[5]` model do something (see
  `DESIGN_NOTES.md §4b`).

---

## 4. The four views

The **top row (keys 3–10)** changes meaning per view; the **step row (11–26)** is always
the current 16 steps; **knobs are always the pot bank**; the **AUX layer is always the
same** (§6).

### 4.1 Mix  _(≈ today's base view)_
- **Keys 3–10 = the 8 tracks.** Tap = select · double-tap = mute · hold = machine-type
  picker (unchanged).
- Step row = the selected track's pattern (OMNI: LTBLUE has-notes / DKBLUE empty /
  WHITE playhead). This is the "arrange / pick a track" view.

→ frame: `form_redesign.json` **state 1**.

### 4.2 Step  _(per-track step editing, paged)_
- **Keys 3–6 = pages 1–4** of the current track (keys 7–10 unused). Tap a page key to
  edit that page. Current edit page = WHITE, others dim.
- Step row = the current page's 16 steps.
- **Hold a step + turn a knob = P-Lock** (§3). A locked step shows MAGENTA.
- **Hold F1+F2 (keys 1 & 2) = choose which pages play** (§5).
- **Hold a page key (3–6) = set that page's length** (press a step 11–26 to set 1–16)
  **[proposed gesture]**.

→ frames: `form_redesign.json` **states 2–4, 8**.

### 4.3 Transpose  _(per-step transpose lane)_
- Unchanged from today's transpose editor: step row shows the transpose pattern
  (`TZERO/THIGH/TLOW`), top keys carry randomize/clear/copy shortcuts.

→ frame: `form_redesign.json` **state 6**.

### 4.4 Notes  _(chord / note entry)_
- Unchanged from today's note editor: a scale keyboard across 1–26, key 11 = REST,
  stored chord notes lit, selected-step cursor blinks.

→ frame: `form_redesign.json` **state 5**.

---

## 5. Pages & playback (Step view)

- **4 pages per track, 16 steps each** (64 steps total, but always shown one page at a
  time — no zoom).
- **Which pages play:** hold **F1+F2** and tap page keys 3–6.
  - Tap one page ⇒ only that page loops.
  - Tap two pages ⇒ the **inclusive range** between them loops (tap page 1 and page 4 ⇒
    pages 1-2-3-4 all play). Active-for-playback pages light GREEN.
- **Per-page length:** each page can be **1–16 steps**, so pages can be different
  lengths (e.g. a 16-step page into a 12-step page for polymeter). Steps beyond a page's
  length are dark.

→ frames: page-range = **state 3**, per-page length = **state 8**.

---

## 6. The unified AUX layer (identical in every view)

Holding **AUX** always shows the same control layer, and **the current view's key
flashes**:

| AUX + key | Action |
|---|---|
| **1** | Play / Pause |
| **2** | Reset |
| **3 / 4 / 5 / 6** | Switch view → **Mix / Step / Transpose / Notes** (current one **flashes**) |
| **11 / 12** | Octave − / + |
| **13 / 14** | Pot bank − / + for the current track (flashes bank color) — same as MI mode |

No view is allowed to reinterpret AUX differently. (Fixes "sometimes holding AUX works
differently.")

→ frame: `form_redesign.json` **state 7**.

---

## 7. Always-16-step view

The K1/K2 page×zoom slice is gone. The grid always shows exactly 16 steps = one page.
Longer material comes from the 4 pages (§5), not from zooming a 64-step lane.

---

## 8. Color proposals (step row)

| Meaning | Color | Hex |
|---|---|---|
| Step has notes | LTBLUE | `#a8a8ff` |
| Step empty (in length) | DKBLUE | `#00004d` |
| Step has a P-Lock | MAGENTA **[proposed]** | `#ff00ff` |
| Playhead (firing / silent) | WHITE / RED | `#ffffff` / `#ff0000` |
| Beyond page length | off | `#000000` |
| Page: current edit page | WHITE | `#ffffff` |
| Page: exists, not current | LOWWHITE | `#202020` |
| Page: active for playback (F1+F2) | GREEN | `#00ff00` |
| View (AUX): current (flashing) / other | WHITE / LOWWHITE | `#ffffff` / `#202020` |

---

## 9. Open questions / assumptions to confirm

1. **View-select keys** — I put the 4 views on **AUX + 3/4/5/6**. OK, or would you
   rather keep them on 13–16 (nearer the old 13–18)? (13/14 now collide with pot-bank,
   so I moved views to 3–6.)
2. **Per-page length gesture** — I proposed **hold page key (3–6) + tap a step**. Is that
   the gesture you want, or something else (e.g. F3 + step as today)?
3. **P-Lock step color** — MAGENTA `#ff00ff`. Fine, or a different accent?
4. **Step functions** (RSET / jump / reverse, today on the step-hold palette): the new
   step-hold gesture is now P-Lock. **Where should step functions live?** (A modifier +
   hold? A dedicated corner? Dropped?) This is the one real collision in the redesign.
5. **Notes/Transpose top row** — these editors consume the whole grid, so keys 3–6 are
   *not* pages there. Confirm that's fine (page/track selection happens via the AUX
   layer while in those views).
6. **Pot-bank scope** — per-track bank index is saved with the track. Confirm banks
   are the 5 global `pots[bank]` maps (shared CC definitions), not per-track CC maps.

---

## 10. Frames in `form_redesign.json`

| State | View / moment |
|---|---|
| 1 | **Mix** — tracks on 3–10, selected track's pattern |
| 2 | **Step** — pages on 3–6, one page's steps (with a P-locked step) |
| 3 | **Step + F1+F2** — page playback-range selection (pages 1–4 active) |
| 4 | **Step — hold step + knob** = P-Lock being dialed in |
| 5 | **Notes** — scale keyboard / chord entry |
| 6 | **Transpose** — per-step transpose lane |
| 7 | **AUX layer** — view + transport + octave + pot-bank, current view flashing |
| 8 | **Step — set page length** (page held, length = 12) |
