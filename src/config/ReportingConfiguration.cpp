//==========================================================================
// Name:            ReportingConfiguration.h
// Purpose:         Implements the reporting configuration for FreeDV
// Created:         July 2, 2023
// Authors:         Mooneer Salem
// 
// License:
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2.1,
//  as published by the Free Software Foundation.  This program is
//  distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
//  License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//
//==========================================================================

#include <wx/tokenzr.h>
#include <wx/numformatter.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <inttypes.h>
#include <algorithm>
#include <cmath>

#include "../defines.h"
#include "reporting/FreeDVReporter.h"
#include "ReportingConfiguration.h"

ReportingConfiguration::ReportingConfiguration()
    : reportingEnabled("/Reporting/Enable", false)
    , reportingCallsign("/Reporting/Callsign", wxT(""))
    , reportingGridSquare("/Reporting/GridSquare", wxT(""))
        
    , reportingFrequency("/Reporting/Frequency", 0)
        
    , manualFrequencyReporting("/Reporting/ManualFrequencyReporting", false)
        
    , pskReporterEnabled("/Reporting/PSKReporter/Enable", true)
        
    , freedvReporterEnabled("/Reporting/FreeDV/Enable", true)
    , freedvReporterForcedOff("/Reporting/FreeDV/ForcedOff", false)
    , freedvReporterHostname("/Reporting/FreeDV/Hostname", wxT(FREEDV_REPORTER_DEFAULT_HOSTNAME))
    , freedvReporterUseTls("/Reporting/FreeDV/UseTls", true)
    , freedvReporterBandFilter("/Reporting/FreeDV/CurrentBandFilter", 0)
    , useMetricDistances("/Reporting/FreeDV/UseMetricDistances", true)
    , freedvReporterBandFilterTracksFrequency("/Reporting/FreeDV/BandFilterTracksFrequency", false)
    , freedvReporterForceReceiveOnly("/Reporting/FreeDV/ForceReceiveOnly", false)
    , freedvReporterBandFilterTracksFreqBand("/Reporting/FreeDV/BandFilterTracking/TracksFreqBand", true)
    , freedvReporterBandFilterTracksExactFreq("/Reporting/FreeDV/BandFilterTracking/TracksExactFreq", false)
    , freedvReporterStatusText("/Reporting/FreeDV/StatusText", wxT(""))
    , freedvReporterRecentStatusTexts("/Reporting/FreeDV/RecentStatusTexts", {})
    
    , freedvReporterColumnOrder("/Reporting/FreeDV/ColumnOrder", { }) /* empty means default ordering */
    , freedvReporterColumnVisibility("/Reporting/FreeDV/ColumnVisibility", { })

    , freedvReporterEnableMaxIdleFilter("/Reporting/FreeDV/EnableMaxIdleFilter", false)
    , freedvReporterMaxIdleMinutes("/Reporting/FreeDV/MaxIdleMinutes", 120)
    , freedvReporterColumnFilterOperators("/Reporting/FreeDV/ColumnFilterOperators", {})
    , freedvReporterColumnFilterValues("/Reporting/FreeDV/ColumnFilterValues", {})

    , udpReportingEnabled("/Reporting/UDP/Enable", false)
    , udpReportingHostname("/Reporting/UDP/Hostname", wxT("127.0.0.1"))
    , udpReportingPort("/Reporting/UDP/Port", 2237)

    , udpBroadcastEnabled("/Reporting/UDPBroadcast/Enable", false)
    , udpBroadcastAddress("/Reporting/UDPBroadcast/Address", wxT("224.0.0.1"))
    , udpBroadcastPort("/Reporting/UDPBroadcast/Port", 7177)

    , useUTCForReporting("/CallsignList/UseUTCTime", false)

    // Legacy dial-frequency list. Kept untouched (key and defaults unchanged)
    // for backward compatibility with older builds that still read this key
    // directly; current builds only use it as a one-time migration source
    // for reportingFrequencyListCentred below (see migrateFrequencyListToCentred_).
    , reportingFrequencyList("/Reporting/FrequencyList", {
        wxT("1.8700"),
        wxT("3.6250"),
        wxT("3.6430"),
        wxT("3.6930"),
        wxT("3.6970"),
        wxT("3.8030"),
        wxT("5.4035"),
        wxT("5.3685"),
        wxT("7.1770"),
        wxT("7.1970"),
        wxT("14.2360"),
        wxT("14.2400"),
        wxT("18.1180"),
        wxT("21.3130"),
        wxT("24.9330"),
        wxT("28.3300"),
        wxT("28.7200"),
        wxT("10489.6400"),
    })
    // Values are centre frequencies (dial frequency +/-1.5kHz depending on
    // sideband), matching the convention used for reportingFrequency. This
    // is the list actually shown/edited in the GUI.
    , reportingFrequencyListCentred("/Reporting/FrequencyListCentred", {
        wxT("1.8685"),
        wxT("3.6235"),
        wxT("3.6415"),
        wxT("3.6915"),
        wxT("3.6955"),
        wxT("3.8015"),
        wxT("5.4050"),
        wxT("5.3700"),
        wxT("7.1755"),
        wxT("7.1955"),
        wxT("14.2375"),
        wxT("14.2415"),
        wxT("18.1195"),
        wxT("21.3145"),
        wxT("24.9345"),
        wxT("28.3315"),
        wxT("28.7215"),
        wxT("10489.6415"),
    })
    , freedvReporterTxRowBackgroundColor("/Reporting/FreeDV/TxRowBackgroundColor", "#fc4500")
    , freedvReporterTxRowForegroundColor("/Reporting/FreeDV/TxRowForegroundColor", "#000000")
    , freedvReporterRxRowBackgroundColor("/Reporting/FreeDV/RxRowBackgroundColor", "#379baf")
    , freedvReporterRxRowForegroundColor("/Reporting/FreeDV/RxRowForegroundColor", "#000000")
    , freedvReporterMsgRowBackgroundColor("/Reporting/FreeDV/MsgRowBackgroundColor", "#E58BE5")
    , freedvReporterMsgRowForegroundColor("/Reporting/FreeDV/MsgRowForegroundColor", "#000000")
        
    , reportingFrequencyAsKhz("/Reporting/FrequencyAsKHz", false)
    , reportingDirectionAsCardinal("/Reporting/DirectionAsCardinal", false)
    , csvLogFilePath("/Reporting/CSV/LogFilePath", wxT(""))
{
    // Special handling for the frequency lists to properly handle locales.
    // Both the legacy and centred lists use the same on-disk convention, so
    // share the same processors (see convertFrequencyListFromStorage_/ToStorage_).
    auto loadProcessor = [this](std::vector<wxString> const& list) { return convertFrequencyListFromStorage_(list); };
    auto saveProcessor = [this](std::vector<wxString> const& list) { return convertFrequencyListToStorage_(list); };

    reportingFrequencyList.setLoadProcessor(loadProcessor);
    reportingFrequencyList.setSaveProcessor(saveProcessor);

    reportingFrequencyListCentred.setLoadProcessor(loadProcessor);
    reportingFrequencyListCentred.setSaveProcessor(saveProcessor);
}

std::vector<wxString> ReportingConfiguration::convertFrequencyListFromStorage_(std::vector<wxString> const& list)
{
    std::vector<uint64_t> freqs;
    for (auto& val : list)
    {
        // Frequencies are unfortunately saved in US format (legacy behavior). We need
        // to manually parse and convert to Hz, then output MHz values in the user's current
        // locale.
        wxStringTokenizer tok(val, ".");
        wxString wholeNumber = tok.GetNextToken();
        wxString fraction = "0";
        if (tok.HasMoreTokens())
        {
            fraction = tok.GetNextToken();
        }

        if (fraction.Length() < 6)
        {
            fraction += wxString('0', 6 - fraction.Length());
        }

        long long hz = 0;
        long long mhz = 0;
        wholeNumber.ToLongLong(&mhz);
        fraction.ToLongLong(&hz);
        freqs.push_back(mhz * 1000000 + hz);
    }

    // Shown in ascending order, whatever order an older config has them in.
    std::sort(freqs.begin(), freqs.end());

    std::vector<wxString> newList;
    for (auto freq : freqs)
    {
        if (reportingFrequencyAsKhz)
        {
            double khzFloat = freq / 1000.0;
            newList.push_back(wxNumberFormatter::ToString(khzFloat, 1));
        }
        else
        {
            double mhzFloat = freq / 1000000.0;
            newList.push_back(wxNumberFormatter::ToString(mhzFloat, 4));
        }
    }

    return newList;
}

std::vector<wxString> ReportingConfiguration::convertFrequencyListToStorage_(std::vector<wxString> const& list)
{
    std::vector<wxString> newList;
    for (auto& val : list)
    {
        // Frequencies are unfortunately saved in US format (legacy behavior). We need
        // to manually parse and convert to Hz, then output MHz values in US format.
        double displayedFreq = 0.0;
        wxNumberFormatter::FromString(val, &displayedFreq);

        // Round to the nearest Hz; most decimal values can't be exactly represented
        // in floating point, so truncating can leave us 1 Hz below what was entered.
        double hzFloat = displayedFreq * (reportingFrequencyAsKhz ? 1000.0 : 1000000.0);
        uint64_t hz = hzFloat > 0 ? std::llround(hzFloat) : 0;
        uint64_t mhzInt = hz / 1000000;
        hz = hz % 1000000;

        wxString newVal = wxString::Format("%" PRIu64 ".%06" PRIu64, mhzInt, hz);
        newList.push_back(newVal);
    }
    return newList;
}

namespace
{
    // Mirrors the offset logic in src/gui/util/FrequencyOps.h's
    // GetModeForFrequency()/GetCentreFreqOffsetHz() (by-band SSB sideband
    // convention, analogActive=false case) - duplicated here rather than
    // included directly since that header pulls in HamlibRigController and
    // its hamlib dependency, which the config library doesn't otherwise need.
    int32_t centreFreqOffsetHzForDialFreq(uint64_t dialFreqHz)
    {
        // Widest 60 meter allocation is 5.250-5.450 MHz per
        // https://en.wikipedia.org/wiki/60-meter_band.
        bool is60MeterBand = dialFreqHz >= 5250000 && dialFreqHz <= 5450000;
        bool isLsb = dialFreqHz < 10000000 && !is60MeterBand;
        return isLsb ? -1500 : 1500;
    }
}

void ReportingConfiguration::migrateFrequencyListToCentred_(wxConfigBase* config)
{
    // First run after the centre-frequency convention was introduced: inherit the
    // user's legacy dial-frequency entries (offset per centreFreqOffsetHzForDialFreq,
    // same convention used to derive reportingFrequencyListCentred's own defaults)
    // into the new centred list, merging with (not replacing) the new defaults.
    // Comparisons and the merged result are kept in raw storage format (period-decimal
    // MHz) to avoid any locale ambiguity - reportingFrequencyList has already been
    // load_()'d by this point, so getWithoutProcessing() holds either its on-disk
    // value or its (storage-format) default, both already in that form.
    std::vector<wxString> migrated = reportingFrequencyListCentred.getDefaultVal();

    for (auto& oldVal : reportingFrequencyList.getWithoutProcessing())
    {
        double mhz = 0.0;
        oldVal.ToCDouble(&mhz);
        uint64_t dialHz = (uint64_t)llround(mhz * 1000000.0);
        int64_t centredHz = (int64_t)dialHz + centreFreqOffsetHzForDialFreq(dialHz);

        bool alreadyPresent = false;
        for (auto& existing : migrated)
        {
            double existingMhz = 0.0;
            existing.ToCDouble(&existingMhz);
            int64_t existingHz = llround(existingMhz * 1000000.0);
            if (existingHz == centredHz)
            {
                alreadyPresent = true;
                break;
            }
        }

        if (!alreadyPresent)
        {
            uint64_t mhzInt = centredHz / 1000000;
            uint64_t hzFrac = centredHz % 1000000;
            migrated.push_back(wxString::Format("%" PRIu64 ".%06" PRIu64, mhzInt, hzFrac));
        }
    }

    reportingFrequencyListCentred.setWithoutProcessing(migrated);

    // Persist immediately so the presence of this key is itself the "already
    // migrated" marker for future runs - no separate version/flag needed.
    save_(config, reportingFrequencyListCentred);
}

void ReportingConfiguration::load(wxConfigBase* config)
{
    // Migration: Save old parameters so that they can be copied over to the new locations.
    auto oldPskEnable = config->ReadBool(wxT("/PSKReporter/Enable"), false); 
    auto oldPskCallsign = config->Read(wxT("/PSKReporter/Callsign"), wxT(""));
    auto oldGridSquare = config->Read(wxT("/PSKReporter/GridSquare"), wxT(""));
    auto oldFreqStr = config->Read(wxT("/PSKReporter/FrequencyHzStr"), wxT("0"));
    reportingEnabled.setDefaultVal(oldPskEnable);
    reportingCallsign.setDefaultVal(oldPskCallsign);
    reportingGridSquare.setDefaultVal(oldGridSquare);
 
    load_(config, reportingEnabled);
    load_(config, reportingCallsign);
    load_(config, reportingGridSquare);
    
    load_(config, pskReporterEnabled);
    
    load_(config, freedvReporterEnabled);
    load_(config, freedvReporterForcedOff);
    load_(config, freedvReporterHostname);
    load_(config, freedvReporterUseTls);
    load_(config, freedvReporterBandFilter);
    load_(config, useMetricDistances);
    load_(config, freedvReporterBandFilterTracksFrequency);
    load_(config, freedvReporterForceReceiveOnly);
    load_(config, freedvReporterBandFilterTracksFreqBand);
    load_(config, freedvReporterBandFilterTracksExactFreq);
    load_(config, freedvReporterStatusText);
    load_(config, freedvReporterRecentStatusTexts);
    
    load_(config, udpReportingEnabled);
    load_(config, udpReportingHostname);
    load_(config, udpReportingPort);

    load_(config, udpBroadcastEnabled);
    load_(config, udpBroadcastAddress);
    load_(config, udpBroadcastPort);

    load_(config, freedvReporterColumnOrder);
    load_(config, freedvReporterColumnVisibility);

    load_(config, freedvReporterEnableMaxIdleFilter);
    load_(config, freedvReporterMaxIdleMinutes);

    load_(config, freedvReporterColumnFilterOperators);
    load_(config, freedvReporterColumnFilterValues);

    load_(config, useUTCForReporting);
    
    // Note: this needs to be loaded before the frequency lists so that
    // we get the values formatted as kHz (if so configured).
    load_(config, reportingFrequencyAsKhz);

    // Legacy list stays untouched (older builds still read it directly), but we
    // always load it here so it's available as a migration source below.
    load_(config, reportingFrequencyList);

    if (!config->HasEntry(reportingFrequencyListCentred.getElementName()))
    {
        // First run after the centre-frequency convention was introduced. Older
        // builds never write or read this key, so they remain unaffected.
        migrateFrequencyListToCentred_(config);
    }
    else
    {
        load_(config, reportingFrequencyListCentred);
    }

    load_(config, manualFrequencyReporting);

    load_(config, freedvReporterTxRowBackgroundColor);
    load_(config, freedvReporterTxRowForegroundColor);
    load_(config, freedvReporterRxRowBackgroundColor);
    load_(config, freedvReporterRxRowForegroundColor);
    load_(config, freedvReporterMsgRowBackgroundColor);
    load_(config, freedvReporterMsgRowForegroundColor);

    load_(config, reportingDirectionAsCardinal);

    load_(config, csvLogFilePath);

    // Set default CSV log file path to Documents/freedv_rx_log.csv if not configured.
    if (csvLogFilePath->IsEmpty())
    {
        wxString defaultPath;
        wxString logFileName = "freedv_rx_log.csv";

#if defined(__linux__)
        // Special logic to force use of XDG_DATA_HOME as wxWidgets doesn't currently
        // provide this in wxStandardPaths.
        wxString xdgDataHome;
        if (!wxGetEnv("XDG_DATA_HOME", &xdgDataHome))
        {
            // Default to $HOME/.local/share per XDG specification.
            wxString home = wxGetHomeDir();
            xdgDataHome = wxString::Format("%s/.local/share", home);
        }

        defaultPath = wxString::Format("%s/freedv", xdgDataHome);
#else
        defaultPath = wxStandardPaths::Get().GetDocumentsDir() + wxFILE_SEP_PATH + "freedv";
#endif // wxCHECK_VERSION(3,1,0)

        // Make folder (including parents as needed)
        wxFileName dn = wxFileName::DirName(defaultPath);
        dn.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

        csvLogFilePath.setWithoutProcessing(defaultPath + wxFILE_SEP_PATH + logFileName);
    }

    // Special load handling for reporting below.
    wxString freqStr = config->Read(reportingFrequency.getElementName(), oldFreqStr);
    reportingFrequency.setWithoutProcessing(atoll(freqStr.ToUTF8()));
}

void ReportingConfiguration::save(wxConfigBase* config)
{ 
    save_(config, reportingEnabled);
    save_(config, reportingCallsign);
    save_(config, reportingGridSquare);
    
    save_(config, pskReporterEnabled);
    
    save_(config, freedvReporterEnabled);
    save_(config, freedvReporterForcedOff);
    save_(config, freedvReporterHostname);
    save_(config, freedvReporterUseTls);
    save_(config, freedvReporterBandFilter);
    save_(config, useMetricDistances);
    save_(config, freedvReporterBandFilterTracksFrequency);
    save_(config, freedvReporterForceReceiveOnly);
    save_(config, freedvReporterBandFilterTracksFreqBand);
    save_(config, freedvReporterBandFilterTracksExactFreq);
    save_(config, freedvReporterStatusText);
    save_(config, freedvReporterRecentStatusTexts);
    
    save_(config, freedvReporterColumnOrder);
    save_(config, freedvReporterColumnVisibility);

    save_(config, freedvReporterEnableMaxIdleFilter);
    save_(config, freedvReporterMaxIdleMinutes);

    save_(config, freedvReporterColumnFilterOperators);
    save_(config, freedvReporterColumnFilterValues);

    save_(config, udpReportingEnabled);
    save_(config, udpReportingHostname);
    save_(config, udpReportingPort);

    save_(config, udpBroadcastEnabled);
    save_(config, udpBroadcastAddress);
    save_(config, udpBroadcastPort);

    save_(config, useUTCForReporting);
    
    save_(config, reportingFrequencyAsKhz);
    save_(config, reportingFrequencyList);
    save_(config, reportingFrequencyListCentred);

    save_(config, manualFrequencyReporting);

    save_(config, freedvReporterTxRowBackgroundColor);
    save_(config, freedvReporterTxRowForegroundColor);
    save_(config, freedvReporterRxRowBackgroundColor);
    save_(config, freedvReporterRxRowForegroundColor);
    save_(config, freedvReporterMsgRowBackgroundColor);
    save_(config, freedvReporterMsgRowForegroundColor);

    save_(config, reportingDirectionAsCardinal);

    save_(config, csvLogFilePath);

    // Special save handling for reporting below.
    wxString tempFreqStr = wxString::Format(wxT("%" PRIu64), reportingFrequency.getWithoutProcessing());
    config->Write(reportingFrequency.getElementName(), tempFreqStr);
}

size_t ReportingConfiguration::SortedFrequencyIndex(std::vector<wxString> const& list, double freq)
{
    size_t index = 0;
    for (; index < list.size(); index++)
    {
        double existing = 0;
        wxNumberFormatter::FromString(list[index], &existing);
        if (existing > freq)
        {
            break;
        }
    }
    return index;
}
