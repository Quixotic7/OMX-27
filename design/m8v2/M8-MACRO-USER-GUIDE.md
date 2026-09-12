# OMX-27 ML Macro (M8 Launchpad Pro) — User Guide

The ML macro turns the OMX-27 into a Launchpad Pro for the Dirtywave M8, so the M8 (hardware or the iOS app) drives the OMX's LEDs and the OMX's keys play, sequence and navigate the M8. It works over USB, including the iOS app through a camera-adapter or USB-C connection.

## Setup

1. **On the OMX:** set the `MCRO` parameter to `ML` (MIDI mode page 4, or CONFIG mode → MIDI). `M8` is the classic mute/solo macro and still works as before. Set `M-CH` to the channel you use for the M8's Control Map (default 10).
2. **Save and power-cycle.** When `ML` is the *saved* macro, the OMX enumerates over USB as a Novation "Launchpad Pro MK3". The M8 app only talks to a port with that name. If you later pick a different macro, save and power-cycle again and the OMX goes back to being `omx-27-v3`.
3. **On the M8:** MIDI Settings → `CTRL SURFACE` = `LAUNCHPAD PRO`. For the OMX's right-hand navigation keys, also set the Control Map channel to the same value as `M-CH`.
4. **Connect** the OMX to the M8 (or the phone/iPad) over USB.
5. **Enter the macro:** in MI, DRUM, CHORDS or FORM mode, double-click AUX. Double-click AUX again to leave.

The screen shows `WAIT S<n> R<n>` until the M8 answers (S = identity replies sent, R = inquiries received), then `LPP LINK`. Once linked, the M8 paints the OMX keys with its own colours.

## Views

Hold **AUX** and press a key from the AUX layer (below) to switch views. The same command switches the M8's own Launchpad view. Mix is an OMX-side view (the M8 stays on its Session screen; the OMX sends Session when you enter Mix). Control is pure M8 Control Map on the M-CH channel (nothing to the Launchpad).

| AUX + key | View | Purpose |
|---|---|---|
| 15 | Session | Clip launch (left/right-hand layout selectable via HAND param) |
| 16 | Clip Launch | Direct row selection: pick a Launchpad row (1-8) and launch clips |
| 17 | Mix | Track mute/solo overview without leaving M8 Session |
| 18 | Notes | Keyboard: 16 consecutive scale notes, Track select, Edit/Play |
| 19 | Seq | Sequencer: keypads + phrase slots, Clear/Duplicate |
| 20 | Phrase | Sequencer phrase selector: 16 phrase slots (like Seq phrase panel) |
| 21 | Beat Repeat | Beat loop: **M8 must be playing** (hold pads to loop a range) |
| 22 | Control | M8 Control Map (arrows, Option, Edit, Shift, Play): two-handed layout |
| 23 | Clip Launch (columns) | Same as Clip Launch, but keys 3-10 pick a track column and 11-18 are that column's clips |

## AUX layer (hold AUX) — global, every view

The AUX layer is identical in all views. Tap keys while AUX is held:

| Key | Function |
|---|---|
| 1 / 2 | **Launchpad Left / Right** (Track <>) — with Shift latched (AUX+3), AUX+1 toggles Live mode |
| 3 | **Shift latch:** hold AUX, press 3; Shift latches until AUX is released. Enables combos like Shift+Session (beat repeat) or Shift+Left (live mode). Key 3 blinks magenta while latched. |
| 4 | — (dark) |
| 5 | **Launchpad Project view** — with Shift latched, selects a project |
| 6 | **Store snapshot:** hold AUX, press 6 to save the M8's current state |
| 7 | **Recall snapshot:** hold AUX, press 7 to load the saved snapshot |
| 8 | **Latch Record held** (Seq / Phrase / all views). Tap to toggle; AUX blinks red while latched. |
| 9 | **Edit/Rec** — global control, works in all views |
| 10 | **Play** — global control |
| 11 / 12 | **Launchpad Down / Up** (octave-like, move notes a row) |
| 13 / 14 | **Pot bank − / +** (cycle through pot banks, same as normal OMX AUX layer) |
| 15-23 | **View selection:** Session, Clip Launch, Mix, Notes, Seq, Phrase, Beat Repeat, Control, Clip Launch by column |
| 26 | **Waveform display** — Control Map macro (Up+Down+Left+Right) |
| 23-25 | — (dark) |

**LED colours:** 15-22 magenta (bright = current view); 11-14 red; 26 yellow; 1/2 red; 3 magenta (blinks when Shift latched); 5/6/7 magenta; 9 red; 10 white/green (green when M8 reports Play lit); 8 dark red (bright red when Record latched).

### Session view (AUX + 15) — left / right hand

Mirrors the M8 Song view. Keys 11-18 are the eight track pads; the M8 colours them (dull pink = empty, white = populated, green = playhead).

The **HAND** parameter (page 2) swaps the navigation keys. Use left-hand if the M8's nav buttons are on your left; right-hand if they are on your right (e.g., iPad on your left, controller on your right).

**Left-hand layout:**

| Key | Function |
|---|---|
| 1 / 2 | Scroll down / up |
| 3 / 4 / 5 | **CLIP / MUTE / SOLO** pad modes |
| 8 | M8 **Up** |
| 9 | M8 **Option** |
| 10 | M8 **Edit** |
| 11-18 | Track pads 1-8 (Launchpad row 8) |
| 19 | Row launch |
| 21 | M8 **Left** |
| 22 | M8 **Down** |
| 23 | M8 **Right** |
| 24 | M8 **Shift** |
| 26 | M8 **Play** |

**Right-hand layout:**

| Key | Function |
|---|---|
| 1 / 2 | Scroll down / up |
| 3 / 4 / 5 | **CLIP / MUTE / SOLO** pad modes |
| 8 | M8 **Option** |
| 9 | M8 **Edit** |
| 10 | M8 **Up** |
| 11-18 | Track pads 1-8 (Launchpad row 8) |
| 19 | Row launch |
| 21 | — (dark) |
| 22 | M8 **Shift** |
| 23 | M8 **Play** |
| 24 | M8 **Left** |
| 25 | M8 **Down** |
| 26 | M8 **Right** |

In both layouts, keys 3/4/5 toggle between CLIP, MUTE and SOLO modes. In MUTE or SOLO, press the key again to toggle latch / momentary (holding the Launchpad button vs. tapping it for each track).

**LED colours:** keys 3/4/5 are magenta/red/yellow (bright when active); nav arrows are indigo, Option orange, Edit blue, Shift green, Play white (green when the M8 reports it lit).

### Clip Launch view (AUX + 16) — direct row selection

Shows any Launchpad row directly. The black keys select the row (1-8), the white keys are that row's pads.

| Key | Function |
|---|---|
| 1 | **Clear**: hold, then tap a pad to delete that chain |
| 2 | **Duplicate**: hold, tap the source pad, tap the destination |
| 1 + 2 | **Mute chord**: hold both and keys 3-10 mute or unmute tracks 1-8 (red = muted) |
| 3-10 | **Select Launchpad row 1-8** (key 3 = row 1 bottom, key 10 = row 8 top; selected row key is white); track mute buttons while the chord is held |
| 11-18 | The eight pads of the selected row (tracks 1-8) |
| 19-26 | **Row launch for rows 1-8** (the Launchpad's right column; key 19 = row 1, key 26 = row 8) |

**Encoder:** turn to select row (same as keys 3-10); no MIDI output. Scrolling the M8's session box is AUX + 11/12.

**Column variant (AUX + 23):** the same view turned sideways. Keys 3-10 select a track column 1-8, keys 11-18 are that column's eight clips (key 11 = bottom row 1, key 18 = top row 8, matching the row-launch keys), and everything else, including Clear, Duplicate, the mute chord and the row-launch keys, behaves the same. Status `ML CLIP C<n>`.

**LED colours:** 11-18 mirror the selected row's pads; 19-26 mirror the scene buttons (the M8 lights the playing row's scene); 3-10 are LOWWHITE with the selected row WHITE. In edit mode (Rec on), the blue cursor pad shows wherever it is in the M8 grid.

Mute/solo modes stay in Session view; Clip Launch is launching only. AUX layer (snapshots, Rec, Play, waveform) works as everywhere.

**Status line:** `ML CLIP R<n>` (e.g., `ML CLIP R5` = row 5 selected).

### Notes view (AUX + 18) — keyboard

The M8 keyboard view. Every pad plays an in-scale note for the current track.

| Key | Function |
|---|---|
| 1 / 2 | Scroll down / up (Launchpad Down/Up; octave-ish) |
| 3 | **Track hold:** keys 11-18 become T1-T8 (track buttons). Hold 3 and tap a track key to switch. |
| 6 / 7 | **Track <** / **Track >** — previous / next track |
| 9 / 10 | **Edit/Rec** / **Play** |
| 11-26 | 16 keyboard pads: 16 consecutive scale steps above the base pad, in ascending order. |

**The 16-note keyboard:** the M8's keyboard view is a 2D grid with root notes repeated every column. The OMX maps keys 11-26 as 16 consecutive scale steps (middle row first, then up by column bands). This makes the layout simpler: key 11 = base note, key 12 = base + 1, …, key 26 = base + 15. The M8 lights roots and pads; the OMX mirrors them.

**Row interval:** the **NROW** parameter (page 3) is `K=4` by default (the M8's standard 4th interval). If your scale or M8 version uses `K=3` (thirds), change NROW to see roots every 7 keys instead. Verify on the M8 by checking that roots pulse at regular intervals.

Hold key 3 (Track) to jump between tracks; hold key 9 (Edit/Rec) and tap key 10 (Play) to live-record into the current phrase.

### Seq view (AUX + 19) — sequencer

The M8 sequencer view. Black keys are pitches, white keys are note slots in the current phrase.

| Key | Function |
|---|---|
| 1 | **Clear** (hold, then tap a slot to delete it) |
| 2 | **Duplicate** (hold, tap source slot, tap destination; double-tap destination = deep clone) |
| 3-10 | Eight keypads (Launchpad row 1): pitches |
| 11-26 | 16 note slots of the displayed phrase (top-left 4×4). Identical phrases pulse. |

Keys 3-10 stay free for pitches; scroll using the encoder or AUX+11/12 to reach lower/higher notes. The M8 only writes slots while in editing submode (turn on with AUX+9), so activate Edit first. With Record latched (AUX+8), use the M8's held-Record combos: tap a pitch keypad to enter a note, or tap AUX+10 to live-record.

### Mix view (AUX + 17) — mute/solo overview

An OMX-side view: navigate tracks and toggle mute/solo without switching the M8's view. The M8 stays in Session.

| Key | Function |
|---|---|
| 1 | **Unmute all** (taps mute for all muted tracks) |
| 3 | Go to M8 Mixer view (Control Map macro) |
| 4 / 5 | **Recall / Store** snapshot |
| 6 | **Unsolo all** (taps solo for all soloed tracks) |
| 9 | **Waveform** display (Control Map macro: all four arrows) |
| 10 | **Play** |
| 11-18 | **Mute** track 1-8: white = playing, red = muted (or floor colour if no M8 feedback yet) |
| 19-26 | **Solo** track 1-8: red = soloed (or floor colour otherwise) |

Mute/solo are sent on the Launchpad's Mute (note 2) and Solo (note 3) buttons; the M8 does the toggling. Keys 1-10 use fixed colours: orange, lime, cyan, magenta, red, yellow, blue as on the classic M8 macro page 1.

### Phrase view (AUX + 20) — phrase selector

The top-right 4×4 of the M8 sequencer: the 16 phrase slots of the current chain. Tap a phrase to edit it; hold and press Play to loop just that phrase; use Duplicate to copy phrases.

| Key | Function |
|---|---|
| 1 | **Clear** (hold, tap a phrase) |
| 2 | **Duplicate** (hold, source, destination) |
| 3-10 | Eight keypads: pitches (same as Seq, so you can audition while choosing) |
| 11-26 | 16 phrase slots (top-right 4×4 of the M8 sequencer): key 11 = top-left |

All other controls are the same as Seq view (Edit/Rec, Play, Record latch on AUX layer). Entering Phrase sends the sequencer button (like Seq), so the M8 stays in sequencer view; the status line shows `ML PHRS`.

Typical flow: enter Phrase view (AUX+20), tap a phrase to select it, switch to Seq view (AUX+19) to edit its slots.

### Beat Repeat view (AUX + 21) — beat loop

The M8's beat-repeat mode (hidden under Shift + Session on the Launchpad). **The M8 must be playing.** Hold one pad, or two pads, on the range rows to loop that range; the track row selects which tracks follow the repeat.

| Key | Function |
|---|---|
| 3-10 | Track select (1-8): the M8 lights these blue when beat repeat is active |
| 11-18 | Range pads, first range row |
| 19-26 | Range pads, second range row |

Hold and release pads to loop: a single pad loops that beat, two pads loop the range between them. Entering Beat Repeat sends Shift + Session; leaving sends Session (`93`) to exit beat repeat on the M8. Status line: `ML BEAT`.

### Control view (AUX + 22) — M8 Control Map

Pure M8 Control Map on the M-CH channel. Nothing goes to the Launchpad. The **HAND** parameter (page 2) picks the layout (left or right hand).

**Left-hand layout (HAND = L):**

| Key | Function | Sends |
|---|---|---|
| 1 | M8 **Option** | CM 3 |
| 2 | M8 **Edit** | CM 2 |
| 8 | M8 **Up** | CM 6 |
| 10 | M8 **Down** | CM 7 |
| 11 | M8 **Left** | CM 4 |
| 12 | M8 **Shift** | CM 1 |
| 13 | M8 **Play** | CM 0 |
| 23 | M8 **Right** | CM 5 |

**Right-hand layout (HAND = R):** (arrows on opposite keys; matches classic M8 macro control page)

| Key | Function | Sends |
|---|---|---|
| 1 | M8 **Up** | CM 6 |
| 9 | M8 **Option** | CM 3 |
| 10 | M8 **Edit** | CM 2 |
| 11 | M8 **Left** | CM 4 |
| 12 | M8 **Down** | CM 7 |
| 13 | M8 **Right** | CM 5 |
| 23 | M8 **Shift** | CM 1 |
| 24 | M8 **Play** | CM 0 |

**LED colours:** arrows are indigo, Option orange, Edit blue, Shift green, Play white (green when M8 reports it lit). All other keys dark.

**Status line:** `ML CTRL L` or `ML CTRL R` (depending on HAND setting).

The AUX layer (snapshots, Rec, Play, waveform, view selection) works as everywhere.

## Following the M8

Double-tap a clip pad in Session or either Clip Launch view and the M8 jumps to its sequencer, exactly like a real Launchpad. The OMX notices the M8 lighting the Seq button and switches to Seq view by itself, showing `M8 > SEQ`. The same happens for Note and Session when the M8 changes view on its own.

## Parameters (encoder)

Click the encoder to toggle between select and edit; turn to change.

**Page 1:** Status line and link state (e.g., `ML SESS L` = Session view left-hand, `ML SESS R` = right-hand, `ML CLIP R5` = Clip Launch row 5, `ML NOTE`, `ML SEQ`, `ML PHRS`, `ML BEAT`, `ML CTRL L/R` = Control left/right-hand).

**Page 2:**

| Param | Setting | Meaning |
|---|---|---|
| `HAND` | L / R | Left-hand or right-hand navigation layout (Session view). Default L. Saved. |
| `MUTE` | M / L | Momentary or latch mute toggle. Default M. Saved. |
| `SOLO` | M / L | Momentary or latch solo toggle. Default M. Saved. |
| `RING` | CC / NOTE | Send ring buttons as CC (default, like a real Launchpad) or as notes. Change only if a button stops responding. Saved. |

**Page 3:**

| Param | Setting | Meaning |
|---|---|---|
| `NROW` | K4 / K3 | Notes view row interval: K=4 (default, standard M8 fourths) or K=3 (thirds). Verify on the M8 by checking root-note spacing. Saved. |

All settings survive power cycles and are saved with CONFIG mode Save.

The five pots always send their CCs on `M-CH`, independent of the view or parameters.

## Colours

The M8 sends colours for grid pads, track buttons and scene buttons; the OMX renders them through the Launchpad palette (including flash and pulse). Fixed OMX colours:

- **AUX** purple (except when Live mode is active: mirrors the Launchpad logo colour; blinks red while Record latched)
- **Keys 1/2** scroll: red
- **Keys 3/4/5** mode selectors: magenta / red / yellow (bright when active, blinking when latched)
- **Navigation keys** (depend on HAND setting): indigo arrows, orange Option, blue Edit, green Shift, white Play (green when the M8 reports it lit)
- **View keys 11-16** (AUX layer): blue (bright for current view)
- **Key 26** waveform: yellow

## Troubleshooting

- **Stuck on `WAIT`.** R stays 0: the host never sent a Device Inquiry, so it has not adopted the OMX as its control surface. Check the M8's `CTRL SURFACE` setting and that the OMX is showing up as "Launchpad Pro MK3" (save the M8 macro and power-cycle). R climbs but no `LINK`: the host rejects the identity reply.
- **Pads work but no LEDs.** The M8 is sending its LEDs to a different port. Same fix: make sure the OMX enumerates under the Launchpad name.
- **A ring button does nothing.** Flip `RING` on page 2.
- **Shift / Option / Edit seem dead in Session view.** They are modifiers. Hold one and press an arrow to see them act. If combos also fail, check the M8's Control Map channel equals `M-CH`.
- **Test without an M8.** Connect the OMX to a computer and run `OMX-27-firmware/tools/virtual_m8.py`; it plays the M8's side of the handshake and paints test colours.

## The classic M8 macro

The original mute/solo + control-page macro is still available as `M8` in the `MCRO` list. Pick `M8` for the old behaviour or `ML` for the Launchpad emulation. Only `ML` changes the USB name.
