#!/usr/bin/env python3
# REMOTE mode PC test harness — drives the OMX-27 the way a norns lua script
# would: full LED + screen control, with key/encoder/pot callbacks.
#
#   python3 remote_test.py            # interactive demo (Ctrl-C to quit)
#   python3 remote_test.py --port omx-27-v3
#
# Demo: screen shows a title, the last input event, and a live pot bar.
# LEDs idle as a dim rainbow; held keys light white; the encoder moves a
# bright cursor along the bottom row; the encoder button inverts the screen.
import argparse, time, sys
import rtmidi
from PIL import Image, ImageDraw

HDR = [0x7D, 0x00, 0x00]
CMD_INPUT, CMD_STATUS = 0x51, 0x52
CMD_LED, CMD_LED_BATCH, CMD_LED_SHOW = 0x59, 0x5A, 0x5B
CMD_DRAW, CMD_DRAW_UPD = 0x5C, 0x5D
MODE_REMOTE = 10  # OMXMode enum index

# ---------------------------------------------------------------- transport
class Omx:
    def __init__(self, port_match):
        self.out = rtmidi.MidiOut()
        self.inp = rtmidi.MidiIn()
        self.out.open_port(self._find(self.out, port_match))
        self.inp.open_port(self._find(self.inp, port_match))
        self.inp.ignore_types(sysex=False, timing=True, active_sense=True)
        self.last_frame = None
        # callbacks, norns-style
        self.on_key = lambda index, z: None        # OMX_Key(index, z)
        self.on_encoder_turn = lambda d: None      # OMX_Enc(d)  d = +/-n
        self.on_encoder_button = lambda z: None    # OMX_Encoder(z)
        self.on_pot = lambda index, v, hires: None # OMX_Pot(index, v)
        self.on_status = lambda s: None

    @staticmethod
    def _find(dev, match):
        for i, p in enumerate(dev.get_ports()):
            if match in p.lower():
                return i
        raise RuntimeError(f"no MIDI port matching '{match}' in {dev.get_ports()}")

    def sysex(self, cmd, *data):
        self.out.send_message([0xF0] + HDR + [cmd] + [d & 0x7F for d in data] + [0xF7])

    # -- mode
    def enter_remote_mode(self):
        self.sysex(0x51, 0x05, MODE_REMOTE)  # input-injection MODE command

    # -- LEDs (r/g/b 0-127; call led_show() to latch, like grid:refresh())
    def led(self, index, r, g, b):
        self.sysex(CMD_LED, index, r, g, b)

    def led_all(self, colors):  # colors: list of 27 (r,g,b)
        flat = []
        for (r, g, b) in colors:
            flat += [r & 0x7F, g & 0x7F, b & 0x7F]
        self.sysex(CMD_LED_BATCH, 0, len(colors), *flat)

    def led_show(self):
        self.sysex(CMD_LED_SHOW)

    # -- screen: pass a PIL 128x32 image (mode '1' or 'L'); rotated + packed here
    def draw(self, img, force=False):
        buf = self._pack(img)
        for c in range(16):
            chunk = buf[c * 32:(c + 1) * 32]
            if not force and self.last_frame is not None and self.last_frame[c * 32:(c + 1) * 32] == chunk:
                continue
            self.sysex(CMD_DRAW, c, *self._enc7(chunk))
            time.sleep(0.002)  # pace so the device FIFO keeps up
        self.sysex(CMD_DRAW_UPD)
        self.last_frame = buf

    @staticmethod
    def _pack(img):
        # PIL image -> SSD1306 page layout. The panel is mounted 180deg vs the
        # buffer (same flip omxctl.render() applies when capturing).
        img = img.convert("1").rotate(180)
        px = img.load()
        buf = bytearray(512)
        for y in range(32):
            for x in range(128):
                if px[x, y]:
                    buf[x + (y // 8) * 128] |= 1 << (y % 8)
        return bytes(buf)

    @staticmethod
    def _enc7(data):
        out = []
        for i in range(0, len(data), 7):
            grp = data[i:i + 7]
            hi = 0
            for j, byte in enumerate(grp):
                hi |= ((byte >> 7) & 1) << j
            out.append(hi)
            out += [b & 0x7F for b in grp]
        return out

    # -- event pump: call often; dispatches callbacks
    def pump(self):
        while True:
            m = self.inp.get_message()
            if not m:
                return
            msg = m[0]
            if len(msg) < 6 or msg[0] != 0xF0 or msg[1] != 0x7D:
                continue
            cmd = msg[4]
            if cmd == CMD_STATUS:
                self.on_status(msg[5])
            elif cmd == CMD_INPUT:
                sub, a = msg[5], msg[6:-1]
                if sub == 0x00 and len(a) >= 2:
                    self.on_key(a[0], a[1])
                elif sub == 0x01 and len(a) >= 2:
                    self.on_encoder_turn(a[1] if a[0] == 2 else -a[1])
                elif sub == 0x02 and len(a) >= 1:
                    self.on_encoder_button(a[0])
                elif sub == 0x03 and len(a) >= 4:
                    self.on_pot(a[0], a[1], (a[2] << 7) | a[3])


# ---------------------------------------------------------------- demo app
def hsv_to_rgb7(h):
    # h 0..1 -> (r,g,b) 0..127, s=v=1
    i = int(h * 6) % 6
    f = h * 6 - int(h * 6)
    q, t = 1 - f, f
    rgb = [(1, t, 0), (q, 1, 0), (0, 1, t), (0, q, 1), (t, 0, 1), (1, 0, q)][i]
    return tuple(int(c * 127) for c in rgb)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="omx-27-v3")
    args = ap.parse_args()

    omx = Omx(args.port.lower())

    state = {
        "held": set(),      # keys currently down
        "cursor": 0,        # encoder-driven cursor 0-15 (bottom row)
        "invert": False,    # encoder button held -> inverted screen
        "pots": [0] * 5,
        "last_event": "waiting for input...",
        "dirty": True,
    }

    def on_key(index, z):
        state["held"].add(index) if z else state["held"].discard(index)
        state["last_event"] = f"OMX_Key({index}, {z})"
        state["dirty"] = True
        print(state["last_event"])

    def on_enc(d):
        state["cursor"] = (state["cursor"] + d) % 16
        state["last_event"] = f"OMX_Enc({d:+d})"
        state["dirty"] = True
        print(state["last_event"])

    def on_ebtn(z):
        state["invert"] = bool(z)
        state["last_event"] = f"OMX_Encoder({z})"
        state["dirty"] = True
        print(state["last_event"])

    def on_pot(index, v, hires):
        state["pots"][index] = v
        state["last_event"] = f"OMX_Pot({index}, {v})  hires={hires}"
        state["dirty"] = True
        print(state["last_event"])

    def on_status(s):
        names = {0x01: "activity", 0x02: "REMOTE active", 0x03: "REMOTE left"}
        print(f"[status] {names.get(s, hex(s))}")

    omx.on_key, omx.on_encoder_turn = on_key, on_enc
    omx.on_encoder_button, omx.on_pot, omx.on_status = on_ebtn, on_pot, on_status

    print("entering REMOTE mode...")
    omx.enter_remote_mode()
    time.sleep(0.3)

    phase = 0.0
    last_led = 0.0
    try:
        while True:
            omx.pump()
            now = time.time()

            # LEDs ~30fps: rainbow base, white for held keys, cursor on bottom row
            if now - last_led > 0.033:
                last_led = now
                phase += 0.005
                colors = []
                for i in range(27):
                    if i in state["held"]:
                        colors.append((127, 127, 127))
                    else:
                        r, g, b = hsv_to_rgb7((phase + i / 27.0) % 1.0)
                        colors.append((r // 6, g // 6, b // 6))  # dim rainbow
                cur = 11 + state["cursor"]
                if cur not in state["held"]:
                    colors[cur] = (127, 127, 0)
                omx.led_all(colors)
                omx.led_show()

            # screen only when something changed
            if state["dirty"]:
                state["dirty"] = False
                img = Image.new("1", (128, 32), 0)
                dr = ImageDraw.Draw(img)
                dr.text((2, 0), "OMX REMOTE test", fill=1)
                dr.text((2, 11), state["last_event"], fill=1)
                # pot bars
                for i, v in enumerate(state["pots"]):
                    x = 2 + i * 25
                    dr.rectangle([x, 29, x + 21, 31], outline=1)
                    dr.rectangle([x, 29, x + int(21 * v / 127), 31], fill=1)
                if state["invert"]:
                    img = Image.eval(img.convert("L"), lambda p: 255 - p).convert("1")
                omx.draw(img)

            time.sleep(0.002)
    except KeyboardInterrupt:
        print("\nbye")


if __name__ == "__main__":
    main()
