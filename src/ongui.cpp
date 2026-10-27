/*
  ongui.cpp

  The simpler GUI event handlers.
*/

#include <sstream>
#include <iomanip>
#include <locale>
#include <cmath>
#include <cstring>
#include <vector>

#include "main.h"

#include "git_version.h"
#include "build_date.h"
#include "gui/dialogs/dlg_easy_setup.h"
#include "gui/dialogs/dlg_filter.h"
#include "gui/dialogs/dlg_audiooptions.h"
#include "gui/dialogs/dlg_options.h"
#include "gui/dialogs/dlg_ptt.h"
#include "gui/dialogs/freedv_reporter.h"
#include "gui/dialogs/monitor_volume_adj.h"
#include "gui/dialogs/log_entry.h"
#include "gui/util/FrequencyOps.h"

#if defined(WIN32)
#include "rig_control/omnirig/OmniRigController.h"
#endif // defined(WIN32)

#include "codec2_fdmdv.h" // for FDMDV_FCENTRE

extern int g_mode;

extern std::atomic<int>   g_SquelchActive;
extern std::atomic<float> g_SquelchLevel;
extern std::atomic<int>   g_analog;
extern std::atomic<bool>   g_tx;
extern std::atomic<int64_t> g_rxMutedUntilNs;
extern std::atomic<int64_t> g_rxInputLatencyUs;

// Maximum time to keep RX muted after TX while waiting for the radio to
// confirm that it has stopped transmitting.
static constexpr auto RX_MUTE_MAX_WAIT_FOR_RADIO = std::chrono::milliseconds(2000);

// Longest audio device latency that's believed. Some sound systems report
// ones that can't be right (e.g. about 2055 seconds for record streams on
// some PipeWire versions, #1601), and waiting that long after TX would keep
// RX muted, or hold up going back to RX.
static constexpr int64_t MAX_AUDIO_LATENCY_US = 1000000;

static int64_t GetPlausibleLatencyUs_(std::shared_ptr<IAudioDevice> device, const char* which)
{
    if (!device)
    {
        return 0;
    }

    int64_t latency = device->getLatencyInMicroseconds();
    if (latency < 0 || latency > MAX_AUDIO_LATENCY_US)
    {
        int64_t used = latency < 0 ? 0 : MAX_AUDIO_LATENCY_US;
        log_warn(
            "The %s device reported a latency of %lld us; using %lld us instead",
            which, (long long)latency, (long long)used);
        latency = used;
    }
    return latency;
}

extern std::atomic<bool>  g_monitorTxAudio;
extern std::atomic<float> g_monitorTxAudioVol;
extern std::atomic<int>   g_State, g_prev_State;
extern FreeDVInterface freedvInterface;
extern std::atomic<bool> g_queueResync;
extern short *g_error_hist, *g_error_histn;
extern std::atomic<int> g_resyncs;
extern int g_Nc;
extern int g_txLevel;
extern std::atomic<float> g_txLevelScale;
extern int g_tuneLevel;
extern std::atomic<float> g_tuneLevelScale;

extern wxConfigBase *pConfig;
extern std::atomic<bool> endingTx;
extern std::atomic<int> g_outfifo1_empty;
extern std::atomic<bool> g_voice_keyer_tx;
extern paCallBackData* g_rxUserdata;

extern std::atomic<SNDFILE*>            g_sfRecFileFromModulator;
extern std::atomic<bool>                g_recFileFromModulator;
extern std::atomic<SNDFILE*>    g_sfRecFile;
extern std::atomic<bool> g_recFileFromRadio;

extern std::atomic<bool>                g_playFileFromRadio;

extern std::atomic<SNDFILE*> g_sfRecMicFile;

extern wxMutex g_mutexProtectingCallbackData;

extern std::atomic<bool>     g_totBeepActive;

static wxString bandNameForFilter(FilterFrequency band);

extern std::atomic<bool> g_eoo_enqueued;

void clickTune(float frequency); // callback to pass new click freq

//-------------------------------------------------------------------------
// Forces redraw of main panels on window resize.
//-------------------------------------------------------------------------
void MainFrame::topFrame_OnSize( wxSizeEvent& event )
{
    m_auiNbookCtrl->Refresh();
    TopFrame::topFrame_OnSize(event);
}

//-------------------------------------------------------------------------
// OnExitClick()
//-------------------------------------------------------------------------
void MainFrame::OnExitClick(wxCommandEvent& event)
{
    OnExit(event);
}

//-------------------------------------------------------------------------
// OnToolsEasySetup()
//-------------------------------------------------------------------------
void MainFrame::OnToolsEasySetup(wxCommandEvent&)
{
    EasySetupDialog* dlg = new EasySetupDialog(this);
    if (dlg->ShowModal() == wxOK)
    {
        // Show/hide frequency box based on CAT control setup.
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

        // Initialize FreeDV Reporter if required.
        initializeFreeDVReporter_();
        
        // Relayout window so that the changes can take effect.
        m_panel->Layout();
    }
}

//-------------------------------------------------------------------------
// OnToolsEasySetupUI()
//-------------------------------------------------------------------------
void MainFrame::OnToolsEasySetupUI(wxUpdateUIEvent& event)
{
    event.Enable(!m_RxRunning.load(std::memory_order_acquire));
}

void MainFrame::OnActivateWindow(wxActivateEvent& event)
{
    if (m_reporterDialog != nullptr)
    {
        m_reporterDialog->closeTooltip();
    }
    
    event.Skip();
}

//-------------------------------------------------------------------------
// OnToolsFreeDVReporter()
//-------------------------------------------------------------------------
void MainFrame::OnToolsFreeDVReporter(wxCommandEvent&)
{
    if (m_reporterDialog == nullptr)
    {
        m_reporterDialog = new FreeDVReporterDialog(this);
    }

    m_reporterDialog->refreshLayout();
    m_reporterDialog->Show();
    m_reporterDialog->Iconize(false); // undo minimize if required
    m_reporterDialog->Raise(); // brings from background to foreground if required
}

//-------------------------------------------------------------------------
// OnToolsFreeDVReporterUI()
//-------------------------------------------------------------------------
void MainFrame::OnToolsFreeDVReporterUI(wxUpdateUIEvent& event)
{
    event.Enable(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterHostname->ToStdString() != "");
}

//-------------------------------------------------------------------------
// OnToolsAudio()
//-------------------------------------------------------------------------
void MainFrame::OnToolsAudio(wxCommandEvent& event)
{
    bool oldRxOnly = g_nSoundCards.load(std::memory_order_acquire) <= 1 ? true : false;

    wxUnusedVar(event);
    int rv = 0;
    AudioOptsDialog *dlg = new AudioOptsDialog(NULL);
    rv = dlg->ShowModal();
    if(rv == wxOK)
    {
        dlg->ExchangeData(EXCHANGE_DATA_OUT);

        bool newRxOnly = g_nSoundCards.load(std::memory_order_acquire) <= 1 ? true : false;
        if (oldRxOnly != newRxOnly &&
            wxGetApp().m_sharedReporterObject->isValidForReporting())
        {
            // Receive Only status has changed, refresh FreeDV Reporter
            initializeFreeDVReporter_();
        }
    }
    delete dlg;
}

//-------------------------------------------------------------------------
// OnToolsAudioUI()
//-------------------------------------------------------------------------
void MainFrame::OnToolsAudioUI(wxUpdateUIEvent& event)
{
    event.Enable(!m_RxRunning.load(std::memory_order_acquire));
}

//-------------------------------------------------------------------------
// OnToolsFilter()
//-------------------------------------------------------------------------
void MainFrame::OnToolsFilter(wxCommandEvent& event)
{
    wxUnusedVar(event);
    
    if (m_filterDialog == nullptr)
    {
         m_filterDialog = new FilterDlg(NULL, m_RxRunning.load(std::memory_order_acquire), &m_newMicInFilter, &m_newSpkOutFilter);
    }
    else
    {
        m_filterDialog->Iconize(false);
        m_filterDialog->SetFocus();
        m_filterDialog->Raise();
    }
    
    m_filterDialog->Show();
}

//-------------------------------------------------------------------------
// OnToolsOptions()
//-------------------------------------------------------------------------
void MainFrame::OnToolsOptions(wxCommandEvent& event)
{
    bool oldFreqAsKHz = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz;

    wxUnusedVar(event);

    if (optionsDlg == nullptr)
    {
        optionsDlg = new OptionsDlg(NULL);
        // Initialize the session-active state so controls are displayed
        // correctly in case a session is already running.
        optionsDlg->setSessionActive(m_RxRunning.load(std::memory_order_acquire));
    }

    if (optionsDlg->ShowModal() == wxOK)
    {
        // Enable/disable FreeDV Reporter quick options
        m_reporterHidden->Enable(
            wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled &&
            wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnabled);

        // Update reporting list.
        updateReportingFreqList_();

        // UDP logging may have been turned on or off.
        if (!m_RxRunning.load(std::memory_order_acquire))
        {
            m_logQSO->Enable(canLogQso_());
        }
    
        // Show/hide frequency box based on CAT control configuration.
        m_freqBox->Show(isFrequencyControlEnabled_());
        
        // Show/hide stats box
        statsBox->Show(wxGetApp().appConfiguration.showDecodeStats);
        
        // Show/hide legacy modes
        modeBox->Show(wxGetApp().appConfiguration.enableLegacyModes);
        
        bool isEnabled = wxGetApp().appConfiguration.enableLegacyModes && !m_rbRADE->GetValue();
        squelchBox->Show(wxGetApp().appConfiguration.enableLegacyModes);
        m_sliderSQ->Enable(isEnabled);
        m_ckboxSQ->Enable(isEnabled);
        m_textSQ->Enable(isEnabled);
        m_btnCenterRx->Enable(isEnabled);
        m_btnCenterRx->Show(wxGetApp().appConfiguration.enableLegacyModes);
        m_BtnReSync->Enable(isEnabled);
        m_BtnReSync->Show(wxGetApp().appConfiguration.enableLegacyModes);

        // XXX - with really short windows, wxWidgets sometimes doesn't size
        // the components properly until the user resizes the window (even if only
        // by a pixel or two). As a really hacky workaround, we emulate this behavior
        // when restoring window sizing. These resize events also happen after configuration
        // is restored but I'm not sure this is necessary.
        wxSize size = GetSize();
        auto w = size.GetWidth();
        auto h = size.GetHeight();
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

        // Show/hide callsign combo box based on reporting Status
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
        
        // Update voice keyer file if different
        wxFileName fullVKPath(wxGetApp().appConfiguration.voiceKeyerWaveFilePath, wxGetApp().appConfiguration.voiceKeyerWaveFile);
        if (wxString::FromUTF8(vkFileName_.c_str()) != fullVKPath.GetFullPath())
        {
            // Clear filename to force reselection next time VK is triggered.
            vkFileName_ = "";
            vkFileCache_.preload(vkFileName_);
            wxGetApp().appConfiguration.voiceKeyerWaveFile = "";
            setVoiceKeyerButtonLabel_("");
        }
        
        // Adjust frequency labels on main window
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

        // If the "Frequency as kHz" option has changed, update the frequencies
        // in the main window's callsign list.
        if (oldFreqAsKHz != wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
        {
            for (int index = 0; index < m_lastReportedCallsignListView->GetItemCount(); index++)
            {
                wxString newFreq = "";
                wxString freq = m_lastReportedCallsignListView->GetItemText(index, 1);
                double freqDouble = 0;
                wxNumberFormatter::FromString(freq, &freqDouble);

                if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
                {
                    freqDouble *= 1000.0;
                    newFreq = wxNumberFormatter::ToString(freqDouble, 1);
                }
                else
                {
                    freqDouble /= 1000.0;
                    newFreq = wxNumberFormatter::ToString(freqDouble, 4);
                }

                m_lastReportedCallsignListView->SetItem(index, 1, newFreq);
            }
        }

        // Initialize FreeDV Reporter if required.
        if (!m_RxRunning.load(std::memory_order_acquire))
        {
            initializeFreeDVReporter_();
        }
        
        // Refresh distance column label in case setting was changed.
        if (m_reporterDialog != nullptr)
        {
            m_reporterDialog->refreshLayout();
        }

        // Relayout window so that the changes can take effect.
        m_panel->Layout();
    }
}

//-------------------------------------------------------------------------
// OnToolsOptionsUI()
//-------------------------------------------------------------------------
void MainFrame::OnToolsOptionsUI(wxUpdateUIEvent&)
{
}

//-------------------------------------------------------------------------
// OnToolsComCfg()
//-------------------------------------------------------------------------
void MainFrame::OnToolsComCfg(wxCommandEvent& event)
{
    wxUnusedVar(event);

    ComPortsDlg *dlg = new ComPortsDlg(NULL);

    if (dlg->ShowModal() == wxID_OK)
    {
        // Show/hide frequency box based on CAT control configuration.
        m_freqBox->Show(isFrequencyControlEnabled_());
        
        // Reinitialize FreeDV Reporter again in case we changed PTT method.
        initializeFreeDVReporter_();
    }

    delete dlg;
}

//-------------------------------------------------------------------------
// OnToolsComCfgUI()
//-------------------------------------------------------------------------
void MainFrame::OnToolsComCfgUI(wxUpdateUIEvent& event)
{
    event.Enable(!m_RxRunning.load(std::memory_order_acquire));
}

//-------------------------------------------------------------------------
// OnHelpCheckUpdates()
//-------------------------------------------------------------------------
void MainFrame::OnHelpCheckUpdates(wxCommandEvent&)
{
    wxLaunchDefaultBrowser("https://github.com/freedv/freedv-gui/releases");
}

//-------------------------------------------------------------------------
// OnHelpCheckUpdatesUI()
//-------------------------------------------------------------------------
void MainFrame::OnHelpCheckUpdatesUI(wxUpdateUIEvent& event)
{
    event.Enable(!m_RxRunning.load(std::memory_order_acquire));
}

//-------------------------------------------------------------------------
// OnHelpManual()
//-------------------------------------------------------------------------
void MainFrame::OnHelpManual( wxCommandEvent& )
{
    wxLaunchDefaultBrowser("https://github.com/freedv/freedv-gui/blob/master/USER_MANUAL.pdf");
}

//-------------------------------------------------------------------------
// OnHelp()
//-------------------------------------------------------------------------
void MainFrame::OnHelp( wxCommandEvent& )
{
    wxLaunchDefaultBrowser("https://freedv.org/#getting-help");
}

//-------------------------------------------------------------------------
//OnHelpAbout()
//-------------------------------------------------------------------------
void MainFrame::OnHelpAbout(wxCommandEvent& event)
{
    wxUnusedVar(event);
    wxString msg;
    wxString version = wxString::FromUTF8(GetFreeDVVersion().c_str());

    msg.Printf( wxT("FreeDV GUI %s\n\n")
                wxT("For Help and Support visit: http://freedv.org\n\n")

                wxT("GNU Public License V2.1\n\n")
                wxT("Created by David Witten KD0EAG and David Rowe VK5DGR (2012).  ")
                wxT("Currently maintained by Mooneer Salem K6AQ and David Rowe VK5DGR.\n\n")
                wxT("freedv-gui version: %s\n")
                wxT("freedv-gui git hash: %s\n")
                wxT("Using %s\n")
                , version, version, FREEDV_GIT_HASH, hamlib_version
                );

    wxMessageBox(msg, wxT("About"), wxOK | wxICON_INFORMATION, this);
}

void MainFrame::onFrequencyModeChange_(IRigFrequencyController*, uint64_t freq, IRigFrequencyController::Mode mode)
{
    CallAfter([&, mode, freq]() {
        if (firstFreqUpdateOnConnect_)
        {
            firstFreqUpdateOnConnect_ = false;
            return;
        }

        // Update string value.
        switch(mode)
        {
            case IRigFrequencyController::USB:
                m_txtModeStatus->SetLabel(wxT("USB"));
                m_txtModeStatus->Enable(true);
                break;
            case IRigFrequencyController::DIGU:
                m_txtModeStatus->SetLabel(wxT("USB-D"));
                m_txtModeStatus->Enable(true);
                break;
            case IRigFrequencyController::LSB:
                m_txtModeStatus->SetLabel(wxT("LSB"));
                m_txtModeStatus->Enable(true);
                break;
            case IRigFrequencyController::DIGL:
                m_txtModeStatus->SetLabel(wxT("LSB-D"));
                m_txtModeStatus->Enable(true);
                break;
            case IRigFrequencyController::FM:
                m_txtModeStatus->SetLabel(wxT("FM"));
                m_txtModeStatus->Enable(true);
                break;
            case IRigFrequencyController::DIGFM:
                m_txtModeStatus->SetLabel(wxT("FM-D"));
                m_txtModeStatus->Enable(true);
                break;
            case IRigFrequencyController::AM:
                m_txtModeStatus->SetLabel(wxT("AM"));
                m_txtModeStatus->Enable(true);
                break;
            default:
                m_txtModeStatus->SetLabel(wxT("unk"));
                m_txtModeStatus->Enable(false);
                break;
        }

        // Round to the nearest 100 Hz.
        uint64_t wholeFreq = freq / 100;
        uint64_t remainder = freq % 100;

        if (remainder >= 50)
        {
            wholeFreq++;
        }

        auto newFreq = wholeFreq * 100;

        // Widest 60 meter allocation is 5.250-5.450 MHz per https://en.wikipedia.org/wiki/60-meter_band.
        bool is60MeterBand = newFreq >= 5250000 && newFreq <= 5450000;

        // Update color based on the mode and current frequency.
        bool isUsbFreq = newFreq >= 10000000 || is60MeterBand;
        bool isLsbFreq = newFreq < 10000000 && !is60MeterBand;

        bool isMatchingMode = 
            (isUsbFreq && (mode == IRigFrequencyController::USB || mode == IRigFrequencyController::DIGU)) ||
            (isLsbFreq && (mode == IRigFrequencyController::LSB || mode == IRigFrequencyController::DIGL));

        if (isMatchingMode)
        {
            m_txtModeStatus->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
        }
        else
        {
            m_txtModeStatus->SetForegroundColour(wxColor(*wxRED));
        }

        // Update frequency box, unless what's reported was read from the
        // radio before the frequency FreeDV last asked for was set: that would
        // put FreeDV back where it was for a moment (and anything done in that
        // moment, e.g. switching to analog, would go by the old frequency).
        bool staleFrequency = wxGetApp().rigFrequencyController != nullptr &&
            wxGetApp().rigFrequencyController->isFrequencyChangePending(freq);
        if (!suppressFreqModeUpdates_ && !staleFrequency && (
            !wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled ||
            !wxGetApp().appConfiguration.reportingConfiguration.manualFrequencyReporting))
        {
            // wxString::Format() doesn't respect locale but wxNumberFormatter should. Use the latter instead.
            wxString freqString;            
            if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
            {
                freqString = wxNumberFormatter::ToString(newFreq / 1000.0, 1);
            }
            else
            {
                freqString = wxNumberFormatter::ToString(newFreq / 1000.0 / 1000.0, 4);
            }
            
            // Set internal reporting frequency to ensure we don't immediately request
            // a frequency change from the radio (i.e. if rapidly changing frequency
            // on the radio side). Note that this also causes reporting to not fire 
            // by m_cboReportFrequency's change handler, so we should fire off reporting
            // here.
            auto oldFreq = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency;
            wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency = newFreq;
            if (oldFreq != newFreq)
            {
                for (auto& ptr : wxGetApp().m_reporters)
                {
                    ptr->freqChange(wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency);
                }
            }
            
            m_cboReportFrequency->SetValue(freqString);
        }
        m_txtModeStatus->Refresh();

        // Auto-save outgoing band levels, then load the new band's levels
        auto newBandEnum = FreeDVReporterDialog::getFilterForFrequency_(newFreq);
        if (!staleFrequency && newBandEnum != BAND_OTHER && newBandEnum != lastBand_)
        {
            autoSaveCurrentBandLevels_();
            lastBand_ = newBandEnum;
            loadTxAttenForBand_(newBandEnum);
            loadTuneAttenForBand_(newBandEnum);
            applyTxLevel(); // apply both loaded values in one call
        }
    });
}

void MainFrame::onRadioConnected_(IRigController*)
{
    if (wxGetApp().rigFrequencyController && 
        (wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges || wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqChangesOnly) &&
        wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency > 0)
    {
        // Suppress the frequency update message that will occur immediately after
        // connect; this will prevent overwriting of whatever's in the text box.
        firstFreqUpdateOnConnect_ = true;

        // Set frequency/mode to the one pre-selected by the user before start.
        wxGetApp().rigFrequencyController->setFrequency(wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency);
        
        if (wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges)
        {
            wxGetApp().rigFrequencyController->setMode(getCurrentMode_());
        }
    }
}

void MainFrame::onRadioDisconnected_(IRigController*)
{
    CallAfter([&]() {
        m_txtModeStatus->SetLabel(wxT("unk"));
        m_txtModeStatus->Enable(false);
    });
}

//-------------------------------------------------------------------------
// onRigPttChange_(): called (from the rig control thread) once the radio
// has acknowledged a PTT change.
//-------------------------------------------------------------------------
void MainFrame::onRigPttChange_(bool pttState)
{
    if (pttState)
    {
        return;
    }
    
    // The radio is back in RX. If we're still waiting for that after TX,
    // resume RX once audio captured while it was transmitting has made it
    // through the input device.
    auto now = std::chrono::steady_clock::now();
    auto nowInNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    if (g_rxMutedUntilNs.load(std::memory_order_acquire) > nowInNanoseconds)
    {
        auto resumeAt = now + std::chrono::microseconds(g_rxInputLatencyUs.load(std::memory_order_acquire));
        g_rxMutedUntilNs.store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(resumeAt.time_since_epoch()).count(),
            std::memory_order_release);
        log_info("Radio confirmed PTT off, resuming RX in %d us", (int)g_rxInputLatencyUs.load(std::memory_order_acquire));
    }
}

// Attempt to talk to rig using Hamlib

bool MainFrame::OpenHamlibRig() {
    if (wxGetApp().appConfiguration.rigControlConfiguration.hamlibUseForPTT != true)
       return false;
    
    int rig = wxGetApp().m_intHamlibRig;    
    if (rig == -1)
    {
        std::string fullErr = "The radio's model is empty. This is likely due to changes in Hamlib between FreeDV releases. Please click Stop Modem, double-check your CAT settings and push Start Modem again.";
        CallAfter([&, fullErr]() {
            wxMessageBox(fullErr, wxT("Error"), wxOK | wxICON_ERROR, this);
        });
        return false;
    }
    
    wxString port = wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialPort;
    wxString pttPort = wxGetApp().appConfiguration.rigControlConfiguration.hamlibPttSerialPort;
    auto pttType = (HamlibRigController::PttType)wxGetApp().appConfiguration.rigControlConfiguration.hamlibPTTType.get();
    
    int serial_rate = wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialRate;
    if (wxGetApp().CanAccessSerialPort((const char*)port.ToUTF8()) && (
        pttType == HamlibRigController::PTT_VIA_CAT || pttType == HamlibRigController::PTT_VIA_NONE || wxGetApp().CanAccessSerialPort((const char*)pttPort.ToUTF8())))
    {
        auto tmp = std::make_shared<HamlibRigController>(
            rig, (const char*)port.mb_str(wxConvUTF8), serial_rate, wxGetApp().appConfiguration.rigControlConfiguration.hamlibIcomCIVAddress,
            pttType, pttType == HamlibRigController::PTT_VIA_CAT || pttType == HamlibRigController::PTT_VIA_NONE ? (const char*)port.mb_str(wxConvUTF8) : (const char*)pttPort.mb_str(wxConvUTF8),
            (wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges || wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqChangesOnly),
            wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqChangesOnly,
            wxGetApp().appConfiguration.rigControlConfiguration.hamlibForceRTSOn,
            wxGetApp().appConfiguration.rigControlConfiguration.hamlibForceDTROn);

        // Hamlib also controls PTT.
        firstFreqUpdateOnConnect_ = false;
        wxGetApp().rigFrequencyController = tmp;
        wxGetApp().rigPttController = tmp;
        
        wxGetApp().rigFrequencyController->onRigError += [this](IRigController*, std::string const& err)
        {
            std::string fullErr = "Couldn't connect to Radio with hamlib: " + err;
            CallAfter([&, fullErr]() {
                wxMessageBox(fullErr, wxT("Error"), wxOK | wxICON_ERROR, this);
            });
        };

        wxGetApp().rigFrequencyController->onRigConnected += [&](IRigController* ptr) {
            onRadioConnected_(ptr);
        };

        wxGetApp().rigFrequencyController->onRigDisconnected += [&](IRigController* ptr) {
            onRadioDisconnected_(ptr);
        };

        wxGetApp().rigFrequencyController->onFreqModeChange += [&](IRigFrequencyController* ptr, uint64_t freq, IRigFrequencyController::Mode mode) {
            onFrequencyModeChange_(ptr, freq, mode);
        };
        
        wxGetApp().rigPttController->onPttChange += [&](IRigPttController*, bool state) {
            onRigPttChange_(state);
        };
        wxGetApp().rigFrequencyController->connect();
        return true;
    }
    else
    {
        return false;
    }
}

#if defined(WIN32)
// TBD -- a lot of this can be combined with the Hamlib logic above.
void MainFrame::OpenOmniRig() 
{
    auto tmp = std::make_shared<OmniRigController>(
        wxGetApp().appConfiguration.rigControlConfiguration.omniRigRigId,
        (wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges || wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqChangesOnly),
        wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqChangesOnly);

    // OmniRig also controls PTT.
    wxGetApp().rigFrequencyController = tmp;
    if (!wxGetApp().rigPttController)
    {
        wxGetApp().rigPttController = tmp;
        wxGetApp().rigPttController->onPttChange += [&](IRigPttController*, bool state) {
            onRigPttChange_(state);
        };
    }

    wxGetApp().rigFrequencyController->onRigError += [this](IRigController*, std::string err)
    {
        std::string fullErr = "Couldn't connect to Radio with OmniRig: " + err;
        CallAfter([&, fullErr]() {
            wxMessageBox(fullErr, wxT("Error"), wxOK | wxICON_ERROR, this);
        });
    };

    wxGetApp().rigFrequencyController->onRigConnected += [&](IRigController* ptr) {
        onRadioConnected_(ptr);
    };

    wxGetApp().rigFrequencyController->onRigDisconnected += [&](IRigController* ptr) {
        onRadioDisconnected_(ptr);
    };

    wxGetApp().rigFrequencyController->onFreqModeChange += [&](IRigFrequencyController* ptr, uint64_t freq, IRigFrequencyController::Mode mode) {
        onFrequencyModeChange_(ptr, freq, mode);
    };

    wxGetApp().rigFrequencyController->connect();
}
#endif // defined(WIN32)

//-------------------------------------------------------------------------
// OnCloseFrame()
//-------------------------------------------------------------------------
void MainFrame::OnCloseFrame(wxCloseEvent&)
{
    auto engine = AudioEngineFactory::GetAudioEngine();
    engine->stop();
    engine->setOnEngineError(nullptr, nullptr);
    
    Destroy();
}

//-------------------------------------------------------------------------
// OnTop()
//-------------------------------------------------------------------------
void MainFrame::OnTop(wxCommandEvent& event)
{
    auto style = GetWindowStyle();

    // Clear wxSTAY_ON_TOP first if it's already set.
    style &= ~wxSTAY_ON_TOP;
    
    // (Re)set wxSTAY_ON_TOP depending on whether the menu option is checked.
    if (event.IsChecked())
    {
        style |= wxSTAY_ON_TOP;
    }

    SetWindowStyle(style);
}

//-------------------------------------------------------------------------
// OnDeleteConfig()
//-------------------------------------------------------------------------
void MainFrame::OnDeleteConfig(wxCommandEvent&)
{
    wxMessageDialog messageDialog(
        this, "Would you like to restore configuration to defaults?", wxT("Restore Defaults"),
        wxYES_NO | wxICON_QUESTION | wxCENTRE);

    auto answer = messageDialog.ShowModal();
    if (answer == wxID_YES)
    {
        if(pConfig->DeleteAll())
        {
            wxLogMessage(wxT("Config file/registry key successfully deleted."));
            
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
        else
        {
            wxLogError(wxT("Deleting config file/registry key failed."));
        }
    }
}

//-------------------------------------------------------------------------
// OnDeleteConfigUI()
//-------------------------------------------------------------------------
void MainFrame::OnDeleteConfigUI( wxUpdateUIEvent& event )
{
    event.Enable(!m_RxRunning.load(std::memory_order_acquire));
}

//-------------------------------------------------------------------------
// Paint()
//-------------------------------------------------------------------------
void MainFrame::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    wxPaintDC dc(this);

    if(GetMenuBar()->IsChecked(ID_PAINT_BG))
    {
        dc.Clear();
    }
    dc.SetUserScale(m_zoom, m_zoom);
}

//-------------------------------------------------------------------------
// OnCmdSliderScroll()
//-------------------------------------------------------------------------
void MainFrame::OnCmdSliderScroll(wxScrollEvent& event)
{
    float newSquelchLevel = (float)m_sliderSQ->GetValue()/2.0 - 5.0;
    g_SquelchLevel.store(newSquelchLevel, std::memory_order_release);
    wxString sqsnr_string = wxNumberFormatter::ToString(newSquelchLevel, 1) + "dB"; // 0.5 dB steps
    m_textSQ->SetLabel(sqsnr_string);

    event.Skip();
}

//-------------------------------------------------------------------------
// bandNameForFilter() - maps FilterFrequency enum to config key string
//-------------------------------------------------------------------------
static wxString bandNameForFilter(FilterFrequency band)
{
    switch (band)
    {
        case BAND_160M:    return "160m";
        case BAND_80M:     return "80m";
        case BAND_60M:     return "60m";
        case BAND_40M:     return "40m";
        case BAND_30M:     return "30m";
        case BAND_20M:     return "20m";
        case BAND_17M:     return "17m";
        case BAND_15M:     return "15m";
        case BAND_12M:     return "12m";
        case BAND_10M:     return "10m";
        case BAND_VHF_UHF: return "vhfuhf";
        default:           return "";
    }
}

// autoSaveCurrentBandLevels_() - if the current band has saved per-band
// values, update them with the current live levels.
//-------------------------------------------------------------------------
void MainFrame::autoSaveCurrentBandLevels_(bool writeConfig)
{
    // Use lastBand_ (not the current reporting frequency) so that on a band
    // change we save the *outgoing* band's levels before loading the new one.
    wxString bandName = bandNameForFilter(lastBand_);
    if (bandName.IsEmpty())
        return;
    bool changed = false;
    auto& txAtten = wxGetApp().appConfiguration.txAttenByBand;
    if (txAtten->find(bandName) != txAtten->end())
    {
        txAtten->insert_or_assign(bandName, g_txLevel);
        changed = true;
    }
    auto& tuneAtten = wxGetApp().appConfiguration.tuneAttenByBand;
    if (tuneAtten->find(bandName) != tuneAtten->end())
    {
        tuneAtten->insert_or_assign(bandName, g_tuneLevel);
        changed = true;
    }
    if (changed && writeConfig)
        wxGetApp().appConfiguration.save(pConfig);
}

// loadTxAttenForBand_() - load saved TX attenuation for the given band
//-------------------------------------------------------------------------
void MainFrame::loadTxAttenForBand_(FilterFrequency band)
{
    wxString bandName = bandNameForFilter(band);
    auto& atten = wxGetApp().appConfiguration.txAttenByBand;
    auto it = atten->find(bandName);
    g_txLevel = (it != atten->end()) ? it->second : (int)wxGetApp().appConfiguration.transmitLevel;
    txLoadedLevel_ = g_txLevel; // snapshot for Restore
    // Caller is responsible for calling applyTxLevel() after all band values are loaded.
}

// loadTuneAttenForBand_() - load saved tune attenuation for the given band
//-------------------------------------------------------------------------
void MainFrame::loadTuneAttenForBand_(FilterFrequency band)
{
    wxString bandName = bandNameForFilter(band);
    auto& atten = wxGetApp().appConfiguration.tuneAttenByBand;
    auto it = atten->find(bandName);
    g_tuneLevel = (it != atten->end()) ? it->second : (int)wxGetApp().appConfiguration.tuneLevel;
    tuneLoadedLevel_ = g_tuneLevel; // snapshot for Restore
    // Caller is responsible for calling applyTxLevel() after all band values are loaded.
}

// applyTxLevel() - shared helper to apply g_txLevel and update the UI
//-------------------------------------------------------------------------
void MainFrame::applyTxLevel()
{
    bool isTuning = m_btnTogTune->GetValue();
    wxString fmtString;

    if (g_txLevel < TX_ATTENUATION_MIN) g_txLevel = TX_ATTENUATION_MIN;
    if (g_txLevel > TX_ATTENUATION_MAX) g_txLevel = TX_ATTENUATION_MAX;
    g_txLevelScale.store(exp(g_txLevel / 10.0 / 20.0 * log(10.0)), std::memory_order_release);

    if (g_tuneLevel < TX_ATTENUATION_MIN) g_tuneLevel = TX_ATTENUATION_MIN;
    if (g_tuneLevel > TX_ATTENUATION_MAX) g_tuneLevel = TX_ATTENUATION_MAX;
    g_tuneLevelScale.store(exp(g_tuneLevel / 10.0 / 20.0 * log(10.0)), std::memory_order_release);

    if (isTuning)
        fmtString = wxString::Format(MIC_SPKR_LEVEL_FORMAT_STR, wxNumberFormatter::ToString((double)g_tuneLevel/10.0, 1), DECIBEL_STR);
    else
        fmtString = wxString::Format(MIC_SPKR_LEVEL_FORMAT_STR, wxNumberFormatter::ToString((double)g_txLevel/10.0, 1), DECIBEL_STR);

    m_txtTxLevelNum->SetLabel(fmtString);

    // Only update the global defaults when the current band has no per-band
    // override, so that per-band values don't contaminate the global default.
    // Use the current reporting frequency directly rather than lastBand_ to
    // avoid stale state before the first band-change event fires.
    wxString bandName = bandNameForFilter(FreeDVReporterDialog::getFilterForFrequency_(
        wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency));
    if (isTuning)
    {
        if (bandName.IsEmpty() || wxGetApp().appConfiguration.tuneAttenByBand->find(bandName) == wxGetApp().appConfiguration.tuneAttenByBand->end())
            wxGetApp().appConfiguration.tuneLevel = g_tuneLevel;
    }
    else
    {
        if (bandName.IsEmpty() || wxGetApp().appConfiguration.txAttenByBand->find(bandName) == wxGetApp().appConfiguration.txAttenByBand->end())
            wxGetApp().appConfiguration.transmitLevel = g_txLevel;
    }
}

void MainFrame::OnTxLevelDecrBig( wxCommandEvent& ) { if (m_btnTogTune->GetValue()) g_tuneLevel -= TX_ATTENUATION_LARGE_STEP; else g_txLevel -= TX_ATTENUATION_LARGE_STEP; applyTxLevel(); }
void MainFrame::OnTxLevelDecr( wxCommandEvent& )    { if (m_btnTogTune->GetValue()) g_tuneLevel -= TX_ATTENUATION_SMALL_STEP;  else g_txLevel -= TX_ATTENUATION_SMALL_STEP;  applyTxLevel(); }
void MainFrame::OnTxLevelIncr( wxCommandEvent& )    { if (m_btnTogTune->GetValue()) g_tuneLevel += TX_ATTENUATION_SMALL_STEP;  else g_txLevel += TX_ATTENUATION_SMALL_STEP;  applyTxLevel(); }
void MainFrame::OnTxLevelIncrBig( wxCommandEvent& ) { if (m_btnTogTune->GetValue()) g_tuneLevel += TX_ATTENUATION_LARGE_STEP; else g_txLevel += TX_ATTENUATION_LARGE_STEP; applyTxLevel(); }

void MainFrame::OnTxLevelMouseWheel( wxMouseEvent& event )
{
    int delta = (event.GetWheelRotation() > 0) ? TX_ATTENUATION_SMALL_STEP : -TX_ATTENUATION_SMALL_STEP;
    if (m_btnTogTune->GetValue()) g_tuneLevel += delta; else g_txLevel += delta;
    applyTxLevel();
}

void MainFrame::OnTxLevelContextMenu( wxContextMenuEvent& )
{
    uint64_t freq = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency;
    FilterFrequency bandEnum = FreeDVReporterDialog::getFilterForFrequency_(freq);
    wxString bandName = bandNameForFilter(bandEnum); // string used for display labels and map keys

    if (bandName.IsEmpty())
        return;

    auto& atten = wxGetApp().appConfiguration.txAttenByBand;
    bool hasSaved = (atten->find(bandName) != atten->end());

    wxMenu menu;
    wxString toggleLabel = hasSaved
        ? wxString::Format(_("Disable auto-save of TX atten for %s"), bandName)
        : wxString::Format(_("Enable auto-save of TX atten for %s"),  bandName);
    auto toggleItem  = menu.Append(wxID_ANY, toggleLabel);
    auto restoreItem = menu.Append(wxID_ANY, wxString::Format(_("Restore TX atten level for %s"), bandName));
    restoreItem->Enable(hasSaved);

    // Toggle: Enable saves the current level and opts the band into auto-save;
    // Disable removes the entry so the band reverts to the global default.
    menu.Bind(wxEVT_MENU, [this, bandName, hasSaved](wxCommandEvent&) {
        if (hasSaved)
            wxGetApp().appConfiguration.txAttenByBand->erase(bandName);
        else
        {
            txLoadedLevel_ = g_txLevel; // record restore point at Enable time
            wxGetApp().appConfiguration.txAttenByBand->insert_or_assign(bandName, g_txLevel);
        }
        wxGetApp().appConfiguration.save(pConfig);
    }, toggleItem->GetId());
    // Restore reverts to txLoadedLevel_: the value active when this band was
    // entered or when Enable was last clicked, whichever is more recent.
    // Using a snapshot avoids the case where auto-save on band departure has
    // already overwritten the map entry with the adjusted value.
    menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        g_txLevel = txLoadedLevel_;
        applyTxLevel();
    }, restoreItem->GetId());

    PopupMenu(&menu);
}

void MainFrame::OnTuneAttenContextMenu( wxContextMenuEvent& )
{
    wxMenu menu;

    auto minItem = menu.Append(wxID_ANY, _("Set tune output to minimum (-30 dB)"));
    menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        g_tuneLevel = TX_ATTENUATION_MIN;
        applyTxLevel();
    }, minItem->GetId());

    uint64_t freq = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency;
    FilterFrequency bandEnum = FreeDVReporterDialog::getFilterForFrequency_(freq);
    wxString bandName = bandNameForFilter(bandEnum);

    if (!bandName.IsEmpty())
    {
        menu.AppendSeparator();
        auto& atten = wxGetApp().appConfiguration.tuneAttenByBand;
        bool hasSaved = (atten->find(bandName) != atten->end());

        wxString toggleLabel = hasSaved
            ? wxString::Format(_("Disable auto-save of tune atten for %s"), bandName)
            : wxString::Format(_("Enable auto-save of tune atten for %s"),  bandName);
        auto toggleItem  = menu.Append(wxID_ANY, toggleLabel);
        auto restoreItem = menu.Append(wxID_ANY, wxString::Format(_("Restore tune atten level for %s"), bandName));
        restoreItem->Enable(hasSaved);

        menu.Bind(wxEVT_MENU, [this, bandName, hasSaved](wxCommandEvent&) {
            if (hasSaved)
                wxGetApp().appConfiguration.tuneAttenByBand->erase(bandName);
            else
            {
                tuneLoadedLevel_ = g_tuneLevel;
                wxGetApp().appConfiguration.tuneAttenByBand->insert_or_assign(bandName, g_tuneLevel);
            }
            wxGetApp().appConfiguration.save(pConfig);
        }, toggleItem->GetId());
        menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            g_tuneLevel = tuneLoadedLevel_;
            applyTxLevel();
        }, restoreItem->GetId());
    }

    PopupMenu(&menu);
}

//-------------------------------------------------------------------------
// OnChangeMicSpkrLevel()
//-------------------------------------------------------------------------
void MainFrame::OnChangeMicSpkrLevel( wxScrollEvent& )
{
    auto sliderLevel = (double)m_sliderMicSpkrLevel->GetValue() / 10.0;
    
    if (g_tx.load(std::memory_order_acquire))
    {
        wxGetApp().appConfiguration.filterConfiguration.micInChannel.volInDB = sliderLevel;
        m_newMicInFilter.store(true, std::memory_order_release);
    }
    else
    {
        wxGetApp().appConfiguration.filterConfiguration.spkOutChannel.volInDB = sliderLevel;
        m_newSpkOutFilter.store(true, std::memory_order_release);
    }
    
    wxString fmtString = wxString::Format(MIC_SPKR_LEVEL_FORMAT_STR, wxNumberFormatter::ToString((double)sliderLevel, 1), DECIBEL_STR);
    m_txtMicSpkrLevelNum->SetLabel(fmtString);
}

//-------------------------------------------------------------------------
// OnCheckSQClick()
//-------------------------------------------------------------------------
void MainFrame::OnCheckSQClick(wxCommandEvent&)
{
    if(!g_SquelchActive.load(std::memory_order_acquire))
    {
        g_SquelchActive.store(true, std::memory_order_release);
    }
    else
    {
        g_SquelchActive.store(false, std::memory_order_release);
    }
}

void MainFrame::setsnrBeta(bool snrSlow)
{
    if(snrSlow)
    {
        m_snrBeta = 0.95; // make this closer to 1.0 to smooth SNR est further
    }
    else
    {
        m_snrBeta = 0.0; // no smoothing of SNR estimate from demodulator
    }
}

//-------------------------------------------------------------------------
// OnCheckSQClick()
//-------------------------------------------------------------------------
void MainFrame::OnCheckSNRClick(wxCommandEvent&)
{
    wxGetApp().appConfiguration.snrSlow = m_ckboxSNR->GetValue();
    setsnrBeta(wxGetApp().appConfiguration.snrSlow);
}

static bool PttKeyDown_ = false;
int MainApp::FilterEvent(wxEvent& event)
{
    if ((event.GetEventType() == wxEVT_KEY_DOWN) &&
        (((wxKeyEvent&)event).GetKeyCode() == wxGetApp().appConfiguration.pttKeyCode))
        {
            bool pttKeyChanging = !PttKeyDown_;
            PttKeyDown_ = true;

            // only use space to toggle PTT if we are running and no modal dialogs (like options) up
            bool mainWindowActive = frame->IsActive();
            bool reporterActiveButNotUpdatingTextMessage =
                frame->m_reporterDialog != nullptr && frame->m_reporterDialog->IsActive() &&
                !frame->m_reporterDialog->isTextMessageFieldInFocus();
            bool totWarningActive = frame->m_totWarningDialog_ != nullptr && frame->m_totWarningDialog_->IsActive();
            bool tuneActive = frame->m_btnTogTune->GetValue();

            // m_pttKeyRequireRelease_ blocks a key held through a forced TX stop
            // (e.g. TOT) from immediately restarting TX -- see main.h.
            if (frame->m_RxRunning.load(std::memory_order_acquire) && !tuneActive && !frame->m_pttKeyRequireRelease_ &&
                (mainWindowActive || totWarningActive || reporterActiveButNotUpdatingTextMessage) &&
                wxGetApp().appConfiguration.enableSpaceBarForPTT && !frame->isReceiveOnly()) {

                // space bar controls tx/rx if keyer not running
                if (frame->vk_state == VK_IDLE) {
                    if (wxGetApp().appConfiguration.pttMomentaryMode) {
                        // Momentary mode: start TX only on the initial key press (not repeated events).
                        if (!g_tx.load(std::memory_order_acquire)) {
                            frame->m_btnTogPTT->SetValue(true);
                            frame->m_btnTogPTT->SetBackgroundColour(*wxRED);
                            frame->togglePTT();
                        }
                    } else if (pttKeyChanging) {
                        // Latching mode: toggle TX state on each key press.
                        if (frame->m_btnTogPTT->GetValue())
                            frame->m_btnTogPTT->SetValue(false);
                        else
                            frame->m_btnTogPTT->SetValue(true);

                        frame->togglePTT();
                    }
                }
                else // space bar stops keyer
                    frame->VoiceKeyerProcessEvent(VK_SPACE_BAR);

                return Event_Processed; // absorb key so we don't toggle control with focus (e.g. Start)

            }
        }

    // In momentary mode, stop TX when the PTT key is released.
    if ((event.GetEventType() == wxEVT_KEY_UP) &&
        (((wxKeyEvent&)event).GetKeyCode() == wxGetApp().appConfiguration.pttKeyCode))
        {
            PttKeyDown_ = false;

            bool mainWindowActive = frame->IsActive();
            bool reporterActiveButNotUpdatingTextMessage =
                frame->m_reporterDialog != nullptr && frame->m_reporterDialog->IsActive() &&
                !frame->m_reporterDialog->isTextMessageFieldInFocus();
            bool totWarningActive = frame->m_totWarningDialog_ != nullptr && frame->m_totWarningDialog_->IsActive();
            if (frame->m_RxRunning.load(std::memory_order_acquire) && (mainWindowActive || totWarningActive || reporterActiveButNotUpdatingTextMessage) &&
                wxGetApp().appConfiguration.enableSpaceBarForPTT && !frame->isReceiveOnly() &&
                wxGetApp().appConfiguration.pttMomentaryMode) {

                if (frame->vk_state == VK_IDLE) {
                    if (g_tx.load(std::memory_order_acquire)) {
                        frame->m_btnTogPTT->SetValue(false);
                        frame->m_btnTogPTT->SetBackgroundColour(wxNullColour);
                        frame->togglePTT();
                    } else if (frame->m_btnTogPTT->GetValue()) {
                        // Key released before g_tx caught up -- likely still inside
                        // togglePTT()'s TX/RX delay loop for the start that's in
                        // progress. Calling togglePTT() here would no-op against its
                        // re-entrancy guard, so remember the release and let
                        // togglePTT() action it once the start finishes -- see
                        // m_momentaryKeyReleasedDuringChangeover_ in main.h.
                        log_info("PTT key released mid-changeover -- deferring momentary stop");
                        frame->m_momentaryKeyReleasedDuringChangeover_ = true;
                    }
                }
                return Event_Processed;
            }
        }

    return Event_Skip;
}

void MainFrame::OnSetMonitorTxAudio( wxCommandEvent& event )
{
    wxGetApp().appConfiguration.monitorTxAudio = event.IsChecked();
    g_monitorTxAudio.store(wxGetApp().appConfiguration.monitorTxAudio, std::memory_order_release);
    adjustMonitorPttVolMenuItem_->Enable(wxGetApp().appConfiguration.monitorTxAudio);
}

void MainFrame::OnSetMonitorTxAudioVol( wxCommandEvent& )
{
    auto popup = new MonitorVolumeAdjPopup(this, wxGetApp().appConfiguration.monitorTxAudioVol, g_monitorTxAudioVol);
    popup->Popup();
}

//-------------------------------------------------------------------------
// OnTogBtnPTTRightClick(): show right-click menu for PTT button
//-------------------------------------------------------------------------
void MainFrame::OnTogBtnPTTRightClick( wxContextMenuEvent& )
{
    m_btnTogPTT->PopupMenu(pttPopupMenu_, LeftOffsetContextMenuPosition(m_btnTogPTT));
}

//-------------------------------------------------------------------------
// OnTogBtnPTTMouseDown()
// Set TX colour immediately on mouse press when going RX->TX, avoiding a GTK
// blue-flash during the TX delay before togglePTT() sets it on release.
// Only fires when the pipeline is genuinely in RX (g_tx false); during the
// TX->RX drain g_tx is still true, so clicks there are correctly ignored.
// NOTE for upstream: this is a simple cosmetic fix. A fuller alternative would
// be to start TX here on press and suppress the togglePTT() call on release.
//-------------------------------------------------------------------------
void MainFrame::OnTogBtnPTTMouseDown(wxMouseEvent& event)
{
    if (txChangeoverOccurring_) return;
    event.Skip();
}

//-------------------------------------------------------------------------
// OnTogBtnPTTMouseLeave()
// Reset premature TX colour if mouse leaves button before release and TX
// has not actually started, preventing a stuck-red button.
//-------------------------------------------------------------------------
void MainFrame::OnTogBtnPTTMouseLeave(wxMouseEvent& event)
{
    if (!m_btnTogPTT->GetValue() && !g_tx.load(std::memory_order_acquire))
    {
        m_btnTogPTT->SetBackgroundColour(wxNullColour);
#if !defined(__APPLE__)
        // macOS limitations prevent the foreground color of toggle buttons from being 
        // reliably set, so don't mess with it in the first place.
        m_btnTogPTT->SetForegroundColour(wxNullColour);
#endif // !defined(__APPLE__)
        m_btnTogPTT->Refresh();
    }
    event.Skip();
}

//-------------------------------------------------------------------------
// OnTogBtnPTT ()
//-------------------------------------------------------------------------
void MainFrame::OnTogBtnPTT (wxCommandEvent&)
{
    if (vk_state == VK_TX)
    {
        // Disable TX via VK code to prevent state inconsistencies.
        VoiceKeyerProcessEvent(VK_SPACE_BAR);
    }
    else 
    {
        togglePTT();
    }
}

void MainFrame::playTotBeep_()
{
    log_info("Playing TOT beep");

    if (g_totBeepActive.load(std::memory_order_acquire))
        return;

    g_totBeepActive.store(true, std::memory_order_release);
}

void MainFrame::stopTotBeep_()
{
    log_info("Stopping TOT beep");
    m_totLastBeepTime_ = {};
    if (!g_totBeepActive.load(std::memory_order_acquire))
        return;

    g_totBeepActive.store(false, std::memory_order_release);
}

//-------------------------------------------------------------------------
// OnTOTTimer()
// Time-Out Timer handler: fires when the configured TX time limit expires.
//-------------------------------------------------------------------------
void MainFrame::OnTOTTimer(wxTimerEvent&)
{
    if (!g_tx.load(std::memory_order_acquire))
        return;

    log_info("Time-Out Timer (TOT) expired — stopping transmit");

    if (m_totWarningTimer.IsRunning())
        m_totWarningTimer.Stop();

    if (m_totWarningDialog_)
    {
        auto dlg = m_totWarningDialog_;
        m_totWarningDialog_ = nullptr;
        dlg->Destroy();
    }
    m_totCurrentDurationMs = 0;
    stopTotBeep_();

    if (vk_state == VK_TX)
    {
        VoiceKeyerProcessEvent(VK_SPACE_BAR);
    }
    else
    {
        m_btnTogPTT->SetValue(false);
        endingTx.store(true, std::memory_order_release);
        togglePTT();

        // If the spacebar PTT key is still physically held down (e.g. something
        // resting on the keyboard), holding it through the timeout must not be
        // able to immediately re-key the rig -- that would defeat the whole
        // point of the TOT. wxEVT_KEY_UP/DOWN aren't reliable for detecting
        // "still held" here: on some platforms, a held key generates real
        // key-up/key-down event pairs at the OS repeat rate rather than a
        // single sustained key-down, so we poll the actual OS key state
        // instead and keep the spacebar disabled until it genuinely goes up.
        if (wxGetApp().appConfiguration.pttMomentaryMode &&
            PttKeyDown_)
        {
            log_info("TOT fired while PTT key still held -- blocking restart until key is released");
            m_pttKeyRequireRelease_ = true;
            m_pttKeyPollTimer.Start(30, wxTIMER_CONTINUOUS);
        }
    }
}

void MainFrame::OnPttKeyPollTimer(wxTimerEvent&)
{
    if (!PttKeyDown_)
    {
        log_info("PTT key released -- spacebar PTT re-armed");
        m_pttKeyRequireRelease_ = false;
        m_pttKeyPollTimer.Stop();
    }
}

void MainFrame::OnTOTWarningTimer(wxTimerEvent&)
{
    if (!g_tx.load(std::memory_order_acquire) || m_totCurrentDurationMs <= 0)
        return;

    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_totTxStartTime).count();
    int remaining = m_totCurrentDurationMs - (int)elapsed;

    if (remaining > 0 && remaining <= 15000)
    {
        // Beep once when the window pops up.
        bool firstBeep = (m_totLastBeepTime_ == decltype(m_totLastBeepTime_){});
        if (firstBeep) {
            playTotBeep_();
            m_totLastBeepTime_ = now;
        }

        if (!m_totWarningDialog_)
        {
            m_totWarningDialog_ = new TotWarningDialog(
                this, remaining,
                [this]() {
                    m_totWarningDialog_->Destroy();
                    m_totWarningDialog_ = nullptr;
                    
                    if (!g_tx.load(std::memory_order_acquire) || m_totCurrentDurationMs <= 0)
                        return;

                    m_totCurrentDurationMs = wxGetApp().appConfiguration.rigControlConfiguration.totTimerSecs * 1000;
                    m_totTxStartTime = std::chrono::high_resolution_clock::now();
                    m_totTimer.Start(m_totCurrentDurationMs, wxTIMER_ONE_SHOT);
                    m_totLastBeepTime_ = decltype(m_totLastBeepTime_){};
                    log_info("Time-Out Timer (TOT) extended — %d ms remaining", m_totCurrentDurationMs);
                }
            );
            m_totWarningDialog_->Show();
            m_totWarningDialog_->Iconize(false); // undo minimize if required
            m_totWarningDialog_->Raise(); // brings from background to foreground if required
        }
        else
        {
            m_totWarningDialog_->updateRemainingTime(remaining);
        }
    }
    else if (remaining > 15000 && m_totWarningDialog_)
    {
        auto dlg = m_totWarningDialog_;
        m_totWarningDialog_ = nullptr;
        dlg->Destroy();
        m_totLastBeepTime_ = decltype(m_totLastBeepTime_){};
    }
}

// Returns the notebook page index that should be restored once we're done showing
// "Frm Mic" (e.g. on RX, or after voice keyer recording finishes).
//
// GetSelection() isn't the right choice here since more than one tab group can be
// visible at the same time once tabs have been split (e.g. via a saved custom tab
// layout) - see https://forums.wxwidgets.org/viewtopic.php?t=14721. Instead, ask the
// specific tab group that "Frm Mic" lives in what's actually active there.
int MainFrame::captureCurrentMicGroupTab_()
{
    auto savedTab = m_auiNbookCtrl->GetSelection();

#if wxCHECK_VERSION(3,1,4)
    wxAuiTabCtrl* fromMicTabControl = nullptr;
    int fromMicTabIndex = 0;
    if (m_panelSpeechIn != nullptr &&
        m_auiNbookCtrl->FindTab(m_panelSpeechIn, &fromMicTabControl, &fromMicTabIndex))
    {
        int localActiveIdx = fromMicTabControl->GetActivePage();
        if (localActiveIdx >= 0 && localActiveIdx < (int)fromMicTabControl->GetPageCount())
        {
            wxWindow* activeWindow = fromMicTabControl->GetWindowFromIdx(localActiveIdx);
            savedTab = m_auiNbookCtrl->GetPageIndex(activeWindow);
        }
    }
#endif // wxCHECK_VERSION(3,1,4)

    return savedTab;
}

void MainFrame::togglePTT(void) {
    // Guard against re-entrant calls during the TX drain (Yield() processes events).
    // This is necessary because we are not disabling the button during the changeover,
    // as doing so causes the text on the button to be unreadable.
    if (txChangeoverOccurring_) 
    {
        return;
    }
    txChangeoverOccurring_ = true;

    // If we're playing a RX file, we want to stop it before TX.
    if (g_playFileFromRadio.load(std::memory_order_acquire))
    {
        StopPlaybackFileFromRadio();
    }

    std::chrono::high_resolution_clock highResClock;

    // Record direction now; button value may be toggled by a stray click during
    // the drain loops below, which would corrupt newTx at the end if not checked.
    const bool wasInTx = g_tx.load(std::memory_order_acquire);

    // Change tabbed page in centre panel depending on PTT state

    if (wasInTx)
    {
        // Amber during TX->RX drain: distinct from TX (red) and RX (default),
        // black text readable throughout. Foreground is pinned explicitly
        // (not just left to the GTK theme) since some themes/window states
        // - e.g. backdrop while the TOT warning dialog has focus - would
        // otherwise dim or recolour the default text away from black.
        m_btnTogPTT->SetBackgroundColour(wxColour(255, 165, 0));
#if !defined(__APPLE__)
        // macOS limitations prevent the foreground color of toggle buttons from being 
        // reliably set, so don't mess with it in the first place.
        m_btnTogPTT->SetForegroundColour(*wxBLACK);
#endif // !defined(__APPLE__)
        m_btnTogPTT->SetLabel("TX Ending");
        m_btnTogPTT->Refresh();

        // Stop Time-Out Timer on TX->RX transition (user stopped, VK finished, or TOT fired).
        if (m_totTimer.IsRunning())
            m_totTimer.Stop();
        if (m_totWarningTimer.IsRunning())
            m_totWarningTimer.Stop();
        if (m_totWarningDialog_)
        {
            auto dlg = m_totWarningDialog_;
            m_totWarningDialog_ = nullptr;
            dlg->Destroy();
        }
        m_totCurrentDurationMs = 0;
        stopTotBeep_();

        // If PTT input is enabled, suspend further changes until after EOO is sent.
        if (wxGetApp().m_pttInSerialPort)
        {
            wxGetApp().m_pttInSerialPort->suspendChanges(true);
        }
        
        // Sleep for long enough that we get the remaining [blocksize] ms of audio.
        int msSleep = (1000 * freedvInterface.getTxNumSpeechSamples()) / freedvInterface.getTxSpeechSampleRate();
        log_debug("Sleeping for %d ms prior to ending TX", msSleep);

        auto before = highResClock.now();

        while(true)
        {
            auto diff = highResClock.now() - before;
            if (diff >= std::chrono::milliseconds(msSleep))
            {
                break;
            }

            wxThread::Sleep(1);
            wxGetApp().Yield(true);
        }
        
        // Trigger end of TX processing. This causes us to wait for the remaining samples
        // to flow through the system before toggling PTT.  Note that there is a 1000ms 
        // timeout as backup.
        if (freedvInterface.getCurrentMode() == FREEDV_MODE_RADE)
        {
            log_info("Waiting for EOO to be queued");
            endingTx.store(true, std::memory_order_release);
            
            auto beginTime = std::chrono::high_resolution_clock::now();
            while(true)
            {
                if (g_eoo_enqueued.load(std::memory_order_acquire))
                {
                    log_info("Detected that EOO has been enqueued");
                    break;
                }
 
                wxThread::Sleep(1);
                wxGetApp().Yield(true);

                auto endTime = std::chrono::high_resolution_clock::now();
                if ((endTime - beginTime) >= std::chrono::seconds(2))
                {
                    log_warn("Timed out waiting for EOO to be enqueued");
                    break;
                }
            }
        }

        int sample = g_outfifo1_empty.load(std::memory_order_relaxed);
        before = highResClock.now();
        while(true)
        {
            auto diff = highResClock.now() - before;
            auto tmp = g_outfifo1_empty.load(std::memory_order_relaxed);
            if (diff >= std::chrono::milliseconds(1000) || (tmp != sample))
            {
                log_info("All TX finished (diff = %d ms, fifo_empty = %d, sample = %d), going out of PTT", (int)std::chrono::duration_cast<std::chrono::milliseconds>(diff).count(), tmp, sample);
                break;
            }

            wxThread::Sleep(1);

            // Yield() used to avoid lack of UI responsiveness during delay.
            wxGetApp().Yield(true);
        }
        
        // Wait for a minimum amount of time before stopping TX to ensure that
        // remaining audio gets piped to the radio from the operating system.
        int latency = (int)GetPlausibleLatencyUs_(txOutSoundDevice, "TX output");

        log_info(
            "Pausing for a minimum of %d us before TX->RX to allow remaining audio to go out", 
            latency);
        before = highResClock.now();
        while(true)
        {
            auto diff = highResClock.now() - before;
            if (diff >= std::chrono::microseconds(latency))
            {
                break;
            }

            wxThread::Sleep(1);

            // Yield() used to avoid lack of UI responsiveness during delay.
            wxGetApp().Yield(true);
        }
                
        // Wait an additional configured timeframe before actually clearing PTT (below)
        if (wxGetApp().appConfiguration.txRxDelayMilliseconds > 0)
        {
            // Delay outbound TX audio if going into TX.
            // Yield() used to avoid lack of UI responsiveness during delay.
            before = highResClock.now();
            while(true)
            {
                auto diff = highResClock.now() - before;
                if (diff >= std::chrono::milliseconds(wxGetApp().appConfiguration.txRxDelayMilliseconds.get()))
                {
                    break;
                }

                wxThread::Sleep(1);
                wxGetApp().Yield(true);
            }
        }
        // The radio keeps transmitting until it has processed the PTT-off
        // request below, so keep RX muted until it confirms (see
        // onRigPttChange_()). If it never does, give up after a timeout.
        if (wxGetApp().rigPttController != nullptr && wxGetApp().rigPttController->isConnected())
        {
            g_rxInputLatencyUs.store(GetPlausibleLatencyUs_(rxInSoundDevice, "RX input"), std::memory_order_release);
            g_rxMutedUntilNs.store(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    (std::chrono::steady_clock::now() + RX_MUTE_MAX_WAIT_FOR_RADIO).time_since_epoch()).count(),
                std::memory_order_release);
        }
        
        g_tx.store(false, std::memory_order_release);
        endingTx.store(false, std::memory_order_release);
        
        if (wxGetApp().m_pttInSerialPort)
        {
            wxGetApp().m_pttInSerialPort->suspendChanges(false);
        }
        
        // tx-> rx transition, swap to the page we were on for last rx
        m_auiNbookCtrl->ChangeSelection(wxGetApp().appConfiguration.currentNotebookTab);
        for (size_t index = 0; index < m_auiNbookCtrl->GetPageCount(); index++)
        {
            auto page = m_auiNbookCtrl->GetPage(index);
            page->Refresh();
        }

        // enable sync text

        m_textSync->Enable();
        m_textCurrentDecodeMode->Enable();
        
        // Re-enable buttons.
        m_togBtnOnOff->Enable(true);
        m_togBtnAnalog->Enable(true);
        m_togBtnVoiceKeyer->Enable(true);
    }
    else
    {
        // Force PTT button colors ASAP to avoid latency after mouse up.
        m_btnTogPTT->SetBackgroundColour(*wxRED);
#if !defined(__APPLE__)
        // macOS limitations prevent the foreground color of toggle buttons from being 
        // reliably set, so don't mess with it in the first place.
        m_btnTogPTT->SetForegroundColour(*wxBLACK);
#endif // !defined(__APPLE__)
        wxGetApp().Yield(true);

        // If PTT input is enabled, suspend further changes until we actually start TX.
        if (wxGetApp().m_pttInSerialPort)
        {
            wxGetApp().m_pttInSerialPort->suspendChanges(true);
        }
        
        // rx-> tx transition, swap to Mic In page to monitor speech

        // Save currently visible plot so we can go back to it on RX.
        wxGetApp().appConfiguration.currentNotebookTab = captureCurrentMicGroupTab_();

        // Note: GetPageIndex sometimes returns the incorrect results, so iterating and finding
        // the current page ourselves is a better bet.
        size_t index = 0;
        for (; index < m_auiNbookCtrl->GetPageCount(); index++)
        {
            auto page = m_auiNbookCtrl->GetPage(index);
            if (page != nullptr && page == (wxWindow *)m_panelSpeechIn)
            {
                m_auiNbookCtrl->ChangeSelection(index);
                page->Refresh();
                break;
            }
        }

        // disable sync text

        m_textSync->Disable();
        m_textCurrentDecodeMode->Disable();

        // Disable On/Off button.
        m_togBtnOnOff->Enable(false);
    }

    // Use wasInTx to determine direction: don't let a stray click during the drain
    // flip newTx and leave the radio keyed with the pipeline in the wrong state.
    auto newTx = !wasInTx;
    if (wxGetApp().rigPttController != nullptr && wxGetApp().rigPttController->isConnected())
    {
        wxGetApp().rigPttController->ptt(newTx);
    }

    // reset level gauge

    m_maxLevel = 0;
    m_gaugeLevel->SetValue(0);
    
    // Report TX change to registered reporters
    for (auto& obj : wxGetApp().m_reporters)
    {
        obj->transmit(freedvInterface.getCurrentTxModeStr(), newTx);
    }
    
    // If we're recording, switch to/from modulator and radio.
    if (g_sfRecFile.load(std::memory_order_acquire) != nullptr)
    {
        if (!newTx)
        {
            g_recFileFromModulator = false;
            g_recFileFromRadio.store(true, std::memory_order_release);
        }
        else
        {
            g_recFileFromRadio.store(false, std::memory_order_release);
            g_recFileFromModulator = true;
        }
    }

    if (newTx)
    {
        endingTx.store(false, std::memory_order_release);
            
        if (wxGetApp().appConfiguration.txRxDelayMilliseconds > 0)
        {
            // Delay outbound TX audio if going into TX.
            // Note: Yield() used here to avoid lack of UI responsiveness during delay.
            auto before = highResClock.now();
            while(true)
            {
                auto diff = highResClock.now() - before;
                if (diff >= std::chrono::milliseconds(wxGetApp().appConfiguration.txRxDelayMilliseconds.get()))
                {
                    break;
                }

                wxThread::Sleep(1);
                wxGetApp().Yield(true);
            }
        }

        // g_tx governs when audio actually goes out during TX, so don't set to true until
        // after the delay occurs.
        g_tx.store(true, std::memory_order_release);

        // Start Time-Out Timer if enabled.
        if (wxGetApp().appConfiguration.rigControlConfiguration.totTimerEnabled &&
            wxGetApp().appConfiguration.rigControlConfiguration.totTimerSecs > 0)
        {
            int totMs = wxGetApp().appConfiguration.rigControlConfiguration.totTimerSecs * 1000;
            log_info("Starting Time-Out Timer (%d seconds)", wxGetApp().appConfiguration.rigControlConfiguration.totTimerSecs.get());
            m_totTimer.Start(totMs, wxTIMER_ONE_SHOT);

            m_totTxStartTime = std::chrono::high_resolution_clock::now();
            m_totCurrentDurationMs = totMs;
            m_totWarningTimer.Start(500, wxTIMER_CONTINUOUS);
        }

        if (wxGetApp().m_pttInSerialPort)
        {
            wxGetApp().m_pttInSerialPort->suspendChanges(false);
        }
    }
    
    // wxWidgets should already be doing the below logic,
    // but for some reason this intermittently doesn't happen
    // on Windows. Just to be sure, we force the correct state
    // here (similar to what's already done for ending TX while
    // using the voice keyer).
    m_btnTogPTT->SetValue(newTx);
    m_btnTogPTT->SetLabel(_("&XMIT"));
    m_btnTogPTT->SetBackgroundColour(m_btnTogPTT->GetValue() ? *wxRED : wxNullColour);
#if !defined(__APPLE__)
    // macOS limitations prevent the foreground color of toggle buttons from being 
    // reliably set, so don't mess with it in the first place.
    m_btnTogPTT->SetForegroundColour(m_btnTogPTT->GetValue() ? *wxBLACK : wxNullColour);
#endif // !defined(__APPLE__)
    
    // The Report Frequency drop-down should not be modifiable during TX.
    // Additionally, tuning during normal TX is verboten.
    m_cboReportFrequency->Enable(!newTx);
    m_btnTogTune->Enable(!newTx);

    if (newTx)
    {
        micSpeakerBox->SetLabel("Mic &Level");

        m_sliderMicSpkrLevel->SetValue(wxGetApp().appConfiguration.filterConfiguration.micInChannel.volInDB * 10);
        wxString fmtString = wxString::Format(MIC_SPKR_LEVEL_FORMAT_STR, wxNumberFormatter::ToString((double)wxGetApp().appConfiguration.filterConfiguration.micInChannel.volInDB, 1), DECIBEL_STR);
        m_txtMicSpkrLevelNum->SetLabel(fmtString);
    }
    else
    {
        micSpeakerBox->SetLabel("Speaker &Level");

        m_sliderMicSpkrLevel->SetValue(wxGetApp().appConfiguration.filterConfiguration.spkOutChannel.volInDB * 10);
        wxString fmtString = wxString::Format(MIC_SPKR_LEVEL_FORMAT_STR, wxNumberFormatter::ToString((double)wxGetApp().appConfiguration.filterConfiguration.spkOutChannel.volInDB, 1), DECIBEL_STR);
        m_txtMicSpkrLevelNum->SetLabel(fmtString);
    }

    CallAfter([&]() {
        txChangeoverOccurring_ = false;
        m_sliderMicSpkrLevel->Refresh(); // Redraw doesn't happen immediately otherwise in some environments
    });

    if (newTx && m_momentaryKeyReleasedDuringChangeover_)
    {
        // The momentary PTT key was released while this start was still in
        // progress (see the wxEVT_KEY_UP handler in FilterEvent). Queued
        // after the CallAfter() above so it runs once txChangeoverOccurring_
        // has cleared, rather than no-opping against it.
        m_momentaryKeyReleasedDuringChangeover_ = false;
        log_info("Momentary PTT key was released while TX was starting -- stopping now");
        CallAfter([this]() {
            m_btnTogPTT->SetValue(false);
            m_btnTogPTT->SetBackgroundColour(wxNullColour);
            togglePTT();
        });
    }
}

void MainFrame::OnTogBtnTune(wxCommandEvent&)
{
    // Ensure background is correct for button.
    bool newTx = m_btnTogTune->GetValue();
    m_btnTogTune->SetBackgroundColour(newTx ? *wxRED : wxNullColour);

    // Update PTT state
    if (wxGetApp().rigPttController != nullptr && wxGetApp().rigPttController->isConnected()) 
    {
        wxGetApp().rigPttController->ptt(newTx);
    }

    // Disable actual TX controls if needed
    m_togBtnVoiceKeyer->Enable(!newTx);
    m_btnTogPTT->Enable(!newTx);
    m_cboReportFrequency->Enable(!newTx);

    // Enable tuning carrier
    g_rxUserdata->tuneSineWaveSampleNumber.store(0, std::memory_order_release);
    g_rxUserdata->isTuning.store(newTx, std::memory_order_release);

    wxString fmtString;
    if (newTx)
    {
        fmtString = wxString::Format(MIC_SPKR_LEVEL_FORMAT_STR, wxNumberFormatter::ToString((double)g_tuneLevel/10.0, 1), DECIBEL_STR);
        m_txLevelBox->SetLabel("Tune &Attenuation");
    }
    else
    {
        fmtString = wxString::Format(MIC_SPKR_LEVEL_FORMAT_STR, wxNumberFormatter::ToString((double)g_txLevel/10.0, 1), DECIBEL_STR);
        m_txLevelBox->SetLabel("TX &Attenuation");
    }

    m_txtTxLevelNum->SetLabel(fmtString);

    // Make sure focus on Tune button is actually cleared once tune state is switched.
    // Seems to be a common problem on some Linux systems for some reason.
    m_auiNbookCtrl->SetFocus();
}

HamlibRigController::Mode MainFrame::getCurrentMode_()
{
    bool useAnalog = 
        wxGetApp().appConfiguration.rigControlConfiguration.hamlibUseAnalogModes || g_analog.load(std::memory_order_relaxed);
    return GetModeForFrequency(wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency, useAnalog);
}

//-------------------------------------------------------------------------
// OnTogBtnAnalogClick()
//-------------------------------------------------------------------------
void MainFrame::OnTogBtnAnalogClick (wxCommandEvent& event)
{
    if (g_analog.load(std::memory_order_relaxed) == 0) {
        g_analog.store(1, std::memory_order_relaxed);
        m_panelSpectrum->setFreqScale(MODEM_STATS_NSPEC*((float)MAX_F_HZ/(FS/2)));
        m_panelWaterfall->setFs(FS);
        
        m_togBtnAnalog->SetLabel(wxT("Switch to Di&gital"));
    }
    else {
        g_analog.store(0, std::memory_order_relaxed);
        m_panelSpectrum->setFreqScale(MODEM_STATS_NSPEC*((float)MAX_F_HZ/(freedvInterface.getRxModemSampleRate()/2)));
        m_panelWaterfall->setFs(freedvInterface.getRxModemSampleRate());
        
        m_togBtnAnalog->SetLabel(wxT("Switch to A&nalog"));
    }

    // Report analog change to registered reporters
    for (auto& obj : wxGetApp().m_reporters)
    {
        if (obj != wxGetApp().m_sharedReporterObject || !m_reporterHidden->GetValue())
        {
            obj->inAnalogMode(g_analog.load(std::memory_order_relaxed));
        }
    }
    
    if (wxGetApp().rigFrequencyController != nullptr && 
        wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency > 0 &&
        wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges)
    {
        // Request mode change on the radio side
        wxGetApp().rigFrequencyController->setMode(getCurrentMode_());
    }

    g_State.store(0, std::memory_order_release);
    g_prev_State.store(0, std::memory_order_release);;
    freedvInterface.getCurrentRxModemStats()->snr_est = 0;

    event.Skip();
}

void MainFrame::OnCallSignReset(wxCommandEvent&)
{
    m_pcallsign = m_callsign;
    memset(m_callsign, 0, MAX_CALLSIGN);
    wxString s;
    s.Printf("%s", m_callsign);
    m_txtCtrlCallSign->SetValue(s);
    
    m_lastReportedCallsignListView->DeleteAllItems();
    m_cboLastReportedCallsigns->SetText(_(""));
}

void MainFrame::OnLogQSO(wxCommandEvent&)
{
    wxString dxCall;
    wxString dxGrid;
    wxString dxFreq;
    wxString logTime;
    wxString snrString;
    wxDateTime logTimeObj = wxDateTime::Now();
    double dxFreqDouble = 0;
    uint64_t dxFreqHz = 0;
    double snr = ILogger::UNKNOWN_SNR;
    
    auto selected = m_lastReportedCallsignListView->GetFirstSelected();
    if (wxGetApp().lastSelectedLoggingRow == MainApp::MAIN_WINDOW && selected != -1)
    {        
        // Get callsign and RX frequency
        dxCall = m_lastReportedCallsignListView->GetItemText(selected, 0);
        dxFreq = m_lastReportedCallsignListView->GetItemText(selected, 1);
        logTime = m_lastReportedCallsignListView->GetItemText(selected, 2);
        snrString = m_lastReportedCallsignListView->GetItemText(selected, 3);
        
        wxNumberFormatter::FromString(dxFreq, &dxFreqDouble);
        wxNumberFormatter::FromString(snrString, &snr);
        
        wxString::const_iterator end;
        logTimeObj.ParseDateTime(logTime, &end);

        if (wxGetApp().appConfiguration.reportingConfiguration.useUTCForReporting)
        {
            // String was stored in UTC; ParseDateTime assumes local — reinterpret as UTC.
            logTimeObj.MakeFromTimezone(wxDateTime::UTC);
        }
        
        if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
        {
            dxFreqDouble *= 1000;
        }
        else
        {
            dxFreqDouble *= 1000000;
        }
        
        // If connected to FreeDV Reporter, get DX grid
        if (m_reporterDialog != nullptr && dxFreq != "")
        {
            dxGrid = m_reporterDialog->getGridSquareForCallsign(dxCall);
        }
        
        dxFreqHz = (uint64_t)dxFreqDouble;
        
        log_info("Logging %s/%s at %" PRIu64 " Hz from main window drop-down list", (const char*)dxCall.ToUTF8(), (const char*)dxGrid.ToUTF8(), dxFreqHz);
    }
    else if (
        m_reporterDialog != nullptr && 
        wxGetApp().lastSelectedLoggingRow == MainApp::FREEDV_REPORTER &&
        m_reporterDialog->getSelectedCallsignInfo(dxCall, dxGrid, dxFreqHz))
    {
        log_info("Logging %s/%s at %" PRIu64 " Hz from FreeDV Reporter", (const char*)dxCall.ToUTF8(), (const char*)dxGrid.ToUTF8(), dxFreqHz);
    }
    else
    {
        dxFreq = m_cboReportFrequency->GetValue();
        wxNumberFormatter::FromString(dxFreq, &dxFreqDouble);
        
        if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
        {
            dxFreqDouble *= 1000;
        }
        else
        {
            dxFreqDouble *= 1000000;
        }
        
        dxFreqHz = (uint64_t)dxFreqDouble;
        
        log_info("No rows selected, defaulting logging to %" PRIu64 " Hz", dxFreqHz);
    }

    // Show log contact dialog 
    auto logDialog = new LogEntryDialog(this);
    logDialog->ShowDialog(dxCall.ToUTF8(), dxGrid.ToUTF8(), logTimeObj, (int64_t)dxFreqHz, (int)snr);
    logDialog->Destroy();
}

// Force manual resync, just in case demod gets stuck on false sync

void MainFrame::OnReSync(wxCommandEvent&)
{
    if (m_RxRunning.load(std::memory_order_acquire))  {
        log_debug("OnReSync");
        
        // Resync must be triggered from the TX/RX thread, so pushing the button queues it until
        // the next execution of the TX/RX loop.
        g_queueResync.store(true, std::memory_order_release);
    }
}

// Deselects item on right-click
void MainFrame::OnRightClickCallsignList(wxMouseEvent&)
{
    auto index = m_lastReportedCallsignListView->GetFirstSelected();
    while (index != -1)
    {
        wxGetApp().lastSelectedLoggingRow = MainApp::UNSELECTED;
        m_lastReportedCallsignListView->Select(index, false);
        index = m_lastReportedCallsignListView->GetFirstSelected();
    }
    // Deselect only -- leave the displayed callsign text alone, consistent
    // with how the FreeDV Reporter list's right-click-to-deselect behaves.
    // See OnCloseCallsignList() for why this is deferred and followed by a
    // forced repaint.
    CallAfter([this]() {
        m_BtnCallSignReset->SetFocus();
        m_cboLastReportedCallsigns->Refresh();
        m_cboLastReportedCallsigns->Update();
    });
}

void MainFrame::OnOpenCallsignList( wxCommandEvent& event )
{
    wxGetApp().lastSelectedLoggingRow = MainApp::MAIN_WINDOW;
    event.Skip();
}

void MainFrame::OnCloseCallsignList( wxCommandEvent& event )
{
    auto index = m_lastReportedCallsignListView->GetFirstSelected();
    if (index == -1)
    {
        // Make sure we're not selected if no callsigns selected.
        wxGetApp().lastSelectedLoggingRow = MainApp::UNSELECTED;

        // Deferred via CallAfter so it runs once the popup's own dismissal
        // processing has fully finished.
        CallAfter([this]() {
            m_BtnCallSignReset->SetFocus();

            // The popup window overlaps the combo's own value area while
            // open; on dismiss GTK doesn't always damage/repaint that
            // region, so the combo can be left showing its stale
            // "focused/selected" look from before the popup covered it.
            // Force a fresh, synchronous repaint.
            m_cboLastReportedCallsigns->Refresh();
            m_cboLastReportedCallsigns->Update();
        });
    }
    event.Skip();
}

void MainFrame::resetStats_()
{
    if (m_RxRunning.load(std::memory_order_acquire))  {
        freedvInterface.resetBitStats();
        g_resyncs.store(0, std::memory_order_relaxed);
        int i;
        for(i=0; i<2*g_Nc; i++) {
            g_error_hist[i] = 0;
            g_error_histn[i] = 0;
        }
        // resets variance stats every time it is called
        freedvInterface.setEq(wxGetApp().appConfiguration.filterConfiguration.enable700CEqualizer);
    }
}

void MainFrame::OnBerReset(wxCommandEvent&)
{
    resetStats_();
}

void MainFrame::OnChangeReportFrequencyVerify( wxCommandEvent& event )
{
    if (wxGetApp().rigFrequencyController != nullptr && suppressFreqModeUpdates_)
    {
        return;
    }
    
    OnChangeReportFrequency(event);
}

void MainFrame::OnChangeReportFrequency( wxCommandEvent& )
{    
    wxString freqStr = m_cboReportFrequency->GetValue();
    auto oldFreq = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency;

    wxString oldFreqString;            
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
    {
        oldFreqString = wxNumberFormatter::ToString(oldFreq / 1000.0, 1);
    }
    else
    {
        oldFreqString = wxNumberFormatter::ToString(oldFreq / 1000.0 / 1000.0, 4);
    }

    if (freqStr.Length() > 0)
    {
        double tmp = 0;
        wxNumberFormatter::FromString(freqStr, &tmp);
        
        if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
        {
            wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency = round(tmp * 1000);
        }
        else
        {
            wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency = round(tmp * 1000 * 1000);
        }

        if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency > 0)
        {
            m_cboReportFrequency->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));

            // Round to the nearest 100 Hz.
            uint64_t wholeFreq = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency / 100;
            uint64_t remainder = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency % 100;

            if (remainder >= 50)
            {
                wholeFreq++;
            }

            wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency = wholeFreq * 100;
        }
        else
        {
            m_cboReportFrequency->SetForegroundColour(wxColor(*wxRED));
        }
    }
    else
    {
        wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency = 0;
        m_cboReportFrequency->SetForegroundColour(wxColor(*wxRED));
    }

    if (freqStr != oldFreqString)
    {
        log_info("Request frequency change to %" PRIu64 " Hz", wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency.get());

        // Report current frequency to reporters
        for (auto& ptr : wxGetApp().m_reporters)
        {
            ptr->freqChange(wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency);
        }
        
        if (wxGetApp().rigFrequencyController != nullptr && 
            wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency > 0 && 
            (wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges || wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqChangesOnly))
        {
            // Request frequency/mode change on the radio side
            wxGetApp().rigFrequencyController->setFrequency(wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency);
            if (wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges)
            {
                wxGetApp().rigFrequencyController->setMode(getCurrentMode_());
            }
        }
    }

    if (m_reporterDialog != nullptr)
    {
        m_reporterDialog->refreshQSYButtonState();
    }

    // Auto-save outgoing band levels, then load the new band's levels
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency > 0)
    {
        auto newBandEnum = FreeDVReporterDialog::getFilterForFrequency_(
            wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency);
        if (newBandEnum != BAND_OTHER && newBandEnum != lastBand_)
        {
            autoSaveCurrentBandLevels_();
            lastBand_ = newBandEnum;
            loadTxAttenForBand_(newBandEnum);
            loadTuneAttenForBand_(newBandEnum);
            applyTxLevel(); // apply both loaded values in one call
        }
    }
}

void MainFrame::OnReportFrequencySetFocus(wxFocusEvent& event)
{
    suppressFreqModeUpdates_ = true;
    TopFrame::OnReportFrequencySetFocus(event);
}

void MainFrame::OnReportFrequencyKillFocus(wxFocusEvent& event)
{
    suppressFreqModeUpdates_ = false;
    
    // Handle any frequency changes as appropriate.
    wxCommandEvent tmpEvent;
    OnChangeReportFrequency(tmpEvent);

    TopFrame::OnReportFrequencyKillFocus(event);
}

void MainFrame::OnSystemColorChanged(wxSysColourChangedEvent& event)
{
    // Works around issues on wxWidgets with certain controls not changing colors
    // when the user switches between light and dark mode.
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency > 0)
    {
        m_cboReportFrequency->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    }
    else
    {
        m_cboReportFrequency->SetForegroundColour(*wxRED);
    }
    TopFrame::OnSystemColorChanged(event);
}

void MainFrame::OnCenterRx(wxCommandEvent&)
{
    clickTune(FDMDV_FCENTRE);
}

void MainFrame::updateReportingFreqList_()
{
    uint64_t prevFreqInt = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency;

    double freq =  ((double)prevFreqInt)/1000.0;
    if (!wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
    {
        freq /= 1000.0;
    }

    wxString prevSelected;    
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
    {
        prevSelected = wxNumberFormatter::ToString(freq, 1);
    }
    else
    {
        prevSelected = wxNumberFormatter::ToString(freq, 4);
    }
    m_cboReportFrequency->Clear();
    
    for (auto& item : wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyList.get())
    {
        m_cboReportFrequency->Append(item);
    }
    
    auto idx = m_cboReportFrequency->FindString(prevSelected);
    if (idx >= 0)
    {
        m_cboReportFrequency->SetSelection(idx);
    }
    m_cboReportFrequency->SetValue(prevSelected);
    
    // Update associated label if the units have changed
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
    {
        m_freqBox->SetLabel(_("Radio Freq. (kHz)"));
    }
    else
    {
        m_freqBox->SetLabel(_("Radio Freq. (MHz)"));
    }
}

void MainFrame::OnResetMicSpkrLevel(wxMouseEvent&)
{
    auto sliderLevel = 0;
    if (g_tx.load(std::memory_order_acquire))
    {
        wxGetApp().appConfiguration.filterConfiguration.micInChannel.volInDB = sliderLevel;
        m_newMicInFilter.store(true, std::memory_order_release);
    }
    else
    {
        wxGetApp().appConfiguration.filterConfiguration.spkOutChannel.volInDB = sliderLevel;
        m_newSpkOutFilter.store(true, std::memory_order_release);
    }
    
    wxString fmtString = wxString::Format(MIC_SPKR_LEVEL_FORMAT_STR, wxNumberFormatter::ToString((double)sliderLevel, 1), DECIBEL_STR);
    m_txtMicSpkrLevelNum->SetLabel(fmtString);
    m_sliderMicSpkrLevel->SetValue(sliderLevel);
}

void MainFrame::OnToggleReporterVisibility (wxCommandEvent&)
{
    if (m_RxRunning.load(std::memory_order_acquire) && !g_analog.load(std::memory_order_relaxed) && wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnabled)
    {
        if (m_reporterHidden->GetValue())
        {
            wxGetApp().m_sharedReporterObject->hideFromView();
        }
        else
        {
            wxGetApp().m_sharedReporterObject->showOurselves();
        }
    }

    if (m_reporterHidden->GetValue())
    {
        m_reporterHidden->SetLabel("Turn On");
    }
    else
    {
        m_reporterHidden->SetLabel("Turn Off");
    }
    
    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterForcedOff = m_reporterHidden->GetValue();
}

void MainFrame::OnToolsExportConfigUI(wxUpdateUIEvent& event)
{
    event.Enable(!m_RxRunning.load(std::memory_order_acquire));
}

void MainFrame::OnToolsImportConfigUI(wxUpdateUIEvent& event)
{
    event.Enable(!m_RxRunning.load(std::memory_order_acquire));
}

void MainFrame::OnToolsExportConfig(wxCommandEvent& event)
{
    wxUnusedVar(event);

    wxFileDialog saveFileDialog(
        this,
        _("Export FreeDV Configuration"),
        wxGetApp().defaultConfigFilePath,
        wxEmptyString,
        wxT("FreeDV configuration files (*.conf)|*.conf|All files (*.*)|*.*"),
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT
    );

    if (saveFileDialog.ShowModal() == wxID_CANCEL)
        return;

    wxString path = saveFileDialog.GetPath();
    wxFileConfig* exportConfig = new wxFileConfig(wxT("FreeDV"), wxT("CODEC2-Project"), path, path, wxCONFIG_USE_LOCAL_FILE);
    exportConfiguration_(exportConfig);
    exportConfig->Flush();
    delete exportConfig;
}

void MainFrame::OnToolsImportConfig(wxCommandEvent& event)
{
    wxUnusedVar(event);

    wxFileDialog openFileDialog(
        this,
        _("Import FreeDV Configuration"),
        wxGetApp().defaultConfigFilePath,
        wxEmptyString,
        wxT("FreeDV configuration files (*.conf)|*.conf|All files (*.*)|*.*"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST
    );

    if (openFileDialog.ShowModal() == wxID_CANCEL)
        return;

    wxString path = openFileDialog.GetPath();

    if (!wxFileExists(path))
    {
        wxMessageBox(_("The selected file does not exist."), _("Import Error"), wxOK | wxICON_ERROR, this);
        return;
    }

    wxFileConfig* importConfig = new wxFileConfig(wxT("FreeDV"), wxT("CODEC2-Project"), path, path, wxCONFIG_USE_LOCAL_FILE);

    if (importConfig->GetNumberOfGroups() == 0 && importConfig->GetNumberOfEntries() == 0)
    {
        delete importConfig;
        wxMessageBox(_("The selected file could not be parsed as a FreeDV configuration."), _("Import Error"), wxOK | wxICON_ERROR, this);
        return;
    }

    // On Linux/macOS, this replaces $HOME with "~" to shorten the title a bit.
    wxFileName fn(path);
    wxGetApp().customConfigFileName = fn.GetFullName();

    SetTitle(wxString::Format("%s (%s)", _("FreeDV ") + wxString::FromUTF8(GetFreeDVVersion().c_str()), wxGetApp().customConfigFileName));
#if defined(UNOFFICIAL_RELEASE)
    wxDateTime buildDate(wxInvalidDateTime);
    wxString::const_iterator iter;
    buildDate.ParseDate(FREEDV_BUILD_DATE, &iter);
    auto expireDate = buildDate + EXPIRES_AFTER_TIMEFRAME;
    SetTitle(GetTitle() + wxString::Format(" [Expires %s]", expireDate.FormatDate()));
#endif // defined(UNOFFICIAL_RELEASE)
    setConfiguration_(importConfig);

    // Remember this file so it is automatically restored on the next startup.
    saveLastUsedConfigPath(path);
}

void MainFrame::OnToolsLoadDefaultConfigUI(wxUpdateUIEvent& event)
{
    event.Enable(!m_RxRunning.load(std::memory_order_acquire));
}

void MainFrame::OnToolsLoadDefaultConfig(wxCommandEvent& event)
{
    wxUnusedVar(event);

    wxMessageDialog messageDialog(
        this, _("This will load the default FreeDV configuration. Are you sure?"),
        _("Load Default Configuration"),
        wxYES_NO | wxICON_QUESTION | wxCENTRE);

    if (messageDialog.ShowModal() != wxID_YES)
        return;

    // The default configuration is platform-appropriate: on Windows it's in
    // the registry (wxRegConfig); on macOS/Linux, a file in the default
    // location (wxFileConfig). It becomes the active pConfig going forward.
    //
    // The default configuration is the one that was opened at startup, if
    // it was (see MainApp::OnCmdLineParsed()). Finding it again from here
    // isn't reliable: wxConfigBase::Create() just hands back whichever
    // configuration is in use and, with that out of the way, looks under the
    // application's name as it is now rather than as it was at startup.
    wxConfigBase* defaultConfig = wxGetApp().defaultConfig;
#if wxCHECK_VERSION(3,3,0) && defined(__linux__)
    if (defaultConfig == nullptr && !wxGetApp().defaultConfigFile.IsEmpty())
    {
        // Startup would have had to say where the default configuration is,
        // so the same goes here. That place was worked out then, as
        // wxWidgets gives a different answer once it's been set up to use it.
        wxString defaultLocation = wxGetApp().defaultConfigFile;
        defaultConfig = new wxFileConfig(wxT("FreeDV"), wxT("CODEC2-Project"), defaultLocation, defaultLocation, wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_SUBDIR | wxCONFIG_USE_XDG);
    }
#endif // wxCHECK_VERSION(3,3,0) && defined(__linux__)
    if (defaultConfig == nullptr)
    {
        wxConfigBase::Set(nullptr);
        defaultConfig = wxConfigBase::Create();
    }
    wxGetApp().defaultConfig = defaultConfig;
    
    setConfiguration_(defaultConfig);

    // Remove the last-used config path so startup reverts to the default next time.
    clearLastUsedConfigPath();

    // Clear any custom config file indicator from the title bar.
    wxGetApp().customConfigFileName = wxEmptyString;
    SetTitle(_("FreeDV ") + wxString::FromUTF8(GetFreeDVVersion().c_str()));
#if defined(UNOFFICIAL_RELEASE)
    wxDateTime buildDate(wxInvalidDateTime);
    wxString::const_iterator iter;
    buildDate.ParseDate(FREEDV_BUILD_DATE, &iter);
    auto expireDate = buildDate + EXPIRES_AFTER_TIMEFRAME;
    SetTitle(GetTitle() + wxString::Format(" [Expires %s]", expireDate.FormatDate()));
#endif // defined(UNOFFICIAL_RELEASE)
}
