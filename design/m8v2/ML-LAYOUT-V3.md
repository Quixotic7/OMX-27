# ML macro — layout v3 (review + proposal)

Reviewed 2026-09-11 against `OMX M8V2 aux mode layout.json`, `OMX M8V2 session mode layout.json`
(two states: left / right hand) and `OMX M8V2 notes mode layout.json` in the reference folder,
plus the Notes-mode text spec. **All layout JSONs in the reference folder were regenerated on
2026-09-11 to match this document** (aux, session L/R, notes, seq, plus new mix, phrase, beat repeat).

Legend: `LPP n` = Launchpad button n on ch 1 (ring as CC, pads as notes). `CM n` = M8 Control
Map note n on M-CH. Verdicts: ✅ as designed · 🔁 changed, reason given · ❓ needs a decision.

---

## 1. AUX layer (hold AUX) — global, every view

| Key | Design | Proposal | |
|---|---|---|---|
| 1 / 2 | Launchpad Left / Right | LPP 91 / 92 tap | 🔁 2026-09-12: with Shift latched, AUX+1 is the Live-mode toggle |
| 3 | LPP Shift | **Shift latched while AUX is held**: AUX+3 sends `90` on, released when AUX goes up | 🔁 A true three-finger hold is awkward on the OMX. Latching gives Shift+Sess (beat repeat), Shift+Up/Down (instrument pool), Shift+Left (Live mode), Shift+Prj (select project) with two fingers. Key 3 blinks while latched. |
| 4 | — | dark | |
| 5 | LPP Project view | LPP 98 tap | ✅ |
| 6 | Store snapshot | `90` on → `1` tap → `90` off, message `SNAP STORED` | ✅ |
| 7 | Recall snapshot | `1` tap, message `SNAP RECALL` | ✅ |
| 8 | Latch Record | Record held (`10`), AUX blinks red; AUX+8 again releases | ✅ |
| 9 | Edit / Rec | LPP 10 tap (ignored while latched) | ✅ |
| 10 | Play | LPP 20 tap | ✅ |
| 11 / 12 | Launchpad Down / Up | LPP 70 / 80 tap | 🔁 2026-09-12: moved here because on the M8 they move the notes a row, octave-like, matching the OMX AUX octave keys |
| 13 / 14 | Pot bank − / + | local: `potSettings.potbank` wraps, flash as the normal OMX AUX layer | 🔁 2026-09-12 |
| 15 | Session view | switch view + LPP 93 | |
| 16 | Clip Launch view (rows) | switch view + LPP 93 | see §9a |
| 17 | Clip Launch view (columns) | switch view + LPP 93 | see §9a |
| 18 | Mix view | switch view + LPP 93 (see §4) | |
| 19 | Notes view | switch view + LPP 94 | |
| 20 | Seq view | switch view + LPP 97 | |
| 21 | Phrase view | switch view + LPP 97 | |
| 22 | Beat Repeat view | switch view + Shift+Session | |
| 23 | Control view | switch view, no Launchpad button | see §9b |
| 24-25 | — | dark | |

LEDs while AUX is held: 15-23 magenta, current view bright; 11-14 red; 26 yellow; 1/2 red; 3 magenta (blinks when
Shift latched); 5/6/7 magenta; 9 red; 10 white/green; 8 dark red / red when latched.

Removed from the old AUX layer: AUX+3/4/5 = Session/Note/Seq (moved to 11-14), AUX+whites =
phrase grid (see §5 for where it goes).

---

## 2. Session view — left / right hand (saved setting)

Both layouts share keys 0-7, 11-19 and 20. Only the nav cluster moves.

| Key | Left hand (current) | Right hand | Sends |
|---|---|---|---|
| 1 / 2 | Scroll down / up | same | LPP 70 / 80 |
| 3 / 4 / 5 | CLIP / MUTE / SOLO pad mode | same | local |
| 8 | Up | **Option** | CM 6 / CM 3 |
| 9 | Option | **Edit** | CM 3 / CM 2 |
| 10 | Edit | **Up** | CM 2 / CM 6 |
| 11-18 | Track pads, LPP row 8 | same | pads 81-88 / T1-T8 in mute-solo |
| 19 | Row launch | same | LPP 89 |
| 20 | — | — | |
| 6 / 7 | Clear / Duplicate (held, Launchpad 60 / 50), both hands | same | LPP 60 / 50 |
| 21 | — | — | unassigned in both |
| 22 | Left | **Shift** | CM 4 / CM 1 |
| 23 | Down | **Play** | CM 7 / CM 0 |
| 24 | Right | **Left** | CM 5 / CM 4 |
| 25 | Shift | **Down** | CM 1 / CM 7 |
| 26 | Play | **Right** | CM 0 / CM 5 |

✅ As designed. Note the right-hand file labels key 21 "left" but leaves it dark; I read that
as unassigned. If you meant it lit, tell me what it should send.

**Setting:** `HAND` = `L` / `R` on the ML param page 2, next to MUTE / SOLO / RING. It also
flips the LED colours so the arrows are always indigo, Option orange, Edit blue, Shift green,
Play white.

**Persistence (§7):** all four ML params are saved together; they are not today.

---

## 3. Notes view

| Key | Design / text | Proposal | |
|---|---|---|---|
| 1 / 2 | Scroll down / up | LPP 70 / 80 | ✅ |
| 3 | Track (hold): 11-18 = T1-T8 | LPP 101-108 while held | ✅ |
| 6 / 7 | Launchpad Left / Right = previous / next track | LPP 91 / 92 (Track < / >) | ✅ per your note (the notes file on disk is still the 9 Sep version and does not show them). |
| 9 / 10 | Edit/Rec, Play | LPP 10 / 20 | ✅ |
| 11-26 | 16 keyboard pads | **16 consecutive scale notes**, see below | 🔁 |

### Continuous 16-note mapping

The M8 keyboard view lays the grid out in 4ths: each row up repeats the row below shifted by a
fixed number of scale steps, call it **K**. So pad `(row r, col c)` is scale step
`(r - base) * K + (c - 1)`, and the same pitch appears on several pads. Today the white keys are
the 4×4 block rows 8→5, columns 1-4, which runs *downward* in pitch and skips notes.

New rule: key `11 + s` plays scale step `s` (0-15) above the base pad, sent as the pad in the
lowest row that contains it:

```
row = base + s / K        col = 1 + (s mod K)          (K = 4)
keys 11-14 = R_b C1-C4   keys 15-18 = R_b+1 C1-C4   keys 19-22 = R_b+2 C1-C4   keys 23-26 = R_b+3 C1-C4
```

With K = 4 this is a column band four pads wide climbing the grid, which is exactly your
"key 19 is the next note after key 18, and the rest is the third row" once you notice that
`R_b+1 C1..C4` and `R_b C5..C8` are the same pitches. Sending the low-row pad keeps the LED
mirror simple (one pad per key) and matches how the M8 lights roots.

If the M8's row interval turns out to be a true 4th (**K = 3**), the same formula still holds,
the band is just three wide: 11-13 = R_b C1-3, 14-16 = R_b+1 C1-3, … 26 = R_b+5 C2. **Verify on
hardware:** in Notes view the M8 lights root notes white; with the right K the white keys show
a root every 7 keys (major/minor) and the pitch rises smoothly across 11→26. Expose K as a
hidden param (`NROW` 3/4) if it varies with the M8's scale setting.

Base row: R_b = 1 (bottom of the grid, lowest notes); keys 1/2 scroll the M8 keyboard. With
K = 4 the band never leaves rows 1-4, so the M8's own row-scroll is the only scroll needed.

---

## 4. Mix view (new)

Behaves like page 1 of the classic M8 macro, but everything goes through the Launchpad.

| Key | Function | Sends |
|---|---|---|
| 1 | Unmute all | for each track whose LED is red: `2` on → `101+i` tap → `2` off |
| 2 | — | |
| 3 | Go to mixer | CM macro from the classic page (Shift+Up, Left×4, Down) |
| 4 | Recall snapshot | LPP `1` tap |
| 5 | Store snapshot | `90` on → `1` tap → `90` off |
| 6 | Unsolo all | `3` on → each soloed track tap → `3` off |
| 7 | — | |
| 8 | — | |
| 9 | Waveform | CM macro (all four arrows), as classic |
| 10 | Play | LPP 20 |
| 11-18 | Mute track 1-8 | `2` on → `101+i` tap → `2` off |
| 19-26 | Solo track 1-8 | `3` on → `101+i` tap → `3` off |

LEDs: 11-18 mirror T1-T8 from the M8 (white = playing, red = muted) with an orange floor when
the M8 has sent nothing; 19-26 red floor, and bright while the M8 reports the track soloed.
Keys 1-10 fixed colours as the classic page (orange, lime, cyan, magenta, red, yellow, blue).

**Open point:** Mix view depends on the M8 telling us which tracks are muted/soloed through the
T1-T8 LEDs after a tap. If it only reports them while Mute/Solo is held, "unmute all" cannot
know which tracks to tap; the fallback is to tap all eight, which would *toggle* unmuted tracks
on. Verify the LED behaviour first; the mute-per-key part works regardless.

Mix is an OMX-side view of the M8's Session screen, so entering it always sends Session (`93`) to make sure the M8 is there (built 2026-09-12).

---

## 5. Seq view — what changes

| Key | Now | Proposal | Sends |
|---|---|---|---|
| 1 | Scroll down | **Clear** (hold, then tap a slot to delete it) | LPP 60 held |
| 2 | Scroll up | **Duplicate** (hold, tap source, tap destination; double-tap destination = deep clone) | LPP 50 held |
| 3-10 | Keypads | same | pads 11-18 |
| 11-26 | 16 note slots | same | pads 51-84 |
| AUX+1 / 2 | Scroll | scroll the keypads (unchanged, now the only scroll) | LPP 70 / 80 |
| AUX+8 / 9 / 10 | Record latch, Rec, Play | same | |
| Encoder | scroll | scroll the keypads (same as AUX+1/2) | LPP 70 / 80 |

The phrase grid leaves Seq view entirely and becomes its own view (§5b), so the AUX layer stays
identical everywhere and Seq keeps every key for editing.

## 5b. Phrase view (new, AUX+21)

The Launchpad's top-right 4×4 in the M8 sequencer view: the 16 **phrase slots of the current
chain**. Tapping one picks which phrase Seq view edits; holding one and pressing Play loops just
that phrase; with Duplicate held it copies / pastes phrases. Identical phrases pulse, mirrored
from the M8.

| Key | Function | Sends |
|---|---|---|
| 1 | Clear (hold + tap a phrase) | LPP 60 held |
| 2 | Duplicate (hold, source, destination) | LPP 50 held |
| 3-10 | Keypads, same as Seq view, so you can audition while picking | pads 11-18 |
| 11-26 | 16 phrase slots, key 11 = top-left of the block | pads 85-88, 75-78, 65-68, 55-58 |
| AUX+8 / 9 / 10 | Record latch, Rec, Play | as everywhere |
| AUX+1 / 2, encoder | scroll the keypads | LPP 70 / 80 |

Entering Phrase view sends LPP 97 like Seq view does, so the M8 shows the sequencer either way;
the two OMX views are just the left and right halves of that screen. Status line `ML PHRS`.
Typical flow: AUX+21, tap a phrase, AUX+20, edit its slots.

## 5c. Beat Repeat view (new, AUX+22)

The M8's hidden beat-repeat mode (Launchpad Shift + Session). **The M8 must be playing.** Hold
one pad, or two pads, on the range rows to loop that range at normal tempo; the track row picks
which tracks follow the repeat, the rest play on. Release to resume.

| Key | Function | Sends |
|---|---|---|
| 3-10 | Track 1-8 include / exclude (the row the M8 lights **blue**) | pads on the track row, cols 1-8 |
| 11-18 | Range pads, first range row, cols 1-8 | pads 61-68 |
| 19-26 | Range pads, second range row, cols 1-8 | pads 71-78 |
| 1 / 2 | — | |

Entering sends Shift+Session; leaving to any other view sends Session (`93`) first so the M8
drops out of beat repeat. Status line `ML BEAT`. LEDs mirror the M8.

**Row numbering to verify.** The docs call them "lines 6 and 7" (range) and "line 8" (tracks).
Launchpad rows count 1 = bottom, so that reads as rows 6, 7 and 8, which is what the layout file
uses. The same docs say "middle two rows" and "blue *bottom* row", which fits counting from the
top, i.e. rows 3, 2 and 1. One constant in the code; the blue track row on the M8 settles it.

## 5d. AUX LED = Live mode

The M8 lights the Launchpad's Novation logo (`CC 99`) while **Live mode** is active and turns it
off when it leaves. The AUX key mirrors that:

| State | AUX LED |
|---|---|
| Live mode off | purple (as now) |
| Live mode on | the logo colour the M8 sends for 99, through the palette, static / flash / pulse honoured |
| Record latched (any view; AUX+8 is global) | blinks red, overrides the above |
| AUX held | purple, so the shortcut layer always looks the same |

The LED cache already covers note 99 (it holds 0-109), so this is a one-line change in the
Session / Mix / Notes / Seq draw paths. Live mode is entered on the Launchpad with Shift + Left:
AUX+3 (Shift latch) then AUX+1 (Left) does it from the OMX, and the AUX key confirms it.

## 5e. Following the M8's view (built 2026-09-12)

On a real Launchpad, double-tapping a Session pad makes the M8 jump to its sequencer, and the
M8 lights the view button of whatever it is showing (Session `93`, Note `94`, Seq `97`). The
OMX watches those three LEDs: when one lights up from off and it is not the button the current
OMX view implies, the OMX switches to the matching view without sending the button back, and
shows `M8 > SEQ` / `M8 > NOTE` / `M8 > SESSION`. Session, Clip, Clip-column, Mix and Beat Repeat
all imply Session; Seq and Phrase imply Seq; Control ignores it. Beat Repeat ignores the Session
button re-lighting (that is how the M8 shows beat repeat). Needs the real M8 to confirm it lights
`97` on the double-tap.

## 5f. OLED grid page (built 2026-09-12)

In every view the first parameter page draws the 8×8 Launchpad grid per
`design/m8v2/Display/cliplaunch.png`: 8 columns × 4 px from x=48, 8 rows × 4 px from y=1 with
Launchpad row 8 on top. A pad whose LED is off draws nothing; pads the current view's keys can reach (Session: row 8,
Clip: the selected row / column, Notes: the 16-note band, Seq / Phrase: keypads + the 4×4, Beat:
the three rows) draw 3×3, all others 2×2. Mix and Control highlight nothing. Three short
5×8 labels sit on the left: view (`SESS L`, `CLIP R5`, `CLIP C3`), pad mode / `MUTE` chord, and
`LINK` / `WAIT`. Page-indicator dots are omitted on this page (they would overlap the grid).
Renderer: `OmxDisp::dispLaunchpadGrid()`.

## 6. Two suggestions beyond the files

- **Clear / Duplicate in Session view too.** They are now bare 1/2 in Seq view (§5). In Session
  view the M8 uses the same buttons to delete / copy chains in the edit submode, but bare 1/2
  are the box scroll there. If you want them, the free keys are 6 and 7.
- **Show the view in the status line** in every view (`ML SESS L`, `ML MIX`, `ML NOTE`,
  `ML SEQ`) so the hand setting is visible without opening the param page.

---

## 7. Saved settings

Header bytes 55-63 are unused (`EEPROM_HEADER_SIZE` 40, patterns start at 64, CONFIG mode
took 40-54). Save the ML settings in one byte at **offset 55**, bit-packed, with `0xFF` on load
meaning "keep defaults" so no `EEPROM_VERSION` bump is needed:

```
bit 0  HAND   0 = left, 1 = right
bit 1  MUTE   0 = momentary, 1 = latch
bit 2  SOLO   0 = momentary, 1 = latch
bit 3  RING   0 = CC, 1 = note
bit 4-5 NROW  0 = K4, 1 = K3 (Notes row interval)
```

Written by `saveToStorage()` alongside the other header bytes, read in `loadHeader()` and
pushed into the ML macro through a small `MidiMacroM8V2::setSettings/getSettings` pair, the
same way CONFIG mode hands globals around.

---

## 8. Build order

1. Persistence + `HAND` param + right-hand cluster (small, self-contained).
2. New AUX layer: mode keys on 11-16, Shift latch, snapshots, Prj, global Rec/Play. Seq: Clear/Dupe on 1/2. New Phrase and Beat Repeat views.
3. Notes: continuous mapping with K, Track </> on 6/7, ascending order fix.
4. Mix view.
5. Clear/Duplicate latches (§6), status line, Docs + user guide + spec update.

Each phase builds on all three targets and gets a bench pass with `omxctl.py` + `virtual_m8.py`
before flashing. Items to confirm on the real M8 are marked above: Notes row interval K, Mix
view LED reporting, and whether Shift-latched combos behave like a physical Shift hold.

---

## 9. Proposal (2026-09-12): Clip Launch view and Control view

Two more views. Neither needs new protocol; both reuse the pad mirror, the Control Map cluster
and the HAND setting.

### 9a. Clip Launch view

Session view shows one Launchpad row (row 8) and scrolls the M8's box. Clip Launch shows any
row directly: the black keys pick the row, the white keys are that row.

| Key | Function | Sends |
|---|---|---|
| 1 | Clear (hold, then tap a pad to delete the chain) | LPP 60 held |
| 2 | Duplicate (hold, tap source, tap destination) | LPP 50 held |
| 1 + 2 | **Mute chord**: while both are held, Clear/Duplicate are released, Launchpad Mute is held, and keys 3-10 tap the track buttons to mute / unmute tracks 1-8 (LEDs mirror T1-T8, red = muted) | LPP 2 held, 101-108 taps |
| 3-10 | **Select Launchpad row 8-1** (key 3 = row 8, top; key 10 = row 1, bottom, the same order as the OLED grid); track buttons while the mute chord is held | local |
| 11-18 | The eight pads of the selected row (tracks 1-8) | pads row*10 + 1..8 |
| 19-26 | **Row launch**, key 19 = row 8 (top) … key 26 = row 1 | LPP 89, 79 … 19 |
| Encoder | select row (same as 3-10) | local |

Scrolling the session box moved to AUX+11/12 (2026-09-12).

**Column variant (AUX+17, `VIEW_CLIPCOL`):** identical, except keys 3-10 select a Launchpad
*column* (track 1-8, left to right) and keys 11-18 are that column's eight pads, key 11 = row 8
(top) … key 18 = row 1, matching the row-launch keys 19-26 (key 19 = row 8). The mute chord, Clear,
Duplicate, row launch and the encoder work the same. Status `ML CLIP C<n>`.

LEDs: 11-18 mirror the selected row; 19-26 mirror the scene buttons 19…89, so the whole right
column is visible at once (the M8 lights the playing row's scene button); 3-10 = LOWWHITE with
the selected row WHITE. In the M8's edit submode (Rec on) the blue cursor pad shows up wherever
it is, so with the row keys you can walk the cursor row by row. Status `ML CLIP R5`.

Mute / solo pad modes stay in Session view; Clip Launch is launching only. AUX layer as
everywhere (snapshots, Rec, Play, waveform).

### 9b. Control view — the M8's own keys, two-handed

Pure Control Map on M-CH, nothing goes to the Launchpad. The `HAND` setting picks the layout.

| Function | HAND = L (arrows right) | HAND = R (arrows left) | Sends |
|---|---|---|---|
| Up | 8 | 1 | CM 6 |
| Down | 23 | 12 | CM 7 |
| Left | 22 | 11 | CM 4 |
| Right | 24 | 13 | CM 5 |
| Option | 1 | 9 | CM 3 |
| Edit | 2 | 10 | CM 2 |
| Shift ("select") | 12 | 23 | CM 1 |
| Play | 13 | 24 | CM 0 |

HAND = R is the classic M8 macro's control page, key for key (arrows on 1/12/11/13), with
Option/Edit moved from 4/5 to 9/10 and Shift/Play from 16/17 to 23/24 as you specified.

Your note says "12 = select". The M8 has no Select key; selection mode is Shift + Option, and
Shift is the key missing from that list, so I read "select" as **Shift**. Say if you meant
something else.

LEDs: arrows INDIGO, Option ORANGE, Edit RBLUE, Shift GREEN, Play WHITE (GREEN while the M8
reports Play lit on LPP 20, which it still sends while the Launchpad is linked). Everything
else dark. Status `ML CTRL L` / `ML CTRL R`.

Unassigned keys (in L: 3-7, 9-11, 14-20, 24-26): dark. Options if you want them filled:
- a one-octave keyboard on the normal MIDI channel, as the classic control page had on its
  right half (needs the macro to call the host mode's note-on, which the manager already wires up
  through DoNoteOn/DoNoteOff);
- or the Mix-view extras (mixer, waveform, snapshots) — though those are already on the AUX layer.

### 9c. AUX layer mode row (final order)

2026-09-12: **15 Session · 16 Clip (rows) · 17 Clip (columns) · 18 Mix · 19 Notes · 20 Seq ·
21 Phrase · 22 Beat Repeat · 23 Control**, with 11/12 = Launchpad Down/Up and 13/14 = pot bank
−/+ below. The `View` enum is in this order and the AUX key is `15 + view`.

### 9d. Build notes

- `View` enum gains `VIEW_CLIP`, `VIEW_CTRL`; `switchView()` entry: Clip sends Session (`93`),
  Control sends nothing.
- Clip: `clipRow_` (1-8) replaces the fixed `row_ = 8`; pads via `clipRow_*10 + col`, scenes via
  `row*10 + 9`. Row keys and encoder set `clipRow_`, no MIDI.
- Control: one table `ctrlMap[2][8]` of key numbers; `controlMapNote()` already handles the
  send/release bookkeeping through `ctrlSent_`.
- Two more layout JSONs: `OMX M8V2 clip mode layout.json`, `OMX M8V2 control mode layout.json`
  (generated with this proposal). All layouts are now tracked in `design/m8v2/Layouts/`.
- Flash: two small views, expect well under 1 KB; teensy31 is at 96.5%.
