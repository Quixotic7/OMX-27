#pragma once

#include "midifx_interface.h"

namespace midifx
{

	class MidiFXRepeat : public MidiFXInterface
	{
	public:
		MidiFXRepeat();
		~MidiFXRepeat() {}

		int getFXType() override;
		const char *getName() override;
		const char *getDispName() override;

		MidiFXInterface *getClone() override;

		void loopUpdate() override;
		void resync() override;

		void onDisplayUpdate(uint8_t funcKeyMode) override;

		void noteInput(MidiNoteGroup note) override;

		int saveToDisk(int startingAddress, Storage *storage) override;
		int loadFromDisk(int startingAddress, Storage *storage) override;

    protected:
		void onEnabled() override;
		void onDisabled() override;

		void onEncoderChangedEditParam(Encoder::Update enc) override;

	private:
        struct RepeatNote
        {
            // bool inUse = false;
            uint8_t noteNumber;
            uint8_t channel : 4;
            uint8_t velocity : 7;
            // bool sendMidi = false;
            // bool sendCV = false;

            RepeatNote()
            {
                noteNumber = 255;
                velocity = 100;
                channel = 0;
            }

            RepeatNote(int noteNumber, uint8_t velocity, uint8_t channel)
            {
                if (noteNumber < 0 || noteNumber > 127)
                {
                    noteNumber = 255;
                }
                else
                {
                    this->noteNumber = noteNumber;
                    this->velocity = velocity;
                    this->channel = channel;
                }
            }

            RepeatNote(MidiNoteGroup *noteGroup)
            {
                if (noteGroup->noteNumber < 0 || noteGroup->noteNumber > 127)
                {
                    noteNumber = 255;
                    return;
                }
                this->noteNumber = noteGroup->noteNumber;
                this->velocity = noteGroup->velocity;
                this->channel = noteGroup->channel;
            }
        };

        struct RepeatSave
		{
			uint8_t chancePerc : 7;
            uint8_t numOfRepeats : 4; 
            uint8_t mode : 3;
            int8_t rateIndex : 5;
            uint8_t rateHz;	
            uint8_t gate : 8;
            uint8_t velStart : 7;
            uint8_t velEnd : 7;
        };
		uint8_t chancePerc_ = 100;

		uint8_t numOfRepeats_ : 4; // 1 to 16, stored as 0 - 15
        uint8_t mode_ : 3; // Off, 1-Shot - Repeats for numOfRepeats_ restarts on new note on, On - Repeats indefinitely while key is hold, Hold - Endlessly repeats, 
        int8_t rateIndex_ : 5; // max 15 or -1 for hz
        uint8_t rateHz_;	
        uint8_t gate_ : 8; // 0-200
        uint8_t velStart_ : 7; // 0-127
        uint8_t velEnd_ : 7; // 0-127

        // Consts
		static const int queueSize = 16;

        bool seqRunning_;

        // Timing stuff
		float multiplier_ = 1;
		uint8_t stepLength_ = 1; // length of note in arp steps

        Micros nextStepTimeP_ = 32;
		Micros lastStepTimeP_ = 32;
		uint32_t stepMicroDelta_ = 0;

		std::vector<RepeatNote> playedNoteQueue; // Keeps track of which notes are being played
		std::vector<RepeatNote> holdNoteQueue;	  // Holds notes
		std::vector<RepeatNote> sortedNoteQueue;	  // Holds notes
		std::vector<RepeatNote> tempNoteQueue;	  // Notes that are used in arp
		MidiNoteGroup trackingNoteGroups[8];

		bool hasMidiNotes();
        bool insertMidiNoteQueue(MidiNoteGroup *note);
		bool removeMidiNoteQueue(MidiNoteGroup *note);

        void changeRepeatMode(uint8_t newMode);
        void repeatNoteOn(MidiNoteGroup *note);
        void repeatNoteOff(MidiNoteGroup *note);
        void startSeq();
		void stopSeq();
        void resetArpSeq();

        void sortNotes();

        void repeatNoteTrigger();
		void playNote(uint32_t noteOnMicros, int16_t noteNumber, uint8_t velocity, uint8_t channel);
	};
}