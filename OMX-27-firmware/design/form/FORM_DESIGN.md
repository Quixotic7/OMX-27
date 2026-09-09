# FORM — Design Reference (as built)

The single design document for the **FORM** sequencer as it ships on
`q7-2026-3-Form-Seq-Upgrade`. It merges the old v1 reference and the v2 redesign
proposal (`FORM_REDESIGN.md`, now retired) into one as-built record.

**Companions:**
- [`FORM_MENU_MAP.md`](FORM_MENU_MAP.md) — **authoritative** parameter / page / group /
  view map (every param has an ID; edit it to respec menus).
- [`FORM_USER_GUIDE.md`](FORM_USER_GUIDE.md) — the user-facing manual.
- [`FORM_V2_REVIEW.md`](FORM_V2_REVIEW.md) — the review + path-to-PR running record
  (what was built, fixed, and hardware-verified, in order).
- [`FORM_IMPLEMENTATION.md`](FORM_IMPLEMENTATION.md), `../../src/form/DESIGN_NOTES.md`
  — historical (phased plan / v1 audit), kept for archaeology.
- `form_*.json` — LED-designer frames (see [`FORMAT.md`](FORMAT.md); load into the
  OMX-27 LED designer). §8 lists them.

---

## 1. Hardware model

27 keys, each with an RGB LED:

| Index | Role |
|---|---|
| **0** | AUX / function key (top-left) |
| **1–10** | top row — **F1 (1)**, **F2 (2)**, **F3 (1+2)**; keys 3–10 = tracks / pages / palettes per view |
| **11–26** | bottom row — the **16-step** row (key 11 = step 0); view switcher under AUX |

Plus **5 pots (K1–K5)**, a **push encoder**, and a **128×32 OLED**.

## 2. Concept

FORM is a **single-engine, 8-track polyphonic step sequencer**. The v1 pluggable
"machine type" architecture (NULL/OMNI/EUCL picker, `FormMachineInterface`) is fully
deleted — every track is the same engine (`FormOmni::FormMachineOmni`), and the shell
(`OmxModeForm`) owns `machines_[8]` directly plus the pattern bank.

- **Track**: up to **64 steps** as **4 fixed pages × 16** (no zoom), per-page length
  1–16 (**polymeter**), enable/disable pages (loop range).
- **Step**: ≤6 notes (chords), velocity, gate length, nudge (±60 micro-timing),
  ratchet ×1–4, probability, conditional trigs (Fill/!Fill, A:B), step functions
  (jumps/resets), accumulating transpose, per-step MidiFX group, per-step mute, and
  **5 CC P-Lock slots** resolved through the track's pot bank.
- **Pattern**: a snapshot of the whole sequencer (all 8 tracks + settings). 16 slots
  on V3/T4, 2 on Teensy 3.1. Switch styles: Finish Loop / Next Bar / Instant /
  Chained (auto-advancing chain). No song mode — chaining covers it (user call).
- **Knobs**: the 5 pots are the selected track's **pot bank** (5 CC numbers from the
  global `pots[bank][slot]` map, editable in-mode via Pot Config). Hold a step + turn
  a knob = **CC P-Lock** on that step.

## 3. Interaction model

- **Encoder, one rule everywhere**: SELECT (turn = move cursor) ⇄ EDIT (turn = change
  value); click toggles, holding AUX is temporary EDIT. Selected = boxed, editing =
  inverted.
- **Views**: seven, on **AUX + keys 13–19** — MIX · STEP(SEQ) · TRANSPOSE · NOTES ·
  PATTERNS · MI · TOOLS. Switching is **live** (the view renders while AUX is still
  held); every view **remembers its page position**. The AUX layer is **modal**
  (unassigned keys are inert while it's held) and identical in every view:
  transport (F1 play/pause · F2 reset · F1+F2 stop), rec arm/mode (3/4), octave
  (11/12), views (13–19), MidiFX shortcuts (5–10, 20–26).
- **F-keys inside a view**: F1 = page gesture (select / double-tap solo / hold-two =
  loop range), F2 = track select (+ hold-track controls), F3 = rate & page length.
  Patterns overrides these for slot copy / cut-paste / clear (with hold labels).
- **Groups**: a view's pages are organized in named groups; crossing into a group pops
  its name once (see the menu map §3/§4).
- **Cell glyphs** (TENFAT `_t_all`): ☐/☑ (`Ć`/`Ĉ`) = boolean off/on · ↗-box (`@`) =
  opens a submenu · ✕ (`µ`) = destructive action.
- **P-Lock convention**: *editing is locking* — setting a step's value on a param page
  (palette, encoder, or knob) sets that param's lock bit; the encoder click clears it.
  Unlocked steps follow the **track defaults**, edited on the same pages with nothing
  held.
- **Live recording**: AUX+3 arm, AUX+4 overdub/replace. Notes record musically —
  nearest step plus **nudge** (how far off-grid) and **length** (hold time); armed +
  stopped, the first note starts the transport. QUANTIZE is a live morph submenu
  (scrub, hear it, apply). Count-in was dropped (MIDI-only device).

## 4. The seven views

Menu contents per view are specified in the **menu map** — this is the one-line role
of each:

| View | Role |
|---|---|
| **MIX** | Track-level: overview, LEVELS velocity mixer, TRACK grid, full track/global param menu. Hold-track = mute/solo/play-mode/colour + the armed **track copy** (key 18: COPY PAT / COPY ALL → tap destination). |
| **SEQ** | Step programming: TRIG overview with 8 hold-modes + palettes, two step-param grids, the **CC page** (live values + held-step P-Locks + bank digit), STEPNOTES editor, then SCALE + ACTIONS. |
| **TRANSPOSE** | The 16-slot transpose lane (per-step values; ACUM makes steps walk it) + the TPOS/TYPE/TPAT params page past its end. |
| **NOTES** | Focused chord editor (6 note slots, names/numbers), with scale, step-param, and ACTIONS pages on the same walk. |
| **PATTERNS** | 16 snapshot slots + switch style + progress bar; F1/F2/F3 = copy / cut-paste / clear slots. |
| **MI** | Live-play keyboard (records when armed) + scale/MIDI/macros/actions/CC pages. |
| **TOOLS** | 11 destructive one-shots: ROTATE · MIRROR · SHUFFLE · HUMANIZE · QUANTIZE · TRANSPOSE · SCALE SNAP · VEL RANDOM · CHANCE RND · EUCLID · GRIDS — shared SCOPE (page/track), live generator previews, Seq-style hold editing. |

## 5. Data & persistence

- `FormPattern` = `FormOmni::OmniSeq[8]`; `sizeof` ≈ **10,576 B** → 16 patterns
  ≈ 165 KB (V3/T4 in RAM; Teensy 3.1 capped at 2).
- Save format **v8** (`kOmniSaveVersion`). `Track::startstep` and `OmniSeq::potMode`
  are RESERVED (never read; kept only for layout).
- **V3 (RP2040)**: the full bank persists to **LittleFS** (`/formbank.dat`, validated
  header) alongside the FRAM active-pattern save, with boot-glitch hardening (header
  re-read + bank restore before any defaults are written). **Teensy**: active pattern
  only (FRAM/EEPROM) — the documented platform limitation.
- Raw blits (bank load / pattern switch) run through `sanitizeSeq()`.

## 6. Design decision log

The 30 resolved decisions from the redesign are preserved verbatim in the git history
of `FORM_REDESIGN.md` (removed 2026-09-01, this branch). The ones that still shape the
code, compressed — plus the deliberate deviations:

- Single engine; AUX+13–19 views; per-page length via F3; Math = conditional-trig
  palette (Fill/!Fill + A:B); one select/edit encoder model; MIDI channels default to
  track index+1; default velocity 100; step-length palette 0.5…64; microtiming is
  encoder-only; MidiFX = Off + 5 groups, per-step and lockable; OLED renders every
  loop (SysEx mirror); SysEx remote control (`NL_CMD_INPUT` 0x51 + screen mirror)
  for host-driven QA.
- **Deviations from the original spec (deliberate):** view switching is AUX-only (the
  page-1 encoder view selector was removed); pages live on F1+3–6, not the AUX layer;
  "hold a mode key = set default" was superseded by param-page palettes; config is
  split by role (Seq = steps, Mix = track/global, Transpose = its own params) instead
  of one track menu, and the 2-octave mini-keyboard page was dropped (MI *is* the
  keyboard); the CC meter is transient/track-page-only — the CC page is the real knob
  surface; as-built colors differ from the proposal tables.
- **Beyond the spec:** TOOLS view; LEVELS + CC pages; LittleFS bank persistence;
  group popups + glyphs; armed track copy; Patterns slot ops; MFX slot bypass
  (AUX+21 / hold-slot F1/F2, bypassed slots red); the interface collapse, dead-code
  removal (~1,500 lines), and the QA rig.
- **Won't build (user calls):** song mode; P-Lock magenta step tint; persistent
  4-line page indicator; count-in.
- **Still open (opportunistic):** the single copy/paste-buffer ideal; re-measure RAM
  before growing the data model.

## 7. Extension notes

There is no machine plug-in interface anymore. New functionality lands either as a
**page** in an existing view (add a row to the menu map, one case per event switch in
`omx_mode_form.cpp` — the six dispatch sites are each a single `switch (formView_)`),
as a **tool** (add to the `kTool*` tables + `toolAction`), or as an **engine feature**
on `FormMachineOmni` (mind `kOmniSaveVersion` for anything persisted).

## 8. Files in this folder

| File | What it is |
|---|---|
| `FORMAT.md` | LED-designer JSON schema |
| `FORM_DESIGN.md` | this reference |
| `FORM_MENU_MAP.md` / `FORM_USER_GUIDE.md` / `FORM_V2_REVIEW.md` / `FORM_IMPLEMENTATION.md` | see header |
| `form_redesign.json` | v2 overview frames: Mix · Step (modes/hold/defaults) · Step+F3 · Notes · Transpose · AUX |
| `form_mix.json` / `form_step.json` / `form_notes.json` / `form_aux.json` / `form_patterns.json` / `form_mi.json` | per-view v2 frames |
| `form_container.json` / `form_omni.json` / `form_euclid.json` | **v1 historical** frames (machine picker / zoom era — superseded) |
