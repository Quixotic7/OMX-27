#!/usr/bin/env python3
# Summarize an OMX MIDI capture written by midimon.py.
#   python3 analyze.py <logfile> [start_time]
# Reports per-channel note counts, velocities, CC values, transport, and BPM.
import sys, re
from collections import defaultdict, Counter

if len(sys.argv) < 2:
    print("usage: analyze.py <logfile> [start_time]"); sys.exit(1)
path = sys.argv[1]
tmin = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0

notes_by_ch = defaultdict(Counter)
vel_by_ch   = defaultdict(set)
cc_by_ch    = defaultdict(lambda: defaultdict(set))
transport, bpms = [], []
noteons, noteoffs = defaultdict(int), defaultdict(int)
first_t = last_t = None
names = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]

for line in open(path):
    if line.startswith("#"): continue
    m = re.match(r"\s*([\d.]+)\s+(.*)", line)
    if not m: continue
    t = float(m.group(1)); body = m.group(2)
    if t < tmin: continue
    first_t = t if first_t is None else first_t; last_t = t
    if body.startswith("NoteOn"):
        g = re.search(r"ch(\d+).*n(\d+)\s+vel(\d+)", body)
        if g:
            ch, n, v = map(int, g.groups())
            notes_by_ch[ch][n] += 1; vel_by_ch[ch].add(v); noteons[ch] += 1
    elif body.startswith("NoteOff"):
        g = re.search(r"ch(\d+)", body)
        if g: noteoffs[int(g.group(1))] += 1
    elif body.startswith("CC"):
        g = re.search(r"ch(\d+)\s+cc(\d+)\s+val(\d+)", body)
        if g:
            ch, cc, v = map(int, g.groups()); cc_by_ch[ch][cc].add(v)
    elif body.startswith(("START", "STOP", "CONTINUE", "PROG")):
        transport.append((round(t, 2), body.strip()))
    elif body.startswith("~BPM"):
        g = re.search(r"~BPM\s+([\d.]+)", body)
        if g: bpms.append(float(g.group(1)))

print(f"window: {first_t}..{last_t}" if first_t else "no events in window")
if bpms: print(f"BPM: {min(bpms):.0f}-{max(bpms):.0f} (n={len(bpms)})")
if transport: print("transport/prog:", transport[-12:])
print("\nchannels active:", sorted(set(list(notes_by_ch) + list(cc_by_ch))))
for ch in sorted(notes_by_ch):
    ns = ", ".join(f"{names[n%12]}{n//12-1}(n{n})x{c}" for n, c in sorted(notes_by_ch[ch].items()))
    print(f" ch{ch}: {noteons[ch]} on / {noteoffs[ch]} off | vels={sorted(vel_by_ch[ch])} | {ns}")
    if noteons[ch] != noteoffs[ch]:
        print(f"   !! on/off mismatch on ch{ch} (window edge, or a stuck/retriggered note)")
for ch in sorted(cc_by_ch):
    for cc in sorted(cc_by_ch[ch]):
        vals = sorted(cc_by_ch[ch][cc])
        print(f" ch{ch} CC{cc}: {len(vals)} distinct -> {vals[:20]}{'...' if len(vals) > 20 else ''}")
