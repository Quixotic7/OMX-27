# FORM v2 — Implementation Plan

A build order that turns [`FORM_REDESIGN.md`](FORM_REDESIGN.md) into firmware. Read that
first for the *what*; this is the *how* and the *in what order*. Grounded in the current
tree (`src/form/…`, `src/hardware/storage.h`, `src/modes/…`).

**Guiding principle:** get to a *playable, editable single pattern* as fast as possible,
then layer features. Resolve the **RAM / pattern-storage** question in Phase 1 — it gates
everything and is the one thing that can force a redesign.

---

## A. What already exists (reuse, don't rebuild)

The current machine-based FORM already contains most of the engine:

| Need in v2 | Already in code |
|---|---|
| 8 tracks | `OmxModeForm` holds `machines_[8]`, each a `FormMachineOmni` with a 1-track `OmniSeq` → **the 8 machines ARE the 8 tracks** |
| 64 steps / 4 pages | OMNI tracks are 64 steps (`Track::len : 6`); today paged/zoomed — fix to 4×16 |
| Per-step data | **`Step` already packs almost everything** — see below |
| Play modes | `Track::playDirection`, `Track::playMode` (pong / rand / shuffle…) |
| Note editor | `omni_note_editor.*` → **Notes view** |
| Transpose editor | `omni_transpose_pattern.*` → **Transpose view** |
| Param pages / menu | `ParamManager` → the **track menu** |
| Clock / ticks | `clockConfig`, `seqConfig`, `EuclideanSequencer::clockTick` pattern |
| MIDI-FX | `subModeMidiFx[NUM_MIDIFX_GROUPS]` (5 groups) → **MIDI-FX mode** |
| Pot banks | `pots[NUM_CC_BANKS=5]`, MI-mode `potSettings.potbank` + AUX flash |
| Save/load | `Storage` (EEPROM / FRAM), `saveToDisk/loadFromDisk` blit + version pattern |
| CC P-locks | `Step::potVals[5]` (+ Tier-1 already wired `sendControlChange` in `triggerStep`) |

**`Step` (`omni_structs.h:158`) already has:** `notes[6]` (Note), `vel` (Velocity),
`len` (Step Length), `prob` (Chance), `condition` (Math = conditional trig, 0–36 + fill),
`func` (Function), `mfxIndex` (MIDI FX), `nudge : 7` (**microtiming**, ±60),
`potVals[5]` (**P-locks**), `mute`, `accumTPat`.

→ **The only new per-step field is `Repeat` (ratchet 1/2/3-triplet/4).** Add ~2–3 bits.
Retire the dead `Track::startstep`.

So v2 is mostly a **UI / ownership re-architecture around the existing engine**, plus:
**patterns** (new), the **value-palette input model** (new), the **view router + AUX
layer** (new), **live-rec** (partly new), and wiring the **8 modes** to the existing
fields.

---

## B. The gating problem — RAM & pattern storage (decide in Phase 1)

### Rough size budget
| Item | Estimate |
|---|---|
| `Step` (bitfields + `notes[6]` + `potVals[5]`, + Repeat) | **≈ 18 bytes** |
| `Track` (64 steps + header) | **≈ 1.2 KB** |
| `Pattern` (8 tracks + settings) | **≈ 9–10 KB** |
| **16 patterns** | **≈ 150–160 KB** |

### Per-platform reality
| Platform | RAM | Persistent store | Verdict |
|---|---|---|---|
| **OMX-27 V3 (RP2040)** | 264 KB | **1 MB flash FS** + 32 KB FRAM | 16 patterns feasible; can also **stream from flash** |
| **Teensy 4.0** | ~512 KB usable | small EEPROM | 16 patterns fit **in RAM** |
| **Teensy 3.1** | **64 KB** | tiny EEPROM | **16 patterns do NOT fit** — must trim |

### Recommended architecture: a `PatternStore` that hides residency
- Keep only the **active pattern (+ queued)** *resident* in RAM. Everything else lives in
  the backing store.
- **`PatternStore` interface:** `active()`, `queueSwitch(idx, style)`, `commitSwitchAtLoopEnd()`,
  `copy(a→b)`, `clear(idx)`, `saveAll()/loadAll()`.
- **Backends by platform (compile-time):**
  - **V3 / RP2040:** patterns on the **flash filesystem**; load the queued pattern into a
    RAM slot at switch time (a ~10 KB read at loop-end is fine). Full 16.
  - **Teensy 4:** all patterns **resident in RAM** (simplest; fits). Full 16.
  - **Teensy 3.1:** RAM-only, **cap patterns low** (e.g. 2–4) and/or trim `NUM_TRACKS` /
    `NUM_PAGES`. No filesystem to stream from.
- **Compile-time caps** in `consts.h`, gated on `BOARDTYPE`:
  `FORM_NUM_PATTERNS`, `FORM_NUM_TRACKS`, `FORM_NUM_PAGES`, `FORM_PATTERNS_RESIDENT`.

> **Phase-1 exit test:** print `sizeof(Pattern)` on each target, confirm the resident
> working set + globals fits with margin, and prove `saveAll/loadAll` round-trips one
> project. Do **not** build UI before this passes.

---

## C. Build order (each phase compiles + flashes on its own)

Work on a branch. **Keep the current machine-based FORM behind `#ifdef FORM_V1`** so the
device stays flashable during the rewrite; delete it in Phase 14 once v2 reaches parity.
Build **all three targets** every phase (`pico`, `teensy31`, `teensy40`); flash **V3 + T31**.

### Phase 0 — Scaffolding
- New files (don't mutate the working FORM in place): `form2/…` or repurpose once stable.
- `#define FORM_V2`; register a v2 mode entry; feature-flag old FORM.
- Data-model header stubs + the per-platform caps (§B).

### Phase 1 — Data model + RAM/store (the foundation, §B)
- Define `Step` (+`Repeat`, −`startstep`), `Track`, `Pattern`, `Project` (patterns +
  globals: BPM/clock/scale/root/swing/groove).
- Implement `PatternStore` + the platform backends; versioned `saveAll/loadAll`.
- **Gate:** size + round-trip tests pass on all three targets.

### Phase 2 — Single-engine collapse
- Retire `FormMachineType` / `kMachineNames` / `changeMachineAtIndex` / SELECTMACHINE /
  EUCL / `FormMachineNull`; collapse `FormMachineInterface` to the one concrete engine.
- `OmxModeForm` (v2) owns `Pattern::tracks[8]` directly and keeps the OMNI note-out path.
- **Gate:** 8 tracks play from stored data; no machine picker. Parity with old OMNI audio.

### Phase 3 — Clock / playback / pages / play-modes
- Fix **4 pages × 16 steps** (drop zoom); per-page length; enabled-page set drives the loop.
- Wire **play modes** (fwd / rev / fwd-pong / rev-pong / **random-page**) per track.
- Global **BPM / clock** (reuse `clockConfig`), internal + external.
- **Gate:** a pattern plays across pages with play-modes and a loop range.

### Phase 4 — View router + AUX layer (the shell)
- `enum FormView { MIX, STEP, TRANSPOSE, NOTES, PATTERNS, MI }`; router dispatches
  keys / LEDs / display / encoder per view.
- **AUX overlay** (hold AUX): transport (1/2), **rec (3) / rec-mode (4)**, **pages (6–9)**,
  octave (11/12), **view-select (13–18)**, current view flashes.
- OLED: **CC meter** + **4-line page indicator**.
- **Gate:** view switching + AUX transport/octave work in an empty shell.

### Phase 5 — Step view (the core editor)
- Mode selector (3–10); **hold-step value palettes** per mode; **hold-mode-key default**;
  **single-click = clear** (→ buffer); **AUX-while-holding = reset**.
- **Encoder fine-tune** (menu param + hold-step) for velocity/length/chance/**microtiming**.
- Wire each mode to its `Step` field; add **Repeat** playback (ratchet); **P-locks** via
  hold-step + knob (potVals already output in `triggerStep`).
- **Gate:** program every mode, hear it; P-locks send CCs; microtiming shifts timing.

### Phase 6 — Mix view
- Track select + `seqColors`; **F1 mute / F2 solo / F3 rate**; **hold-track** controls
  (mute/solo/play-mode + 25 copy / 26 paste); low-row **triggers programmed steps**.

### Phase 7 — Copy / paste / undo buffer
- One buffer; **F1 Copy / F2 Paste-Cut** logic (empty not buffered; cut⇄paste toggle);
  scopes: **step / track / pattern**; clear/cut → buffer = undo. (Adapt the existing OMNI
  step copy/paste.)

### Phase 8 — Notes / MI / Transpose views
- **Notes:** reuse `omni_note_editor`; F4-start keyboard, F1/F2 copy-paste, 11/12 step nav,
  F3 jump-to-step, 16-box OLED strip, live audition when stopped.
- **MI:** reuse MI-mode keyboard render; plays selected track; records when armed.
- **Transpose:** reuse `omni_transpose_pattern` as-is (its own key map — an accepted island).

### Phase 9 — Track menu (config)
- `ParamManager` pages: **track settings front** (first page = 2-oct mini-keyboard),
  **global back** (BPM / clock / scale / root / swing / groove; count-in toggle).
- This is also where **fine values / microtiming** are edited (Phase 5 hook).

### Phase 10 — Patterns
- Patterns view: slots on the low row, **switch styles (3–6)**, queued switching via
  `PatternStore.commitSwitchAtLoopEnd()`, copy/paste (§ buffer), clear.
- Exercises the Phase-1 store on real switches (esp. flash streaming on V3).

### Phase 11 — Live recording
- Rec arm (AUX 3), quantize-to-step into the selected track, overdub/replace (AUX 4),
  **1-bar count-in from stopped**, **quantize-off** via per-step microtiming (`nudge`).

### Phase 12 — Persistence + polish
- Finalize versioned project save/load; **per-platform trims** confirmed by measurement.
- LED/OLED polish (page colors, flashes, P-lock MAGENTA), pattern chaining / song-mode
  (deferred hook), remaining §10 open items.

### Phase 13 — Retire v1
- Remove the machine abstraction + EUCL + old FORM once v2 is at parity; drop `FORM_V1`.
- Cull dead OMNI bits (`startstep`, `setTest`, commented blocks — see `DESIGN_NOTES.md`).

---

## D. Risks & mitigations

| Risk | Mitigation |
|---|---|
| **Patterns blow the RAM budget** (esp. T3.1) | Phase-1 `PatternStore` + per-platform caps; measure before UI |
| **Flash-stream latency on pattern switch (V3)** | Switch is queued to loop-end; preload the queued pattern into RAM ahead of the boundary |
| **`std::vector` churn per tick** (existing OMNI wart, `DESIGN_NOTES.md §6`) | Replace with fixed arrays during Phase 2/5 (steps ≤64, notes ≤6) |
| **Save-format drift bricking projects** | Version byte + skip-on-mismatch (already the pattern); bump `EEPROM_VERSION` |
| **Big rewrite breaks the flashable device** | Old FORM stays behind `FORM_V1` until parity |
| **T3.1 can't hold the full feature set** | Compile-time trims (fewer patterns / pages / tracks); it's the floor, not the target |

## E. Testing cadence (every phase)
1. `pio run -e pico && pio run -e teensy31 && pio run -e teensy40` — all green.
2. Flash **V3** (`-e pico -t upload`), verify re-enum, then **T31** (`-e teensy31 -t upload`).
3. Exercise the phase's gate on hardware (both devices).
4. Watch the `RAM:`/`Flash:` size lines — especially T3.1 — and log any trim.

## F. Suggested first PR slices
- **PR 1:** Phases 0–1 (data model + `PatternStore` + save/load + size gates). No UI.
- **PR 2:** Phases 2–3 (single-engine collapse + playback). Plays, no new UI.
- **PR 3:** Phases 4–5 (shell + Step editor). First usable instrument.
- **PR 4+:** one view/feature per PR (Mix, Notes/MI/Transpose, menu, patterns, live-rec).

---

_Companion: [`FORM_REDESIGN.md`](FORM_REDESIGN.md) (design) · `FORM_DESIGN.md` (as-built) ·
`form_*.json` (LED frames). Everything on branch `ChordGhostFix`._
