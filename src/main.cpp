//==========================================================================
// Name:            main.cpp
//
// Purpose:         FreeDV main()
// Created:         Apr. 9, 2012
// Authors:         David Rowe, David Witten
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

#include <csignal>
#include <algorithm>
#include <inttypes.h>
#include <time.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <deque>
#include <random>
#include <chrono>
#include <climits>
#include <mutex>
#include <wx/cmdline.h>
#include <wx/stdpaths.h>
#include <wx/uiaction.h>

#if wxCHECK_VERSION(3,2,0)
#include <wx/uilocale.h>
#endif // wxCHECK_VERSION(3,2,0)

#include "defines.h"
#include "git_version.h"
#include "build_date.h"
#include "main.h"
#include "os/os_interface.h"
#include "freedv_interface.h"
#include "audio/AudioEngineFactory.h"
#include "codec2_fdmdv.h"
#include "pipeline/TxRxThread.h"
#include "reporting/pskreporter.h"
#include "reporting/FreeDVReporter.h"
#include "reporting/CsvReporter.h"
#include "reporting/UdpReporter.h"

#include "logging/WSJTXNetworkLogger.h"

#include "gui/dialogs/dlg_options.h"
#include "gui/dialogs/dlg_filter.h"
#include "gui/dialogs/dlg_easy_setup.h"
#include "gui/dialogs/freedv_reporter.h"
#include "gui/util/WindowPositionRestore.h"
#include "gui/util/TabLayoutSerializer.h"

#include "util/logging/ulog.h"
#include "util/audio_spin_mutex.h"

#if defined(__WXGTK__) && defined(HAS_GTK3)
#include <gtk/gtk.h>
#endif // defined(__WXGTK__) && defined(HAS_GTK3)

#include "rade_api.h"

using namespace std::chrono_literals;
using namespace std::placeholders;

#define wxUSE_FILEDLG   1
#define wxUSE_LIBPNG    1
#define wxUSE_LIBJPEG   1
#define wxUSE_GIF       1
#define wxUSE_PCX       1
#define wxUSE_LIBTIFF   1

extern "C" {
    extern void golay23_init(void);
}


//-------------------------------------------------------------------
// Bunch of globals used for communication with sound card call
// back functions
// ------------------------------------------------------------------

// freedv states
int                 g_Nc;
int                 g_mode;

FreeDVInterface     freedvInterface;
std::shared_ptr<TxRxThread> m_txThread;
std::shared_ptr<TxRxThread> m_rxThread;
float               g_pwr_scale;
int                 g_clip;
int                 g_freedv_verbose;
std::atomic<bool>   g_queueResync;

// test Frames
int                 g_testFrames;
int                 g_test_frame_sync_state;
int                 g_test_frame_count;
std::atomic<int>    g_channel_noise;
std::atomic<int>    g_resyncs;
float               g_sig_pwr_av = 0.0;
short              *g_error_hist, *g_error_histn;
std::atomic<float>    g_tone_phase;

// time averaged magnitude spectrum used for waterfall and spectrum display
GenericFIFO<float>  g_avmag(MODEM_STATS_NSPEC * 10 / DT); // 1s worth
float               g_avmag_waterfall[MODEM_STATS_NSPEC];
float               g_avmag_spectrum[MODEM_STATS_NSPEC];

// TX level for attenuation
int g_txLevel = 0;
std::atomic<float> g_txLevelScale;
int g_tuneLevel = 0;
std::atomic<float> g_tuneLevelScale;

// GUI controls that affect rx and tx processes
std::atomic<int>    g_SquelchActive;
std::atomic<float>  g_SquelchLevel;
std::atomic<int>    g_analog;
std::atomic<bool>   g_tx;
float g_snr;
std::atomic<bool>  g_half_duplex;
std::atomic<bool>  g_voice_keyer_tx;
std::atomic<bool>  g_agcEnabled;

// In half duplex, RX (including the waterfall/spectrum) stays muted until this
// steady_clock time (in ns) after TX ends, so that we don't display or decode
// audio from the radio while it's still transmitting. See
// MainFrame::onRigPttChange_().
std::atomic<int64_t> g_rxMutedUntilNs(0);
std::atomic<int64_t> g_rxInputLatencyUs(0);

// Copies of configuration values read by the audio threads. The configuration
// elements themselves aren't thread-safe, so the UI thread must update these
// whenever the corresponding setting changes.
std::atomic<bool>  g_monitorTxAudio;
std::atomic<bool>  g_monitorVoiceKeyerAudio;
std::atomic<float> g_monitorTxAudioVol;
std::atomic<float> g_monitorVoiceKeyerAudioVol;
std::atomic<int>   g_noiseSNR;
std::atomic<bool>  g_noiseReductionEnabled;

std::atomic<bool>  g_bwExpandEnabled;
// sending and receiving Call Sign data
std::atomic<GenericFIFO<short>*> g_txDataInFifo;
std::atomic<GenericFIFO<short>*> g_rxDataOutFifo;

// tx/rx processing states
std::atomic<int>                 g_State, g_prev_State;
paCallBackData     *g_rxUserdata;
std::atomic<int>                 g_dump_timing;
std::atomic<int>                 g_dump_fifo_state;
time_t              g_sync_time;

// FIFOs used for plotting waveforms
constexpr int PLOT_BUF_MULTIPLIER=8;
GenericFIFO<short>  g_plotDemodInFifo(PLOT_BUF_MULTIPLIER*WAVEFORM_PLOT_BUF);
GenericFIFO<short>  g_plotSpeechOutFifo(PLOT_BUF_MULTIPLIER*WAVEFORM_PLOT_BUF);
GenericFIFO<short>  g_plotSpeechInFifo(PLOT_BUF_MULTIPLIER*WAVEFORM_PLOT_BUF);

// Soundcard config
std::atomic<int>         g_nSoundCards;

// PortAudio over/underflow counters

std::atomic<int>    g_infifo1_full;
std::atomic<int>    g_outfifo1_empty;
std::atomic<int>    g_infifo2_full;
std::atomic<int>    g_outfifo2_empty;
std::atomic<int>         g_AEstatus1[4];
std::atomic<int>         g_AEstatus2[4];

// playing and recording from sound files

extern std::atomic<SNDFILE*> g_sfPlayFile;
extern std::unique_ptr<VoiceKeyerMemoryReader> g_sfPlayFileReader;
extern std::atomic<bool>                g_playFileToMicIn;
extern std::atomic<bool>   g_loopPlayFileToMicIn;
extern int                 g_playFileToMicInEventId;

extern std::atomic<SNDFILE*>     g_sfRecFile;
extern std::atomic<bool>         g_recFileFromRadio;
extern std::atomic<unsigned int> g_recFromRadioSamples;
extern int                 g_recFileFromRadioEventId;
extern int                 g_recFileFromDecoderEventId;

extern std::atomic<SNDFILE*> g_sfPlayFileFromRadio;
extern std::atomic<bool>                g_playFileFromRadio;
extern std::atomic<int>    g_sfFs;
extern std::atomic<int>    g_sfTxFs;
extern std::atomic<bool>   g_loopPlayFileFromRadio;
extern int                 g_playFileFromRadioEventId;

extern std::atomic<SNDFILE*>            g_sfRecFileFromModulator;
extern std::atomic<bool>                g_recFileFromModulator;
extern int                 g_recFileFromModulatorEventId;

extern std::atomic<SNDFILE*> g_sfRecMicFile;
extern std::atomic<bool>   g_recFileFromMic;
extern std::atomic<bool>   g_recVoiceKeyerFile;

extern std::atomic<SNDFILE*> g_sfRecDecoderFile;
extern std::atomic<bool> g_recFileFromDecoder;

wxWindow           *g_parent;

// Click to tune rx and tx frequency offset states
std::atomic<float>  g_RxFreqOffsetHz;
std::atomic<float>  g_TxFreqOffsetHz;

// experimental mutex to make sound card callbacks mutually exclusive
// TODO: review code and see if we need this any more, as fifos should
// now be thread safe

wxMutex g_mutexProtectingCallbackData(wxMUTEX_RECURSIVE);

// Protects the lifetime of the SNDFILE handles read by PlaybackStep
// (g_sfPlayFile and g_sfPlayFileFromRadio). Kept separate from
// g_mutexProtectingCallbackData so that slow file reads/decodes on the
// playback thread can't stall the UI thread. If both are needed, take
// g_mutexProtectingCallbackData first.
std::mutex g_mutexProtectingPlayFiles;

// End of TX state control
std::atomic<bool> endingTx;

// Running state
std::atomic<bool> isModemRunning;

// Option test file to log samples

FILE *ftest;

// Config file management
wxConfigBase *pConfig = NULL;

// Name used for the separate state-store config object (distinct from the main
// app config so the last-used path is readable regardless of which backend is
// active).  On Windows this becomes HKCU\Software\CODEC2-Project\FreeDV-State;
// on macOS/Linux it becomes a file in the per-user config directory.
static const wxChar* const FREEDV_STATE_APP_NAME    = wxT("FreeDV-State");
static const wxChar* const FREEDV_VENDOR_NAME = wxT("CODEC2-Project");
static const wxChar* const LAST_USED_CONFIG_KEY     = wxT("/LastUsedConfigFile");

wxString getLastUsedConfigPath()
{
    wxConfig stateConfig(FREEDV_STATE_APP_NAME, FREEDV_VENDOR_NAME);
    wxString path;
    stateConfig.Read(LAST_USED_CONFIG_KEY, &path, wxEmptyString);
    return path;
}

void saveLastUsedConfigPath(const wxString& path)
{
    wxConfig stateConfig(FREEDV_STATE_APP_NAME, FREEDV_VENDOR_NAME);
    stateConfig.Write(LAST_USED_CONFIG_KEY, path);
    stateConfig.Flush();
}

void clearLastUsedConfigPath()
{
    wxConfig stateConfig(FREEDV_STATE_APP_NAME, FREEDV_VENDOR_NAME);
    stateConfig.Write(LAST_USED_CONFIG_KEY, wxEmptyString);
    stateConfig.Flush();
}

// Unit test management
wxString testName;
wxString utFreeDVMode;
wxString utTxFile;
wxString utTxOutFile;
wxString utRxFile;
wxString utRxOutFile;
std::string utTxFeatureFile;
std::string utRxFeatureFile;
long utTxTimeSeconds;
long utTxAttempts;

// WxWidgets - initialize the application

IMPLEMENT_APP(MainApp);

std::mutex logMutex;
static void LogLockFunction_(bool lock, void *)
{
    if (lock)
    {
        logMutex.lock();
    }
    else
    {
        logMutex.unlock();
    }
}

template<int soundCardId, bool isOut>
void MainFrame::handleAudioDeviceChange_(std::string const& newDeviceName)
{
    wxString devName = wxString::FromUTF8(newDeviceName.c_str());
    if (soundCardId == 1)
    {
        if (isOut)
        {
            wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName = devName;
        }
        else
        {
            wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName = devName;
        }
    }
    else if (soundCardId == 2)
    {
        if (isOut)
        {
            wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName = devName;
        }
        else
        {
            wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName = devName;
        }
    }
    wxGetApp().appConfiguration.save(pConfig);
}

void MainApp::UnitTest_()
{
    SetThreadName("UnitTest");

    // List audio devices
    auto engine = AudioEngineFactory::GetAudioEngine();
    engine->start();
    for (auto& dev : engine->getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_IN))
    {
        log_info("Input audio device: %s (ID %d, sample rate %d, valid channels: %d-%d)", (const char*)dev.name.ToUTF8(), dev.deviceId,  dev.defaultSampleRate, dev.minChannels, dev.maxChannels);
    }
    for (auto& dev : engine->getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_OUT))
    {
        log_info("Output audio device: %s (ID %d, sample rate %d, valid channels: %d-%d)", (const char*)dev.name.ToUTF8(), dev.deviceId,  dev.defaultSampleRate, dev.minChannels, dev.maxChannels);
    }
    engine->stop();

    // Bring window to the front
    CallAfter([this]() {
        frame->Iconize(false);
        frame->SetFocus();
        frame->Raise();
        frame->Show(true);
    });
    
    // Wait 100ms for FreeDV to come to foreground
    std::this_thread::sleep_for(100ms);

    // Select FreeDV mode.
    wxRadioButton* modeBtn = nullptr;
    if (utFreeDVMode == "RADEV1")
    {
        modeBtn = frame->m_rbRADE;
    }
    else if (utFreeDVMode == "700D")
    {
        modeBtn = frame->m_rb700d;
    }
    else if (utFreeDVMode == "700E")
    {
        modeBtn = frame->m_rb700e;
    }
    else if (utFreeDVMode == "1600")
    {
        modeBtn = frame->m_rb1600;
    }
    
    if (modeBtn != nullptr)
    {
        log_info("Firing mode change");
        /*sim.MouseMove(modeBtn->GetScreenPosition());
        sim.MouseClick();*/
        CallAfter([this, modeBtn]() {
            modeBtn->SetValue(true);
            wxCommandEvent* modeEvent = new wxCommandEvent(wxEVT_RADIOBUTTON, modeBtn->GetId());
            modeEvent->SetEventObject(modeBtn);
            QueueEvent(modeEvent);
        });
    }
    
    // Fire event to start FreeDV
    log_info("Firing start");
    CallAfter([this]() {
        frame->m_togBtnOnOff->SetValue(true);
        wxCommandEvent* onEvent = new wxCommandEvent(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, frame->m_togBtnOnOff->GetId());
        onEvent->SetEventObject(frame->m_togBtnOnOff);
        frame->OnTogBtnOnOff(*onEvent);
        delete onEvent;
        //QueueEvent(onEvent);
    });
    /*sim.MouseMove(frame->m_togBtnOnOff->GetScreenPosition());
    sim.MouseClick();*/
    
    // Wait for FreeDV to start
    while (!isModemRunning.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(20ms);
    }
    std::this_thread::sleep_for(2s);
    
    constexpr int MAX_TIME_AS_COUNTER = 12000; // 20 minutes
    if (testName == "tx")
    {
        if (utTxOutFile != "")
        {
            SF_INFO recSf;
            recSf.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
            recSf.channels   = 1;
            recSf.samplerate = RECORD_FILE_SAMPLE_RATE;

            g_sfRecFileFromModulator = sf_open((const char*)utTxOutFile.ToUTF8(), SFM_WRITE, &recSf);
            g_recFileFromModulator = true;
        }

        log_info("Transmitting %d times", utTxAttempts);
        for (int numTimes = 0; numTimes < utTxAttempts; numTimes++)
        {
            // Fire event to begin TX
            //sim.MouseMove(frame->m_btnTogPTT->GetScreenPosition());
            //sim.MouseClick();
            
            if (utTxFile != "")
            {
                log_info("Using audio from file %s", (const char*)utTxFile.ToUTF8());
                
                // Transmit until file has finished playing
                SF_INFO     sfInfo;
                sfInfo.format = 0;
                g_sfPlayFile.store(sf_open((const char*)utTxFile.ToUTF8(), SFM_READ, &sfInfo), std::memory_order_release);
                g_sfTxFs.store(sfInfo.samplerate, std::memory_order_release);
                g_loopPlayFileToMicIn.store(false, std::memory_order_relaxed);
                g_playFileToMicIn.store(true, std::memory_order_release);

                log_info("Firing PTT");
                CallAfter([this]() {
                    frame->m_btnTogPTT->SetValue(true);
                    wxCommandEvent* txEvent = new wxCommandEvent(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, frame->m_btnTogPTT->GetId());
                    txEvent->SetEventObject(frame->m_btnTogPTT);
                    //QueueEvent(txEvent);
                    frame->OnTogBtnPTT(*txEvent);
                    delete txEvent;
                });

                int counter = 0;
                while (g_playFileToMicIn.load(std::memory_order_acquire) && (counter++) < MAX_TIME_AS_COUNTER)
                {
                    std::this_thread::sleep_for(100ms);
                } 
            }
            else
            {
                log_info("Firing PTT");
                CallAfter([this]() {
                    frame->m_btnTogPTT->SetValue(true);
                    wxCommandEvent* txEvent = new wxCommandEvent(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, frame->m_btnTogPTT->GetId());
                    txEvent->SetEventObject(frame->m_btnTogPTT);
                    //QueueEvent(txEvent);
                    frame->OnTogBtnPTT(*txEvent);
                    delete txEvent;
                });

                // Transmit for user given time period (default 60 seconds)
                log_info("Sleeping for %d seconds", utTxTimeSeconds);
                std::this_thread::sleep_for(std::chrono::seconds(utTxTimeSeconds));
            }
            
            // Stop transmitting
            log_info("Firing PTT");
            std::this_thread::sleep_for(1s);
            CallAfter([this]() {
                frame->m_btnTogPTT->SetValue(false);
                endingTx.store(true, std::memory_order_release);
                wxCommandEvent* rxEvent = new wxCommandEvent(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, frame->m_btnTogPTT->GetId());
                rxEvent->SetEventObject(frame->m_btnTogPTT);
                frame->OnTogBtnPTT(*rxEvent);
                delete rxEvent;
                //QueueEvent(rxEvent);
            });
            /*sim.MouseMove(frame->m_btnTogPTT->GetScreenPosition());
            sim.MouseClick();*/
            
            // Wait 5 seconds for FreeDV to stop. Add up to 500ms of additional
            // random delay to avoid mpp test failures.
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> distrib(0, 500);
            std::this_thread::sleep_for(5s + std::chrono::milliseconds(distrib(gen)));
        }

        if (g_sfRecFileFromModulator)
        {
            g_recFileFromModulator = false;
            sf_close(g_sfRecFileFromModulator);
        }
    }
    else
    {
        if (utRxOutFile != "")
        {
            SF_INFO recSf;
            recSf.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
            recSf.channels   = 1;
            recSf.samplerate = RECORD_FILE_SAMPLE_RATE;

            g_sfRecDecoderFile.store(sf_open((const char*)utRxOutFile.ToUTF8(), SFM_WRITE, &recSf), std::memory_order_release);
            g_recFileFromDecoder.store(true, std::memory_order_release);
        }

        if (utRxFile != "")
        {
            // Receive until file has finished playing
            SF_INFO     sfInfo;
            sfInfo.format = 0;
            g_sfPlayFileFromRadio.store(sf_open((const char*)utRxFile.ToUTF8(), SFM_READ, &sfInfo), std::memory_order_release);
            g_sfFs.store(sfInfo.samplerate, std::memory_order_release);
            g_loopPlayFileFromRadio.store(false, std::memory_order_relaxed);
            g_playFileFromRadio.store(true, std::memory_order_release);

            auto sync = 0;
            int counter = 0;
            while (g_playFileFromRadio.load(std::memory_order_acquire) && (counter++) < MAX_TIME_AS_COUNTER)
            {
                std::this_thread::sleep_for(100ms);
                auto newSync = freedvInterface.getSync();
                if (newSync != sync)
                {
                    log_info("Sync changed from %d to %d", sync, newSync);
                    sync = newSync;
                }
            }
        }
        else
        {
            // Receive for txtime seconds
            auto sync = 0;
            for (int i = 0; i < utTxTimeSeconds*10; i++)
            {
                std::this_thread::sleep_for(100ms);
                auto newSync = freedvInterface.getSync();
                if (newSync != sync)
                {
                    log_info("Sync changed from %d to %d", sync, newSync);
                    sync = newSync;
                }
            } 
        }

        if (g_recFileFromDecoder.load(std::memory_order_acquire))
        {
            g_recFileFromDecoder.store(false, std::memory_order_release);
            sf_close(g_sfRecDecoderFile.load(std::memory_order_acquire));
        }
    }
    
    // Wait a second to make sure we're not doing any more processing
    std::this_thread::sleep_for(1000ms);
 
    // Fire event to stop FreeDV
    log_info("Firing stop");
    CallAfter([this]() {
        wxCommandEvent* offEvent = new wxCommandEvent(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, frame->m_togBtnOnOff->GetId());
        offEvent->SetEventObject(frame->m_togBtnOnOff);
        frame->m_togBtnOnOff->SetValue(false);
        //QueueEvent(offEvent);
        frame->OnTogBtnOnOff(*offEvent);
        delete offEvent;
    });
    //sim.MouseMove(frame->m_togBtnOnOff->GetScreenPosition());
    //sim.MouseClick();
    
    // Wait 5 seconds for FreeDV to stop
    std::this_thread::sleep_for(5s);
    
    if (frame->m_reporterDialog)
    {
        // Force deletion of FreeDV Reporter linkage to avoid spurious asan errors.
        CallAfter([this]() {
            frame->m_reporterDialog->setReporter(nullptr);
            wxGetApp().SafeYield(nullptr, false); // make sure we handle any remaining Reporter messages before dispose
        });
        
        std::this_thread::sleep_for(5s);
    }
    
    // Destroy main window to exit application. Must be done in UI thread to avoid problems.
    CallAfter([this]() {
        frame->Destroy();
    });
}

void MainApp::OnInitCmdLine(wxCmdLineParser& parser)
{
    wxApp::OnInitCmdLine(parser);
    parser.AddOption("f", "config", "Use different configuration file instead of the default.");
    parser.AddOption("ut", "unit_test", "Execute FreeDV in unit test mode.");
    parser.AddOption("utmode", wxEmptyString, "Switch FreeDV to the given mode before UT execution.");
    parser.AddOption("rxfile", wxEmptyString, "In UT mode, pipes given WAV file through receive pipeline.");
    parser.AddOption("rxoutfile", wxEmptyString, "In UT mode, records RX output to the given WAV file.");
    parser.AddOption("txfile", wxEmptyString, "In UT mode, pipes given WAV file through transmit pipeline.");
    parser.AddOption("txoutfile", wxEmptyString, "In UT mode, records TX output to the given WAV file.");
    parser.AddOption("rxfeaturefile", wxEmptyString, "Capture RX features from RADE decoder into the provided file.");
    parser.AddOption("txfeaturefile", wxEmptyString, "Capture TX features from FARGAN encoder into the provided file.");
    parser.AddOption("txtime", "60", "In UT mode, the amount of time to transmit (default 60 seconds)", wxCMD_LINE_VAL_NUMBER);
    parser.AddOption("txattempts", "1", "In UT mode, the number of times to transmit (default 1)", wxCMD_LINE_VAL_NUMBER);
}

bool MainApp::OnCmdLineParsed(wxCmdLineParser& parser)
{
    ulog_set_lock(&LogLockFunction_, nullptr);
    ulog_set_prefix_fn([](ulog_Event *, char *prefix, size_t prefix_size) {
        static unsigned int counter = 0;
        snprintf(prefix, prefix_size, " [%u]", ++counter);
    });
        
    log_info("FreeDV version %s starting", GetFreeDVVersion().c_str());

    if (!wxApp::OnCmdLineParsed(parser))
    {
        return false;
    }

    wxString configPath;
    if (parser.Found("f", &configPath))
    {
        log_info("Loading configuration from %s", (const char*)configPath.ToUTF8());
        pConfig = new wxFileConfig(wxT("FreeDV"), FREEDV_VENDOR_NAME, configPath, configPath, wxCONFIG_USE_LOCAL_FILE);
        wxConfigBase::Set(pConfig);
        
        // On Linux/macOS, this replaces $HOME with "~" to shorten the title a bit.
        wxFileName fn(configPath);        
        customConfigFileName = fn.GetFullName();
        defaultConfigFilePath = fn.GetPath();

#if wxCHECK_VERSION(3,3,0) && defined(__linux__)
        // For Load Default Configuration (see below for why it's this).
        defaultConfigFile = wxFileConfig::GetLocalFile("freedv", wxCONFIG_USE_XDG | wxCONFIG_USE_SUBDIR).GetFullPath();
#else
        // Likewise: open the default configuration now, while wxWidgets
        // would find the same one as it does when FreeDV is started
        // without being given a configuration to use.
        wxConfigBase::Set(nullptr);
        defaultConfig = wxConfigBase::Create();
        wxConfigBase::Set(pConfig);
#endif // wxCHECK_VERSION(3,3,0) && defined(__linux__)
    }
    else
    {
        wxString oldFileLocation = wxFileConfig::GetLocalFile("freedv", 0).GetFullPath();
        wxFileName tempOldFile(oldFileLocation);

#if wxCHECK_VERSION(3,3,0) && defined(__linux__)
        // Execute this early during the application startup, before the
        // global wxConfig object is created.
        bool migrateSuccess = true;
        wxString newFileLocation = wxFileConfig::GetLocalFile("freedv", wxCONFIG_USE_XDG | wxCONFIG_USE_SUBDIR).GetFullPath();
        wxString newFileDir = wxFileConfig::GetLocalFile("freedv", wxCONFIG_USE_XDG | wxCONFIG_USE_SUBDIR).GetPath();
        log_info("Determining if we need to migrate config file to standard location...");
        log_info("   Old location: %s", (const char*)oldFileLocation.ToUTF8());
        log_info("   New location: %s", (const char*)newFileLocation.ToUTF8());

        wxFileName tempNewFile(newFileLocation);
        if (!tempNewFile.IsFileReadable() && tempOldFile.IsFileReadable())
        {
            // Migration hasn't happened yet, try to copy to new location.
            // Note that the return value from Mkdir isn't reliable despite actually
            // creating the folder, so we should check for its existence separately.
            wxFileName::Mkdir(newFileDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
            if (!wxDirExists(newFileDir))
            {
                log_warn("Could not create folder %s. Migration will not proceed.", (const char*)newFileDir.ToUTF8());
                migrateSuccess = false;
            }
            else
            {
                bool copyResult = wxCopyFile(oldFileLocation, newFileLocation);
                if (!copyResult)
                {
                    log_warn("Could not copy %s to %s. Migration will not proceed.", (const char*)oldFileLocation.ToUTF8(), (const char*)newFileLocation.ToUTF8());
                    migrateSuccess = false;
                }
            }
        }
 
        // Prefer doing it only after successfully calling MigrateLocalFile(),
        // otherwise, i.e. if it failed, the old config file wouldn't be used.
        if (migrateSuccess)
        {
            wxStandardPaths::Get().SetFileLayout(wxStandardPaths::FileLayout_XDG);

            // Need to explicitly create the wxFileConfig on Linux so that we can force wxWidgets
            // to load configuration files under a subdirectory. Otherwise, simply FileLayout_XDG
            // above will use ~/.config/freedv.conf.
            pConfig = new wxFileConfig(wxT("FreeDV"), FREEDV_VENDOR_NAME, newFileLocation, newFileLocation, wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_SUBDIR | wxCONFIG_USE_XDG);

            wxConfigBase::Set(pConfig);
            defaultConfigFilePath = tempNewFile.GetPath();
            defaultConfigFile = newFileLocation;
        }
#else
        defaultConfigFilePath = tempOldFile.GetPath();
#endif // wxCHECK_VERSION(3,3,0) && defined(__linux__)

        // Whichever it is, this is the default configuration.
        defaultConfig = wxConfigBase::Get();

        // If a config file was previously loaded via "Use Configuration...",
        // restore it now so the user's last session is preserved.
        wxString lastUsedPath = getLastUsedConfigPath();
        if (!lastUsedPath.IsEmpty())
        {
            if (wxFileExists(lastUsedPath))
            {
                log_info("Restoring last-used configuration from %s",
                         (const char*)lastUsedPath.ToUTF8());
                pConfig = new wxFileConfig(wxT("FreeDV"), FREEDV_VENDOR_NAME,
                                           lastUsedPath, lastUsedPath,
                                           wxCONFIG_USE_LOCAL_FILE);
                wxConfigBase::Set(pConfig);
                wxFileName fn(lastUsedPath);
                customConfigFileName = fn.GetFullName();
                defaultConfigFilePath = fn.GetPath();
            }
            else
            {
                log_warn("Last-used config file '%s' no longer exists; reverting to default.",
                         (const char*)lastUsedPath.ToUTF8());
                clearLastUsedConfigPath();
            }
        }
    }

    pConfig = wxConfigBase::Get();
    pConfig->SetRecordDefaults();
    
    if (parser.Found("ut", &testName))
    {
        log_info("Executing test %s", (const char*)testName.ToUTF8());
        if (parser.Found("utmode", &utFreeDVMode))
        {
            log_info("Using mode %s for tests", (const char*)utFreeDVMode.ToUTF8());
        }

        if (parser.Found("rxfile", &utRxFile))
        {
            log_info("Piping %s through RX pipeline", (const char*)utRxFile.ToUTF8());
        }
        
        if (parser.Found("rxoutfile", &utRxOutFile))
        {
            log_info("Recording RX output to %s", (const char*)utRxOutFile.ToUTF8());
        }

        if (parser.Found("txfile", &utTxFile))
        {
            log_info("Piping %s through TX pipeline", (const char*)utTxFile.ToUTF8());
        }

        if (parser.Found("txoutfile", &utTxOutFile))
        {
            log_info("Recording TX output to %s", (const char*)utTxOutFile.ToUTF8());
        }

        if (parser.Found("txtime", &utTxTimeSeconds))
        {
            log_info("Will transmit for %d seconds", utTxTimeSeconds);
        }
        else
        {
            utTxTimeSeconds = 60;
        }

        if (parser.Found("txattempts", &utTxAttempts))
        {
            log_info("Will transmit %d time(s)", utTxAttempts);
        }
        else
        {
            utTxAttempts = 1;
        }
    }
    
    wxString utRxFeatureFileTmp; 
    if (parser.Found("rxfeaturefile", &utRxFeatureFileTmp))
    {
        utRxFeatureFile = utRxFeatureFileTmp.ToUTF8();
        log_info("Capturing RADE RX features into file %s", utRxFeatureFile.c_str());
    }
   
    wxString utTxFeatureFileTmp; 
    if (parser.Found("txfeaturefile", &utTxFeatureFileTmp))
    {
        utTxFeatureFile = utTxFeatureFileTmp.ToUTF8();
        log_info("Capturing RADE TX features into file %s", utTxFeatureFile.c_str());
    }
    
    return true;
}

//-------------------------------------------------------------------------
#if defined(__WXGTK__) && defined(HAS_GTK3)
// Suppress the GTK theme :active (button-press) colour flash app-wide.
// Queries the theme's normal button background and installs a screen-level
// CSS rule so button:active renders identically to the resting state.
// Fails gracefully if the named colour variables are absent (non-Breeze themes).
static bool TryLookupThemeColour_(const char* name, GdkRGBA& out)
{
    GtkWidget* tmpButton = gtk_button_new();
    GtkWidget* tmpWindow = gtk_offscreen_window_new();
    gtk_container_add(GTK_CONTAINER(tmpWindow), tmpButton);
    gtk_widget_show_all(tmpWindow);
    GtkStyleContext* ctx = gtk_widget_get_style_context(tmpButton);
    bool found = gtk_style_context_lookup_color(ctx, name, &out);
    gtk_widget_destroy(tmpWindow);
    return found;
}

static void SuppressButtonPressFlicker_()
{
    GdkRGBA bg;
    bool found = false;
    // theme_button_background_normal is Breeze-specific; theme_bg_color is
    // a broader fallback. theme_base_color is deliberately excluded — it
    // resolves to the text-field background (white in light themes).
    const char* names[] = { "theme_button_background_normal", "theme_bg_color", nullptr };
    for (int i = 0; names[i] && !found; i++)
        found = TryLookupThemeColour_(names[i], bg);
    if (!found)
        return;

    gchar* cssColour = gdk_rgba_to_string(&bg);
    gchar* css = g_strdup_printf(
        "button:active {"
        "  background-color: %s;"
        "  background-image: none;"
        "  box-shadow: none;"
        "}", cssColour);
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_free(css);
    g_free(cssColour);
    g_object_unref(provider);
}

#endif // defined(__WXGTK__) && defined(HAS_GTK3)

// OnInit()
//-------------------------------------------------------------------------
bool MainApp::OnInit()
{
#if !defined(_WIN32)
    // Writing to a socket whose peer has closed raises SIGPIPE, which by
    // default kills the process. That can happen whenever a server (e.g. FreeDV
    // Reporter) closes a connection just as we send to it, including inside
    // TLS library writes we can't flag individually. Ignore it so the write
    // fails with EPIPE and the connection code handles it as a disconnect.
    signal(SIGPIPE, SIG_IGN);
#endif // !defined(_WIN32)

    // Initialize locale.
#if wxCHECK_VERSION(3,2,0)
    wxUILocale::UseDefault();
#else
    m_locale.Init();
#endif // wxCHECK_VERSION(3,2,0)

    lastSelectedLoggingRow = LastSelectedRow::UNSELECTED;
    m_reporters.clear();
    m_reportCounter = 0;
    
    if(!wxApp::OnInit())
    {
        return false;
    }
#if defined(__WXGTK__) && defined(HAS_GTK3)
    SuppressButtonPressFlicker_();
#endif // defined(__WXGTK__) && defined(HAS_GTK3)
    SetVendorName(FREEDV_VENDOR_NAME);
    SetAppName(wxT("FreeDV"));      // not needed, it's the default value
    
    golay23_init();

#if defined(UNOFFICIAL_RELEASE)
    // Terminate the application if the current date > expiration date
    wxDateTime buildDate(wxInvalidDateTime); // silence UBSan error on some platforms
    wxString::const_iterator iter;
    buildDate.ParseDate(FREEDV_BUILD_DATE, &iter);
    
    auto expireDate = buildDate + EXPIRES_AFTER_TIMEFRAME;
    auto currentDate = wxDateTime::Now();
    
    if (currentDate > expireDate)
    {
        wxMessageBox("This version of FreeDV has expired. Please download a new version from freedv.org.", "Application Expired");
        return false;
    }
#endif // UNOFFICIAL_RELEASE
    
    // Initialize RADE.
    rade_initialize();
 
    m_rTopWindow = wxRect(0, 0, 0, 0);

     // Create the main application window

    frame = new MainFrame(NULL);
    SetTopWindow(frame);

    frame->Layout();    
    frame->Show();
    g_parent = frame;

    // Begin test execution
    if (testName != "")
    {
        std::thread utThread(std::bind(&MainApp::UnitTest_, this));
        utThread.detach();
    }
    
    return true;
}

//-------------------------------------------------------------------------
// OnExit()
//-------------------------------------------------------------------------
int MainApp::OnExit()
{
    return 0;
}

//-------------------------------------------------------------------------
// loadConfiguration_(): Loads or sets default configuration options.
//-------------------------------------------------------------------------
void MainFrame::loadConfiguration_()
{
    wxGetApp().appConfiguration.load(pConfig);
    
    // restore frame position and size
    int x = wxGetApp().appConfiguration.mainWindowLeft;
    int y = wxGetApp().appConfiguration.mainWindowTop;
    int w = wxGetApp().appConfiguration.mainWindowWidth;
    int h = wxGetApp().appConfiguration.mainWindowHeight;

    // sanitise frame position as a first pass at Win32 registry bug

    if (x < 0 || x > 2048) x = 20;
    if (y < 0 || y > 2048) y = 20;
    // (Nor anything too small to be of use: macOS, unlike Linux's window
    // managers, makes the window as small as it's asked to.) The default,
    // for when none has been saved, and that limit are scaled to suit the
    // display: on Windows, sizes are in the screen's own pixels, and 800 of
    // those at 200% are only as wide as 400 elsewhere, leaving no room for
    // the plots beside everything else. (wxWidgets 3.0, which only Linux
    // builds use, has no FromDIP(), and needs none there.)
#if wxCHECK_VERSION(3,1,0)
    wxSize defaultSize = FromDIP(wxSize(800, 780));
    wxSize smallestSize = FromDIP(wxSize(300, 200));
#else
    wxSize defaultSize(800, 780);
    wxSize smallestSize(300, 200);
#endif // wxCHECK_VERSION(3,1,0)
    if (w < smallestSize.GetWidth() || w > 2048) w = defaultSize.GetWidth();
    if (h < smallestSize.GetHeight() || h > 2048) h = defaultSize.GetHeight();

    g_SquelchActive.store(wxGetApp().appConfiguration.squelchActive, std::memory_order_release);
    g_SquelchLevel.store(wxGetApp().appConfiguration.squelchLevel / 2.0, std::memory_order_release);
    
    wxSize size = GetMinSize();

    if (w < size.GetWidth()) w = size.GetWidth();
    if (h < size.GetHeight()) h = size.GetHeight();

    RestoreWindowPosition(this, x, y);

    // XXX - with really short windows, wxWidgets sometimes doesn't size
    // the components properly until the user resizes the window (even if only
    // by a pixel or two). As a really hacky workaround, we emulate this behavior
    // when restoring window sizing. These resize events also happen after configuration
    // is restored but I'm not sure this is necessary.
    CallAfter([=, this]()
    {
        SetSize(w, h);
    });
    CallAfter([=, this]()
    {
        SetSize(w + 1, h + 1);
    });
    CallAfter([=, this]()
    {
        SetSize(w, h);
    });
    
    // Load AGC state
    g_agcEnabled.store(wxGetApp().appConfiguration.filterConfiguration.agcEnabled, std::memory_order_release);
    
    // Load noise reduction state
    g_noiseReductionEnabled.store(wxGetApp().appConfiguration.filterConfiguration.noiseReductionEnable, std::memory_order_release);
    
    // Load TX/VK monitor state
    g_monitorTxAudio.store(wxGetApp().appConfiguration.monitorTxAudio, std::memory_order_release);
    g_monitorVoiceKeyerAudio.store(wxGetApp().appConfiguration.monitorVoiceKeyerAudio, std::memory_order_release);
    g_monitorTxAudioVol.store(wxGetApp().appConfiguration.monitorTxAudioVol, std::memory_order_release);
    g_monitorVoiceKeyerAudioVol.store(wxGetApp().appConfiguration.monitorVoiceKeyerAudioVol, std::memory_order_release);
    
    // Load channel noise SNR
    g_noiseSNR.store(wxGetApp().appConfiguration.noiseSNR, std::memory_order_release);
    
    // Load BW expander state
    g_bwExpandEnabled.store(wxGetApp().appConfiguration.filterConfiguration.bwExpandEnabled, std::memory_order_release);
    
    g_txLevel = wxGetApp().appConfiguration.transmitLevel;
    float dbLoss = g_txLevel / 10.0;
    float scaleFactor = exp(dbLoss/20.0 * log(10.0));
    g_txLevelScale.store(scaleFactor, std::memory_order_release);

    wxString fmtString = wxString::Format(MIC_SPKR_LEVEL_FORMAT_STR, wxNumberFormatter::ToString((double)dbLoss, 1), DECIBEL_STR);
    m_txtTxLevelNum->SetLabel(fmtString);

    g_tuneLevel = wxGetApp().appConfiguration.tuneLevel;
    dbLoss = g_tuneLevel / 10.0;
    scaleFactor = exp(dbLoss/20.0 * log(10.0));
    g_tuneLevelScale.store(scaleFactor, std::memory_order_release);

    m_sliderMicSpkrLevel->SetValue(wxGetApp().appConfiguration.filterConfiguration.spkOutChannel.volInDB * 10);
    fmtString = wxString::Format(MIC_SPKR_LEVEL_FORMAT_STR, wxNumberFormatter::ToString((double)wxGetApp().appConfiguration.filterConfiguration.spkOutChannel.volInDB, 1), DECIBEL_STR);
    m_txtMicSpkrLevelNum->SetLabel(fmtString);

    // Adjust frequency entry labels
    wxListItem colInfo;
    m_lastReportedCallsignListView->GetColumn(1, colInfo);
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
    {
        m_freqBox->SetLabel(_("Radio Freq. (kHz)"));
        colInfo.SetText(_("kHz"));
    }
    else
    {
        m_freqBox->SetLabel(_("Radio Freq. (MHz)"));
        colInfo.SetText(_("MHz"));
    }
    m_lastReportedCallsignListView->SetColumn(1, colInfo);
    
    // PTT -------------------------------------------------------------------
    
    // Note: we're no longer using RigName but we need to bring over the old data
    // for backwards compatibility.    
    if (wxGetApp().appConfiguration.rigControlConfiguration.hamlibRigName == wxT(""))
    {
        wxGetApp().m_intHamlibRig = pConfig->ReadLong("/Hamlib/RigName", -1);
        if (wxGetApp().m_intHamlibRig >= 0)
        {
            wxGetApp().appConfiguration.rigControlConfiguration.hamlibRigName = HamlibRigController::RigIndexToName(wxGetApp().m_intHamlibRig);
        }
    }
    else
    {
        wxGetApp().m_intHamlibRig = HamlibRigController::RigNameToIndex(std::string(wxGetApp().appConfiguration.rigControlConfiguration.hamlibRigName->ToUTF8()));
    }
    
    // -----------------------------------------------------------------------

    wxGetApp().m_FreeDV700Combine = 1;

    if (wxGetApp().appConfiguration.debugVerbose)
    {
        ulog_set_level(LOG_TRACE);
    }
    else
    {
        ulog_set_level(LOG_INFO);
    }
    g_freedv_verbose = wxGetApp().appConfiguration.apiVerbose;

    wxGetApp().m_attn_carrier_en = 0;
    wxGetApp().m_attn_carrier    = 0;

    wxGetApp().m_tone = 0;
    wxGetApp().m_tone_freq_hz = 1000;
    wxGetApp().m_tone_amplitude = 500;
    
    // General reporting parameters

    // wxString::Format() doesn't respect locale but C++ iomanip should. Use the latter instead.
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency > 0)
    {
        double freqFactor = 1000.0;
        
        if (!wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
        {
            freqFactor *= 1000.0;
        }
        
        double freq =  ((double)wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency) / freqFactor;

        wxString sVal;
        if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
        {
            sVal = wxNumberFormatter::ToString(freq, 1);
        }
        else
        {
            sVal = wxNumberFormatter::ToString(freq, 4);
        }
        m_cboReportFrequency->SetValue(sVal);
    }

    int defaultMode = wxGetApp().appConfiguration.currentFreeDVMode.getDefaultVal();
    int mode = wxGetApp().appConfiguration.currentFreeDVMode;
setDefaultMode:
    if (mode == 0)
    {
        m_rb1600->SetValue(1);
    }
    else if (mode == 4)
    {
        m_rb700d->SetValue(1);
    }
    else if (mode == 5)
    {
        m_rb700e->SetValue(1);
    }
    else if (mode == FREEDV_MODE_RADE)
    {
        m_rbRADE->SetValue(1);
    }
    else
    {
        // Default to RADE otherwise
        mode = defaultMode;
        goto setDefaultMode;
    }
    
    // Disable controls not supported by RADE.
    bool isEnabled = wxGetApp().appConfiguration.enableLegacyModes && mode != FREEDV_MODE_RADE;
    squelchBox->Show(wxGetApp().appConfiguration.enableLegacyModes);
    m_sliderSQ->Enable(isEnabled);
    m_ckboxSQ->Enable(isEnabled);
    m_textSQ->Enable(isEnabled);
    m_btnCenterRx->Enable(isEnabled);
    m_btnCenterRx->Show(wxGetApp().appConfiguration.enableLegacyModes);
    m_BtnReSync->Enable(isEnabled);
    m_BtnReSync->Show(wxGetApp().appConfiguration.enableLegacyModes);

    if (!isEnabled)
    {
        m_textBits->SetLabel("Bits: unk");
        m_textErrors->SetLabel("Errs: unk");
        m_textBER->SetLabel("BER: unk");
        m_textFreqOffset->SetLabel("FrqOff: unk");
        m_textSyncMetric->SetLabel("Sync: unk");
        m_textCodec2Var->SetLabel("Var: unk");
        m_textClockOffset->SetLabel("ClkOff: unk");
    }
    
    pConfig->SetPath(wxT("/"));
    
    m_togBtnAnalog->Disable();
    m_btnTogPTT->Disable();
    m_togBtnVoiceKeyer->Disable();

    // squelch settings
    m_sliderSQ->SetValue((int)((g_SquelchLevel.load(std::memory_order_acquire)+5.0)*2.0));
    wxString sqsnr_string = wxNumberFormatter::ToString(g_SquelchLevel.load(std::memory_order_acquire), 1) + "dB";
    m_textSQ->SetLabel(sqsnr_string);
    m_ckboxSQ->SetValue(g_SquelchActive.load(std::memory_order_acquire));

    // SNR settings

    m_ckboxSNR->SetValue(wxGetApp().appConfiguration.snrSlow);
    setsnrBeta(wxGetApp().appConfiguration.snrSlow);
    
    // Show/hide frequency box based on CAT control status
    m_freqBox->Show(isFrequencyControlEnabled_());

    restoreCallsignListFromCsv_();
    m_logQSO->Enable(canLogQso_());

    // Show/hide callsign combo box based on reporting enablement
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled)
    {
        m_cboLastReportedCallsigns->Show();
        m_txtCtrlCallSign->Hide();
        m_cboLastReportedCallsigns->Enable(m_lastReportedCallsignListView->GetItemCount() > 0);
    }
    else
    {
        m_cboLastReportedCallsigns->Hide();
        m_txtCtrlCallSign->Show();
    }

    // Enable/disable FreeDV Reporter quick options
    m_reporterHidden->Enable(
        wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled &&
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnabled);
    m_reporterHidden->SetValue(
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterForcedOff);
    if (wxGetApp().appConfiguration.reportingConfiguration.freedvReporterForcedOff)
    {
        m_reporterHidden->SetLabel(_("Turn On"));
    }
    else
    {
        m_reporterHidden->SetLabel(_("Turn Off"));
    }
    
    // Ensure that sound card count is correct. Otherwise the Audio Options won't show
    // the correct devices prior to start.
    bool hasSoundCard1InDevice = wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName != "none";
    bool hasSoundCard1OutDevice = wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName != "none";
    bool hasSoundCard2InDevice = wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName != "none";
    bool hasSoundCard2OutDevice = wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName != "none";
    
    g_nSoundCards.store(0, std::memory_order_release);
    if (hasSoundCard1InDevice && hasSoundCard1OutDevice) {
        g_nSoundCards.store(1, std::memory_order_release);
        if (hasSoundCard2InDevice && hasSoundCard2OutDevice)
            g_nSoundCards.store(2, std::memory_order_release);
    }
    
    // Update the reporting list as needed.
    updateReportingFreqList_();
    
    // Relayout window so that the changes can take effect.
    auto currentSizer = m_panel->GetSizer();
    m_panel->SetSizerAndFit(currentSizer, false);
    m_panel->Layout();
    
    // Load default voice keyer file as current.
    if (wxGetApp().appConfiguration.voiceKeyerWaveFile != "")
    {
        wxFileName fullVKPath(wxGetApp().appConfiguration.voiceKeyerWaveFilePath, wxGetApp().appConfiguration.voiceKeyerWaveFile);
        vkFileName_ = fullVKPath.GetFullPath().mb_str();
        vkFileCache_.preload(vkFileName_);
        
        m_togBtnVoiceKeyer->SetToolTip(_("Toggle Voice Keyer using file ") + wxGetApp().appConfiguration.voiceKeyerWaveFile + _(". Right-click for additional options."));
        
        wxString fileNameWithoutExt;
        wxFileName::SplitPath(wxGetApp().appConfiguration.voiceKeyerWaveFile, nullptr, &fileNameWithoutExt, nullptr);
        setVoiceKeyerButtonLabel_(fileNameWithoutExt);
    }
    else
    {
        vkFileName_ = "";
    }
    
    // Cache this now rather than re-reading the live config value at exit time: the
    // checkbox can be toggled mid-session without reloading/reapplying a layout (that
    // only happens here, at startup), so exit-time save must be gated on the same
    // flag value the load decision above used, not whatever it's since been changed to.
    tabLayoutPersistenceEnabledAtStartup_ = wxGetApp().appConfiguration.experimentalFeatures;
    if (tabLayoutPersistenceEnabledAtStartup_ && wxGetApp().appConfiguration.tabLayout != "")
    {
#if wxCHECK_VERSION(3, 3, 0)
        TabLayoutDeserializer deserializer(wxGetApp().appConfiguration.tabLayout);
        m_auiNbookCtrl->LoadLayout("notebook", deserializer);
#else
        ((TabFreeAuiNotebook*)m_auiNbookCtrl)->LoadPerspective(wxGetApp().appConfiguration.tabLayout);
#endif // wxCHECK_VERSION(3, 3, 0)
        const_cast<wxAuiManager&>(m_auiNbookCtrl->GetAuiManager()).Update();

        // Select previous active tab.
        m_auiNbookCtrl->ChangeSelection(wxGetApp().appConfiguration.currentNotebookTab);
    }
    
    statsBox->Show(wxGetApp().appConfiguration.showDecodeStats);
    modeBox->Show(wxGetApp().appConfiguration.enableLegacyModes);
    m_BtnReSync->Show(wxGetApp().appConfiguration.enableLegacyModes);

    // Initialize FreeDV Reporter as required
    CallAfter(&MainFrame::initializeFreeDVReporter_);
    
    // If the FreeDV Reporter window was open on last execution, reopen it now.
    CallAfter([&]() {
        if (wxGetApp().appConfiguration.reporterWindowVisible)
        {
            wxCommandEvent event;
            OnToolsFreeDVReporter(event);
        }
    });
}

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// Class MainFrame(wxFrame* pa->ent) : TopFrame(parent)
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
MainFrame::MainFrame(wxWindow *parent) : TopFrame(parent, wxID_ANY, _("FreeDV ") + wxString::FromUTF8(GetFreeDVVersion().c_str())),

    // Create needed strings in advance so we don't need to continually 
    // reallocate memory every time through OnTimer() below. We prioritize
    // the strings that are used when RADE is selected as this mode is not
    // fully optimized for real-time use yet (i.e. it dynamically allocates
    // memory while processing audio).
    SNR_FORMAT_STR("%ddB"),
    MODE_FORMAT_STR("Mode: %s"),
    MODE_RADE_FORMAT_STR("Mode: RADEV1"),
    NO_SNR_LABEL("--"),
    EMPTY_STR(""),
    MODEM_LABEL("Modem"),
    BITS_UNK_LABEL("Bits: unk"),
    ERRS_UNK_LABEL("Errs: unk"),
    BER_UNK_LABEL("BER: unk"),
    FRQ_OFF_UNK_LABEL("FrqOff: unk"),
    SYNC_UNK_LABEL("Sync: unk"),
    VAR_UNK_LABEL("Var: unk"),
    CLK_OFF_UNK_LABEL("ClkOff: unk"),
    MIC_SPKR_LEVEL_FORMAT_STR("%s%s"),
    DECIBEL_STR("dB"),
    CURRENT_TIME_FORMAT_STR("%s %s"),
    SNR_FORMAT_STR_NO_DB("%0.1f"),
    CALLSIGN_FORMAT_RGX("(([A-Za-z0-9]+/)?[A-Za-z0-9]{1,3}[0-9][A-Za-z0-9]*[A-Za-z](/[A-Za-z0-9]+)?)"),
    BITS_FMT("Bits: %d"),
    ERRS_FMT("Errs: %d"),
    BER_FMT("BER: %4.3f"),
    RESYNC_FMT("Resyncs: %d"),
    FRQ_OFF_FMT("FrqOff: %3.1f"),
    SYNC_FMT("Sync: %3.2f"),
    VAR_FMT("Var: %4.1f"),
    CLK_OFF_FMT("ClkOff: %+-d")
{
    SetThreadName("GUI");

    terminating_ = false;
    closingReporterDialog_ = false;
    realigned_ = false;
    syncState_ = false;
    tabLayoutPersistenceEnabledAtStartup_ = false;
    txChangeoverOccurring_ = false;

    // Add config file name to title bar if provided at the command line.
    if (wxGetApp().customConfigFileName != "")
    {
        SetTitle(wxString::Format("%s (%s)", _("FreeDV ") + wxString::FromUTF8(GetFreeDVVersion().c_str()), wxGetApp().customConfigFileName));
    }
    
#if defined(UNOFFICIAL_RELEASE)
    wxDateTime buildDate(wxInvalidDateTime); // silence UBSan warning on some platforms
    wxString::const_iterator iter;
    buildDate.ParseDate(FREEDV_BUILD_DATE, &iter);
    
    auto expireDate = buildDate + EXPIRES_AFTER_TIMEFRAME;
    auto currentTitle = GetTitle();
    
    currentTitle += wxString::Format(" [Expires %s]", expireDate.FormatDate());
    SetTitle(currentTitle);
#endif // defined(UNOFFICIAL_RELEASE)
    
    m_reporterDialog = nullptr;
    m_filterDialog = nullptr;
    optionsDlg = nullptr;

    // Initialize panel pointers to null before creation since "page changed" 
    // events fire as we're adding these (and we compare against these pointers
    // inside the handler).
    m_panelSpectrum = nullptr;
    m_panelWaterfall = nullptr;
    m_panelSpeechIn = nullptr;
    m_panelSpeechOut = nullptr;
    m_panelDemodIn = nullptr;
    m_panelSNR = nullptr;

    m_zoom              = 1.;
    suppressFreqModeUpdates_ = false;
    lastBand_ = BAND_OTHER;
    
    tools->AppendSeparator();
    wxMenuItem* m_menuItemToolsConfigDelete;
    m_menuItemToolsConfigDelete = new wxMenuItem(tools, wxID_ANY, wxString(_("&Restore defaults")) , wxT("Delete config file/keys and restore defaults"), wxITEM_NORMAL);
    this->Connect(m_menuItemToolsConfigDelete->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(MainFrame::OnDeleteConfig));
    this->Connect(m_menuItemToolsConfigDelete->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler(MainFrame::OnDeleteConfigUI));

    tools->Append(m_menuItemToolsConfigDelete);
    
    // Add Waterfall Plot window
    m_panelWaterfall = new PlotWaterfall(m_auiNbookCtrl, g_avmag_waterfall, false, 0);
    m_panelWaterfall->SetToolTip(_("Double click to tune, middle click to re-center"));
    m_auiNbookCtrl->AddPage(m_panelWaterfall, _("Waterfall"), true, wxNullBitmap);

    // Add Spectrum Plot window
    m_panelSpectrum = new PlotSpectrum(m_auiNbookCtrl, g_avmag_spectrum,
                                       MODEM_STATS_NSPEC*((float)MAX_F_HZ/MODEM_STATS_MAX_F_HZ));
    m_panelSpectrum->SetToolTip(_("Double click to tune, middle click to re-center"));    
    m_auiNbookCtrl->AddPage(m_panelSpectrum, _("Spectrum"), false, wxNullBitmap);

    // Add Demod Input window
    m_panelDemodIn = new PlotScalar(m_auiNbookCtrl, WAVEFORM_PLOT_TIME, 1.0/WAVEFORM_PLOT_FS, -1, 1, 1, 0.2, "%2.1f", 0);
    m_auiNbookCtrl->AddPage(m_panelDemodIn, _("Frm Radio"), false, wxNullBitmap);

    // Add Speech Input window
    m_panelSpeechIn = new PlotScalar(m_auiNbookCtrl, WAVEFORM_PLOT_TIME, 1.0/WAVEFORM_PLOT_FS, -1, 1, 1, 0.2, "%2.1f", 0);
    m_auiNbookCtrl->AddPage(m_panelSpeechIn, _("Frm Mic"), false, wxNullBitmap);

    // Add Speech Output window
    m_panelSpeechOut = new PlotScalar(m_auiNbookCtrl, WAVEFORM_PLOT_TIME, 1.0/WAVEFORM_PLOT_FS, -1, 1, 1, 0.2, "%2.1f", 0);
    m_auiNbookCtrl->AddPage(m_panelSpeechOut, _("Frm Decoder"), false, wxNullBitmap);

    // Add SNR window
    m_panelSNR = new PlotScalar(m_auiNbookCtrl, SNR_PLOT_SECONDS, DT, NO_SNR_VAL, MAX_SNR_VAL, SNR_PLOT_SECONDS / SNR_PLOT_SECOND_SEGMENTS, 5, "%.0f", 0, "", true, NO_SNR_VAL, false);
    m_auiNbookCtrl->AddPage(m_panelSNR, _("SNR"), false, wxNullBitmap);

    m_togBtnOnOff->Connect(wxEVT_UPDATE_UI, wxUpdateUIEventHandler(MainFrame::OnTogBtnOnOffUI), NULL, this);
    m_togBtnAnalog->Connect(wxEVT_UPDATE_UI, wxUpdateUIEventHandler(MainFrame::OnTogBtnAnalogClickUI), NULL, this);
    m_btnTogPTT->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnTogBtnPTTMouseDown, this);
    m_btnTogPTT->Bind(wxEVT_LEFT_DCLICK, &MainFrame::OnTogBtnPTTMouseDown, this);
    m_btnTogPTT->Bind(wxEVT_LEAVE_WINDOW, &MainFrame::OnTogBtnPTTMouseLeave, this);

    loadConfiguration_();
    
#ifdef _USE_TIMER
    Bind(wxEVT_TIMER, &MainFrame::OnTimer, this);       // ID_MY_WINDOW);
    m_plotWaterfallTimer.SetOwner(this, ID_TIMER_WATERFALL);
    m_plotSpectrumTimer.SetOwner(this, ID_TIMER_SPECTRUM);
    m_plotSpeechInTimer.SetOwner(this, ID_TIMER_SPEECH_IN);
    m_plotSpeechOutTimer.SetOwner(this, ID_TIMER_SPEECH_OUT);
    m_plotDemodInTimer.SetOwner(this, ID_TIMER_DEMOD_IN);
    m_plotSNRTimer.SetOwner(this, ID_TIMER_SNR);

    m_plotTimer.SetOwner(this, ID_TIMER_UPDATE_OTHER);
    m_pskReporterTimer.SetOwner(this, ID_TIMER_PSKREPORTER);
    m_updFreqStatusTimer.SetOwner(this,ID_TIMER_UPD_FREQ);
    m_totTimer.SetOwner(this, ID_TIMER_TOT);
    Bind(wxEVT_TIMER, &MainFrame::OnTOTTimer, this, ID_TIMER_TOT);
    m_totWarningTimer.SetOwner(this, ID_TIMER_TOT_WARNING);
    Bind(wxEVT_TIMER, &MainFrame::OnTOTWarningTimer, this, ID_TIMER_TOT_WARNING);
    m_pttKeyPollTimer.SetOwner(this, ID_TIMER_PTT_KEY_POLL);
    Bind(wxEVT_TIMER, &MainFrame::OnPttKeyPollTimer, this, ID_TIMER_PTT_KEY_POLL);
#endif
    
    // Create voice keyer popup menu.
    voiceKeyerPopupMenu_ = new wxMenu();
    assert(voiceKeyerPopupMenu_ != nullptr);

    chooseVKFileMenuItem_ = voiceKeyerPopupMenu_->Append(wxID_ANY, _("&Use another voice keyer file..."));
    voiceKeyerPopupMenu_->Connect(
        chooseVKFileMenuItem_->GetId(), wxEVT_COMMAND_MENU_SELECTED, 
        wxCommandEventHandler(MainFrame::OnChooseAlternateVoiceKeyerFile),
        NULL,
        this);
        
    recordNewVoiceKeyerFileMenuItem_ = voiceKeyerPopupMenu_->Append(wxID_ANY, _("&Record new voice keyer file..."));
    voiceKeyerPopupMenu_->Connect(
        recordNewVoiceKeyerFileMenuItem_->GetId(), wxEVT_COMMAND_MENU_SELECTED, 
        wxCommandEventHandler(MainFrame::OnRecordNewVoiceKeyerFile),
        NULL,
        this);
    
    voiceKeyerPopupMenu_->AppendSeparator();
    
    auto monitorVKMenuItem = voiceKeyerPopupMenu_->AppendCheckItem(wxID_ANY, _("Monitor transmitted audio"));
    voiceKeyerPopupMenu_->Check(monitorVKMenuItem->GetId(), wxGetApp().appConfiguration.monitorVoiceKeyerAudio);
    voiceKeyerPopupMenu_->Connect(
        monitorVKMenuItem->GetId(), wxEVT_COMMAND_MENU_SELECTED, 
        wxCommandEventHandler(MainFrame::OnSetMonitorVKAudio),
        NULL,
        this);
        
    adjustMonitorVKVolMenuItem_ = voiceKeyerPopupMenu_->Append(wxID_ANY, _("Adjust Monitor Volume..."));
    adjustMonitorVKVolMenuItem_->Enable(wxGetApp().appConfiguration.monitorVoiceKeyerAudio);
    voiceKeyerPopupMenu_->Connect(
        adjustMonitorVKVolMenuItem_->GetId(), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(MainFrame::OnSetMonitorVKAudioVol),
        NULL,
        this
        );
        
    // Create PTT popup menu
    pttPopupMenu_ = new wxMenu();
    assert(pttPopupMenu_ != nullptr);
    
    auto monitorMenuItem = pttPopupMenu_->AppendCheckItem(wxID_ANY, _("Monitor transmitted audio"));
    pttPopupMenu_->Check(monitorMenuItem->GetId(), wxGetApp().appConfiguration.monitorTxAudio);
    pttPopupMenu_->Connect(
        monitorMenuItem->GetId(), wxEVT_COMMAND_MENU_SELECTED, 
        wxCommandEventHandler(MainFrame::OnSetMonitorTxAudio),
        NULL,
        this);
        
    adjustMonitorPttVolMenuItem_ = pttPopupMenu_->Append(wxID_ANY, _("Adjust Monitor Volume..."));
    adjustMonitorPttVolMenuItem_->Enable(wxGetApp().appConfiguration.monitorTxAudio);
    pttPopupMenu_->Connect(
        adjustMonitorPttVolMenuItem_->GetId(), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(MainFrame::OnSetMonitorTxAudioVol),
        NULL,
        this
        );

    m_RxRunning.store(false, std::memory_order_release);

    m_txThread = nullptr;
    m_rxThread = nullptr;
    wxGetApp().linkStep = nullptr;
    
#ifdef _USE_ONIDLE
    Connect(wxEVT_IDLE, wxIdleEventHandler(MainFrame::OnIdle), NULL, this);
#endif //_USE_ONIDLE

    g_sfPlayFile.store(NULL, std::memory_order_release);
    g_playFileToMicIn.store(false, std::memory_order_release);
    g_loopPlayFileToMicIn.store(false, std::memory_order_relaxed);

    g_sfRecFile.store(NULL, std::memory_order_release);
    g_recFileFromRadio.store(false, std::memory_order_release);

    g_sfPlayFileFromRadio.store(NULL, std::memory_order_release);
    g_playFileFromRadio.store(false, std::memory_order_release);
    g_loopPlayFileFromRadio.store(false, std::memory_order_relaxed);

    g_sfRecFileFromModulator = NULL;
    g_recFileFromModulator = false;
    
    g_sfRecMicFile.store(nullptr, std::memory_order_release);
    g_recFileFromMic.store(false, std::memory_order_relaxed);
    g_recVoiceKeyerFile.store(false, std::memory_order_relaxed);

    // init click-tune states

    g_RxFreqOffsetHz.store(0.0f, std::memory_order_relaxed);
    m_panelWaterfall->setRxFreq(FDMDV_FCENTRE - g_RxFreqOffsetHz.load(std::memory_order_relaxed));
    m_panelSpectrum->setRxFreq(FDMDV_FCENTRE - g_RxFreqOffsetHz.load(std::memory_order_relaxed));

    g_TxFreqOffsetHz.store(0.0f, std::memory_order_relaxed);

    g_tx.store(false, std::memory_order_release);
    g_voice_keyer_tx.store(false, std::memory_order_release);

    // data states
    g_txDataInFifo.store(new GenericFIFO<short>(MAX_CALLSIGN*FREEDV_VARICODE_MAX_BITS), std::memory_order_release);
    g_rxDataOutFifo.store(new GenericFIFO<short>(MAX_CALLSIGN*FREEDV_VARICODE_MAX_BITS), std::memory_order_release);

    sox_biquad_start();

    g_testFrames = 0;
    g_test_frame_sync_state = 0;
    g_resyncs.store(0, std::memory_order_relaxed);
    wxGetApp().m_testFrames = false;
    wxGetApp().m_channel_noise = false;
    g_tone_phase.store(0.0f, std::memory_order_relaxed);

    m_schedule_restore = false;

    vk_state = VK_IDLE;

    m_timeSinceSyncLoss = 0;

    // Init optional Windows debug console so we can see all those printfs

#ifdef __WXMSW__
    if (wxGetApp().appConfiguration.debugConsoleEnabled || wxGetApp().appConfiguration.firstTimeUse) {
        // somewhere to send printfs while developing
        int ret = AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        log_info("AllocConsole: %d m_debug_console: %d", ret, wxGetApp().appConfiguration.debugConsoleEnabled.get());
    }
#endif
    
    // Print RADE API version. This also forces the RADE library to be linked.
    log_info("Using RADE API version %d", rade_version());

    if (wxGetApp().appConfiguration.firstTimeUse)
    {
        // Initial setup. Display Easy Setup dialog.
        CallAfter([&]() {
            EasySetupDialog* dlg = new EasySetupDialog(this);
            if (dlg->ShowModal() == wxOK)
            {
                // Show/hide frequency box based on CAT control configuration.
                m_freqBox->Show(isFrequencyControlEnabled_());

                // Show/hide callsign combo box based on PSK Reporter Status
                if (wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled)
                {
                    m_cboLastReportedCallsigns->Show();
                    m_txtCtrlCallSign->Hide();
                }
                else
                {
                    m_cboLastReportedCallsigns->Hide();
                    m_txtCtrlCallSign->Show();
                }

                // Relayout window so that the changes can take effect.
                m_panel->Layout();
            }
        });
    }
    else if (wxGetApp().appConfiguration.autoStartOnLaunch)
    {
        // Simulate a press of the Start button once the main window is up,
        // but only if the audio configuration is actually valid. Checking
        // silently first avoids popping up error dialogs (or the Easy Setup
        // dialog) at launch for users who haven't fully configured audio yet.
        CallAfter([&]() {
            if (!validateSoundCardSetup(true)) return;

            m_togBtnOnOff->SetValue(true);
            wxCommandEvent onEvent(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, m_togBtnOnOff->GetId());
            onEvent.SetEventObject(m_togBtnOnOff);
            OnTogBtnOnOff(onEvent);
        });
    }

    wxGetApp().appConfiguration.firstTimeUse = false;

    //#define FTEST
    #ifdef FTEST
    ftest = fopen("ftest.raw", "wb");
    assert(ftest != NULL);
    #endif

    /* experimental checkbox control of thread priority, used
       to helpo debug 700D windows sound break up */

    wxGetApp().m_txRxThreadHighPriority = true;
    g_dump_timing.store(0, std::memory_order_relaxed);
    g_dump_fifo_state.store(0, std::memory_order_relaxed);
}

void MainFrame::restoreCallsignListFromCsv_()
{
    auto csvPath = wxGetApp().appConfiguration.reportingConfiguration.csvLogFilePath.get();
    if (csvPath.IsEmpty())
        return;

    std::ifstream file(csvPath.ToStdString());
    if (!file.is_open())
        return;

    std::vector<std::string> lines;
    std::string line;
    bool firstLine = true;
    while (std::getline(file, line))
    {
        if (firstLine) { firstLine = false; continue; } // skip CSV header
        if (!line.empty())
            lines.push_back(line);
    }

    const int MAX_RESTORE_ROWS = 100;
    if ((int)lines.size() > MAX_RESTORE_ROWS)
        lines.erase(lines.begin(), lines.begin() + ((int)lines.size() - MAX_RESTORE_ROWS));

    bool freqAsKhz = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz;

    for (auto& csvLine : lines)
    {
        // CSV columns: date,time,callsign,mode,frequency_hz,snr_db
        std::istringstream ss(csvLine);
        std::string date, time, callsign, mode, freqStr, snrStr;
        if (!std::getline(ss, date, ',')) continue;
        if (!std::getline(ss, time, ',')) continue;
        if (!std::getline(ss, callsign, ',')) continue;
        if (!std::getline(ss, mode, ',')) continue;
        if (!std::getline(ss, freqStr, ',')) continue;
        if (!std::getline(ss, snrStr)) continue;

        uint64_t freqHz = 0;
        try { freqHz = std::stoull(freqStr); } catch (...) { continue; }

        wxString freqDisplay;
        if (freqAsKhz)
            freqDisplay = wxNumberFormatter::ToString(freqHz / 1000.0, 1);
        else
            freqDisplay = wxNumberFormatter::ToString(freqHz / 1000000.0, 4);

        wxString dateTime = wxString::Format("%s %s", date, time);

        int snrInt = 0;
        try { snrInt = std::stoi(snrStr); } catch (...) { continue; }
        wxString snrDisplay;
        snrDisplay.Printf(SNR_FORMAT_STR_NO_DB, (float)snrInt);

        // The log gets a row for every time a callsign is decoded, but while
        // on the air repeated decodes of the same station on the same
        // frequency only update the SNR of the top row. Do the same here so
        // the list looks the same after a restart.
        if (m_lastReportedCallsignListView->GetItemCount() > 0 &&
            m_lastReportedCallsignListView->GetItemText(0, 0) == wxString(callsign) &&
            m_lastReportedCallsignListView->GetItemText(0, 1) == freqDisplay)
        {
            m_lastReportedCallsignListView->SetItem(0, 3, snrDisplay);
            continue;
        }

        auto index = m_lastReportedCallsignListView->InsertItem(0, wxString(callsign), 0);
        m_lastReportedCallsignListView->SetItem(index, 1, freqDisplay);
        m_lastReportedCallsignListView->SetItem(index, 2, dateTime);
        m_lastReportedCallsignListView->SetItem(index, 3, snrDisplay);
        m_lastReportedCallsignListView->SetItemTextColour(index, wxColour(160, 160, 160));
    }

    for (int col = 0; col < 4; col++)
        m_lastReportedCallsignListView->SetColumnWidth(col, getIdealStationsHeardColumnLength_(col));
}

static std::recursive_mutex stoppingMutex;

void MainFrame::setConfiguration_(wxConfigBase* config)
{
    pConfig = config;
    wxConfigBase::Set(pConfig);
        
    if (wxGetApp().m_sharedReporterObject)
    {
        wxGetApp().m_sharedReporterObject = nullptr;
    }

    if (m_reporterDialog != nullptr)
    {
        m_reporterDialog->setReporter(nullptr);
        wxGetApp().SafeYield(nullptr, false); // make sure we handle any remaining Reporter messages before dispose
        m_reporterDialog->Close();
        m_reporterDialog->Destroy();
        m_reporterDialog = nullptr;
    }
    
    // Resets all configuration to defaults.
    loadConfiguration_();
}

void MainFrame::exportConfiguration_(wxConfigBase* config)
{
    if (!IsIconized()) {
        int w = 0;
        int h = 0;
        int x = 0;
        int y = 0;
        GetSize(&w, &h);
        GetPosition(&x, &y);
        
        wxGetApp().appConfiguration.mainWindowLeft = x;
        wxGetApp().appConfiguration.mainWindowTop = y;
        wxGetApp().appConfiguration.mainWindowWidth = w;
        wxGetApp().appConfiguration.mainWindowHeight = h;
    }

    if (tabLayoutPersistenceEnabledAtStartup_)
    {
#if wxCHECK_VERSION(3, 3, 0)
        TabLayoutSerializer serializer;
        m_auiNbookCtrl->SaveLayout("notebook", serializer);
        wxGetApp().appConfiguration.tabLayout = serializer.GetLayout();
#else
        wxGetApp().appConfiguration.tabLayout = ((TabFreeAuiNotebook*)m_auiNbookCtrl)->SavePerspective();
#endif // wxCHECK_VERSION(3, 3, 0)
    }


    wxGetApp().appConfiguration.squelchActive = g_SquelchActive.load(std::memory_order_acquire);
    wxGetApp().appConfiguration.squelchLevel = (int)(g_SquelchLevel.load(std::memory_order_acquire)*2.0);

    wxGetApp().appConfiguration.transmitLevel = g_txLevel;
    autoSaveCurrentBandLevels_(false);

    int mode = FREEDV_MODE_RADE;
    if (m_rb1600->GetValue())
        mode = 0;
    if (m_rb700d->GetValue())
        mode = 4;
    if (m_rb700e->GetValue())
        mode = 5;
    if (m_rbRADE->GetValue())
        mode = FREEDV_MODE_RADE;
    
    wxGetApp().appConfiguration.currentFreeDVMode = mode;
    wxGetApp().appConfiguration.save(config);
}

//-------------------------------------------------------------------------
// ~MainFrame()
//-------------------------------------------------------------------------
MainFrame::~MainFrame()
{
    delete voiceKeyerPopupMenu_;
    
    if (m_filterDialog != nullptr)
    {
        m_filterDialog->Close();
    }
    
    if (m_reporterDialog != nullptr)
    {
        // Grab and save the final position explicitly rather than relying
        // solely on OnMove's live tracking -- Close()/Destroy() below
        // trigger one last stray wxEVT_MOVE reporting a position with the
        // title bar's height already stripped off, so position tracking
        // must be stopped first or that stray event silently overwrites
        // the correct value saved here.
        auto pos = m_reporterDialog->GetPosition();
        wxGetApp().appConfiguration.reporterWindowLeft = pos.x;
        wxGetApp().appConfiguration.reporterWindowTop = pos.y;
        m_reporterDialog->stopTrackingPosition();

        m_reporterDialog->setReporter(nullptr);
        wxGetApp().SafeYield(nullptr, false); // make sure we handle any remaining Reporter messages before dispose
        m_reporterDialog->Close();
        m_reporterDialog->Destroy();
        m_reporterDialog = nullptr;
    }
    
#ifdef FTEST
    fclose(ftest);
    #endif

    // If the modem was stopped just before closing, the rig may still be
    // disconnecting on the threads started by performFreeDVOff_(), and will
    // call back into this object when it's done. Give that a bounded chance
    // to finish first, as OnTogBtnOnOff() does when closing while running.
    auto rigDisconnectDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    if (rigPttDisconnectFuture_.valid())
    {
        rigPttDisconnectFuture_.wait_until(rigDisconnectDeadline);
    }
    if (rigFreqDisconnectFuture_.valid())
    {
        rigFreqDisconnectFuture_.wait_until(rigDisconnectDeadline);
    }

    wxGetApp().rigPttController = nullptr;
    wxGetApp().rigFrequencyController = nullptr;
    wxGetApp().m_pttInSerialPort = nullptr;
    
    exportConfiguration_(pConfig);

    m_togBtnOnOff->Disconnect(wxEVT_UPDATE_UI, wxUpdateUIEventHandler(MainFrame::OnTogBtnOnOffUI), NULL, this);
    m_togBtnAnalog->Disconnect(wxEVT_UPDATE_UI, wxUpdateUIEventHandler(MainFrame::OnTogBtnAnalogClickUI), NULL, this);

    {
        std::unique_lock<std::recursive_mutex> lk(stoppingMutex);
        if (m_RxRunning.load(std::memory_order_acquire))
        {
            stopRxStream();
            freedvInterface.stop();
        }  
    }
    sox_biquad_finish();

    {
        std::unique_lock<std::mutex> lk(g_mutexProtectingPlayFiles);
        auto playFile = g_sfPlayFile.load(std::memory_order_acquire);
        if (playFile != NULL)
        {
            sf_close(playFile);
            g_sfPlayFile.store(NULL, std::memory_order_release);
            g_sfPlayFileReader = nullptr;
        }
    }
    if (g_sfRecFile.load(std::memory_order_acquire) != NULL)
    {
        sf_close(g_sfRecFile.load(std::memory_order_acquire));
        g_sfRecFile.store(NULL, std::memory_order_release);
    }
    if (g_sfRecFileFromModulator != NULL)
    {
        sf_close(g_sfRecFileFromModulator);
        g_sfRecFileFromModulator = NULL;
    }
#ifdef _USE_TIMER
    if(m_pskReporterTimer.IsRunning())
    {
        m_pskReporterTimer.Stop();
    }
    if(m_plotTimer.IsRunning())
    {
        m_plotTimer.Stop();
        m_plotWaterfallTimer.Stop();
        m_plotSpectrumTimer.Stop();
        m_plotSpeechInTimer.Stop();
        m_plotSpeechOutTimer.Stop();
        m_plotDemodInTimer.Stop();
        m_plotSNRTimer.Stop();
        Unbind(wxEVT_TIMER, &MainFrame::OnTimer, this);
    }
#endif //_USE_TIMER

#ifdef _USE_ONIDLE
    Disconnect(wxEVT_IDLE, wxIdleEventHandler(MainFrame::OnIdle), NULL, this);
#endif // _USE_ONIDLE

    if (optionsDlg != NULL) {
        delete optionsDlg;
        optionsDlg = NULL;
    }

    wxGetApp().rigFrequencyController = nullptr;
    wxGetApp().rigPttController = nullptr;
    wxGetApp().m_reporters.clear();

    // Clean up RADE.
    rade_finalize();
    
    auto engine = AudioEngineFactory::GetAudioEngine();
    engine->stop();
    engine->setOnEngineError(nullptr, nullptr);
}


#ifdef _USE_ONIDLE
void MainFrame::OnIdle(wxIdleEvent &) {
}
#endif

int MainFrame::getIdealStationsHeardColumnLength_(int col)
{
    int curColWidth = m_lastReportedCallsignListView->GetColumnWidth(col);
    
    for (int index = 0; index < m_lastReportedCallsignListView->GetItemCount(); index++)
    {
        auto itemText = m_lastReportedCallsignListView->GetItemText(index, col);
        wxSize itemSize = m_togBtnVoiceKeyer->GetTextExtent(itemText);
        
        curColWidth = std::max(curColWidth, itemSize.GetWidth() + 10); // 10px buffer around text
    }
    
    return curColWidth;
}

#ifdef _USE_TIMER
//----------------------------------------------------------------
// OnTimer()
//
// when the timer fires every DT seconds we update the GUI displays.
// the tabs only the plot that is visible actually gets updated, this
// keeps CPU load reasonable
//----------------------------------------------------------------
void MainFrame::OnTimer(wxTimerEvent &evt)
{
    short speechInPlotSamples[WAVEFORM_PLOT_BUF];
    short speechOutPlotSamples[WAVEFORM_PLOT_BUF];
    short demodInPlotSamples[WAVEFORM_PLOT_BUF];
    bool txState = false;
    bool halfDuplexState = false;
    int syncState = 0;

    auto& timer = evt.GetTimer();
    auto timerId = timer.GetId();
    if (!m_RxRunning.load(std::memory_order_acquire) || !timer.IsRunning())
    {
        return;
    }
    
    // Most plots don't need TX/sync state.
    if (timerId == ID_TIMER_UPDATE_OTHER || timerId == ID_TIMER_SNR || timerId == ID_TIMER_DEMOD_IN)
    {
        txState = g_tx.load(std::memory_order_relaxed);
        halfDuplexState = g_half_duplex.load(std::memory_order_relaxed);
        syncState_ = freedvInterface.getSync();
    }
    syncState = syncState_;

    if (timerId == ID_TIMER_PSKREPORTER)
    {
        // Reporter timer fired; send in-progress packet.
        for (auto& obj : wxGetApp().m_reporters)
        {
            obj->send();
        }
    }
    else if (timerId == ID_TIMER_UPD_FREQ)
    {
        // show freq. and mode [UP]
        if (wxGetApp().rigFrequencyController && wxGetApp().rigFrequencyController->isConnected()) 
        {
            log_debug("update freq and mode ...."); 
            wxGetApp().rigFrequencyController->requestCurrentFrequencyMode();
        }
     }
     else if (timerId == ID_TIMER_WATERFALL)
     {
          if (m_panelWaterfall->checkDT()) {
              m_panelWaterfall->setRxFreq(FDMDV_FCENTRE - g_RxFreqOffsetHz.load(std::memory_order_relaxed));
              m_panelWaterfall->m_newdata = true;
              m_panelWaterfall->setColor(wxGetApp().appConfiguration.waterfallColor);
              m_panelWaterfall->addOffset(freedvInterface.getCurrentRxModemOffset());
              m_panelWaterfall->setSync(syncState ? true : false);
              m_panelWaterfall->Refresh();
          }
      }
      else if (timerId == ID_TIMER_SPECTRUM)
      {
          m_panelSpectrum->setRxFreq(FDMDV_FCENTRE - g_RxFreqOffsetHz.load(std::memory_order_relaxed));
    
          // Note: each element in this combo box is a numeric value starting from 1,
          // so just incrementing the selected index should get us the correct results.
          m_panelSpectrum->setNumAveraging(wxGetApp().appConfiguration.currentSpectrumAveraging + 1);
          m_panelSpectrum->addOffset(freedvInterface.getCurrentRxModemOffset());
          m_panelSpectrum->setSync(syncState ? true : false);
          m_panelSpectrum->m_newdata = true;
          m_panelSpectrum->Refresh();
      }
      else if (timerId == ID_TIMER_SPEECH_IN)
      {
          if (g_plotSpeechInFifo.read(speechInPlotSamples, WAVEFORM_PLOT_BUF)) {
              memset(speechInPlotSamples, 0, WAVEFORM_PLOT_BUF*sizeof(short));
          }
          m_panelSpeechIn->add_new_short_samples(speechInPlotSamples, WAVEFORM_PLOT_BUF, 32767);
          m_panelSpeechIn->refreshData();
      }
      else if (timerId == ID_TIMER_SPEECH_OUT)
      {
          if (g_plotSpeechOutFifo.read(speechOutPlotSamples, WAVEFORM_PLOT_BUF))
              memset(speechOutPlotSamples, 0, WAVEFORM_PLOT_BUF*sizeof(short));
          m_panelSpeechOut->add_new_short_samples(speechOutPlotSamples, WAVEFORM_PLOT_BUF, 32767);
          m_panelSpeechOut->refreshData();
      }
      else if (timerId == ID_TIMER_DEMOD_IN)
      {
          if (g_plotDemodInFifo.read(demodInPlotSamples, WAVEFORM_PLOT_BUF)) {
              memset(demodInPlotSamples, 0, WAVEFORM_PLOT_BUF*sizeof(short));
          }
          m_panelDemodIn->add_new_short_samples(demodInPlotSamples, WAVEFORM_PLOT_BUF, 32767);
          m_panelDemodIn->refreshData();
      }
      else if (timerId == ID_TIMER_SNR)
      {
          // Only the SNR plot here: everything below goes by time passing
          // (the SNR filter, the voice keyer, how long since sync was lost)
          // and so must only happen once per DT, on ID_TIMER_UPDATE_OTHER.
          if (!halfDuplexState || !txState)
          {
              float snr = freedvInterface.getSync() ? g_snr : NO_SNR_VAL;
              snr = std::min(snr, (float)MAX_SNR_VAL);
              snr = std::max(snr, (float)NO_SNR_VAL);
              m_panelSNR->add_new_sample(snr);
              m_panelSNR->refreshData();
          }
      }
      else
      {
         // Update average magnitudes
         float rxSpectrum[MODEM_STATS_NSPEC];
         memset(rxSpectrum, 0, sizeof(float) * MODEM_STATS_NSPEC);
         bool txNotInFullDuplex = halfDuplexState && txState;
         if (!txNotInFullDuplex)
         {
             while (g_avmag.numUsed() >= MODEM_STATS_NSPEC)
             {
                 g_avmag.read(rxSpectrum, MODEM_STATS_NSPEC);
                 for (int index = 0; index < MODEM_STATS_NSPEC; index++)
                 {
                     g_avmag_waterfall[index] = BETA * g_avmag_waterfall[index] + (1.0 - BETA) * rxSpectrum[index];
                 }
                 memcpy(g_avmag_spectrum, g_avmag_waterfall, sizeof(g_avmag_waterfall));
              }
         }
         else
         {
            // Assume zero spectrum to avoid waterfall artifacts. Note:
            // this display's dB scale runs 0 (loudest) to MIN_MAG_DB
            // (quietest) -- memset()'ing to zero bytes sets every bin to
            // 0.0 dB, the *loudest* possible value, not silence, which
            // painted the waterfall solid instead of blanking it. Fill
            // with MIN_MAG_DB instead so it actually reads as quiet/black.
            std::fill_n(g_avmag_waterfall, MODEM_STATS_NSPEC, MIN_MAG_DB);
            memcpy(g_avmag_spectrum, g_avmag_waterfall, sizeof(g_avmag_waterfall));
         }

         // Synchronize changes with Filter dialog
         if (m_filterDialog != nullptr)
         {
             // Sync Filter dialog as well
             m_filterDialog->syncVolumes();

             if (m_filterDialog->haveVolumesBeenChanged())
             {
                auto sliderVal = 0.0;
                if (txState)
                {
                    sliderVal = wxGetApp().appConfiguration.filterConfiguration.micInChannel.volInDB;
                }
                else
                {
                    sliderVal = wxGetApp().appConfiguration.filterConfiguration.spkOutChannel.volInDB;
                }

                if ((sliderVal * 10) != m_sliderMicSpkrLevel->GetValue())
                {
                    m_sliderMicSpkrLevel->SetValue(sliderVal * 10);
                    m_sliderMicSpkrLevel->Refresh();
                    wxString fmt = wxString::Format(MIC_SPKR_LEVEL_FORMAT_STR, wxNumberFormatter::ToString((double)sliderVal, 1), DECIBEL_STR);
                    m_txtMicSpkrLevelNum->SetLabel(fmt);
                }
            }
         }

        // SNR text box and gauge ------------------------------------------------------------

        // LP filter freedvInterface.getCurrentRxModemStats()->snr_est some more to stabilise the
        // display. freedvInterface.getCurrentRxModemStats()->snr_est already has some low pass filtering
        // but we need it fairly fast to activate squelch.  So we
        // optionally perform some further filtering for the display
        // version of SNR.  The "Slow" checkbox controls the amount of
        // filtering.  The filtered snr also controls the squelch

        float snr_limited;
        // some APIs pass us invalid values, so lets trap it rather than bombing
        float snrEstimate = freedvInterface.getSNREstimate();
        if (!(isnan(snrEstimate) || isinf(snrEstimate)) && syncState) {
            g_snr = m_snrBeta*g_snr + (1.0 - m_snrBeta)*snrEstimate;
        }
        snr_limited = g_snr;
        if (snr_limited < -5.0) snr_limited = -5.0;
        if (snr_limited > 40.0) snr_limited = 40.0;
        wxString snrString = wxString::Format(SNR_FORMAT_STR, (int)(g_snr + 0.5));

        if (syncState)
        {
            m_textSNR->SetLabel(snrString);
            m_gaugeSNR->SetValue((int)(snr_limited+5));
        }
        else
        {
            m_textSNR->SetLabel(NO_SNR_LABEL);
            m_gaugeSNR->SetValue(0);
        }

        // sync LED (Colours don't work on Windows) ------------------------

        auto state = g_State.load(std::memory_order_acquire);
        if (m_textSync->IsEnabled())
        {
            auto oldColor = m_textSync->GetForegroundColour();
            wxColour newColor = state ? wxColour( 0, 255, 0 ) : wxColour( 255, 0, 0 ); // green if sync, red otherwise
        
            if (state) 
            {
                if (g_prev_State.load(std::memory_order_acquire) == 0) 
                {
                    g_resyncs.fetch_add(1, std::memory_order_relaxed);
                
                    // Auto-reset stats if we've gone long enough since losing sync.
                    // NOTE: m_timeSinceSyncLoss is in milliseconds.
                    if (m_timeSinceSyncLoss >= wxGetApp().appConfiguration.statsResetTimeSecs * 1000)
                    {
                        resetStats_();
                        
                        // Clear RX text to reduce the incidence of incorrect callsigns extracted with
                        // the PSK Reporter callsign extraction logic.
                        m_txtCtrlCallSign->SetValue(EMPTY_STR);
                        m_cboLastReportedCallsigns->SetValue(EMPTY_STR);
                        m_cboLastReportedCallsigns->Enable(m_lastReportedCallsignListView->GetItemCount() > 0);
                        memset(m_callsign, 0, MAX_CALLSIGN);
                        m_pcallsign = m_callsign;
            
                        // Get current time to enforce minimum sync time requirement for PSK Reporter.
                        g_sync_time = time(0);
            
                        freedvInterface.resetReliableText();
                    }
                }
                m_timeSinceSyncLoss = 0;
            }
            else
            {
                // Counts the amount of time since losing sync. Once we exceed
                // wxGetApp().appConfiguration.statsResetTimeSecs, we will reset the stats. 
                m_timeSinceSyncLoss += _REFRESH_TIMER_PERIOD;
            }
        
            if (oldColor != newColor)
            {
                m_textSync->SetForegroundColour(newColor);
                m_textSync->SetLabel(MODEM_LABEL);
                m_textSync->Refresh();
            }
        }
        g_prev_State.store(state, std::memory_order_release);

        // send Callsign ----------------------------------------------------

        char callsign[MAX_CALLSIGN];
        memset(callsign, 0, MAX_CALLSIGN);
    
        if (!wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled)
        {
            strncpy(callsign, (const char*) wxGetApp().appConfiguration.reportingConfiguration.reportingFreeTextString->mb_str(wxConvUTF8), MAX_CALLSIGN - 2);
            if (strlen(callsign) < MAX_CALLSIGN - 1)
            {
                strncat(callsign, "\r", 2);
            }     
     
            // buffer 1 txt message to ensure tx data fifo doesn't "run dry"
            char* sendBuffer = &callsign[0];
            auto txDataFifo = g_txDataInFifo.load(std::memory_order_acquire);
            if ((unsigned)txDataFifo->numUsed() < strlen(sendBuffer)) {
                unsigned int  i;

                // write chars to tx data fifo
                for(i = 0; i < strlen(sendBuffer); i++) {
                    short ashort = (unsigned char)sendBuffer[i];
                    txDataFifo->write(&ashort, 1);
                }
            }

            // See if any Callsign info received --------------------------------

            short ashort;
            auto rxDataFifo = g_rxDataOutFifo.load(std::memory_order_acquire);
            while (rxDataFifo != nullptr && rxDataFifo->read(&ashort, 1) == 0) {
                unsigned char incomingChar = (unsigned char)ashort;
        
                // Pre-1.5.1 behavior, where text is handled as-is.
                if (incomingChar == '\r' || incomingChar == '\n' || incomingChar == 0 || ((m_pcallsign - m_callsign) > MAX_CALLSIGN-1))
                {                        
                    // CR completes line. Fill in remaining positions with zeroes.
                    if ((m_pcallsign - m_callsign) <= MAX_CALLSIGN-1)
                    {
                        memset(m_pcallsign, 0, MAX_CALLSIGN - (m_pcallsign - m_callsign));
                    }
            
                    // Reset to the beginning.
                    m_pcallsign = m_callsign;
                }
                else
                {
                    *m_pcallsign++ = incomingChar;
                }
                m_txtCtrlCallSign->SetValue(m_callsign);
            }
        }

        // We should only report to reporters when all of the following are true:
        // a) The callsign encoder indicates a valid callsign has been received.
        // b) We detect a valid format callsign in the text (see https://en.wikipedia.org/wiki/Amateur_radio_call_signs).
        // c) We don't currently have a pending report to add to the outbound list for the active callsign.
        // When the above is true, capture the callsign and current SNR and add to the PSK Reporter object's outbound list.
        if (wxGetApp().m_reporters.size() > 0 && wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled)
        {
            const char* text = freedvInterface.getReliableText();
            assert(text != nullptr);

            wxString wxCallsign; 
            if (text != nullptr && strlen(text) > 0) wxCallsign = text;

            delete[] text;
        
            auto pendingSnr = (int)(g_snr + 0.5);
            if (wxCallsign.Length() > 0)
            {
                freedvInterface.resetReliableText();

                wxRegEx callsignFormat(CALLSIGN_FORMAT_RGX);
                if (callsignFormat.Matches(wxCallsign))
                {
                    wxString rxCallsign = callsignFormat.GetMatch(wxCallsign, 1);
                    std::string pendingCallsign = rxCallsign.ToStdString();

                    wxString freqString;
                    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
                    {
                        double freq = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency.get() / 1000.0;
                        freqString = wxNumberFormatter::ToString(freq, 1);
                    }
                    else
                    {
                        double freq = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency.get() / 1000000.0;
                        freqString = wxNumberFormatter::ToString(freq, 4);
                    }

                    if ((m_lastReportedCallsignListView->GetItemCount() == 0 || 
                        m_lastReportedCallsignListView->GetItemText(0, 0) != rxCallsign ||
                        m_lastReportedCallsignListView->GetItemText(0, 1) != freqString) ||
                        (m_lastReportedCallsignListView->GetItemCount() > 0 && m_lastReportedCallsignListView->GetItemTextColour(0) == wxColour(160, 160, 160)))
                    {
                        auto currentTime = wxDateTime::Now();
                        wxString currentTimeAsString = EMPTY_STR;
                        
                        if (wxGetApp().appConfiguration.reportingConfiguration.useUTCForReporting)
                        {
                            currentTime = currentTime.ToUTC();
                        }
                        currentTimeAsString.Printf(CURRENT_TIME_FORMAT_STR, currentTime.FormatISODate(), currentTime.FormatISOTime());
                        
                        auto index = m_lastReportedCallsignListView->InsertItem(0, rxCallsign, 0);
                        m_lastReportedCallsignListView->SetItem(index, 1, freqString);
                        m_lastReportedCallsignListView->SetItem(index, 2, currentTimeAsString);

                        // Make sure all columns are wide enough to show contents
                        m_lastReportedCallsignListView->SetColumnWidth(0, getIdealStationsHeardColumnLength_(0));
                        m_lastReportedCallsignListView->SetColumnWidth(1, getIdealStationsHeardColumnLength_(1));
                        m_lastReportedCallsignListView->SetColumnWidth(2, getIdealStationsHeardColumnLength_(2));
                    }
                    
                    wxString snrAsString;
                    snrAsString.Printf(SNR_FORMAT_STR_NO_DB, g_snr);
                    auto index = m_lastReportedCallsignListView->GetTopItem();
                    m_lastReportedCallsignListView->SetItem(index, 3, snrAsString);
                    m_lastReportedCallsignListView->SetColumnWidth(3, getIdealStationsHeardColumnLength_(3));
                    
                    m_cboLastReportedCallsigns->SetText(rxCallsign);
                    m_cboLastReportedCallsigns->Enable(m_lastReportedCallsignListView->GetItemCount() > 0);
           
                    if (wxGetApp().appConfiguration.rigControlConfiguration.hamlibUseForPTT)
                    {
                        wxGetApp().rigFrequencyController->requestCurrentFrequencyMode();
                    }
            
                    int64_t freq = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency;

                    // Only report if there's a valid reporting frequency and if we're not playing 
                    // a recording through ourselves (to avoid false reports).
                    if (freq > 0)
                    {
                        long long freqLongLong = freq;
                        log_info(
                            "Reporting callsign %s @ SNR %d, freq %lld to reporting services.\n", 
                            pendingCallsign.c_str(), 
                            pendingSnr,
                            freqLongLong);
        
                        if (!g_playFileFromRadio.load(std::memory_order_acquire))
                        {
                            for (auto& obj : wxGetApp().m_reporters)
                            {
                                obj->addReceiveRecord(
                                    pendingCallsign,
                                    freedvInterface.getCurrentModeStr(),
                                    freq,
                                    pendingSnr);
                            }
                        }
                    }
                }
            }
            else if (
                wxGetApp().m_sharedReporterObject && 
                freedvInterface.getCurrentMode() == FREEDV_MODE_RADE && 
                syncState)
            {               
                // Special case for RADE--report '--' for callsign so we can
                // at least report that we're receiving *something*.
                int64_t freq = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency;

                // Only report if there's a valid reporting frequency and if we're not playing 
                // a recording through ourselves (to avoid false reports).
                wxGetApp().m_reportCounter = (wxGetApp().m_reportCounter + 1) % 10;
                if (freq > 0 && wxGetApp().m_reportCounter == 0)
                {
                    wxGetApp().m_reportCounter = 0;
                    if (!g_playFileFromRadio.load(std::memory_order_acquire))
                    {                
                        wxGetApp().m_sharedReporterObject->addReceiveRecord(
                            "",
                            freedvInterface.getCurrentModeStr(),
                            freq,
                            pendingSnr
                        );
                    }
                }
            }
        }
    
        // Run time update of EQ filters -----------------------------------

        g_mutexProtectingCallbackData.Lock();

        bool micEqEnableOld = g_rxUserdata->micInEQEnable.load(std::memory_order_relaxed);
        bool spkrEqEnableOld = g_rxUserdata->spkOutEQEnable.load(std::memory_order_relaxed);

        if (m_newMicInFilter.load(std::memory_order_relaxed) || m_newSpkOutFilter.load(std::memory_order_relaxed) ||
            micEqEnableOld != wxGetApp().appConfiguration.filterConfiguration.micInChannel.eqEnable ||
            spkrEqEnableOld != wxGetApp().appConfiguration.filterConfiguration.spkOutChannel.eqEnable) {
            
            deleteEQFilters(g_rxUserdata);
        
            g_rxUserdata->micInEQEnable.store(wxGetApp().appConfiguration.filterConfiguration.micInChannel.eqEnable, std::memory_order_relaxed);
            g_rxUserdata->spkOutEQEnable.store(wxGetApp().appConfiguration.filterConfiguration.spkOutChannel.eqEnable, std::memory_order_relaxed);

            if (m_newMicInFilter.load(std::memory_order_relaxed) || m_newSpkOutFilter.load(std::memory_order_relaxed))
            {
                if (g_nSoundCards.load(std::memory_order_relaxed) == 1)
                {
                    designEQFilters(g_rxUserdata, wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate, 0);
                }
                else
                {   
                    designEQFilters(g_rxUserdata, wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate, wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate);
                }
            }

            m_newMicInFilter.store(false, std::memory_order_relaxed);
            m_newSpkOutFilter.store(false, std::memory_order_relaxed);
        }
        g_mutexProtectingCallbackData.Unlock();
    
        // set some run time options (if applicable)
        freedvInterface.setRunTimeOptions(
            (int)wxGetApp().appConfiguration.freedv700Clip,
            (int)wxGetApp().appConfiguration.freedv700TxBPF);

        // Test Frame Bit Error Updates ------------------------------------

        // Toggle test frame mode at run time

        if (!freedvInterface.usingTestFrames() && wxGetApp().m_testFrames) {
            // reset stats on check box off to on transition
            freedvInterface.resetTestFrameStats();
        }
        freedvInterface.setTestFrames(wxGetApp().m_testFrames, wxGetApp().m_FreeDV700Combine);
        g_channel_noise.store(wxGetApp().m_channel_noise, std::memory_order_release);

        // update stats on main page
        wxString modeString; 
        if (g_mode == FREEDV_MODE_RADE) modeString = MODE_RADE_FORMAT_STR; // optimization to reduce allocs
        else modeString = wxString::Format(MODE_FORMAT_STR, freedvInterface.getCurrentModeStr());
        bool relayout = 
            m_textCurrentDecodeMode->GetLabel() != modeString &&
            !realigned_;
        m_textCurrentDecodeMode->SetLabel(modeString);
        if (relayout)
        {
            // XXX - force resize events to make mode string re-center itself.
            wxSize minSize = GetSize();
            auto w = minSize.GetWidth();
            auto h = minSize.GetHeight();

            CallAfter([=, this]()
            {
                SetSize(w, h);
            });
            CallAfter([=, this]()
            {
                SetSize(w + 1, h + 1);
            });
            CallAfter([=, this]()
            {
                SetSize(w, h);
            });
            
            realigned_ = true;
        }

        wxString freqOffset = wxString::Format(FRQ_OFF_FMT, freedvInterface.getCurrentRxModemOffset());
        m_textFreqOffset->SetLabel(freqOffset);

        if (g_mode == FREEDV_MODE_RADE)
        {
            m_textBits->SetLabel(BITS_UNK_LABEL);
            m_textErrors->SetLabel(ERRS_UNK_LABEL);
            m_textBER->SetLabel(BER_UNK_LABEL);
            m_textSyncMetric->SetLabel(SYNC_UNK_LABEL);
            m_textCodec2Var->SetLabel(VAR_UNK_LABEL);
        }
        else
        {
            wxString bits = wxString::Format(BITS_FMT, freedvInterface.getTotalBits()); 
            m_textBits->SetLabel(bits);

            wxString errors = wxString::Format(ERRS_FMT, freedvInterface.getTotalBitErrors()); 
            m_textErrors->SetLabel(errors);

            float b = (float)freedvInterface.getTotalBitErrors()/(1E-6+freedvInterface.getTotalBits());
            wxString ber = wxString::Format(BER_FMT, b); 
            m_textBER->SetLabel(ber);

            wxString resyncs = wxString::Format(RESYNC_FMT, g_resyncs.load(std::memory_order_relaxed));
            m_textResyncs->SetLabel(resyncs);

            wxString syncMetric = wxString::Format(SYNC_FMT, freedvInterface.getCurrentRxModemStats()->sync_metric);
            m_textSyncMetric->SetLabel(syncMetric);

            // Codec 2 700D/E "auto EQ" equaliser variance
            auto var = freedvInterface.getVariance();
            wxString var_string = wxString::Format(VAR_FMT, var);
            m_textCodec2Var->SetLabel(var_string);
        }

        if (state) {

            if (g_mode == FREEDV_MODE_RADE)
            {
                m_textClockOffset->SetLabel(CLK_OFF_UNK_LABEL);
            }
            else
            {
                wxString clockOffset = wxString::Format(CLK_OFF_FMT, (int)round(freedvInterface.getCurrentRxModemStats()->clock_offset*1E6) % 10000);
                m_textClockOffset->SetLabel(clockOffset);
            }
        }

        /* FIFO and PortAudio under/overflow debug counters */
        if (optionsDlg != nullptr)
        {
            optionsDlg->DisplayFifoPACounters();
        }

        // command from UDP thread that is best processed in main thread to avoid seg faults

        if (m_schedule_restore) {
            if (IsIconized())
                Restore();
            m_schedule_restore = false;
        }
    
        // Voice Keyer state machine
        VoiceKeyerProcessEvent(VK_DT);
    }
    
    if (timerId == ID_TIMER_SPEECH_IN ||
        timerId == ID_TIMER_DEMOD_IN)
    {
        // Level Gauge -----------------------------------------------------------------------

        bool updated = false;
        if (timerId == ID_TIMER_DEMOD_IN && !txState && m_RxRunning.load(std::memory_order_acquire))
        {
            // receive mode - display From Radio peaks
            int maxDemodIn = 0;
            for(int i=0; i<WAVEFORM_PLOT_BUF; i++)
                if (maxDemodIn < abs(demodInPlotSamples[i]))
                    maxDemodIn = abs(demodInPlotSamples[i]);

            // peak from last second
            if (maxDemodIn > m_maxLevel)
                m_maxLevel = maxDemodIn;

            updated = true;
        }
        else if (timerId == ID_TIMER_SPEECH_IN)
        {
            // transmit mode - display From Mic peaks

            // peak from this DT sampling period
            int maxSpeechIn = 0;
            for(int i=0; i<WAVEFORM_PLOT_BUF; i++)
                if (maxSpeechIn < abs(speechInPlotSamples[i]))
                    maxSpeechIn = abs(speechInPlotSamples[i]);

            // peak from last second
            if (maxSpeechIn > m_maxLevel)
                m_maxLevel = maxSpeechIn;

           updated = true;
        }

        if (updated)
        {
            // Peak Reading meter: updates peaks immediately, then slowly decays
            int maxScaled = (int)(100.0 * ((float)m_maxLevel/32767.0));
            m_gaugeLevel->SetValue(maxScaled);
            m_maxLevel *= LEVEL_BETA;
        }
    }
}
#endif

void MainFrame::topFrame_OnClose( wxCloseEvent& event )
{
    if (terminating_)
    {
        // A previous close request already kicked off the async RX/PTT
        // shutdown below, which calls Destroy() itself once done (see
        // OnTogBtnOnOff()) -- nothing left to do here. This guards more than
        // just the m_reporterDialog dereference below: m_RxRunning isn't
        // cleared until deep inside that shutdown (stopRxStream(), called
        // after the rig-disconnect code), so a repeat close request arriving
        // first would otherwise fall into the `if (m_RxRunning)` block again
        // and re-enter OnTogBtnOnOff() a second time concurrently with the
        // shutdown already in progress.
        return;
    }

    if (closingReporterDialog_)
    {
        // Another request to close (say, a second click on the close button)
        // while the one before it waits below for the Reporter dialog's last
        // messages. Going on would destroy the dialog, and this window, out
        // from under that one, which finishes closing by itself.
        if (event.CanVeto())
        {
            event.Veto();
        }
        return;
    }

    if (m_reporterDialog != nullptr)
    {
        // Grab and save the final position explicitly rather than relying
        // solely on OnMove's live tracking -- Close()/Destroy() below trigger
        // one last stray wxEVT_MOVE reporting a position with the title bar's
        // height already stripped off, so position tracking must be stopped
        // first or that stray event silently overwrites the correct value
        // saved here.
        auto pos = m_reporterDialog->GetPosition();
        wxGetApp().appConfiguration.reporterWindowLeft = pos.x;
        wxGetApp().appConfiguration.reporterWindowTop = pos.y;
        m_reporterDialog->stopTrackingPosition();

        m_reporterDialog->setReporter(nullptr);
        closingReporterDialog_ = true;
        wxGetApp().SafeYield(nullptr, false); // make sure we handle any remaining Reporter messages before dispose
        closingReporterDialog_ = false;
        m_reporterDialog->Close();
        m_reporterDialog->Destroy();
        m_reporterDialog = nullptr;
    }

    if (m_RxRunning.load(std::memory_order_acquire))
    {
        if (m_btnTogPTT->GetValue())
        {
            // Stop PTT first
            togglePTT();
        }
        
        // Stop execution.
        terminating_ = true;
        wxCommandEvent* offEvent = new wxCommandEvent(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, m_togBtnOnOff->GetId());
        offEvent->SetEventObject(m_togBtnOnOff);
        m_togBtnOnOff->SetValue(false);
        OnTogBtnOnOff(*offEvent);
        delete offEvent;
    } 
    else
    {
        TopFrame::topFrame_OnClose(event);
    }
}

//-------------------------------------------------------------------------
// OnExit()
//-------------------------------------------------------------------------
void MainFrame::OnExit(wxCommandEvent&)
{
    if (m_RxRunning.load(std::memory_order_acquire))
    {
        if (m_btnTogPTT->GetValue())
        {
            // Stop PTT first
            togglePTT();
        }
        
        // Stop execution.
        terminating_ = true;
        wxCommandEvent* offEvent = new wxCommandEvent(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, m_togBtnOnOff->GetId());
        offEvent->SetEventObject(m_togBtnOnOff);
        m_togBtnOnOff->SetValue(false);
        OnTogBtnOnOff(*offEvent);
        delete offEvent;
    } 
    else
    {
        Destroy();
    }
}

void MainFrame::OnChangeTxMode( wxCommandEvent& event )
{
    auto eventObject = (wxRadioButton*)event.GetEventObject();
    auto newMode = g_mode;
    if (eventObject == m_rb1600 || (eventObject == nullptr && m_rb1600->GetValue())) 
    {
        newMode = FREEDV_MODE_1600;
    }
    else if (eventObject == m_rbRADE || (eventObject == nullptr && m_rbRADE->GetValue())) 
    {
        newMode = FREEDV_MODE_RADE;
    }
    else if (eventObject == m_rb700d || (eventObject == nullptr && m_rb700d->GetValue())) 
    {
        newMode = FREEDV_MODE_700D;
    }
    else if (eventObject == m_rb700e || (eventObject == nullptr && m_rb700e->GetValue())) 
    {
        newMode = FREEDV_MODE_700E;
    }

    if (newMode != g_mode)
    {
        g_mode = newMode; 
        if (freedvInterface.isRunning())
        {
            // Need to change the TX interface live.
            freedvInterface.changeTxMode(g_mode);
        }
    
        // Force recreation of EQ filters.
        m_newMicInFilter.store(true, std::memory_order_release);
        m_newSpkOutFilter.store(true, std::memory_order_release);
    
        // Report TX change to registered reporters
        auto txStatus = g_tx.load(std::memory_order_acquire);
        for (auto& obj : wxGetApp().m_reporters)
        {
            obj->transmit(freedvInterface.getCurrentTxModeStr(), txStatus);
        }
    
        // Disable controls not supported by RADE
        bool isEnabled = g_mode != FREEDV_MODE_RADE;
        m_sliderSQ->Enable(isEnabled);
        m_ckboxSQ->Enable(isEnabled);
        m_textSQ->Enable(isEnabled);
        m_btnCenterRx->Enable(isEnabled);
        m_BtnReSync->Enable(isEnabled);
    }
}

void MainFrame::performFreeDVOn_()
{
    log_debug("Start .....");
    isModemRunning.store(false, std::memory_order_release);
    g_queueResync.store(false, std::memory_order_release);
    endingTx.store(false, std::memory_order_release);
    g_voice_keyer_tx.store(false, std::memory_order_release);
    g_tx.store(false, std::memory_order_release);

    // Nothing from a previous session should keep RX muted.
    g_rxMutedUntilNs.store(0, std::memory_order_release);
    
    m_timeSinceSyncLoss = 0;
    syncState_ = false;

    executeOnUiThreadAndWait_([&]() 
    {
        // Blank out spectrum plots. Note: this display's dB scale runs 0
        // (loudest) to MIN_MAG_DB (quietest) -- memset()'ing to zero bytes
        // sets every bin to 0.0 dB, the *loudest* possible value, not
        // silence. Fill with MIN_MAG_DB instead so it actually reads as
        // quiet/black.
        std::fill_n(g_avmag_waterfall, MODEM_STATS_NSPEC, MIN_MAG_DB);
        std::fill_n(g_avmag_spectrum, MODEM_STATS_NSPEC, MIN_MAG_DB);

        // Reset plot FIFOs
        g_plotDemodInFifo.reset();
        g_plotSpeechOutFifo.reset();
        g_plotSpeechInFifo.reset();

        m_txtCtrlCallSign->SetValue(wxT(""));
        m_lastReportedCallsignListView->DeleteAllItems();
        restoreCallsignListFromCsv_();
        m_cboLastReportedCallsigns->Enable(m_lastReportedCallsignListView->GetItemCount() > 0);
        m_cboLastReportedCallsigns->SetText(wxT(""));
        
        m_logQSO->Disable();
    });
    
    memset(m_callsign, 0, MAX_CALLSIGN);
    m_pcallsign = m_callsign;

    freedvInterface.resetReliableText();
    
    //
    // Start Running -------------------------------------------------
    //

    vk_state = VK_IDLE;

    // modify some button states when running
    executeOnUiThreadAndWait_([&]() 
    {
        m_textSync->Enable();
        m_textCurrentDecodeMode->Enable();
        
        // determine what mode we are using
        wxCommandEvent tmpEvent;
        OnChangeTxMode(tmpEvent);

        if (!wxGetApp().appConfiguration.enableLegacyModes || !wxGetApp().appConfiguration.multipleReceiveEnabled || m_rbRADE->GetValue())
        {
            m_rb1600->Disable();
            m_rbRADE->Disable();
            m_rb700d->Disable();
            m_rb700e->Disable();
            
            if (!wxGetApp().appConfiguration.enableLegacyModes)
            {
                // If legacy modes are not enabled, RADE is the only option.
                g_mode = FREEDV_MODE_RADE;
            }
            freedvInterface.addRxMode(g_mode);
        }
        else
        {
            m_rbRADE->Disable();
                    
            int rxModes[] = {
                FREEDV_MODE_1600,
                FREEDV_MODE_700E,
                FREEDV_MODE_700D,
            };

            for (auto& mode : rxModes)
            {
                freedvInterface.addRxMode(mode);
            }
        
            // If we're receive-only, it doesn't make sense to be able to change TX mode.
            if (g_nSoundCards.load(std::memory_order_acquire) <= 1)
            {
                m_rb1600->Disable();
                m_rbRADE->Disable();
                m_rb700d->Disable();
                m_rb700e->Disable();
            }
        }
        
        // Default voice keyer sample rate to 8K. The exact voice keyer
        // sample rate will be determined when the .wav file is loaded.
        g_sfTxFs.store(FS, std::memory_order_release);
    
        wxGetApp().m_prevMode = g_mode;
        freedvInterface.start(g_mode, wxGetApp().appConfiguration.fifoSizeMs, !wxGetApp().appConfiguration.multipleReceiveEnabled || wxGetApp().appConfiguration.multipleReceiveOnSingleThread, wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled, wxGetApp().appConfiguration.debugVerbose);

        // Codec 2 VQ Equaliser
        freedvInterface.setEq(wxGetApp().appConfiguration.filterConfiguration.enable700CEqualizer);

        // Codec2 verbosity setting
        freedvInterface.setVerbose(g_freedv_verbose);

        // Text field/callsign callbacks.
        if (!wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled)
        {
            freedvInterface.setTextCallbackFn(&my_put_next_rx_char, &my_get_next_tx_char);
        }
        else
        {
            char temp[9];
            memset(temp, 0, 9);
            strncpy(temp, wxGetApp().appConfiguration.reportingConfiguration.reportingCallsign->ToUTF8(), 8); // One less than the size of temp to ensure we don't overwrite the null.
            log_info("Setting callsign to %s", temp);
            freedvInterface.setReliableText(temp);
            
            // Create logger object
            if (wxGetApp().appConfiguration.reportingConfiguration.udpReportingEnabled)
            {
                wxGetApp().logger = std::make_shared<WSJTXNetworkLogger>(
                    (const char*)wxGetApp().appConfiguration.reportingConfiguration.udpReportingHostname->ToUTF8(),
                    wxGetApp().appConfiguration.reportingConfiguration.udpReportingPort);
            }
        }
    
        g_error_hist = new short[MODEM_STATS_NC_MAX*2];
        g_error_histn = new short[MODEM_STATS_NC_MAX*2];
        int i;
        for(i=0; i<2*MODEM_STATS_NC_MAX; i++) {
            g_error_hist[i] = 0;
            g_error_histn[i] = 0;
        }

        // init Codec 2 LPC Post Filter (FreeDV 1600)
        freedvInterface.setLpcPostFilter(
                                       wxGetApp().appConfiguration.filterConfiguration.codec2LPCPostFilterEnable,
                                       wxGetApp().appConfiguration.filterConfiguration.codec2LPCPostFilterBassBoost,
                                       wxGetApp().appConfiguration.filterConfiguration.codec2LPCPostFilterBeta,
                                       wxGetApp().appConfiguration.filterConfiguration.codec2LPCPostFilterGamma);

        log_debug("freedv_get_n_speech_samples(tx): %d", freedvInterface.getTxNumSpeechSamples());
        log_debug("freedv_get_speech_sample_rate(tx): %d", freedvInterface.getTxSpeechSampleRate());
    
        // adjust spectrum and waterfall freq scaling base on mode
        m_panelSpectrum->setFreqScale(MODEM_STATS_NSPEC*((float)MAX_F_HZ/(freedvInterface.getTxModemSampleRate()/2)));
        m_panelWaterfall->setFs(freedvInterface.getTxModemSampleRate());
    
        // Init text msg decoding
        if (!wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled)
            freedvInterface.setTextVaricodeNum(1);
    });

    g_State.store(0, std::memory_order_release);
    g_prev_State.store(0, std::memory_order_release);
    g_snr = 0.0;
    g_half_duplex.store(wxGetApp().appConfiguration.halfDuplexMode, std::memory_order_release);

    m_pcallsign = m_callsign;
    memset(m_callsign, 0, sizeof(m_callsign));

    m_maxLevel = 0;
    executeOnUiThreadAndWait_([&]() 
    {
        m_gaugeLevel->SetValue(0);
        
        if (wxGetApp().logger != nullptr)
        {
            m_logQSO->Enable(true);
        }
    });
    
    // attempt to start sound cards and tx/rx processing
    std::promise<bool> tmpPromise;
    std::future<bool> tmpFuture = tmpPromise.get_future();

    // Note: this executes on the UI thread as macOS may need to display popups
    // to process this request.
    CallAfter([&]() {
        VerifyMicrophonePermissions(tmpPromise);
    });

    tmpFuture.wait();
    
    if (tmpFuture.get())
    {
        bool soundCardSetupValid = false;
        executeOnUiThreadAndWait_([&]() {
            soundCardSetupValid = validateSoundCardSetup();
        });
        
        if (soundCardSetupValid)
        {
            // The reporter list is used from the UI thread, so it's only
            // changed there.
            executeOnUiThreadAndWait_([&]() {
                wxGetApp().m_reporters.clear();
            });
            
            startRxStream();

            if (m_RxRunning.load(std::memory_order_acquire))
            {
                // Enable Tune button if TX is valid.
                if (g_nSoundCards.load(std::memory_order_acquire) > 1)
                {
                    executeOnUiThreadAndWait_([&]() {
                        m_btnTogTune->Enable(true);
                    });
                }

                // attempt to start PTT ......            
                if (wxGetApp().appConfiguration.rigControlConfiguration.hamlibUseForPTT)
                {
                    OpenHamlibRig();
                }
                else if (wxGetApp().appConfiguration.rigControlConfiguration.useSerialPTT) 
                {
                    OpenSerialPort();
                }
                else
                {
                    wxGetApp().rigPttController = nullptr;
                }
                
    #if defined(WIN32)
                if (wxGetApp().appConfiguration.rigControlConfiguration.useOmniRig)
                {
                    // OmniRig can be anbled along with serial port PTT.
                    // The logic below will ensure we don't overwrite the serial PTT
                    // handler.
                    OpenOmniRig();
                }
    #endif // defined(WIN32)
                
                // Set frequency/mode to the one pre-selected by the user before start.
                if (wxGetApp().rigFrequencyController && 
                    (wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges || wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqChangesOnly) &&
                    wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency > 0)
                {
                    wxGetApp().rigFrequencyController->setFrequency(wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency);
        
                    if (wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges)
                    {
                        wxGetApp().rigFrequencyController->setMode(getCurrentMode_());
                    }
                }
                    
                // Initialize PSK Reporter reporting.
                if (wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled)
                {        
                    if (wxGetApp().appConfiguration.reportingConfiguration.reportingCallsign->ToStdString() == "" || wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare->ToStdString() == "")
                    {
                        executeOnUiThreadAndWait_([&]() 
                        {
                            wxMessageBox("Reporting requires a valid callsign and grid square in Tools->Options. Reporting will be disabled.", wxT("Error"), wxOK | wxICON_ERROR, this);
                        });
                    }
                    else
                    {
                        std::vector<std::shared_ptr<IReporter>> reporters;
                        auto csvPath = wxGetApp().appConfiguration.reportingConfiguration.csvLogFilePath.get();
                        reporters.push_back(std::make_shared<CsvReporter>(csvPath.ToStdString()));

                        if (wxGetApp().appConfiguration.reportingConfiguration.pskReporterEnabled)
                        {
                            auto pskReporter =
                                std::make_shared<PskReporter>(
                                    wxGetApp().appConfiguration.reportingConfiguration.reportingCallsign->ToStdString(),
                                    wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare->ToStdString(),
                                    std::string("FreeDV ") + GetFreeDVVersion());
                            assert(pskReporter != nullptr);
                            reporters.push_back(pskReporter);
                        }

                        if (wxGetApp().appConfiguration.reportingConfiguration.udpBroadcastEnabled)
                        {
                            auto udpBroadcastReporter = std::make_shared<UdpReporter>(
                                wxGetApp().appConfiguration.reportingConfiguration.udpBroadcastAddress->ToStdString(),
                                wxGetApp().appConfiguration.reportingConfiguration.udpBroadcastPort);
                            reporters.push_back(udpBroadcastReporter);
                        }

                        executeOnUiThreadAndWait_([&]() 
                        {
                            wxGetApp().m_reporters = std::move(reporters);

                            if (wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnabled)
                            {
                                wxGetApp().m_reporters.push_back(wxGetApp().m_sharedReporterObject);

                                if (!m_reporterHidden->GetValue())
                                {
                                    wxGetApp().m_sharedReporterObject->showOurselves();
                                }
                            }

                            // Enable FreeDV Reporter timer (every 5 minutes).
                            m_pskReporterTimer.Start(5 * 60 * 1000);

                            // Make sure QSY button becomes enabled after start.
                            if (m_reporterDialog != nullptr)
                            {
                                m_reporterDialog->refreshQSYButtonState();
                            }
                        
                            // Immediately transmit selected TX mode and frequency to avoid UI glitches.
                            for (auto& obj : wxGetApp().m_reporters)
                            {
                                obj->transmit(freedvInterface.getCurrentTxModeStr(), g_tx.load(std::memory_order_acquire));
                                obj->freqChange(wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency);
                            }
                        });
                    }
                }
                else
                {
                    executeOnUiThreadAndWait_([&]() {
                        wxGetApp().m_reporters.clear();
                    });
                }

                if (wxGetApp().appConfiguration.rigControlConfiguration.useSerialPTTInput)
                {
                    OpenPTTInPort();
                }

                executeOnUiThreadAndWait_([&]() 
                {
                    m_sliderMicSpkrLevel->Enable(true);

        #ifdef _USE_TIMER
                    m_plotTimer.Start(_REFRESH_TIMER_PERIOD, wxTIMER_CONTINUOUS);
                    m_plotWaterfallTimer.Start(_REFRESH_TIMER_PERIOD, wxTIMER_CONTINUOUS);
                    m_plotSpectrumTimer.Start(_REFRESH_TIMER_PERIOD, wxTIMER_CONTINUOUS);
                    m_plotSpeechInTimer.Start(_REFRESH_TIMER_PERIOD, wxTIMER_CONTINUOUS);
                    m_plotSpeechOutTimer.Start(_REFRESH_TIMER_PERIOD, wxTIMER_CONTINUOUS);
                    m_plotDemodInTimer.Start(_REFRESH_TIMER_PERIOD, wxTIMER_CONTINUOUS);
                    m_plotSNRTimer.Start(_REFRESH_TIMER_PERIOD, wxTIMER_CONTINUOUS);

                    m_updFreqStatusTimer.Start(1000); // every 1 second[UP]
        #endif // _USE_TIMER
                });

                isModemRunning.store(true, std::memory_order_release);
            }
        }
    }
    else
    {
        executeOnUiThreadAndWait_([&]() 
        {
            wxMessageBox(wxString("Microphone permissions must be granted to FreeDV for it to function properly."), wxT("Error"), wxOK | wxICON_ERROR, this);
        });
    }

    // Clear existing TX text, if any.
    auto tmpFifo = g_txDataInFifo.load(std::memory_order_acquire);
    tmpFifo->reset();
}

void MainFrame::performFreeDVOff_()
{
    log_debug("Stop .....");
    
    //
    // Stop Running -------------------------------------------------
    //

    isModemRunning.store(false, std::memory_order_release);

#ifdef _USE_TIMER
    executeOnUiThreadAndWait_([&]() 
    {
        // Disable Tune mode if needed
        if (m_btnTogTune->GetValue())
        {
            m_btnTogTune->SetValue(false);
            
            // Ensures that Tune button actions are actually run to stop transmitting the tone.
            wxCommandEvent tmpEvent;
            OnTogBtnTune(tmpEvent);
        }
        
        m_sliderMicSpkrLevel->Enable(false);
        m_btnTogTune->Enable(false);

        m_plotTimer.Stop();
        m_plotWaterfallTimer.Stop();
        m_plotSpectrumTimer.Stop();
        m_plotSpeechInTimer.Stop();
        m_plotSpeechOutTimer.Stop();
        m_plotDemodInTimer.Stop();
        m_plotSNRTimer.Stop();
        m_pskReporterTimer.Stop();
        m_updFreqStatusTimer.Stop(); // [UP]
    });
#endif // _USE_TIMER
    
    // always end with PTT in rx state
    if (wxGetApp().rigPttController != nullptr && wxGetApp().rigPttController->isConnected())
    {
        wxGetApp().rigPttController->ptt(false);
        wxGetApp().rigPttController->disconnect();
    }
    // Dropping the last shared_ptr reference below runs the controller's
    // destructor, which blocks until the rig actually finishes
    // disconnecting. Against an unresponsive radio (e.g. powered off, via
    // rigctld) that can take far longer than Hamlib's own client-side
    // timeout/retry settings suggest, since those don't bound however long
    // rigctld itself waits on the physical radio it's talking to. Move the
    // last reference onto a detached thread so that wait can't hold up
    // turning the modem off (or app shutdown) -- the controller stays
    // alive exactly as long as it needs to, just off to the side rather
    // than blocking here. The associated future lets a terminating shutdown
    // (see OnTogBtnOnOff()) wait a bounded amount of time for this to finish
    // before the process exits out from under the thread.
    std::promise<void> pttDisconnectProm;
    rigPttDisconnectFuture_ = pttDisconnectProm.get_future();
    std::thread([ptr = std::move(wxGetApp().rigPttController), prom = std::move(pttDisconnectProm)]() mutable
    {
        SetThreadName("RigPttDisconnect");
        ptr = nullptr;
        prom.set_value();
    }).detach();

    if (wxGetApp().rigFrequencyController != nullptr && wxGetApp().rigFrequencyController->isConnected())
    {
        wxGetApp().rigFrequencyController->disconnect();
    }
    std::promise<void> freqDisconnectProm;
    rigFreqDisconnectFuture_ = freqDisconnectProm.get_future();
    std::thread([ptr = std::move(wxGetApp().rigFrequencyController), prom = std::move(freqDisconnectProm)]() mutable
    {
        SetThreadName("RigFreqDisconnect");
        ptr = nullptr;
        prom.set_value();
    }).detach();

    if (wxGetApp().appConfiguration.rigControlConfiguration.useSerialPTTInput)
    {
        ClosePTTInPort();
    }
    
    executeOnUiThreadAndWait_([&]() 
    {
        m_btnTogPTT->SetValue(false);
        VoiceKeyerProcessEvent(VK_SPACE_BAR);
    });
    
    stopRxStream();
         
    executeOnUiThreadAndWait_([&]() 
    {
        wxGetApp().m_reporters.clear();
        if (wxGetApp().m_sharedReporterObject)
        {
            wxGetApp().m_sharedReporterObject->hideFromView();
        }
    });
    
    // FreeDV clean up
    delete[] g_error_hist;
    delete[] g_error_histn;
    freedvInterface.stop();
    
    m_newMicInFilter.store(true, std::memory_order_release);
    m_newSpkOutFilter.store(true, std::memory_order_release);
    
    wxGetApp().logger = nullptr;

    executeOnUiThreadAndWait_([&]() 
    {
        m_textSync->Disable();
        m_textCurrentDecodeMode->Disable();

        m_togBtnAnalog->Disable();
        m_btnTogPTT->Disable();
        m_togBtnVoiceKeyer->Disable();
    
        m_rbRADE->Enable();
        m_rb1600->Enable();
        m_rb700d->Enable();
        m_rb700e->Enable();
        
        m_logQSO->Enable(canLogQso_());

        // Make sure QSY button becomes disabled after stop.
        if (m_reporterDialog != nullptr)
        {
            m_reporterDialog->refreshQSYButtonState();
        }
    });
}

//-------------------------------------------------------------------------
// OnTogBtnOnOff()
//-------------------------------------------------------------------------
void MainFrame::OnTogBtnOnOff(wxCommandEvent&)
{
    if (!m_togBtnOnOff->IsEnabled()) return;

    m_togBtnOnOff->SetFocus();
    
    // Disable buttons while on/off is occurring
    m_togBtnOnOff->Enable(false);
    m_togBtnAnalog->Enable(false);
    m_togBtnVoiceKeyer->Enable(false);
    m_btnTogPTT->Enable(false);
        
    // we are attempting to start

    if (!m_RxRunning.load(std::memory_order_acquire))
    {
        std::thread onOffExec([this]() 
        {
            SetThreadName("TurningOn");

            performFreeDVOn_();
            
            if (!m_RxRunning.load(std::memory_order_acquire))
            {
                // Startup failed.
                performFreeDVOff_();
            }

            // On/Off actions complete, re-enable button.
            executeOnUiThreadAndWait_([&]() {
                bool txEnabled = 
                    m_RxRunning.load(std::memory_order_acquire) && 
                    !wxGetApp().appConfiguration.reportingConfiguration.freedvReporterForceReceiveOnly &&
                    (g_nSoundCards.load(std::memory_order_acquire) == 2);
                
                m_togBtnAnalog->Enable(m_RxRunning.load(std::memory_order_acquire));
                m_togBtnVoiceKeyer->Enable(txEnabled);
                m_btnTogPTT->Enable(txEnabled);
                if (optionsDlg != nullptr)
                {
                    optionsDlg->setSessionActive(m_RxRunning.load(std::memory_order_acquire));
                }

                if (m_RxRunning.load(std::memory_order_acquire))
                {
                    m_togBtnOnOff->SetLabel(wxT("&Stop Modem"));
                }
                m_togBtnOnOff->SetValue(m_RxRunning.load(std::memory_order_acquire));
                m_togBtnOnOff->Enable(true);

                // On some systems the Report Frequency box ends up getting
                // focus after clicking on Start. This causes the frequency
                // to never update. To avoid this, we force focus to be elsewhere
                // in the window.
                m_auiNbookCtrl->SetFocus();
            });
        });
        onOffExec.detach();
    }
    else
    {
        std::thread onOffExec([this]() 
        {
            SetThreadName("TurningOff");

            performFreeDVOff_();

            if (terminating_)
            {
                // We're about to destroy the frame and let the process exit,
                // which would kill the detached rig-disconnect threads from
                // performFreeDVOff_() mid-flight if a slow/unresponsive rig
                // hasn't finished with them yet -- e.g. a queued ptt(false)
                // might never reach the radio. Give them a bounded chance to
                // finish first; an unresponsive rig still can't hang shutdown
                // indefinitely, it just gets abandoned after the grace period.
                auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                if (rigPttDisconnectFuture_.valid())
                {
                    rigPttDisconnectFuture_.wait_until(deadline);
                }
                if (rigFreqDisconnectFuture_.valid())
                {
                    rigFreqDisconnectFuture_.wait_until(deadline);
                }
            }

            // On/Off actions complete, re-enable button.
            executeOnUiThreadAndWait_([&]() {
                m_togBtnAnalog->Enable(m_RxRunning.load(std::memory_order_acquire));
                m_togBtnVoiceKeyer->Enable(m_RxRunning.load(std::memory_order_acquire));
                m_btnTogPTT->Enable(m_RxRunning.load(std::memory_order_acquire));
                if (optionsDlg != nullptr)
                {
                    optionsDlg->setSessionActive(m_RxRunning.load(std::memory_order_acquire));
                }
                m_togBtnOnOff->SetValue(m_RxRunning.load(std::memory_order_acquire));
                m_togBtnOnOff->SetLabel(wxT("&Start Modem"));
                m_togBtnOnOff->Enable(true);

                if (terminating_)
                {
                    CallAfter([&]() { Destroy(); });
                }
            });
        });
        onOffExec.detach();
    }
}

//-------------------------------------------------------------------------
// stopRxStream()
//-------------------------------------------------------------------------
void MainFrame::stopRxStream()
{
    std::unique_lock<std::recursive_mutex> lk(stoppingMutex);

    if(m_RxRunning.load(std::memory_order_acquire))
    {
        StopLowLatencyActivity();

        m_RxRunning.store(false, std::memory_order_release);

        if (m_txThread)
        {
            m_txThread->stop();
            
            if (txInSoundDevice)
            {
                txInSoundDevice->stop();
                txInSoundDevice.reset();
            }
            
            if (txOutSoundDevice)
            {
                txOutSoundDevice->stop();
                txOutSoundDevice.reset();
            }
            
            m_txThread = nullptr;
        }

        if (m_rxThread)
        {
            m_rxThread->stop();
            
            if (rxInSoundDevice)
            {
                rxInSoundDevice->stop();
                rxInSoundDevice.reset();
            }
        
            if (rxOutSoundDevice)
            {
                rxOutSoundDevice->stop();
                rxOutSoundDevice.reset();
            }
            
            m_rxThread = nullptr;
        }

        wxGetApp().linkStep = nullptr;
        destroy_fifos();
        
        // Free memory allocated for filters.
        m_newMicInFilter.store(true, std::memory_order_relaxed);
        m_newSpkOutFilter.store(true, std::memory_order_relaxed);
        deleteEQFilters(g_rxUserdata);
        delete g_rxUserdata;
        
        auto engine = AudioEngineFactory::GetAudioEngine();
        engine->stop();
        engine->setOnEngineError(nullptr, nullptr);
    }
}

void MainFrame::destroy_fifos(void)
{
    if (g_rxUserdata->infifo1) delete g_rxUserdata->infifo1;
    if (g_rxUserdata->outfifo1) delete g_rxUserdata->outfifo1;
    if (g_rxUserdata->infifo2) delete g_rxUserdata->infifo2;
    if (g_rxUserdata->outfifo2) delete g_rxUserdata->outfifo2;
    
    g_rxUserdata->infifo1 = nullptr;
    g_rxUserdata->infifo2 = nullptr;
    g_rxUserdata->outfifo1 = nullptr;
    g_rxUserdata->outfifo2 = nullptr;
}

//-------------------------------------------------------------------------
// MakeTxRxUiHooks()
// Wires TxRxThread's callbacks into the UI/app. TxRxThread invokes these
// from its real-time thread; CallAfter() defers the UI work to the main
// thread, and the tone getters read MainApp's atomics directly.
//-------------------------------------------------------------------------
static TxRxThreadUiHooks MakeTxRxUiHooks()
{
    TxRxThreadUiHooks hooks;
    hooks.playFileToMicInFinished = []() { g_parent->CallAfter(&MainFrame::StopPlayFileToMicIn); };
    hooks.recFileFromRadioFinished = []() { g_parent->CallAfter(&MainFrame::StopRecFileFromRadio); };
    hooks.playFileFromRadioFinished = []() { g_parent->CallAfter(&MainFrame::StopPlaybackFileFromRadio); };
    hooks.toneFreqHz = +[]() FREEDV_NONBLOCKING -> float {
        FREEDV_BEGIN_VERIFIED_SAFE
        auto value = (float)wxGetApp().m_tone_freq_hz.load(std::memory_order_acquire);
        FREEDV_END_VERIFIED_SAFE
        return value;
    };
    hooks.toneAmplitude = +[]() FREEDV_NONBLOCKING -> float {
        FREEDV_BEGIN_VERIFIED_SAFE
        auto value = (float)wxGetApp().m_tone_amplitude.load(std::memory_order_acquire);
        FREEDV_END_VERIFIED_SAFE
        return value;
    };
    hooks.toneEnabled = +[]() FREEDV_NONBLOCKING -> bool {
        FREEDV_BEGIN_VERIFIED_SAFE
        auto value = wxGetApp().m_tone.load(std::memory_order_acquire);
        FREEDV_END_VERIFIED_SAFE
        return value;
    };
    return hooks;
}

//-------------------------------------------------------------------------
// startRxStream()
//-------------------------------------------------------------------------
void MainFrame::startRxStream()
{
    log_debug("startRxStream .....");
    if(!m_RxRunning.load(std::memory_order_acquire)) {
        m_RxRunning.store(true, std::memory_order_release);
        
        auto engine = AudioEngineFactory::GetAudioEngine();
        engine->setOnEngineError([](IAudioEngine& dev, std::string const& error, void* state) { 
            MainFrame* castedThis = (MainFrame*)state;
            castedThis->onAudioEngineError_(dev, error, state); 
        }, this);
        engine->start();

        IAudioDevice::AudioErrorCallbackFn errorCallback = &MainFrame::OnAudioDeviceError_;
        
        // Init call back data structure ----------------------------------------------

        g_rxUserdata = new paCallBackData;
                
        // create FIFOs used to interface between IAudioEngine and txRx
        // processing loop, which iterates about once every 10-40ms
        // (depending on platform/audio library). Sample rate conversion, 
        // stats for spectral plots, and transmit processng are all performed 
        // in the tx/rxProcessing loop.
        //
        // Note that soundCard[12]InFifoSizeSamples are significantly larger than
        // the other FIFO sizes. This is to better handle PulseAudio/pipewire
        // behavior on some devices, where the system sends multiple *seconds*
        // of audio samples at once followed by long periods with no samples at
        // all. Without a very large FIFO size (or a way to dynamically change
        // FIFO sizes, which isn't recommended for real-time operation), we will
        // definitely lose audio.
        constexpr int MAX_INCOMING_AUDIO_SEC = 75;
        int m_fifoSize_ms = wxGetApp().appConfiguration.fifoSizeMs;
        int soundCard1InFifoSizeSamples = MAX_INCOMING_AUDIO_SEC * wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate;

        // Guards against FIFO sizes accidentally being too small to fit an entier TX block.
        // Theoretically allows up to three TX packets to be queued at a time at minimum
        // (depending on mode).
        int soundCard1OutFifoSizeSamples = std::max(
            3 * (freedvInterface.getTxNNomModemSamples() * wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate) / freedvInterface.getTxModemSampleRate(),
            m_fifoSize_ms*wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate / 1000);

        if (g_nSoundCards.load(std::memory_order_acquire) == 2)
        {
            int soundCard2InFifoSizeSamples = MAX_INCOMING_AUDIO_SEC * wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate;

            // Guards against FIFO sizes accidentally being too small to fit an entier TX block.
            // Theoretically allows up to three RX packets to be queued at a time at minimum
            // (depending on mode).
            int soundCard2OutFifoSizeSamples = std::max(
                3 * (freedvInterface.getRxNumSpeechSamples() * wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate) / freedvInterface.getRxSpeechSampleRate(),
                m_fifoSize_ms*wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate / 1000);
            g_rxUserdata->outfifo1 = new GenericFIFO<short>(soundCard1OutFifoSizeSamples);
            g_rxUserdata->infifo2 = new GenericFIFO<short>(soundCard2InFifoSizeSamples);
            g_rxUserdata->infifo1 = new GenericFIFO<short>(soundCard1InFifoSizeSamples);
            g_rxUserdata->outfifo2 = new GenericFIFO<short>(soundCard2OutFifoSizeSamples);
        
            log_debug("fifoSize_ms:  %d infifo2: %d/outfilo2: %d",
                wxGetApp().appConfiguration.fifoSizeMs.get(), soundCard2InFifoSizeSamples, soundCard2OutFifoSizeSamples);

            g_rxUserdata->tmpReadRxBuffer_ = std::make_unique<short[]>(std::max(soundCard1InFifoSizeSamples, soundCard2InFifoSizeSamples));
            g_rxUserdata->tmpReadTxBuffer_ = std::make_unique<short[]>(std::max(soundCard1InFifoSizeSamples, soundCard2InFifoSizeSamples));
            g_rxUserdata->tmpWriteRxBuffer_ = std::make_unique<short[]>(std::max(soundCard1OutFifoSizeSamples, soundCard2OutFifoSizeSamples));
            g_rxUserdata->tmpWriteTxBuffer_ = std::make_unique<short[]>(std::max(soundCard1OutFifoSizeSamples, soundCard2OutFifoSizeSamples));
        }
        else
        {
            g_rxUserdata->infifo1 = new GenericFIFO<short>(soundCard1InFifoSizeSamples);
            g_rxUserdata->outfifo1 = new GenericFIFO<short>(soundCard1OutFifoSizeSamples);
            g_rxUserdata->infifo2 = nullptr;
            g_rxUserdata->outfifo2 = nullptr;

            g_rxUserdata->tmpReadRxBuffer_ = std::make_unique<short[]>(soundCard1InFifoSizeSamples);
            g_rxUserdata->tmpReadTxBuffer_ = std::make_unique<short[]>(soundCard1InFifoSizeSamples);
            g_rxUserdata->tmpWriteRxBuffer_ = std::make_unique<short[]>(soundCard1OutFifoSizeSamples);
            g_rxUserdata->tmpWriteTxBuffer_ = std::make_unique<short[]>(soundCard1OutFifoSizeSamples);
        }

        log_debug("fifoSize_ms: %d infifo1: %d/outfilo1 %d",
                wxGetApp().appConfiguration.fifoSizeMs.get(), soundCard1InFifoSizeSamples, soundCard1OutFifoSizeSamples);

        if (g_nSoundCards.load(std::memory_order_acquire) == 0) 
        {
            executeOnUiThreadAndWait_([&]() {
                wxMessageBox(wxT("No Sound Cards configured, use Tools - Audio Config to configure"), wxT("Error"), wxOK);
            });
            
            m_RxRunning.store(false, std::memory_order_release);
            
            engine->stop();
            engine->setOnEngineError(nullptr, nullptr);
            delete g_rxUserdata;
            return;
        }
        else if (g_nSoundCards.load(std::memory_order_acquire) == 1)
        {
            // RX-only setup.
            // Note: we assume 2 channels, but IAudioEngine will automatically downgrade to 1 channel if needed.
            rxInSoundDevice = engine->getAudioDevice(wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName, IAudioEngine::AUDIO_ENGINE_IN, wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate, 2);
            rxOutSoundDevice = engine->getAudioDevice(wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName, IAudioEngine::AUDIO_ENGINE_OUT, wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate, 2);
            
            bool failed = false;
            if (!rxInSoundDevice)
            {
                executeOnUiThreadAndWait_([&]() {
                    wxMessageBox(wxString::Format("Could not find RX input sound device '%s'. Please check settings and try again.", wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName.get()), wxT("Error"), wxOK);
                });
                failed = true;
            }
            else
            {
                rxInSoundDevice->setDescription("Radio to FreeDV");
                rxInSoundDevice->setOnAudioDeviceChanged([](IAudioDevice&, std::string newDeviceName, void* state) {
                    MainFrame* castedThis = (MainFrame*)state;
                    castedThis->CallAfter(&MainFrame::handleAudioDeviceChange_<1, false>, std::move(newDeviceName));
                }, this);
            }
            
            if (!rxOutSoundDevice && !failed)
            {
                executeOnUiThreadAndWait_([]() {
                    wxMessageBox(wxString::Format("Could not find RX output sound device '%s'. Please check settings and try again.", wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName.get()), wxT("Error"), wxOK);
                });
                failed = true;
            }
            else if (!failed)
            {
                rxOutSoundDevice->setDescription("FreeDV to Speaker");
                rxOutSoundDevice->setOnAudioDeviceChanged([](IAudioDevice&, std::string newDeviceName, void* state) {
                    MainFrame* castedThis = (MainFrame*)state;
                    castedThis->CallAfter(&MainFrame::handleAudioDeviceChange_<1, true>, std::move(newDeviceName));
                }, this);
            }
 
            if (failed)
            {
                if (rxInSoundDevice)
                {
                    rxInSoundDevice.reset();
                }
                
                if (rxOutSoundDevice)
                {
                    rxOutSoundDevice.reset();
                }
                
                m_RxRunning.store(false, std::memory_order_release);
            
                engine->stop();
                engine->setOnEngineError(nullptr, nullptr);
                delete g_rxUserdata;
                return;
            }
            else
            {
                // Re-save sample rates in case they were somehow invalid before
                // device creation.
                wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate = rxInSoundDevice->getSampleRate();
                wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate = rxOutSoundDevice->getSampleRate();
            }
        }
        else
        {
            // RX + TX setup
            // Same note as above re: number of channels.
            // Note: TX devices are started here as RX device sample rates could change as a result (e.g. Bluetooth on macOS).
            bool failed = false;
            
            txInSoundDevice = engine->getAudioDevice(wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName, IAudioEngine::AUDIO_ENGINE_IN, wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate, 2);
            
            if (!txInSoundDevice)
            {
                executeOnUiThreadAndWait_([]() {
                    wxMessageBox(wxString::Format("Could not find TX input sound device '%s'. Please check settings and try again.", wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName.get()), wxT("Error"), wxOK);
                });
                failed = true;
            }
            else
            {
                txInSoundDevice->setDescription("Mic to FreeDV");
                txInSoundDevice->setOnAudioDeviceChanged([](IAudioDevice&, std::string newDeviceName, void* state) {
                    MainFrame* castedThis = (MainFrame*)state;
                    castedThis->CallAfter(&MainFrame::handleAudioDeviceChange_<2, false>, std::move(newDeviceName));
                }, this);
                txInSoundDevice->setOnAudioData(&OnTxInAudioData_, g_rxUserdata);
        
                txInSoundDevice->setOnAudioOverflow([](IAudioDevice&, void*)
                {
                    g_AEstatus2[1].fetch_add(1, std::memory_order_relaxed);
                }, nullptr);
        
                txInSoundDevice->setOnAudioUnderflow([](IAudioDevice&, void*)
                {
                    g_AEstatus2[0].fetch_add(1, std::memory_order_relaxed);
                }, nullptr);
                
                txInSoundDevice->setOnAudioError(errorCallback, this);                
                txInSoundDevice->start();
            }

            txOutSoundDevice = engine->getAudioDevice(wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName, IAudioEngine::AUDIO_ENGINE_OUT, wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate, 2);
            
            if (!txOutSoundDevice && !failed)
            {
                executeOnUiThreadAndWait_([]() {
                    wxMessageBox(wxString::Format("Could not find TX output sound device '%s'. Please check settings and try again.", wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName.get()), wxT("Error"), wxOK);
                });
                failed = true;
            }
            else if (!failed)
            {
                txOutSoundDevice->setDescription("FreeDV to Radio");
                txOutSoundDevice->setOnAudioDeviceChanged([](IAudioDevice&, std::string newDeviceName, void* state) {
                    MainFrame* castedThis = (MainFrame*)state;
                    castedThis->CallAfter(&MainFrame::handleAudioDeviceChange_<1, true>, std::move(newDeviceName));
                }, this);
                txOutSoundDevice->setOnAudioData(&OnTxOutAudioData_, g_rxUserdata);
        
                txOutSoundDevice->setOnAudioOverflow([](IAudioDevice&, void*)
                {
                    g_AEstatus1[3].fetch_add(1, std::memory_order_relaxed);
                }, nullptr);
        
                txOutSoundDevice->setOnAudioUnderflow([](IAudioDevice&, void*)
                {
                    g_AEstatus1[2].fetch_add(1, std::memory_order_relaxed);
                }, nullptr);
            
                txOutSoundDevice->setOnAudioError(errorCallback, this);
                txOutSoundDevice->start();
            }
            
            rxInSoundDevice = engine->getAudioDevice(wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName, IAudioEngine::AUDIO_ENGINE_IN, wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate, 2);

            rxOutSoundDevice = engine->getAudioDevice(wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName, IAudioEngine::AUDIO_ENGINE_OUT, wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate, 2);
            
            if (!rxInSoundDevice && !failed)
            {
                executeOnUiThreadAndWait_([]() {
                    wxMessageBox(wxString::Format("Could not find RX input sound device '%s'. Please check settings and try again.", wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName.get()), wxT("Error"), wxOK);
                });
                failed = true;
            }
            else if (!failed)
            {
                rxInSoundDevice->setDescription("Radio to FreeDV");
                rxInSoundDevice->setOnAudioDeviceChanged([](IAudioDevice&, std::string newDeviceName, void* state) {
                    MainFrame* castedThis = (MainFrame*)state;
                    castedThis->CallAfter(&MainFrame::handleAudioDeviceChange_<1, false>, std::move(newDeviceName));
                }, this);
            }
 
            if (!rxOutSoundDevice && !failed)
            {
                executeOnUiThreadAndWait_([]() {
                    wxMessageBox(wxString::Format("Could not find RX output sound device '%s'. Please check settings and try again.", wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName.get()), wxT("Error"), wxOK);
                });
                failed = true;
            }
            else if (!failed)
            {
                rxOutSoundDevice->setDescription("FreeDV to Speaker");
                rxOutSoundDevice->setOnAudioDeviceChanged([](IAudioDevice&, std::string newDeviceName, void* state) {
                    MainFrame* castedThis = (MainFrame*)state;
                    castedThis->CallAfter(&MainFrame::handleAudioDeviceChange_<2, true>, std::move(newDeviceName));
                }, this);
            }
 
            if (failed)
            {
                if (rxInSoundDevice)
                {
                    rxInSoundDevice.reset();
                }
                
                if (rxOutSoundDevice)
                {
                    rxOutSoundDevice.reset();
                }
                
                if (txInSoundDevice)
                {
                    txInSoundDevice->stop();
                    txInSoundDevice.reset();
                }
                
                if (txOutSoundDevice)
                {
                    txOutSoundDevice->stop();
                    txOutSoundDevice.reset();
                }
                
                m_RxRunning.store(false, std::memory_order_release);
            
                engine->stop();
                engine->setOnEngineError(nullptr, nullptr);
                delete g_rxUserdata;
                return;
            }
            else
            {
                // Re-save sample rates in case they were somehow invalid before
                // device creation.
                wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate = rxInSoundDevice->getSampleRate();
                wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate = rxOutSoundDevice->getSampleRate();

                wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate = txInSoundDevice->getSampleRate();
                wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate = txOutSoundDevice->getSampleRate();
            }
        }

        // reset debug stats for FIFOs

        g_infifo1_full.store(0, std::memory_order_relaxed);
        g_outfifo1_empty.store(0, std::memory_order_relaxed);
        g_infifo2_full.store(0, std::memory_order_relaxed);
        g_outfifo2_empty.store(0, std::memory_order_relaxed);
        for (int i=0; i<4; i++) {
            g_AEstatus1[i].store(0, std::memory_order_relaxed);
            g_AEstatus2[i].store(0, std::memory_order_relaxed);
        }

        // These FIFOs interface between the 20ms tx/rxProcessing()
        // loop and the demodulator, which requires a variable number
        // of input samples to adjust for timing clock differences
        // between remote tx and rx.  These FIFOs also help with the
        // different processing block size of different FreeDV modes.

        // TODO: might be able to tune these on a per waveform basis, or refactor
        // to a neater design with less layers of FIFOs

        int modem_samplerate, rxInFifoSizeSamples, rxOutFifoSizeSamples;
        modem_samplerate = freedvInterface.getRxModemSampleRate();
        rxInFifoSizeSamples = freedvInterface.getRxNumModemSamples();
        rxOutFifoSizeSamples = freedvInterface.getRxNumSpeechSamples();

        // add an extra 40ms to give a bit of headroom for processing loop adding samples
        // which operates on 20ms buffers

        rxInFifoSizeSamples += 0.04*modem_samplerate;
        rxOutFifoSizeSamples += 0.04*modem_samplerate;

        log_debug("rxInFifoSizeSamples: %d rxOutFifoSizeSamples: %d", rxInFifoSizeSamples, rxOutFifoSizeSamples);

        // Init Equaliser Filters ------------------------------------------------------

        m_newMicInFilter = m_newSpkOutFilter = true;
        g_mutexProtectingCallbackData.Lock();

        g_rxUserdata->micInEQEnable.store(wxGetApp().appConfiguration.filterConfiguration.micInChannel.eqEnable, std::memory_order_relaxed);
        g_rxUserdata->spkOutEQEnable.store(wxGetApp().appConfiguration.filterConfiguration.spkOutChannel.eqEnable, std::memory_order_relaxed);

        if (g_nSoundCards == 1)
        {
            designEQFilters(
                g_rxUserdata, 
                wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate, 
                0);
        }
        else
        {
            designEQFilters(
                g_rxUserdata, 
                wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate, 
                wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate);
        }

        m_newMicInFilter = m_newSpkOutFilter = false;
        g_mutexProtectingCallbackData.Unlock();

        // optional tone in left channel to reliably trigger vox

        g_rxUserdata->leftChannelVoxTone = wxGetApp().appConfiguration.rigControlConfiguration.leftChannelVoxTone;
        g_rxUserdata->voxTonePhase = 0;

        // Set sound card callbacks
        rxInSoundDevice->setOnAudioData(&OnRxInAudioData_, g_rxUserdata);
        
        rxInSoundDevice->setOnAudioOverflow([](IAudioDevice&, void*)
        {
            g_AEstatus1[1].fetch_add(1, std::memory_order_relaxed);
        }, nullptr);
        
        rxInSoundDevice->setOnAudioUnderflow([](IAudioDevice&, void*)
        {
            g_AEstatus1[0].fetch_add(1, std::memory_order_relaxed);
        }, nullptr);
        
        rxInSoundDevice->setOnAudioError(errorCallback, this);
        rxOutSoundDevice->setOnAudioError(errorCallback, this);
        
        if (txInSoundDevice && txOutSoundDevice)
        {
            rxOutSoundDevice->setOnAudioData(&OnRxOutAudioData_, g_rxUserdata);
            
            rxOutSoundDevice->setOnAudioOverflow([](IAudioDevice&, void*)
            {
                g_AEstatus2[3].fetch_add(1, std::memory_order_relaxed);
            }, nullptr);
        
            rxOutSoundDevice->setOnAudioUnderflow([](IAudioDevice&, void*)
            {
                g_AEstatus2[2].fetch_add(1, std::memory_order_relaxed);
            }, nullptr);
        }
        else
        {
            rxOutSoundDevice->setOnAudioData([](IAudioDevice& dev, void* data, size_t size, void* state) FREEDV_NONBLOCKING {
                paCallBackData* cbData = static_cast<paCallBackData*>(state);
                short* audioData = static_cast<short*>(data);
                short* tmpOutput = cbData->tmpWriteRxBuffer_.get();

                auto toRead = std::min((size_t)cbData->outfifo1->numUsed(), size);
                if (toRead < size)
                {
                    g_outfifo1_empty.fetch_add(1, std::memory_order_relaxed);
                }

                if (toRead > 0 && cbData->outfifo1->read(tmpOutput, toRead) != 0)
                {
                    // A concurrent reset() (e.g. on a TX/RX transition) can make a
                    // read that numUsed() just reported as available fail. Treat
                    // that as silence rather than replaying whatever was left over
                    // in tmpOutput from the previous callback.
                    toRead = 0;
                }
                auto numChannels = dev.getNumChannels();
                for (size_t count = 0; count < size; count++)
                {
                    for (int j = 0; j < numChannels; j++)
                    {
                        *audioData++ = (count < toRead) ? tmpOutput[count] : 0;
                    }
                }
            }, g_rxUserdata);
            
            rxOutSoundDevice->setOnAudioOverflow([](IAudioDevice&, void*)
            {
                g_AEstatus1[3].fetch_add(1, std::memory_order_relaxed);
            }, nullptr);
        
            rxOutSoundDevice->setOnAudioUnderflow([](IAudioDevice&, void*)
            {
                g_AEstatus1[2].fetch_add(1, std::memory_order_relaxed);
            }, nullptr);
        }
        
        // Create link to allow monitoring TX/VK audio
        wxGetApp().linkStep = std::make_shared<LinkStep>(rxOutSoundDevice->getSampleRate());
        
        // Nothing's left over from an earlier start that got no further
        // than this (below, when a device couldn't be opened): its
        // processing threads were never started, and mustn't be mistaken
        // for this start's, e.g. on changing to receiving only.
        m_txThread = nullptr;
        m_rxThread = nullptr;

        // start tx/rx processing thread
        if (txInSoundDevice && txOutSoundDevice)
        {
            m_txThread = std::make_shared<TxRxThread>(true, txInSoundDevice->getSampleRate(), txOutSoundDevice->getSampleRate(), wxGetApp().linkStep, txInSoundDevice, MakeTxRxUiHooks());
            
            if (!txInSoundDevice->isRunning())
            {
                rxInSoundDevice.reset();
                rxOutSoundDevice.reset();
                txInSoundDevice.reset();
                txOutSoundDevice.reset();
                m_RxRunning.store(false, std::memory_order_release);
                return;
            }

            if (!txOutSoundDevice->isRunning())
            {
                txInSoundDevice->stop();

                rxInSoundDevice.reset();
                rxOutSoundDevice.reset();
                txInSoundDevice.reset();
                txOutSoundDevice.reset();
                m_RxRunning.store(false, std::memory_order_release);
                return;
            }
        }

        m_rxThread = std::make_shared<TxRxThread>(false, rxInSoundDevice->getSampleRate(), rxOutSoundDevice->getSampleRate(), wxGetApp().linkStep, rxInSoundDevice, MakeTxRxUiHooks());

        rxInSoundDevice->start();
        if (!rxInSoundDevice->isRunning())
        {
            if (txInSoundDevice) txInSoundDevice->stop();
            if (txOutSoundDevice) txOutSoundDevice->stop();
            
            rxInSoundDevice.reset();
            rxOutSoundDevice.reset();
            txInSoundDevice.reset();
            txOutSoundDevice.reset();
            m_RxRunning.store(false, std::memory_order_release);
            return;
        }

        rxOutSoundDevice->start();
        if (!rxOutSoundDevice->isRunning())
        {
            if (txInSoundDevice) txInSoundDevice->stop();
            if (txOutSoundDevice) txOutSoundDevice->stop();
            rxInSoundDevice->stop();

            rxInSoundDevice.reset();
            rxOutSoundDevice.reset();
            txInSoundDevice.reset();
            txOutSoundDevice.reset();
            m_RxRunning.store(false, std::memory_order_release);
            return;
        }

        // Both processing threads are only started now, once every device
        // is open: a thread that was started but never signalled to start
        // (what the failures above would leave behind) can't be stopped,
        // and FreeDV then hangs when it exits.
        if (m_txThread != nullptr)
        {
            m_txThread->start();
        }

        m_rxThread->start();

        // Logic to ensure that both TX/RX threads start work at the 
        // same time. This makes sure there are no dropouts at the beginning
        // of full duplex TX.
        m_rxThread->waitForReady();
        if (m_txThread != nullptr)
        {
            m_txThread->waitForReady();
        }
        m_rxThread->signalToStart();
        if (m_txThread != nullptr)
        {
            m_txThread->signalToStart();
        }
    
        log_debug("starting tx/rx processing thread");

        // Work around an issue where the buttons stay disabled even if there
        // is an error opening one or more audio device(s).
        bool txDevicesRunning = 
            (!txInSoundDevice || txInSoundDevice->isRunning()) &&
            (!txOutSoundDevice || txOutSoundDevice->isRunning());
        bool rxDevicesRunning = 
            (rxInSoundDevice && rxInSoundDevice->isRunning()) &&
            (rxOutSoundDevice && rxOutSoundDevice->isRunning());
        m_RxRunning.store(txDevicesRunning && rxDevicesRunning, std::memory_order_release);
    }

    if (m_RxRunning.load(std::memory_order_acquire))
    {
        StartLowLatencyActivity();
    }
}

bool MainFrame::validateSoundCardSetup(bool silent)
{
    bool canRun = true;

    // Translate device names to IDs
    auto engine = AudioEngineFactory::GetAudioEngine();
    engine->setOnEngineError([this, silent](IAudioEngine&, std::string error, void*) {
        if (silent) return;
        CallAfter([this, error = std::move(error)]() {
            wxMessageBox(wxString::Format(
                "Error encountered while initializing the audio engine: %s.",
                error), wxT("Error"), wxOK, this);
        });
    }, nullptr);
    engine->start();
    
    auto defaultInputDevice = engine->getDefaultAudioDevice(IAudioEngine::AUDIO_ENGINE_IN);
    auto defaultOutputDevice = engine->getDefaultAudioDevice(IAudioEngine::AUDIO_ENGINE_OUT);
    
    bool hasSoundCard1InDevice = wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName != "none";
    bool hasSoundCard1OutDevice = wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName != "none";
    bool hasSoundCard2InDevice = wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName != "none";
    bool hasSoundCard2OutDevice = wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName != "none";
    
    g_nSoundCards.store(0, std::memory_order_release);
    if (hasSoundCard1InDevice && hasSoundCard1OutDevice) {
        g_nSoundCards.store(1, std::memory_order_release);
        if (hasSoundCard2InDevice && hasSoundCard2OutDevice)
            g_nSoundCards.store(2, std::memory_order_release);
    }
    
    // For the purposes of validation, number of channels isn't necessary.
    auto soundCard1InDevice = engine->getAudioDevice(wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName, IAudioEngine::AUDIO_ENGINE_IN, wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate, 1);
    auto soundCard1OutDevice = engine->getAudioDevice(wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName, IAudioEngine::AUDIO_ENGINE_OUT, wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate, 1);
    auto soundCard2InDevice = engine->getAudioDevice(wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName, IAudioEngine::AUDIO_ENGINE_IN, wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate, 1);
    auto soundCard2OutDevice = engine->getAudioDevice(wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName, IAudioEngine::AUDIO_ENGINE_OUT, wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate, 1);

    wxString failedDeviceName;
    if (wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName != "none" && !soundCard1InDevice)
    {
        failedDeviceName = wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName.get();
        canRun = false;
    }
    else if (canRun && wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName != "none" && !soundCard1OutDevice)
    {
        failedDeviceName = wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName.get();
        canRun = false;
    }
    else if (canRun && wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName != "none" && !soundCard2InDevice)
    {
        failedDeviceName = wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName.get();
        canRun = false;
    }
    else if (canRun && wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName != "none" && !soundCard2OutDevice)
    {
        failedDeviceName = wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName.get();
        canRun = false;
    }
    
    if (canRun && g_nSoundCards.load(std::memory_order_acquire) == 0)
    {
        if (!silent)
        {
            // Initial setup. Display Easy Setup dialog.
            CallAfter([&]() {
                EasySetupDialog* dlg = new EasySetupDialog(this);
                if (dlg->ShowModal() == wxOK)
                {
                    // Show/hide frequency box based on CAT control status
                    m_freqBox->Show(isFrequencyControlEnabled_());

                    // Show/hide callsign combo box based on PSK Reporter Status
                    if (wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled)
                    {
                        m_cboLastReportedCallsigns->Show();
                        m_txtCtrlCallSign->Hide();
                    }
                    else
                    {
                        m_cboLastReportedCallsigns->Hide();
                        m_txtCtrlCallSign->Show();
                    }

                    // Relayout window so that the changes can take effect.
                    m_panel->Layout();
                }
            });
        }
        canRun = false;
    }
    else if (!canRun)
    {
        if (!silent)
        {
            wxMessageBox(wxString::Format(
                "Your %s device cannot be found and may have been removed from your system. Please reattach this device, close this message box and retry. If this fails, go to Tools->Audio Config... to check your settings.",
                failedDeviceName), wxT("Sound Device Not Found"), wxOK, this);
        }
    }
    else
    {
        const int MIN_SAMPLE_RATE_ANALOG = 16000;
        const int MIN_SAMPLE_RATE_RADIO = 8000;
        int failedSampleRate = 0;
        int expectedSampleRate = 0;
        int expectedSampleRate1Out = (g_nSoundCards.load(std::memory_order_acquire) == 1 ? MIN_SAMPLE_RATE_ANALOG : MIN_SAMPLE_RATE_RADIO);
        
        // Validate sample rates
        if (wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName != "none" && wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate < MIN_SAMPLE_RATE_RADIO)
        {
            failedDeviceName = wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName.get();
            failedSampleRate = wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate;
            expectedSampleRate = MIN_SAMPLE_RATE_RADIO;
            canRun = false;
        }
        else if (wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName != "none" && wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate < expectedSampleRate1Out)
        {
            failedDeviceName = wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName.get();
            failedSampleRate = wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate;
            expectedSampleRate = expectedSampleRate1Out;
            canRun = false;
        }
        else if (wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName != "none" && wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate < MIN_SAMPLE_RATE_ANALOG)
        {
            failedDeviceName = wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName.get();
            failedSampleRate = wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate;
            expectedSampleRate = MIN_SAMPLE_RATE_ANALOG;
            canRun = false;
        }
        else if (wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName != "none" && wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate < MIN_SAMPLE_RATE_ANALOG)
        {
            failedDeviceName = wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName.get();
            failedSampleRate = wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate;
            expectedSampleRate = MIN_SAMPLE_RATE_ANALOG;
            canRun = false;
        }
        
        if (!canRun && !silent)
        {
            wxMessageBox(wxString::Format(
                "Your %s device is set to use a sample rate of %d, which is less than the minimum of %d. Please go to Tools->Audio Config... to check your settings.",
                failedDeviceName, failedSampleRate, expectedSampleRate), wxT("Sample Rate Too Low"), wxOK, this);
        }
    }
    
    engine->stop();
    engine->setOnEngineError(nullptr, nullptr);
    
    return canRun;
}

void MainFrame::initializeFreeDVReporter_()
{
    bool receiveOnly = isReceiveOnly();
    
    auto oldReporterObject = wxGetApp().m_sharedReporterObject;
    wxGetApp().m_sharedReporterObject =
        std::make_shared<FreeDVReporter>(
            wxGetApp().appConfiguration.reportingConfiguration.freedvReporterHostname->ToStdString(),
            wxGetApp().appConfiguration.reportingConfiguration.reportingCallsign->ToStdString(),
            wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare->ToStdString(),
            std::string("FreeDV ") + GetFreeDVVersion(),
            receiveOnly,
            false,
            wxGetApp().appConfiguration.reportingConfiguration.freedvReporterUseTls);
    assert(wxGetApp().m_sharedReporterObject);
    
    // If we're running, remove any existing reporter object.
    if (oldReporterObject != nullptr)
    {
        for (auto& ptr : wxGetApp().m_reporters)
        {
            if (ptr == oldReporterObject)
            {
                oldReporterObject = nullptr;
                ptr = wxGetApp().m_sharedReporterObject;
                break;
            }
        }
    }
    
    // Make built in FreeDV Reporter client available.
    if (m_reporterDialog == nullptr)
    {
        m_reporterDialog = new FreeDVReporterDialog(this);
    }
        
    m_reporterDialog->setReporter(wxGetApp().m_sharedReporterObject);
    m_reporterDialog->refreshLayout();
    
    // Set up QSY request handler
    wxGetApp().m_sharedReporterObject->setOnQSYRequestFn(std::bind(&MainFrame::onQsyRequest_, this, _1, _2, _3));
    
    auto txStatus = g_tx.load(std::memory_order_acquire);
    if (!freedvInterface.isRunning())
    {
        wxGetApp().m_sharedReporterObject->hideFromView();
    }
    else
    {
        wxGetApp().m_sharedReporterObject->transmit(freedvInterface.getCurrentTxModeStr(), txStatus);
        wxGetApp().m_sharedReporterObject->freqChange(wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency);
    }
    wxGetApp().m_sharedReporterObject->connect();
}

void MainFrame::onQsyRequest_(std::string callsign, uint64_t freqHz, std::string message)
{
    // A separate object needs to be created here since wxWidgets doesn't support more
    // than two arguments for CallAfter().
    QsyRequestArgs* args = new QsyRequestArgs;
    assert(args != nullptr);
    args->callsign = std::move(callsign);
    args->freqHz = freqHz;
    args->message = std::move(message);

    CallAfter(&MainFrame::onQsyRequestUIThread_, args);
}

void MainFrame::onQsyRequestUIThread_(QsyRequestArgs* args)
{
    // Copy passed-in args and delete args carrier object.
    std::string callsign = args->callsign;
    uint64_t freqHz = args->freqHz;
    std::string message = args->message;
    delete args;

    double freqFactor = 1000.0;
    std::string fmtMsg = "%s has requested that you QSY to %s kHz.";
        
    if (!wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
    {
        freqFactor *= 1000.0;
        fmtMsg = "%s has requested that you QSY to %s MHz.";
    }
        
    double frequencyReadable = freqHz / freqFactor;
    wxString freqString;
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
    {
        freqString = wxNumberFormatter::ToString(frequencyReadable, 1);
    }
    else
    {
        freqString = wxNumberFormatter::ToString(frequencyReadable, 4);
    }
    
    wxString fullMessage = wxString::Format(wxString(fmtMsg), callsign, freqString);
    int dialogStyle = wxOK | wxICON_INFORMATION | wxCENTRE;
        
    if (wxGetApp().rigFrequencyController != nullptr && 
        (wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges || wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqChangesOnly))
    {
        fullMessage = wxString::Format(_("%s Would you like to change to that frequency now?"), fullMessage);
        dialogStyle = wxYES_NO | wxICON_QUESTION | wxCENTRE;
    }
        
    wxMessageDialog messageDialog(this, fullMessage, wxT("FreeDV Reporter"), dialogStyle);

    if (dialogStyle & wxYES_NO)
    {
        messageDialog.SetYesNoLabels(_("Change Frequency"), _("Cancel"));
    }

    auto answer = messageDialog.ShowModal();
    if (answer == wxID_YES)
    {
        // This will implicitly cause Hamlib to change the frequency and mode.
        m_cboReportFrequency->SetValue(freqString);
    }
}

void MainFrame::onAudioEngineError_(IAudioEngine&, std::string const& error, void*)
{
     executeOnUiThreadAndWait_([&, error]() {
         wxMessageBox(wxString::Format(
                          "Error encountered while initializing the audio engine: %s.", 
                          error), wxT("Error"), wxOK, this); 
     });
}

void MainFrame::onAudioDeviceError_(std::string error)
{
    wxMessageBox(wxString::Format("Error encountered while processing audio: %s", std::move(error)), wxT("Error"), wxOK);
}

void MainFrame::OnAudioDeviceError_(IAudioDevice&, std::string const& error, void* state)
{
    MainFrame* castedState = (MainFrame*)state;
    log_error("%s", error.c_str());
    castedState->CallAfter(&MainFrame::onAudioDeviceError_, error);
}

void MainFrame::OnTxInAudioData_(IAudioDevice& dev, void* data, size_t size, void* state) FREEDV_NONBLOCKING
{
    paCallBackData* cbData = static_cast<paCallBackData*>(state);
    short* audioData = static_cast<short*>(data);
    short* tmpInput = cbData->tmpReadTxBuffer_.get();

    if (!endingTx.load(std::memory_order_acquire)) 
    {
        auto numChannels = dev.getNumChannels();
        for(size_t i = 0; i < size; i++, audioData += numChannels)
        {
            tmpInput[i] = audioData[0];
        }
        if (isModemRunning.load(std::memory_order_acquire) && cbData->infifo2->write(tmpInput, size)) 
        {
            g_infifo2_full.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void MainFrame::OnTxOutAudioData_(IAudioDevice& dev, void* data, size_t size, void* state) FREEDV_NONBLOCKING
{
    paCallBackData* cbData = static_cast<paCallBackData*>(state);
    short* audioData = static_cast<short*>(data);
    short* tmpOutput = cbData->tmpWriteTxBuffer_.get();

    auto toRead = std::min((size_t)cbData->outfifo1->numUsed(), size);
    auto isTuning = cbData->isTuning.load(std::memory_order_acquire);
    if (toRead < size && !isTuning)
    {
        g_outfifo1_empty.fetch_add(1, std::memory_order_relaxed);
    }
    else
    {
        if (toRead >= size && cbData->outfifo1->read(tmpOutput, size) != 0)
        {
            // Raced with a concurrent reset(); nothing was actually copied
            // into tmpOutput, so fall back to silence below instead of
            // replaying stale samples from the previous callback.
            toRead = 0;
        }

        auto numChannels = dev.getNumChannels();
        auto enableVoxTone = g_tx.load(std::memory_order_acquire) && cbData->leftChannelVoxTone.load(std::memory_order_acquire);
        auto sr = dev.getSampleRate();

        if (isTuning)
        {
            // This may be better as a pipeline step but would also add additional
            // complexity (i.e. additional decision steps to let through the sine wave
            // vs. regular TX).
            auto txLevel = g_tuneLevelScale.load(std::memory_order_acquire) * (SHRT_MAX / 2);

            // Load once before the loop and store once after to avoid per-sample atomic
            // memory barriers, which can cause the audio callback to overrun its deadline.
            auto sineWaveSampleNumber = cbData->tuneSineWaveSampleNumber.load(std::memory_order_acquire);
            const double phaseIncrement = 2.0 * M_PI * FDMDV_FCENTRE / sr;
            for (unsigned long index = 0; index < size; index++)
            {
                auto carrierSample = txLevel * sin(phaseIncrement * sineWaveSampleNumber);
                for (int i = 0; i < numChannels; i++)
                {
                    *audioData++ = carrierSample;
                }
                // Conditional branch is cheaper than integer division (% sr).
                if (++sineWaveSampleNumber >= (int)sr)
                    sineWaveSampleNumber = 0;
            }
            cbData->tuneSineWaveSampleNumber.store(sineWaveSampleNumber, std::memory_order_release);
        }
        else
        {
            for (size_t count = 0; count < size; count++, audioData += numChannels)
            {
                 auto output = (count < toRead) ? tmpOutput[count] : 0;

                // write signal to all channels to start. This is so that
                // the compiler can optimize for the most common case.
                for (auto j = 0; j < numChannels; j++)
                {
                    audioData[j] = output;
                }
                        
                // If VOX tone is enabled, go back through and add the VOX tone
                // on the left channel.
                if (enableVoxTone)
                {
                    cbData->voxTonePhase += 2.0*M_PI*VOX_TONE_FREQ/sr;
                    cbData->voxTonePhase -= 2.0*M_PI*floor(cbData->voxTonePhase/(2.0*M_PI));
                    audioData[0] = VOX_TONE_AMP*cos(cbData->voxTonePhase);
                }
            }
        }
    }
}

void MainFrame::OnRxInAudioData_(IAudioDevice& dev, void* data, size_t size, void* state) FREEDV_NONBLOCKING
{
    paCallBackData* cbData = static_cast<paCallBackData*>(state);
    short* audioData = static_cast<short*>(data);
    short* tmpInput = cbData->tmpReadRxBuffer_.get();

    auto numChannels = dev.getNumChannels();
    for (size_t i = 0; i < size; i++, audioData += numChannels)
    {
        tmpInput[i] = audioData[0];
    }
    if (isModemRunning.load(std::memory_order_acquire) && cbData->infifo1->write(tmpInput, size)) 
    {
        g_infifo1_full.fetch_add(1, std::memory_order_relaxed);
    }
}

void MainFrame::OnRxOutAudioData_(IAudioDevice& dev, void* data, size_t size, void* state) FREEDV_NONBLOCKING
{
    paCallBackData* cbData = static_cast<paCallBackData*>(state);
    short* audioData = static_cast<short*>(data);
    short* tmpOutput = cbData->tmpWriteRxBuffer_.get();

    auto toRead = std::min((size_t)cbData->outfifo2->numUsed(), size);
    if (toRead < size)
    {
        g_outfifo2_empty.fetch_add(1, std::memory_order_relaxed);
    }

    if (toRead > 0 && cbData->outfifo2->read(tmpOutput, toRead) != 0)
    {
        // Raced with a concurrent reset(); nothing was actually copied
        // into tmpOutput, so fall back to silence below instead of
        // replaying stale samples from the previous callback.
        toRead = 0;
    }

    auto numChannels = dev.getNumChannels();
    for (size_t count = 0; count < size; count++)
    {
        for (int j = 0; j < numChannels; j++)
        {
            *audioData++ = (count < toRead) ? tmpOutput[count] : 0;
        }
    }
}

