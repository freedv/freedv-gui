#include <cinttypes>
#include <vector>

#include <wx/init.h>
#include <wx/numformatter.h>
#include <wx/string.h>

#include "../ReportingConfiguration.h"
#include "../../test/UnitTestCommon.h"

// The frequency list is stored on disk as MHz in US format with six decimal
// places (i.e. down to the Hz) but is presented to the user as either MHz
// (four decimal places) or kHz (one decimal place) in their own locale.

namespace {

using StringList = std::vector<wxString>;

bool listIs(StringList const& actual, StringList const& expected)
{
    if (actual != expected)
    {
        std::cout << "[got {";
        for (auto& val : actual) std::cout << " " << val.ToStdString();
        std::cout << " }, expected {";
        for (auto& val : expected) std::cout << " " << val.ToStdString();
        std::cout << " }] ";
        return false;
    }
    return true;
}

wxString storedFormat(uint64_t freqHz)
{
    return wxString::Format("%" PRIu64 ".%06" PRIu64, freqHz / 1000000, freqHz % 1000000);
}

bool loadConvertsToMHz()
{
    ReportingConfiguration config;
    config.reportingFrequencyAsKhz = false;
    config.reportingFrequencyList.setWithoutProcessing({ "14.236000", "7.177000", "3.625", "50", "1.997500" });
    return listIs(config.reportingFrequencyList.get(), { "1.9975", "3.6250", "7.1770", "14.2360", "50.0000" });
}

bool loadConvertsToKHz()
{
    ReportingConfiguration config;
    config.reportingFrequencyAsKhz = true;
    config.reportingFrequencyList.setWithoutProcessing({ "14.236000", "7.177000", "3.625", "50", "1.997500" });
    return listIs(config.reportingFrequencyList.get(), { "1997.5", "3625.0", "7177.0", "14236.0", "50000.0" });
}

bool saveConvertsFromMHz()
{
    ReportingConfiguration config;
    config.reportingFrequencyAsKhz = false;
    config.reportingFrequencyList = StringList { "14.2360", "7.1770", "3.625", "50", "1.9975" };
    return listIs(
        config.reportingFrequencyList.getWithoutProcessing(), 
        { "14.236000", "7.177000", "3.625000", "50.000000", "1.997500" });
}

bool saveConvertsFromKHz()
{
    ReportingConfiguration config;
    config.reportingFrequencyAsKhz = true;
    config.reportingFrequencyList = StringList { "14236.0", "7177.0", "3625", "50000", "1997.5" };
    return listIs(
        config.reportingFrequencyList.getWithoutProcessing(), 
        { "14.236000", "7.177000", "3.625000", "50.000000", "1.997500" });
}

// Saves every frequency between 1.8 and 54 MHz (in steps of the smallest unit
// that can be displayed) and verifies that what's stored is that exact frequency.
bool savedFrequencyIsExact(bool asKhz)
{
    const uint64_t MIN_FREQ_HZ = 1800000;
    const uint64_t MAX_FREQ_HZ = 54000000;
    const uint64_t STEP_HZ = 100; // both display formats have 100 Hz resolution

    ReportingConfiguration config;
    config.reportingFrequencyAsKhz = asKhz;

    StringList displayed;
    StringList expected;
    for (uint64_t freqHz = MIN_FREQ_HZ; freqHz <= MAX_FREQ_HZ; freqHz += STEP_HZ)
    {
        // Style_None as thousands separators aren't relevant to this test.
        displayed.push_back(
            asKhz ?
            wxNumberFormatter::ToString(freqHz / 1000.0, 1, wxNumberFormatter::Style_None) :
            wxNumberFormatter::ToString(freqHz / 1000000.0, 4, wxNumberFormatter::Style_None));
        expected.push_back(storedFormat(freqHz));
    }

    config.reportingFrequencyList = displayed;
    StringList stored = config.reportingFrequencyList.getWithoutProcessing();
    if (stored.size() != expected.size())
    {
        std::cout << "[got " << stored.size() << " entries, expected " << expected.size() << "] ";
        return false;
    }

    size_t numWrong = 0;
    for (size_t index = 0; index < expected.size(); index++)
    {
        if (stored[index] != expected[index])
        {
            if (numWrong == 0)
            {
                std::cout << "[e.g. " << displayed[index].ToStdString() << " stored as " 
                          << stored[index].ToStdString() << ", expected " << expected[index].ToStdString() << "] ";
            }
            numWrong++;
        }
    }

    if (numWrong > 0)
    {
        std::cout << "[" << numWrong << " of " << expected.size() << " frequencies stored incorrectly] ";
    }
    return numWrong == 0;
}

bool savedThenLoadedIsUnchanged(bool asKhz)
{
    ReportingConfiguration config;
    config.reportingFrequencyAsKhz = asKhz;

    StringList displayed = 
        asKhz ? 
        StringList { "2002.0", "7177.5", "14236.0", "50313.0" } :
        StringList { "2.0020", "7.1775", "14.2360", "50.3130" };
    config.reportingFrequencyList = displayed;
    return listIs(config.reportingFrequencyList.get(), displayed);
}

// A frequency added to the list goes before the first one above it.
bool sortedFrequencyIndex()
{
    StringList list { "3.6250", "7.1770", "14.2360" };
    return ReportingConfiguration::SortedFrequencyIndex(list, 1.9975) == 0 &&
           ReportingConfiguration::SortedFrequencyIndex(list, 10.136) == 2 &&
           ReportingConfiguration::SortedFrequencyIndex(list, 28.33) == 3 &&
           ReportingConfiguration::SortedFrequencyIndex(StringList {}, 7.0) == 0;
}

// Entries that aren't a positive frequency (a cleared field, a stray zero,
// something negative or blank) store as zero rather than as a garbage
// frequency.
bool nonPositiveEntriesStoreAsZero()
{
    ReportingConfiguration config;
    config.reportingFrequencyAsKhz = false;
    config.reportingFrequencyList = StringList { "0", "-1.5", "", "0.0000" };
    return listIs(config.reportingFrequencyList.getWithoutProcessing(),
                  { "0.000000", "0.000000", "0.000000", "0.000000" });
}

}

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk())
    {
        std::cout << "Could not initialize wxWidgets" << std::endl;
        return -1;
    }

    executeTestCase("loadConvertsToMHz", loadConvertsToMHz);
    executeTestCase("loadConvertsToKHz", loadConvertsToKHz);
    executeTestCase("saveConvertsFromMHz", saveConvertsFromMHz);
    executeTestCase("saveConvertsFromKHz", saveConvertsFromKHz);
    executeTestCase("savedFrequencyIsExact (MHz)", []() { return savedFrequencyIsExact(false); });
    executeTestCase("savedFrequencyIsExact (kHz)", []() { return savedFrequencyIsExact(true); });
    executeTestCase("savedThenLoadedIsUnchanged (MHz)", []() { return savedThenLoadedIsUnchanged(false); });
    executeTestCase("savedThenLoadedIsUnchanged (kHz)", []() { return savedThenLoadedIsUnchanged(true); });
    executeTestCase("sortedFrequencyIndex", sortedFrequencyIndex);
    executeTestCase("nonPositiveEntriesStoreAsZero", nonPositiveEntriesStoreAsZero);
    return testResult();
}
