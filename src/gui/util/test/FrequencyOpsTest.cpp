#include "../FrequencyOps.h"
#include "../../../test/UnitTestCommon.h"

// By convention, voice on the amateur bands uses lower sideband below 10 MHz
// and upper sideband above, with 60 meters being the one exception.

namespace {

using Mode = HamlibRigController::Mode;

bool modeIs(uint64_t frequency, bool useAnalog, bool inAnalog, Mode expected)
{
    Mode actual = GetModeForFrequency(frequency, useAnalog, inAnalog);
    if (actual != expected)
    {
        std::cout << "[" << frequency << " Hz (" << (useAnalog ? "analog" : "digital") << "): got mode " 
                  << actual << ", expected " << expected << "] ";
        return false;
    }
    return true;
}

// Checks with the radio's analog modes both in use and not. While receiving
// analog, the sideband's analog mode is used either way; the digital (data)
// modes are only for RADE.
bool sidebandIs(uint64_t frequency, bool analog, bool upper)
{
    Mode analogMode = upper ? HamlibRigController::USB : HamlibRigController::LSB;
    Mode digitalMode = upper ? HamlibRigController::DIGU : HamlibRigController::DIGL;

    bool result = true;
    result &= modeIs(frequency, false, analog, analog ? analogMode : digitalMode);
    result &= modeIs(frequency, true, analog, analogMode);
    return result;
}

bool upperSidebandBelow10MHzDigital()
{
    bool result = true;
    result &= sidebandIs(1997000, false, true); // 160m
    result &= sidebandIs(3625000, false, true); // 80m
    result &= sidebandIs(7177000, false, true); // 40m
    result &= sidebandIs(9999999, false, true);
    return result;
}

bool lowerSidebandBelow10MHzAnalog()
{
    bool result = true;
    result &= sidebandIs(1997000, true, false); // 160m
    result &= sidebandIs(3625000, true, false); // 80m
    result &= sidebandIs(7177000, true, false); // 40m
    result &= sidebandIs(9999999, true, false);
    return result;
}

bool upperSidebandFrom10MHzDigital()
{
    bool result = true;
    result &= sidebandIs(10000000, false, true);
    result &= sidebandIs(10136000, false, true);  // 30m
    result &= sidebandIs(14236000, false, true);  // 20m
    result &= sidebandIs(28330000, false, true);  // 10m
    result &= sidebandIs(50313000, false, true);  // 6m
    result &= sidebandIs(144200000, false, true); // 2m
    return result;
}

bool upperSidebandFrom10MHzAnalog()
{
    bool result = true;
    result &= sidebandIs(10000000, true, true);
    result &= sidebandIs(10136000, true, true);  // 30m
    result &= sidebandIs(14236000, true, true);  // 20m
    result &= sidebandIs(28330000, true, true);  // 10m
    result &= sidebandIs(50313000, true, true);  // 6m
    result &= sidebandIs(144200000, true, true); // 2m
    return result;
}

bool upperSidebandOn60MetersAnalog()
{
    bool result = true;
    result &= sidebandIs(5250000, true, true); // bottom edge
    result &= sidebandIs(5403500, true, true);
    result &= sidebandIs(5450000, true, true); // top edge
    return result;
}

bool upperSidebandOn60MetersDigital()
{
    bool result = true;
    result &= sidebandIs(5250000, false, true); // bottom edge
    result &= sidebandIs(5403500, false, true);
    result &= sidebandIs(5450000, false, true); // top edge
    return result;
}

bool lowerSidebandJustOutside60MetersAnalog()
{
    bool result = true;
    result &= sidebandIs(5249999, true, false);
    result &= sidebandIs(5450001, true, false);
    return result;
}

bool upperSidebandJustOutside60MetersDigital()
{
    bool result = true;
    result &= sidebandIs(5249999, false, true);
    result &= sidebandIs(5450001, false, true);
    return result;
}

// Going to a station on the sideband it's said it's on: that sideband,
// whatever the band's usual one, in the same kind of mode (data or not) as
// GetModeForFrequency() would use. Not known: as GetModeForFrequency().
bool followsGivenSideband()
{
    using Sideband = IReporter::Sideband;
    bool result = true;
    for (uint64_t frequency : { 7177000ULL, 5403500ULL, 14236000ULL })
    {
        for (bool useAnalog : { false, true })
        {
            for (bool inAnalog : { false, true })
            {
                bool analogMode = useAnalog || inAnalog;
                result &= (GetModeForSideband(frequency, Sideband::LSB, useAnalog, inAnalog) ==
                           (analogMode ? HamlibRigController::LSB : HamlibRigController::DIGL));
                result &= (GetModeForSideband(frequency, Sideband::USB, useAnalog, inAnalog) ==
                           (analogMode ? HamlibRigController::USB : HamlibRigController::DIGU));
                result &= (GetModeForSideband(frequency, Sideband::UNKNOWN, useAnalog, inAnalog) ==
                           GetModeForFrequency(frequency, useAnalog, inAnalog));
            }
        }
    }
    return result;
}

bool lowerSidebandModes()
{
    return IsLowerSidebandMode(HamlibRigController::LSB) && IsLowerSidebandMode(HamlibRigController::DIGL) &&
           !IsLowerSidebandMode(HamlibRigController::USB) && !IsLowerSidebandMode(HamlibRigController::DIGU) &&
           !IsLowerSidebandMode(HamlibRigController::AM) && !IsLowerSidebandMode(HamlibRigController::UNKNOWN);
}

}

int main()
{
    executeTestCase("lowerSidebandBelow10MHzAnalog", lowerSidebandBelow10MHzAnalog);
    executeTestCase("upperSidebandBelow10MHzDigital", upperSidebandBelow10MHzDigital);
    executeTestCase("upperSidebandFrom10MHzAnalog", upperSidebandFrom10MHzAnalog);
    executeTestCase("upperSidebandFrom10MHzDigital", upperSidebandFrom10MHzDigital);
    executeTestCase("upperSidebandOn60MetersAnalog", upperSidebandOn60MetersAnalog);
    executeTestCase("upperSidebandOn60MetersDigital", upperSidebandOn60MetersDigital);
    executeTestCase("lowerSidebandJustOutside60MetersAnalog", lowerSidebandJustOutside60MetersAnalog);
    executeTestCase("upperSidebandJustOutside60MetersDigital", upperSidebandJustOutside60MetersDigital);
    executeTestCase("followsGivenSideband", followsGivenSideband);
    executeTestCase("lowerSidebandModes", lowerSidebandModes);
    return testResult();
}
