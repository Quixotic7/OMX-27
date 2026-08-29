# FORM v2 — Branch Review & Path to PR

_A full review of the FORM v2 work on `q7-2026-3-Form-Seq-Upgrade` (133 commits since
forking `q7-2026-3`), performed 2026-08-28: the UI as shipped, the code structure, and
the cleanup needed before this branch can PR. Line numbers reference the tree at commit
`d3496b7`. Companion docs: [`FORM_REDESIGN.md`](FORM_REDESIGN.md) (the design),
[`FORM_IMPLEMENTATION.md`](FORM_IMPLEMENTATION.md) (the original phased plan),
[`../../src/form/DESIGN_NOTES.md`](../../src/form/DESIGN_NOTES.md) (the pre-v2 audit)._

---

## 0. Verdict

**The redesign succeeded functionally but stopped halfway architecturally.** All six
views, patterns (switch styles + chaining), live recording (nudge / length / quantize /
start-on-note), P-Locks, ratchets, and pot banks are built, and the engine core is
sound. Two things keep it from PR quality:

1. **v2 was layered on top of the v1 machine architecture** instead of replacing it —
   the planned Phase-2 "single-engine collapse" never happened. The shell drives the
   engine through 38 `static_cast<FormMachineOmni*>` sites; `FormMachineInterface` is a
   fiction; ~1,200 lines are dead v1 code.
2. **The OLED mixes UI generations.** A good new design system (the track page) covers
   only part of the surface; the rest is legacy layouts and ~30 full-screen popups.

Both are addressed by the plan in §5.

---

## 1. Code review — structure & modularity

### 1.1 The machine abstraction is a fiction (collapse it)

Per `FORM_REDESIGN.md` §0 the switchable-machine concept is dropped, but the
implementation still routes everything through `FormMachineInterface`:

- **38 `static_cast<FormOmni::FormMachineOmni *>` call sites** in `omx_mode_form.cpp`;
  the v2 shell uses ~60 OMNI-specific public methods that aren't on the interface at all.
- The interface survives only as virtual-call noise, the mostly-bypassed
  `doesConsume{Keys,LEDs,Display,Pots}` protocol, the `void* context_` + C-fptr note
  callbacks, and a machine-type byte in the save format.
- `getClone()` (omni.cpp:166) doesn't even copy `seq_` — the machine copy/cut/paste
  plumbing it serves (`omx_mode_form.cpp:159–211`) is unreachable in v2 anyway.

**Collapsing** (`FormMachineInterface* machines_[8]` → concrete track objects, or one
engine over `Track[8]`) deletes the casts, the 8 heap `new`s, the clone machinery, the
consume protocol, and the static-init `ParamManager` self-healing hack
(omni.cpp:106–148, re-checked every `loopUpdate`) — and enables the live pattern to be
a **pointer into `patterns_`** instead of a 10.6 KB copy per switch
(`snapshotActivePattern`, shell:216).

### 1.2 Six parallel dispatch sites (table them)

Every view already has a uniform method set (`onKeyUpdateX` / `updateXLEDs` /
`onDisplayX` / `onEncoderX` / `onEncoderButtonX`), but six places each re-implement
"which view gets this event": keys (shell:3105), LEDs (:3382), display (:3484), encoder
turn (:2755), encoder click (:2870), plus per-view special cases in
`updateShortcutMode` (:2429) and `onPotChanged` (:2596). A `FormViewHandler`
table/struct indexed by `formView_` collapses all six to one lookup each.
Prerequisites:

- Fold Mix's inline key routing (:3149–3177) into `onKeyUpdateMix`.
- Make the per-view shortcut freezes a `bool freezeShortcuts()` hook.
- AUX layer + the shared track page stay in the container.

Do this **before new features** — otherwise every feature adds a case to six switches.

### 1.3 Dead code (~15% of the shell, ~500–600 lines of the machine)

Shell (`omx_mode_form.cpp`):
- The `FORMMODE_BASE` fallback switch (:3197–3311) is effectively unreachable (every
  view returns earlier or the machine consumes).
- ~500 lines of commented-out v1 blocks (:2786–2860, :3584–3667, :3676–3803).
- Dead members/functions: `stepPasteArmed_`, `viewSelectEdit_` (written, never read),
  `isTrackPage()` (no callers), `onEncoderTrackPage`/`onEncoderButtonTrackPage`
  (no-ops), `getMachineName()` (no callers), empty `saveKit`/`loadKit`, `setTest()`,
  `FormModePage` enum + the `params` pages, `changeFormMode`/one-value `FormMode` enum,
  `omxFormGlobal.quantizeReset`.

Machine (`form_machine_omni.cpp`) — unreachable v1 UI:
- AUX key branch (:2367–2384 — would desync `omniUiMode_` from `formView_` if it ever fired).
- The MIX/CONFIG step-held machinery (`stepHeld()`/`selStep()` :574–609, key handling
  :2392–2464, func-palette LEDs :2287–2300, step-held pot editing :1716–1770,
  `setPotPickups` :208–236).
- Old `OMNIPAGE_STEP1`/`OMNIPAGE_STEPCONDITION` pages (superseded by the shell's param
  grids; fenced off by `seqMenuAtStart`, but still reachable via the Mix-view encoder —
  see bug B6).
- Zoom plumbing (`zoomLevel_` pinned 0, still threaded through `key16toStep`, LED
  playhead math, F3 length).
- `dispTrackHold` / `dispStepPlayModes` in `omx_disp.cpp` (:576, :823) — no callers.

`form2/`: only `form2_config.h` is included by firmware. `form2_data.h` /
`form2_store.{h,cpp}` are unused scaffolding — either wire the store in for bank
persistence (§1.5) or delete them (their size estimates are stale anyway, §1.5).

### 1.4 Duplication (extract ~150+ lines of shared helpers)

Shell:
1. **F1+page gesture** (select / double-click solo / hold-two loop-range): Notes
   :1148–1182 vs Step :1580–1621, near-verbatim incl. `heldPageMask_` bookkeeping.
2. **F1/F2 quick-tap-copy/paste vs held-modifier** idiom: Patterns :407–438 vs Notes
   :1093–1114.
3. **F2 + top-row track select**: Notes :1195–1214 vs Step :1622–1642 — identical.
4. **F3 rate/length keys + `dispTrackLength` display**: three copies each.
5. **`stepState[16]` build loop**: :1364, :2156, :2194.
6. **Scale page marshalling**: MI :686–698 vs Notes :1441–1455, byte-for-byte; the
   `dispStepParams` 4-slot marshalling pattern appears 5×.
7. **Track hue color expression** at 7 sites → `trackColor(t)`.
8. **`kViewNames` defined twice** (:315, :3073) + `kViewTags` (:2261) → one file-scope pair.
9. Hold-popup delay mechanic twice (`stepHoldStartMs_…` vs `notesHoldStartMs_…`) →
   one `HoldPopup` helper; three bespoke held-key bitmasks → one tracker.

Engine:
1. **"Write a step param with clamp" exists 6×** (`editStepParam` :871,
   `editParamDefault` :839, `clearStepParamLock` :896, `editPage` :2630–2673, dead pot
   branch :1722, `setStepPalette` :671) with ranges restated in 3 places → one
   `setStepParam(Step*, pid, value)` + a `kStepParamRange[]` table.
2. **Param value formatting 4×** (:757, :931, :779, :2905) → one formatter.
3. **≥4 copy/paste-buffer state machines** where the design (§3.5) wants exactly one.
4. `triggerStep` vs `auditionStep` note-iteration duplication; three note-clearing
   helpers; `recordStepIndex` is a stale duplicate of `recordResolveStep`'s first half;
   `lenFromSteps` hand-inverts `getStepLenMult`.

### 1.5 RAM & persistence (re-measure; the bank doesn't save)

- The design's "145 KB for 16 patterns" came from the **unused** `form2` model. The
  shipping `FormPattern` (OmniSeq×8) is **10,576 B → 165 KB for 16 patterns**; with live
  machines + `patternBuffer_` ≈ 187 KB, ~71 % of RP2040 RAM. Likely fits, but
  re-measure before adding features; revisit the Teensy trims against real sizeof.
- **The pattern bank is never persisted** — `saveToDisk` (shell:4053) saves only the 8
  live machines (the active pattern). `switchStyle_`, `recQuantize_`, `trackHue_` also
  don't save. Bank persistence needs the V3 flash-FS backend; if deferred past the PR,
  state the limitation in the PR description.
- `Track::startstep` is dead (1 B × 8 × 17 copies); `OmniSeq::potMode` ("CC Fade") is
  stored/editable/displayed but unimplemented in `triggerStep`.
- Audio-path heap: four `std::vector`s per machine (`shuffleVec`, `tempShuffleVec`,
  `triggeredNotes_`, `noteOns_`) — bounded, but fixed arrays would remove tick-path
  allocation (known wart, `DESIGN_NOTES.md` §6).

---

## 2. Bugs found (substantiated)

User-visible first. B = bug, file:line in the tree at `d3496b7`.

| # | Bug | Where |
|---|---|---|
| B1 | **Stuck preview notes**: MI/Notes swallow key **releases** while AUX is held (early return on `FORMSHORTCUT_AUX`) — press piano key, press AUX, release key → note-off never sends | shell:550, :1055 |
| B2 | **Recording commits to whichever track is selected at RELEASE** — press resolves step/nudge on one machine (:809), commit re-fetches `getSelectedMachine()` (:832); switching tracks mid-hold writes the wrong track | shell:809/832 |
| B3 | **Stale page gesture**: `heldPageMask_`/`pageGestureDone_` not reset by `setFormView` → a hold/release/AUX sequence leaves a stuck bit that later fires a phantom loop-range | shell:279 |
| B4 | **Stuck queued pattern**: changing switch style to Instant while queued — `loopUpdate` only commits styles 0/1, slot blinks forever | shell:443, :2704 |
| B5 | **Probability off-by-one**: `random(100) > prob` → 50 fires 51 %, 99 fires 100 % | omni.cpp:1067 |
| B6 | **Track LEN desync**: legacy TRACK menu page writes `track->len` without `syncLen()`; next page/length edit silently reverts it (goes away when the page is modernized, §4) | omni.cpp:2711 |
| B7 | **`omxFormGlobal.selMidiFX` never written** → AUX+20 MidiFX shortcut always opens group 1 | omx_form_global.h:51 |
| B8 | Replace-mode rec clears the step **before** the 8-slot capacity check — a 9th simultaneous note erases content then drops | shell:816–823 |
| B9 | Step-view Note palette passes `getNoteByDegree` result unvalidated to add/preview (octave extremes can leave 0–127) | shell:1537 |
| B10 | `omxDisp.isDirty()` called for a side effect it doesn't have (meant `setDirty()`); masked today | shell:2891 |
| B11 | PRE/!PRE inconsistency: `evaluateTrig`'s prob-100 early-out skips `prevCondWasTrue_`, prob-99-pass sets it — pick one semantic | omni.cpp:1065 |
| B12 | Transpose key palette writes only 0–9 — **no negative transpose from keys** though the display shows −10..10 (encoder allows ±48); also fires on key-up | omni_transpose_pattern.cpp:253 |
| B13 | MI QUANTIZE/CLEAR submenus not reset in `onModeActivated/Deactivated` → stale `quantOrigNudges_` on re-entry | shell:2485 |
| B14 | Ratchet holds raw pointers into `seq_`; clear/paste during playback can re-init the pointed-at step mid-ratchet — use indices | omni.h:403 |
| B15 | AUX/shortcut desync: release F1 with AUX already down → `shortcutMode`=AUX while `midiAUX` stays false; AUX keys live but LEDs/view-commit are not. Derive both from one source | shell:2457, :3014 |
| B16 | ~~`noteOns_` cap drops the 17th note after already sending offs for matching notes~~ — **re-verified during the fix pass: not a bug.** Removing a matching note frees a slot before the insert; when full with no match, no offs are sent. No change. | omni.cpp:1441–1460 |
| B17 | Minor: uninit `copyBuffer_`/`undoBuffer_` (global zero-init saves it — add `= nullptr`); `patternBuffer_` never freed in `cleanup()`; `getStepLenString` dead statement after return; `heldTrackKey` comment describes keys that don't match the code (shell:2332) |  |

Verified non-issues: bar-tick wrap detection; `channel = i & 0x0F`; the palette
shortcut-freeze logic; pause/stop note-off flushing (rate-change-mid-note is the only
gap).

---

## 3. UI review — diagnosis

Full-screen inventory found ~25 distinct OLED screens across **three coexisting UI
generations**:

- **Family A–D (new, good)**: the track page (`dispSeqTrackPage`) + step-row companions
  (`dispStepOverview`, `dispStepNoteKeyboard`), LEN|RATE (`dispTrackLength`), and the
  new 4-param grid (`dispStepParams`).
- **Family E (legacy legend rows, `dispGenericMode2`)**: the machine-menu pages TRACK /
  TRACKMODES / SEQMIX / SEQTPOSE / SEQMIDI / TIMINGS / SCALE — different fonts,
  different selection convention, page dots the new pages lack.
- **Specialized editors**: STEPNOTES / STEPPOTS (`dispCenteredSlots`), the Transpose
  view + TPAT (`dispValues16` bars).
- **One-offs**: Patterns screen, Mix-F2 SOLO/Fill split, QUANTIZE submenu, and ~30
  actions still using 500 ms full-screen popups while a similar number were converted
  to inline state (muting from Mix-F1 is silent; the same mute from hold-track pops
  "MUTE").

Information-architecture gaps: Notes/Patterns/Transpose show no view tag / track / BPM
/ transport / rec state; the active step-edit mode is popup-and-LED only; menu depth
has no indicator on the new-style pages; the promised persistent chrome (CC meter
everywhere, 4-line page indicator, P-Lock magenta) is transient, unbuilt, and unbuilt
respectively — and the spec now contradicts itself on the CC meter (§2 "always on
screen" vs decision 29 "transient"). The step-row tick glyph means *playhead* on the
track page but *selected step* on Notes.

Redundancy: scale is reachable in 3 menus in 2 looks; SEQMIX duplicates Mix, TPAT
duplicates the Transpose view, STEPNOTES/STEPPOTS duplicate what the shell param grids
+ Notes view cover; rate/BPM/mute/track-select each live in 3–4 places.

Spec drift worth reconciling in `FORM_REDESIGN.md`: the §4.0 encoder view selector is
documented as built but was since **removed** (d3496b7/5dc52c9); hold-a-mode-key →
default palette was never built; pages moved off AUX 6–9 to F1+3–6 (Step only); Mix
hold-track 25/26 copy/paste not built; `kStepModeColors` in code doesn't match the §9
table; the "record not yet wired" note is stale (it is wired).

---

## 4. UI decisions (2026-08-28)

Review suggestions were **not all accepted**. Decided:

1. **Keep the specialized editors as they are** — they're good and usable:
   - the **Transpose** view/menu (`dispValues16` bars, incl. the TPAT page),
   - the **notes menu** (Step view encoder page index 3 — STEPNOTES, `dispCenteredSlots`),
   - the **CC edit view** (Step view encoder page index 4 — STEPPOTS).
   These are *specialized editors*, not param pages; their bespoke layouts earn their
   keep. Do **not** port them to the 4-param grid or delete them.
2. **Modernize the legacy 4-param legend pages** (Family E: TRACK / TRACKMODES /
   SEQMIX / SEQTPOSE / SEQMIDI / TIMINGS / SCALE) to the new `dispStepParams` layout.
3. **Value-label rules for the new 4-param grid** (`dispStepParams`) — needed before
   (2) lands, since the chunky value font overflows on text:
   - **Numeric values up to 3 digits fit the cell** ("127", "−60", "1:16" where it
     fits) — display inline as today.
   - **3-character TEXT values do NOT fit** (e.g. "TRK") — a cell must never show a
     squeezed/overflowing text value. For text-valued params, the cell shows an
     abbreviation that fits (or the selection state alone), and the **full value name
     is shown only while turning the encoder** (transient, e.g. the popup/message line
     or an expanded cell during edit).
   - Corollary: audit every param's value strings against the cell width once the rule
     is implemented; no per-page exceptions.
4. The remaining §3 suggestions (shared persistent header on every screen, popup→inline
   rule, a mode field on the SEQ screen, CC-meter/page-indicator/P-Lock-tint
   completion, glyph split for playhead vs selection) are **proposals — open, not
   agreed**. Evaluate individually after the code cleanup lands.

---

## 5. Path to PR

Ordered; each step is independently landable.

1. **Bug pass** — B1–B15 + B17 (§2). ✅ **Done 2026-08-28** (B16 re-verified as not a
   bug; all three targets build). Notes on the fixes:
   - B1 grew a small mechanism: per-key preview-note bookkeeping
     (`previewKeyOn/previewKeyOff` + `previewNote_[27]`/`previewMach_[27]`) so every
     manual note-off is sent with the note/track that actually played, regardless of
     modifiers, octave changes, or track/view switches mid-hold. MI, Notes, and the
     Step Note palette all route through it; `onModeDeactivated` flushes it.
   - B2: `RecHeld` gained a `track` field, captured at press.
   - B12 changed the transpose palette mapping: keys 1–10 now enter **−5..+4**
     (key 6 = 0) instead of 0..9, and only on key-down.
   - B15: `FORMSHORTCUT_AUX` now additionally requires `midiSettings.midiAUX` (set
     only by a real AUX press event), so a swallowed AUX press can't half-arm the layer.
2. **Deletion pass** — §1.3. ✅ **Done 2026-08-29** (~1,500 lines removed; all three
   targets build). What went, beyond the list:
   - Shell: FORMMODE_BASE fallback (its one live path — the Mix F3 machine forward —
     preserved as a plain `selMachine->onKeyUpdate(e)`), the legacy display switch
     (Mix default path preserved), the v1 spec comment block in the header, the
     machine cut/copy/paste plumbing + `copyBuffer_`/`undoBuffer_` (`setMachineTo`
     now deletes the old machine directly), `FormModePage`, `FormMode`/`formMode`,
     `quantizeReset`, `selMidiFX`, `potPickups`, the no-op view-selector functions,
     `setTest`, `getMachineName/Color` + `kMachineNames/Colors`.
   - Machine: the AUX uiMode-switch branch, the whole v1 step-held machinery
     (`stepHeld_`/`stepHeld()`/`stepReleased()`/`selStep()`/`setPotPickups` + the
     step-held pot-editing branch + its LED/func-palette blocks + the `doesConsume*`
     special cases), the F1/F2 step copy/paste + double-click/F1+AUX note-editor
     entries (which could desync `omniUiMode_` from the view router), the
     `onKeyQuickClicked` AUX-exit override (same desync class — its removal also
     stops it swallowing AUX releases in Notes view), `recordStepIndex`,
     `encoderSelect_` (interface).
   - `form2_data.h` / `form2_store.{h,cpp}` deleted; the geometry macros moved into
     `form2_config.h`, whose stale 145 KB size note now points at the real ~10.6 KB
     `FormPattern` measurement. `dispTrackHold`/`dispStepPlayModes` deleted from
     `omx_disp`.
   - Deliberately KEPT (still reachable, dies in step 3/4): the legacy
     `OMNIPAGE_STEP1`/`STEPCONDITION` menu pages (Mix's encoder can walk to them),
     the zoom plumbing (pinned, simplification not deletion), `saveKit`/`loadKit`
     stubs (presetManager contract), `getClone`/`changeMachineAtIndex` (save format).
3. **Param-page modernization** — §4 decisions 2–3. ✅ **Done 2026-08-29** (all three
   targets build). What shipped:
   - The seven legend pages (TRACK / TRACKMODES / SEQMIX / SEQTPOSE / SEQMIDI /
     TIMINGS / SCALE) render in the shared `dispStepParams` grid via a new
     `dispParamGrid()` helper; `dispGenericMode2` is no longer used by FORM.
   - The duplicate STEP1/STEPCONDITION menu pages are **gone** (enum + pages
     restructured; the menu now runs STEPNOTES → STEPPOTS → TPAT → the seven param
     pages, entered at STEPNOTES from both Step and Mix). The specialized editors
     (STEPNOTES / STEPPOTS / TPAT, Transpose view) are untouched per decision 1.
     Dead `stepParams_` ParamManager removed. Page param counts now match the real
     param counts (no empty selectable cells).
   - **Label rules applied** (decision 3): cells show ≤3-digit numerics inline and
     ≤2-char text ("On"/"--", ">>"/"<<", play-mode codes `--/PG/RD/R2/SF/SH`,
     transpose-mode codes `GI/SE/LI`, swing-div `16/8`, rate as the bare divisor);
     full names pop transiently while the encoder turns (play mode, transpose mode,
     `RATE 1:n`, `SWING 16th/8th`, scale name). The MI menu's CLEAR cell — the
     original "TRK" overflow — now shows ">" (click to open). The shell's Notes/MI
     scale pages unified to the same "--"/"On" convention as the machine SCALE page.
   - Full-width overlays (hold-step big value, `dispStepOverview`) are exempt — the
     rule constrains only the 32px grid cells.
   - **Mix encoder pages** (added 2026-08-29, user request): Mix previously showed only
     the track overview while its encoder walked the machine menu *blind* (the display
     never rendered it). Mix now has its own flat cursor (`mixCursor_`, like MI/Notes):
     **0** = track overview (edit-turn selects the track) · **1–8** = **LEVELS**, a
     per-track default-velocity mixer (8 bars, new `dispMixLevels` renderer; edit-turn
     adjusts the bar under the cursor and pushes to every step without its own velocity
     lock) · **9–12** = **TRACK** grid: Mute / Solo / Gate / Rate for the selected track
     (label rules apply; rate pops "RATE 1:n" while turning). The blind machine-menu
     walk from Mix is gone — the menu is reached from the Step view.
4. **Interface collapse** (§1.1) + **view-handler table** (§1.2) — mechanical but wide;
   much smaller after step 2. Save-format change rides the existing version byte.
5. **Extraction pass** (§1.4) — opportunistic; the page-gesture and quick-copy/paste
   helpers alone remove ~150 duplicated lines.
6. **Persistence decision** (§1.5) — bank save/load via the V3 flash-FS backend, or
   explicitly deferred with the limitation noted in the PR description. Re-measure RAM
   against real `sizeof(FormPattern)` and reconfirm the Teensy trims.
7. **Spec sync** — update `FORM_REDESIGN.md` for the drift in §3 and the decisions in
   §4, so the doc matches what the PR ships.

Open UI proposals (§4.4) come **after** the PR, alongside the planned new features.
