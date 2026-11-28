#include <iostream>

#include <wx/string.h>

#include "../NumberEntry.h"
#include "../../../test/UnitTestCommon.h"

// What the dialogs' fields keep when something else is typed into them:
// expected values were taken from what each setting can hold.

namespace {

bool rateIs(wxString const& text, bool accepted, int expected = 0)
{
    int rate = -12345;
    bool actual = ReadSampleRate(text, rate);
    if (actual == accepted && (!accepted || rate == expected)) return true;

    std::cout << "[" << text.ToStdString() << " gave "
              << (actual ? "accepted " + std::to_string(rate) : std::string("rejected"))
              << ", expected " << (accepted ? "accepted " + std::to_string(expected) : std::string("rejected")) << "] ";
    return false;
}

bool acceptsRatesAndTheirDefault()
{
    bool result = true;
    result &= rateIs("8000", true, 8000);
    result &= rateIs(" 44100 ", true, 44100);
    result &= rateIs("768000", true, 768000);
    result &= rateIs("-1", true, -1);
    // What the box shows for a device that lists no rates of its own.
    result &= rateIs("None", true, -1);
    result &= rateIs(" None ", true, -1);
    return result;
}

bool rejectsWhatIsNotARate()
{
    bool result = true;
    // A box emptied, or filled with something that isn't a rate at all,
    // leaves the rate the setting had rather than making it 0.
    result &= rateIs("", false);
    result &= rateIs(" ", false);
    result &= rateIs("eight thousand", false);
    result &= rateIs("8000 Hz", false);
    result &= rateIs("0", false);
    result &= rateIs("-2", false);
    // Too low to be a rate: "44" typed for 44.1 kHz isn't saved as 44 Hz.
    result &= rateIs("44", false);
    result &= rateIs("7999", false);
    // Not wrapped around into another number on the way to the setting.
    result &= rateIs("768001", false);
    result &= rateIs("99999999999", false);
    return result;
}

} // namespace

int main()
{
    executeTestCase("acceptsRatesAndTheirDefault", acceptsRatesAndTheirDefault);
    executeTestCase("rejectsWhatIsNotARate", rejectsWhatIsNotARate);

    return testResult();
}
