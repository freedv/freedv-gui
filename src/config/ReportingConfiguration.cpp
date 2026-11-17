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
#include <cmath>

#include "../defines.h"
#include "reporting/FreeDVReporter.h"
#include "ReportingConfiguration.h"

ReportingConfiguration::ReportingConfiguration()
    : reportingFreeTextString("/Data/CallSign", _(""))
        
    , reportingEnabled("/Reporting/Enable", false)
    , reportingCallsign("/Reporting/Callsign", _(""))
    , reportingGridSquare("/Reporting/GridSquare", _(""))
        
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
    , freedvReporterStatusText("/Reporting/FreeDV/StatusText", _(""))
    , freedvReporterRecentStatusTexts("/Reporting/FreeDV/RecentStatusTexts", {})
    
    , freedvReporterColumnOrder("/Reporting/FreeDV/ColumnOrder", { }) /* empty means default ordering */
    , freedvReporterColumnVisibility("/Reporting/FreeDV/ColumnVisibility", { })

    , freedvReporterEnableMaxIdleFilter("/Reporting/FreeDV/EnableMaxIdleFilter", false)
    , freedvReporterMaxIdleMinutes("/Reporting/FreeDV/MaxIdleMinutes", 120)
    , freedvReporterColumnFilterOperators("/Reporting/FreeDV/ColumnFilterOperators", {})
    , freedvReporterColumnFilterValues("/Reporting/FreeDV/ColumnFilterValues", {})

    , udpReportingEnabled("/Reporting/UDP/Enable", false)
    , udpReportingHostname("/Reporting/UDP/Hostname", _("127.0.0.1"))
    , udpReportingPort("/Reporting/UDP/Port", 2237)

    , udpBroadcastEnabled("/Reporting/UDPBroadcast/Enable", false)
    , udpBroadcastAddress("/Reporting/UDPBroadcast/Address", _("224.0.0.1"))
    , udpBroadcastPort("/Reporting/UDPBroadcast/Port", 7177)

    , useUTCForReporting("/CallsignList/UseUTCTime", false)

    , reportingFrequencyList("/Reporting/FrequencyList", {
        _("1.8700"),
        _("3.6250"),
        _("3.6430"),
        _("3.6930"),
        _("3.6970"),
        _("3.8030"),
        _("5.4035"),
        _("5.3685"),
        _("7.1770"),
        _("7.1970"),
        _("14.2360"),
        _("14.2400"),
        _("18.1180"),
        _("21.3130"),
        _("24.9330"),
        _("28.3300"),
        _("28.7200"),
        _("10489.6400"),
    })
    , freedvReporterTxRowBackgroundColor("/Reporting/FreeDV/TxRowBackgroundColor", "#fc4500")
    , freedvReporterTxRowForegroundColor("/Reporting/FreeDV/TxRowForegroundColor", "#000000")
    , freedvReporterRxRowBackgroundColor("/Reporting/FreeDV/RxRowBackgroundColor", "#379baf")
    , freedvReporterRxRowForegroundColor("/Reporting/FreeDV/RxRowForegroundColor", "#000000")
    , freedvReporterMsgRowBackgroundColor("/Reporting/FreeDV/MsgRowBackgroundColor", "#E58BE5")
    , freedvReporterMsgRowForegroundColor("/Reporting/FreeDV/MsgRowForegroundColor", "#000000")
        
    , reportingFrequencyAsKhz("/Reporting/FrequencyAsKHz", false)
    , reportingDirectionAsCardinal("/Reporting/DirectionAsCardinal", false)
    , csvLogFilePath("/Reporting/CSV/LogFilePath", _(""))
{
    // Ports, whatever's typed into Settings or written into the config file.
    udpReportingPort.setLimits(1, 65535);
    udpBroadcastPort.setLimits(1, 65535);

    // Special handling for the frequency list to properly handle locales
    reportingFrequencyList.setLoadProcessor([this](std::vector<wxString> const& list) {
        std::vector<wxString> newList;
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

            // Only what FreeDV writes is taken: digits on each side. Anything
            // else written into the file (a sign, a word, a number too big
            // to be a frequency) is dropped rather than turned into one, and
            // digits past the sixth after the point (below 1 Hz) are ignored.
            unsigned long long mhz = 0;
            unsigned long long hz = 0;
            fraction.Truncate(6);
            if (!wholeNumber.IsNumber() || !fraction.IsNumber() ||
                wholeNumber.StartsWith("-") || fraction.StartsWith("-") ||
                !wholeNumber.ToULongLong(&mhz) || !fraction.ToULongLong(&hz) ||
                mhz > MAX_FREQUENCY_HZ / 1000000)
            {
                continue;
            }
            uint64_t freq = mhz * 1000000 + hz;

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
    });

    reportingFrequencyList.setSaveProcessor([this](std::vector<wxString> const& list) {
        std::vector<wxString> newList;
        for (auto& val : list)
        {
            // Frequencies are unfortunately saved in US format (legacy behavior). We need 
            // to manually parse and convert to Hz, then output MHz values in US format.
            // (An entry that isn't a frequency is saved as 0, rather than as
            // one made up from it.)
            uint64_t hz = DisplayedFrequencyToHz(val, reportingFrequencyAsKhz);
            uint64_t mhzInt = hz / 1000000;
            hz = hz % 1000000;

            wxString newVal = wxString::Format("%" PRIu64 ".%06" PRIu64, mhzInt, hz);
            newList.push_back(newVal);
        }
        return newList;
    });
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
    
    load_(config, reportingFreeTextString);
 
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
    
    // Note: this needs to be loaded before the frequency list so that
    // we get the values formatted as kHz (if so configured).
    load_(config, reportingFrequencyAsKhz);
    
    load_(config, reportingFrequencyList);
    
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
    save_(config, reportingFreeTextString);
 
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

uint64_t ReportingConfiguration::DisplayedFrequencyToHz(wxString const& text, bool asKhz)
{
    double value = 0;
    if (!wxNumberFormatter::FromString(text.Strip(wxString::both), &value) || !std::isfinite(value))
    {
        return 0;
    }

    // Rounded to the nearest Hz: most decimal values can't be represented
    // exactly in floating point, so truncating can leave one 1 Hz below what
    // was entered.
    double hz = value * (asKhz ? 1000.0 : 1000000.0);
    if (!(hz >= 0.5) || hz > MAX_FREQUENCY_HZ)
    {
        return 0;
    }
    return (uint64_t)std::llround(hz);
}
