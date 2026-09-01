# FORM Menu Map — parameters · pages · views

> **Purpose:** the single editable picture of the FORM sequencer's menu system, for the
> full menu-design pass. Every parameter has an ID; pages list parameter IDs; views list
> page IDs. Edit this file (reorder params in pages, reassign pages to views, add/drop
> anything) and the edits become the spec.
>
> **State captured:** as-built on `q7-2026-3-Form-Seq-Upgrade`, 2026-09-01.
> Specialized editors (Notes slots, TPAT grid, CC bars, Pot Config, tools bars/previews)
> are listed as pages with their params, even though they don't use the 4-cell grid.

**Parameter Type key:**
`int` number · `enum` named choice · `bool` On/-- · `action` click-to-fire ·
`note` note value · `sel` selector (changes what other things target)

**Scope key (in description):** *step* = per-step, P-Lockable · *track* = per-track ·
*global* = whole device/mode

---

## 1. Parameters

### 1.1 Step parameters (per-step, P-Lockable — pid 0–7 in the engine)

| ID | Short | In FORM? | Description | Type |
|----|-------|----------|-------------|------|
| ST01 | NOTE | ✅ | Step's notes (up to 6, chord entry) | note×6 |
| ST02 | VEL | ✅ | Velocity 0–127 (pid 0) | int |
| ST03 | NUDG | ✅ | Micro-timing nudge −60…+60 (pid 1) | int |
| ST04 | LEN | ✅ | Gate length index 0–22 (pid 2) | enum |
| ST05 | MFX | ✅ | MidiFX group --/1–5 (pid 3) | enum |
| ST06 | PROB | ✅ | Trigger chance 0–100 % (pid 4) | int |
| ST07 | COND | ✅ | Conditional trig (--/FILL/!FIL/A:B…) (pid 5) | enum |
| ST08 | FUNC | ✅ | Step function (jumps/resets…) (pid 6) | enum |
| ST09 | ACUM | ✅ | Accumulating transpose-pattern amount 0–4 (pid 7) | int |
| ST10 | RPT | ✅ | Ratchet/repeat ×1–×4 (page-0 palette only, not a grid pid) | enum |
| ST11 | MUTE | ✅ | Step mute (Mix F1 + low row; Seq layers) | bool |
| ST12 | CC-L | ✅ | Per-step CC P-Locks, 5 slots on the track's pot bank (Mix CC page) | int×5 |

### 1.2 Track parameters (per-track)

| ID | Short | In FORM? | Description | Type |
|----|-------|----------|-------------|------|
| TR01 | LEN | ✅ | Track length 1–64 (flat; pages derive from it) | int |
| TR02 | MFX | ✅ | Track default MidiFX group --/1–5 | enum |
| TR03 | TRIP | ✅ | Triplet mode | bool |
| TR04 | DIR | ✅ | Play direction >> / << | enum |
| TR05 | MODE | ✅ | Play mode (None/Pong/Rand/Rnd2/Shuf/Shld) | enum |
| TR06 | TPOS | ✅ | Live transpose ±64 | int |
| TR07 | TYPE | ✅ | Transpose mode (intervals/semitones) | enum |
| TR08 | TPAT | ✅ | Apply the transpose pattern | bool |
| TR09 | CHAN | ✅ | MIDI channel 1–16 | int |
| TR10 | MONO | ✅ | Monophonic (last note only) | bool |
| TR11 | MIDI | ✅ | Send MIDI out | bool |
| TR12 | CV | ✅ | Send CV out | bool |
| TR13 | RATE | ✅ | Step rate 1:1…1:64 | enum |
| TR14 | SWNG | ✅ | Swing ±100 | int |
| TR15 | S-DV | ✅ | Swing division 16th/8th | enum |
| TR16 | GATE | ✅ | Track gate 0–200 % | int |
| TR17 | MUTE | ✅ | Track mute | bool |
| TR18 | SOLO | ✅ | Track solo | bool |
| TR19 | VELD | ✅ | Default velocity (pushed to unlocked steps; = ST02's default) | int |
| TR20 | PBNK | ✅ | Track's active pot/CC bank 1–N | int |
| TR21 | CCV | ✅ | Live CC values, 5 slots on the active bank (sends on edit) | int×5 |
| TR22 | HUE | ✅ | Track LED colour (knob 5 or 8 presets while held) | int |
| TR23 | PGEN | ✅ | Enabled-pages mask (F1 page gesture: solo/loop-range) | bitmask |
| TR24 | PGLN | ✅ | Per-page length 1–16 (polymeter, F1+F2 layer) | int×4 |
| TR25 | TPATD | ✅ | Transpose pattern data (LEN/SEL/OFS + 16 slots) | editor |

### 1.3 Global / mode-level parameters

| ID | Short | In FORM? | Description | Type |
|----|-------|----------|-------------|------|
| GL01 | BPM | ✅ | Global tempo (TIMINGS page / pot in some modes) | int |
| GL02 | ROOT | ✅ | Scale root | note |
| GL03 | SCAL | ✅ | Scale pattern (--/index) | enum |
| GL04 | LOCK | ✅ | Lock keys to scale | bool |
| GL05 | GRP | ✅ | Group-16 scale layout | bool |
| GL06 | OCT | ✅ | Keyboard octave −5…+4 (MI view) | int |
| GL07 | MCRO | ✅ | AUX macro select (Off/M8/NRN/DEL) | enum |
| GL08 | MPOT | ✅ | Macro may consume the pots in FORM | bool |
| GL09 | QNT | ✅ | Record/quantize amount (MI QUANT submenu) | action+int |
| GL10 | CLR | ✅ | Clear track pattern (Yes/No confirm) | action |
| GL11 | POTS | ✅ | Pot Config submode: CC numbers per bank (global `pots[][]`) | editor |
| GL12 | RARM | ✅ | Record arm (AUX+3) | bool |
| GL13 | RMODE | ✅ | Record replace/overdub (AUX layer) | enum |
| GL14 | NNUM | ✅ | Show note numbers vs note names (Notes/STEPNOTES switch) | bool |
| GL15 | PSW | ✅ | Pattern switch style (Instant / Next Bar / Chained …) | enum |
| GL16 | PSEL | ✅ | Active pattern slot 1–16 (+ copy/cut/paste via F1/F2) | sel |

### 1.4 Tool parameters (TOOLS view; SCOPE is shared)

| ID | Short | In FORM? | Description | Type |
|----|-------|----------|-------------|------|
| TL01 | SCOPE | ✅ | Page / Track (shared; keys 9/10 everywhere) | enum |
| TL02 | ROT ‹ › | ✅ | Rotate left/right (buttons, keys 6/7) | action |
| TL03 | MIRROR | ✅ | Mirror steps (button) | action |
| TL04 | SHUF | ✅ | Shuffle steps (button) | action |
| TL05 | HUM% | ✅ | Humanize amount (% of max nudge) + Apply | int+action |
| TL06 | QNT% | ✅ | Quantize pull-to-grid % + Apply | int+action |
| TL07 | TRNS | ✅ | Transpose all notes: Oct−/Oct+/Semi−/Semi+ (keys 5–8) | action×4 |
| TL08 | SNAP | ✅ | Scale snap: Root/Scale params + Snap button (key 7) | enum×2+action |
| TL09 | VMIN/VMAX | ✅ | Vel Random range (min/max bar) + per-step bars; key 7 apply | int×2 |
| TL10 | CMIN/CMAX | ✅ | Chance Random range + per-step bars; key 7 apply | int×2 |
| TL11 | EUC | ✅ | Euclid: Pulses / Rotate (+Scope); live preview; key 7 apply | int×2 |
| TL12 | GRIDS | ✅ | Grids: Inst (BD/SD/HH/AC) / X / Y / Density (+Scope); key 7 apply | enum+int×3 |

### 1.5 Main-OMX MIDI-mode parameters (**not in FORM** — candidates)

| ID | Short | In FORM? | Description | Type |
|----|-------|----------|-------------|------|
| MM01 | RR | ❌ | Round-robin channel count | int |
| MM02 | RROF | ❌ | Round-robin channel offset | int |
| MM03 | PGM | ❌ | Program change out | int |
| MM04 | BNK | ❌ | Bank select out | int |
| MM05 | THRU | ❌ | MIDI soft-thru | bool |
| MM06 | M-CH | ❌ | Macro MIDI channel | int |
| MM07 | P CC / P VAL | ❌ | Inspect: last pot CC + value | ro |
| MM08 | NOTE/VEL | ❌ | Inspect: last note + velocity in | ro |
| MM09 | CV M | ❌ | CV trigger mode | enum |
| MM10 | GQNT | ❌ | Global quantize (1/n) | enum |
| MM11 | DVEL | ❌ | Global default velocity (FORM uses per-track TR19 instead) | int |

---

## 2. Pages

`(S)` = specialized renderer (not the 4-cell grid).

| ID | Short | Description | Parameters |
|----|-------|-------------|------------|
| PG01 | TRIG (S) | Seq page 0 — step overview; top row = mode palette (NOTE/VEL/LEN/RPT/CHANCE/MATH/FUNC/MFX), hold-step + palette/encoder edits | ST01–ST08, ST10 |
| PG02 | STEP A | 4-grid: Vel / Nudge / Len / MFX (step or default; palettes + LEDs) | ST02 ST03 ST04 ST05 |
| PG03 | STEP B | 4-grid: Prob / Cond / Func / Accum | ST06 ST07 ST08 ST09 |
| PG04 | STEPNOTES (S) | 6 note slots + names/numbers switch (selected step) | ST01, GL14 |
| PG05 | POTS → | Single action cell: open Pot Config | GL11 |
| PG06 | TRACK | 4-grid: Len / MFX | TR01 TR02 |
| PG07 | TRACKMODES | 4-grid: Trip / Dir / Mode | TR03 TR04 TR05 |
| PG08 | SEQTPOSE | 4-grid: TPos / Type / TPat-apply | TR06 TR07 TR08 |
| PG09 | SEQMIDI | 4-grid: Chan / Mono / Midi / CV | TR09 TR10 TR11 TR12 |
| PG10 | TIMINGS | 4-grid: BPM / Rate / Swing / Swing-div | GL01 TR13 TR14 TR15 |
| PG11 | SCALE | 4-grid: Root / Scale / Lock / Group | GL02 GL03 GL04 GL05 |
| PG12 | MIX OVERVIEW (S) | Track name/selector, transport, pattern icons; hold-track = mute/solo/mode/hue/copy | TR17 TR18 TR05 TR22, track copy |
| PG13 | LEVELS (S) | 8-bar per-track velocity mixer | TR19 ×8 |
| PG14 | CC (S) | 5 CC bars + big bank digit (+ step P-Locks in Mix; click title = CC editor) | TR21 TR20 ST12 GL11 |
| PG15 | TRACK GRID | 4-grid: Mute / Solo / Gate / Rate | TR17 TR18 TR16 TR13 |
| PG16 | MI KEYBOARD (S) | Live-play keyboard (edit-turn = track select) | GL06, track sel |
| PG17 | MI MIDI | 4-grid: Chan / Vel / Oct / Macro | TR09 TR19 GL06 GL07 |
| PG18 | MI ACTIONS | Quant / Clear / Pots / MPot | GL09 GL10 GL11 GL08 |
| PG19 | TPAT EDITOR (S) | Transpose-pattern grid: LEN/SEL/OFS + 16 slots | TR25 |
| PG20 | TPOSE PARAMS | 4-grid past the TPAT editor: TPos / Type / TPat-apply | TR06 TR07 TR08 |
| PG21 | PATTERNS (S) | 16 slots + switch-style row; F1/F2 copy-cut-paste | GL15 GL16 |
| PG22 | NOTES OVERVIEW (S) | Step-note editor: overview + jump (F1) + rate/len (F3) | ST01, nav |
| PG23 | TOOL PAGES (S) | One page per tool (11), shared SCOPE | TL01–TL12 |

---

## 3. Views → pages

Order = encoder order within the view.

**MIX** (AUX+13) — *track-level & global*
1. PG12 MIX OVERVIEW
2. PG13 LEVELS
3. PG14 CC
4. PG15 TRACK GRID
5. PG06 TRACK → PG07 TRACKMODES → PG08 SEQTPOSE → PG09 SEQMIDI → PG10 TIMINGS → PG11 SCALE → PG05 POTS (the machine menu, cursor 20)

**SEQ / STEP** (AUX+14) — *programming steps*
1. PG01 TRIG
2. PG02 STEP A
3. PG03 STEP B
4. PG04 STEPNOTES
5. PG05 POTS

**TRANSPOSE** (AUX+15)
1. PG19 TPAT EDITOR
2. PG20 TPOSE PARAMS

**NOTES** (AUX+16)
1. PG22 NOTES OVERVIEW
2. PG04 STEPNOTES (cursor 1–7)
3. PG11 SCALE (cursor 8–11)
4. PG02 STEP A (cursor 12–15)
5. PG03 STEP B (cursor 16–19)

**PATTERNS** (AUX+17)
1. PG21 PATTERNS

**MI** (AUX+18) — *live play*
1. PG16 MI KEYBOARD
2. PG11 SCALE (cursor 1–4)
3. PG17 MI MIDI (cursor 5–8)
4. PG14 CC (cursor 9–15, no P-Locks)
5. PG18 MI ACTIONS (cursor 16–19)

**TOOLS** (AUX+19)
1. PG23 — ROTATE · MIRROR · SHUFFLE · HUMANIZE · QUANTIZE · TRANSPOSE · SCALE SNAP · VEL RANDOM · CHANCE RND · EUCLID · GRIDS

---

## 4. Observations for the pass (delete freely)

- **PG06 TRACK** has 2 empty grid cells; **PG07 TRACKMODES** has 1 — merge candidates
  (e.g. one TRACK page: Len/MFX/Dir/Mode, with Trip moving to TIMINGS's spare slot? —
  TIMINGS is full, though).
- **PG08 SEQTPOSE** duplicates PG20 (same three params, reachable in both Mix and
  Transpose) — intentional today; decide if both stay.
- **PG11 SCALE** appears in 3 views (Mix menu, Notes, MI) — global params, so
  duplication is cheap but worth a look.
- **TR13 RATE** is editable in 3 places (TIMINGS, TRACK GRID, F3 shortcut).
- **MM03/MM04 (PGM/BNK)** are the most-requested MIDI-mode candidates for a per-track
  FORM page (send program/bank per track); MM01/MM02 (round-robin) make less sense
  per-track. MM05 THRU and MM09/MM10 are global config — arguably CONFIG-mode material,
  not FORM.
- FORM has no equivalent of the MIDI-mode **inspect** page (MM07/MM08).
