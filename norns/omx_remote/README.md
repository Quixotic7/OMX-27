# omx_remote — OMX-27 as a norns controller

A standalone norns **script + library** for driving an OMX-27 in **REMOTE mode**:
the OMX is to norns as a monome grid is to norns. The script owns all 27 RGB LEDs
and the 128x32 screen, and receives every key / encoder / pot event. No mod
required — a single USB cable (OMX USB-C → norns USB-A) and the REMOTE-mode
firmware.

## Install

Copy this `omx_remote` folder to `~/dust/code/omx_remote` on norns, then run
**omx_remote/omx_remote_test** from SELECT.

## Test script

- OMX keys light white while held over a dim rainbow; the encoder moves a yellow
  cursor along the bottom row; holding the encoder button inverts the screen.
- The OMX screen shows the last input event and live pot bars; the top half of
  the norns screen is the exact region mirrored to the OMX.
- **Exit REMOTE mode on the device with AUX + encoder HOLD.** K2 on norns
  reconnects/re-enters REMOTE mode.

## Library (`lib/omx27.lua`)

```lua
local omx = include('omx_remote/lib/omx27')

function init()
  omx.connect()                 -- finds the OMX vport, enters REMOTE mode
  omx.key = function(n, ev) end -- n 0-26 (0 = AUX); ev "down"|"up"|"hold"|"quick"
  omx.enc = function(d) end     -- signed encoder delta
  omx.enc_btn = function(z) end -- encoder button 1/0 (do your own hold timing)
  omx.pot = function(n, v, hires) end -- n 0-4, v 0-127, hires 0-16383
end

-- LEDs (grid-style: stage then show; show() sends only what changed)
omx.led(n, r, g, b)   -- n 0-26, channels 0-127
omx.led_all(r, g, b)
omx.led_show()

-- screen: draw a 128x32 region with the normal norns screen API, then:
omx.screen_send(x, y) -- peek the region at (x,y) (default 0,0), ship it to the OMX
```

`screen_send` is flow-controlled: the OMX acks every frame and the library keeps
exactly one frame in flight, so drawing fast can never build up latency — extra
calls are simply dropped (they return `false`). Only changed 32-byte chunks are
sent. Call it at up to ~15fps from a metro or `redraw()`.

Key gestures need no host timing logic: `down`/`up` always balance, `hold`
fires once when a key is held past the threshold, and `quick` follows the
`up` of a short press.

The wire protocol (`F0 7D 00 00 <cmd> … F7`) is documented in the firmware
repo's `SYSEX_SPEC.md` ("REMOTE mode").
