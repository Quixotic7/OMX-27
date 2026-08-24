#pragma once

#include "form_machine_interface.h"
#include "../../modes/euclidean_sequencer.h"
#include "../../utils/param_manager.h"

// A FORM machine wrapping the existing EuclideanSequencer: generates a Euclidean
// rhythm (events distributed over steps, with rotation) and plays one note per hit.
// Params edited via the encoder; the pattern is shown on the lower 16 keys.
namespace FormEuclid
{
	class FormMachineEuclid : public FormMachineInterface
	{
	public:
		FormMachineEuclid();
		~FormMachineEuclid();

		FormMachineType getType() override { return FORMMACH_EUCLID; }
		FormMachineInterface *getClone() override;

		void onSelected() override;

		bool getMute() override;
		void setMute(bool isMuted) override;
		bool didTriggerThisStep() override;

		bool doesConsumeDisplay() override { return true; }
		bool doesConsumeKeys() override { return true; }
		bool doesConsumeLEDs() override { return false; } // container paints the machine row; we paint the lower 16

		const char *getF3shortcutName() override { return "RATE"; }

		void playBackStateChanged(bool newIsPlaying) override;
		void resetPlayback() override;

		void onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta) override;
		void loopUpdate() override;
		bool updateLEDs() override;
		void onEncoderButtonDown() override;
		bool onKeyUpdate(OMXKeypadEvent e) override;
		bool onKeyHeldUpdate(OMXKeypadEvent e) override;
		void onDisplayUpdate() override;

		int saveToDisk(int startingAddress, Storage *storage) override;
		int loadFromDisk(int startingAddress, Storage *storage) override;

	private:
		euclidean::EuclideanSequencer euclid_;
		ParamManager params_;

		bool didTrigger_ = false;

		// Simple fixed-size pending note-off scheduler (we manage our own offs, like OMNI).
		static const uint8_t kMaxPending = 8;
		MidiNoteGroup pendingOff_[kMaxPending];
		uint32_t pendingOffMicros_[kMaxPending];
		bool pendingActive_[kMaxPending] = {false};

		void schedulePending(MidiNoteGroup noteGroup);
		void servicePending();
		void allNotesOff();

		void onEnabled() override;
		void onDisabled() override;

		void onEncoderChangedSelectParam(Encoder::Update enc) override;
		void onEncoderChangedEditParam(Encoder::Update enc) override;

		// Static glue: EuclideanSequencer note-out callback -> this instance.
		static void onNoteTriggeredForwarder(void *context, uint8_t euclidIndex, MidiNoteGroup note)
		{
			static_cast<FormMachineEuclid *>(context)->onNoteTriggered(euclidIndex, note);
		}
		void onNoteTriggered(uint8_t euclidIndex, MidiNoteGroup note);
	};
}
