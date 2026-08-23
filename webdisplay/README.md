# OMX-27 Web Display

A browser mirror of the OMX-27's 128×32 OLED, over USB-MIDI, using the **Web MIDI API**.
It speaks the same screen-stream SysEx protocol as the norns mod, so it needs the
display-mirror firmware (RP2040 / `omx-27-v3`).

![test pattern](../images/) <!-- optional screenshot -->

## Requirements

- **Chrome or Edge** (Safari/Firefox don't support Web MIDI).
- Served over **`https://` or `http://localhost`** — Web MIDI + SysEx requires a secure context.
  Opening the file directly (`file://`) will not get SysEx access.
- OMX-27 (RP2040) connected by USB, running the display-mirror firmware.

## Run it locally

```bash
cd webdisplay
python3 -m http.server 8000
```

Open <http://localhost:8000> in Chrome, press **Connect**, and allow MIDI access.
The OMX-27 is detected automatically by name (`omx-27-v3`).

You can also host `index.html` anywhere with HTTPS (e.g. GitHub Pages) and open that URL.

## Controls

- **Connect** — request Web MIDI access and start streaming.
- **Test pattern** — animated demo with no hardware (verifies rendering).
- **colour / size / glow / grid** — display appearance.
- **OMX pace ms** — round-trip to the OMX: gap between chunk messages. Lower = lower
  latency; raise it if you ever see corruption. Sent live via SysEx (`0x5F`).

## How it works

On connect the page sends `MIRROR_EN` (`F0 7D 00 00 58 01 F7`) to start the stream and
re-asserts it every 2 s (so it recovers if the OMX reboots). It then receives, per frame:

| cmd  | meaning                                                            |
|------|-------------------------------------------------------------------|
| 0x50 | frame chunk: `[chunk 0-15] [frame-id] [checksum] [7-bit 32 bytes]`|
| 0x53 | frame end: `[frame-id] [mask ×3]` — the chunks in this pass        |

Chunks are checksum-verified; any chunk listed in the frame-end that didn't arrive is
re-requested (`0x5E`). The 512-byte SSD1306 framebuffer is unpacked, rotated 180° (the
OMX draws with `setRotation(2)`), and drawn to a canvas.

Protocol reference: `OMX-27-firmware/src/midi/norns_link.h`.
