# FORM — A Producer's Review

*2026-09-03 · branch `q7-2026-3-form_bugfixes` @ `ab70cf8` · companion to `FORM_V2_REVIEW.md`*

This is a musician's-eye review of FORM as it stands after the bugfix pass: what feels right
in the hands, what costs flow in a real session, and concrete proposals. **Nothing in the
Proposals section is implemented** — this document is the deliverable.

---

## 1. What clicks

Things that already feel like an instrument, and why they matter musically:

- **The AUX layer as a shift-world.** Seven views on one held key (AUX+13–19), transport on
  the same hand (AUX+1/2, chord = stop). No menus between you and a view change — this is the
  muscle-memory backbone, and it holds up.
- **Stop twice = kill.** Panic is a first-class gesture. Every hardware sequencer that routes
  external synths needs this, and most bury it.
- **Per-track scale modes (GLOBAL / CHROMATIC / LOCAL).** This is the sleeper feature. Drums
  chromatic, a lead on its own local scale, everything else following the global — almost no
  hardware box does this, and it's what turns "scale mode" from a demo toy into something you
  leave on. The unified 5-cell scale page (one renderer everywhere, palette keys in Seq)
  makes it legible.
- **F2 pick-up/drop.** A step becomes a physical object you grab and put down; the cut/paste
  alternation gives you *swap* for free. And F2+top-row still selects tracks, so the hand
  never leaves the view.
- **Per-step depth.** CC P-Locks (5 slots × pot banks) *and* per-step MidiFX routing — a
  single step sent through its own chord/arp group is genuinely novel; Elektron boxes can't
  route FX per step. Combined with chance/cond/math/accum, one 16-step page goes a long way.
- **Start-on-note recording.** Arm, play a key, the machine starts and the first note lands
  on the 1. That's how it should work and now it does.
- **Tools with preview.** QUANT scrubs a live morph preview; Euclid and Grids preview before
  apply. The tool-jump (hold key 3 + low row) makes 13 tools feel like one page.
- **Polymeter by default.** Per-track rate + per-page lengths with no ceremony.
- **Storage is now trustworthy.** FORM saved last (fixed offsets for every other mode +
  MidiFX), version-gated reinit, MFX configs / CC locks / scale tails all survive power.

## 2. Friction, ranked by what it costs in a session

1. **Tempo lives in one tool.** BPM + tap are inside Tools → BPM. Tempo is a *performance*
   control; reaching it should not require leaving the view you're playing in. (Known open
   decision — see P1.)
2. **No undo.** CLR, randomize, shuffle, mirror, Euclid/Grids apply, paste-over — all commit
   instantly. The fear tax is real: you stop experimenting with the destructive tools on a
   pattern you like, which is exactly when they'd be most fun. (P2.)
3. **The MidiFX editor has no front door.** AUX+*hold* key 6 is a great expert shortcut and an
   invisible one. A producer who doesn't read the manual will never find the chord/arp
   engine. (P3.)
4. **Live-record silently drops notes past 8 held.** Rare, but when it happens (big sustained
   chord + moving line) there's no feedback that anything was lost. (P4.)
5. **Per-track scale persistence needs a decision** (sharpened by the code review): the
   scale modes + NTRY live only in the RP2040 bank-file tail — on **Teensy builds they are
   never saved at all** (every power cycle reverts tracks to GLOBAL/Pressed while their
   notes reload), on V3 they are global-not-per-pattern (switching pattern slots keeps the
   previous pattern's scale modes, though Copy Track does copy them), and the tail is an
   exact-length match with no version byte, so the next added field silently resets all
   saved scale config. Two candidate fixes, both format changes: (a) move the 3 bytes into
   `OmniSeq` + bump `kOmniSaveVersion` — per-pattern scale + all boards, at the cost of a
   one-time invalidation of existing saved FORM patterns; (b) version the tail (marker +
   version + length) — keeps saves, stays RP2040-only and global-scope. (a) is the honest
   fix if a reinit is acceptable now, pre-release. **Owner's call — not implemented.**
6. Quiet limits worth knowing, fine as they are: track hue is global (not per-pattern);
   Teensy 4 saves only the active pattern; LOCK/GROUP on the scale page stay global even for
   LOCAL-scale tracks (document it in the user guide rather than change it).

## 3. Proposals (not implemented — for discussion)

### P1 · BPM from anywhere
**Itch:** friction #1. **Proposal:** `F3 + encoder turn = BPM` in every view. F3 (F1+F2 held)
is already the "track/page-level" modifier and is otherwise unused with the encoder; the
gesture was floated earlier and fits ("could work for BPM"). Tap tempo *stays* in the BPM
tool — a chorded tap gesture is rhythmically unusable anyway. Show the standard BPM popup
while turning. **Cost:** small; one hook in the shared encoder dispatch, per-view opt-out not
needed. **Rejected alternative (stands):** AUX+encoder — AUX+turn must stay "edit the
selected cell."

### P2 · One-level undo for destructive edits
**Itch:** friction #2. **Proposal:** before any destructive commit (tool apply, CLR, F2
paste-over/cut, randomize), snapshot the affected *track's* pattern into a single undo slot;
`AUX + F1` (or an UNDO entry as tool 0) restores it. One slot, last action only, per session
— not a history. **Cost:** one `OmniSeq` copy (~a few KB RAM — verify headroom on Teensy 3.1,
which is the tight board; if it doesn't fit there, gate it to RP2040/T4). The win is
outsized: undo converts every destructive tool from "careful" to "playable."

### P3 · A front door for MidiFX
**Itch:** friction #3. **Proposal:** add an `FX` cell to the MIX machine menu (next to the
TRACK page's MidiFX-routing cell): click = open the routed group's editor, exactly what
AUX+hold-key-6 does today. The expert shortcut stays; the menu path makes it discoverable.
**Cost:** trivial — the submode open call already exists.

### P4 · Feedback when live-record drops a note
**Itch:** friction #4. **Proposal:** when the 8-slot held-note buffer overflows, flash the
rec-arm AUX LED red for ~150 ms and pop `REC FULL` once. No behavior change, just honesty.
**Cost:** trivial.

### P5 · Momentary (performance) mute
**Itch:** live sets are built on ducking a track for two bars. Mutes today are toggles.
**Proposal:** in MIX, `F1 + hold a track key ≥ ~350 ms` = momentary mute that releases with
the key; a quick F1+tap stays a toggle (unchanged). Same shape for F2/solo. Uses the existing
flushNotes path so releases can't strand notes. **Cost:** moderate — needs a hold-timer per
track key and careful interaction with the existing hold-track controls; prototype behind a
CONFIG toggle first.

### P6 · Optional MIDI click out
**Itch:** recording the *first* track against silence. **Prior decision respected:** count-in
was dropped — on a MIDI-only device a count-in has nothing to sound through, and that logic
was right. A *click track* is the adjacent thing that still makes sense **only when routed to
a sound source**: an off-by-default `CLICK` setting (channel, note, velocity, accent-on-1) in
CONFIG that emits quarter notes while playing/recording. If this still feels like the dropped
feature wearing a hat, skip it — P4 + start-on-note already cover most of the need.

### P7 · Per-pattern tempo (opt-in)
**Itch:** switching between two songs' patterns live means riding the BPM tool between them.
**Proposal:** store BPM in the pattern (bank-tail field, RP2040 only), plus a global
`PBPM: On/--` switch (default off) that applies a pattern's BPM when the switch commits at
its boundary. Off = exactly today's behavior. **Cost:** small field + one apply hook; the
subtlety is only applying on *committed* switches, not previews.

### P8 · Pattern chains as a list (v3-scale idea)
**Itch:** `Chained` switch-mode advances patterns, but an arrangement ("A A B C") lives in
your head. **Proposal sketch:** in PATTERNS with switch-mode = Chained, `F3 + tap slots` in
order builds a chain list (repeat a slot by tapping it again); low row shows the chain as
running lights; clear by F3+tap on the current chain display. 16 entries is plenty. This is a
real feature with UI surface — park it for the next design cycle alongside
`form_redesign.json`, don't sneak it into a bugfix branch.

## 4. Decisions reviewed and deliberately left alone

- **Count-in:** stays dropped (see P6 for the narrow adjacent case).
- **AUX + encoder = tempo:** stays rejected; AUX+turn edits the selected cell.
- **Transpose held-key palette 0–9 positive-only:** by design; encoder covers ±48.
- **Transpose randomize keeping random LEN on key 9:** liked; key 8 is the values-only
  variant.
- **Scale palette keys Seq-only:** explicit choice; other views' top rows have jobs.
- **NTRY as a global:** fine — note-entry feel is a player preference, not a track property.

## 5. Small nits (no proposal needed)

- The BPM tool's TAP flash is good; consider also echoing the live BPM value during a tap
  run (it settles fast enough that this may already read fine).
- `FORM_USER_GUIDE.md` should gain one line each for: stop-twice-kill, F2 pick-up/drop
  semantics (initial empty-grab rule), and the LOCK/GROUP-are-global subtlety on LOCAL
  tracks.
