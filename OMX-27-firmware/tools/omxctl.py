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
def save_state():         inp(0x04)   # persist to storage (FRAM + FORM bank)
def set_mode(m):          inp(0x05, m) # switch OMX mode (3 = FORM)

# ---- LED state query (SysEx 0x54) ----
def leds(timeout=1.2):
    """Query the 27 keypad LEDs. Returns [(r,g,b), ...] 0-254 (7-bit*2); index 0 = AUX.
    The OLED mirror can't show the RGB keypad LEDs — this can."""
    midiin = _open_in()
    try:
        while midiin.get_message():
            pass  # drain
        sysex(0x54)  # request LED state -> device replies with two 0x54 parts
        parts = {}
        deadline = time.time() + timeout
        while time.time() < deadline and len(parts) < 2:
            m = midiin.get_message()
            if not m:
                time.sleep(0.001); continue
            msg = m[0]
            if len(msg) < 6 or msg[1] != 0x7D or msg[4] != 0x54:
                continue
            parts[msg[5]] = [b & 0x7F for b in msg[6:-1]]  # strip trailing F7
    finally:
        midiin.close_port()
    out = []
    for part in (0, 1):
        d = parts.get(part, [])
        for j in range(0, len(d) - 2, 3):
            out.append((d[j] << 1, d[j + 1] << 1, d[j + 2] << 1))
    return out

def led_name(rgb):
    """Rough human label for an (r,g,b) LED colour (QA convenience)."""
    r, g, b = rgb; mx = max(r, g, b); mn = min(r, g, b)
    if mx < 20: return "off"
    lvl = "dim " if mx < 90 else ""
    if mn > 130:
        return (lvl + ("blue-white" if (b >= r and b >= g and b - mn > 15) else "white")).strip()
    if mx - mn < 40: return (lvl + "white").strip()
    if b >= r and b >= g: return lvl + ("cyan" if g > b * 0.6 else ("purple" if r > b * 0.5 else "blue"))
    if r >= g and r >= b:
        if g > r * 0.55: return lvl + ("yellow" if b < g * 0.6 else "white")
        return lvl + ("orange" if g > r * 0.25 else ("magenta" if b > r * 0.4 else "red"))
    if g >= r and g >= b: return lvl + ("green" if r < g * 0.6 else "lime")
    return lvl + "?"

def leds_ascii(L=None):
    """A readable dump of the current LED state (AUX, top row 1-10, low row 11-26)."""
    if L is None: L = leds()
    if len(L) < 27:
        return "LED query incomplete (%d/27): %s" % (len(L), L)
    def cell(i): return "%2d:%s" % (i, led_name(L[i]))
    return ("AUX  " + cell(0) + "\n" +
            "top  " + " ".join(cell(i) for i in range(1, 11)) + "\n" +
            "low  " + " ".join(cell(i) for i in range(11, 27)))

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
    if len(sys.argv) > 1 and sys.argv[1] == "leds":
        # query + print the RGB keypad LED state
        print(leds_ascii())
    else:
        out = sys.argv[1] if len(sys.argv) > 1 else "screen.png"
        b = screen(out)
        print("saved", out)
        print(ascii_art(b))
