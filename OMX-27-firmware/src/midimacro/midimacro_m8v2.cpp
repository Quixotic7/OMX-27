#include "midimacro_m8v2.h"
#include "../globals.h"
#include "../utils/omx_util.h"
#include "../hardware/omx_disp.h"
#include "../hardware/omx_leds.h"
#include "../midi/midi.h"
#include "../consts/consts.h"
#include "../consts/colors.h"
#include "lpp_palette.h"
#include "../utils/pot_bank_aux.h"

namespace midimacro
{
	// Launchpad Pro MK3 identity reply.
	// Full message: F0 7E 00 06 02 00 20 29 23 01 00 00 00 01 00 00 F7
	// MM::sendSysEx is called with hasBeginEnd = false and the F0/F7 omitted, the same
	// convention SysEx::sendCurrentState() uses.
	static const uint8_t kIdentityReply[] = {
		0x7E, 0x00, 0x06, 0x02,	// device inquiry reply
		0x00, 0x20, 0x29,		// Novation
		0x23, 0x01,				// Launchpad Pro MK3 family
		0x00, 0x00,				// member
		0x00, 0x01, 0x00, 0x00	// version
	};

	// Handshake diagnostics shown on the display while unlinked: identities sent / inquiries received.
	static uint16_t s_identitySent = 0;
	static uint16_t s_inquiryRx = 0;

	static void sendIdentityReply()
	{
		s_identitySent++;
		MM::sendSysEx(sizeof(kIdentityReply), kIdentityReply, false); // USB + TRS: iOS app is on USB, a hardware M8 is on the DIN/TRS ports; both need the reply
	}

	void onDeviceInquiry(const uint8_t *data, unsigned length)
	{
		(void)data;
		(void)length;
		s_inquiryRx++;

		// Reply even when the macro isn't entered, and from any OMX mode - the M8 asks as
		// soon as it's plugged in.
		if (midiMacroConfig.midiMacro == MIDIMACRO_M8LP)
		{
			sendIdentityReply();
		}
	}


	enum M8V2Page
	{
		M8V2PAGE_MAIN,	  // row display, encoder scrolls
		M8V2PAGE_SETTING, // HAND / MUTE / SOLO / RING
		M8V2PAGE_SETTING2 // NROW  (the display only holds 4 legends per page)
	};

	// LPP button numbers used in more than one place.
	static const uint8_t kLppSShot = 1;
	static const uint8_t kLppMute = 2;
	static const uint8_t kLppSolo = 3;
	static const uint8_t kLppRec = 10;
	static const uint8_t kLppPlay = 20;
	static const uint8_t kLppDup = 50;
	static const uint8_t kLppClear = 60;
	static const uint8_t kLppDown = 70;
	static const uint8_t kLppUp = 80;
	static const uint8_t kLppShift = 90;
	static const uint8_t kLppTrackL = 91;
	static const uint8_t kLppTrackR = 92;
	static const uint8_t kLppSession = 93;
	static const uint8_t kLppNote = 94;
	static const uint8_t kLppSeq = 97;
	static const uint8_t kLppProject = 98;
	static const uint8_t kLppLogo = 99;
	static const uint8_t kLppTrack1 = 101;

	// M8 Session track-button LED palette (hardware-decoded 2026-09-13 by matching the mirrored
	// RGB against lpp_palette.h and cross-checking the M8's own M/S mixer panel):
	// muted = coral 0xff6161 (palette idx 6 or its alias 73 - same colour), soloed = cyan
	// 0x61e9ff (idx 79), 0 = empty. Playing tracks pulse green/grey. The earlier 5/78 guess was
	// wrong. Match both muted aliases since which index the M8 emits can't be told from colour.
	static const uint8_t kTrkMuted = 6;
	static const uint8_t kTrkMutedAlt = 73;
	static const uint8_t kTrkSoloed = 79;

	// Settle gap (ms) after pressing a Mute/Solo modifier before tapping the track, so the
	// M8 registers the modifier as held first. Without it the M8 processes the track tap in
	// the same burst and falls through to its default (mute) - which is why Solo behaved
	// like Mute. Only atomic chords (press modifier + tap track in one handler) need it;
	// latch-style holds (pad modes) already hold the modifier long before any track tap.
	static const uint8_t kModSettleMs = 20;

	// M8 Control Map notes on the macro channel.
	static const uint8_t kCmPlay = 0;
	static const uint8_t kCmShift = 1;
	static const uint8_t kCmEdit = 2;
	static const uint8_t kCmOption = 3;
	static const uint8_t kCmLeft = 4;
	static const uint8_t kCmRight = 5;
	static const uint8_t kCmUp = 6;
	static const uint8_t kCmDown = 7;

	// Returns a half-brightness version of a 0xRRGGBB colour by shifting each channel
	// right by 1 - used for the "off" phase of a pulsing (mode 2) LED.
	static inline uint32_t halfColor(uint32_t c)
	{
		uint8_t r = (uint8_t)(c >> 16);
		uint8_t g = (uint8_t)(c >> 8);
		uint8_t b = (uint8_t)c;
		r >>= 1;
		g >>= 1;
		b >>= 1;
		return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
	}

	MidiMacroM8V2::MidiMacroM8V2()
	{
		params_.addPage(2); // Main: p0 = row/scroll, p1 = Clip orientation (row/col)
		params_.addPage(4); // HAND, MUTE, SOLO, RING
		params_.addPage(1); // NROW
		encoderSelect_ = false;

		for (uint8_t i = 0; i < kNumLppNotes; i++)
		{
			ledColor_[i] = 0;
			ledMode_[i] = 0;
		}
		for (uint8_t i = 0; i < kNumKeys; i++)
		{
			keyNoteSent_[i] = 0;
			ctrlSent_[i] = -1;
		}

		dispLabel_[0] = 0;
		dispStatus_[0] = 0;
	}

	String MidiMacroM8V2::getName()
	{
		return String("ML"); // M8 Launchpad Pro
	}

	// ------------------------------------------------------------ Saved settings

	uint8_t MidiMacroM8V2::getSettingsByte() const
	{
		uint8_t v = 0;
		if (rightHand_)
			v |= 0x01;
		if (muteLatch_)
			v |= 0x02;
		if (soloLatch_)
			v |= 0x04;
		if (!ringAsCC_)
			v |= 0x08; // bit3: 0 = CC, 1 = note
		v |= (uint8_t)((nrow_ & 0x07) << 4);
		return v;
	}

	void MidiMacroM8V2::setSettingsByte(uint8_t v)
	{
		if (v == 0xFF)
			return; // never written (old save / fresh EEPROM) - keep defaults

		rightHand_ = (v & 0x01) != 0;
		muteLatch_ = (v & 0x02) != 0;
		soloLatch_ = (v & 0x04) != 0;
		ringAsCC_ = (v & 0x08) == 0;
		nrow_ = (uint8_t)((v >> 4) & 0x07); // 0..3 = K3..K6, 4 = AUTO
		if (nrow_ > 4)
			nrow_ = 0;
	}

	// ------------------------------------------------------------ Enable / disable

	void MidiMacroM8V2::sendIdentity()
	{
		sendIdentityReply();
		lastIdentityMs_ = millis();
	}

	void MidiMacroM8V2::onEnabled()
	{
		linked_ = false;
		auxHeld_ = false;
		shiftLatched_ = false;

		// Always land back in Session/CLIP and release any modifier that might still be
		// held from a previous session (e.g. macro switched away while Mute was down).
		sendLpp(kLppMute, false);
		sendLpp(kLppSolo, false);
		sendLpp(kLppShift, false);
		padMode_ = PAD_CLIP;
		view_ = VIEW_SESSION;
		row_ = 8; // Session pins the left keys to Launchpad row 8; keys 1/2 move the box.
		clipRow_ = 8; // Clip Launch starts on the top row (not persisted)
		clipColMode_ = false; // land in row orientation
		seqHeldStep_ = 0;
		seqMode_ = 0; // Seq v2 defaults: no step held, NOTE mode
		latchedTracks_ = 0;
		momentaryTracks_ = 0;
		trackHeld_ = false;
		recLatched_ = false;
		for (uint8_t i = 0; i < kNumKeys; i++)
			ctrlSent_[i] = -1;

		params_.setSelPageAndParam(M8V2PAGE_MAIN, 0); // land on the 8x8 grid page, not a settings page
		encoderSelect_ = false;

		sendIdentity();

		// Ask the M8 for Session view (LPP Session button = note 93).
		sendLppTap(kLppSession);

		omxLeds.setDirty();
		omxDisp.setDirty();
	}

	void MidiMacroM8V2::onDisabled()
	{
		releaseAllHeld();

		// Leaving the macro entirely while in Beat Repeat needs the same plain-Session exit
		// tap as switching views does (spec section 5c), or the M8 is left in Beat Repeat
		// with no way back short of finding Shift+Session on the hardware.
		if (view_ == VIEW_BEAT)
		{
			if (shiftLatched_)
			{
				sendLpp(kLppShift, false);
				sendLppTap(kLppSession);
			}
			else
			{
				sendLppTap(kLppSession);
			}
		}

		// Release the Mute/Solo modifiers regardless of which pad mode was active -
		// harmless if they weren't held.
		sendLpp(kLppMute, false);
		sendLpp(kLppSolo, false);
		sendLpp(kLppShift, false);
		shiftLatched_ = false;

		for (uint8_t i = 0; i < kNumLppNotes; i++)
		{
			ledColor_[i] = 0;
			ledMode_[i] = 0;
		}

		auxHeld_ = false;
		omxLeds.setDirty();
	}

	void MidiMacroM8V2::releaseAllKeys()
	{
		for (uint8_t i = 0; i < kNumKeys; i++)
		{
			if (keyNoteSent_[i] != 0)
			{
				sendLpp(keyNoteSent_[i], false);
				keyNoteSent_[i] = 0;
			}
		}
	}

	void MidiMacroM8V2::releaseControlNotes()
	{
		for (uint8_t i = 0; i < kNumKeys; i++)
		{
			if (ctrlSent_[i] >= 0)
			{
				MM::sendNoteOff((uint8_t)ctrlSent_[i], 0, midiMacroConfig.midiMacroChan);
				ctrlSent_[i] = -1;
			}
		}
	}

	// Everything the macro can be holding down on the M8 side, released in one place.
	// Shift is deliberately NOT released here: it is latched for the duration of an AUX
	// hold and dropped when AUX comes up (see onKeyUpdate), which is also when views switch.
	void MidiMacroM8V2::releaseAllHeld()
	{
		releaseAllKeys();
		releaseLatchedTracks();
		releaseMomentaryTracks(); // must run before the modifier release just below
		releaseControlNotes();

		if (padMode_ == PAD_MUTE)
			sendLpp(kLppMute, false);
		else if (padMode_ == PAD_SOLO)
			sendLpp(kLppSolo, false);
		padMode_ = PAD_CLIP;

		trackHeld_ = false;
		seqHeldStep_ = 0;
		if (clipMuteChord_)
		{
			clipMuteChord_ = false;
			sendLpp(kLppMute, false);
		}
		if (recLatched_)
		{
			recLatched_ = false;
			sendLpp(kLppRec, false);
		}
	}

	bool MidiMacroM8V2::isRingButton(uint8_t n)
	{
		if (n >= 11 && n <= 88)
		{
			uint8_t col = n % 10;
			return (col == 0 || col == 9);
		}
		return true; // 1-8, 10/20/..90, 91-99, 101-108
	}

	int8_t MidiMacroM8V2::controlMapNote(uint8_t key) const
	{
		if (rightHand_)
		{
			switch (key)
			{
			case 8:  return kCmOption;
			case 9:  return kCmEdit;
			case 10: return kCmUp;
			case 22: return kCmShift;
			case 23: return kCmPlay;
			case 24: return kCmLeft;
			case 25: return kCmDown;
			case 26: return kCmRight;
			default: return -1; // key 21 is unassigned in the right-hand layout
			}
		}

		switch (key)
		{
		case 8:  return kCmUp;
		case 9:  return kCmOption;
		case 10: return kCmEdit;
		case 22: return kCmLeft;
		case 23: return kCmDown;
		case 24: return kCmRight;
		case 25: return kCmShift;
		case 26: return kCmPlay;
		default: return -1; // key 21 unassigned in the left-hand layout too
		}
	}

	// Control view key cluster (spec section 9b). Indexed [rightHand_][Control Map note 0-7],
	// so HAND just picks a row. 0 = no key (never used - every CM note has a key in both).
	static const uint8_t kCtrlViewKeys[2][8] = {
		//  Play Shift Edit Option Left Right Up  Down
		{     13,   12,   2,     1,  22,   24,  8,   23 }, // HAND = L (arrows right: Left 22, Down 23, Right 24)
		{     24,   23,  10,     9,  11,   13,  1,   12 }, // HAND = R (arrows left, classic)
	};

	int8_t MidiMacroM8V2::ctrlViewNote(uint8_t key) const
	{
		if (key == 0)
			return -1;
		const uint8_t *row = kCtrlViewKeys[rightHand_ ? 1 : 0];
		for (uint8_t cm = 0; cm < 8; cm++)
		{
			if (row[cm] == key)
				return (int8_t)cm;
		}
		return -1;
	}

	// AUX+13 / AUX+14, identical to the normal OMX AUX layer (omx_mode_midi_keyboard.cpp).
	void MidiMacroM8V2::changePotBank(bool next)
	{
		const int n = NUM_CC_BANKS;
		int b = potSettings.potbank;
		b = next ? ((b + 1) % n) : ((b + n - 1) % n);
		potSettings.potbank = b;
		potBankAuxTriggerFlash((uint8_t)b);
		MM::sendControlChange(90, potSettings.potbank, sysSettings.midiChannel);
		omxDisp.displayMessagef("Pot Bank %d", b + 1);
	}

	void MidiMacroM8V2::sendControlMapTap(uint8_t cm)
	{
		MM::sendNoteOn(cm, 1, midiMacroConfig.midiMacroChan);
		MM::sendNoteOff(cm, 0, midiMacroConfig.midiMacroChan);
	}

	void MidiMacroM8V2::sendLpp(uint8_t n, bool on)
	{
		if (ringAsCC_ && isRingButton(n))
			MM::sendControlChange(n, on ? 127 : 0, 1);
		else if (on)
			MM::sendNoteOn(n, 127, 1);
		else
			MM::sendNoteOff(n, 0, 1);
	}

	void MidiMacroM8V2::releaseLatchedTracks()
	{
		for (uint8_t i = 0; i < 8; i++)
		{
			if (latchedTracks_ & (1 << i))
				sendLpp((uint8_t)(kLppTrack1 + i), false);
		}
		latchedTracks_ = 0;
	}

	// A momentary mute/solo press toggled the M8-side track state on key-down and expects a
	// re-tap on key-up to undo it (see onKeyUpdate). If we abandon that key mid-press (view
	// switch, mode change, macro exit) without the re-tap, the track is left muted/soloed on
	// the M8 forever. Mute/Solo (2/3) must still be held when this runs, so callers do this
	// before releasing that modifier.
	void MidiMacroM8V2::releaseMomentaryTracks()
	{
		for (uint8_t i = 0; i < 8; i++)
		{
			if (momentaryTracks_ & (1 << i))
				sendLppTap((uint8_t)(kLppTrack1 + i));
		}
		momentaryTracks_ = 0;
	}

	// ------------------------------------------------------------------- Macros

	void MidiMacroM8V2::doWaveformMacro()
	{
		// All four Control Map arrows pressed together, exactly as the classic M8 macro.
		MM::sendNoteOn(kCmUp, 1, midiMacroConfig.midiMacroChan);
		MM::sendNoteOn(kCmDown, 1, midiMacroConfig.midiMacroChan);
		MM::sendNoteOn(kCmRight, 1, midiMacroConfig.midiMacroChan);
		MM::sendNoteOn(kCmLeft, 1, midiMacroConfig.midiMacroChan);
		delay(40);
		MM::sendNoteOff(kCmUp, 0, midiMacroConfig.midiMacroChan);
		MM::sendNoteOff(kCmDown, 0, midiMacroConfig.midiMacroChan);
		MM::sendNoteOff(kCmRight, 0, midiMacroConfig.midiMacroChan);
		MM::sendNoteOff(kCmLeft, 0, midiMacroConfig.midiMacroChan);

		omxDisp.displayMessageTimed("WAVEFORM", 5);
	}

	void MidiMacroM8V2::doGotoMixerMacro()
	{
		// Copied verbatim from the legacy MidiMacroM8 page 1 key 3: hold Shift, Up, Left x4,
		// Down, release Shift. The delays are the legacy ones and are the only blocking
		// delay()s in this macro.
		const uint8_t ch = midiMacroConfig.midiMacroChan;
		MM::sendNoteOn(kCmShift, 1, ch);
		delay(40);
		MM::sendNoteOn(kCmUp, 1, ch);
		delay(20);
		MM::sendNoteOff(kCmUp, 0, ch);
		for (uint8_t i = 0; i < 4; i++)
		{
			delay(40);
			MM::sendNoteOn(kCmLeft, 1, ch);
			delay(20);
			MM::sendNoteOff(kCmLeft, 0, ch);
		}
		delay(40);
		MM::sendNoteOn(kCmDown, 1, ch);
		delay(20);
		MM::sendNoteOff(kCmDown, 0, ch);
		MM::sendNoteOff(kCmShift, 0, ch);

		omxDisp.displayMessageTimed("GOTO MIXER", 5);
	}

	void MidiMacroM8V2::doStoreSnapshot()
	{
		if (shiftLatched_)
		{
			sendLppTap(kLppSShot); // Shift is already down
		}
		else
		{
			sendLpp(kLppShift, true);
			sendLppTap(kLppSShot);
			sendLpp(kLppShift, false);
		}
		omxDisp.displayMessageTimed("SNAP STORED", 5);
	}

	void MidiMacroM8V2::doRecallSnapshot()
	{
		sendLppTap(kLppSShot);
		omxDisp.displayMessageTimed("SNAP RECALL", 5);
	}

	// Mix view "all" keys, using the hardware-decoded track LED palette
	// (kTrkMuted / kTrkSoloed).
	//
	// - UNMUTE ALL (Mute modifier): toggle mute only on tracks the M8 reports as muted (coral).
	//   The old bug tapped every non-off/non-white track - which includes the pulsing green/grey
	//   playing tracks - and so muted them (the inversion the user reported).
	// - CLEAR SOLO (Solo modifier): tap only tracks the M8 reports as soloed (cyan); re-tapping a
	//   soloed track un-solos it, returning to all-playing. Nothing else is touched.
	void MidiMacroM8V2::doMixAllTracks(uint8_t modifier)
	{
		// Note: this reads the mirrored track-button LEDs, which the M8 only streams on a
		// mute/solo change while playing - so it acts on tracks toggled since the macro linked.
		// That is fine (and safe: it never touches a playing track, avoiding the old inversion
		// bug) but it can't see mutes set before linking or while stopped.
		sendLpp(modifier, true);
		delay(kModSettleMs); // let the M8 register the modifier as held before any track tap
		for (uint8_t i = 0; i < 8; i++)
		{
			// Act only on tracks already in the matching state, so nothing is toggled the
			// wrong way: unmute the muted (coral), unsolo the soloed (cyan).
			uint8_t c = ledColor_[kLppTrack1 + i];
			bool match = (modifier == kLppMute) ? (c == kTrkMuted || c == kTrkMutedAlt)
												: (c == kTrkSoloed);
			if (match)
				sendLppTap((uint8_t)(kLppTrack1 + i));
		}
		sendLpp(modifier, false);
		omxDisp.displayMessageTimed(modifier == kLppMute ? "UNMUTE ALL" : "CLEAR SOLO", 5);
	}

	// ------------------------------------------------------------------- Views

	uint8_t MidiMacroM8V2::impliedViewButton() const
	{
		switch (view_)
		{
		case VIEW_NOTE: return kLppNote;
		case VIEW_SEQ:
		case VIEW_PHRASE: return kLppSeq;
		case VIEW_CTRL: return 0; // Control view doesn't care what the M8 shows
		default: return kLppSession; // Session, Clip, Clip-col, Mix, Beat Repeat all sit on the Session screen
		}
	}

	void MidiMacroM8V2::followM8View(uint8_t button, uint8_t prevColor, uint8_t newColor)
	{
		if (!enabled_ || prevColor != 0 || newColor == 0)
			return; // only on a button lighting up from off
		if (button != kLppSession && button != kLppNote && button != kLppSeq)
			return;
		uint8_t implied = impliedViewButton();
		if (implied == 0 || implied == button)
			return; // we asked for this view ourselves (or don't care)

		View v = (button == kLppSeq) ? VIEW_SEQ : (button == kLppNote) ? VIEW_NOTE : VIEW_SESSION;
		if (view_ == VIEW_BEAT && button == kLppSession)
			return; // Beat Repeat lives on the Session button (lit red); not a view change
		switchView(v, false); // don't send the button back - the M8 is already there
		omxDisp.displayMessageTimed(button == kLppSeq ? "M8 > SEQ" : button == kLppNote ? "M8 > NOTE" : "M8 > SESSION", 8);
	}

	void MidiMacroM8V2::switchView(View v, bool sendButton)
	{
		if (v >= VIEW_COUNT || v == view_)
			return;

		View oldView = view_;
		releaseAllHeld();

		// Leaving Beat Repeat always drops the M8 back into Session first. This has to be a
		// PLAIN Session tap - if Shift is still latched (AUX+3, held for the rest of the AUX
		// hold) it has to be released around the tap, or the M8 sees Shift+Session again and
		// re-enters Beat Repeat instead of leaving it.
		bool sentSession = false;
		if (oldView == VIEW_BEAT)
		{
			if (shiftLatched_)
			{
				sendLpp(kLppShift, false);
				sendLppTap(kLppSession);
				sendLpp(kLppShift, true);
			}
			else
			{
				sendLppTap(kLppSession);
			}
			sentSession = true;
		}

		view_ = v;
		row_ = 8;

		const char *msg = "SESSION";
		switch (v)
		{
		case VIEW_SESSION:
			if (!sentSession && sendButton)
				sendLppTap(kLppSession);
			msg = "SESSION";
			break;
		case VIEW_CLIP:
			// OMX-side row/column picker over the M8's Session screen, so make sure it is there.
			if (!sentSession)
				sendLppTap(kLppSession);
			msg = "CLIP";
			break;
		case VIEW_CTRL:
			// Pure Control Map view: nothing goes to the Launchpad.
			msg = "CONTROL";
			break;
		case VIEW_MIX:
			// OMX-side view, but the mute/solo taps only make sense on the M8's Session
			// screen, so make sure it is there (skip if leaving Beat already sent it).
			if (!sentSession)
				sendLppTap(kLppSession);
			msg = "MIX";
			break;
		case VIEW_NOTE:
			if (sendButton)
				sendLppTap(kLppNote);
			msg = "NOTE";
			break;
		case VIEW_SEQ:
			if (sendButton)
				sendLppTap(kLppSeq);
			msg = "SEQ";
			break;
		case VIEW_PHRASE:
			sendLppTap(kLppSeq);
			msg = "PHRASE";
			break;
		case VIEW_BEAT:
			// Shift + Session. Shift may already be latched by AUX+3.
			if (shiftLatched_)
			{
				sendLppTap(kLppSession);
			}
			else
			{
				sendLpp(kLppShift, true);
				sendLppTap(kLppSession);
				sendLpp(kLppShift, false);
			}
			msg = "BEAT RPT";
			break;
		default:
			break;
		}

		omxDisp.displayMessageTimed(msg, 5);
		omxLeds.setDirty();
		omxDisp.setDirty();
	}

	// ---------------------------------------------------------------- Key -> pad

	// NOTE view whites: key 11 + s plays scale step s (0-15) above the base pad, sent as
	// the pad in the lowest grid row that holds it (spec section 3). K is the M8's row
	// interval, the NROW setting: 4 by default, 3 if the M8 lays rows out in true 4ths.
	// Infer the Notes-view row interval from the M8's root LEDs. The M8 lights roots on a
	// lattice: pad(r,c) is scale step (r-1)*K + (c-1); roots are the steps that are multiples of
	// the scale size N. We try each lit colour as the "root" colour and each K in 3..6, and keep
	// the (colour, K) whose root pads are a consistent multiple-of-N lattice (5<=N<=12) spanning
	// at least two rows. Returns the winning K, or 0 when nothing is confident enough.
	static int m8v2_gcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }

	uint8_t MidiMacroM8V2::detectAutoK() const
	{
		uint8_t bestK = 0;
		int bestScore = -1;
		for (uint8_t ci = 0; ci < 64; ci++)
		{
			uint8_t col = ledColor_[(1 + ci / 8) * 10 + (1 + ci % 8)];
			if (col == 0)
				continue;
			bool seen = false; // test each colour once
			for (uint8_t cj = 0; cj < ci && !seen; cj++)
				if (ledColor_[(1 + cj / 8) * 10 + (1 + cj % 8)] == col)
					seen = true;
			if (seen)
				continue;
			for (uint8_t K = 3; K <= 6; K++)
			{
				int minStep = 1000, cnt = 0;
				uint8_t rowsMask = 0;
				for (uint8_t pj = 0; pj < 64; pj++)
				{
					uint8_t r = 1 + pj / 8, c = 1 + pj % 8;
					if (ledColor_[r * 10 + c] != col)
						continue;
					int step = (r - 1) * K + (c - 1);
					if (step < minStep)
						minStep = step;
					rowsMask |= (uint8_t)(1 << (r - 1));
					cnt++;
				}
				if (cnt < 2)
					continue;
				uint8_t nrows = 0;
				for (uint8_t b = 0; b < 8; b++)
					if (rowsMask & (1 << b))
						nrows++;
				if (nrows < 2)
					continue; // need >=2 rows or K is unconstrained
				int g = 0;
				for (uint8_t pj = 0; pj < 64; pj++)
				{
					uint8_t r = 1 + pj / 8, c = 1 + pj % 8;
					if (ledColor_[r * 10 + c] != col)
						continue;
					g = m8v2_gcd(g, ((r - 1) * K + (c - 1)) - minStep);
				}
				if (g < 5 || g > 12)
					continue; // not a plausible scale size
				int score = cnt * 100 + g; // prefer more roots, then a larger scale period
				if (score > bestScore)
				{
					bestScore = score;
					bestK = K;
				}
			}
		}
		return bestK;
	}

	uint8_t MidiMacroM8V2::notesPadForKey(uint8_t key) const
	{
		if (key < 11 || key > 26)
			return 0;
		uint8_t s = (uint8_t)(key - 11);
		uint8_t k = (nrow_ == 4) ? autoK_ : (uint8_t)(3 + nrow_); // 0..3 = K3..K6, 4 = AUTO
		uint8_t row = (uint8_t)(1 + s / k);
		uint8_t col = (uint8_t)(1 + s % k);
		if (row > 8)
			return 0;
		return (uint8_t)(row * 10 + col);
	}

	uint8_t MidiMacroM8V2::beatPadForKey(uint8_t key)
	{
		if (key >= 3 && key <= 10)
			return (uint8_t)(kBeatTrackRow * 10 + (key - 2));   // track row, cols 1-8
		if (key >= 11 && key <= 18)
			return (uint8_t)(kBeatRangeRowA * 10 + (key - 10)); // range row A, cols 1-8
		if (key >= 19 && key <= 26)
			return (uint8_t)(kBeatRangeRowB * 10 + (key - 18)); // range row B, cols 1-8
		return 0;
	}

	uint8_t MidiMacroM8V2::sessionPadForKey(uint8_t key) const
	{
		if (key >= 11 && key <= 18)
		{
			if (padMode_ != PAD_CLIP)
				return (uint8_t)(kLppTrack1 + (key - 11)); // track buttons T1-T8
			return (uint8_t)(row_ * 10 + (key - 10));      // grid cols 1-8 of row_
		}
		if (key == 19)
			return (uint8_t)(row_ * 10 + 9); // row launch / scene
		return 0;
	}

	// CLIP whites 11-18: the eight pads of the selected row (spec section 9a).
	uint8_t MidiMacroM8V2::clipPadForKey(uint8_t key) const
	{
		if (key < 11 || key > 18)
			return 0;
		if (clipColMode_)
			return (uint8_t)((19 - key) * 10 + clipRow_); // column clipRow_, key 11 = row 8 (top) .. 18 = row 1
		return (uint8_t)(clipRow_ * 10 + (key - 10));
	}

	// CLIP whites 19-26: the right-column scene buttons for rows 1-8 (19, 29 ... 89).
	uint8_t MidiMacroM8V2::clipScenePadForKey(uint8_t key)
	{
		if (key < 19 || key > 26)
			return 0;
		return (uint8_t)((27 - key) * 10 + 9); // key 19 = row 8 (top) .. 26 = row 1
	}

	uint8_t MidiMacroM8V2::seqNotePadNote(uint8_t key)
	{
		if (key >= 3 && key <= 10)
			return (uint8_t)(key + 8); // LPP row 1 C1-C8 -> 11-18 (keypads)
		return 0;
	}

	uint8_t MidiMacroM8V2::seqSlotNote(uint8_t key)
	{
		if (key < 11 || key > 26) return 0;
		uint8_t idx = key - 11;
		return (uint8_t)((8 - idx / 4) * 10 + (idx % 4) + 1); // cols 1-4
	}

	uint8_t MidiMacroM8V2::seqPatternNote(uint8_t key)
	{
		if (key < 11 || key > 26) return 0;
		uint8_t idx = key - 11;
		return (uint8_t)((8 - idx / 4) * 10 + (idx % 4) + 5); // cols 5-8
	}

	// SEQ v2: top row keys 1-10 -> 10 consecutive keyboard notes (rows 1-4), same interval as
	// Notes view so K / auto-K carry over. Used while a step is held to lock a note into it.
	uint8_t MidiMacroM8V2::seqTopNotePad(uint8_t key) const
	{
		if (key < 1 || key > 10) return 0;
		uint8_t s = (uint8_t)(key - 1);
		uint8_t k = (nrow_ == 4) ? autoK_ : (uint8_t)(3 + nrow_);
		uint8_t row = (uint8_t)(1 + s / k), col = (uint8_t)(1 + s % k);
		if (row > 8) return 0;
		return (uint8_t)(row * 10 + col);
	}

	// ------------------------------------------------------------------ Runtime

	void MidiMacroM8V2::loopUpdate()
	{
		if (!enabled_)
			return;

		if (!linked_)
		{
			uint32_t now = millis();
			if (now - lastIdentityMs_ >= 1000)
			{
				sendIdentity();
			}
		}

		// omxLeds normally redraws itself on every blink transition (blinkAutoRefresh),
		// but we don't want to rely on that staying true for whichever mode currently
		// owns the strip. Force a redraw ourselves, at most once per transition, and only
		// when something we're mirroring is actually flash/pulse.
		bool blink = omxLeds.getBlinkState();
		bool slowBlink = omxLeds.getSlowBlinkState();
		if (blink != lastBlinkState_ || slowBlink != lastSlowBlinkState_)
		{
			lastBlinkState_ = blink;
			lastSlowBlinkState_ = slowBlink;

			bool anyBlinking = (padMode_ == PAD_MUTE && muteLatch_) || (padMode_ == PAD_SOLO && soloLatch_) ||
							   recLatched_ || shiftLatched_;
			for (uint8_t i = 0; i < kNumLppNotes && !anyBlinking; i++)
			{
				if (ledMode_[i] != 0 && ledColor_[i] != 0)
					anyBlinking = true;
			}

			if (anyBlinking)
				omxLeds.setDirty();
		}

		// AUTO Notes row interval: watch the M8's root LEDs and lock K to them (throttled).
		if (view_ == VIEW_NOTE && nrow_ == 4)
		{
			uint32_t now = millis();
			if (now - lastAutoNrowMs_ >= 250)
			{
				lastAutoNrowMs_ = now;
				uint8_t k = detectAutoK();
				if (k >= 3 && k != autoK_)
				{
					autoK_ = k;
					omxLeds.setDirty();
					omxDisp.setDirty();
				}
			}
		}
	}

	void MidiMacroM8V2::onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta)
	{
		(void)prevValue;
		(void)newValue;
		(void)analogDelta;
		omxUtil.sendPots(potIndex, midiMacroConfig.midiMacroChan);
	}

	// ---------------------------------------------------------------- MIDI in

	bool MidiMacroM8V2::inMidiNoteOn(byte channel, byte note, byte velocity)
	{
		// LPP LED feedback: ch 1 static, ch 2 flash, ch 3 pulse.
		if (channel < 1 || channel > 3)
			return false;

		if (note < kNumLppNotes)
		{
			uint8_t prev = ledColor_[note];
			ledColor_[note] = velocity; // vel 0 == note off
			ledMode_[note] = channel - 1;
			followM8View(note, prev, velocity);
			// Live-refresh the OLED 8x8 grid while the M8 repaints (e.g. mid AUX scroll),
			// not just when AUX is released.
			if (enabled_ && prev != velocity)
			{
				uint8_t c = note % 10;
				if (note >= 11 && note <= 88 && c >= 1 && c <= 8)
					omxDisp.setDirty();
			}
		}

		if (!linked_)
		{
			linked_ = true;
			if (enabled_)
				omxDisp.setDirty();
		}

		// Only the macro's own drawLEDs() reads the cache, so don't force the host mode
		// to redraw its strip on every LED note when the macro isn't entered.
		if (enabled_)
			omxLeds.setDirty();
		return true;
	}

	bool MidiMacroM8V2::inMidiNoteOff(byte channel, byte note, byte velocity)
	{
		(void)velocity;

		if (channel < 1 || channel > 3)
			return false;

		if (note < kNumLppNotes)
		{
			ledColor_[note] = 0;
			ledMode_[note] = channel - 1;
		}

		if (!linked_)
		{
			linked_ = true;
			if (enabled_)
				omxDisp.setDirty();
		}

		if (enabled_)
			omxLeds.setDirty();
		return true;
	}

	void MidiMacroM8V2::inMidiControlChange(byte channel, byte control, byte value)
	{
		// Some hosts send the ring LEDs as CCs with the same number as the LPP note.
		if (channel < 1 || channel > 3)
			return;

		if (control < kNumLppNotes)
		{
			uint8_t prev = ledColor_[control];
			ledColor_[control] = value;
			followM8View(control, prev, value);
			ledMode_[control] = channel - 1;
			if (enabled_ && prev != value)
			{
				uint8_t c = control % 10;
				if (control >= 11 && control <= 88 && c >= 1 && c <= 8)
					omxDisp.setDirty();
			}

			if (!linked_)
			{
				linked_ = true;
				if (enabled_)
					omxDisp.setDirty();
			}

			if (enabled_)
				omxLeds.setDirty();
		}
	}

	// ------------------------------------------------------------------- Keys

	// Pressing the active MUTE/SOLO key again toggles latch vs momentary; pressing a
	// different mode key switches modes, releasing/reacquiring the M8-side modifier.
	//
	// NOTE (unverified on hardware): the actual M8/LPP Mute-Solo semantics (a held modifier
	// + track button vs a toggled mode) are not confirmed. What we implement here is
	// intentionally simple: entering MUTE/SOLO holds down Note 2/3 for as long as that mode
	// is active and keys 11-18 send track buttons 101-108 while held.
	void MidiMacroM8V2::setPadMode(PadMode newMode)
	{
		if (newMode == padMode_)
		{
			if (padMode_ == PAD_MUTE)
			{
				muteLatch_ = !muteLatch_;
				omxDisp.displayMessageTimed(muteLatch_ ? "Mute: Latch" : "Mute: Moment", 5);
			}
			else if (padMode_ == PAD_SOLO)
			{
				soloLatch_ = !soloLatch_;
				omxDisp.displayMessageTimed(soloLatch_ ? "Solo: Latch" : "Solo: Moment", 5);
			}

			omxLeds.setDirty();
			return;
		}

		// Leaving MUTE/SOLO releases its modifier and any OMX-side latched tracks.
		releaseLatchedTracks();
		releaseMomentaryTracks(); // must run before the modifier release just below
		if (padMode_ == PAD_MUTE)
			sendLpp(kLppMute, false);
		else if (padMode_ == PAD_SOLO)
			sendLpp(kLppSolo, false);

		padMode_ = newMode;

		if (padMode_ == PAD_MUTE)
			sendLpp(kLppMute, true);
		else if (padMode_ == PAD_SOLO)
			sendLpp(kLppSolo, true);

		omxDisp.displayMessageTimed(padMode_ == PAD_MUTE ? "MUTE MODE" : padMode_ == PAD_SOLO ? "SOLO MODE" : "CLIP MODE", 5);
		omxLeds.setDirty();
		omxDisp.setDirty();
	}

	void MidiMacroM8V2::onKeyUpdate(OMXKeypadEvent e)
	{
		uint8_t thisKey = (uint8_t)e.key();

		// Ignore held repeats, same as the other macros.
		if (e.held())
			return;

		if (thisKey >= kNumKeys)
			return;

		// Key up: always release what we actually sent, never a recomputed note.
		// Runs before the AUX guard so a pad held when AUX goes down can't stick.
		if (!e.down())
		{
			if (thisKey == 0)
			{
				auxHeld_ = false;
				if (shiftLatched_)
				{
					shiftLatched_ = false;
					sendLpp(kLppShift, false);
				}
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			}
			if (thisKey == 3 && trackHeld_)
			{
				trackHeld_ = false;
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			}

			// Double-tap a clip pad -> jump to the sequencer, like a real Launchpad. Handled
			// OMX-side so it does not depend on the M8 echoing the Seq button back.
			if (e.clicks() >= 2 && thisKey >= 11 && thisKey <= 18 && !clipMuteChord_ &&
				(view_ == VIEW_CLIP || (view_ == VIEW_SESSION && padMode_ == PAD_CLIP)))
			{
				if (keyNoteSent_[thisKey] != 0)
				{
					sendLpp(keyNoteSent_[thisKey], false);
					keyNoteSent_[thisKey] = 0;
				}
				switchView(VIEW_SEQ); // sends the Seq button (97); the M8 follows
				omxDisp.displayMessageTimed("M8 > SEQ", 8);
				return;
			}

			// CLIP mute chord ends as soon as either of keys 1/2 lifts; the other key's
			// Clear/Duplicate is not re-asserted until it is pressed again.
			if (clipMuteChord_ && (thisKey == 1 || thisKey == 2))
			{
				clipMuteChord_ = false;
				sendLpp(kLppMute, false);
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			}

			// Control Map cluster (macro channel).
			if (ctrlSent_[thisKey] >= 0)
			{
				MM::sendNoteOff((uint8_t)ctrlSent_[thisKey], 0, midiMacroConfig.midiMacroChan);
				ctrlSent_[thisKey] = -1;
				omxLeds.setDirty();
				return;
			}

			// Momentary MUTE/SOLO track: re-tap to undo the toggle the press made.
			if (thisKey >= 11 && thisKey <= 18)
			{
				uint8_t bit = (uint8_t)(1 << (thisKey - 11));
				if (momentaryTracks_ & bit)
				{
					sendLppTap((uint8_t)(kLppTrack1 + (thisKey - 11)));
					momentaryTracks_ &= (uint8_t)~bit;
					omxLeds.setDirty();
					return;
				}
			}

			if (keyNoteSent_[thisKey] != 0)
			{
				sendLpp(keyNoteSent_[thisKey], false);
				keyNoteSent_[thisKey] = 0;
				omxLeds.setDirty();
			}
			if (thisKey == seqHeldStep_)
				seqHeldStep_ = 0; // Seq v2: this step is no longer held
			return;
		}

		// Key down
		if (thisKey == 0)
		{
			auxHeld_ = true;
			omxLeds.setDirty();
			return;
		}

		// AUX shortcut layer - identical in every view (spec section 1).
		if (auxHeld_)
		{
			switch (thisKey)
			{
			case 1:
				sendLppTap(kLppTrackL); // Track < ; with Shift latched this is the M8 Live mode toggle (AUX LED confirms)
				return;
			case 2:
				sendLppTap(kLppTrackR); // Track >
				return;
			case 3:
				// Shift latched for the rest of the AUX hold (a second press cancels it).
				shiftLatched_ = !shiftLatched_;
				sendLpp(kLppShift, shiftLatched_);
				omxDisp.displayMessageTimed(shiftLatched_ ? "SHIFT ON" : "SHIFT OFF", 5);
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			case 4:
				return; // dark, no-op
			case 5:
				sendLppTap(kLppProject);
				omxDisp.displayMessageTimed(shiftLatched_ ? "SEL PROJECT" : "PROJECT", 5);
				return;
			case 6:
				doStoreSnapshot();
				return;
			case 7:
				doRecallSnapshot();
				return;
			case 8:
				recLatched_ = !recLatched_;
				sendLpp(kLppRec, recLatched_);
				omxDisp.displayMessageTimed(recLatched_ ? "REC HELD" : "REC OFF", 5);
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			case 9:
				if (!recLatched_)
					sendLppTap(kLppRec);
				omxDisp.displayMessageTimed("EDIT/REC", 5);
				return;
			case 10:
				sendLppTap(kLppPlay);
				omxDisp.displayMessageTimed("PLAY", 5);
				return;
			case 11:
				sendLppTap(kLppDown); // scroll - no message so the 8x8 grid stays visible
				return;
			case 12:
				sendLppTap(kLppUp); // scroll - no message
				return;
			case 13:
			case 14:
				changePotBank(thisKey == 14);
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			case 15:
			case 16:
			case 17:
			case 18:
			case 19:
			case 20:
			case 21:
			case 22:
				switchView((View)(thisKey - 15));
				return;
			case 26:
				doWaveformMacro();
				return;
			default:
				return; // consumed, no-op
			}
		}

		// ---------------------------------------------------------- bare key, per view

		if (view_ == VIEW_CTRL)
		{
			// Pure Control Map cluster on M-CH; every other key is inert.
			int8_t cm = ctrlViewNote(thisKey);
			if (cm >= 0)
			{
				MM::sendNoteOn((uint8_t)cm, 1, midiMacroConfig.midiMacroChan);
				ctrlSent_[thisKey] = cm; // key-up releases this stored note, never a recomputed one
				omxLeds.setDirty();
			}
			return;
		}

		if (isClipView())
		{
			// Keys 1/2 = Clear / Duplicate (held). Both together = Mute chord: Clear and
			// Duplicate are released and Launchpad Mute is held instead, so keys 3-10 become
			// the track buttons (tap to mute/unmute that track) for as long as both are down.
			if (thisKey == 1 || thisKey == 2)
			{
				uint8_t other = (thisKey == 1) ? 2 : 1;
				if (keyNoteSent_[other] != 0 && !clipMuteChord_)
				{
					sendLpp(keyNoteSent_[other], false);
					keyNoteSent_[other] = 0;
					clipMuteChord_ = true;
					sendLpp(kLppMute, true);
					omxDisp.displayMessageTimed("MUTE", 5);
				}
				else if (!clipMuteChord_)
				{
					uint8_t note = (thisKey == 1) ? kLppClear : kLppDup;
					sendLpp(note, true);
					keyNoteSent_[thisKey] = note;
					omxDisp.displayMessageTimed(thisKey == 1 ? "CLEAR" : "DUPLICATE", 5);
				}
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			}

			if (thisKey >= 3 && thisKey <= 10)
			{
				if (clipMuteChord_)
				{
					sendLppTap((uint8_t)(kLppTrack1 + (thisKey - 3))); // mute/unmute track 1-8
					omxLeds.setDirty();
					return;
				}
				// Row mode: key 3 = row 8 (top) .. key 10 = row 1, like the OLED grid.
				// Column mode: key 3 = column 1 .. key 10 = column 8.
				clipRow_ = clipColMode_ ? (uint8_t)(thisKey - 2) : (uint8_t)(11 - thisKey); // local only, no MIDI
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			}

			uint8_t note = 0;
			if (thisKey <= 18)
				note = clipPadForKey(thisKey);
			else
				note = clipScenePadForKey(thisKey);

			if (note != 0)
			{
				sendLpp(note, true);
				keyNoteSent_[thisKey] = note;
				omxLeds.setDirty();
			}
			return;
		}

		if (view_ == VIEW_SEQ)
		{
			// Seq v2 (spec ML-SEQ-V2-SPEC): white keys 11-26 are the 16 steps (top-left 4x4);
			// hold a step, then the top row edits it per the selected mode. Clear/Duplicate are
			// modifier-first (hold, then tap steps). Record must be armed (AUX+8/9) to lock.
			bool modHeld = (keyNoteSent_[1] == kLppClear) || (keyNoteSent_[2] == kLppDup);
			if (thisKey >= 11 && thisKey <= 26)
			{
				uint8_t pad = seqSlotNote(thisKey);
				if (pad == 0)
					return;
				sendLpp(pad, true); // with a modifier held: clear/dup this step; else select it to edit
				keyNoteSent_[thisKey] = pad;
				if (!modHeld)
					seqHeldStep_ = thisKey; // step-first: this step is now held for editing
				omxLeds.setDirty();
				return;
			}
			if (seqHeldStep_ != 0)
			{
				// a step is held: the top row applies the selected mode to it
				if (seqMode_ == 0) // NOTE
				{
					uint8_t p = seqTopNotePad(thisKey);
					if (p)
						sendLppTap(p);
				}
				else if (seqMode_ == 1) // VEL: side-row ring column 19..89
				{
					if (thisKey >= 1 && thisKey <= 8)
						sendLppTap((uint8_t)(thisKey * 10 + 9));
				}
				else // OCT: left half = down, right half = up
				{
					if (thisKey >= 1 && thisKey <= 10)
						sendLppTap(thisKey <= 5 ? kLppDown : kLppUp);
				}
				omxLeds.setDirty();
				return;
			}
			// idle top row: Clear/Dup modifiers + mode select
			switch (thisKey)
			{
			case 1: sendLpp(kLppClear, true); keyNoteSent_[1] = kLppClear; omxDisp.displayMessageTimed("CLEAR", 5); break;
			case 2: sendLpp(kLppDup, true); keyNoteSent_[2] = kLppDup; omxDisp.displayMessageTimed("DUPLICATE", 5); break;
			case 3: seqMode_ = 0; omxDisp.displayMessageTimed("SEQ: NOTE", 5); break;
			case 4: seqMode_ = 1; omxDisp.displayMessageTimed("SEQ: VEL", 5); break;
			case 5: seqMode_ = 2; omxDisp.displayMessageTimed("SEQ: OCT", 5); break;
			default: return; // 6-10 reserved
			}
			omxLeds.setDirty();
			omxDisp.setDirty();
			return;
		}

		if (view_ == VIEW_PHRASE)
		{
			uint8_t note = 0;
			if (thisKey == 1)
				note = kLppClear; // Clear held
			else if (thisKey == 2)
				note = kLppDup; // Duplicate held
			else if (thisKey <= 10)
				note = seqNotePadNote(thisKey);
			else
				note = seqPatternNote(thisKey);

			if (note != 0)
			{
				sendLpp(note, true);
				keyNoteSent_[thisKey] = note;
				omxLeds.setDirty();
			}
			return;
		}

		if (view_ == VIEW_BEAT)
		{
			uint8_t note = beatPadForKey(thisKey);
			if (note != 0)
			{
				sendLpp(note, true);
				keyNoteSent_[thisKey] = note;
				omxLeds.setDirty();
			}
			return;
		}

		if (view_ == VIEW_MIX)
		{
			if (thisKey >= 11 && thisKey <= 18)
			{
				sendLpp(kLppMute, true);
				delay(kModSettleMs); // register Mute-held before the track tap
				sendLppTap((uint8_t)(kLppTrack1 + (thisKey - 11)));
				sendLpp(kLppMute, false);
				omxLeds.setDirty();
				return;
			}
			if (thisKey >= 19 && thisKey <= 26)
			{
				sendLpp(kLppSolo, true);
				delay(kModSettleMs); // register Solo-held before the track tap (else it mutes)
				sendLppTap((uint8_t)(kLppTrack1 + (thisKey - 19)));
				sendLpp(kLppSolo, false);
				omxLeds.setDirty();
				return;
			}

			switch (thisKey)
			{
			case 1: doMixAllTracks(kLppMute); break;
			case 3: doGotoMixerMacro(); break;
			case 4: doRecallSnapshot(); break;
			case 5: doStoreSnapshot(); break;
			case 6: doMixAllTracks(kLppSolo); break;
			case 9: doWaveformMacro(); break;
			case 10: sendLppTap(kLppPlay); break;
			default: break;
			}
			omxLeds.setDirty();
			return;
		}

		if (view_ == VIEW_NOTE)
		{
			if (thisKey == 3)
			{
				trackHeld_ = true; // hold: keys 11-18 become track select
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			}

			uint8_t note = 0;
			switch (thisKey)
			{
			case 1: note = kLppDown; break;
			case 2: note = kLppUp; break;
			case 6: note = kLppTrackL; break;
			case 7: note = kLppTrackR; break;
			case 9: note = kLppRec; break;
			case 10: note = kLppPlay; break;
			default: break;
			}
			if (note == 0 && thisKey >= 11 && thisKey <= 26)
				note = (trackHeld_ && thisKey <= 18) ? (uint8_t)(kLppTrack1 + (thisKey - 11))
													 : notesPadForKey(thisKey);

			if (note != 0)
			{
				sendLpp(note, true);
				keyNoteSent_[thisKey] = note;
				omxLeds.setDirty();
			}
			return;
		}

		// VIEW_SESSION
		switch (thisKey)
		{
		case 3: setPadMode(PAD_CLIP); return;
		case 4: setPadMode(PAD_MUTE); return;
		case 5: setPadMode(PAD_SOLO); return;
		case 6: // Clear (held): tap a pad to delete that chain (M8 edit submode)
			sendLpp(kLppClear, true);
			keyNoteSent_[thisKey] = kLppClear;
			omxDisp.displayMessageTimed("CLEAR", 5);
			omxLeds.setDirty();
			return;
		case 7: // Duplicate (held): tap source, tap destination
			sendLpp(kLppDup, true);
			keyNoteSent_[thisKey] = kLppDup;
			omxDisp.displayMessageTimed("DUPLICATE", 5);
			omxLeds.setDirty();
			return;
		default: break;
		}

		// Nav cluster drives the M8 Control Map on the macro channel (M-CH), like the
		// pre-Launchpad M8 macro. Fixed per key, so key-up releases cleanly.
		int8_t cm = controlMapNote(thisKey);
		if (cm >= 0)
		{
			MM::sendNoteOn((uint8_t)cm, 1, midiMacroConfig.midiMacroChan);
			ctrlSent_[thisKey] = cm;
			omxLeds.setDirty();
			return;
		}

		if (thisKey == 1 || thisKey == 2)
		{
			uint8_t note = (thisKey == 1) ? kLppDown : kLppUp;
			sendLpp(note, true);
			keyNoteSent_[thisKey] = note;
			omxLeds.setDirty();
			return;
		}

		uint8_t note = sessionPadForKey(thisKey);
		if (note == 0)
			return;

		// MUTE/SOLO: keys 11-18 are the M8 track buttons. A tap toggles that track's
		// mute/solo (the M8 acts on the press and ignores the release). Momentary re-taps
		// on key-up to undo it; latch leaves it toggled until pressed again.
		if (padMode_ != PAD_CLIP && thisKey >= 11 && thisKey <= 18)
		{
			uint8_t bit = (uint8_t)(1 << (thisKey - 11));
			bool latch = (padMode_ == PAD_MUTE && muteLatch_) || (padMode_ == PAD_SOLO && soloLatch_);
			sendLppTap(note);
			if (latch)
				latchedTracks_ ^= bit; // LED memory only; the M8 holds the real state
			else
				momentaryTracks_ |= bit;
			omxLeds.setDirty();
			return;
		}

		sendLpp(note, true);
		keyNoteSent_[thisKey] = note;
		omxLeds.setDirty();
	}

	// ------------------------------------------------------------------- LEDs

	void MidiMacroM8V2::drawPaletteKey(uint8_t key, uint8_t note, uint32_t offColor)
	{
		if (note >= kNumLppNotes)
		{
			strip.setPixelColor(key, offColor);
			return;
		}

		uint8_t idx = ledColor_[note];
		if (idx == 0)
		{
			strip.setPixelColor(key, offColor);
			return;
		}

		uint32_t color = lppPaletteColor(idx);
		uint8_t mode = ledMode_[note];

		if (mode == 1) // flash
		{
			strip.setPixelColor(key, omxLeds.getBlinkState() ? color : LEDOFF);
		}
		else if (mode == 2) // pulse
		{
			strip.setPixelColor(key, omxLeds.getSlowBlinkState() ? color : halfColor(color));
		}
		else // static
		{
			strip.setPixelColor(key, color);
		}
	}

	// AUX key = M8 Live mode (Novation logo, note 99), or red slow-blink while Record is
	// latched, or plain purple (spec section 5d). Always plain purple while AUX is held so
	// the shortcut layer always looks the same.
	void MidiMacroM8V2::drawAuxKey()
	{
		if (auxHeld_)
		{
			strip.setPixelColor(0, PURPLE);
			return;
		}
		if (recLatched_)
		{
			strip.setPixelColor(0, omxLeds.getSlowBlinkState() ? RED : PURPLE);
			return;
		}
		drawPaletteKey(0, kLppLogo, PURPLE);
	}

	void MidiMacroM8V2::drawLEDs()
	{
		if (omxLeds.isDirty() == false)
			return;

		omxLeds.setAllLEDS(0, 0, 0);

		drawAuxKey();

		if (auxHeld_)
		{
			// AUX shortcut overlay - everything else stays dark.
			strip.setPixelColor(1, RED);
			strip.setPixelColor(2, RED);
			bool shiftDark = shiftLatched_ && !omxLeds.getBlinkState();
			strip.setPixelColor(3, shiftDark ? LEDOFF : MAGENTA);
			strip.setPixelColor(4, LEDOFF); // dark, no-op
			strip.setPixelColor(5, MAGENTA);
			strip.setPixelColor(6, MAGENTA);
			strip.setPixelColor(7, MAGENTA);
			strip.setPixelColor(8, recLatched_ ? RED : DKRED);
			strip.setPixelColor(9, RED);
			strip.setPixelColor(10, ledColor_[kLppPlay] != 0 ? GREEN : WHITE);
			strip.setPixelColor(11, RED); // LPP Down
			strip.setPixelColor(12, RED); // LPP Up

			// Pot bank -/+ : same preview / switch-flash treatment as the normal AUX layer.
			{
				uint32_t fc;
				bool lit;
				if (potBankAuxPollFlash(&fc, &lit))
				{
					strip.setPixelColor(13, lit ? fc : LEDOFF);
					strip.setPixelColor(14, lit ? fc : LEDOFF);
				}
				else
				{
					uint32_t c13;
					uint32_t c14;
					potBankAuxPreviewColors((uint8_t)potSettings.potbank, &c13, &c14);
					strip.setPixelColor(13, c13);
					strip.setPixelColor(14, c14);
				}
			}

			for (uint8_t k = 15; k <= 22; k++)
				strip.setPixelColor(k, (uint8_t)view_ == (k - 15) ? MAGENTA : DKMAGENTA);
			strip.setPixelColor(26, YELLOW);
			return;
		}

		if (view_ == VIEW_SESSION)
		{
			// Keys 1/2 move the M8 session box up/down.
			strip.setPixelColor(1, RBLUE);
			strip.setPixelColor(2, RBLUE);

			// Pad mode keys: active bright, inactive dim; MUTE/SOLO blink when latched.
			bool muteDark = (padMode_ == PAD_MUTE && muteLatch_ && !omxLeds.getSlowBlinkState());
			bool soloDark = (padMode_ == PAD_SOLO && soloLatch_ && !omxLeds.getSlowBlinkState());

			strip.setPixelColor(3, padMode_ == PAD_CLIP ? MAGENTA : DKMAGENTA);
			strip.setPixelColor(4, (padMode_ == PAD_MUTE && !muteDark) ? RED : DKRED);
			strip.setPixelColor(5, (padMode_ == PAD_SOLO && !soloDark) ? YELLOW : DKYELLOW);
			strip.setPixelColor(6, keyNoteSent_[6] ? WHITE : PURPLE); // Clear (held)
			strip.setPixelColor(7, keyNoteSent_[7] ? WHITE : PURPLE); // Duplicate (held)

			// Nav cluster: colour follows the function, wherever HAND put it.
			for (uint8_t k = 8; k <= 26; k++)
			{
				if (k > 10 && k < 21)
					continue;
				int8_t cm = controlMapNote(k);
				uint32_t c = LEDOFF;
				switch (cm)
				{
				case kCmUp:
				case kCmDown:
				case kCmLeft:
				case kCmRight: c = INDIGO; break;
				case kCmOption: c = ORANGE; break;
				case kCmEdit: c = RBLUE; break;
				case kCmShift: c = GREEN; break;
				case kCmPlay: c = ledColor_[kLppPlay] != 0 ? GREEN : WHITE; break;
				default: continue;
				}
				strip.setPixelColor(k, c);
			}

			drawPaletteKey(19, (uint8_t)(row_ * 10 + 9), LOWWHITE); // Row launch / scene

			for (uint8_t k = 11; k <= 18; k++)
			{
				uint8_t note = sessionPadForKey(k);
				drawPaletteKey(k, note, LEDOFF);
				if (padMode_ != PAD_CLIP && ledColor_[note] == 0 && (latchedTracks_ & (1 << (k - 11))))
					strip.setPixelColor(k, padMode_ == PAD_MUTE ? RED : YELLOW); // latched, no colour from the M8
			}
		}
		else if (isClipView())
		{
			strip.setPixelColor(1, clipMuteChord_ ? RED : (keyNoteSent_[1] ? WHITE : RED));    // Clear
			strip.setPixelColor(2, clipMuteChord_ ? RED : (keyNoteSent_[2] ? WHITE : ORANGE)); // Duplicate

			if (clipMuteChord_)
			{
				for (uint8_t k = 3; k <= 10; k++) // track buttons: mirror T1-T8 (red = muted)
					drawPaletteKey(k, (uint8_t)(kLppTrack1 + (k - 3)), DKRED);
			}
			else
			{
				for (uint8_t k = 3; k <= 10; k++)
					strip.setPixelColor(k, (clipColMode_ ? (uint8_t)(k - 2) : (uint8_t)(11 - k)) == clipRow_ ? WHITE : LOWWHITE);
			}

			for (uint8_t k = 11; k <= 18; k++)
				drawPaletteKey(k, clipPadForKey(k), LEDOFF);
			for (uint8_t k = 19; k <= 26; k++)
				drawPaletteKey(k, clipScenePadForKey(k), LOWWHITE);
		}
		else if (view_ == VIEW_CTRL)
		{
			// Only the cluster is lit; everything else stays dark (setAllLEDS above).
			for (uint8_t k = 1; k < kNumKeys; k++)
			{
				int8_t cm = ctrlViewNote(k);
				uint32_t c;
				switch (cm)
				{
				case kCmUp:
				case kCmDown:
				case kCmLeft:
				case kCmRight: c = INDIGO; break;
				case kCmOption: c = ORANGE; break;
				case kCmEdit: c = RBLUE; break;
				case kCmShift: c = GREEN; break;
				case kCmPlay: c = ledColor_[kLppPlay] != 0 ? GREEN : WHITE; break;
				default: continue;
				}
				strip.setPixelColor(k, c);
			}
		}
		else if (view_ == VIEW_MIX)
		{
			strip.setPixelColor(1, ORANGE);	 // unmute all
			strip.setPixelColor(3, LIME);	 // goto mixer
			strip.setPixelColor(4, CYAN);	 // recall snapshot
			strip.setPixelColor(5, MAGENTA); // store snapshot
			strip.setPixelColor(6, RED);	 // unsolo all
			strip.setPixelColor(9, YELLOW);	 // waveform
			strip.setPixelColor(10, ledColor_[kLppPlay] != 0 ? GREEN : BLUE);

			for (uint8_t k = 11; k <= 18; k++)
				drawPaletteKey(k, (uint8_t)(kLppTrack1 + (k - 11)), ORANGE);
			for (uint8_t k = 19; k <= 26; k++)
				drawPaletteKey(k, (uint8_t)(kLppTrack1 + (k - 19)), RED);
		}
		else if (view_ == VIEW_SEQ)
		{
			// steps on the white keys (held step = white); top row depends on hold state
			for (uint8_t k = 11; k <= 26; k++)
			{
				if (k == seqHeldStep_)
					strip.setPixelColor(k, WHITE);
				else
					drawPaletteKey(k, seqSlotNote(k), LOWWHITE);
			}
			if (seqHeldStep_ != 0)
			{
				if (seqMode_ == 0)
					for (uint8_t k = 1; k <= 10; k++) drawPaletteKey(k, seqTopNotePad(k), DKCYAN);
				else if (seqMode_ == 1)
					for (uint8_t k = 1; k <= 10; k++) strip.setPixelColor(k, (k <= 8) ? GREEN : LEDOFF);
				else
					for (uint8_t k = 1; k <= 10; k++) strip.setPixelColor(k, (k <= 5) ? BLUE : CYAN);
			}
			else
			{
				strip.setPixelColor(1, keyNoteSent_[1] ? WHITE : RED);	 // Clear
				strip.setPixelColor(2, keyNoteSent_[2] ? WHITE : ORANGE); // Duplicate
				strip.setPixelColor(3, seqMode_ == 0 ? CYAN : DKCYAN);	 // NOTE
				strip.setPixelColor(4, seqMode_ == 1 ? GREEN : DKGREEN); // VEL
				strip.setPixelColor(5, seqMode_ == 2 ? MAGENTA : DKMAGENTA); // OCT
			}
		}
		else if (view_ == VIEW_PHRASE)
		{
			strip.setPixelColor(1, RED);	// Clear (hold)
			strip.setPixelColor(2, ORANGE); // Duplicate (hold)
			for (uint8_t k = 3; k <= 10; k++)
				drawPaletteKey(k, seqNotePadNote(k), DKCYAN);
			for (uint8_t k = 11; k <= 26; k++)
				drawPaletteKey(k, seqPatternNote(k), DKMAGENTA);
		}
		else if (view_ == VIEW_BEAT)
		{
			for (uint8_t k = 3; k <= 10; k++)
				drawPaletteKey(k, beatPadForKey(k), DKBLUE);
			for (uint8_t k = 11; k <= 18; k++)
				drawPaletteKey(k, beatPadForKey(k), DKCYAN);
			for (uint8_t k = 19; k <= 26; k++)
				drawPaletteKey(k, beatPadForKey(k), DKCYAN);
		}
		else // VIEW_NOTE
		{
			strip.setPixelColor(1, INDIGO);
			strip.setPixelColor(2, INDIGO);
			strip.setPixelColor(3, trackHeld_ ? WHITE : RED);			// Track (hold)
			strip.setPixelColor(6, RBLUE);								// Track <
			strip.setPixelColor(7, RBLUE);								// Track >
			strip.setPixelColor(9, ledColor_[kLppRec] != 0 ? RED : DKRED);	// Edit / Rec
			strip.setPixelColor(10, ledColor_[kLppPlay] != 0 ? GREEN : WHITE);	// Play

			for (uint8_t k = 11; k <= 26; k++)
			{
				bool trk = (trackHeld_ && k <= 18);
				uint8_t note = trk ? (uint8_t)(kLppTrack1 + (k - 11)) : notesPadForKey(k);
				drawPaletteKey(k, note, trk ? DKRED : LOWWHITE);
			}
		}
	}

	// ---------------------------------------------------------------- Display

	void MidiMacroM8V2::onEncoderChangedEditParam(Encoder::Update enc)
	{
		int8_t page = params_.getSelPage();
		int8_t param = params_.getSelParam();

		if (enc.dir() == 0)
			return;

		if (page == M8V2PAGE_SETTING)
		{
			if (param == 0)
			{
				releaseControlNotes(); // the cluster moves under the user's fingers
				rightHand_ = !rightHand_;
				omxDisp.displayMessageTimed(rightHand_ ? "Hand: Right" : "Hand: Left", 5);
			}
			else if (param == 1)
			{
				muteLatch_ = !muteLatch_;
				omxDisp.displayMessageTimed(muteLatch_ ? "Mute: Latch" : "Mute: Moment", 5);
			}
			else if (param == 2)
			{
				soloLatch_ = !soloLatch_;
				omxDisp.displayMessageTimed(soloLatch_ ? "Solo: Latch" : "Solo: Moment", 5);
			}
			else if (param == 3)
			{
				releaseAllKeys(); // never leave a note held while the message type changes
				releaseLatchedTracks();
				ringAsCC_ = !ringAsCC_;
				omxDisp.displayMessageTimed(ringAsCC_ ? "Ring: CC" : "Ring: Note", 5);
			}
			omxLeds.setDirty();
			omxDisp.setDirty();
			return;
		}

		if (page == M8V2PAGE_SETTING2)
		{
			if (param == 0)
			{
				releaseAllKeys();
				nrow_ = (uint8_t)((nrow_ + 1) % 5);
				if (nrow_ == 4)
					omxDisp.displayMessageTimed("Notes: AUTO", 5);
				else
				{
					char m[16];
					snprintf(m, sizeof(m), "Notes: K%d", (int)(3 + nrow_));
					omxDisp.displayMessageTimed(m, 5);
				}
				omxLeds.setDirty();
				omxDisp.setDirty();
			}
			return;
		}

		// Main page. In Clip Launch the encoder picks the row (spec section 9a); everywhere
		// else it is the LPP scroll, same as AUX+12 / AUX+11.
		if (isClipView() && param == 1)
		{
			// Orientation param (right of the grid): toggle rows <-> columns.
			clipColMode_ = !clipColMode_;
			omxDisp.displayMessageTimed(clipColMode_ ? "Clip: Cols" : "Clip: Rows", 5);
			omxLeds.setDirty();
			omxDisp.setDirty();
			return;
		}
		if (isClipView())
		{
			int step = (enc.dir() > 0 ? 1 : -1);
			if (!clipColMode_)
				step = -step; // rows are listed top-down (8..1), so clockwise = next row down
			int r = (int)clipRow_ + step;
			clipRow_ = (uint8_t)constrain(r, 1, 8);
			omxLeds.setDirty();
			omxDisp.setDirty();
			return;
		}

		sendLppTap(enc.dir() > 0 ? kLppUp : kLppDown);
	}

	void MidiMacroM8V2::onDisplayUpdate()
	{
		omxDisp.clearLegends();

		int8_t page = params_.getSelPage();

		if (page == M8V2PAGE_SETTING)
		{
			// Plain text form: L and R are both real values, neither is "OFF".
			omxDisp.setLegend(0, "HAND", rightHand_ ? "R" : "L");
			omxDisp.setLegend(1, "MUTE", muteLatch_ ? "LAT" : "MOM");
			omxDisp.setLegend(2, "SOLO", soloLatch_ ? "LAT" : "MOM");
			omxDisp.setLegend(3, "RING", ringAsCC_ ? "CC" : "NOTE");
			omxDisp.dispGenericMode2(params_.getNumPages(), params_.getSelPage(), params_.getSelParam(), encoderSelect_);
			return;
		}

		if (page == M8V2PAGE_SETTING2)
		{
			{
				static char nrowVal[6];
				if (nrow_ == 4)
					snprintf(nrowVal, sizeof(nrowVal), "A%d", (int)autoK_); // AUTO, showing the locked interval
				else
					snprintf(nrowVal, sizeof(nrowVal), "K%d", (int)(3 + nrow_));
				omxDisp.setLegend(0, "NROW", nrowVal);
			}
			omxDisp.dispGenericMode2(params_.getNumPages(), params_.getSelPage(), params_.getSelParam(), encoderSelect_);
			return;
		}

		switch (view_)
		{
		case VIEW_CLIP:
			snprintf(dispLabel_, sizeof(dispLabel_), "ML CLIP %c%d", clipColMode_ ? 'C' : 'R', (int)clipRow_);
			break;
		case VIEW_CTRL:
			snprintf(dispLabel_, sizeof(dispLabel_), "ML CTRL %c", rightHand_ ? 'R' : 'L');
			break;
		case VIEW_MIX:
			snprintf(dispLabel_, sizeof(dispLabel_), "ML MIX");
			break;
		case VIEW_NOTE:
			snprintf(dispLabel_, sizeof(dispLabel_), trackHeld_ ? "ML NOTE TRK" : "ML NOTE");
			break;
		case VIEW_SEQ:
			snprintf(dispLabel_, sizeof(dispLabel_), recLatched_ ? "ML SEQ REC" : "ML SEQ");
			break;
		case VIEW_PHRASE:
			snprintf(dispLabel_, sizeof(dispLabel_), recLatched_ ? "ML PHRS REC" : "ML PHRS");
			break;
		case VIEW_BEAT:
			snprintf(dispLabel_, sizeof(dispLabel_), "ML BEAT");
			break;
		default:
		{
			const char *modeName = "CLIP";
			if (padMode_ == PAD_MUTE)
				modeName = "MUTE";
			else if (padMode_ == PAD_SOLO)
				modeName = "SOLO";
			snprintf(dispLabel_, sizeof(dispLabel_), "ML SESS %c R%d %s",
					 rightHand_ ? 'R' : 'L', (int)row_, modeName);
			break;
		}
		}

		if (linked_)
			snprintf(dispStatus_, sizeof(dispStatus_), "LPP LINK");
		else // sent/received counters help debug a host that never asks or never answers
			snprintf(dispStatus_, sizeof(dispStatus_), "WAIT S%u R%u", (unsigned)(s_identitySent % 100), (unsigned)(s_inquiryRx % 100));

		// Page 1 of every view: the 8x8 Launchpad grid (design/m8v2/Display/cliplaunch.png). Pads
		// the current view's keys reach draw 3x3, everything else lit on the M8 draws 2x2.
		{
			uint64_t mask = 0;
			auto add = [&mask](uint8_t pad) {
				uint8_t r = pad / 10, c = pad % 10;
				if (r >= 1 && r <= 8 && c >= 1 && c <= 8)
					mask |= (uint64_t)1 << ((r - 1) * 8 + (c - 1));
			};
			for (uint8_t k = 1; k < kNumKeys; k++)
			{
				switch (view_)
				{
				case VIEW_SESSION: if (k >= 11 && k <= 18) add((uint8_t)(80 + (k - 10))); break;
				case VIEW_CLIP: if (k >= 11 && k <= 18) add(clipPadForKey(k)); break;
				case VIEW_NOTE: if (k >= 11) add(notesPadForKey(k)); break;
				case VIEW_SEQ:
					if (k >= 11) add(seqSlotNote(k));
					else if (seqHeldStep_ != 0) add(seqTopNotePad(k));
					break;
				case VIEW_PHRASE: add(k <= 10 ? seqNotePadNote(k) : seqPatternNote(k)); break;
				case VIEW_BEAT: if (k >= 3) add(beatPadForKey(k)); break;
				default: break; // MIX, CTRL: nothing in view
				}
			}

			char l1[10], l2[10];
			l2[0] = 0;
			switch (view_)
			{
			case VIEW_SESSION:
				snprintf(l1, sizeof(l1), "SESS %c", rightHand_ ? 'R' : 'L');
				snprintf(l2, sizeof(l2), "%s", padMode_ == PAD_MUTE ? "MUTE" : padMode_ == PAD_SOLO ? "SOLO" : "CLIP");
				break;
			case VIEW_CLIP:
				snprintf(l1, sizeof(l1), "CLIP");
				if (clipMuteChord_) snprintf(l2, sizeof(l2), "MUTE");
				break;
			case VIEW_MIX: snprintf(l1, sizeof(l1), "MIX"); break;
			case VIEW_NOTE:
				snprintf(l1, sizeof(l1), "NOTE");
				if (trackHeld_) snprintf(l2, sizeof(l2), "TRACK");
				break;
			case VIEW_SEQ:
				snprintf(l1, sizeof(l1), ledColor_[kLppRec] != 0 ? "SEQ REC" : "SEQ");
				snprintf(l2, sizeof(l2), "%s", seqMode_ == 0 ? "NOTE" : seqMode_ == 1 ? "VEL" : "OCT");
				break;
			case VIEW_PHRASE:
				snprintf(l1, sizeof(l1), "PHRASE");
				if (recLatched_) snprintf(l2, sizeof(l2), "REC");
				break;
			case VIEW_BEAT: snprintf(l1, sizeof(l1), "BEAT RPT"); break;
			case VIEW_CTRL: snprintf(l1, sizeof(l1), "CTRL %c", rightHand_ ? 'R' : 'L'); break;
			default: snprintf(l1, sizeof(l1), "ML"); break;
			}
			const char *rlabel = nullptr, *rvalue = nullptr;
			bool rsel = false;
			if (view_ == VIEW_CLIP)
			{
				rlabel = "ORIENT";
				rvalue = clipColMode_ ? "COL" : "ROW";
				rsel = (page == M8V2PAGE_MAIN && params_.getSelParam() == 1);
			}
			omxDisp.dispLaunchpadGrid(ledColor_, mask, l1, l2[0] ? l2 : nullptr, linked_ ? "LINK" : "WAIT", rlabel, rvalue, rsel);
			return;
		}

		omxDisp.dispGenericModeLabelDoubleLine(dispLabel_, dispStatus_, params_.getNumPages(), params_.getSelPage());
	}

}
