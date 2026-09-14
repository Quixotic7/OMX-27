#pragma once
#include "midimacro_interface.h"

// ML - Launchpad Pro MK3 emulation for the Dirtywave M8 (macro slot 4, "ML").
// See design/m8v2/ML-LAYOUT-V3.md (authoritative layout spec), design/m8v2/M8V2-PLAN.md
// and design/m8v2/M8-MACRO-USER-GUIDE.md.

namespace midimacro
{
	// Universal Device Inquiry hook, called from sysex.cpp BEFORE the F0 7D 00 00 gate.
	// Replies with the Launchpad Pro MK3 identity whenever the ML macro slot is selected,
	// regardless of whether the macro is entered or which OMX mode is running.
	void onDeviceInquiry(const uint8_t *data, unsigned length);


	class MidiMacroM8V2 : public MidiMacroInterface
	{
	public:
		// Order matters: the AUX layer maps keys 15-22 to these in order (spec v3 sections 1 / 9c).
		enum View : uint8_t
		{
			// Order = AUX layer keys 15..22.
			VIEW_SESSION,
			VIEW_CLIP,
			VIEW_MIX,
			VIEW_NOTE,
			VIEW_SEQ,
			VIEW_PHRASE,
			VIEW_BEAT,
			VIEW_CTRL,
			VIEW_COUNT
		};

		enum PadMode : uint8_t
		{
			PAD_CLIP,
			PAD_MUTE,
			PAD_SOLO
		};

		MidiMacroM8V2();
		~MidiMacroM8V2() {}

		bool consumesPots() override { return true; }
		bool consumesDisplay() override { return true; }

		String getName() override;

		void loopUpdate() override;

		void onDisplayUpdate() override;

		void onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta) override;
		void onKeyUpdate(OMXKeypadEvent e) override;
		void drawLEDs() override;

		void inMidiControlChange(byte channel, byte control, byte value) override;
		bool inMidiNoteOn(byte channel, byte note, byte velocity) override;
		bool inMidiNoteOff(byte channel, byte note, byte velocity) override;

		// Bit-packed persistent settings, EEPROM header offset 55 (spec v3 section 7).
		// bit0 HAND (0 L, 1 R)  bit1 MUTE latch  bit2 SOLO latch
		// bit3 RING (0 CC, 1 note)  bits4-5 NROW (0 = K4, 1 = K3)
		uint8_t getSettingsByte() const;
		void setSettingsByte(uint8_t v); // 0xFF = never saved, keep defaults

	protected:
		void onEnabled() override;
		void onDisabled() override;

		void onEncoderChangedEditParam(Encoder::Update enc) override;

	private:
		// Highest LPP programmer note we cache (grid tops out at 98, ring at 108 but
		// the track row 101-108 is inside this range too).
		static const uint8_t kNumLppNotes = 110;
		static const uint8_t kNumKeys = 27;

		// Beat Repeat grid rows (spec section 5c). Launchpad rows count 1 = bottom, and the
		// M8 docs say "line 8" = tracks, "lines 6/7" = range. If the docs actually count from
		// the top these become 1, 3, 2 - the track row lights BLUE on the M8, so that settles
		// it on hardware. One place to flip.
		static const uint8_t kBeatTrackRow = 1; // M8 doc: track toggles on Launchpad row 1 (bottom)
		static const uint8_t kBeatRangeRowA = 3; // first loop-selection row (row 3)
		static const uint8_t kBeatRangeRowB = 2; // second loop-selection row (row 2)

		void sendIdentity();
		void releaseAllKeys();
		void setPadMode(PadMode newMode);

		// Sends an LPP button. Grid pads (11-88, cols 1-8) are always Note On/Off; the ring
		// (top/bottom rows, side columns, 101-108, 1-8) goes out as CC when ringAsCC_ is set,
		// which is what a real Launchpad Pro MK3 does in programmer mode.
		void sendLpp(uint8_t n, bool on);
		void sendLppTap(uint8_t n) { sendLpp(n, true); sendLpp(n, false); }
		// Tap a track button (101-108) to toggle its mute (solo=false) / solo (solo=true) and
		// flip the OMX-authoritative bit to match. The Mute/Solo modifier must already be held.
		void tapTrackToggle(uint8_t trackIdx, bool solo);
		static bool isRingButton(uint8_t n);
		void releaseLatchedTracks();
		void releaseMomentaryTracks(); // undo (retap) any momentary mute/solo track still mid-press
		void releaseControlNotes();
		// Drops every held/latched thing the macro owns (called on view change and exit).
		void releaseAllHeld();
		void lockSeqStep(uint8_t stepKey); // Seq v2: lock a step for hands-free editing (auto-arms Record)
		void unlockSeqStep();			   // release the locked step + disarm auto-Record
		void switchView(View v, bool sendButton = true); // sendButton=false when following the M8

		// Session nav cluster -> M8 Control Map note (0-7) on the macro channel, or -1 if this
		// key is not part of the cluster. Depends on the HAND setting (spec section 2).
		int8_t controlMapNote(uint8_t key) const;
		// Control view (spec section 9b): the M8's own key cluster, two-handed. Returns the
		// Control Map note (0-7) for an OMX key, or -1 when the key is unassigned.
		int8_t ctrlViewNote(uint8_t key) const;
		void sendControlMapTap(uint8_t cm); // note on + off, no delay

		void doWaveformMacro();  // Control Map Up+Down+Left+Right together (classic macro)
		void doGotoMixerMacro(); // Shift + Up, Left x4, Down (classic macro, same delays)
		void doStoreSnapshot();  // Shift + SShot
		void doRecallSnapshot(); // SShot
		void doMixAllTracks(uint8_t modifier); // unmute-all / unsolo-all

		uint8_t notesPadForKey(uint8_t key) const;  // NOTE whites -> 16 consecutive scale steps
		uint8_t detectAutoK() const;                // infer NOTE row interval from the M8 root LEDs (0 = unsure)
		static uint8_t beatPadForKey(uint8_t key);  // BEAT keys 3-26 -> track / range rows
		static uint8_t seqNotePadNote(uint8_t key); // SEQ/PHRASE black keys 3-10 -> pads 11-18
		static uint8_t seqSlotNote(uint8_t key);    // SEQ whites -> left 4x4 note slots
		uint8_t seqTopNotePad(uint8_t key) const;    // SEQ v2 top row 1-10 -> 10 consecutive keyboard notes
		static uint8_t seqPatternNote(uint8_t key); // PHRASE whites -> right 4x4 phrase slots
		uint8_t sessionPadForKey(uint8_t key) const; // SESSION 11-19 pads / track buttons
		uint8_t clipPadForKey(uint8_t key) const;    // CLIP 11-18 -> pads of clipRow_ (row) / clipRow_ as column when clipColMode_
		bool isClipView() const { return view_ == VIEW_CLIP; }

		// The M8 lights the Launchpad view button (Session 93 / Note 94 / Seq 97) of the view it
		// is showing. When one lights up that our current view does not imply (e.g. the M8
		// jumped to its sequencer after a pad double-tap), follow it.
		void followM8View(uint8_t button, uint8_t prevColor, uint8_t newColor);
		uint8_t impliedViewButton() const;
		static uint8_t clipScenePadForKey(uint8_t key); // CLIP 19-26 -> scene buttons 19..89
		void changePotBank(bool next); // AUX+13/14, same behaviour as the normal OMX AUX layer

		// Draws one grid/scene key from the palette cache: static/flash/pulse per ledMode_,
		// offColor when the cached index is 0 (or the note is out of range).
		void drawPaletteKey(uint8_t key, uint8_t note, uint32_t offColor);
		void drawAuxKey();

		View view_ = VIEW_SESSION;
		PadMode padMode_ = PAD_CLIP;
		bool muteLatch_ = true;   // default LATCH (a tap toggles and stays); Moment is opt-in
		bool soloLatch_ = true;   // default LATCH; Moment is opt-in
		bool ringAsCC_ = true;        // ring buttons as CC (LPP MK3 programmer mode) or notes
		bool rightHand_ = false;      // HAND setting: false = L, true = R
		uint8_t nrow_ = 0;            // NROW: 0..3 = K3..K6, 4 = AUTO (detect from M8 root LEDs)
		uint8_t autoK_ = 5;           // AUTO mode: last detected row interval (fallback 5 = chromatic)
		uint32_t lastAutoNrowMs_ = 0; // throttle for autodetect
		uint8_t latchedTracks_ = 0;   // bit i = track 101+i toggled on by the latch (LED memory)
		uint8_t momentaryTracks_ = 0; // bit i = track 101+i toggled by a still-held momentary press
		bool trackHeld_ = false;      // Note view: key 3 held -> white keys 11-18 are track buttons
		bool recLatched_ = false;     // AUX+8 latches LPP Record held (for Rec+keypad / Rec+Play combos)
		bool shiftLatched_ = false;   // AUX+3 latches LPP Shift for the duration of the AUX hold
		uint8_t row_ = 8; // 1..8, which grid row Session keys 11-18 show
		uint8_t clipRow_ = 8; // 1..8, Clip Launch selected row / column (not persisted)
		bool clipColMode_ = false; // Clip Launch orientation: false = rows, true = columns (page-1 param)
		uint8_t seqMode_ = 0;        // Seq v2 edit mode: 0 NOTE, 1 VEL, 2 OCT
		uint8_t seqLockedStep_ = 0;  // Seq v2: white key (11-26) LOCKED as the step being edited
									 // (its pad stays held on the M8); 0 = none. Unlock via AUX or re-tap.
		bool clipMuteChord_ = false; // CLIP: keys 1+2 held together -> Launchpad Mute held, keys 3-10 = track buttons

		bool linked_ = false;
		uint32_t lastIdentityMs_ = 0;
		bool auxHeld_ = false;
		bool auxConsumed_ = false; // AUX became a hold, or a key was used while AUX was held; a
								   // quick standalone AUX tap (auxConsumed_ still false on release)
								   // is what unlocks a locked Seq step
		bool seqAutoRec_ = false;  // Seq lock auto-armed Record; disarm it again on unlock

		// Last seen blink phases, so loopUpdate() only calls omxLeds.setDirty() once per
		// actual blink transition (and only when something cached is actually flash/pulse).
		bool lastBlinkState_ = false;
		bool lastSlowBlinkState_ = false;

		uint8_t ledColor_[kNumLppNotes]; // LPP palette index by LPP note
		uint8_t ledMode_[kNumLppNotes];	 // 0 static, 1 flash, 2 pulse
		// OMX-authoritative track state for the Mix/Session strip and the "all" keys. The M8 does
		// not reliably stream the Session track-button (101-108) mute/solo LEDs back over TRS, so
		// instead we track state from the OMX's own toggles (bit i = track i+1). Can drift only if
		// the user mutes/solos on the M8 hardware directly, which this controller setup doesn't do.
		uint8_t omxMuted_ = 0;
		uint8_t omxSoloed_ = 0;
		uint8_t keyNoteSent_[kNumKeys];	 // LPP note currently held per OMX key, 0 = none
		int8_t ctrlSent_[kNumKeys];		 // Control Map note sent per OMX key (M-CH), -1 = none

		// Persistent display buffers - never pass String(...).c_str() of a temporary.
		char dispLabel_[24];
		char dispStatus_[16];
	};

	// The one shared ML macro instance (defined in utils/aux_macro_manager.cpp, which owns
	// all the static macro objects). Lets the .ino read/write its saved settings byte.
	MidiMacroM8V2 &m8lpMacroInstance();

}
