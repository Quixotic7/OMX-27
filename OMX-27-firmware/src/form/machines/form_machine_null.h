#pragma once
#include "form_machine_interface.h"

// Very powerful step sequencer
class FormMachineNull : public FormMachineInterface
{
public:
    FormMachineNull();
    ~FormMachineNull();

    FormMachineType getType() { return FORMMACH_NULL; }
	FormMachineInterface *getClone() override;

    // Standard Updates
    void onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta) override;
    void onClockTick() override;
    void loopUpdate() override;
    bool updateLEDs() override;
    void onEncoderButtonDown() override;
    bool onKeyUpdate(OMXKeypadEvent e) override;
    bool onKeyHeldUpdate(OMXKeypadEvent e) override;
    void onDisplayUpdate() override;
    void onAUXFunc(uint8_t funcKey) override;

private:
    void onEnabled();
    void onDisabled();

    void onEncoderChangedSelectParam(Encoder::Update enc);
    void onEncoderChangedEditParam(Encoder::Update enc);
};