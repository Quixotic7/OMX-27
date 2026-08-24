#include "form_machine_euclid.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../consts/colors.h"
#include "../../utils/omx_util.h"
#include "../../hardware/omx_disp.h"
#include "../../hardware/omx_leds.h"

namespace FormEuclid
{
	enum EuclidPage
	{
		EUCLIDPAGE_RHYTHM, // Steps, Hits, Rotation, Note
		EUCLIDPAGE_NOTE,   // Velocity, Channel, Length, Swing
		EUCLIDPAGE_COUNT
	};

	// Lower 16 keys used for the step pattern.
	static const uint8_t kSeqKeyStart = 11;
	static const uint8_t kMaxSteps = 16; // one key per step

	static const uint8_t kEuclidSaveVersion = 1;

	FormMachineEuclid::FormMachineEuclid()
	{
		params_.addPage(4); // RHYTHM
		params_.addPage(4); // NOTE

		euclid_.setNoteOutputFunc(&FormMachineEuclid::onNoteTriggeredForwarder, this, 0);

		euclid_.setSteps(16);
		euclid_.setEvents(4);
		euclid_.setRotation(0);
		euclid_.setNoteNumber(36);
		euclid_.setVelocity(100);
		euclid_.setMidiChannel(1);
		euclid_.setNoteLength(1);
		euclid_.setSwing(0);
		euclid_.setPolyRhythmMode(false);
	}

	FormMachineEuclid::~FormMachineEuclid()
	{
	}

	FormMachineInterface *FormMachineEuclid::getClone()
	{
		auto clone = new FormMachineEuclid();
		clone->euclid_.loadSave(euclid_.getSave());
		return clone;
	}

	void FormMachineEuclid::onSelected()
	{
	}

	bool FormMachineEuclid::getMute()
	{
		return euclid_.getMute();
	}
	void FormMachineEuclid::setMute(bool isMuted)
	{
		euclid_.setMute(isMuted);
		if (isMuted)
			allNotesOff();
	}
	bool FormMachineEuclid::didTriggerThisStep()
	{
		return didTrigger_;
	}

	void FormMachineEuclid::onEnabled()
	{
	}
	void FormMachineEuclid::onDisabled()
	{
		allNotesOff();
	}

	void FormMachineEuclid::playBackStateChanged(bool newIsPlaying)
	{
		if (newIsPlaying)
		{
			euclid_.start();
		}
		else
		{
			euclid_.stop();
			allNotesOff();
		}
	}

	void FormMachineEuclid::resetPlayback()
	{
		allNotesOff();
		euclid_.stop();
		euclid_.start();
	}

	// -----------------------------------------------------------------------
	// Note-off scheduling (we send our own note-offs from stepLength)
	// -----------------------------------------------------------------------
	void FormMachineEuclid::schedulePending(MidiNoteGroup noteGroup)
	{
		uint32_t offMicros = seqConfig.currentFrameMicros + (uint32_t)(noteGroup.stepLength * clockConfig.step_micros);
		for (uint8_t i = 0; i < kMaxPending; i++)
		{
			if (!pendingActive_[i])
			{
				pendingOff_[i] = noteGroup;
				pendingOffMicros_[i] = offMicros;
				pendingActive_[i] = true;
				return;
			}
		}
		// No free slot: send the note off immediately to avoid a stuck note.
		seqNoteOff(noteGroup, 255);
	}

	void FormMachineEuclid::servicePending()
	{
		uint32_t now = seqConfig.currentFrameMicros;
		for (uint8_t i = 0; i < kMaxPending; i++)
		{
			if (pendingActive_[i] && (int32_t)(now - pendingOffMicros_[i]) >= 0)
			{
				seqNoteOff(pendingOff_[i], 255);
				pendingActive_[i] = false;
			}
		}
	}

	void FormMachineEuclid::allNotesOff()
	{
		for (uint8_t i = 0; i < kMaxPending; i++)
		{
			if (pendingActive_[i])
			{
				seqNoteOff(pendingOff_[i], 255);
				pendingActive_[i] = false;
			}
		}
	}

	void FormMachineEuclid::onNoteTriggered(uint8_t euclidIndex, MidiNoteGroup note)
	{
		didTrigger_ = true;
		seqNoteOn(note, 255);
		schedulePending(note);
		omxLeds.setDirty();
	}

	// -----------------------------------------------------------------------
	// Updates
	// -----------------------------------------------------------------------
	void FormMachineEuclid::onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta)
	{
	}

	void FormMachineEuclid::loopUpdate()
	{
		didTrigger_ = false;
		euclid_.clockTick(seqConfig.currentFrameMicros, clockConfig.step_micros);
		if (euclid_.getTriggered())
			didTrigger_ = true;
		servicePending();
	}

	bool FormMachineEuclid::updateLEDs()
	{
		bool *pattern = euclid_.getPattern();
		uint8_t steps = euclid_.getSteps();
		uint8_t pos = euclid_.getSeqPos();

		for (uint8_t i = 0; i < kMaxSteps; i++)
		{
			uint32_t col = LEDOFF;
			if (i < steps)
			{
				col = pattern[i] ? ORANGE : LOWWHITE;
				if (i == pos && euclid_.isRunning())
					col = WHITE;
			}
			strip.setPixelColor(kSeqKeyStart + i, col);
		}
		return true;
	}

	void FormMachineEuclid::onEncoderButtonDown()
	{
	}

	bool FormMachineEuclid::onKeyUpdate(OMXKeypadEvent e)
	{
		uint8_t thisKey = e.key();
		// Tap a step key to rotate the pattern so it starts on that step.
		if (e.down() && thisKey >= kSeqKeyStart && thisKey < kSeqKeyStart + kMaxSteps)
		{
			uint8_t steps = euclid_.getSteps();
			if (steps > 0)
				euclid_.setRotation((thisKey - kSeqKeyStart) % steps);
			omxDisp.setDirty();
			omxLeds.setDirty();
		}
		return true;
	}

	bool FormMachineEuclid::onKeyHeldUpdate(OMXKeypadEvent e)
	{
		return true;
	}

	void FormMachineEuclid::onEncoderChangedSelectParam(Encoder::Update enc)
	{
		params_.changeParam(enc.dir());
		omxDisp.setDirty();
	}

	void FormMachineEuclid::onEncoderChangedEditParam(Encoder::Update enc)
	{
		int8_t amt = enc.accel(5);
		int8_t page = params_.getSelPage();
		int8_t param = params_.getSelParam();

		if (page == EUCLIDPAGE_RHYTHM)
		{
			if (param == 0) // Steps
				euclid_.setSteps(constrain(euclid_.getSteps() + amt, 1, kMaxSteps));
			else if (param == 1) // Hits / events
				euclid_.setEvents(constrain(euclid_.getEvents() + amt, 0, euclid_.getSteps()));
			else if (param == 2) // Rotation
				euclid_.setRotation(constrain(euclid_.getRotation() + amt, 0, euclid_.getSteps() - 1));
			else if (param == 3) // Note
				euclid_.setNoteNumber(constrain(euclid_.getNoteNumber() + amt, 0, 127));
		}
		else if (page == EUCLIDPAGE_NOTE)
		{
			if (param == 0) // Velocity
				euclid_.setVelocity(constrain(euclid_.getVelocity() + amt, 1, 127));
			else if (param == 1) // Channel
				euclid_.setMidiChannel(constrain(euclid_.getMidiChannel() + amt, 1, 16));
			else if (param == 2) // Note length
				euclid_.setNoteLength(constrain(euclid_.getNoteLength() + amt, 0, 15));
			else if (param == 3) // Swing
				euclid_.setSwing(constrain(euclid_.getSwing() + amt, 0, 100));
		}

		omxDisp.setDirty();
		omxLeds.setDirty();
	}

	void FormMachineEuclid::onDisplayUpdate()
	{
		omxDisp.clearLegends();

		int8_t page = params_.getSelPage();
		if (page == EUCLIDPAGE_RHYTHM)
		{
			omxDisp.setLegend(0, "STEP", (int)euclid_.getSteps());
			omxDisp.setLegend(1, "HITS", (int)euclid_.getEvents());
			omxDisp.setLegend(2, "ROT", (int)euclid_.getRotation());
			omxDisp.setLegend(3, "NOTE", MusicScales::getNoteName(euclid_.getNoteNumber(), true));
		}
		else if (page == EUCLIDPAGE_NOTE)
		{
			omxDisp.setLegend(0, "VEL", (int)euclid_.getVelocity());
			omxDisp.setLegend(1, "CH", (int)euclid_.getMidiChannel());
			omxDisp.setLegend(2, "LEN", (int)euclid_.getNoteLength());
			omxDisp.setLegend(3, "SWG", (int)euclid_.getSwing());
		}

		omxDisp.dispGenericMode2(params_.getNumPages(), params_.getSelPage(), params_.getSelParam(), getEncoderSelect());
	}

	// -----------------------------------------------------------------------
	// Save / load (versioned EuclidSave blit)
	// -----------------------------------------------------------------------
	int FormMachineEuclid::saveToDisk(int startingAddress, Storage *storage)
	{
		storage->write(startingAddress, kEuclidSaveVersion);
		startingAddress += 1;

		auto save = euclid_.getSave();
		int saveSize = sizeof(euclidean::EuclidSave);
		byte *p = (byte *)&save;
		for (int j = 0; j < saveSize; j++)
			storage->write(startingAddress + j, *p++);
		startingAddress += saveSize;

		return startingAddress;
	}

	int FormMachineEuclid::loadFromDisk(int startingAddress, Storage *storage)
	{
		uint8_t ver = storage->read(startingAddress);
		startingAddress += 1;

		int saveSize = sizeof(euclidean::EuclidSave);
		if (ver == kEuclidSaveVersion)
		{
			euclidean::EuclidSave save;
			byte *p = (byte *)&save;
			for (int j = 0; j < saveSize; j++)
				*p++ = storage->read(startingAddress + j);
			euclid_.loadSave(save);
		}
		startingAddress += saveSize;

		euclid_.setNoteOutputFunc(&FormMachineEuclid::onNoteTriggeredForwarder, this, 0);
		return startingAddress;
	}
}
