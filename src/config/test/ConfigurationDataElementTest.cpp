#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <thread>
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

bool getAndAssignReplaceUnderlyingValue()
{
    ConfigurationDataElement<std::string> element("/Test/Value", "abc");
    if (element.get().size() != 3) return false;

    std::string value = element.get();
    value.append("def");
    element = value;
    if (element.get() != "abcdef") return false;

    // The same round trip with both processors set: what's assigned goes
    // through the save processor, what's read back through the load one.
    element.setSaveProcessor([](std::string val) { return val + "|stored"; });
    element.setLoadProcessor([](std::string val) { return val.substr(0, val.size() - 7); });

    element = "ghi";
    std::string converted = element;
    return element.getWithoutProcessing() == "ghi|stored" &&
        element.get() == "ghi" && converted == "ghi";
}

// A writer thread does get/modify/set read-modify-write cycles while a
// reader thread continuously reads through the load processor. Built with
// ThreadSanitizer this test fails if the element ever stops serializing
// access to its stored value.
bool concurrentGetAndSetIsSerialized()
{
    ConfigurationDataElement<std::vector<int> > element("/Test/Vec", std::vector<int>());

    const int kIterations = 2000;
    std::atomic<size_t> totalRead{0};

    std::thread writer([&]() {
        for (int i = 0; i < kIterations; i++)
        {
            auto value = element.get();
            value.push_back(i);
            element.setWithoutProcessing(value);
        }
    });

    std::thread reader([&]() {
        for (int i = 0; i < kIterations * 2; i++)
        {
            totalRead.fetch_add(element.get().size());
        }
    });

    writer.join();
    reader.join();

    auto finalValue = element.get();
    if ((int)finalValue.size() != kIterations) return false;
    if (finalValue.back() != kIterations - 1) return false;
    return true;
}

// Two writers each add their own keys with update(). If the
// read-modify-write weren't done in one locked step, one writer could
// overwrite the other's copy and keys would go missing.
bool concurrentUpdatesAreNotLost()
{
    ConfigurationDataElement<std::map<int, int> > element("/Test/Map", std::map<int, int>());

    const int kIterations = 2000;
    auto addKeys = [&](int first) {
        for (int i = 0; i < kIterations; i++)
        {
            element.update([&](std::map<int, int>& value) {
                value.insert_or_assign(first + i, i);
            });
        }
    };

    std::thread writer1(addKeys, 0);
    std::thread writer2(addKeys, kIterations);
    writer1.join();
    writer2.join();

    return (int)element.get().size() == 2 * kIterations;
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

// Limits hold however the value is set: assigned (from Settings), set
// without processing (from the config file) or updated in place.
bool limitsApplyHoweverValueIsSet()
{
    ConfigurationDataElement<int> element("/Test/Value", 50);
    element.setLimits(10, 100);

    bool result = element.get() == 50;
    element = 5;
    result &= element.get() == 10;
    element = 1000;
    result &= element.get() == 100;
    element.setWithoutProcessing(-7);
    result &= element.get() == 10;
    element.update([](int& value) { value = 500; });
    result &= element.get() == 100;
    element = 42;
    result &= element.get() == 42;
    return result;
}

// One set before the limits come in is brought within them.
bool limitsApplyToExistingValue()
{
    ConfigurationDataElement<unsigned int> element("/Test/Value", 0x1FF);
    element.setLimits(0, 0xFF);
    return element.get() == 0xFF;
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

// What a change to each kind of element needs done for it to take effect,
// as mergeChangesFrom() reports it.
bool changeEffectsComeBackForEveryKind()
{
    ConfigurationDataElement<int> frequency("/Test/Frequency", 8000, ConfigChange::UPDATES_RUNNING_AUDIO);
    ConfigurationDataElement<long> volume("/Test/Volume", 1, ConfigChange::RESTARTS_AUDIO);
    ConfigurationDataElement<unsigned long> timeout("/Test/Timeout", 5, ConfigChange::RECONNECTS_RADIO);
    ConfigurationDataElement<float> gamma("/Test/Gamma", 1.0f, ConfigChange::RESTARTS_AUDIO | ConfigChange::UPDATES_RUNNING_AUDIO);
    ConfigurationDataElement<wxString> callsign("/Test/Callsign", wxString(), ConfigChange::RECONNECTS_FREEDV_REPORTER);
    ConfigurationDataElement<std::map<wxString, int>> bands("/Test/Bands", std::map<wxString, int>(), ConfigChange::NONE);
    ConfigurationDataElement<std::vector<int>> filters("/Test/Filters", std::vector<int>(), ConfigChange::REOPENS_PTT_INPUT);

    bool result = true;
    result &= frequency.getChangeEffects() == (uint32_t)ConfigChange::UPDATES_RUNNING_AUDIO;
    result &= volume.getChangeEffects() == (uint32_t)ConfigChange::RESTARTS_AUDIO;
    result &= timeout.getChangeEffects() == (uint32_t)ConfigChange::RECONNECTS_RADIO;
    result &= gamma.getChangeEffects() == (uint32_t)(ConfigChange::RESTARTS_AUDIO | ConfigChange::UPDATES_RUNNING_AUDIO);
    result &= callsign.getChangeEffects() == (uint32_t)ConfigChange::RECONNECTS_FREEDV_REPORTER;
    result &= bands.getChangeEffects() == (uint32_t)ConfigChange::NONE;
    result &= filters.getChangeEffects() == (uint32_t)ConfigChange::REOPENS_PTT_INPUT;
    return result;
}

// Settings' copy of a setting is copied back into the live one, taking its
// value and its default with it (a migration may have changed the default
// of the copy, and Settings' "restore default" works from that).
template<typename UnderlyingType>
bool copyCarriesValueAndDefault(
    UnderlyingType const& sourceDefault, UnderlyingType const& sourceValue, UnderlyingType const& targetValue)
{
    ConfigurationDataElement<UnderlyingType> source("/Test/Source", sourceDefault);
    source = sourceValue;

    ConfigurationDataElement<UnderlyingType> target("/Test/Target", sourceDefault);
    target = targetValue;

    target.copyValueFrom(source);

    return target.getWithoutProcessing() == sourceValue &&
        target.getDefaultVal() == sourceDefault &&
        source.getWithoutProcessing() == sourceValue &&
        source.getDefaultVal() == sourceDefault;
}

bool copyCarriesValueAndDefaultForEveryKind()
{
    bool result = true;
    result &= copyCarriesValueAndDefault<int>(1, 2, 3);
    result &= copyCarriesValueAndDefault<long>(1L, 2L, 3L);
    result &= copyCarriesValueAndDefault<unsigned long>(1UL, 2UL, 3UL);
    result &= copyCarriesValueAndDefault<float>(1.0f, 2.0f, 3.0f);
    result &= copyCarriesValueAndDefault<wxString>(
        wxString::FromUTF8("a"), wxString::FromUTF8("b"), wxString::FromUTF8("c"));
    result &= copyCarriesValueAndDefault<std::map<wxString, int>>(
        { { wxString::FromUTF8("a"), 1 } }, { { wxString::FromUTF8("b"), 2 } }, { { wxString::FromUTF8("c"), 3 } });
    result &= copyCarriesValueAndDefault<std::string>("a", "abcdefghijklmnop", "c");
    result &= copyCarriesValueAndDefault<std::vector<std::string>>(
        std::vector<std::string>(), { "a", "abcdefghijklmnop" }, { "c" });
    result &= copyCarriesValueAndDefault<std::map<int, int>>(
        std::map<int, int>(), { { 1, 1 }, { 2, 2 } }, { { 3, 3 } });
    result &= copyCarriesValueAndDefault<std::vector<int>>({ 1 }, { 2 }, { 3 });
    return result;
}

// A setting's limits hold for every kind of value it can hold, however it's
// set: assigned from Settings, read from the configuration file, updated in
// place, or copied from another copy of the configuration.
template<typename UnderlyingType>
bool limitsHoldForKind(
    UnderlyingType const& low, UnderlyingType const& middle, UnderlyingType const& high,
    UnderlyingType const& below, UnderlyingType const& above)
{
    ConfigurationDataElement<UnderlyingType> source("/Test/Source", middle);
    source = above;

    // One callback for both update() calls, so that this instantiation takes
    // both sides of the limit check rather than adding a second copy of
    // itself that only takes one.
    auto setToBelow = [&](UnderlyingType& value) { value = below; };

    ConfigurationDataElement<UnderlyingType> element("/Test/Value", middle);
    bool result = element.getWithoutProcessing() == middle;

    // With no limits, each way of setting the value takes it exactly as given.
    element = above;
    result &= element.getWithoutProcessing() == above;
    element.setWithoutProcessing(below);
    result &= element.getWithoutProcessing() == below;
    element.update(setToBelow);
    result &= element.getWithoutProcessing() == below;
    element.copyValueFrom(source);
    result &= element.getWithoutProcessing() == above;

    // Setting the limits brings the value that's already there inside them...
    element.setLimits(low, high);
    result &= element.getWithoutProcessing() == high;

    // ...and every way of setting it after that.
    element = below;
    result &= element.getWithoutProcessing() == low;
    element.setWithoutProcessing(above);
    result &= element.getWithoutProcessing() == high;
    element.update(setToBelow);
    result &= element.getWithoutProcessing() == low;
    element.copyValueFrom(source);
    result &= element.getWithoutProcessing() == high;

    // What comes across with a copied value is its default, which the limits
    // of the element it lands in don't touch.
    result &= element.getDefaultVal() == middle;
    return result;
}

bool limitsHoldForEveryKind()
{
    bool result = true;
    result &= limitsHoldForKind<int>(10, 50, 100, 5, 1000);
    result &= limitsHoldForKind<long>(10L, 50L, 100L, 5L, 1000L);
    result &= limitsHoldForKind<unsigned long>(10UL, 50UL, 100UL, 0UL, 1000UL);
    result &= limitsHoldForKind<float>(10.0f, 50.0f, 100.0f, 0.0f, 1000.0f);
    result &= limitsHoldForKind<wxString>(
        wxString::FromUTF8("b"), wxString::FromUTF8("m"), wxString::FromUTF8("y"),
        wxString::FromUTF8("a"), wxString::FromUTF8("z"));
    result &= limitsHoldForKind<std::map<wxString, int>>(
        { { wxString::FromUTF8("a"), 1 } }, { { wxString::FromUTF8("m"), 1 } }, { { wxString::FromUTF8("z"), 1 } },
        { { wxString::FromUTF8("a"), 0 } }, { { wxString::FromUTF8("z"), 2 } });
    result &= limitsHoldForKind<std::vector<int>>({ 1 }, { 5 }, { 10 }, { 0 }, { 11 });
    // bool has nothing outside [false,true], so only the above value gets
    // brought back; the lists clamp the way they compare.
    result &= limitsHoldForKind<bool>(false, false, false, false, true);
    result &= limitsHoldForKind<std::vector<bool>>(
        { false, false }, { false, true }, { true, true }, { false }, { true, true, true });
    result &= limitsHoldForKind<std::vector<wxString>>(
        { wxString::FromUTF8("b") }, { wxString::FromUTF8("m") }, { wxString::FromUTF8("y") },
        { wxString::FromUTF8("a") }, { wxString::FromUTF8("z") });
    result &= limitsHoldForKind<std::string>(
        "b", "m", "y", "a", "zabcdefghijklmnop");
    result &= limitsHoldForKind<std::vector<std::string>>(
        { "b" }, { "m" }, { "y" }, { "a" }, { "zabcdefghijklmnop" });
    result &= limitsHoldForKind<std::map<int, int>>(
        { { 1, 1 } }, { { 5, 5 } }, { { 10, 10 } }, { { 0, 0 } }, { { 11, 11 } });
    return result;
}

// Whether Settings' copy of a setting is the same as the live one, for each
// kind of value the configuration holds.
template<typename UnderlyingType>
bool sameValueComparesForKind(UnderlyingType const& first, UnderlyingType const& second)
{
    ConfigurationDataElement<UnderlyingType> a("/Test/A", first);
    ConfigurationDataElement<UnderlyingType> b("/Test/B", first);
    ConfigurationDataElement<UnderlyingType> c("/Test/C", second);

    bool result = a.hasSameValueAs(b) && !a.hasSameValueAs(c);

    // Assigned from Settings, back to what it was and then to something else.
    a = first;
    result &= a.hasSameValueAs(b);
    a = second;
    result &= !a.hasSameValueAs(b);
    return result;
}

bool sameValueComparesForEveryKind()
{
    bool result = true;
    result &= sameValueComparesForKind<int>(1, 2);
    result &= sameValueComparesForKind<long>(1L, 2L);
    result &= sameValueComparesForKind<unsigned long>(1UL, 2UL);
    result &= sameValueComparesForKind<float>(1.0f, 2.0f);
    result &= sameValueComparesForKind<bool>(false, true);
    result &= sameValueComparesForKind<wxString>(
        wxString::FromUTF8("a"), wxString::FromUTF8("abcdefghijklmnop"));
    // Strings long enough to leave the small-string buffer, and containers
    // empty as well as filled, so that both ways of copying them are taken.
    result &= sameValueComparesForKind<std::string>("a", "abcdefghijklmnop");
    result &= sameValueComparesForKind<std::vector<bool>>(
        std::vector<bool>(), std::vector<bool> { true, false });
    result &= sameValueComparesForKind<std::vector<int>>(
        std::vector<int>(), std::vector<int> { 1, 2, 3 });
    result &= sameValueComparesForKind<std::vector<wxString>>(
        std::vector<wxString>(), std::vector<wxString> { wxString::FromUTF8("abcdefghijklmnop") });
    result &= sameValueComparesForKind<std::vector<std::string>>(
        std::vector<std::string>(), std::vector<std::string> { "a", "abcdefghijklmnop" });
    result &= sameValueComparesForKind<std::map<wxString, int>>(
        std::map<wxString, int>(), { { wxString::FromUTF8("a"), 1 }, { wxString::FromUTF8("b"), 2 } });
    result &= sameValueComparesForKind<std::map<int, int>>(
        std::map<int, int>(), { { 1, 1 }, { 2, 2 }, { 3, 3 } });
    return result;
}

bool unsignedLongProcessorRoundTrip()
{
    ConfigurationDataElement<unsigned long> element("/Test/Value", 0);
    element.setSaveProcessor([](unsigned long val) { return val * 1000UL; });
    element.setLoadProcessor([](unsigned long val) { return val / 1000UL; });

    element = 42UL;
    unsigned long converted = element;
    return element.getWithoutProcessing() == 42000UL && element.get() == 42UL && converted == 42UL;
}

}

int main()
{
    executeTestCase("startsWithDefaultValue", startsWithDefaultValue);
    executeTestCase("assignmentChangesValueButNotDefault", assignmentChangesValueButNotDefault);
    executeTestCase("changingDefaultDoesNotChangeValue", changingDefaultDoesNotChangeValue);
    executeTestCase("getAndAssignReplaceUnderlyingValue", getAndAssignReplaceUnderlyingValue);
    executeTestCase("concurrentGetAndSetIsSerialized", concurrentGetAndSetIsSerialized);
    executeTestCase("concurrentUpdatesAreNotLost", concurrentUpdatesAreNotLost);
    executeTestCase("saveProcessorAppliesOnAssignment", saveProcessorAppliesOnAssignment);
    executeTestCase("loadProcessorAppliesOnRead", loadProcessorAppliesOnRead);
    executeTestCase("limitsApplyHoweverValueIsSet", limitsApplyHoweverValueIsSet);
    executeTestCase("limitsApplyToExistingValue", limitsApplyToExistingValue);
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
    executeTestCase("changeEffectsComeBackForEveryKind", changeEffectsComeBackForEveryKind);
    executeTestCase("copyCarriesValueAndDefaultForEveryKind", copyCarriesValueAndDefaultForEveryKind);
    executeTestCase("sameValueComparesForEveryKind", sameValueComparesForEveryKind);
    executeTestCase("limitsHoldForEveryKind", limitsHoldForEveryKind);
    executeTestCase("unsignedLongProcessorRoundTrip", unsignedLongProcessorRoundTrip);
    return testResult();
}
