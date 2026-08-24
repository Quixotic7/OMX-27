#include "form_machine_null.h"

FormMachineNull::FormMachineNull()
{
}
FormMachineNull::~FormMachineNull()
{
}

FormMachineInterface * FormMachineNull::getClone()
{
    auto clone = new FormMachineNull();
    return clone;
}

void FormMachineNull::onEnabled()
{
}
void FormMachineNull::onDisabled()
{
}

void FormMachineNull::onEncoderChangedSelectParam(Encoder::Update enc)
{
}
void FormMachineNull::onEncoderChangedEditParam(Encoder::Update enc)
{
}

void FormMachineNull::onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta)
{
}
void FormMachineNull::onClockTick()
{
}
void FormMachineNull::loopUpdate()
{
}
bool FormMachineNull::updateLEDs()
{
    return true;
}
void FormMachineNull::onEncoderButtonDown()
{
}
bool FormMachineNull::onKeyUpdate(OMXKeypadEvent e)
{
    return true;
}
bool FormMachineNull::onKeyHeldUpdate(OMXKeypadEvent e)
{
    return true;
}
void FormMachineNull::onDisplayUpdate()
{
}

void FormMachineNull::onAUXFunc(uint8_t funcKey) {}