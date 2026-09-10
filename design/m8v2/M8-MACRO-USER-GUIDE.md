# OMX-27 M8 Macro — User Guide

The M8 macro turns the OMX-27 into a Launchpad Pro for the Dirtywave M8, so the M8 (hardware or the iOS app) drives the OMX's LEDs and the OMX's keys play, sequence and navigate the M8. It works over USB, including the iOS app through a camera-adapter or USB-C connection.

## Setup

1. **On the OMX:** set the `MCRO` parameter to `M8` (MIDI mode page 4, or CONFIG mode → MIDI). Set `M-CH` to the channel you use for the M8's Control Map (default 10).
2. **Save and power-cycle.** When the M8 macro is the *saved* macro, the OMX enumerates over USB as a Novation "Launchpad Pro MK3". The M8 app only talks to a port with that name. If you later pick a different macro, save and power-cycle again and the OMX goes back to being `omx-27-v3`.
3. **On the M8:** MIDI Settings → `CTRL SURFACE` = `LAUNCHPAD PRO`. For the OMX's right-hand navigation keys, also set the Control Map channel to the same value as `M-CH`.
4. **Connect** the OMX to the M8 (or the phone/iPad) over USB.
5. **Enter the macro:** in MI, DRUM, CHORDS or FORM mode, double-click AUX. Double-click AUX again to leave.

The screen shows `WAIT S<n> R<n>` until the M8 answers (S = identity replies sent, R = inquiries received), then `LPP LINK`. Once linked, the M8 paints the OMX keys with its own colours.

## The three views

Hold **AUX** and tap a black key to switch views. The same tap switches the M8's own Launchpad view.

| AUX + key | View |
|---|---|
| 3 | Session (clip launch) |
| 4 | Notes (keyboard) |
| 5 | Seq (sequencer) |

AUX plus keys 1/2 also nudge Launchpad Down/Up in any view.

### Session view (AUX + 3)

Mirrors the M8 Song view. Keys 11-18 are the eight tracks of the Launchpad's top row; the M8 colours them (dull pink = empty phrase, white = populated, green = playhead).

| Key | Function |
|---|---|
| 1 / 2 | Scroll the session box **down / up** on the M8 |
| 3 | CLIP mode: keys 11-18 launch clips, key 19 launches the whole row |
| 4 | MUTE mode: keys 11-18 mute tracks 1-8. Press 4 again to toggle latch / momentary |
| 5 | SOLO mode: keys 11-18 solo tracks 1-8. Press 5 again to toggle latch / momentary |
| 11-18 | Track pads (Launchpad row 8) |
| 19 | Row launch |
| 8 / 21 / 22 / 23 | M8 **Up / Left / Down / Right** |
| 9 | M8 **Option** |
| 10 | M8 **Edit** |
| 24 | M8 **Shift** |
| 26 | M8 **Play** |
| Encoder | Scroll the session box |

Keys 8, 9, 10, 21-24 and 26 send the M8 Control Map, exactly like the original M8 macro, so they behave like the M8's own buttons: hold Shift and tap Down to page, hold Option and use the arrows to select, and so on. Shift, Option and Edit do nothing on their own.

**Momentary vs latch (page 2):** in momentary mode a track is muted only while you hold its key. In latch mode a tap mutes and a second tap unmutes. Leaving mute or solo mode releases everything.

### Notes view (AUX + 4)

The M8 keyboard view. Every pad plays an in-scale note for the current track and the M8 lights root notes white.

| Key | Function |
|---|---|
| 1 / 2 | Scroll the keyboard **down / up** an octave-ish (Launchpad Down/Up) |
| 3 | **Track** (hold): keys 11-18 become the track buttons T1-T8 to switch instruments |
| 9 | **Edit/Rec** |
| 10 | **Play** |
| 11-26 | 16 keyboard pads: the bottom-left 4×4 of the Launchpad (rows 5-8, columns 1-4) |
| Encoder | Scroll the keyboard |

Hold key 9 (Rec) and tap key 10 (Play) to live-record into the current phrase.

### Seq view (AUX + 5)

The M8 sequencer view. Black keys are pitches, white keys are the phrase's note slots.

| Key | Function |
|---|---|
| 1 / 2 | Scroll the keypads **down / up** to reach lower / higher notes |
| 3-10 | Eight keypads (Launchpad row 1) |
| 11-26 | The 16 note slots of the displayed phrase (top-left 4×4). Identical phrases pulse. |
| AUX + 11-26 | The 16 phrase slots of the displayed chain (top-right 4×4) |
| AUX + 9 | **Edit/Rec** tap: toggles the M8 editing submode (lights red on the M8) |
| AUX + 10 | **Play** tap |
| AUX + 8 | **Latch Record held.** The AUX key blinks red while latched; AUX + 8 again releases it |
| Encoder | Scroll the keypads |

The M8 only writes notes into slots while its editing submode is on, so tap AUX + 9 first. With Record latched (AUX + 8) the M8's held-Record combos work: tap a keypad to change the note at the cursor, or tap AUX + 10 to live-record.

## Parameters (encoder)

Click the encoder to switch between selecting and editing, turn to change.

| Page | Param | Meaning |
|---|---|---|
| 1 | — | Status line (`M8 SESS R8 CLIP`, `M8 NOTE`, `M8 SEQ`) and link state |
| 2 | `MUTE` | Latch / Moment for mute mode |
| 2 | `SOLO` | Latch / Moment for solo mode |
| 2 | `RING` | Send the buttons around the grid as `CC` (what a real Launchpad does) or `NOTE`. Leave on CC unless a button stops responding |

The five pots keep sending their CCs on `M-CH`, the same as the original macro.

## Colours

The M8 sends its own colours for pads, track and scene buttons; the OMX shows them through the Launchpad palette, including flash and pulse. Fixed OMX colours:

- AUX purple (blinks red while Record is latched in Seq view)
- Keys 1/2 scroll: indigo (blue in Session)
- Mode keys 3/4/5: magenta / red / yellow, bright when active, blinking when latched
- Nav keys: indigo arrows, orange Option, blue Edit, green Shift, white Play (green when the M8 reports it lit)

## Troubleshooting

- **Stuck on `WAIT`.** R stays 0: the host never sent a Device Inquiry, so it has not adopted the OMX as its control surface. Check the M8's `CTRL SURFACE` setting and that the OMX is showing up as "Launchpad Pro MK3" (save the M8 macro and power-cycle). R climbs but no `LINK`: the host rejects the identity reply.
- **Pads work but no LEDs.** The M8 is sending its LEDs to a different port. Same fix: make sure the OMX enumerates under the Launchpad name.
- **A ring button does nothing.** Flip `RING` on page 2.
- **Shift / Option / Edit seem dead in Session view.** They are modifiers. Hold one and press an arrow to see them act. If combos also fail, check the M8's Control Map channel equals `M-CH`.
- **Test without an M8.** Connect the OMX to a computer and run `OMX-27-firmware/tools/virtual_m8.py`; it plays the M8's side of the handshake and paints test colours.

## Building the old macro

The previous mute/solo macro is still in the source. Define `OMX_M8_MACRO_LEGACY` in `config.h` and rebuild to get it back in the same `M8` slot.
