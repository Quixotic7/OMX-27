# omx_mod — OMX-27 ⇄ norns

A norns **mod** that bridges an [OMX-27](https://github.com/okyeron/OMX-27) (RP2040 build) to
[monome norns](https://monome.org/docs/norns/) over a single USB cable.

Because it's a mod, it runs in the background alongside whatever norns script you're
using — only the norns *screen* is borrowed while mirroring is active.

## Features

- **Phase 1 — Screen mirror (this release).** The OMX-27 streams its 128×32 OLED to
  norns over USB-MIDI SysEx; the mod renders it **1:1, pixel-exact, centered** on the
  norns 128×64 screen (black bands top/bottom). Toggle it from the mod menu or the
  **`OMX screen mirror`** param that this mod adds to every script. While on, it
  replaces the foreground script's display; turn it off to hand the screen back.
- **Phase 2 — Norns Takeover ("NT") mode (planned).** A norns script will fully drive
  the OMX (LEDs + screen) and receive its keys/knobs/encoder. Command bytes are already
  reserved in `lib/omx_sysex.lua`.

## Requirements

- An **RP2040 OMX-27** flashed with the `nornsmode` firmware branch (adds the SysEx
  screen-stream; see the firmware repo).
- norns / norns shield running a recent version (needs `screen.poke`).
- USB: OMX-27 (USB-C) → norns host port (USB-A).

## Install

1. Copy this `omx_mod` folder to `~/dust/code/omx_mod` on norns
   (or `;install` from maiden using the repo URL).
2. On norns: **SYSTEM → MODS → omx_mod**, toggle it **on** (K3), then **reboot**
   (mods only load at startup).

## Setup

1. **SYSTEM → DEVICES → MIDI** — note which port number the OMX-27 is mapped to.
2. Open any script → **PARAMS → omx_mod**:
   - set **`OMX midi port`** to that port number,
   - set **`OMX screen mirror`** to **on**.
   (Or use the mod menu: **SYSTEM → MODS → omx_mod** — E2 sets the port, E3/K3 toggle the mirror.)
3. The OMX screen should appear on norns. Turn the mirror **off** to return the screen
   to your script. Your setting persists across reboots.

## How it works

All traffic uses the OMX manufacturer SysEx envelope `F0 7D 00 00 <cmd> … F7`:

| cmd  | dir          | meaning                                             |
|------|--------------|-----------------------------------------------------|
| 0x50 | OMX → norns  | screen page: `[page 0–3][7-bit-packed 128 B page]`  |
| 0x58 | norns → OMX  | `[0/1]` enable/disable mirror streaming             |

The OMX sends only **changed** SSD1306 pages (delta), USB-only (never the TRS DIN
port), 7-bit packed. The mod reassembles the 512-byte framebuffer, unpacks it to a
one-byte-per-pixel buffer, and blits with `screen.poke(0, 16, 128, 32, …)`. Enabling
the mirror also sends `0x58 1` to the OMX (re-asserted every ~2 s so it survives an OMX
reconnect); disabling sends `0x58 0`.

## Firmware side (nornsmode branch)

- `src/midi/norns_link.{h,cpp}` — frame delta + 7-bit encode + SysEx stream
- `src/midi/midi.{h,cpp}` — `MM::sendSysExUSB()` (USB-only SysEx)
- `src/midi/sysex.cpp` — handles incoming `0x58` mirror-enable
- `OMX-27-firmware.ino` — `nornsLink.streamIfChanged()` after the display push

## Notes / limitations

- While the mirror is on, the mod overrides the script's `redraw`; a script that calls
  `screen.update()` directly (outside `redraw`) may cause brief flicker — the 30 fps
  refresh re-asserts the mirror immediately.
- The mirror pauses while you're in a norns menu, so system/mod menus stay usable.
