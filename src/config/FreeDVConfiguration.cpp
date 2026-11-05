//==========================================================================
// Name:            FreeDVConfiguration.cpp
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

#include <wx/stdpaths.h>
#include <wx/filename.h>

#include "../defines.h"
#include "FreeDVConfiguration.h"
#include "../freedv_interface.h"

FreeDVConfiguration::FreeDVConfiguration()
    /* First time configuration options */
    : firstTimeUse("/FirstTimeUse", true)
        
    /* Position and size of main window */
    , mainWindowLeft("/MainFrame/left", 20)
    , mainWindowTop("/MainFrame/top", 20)
    // (None saved: see MainFrame::loadConfiguration_() for the default.)
    , mainWindowWidth("/MainFrame/width", -1)
    , mainWindowHeight("/MainFrame/height", -1)

    /* Position and size of Audio Config window */
    , audioConfigWindowLeft("/Windows/AudioConfig/left", -1)
    , audioConfigWindowTop("/Windows/AudioConfig/top", -1)
    , audioConfigWindowWidth("/Windows/AudioConfig/width", -1)
    , audioConfigWindowHeight("/Windows/AudioConfig/height", -1)
        
    /* Position and size of FreeDV Reporter */
    , reporterWindowLeft("/Windows/FreeDVReporter/left", 20)
    , reporterWindowTop("/Windows/FreeDVReporter/top", 20)
    , reporterWindowWidth("/Windows/FreeDVReporter/width", -1)
    , reporterWindowHeight("/Windows/FreeDVReporter/height", -1)
    , reporterWindowVisible("/Windows/FreeDVReporter/visible", false)
    , msgEditDialogWidth("/Windows/FreeDVReporter/msgEditDialogWidth", -1)
    , reporterWindowCurrentSort("/Windows/FreeDVReporter/currentSort", 5) /* FREQUENCY_COL */
    , reporterWindowCurrentSortDirection("/Windows/FreeDVReporter/currentSortDirection", true)
        
    /* Current tab view */
    , currentNotebookTab("/MainFrame/rxNbookCtrl", 0)
        
    /* Misc. audio settings */
    , fifoSizeMs("/Audio/fifoSize_ms", (int)FIFO_SIZE)
    , transmitLevel("/Audio/transmitLevel", 0)
    , tuneLevel("/Audio/tuneLevel", 0)
    , txAttenByBand("/Audio/transmitLevelByBand", {})
    , tuneAttenByBand("/Audio/tuneLevelByBand", {})
        
    /* Recording settings */
    , playFileToMicInPath("/File/playFileToMicInPath", wxT(""))
    , playFileFromRadioPath("/File/playFileFromRadioPath", wxT(""))
        
    , enableSpaceBarForPTT("/Rig/EnableSpacebarForPTT", true)
    , pttKeyCode("/Rig/PttKeyCode", WXK_SPACE)
    , pttMomentaryMode("/Rig/PttMomentaryMode", false)

    , voiceKeyerWaveFilePath("/VoiceKeyer/WaveFilePath", wxT(""))
    , voiceKeyerWaveFile("/VoiceKeyer/WaveFile", wxT("voicekeyer.wav"))
    , voiceKeyerRxPause("/VoiceKeyer/RxPause", 10)
    , voiceKeyerRepeats("/VoiceKeyer/Repeats", 5)
        
    , halfDuplexMode("/Rig/HalfDuplex", true)
        
    , quickRecordRawPath("/QuickRecord/SavePath", wxT(""))
    , quickRecordDecodedPath("/QuickRecord/SaveDecodedPath", wxT(""))
        
    , debugConsoleEnabled("/Debug/console", false)
        
    , snrSlow("/Audio/snrSlow", false)
        
    , debugVerbose("/Debug/verbose", false)
    , apiVerbose("/Debug/APIverbose", false)
        
    , waterfallColor("/Waterfall/Color", 0)
    , statsResetTimeSecs("/Stats/ResetTime", 10)
        
    , currentSpectrumAveraging("/Plot/Spectrum/CurrentAveraging", 0)
    
    , experimentalFeatures("/ExperimentalFeatures", false)
    , tabLayout("/MainFrame/TabLayout", wxT(""))

    , monitorVoiceKeyerAudio("/Monitor/VoiceKeyerAudio", false)
    , monitorVoiceKeyerAudioVol("/Monitor/VoiceKeyerAudioVol", 0)
    , monitorTxAudio("/Monitor/TransmitAudio", false)
    , monitorTxAudioVol("/Monitor/TransmitAudioVol", 0)

    , txRxDelayMilliseconds("/Audio/TxRxDelayMilliseconds", 0)

    , reportingUserMsgColWidth("/Windows/FreeDVReporter/reportingUserMsgColWidth", 130)
        
    , showDecodeStats("/Debug/showDecodeStats", false)
    , autoStartOnLaunch("/Modem/autoStartOnLaunch", false)
{
    // empty
}

void FreeDVConfiguration::load(wxConfigBase* config)
{
    // Settings should be read back exactly as they were saved; see WxWidgetsConfigStore.cpp.
    // (This covers the settings that are read directly rather than through load_().)
    config->SetExpandEnvVars(false);

    audioConfiguration.load(config);
    filterConfiguration.load(config);
    rigControlConfiguration.load(config);
    reportingConfiguration.load(config);
    
    load_(config, firstTimeUse);
    
    load_(config, mainWindowLeft);
    load_(config, mainWindowTop);
    load_(config, mainWindowWidth);
    load_(config, mainWindowHeight);

    load_(config, audioConfigWindowLeft);
    load_(config, audioConfigWindowTop);
    load_(config, audioConfigWindowWidth);
    load_(config, audioConfigWindowHeight);
    
    load_(config, reporterWindowLeft);
    load_(config, reporterWindowTop);
    load_(config, reporterWindowWidth);
    load_(config, reporterWindowHeight);
    load_(config, reporterWindowVisible);
    load_(config, msgEditDialogWidth);
    load_(config, reporterWindowCurrentSort);
    load_(config, reporterWindowCurrentSortDirection);
    
    load_(config, currentNotebookTab);
    
    load_(config, fifoSizeMs);
    load_(config, transmitLevel);
    load_(config, tuneLevel);
    
    load_(config, playFileToMicInPath);
    load_(config, playFileFromRadioPath);
    
    load_(config, enableSpaceBarForPTT);
    load_(config, pttKeyCode);
    load_(config, pttMomentaryMode);

    load_(config, voiceKeyerWaveFilePath);
    load_(config, voiceKeyerWaveFile);
    
    auto wxStandardPathObj = wxStandardPaths::Get();
    auto documentsDir = wxStandardPathObj.GetDocumentsDir();
    
    if (voiceKeyerWaveFilePath == "")
    {
        // Migrate from previous versions where voiceKeyerWaveFilePath wasn't used.
        wxString tmp = voiceKeyerWaveFile;
        wxString path;
        wxString name;
        wxString ext;
        
        wxFileName::SplitPath(tmp, &path, &name, &ext);
        if (ext != "")
        {
            name = name + "." + ext;
        }
        
        if (path == "")
        {
            // Default path to the Documents folder if one isn't provided
            // (i.e. in the case of the old VK filename default)
            path = documentsDir;
        }
        
        voiceKeyerWaveFilePath = path;
        voiceKeyerWaveFile = name;
    }
    else
    {
        // Make sure path isn't in the filename
        wxString waveFile = voiceKeyerWaveFile.get();
        waveFile.Replace(voiceKeyerWaveFilePath.get(), "");
        waveFile.Replace("/", "");
        waveFile.Replace("\\", "");
        voiceKeyerWaveFile = waveFile;
    }

    load_(config, voiceKeyerRxPause);
    load_(config, voiceKeyerRepeats);
    
    load_(config, halfDuplexMode);
        
    load_(config, debugConsoleEnabled);
    
    load_(config, snrSlow);
    
    load_(config, debugVerbose);
    load_(config, apiVerbose);
    
    load_(config, waterfallColor);
    
    load_(config, statsResetTimeSecs);
    
    load_(config, currentSpectrumAveraging);
    
    load_(config, monitorVoiceKeyerAudio);
    load_(config, monitorTxAudio);
    load_(config, monitorVoiceKeyerAudioVol);
    load_(config, monitorTxAudioVol);
    
    quickRecordRawPath.setDefaultVal(documentsDir);
    quickRecordDecodedPath.setDefaultVal(documentsDir);
    load_(config, quickRecordRawPath);
    load_(config, quickRecordDecodedPath);
    
    load_(config, experimentalFeatures);
    load_(config, tabLayout);

    load_(config, txRxDelayMilliseconds);

    load_(config, reportingUserMsgColWidth);
    
    load_(config, showDecodeStats);
    load_(config, autoStartOnLaunch);

    load_(config, txAttenByBand);
    load_(config, tuneAttenByBand);
}

void FreeDVConfiguration::save(wxConfigBase* config)
{
    audioConfiguration.save(config);
    filterConfiguration.save(config);
    rigControlConfiguration.save(config);
    reportingConfiguration.save(config);
    
    save_(config, firstTimeUse);
    
    save_(config, mainWindowLeft);
    save_(config, mainWindowTop);
    save_(config, mainWindowWidth);
    save_(config, mainWindowHeight);

    save_(config, audioConfigWindowLeft);
    save_(config, audioConfigWindowTop);
    save_(config, audioConfigWindowWidth);
    save_(config, audioConfigWindowHeight);
    
    save_(config, reporterWindowLeft);
    save_(config, reporterWindowTop);
    save_(config, reporterWindowWidth);
    save_(config, reporterWindowHeight);
    save_(config, reporterWindowVisible);
    save_(config, msgEditDialogWidth);
    save_(config, reporterWindowCurrentSort);
    save_(config, reporterWindowCurrentSortDirection);
    
    save_(config, currentNotebookTab);
    
    save_(config, fifoSizeMs);
    save_(config, transmitLevel);
    save_(config, tuneLevel);
    
    save_(config, playFileToMicInPath);
    save_(config, playFileFromRadioPath);
    
    save_(config, enableSpaceBarForPTT);
    save_(config, pttKeyCode);
    save_(config, pttMomentaryMode);

    save_(config, voiceKeyerWaveFilePath);
    save_(config, voiceKeyerWaveFile);
    save_(config, voiceKeyerRxPause);
    save_(config, voiceKeyerRepeats);
    
    save_(config, halfDuplexMode);
    
    save_(config, quickRecordRawPath);
    save_(config, quickRecordDecodedPath);
    
    save_(config, debugConsoleEnabled);
    
    save_(config, snrSlow);
    
    save_(config, debugVerbose);
    save_(config, apiVerbose);
    
    save_(config, waterfallColor);
    
    save_(config, statsResetTimeSecs);
    
    save_(config, currentSpectrumAveraging);
    
    save_(config, experimentalFeatures);
    save_(config, tabLayout);

    save_(config, monitorVoiceKeyerAudio);
    save_(config, monitorTxAudio);
    save_(config, monitorVoiceKeyerAudioVol);
    save_(config, monitorTxAudioVol);

    save_(config, txRxDelayMilliseconds);

    save_(config, reportingUserMsgColWidth);
    
    save_(config, showDecodeStats);
    save_(config, autoStartOnLaunch);

    save_(config, txAttenByBand);
    save_(config, tuneAttenByBand);

    config->Flush();
}
