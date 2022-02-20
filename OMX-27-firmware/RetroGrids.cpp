#include "grids.h"
#include "MM.h"
#include <Arduino.h>

using namespace grids;

#define NUM_GRIDS 8
constexpr static uint8_t kMidiChannel = 1;

static uint32_t random_value(std::uint32_t init = 0) {
  static std::uint32_t val = 0x12345;
  if (init) {
    val = init;
    return 0;
  }
  val = val * 214013 + 2531011;
  return val;
}

enum Resolutions
{
  HALF = 0,
  NORMAL,
  DOUBLE,
  FOUR,
  COUNT
};


struct GridPatterns {
	uint8_t density;
	uint8_t x;
	uint8_t y;
};


class GridsWrapper {
	public:
		uint8_t chaos;
		uint8_t accent;

		uint8_t grids_notes[4] = { 36, 40, 42, 46 };
		static const uint8_t num_notes = sizeof(grids_notes);
		int playingPattern = 0;
		
		GridPatterns gridSaves[8][4] =  {
			{{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128}},
			{{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128}},
			{{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128}},
			{{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128}},
			{{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128}},
			{{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128}},
			{{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128}},
			{{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128},{.density = 0, .x = 128, .y = 128}}
			};
		
		GridsWrapper()
		{
			tickCount_ = 0;
			for (auto i = 0; i < num_notes; i ++){
				channelTriggered_[i] = false;
				density_[i] = i == 0 ? 128 : 64;
				perturbations_[i] = 0;
				x_[i] = 128;
				y_[i] = 128;
			}

			accent = 128;
			chaos = 0;
			divider_ = 0;
			multiplier_ = 1;
			running_ = false;
		}

		void start()
		{
			tickCount_ = 0;
			running_ = true;
		}

		void stop()
		{
			running_ = false;
		}

		void proceed()
		{
			running_ = true;
		}
  
		void grids_tick()
		{
			if (!running_) return;

			uint32_t ticksPerClock = 3 << divider_;
			bool trigger = ((tickCount_ % ticksPerClock) == 0);

			if (trigger) {
				const auto step = (tickCount_ / ticksPerClock * multiplier_) % grids::kStepsPerPattern;
				channel_.setStep(step);

				for (auto channel = 0; channel < num_notes; channel++){
					if (step == 0){
					  std::uint32_t r = random_value();
					  perturbations_[channel] = ((r & 0xFF) * (chaos >> 2)) >> 8;
					}

					const uint8_t threshold = ~density_[channel];
					auto level = channel_.level(channel, x_[channel], y_[channel]);
					if (level < 255 - perturbations_[channel]){
					  level += perturbations_[channel];
					}

					if (level > threshold){
						uint8_t targetLevel =  uint8_t(127.f * float(level - threshold) / float(256 - threshold));
						uint8_t noteLevel = grids::U8Mix(127, targetLevel, accent);
						MM::sendNoteOn(grids_notes[channel], noteLevel, kMidiChannel);
						channelTriggered_[channel] = true;
					}    
				}
			} else {
				for (auto channel = 0; channel < num_notes; channel++){
					if (channelTriggered_[channel]){
						MM::sendNoteOff(grids_notes[channel], 0, kMidiChannel);
						channelTriggered_[channel] = false;          
					}
				}
			}
			tickCount_++;
		}

		void setDensity(uint8_t channel, uint8_t density)
		{
			density_[channel] = density;
		}

		uint8_t getDensity(uint8_t channel)
		{
			return density_[channel];
		}

		void setX(uint8_t channel, uint8_t x)
		{
			x_[channel] = x;
// 			Serial.print("setX:");
// 			Serial.print(channel);
// 			Serial.print(":");
// 			Serial.println(x);
		}

		uint8_t getX(uint8_t channel)
		{
			return x_[channel];
		}

		void setY(uint8_t channel,uint8_t y)
		{
			y_[channel] = y;
		}

		uint8_t getY(uint8_t channel)
		{
			return y_[channel];
		}

		void setChaos(uint8_t c)
		{
			chaos = c;
		}

		uint8_t getChaos()
		{
			return chaos;
		}

		void setResolution(uint8_t r)
		{
			divider_ = 0;
			if (r == 0) { 
				multiplier_ = 1;
				divider_ = 1;
			} else if (r == 1){
				multiplier_ = 1;
			} else if (r == 2){
				multiplier_ = 2;
			//     } else if (r == 3){
			//     	multiplier_ = 4;
			}
		}

		void setAccent(uint8_t a)
		{
			accent = a;
		}
		uint8_t getAccent()
		{
			return accent;
		}

	private:
		Channel channel_;
		uint32_t divider_;
		uint8_t multiplier_;
		uint32_t tickCount_;
		uint8_t density_[num_notes];
		uint8_t perturbations_[num_notes];
		uint8_t x_[num_notes];
		uint8_t y_[num_notes];
		bool channelTriggered_[num_notes];
		bool running_;  
};



/*
bool RK002_onClock()
{
  grids_wrapper.grids_tick();
  return true;
}

bool RK002_onStart()
{
  grids_wrapper.start();
  return true;  
}

bool RK002_onStop()
{
  grids_wrapper.stop();
  return true;  
}

bool RK002_onContinue()
{
  grids_wrapper.proceed();
  return true;  
}

bool RK002_onNoteOn(byte channel, byte key, byte velocity)
{
  return channel != 9;
}

boolean RK002_onControlChange(byte channel, byte nr, byte value)
{
  if (channel == 0)
  {
    switch(nr)
    {
      case 0x50:
        grids_wrapper.setX(uint8_t(value) << 1);
        break;
      case 0x51:
        grids_wrapper.setY(uint8_t(value) << 1);
        break;
      case 0x52:
        grids_wrapper.setDensity(0, uint8_t(value) << 1);
        break;
      case 0x53:
        grids_wrapper.setDensity(1, uint8_t(value) << 1);
        break;
      case 0x54:
        grids_wrapper.setDensity(2, uint8_t(value) << 1);
        break;
      case 0x55:
        grids_wrapper.setChaos(uint8_t(value) << 1);
        break;
      case 0x56:
        grids_wrapper.setAccent(uint8_t(value) << 1);
        break;
      case 0x57:
        grids_wrapper.setResolution(Resolutions((float(value) / 128.f) * Resolutions::COUNT));
        break;
    }
    return false;
  }
  return true;
}
*/