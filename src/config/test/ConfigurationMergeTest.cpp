// Copying a configuration and merging what was changed in a copy back into
// it, as Settings does (see FreeDVConfiguration::mergeChangesFrom()).

#include <set>
#include <utility>
#include <string>
#include <vector>

#include "../FreeDVConfiguration.h"
#include "ConfigTestCommon.h"

namespace {

// What the configuration would be saved as.
wxString savedContents(FreeDVConfiguration& config)
{
    auto file = makeConfig();
    config.save(file.get());
    return contentsOf(*file);
}

// A configuration loaded from nothing (i.e. its defaults), as at first start.
void loadDefaults(FreeDVConfiguration& config)
{
    auto file = makeConfig();
    config.load(file.get());
}

// The live configuration and Settings' two copies of it.
struct Merge
{
    FreeDVConfiguration live;
    FreeDVConfiguration original;
    FreeDVConfiguration edited;

    std::vector<unsigned> eventsSeen;

    Merge()
    {
        loadDefaults(live);
        live.onChanged += [this](unsigned effects) { eventsSeen.push_back(effects); };
        original.copyFrom(live);
        edited.copyFrom(live);
    }

    unsigned merge()
    {
        return live.mergeChangesFrom(original, edited);
    }
};

bool copyIsIdentical()
{
    FreeDVConfiguration source;
    loadDefaults(source);
    source.reportingConfiguration.reportingCallsign = "AA1ZZZ";
    source.audioConfiguration.soundCard1In.deviceName = "Some Microphone";
    source.fifoSizeMs = 500;
    source.txAttenByBand = std::map<wxString, int>{ { "20m", -30 } };
    source.reportingConfiguration.reportingFrequencyList = std::vector<wxString>{ "14.2360", "7.1770" };

    FreeDVConfiguration copy;
    copy.copyFrom(source);
    return check("copy as saved", savedContents(copy), savedContents(source));
}

bool elementsAreEachSettingOnce()
{
    FreeDVConfiguration config;
    std::set<std::string> names;
    bool result = true;
    for (auto* element : config.elements())
    {
        std::string name = element->getElementName();
        if (!names.insert(name).second)
        {
            std::cout << "[" << name << " listed twice] ";
            result = false;
        }
    }

    // A few from each part of the configuration.
    for (auto name : { "/Audio/soundCard1InDeviceName", "/Audio/fifoSize_ms", "/Hamlib/SerialPort",
                       "/Rig/PttInPort", "/Reporting/Callsign", "/MainFrame/rxNbookCtrl" })
    {
        if (names.count(name) == 0)
        {
            std::cout << "[" << name << " missing] ";
            result = false;
        }
    }
    return result;
}

bool noticesSettingsThatDontPairUp()
{
    FreeDVConfiguration first;
    FreeDVConfiguration second;
    auto firstList = first.elements();
    auto secondList = second.elements();

    bool result = true;
    result &= checkTrue("two copies pair up", WxWidgetsConfigStore::elementsPairUp(firstList, secondList));

    auto reordered = secondList;
    std::swap(reordered[3], reordered[4]);
    result &= checkTrue("two settings swapped", !WxWidgetsConfigStore::elementsPairUp(firstList, reordered));

    auto shorter = secondList;
    shorter.pop_back();
    result &= checkTrue("one missing", !WxWidgetsConfigStore::elementsPairUp(firstList, shorter));

    result &= checkTrue("other kinds of configuration",
        !WxWidgetsConfigStore::elementsPairUp(
            first.rigControlConfiguration.elements(), first.reportingConfiguration.elements()));
    return result;
}

bool mergesOnlyWhatWasEdited()
{
    Merge m;
    m.edited.reportingConfiguration.reportingCallsign = "AA1ZZZ";

    // Changed by the rest of FreeDV while Settings was open.
    m.live.reportingConfiguration.freedvReporterStatusText = "Changed meanwhile";
    m.live.currentNotebookTab = 3;

    m.merge();

    bool result = true;
    result &= check("callsign", m.live.reportingConfiguration.reportingCallsign.get(), "AA1ZZZ");
    result &= check("status text changed meanwhile", m.live.reportingConfiguration.freedvReporterStatusText.get(), "Changed meanwhile");
    result &= check("tab changed meanwhile", m.live.currentNotebookTab.get(), 3L);
    return result;
}

bool editWinsOverChangeMadeMeanwhile()
{
    Merge m;
    m.live.reportingConfiguration.reportingGridSquare = "FN20";
    m.edited.reportingConfiguration.reportingGridSquare = "DM12";
    m.merge();
    return check("grid square", m.live.reportingConfiguration.reportingGridSquare.get(), "DM12");
}

bool saysWhatChangesNeed()
{
    bool result = true;
    {
        Merge m;
        m.edited.audioConfiguration.soundCard2In.deviceName = "Radio";
        m.edited.rigControlConfiguration.hamlibSerialPort = "/dev/ttyUSB0";
        result &= check("audio and radio", m.merge(), (unsigned)(ConfigChange::RESTARTS_AUDIO | ConfigChange::RECONNECTS_RADIO));
        result &= check("events", m.eventsSeen, std::vector<unsigned>{ ConfigChange::RESTARTS_AUDIO | ConfigChange::RECONNECTS_RADIO });
    }
    {
        Merge m;
        m.edited.reportingConfiguration.udpReportingPort = 2238;
        m.edited.rigControlConfiguration.serialPTTInputPort = "/dev/ttyUSB1";
        result &= check("reporting and PTT input", m.merge(), (unsigned)(ConfigChange::RESTARTS_REPORTING | ConfigChange::REOPENS_PTT_INPUT));
    }
    {
        Merge m;
        m.edited.reportingConfiguration.reportingCallsign = "AA1ZZZ";
        result &= check("callsign", m.merge(), (unsigned)(ConfigChange::RESTARTS_REPORTING | ConfigChange::RECONNECTS_FREEDV_REPORTER));
    }
    {
        // Kept by the running audio, which is told without restarting.
        Merge m;
        m.edited.rigControlConfiguration.leftChannelVoxTone = true;
        result &= check("VOX tone", m.merge(), (unsigned)ConfigChange::UPDATES_RUNNING_AUDIO);
    }
    {
        // Taken up as they're used: nothing to do, and no event.
        Merge m;
        m.edited.transmitLevel = -10;
        m.edited.halfDuplexMode = false;
        result &= check("used as they are", m.merge(), (unsigned)ConfigChange::NONE);
        result &= check("used as they are: values", m.live.transmitLevel.get(), -10);
        result &= check("used as they are: events", m.eventsSeen.size(), (size_t)0);
    }
    {
        // Edited back to what it was: nothing changed.
        Merge m;
        m.edited.fifoSizeMs = m.live.fifoSizeMs.get() + 100;
        m.edited.fifoSizeMs = m.live.fifoSizeMs.get();
        result &= check("edited back", m.merge(), (unsigned)ConfigChange::NONE);
    }
    {
        // Changed meanwhile to what was entered: nothing more to do.
        Merge m;
        m.live.audioConfiguration.soundCard1Out.deviceName = "Speaker";
        m.edited.audioConfiguration.soundCard1Out.deviceName = "Speaker";
        result &= check("already changed meanwhile", m.merge(), (unsigned)ConfigChange::NONE);
    }
    return result;
}

bool frequencyUnitsChangedWithList()
{
    // Settings enters the list in the units it's shown in, then changes the
    // units: the list has to come out the same as when that's done to the
    // configuration directly.
    Merge m;
    FreeDVConfiguration direct;
    direct.copyFrom(m.live);

    std::vector<wxString> list{ "14.2360", "7.1770" };
    m.edited.reportingConfiguration.reportingFrequencyList = list;
    m.edited.reportingConfiguration.reportingFrequencyAsKhz = true;
    direct.reportingConfiguration.reportingFrequencyList = list;
    direct.reportingConfiguration.reportingFrequencyAsKhz = true;

    m.merge();
    return check("frequency list", m.live.reportingConfiguration.reportingFrequencyList.get(),
                 direct.reportingConfiguration.reportingFrequencyList.get());
}

}

int main()
{
    executeTestCase("copyIsIdentical", copyIsIdentical);
    executeTestCase("elementsAreEachSettingOnce", elementsAreEachSettingOnce);
    executeTestCase("noticesSettingsThatDontPairUp", noticesSettingsThatDontPairUp);
    executeTestCase("mergesOnlyWhatWasEdited", mergesOnlyWhatWasEdited);
    executeTestCase("editWinsOverChangeMadeMeanwhile", editWinsOverChangeMadeMeanwhile);
    executeTestCase("saysWhatChangesNeed", saysWhatChangesNeed);
    executeTestCase("frequencyUnitsChangedWithList", frequencyUnitsChangedWithList);
    return testResult();
}
