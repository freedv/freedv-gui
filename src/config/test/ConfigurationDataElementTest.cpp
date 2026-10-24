#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <wx/string.h>

#include "../ConfigurationDataElement.h"
#include "../../test/UnitTestCommon.h"

namespace {

bool startsWithDefaultValue()
{
    ConfigurationDataElement<int> element("/Test/Value", 42);
    return 
        std::string(element.getElementName()) == "/Test/Value" &&
        element.get() == 42 &&
        element.getWithoutProcessing() == 42 &&
        element.getDefaultVal() == 42;
}

bool assignmentChangesValueButNotDefault()
{
    ConfigurationDataElement<int> element("/Test/Value", 42);
    element = 7;

    int converted = element;
    return element.get() == 7 && converted == 7 && element.getDefaultVal() == 42;
}

bool changingDefaultDoesNotChangeValue()
{
    // Used during migrations, prior to the value being loaded.
    ConfigurationDataElement<int> element("/Test/Value", 42);
    int newDefault = 99;
    element.setDefaultVal(newDefault);
    return element.getDefaultVal() == 99 && element.get() == 42;
}

bool arrowOperatorAccessesUnderlyingValue()
{
    ConfigurationDataElement<std::string> element("/Test/Value", "abc");
    if (element->size() != 3) return false;

    element->append("def");
    return element.get() == "abcdef";
}

bool saveProcessorAppliesOnAssignment()
{
    ConfigurationDataElement<int> element("/Test/Value", 1);
    element.setSaveProcessor([](int val) { return val * 100; });

    // Default is taken as is, since it's already in stored form.
    if (element.getWithoutProcessing() != 1) return false;

    element = 5;
    return element.getWithoutProcessing() == 500;
}

bool loadProcessorAppliesOnRead()
{
    ConfigurationDataElement<int> element("/Test/Value", 300);
    element.setLoadProcessor([](int val) { return val / 100; });

    int converted = element;
    return element.get() == 3 && converted == 3 && element.getWithoutProcessing() == 300;
}

bool processorsRoundTrip()
{
    // Same arrangement as the codec2 post filter gamma/beta settings.
    ConfigurationDataElement<float> element("/Test/Value", 50);
    element.setSaveProcessor([](float val) { return val * 100; });
    element.setLoadProcessor([](float val) { return val / 100; });

    if (element.get() != 0.5f) return false;

    element = 0.25f;
    return element.getWithoutProcessing() == 25.0f && element.get() == 0.25f;
}

bool withoutProcessingBypassesProcessors()
{
    ConfigurationDataElement<int> element("/Test/Value", 0);
    element.setSaveProcessor([](int val) { return val * 100; });
    element.setLoadProcessor([](int val) { return val / 100; });

    // Used when loading from/saving to the configuration file.
    element.setWithoutProcessing(1200);
    return element.getWithoutProcessing() == 1200 && element.get() == 12;
}

bool worksWithContainers()
{
    ConfigurationDataElement<std::vector<std::string>> element("/Test/Value", {});
    element.setSaveProcessor([](std::vector<std::string> val) {
        for (auto& item : val) item = "stored:" + item;
        return val;
    });

    element = std::vector<std::string> { "a", "b" };
    return element.getWithoutProcessing() == std::vector<std::string> { "stored:a", "stored:b" };
}

// Round trips through both processors for each element type the
// configuration uses (as ReportingConfiguration and friends do).

bool boolProcessorRoundTrip()
{
    ConfigurationDataElement<bool> element("/Test/Value", false);
    element.setSaveProcessor([](bool val) { return !val; });
    element.setLoadProcessor([](bool val) { return !val; });

    element = true;
    bool converted = element;
    return element.getWithoutProcessing() == false &&
        element.get() == true && converted == true;
}

bool longProcessorRoundTrip()
{
    ConfigurationDataElement<long> element("/Test/Value", 0);
    element.setSaveProcessor([](long val) { return val * 1000; });
    element.setLoadProcessor([](long val) { return val / 1000; });

    element = 42L;
    long converted = element;
    return element.getWithoutProcessing() == 42000L &&
        element.get() == 42L && converted == 42L;
}

bool unsignedIntProcessorRoundTrip()
{
    ConfigurationDataElement<unsigned int> element("/Test/Value", 0);
    element.setSaveProcessor([](unsigned int val) { return val * 100; });
    element.setLoadProcessor([](unsigned int val) { return val / 100; });

    element = 7u;
    unsigned int converted = element;
    return element.getWithoutProcessing() == 700u &&
        element.get() == 7u && converted == 7u;
}

bool wideIntegerProcessorRoundTrip()
{
    ConfigurationDataElement<uint64_t> element("/Test/Value", 0);
    element.setSaveProcessor([](uint64_t val) { return val * 1000000ULL; });
    element.setLoadProcessor([](uint64_t val) { return val / 1000000ULL; });

    element = 12345;
    uint64_t converted = element;
    return element.getWithoutProcessing() == 12345000000ULL &&
        element.get() == 12345ULL && converted == 12345ULL;
}

bool wxStringProcessorRoundTrip()
{
    ConfigurationDataElement<wxString> element("/Test/Value", wxString());
    element.setSaveProcessor([](wxString val) { return wxString("stored:") + val; });
    element.setLoadProcessor([](wxString val) { return val.Mid(7); });

    element = wxString::FromUTF8("hello");
    wxString converted = element;
    return element.getWithoutProcessing() == wxString::FromUTF8("stored:hello") &&
        element.get() == wxString::FromUTF8("hello") &&
        converted == wxString::FromUTF8("hello");
}

bool boolListProcessorRoundTrip()
{
    ConfigurationDataElement<std::vector<bool>> element("/Test/Value", {});
    element.setSaveProcessor([](std::vector<bool> val) {
        for (size_t i = 0; i < val.size(); i++) val[i] = !val[i];
        return val;
    });
    element.setLoadProcessor([](std::vector<bool> val) {
        for (size_t i = 0; i < val.size(); i++) val[i] = !val[i];
        return val;
    });

    element = std::vector<bool> { true, false };
    std::vector<bool> converted = element;
    return element.getWithoutProcessing() == std::vector<bool> { false, true } &&
        element.get() == std::vector<bool> { true, false } &&
        converted == std::vector<bool> { true, false };
}

bool intListProcessorRoundTrip()
{
    ConfigurationDataElement<std::vector<int>> element("/Test/Value", {});
    element.setSaveProcessor([](std::vector<int> val) {
        for (auto& item : val) item *= 100;
        return val;
    });
    element.setLoadProcessor([](std::vector<int> val) {
        for (auto& item : val) item /= 100;
        return val;
    });

    element = std::vector<int> { 2, 0, 1 };
    std::vector<int> converted = element;
    return element.getWithoutProcessing() == std::vector<int> { 200, 0, 100 } &&
        element.get() == std::vector<int> { 2, 0, 1 } &&
        converted == std::vector<int> { 2, 0, 1 };
}

bool stringListProcessorRoundTrip()
{
    ConfigurationDataElement<std::vector<wxString>> element("/Test/Value", {});
    element.setSaveProcessor([](std::vector<wxString> val) {
        for (auto& item : val) item = wxString("stored:") + item;
        return val;
    });
    element.setLoadProcessor([](std::vector<wxString> val) {
        for (auto& item : val) item = item.Mid(7);
        return val;
    });

    element = std::vector<wxString> { wxString::FromUTF8("a"), wxString::FromUTF8("b") };
    std::vector<wxString> converted = element;
    return element.getWithoutProcessing() == std::vector<wxString> {
            wxString::FromUTF8("stored:a"), wxString::FromUTF8("stored:b") } &&
        element.get() == std::vector<wxString> { wxString::FromUTF8("a"), wxString::FromUTF8("b") } &&
        converted == std::vector<wxString> { wxString::FromUTF8("a"), wxString::FromUTF8("b") };
}

bool intMapProcessorRoundTrip()
{
    ConfigurationDataElement<std::map<wxString, int>> element("/Test/Value", {});
    element.setSaveProcessor([](std::map<wxString, int> val) {
        for (auto& item : val) item.second *= 100;
        return val;
    });
    element.setLoadProcessor([](std::map<wxString, int> val) {
        for (auto& item : val) item.second /= 100;
        return val;
    });

    element = std::map<wxString, int> {
        { wxString::FromUTF8("a"), 1 }, { wxString::FromUTF8("b"), 2 } };
    std::map<wxString, int> converted = element;
    return element.getWithoutProcessing() == std::map<wxString, int> {
            { wxString::FromUTF8("a"), 100 }, { wxString::FromUTF8("b"), 200 } } &&
        element.get() == std::map<wxString, int> {
            { wxString::FromUTF8("a"), 1 }, { wxString::FromUTF8("b"), 2 } } &&
        converted == std::map<wxString, int> {
            { wxString::FromUTF8("a"), 1 }, { wxString::FromUTF8("b"), 2 } };
}

// The types that the rest of the configuration code reads with a plain
// assignment all convert through the load processor (or don't, where there
// isn't one).
bool everyTypeConvertsImplicitly()
{
    ConfigurationDataElement<float> frequency("/Test/Frequency", 8.0f);
    frequency = 8000.0f;                    // no save processor: stored as given
    frequency.setLoadProcessor([](float val) { return val / 1000.0f; });
    float converted = frequency;   // reads the stored 8000 through the processor
    if (frequency.getWithoutProcessing() != 8000.0f || converted != 8.0f) return false;

    ConfigurationDataElement<std::string> name("/Test/Name", "abc");
    std::string given = name;
    name.setLoadProcessor([](std::string val) { return val + "!"; });
    std::string processed = name;
    if (given != "abc" || processed != "abc!" || name.get() != "abc!") return false;

    ConfigurationDataElement<std::vector<std::string>> list("/Test/List", {});
    list = std::vector<std::string> { "x" };  // no save processor: stored as given
    std::vector<std::string> items = list;
    list.setLoadProcessor([](std::vector<std::string> val) {
        std::vector<std::string> extended = val;
        extended.push_back("more");
        return extended;
    });
    std::vector<std::string> extended = list;
    if (items != std::vector<std::string> { "x" } ||
        extended != std::vector<std::string> { "x", "more" }) return false;

    // The kinds of list and map that configuration settings really hold,
    // converted the plain way and through a load processor.
    ConfigurationDataElement<std::vector<wxString>> words("/Test/Words", {});
    words = std::vector<wxString> { wxString::FromUTF8("a") };
    std::vector<wxString> plainWords = words;
    words.setLoadProcessor([](std::vector<wxString> val) {
        val.push_back(wxString::FromUTF8("b"));
        return val;
    });
    std::vector<wxString> saidWords = words;
    if (plainWords != std::vector<wxString> { wxString::FromUTF8("a") } ||
        saidWords != std::vector<wxString> { wxString::FromUTF8("a"), wxString::FromUTF8("b") }) return false;

    ConfigurationDataElement<std::vector<bool>> flags("/Test/Flags", {});
    flags = std::vector<bool> { true };
    std::vector<bool> plainFlags = flags;
    flags.setLoadProcessor([](std::vector<bool> val) { val.push_back(false); return val; });
    std::vector<bool> addedFlags = flags;
    if (plainFlags != std::vector<bool> { true } ||
        addedFlags != std::vector<bool> { true, false }) return false;

    ConfigurationDataElement<std::vector<int>> numbers("/Test/Numbers", {});
    numbers = std::vector<int> { 3 };
    std::vector<int> plainNumbers = numbers;
    numbers.setLoadProcessor([](std::vector<int> val) { val.push_back(4); return val; });
    std::vector<int> extraNumbers = numbers;
    if (plainNumbers != std::vector<int> { 3 } ||
        extraNumbers != std::vector<int> { 3, 4 }) return false;

    ConfigurationDataElement<std::map<wxString, int>> bands("/Test/BandMap", {});
    bands = std::map<wxString, int> { { wxString::FromUTF8("40m"), 7 } };
    std::map<wxString, int> plainBands = bands;
    bands.setLoadProcessor([](std::map<wxString, int> val) {
        for (auto& item : val) item.second += 1;
        return val;
    });
    std::map<wxString, int> tunedBands = bands;
    return plainBands == std::map<wxString, int> { { wxString::FromUTF8("40m"), 7 } } &&
        tunedBands == std::map<wxString, int> { { wxString::FromUTF8("40m"), 8 } };
}

}

int main()
{
    executeTestCase("startsWithDefaultValue", startsWithDefaultValue);
    executeTestCase("assignmentChangesValueButNotDefault", assignmentChangesValueButNotDefault);
    executeTestCase("changingDefaultDoesNotChangeValue", changingDefaultDoesNotChangeValue);
    executeTestCase("arrowOperatorAccessesUnderlyingValue", arrowOperatorAccessesUnderlyingValue);
    executeTestCase("saveProcessorAppliesOnAssignment", saveProcessorAppliesOnAssignment);
    executeTestCase("loadProcessorAppliesOnRead", loadProcessorAppliesOnRead);
    executeTestCase("processorsRoundTrip", processorsRoundTrip);
    executeTestCase("withoutProcessingBypassesProcessors", withoutProcessingBypassesProcessors);
    executeTestCase("worksWithContainers", worksWithContainers);
    executeTestCase("boolProcessorRoundTrip", boolProcessorRoundTrip);
    executeTestCase("longProcessorRoundTrip", longProcessorRoundTrip);
    executeTestCase("unsignedIntProcessorRoundTrip", unsignedIntProcessorRoundTrip);
    executeTestCase("wideIntegerProcessorRoundTrip", wideIntegerProcessorRoundTrip);
    executeTestCase("wxStringProcessorRoundTrip", wxStringProcessorRoundTrip);
    executeTestCase("boolListProcessorRoundTrip", boolListProcessorRoundTrip);
    executeTestCase("intListProcessorRoundTrip", intListProcessorRoundTrip);
    executeTestCase("stringListProcessorRoundTrip", stringListProcessorRoundTrip);
    executeTestCase("intMapProcessorRoundTrip", intMapProcessorRoundTrip);
    executeTestCase("everyTypeConvertsImplicitly", everyTypeConvertsImplicitly);
    return testResult();
}
