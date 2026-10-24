#include <map>
#include <vector>

#include <wx/init.h>

#include "../WxWidgetsConfigStore.h"
#include "ConfigTestCommon.h"

namespace {

using StringList = std::vector<wxString>;
using IntList = std::vector<int>;
using BoolList = std::vector<bool>;
using IntMap = std::map<wxString, int>;

// Provides access to the helpers that the configuration classes are built on.
class TestStore : public WxWidgetsConfigStore
{
public:
    virtual void load(wxConfigBase*) override { }
    virtual void save(wxConfigBase*) override { }

    using WxWidgetsConfigStore::load_;
    using WxWidgetsConfigStore::save_;
    using WxWidgetsConfigStore::generateStringFromArray_;
    using WxWidgetsConfigStore::generateStrArrayFromString_;
    using WxWidgetsConfigStore::generateNumArrayFromString_;
    using WxWidgetsConfigStore::generateBoolArrayFromString_;
};

// Saves the given value, "restarts" and loads it into a new element whose
// default is something else.
template<typename T>
T saveAndLoad(T const& value, T const& defaultVal = T())
{
    TestStore store;
    auto config = makeConfig();

    ConfigurationDataElement<T> saved("/Test/Value", defaultVal);
    saved.setWithoutProcessing(value);
    store.save_(config.get(), saved);

    auto reloaded = reload(*config);
    ConfigurationDataElement<T> loaded("/Test/Value", defaultVal);
    store.load_(reloaded.get(), loaded);
    return loaded.getWithoutProcessing();
}

template<typename T>
bool roundTrips(const char* description, T const& value, T const& defaultVal = T())
{
    return check(description, saveAndLoad(value, defaultVal), value);
}

// Loads from a configuration where the element has the given raw contents.
template<typename T>
T loadFrom(wxString const& rawValue, T const& defaultVal = T())
{
    TestStore store;
    auto config = makeConfig("[Test]\nValue=" + rawValue + "\n");

    ConfigurationDataElement<T> loaded("/Test/Value", defaultVal);
    store.load_(config.get(), loaded);
    return loaded.getWithoutProcessing();
}

bool missingEntriesLoadAsDefault()
{
    TestStore store;
    auto config = makeConfig();

    ConfigurationDataElement<int> intVal("/Test/Int", 42);
    ConfigurationDataElement<long> longVal("/Test/Long", 43);
    ConfigurationDataElement<unsigned int> unsignedVal("/Test/Unsigned", 44);
    ConfigurationDataElement<bool> boolVal("/Test/Bool", true);
    ConfigurationDataElement<float> floatVal("/Test/Float", 1.5f);
    ConfigurationDataElement<wxString> stringVal("/Test/String", "default");
    ConfigurationDataElement<StringList> stringListVal("/Test/StringList", { "a", "b,c" });
    ConfigurationDataElement<IntList> intListVal("/Test/IntList", { 3, 2, 1 });
    ConfigurationDataElement<BoolList> boolListVal("/Test/BoolList", { true, false });
    ConfigurationDataElement<IntMap> mapVal("/Test/Map", {});

    // Make sure it's not just leaving the existing value alone.
    intVal.setWithoutProcessing(0);
    longVal.setWithoutProcessing(0);
    unsignedVal.setWithoutProcessing(0);
    boolVal.setWithoutProcessing(false);
    floatVal.setWithoutProcessing(0);
    stringVal.setWithoutProcessing("");
    stringListVal.setWithoutProcessing({});
    intListVal.setWithoutProcessing({});
    boolListVal.setWithoutProcessing({});
    mapVal.setWithoutProcessing({ { "stale", 1 } });

    store.load_(config.get(), intVal);
    store.load_(config.get(), longVal);
    store.load_(config.get(), unsignedVal);
    store.load_(config.get(), boolVal);
    store.load_(config.get(), floatVal);
    store.load_(config.get(), stringVal);
    store.load_(config.get(), stringListVal);
    store.load_(config.get(), intListVal);
    store.load_(config.get(), boolListVal);
    store.load_(config.get(), mapVal);

    bool result = true;
    result &= check("int", intVal.getWithoutProcessing(), 42);
    result &= check("long", longVal.getWithoutProcessing(), 43L);
    result &= check("unsigned int", unsignedVal.getWithoutProcessing(), 44u);
    result &= check("bool", boolVal.getWithoutProcessing(), true);
    result &= check("float", floatVal.getWithoutProcessing(), 1.5f);
    result &= check("string", stringVal.getWithoutProcessing(), "default");
    result &= check("string list", stringListVal.getWithoutProcessing(), StringList { "a", "b,c" });
    result &= check("int list", intListVal.getWithoutProcessing(), IntList { 3, 2, 1 });
    result &= check("bool list", boolListVal.getWithoutProcessing(), BoolList { true, false });
    result &= check("map", mapVal.getWithoutProcessing(), IntMap {});
    return result;
}

bool scalarsRoundTrip()
{
    bool result = true;
    result &= roundTrips("int", 48000, -1);
    result &= roundTrips("negative int", -12345, 0);
    result &= roundTrips("int equal to default", 0, 0);
    result &= roundTrips("long", 123456789L, -1L);
    result &= roundTrips("unsigned int", 0x94u, 0u);
    result &= roundTrips("large unsigned int", 4000000000u, 0u);
    result &= roundTrips("true", true, false);
    result &= roundTrips("false", false, true);
    result &= roundTrips("float", 0.5f, 0.0f);
    result &= roundTrips("fractional float", -6.3f, 0.0f);
    result &= roundTrips("large float", 3000.0f, 0.0f);
    return result;
}

bool stringsRoundTrip()
{
    bool result = true;
    result &= roundTrips<wxString>("string", "K6AQ", "default");
    result &= roundTrips<wxString>("empty string", "", "default");
    result &= roundTrips<wxString>("string with spaces", "  USB Audio CODEC  ", "default");
    result &= roundTrips<wxString>("string with punctuation", "a=b;c#d[e]\"f\"", "default");
    result &= roundTrips<wxString>("Windows path", "C:\\Users\\me\\voicekeyer.wav", "default");
    result &= roundTrips<wxString>("UNIX path", "/home/me/My Recordings/vk.wav", "default");
    result &= roundTrips<wxString>("non-ASCII string", wxString::FromUTF8("Jos\xc3\xa9 \xe5\xa4\xaa\xe9\x83\x8e"), "default");
    return result;
}

bool stringsAreNotExpandedOnLoad()
{
    // Whatever the user typed in is what they should get back, even if it
    // looks like a reference to an environment variable.
    bool result = true;
    result &= roundTrips<wxString>("dollar amount", "$5 QSO party", "default");
    result &= roundTrips<wxString>("UNIX variable", "QRV from $HOME", "default");
    result &= roundTrips<wxString>("braced variable", "QRV from ${HOME}", "default");
    result &= roundTrips<wxString>("percent", "100% solar powered", "default");
    result &= roundTrips<wxString>("Windows variable", "QRV from %USERPROFILE%", "default");
    result &= roundTrips<wxString>("escaped dollar", "a\\$b", "default");
    result &= roundTrips<StringList>("list", { "$HOME", "${HOME}", "a\\$b" });

    // Reading a string shouldn't change how the rest of the application sees the configuration.
    TestStore store;
    auto config = makeConfig();
    ConfigurationDataElement<wxString> element("/Test/Value", "default");
    store.load_(config.get(), element);
    result &= checkTrue("expansion setting was changed", config->IsExpandingEnvVars());
    return result;
}

bool processorsAreBypassed()
{
    TestStore store;
    auto config = makeConfig();

    // What's stored in the element is what goes into the file, and vice versa.
    ConfigurationDataElement<int> saved("/Test/Value", 0);
    saved.setSaveProcessor([](int val) { return val * 100; });
    saved.setLoadProcessor([](int val) { return val / 100; });
    saved = 5;
    store.save_(config.get(), saved);

    long raw = 0;
    config->Read("/Test/Value", &raw);
    if (!check("raw value", raw, 500L)) return false;

    ConfigurationDataElement<int> loaded("/Test/Value", 0);
    loaded.setSaveProcessor([](int val) { return val * 100; });
    loaded.setLoadProcessor([](int val) { return val / 100; });
    store.load_(config.get(), loaded);
    return
        check("value without processing", loaded.getWithoutProcessing(), 500) &&
        check("value", loaded.get(), 5);
}

bool stringListsRoundTrip()
{
    bool result = true;
    result &= roundTrips<StringList>("simple list", { "one", "two", "three" });
    result &= roundTrips<StringList>("empty list", {}, { "default" });
    result &= roundTrips<StringList>("single item", { "one" });
    result &= roundTrips<StringList>("commas", { "CQ, CQ", ",", "a,b,c", "trailing," });
    result &= roundTrips<StringList>("backslashes", { "C:\\Users\\me", "\\", "\\\\", "trailing\\" });
    result &= roundTrips<StringList>("backslashes next to commas", { "a\\,b", "\\,", ",\\", "a\\", ",b" });
    result &= roundTrips<StringList>("empty first item", { "", "b", "c" });
    result &= roundTrips<StringList>("empty middle item", { "a", "", "c" });
    result &= roundTrips<StringList>("empty last item", { "a", "b", "" });
    result &= roundTrips<StringList>("all empty items", { "", "", "" });
    result &= roundTrips<StringList>("spaces", { " a ", "  ", "b c" });
    result &= roundTrips<StringList>("non-ASCII", { wxString::FromUTF8("Jos\xc3\xa9"), wxString::FromUTF8("\xe5\xa4\xaa\xe9\x83\x8e") });
    return result;
}

bool singleEmptyStringLoadsAsEmptyList()
{
    // Limitation of the storage format: a list containing only an empty
    // string is stored the same way as an empty list.
    return check("list", saveAndLoad<StringList>({ "" }, { "default" }), StringList {});
}

bool stringListFormatIsStable()
{
    // Configuration files written by existing releases need to keep loading.
    TestStore store;
    bool result = true;
    result &= check("simple list", store.generateStringFromArray_(StringList { "14.2360", "7.1770" }), "14.2360,7.1770");
    result &= check("escaped list", store.generateStringFromArray_(StringList { "a,b", "c\\d" }), "a\\,b,c\\\\d");
    result &= check("parsed list", store.generateStrArrayFromString_("a\\,b,c\\\\d,e"), StringList { "a,b", "c\\d", "e" });

    // A backslash that isn't escaping anything is kept as is.
    result &= check("unescaped backslash", store.generateStrArrayFromString_("a\\b,c\\"), StringList { "a\\b", "c\\" });
    return result;
}

bool intListsRoundTrip()
{
    bool result = true;
    result &= roundTrips<IntList>("simple list", { 0, 1, 2, 3 });
    result &= roundTrips<IntList>("empty list", {}, { 99 });
    result &= roundTrips<IntList>("single item", { 7 });
    result &= roundTrips<IntList>("negative numbers", { -1, 0, -200 });

    // i.e. no thousands separators.
    result &= roundTrips<IntList>("large numbers", { 1000000, -2147483647, 2147483647 });
    return result;
}

bool intListToleratesBadInput()
{
    bool result = true;
    result &= check("non-numeric items", loadFrom<IntList>("1,abc,3"), IntList { 1, 0, 3 });
    result &= check("empty item", loadFrom<IntList>("1,,3"), IntList { 1, 0, 3 });
    result &= check("trailing separator", loadFrom<IntList>("1,2,"), IntList { 1, 2 });
    result &= check("empty value", loadFrom<IntList>(""), IntList {});
    return result;
}

bool boolListsRoundTrip()
{
    bool result = true;
    result &= roundTrips<BoolList>("simple list", { true, false, true, true });
    result &= roundTrips<BoolList>("empty list", {}, { true });
    result &= roundTrips<BoolList>("all false", { false, false });
    result &= check("stored format", TestStore().generateStringFromArray_(BoolList { true, false, true }), "1,0,1");

    // Only "1" means true.
    result &= check("unexpected values", loadFrom<BoolList>("1,0,2,abc,-1"), BoolList { true, false, false, false, false });
    return result;
}

bool mapsRoundTrip()
{
    bool result = true;
    result &= roundTrips<IntMap>("simple map", { { "20m", 5 }, { "40m", 12 }, { "80m", 0 } });
    result &= roundTrips<IntMap>("empty map", {});
    result &= roundTrips<IntMap>("negative values", { { "a", -30 }, { "b", -1 } });
    result &= roundTrips<IntMap>("keys with spaces", { { "70 cm", 1 }, { "2 m", 2 } });
    return result;
}

bool savingMapRemovesStaleEntries()
{
    TestStore store;
    auto config = makeConfig();

    ConfigurationDataElement<IntMap> element("/Audio/levelByBand", {});
    element.setWithoutProcessing({ { "20m", 5 }, { "40m", 12 } });
    store.save_(config.get(), element);

    element.setWithoutProcessing({ { "40m", 13 }, { "80m", 1 } });
    store.save_(config.get(), element);

    auto reloaded = reload(*config);
    ConfigurationDataElement<IntMap> loaded("/Audio/levelByBand", {});
    store.load_(reloaded.get(), loaded);
    return check("map", loaded.getWithoutProcessing(), IntMap { { "40m", 13 }, { "80m", 1 } });
}

bool savingEmptyMapRemovesAllEntries()
{
    TestStore store;
    auto config = makeConfig();

    ConfigurationDataElement<IntMap> element("/Audio/levelByBand", {});
    element.setWithoutProcessing({ { "20m", 5 } });
    store.save_(config.get(), element);

    element.setWithoutProcessing({});
    store.save_(config.get(), element);

    auto reloaded = reload(*config);
    ConfigurationDataElement<IntMap> loaded("/Audio/levelByBand", {});
    loaded.setWithoutProcessing({ { "stale", 1 } });
    store.load_(reloaded.get(), loaded);
    return check("map", loaded.getWithoutProcessing(), IntMap {});
}

bool mapsDoNotDisturbNeighbours()
{
    TestStore store;
    auto config = makeConfig();

    // Siblings of the map's group, another map alongside it and something
    // whose name merely begins the same way.
    ConfigurationDataElement<int> sibling("/Audio/transmitLevel", 0);
    ConfigurationDataElement<IntMap> otherMap("/Audio/tuneLevelByBand", {});
    ConfigurationDataElement<int> similarName("/Audio/transmitLevelByBandCount", 0);
    ConfigurationDataElement<wxString> elsewhere("/Rig/Port", "");
    sibling.setWithoutProcessing(-7);
    otherMap.setWithoutProcessing({ { "20m", 1 } });
    similarName.setWithoutProcessing(3);
    elsewhere.setWithoutProcessing("COM3");
    store.save_(config.get(), sibling);
    store.save_(config.get(), otherMap);
    store.save_(config.get(), similarName);
    store.save_(config.get(), elsewhere);

    ConfigurationDataElement<IntMap> map("/Audio/transmitLevelByBand", {});
    map.setWithoutProcessing({ { "20m", 5 } });
    store.save_(config.get(), map);
    map.setWithoutProcessing({ { "40m", 6 } });
    store.save_(config.get(), map);

    bool result = check("path after saving", config->GetPath(), "");

    auto reloaded = reload(*config);
    store.load_(reloaded.get(), map);
    result &= check("path after loading", reloaded->GetPath(), "");

    sibling.setWithoutProcessing(0);
    otherMap.setWithoutProcessing({});
    similarName.setWithoutProcessing(0);
    elsewhere.setWithoutProcessing("");
    store.load_(reloaded.get(), sibling);
    store.load_(reloaded.get(), otherMap);
    store.load_(reloaded.get(), similarName);
    store.load_(reloaded.get(), elsewhere);

    result &= check("map", map.getWithoutProcessing(), IntMap { { "40m", 6 } });
    result &= check("sibling", sibling.getWithoutProcessing(), -7);
    result &= check("other map", otherMap.getWithoutProcessing(), IntMap { { "20m", 1 } });
    result &= check("similarly named entry", similarName.getWithoutProcessing(), 3);
    result &= check("entry elsewhere", elsewhere.getWithoutProcessing(), "COM3");
    return result;
}

bool mapSkipsNonNumericEntries()
{
    TestStore store;
    auto config = makeConfig("[Audio/levelByBand]\n20m=5\n40m=abc\n80m=-3\n");

    ConfigurationDataElement<IntMap> loaded("/Audio/levelByBand", {});
    store.load_(config.get(), loaded);
    return check("map", loaded.getWithoutProcessing(), IntMap { { "20m", 5 }, { "80m", -3 } });
}

bool unsignedIntLoadsFromOlderFormats()
{
    // Stored as a plain decimal number.
    return
        check("value", loadFrom<unsigned int>("148", 0u), 148u) &&
        check("non-numeric value", loadFrom<unsigned int>("abc", 7u), 7u);
}


// A map whose name has no parent path (stored straight under the root, as
// FreeDV's own configuration elements are) still saves, cleans out stale
// entries and loads back.
bool mapWithoutAParentPathRoundTrips()
{
    TestStore store;
    auto config = makeConfig();

    ConfigurationDataElement<IntMap> element("FrequencyBandMap", {});
    element.setWithoutProcessing({ { "20m", 5 }, { "40m", 12 } });
    store.save_(config.get(), element);

    element.setWithoutProcessing({ { "40m", 13 } });
    store.save_(config.get(), element);

    auto reloaded = reload(*config);
    ConfigurationDataElement<IntMap> loaded("FrequencyBandMap", { { "stale", 1 } });
    store.load_(reloaded.get(), loaded);
    return check("map", loaded.getWithoutProcessing(), IntMap { { "40m", 13 } });
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

    executeTestCase("missingEntriesLoadAsDefault", missingEntriesLoadAsDefault);
    executeTestCase("scalarsRoundTrip", scalarsRoundTrip);
    executeTestCase("stringsRoundTrip", stringsRoundTrip);
    executeTestCase("stringsAreNotExpandedOnLoad", stringsAreNotExpandedOnLoad);
    executeTestCase("processorsAreBypassed", processorsAreBypassed);
    executeTestCase("stringListsRoundTrip", stringListsRoundTrip);
    executeTestCase("singleEmptyStringLoadsAsEmptyList", singleEmptyStringLoadsAsEmptyList);
    executeTestCase("stringListFormatIsStable", stringListFormatIsStable);
    executeTestCase("intListsRoundTrip", intListsRoundTrip);
    executeTestCase("intListToleratesBadInput", intListToleratesBadInput);
    executeTestCase("boolListsRoundTrip", boolListsRoundTrip);
    executeTestCase("mapsRoundTrip", mapsRoundTrip);
    executeTestCase("savingMapRemovesStaleEntries", savingMapRemovesStaleEntries);
    executeTestCase("savingEmptyMapRemovesAllEntries", savingEmptyMapRemovesAllEntries);
    executeTestCase("mapsDoNotDisturbNeighbours", mapsDoNotDisturbNeighbours);
    executeTestCase("mapSkipsNonNumericEntries", mapSkipsNonNumericEntries);
    executeTestCase("mapWithoutAParentPathRoundTrips", mapWithoutAParentPathRoundTrips);
    executeTestCase("unsignedIntLoadsFromOlderFormats", unsignedIntLoadsFromOlderFormats);
    return testResult();
}
