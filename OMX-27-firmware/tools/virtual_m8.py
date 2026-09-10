#!/usr/bin/env python3
"""
Virtual M8 — Simulates a Dirtywave M8 for testing OMX-27 M8 Launchpad emulation

This script acts as a virtual M8 device, communicating with an OMX-27 that is
emulating a Launchpad Pro MK3. It sends the real M8 handshake (Device Inquiry),
responds to button presses with LED feedback, and can stress-test the MIDI pipeline.

Usage:
    python3 virtual_m8.py [--list] [--input NAME] [--output NAME] [--flash-demo] [--stress]

Requirements:
    pip3 install mido python-rtmidi

The script will:
1. Auto-detect the OMX-27 MIDI port ("omx-27-v3", then "omx-27", then "Launchpad Pro MK3")
2. Send Device Inquiry Request (F0 7E 7F 06 01 F7) like a real M8
3. Wait for Launchpad identity response from the OMX
4. Send initial LED state using Note On messages with palette indices
5. Receive button/pad presses and respond with LED updates
6. Log all MIDI traffic for debugging

Use --flash-demo to additionally send notes on channels 2 and 3 (for pulse/flash modes).
Use --stress to send all 80 grid+scene LEDs every 500ms with rotating colours.
"""

import argparse
import sys
import time
from datetime import datetime

try:
    import mido
except ImportError:
    print("Error: mido not installed. Run: pip3 install mido python-rtmidi")
    sys.exit(1)


# =============================================================================
# Launchpad Pro MK3 Button Layout & Palette
# =============================================================================

# Launchpad Pro MK3 note map:
#     1   2   3   4   5   6   7   8
#    ┌───┬───┬───┬───┬───┬───┬───┬───┐
# 91 │   │   │   │   │   │   │   │   │ 99  (scene launch buttons)
#    ├───┼───┼───┼───┼───┼───┼───┼───┤
# 81 │   │   │   │   │   │   │   │   │ 89  (row 8 - grid)
# 71 │   │   │   │   │   │   │   │   │ 79  (row 7)
# 61 │   │   │   │   │   │   │   │   │ 69  (row 6)
# 51 │   │   │   │   │   │   │   │   │ 59  (row 5)
# 41 │   │   │   │   │   │   │   │   │ 49  (row 4)
# 31 │   │   │   │   │   │   │   │   │ 39  (row 3)
# 21 │   │   │   │   │   │   │   │   │ 29  (row 2)
# 11 │   │   │   │   │   │   │   │   │ 19  (row 1 - grid)
#    └───┴───┴───┴───┴───┴───┴───┴───┘
#     1   2   3   4   5   6   7   8       (bottom function row)
#   101 102 103 104 105 106 107 108

# Launchpad Pro MK3 palette indices (velocity-based LED control)
# These are standard palette indices that the OMX will render
PALETTE = {
    'off': 0,
    'dim': 1,
    'red': 5,
    'orange': 9,
    'yellow': 13,
    'lime': 17,
    'green': 21,
    'spring': 25,
    'cyan': 29,
    'sky': 33,
    'blue': 37,
    'purple': 41,
    'magenta': 45,
    'pink': 49,
    'white': 3,
}

# Launchpad Pro MK3 button labels (for logging)
BUTTON_LABELS = {
    # Grid pads (11-88)
    # Scene buttons (19, 29, ..., 89)
    19: 'scene 1',
    29: 'scene 2',
    39: 'scene 3',
    49: 'scene 4',
    59: 'scene 5',
    69: 'scene 6',
    79: 'scene 7',
    89: 'scene 8',

    # Top row (91-98) - track/shift buttons
    80: 'up',
    70: 'down',
    60: 'clear',
    50: 'dupe',

    # Right side (90-99)
    90: 'shift',
    91: 'track<',
    92: 'track>',
    93: 'session',
    94: 'note',
    97: 'seq',
    98: 'prj',
    99: 'logo',

    # Bottom buttons
    20: 'play',
    10: 'edit',

    # Track buttons (101-108)
    1: 'sshot',   # screen shot
    2: 'mute',
    3: 'solo',
}


# Device Inquiry handshake
M8_DEVICE_INQUIRY = [0x7E, 0x7F, 0x06, 0x01]  # F0 7E 7F 06 01 F7 (without F0/F7)
LPP_IDENTITY_PATTERN = (0x7E, 0x00, 0x06, 0x02)  # Expected prefix of identity response


# =============================================================================
# Logging
# =============================================================================

def log(msg, category='info'):
    """Log with timestamp and category"""
    timestamp = datetime.now().strftime('%H:%M:%S.%f')[:-3]
    prefix = {
        'rx': '← RX',
        'tx': '→ TX',
        'info': '  ℹ',
        'state': ' 📊',
        'error': ' ❌',
    }.get(category, '    ')
    print(f"[{timestamp}] {prefix} {msg}")


def note_label(note, velocity):
    """Return a human-readable label for a note"""
    # Grid pads: 11-88 (8x8)
    if 11 <= note <= 88 and note % 10 != 0 and note % 10 != 9:
        row = (note // 10)
        col = (note % 10)
        return f"pad r{row}c{col}"

    # Scene buttons: 19, 29, ..., 89 (column 9)
    if note % 10 == 9 and 19 <= note <= 89:
        row = (note // 10)
        return f"scene {row}"

    # Track buttons: 101-108
    if 101 <= note <= 108:
        return f"track {note - 100}"

    # Named buttons
    if note in BUTTON_LABELS:
        return BUTTON_LABELS[note]

    return f"note {note}"


def list_ports():
    """List available MIDI ports"""
    print("\nAvailable MIDI Input Ports:")
    for i, name in enumerate(mido.get_input_names()):
        print(f"  {i}: {name}")

    print("\nAvailable MIDI Output Ports:")
    for i, name in enumerate(mido.get_output_names()):
        print(f"  {i}: {name}")


def find_omx_ports():
    """Auto-detect OMX-27 MIDI ports.

    Matches, in order: "omx-27-v3" (Pico), "omx-27" (Teensy), then "launchpad pro mk3"
    (an OMX booted with the M8 macro saved as MCRO enumerates under that name).
    """
    patterns = ('omx-27-v3', 'omx-27', 'launchpad pro mk3')

    def pick(names):
        for pat in patterns:
            for name in names:
                if pat in name.lower():
                    return name
        return None

    return pick(mido.get_input_names()), pick(mido.get_output_names())


# =============================================================================
# LED State Management
# =============================================================================

class LaunchpadLEDs:
    """Manage Launchpad Pro MK3 LED state"""

    def __init__(self):
        self.state = {}  # note -> velocity (palette index)
        self.row8_index = 0  # Rainbow cycle index for row 8
        self._init_leds()

    def _init_leds(self):
        """Set up initial LED state"""
        # Grid pads 11-88: dim (velocity 1)
        for row in range(1, 9):
            for col in range(1, 9):
                note = row * 10 + col
                self.state[note] = PALETTE['dim']

        # Row 8 (81-88): rainbow palette
        row8_palette = [5, 9, 13, 21, 37, 45, 53, 57]
        for i, note in enumerate(range(81, 89)):
            self.state[note] = row8_palette[i]

        # Scene buttons (19, 29, ..., 89): white (velocity 3)
        for note in [19, 29, 39, 49, 59, 69, 79, 89]:
            self.state[note] = PALETTE['white']

        # Function buttons
        self.state[20] = PALETTE['green']   # Play = 21 (green)
        self.state[10] = PALETTE['red']     # Edit = 5 (red)
        self.state[90] = PALETTE['dim']     # Shift = 1
        self.state[93] = PALETTE['white']   # Session = 3
        self.state[94] = PALETTE['dim']     # Note = 1

        # Navigation
        self.state[80] = PALETTE['magenta'] # Up = 45
        self.state[70] = PALETTE['magenta'] # Down = 45
        self.state[91] = PALETTE['magenta'] # Track< = 45
        self.state[92] = PALETTE['magenta'] # Track> = 45

        # Side buttons
        self.state[2] = PALETTE['orange']   # Mute = 9
        self.state[3] = PALETTE['yellow']   # Solo = 13

        # Track buttons (101-108): dim (velocity 1)
        for note in range(101, 109):
            self.state[note] = PALETTE['dim']

    def get_all(self):
        """Get all LED states as (note, velocity) tuples"""
        return list(self.state.items())

    def set(self, note, velocity):
        """Set LED state"""
        self.state[note] = velocity

    def get(self, note):
        """Get LED velocity for a note"""
        return self.state.get(note, 0)

    def cycle_row8(self):
        """Cycle row 8 rainbow one step"""
        row8_palette = [5, 9, 13, 21, 37, 45, 53, 57]
        self.row8_index = (self.row8_index + 1) % len(row8_palette)

        # Rotate palette indices
        updates = []
        for i, note in enumerate(range(81, 89)):
            idx = (i + self.row8_index) % len(row8_palette)
            velocity = row8_palette[idx]
            self.state[note] = velocity
            updates.append((note, velocity))
        return updates


# =============================================================================
# MIDI Communication
# =============================================================================

def create_note_message(note, velocity, channel=0):
    """Create a note-on message with velocity as palette index"""
    return mido.Message('note_on', note=note, velocity=velocity, channel=channel)


def run_virtual_m8(input_port, output_port, verbose=False, flash_demo=False, stress=False):
    """Main loop - receive MIDI, simulate M8, send responses"""

    leds = LaunchpadLEDs()
    connected = False
    inquiry_sent = False
    inquiry_retry_time = 0
    INQUIRY_RETRY_INTERVAL = 2.0

    stress_last_send = 0
    STRESS_INTERVAL = 0.5

    log(f"Opening input:  {input_port}")
    log(f"Opening output: {output_port}")

    try:
        inport = mido.open_input(input_port)
        outport = mido.open_output(output_port)
    except Exception as e:
        log(f"Failed to open MIDI ports: {e}", 'error')
        return

    log("Virtual M8 running! Press Ctrl+C to exit.")
    log("Sending Device Inquiry Request: F0 7E 7F 06 01 F7")

    outport.send(mido.Message('sysex', data=M8_DEVICE_INQUIRY))
    inquiry_sent = True
    inquiry_retry_time = time.time()
    log("Waiting for OMX-27 Launchpad identity response...")

    try:
        while True:
            # Retry Device Inquiry every 2 seconds
            if not connected and inquiry_sent:
                if time.time() - inquiry_retry_time > INQUIRY_RETRY_INTERVAL:
                    log("No response yet, retrying Device Inquiry Request...")
                    outport.send(mido.Message('sysex', data=M8_DEVICE_INQUIRY))
                    inquiry_retry_time = time.time()

            # Stress test: send all LEDs every 500ms
            if stress and connected:
                now = time.time()
                if now - stress_last_send > STRESS_INTERVAL:
                    # Rotate all LEDs through the palette
                    updates = []
                    for note in range(11, 89):
                        if note % 10 != 0 and note % 10 != 9:
                            # Grid pad
                            vel = (leds.get(note) + 1) % 60
                            leds.set(note, vel)
                            updates.append((note, vel))

                    # Send them all in one burst
                    for note, vel in updates:
                        outport.send(create_note_message(note, vel, channel=0))

                    log(f"Stress burst: {len(updates)} LEDs", 'tx')
                    stress_last_send = now

            # Process incoming MIDI
            for msg in inport.iter_pending():
                if msg.type == 'note_on' and msg.velocity > 0:
                    label = note_label(msg.note, msg.velocity)
                    log(f"Note ON  {label:20s} note={msg.note:3d} vel={msg.velocity:3d} ch={msg.channel}", 'rx')

                    # Grid pad: echo lit (velocity 3) while held, return to dim on release
                    if 11 <= msg.note <= 88 and msg.note % 10 != 0 and msg.note % 10 != 9:
                        outport.send(create_note_message(msg.note, 3, channel=0))
                        log(f"LED echo: pad lit", 'tx')

                    # Up/Down: cycle row 8 rainbow
                    elif msg.note in [80, 70]:
                        updates = leds.cycle_row8()
                        for note, vel in updates:
                            outport.send(create_note_message(note, vel, channel=0))
                        log(f"Row 8 cycled: {len(updates)} LEDs", 'tx')

                elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
                    label = note_label(msg.note, 0)
                    log(f"Note OFF {label:20s} note={msg.note:3d}", 'rx')

                    # Grid pad: return to dim (velocity 1)
                    if 11 <= msg.note <= 88 and msg.note % 10 != 0 and msg.note % 10 != 9:
                        leds.set(msg.note, PALETTE['dim'])
                        outport.send(create_note_message(msg.note, PALETTE['dim'], channel=0))
                        log(f"LED echo: pad dim", 'tx')

                elif msg.type == 'control_change':
                    log(f"CC: control={msg.control:3d} value={msg.value:3d} ch={msg.channel}", 'rx')

                elif msg.type == 'sysex':
                    log(f"SysEx received: {len(msg.data)} bytes", 'rx')
                    if verbose:
                        hex_str = ' '.join(f'{b:02X}' for b in msg.data[:40])
                        log(f"  Data: {hex_str}{'...' if len(msg.data) > 40 else ''}", 'rx')

                    # Check for Launchpad identity response
                    if len(msg.data) >= 7 and msg.data[0:4] == LPP_IDENTITY_PATTERN:
                        # Extract family bytes (typically 00 20 29 for Novation)
                        family_bytes = ' '.join(f'{b:02X}' for b in msg.data[4:7])
                        log(f"Launchpad identity response detected! Family: {family_bytes}", 'info')

                        if not connected:
                            connected = True
                            log("Connection established! Sending initial LED state...", 'info')

                            # Send all initial LEDs
                            all_leds = leds.get_all()
                            for note, vel in all_leds:
                                outport.send(create_note_message(note, vel, channel=0))

                            # Send flash/pulse demo if requested
                            if flash_demo:
                                log("Sending flash/pulse demo on channels 2-3...", 'tx')
                                # Channel 2 (mido channel=1): flash some pads
                                for note in [11, 22, 33, 44]:
                                    outport.send(create_note_message(note, 5, channel=1))
                                # Channel 3 (mido channel=2): pulse some pads
                                for note in [15, 26, 37, 48]:
                                    outport.send(create_note_message(note, 9, channel=2))

                            log(f"Sent {len(all_leds)} LED states", 'tx')

            time.sleep(0.001)  # Prevent CPU spin

    except KeyboardInterrupt:
        log("\nShutting down...")
    finally:
        inport.close()
        outport.close()


# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='Virtual M8 — Simulates a Dirtywave M8 for OMX-27 M8 Launchpad emulation',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --list                    # List available MIDI ports
  %(prog)s                           # Auto-detect OMX-27 ports and connect
  %(prog)s --flash-demo              # Connect and send flash/pulse demo
  %(prog)s --stress                  # Stress-test with continuous LED updates
  %(prog)s -i "omx-27-v3" -o "omx-27-v3"  # Explicit port names
  %(prog)s -v                        # Verbose output (show hex dumps)
        """
    )

    parser.add_argument('--list', '-l', action='store_true',
                        help='List available MIDI ports and exit')
    parser.add_argument('--input', '-i', metavar='NAME',
                        help='Input MIDI port name (from OMX-27)')
    parser.add_argument('--output', '-o', metavar='NAME',
                        help='Output MIDI port name (to OMX-27)')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Verbose output (show SysEx hex dumps)')
    parser.add_argument('--flash-demo', action='store_true',
                        help='Send flash/pulse demo on channels 2-3')
    parser.add_argument('--stress', action='store_true',
                        help='Stress-test with all 80 LEDs every 500ms')

    args = parser.parse_args()

    if args.list:
        list_ports()
        return

    # Find ports
    input_port = args.input
    output_port = args.output

    if not input_port or not output_port:
        auto_in, auto_out = find_omx_ports()
        input_port = input_port or auto_in
        output_port = output_port or auto_out

    if not input_port or not output_port:
        log("Could not auto-detect OMX-27 MIDI ports.", 'error')
        log("Use --list to see available ports, then specify with: --input 'port' --output 'port'", 'error')
        list_ports()
        return

    run_virtual_m8(input_port, output_port, verbose=args.verbose,
                   flash_demo=args.flash_demo, stress=args.stress)


if __name__ == '__main__':
    main()
