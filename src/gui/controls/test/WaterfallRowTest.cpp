#include <vector>

#include "../waterfall_row.h"
#include "../../../test/UnitTestCommon.h"

// The waterfall blanks rows during half-duplex TX by filling them with the
// display floor (MIN_MAG_DB). Anything else, however weak, is a real
// spectrum and has to be drawn (PR #1618).

namespace {

constexpr float FLOOR_DB = -40.0f; // MIN_MAG_DB
constexpr int NBINS = 512;         // MODEM_STATS_NSPEC
constexpr int FROM_BIN = 25;       // 200 Hz at a 4 kHz plot
constexpr int TO_BIN = 358;        // 2800 Hz

bool blankedIs(std::vector<float> const& row, bool expected, const char* what)
{
    bool actual = IsBlankedWaterfallRow(row.data(), FROM_BIN, TO_BIN, FLOOR_DB);
    if (actual != expected)
    {
        std::cout << "[" << what << ": got " << (actual ? "blanked" : "drawn")
                  << ", expected " << (expected ? "blanked" : "drawn") << "] ";
        return false;
    }
    return true;
}

bool rowAtTheFloorIsBlanked()
{
    return blankedIs(std::vector<float>(NBINS, FLOOR_DB), true, "every bin at the floor");
}

// What #1618 fixed: with the radio's audio turned well down, a real row can
// peak below the floor. Testing only the peak called that blank.
bool weakSignalBelowTheFloorIsDrawn()
{
    std::vector<float> row(NBINS);
    for (int i = 0; i < NBINS; i++)
    {
        row[i] = -60.0f + (float)(i % 13);   // -60 to -48 dB
    }
    return blankedIs(row, false, "row peaking 8 dB below the floor");
}

bool oneBinOffTheFloorIsDrawn()
{
    bool result = true;

    std::vector<float> below(NBINS, FLOOR_DB);
    below[200] = FLOOR_DB - 0.5f;
    result &= blankedIs(below, false, "one bin just below the floor");

    std::vector<float> above(NBINS, FLOOR_DB);
    above[TO_BIN - 1] = FLOOR_DB + 0.5f;
    result &= blankedIs(above, false, "last bin just above the floor");

    std::vector<float> first(NBINS, FLOOR_DB);
    first[FROM_BIN] = -10.0f;
    result &= blankedIs(first, false, "first bin with a signal");
    return result;
}

bool ordinarySignalIsDrawn()
{
    std::vector<float> row(NBINS, -30.0f);
    row[150] = -5.0f;
    return blankedIs(row, false, "ordinary received spectrum");
}

// Only 200-2800 Hz is looked at, as for the waterfall's auto-ranging.
bool binsOutsideTheRangeAreIgnored()
{
    std::vector<float> row(NBINS, FLOOR_DB);
    for (int i = 0; i < FROM_BIN; i++) row[i] = -10.0f;
    for (int i = TO_BIN; i < NBINS; i++) row[i] = -10.0f;
    return blankedIs(row, true, "signal only outside 200-2800 Hz");
}

}

int main()
{
    executeTestCase("rowAtTheFloorIsBlanked", rowAtTheFloorIsBlanked);
    executeTestCase("weakSignalBelowTheFloorIsDrawn", weakSignalBelowTheFloorIsDrawn);
    executeTestCase("oneBinOffTheFloorIsDrawn", oneBinOffTheFloorIsDrawn);
    executeTestCase("ordinarySignalIsDrawn", ordinarySignalIsDrawn);
    executeTestCase("binsOutsideTheRangeAreIgnored", binsOutsideTheRangeAreIgnored);
    return testResult();
}
