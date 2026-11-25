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

#ifndef REPORTING_CONFIGURATION_H
#define REPORTING_CONFIGURATION_H

#include "WxWidgetsConfigStore.h"
#include "ConfigurationDataElement.h"

class ReportingConfiguration : public WxWidgetsConfigStore
{
public:
    ReportingConfiguration();
    virtual ~ReportingConfiguration() = default;
    
    ConfigurationDataElement<bool> reportingEnabled;
    ConfigurationDataElement<wxString> reportingCallsign;
    ConfigurationDataElement<wxString> reportingGridSquare;
    
    // NOTE: this needs special handling for load/save as it's a string on disk but
    // uint64_t inside the FreeDV application.
    ConfigurationDataElement<uint64_t> reportingFrequency; 
    
    ConfigurationDataElement<bool> manualFrequencyReporting;
    
    ConfigurationDataElement<bool> pskReporterEnabled;
    
    ConfigurationDataElement<bool> freedvReporterEnabled;
    ConfigurationDataElement<bool> freedvReporterForcedOff;
    ConfigurationDataElement<wxString> freedvReporterHostname;
    ConfigurationDataElement<bool> freedvReporterUseTls;
    ConfigurationDataElement<int> freedvReporterBandFilter;
    ConfigurationDataElement<bool> useMetricDistances;
    ConfigurationDataElement<bool> freedvReporterBandFilterTracksFrequency;
    ConfigurationDataElement<bool> freedvReporterForceReceiveOnly;
    ConfigurationDataElement<bool> freedvReporterBandFilterTracksFreqBand;
    ConfigurationDataElement<bool> freedvReporterBandFilterTracksExactFreq;
    ConfigurationDataElement<wxString> freedvReporterStatusText;
    ConfigurationDataElement<std::vector<wxString> > freedvReporterRecentStatusTexts;
    
    ConfigurationDataElement<std::vector<int> > freedvReporterColumnOrder;
    ConfigurationDataElement<std::vector<bool> > freedvReporterColumnVisibility;

    ConfigurationDataElement<bool> freedvReporterEnableMaxIdleFilter;
    ConfigurationDataElement<int> freedvReporterMaxIdleMinutes;

    ConfigurationDataElement<std::vector<int> > freedvReporterColumnFilterOperators;
    ConfigurationDataElement<std::vector<wxString> > freedvReporterColumnFilterValues;

    ConfigurationDataElement<bool> udpReportingEnabled;
    ConfigurationDataElement<wxString> udpReportingHostname;
    ConfigurationDataElement<int> udpReportingPort;

    ConfigurationDataElement<bool> udpBroadcastEnabled;
    ConfigurationDataElement<wxString> udpBroadcastAddress;
    ConfigurationDataElement<int> udpBroadcastPort;

    ConfigurationDataElement<bool> useUTCForReporting;
    
    ConfigurationDataElement<std::vector<wxString> > reportingFrequencyList;
    ConfigurationDataElement<std::vector<wxString> > reportingFrequencyListCentred;

    ConfigurationDataElement<wxString> freedvReporterTxRowBackgroundColor;
    ConfigurationDataElement<wxString> freedvReporterTxRowForegroundColor;
    ConfigurationDataElement<wxString> freedvReporterRxRowBackgroundColor;
    ConfigurationDataElement<wxString> freedvReporterRxRowForegroundColor;
    ConfigurationDataElement<wxString> freedvReporterMsgRowBackgroundColor;
    ConfigurationDataElement<wxString> freedvReporterMsgRowForegroundColor;
    
    ConfigurationDataElement<bool> reportingFrequencyAsKhz;
    ConfigurationDataElement<bool> reportingDirectionAsCardinal;

    ConfigurationDataElement<wxString> csvLogFilePath;

    virtual void load(wxConfigBase* config) override;
    virtual void save(wxConfigBase* config) override;

    // Where a frequency (in the list's units, MHz or kHz) belongs in a
    // list kept in ascending order: the index of the first entry above it.
    static size_t SortedFrequencyIndex(std::vector<wxString> const& list, double freq);

    // The highest frequency FreeDV takes, in Hz: well past any radio, but
    // short of where a uint64_t of Hz (or a double's precision) runs out.
    static constexpr double MAX_FREQUENCY_HZ = 1e12;

    // The frequency, in Hz, that text gives in the units frequencies are
    // shown in (kHz with asKhz, otherwise MHz), or 0 if it isn't one: not a
    // number, not above 0, or beyond MAX_FREQUENCY_HZ. Anything typed by a
    // user, or read back from a list of them, goes through this before it
    // becomes a frequency, rather than having a made-up one cast out of it.
    static uint64_t DisplayedFrequencyToHz(wxString const& text, bool asKhz);

private:
    std::vector<wxString> convertFrequencyListFromStorage_(std::vector<wxString> const& list);
    std::vector<wxString> convertFrequencyListToStorage_(std::vector<wxString> const& list);
    void migrateFrequencyListToCentred_(wxConfigBase* config);
};

#endif // REPORTING_CONFIGURATION_H
