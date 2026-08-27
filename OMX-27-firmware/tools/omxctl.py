#!/usr/bin/env python3
# OMX-27 SysEx remote control + screen mirror (host side).
# Inject key/encoder/pot events and capture the 128x32 OLED over SysEx.
import rtmidi, time, sys
from PIL import Image

HDR = [0x7D, 0x00, 0x00]
PORT_MATCH = "omx-27"

def _open_out():
    o = rtmidi.MidiOut()
    for i, p in enumerate(o.get_ports()):
        if PORT_MATCH in p.lower():
            o.open_port(i); return o
    raise RuntimeError("omx out port not found: " + str(o.get_ports()))

def _open_in():
    inp = rtmidi.MidiIn()
    for i, p in enumerate(inp.get_ports()):
        if PORT_MATCH in p.lower():
            inp.open_port(i); inp.ignore_types(sysex=False, timing=True, active_sense=True); return inp
    raise RuntimeError("omx in port not found")

_out = _open_out()

def sysex(cmd, *args):
    _out.send_message([0xF0] + HDR + [cmd] + [a & 0x7F for a in args] + [0xF7])

# ---- input injection (NL_CMD_INPUT 0x51) ----
def inp(sub, *args): sysex(0x51, sub, *args)
def k_down(k):            inp(0x00, k, 1, 0, 0, 1)
def k_up(k, quick=True):  inp(0x00, k, 0, 0, 1 if quick else 0, 1)
def k_held(k):            inp(0x00, k, 1, 1, 0, 1)   # emits onKeyHeldUpdate
def tap(k, ms=40):        k_down(k); time.sleep(ms/1000.0); k_up(k, quick=True)
def hold(k, ms=300, held=False):
    k_down(k); time.sleep(ms/1000.0)
    if held: k_held(k); time.sleep(0.02)
    k_up(k, quick=False)
def enc(n):
    d = 2 if n > 0 else 0
    inp(0x01, d, abs(int(n)), 0)
def enc_click():          inp(0x02, 0); time.sleep(0.02); inp(0x02, 1)
def enc_long():           inp(0x02, 2)
def pot(k, v):            inp(0x03, k, v)

# ---- screen mirror ----
def _decode7(enc):
    out = bytearray(); i = 0
    while i < len(enc):
        hi = enc[i]; i += 1
        for j in range(7):
            if i >= len(enc): break
            out.append((enc[i] & 0x7F) | (((hi >> j) & 1) << 7)); i += 1
    return out

def capture(timeout=2.5, retries=3):
    """Force a full frame and assemble the 512-byte SSD1306 buffer."""
    midiin = _open_in()
    try:
        for _ in range(retries):
            buf = bytearray(512); got = [False]*16
            sysex(0x58, 1)  # mirror enable -> forces a full frame
            deadline = time.time() + timeout
            while time.time() < deadline:
                m = midiin.get_message()
                if not m:
                    time.sleep(0.001); continue
                msg = m[0]
                if len(msg) < 6 or msg[1] != 0x7D: continue
                cmd = msg[4]
                if cmd == 0x50:  # FRAME chunk
                    chunk = msg[5]
                    data = _decode7(msg[8:-1])  # strip trailing F7
                    if chunk < 16 and len(data) >= 32:
                        buf[chunk*32:(chunk+1)*32] = data[:32]; got[chunk] = True
                elif cmd == 0x53:  # FRAME_END
                    if all(got) and any(buf):  # reject all-black (mid-redraw) frames
                        sysex(0x58, 0)  # disable so it stops streaming
                        return buf
            # incomplete or blank; loop and retry
        sysex(0x58, 0)
        return buf  # partial
    finally:
        midiin.close_port()

def render(buf, scale=4):
    img = Image.new("1", (128, 32))
    px = img.load()
    for page in range(4):
        for col in range(128):
            b = buf[col + page*128]
            for bit in range(8):
                px[col, page*8+bit] = (b >> bit) & 1
    img = img.rotate(180)  # OMX panel is mounted 180deg vs the SSD1306 buffer
    return img.resize((128*scale, 32*scale), Image.NEAREST)

def ascii_art(buf):
    # 2 vertical pixels per char using half-blocks
    rows = []
    for y in range(0, 32, 2):
        line = ""
        for x in range(128):
            top = (buf[x + (y//8)*128] >> (y % 8)) & 1
            bot = (buf[x + ((y+1)//8)*128] >> ((y+1) % 8)) & 1
            line += {(0,0):" ",(1,0):"▀",(0,1):"▄",(1,1):"█"}[(top,bot)]
        rows.append(line)
    return "\n".join(rows)

def screen(path=None):
    buf = capture()
    if path:
        render(buf).save(path)
    return buf

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "screen.png"
    b = screen(out)
    print("saved", out)
    print(ascii_art(b))
