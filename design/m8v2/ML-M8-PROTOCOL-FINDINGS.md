# M8 Launchpad Pro control-surface — hardware findings

Probed 2026-09-13 against a **real Dirtywave M8** over its DIN MIDI ports (a "USB Midi" USB-MIDI
interface), driven from Web MIDI in the browser. The Launchpad control surface does **not** work
over the M8's own USB, only the hardware MIDI ports. These confirm the protocol our ML macro
implements and match what the iOS M8 app should do.

## Handshake

- Send the Launchpad Pro MK3 identity reply and the M8 begins streaming LED feedback:
  `F0 7E 00 06 02 00 20 29 23 01 00 00 00 01 00 00 F7`.
- Re-sending the identity **resets the M8 back to its Session view** — don't use it as a "refresh."
- The generic vlpp page uses a shorter identity (`… 20 29 00 00 …`); the MK3 form above works too.

## LED feedback (M8 → surface)

- Sent as **Note On**, channel 1, `note` = the button/pad, `velocity` = colour index; off is
  velocity 0 (or Note Off). This is exactly what our macro already caches and mirrors.
- **Ring buttons arrive as notes**, not CC: Shift 90, Session 93, Note 94, Seq 97, Project 98,
  Logo 99, the scene/side column 19 29 39 49 59 69 79 89, and track buttons 101-108.
- Grid pads are `row*10 + col`, row 1 bottom, col 1 left (11-88).
- Root/keyboard highlight colour observed = velocity **3**.

## Button presses (surface → M8)

- The M8 accepts a button press as **either a Note On or a CC** on the same number — tested with
  the Note view button (94): both a note press and a CC press switched the M8 to Note view.
- So our OMX sending the ring as CC (RING = CC default) is compatible. Grid pads we send as notes.

## Note view keyboard (button 94) — CONFIRMS K5 for chromatic

- With the M8 on **D Chromatic**, the keyboard lights roots (velocity 3) at grid pads:
  R8C3, R7C8, R6C1, R5C6, R3C4, R1C2.
- Solving `step = (r-1)*K + (c-1)` with roots a constant mod N: **K = 5, N = 12**. Each row up is
  a perfect fourth (5 semitones); roots recur every 12 steps. This matches the K5 we derived from
  the played-note report, on hardware, and means the auto-detect (root colour → lattice → K) will
  read K5 correctly here.

## Sequencer step editor — CONFIRMED on hardware

Verified 2026-09-13 with the M8 on a PHRASE screen (m8.run shows the display; its keyboard control
navigates the M8: arrows move, Left-Shift = the M8 "select" button, Space = Play, Z = Option,
X = Edit; screen-to-screen is select + a direction). The Launchpad Seq view (button 97) overlays
the phrase; the M8's own screen must already be on that phrase.

**Record must be ON.** Send the Rec button (10). Record lit = step editing is armed; Clear (60)
and Duplicate (50) also light while armed. Without Record, grid pads just audition notes.

**Regions of the 8×8 in the Seq view:**
- **Steps = the top-left 4×4** (rows 5-8, cols 1-4): pads 81 82 83 84 / 71-74 / 61-64 / 51-54.
  Order is top-to-bottom then left-to-right: `step = (8 - row)*4 + (col - 1)`, so step 0 = R8C1
  (pad 81) … step 15 = R5C4 (pad 54). A step that holds a note lights dim.
- **Keyboard = rows 1-4**, chromatic: col +1 = +1 semitone, row +1 = a perfect fourth (K5).
  Pad 11 (R1C1) is the lowest note. The currently selected note highlights.

**Gestures (Record on):**
- **Lock a note:** hold a step pad, then tap a keyboard pad → the note is written to that step at
  default velocity 64 (verified: step 9 → B-3 then C#4; step 12 → C#4). Holding the step also
  moves the M8's phrase cursor to it.
- **Velocity (CONFIRMED — edits the value):** hold a step **that already holds a note**, tap the
  **side/scene column** (19 29 39 49 59 69 79 89) → the step's velocity changes. It's a ramp:
  bottom (19) = low, top (89) = high; taps near an end nudge toward the cap. Verified: step at V
  0x64 went 0x64→0x74→0x7F tapping 89, then 0x0A tapping 19. **The M8 shows velocity in hex;
  default is 0x64 (=100 dec).** An empty step has no velocity to edit — hold a step with a note.
- **Octave (CONFIRMED — edits the value):** hold a step with a note, tap **Down (70) / Up (80)** →
  the held step's note shifts an octave. Verified: F-3 → F-4 (Up), F-4 → F-3 → F-2 (Down).
- Shift (90) also lights while a step is held (instrument-pool / other combos).

This matches the Seq v2 spec's model and maps cleanly onto the OMX: the 16 white keys = the 16
step pads (51-84 in that order), the top row = the keyboard (rows 1-4, K5), velocity mode = the
side column, octave = Up/Down, and the macro must send Rec first. Note our **current** Seq view
already targets these same step pads (top-left 4×4) and row-1 keyboard, so the pad math is proven.

Test residue: chain 11 / phrase 10 now holds C#4 at steps 9 and 12 (created during probing) —
clear or ignore as you like.

## Mute / Solo (Session view track buttons) — CONFIRMED + CORRECTED 2026-09-13

Re-probed end-to-end through the OMX (driven over USB by omxctl, M8 watched on m8.run). This
supersedes the earlier notes below it — two of the earlier claims were wrong.

- Track buttons are **101-108**. Mute button = **2**, Solo button = **3** (as documented).
- **Mute:** hold Mute (2), tap a track → toggles that track's mute (M8 shows `M`). Works.
- **Solo:** hold Solo (3), tap a track → solos it exclusively (M8 shows `S` on it, `M` on the
  rest). Re-tapping a soloed track un-solos it (it *does* toggle — the earlier "doesn't toggle
  off" note was wrong). Solo works — see the timing caveat.

### CRITICAL — a settle gap is required after the modifier

The M8 needs the Mute/Solo button registered as **held before** the track tap arrives. If the OMX
sends `modifier-on` and `track-on` back-to-back (0 ms apart, as it did), the M8 processes the
track first and falls through to its **default, which is mute** — so **Solo behaved exactly like
Mute** (the user's "solo is doing mutes" report). Mute was immune because its fallback *is* mute,
which masked the bug. **Fix: insert ~20 ms (`kModSettleMs`) between the modifier press and the
track tap.** Verified: with the gap, solo-row now shows `4S`; without it, `4M`. The OMX was already
sending the correct MIDI (captured on its USB port: `NoteOn 3`, `NoteOn 105`, `NoteOff 105`,
`NoteOff 3`) — only the timing was the problem.

### CORRECTED track-button LED palette

Decoded by matching the mirrored LED RGB against `lpp_palette.h` and cross-checking the M8's own
per-track M/S mixer panel:
- **muted = 0xff6161 (palette idx 6, alias 73)** — coral. (NOT 5.)
- **soloed = 0x61e9ff (idx 79)** — cyan. (NOT 78.)
- **playing = pulses green (idx 22/88) / grey (idx 2/118)** — not a single index.
- empty = off (0).

### Big caveat — the OMX's LED cache is only as fresh as the M8's stream

The M8 sends a track-button LED note only **when that track's mute/solo changes, and only while
playing**. It does **not** re-send existing states on view-entry or after a re-handshake, and when
**stopped it reports all track LEDs as off**. So the OMX's cache (`ledColor_[101..108]`) only holds
tracks toggled since the macro linked, during playback. The **"all" keys read this cache**, so:
- **Unmute-all / unsolo-all** act only on tracks the cache shows as muted/soloed. This is **safe**
  (it never touches a playing track, so no inversion) and correct for tracks toggled in-session,
  but it **can't see mutes set before linking or while stopped**. That's an M8 limitation, not
  fixable from the OMX without a way to re-request LED state.

**Fixes applied to the Mix view (all in `midimacro_m8v2.cpp`):**
- `kModSettleMs` (20 ms) gap after Mute/Solo-on in every atomic chord (per-track keys 11-26 and
  `doMixAllTracks`). Latch-style pad modes don't need it (they hold the modifier persistently).
- `doMixAllTracks`: unmute-all taps only LED == muted (6/73), unsolo-all taps only LED == soloed
  (79). Symmetric and safe; replaces the old "tap any non-white track" that caused the inversion.
- Palette constants corrected: `kTrkMuted=6`, `kTrkMutedAlt=73`, `kTrkSoloed=79`.
- Per-track mute (11-18) and solo (19-26) verified: `2M` / `6M` for mute, `4S` for solo.

## OMX-to-M8 over TRS/DIN (2026-09-13)

Wired the OMX's MIDI TRS ports to the M8's DIN ports. Two firmware bugs blocked the handshake on
the hardware path; both fixed:

1. **Identity was USB-only.** `sendIdentityReply()` used `MM::sendSysExUSB`; over DIN the M8 never
   saw it. Changed to `MM::sendSysEx` (USB + TRS).
2. **TRS SysEx was never processed.** `OnSysExHW` (the HWMIDI SysEx handler in midi.cpp) only
   echoed the bytes; it never called `processIncomingSysex`, so the M8's Device Inquiry over DIN
   was ignored. Changed it to process like the USB handler.

Outgoing notes/CC already go to both HWMIDI and usbMIDI, and the loop reads HWMIDI, so those paths
were fine.

**Flashing gotcha:** with ML saved, the OMX enumerates as "Launchpad Pro MK3" and picotool cannot
reset it into BOOTSEL (no reset interface), so `pio ... -t upload` fails with "No RPxxxx in BOOTSEL".
It still exposes a CDC serial (identified via ioreg as `/dev/cu.usbmodem*` under the Launchpad
device), so a **1200-baud touch** on that port drops it into BOOTSEL; then copy
`.pio/build/pico/firmware.uf2` to `/Volumes/RPI-RP2/`. (Worth adding a software reboot-to-bootloader
SysEx command later so this isn't needed.)

**Still not linking after the fix + flash** — the OMX enters the macro and sends identity over TRS
but stays on WAIT (no LEDs back). Remaining suspects are physical / M8-config, to check on hardware:
- **TRS MIDI type A vs B mismatch** (the usual culprit) — confirm both ends are Type A and the
  cables are MIDI-TRS, not audio.
- **M8 MIDI output routing** — the control surface must transmit on the DIN/MIDI port
  (MIDI Settings: MIDI OUT PORT includes MIDI, not USB-only).
- **Cable direction** — OMX MIDI OUT → M8 MIDI IN, and M8 MIDI OUT → OMX MIDI IN.

### RESOLVED: OMX-27 TRS polarity switch (2026-09-13)

The OMX-27 has a hardware A/B TRS-polarity switch; it was on **A**, which did not match the M8 /
USB-MIDI interface (both Type A behaviour). Flipping the switch to **B** fixed both directions:

- **Transmit:** OMX key presses now appear on the TRS path (16 messages on the "USB Midi"
  interface, identical to its USB port). Previously zero.
- **Receive:** sending LED note-ons into the OMX's TRS in now lights its keys and the ML macro
  flips to **LINK** (previously stuck on WAIT / R0).

So the firmware fixes (identity over TRS, `OnSysExHW` processing) were necessary and are confirmed
working over the hardware path; the remaining blocker was purely the OMX's polarity switch. With
it on B, wire OMX MIDI OUT → M8 MIDI IN and M8 MIDI OUT → OMX MIDI IN and the ML macro links.

Also set MCRO back to **ML** on the OMX and saved it (it had drifted to the classic M8 macro).
The `R<n>` diagnostic on the WAIT screen can be reverted now that the path is proven.

### END-TO-END VERIFIED on real hardware (2026-09-13)

OMX ↔ M8 over TRS (switch B), OMX driven over USB via omxctl, M8 watched on m8.run. All confirmed:
- **Link + follow-view:** ML macro shows LINK; on entry it auto-followed the M8's view.
- **Notes:** holding an OMX keyboard pad played **F-4 on track 1** on the M8 (waveform + track panel), with the keyboard LED feedback returning to the OMX.
- **Seq step-lock:** OMX Seq view + Record on, hold a step (white key) + tap a pitch (black key) → the M8 wrote **D-4 into phrase step 2** (default vel 64). The live step pads mirrored back as lit keys.
- **Mix mute:** OMX Mix view muting track 1 → the M8 showed **1M / M1** (track 1 muted); entering Mix also sent Session so the M8 went to its Song screen.

Test residue: phrase 10 now has an extra D-4 at step 2. The two firmware fixes (identity over TRS, OnSysExHW processing) are confirmed necessary and working; safe to commit. The WAIT-screen `R<n>` diagnostic can be reverted.

### Additional view verifications on real hardware (2026-09-13)

Closing the remaining untested-on-M8 paths (same rig: OMX over USB via omxctl, M8 on m8.run):

- **Beat Repeat** (AUX+21 → Shift+Session): the M8 enters it (no dedicated M8 screen — it's a
  launchpad overlay; the M8 stays on Song) and streams the layout. Track row (keys 3-10) toggles
  a track's inclusion (LED cyan ↔ dim blue); the range rows (keys 11-18 = row 3, 19-26 = row 2)
  drive the loop — holding one lights it and the green position marker advances. Row mapping matches
  the doc (tracks row 1, loop rows 3 then 2). Exiting to another view sends plain Session and the M8
  leaves Beat Repeat cleanly (LEDs return to the Session layout).
- **Session PAD-SOLO** (Session key 5, then track keys 11-18): solos the track (M8 showed `2S`).
  This path holds the Solo modifier persistently (from `setPadMode`), so — unlike the atomic Mix
  chords — it does **not** need the `kModSettleMs` gap; the modifier is registered long before the
  tap. (Clip view has a Mute chord, not a solo pad-mode, by design.)
- **Notes AUTO-K** (NROW = AUTO): entering Notes, the auto-detect read the D-Chromatic root LEDs and
  locked **K5** — the NROW legend showed `A5`, and the grid highlighted roots with a perfect-fourth
  row interval. Matches the manual-K5 result.
- **Follow-view double-tap**: double-tapping a clip pad in Clip view launched the clip and switched
  the OMX to Seq; the M8 followed into **PHRASE 20**. (Minor UX note: the OLED page doesn't change
  with the view on a double-tap — if you're on a settings page it stays there — but the launchpad
  view and the M8 both switch correctly.)

All four passed; no code changes were needed.
