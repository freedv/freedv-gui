#include <vector>

#include "../waterfall_scale.h"
#include "../../../test/UnitTestCommon.h"

// The waterfall derives its bin range and row width from the sample rate it
// is handed. One row of spectrum is a fixed number of bins wide, so an index
// or width beyond that reads past the end of the row -- found by fuzzing
// setFs(): a 2 kHz modem rate asked for bin 716 of a 512 bin row.

namespace {

constexpr int NBINS = 512;              // MODEM_STATS_NSPEC
constexpr int DISPLAY_MAX_F_HZ = 3000;  // MAX_F_HZ
constexpr float FLOOR_DB = -40.0f;      // MIN_MAG_DB

bool check(int maxFHertz, int firstBin, int lastBin, int rowWidthPx, const char* what)
{
    WaterfallScale scale = waterfallScaleFor(maxFHertz, NBINS, DISPLAY_MAX_F_HZ);
    if (scale.firstBin != firstBin || scale.lastBin != lastBin || scale.rowWidthPx != rowWidthPx)
    {
        std::cout << "[" << what << ": maxFHertz=" << maxFHertz
                  << " gave bins [" << scale.firstBin << "," << scale.lastBin
                  << ") width " << scale.rowWidthPx
                  << ", expected [" << firstBin << "," << lastBin << ") width " << rowWidthPx
                  << "] ";
        return false;
    }
    return true;
}

// The rates the app uses: MODEM_STATS_MAX_F_HZ, and half of the 8 kHz and
// 16 kHz modem sample rates passed to setFs().
bool ordinaryRatesMatchTheOldArithmetic()
{
    bool result = true;
    result &= check(4000, 25, 358, 384, "4 kHz (MODEM_STATS_MAX_F_HZ)");
    result &= check(8000, 12, 179, 192, "8 kHz");
    result &= check(16000, 6, 89, 96, "16 kHz");
    return result;
}

// The fuzzing find: at 2 kHz the band ran to bin 716 and the row to 768 px,
// both past the end of the row.
bool smallRatesAreClampedToTheRow()
{
    bool result = true;
    result &= check(2000, 51, 512, 512, "2 kHz");
    result &= check(5000, 20, 286, 307, "5 kHz, band still inside the row");
    result &= check(5600, 18, 256, 274, "5.6 kHz");
    result &= check(200, 512, 512, 512, "200 Hz");
    return result;
}

// Zero and negative rates used to overflow int on the way to a float to int
// conversion; now they leave an empty band and a full width row, both in
// bounds.
bool degenerateRatesStayInBounds()
{
    bool result = true;
    result &= check(0, 512, 512, 512, "0 Hz");
    result &= check(-1, 512, 512, 512, "-1 Hz");
    result &= check(-4000, 512, 512, 512, "-4 kHz");
    result &= check(2147483647, 0, 0, 1, "INT_MAX Hz");
    return result;
}

// What the drawing code relies on, for every rate it might be handed.
template <int N>
bool invariantsHold(const int (&rates)[N])
{
    bool result = true;

    for (int rate : rates)
    {
        WaterfallScale scale = waterfallScaleFor(rate, NBINS, DISPLAY_MAX_F_HZ);

        bool bandInsideRow = (scale.firstBin >= 0) && (scale.firstBin <= scale.lastBin)
                             && (scale.lastBin <= NBINS);
        bool widthInsideRow = (scale.rowWidthPx >= 1) && (scale.rowWidthPx <= NBINS);

        if (!bandInsideRow || !widthInsideRow)
        {
            std::cout << "[maxFHertz=" << rate << " bins [" << scale.firstBin << ","
                      << scale.lastBin << ") width " << scale.rowWidthPx << "] ";
            result = false;
            continue;
        }

        // Read the row the way plotPixelData() does: it may not touch a bin
        // outside the row.
        std::vector<float> row(NBINS, FLOOR_DB);
        float maxMag = FLOOR_DB;
        for (int i = scale.firstBin; i < scale.lastBin; i++)
        {
            if (row[i] > maxMag) maxMag = row[i];
        }

        if (maxMag != FLOOR_DB)
        {
            std::cout << "[maxFHertz=" << rate << " row read wrong] ";
            result = false;
        }
    }

    return result;
}

bool sweepKeepsEveryIndexInsideTheRow()
{
    const int rates[] = {
        -2147483647, -100000, -4000, -1, 0, 1, 2, 50, 100, 200, 400, 500, 600,
        1000, 1500, 2000, 2500, 2800, 3000, 3500, 4000, 5000, 5600, 6000,
        8000, 12000, 16000, 24000, 44100, 48000, 96000, 192000, 1000000, 2147483647
    };
    return invariantsHold(rates);
}

}

int main()
{
    executeTestCase("ordinaryRatesMatchTheOldArithmetic", ordinaryRatesMatchTheOldArithmetic);
    executeTestCase("smallRatesAreClampedToTheRow", smallRatesAreClampedToTheRow);
    executeTestCase("degenerateRatesStayInBounds", degenerateRatesStayInBounds);
    executeTestCase("sweepKeepsEveryIndexInsideTheRow", sweepKeepsEveryIndexInsideTheRow);
    return testResult();
}
