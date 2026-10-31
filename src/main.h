//==========================================================================
// Name:            main.h
//
// Purpose:         Declares simple wxWidgets application with GUI.
// Created:         Apr. 9, 2012
// Authors:         David Rowe, David Witten
//
// License:
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General License version 2.1,
//  as published by the Free Software Foundation.  This program is
//  distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
//  License for more details.
//
//  You should have received a copy of the GNU General License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//
//==========================================================================
#ifndef __FDMDV2_MAIN__
#define __FDMDV2_MAIN__

#include "config.h"
#include <atomic>
#include <wx/wx.h>

#include <wx/tglbtn.h>
#include <wx/app.h>
#include "wx/rawbmp.h"
#include "wx/file.h"
#include "wx/filename.h"
#include "wx/config.h"
#include <wx/fileconf.h>
#include "wx/graphics.h"
#include "wx/mstream.h"
#include "wx/wfstream.h"
#include "wx/quantize.h"
#include "wx/scopedptr.h"
#include "wx/stopwatch.h"
#include "wx/versioninfo.h"
#include <wx/sound.h>
#include <wx/url.h>
#include <wx/sstream.h>
#include <wx/listbox.h>
#include <wx/textdlg.h>
#include <wx/regex.h>
#include <wx/socket.h>
#include <wx/numformatter.h>

#include <stdint.h>
#include <future>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#include <cpuid.h>
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <sys/ioctl.h>
#include <dlfcn.h>
#endif

#ifdef _MSC_VER
// used for AVX checking
#include <intrin.h>
#endif

#include "pipeline/modem_stats.h"

#include "topFrame.h"
#include "gui/dialogs/filter_frequency.h"
#include "gui/dialogs/tot_warning.h"
#include "gui/controls/plot.h"
#include "gui/controls/plot_scalar.h"
#include "gui/controls/plot_waterfall.h"
#include "comp.h"
#include "gui/controls/plot_spectrum.h"
#include "sndfile.h"
#include "sox_biquad.h"
#include "comp_prim.h"
#include "rig_control/HamlibRigController.h"
#include "rig_control/SerialPortOutRigController.h"
#include "rig_control/SerialPortInRigController.h"
#include "reporting/IReporter.h"
#include "reporting/FreeDVReporter.h"
#include "freedv_interface.h"
#include "audio/AudioEngineFactory.h"
#include "audio/IAudioDevice.h"
#include "config/FreeDVConfiguration.h"
#include "logging/ILogger.h"
#include "pipeline/paCallbackData.h"
#include "pipeline/LinkStep.h"
#include "freedv_sanitizers.h"
#include "gui/util/wxMessageBoxWrapper.h"
#include "gui/util/PopupMenuWrapper.h"
#include "gui/util/PathDialogWrapper.h"
#include "gui/util/BrowserWrapper.h"
#include "voicekeyer_cache.h"

#define _USE_TIMER              1
#define _USE_ONIDLE             1
#define _DUMMY_DATA             1
//#define _AUDIO_PASSTHROUGH    1
#define _REFRESH_TIMER_PERIOD   (DT*1000)

#if defined(UNOFFICIAL_RELEASE)
#define EXPIRES_AFTER_TIMEFRAME (wxDateSpan(0, 6, 0)) /* 6 months */
#endif // defined(UNOFFICIAL_RELEASE)

//#define _USE_ABOUT_DIALOG       1

enum {
        ID_START = wxID_HIGHEST,
        ID_TIMER_WATERFALL,
        ID_TIMER_SPECTRUM,
        ID_TIMER_SPEECH_IN,
        ID_TIMER_LEVEL_METER_TX,
        ID_TIMER_SPEECH_OUT,
        ID_TIMER_DEMOD_IN,
        ID_TIMER_SNR,
        ID_TIMER_UPDATE_OTHER,
        ID_TIMER_PSKREPORTER,
        ID_TIMER_UPD_FREQ,
        ID_TIMER_TOT,           // Time-Out Timer
        ID_TIMER_TOT_WARNING,   // Polls remaining TOT time to show warning
        ID_TIMER_PTT_KEY_POLL,  // Polls physical PTT key state after a forced TX stop
     };

#define EXCHANGE_DATA_IN    0
#define EXCHANGE_DATA_OUT   1

extern std::atomic<int>      g_nSoundCards;

// Last-used configuration file helpers.
// The path is stored in a platform-appropriate state store
// (registry on Windows, file on macOS/Linux) that is independent
// of the main application config so it can always be read and written
// regardless of which config backend is currently active.
wxString  getLastUsedConfigPath();
void      saveLastUsedConfigPath(const wxString& path);
void      clearLastUsedConfigPath();

// Voice Keyer Constants

#define VK_SYNC_WAIT_TIME 5.0

// Voice Keyer States

#define VK_IDLE      0
#define VK_TX        1
#define VK_RX        2
#define VK_SYNC_WAIT 3

// Voice Keyer Events

#define VK_START         0
#define VK_SPACE_BAR     1
#define VK_PLAY_FINISHED 2
#define VK_DT            3
#define VK_SYNC          4

// "Detect Sync" state machine states and constants

#define DS_IDLE           0
#define DS_SYNC_WAIT      1
#define DS_UNSYNC_WAIT    2
#define DS_SYNC_WAIT_TIME 5.0

class MainFrame;
class FilterDlg;
class FreeDVReporterDialog;
class TestAgent;

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// Class MainApp
//
// @class $(Name)
// @author $(User)
// @date $(Date)
// @file $(CurrentFileName).$(CurrentFileExt)
// @brief
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
class MainApp : public wxApp
{
    public:
        // Indicates which row was last selected (for auto-filling during logging)
        enum LastSelectedRow 
        {
            UNSELECTED,
            MAIN_WINDOW,
            FREEDV_REPORTER,
        };

        virtual bool        OnInit();
        virtual void        OnInitCmdLine(wxCmdLineParser& parser);
        virtual bool        OnCmdLineParsed(wxCmdLineParser& parser);
        virtual int         OnExit();


        bool                    CanAccessSerialPort(std::string const& portName);
        
        LastSelectedRow lastSelectedLoggingRow;

        FreeDVConfiguration appConfiguration;
        wxString customConfigFileName;
        wxString defaultConfigFilePath;

        // Where the default configuration is, if FreeDV has to say (rather
        // than leaving it to wxWidgets). See MainApp::OnCmdLineParsed().
        wxString defaultConfigFile;

        // The default configuration itself, if it's been opened. It can't
        // reliably be found again later: wxWidgets looks for it by the
        // application's name, which isn't the same at startup as afterwards.
        wxConfigBase* defaultConfig = nullptr;
        
        // PTT -----------------------------------    
        int        m_intHamlibRig;
        std::shared_ptr<IRigFrequencyController> rigFrequencyController;
        std::shared_ptr<IRigPttController> rigPttController;

        // PTT Input
        std::shared_ptr<SerialPortInRigController> m_pttInSerialPort;
        
        // Logging
        std::shared_ptr<ILogger> logger;

        wxRect              m_rTopWindow;

        // To support viewing FreeDV Reporter data outside of a session, we need to have
        // a running connection and know when to appropriately kill it. A shared_ptr
        // allows us to do so.
        std::shared_ptr<FreeDVReporter> m_sharedReporterObject;
        
        std::vector<std::shared_ptr<IReporter> > m_reporters;
        
        bool                loadConfig();
        bool                saveConfig();

        // misc
        float      m_channel_snr_dB;

        int        FilterEvent(wxEvent& event);
        MainFrame *frame;

        // tone interferer simulation

        std::atomic<bool> m_tone; // read by RX thread
        std::atomic<int> m_tone_freq_hz; // read by RX thread
        std::atomic<int> m_tone_amplitude; // read by RX thread

        // debugging 700D audio break up

        bool       m_txRxThreadHighPriority;

        int        m_prevMode;
        
        std::shared_ptr<LinkStep> linkStep;

#if !wxCHECK_VERSION(3,2,0)
        wxLocale m_locale;
#endif // !wxCHECK_VERSION(3,2,0)

        int m_reportCounter;
    protected:
    private:
        // Lets a GUI test script drive FreeDV (see test_agent.h).
        std::shared_ptr<TestAgent> testAgent_;

        void UnitTest_();
};

// declare global static function wxGetApp()
DECLARE_APP(MainApp)

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// panel with custom loop checkbox for play file dialog
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
class MyExtraPlayFilePanel : public wxPanel
{
public:
    MyExtraPlayFilePanel(wxWindow *parent);
    void setLoopPlayFileToMicIn(bool checked) { m_cb->SetValue(checked); }
    bool getLoopPlayFileToMicIn(void) { return m_cb->GetValue(); }
private:
    wxCheckBox *m_cb;
};

class TxRxThread;

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// Class MainFrame
//
// @class $(Name)
// @author $(User)
// @date $(Date)
// @file $(CurrentFileName).$(CurrentFileExt)
// @brief
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
class MainFrame : public TopFrame
{
    public:
        MainFrame(wxWindow *parent);
        virtual ~MainFrame();

        FilterDlg*              m_filterDialog;

        // Updates the speaker level control to match the configuration.
        void syncSpeakerLevelWithConfiguration();

        FreeDVReporterDialog*   m_reporterDialog;
        PlotSpectrum*           m_panelSpectrum;
        PlotWaterfall*          m_panelWaterfall;
        PlotScalar*             m_panelSpeechIn;
        PlotScalar*             m_panelSpeechOut;
        PlotScalar*             m_panelDemodIn;
        PlotScalar*             m_panelSNR;
        PlotScalar*             m_panelAgcGain; // evaluation aid, see AGC_GAIN_PLOT_* in defines.h

        // Set by startRxStream()/stopRxStream(), mostly on the thread that
        // starts and stops FreeDV (see OnTogBtnOnOff()), and read all over
        // the UI thread, so atomic.
        std::atomic<bool>       m_RxRunning;
        bool                    txChangeoverOccurring_;
        
        bool                    OpenHamlibRig();
#if defined(WIN32)
        void                    OpenOmniRig();
#endif // defined(WIN32)
        void                    OpenSerialPort(void);
        void                    OpenPTTInPort(void);
        void                    ClosePTTInPort(void);

        bool                    m_modal;

#ifdef _USE_TIMER
        wxTimer                 m_plotTimer;

        // Not sure why we have the option to disable timers. TBD?
        wxTimer                 m_pskReporterTimer;
        wxTimer                 m_updFreqStatusTimer; //[UP]

        wxTimer                 m_plotWaterfallTimer;
        wxTimer                 m_plotSpectrumTimer;
        wxTimer                 m_plotScatterTimer;
        wxTimer                 m_plotSpeechInTimer;
        wxTimer                 m_levelMeterTxTimer;
        wxTimer                 m_plotSpeechOutTimer;
        wxTimer                 m_plotDemodInTimer;
        wxTimer                 m_plotSNRTimer;

        // Time-Out Timer (TOT): stops TX after configured period
        wxTimer                 m_totTimer;
        wxTimer                 m_totWarningTimer;

        // Polls the physical PTT key state (via wxGetKeyState) after a forced
        // TX stop, so a held key can't immediately restart TX -- see
        // m_pttKeyRequireRelease_ below.
        wxTimer                 m_pttKeyPollTimer;
#endif

        // TOT warning state
        std::chrono::time_point<std::chrono::high_resolution_clock> m_totTxStartTime;
        int                     m_totCurrentDurationMs{0};
        TotWarningDialog*       m_totWarningDialog_{nullptr};

        // Set when TX is force-stopped (e.g. by the TOT) while the PTT key is
        // still physically held down. Blocks the spacebar from restarting TX
        // until wxGetKeyState() confirms a genuine release -- this can't be
        // determined reliably from wxEVT_KEY_UP/DOWN alone, since holding a
        // key down can generate real (non-auto-repeat-flagged) up/down event
        // pairs at the OS key-repeat rate.
        bool                    m_pttKeyRequireRelease_{false};

        // Set when the momentary PTT key is released while a TX start is
        // still in progress (e.g. during the configured TX/RX delay in
        // togglePTT()). A stop requested during that window can't be
        // actioned immediately -- togglePTT()'s re-entrancy guard
        // (txChangeoverOccurring_) would just no-op it -- so togglePTT()
        // checks this once the start completes and immediately stops TX
        // if it's set, instead of leaving the radio keyed indefinitely.
        bool                    m_momentaryKeyReleasedDuringChangeover_{false};

        // TOT beep state
        std::chrono::time_point<std::chrono::high_resolution_clock> m_totLastBeepTime_;

    void destroy_fifos(void);

    void togglePTT(void);


    // Voice Keyer state machine

    int                     vk_state;
    void VoiceKeyerProcessEvent(int vk_event);

        void StopPlayFileToMicIn(void);
        void StopPlaybackFileFromRadio();
        void StopRecFileFromRadio();
        void StopRecFileFromDecoder();
        
        bool isReceiveOnly();
        
    protected:

        void setsnrBeta(bool snrSlow);

        // protected event handlers
        virtual void topFrame_OnSize( wxSizeEvent& event ) override;
        virtual void topFrame_OnClose( wxCloseEvent& event ) override;
        virtual void OnActivateWindow(wxActivateEvent& event) override;

        void startTxStream();
        void startRxStream();
        void stopTxStream();
        void stopRxStream();
        void abortTxStream();
        void abortRxStream();

        void OnTop(wxCommandEvent& event) override;
        void OnExit( wxCommandEvent& event ) override;

        void OnToolsFreeDVReporter( wxCommandEvent& event ) override;
        void OnToolsFreeDVReporterUI( wxUpdateUIEvent& event ) override;
        void OnToolsFilter( wxCommandEvent& event ) override;
        void OnToolsSetupWizard(wxCommandEvent& event) override;
        void OnToolsSetupWizardUI(wxUpdateUIEvent& event) override;
        void OnToolsOptions(wxCommandEvent& event) override;
        void OnToolsOptionsUI(wxUpdateUIEvent& event) override;

        void OnPlayFileFromRadio( wxCommandEvent& event ) override;
        void OnToolsExportConfig( wxCommandEvent& event ) override;
        void OnToolsExportConfigUI( wxUpdateUIEvent& event ) override;
        void OnToolsImportConfig( wxCommandEvent& event ) override;
        void OnToolsImportConfigUI( wxUpdateUIEvent& event ) override;
        void OnToolsLoadDefaultConfig( wxCommandEvent& event ) override;
        void OnToolsLoadDefaultConfigUI( wxUpdateUIEvent& event ) override;

        void OnCenterRx(wxCommandEvent& event) override;

        void OnHelpCheckUpdates( wxCommandEvent& event ) override;
        void OnHelpCheckUpdatesUI( wxUpdateUIEvent& event ) override;
        void OnHelpAbout( wxCommandEvent& event ) override;
        void OnHelpManual( wxCommandEvent& event ) override;
        void OnCheckSNRClick( wxCommandEvent& event ) override;

        // Toggle Buttons
        void OnTogBtnSplitClick(wxCommandEvent& event);
        void OnTogBtnAnalogClick(wxCommandEvent& event) override;
        void OnTogBtnPTT( wxCommandEvent& event ) override;
        void OnTogBtnPTTRightClick( wxContextMenuEvent& event ) override;

        // NOTE: sets TX colour on press to avoid a GTK blue-flash during the TX delay.
        // Upstream may prefer a different approach (e.g. true press-to-start TX).
        void OnTogBtnPTTMouseDown( wxMouseEvent& event );
        void OnTogBtnPTTMouseLeave( wxMouseEvent& event );

        void OnTogBtnVoiceKeyerClick (wxCommandEvent& event) override;
        void OnTogBtnVoiceKeyerRightClick( wxContextMenuEvent& event ) override;
        
        void OnHelp( wxCommandEvent& event ) override;

        void OnTogBtnOnOff( wxCommandEvent& event ) override;
        void OnTogBtnRecord( wxCommandEvent& event ) override;

        virtual void OnLogQSO(wxCommandEvent& event) override;
        
        void OnCallSignReset( wxCommandEvent& event ) override;
        void OnBerReset( wxCommandEvent& event ) override;

        //System Events
        void OnSize( wxSizeEvent& event );
        void OnUpdateUI( wxUpdateUIEvent& event );
        void OnDeleteConfig(wxCommandEvent&);
        void OnDeleteConfigUI( wxUpdateUIEvent& event );
#ifdef _USE_TIMER
        void OnTimer(wxTimerEvent &evt);
#endif
#ifdef _USE_ONIDLE
        void OnIdle(wxIdleEvent &evt);
#endif

        int VoiceKeyerStartTx(void);
        
        void applyTxLevel();
        void OnTxLevelDecrBig( wxCommandEvent& event ) override;
        void OnTxLevelDecr( wxCommandEvent& event ) override;
        void OnTxLevelIncr( wxCommandEvent& event ) override;
        void OnTxLevelIncrBig( wxCommandEvent& event ) override;
        void OnTxLevelMouseWheel( wxMouseEvent& event ) override;
        void OnTxLevelContextMenu( wxContextMenuEvent& event ) override;
        void OnTuneAttenContextMenu( wxContextMenuEvent& event ) override;
        void loadTxAttenForBand_(FilterFrequency band);
        void loadTuneAttenForBand_(FilterFrequency band);
        void autoSaveCurrentBandLevels_(bool writeConfig = true);
        
        void OnChangeMicSpkrLevel( wxScrollEvent& event ) override;
        
        void OnChangeReportFrequency( wxCommandEvent& event ) override;
        void OnChangeReportFrequencyVerify( wxCommandEvent& event ) override;
        
        void OnReportFrequencySetFocus(wxFocusEvent& event) override;
        void OnReportFrequencyKillFocus(wxFocusEvent& event) override;
        void OnReportFrequencyContextMenu(wxContextMenuEvent& event) override;
        void OnReportFrequencyItemMenu(int item) override;
        void OnReportFrequencyItemDelete(int item) override;

        void OnSystemColorChanged(wxSysColourChangedEvent& event) override;
        
        void OnChooseAlternateVoiceKeyerFile( wxCommandEvent& event );
        void OnRecordNewVoiceKeyerFile( wxCommandEvent& event );

        void OnTOTTimer(wxTimerEvent& evt);
        void OnTOTWarningTimer(wxTimerEvent& evt);
        void OnPttKeyPollTimer(wxTimerEvent& evt);
        void playTotBeep_();
        void stopTotBeep_();
        
        void OnSetMonitorVKAudio( wxCommandEvent& event );
        void OnSetMonitorTxAudio( wxCommandEvent& event );
        
        void OnSetMonitorVKAudioVol( wxCommandEvent& event );
        void OnSetMonitorTxAudioVol( wxCommandEvent& event );
        
        void OnResetMicSpkrLevel(wxMouseEvent& event) override;

        void OnRightClickCallsignList(wxMouseEvent& event) override;

        void OnOpenCallsignList( wxCommandEvent& event ) override;
        void OnCloseCallsignList( wxCommandEvent& event ) override;

        void OnToggleReporterVisibility (wxCommandEvent& event) override;
        void OnTogBtnTune(wxCommandEvent& event) override;
        
    private:
        const wxString SNR_FORMAT_STR;
        const wxString MODE_FORMAT_STR;
        const wxString MODE_RADE_FORMAT_STR;
        const wxString NO_SNR_LABEL;
        const wxString EMPTY_STR;
        const wxString MODEM_LABEL;
        const wxString BITS_UNK_LABEL;
        const wxString ERRS_UNK_LABEL;
        const wxString BER_UNK_LABEL;
        const wxString FRQ_OFF_UNK_LABEL;
        const wxString SYNC_UNK_LABEL;
        const wxString VAR_UNK_LABEL;
        const wxString CLK_OFF_UNK_LABEL;
        const wxString MIC_SPKR_LEVEL_FORMAT_STR;
        const wxString DECIBEL_STR;
        const wxString CURRENT_TIME_FORMAT_STR;
        const wxString SNR_FORMAT_STR_NO_DB;
        const wxString CALLSIGN_FORMAT_RGX;
        const wxString BITS_FMT;
        const wxString ERRS_FMT;
        const wxString BER_FMT;
        const wxString RESYNC_FMT;
        const wxString FRQ_OFF_FMT;
        const wxString SYNC_FMT;
        const wxString VAR_FMT;
        const wxString CLK_OFF_FMT;

        friend class MainApp; // needed for unit tests
        friend class TxRxThread; // XXX - needed for execOnUiThreadAndWait_().

        std::shared_ptr<IAudioDevice> rxInSoundDevice;
        std::shared_ptr<IAudioDevice> rxOutSoundDevice;
        std::shared_ptr<IAudioDevice> txInSoundDevice;
        std::shared_ptr<IAudioDevice> txOutSoundDevice;
        
        unsigned int         m_timeSinceSyncLoss;
        bool        m_useMemory;
        wxTextCtrl* m_tc;
        float       m_snrBeta;

        // Callsign/text messaging
        char        m_callsign[MAX_CALLSIGN];
        char       *m_pcallsign;

        // Events
        void        processTxtEvent(char event[]);
        class OptionsDlg *optionsDlg;

        // level Gauge
        float       m_maxLevel;      // RX: linear peak amplitude
        float       m_maxLevelDbTx;  // TX: peak level in dBFS

        // flags to indicate when new EQ filters need to be designed

        std::atomic<bool> m_newMicInFilter;
        std::atomic<bool> m_newSpkOutFilter;

        void        designEQFilters(paCallBackData *cb, int rxSampleRate, int txSampleRate);
        void        deleteEQFilters(paCallBackData *cb);

        // Voice Keyer States

        int        vk_rx_pause;
        int        vk_repeats, vk_repeat_counter;
        float      vk_rx_time;
        float      vk_rx_sync_time;
        bool suppressFreqModeUpdates_;
        bool firstFreqUpdateOnConnect_;
        FilterFrequency lastBand_;
        // Restore-point: the TX/tune level that was active when we entered the
        // current band (or when Enable was first clicked for that band). Restore
        // reverts to this rather than re-reading the map, which may already have
        // been overwritten by auto-save on a prior band departure.
        // Initialised to -20 dB (units are tenths of dB) as a safe default in
        // case Restore is invoked before any band load or Enable has occurred.
        int txLoadedLevel_{-200};
        int tuneLoadedLevel_{-200};
        
        std::string vkFileName_;
        VoiceKeyerFileCache vkFileCache_;
        
        wxMenu* voiceKeyerPopupMenu_;
        wxMenu* pttPopupMenu_;
        wxMenuItem* adjustMonitorPttVolMenuItem_;
        wxMenuItem* adjustMonitorVKVolMenuItem_;
        wxMenuItem* chooseVKFileMenuItem_;
        wxMenuItem* recordNewVoiceKeyerFileMenuItem_;

        bool terminating_; // used for terminating FreeDV
        bool closingReporterDialog_; // set while closing waits for the Reporter dialog's last messages
        bool realigned_; // used to inhibit resize hack once already done
        bool syncState_; // GUI copy of current sync state

        // Signalled once the detached rig PTT/frequency controller disconnect
        // threads (see performFreeDVOff_()) finish tearing down. Only waited on,
        // with a bounded timeout, when terminating_ is set -- lets a responsive
        // rig disconnect cleanly before the process exits out from under the
        // detached thread, without reintroducing an unbounded hang against an
        // unresponsive one.
        std::future<void> rigPttDisconnectFuture_;
        std::future<void> rigFreqDisconnectFuture_;

        // Caches appConfiguration.experimentalFeatures as of the last tab layout load
        // attempt, so exit-time save uses that instead of a possibly-since-toggled live
        // value (toggling the checkbox mid-session doesn't reload/reapply a layout).
        bool tabLayoutPersistenceEnabledAtStartup_;
        
        int         getSoundCardIDFromName(wxString& name, bool input);
        bool        validateSoundCardSetup(bool silent = false);
        
        void loadConfiguration_();
        void restoreCallsignListFromCsv_();
        void resetStats_();
        void exportConfiguration_(wxConfigBase* config);
        void setConfiguration_(wxConfigBase* config);

        HamlibRigController::Mode getCurrentMode_();
        
        void performFreeDVOn_();
        void performFreeDVOff_();
        
        void executeOnUiThreadAndWait_(std::function<void()> fn);
        
        void updateReportingFreqList_();
        wxString reportFrequencyListString_(wxString const& text);
        void addReportFrequencyToList_(wxString const& freq);
        void updateFreqBoxLabel_();
        void updateFreqBoxTooltip_();
        
        void initializeFreeDVReporter_();
        void updateVoiceKeyerButtonLabel_();
        int captureCurrentMicGroupTab_();
        
        void onFrequencyModeChange_(IRigFrequencyController*, uint64_t freq, IRigFrequencyController::Mode mode);
        void onRadioConnected_(IRigController* ptr);
        void onRigPttChange_(bool pttState);
        void onRadioDisconnected_(IRigController* ptr);

        // Audio error handlers
        void onAudioEngineError_(IAudioEngine&, std::string const& error, void* state);
        void onAudioDeviceError_(std::string error);
        static void OnAudioDeviceError_(IAudioDevice&, std::string const& error, void* state);

        // Audio device change handling
        template<int soundCardId, bool isOut>
        void handleAudioDeviceChange_(std::string const& newDeviceName);

        // Audio device data handlers
        static void OnTxInAudioData_(IAudioDevice& dev, void* data, size_t size, void* state) FREEDV_NONBLOCKING;
        static void OnTxOutAudioData_(IAudioDevice& dev, void* data, size_t size, void* state) FREEDV_NONBLOCKING;
        static void OnRxInAudioData_(IAudioDevice& dev, void* data, size_t size, void* state) FREEDV_NONBLOCKING;
        static void OnRxOutAudioData_(IAudioDevice& dev, void* data, size_t size, void* state) FREEDV_NONBLOCKING;

        // QSY request handling
        struct QsyRequestArgs {
            std::string callsign;
            uint64_t freqHz;
            std::string message;
        };

        void onQsyRequest_(std::string callsign, uint64_t freqHz, std::string message);
        void onQsyRequestUIThread_(QsyRequestArgs* args);
        
        bool isFrequencyControlEnabled_()
        {
#if 0
            auto& rigControlConfig = wxGetApp().appConfiguration.rigControlConfiguration;
            return 
                wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled || 
                ((rigControlConfig.hamlibUseForPTT 
#if defined(WIN32)
                || rigControlConfig.useOmniRig
#endif // defined(WIN32)
                ) && (rigControlConfig.hamlibEnableFreqModeChanges || rigControlConfig.hamlibEnableFreqChangesOnly));
#else
            return true;
#endif // 0
        }
        
        // A contact can only be logged by sending it to a
        // logging program over UDP, so only allow Log QSO if that's enabled.
        bool canLogQso_()
        {
            return
                wxGetApp().appConfiguration.reportingConfiguration.udpReportingEnabled &&
                m_lastReportedCallsignListView->GetItemCount() > 0;
        }
        
        int getIdealStationsHeardColumnLength_(int col);
};

void resample_for_plot(GenericFIFO<short> *plotFifo, short buf[], short* dec_samples, int length, int fs) FREEDV_NONBLOCKING;

void txRxProcessing();

// FreeDv API calls this when there is a test frame that needs a-plottin'

void my_freedv_put_error_pattern(void *state, short error_pattern[], int sz_error_pattern);

// FreeDv API calls these puppies when it needs/receives a text char

char my_get_next_tx_char(void *callback_state);
void my_put_next_rx_char(void *callback_state, char c);

// helper complex freq shift function


#endif //__FDMDV2_MAIN__
