//==========================================================================
// Name:            dlg_options.h
// Purpose:         Dialog for controlling misc FreeDV options
// Created:         Nov 25 2012
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

#ifndef __OPTIONS_DIALOG__
#define __OPTIONS_DIALOG__

#include <wx/clrpicker.h>
#include <wx/listctrl.h>
#include <map>
#include <vector>
#include <wx/propgrid/property.h>
#include <wx/propgrid/props.h>

#include "../../main.h"
#include "defines.h"
#include "audio/IAudioEngine.h"

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// Class OptionsDlg
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
class OptionsDlg : public wxDialog
{
    public:
    OptionsDlg( wxWindow* parent,
                wxWindowID id = wxID_ANY, const wxString& title = _("Settings"), 
                const wxPoint& pos = wxDefaultPosition, 
                const wxSize& size = wxDefaultSize, 
                long style = wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER|wxTAB_TRAVERSAL );
        ~OptionsDlg();

        void    ExchangeData(int inout, bool storePersistent);

        void DisplayFifoPACounters();
        
        void setSessionActive(bool active) 
        {
            sessionActive_ = active; 
        
            updateReportingState();
        }
        
    protected:

        // Handlers for events.

        void    OnOK(wxCommandEvent& event);
        void    OnCancel(wxCommandEvent& event);
        void    OnApply(wxCommandEvent& event);
        void    OnClose(wxCloseEvent& event);
        void    OnInitDialog(wxInitDialogEvent& event);
 
        void    OnDebugConsole(wxScrollEvent& event);

        void    OnFifoReset(wxCommandEvent& event);
        
        void    OnReportingEnable(wxCommandEvent& event);
        void    OnToneStateEnable(wxCommandEvent& event);
        void    OnFreqModeChangeEnable(wxCommandEvent& event);
        void    OnEnableSpacebarForPTT(wxCommandEvent& event);
        void    OnSetPTTKey(wxCommandEvent& event);
        void    OnTOTTimerEnable(wxCommandEvent& event);
        void    OnDialogCharHook(wxKeyEvent& event);
        void    OnPTTKeyCapture(wxKeyEvent& event);
        void    enterPTTCaptureMode_();
        void    exitPTTCaptureMode_(bool accept, int keyCode = 0);

        wxCheckBox* m_ckHalfDuplex;

        wxNotebook  *m_notebook;
        wxNotebookPage *m_radioTab; // Radio (CAT/PTT Config)
        wxNotebookPage *m_reportingTab; // txt msg/PSK Reporter
        wxNotebookPage *m_rigControlTab; // Rig Control
        wxNotebookPage *m_displayTab; // Waterfall color, other display config
        wxNotebookPage *m_rxAudioTab; // Receive Audio devices
        wxNotebookPage *m_txAudioTab; // Transmit Audio devices
        wxNotebookPage *m_voiceKeyerTab; // Voice Keyer / Quick Record
        wxNotebookPage *m_modemTab; // 700/OFDM/duplex
        wxNotebookPage *m_debugTab; // Debug

        /* Radio tab - VOX PTT Settings */
        wxCheckBox* m_ckLeftChannelVoxTone;

        /* Radio tab - Hamlib Settings */
        wxCheckBox *m_ckUseHamlibPTT;
        wxComboBox *m_cbRigName;
        wxComboBox *m_cbSerialPort;
        wxComboBox *m_cbPttSerialPort;
        wxComboBox *m_cbSerialRate;
        wxStaticText *m_stIcomCIVHex;
        wxTextCtrl *m_tcIcomCIVHex;
        wxComboBox *m_cbPttMethod;
        wxCheckBox *m_ckForceRTSOn;
        wxCheckBox *m_ckForceDTROn;

        /* Radio tab - Serial Port Settings */
        wxCheckBox    *m_ckUseSerialPTT;
        wxStaticText  *m_staticTextSerialDevice;
        wxComboBox    *m_cbCtlDevicePath;
        wxRadioButton *m_rbUseDTR;
        wxCheckBox    *m_ckRTSPos;
        wxRadioButton *m_rbUseRTS;
        wxCheckBox    *m_ckDTRPos;

        /* Radio tab - PTT In Settings */
        wxCheckBox    *m_ckUsePTTInput;
        wxStaticText  *m_pttInSerialDeviceLabel;
        wxCheckBox    *m_ckCTSPos;
        wxComboBox    *m_cbCtlDevicePathPttIn;

#if defined(WIN32)
        /* Radio tab - OmniRig Settings */
        wxCheckBox    *m_ckUseOmniRig;
        wxComboBox    *m_cbOmniRigRigId;
#endif

        wxButton* m_buttonTestPTT;
        
        /* Hamlib options */
        wxCheckBox    *m_ckboxUseAnalogModes;
        wxCheckBox    *m_ckboxRestoreFreqModeOnStop;
        wxRadioButton *m_ckboxEnableFreqModeChanges;
        wxRadioButton *m_ckboxEnableFreqChangesOnly;
        wxRadioButton *m_ckboxNoFreqModeChanges;
        wxCheckBox    *m_ckboxEnableSpacebarForPTT;
        wxCheckBox    *m_ckboxPTTMomentaryMode;
        wxTextCtrl    *m_txtPTTKeyName;
        wxButton      *m_btnSetPTTKey;
        int            m_selectedPTTKeyCode;
        bool           m_capturingPTTKey;
        wxTextCtrl    *m_txtTxRxDelayMilliseconds;
        wxCheckBox    *m_ckboxFrequencyEntryAsKHz;

        /* Time-Out Timer options */
        wxCheckBox    *m_ckboxTOTTimerEnabled;
        wxTextCtrl    *m_txtTOTTimerSecs;
        
        /* Waterfall color */
        wxRadioButton *m_waterfallColorScheme1; // Multicolored
        wxRadioButton *m_waterfallColorScheme2; // Black & white
        wxRadioButton *m_waterfallColorScheme3; // Blue tint?

        /* FreeDV Reporter colors */
        wxColourPickerCtrl* m_freedvReporterTxBackgroundColor;
        wxColourPickerCtrl* m_freedvReporterTxForegroundColor;
        wxColourPickerCtrl* m_freedvReporterRxBackgroundColor;
        wxColourPickerCtrl* m_freedvReporterRxForegroundColor;
        wxColourPickerCtrl* m_freedvReporterMsgBackgroundColor;
        wxColourPickerCtrl* m_freedvReporterMsgForegroundColor;
        
        /* Spectrum plot averaging */
        wxComboBox*             m_cbxNumSpectrumAveraging;

        /* Voice Keyer */

        wxButton     *m_buttonChooseVoiceKeyerWaveFilePath;
        wxTextCtrl   *m_txtCtrlVoiceKeyerWaveFilePath;
        wxTextCtrl   *m_txtCtrlVoiceKeyerRxPause;
        wxTextCtrl   *m_txtCtrlVoiceKeyerRepeats;

        /* Quick Record */
        wxButton     *m_buttonChooseQuickRecordRawPath;
        wxTextCtrl   *m_txtCtrlQuickRecordRawPath;
        wxButton     *m_buttonChooseQuickRecordDecodedPath;
        wxTextCtrl   *m_txtCtrlQuickRecordDecodedPath;

        /* Audio tab - device selection lists */
        wxListCtrl* m_lcSoundCard1InDevice;
        wxListCtrl* m_lcSoundCard1OutDevice;
        wxListCtrl* m_lcSoundCard2InDevice;
        wxListCtrl* m_lcSoundCard2OutDevice;

        /* Audio tab - selected device text controls (readonly) */
        wxTextCtrl* m_tcSoundCard1InDevice;
        wxTextCtrl* m_tcSoundCard1OutDevice;
        wxTextCtrl* m_tcSoundCard2InDevice;
        wxTextCtrl* m_tcSoundCard2OutDevice;

        /* Audio tab - test buttons */
        wxButton*    m_btnSoundCard1InTest;
        wxButton*    m_btnSoundCard1OutTest;
        wxButton*    m_btnSoundCard2InTest;
        wxButton*    m_btnSoundCard2OutTest;

        /* Audio tab - refresh buttons */
        wxButton*    m_btnRefreshRxAudio;
        wxButton*    m_btnRefreshTxAudio;

        /* Audio tab - receive only */
        wxCheckBox*  m_ckTxReceiveOnly;

        void OnSoundCard1InTest(wxCommandEvent& event);
        void OnSoundCard1OutTest(wxCommandEvent& event);
        void OnSoundCard2InTest(wxCommandEvent& event);
        void OnSoundCard2OutTest(wxCommandEvent& event);
        void OnSoundCard1InDeviceSelect(wxListEvent& event);
        void OnSoundCard1OutDeviceSelect(wxListEvent& event);
        void OnSoundCard2InDeviceSelect(wxListEvent& event);
        void OnSoundCard2OutDeviceSelect(wxListEvent& event);
        void OnTxReceiveOnlyChanged(wxCommandEvent& event);
        void OnRefreshAudioDevices(wxCommandEvent& event);

        /* test frames, other simulated channel impairments */
        wxCheckBox   *m_ckboxTone;
        wxTextCtrl   *m_txtToneFreqHz;
        wxTextCtrl   *m_txtToneAmplitude;

        wxCheckBox   *m_ckboxAutoStartOnLaunch;

        wxRadioButton *m_rb_textEncoding1;
        wxRadioButton *m_rb_textEncoding2;

        wxCheckBox    *m_ckboxReportingEnable;
        wxTextCtrl    *m_txt_callsign;
        wxTextCtrl    *m_txt_grid_square;
        
        wxCheckBox    *m_ckboxManualFrequencyReporting;
        
        wxCheckBox    *m_ckboxPskReporterEnable;
        
        wxCheckBox    *m_ckboxFreeDVReporterEnable;
        wxTextCtrl    *m_freedvReporterHostname;
        wxCheckBox    *m_ckboxFreeDVReporterUseTls;
        wxCheckBox    *m_useMetricDistances;
        wxCheckBox    *m_useCardinalDirections;
        wxCheckBox    *m_ckboxFreeDVReporterForceReceiveOnly;
        
        wxCheckBox    *m_ckboxUDPReportingEnable;
        wxTextCtrl    *m_udpHostname;
        wxTextCtrl    *m_udpPort;

        wxCheckBox    *m_ckboxUDPBroadcastEnable;
        wxTextCtrl    *m_udpBroadcastAddress;
        wxTextCtrl    *m_udpBroadcastPort;

        wxTextCtrl    *m_txtCtrlCsvLogFilePath;
        wxButton      *m_buttonChooseCsvLogFilePath;
        
        wxButton*     m_BtnFifoReset;
        wxStaticText  *m_textFifos;
        wxStaticText  *m_textPA1;
        wxStaticText  *m_textPA2;
        wxTextCtrl    *m_txtCtrlFifoSize;
        wxCheckBox    *m_ckboxTxRxThreadPriority;
        wxCheckBox    *m_ckboxTxRxDumpTiming;
        wxCheckBox    *m_ckboxTxRxDumpFifoState;
        wxCheckBox    *m_ckboxVerbose;
        wxCheckBox    *m_ckboxFreeDVAPIVerbose;
        wxCheckBox    *m_experimentalFeatures;
        wxCheckBox    *m_showDecodeStats;
        
        wxButton*     m_sdbSizer5OK;
        wxButton*     m_sdbSizer5Cancel;
        wxButton*     m_sdbSizer5Apply;

        wxCheckBox   *m_ckboxDebugConsole;

        wxTextCtrl*  m_statsResetTime;
        
        wxCheckBox*  m_ckbox_use_utc_time;
        
        wxListBox*  m_freqList;
        wxStaticText* m_labelEnterFreq;
        wxTextCtrl* m_txtCtrlNewFrequency;
        wxButton*   m_freqListAdd;
        wxButton*   m_freqListRemove;
        
        unsigned int  event_in_serial, event_out_serial;

        void OnChooseVoiceKeyerWaveFilePath(wxCommandEvent& event);
        void OnChooseQuickRecordPath(wxCommandEvent& event);
        void OnChooseCsvLogFilePath(wxCommandEvent& event);

        /* Radio tab event handlers */
        void PTTUseHamLibClicked(wxCommandEvent& event);
        void PTTUseSerialClicked(wxCommandEvent& event);
#if defined(WIN32)
        void PTTUseOmniRigClicked(wxCommandEvent& event);
#endif
        void PTTUseSerialInputClicked(wxCommandEvent& event);
        void HamlibRigNameChanged(wxCommandEvent& event);
        void OnHamlibSerialPortChanged(wxCommandEvent& event);
        void OnHamlibPttMethodChanged(wxCommandEvent& event);
        void resetIcomCIVStatus();
        void OnTestPTT(wxCommandEvent& event);
        
        void OnReportingFreqSelectionChange(wxCommandEvent& event);
        void OnReportingFreqTextChange(wxCommandEvent& event);
        void OnReportingFreqAdd(wxCommandEvent& event);
        void OnReportingFreqRemove(wxCommandEvent& event);
        
     private:
         void updateReportingState();
         void updateChannelNoiseState();
         void updateToneState();
         void updateRigControlState();
         void updateRadioControlState();
         void populatePortList();
         void populateBaudRateList(int min = 0, int max = 0);

         void populateAudioDeviceList(wxListCtrl* list, IAudioEngine::AudioDirection direction);
         void selectListDevice(wxListCtrl* list, wxTextCtrl* tc, const wxString& devName);
         void onAudioDeviceSelected(wxListCtrl* list, wxTextCtrl* tc, long index);
         static wxString getSelectedAudioDeviceName(wxTextCtrl* tc);
         void testAudioOutput(const wxString& devName, wxButton* btn);
         void testAudioInput(const wxString& inDevName, const wxString& outDevName, wxButton* btn);

         bool sessionActive_;
         bool isTesting_;
         std::thread* m_audioPlotThread;

         // Internal (config) device names for each row of the audio device lists.
         // The lists themselves show the user-friendly names.
         std::map<wxListCtrl*, std::vector<wxString>> audioDeviceNames_;
};

#endif // __OPTIONS_DIALOG__
