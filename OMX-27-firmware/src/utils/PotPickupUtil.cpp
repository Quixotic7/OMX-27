#include "PotPickupUtil.h"
#include "../hardware/omx_disp.h"

void PotPickupUtil::SetVal(uint8_t newValue, bool midiIn)
{
    value = newValue;
    if(midiIn)
    {
        revertValue = value;
    }
    directionDetermined = false;
    pickedUp = false;
}

void PotPickupUtil::SetValRemap(int setValue, int maxValue)
{
    SetValRemap(setValue, 0, maxValue);
}

void PotPickupUtil::SetValRemap(int setValue, int minValue, int maxValue)
{
    uint8_t newValue = map(setValue, minValue, maxValue, 0, 127);

    if (newValue != value)
    {
        SetVal(newValue, true);
    }
}

void PotPickupUtil::SaveRevertVal()
{
    revertValue = value;
}

void PotPickupUtil::RevertVal()
{
    value = revertValue;
    directionDetermined = false;
    pickedUp = false;
}

void PotPickupUtil::DisplayLabel(const char *label)
{
    if(!hasChanged) return;

    omxDisp.dispPickupBarLabelTimed(label, potValue, value, 0, 127, pickedUp, false);
    hasChanged = false;
}
void PotPickupUtil::DisplayValue(const char *label, int dispValue)
{
    if(!hasChanged) return;

    tempString = String(label) + " " + String(dispValue);
    omxDisp.dispPickupBarLabelTimed(tempString.c_str(), potValue, value, 0, 127, pickedUp, false);
    hasChanged = false;
}
void PotPickupUtil::DisplayMappedValue(int minValue, int maxValue, const char *bankName, const char *paramName)
{
    if(!hasChanged) return;

    int mappedValue = map(value, 0, 127, minValue, maxValue);
    omxDisp.dispPickupBarValueTimed(mappedValue, potValue, value, 0, 127, pickedUp, false, bankName, paramName);
    hasChanged = false;
}

int PotPickupUtil::UpdatePotGetMappedValue(uint8_t prevPot, uint8_t newPot, int minValue, int maxValue)
{
    UpdatePot(prevPot, newPot);
    return map(value, 0, 127, minValue, maxValue);
}

void PotPickupUtil::UpdatePot(uint8_t prevPot, uint8_t newPot)
{
    if (!directionDetermined)
    {
        directionCW = prevPot < value;
        pickedUp = prevPot == value;
        directionDetermined = true;
    }

    if (!pickedUp)
    {
        if (directionCW)
        {
            pickedUp = newPot >= value;
        }
        else
        {
            pickedUp = newPot <= value;
        }
    }

    if (pickedUp)
    {
        value = newPot;
    }

    potValue = newPot;

    hasChanged = newPot != prevPot;
}