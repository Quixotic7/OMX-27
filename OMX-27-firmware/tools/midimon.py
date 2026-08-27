#!/usr/bin/env python3
# OMX-27 MIDI smoke-test monitor. Captures note/CC/PC/transport from omx-27-v3,
# counts clock for a BPM estimate, flags stuck notes. One decoded event per line.
import rtmidi, time, sys

logpath = sys.argv[1] if len(sys.argv) > 1 else "midimon.log"
midiin = rtmidi.MidiIn()
ports = midiin.get_ports()
idx = None
for i, p in enumerate(ports):
    if "omx-27" in p.lower():
        idx = i; break
if idx is None:
    print("omx-27 not found in:", ports); sys.exit(1)
midiin.open_port(idx)
midiin.ignore_types(sysex=False, timing=False, active_sense=True)  # keep clock (timing)

logf = open(logpath, "w", buffering=1)
start = time.time()
logf.write(f"# monitoring '{ports[idx]}'  t0={time.strftime('%H:%M:%S')}\n")

active = {}          # (ch,note) -> t   (for stuck-note detection)
clk = {"n": 0, "t0": None}

NOTE_NAMES = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]
def nn(n): return f"{NOTE_NAMES[n%12]}{n//12-1}"

def decode(m):
    s = m[0]; typ = s & 0xF0; ch = (s & 0x0F) + 1
    if typ == 0x90 and m[2] > 0: return ("on",  f"NoteOn  ch{ch:<2} {nn(m[1]):<4} n{m[1]:<3} vel{m[2]}", ch, m[1])
    if typ == 0x80 or (typ == 0x90 and m[2] == 0): return ("off", f"NoteOff ch{ch:<2} {nn(m[1]):<4} n{m[1]:<3}", ch, m[1])
    if typ == 0xB0: return ("cc",  f"CC      ch{ch:<2} cc{m[1]:<3} val{m[2]}", ch, m[1])
    if typ == 0xC0: return ("pc",  f"PROG    ch{ch:<2} prog{m[1]}", ch, None)
    if typ == 0xE0: return ("pb",  f"PBEND   ch{ch:<2} {m[1]|(m[2]<<7)}", ch, None)
    if s == 0xFA: return ("start","START", None, None)
    if s == 0xFB: return ("cont","CONTINUE", None, None)
    if s == 0xFC: return ("stop","STOP", None, None)
    if s == 0xF0: return ("sysex", f"SYSEX {len(m)}B " + " ".join(f"{b:02X}" for b in m[:16]), None, None)
    return ("raw", f"RAW {m}", None, None)

last_bpm = 0
while True:
    msg = midiin.get_message()
    if not msg:
        # emit a BPM line ~every 2s of clock
        if clk["t0"] and clk["n"] >= 48 and (time.time()-clk["t0"]) > 1.5:
            bpm = (clk["n"]/24.0) / ((time.time()-clk["t0"])/60.0)
            logf.write(f"{time.time()-start:8.3f} ~BPM {bpm:.1f} ({clk['n']} clocks)\n")
            clk["n"] = 0; clk["t0"] = time.time()
        time.sleep(0.0005); continue
    m, _ = msg
    t = time.time() - start
    if m and m[0] == 0xF8:  # clock
        if clk["t0"] is None: clk["t0"] = time.time()
        clk["n"] += 1
        continue
    kind, txt, ch, note = decode(m)
    if kind == "on":
        active[(ch, note)] = t
    elif kind == "off":
        active.pop((ch, note), None)
    logf.write(f"{t:8.3f} {txt}\n")
