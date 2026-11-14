//==========================================================================
// Name:            FreeDVConfiguration.h
// Purpose:         Implements the configuration for FreeDV
// Created:         July 1, 2023
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

#ifndef FREEDV_CONFIGURATION_H
#define FREEDV_CONFIGURATION_H

#include <inttypes.h>
#include <wx/string.h>
#include "WxWidgetsConfigStore.h"
#include "ConfigurationDataElement.h"
#include "AudioConfiguration.h"
#include "FilterConfiguration.h"
#include "RigControlConfiguration.h"
#include "ReportingConfiguration.h"

class FreeDVConfiguration : public WxWidgetsConfigStore
{
public:
    FreeDVConfiguration();
    virtual ~FreeDVConfiguration() = default;
    
    AudioConfiguration audioConfiguration;
    FilterConfiguration filterConfiguration;
    RigControlConfiguration rigControlConfiguration;
    ReportingConfiguration reportingConfiguration;
    
    ConfigurationDataElement<bool> firstTimeUse;
    
    ConfigurationDataElement<long> mainWindowLeft;
    ConfigurationDataElement<long> mainWindowTop;
    ConfigurationDataElement<long> mainWindowWidth;
    ConfigurationDataElement<long> mainWindowHeight;

    ConfigurationDataElement<long> audioConfigWindowLeft;
    ConfigurationDataElement<long> audioConfigWindowTop;
    ConfigurationDataElement<long> audioConfigWindowWidth;
    ConfigurationDataElement<long> audioConfigWindowHeight;
    
    ConfigurationDataElement<long> reporterWindowLeft;
    ConfigurationDataElement<long> reporterWindowTop;
    ConfigurationDataElement<long> reporterWindowWidth;
    ConfigurationDataElement<long> reporterWindowHeight;
    ConfigurationDataElement<bool> reporterWindowVisible;
    ConfigurationDataElement<long> msgEditDialogWidth;
    ConfigurationDataElement<int> reporterWindowCurrentSort;
    ConfigurationDataElement<bool> reporterWindowCurrentSortDirection;
    
    ConfigurationDataElement<long> currentNotebookTab;
    
    ConfigurationDataElement<int> fifoSizeMs;
    ConfigurationDataElement<int> transmitLevel;
    ConfigurationDataElement<int> tuneLevel;
    ConfigurationDataElement<std::map<wxString, int>> txAttenByBand;
    ConfigurationDataElement<std::map<wxString, int>> tuneAttenByBand;
    
    ConfigurationDataElement<wxString> playFileToMicInPath;
    ConfigurationDataElement<wxString> playFileFromRadioPath;
    
    ConfigurationDataElement<bool> enableSpaceBarForPTT;
    ConfigurationDataElement<int> pttKeyCode;
    ConfigurationDataElement<bool> pttMomentaryMode;

    ConfigurationDataElement<wxString> voiceKeyerWaveFilePath;
    ConfigurationDataElement<wxString> voiceKeyerWaveFile;
    ConfigurationDataElement<int> voiceKeyerRxPause;
    ConfigurationDataElement<int> voiceKeyerRepeats;
    
    ConfigurationDataElement<bool> halfDuplexMode;
    
    ConfigurationDataElement<wxString> quickRecordRawPath;
    ConfigurationDataElement<wxString> quickRecordDecodedPath;
    
    ConfigurationDataElement<bool> debugConsoleEnabled; // note: Windows only
    
    ConfigurationDataElement<bool> snrSlow;
    
    ConfigurationDataElement<bool> debugVerbose;
    
    ConfigurationDataElement<int> waterfallColor;
    ConfigurationDataElement<unsigned int> statsResetTimeSecs;
        
    ConfigurationDataElement<int> currentSpectrumAveraging;
    
    ConfigurationDataElement<bool> experimentalFeatures;
    ConfigurationDataElement<wxString> tabLayout;

    ConfigurationDataElement<bool> monitorVoiceKeyerAudio;
    ConfigurationDataElement<float> monitorVoiceKeyerAudioVol;
    ConfigurationDataElement<bool> monitorTxAudio;
    ConfigurationDataElement<float> monitorTxAudioVol;

    ConfigurationDataElement<int> txRxDelayMilliseconds;

    ConfigurationDataElement<int> reportingUserMsgColWidth;
    
    ConfigurationDataElement<bool> showDecodeStats;
    
    ConfigurationDataElement<bool> autoStartOnLaunch;

    virtual void load(wxConfigBase* config) override;
    virtual void save(wxConfigBase* config) override;
};

#endif // FREEDV_CONFIGURATION_H
