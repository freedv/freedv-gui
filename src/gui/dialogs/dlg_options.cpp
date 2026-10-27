//==========================================================================
// Name:            dlg_options.cpp
// Purpose:         Dialog for controlling misc FreeDV options
// Date:            May 24 2013
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

#include <wx/gbsizer.h>
#include <wx/listctrl.h>
#include <wx/statline.h>
#include <wx/numformatter.h>
#include "dlg_options.h"

#ifdef __WXMSW__
#include <wx/msw/registry.h>
#else
#include <glob.h>
#include <string.h>
#endif

#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

#include "rig_control/HamlibRigController.h"
#include "rig_control/SerialPortOutRigController.h"

#if defined(WIN32)
#include "rig_control/omnirig/OmniRigController.h"
#endif

#include "audio/AudioEngineFactory.h"
#include "audio/IAudioDevice.h"
#include "samplerate.h"

using namespace std::chrono_literals;

#define OPTIONS_AUDIO_TEST_DURATION_SECS   2
#define OPTIONS_AUDIO_RECORD_DURATION_SECS 5

extern FreeDVInterface freedvInterface;

// F13-F24 on Linux (and possibly other platforms) return values different
// than how they're defined in wxWidgets. These constants are so we can
// check for these as well and do the right thing for key->name mapping.
constexpr int WXK_F13_LINUX = 436;
constexpr int WXK_F24_LINUX = WXK_F13_LINUX + 11;

// Returns a human-readable name for a PTT key code.
static wxString getPTTKeyName(int keyCode)
{
    if (keyCode >= 'A' && keyCode <= 'Z')
    {
        return wxString((char)keyCode);
    }
    else if (keyCode >= '0' && keyCode <= '9')
    {
        return wxString((char)keyCode);
    }
    else if (keyCode >= WXK_F1 && keyCode <= WXK_F24)
    {
        return wxString::Format(_("F%d"), (keyCode - WXK_F1) + 1);
    }
    else if (keyCode >= WXK_F13_LINUX && keyCode <= WXK_F24_LINUX)
    {
        return wxString::Format(_("F%d"), (keyCode - WXK_F13_LINUX) + 13);
    }

    switch (keyCode)
    {
        case WXK_SPACE:    return _("Space");
        case WXK_TAB:      return _("Tab");
        case WXK_RETURN:   return _("Enter");
        case WXK_ESCAPE:   return _("Escape");
        case WXK_BACK:     return _("Backspace");
        case WXK_DELETE:   return _("Delete");
        case WXK_INSERT:   return _("Insert");
        case WXK_HOME:     return _("Home");
        case WXK_END:      return _("End");
        case WXK_PAGEUP:   return _("Page Up");
        case WXK_PAGEDOWN: return _("Page Down");
        case WXK_UP:       return _("Up");
        case WXK_DOWN:     return _("Down");
        case WXK_LEFT:     return _("Left");
        case WXK_RIGHT:    return _("Right");
        default:
            if (keyCode > 32 && keyCode < 127)
                return wxString((char)keyCode);
            return wxString::Format(_("Key(%d)"), keyCode);
    }
}

// PortAudio over/underflow counters

extern std::atomic<int>    g_noiseSNR;
extern std::atomic<int>    g_infifo1_full;
extern std::atomic<int>    g_outfifo1_empty;
extern std::atomic<int>    g_infifo2_full;
extern std::atomic<int>    g_outfifo2_empty;
extern std::atomic<int>    g_AEstatus1[4];
extern std::atomic<int>    g_AEstatus2[4];
extern wxDatagramSocket    *g_sock;
extern std::atomic<int>    g_dump_timing;
extern std::atomic<int>    g_dump_fifo_state;
extern int                 g_freedv_verbose;
extern wxConfigBase *pConfig;

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// Class OptionsDlg
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
OptionsDlg::OptionsDlg(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxDialog(parent, id, title, pos, size, style)
{
    WXWIDGETS_SET_LAYOUT_DIRECTION;
    
    if (wxGetApp().customConfigFileName != "")
    {
        SetTitle(wxString::Format("%s (%s)", title, wxGetApp().customConfigFileName));
    }
    
    sessionActive_ = false;
    isTesting_ = false;
    m_audioPlotThread = nullptr;

    wxPanel* panel = new wxPanel(this);

    wxBoxSizer* bSizer30;
    bSizer30 = new wxBoxSizer(wxVERTICAL);

    // Create notebook and tabs.
    m_notebook = new wxNotebook(panel, wxID_ANY);
    m_radioTab = new wxPanel(m_notebook, wxID_ANY);
    m_reportingTab = new wxPanel(m_notebook, wxID_ANY);
    m_rigControlTab = new wxPanel(m_notebook, wxID_ANY);
    m_displayTab = new wxPanel(m_notebook, wxID_ANY);
    m_rxAudioTab = new wxPanel(m_notebook, wxID_ANY);
    m_txAudioTab = new wxPanel(m_notebook, wxID_ANY);
    m_voiceKeyerTab = new wxPanel(m_notebook, wxID_ANY);
    m_modemTab = new wxPanel(m_notebook, wxID_ANY);
    m_debugTab = new wxPanel(m_notebook, wxID_ANY);

    m_notebook->AddPage(m_rxAudioTab, _("RX Audio"));
    m_notebook->AddPage(m_txAudioTab, _("TX Audio"));
    m_notebook->AddPage(m_radioTab, _("Radio"));
    m_notebook->AddPage(m_rigControlTab, _("Rig Control"));
    m_notebook->AddPage(m_reportingTab, _("Reporting"));
    m_notebook->AddPage(m_voiceKeyerTab, _("Voice Keyer"));
    m_notebook->AddPage(m_displayTab, _("Display"));
    m_notebook->AddPage(m_modemTab, _("Modem"));
    m_notebook->AddPage(m_debugTab, _("Debugging"));
    
    bSizer30->Add(m_notebook, 1, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 3);

    // Radio tab (CAT and PTT Config)
    wxBoxSizer* sizerRadio = new wxBoxSizer(wxVERTICAL);

    // VOX tone option
    wxStaticBox* voxBox = new wxStaticBox(m_radioTab, wxID_ANY, _("VOX PTT Settings"));
    wxStaticBoxSizer* sizerVox = new wxStaticBoxSizer(voxBox, wxHORIZONTAL);
    m_ckLeftChannelVoxTone = new wxCheckBox(voxBox, wxID_ANY, _("Left Channel Vox Tone"), wxDefaultPosition, wxSize(-1,-1), 0);
    sizerVox->Add(m_ckLeftChannelVoxTone, 0, wxALIGN_LEFT|static_cast<int>(wxALIGN_CENTER_VERTICAL), 5);
    sizerRadio->Add(sizerVox, 0, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 5);

    // Hamlib settings
    wxStaticBox* hamlibBox = new wxStaticBox(m_radioTab, wxID_ANY, _("Hamlib Settings"));
    wxStaticBoxSizer* sizerHamlib = new wxStaticBoxSizer(hamlibBox, wxHORIZONTAL);
    wxGridSizer* gridSizerhl = new wxGridSizer(8, 2, 0, 0);
    sizerHamlib->Add(gridSizerhl, 1, static_cast<int>(wxEXPAND)|wxALIGN_LEFT, 5);

    m_ckUseHamlibPTT = new wxCheckBox(hamlibBox, wxID_ANY, _("Enable CAT control via Hamlib"), wxDefaultPosition, wxSize(-1, -1), 0);
    m_ckUseHamlibPTT->SetValue(false);
    gridSizerhl->Add(m_ckUseHamlibPTT, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 0);
    gridSizerhl->Add(new wxStaticText(hamlibBox, -1, wxT("")), 0, static_cast<int>(wxEXPAND));

    gridSizerhl->Add(new wxStaticText(hamlibBox, wxID_ANY, _("Rig Model:"), wxDefaultPosition, wxDefaultSize, 0),
                      0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 20);
    m_cbRigName = new wxComboBox(hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(250, -1), 0, NULL, wxCB_DROPDOWN | wxCB_READONLY);
    {
        auto numHamlibDevices = HamlibRigController::GetNumberSupportedRadios();
        for (auto index = 0; index < numHamlibDevices; index++)
        {
            m_cbRigName->Append(HamlibRigController::RigIndexToName(index));
        }
        m_cbRigName->SetSelection(wxGetApp().m_intHamlibRig);
    }
    gridSizerhl->Add(m_cbRigName, 0, static_cast<int>(wxEXPAND), 0);

    gridSizerhl->Add(new wxStaticText(hamlibBox, wxID_ANY, _("Serial Device (or hostname:port):"), wxDefaultPosition, wxDefaultSize, 0),
                      0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 20);
    m_cbSerialPort = new wxComboBox(hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_DROPDOWN);
    m_cbSerialPort->SetMinSize(wxSize(140, -1));
    gridSizerhl->Add(m_cbSerialPort, 0, static_cast<int>(wxEXPAND), 0);

    m_stIcomCIVHex = new wxStaticText(hamlibBox, wxID_ANY, _("Radio Address:"), wxDefaultPosition, wxDefaultSize, 0);
    gridSizerhl->Add(m_stIcomCIVHex, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 20);
    m_tcIcomCIVHex = new wxTextCtrl(hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(35, -1), 0, wxNumericPropertyValidator(wxNumericPropertyValidator::Unsigned, 16));
    m_tcIcomCIVHex->SetMaxLength(2);
    gridSizerhl->Add(m_tcIcomCIVHex, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 0);

    gridSizerhl->Add(new wxStaticText(hamlibBox, wxID_ANY, _("Serial Rate:"), wxDefaultPosition, wxDefaultSize, 0),
                      0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 20);
    m_cbSerialRate = new wxComboBox(hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(140, -1), 0, NULL, wxCB_DROPDOWN);
    gridSizerhl->Add(m_cbSerialRate, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 0);

    gridSizerhl->Add(new wxStaticText(hamlibBox, wxID_ANY, _("PTT uses:"), wxDefaultPosition, wxDefaultSize, 0),
                      0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 20);
    m_cbPttMethod = new wxComboBox(hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_DROPDOWN | wxCB_READONLY);
    m_cbPttMethod->SetSize(wxSize(140, -1));
    gridSizerhl->Add(m_cbPttMethod, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 0);

    gridSizerhl->Add(new wxStaticText(hamlibBox, wxID_ANY, _("PTT Serial Device:"), wxDefaultPosition, wxDefaultSize, 0),
                      0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 20);
    m_cbPttSerialPort = new wxComboBox(hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_DROPDOWN);
    m_cbPttSerialPort->SetMinSize(wxSize(140, -1));
    gridSizerhl->Add(m_cbPttSerialPort, 0, static_cast<int>(wxEXPAND), 0);

    m_cbPttMethod->Append(wxT("CAT"));
    m_cbPttMethod->Append(wxT("RTS"));
    m_cbPttMethod->Append(wxT("DTR"));
    m_cbPttMethod->Append(_("None"));
    m_cbPttMethod->Append(_("CAT via Data port"));

    wxBoxSizer* forceRtsSizer = new wxBoxSizer(wxHORIZONTAL);
    m_ckForceRTSOn = new wxCheckBox(hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, -1), 0);
    m_ckForceRTSOn->SetToolTip(_("Always assert RTS on the Hamlib serial port (e.g. to power a radio interface)"));
    forceRtsSizer->Add(m_ckForceRTSOn, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 0);
    forceRtsSizer->Add(new wxStaticText(hamlibBox, wxID_ANY, _("Force RTS"), wxDefaultPosition, wxDefaultSize, 0),
                       0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxLEFT, 5);
    gridSizerhl->Add(forceRtsSizer, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 0);

    wxBoxSizer* forceDtrSizer = new wxBoxSizer(wxHORIZONTAL);
    m_ckForceDTROn = new wxCheckBox(hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, -1), 0);
    m_ckForceDTROn->SetToolTip(_("Always assert DTR on the Hamlib serial port (e.g. to power a radio interface)"));
    forceDtrSizer->Add(m_ckForceDTROn, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxLEFT, 10);
    forceDtrSizer->Add(new wxStaticText(hamlibBox, wxID_ANY, _("Force DTR"), wxDefaultPosition, wxDefaultSize, 0),
                       0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxLEFT, 5);
    gridSizerhl->Add(forceDtrSizer, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 0);

    sizerRadio->Add(sizerHamlib, 0, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 5);

    // Serial port PTT
    wxStaticBox* serialSettingsBox = new wxStaticBox(m_radioTab, wxID_ANY, _("Serial Port Settings"));
    wxStaticBoxSizer* sizerSerialSettings = new wxStaticBoxSizer(serialSettingsBox, wxVERTICAL);
    sizerRadio->Add(sizerSerialSettings, 0, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 5);

    wxStaticBox* pttBox = new wxStaticBox(serialSettingsBox, wxID_ANY, _("PTT Port"));
    wxStaticBoxSizer* sizerPttPort = new wxStaticBoxSizer(pttBox, wxVERTICAL);
    sizerSerialSettings->Add(sizerPttPort, 0, static_cast<int>(wxEXPAND), 5);

    wxGridSizer* gridSizer200 = new wxGridSizer(1, 3, 0, 0);

    m_ckUseSerialPTT = new wxCheckBox(pttBox, wxID_ANY, _("Use Serial Port PTT"), wxDefaultPosition, wxSize(-1,-1), 0);
    m_ckUseSerialPTT->SetValue(false);
    gridSizer200->Add(m_ckUseSerialPTT, 1, wxALIGN_CENTER|static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    m_staticTextSerialDevice = new wxStaticText(pttBox, wxID_ANY, _("Serial Device:  "), wxDefaultPosition, wxDefaultSize, 0);
    m_staticTextSerialDevice->Wrap(-1);
    gridSizer200->Add(m_staticTextSerialDevice, 1, wxALIGN_RIGHT|static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    m_cbCtlDevicePath = new wxComboBox(pttBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_DROPDOWN);
    m_cbCtlDevicePath->SetMinSize(wxSize(140, -1));
    gridSizer200->Add(m_cbCtlDevicePath, 1, static_cast<int>(wxEXPAND), 2);

    sizerPttPort->Add(gridSizer200, 0, static_cast<int>(wxEXPAND), 1);

    wxGridSizer* gridSizer17radio = new wxGridSizer(2, 2, 0, 0);

    m_rbUseDTR = new wxRadioButton(pttBox, wxID_ANY, _("Use DTR"), wxDefaultPosition, wxSize(-1,-1), wxRB_GROUP);
    m_rbUseDTR->SetToolTip(_("Toggle DTR line for PTT"));
    m_rbUseDTR->SetValue(1);
    gridSizer17radio->Add(m_rbUseDTR, 0, wxALIGN_CENTER|static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    m_rbUseRTS = new wxRadioButton(pttBox, wxID_ANY, _("Use RTS"), wxDefaultPosition, wxSize(-1,-1), 0);
    m_rbUseRTS->SetToolTip(_("Toggle the RTS pin for PTT"));
    m_rbUseRTS->SetValue(1);
    gridSizer17radio->Add(m_rbUseRTS, 0, wxALIGN_CENTER, 2);

    m_ckDTRPos = new wxCheckBox(pttBox, wxID_ANY, _("DTR = +V"), wxDefaultPosition, wxSize(-1,-1), 0);
    m_ckDTRPos->SetToolTip(_("Set Polarity of the DTR line"));
    m_ckDTRPos->SetValue(false);
    gridSizer17radio->Add(m_ckDTRPos, 0, wxALIGN_CENTER, 2);

    m_ckRTSPos = new wxCheckBox(pttBox, wxID_ANY, _("RTS = +V"), wxDefaultPosition, wxSize(-1,-1), 0);
    m_ckRTSPos->SetValue(false);
    m_ckRTSPos->SetToolTip(_("Set Polarity of the RTS line"));
    gridSizer17radio->Add(m_ckRTSPos, 0, wxALIGN_CENTER, 2);

    sizerPttPort->Add(gridSizer17radio, 0, static_cast<int>(wxEXPAND), 2);

    m_rbUseDTR->MoveBeforeInTabOrder(m_rbUseRTS);
    m_rbUseRTS->MoveBeforeInTabOrder(m_ckDTRPos);
    m_ckDTRPos->MoveBeforeInTabOrder(m_ckRTSPos);

    wxStaticBox* pttInBox = new wxStaticBox(serialSettingsBox, wxID_ANY, _("PTT In"));
    wxStaticBoxSizer* pttInBoxSizer = new wxStaticBoxSizer(pttInBox, wxVERTICAL);
    sizerSerialSettings->Add(pttInBoxSizer, 0, static_cast<int>(wxEXPAND), 5);

    wxGridSizer* gridSizerPttIn = new wxGridSizer(2, 3, 0, 0);

    m_ckUsePTTInput = new wxCheckBox(pttInBox, wxID_ANY, _("Enable PTT Input"), wxDefaultPosition, wxSize(-1,-1), 0);
    m_ckUsePTTInput->SetValue(false);
    gridSizerPttIn->Add(m_ckUsePTTInput, 1, wxALIGN_CENTER|static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    m_pttInSerialDeviceLabel = new wxStaticText(pttInBox, wxID_ANY, _("Serial Device:  "), wxDefaultPosition, wxDefaultSize, 0);
    m_pttInSerialDeviceLabel->Wrap(-1);
    gridSizerPttIn->Add(m_pttInSerialDeviceLabel, 1, wxALIGN_RIGHT|static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    m_cbCtlDevicePathPttIn = new wxComboBox(pttInBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_DROPDOWN);
    m_cbCtlDevicePathPttIn->SetMinSize(wxSize(140, -1));
    gridSizerPttIn->Add(m_cbCtlDevicePathPttIn, 1, static_cast<int>(wxEXPAND), 2);

    gridSizerPttIn->AddSpacer(1);

    m_ckCTSPos = new wxCheckBox(pttInBox, wxID_ANY, _("CTS = +V"), wxDefaultPosition, wxSize(-1,-1), 0);
    m_ckCTSPos->SetValue(false);
    m_ckCTSPos->SetToolTip(_("Set Polarity of the CTS line"));
    gridSizerPttIn->Add(m_ckCTSPos, 1, wxALIGN_CENTER, 5);

    pttInBoxSizer->Add(gridSizerPttIn, 0, static_cast<int>(wxEXPAND), 5);

#if defined(WIN32)
    wxStaticBox* omniRigBox = new wxStaticBox(m_radioTab, wxID_ANY, _("OmniRig Settings"));
    wxStaticBoxSizer* omniRigBoxSizer = new wxStaticBoxSizer(omniRigBox, wxHORIZONTAL);

    m_ckUseOmniRig = new wxCheckBox(omniRigBox, wxID_ANY, _("Enable CAT control via OmniRig"), wxDefaultPosition, wxSize(-1, -1), 0);
    m_ckUseOmniRig->SetValue(false);
    omniRigBoxSizer->Add(m_ckUseOmniRig, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 5);

    omniRigBoxSizer->Add(new wxStaticText(omniRigBox, wxID_ANY, _("Rig ID:"), wxDefaultPosition, wxDefaultSize, 0),
                      0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 5);
    m_cbOmniRigRigId = new wxComboBox(omniRigBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(50, -1), 0, NULL, wxCB_DROPDOWN | wxCB_READONLY);
    m_cbOmniRigRigId->Append("1");
    m_cbOmniRigRigId->Append("2");
    omniRigBoxSizer->Add(m_cbOmniRigRigId, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 5);
    sizerRadio->Add(omniRigBoxSizer, 0, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 5);
#endif

    // Test PTT button
    wxBoxSizer* testButtonSizer = new wxBoxSizer(wxVERTICAL);
    m_buttonTestPTT = new wxButton(m_radioTab, wxID_ANY, _("Test PTT"), wxDefaultPosition, wxDefaultSize, 0);
    testButtonSizer->Add(m_buttonTestPTT, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_HORIZONTAL), 5);
    sizerRadio->Add(testButtonSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);

    m_radioTab->SetSizer(sizerRadio);

    // Reporting tab
    wxBoxSizer* sizerReporting = new wxBoxSizer(wxVERTICAL);
     
    //----------------------------------------------------------
    // Reporting Options 
    //----------------------------------------------------------

    wxStaticBoxSizer* sbSizerReportingRows;
    wxBoxSizer* sbSizerReportingGeneral = new wxBoxSizer(wxHORIZONTAL);
    
    wxStaticBox* sbReporting = new wxStaticBox(m_reportingTab, wxID_ANY, _("Reporting"));
    
    sbSizerReportingRows = new wxStaticBoxSizer(sbReporting, wxVERTICAL);
    m_ckboxReportingEnable = new wxCheckBox(sbReporting, wxID_ANY, _("Enable Reporting"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizerReportingGeneral->Add(m_ckboxReportingEnable, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    wxStaticText* labelPskCallsign = new wxStaticText(sbReporting, wxID_ANY, _("Callsign:"), wxDefaultPosition, wxDefaultSize, 0);
    sbSizerReportingGeneral->Add(labelPskCallsign, 0,  static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    m_txt_callsign = new wxTextCtrl(sbReporting, wxID_ANY,  wxEmptyString, wxDefaultPosition, wxSize(180,-1), 0, wxTextValidator(wxFILTER_ALPHANUMERIC));
    sbSizerReportingGeneral->Add(m_txt_callsign, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    wxStaticText* labelPskGridSquare = new wxStaticText(sbReporting, wxID_ANY, _("Grid Square/Locator:"), wxDefaultPosition, wxDefaultSize, 0);
    sbSizerReportingGeneral->Add(labelPskGridSquare, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    m_txt_grid_square = new wxTextCtrl(sbReporting, wxID_ANY,  wxEmptyString, wxDefaultPosition, wxSize(180,-1), 0, wxTextValidator(wxFILTER_ALPHANUMERIC));
    sbSizerReportingGeneral->Add(m_txt_grid_square, 0,  static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    sbSizerReportingRows->Add(sbSizerReportingGeneral, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    
    wxBoxSizer* sbSizerReportingManualFrequency = new wxBoxSizer(wxHORIZONTAL);
    m_ckboxManualFrequencyReporting = new wxCheckBox(sbReporting, wxID_ANY, _("Manual Frequency Reporting"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizerReportingManualFrequency->Add(m_ckboxManualFrequencyReporting, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    sbSizerReportingRows->Add(sbSizerReportingManualFrequency, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    
    // PSK Reporter options
    wxBoxSizer* sbSizerReportingPSK = new wxBoxSizer(wxHORIZONTAL);
    m_ckboxPskReporterEnable = new wxCheckBox(sbReporting, wxID_ANY, _("Report to PSK Reporter"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizerReportingPSK->Add(m_ckboxPskReporterEnable, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    sbSizerReportingRows->Add(sbSizerReportingPSK, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    
    // FreeDV Reporter options
    wxBoxSizer* sbSizerReportingFreeDV = new wxBoxSizer(wxHORIZONTAL);
    m_ckboxFreeDVReporterEnable = new wxCheckBox(sbReporting, wxID_ANY, _("Report to FreeDV Reporter"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizerReportingFreeDV->Add(m_ckboxFreeDVReporterEnable, 0,  static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    sbSizerReportingRows->Add(sbSizerReportingFreeDV, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    
    // UDP reporting options
    wxBoxSizer* sbSizerReportingUDP = new wxBoxSizer(wxHORIZONTAL);
    m_ckboxUDPReportingEnable = new wxCheckBox(sbReporting, wxID_ANY, _("Enable QSO Logging"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    m_ckboxUDPReportingEnable->SetToolTip(_("Enables QSO logging using the WSJT-X support in your preferred logging program."));
    sbSizerReportingUDP->Add(m_ckboxUDPReportingEnable, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    wxStaticText* labelUDPHostName = new wxStaticText(sbReporting, wxID_ANY, _("IP Address:"), wxDefaultPosition, wxDefaultSize, 0);
    sbSizerReportingUDP->Add(labelUDPHostName, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    m_udpHostname = new wxTextCtrl(sbReporting, wxID_ANY,  wxEmptyString, wxDefaultPosition, wxSize(150,-1), 0);
    sbSizerReportingUDP->Add(m_udpHostname, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    wxStaticText* labelUDPPort = new wxStaticText(sbReporting, wxID_ANY, _("Port:"), wxDefaultPosition, wxDefaultSize, 0);
    sbSizerReportingUDP->Add(labelUDPPort, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    m_udpPort = new wxTextCtrl(sbReporting, wxID_ANY,  wxEmptyString, wxDefaultPosition, wxSize(60,-1), 0);
    sbSizerReportingUDP->Add(m_udpPort, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    sbSizerReportingRows->Add(sbSizerReportingUDP, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);

    // UDP broadcast reporting options (UdpReporter)
    wxBoxSizer* sbSizerReportingUDPBroadcast = new wxBoxSizer(wxHORIZONTAL);
    m_ckboxUDPBroadcastEnable = new wxCheckBox(sbReporting, wxID_ANY, _("Enable UDP Broadcast"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    m_ckboxUDPBroadcastEnable->SetToolTip(_("Broadcasts received callsign/frequency data as JSON UDP datagrams (e.g. to a multicast group or a local listener)."));
    sbSizerReportingUDPBroadcast->Add(m_ckboxUDPBroadcastEnable, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    wxStaticText* labelUDPBroadcastAddr = new wxStaticText(sbReporting, wxID_ANY, _("IP Address:"), wxDefaultPosition, wxDefaultSize, 0);
    sbSizerReportingUDPBroadcast->Add(labelUDPBroadcastAddr, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    m_udpBroadcastAddress = new wxTextCtrl(sbReporting, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(150, -1), 0);
    sbSizerReportingUDPBroadcast->Add(m_udpBroadcastAddress, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    wxStaticText* labelUDPBroadcastPort = new wxStaticText(sbReporting, wxID_ANY, _("Port:"), wxDefaultPosition, wxDefaultSize, 0);
    sbSizerReportingUDPBroadcast->Add(labelUDPBroadcastPort, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    m_udpBroadcastPort = new wxTextCtrl(sbReporting, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(60, -1), 0);
    sbSizerReportingUDPBroadcast->Add(m_udpBroadcastPort, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    sbSizerReportingRows->Add(sbSizerReportingUDPBroadcast, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);

    // CSV log file path
    wxBoxSizer* sbSizerCsvLog = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* labelCsvLogPath = new wxStaticText(sbReporting, wxID_ANY, _("Stations Heard Log File:"), wxDefaultPosition, wxDefaultSize, 0);
    sbSizerCsvLog->Add(labelCsvLogPath, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    m_txtCtrlCsvLogFilePath = new wxTextCtrl(sbReporting, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(200, -1), 0);
    sbSizerCsvLog->Add(m_txtCtrlCsvLogFilePath, 1, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    m_buttonChooseCsvLogFilePath = new wxButton(sbReporting, wxID_ANY, _("Choose"), wxDefaultPosition, wxSize(-1, -1), 0);
    m_buttonChooseCsvLogFilePath->SetMinSize(wxSize(120, -1));
    sbSizerCsvLog->Add(m_buttonChooseCsvLogFilePath, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    sbSizerReportingRows->Add(sbSizerCsvLog, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);

    sizerReporting->Add(sbSizerReportingRows, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    
    // FreeDV Reporter options that don't depend on Reporting checkboxes
    wxStaticBox* sbReportingFreeDV = new wxStaticBox(m_reportingTab, wxID_ANY, _("FreeDV Reporter"));
    wxStaticBoxSizer* sbSizerReportingFreeDVNoCall = new wxStaticBoxSizer(sbReportingFreeDV, wxHORIZONTAL);

    wxStaticText* labelFreeDVHostName = new wxStaticText(sbReportingFreeDV, wxID_ANY, _("Hostname:"), wxDefaultPosition, wxDefaultSize, 0);
    m_freedvReporterHostname = new wxTextCtrl(sbReportingFreeDV, wxID_ANY,  wxEmptyString, wxDefaultPosition, wxSize(250, -1), 0);
    sbSizerReportingFreeDVNoCall->Add(labelFreeDVHostName, 0,  static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    sbSizerReportingFreeDVNoCall->Add(m_freedvReporterHostname, 0,  static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_ckboxFreeDVReporterUseTls = new wxCheckBox(sbReportingFreeDV, wxID_ANY, _("Secure Connection"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizerReportingFreeDVNoCall->Add(m_ckboxFreeDVReporterUseTls, 0,  static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_useMetricDistances = new wxCheckBox(sbReportingFreeDV, wxID_ANY, _("Distances in km"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizerReportingFreeDVNoCall->Add(m_useMetricDistances, 0,  static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_useCardinalDirections = new wxCheckBox(sbReportingFreeDV, wxID_ANY, _("Show cardinal directions"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizerReportingFreeDVNoCall->Add(m_useCardinalDirections, 0,  static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_ckboxFreeDVReporterForceReceiveOnly = new wxCheckBox(sbReportingFreeDV, wxID_ANY, _("Force RX Only reporting"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizerReportingFreeDVNoCall->Add(m_ckboxFreeDVReporterForceReceiveOnly, 0,  static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    sizerReporting->Add(sbSizerReportingFreeDVNoCall, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);

    // Callsign list settings
    wxStaticBoxSizer* sbSizer_callsign_list;
    wxStaticBox* sb_callsignList = new wxStaticBox(m_reportingTab, wxID_ANY, _("Callsign List"));
    sbSizer_callsign_list = new wxStaticBoxSizer(sb_callsignList, wxHORIZONTAL);
    m_ckbox_use_utc_time = new wxCheckBox(sb_callsignList, wxID_ANY, _("Use UTC Time"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizer_callsign_list->Add(m_ckbox_use_utc_time, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    sizerReporting->Add(sbSizer_callsign_list,0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    
    m_reportingTab->SetSizer(sizerReporting);
    
    // Rig Control tab
    wxBoxSizer* sizerRigControl = new wxBoxSizer(wxVERTICAL);
    
    //------------------------------
    // Rig Control options
    //------------------------------
    
    wxStaticBoxSizer* sbSizer_ptt;
    wxStaticBox *sb_ptt = new wxStaticBox(m_rigControlTab, wxID_ANY, _("PTT Options"));
    sbSizer_ptt = new wxStaticBoxSizer(sb_ptt, wxVERTICAL);
    
    wxSizer* pttKeySizer = new wxBoxSizer(wxHORIZONTAL);
    m_ckboxEnableSpacebarForPTT = new wxCheckBox(sb_ptt, wxID_ANY, _("Enable key for PTT:"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    pttKeySizer->Add(m_ckboxEnableSpacebarForPTT, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_txtPTTKeyName = new wxTextCtrl(sb_ptt, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(120, -1), wxTE_READONLY | wxTE_PROCESS_ENTER);
    m_txtPTTKeyName->SetToolTip(_("The key currently assigned to PTT."));
    pttKeySizer->Add(m_txtPTTKeyName, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_btnSetPTTKey = new wxButton(sb_ptt, wxID_ANY, _("Change..."), wxDefaultPosition, wxDefaultSize);
    pttKeySizer->Add(m_btnSetPTTKey, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    sbSizer_ptt->Add(pttKeySizer, 0, static_cast<int>(wxALL), 0);

    m_ckboxPTTMomentaryMode = new wxCheckBox(sb_ptt, wxID_ANY, _("Momentary PTT (hold key to transmit)"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    m_ckboxPTTMomentaryMode->SetToolTip(_("When enabled, you must hold the PTT button or key to keep transmitting. Releasing it returns to receive."));
    sbSizer_ptt->Add(m_ckboxPTTMomentaryMode, 0, static_cast<int>(wxALL), 5);

    wxSizer* txRxDelaySizer = new wxBoxSizer(wxHORIZONTAL);

    auto txRxDelayLabel = new wxStaticText(sb_ptt, wxID_ANY, _("TX/RX Delay (milliseconds): "));
    txRxDelaySizer->Add(txRxDelayLabel, 0, static_cast<int>(wxALL) | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);

    m_txtTxRxDelayMilliseconds = new wxTextCtrl(sb_ptt, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(80,-1), 0, wxTextValidator(wxFILTER_DIGITS));
    m_txtTxRxDelayMilliseconds->SetToolTip(_("The amount of time to wait between toggling PTT and stopping/starting TX audio in milliseconds."));
    txRxDelaySizer->Add(m_txtTxRxDelayMilliseconds, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    sbSizer_ptt->Add(txRxDelaySizer, 0, static_cast<int>(wxALL), 0);

    wxSizer* totTimerSizer = new wxBoxSizer(wxHORIZONTAL);

    m_ckboxTOTTimerEnabled = new wxCheckBox(sb_ptt, wxID_ANY, _("Enable Time-Out Timer (TOT):"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    m_ckboxTOTTimerEnabled->SetToolTip(_("When enabled, FreeDV will automatically stop transmitting after the configured time period has elapsed."));
    totTimerSizer->Add(m_ckboxTOTTimerEnabled, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_txtTOTTimerSecs = new wxTextCtrl(sb_ptt, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(80, -1), 0, wxTextValidator(wxFILTER_DIGITS));
    m_txtTOTTimerSecs->SetToolTip(_("The number of seconds FreeDV will transmit before automatically dropping back to receive."));
    totTimerSizer->Add(m_txtTOTTimerSecs, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    auto totTimerSecsLabel = new wxStaticText(sb_ptt, wxID_ANY, _("seconds"));
    totTimerSizer->Add(totTimerSecsLabel, 0, static_cast<int>(wxALL) | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);

    sbSizer_ptt->Add(totTimerSizer, 0, static_cast<int>(wxALL), 0);

    m_ckboxTOTTimerEnabled->Connect(wxEVT_CHECKBOX, wxCommandEventHandler(OptionsDlg::OnTOTTimerEnable), NULL, this);

    sizerRigControl->Add(sbSizer_ptt,0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    
    wxStaticBoxSizer* sbSizer_hamlib;
    wxStaticBox *sb_hamlib = new wxStaticBox(m_rigControlTab, wxID_ANY, _("Frequency/Mode Control Options"));
    sbSizer_hamlib = new wxStaticBoxSizer(sb_hamlib, wxVERTICAL);
    
    wxSizer* freqModeSizer = new wxBoxSizer(wxHORIZONTAL);
    m_ckboxEnableFreqModeChanges = new wxRadioButton(sb_hamlib, wxID_ANY, _("Enable frequency and mode changes"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    freqModeSizer->Add(m_ckboxEnableFreqModeChanges, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);
    
    m_ckboxEnableFreqChangesOnly = new wxRadioButton(sb_hamlib, wxID_ANY, _("Enable frequency changes only"), wxDefaultPosition, wxDefaultSize);
    freqModeSizer->Add(m_ckboxEnableFreqChangesOnly, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);
    
    m_ckboxNoFreqModeChanges = new wxRadioButton(sb_hamlib, wxID_ANY, _("No frequency or mode changes"), wxDefaultPosition, wxDefaultSize);
    freqModeSizer->Add(m_ckboxNoFreqModeChanges, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);
    
    sbSizer_hamlib->Add(freqModeSizer, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);
    
    m_ckboxUseAnalogModes = new wxCheckBox(sb_hamlib, wxID_ANY, _("Use USB instead of DIGU"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizer_hamlib->Add(m_ckboxUseAnalogModes, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);

    m_ckboxRestoreLastFrequency = new wxCheckBox(sb_hamlib, wxID_ANY, _("Set the radio to the last used frequency on start"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    m_ckboxRestoreLastFrequency->SetToolTip(_("When unchecked, FreeDV leaves the radio on its current frequency when you push Start."));
    sbSizer_hamlib->Add(m_ckboxRestoreLastFrequency, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);
    
    m_ckboxFrequencyEntryAsKHz = new wxCheckBox(sb_hamlib, wxID_ANY, _("Frequency entry in kHz"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizer_hamlib->Add(m_ckboxFrequencyEntryAsKHz, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);

    sizerRigControl->Add(sbSizer_hamlib,0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    
    wxStaticBoxSizer* sbSizer_freqList;
    wxStaticBox *sb_freqList = new wxStaticBox(m_rigControlTab, wxID_ANY, _("Predefined Frequencies"));
    sbSizer_freqList = new wxStaticBoxSizer(sb_freqList, wxVERTICAL);
    
    wxGridBagSizer* gridSizer = new wxGridBagSizer(5, 5);
    
    m_freqList = new wxListBox(sb_freqList, wxID_ANY, wxDefaultPosition, wxSize(350,150), 0, NULL, wxLB_SINGLE | wxLB_NEEDED_SB);
    gridSizer->Add(m_freqList, wxGBPosition(0, 0), wxGBSpan(1, 2), static_cast<int>(wxEXPAND));

    // Add and Remove stacked at the top beside the list, rather than spread
    // over its height.
    const int FREQ_LIST_BUTTON_WIDTH = 100; 
    const int FREQ_LIST_BUTTON_HEIGHT = -1;
    wxBoxSizer* freqListButtons = new wxBoxSizer(wxVERTICAL);
    m_freqListAdd = new wxButton(sb_freqList, wxID_ANY, _("Add"), wxDefaultPosition, wxSize(FREQ_LIST_BUTTON_WIDTH,FREQ_LIST_BUTTON_HEIGHT), 0);
    freqListButtons->Add(m_freqListAdd, 0, static_cast<int>(wxEXPAND) | wxBOTTOM, 5);
    m_freqListRemove = new wxButton(sb_freqList, wxID_ANY, _("Remove"), wxDefaultPosition, wxSize(FREQ_LIST_BUTTON_WIDTH,FREQ_LIST_BUTTON_HEIGHT), 0);
    freqListButtons->Add(m_freqListRemove, 0, static_cast<int>(wxEXPAND));
    gridSizer->Add(freqListButtons, wxGBPosition(0, 2), wxDefaultSpan, wxALIGN_TOP);
    
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
    {
        m_labelEnterFreq = new wxStaticText(sb_freqList, wxID_ANY, _("Enter frequency (kHz):"), wxDefaultPosition, wxDefaultSize, 0);
    }
    else
    {
        m_labelEnterFreq = new wxStaticText(sb_freqList, wxID_ANY, _("Enter frequency (MHz):"), wxDefaultPosition, wxDefaultSize, 0);
    }
    gridSizer->Add(m_labelEnterFreq, wxGBPosition(1, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
    
    m_txtCtrlNewFrequency = new wxTextCtrl(sb_freqList, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    gridSizer->Add(m_txtCtrlNewFrequency, wxGBPosition(1, 1), wxGBSpan(1, 2), static_cast<int>(wxEXPAND));
    
    sbSizer_freqList->Add(gridSizer, 0, static_cast<int>(wxALL), 5);
    
    sizerRigControl->Add(sbSizer_freqList,0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    
    m_rigControlTab->SetSizer(sizerRigControl);
        
    // Display tab
    wxBoxSizer* sizerDisplay = new wxBoxSizer(wxVERTICAL);
    
    //----------------------------------------------------------
    // Waterfall color 
    //----------------------------------------------------------
    wxStaticBox* sb_waterfall = new wxStaticBox(m_displayTab, wxID_ANY, _("Waterfall Style"));
    wxStaticBoxSizer* sbSizer_waterfallColor =  new wxStaticBoxSizer(sb_waterfall, wxHORIZONTAL);
    
    m_waterfallColorScheme1 = new wxRadioButton(sb_waterfall, wxID_ANY, _("Multicolor"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    sbSizer_waterfallColor->Add(m_waterfallColorScheme1, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    m_waterfallColorScheme2 = new wxRadioButton(sb_waterfall, wxID_ANY, _("Black && White"), wxDefaultPosition, wxDefaultSize);
    sbSizer_waterfallColor->Add(m_waterfallColorScheme2, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    m_waterfallColorScheme3 = new wxRadioButton(sb_waterfall, wxID_ANY, _("Blue Tint"), wxDefaultPosition, wxDefaultSize);
    sbSizer_waterfallColor->Add(m_waterfallColorScheme3, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    sizerDisplay->Add(sbSizer_waterfallColor, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);

    //----------------------------------------------------------
    // FreeDV Reporter colors
    //----------------------------------------------------------
    wxStaticBox* sb_reporterColor = new wxStaticBox(m_displayTab, wxID_ANY, _("FreeDV Reporter colors"));
    wxStaticBoxSizer* sbSizer_reporterColor =  new wxStaticBoxSizer(sb_reporterColor, wxVERTICAL);

    wxFlexGridSizer* reporterColorSizer = new wxFlexGridSizer(5, wxSize(5, 5));

    // TX colors
    wxStaticText* labelReporterTxStation = new wxStaticText(sb_reporterColor, wxID_ANY, _("TX Stations:"), wxDefaultPosition, wxDefaultSize, 0);
    reporterColorSizer->Add(labelReporterTxStation, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    wxStaticText* labelReporterTxBackgroundColor = new wxStaticText(sb_reporterColor, wxID_ANY, _("Background"), wxDefaultPosition, wxDefaultSize, 0);
    reporterColorSizer->Add(labelReporterTxBackgroundColor, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_freedvReporterTxBackgroundColor = new wxColourPickerCtrl(sb_reporterColor, wxID_ANY);
    reporterColorSizer->Add(m_freedvReporterTxBackgroundColor, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    wxStaticText* labelReporterTxForegroundColor = new wxStaticText(sb_reporterColor, wxID_ANY, _("Foreground"), wxDefaultPosition, wxDefaultSize, 0);
    reporterColorSizer->Add(labelReporterTxForegroundColor, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_freedvReporterTxForegroundColor = new wxColourPickerCtrl(sb_reporterColor, wxID_ANY);
    reporterColorSizer->Add(m_freedvReporterTxForegroundColor, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    // RX colors
    wxStaticText* labelReporterRxStation = new wxStaticText(sb_reporterColor, wxID_ANY, _("RX Stations:"), wxDefaultPosition, wxDefaultSize, 0);
    reporterColorSizer->Add(labelReporterRxStation, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    wxStaticText* labelReporterRxBackgroundColor = new wxStaticText(sb_reporterColor, wxID_ANY, _("Background"), wxDefaultPosition, wxDefaultSize, 0);
    reporterColorSizer->Add(labelReporterRxBackgroundColor, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_freedvReporterRxBackgroundColor = new wxColourPickerCtrl(sb_reporterColor, wxID_ANY);
    reporterColorSizer->Add(m_freedvReporterRxBackgroundColor, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    wxStaticText* labelReporterRxForegroundColor = new wxStaticText(sb_reporterColor, wxID_ANY, _("Foreground"), wxDefaultPosition, wxDefaultSize, 0);
    reporterColorSizer->Add(labelReporterRxForegroundColor, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_freedvReporterRxForegroundColor = new wxColourPickerCtrl(sb_reporterColor, wxID_ANY);
    reporterColorSizer->Add(m_freedvReporterRxForegroundColor, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    // Message colors
    wxStaticText* labelReporterMsgStation = new wxStaticText(sb_reporterColor, wxID_ANY, _("Message updates:"), wxDefaultPosition, wxDefaultSize, 0);
    reporterColorSizer->Add(labelReporterMsgStation, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    wxStaticText* labelReporterMsgBackgroundColor = new wxStaticText(sb_reporterColor, wxID_ANY, _("Background"), wxDefaultPosition, wxDefaultSize, 0);
    reporterColorSizer->Add(labelReporterMsgBackgroundColor, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_freedvReporterMsgBackgroundColor = new wxColourPickerCtrl(sb_reporterColor, wxID_ANY);
    reporterColorSizer->Add(m_freedvReporterMsgBackgroundColor, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    wxStaticText* labelReporterMsgForegroundColor = new wxStaticText(sb_reporterColor, wxID_ANY, _("Foreground"), wxDefaultPosition, wxDefaultSize, 0);
    reporterColorSizer->Add(labelReporterMsgForegroundColor, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_freedvReporterMsgForegroundColor = new wxColourPickerCtrl(sb_reporterColor, wxID_ANY);
    reporterColorSizer->Add(m_freedvReporterMsgForegroundColor, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    sbSizer_reporterColor->Add(reporterColorSizer, 0, static_cast<int>(wxALL), 5);

    sizerDisplay->Add(sbSizer_reporterColor, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    
    // Plot settings
    wxStaticBox* sb_PlotSettings = new wxStaticBox(m_displayTab, wxID_ANY, _("Plot settings"));
    wxStaticBoxSizer* sbSizer_PlotSettings =  new wxStaticBoxSizer(sb_PlotSettings, wxVERTICAL);

    wxBoxSizer* spectrumPanelControlSizer = new wxBoxSizer(wxHORIZONTAL);
    
    wxStaticText* labelAveraging = new wxStaticText(sb_PlotSettings, wxID_ANY, _("Average spectrum plot across"), wxDefaultPosition, wxDefaultSize, 0);
    spectrumPanelControlSizer->Add(labelAveraging, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    wxString samplingChoices[] = {
        "1",
        "2",
        "3"
    };
    m_cbxNumSpectrumAveraging = new wxComboBox(sb_PlotSettings, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 3, samplingChoices, wxCB_DROPDOWN | wxCB_READONLY);
    m_cbxNumSpectrumAveraging->SetSelection(wxGetApp().appConfiguration.currentSpectrumAveraging);
    spectrumPanelControlSizer->Add(m_cbxNumSpectrumAveraging, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    wxStaticText* labelSamples = new wxStaticText(sb_PlotSettings, wxID_ANY, _("sample(s)"), wxDefaultPosition, wxDefaultSize, 0);
    spectrumPanelControlSizer->Add(labelSamples, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    sbSizer_PlotSettings->Add(spectrumPanelControlSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    sizerDisplay->Add(sbSizer_PlotSettings, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    
    m_displayTab->SetSizer(sizerDisplay);
    
    //----------------------------------------------------------------------
    // Receive Audio tab
    //----------------------------------------------------------------------
    {
        wxBoxSizer* sizerRxAudio = new wxBoxSizer(wxVERTICAL);

        // SC1 In
        sizerRxAudio->Add(new wxStaticText(m_rxAudioTab, wxID_ANY, _("Input To Computer From Radio:")), 0, wxLEFT | wxTOP, 5);
        {
            wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
            m_lcSoundCard1InDevice = new wxListCtrl(m_rxAudioTab, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_HRULES | wxLC_VRULES | wxLC_SINGLE_SEL);
            row->Add(m_lcSoundCard1InDevice, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 3);
            m_btnSoundCard1InTest = new wxButton(m_rxAudioTab, wxID_ANY, _("Test"));
            row->Add(m_btnSoundCard1InTest, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 3);
            sizerRxAudio->Add(row, 1, static_cast<int>(wxEXPAND) | wxLEFT | wxRIGHT, 2);
        }
        {
            wxBoxSizer* selRow = new wxBoxSizer(wxHORIZONTAL);
            selRow->Add(new wxStaticText(m_rxAudioTab, wxID_ANY, _("Selected:")), 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 3);
            m_tcSoundCard1InDevice = new wxTextCtrl(m_rxAudioTab, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
            selRow->Add(m_tcSoundCard1InDevice, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 3);
            sizerRxAudio->Add(selRow, 0, static_cast<int>(wxEXPAND) | wxLEFT | wxRIGHT, 2);
        }

        {
            wxPanel* divider = new wxPanel(m_rxAudioTab, wxID_ANY, wxDefaultPosition, wxSize(-1, 3));
            divider->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW));
            sizerRxAudio->Add(divider, 0, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 18);
        }

        // SC1 Out
        sizerRxAudio->Add(new wxStaticText(m_rxAudioTab, wxID_ANY, _("Output From Computer To Speaker/Headphones:")), 0, wxLEFT | wxTOP, 5);
        {
            wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
            m_lcSoundCard1OutDevice = new wxListCtrl(m_rxAudioTab, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_HRULES | wxLC_VRULES | wxLC_SINGLE_SEL);
            row->Add(m_lcSoundCard1OutDevice, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 3);
            m_btnSoundCard1OutTest = new wxButton(m_rxAudioTab, wxID_ANY, _("Test"));
            row->Add(m_btnSoundCard1OutTest, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 3);
            sizerRxAudio->Add(row, 1, static_cast<int>(wxEXPAND) | wxLEFT | wxRIGHT, 2);
        }
        {
            wxBoxSizer* selRow = new wxBoxSizer(wxHORIZONTAL);
            selRow->Add(new wxStaticText(m_rxAudioTab, wxID_ANY, _("Selected:")), 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 3);
            m_tcSoundCard1OutDevice = new wxTextCtrl(m_rxAudioTab, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
            selRow->Add(m_tcSoundCard1OutDevice, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 3);
            sizerRxAudio->Add(selRow, 0, static_cast<int>(wxEXPAND) | wxLEFT | wxRIGHT | wxBOTTOM, 2);
        }

        {
            wxBoxSizer* refreshRow = new wxBoxSizer(wxHORIZONTAL);
            refreshRow->AddStretchSpacer();
            m_btnRefreshRxAudio = new wxButton(m_rxAudioTab, wxID_ANY, _("Refresh Device List"));
            refreshRow->Add(m_btnRefreshRxAudio, 0, static_cast<int>(wxALL), 3);
            sizerRxAudio->Add(refreshRow, 0, static_cast<int>(wxEXPAND) | wxLEFT | wxRIGHT | wxBOTTOM, 2);
        }

        m_rxAudioTab->SetSizer(sizerRxAudio);
    }

    //----------------------------------------------------------------------
    // Transmit Audio tab
    //----------------------------------------------------------------------
    {
        wxBoxSizer* sizerTxAudio = new wxBoxSizer(wxVERTICAL);

        m_ckTxReceiveOnly = new wxCheckBox(m_txAudioTab, wxID_ANY, _("Receive only (I will not be transmitting)"));
        sizerTxAudio->Add(m_ckTxReceiveOnly, 0, wxALL, 5);

        // SC2 In
        sizerTxAudio->Add(new wxStaticText(m_txAudioTab, wxID_ANY, _("Input From Microphone To Computer:")), 0, wxLEFT | wxTOP, 5);
        {
            wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
            m_lcSoundCard2InDevice = new wxListCtrl(m_txAudioTab, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_HRULES | wxLC_VRULES | wxLC_SINGLE_SEL);
            row->Add(m_lcSoundCard2InDevice, 1, static_cast<int>(wxEXPAND) | wxALL, 3);
            m_btnSoundCard2InTest = new wxButton(m_txAudioTab, wxID_ANY, _("Test"));
            row->Add(m_btnSoundCard2InTest, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 3);
            sizerTxAudio->Add(row, 1, static_cast<int>(wxEXPAND) | wxLEFT | wxRIGHT, 2);
        }
        {
            wxBoxSizer* selRow = new wxBoxSizer(wxHORIZONTAL);
            selRow->Add(new wxStaticText(m_txAudioTab, wxID_ANY, _("Selected:")), 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 3);
            m_tcSoundCard2InDevice = new wxTextCtrl(m_txAudioTab, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
            selRow->Add(m_tcSoundCard2InDevice, 1, static_cast<int>(wxEXPAND) | wxALL, 3);
            sizerTxAudio->Add(selRow, 0, static_cast<int>(wxEXPAND) | wxLEFT | wxRIGHT, 2);
        }

        {
            wxPanel* divider = new wxPanel(m_txAudioTab, wxID_ANY, wxDefaultPosition, wxSize(-1, 3));
            divider->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW));
            sizerTxAudio->Add(divider, 0, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 18);
        }

        // SC2 Out
        sizerTxAudio->Add(new wxStaticText(m_txAudioTab, wxID_ANY, _("Output From Computer To Radio:")), 0, wxLEFT | wxTOP, 5);
        {
            wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
            m_lcSoundCard2OutDevice = new wxListCtrl(m_txAudioTab, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_HRULES | wxLC_VRULES | wxLC_SINGLE_SEL);
            row->Add(m_lcSoundCard2OutDevice, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 3);
            m_btnSoundCard2OutTest = new wxButton(m_txAudioTab, wxID_ANY, _("Test"));
            row->Add(m_btnSoundCard2OutTest, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 3);
            sizerTxAudio->Add(row, 1, static_cast<int>(wxEXPAND) | wxLEFT | wxRIGHT, 2);
        }
        {
            wxBoxSizer* selRow = new wxBoxSizer(wxHORIZONTAL);
            selRow->Add(new wxStaticText(m_txAudioTab, wxID_ANY, _("Selected:")), 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 3);
            m_tcSoundCard2OutDevice = new wxTextCtrl(m_txAudioTab, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
            selRow->Add(m_tcSoundCard2OutDevice, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 3);
            sizerTxAudio->Add(selRow, 0, static_cast<int>(wxEXPAND) | wxLEFT | wxRIGHT | wxBOTTOM, 2);
        }

        {
            wxBoxSizer* refreshRow = new wxBoxSizer(wxHORIZONTAL);
            refreshRow->AddStretchSpacer();
            m_btnRefreshTxAudio = new wxButton(m_txAudioTab, wxID_ANY, _("Refresh Device List"));
            refreshRow->Add(m_btnRefreshTxAudio, 0, static_cast<int>(wxALL), 3);
            sizerTxAudio->Add(refreshRow, 0, static_cast<int>(wxEXPAND) | wxLEFT | wxRIGHT | wxBOTTOM, 2);
        }

        m_txAudioTab->SetSizer(sizerTxAudio);
    }

    //----------------------------------------------------------------------
    // Voice Keyer tab
    //----------------------------------------------------------------------

    wxBoxSizer* sizerVoiceKeyer = new wxBoxSizer(wxVERTICAL);

    wxStaticBox* voiceKeyerBox = new wxStaticBox(m_voiceKeyerTab, wxID_ANY, _("Voice Keyer"));
    wxStaticBoxSizer* staticBoxSizer28a = new wxStaticBoxSizer(voiceKeyerBox, wxVERTICAL);

    wxBoxSizer* voiceKeyerSizer1 = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* voiceKeyerSizer2 = new wxBoxSizer(wxHORIZONTAL);

    wxStaticText *m_staticText28b = new wxStaticText(voiceKeyerBox, wxID_ANY, _("File location: "), wxDefaultPosition, wxDefaultSize, 0);
    voiceKeyerSizer1->Add(m_staticText28b, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_txtCtrlVoiceKeyerWaveFilePath = new wxTextCtrl(voiceKeyerBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(450,-1), 0);
    m_txtCtrlVoiceKeyerWaveFilePath->SetToolTip(_("Path to Voice Keyer audio files"));
    voiceKeyerSizer1->Add(m_txtCtrlVoiceKeyerWaveFilePath, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_buttonChooseVoiceKeyerWaveFilePath = new wxButton(voiceKeyerBox, wxID_APPLY, _("Choose"), wxDefaultPosition, wxSize(-1,-1), 0);
    m_buttonChooseVoiceKeyerWaveFilePath->SetMinSize(wxSize(120, -1));
    voiceKeyerSizer1->Add(m_buttonChooseVoiceKeyerWaveFilePath, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    wxStaticText *m_staticText28c = new wxStaticText(voiceKeyerBox, wxID_ANY, _("Rx Pause:"), wxDefaultPosition, wxDefaultSize, 0);
    voiceKeyerSizer2->Add(m_staticText28c, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_txtCtrlVoiceKeyerRxPause = new wxTextCtrl(voiceKeyerBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(50,-1), 0);
    m_txtCtrlVoiceKeyerRxPause->SetToolTip(_("How long to wait in Rx mode before repeat"));
    voiceKeyerSizer2->Add(m_txtCtrlVoiceKeyerRxPause, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    wxStaticText *m_staticText28d = new wxStaticText(voiceKeyerBox, wxID_ANY, _("Repeats:"), wxDefaultPosition, wxDefaultSize, 0);
    voiceKeyerSizer2->Add(m_staticText28d, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_txtCtrlVoiceKeyerRepeats = new wxTextCtrl(voiceKeyerBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(50,-1), 0);
    m_txtCtrlVoiceKeyerRepeats->SetToolTip(_("How long to wait in Rx mode before repeat"));
    voiceKeyerSizer2->Add(m_txtCtrlVoiceKeyerRepeats, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    staticBoxSizer28a->Add(voiceKeyerSizer1);
    staticBoxSizer28a->Add(voiceKeyerSizer2);

    sizerVoiceKeyer->Add(staticBoxSizer28a,0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    
    //------------------------------
    // Quick Record
    //------------------------------

    wxStaticBox* quickRecordBox = new wxStaticBox(m_voiceKeyerTab, wxID_ANY, _("Quick Record"));
    wxStaticBoxSizer* sbsQuickRecord = new wxStaticBoxSizer(quickRecordBox, wxVERTICAL);

    wxFlexGridSizer* quickRecordSizer = new wxFlexGridSizer(2, 3, 5, 5);
    quickRecordSizer->AddGrowableCol(1);

    wxStaticText *staticTextQRPath = new wxStaticText(quickRecordBox, wxID_ANY, _("Location to save raw recordings: "), wxDefaultPosition, wxDefaultSize, 0);
    quickRecordSizer->Add(staticTextQRPath, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_txtCtrlQuickRecordRawPath = new wxTextCtrl(quickRecordBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(450,-1), 0);
    m_txtCtrlQuickRecordRawPath->SetToolTip(_("Location which to save raw recordings started via the Record button in the main window."));
    quickRecordSizer->Add(m_txtCtrlQuickRecordRawPath, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_buttonChooseQuickRecordRawPath = new wxButton(quickRecordBox, wxID_APPLY, _("Choose"), wxDefaultPosition, wxSize(-1,-1), 0);
    m_buttonChooseQuickRecordRawPath->SetMinSize(wxSize(120, -1));
    quickRecordSizer->Add(m_buttonChooseQuickRecordRawPath, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    
    staticTextQRPath = new wxStaticText(quickRecordBox, wxID_ANY, _("Location to save decoded recordings: "), wxDefaultPosition, wxDefaultSize, 0);
    quickRecordSizer->Add(staticTextQRPath, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_txtCtrlQuickRecordDecodedPath = new wxTextCtrl(quickRecordBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(450,-1), 0);
    m_txtCtrlQuickRecordDecodedPath->SetToolTip(_("Location which to save decoded recordings started via the Record button in the main window."));
    quickRecordSizer->Add(m_txtCtrlQuickRecordDecodedPath, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_buttonChooseQuickRecordDecodedPath = new wxButton(quickRecordBox, wxID_APPLY, _("Choose"), wxDefaultPosition, wxSize(-1,-1), 0);
    m_buttonChooseQuickRecordDecodedPath->SetMinSize(wxSize(120, -1));
    quickRecordSizer->Add(m_buttonChooseQuickRecordDecodedPath, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    sbsQuickRecord->Add(quickRecordSizer);

    sizerVoiceKeyer->Add(sbsQuickRecord,0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);

    m_voiceKeyerTab->SetSizer(sizerVoiceKeyer);

    // Modem tab
    wxBoxSizer* sizerModem = new wxBoxSizer(wxVERTICAL);
    
    wxStaticBoxSizer* sbSizer_freedv700;
    wxStaticBox *sb_freedv700 = new wxStaticBox(m_modemTab, wxID_ANY, _("Modem Options"));
    sbSizer_freedv700 = new wxStaticBoxSizer(sb_freedv700, wxHORIZONTAL);

    m_ckboxAutoStartOnLaunch = new wxCheckBox(sb_freedv700, wxID_ANY, _("Start Automatically on Launch"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizer_freedv700->Add(m_ckboxAutoStartOnLaunch, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);

    sizerModem->Add(sbSizer_freedv700, 0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 5);

    //------------------------------
    // Half/Full duplex selection
    //------------------------------

    wxStaticBox *sb_duplex = new wxStaticBox(m_modemTab, wxID_ANY, _("Half/Full Duplex Operation"));
    wxStaticBoxSizer* sbSizer_duplex = new wxStaticBoxSizer(sb_duplex, wxHORIZONTAL);

    m_ckHalfDuplex = new wxCheckBox(sb_duplex, wxID_ANY, _("Half Duplex"), wxDefaultPosition, wxSize(-1,-1), 0);
    sbSizer_duplex->Add(m_ckHalfDuplex, 0, static_cast<int>(wxALL) | wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL, 5);

    sizerModem->Add(sbSizer_duplex,0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    
    wxStaticBox *sb_modemstats = new wxStaticBox(m_modemTab, wxID_ANY, _("Modem Statistics"));
    wxStaticBoxSizer* sbSizer_modemstats = new wxStaticBoxSizer(sb_modemstats, wxVERTICAL);

    m_showDecodeStats = new wxCheckBox(sb_modemstats, wxID_ANY, _("Show Decode Stats"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizer_modemstats->Add(m_showDecodeStats, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5); 
    
    wxBoxSizer* sbSizer_statsResetTime = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText *m_staticTextResetTime = new wxStaticText(sb_modemstats, wxID_ANY, _("Time before resetting stats (in seconds):"), wxDefaultPosition, wxDefaultSize, 0);
    sbSizer_statsResetTime->Add(m_staticTextResetTime, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_statsResetTime = new wxTextCtrl(sb_modemstats, wxID_ANY,  wxEmptyString, wxDefaultPosition, wxSize(50,-1), 0, wxTextValidator(wxFILTER_DIGITS));
    sbSizer_statsResetTime->Add(m_statsResetTime, 0, static_cast<int>(wxALL) | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);

    sbSizer_modemstats->Add(sbSizer_statsResetTime, 0, wxALIGN_LEFT, 0);
    
    sizerModem->Add(sbSizer_modemstats,0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
        
    m_modemTab->SetSizer(sizerModem);

    // Debug tab
    wxBoxSizer* sizerDebug = new wxBoxSizer(wxVERTICAL);
    
#ifdef __WXMSW__
    //------------------------------
    // debug console, for WIndows build make console pop up for debug messages
    //------------------------------

    wxStaticBoxSizer* sbSizer_console;
    wxStaticBox *sb_console = new wxStaticBox(m_debugTab, wxID_ANY, _("Debug: Windows"));
    sbSizer_console = new wxStaticBoxSizer(sb_console, wxHORIZONTAL);

    m_ckboxDebugConsole = new wxCheckBox(sb_console, wxID_ANY, _("Show Console"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizer_console->Add(m_ckboxDebugConsole, 0, wxALIGN_LEFT, 5);

    sizerDebug->Add(sbSizer_console,0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 5);

#endif // __WXMSW__
    
    //----------------------------------------------------------
    // FIFO and under/overflow counters used for debug
    //----------------------------------------------------------

    wxStaticBox* sb_fifo = new wxStaticBox(m_debugTab, wxID_ANY, _("Debug: FIFO and Under/Over Flow Counters"));
    wxStaticBoxSizer* sbSizer_fifo = new wxStaticBoxSizer(sb_fifo, wxVERTICAL);

    wxBoxSizer* sbSizer_fifo1 = new wxBoxSizer(wxHORIZONTAL);

    // FIFO size in ms

    wxStaticText *m_staticTextFifo1 = new wxStaticText(sb_fifo, wxID_ANY, _("Fifo Size (ms):"), wxDefaultPosition, wxDefaultSize, 0);
    sbSizer_fifo1->Add(m_staticTextFifo1, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);
    m_txtCtrlFifoSize = new wxTextCtrl(sb_fifo, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(80,-1), 0);
    sbSizer_fifo1->Add(m_txtCtrlFifoSize, 0, static_cast<int>(wxALL), 5);

    // Reset stats button
    
    m_BtnFifoReset = new wxButton(sb_fifo, wxID_ANY, _("Reset"), wxDefaultPosition, wxDefaultSize, 0);
    sbSizer_fifo1->Add(m_BtnFifoReset, 0,  wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);
    sbSizer_fifo->Add(sbSizer_fifo1);

    // text lines with fifo counters
    
    m_textPA1 = new wxStaticText(sb_fifo, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    sbSizer_fifo->Add(m_textPA1, 0, wxALIGN_LEFT, 1);
    m_textPA2 = new wxStaticText(sb_fifo, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    sbSizer_fifo->Add(m_textPA2, 0, wxALIGN_LEFT, 1);

    m_textFifos = new wxStaticText(sb_fifo, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    sbSizer_fifo->Add(m_textFifos, 0, wxALIGN_LEFT, 1);

    // 2nd line
    
    wxStaticBox* sb_fifo2 = new wxStaticBox(m_debugTab, wxID_ANY, _("Debug: Application Options"));
    wxStaticBoxSizer* sbSizer_fifo2 = new wxStaticBoxSizer(sb_fifo2, wxVERTICAL);

    wxBoxSizer* sbDebugOptionsSizer = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* sbDebugOptionsSizer2 = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* sbDebugOptionsSizer3 = new wxBoxSizer(wxHORIZONTAL);

    m_ckboxVerbose = new wxCheckBox(sb_fifo2, wxID_ANY, _("Verbose"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbDebugOptionsSizer->Add(m_ckboxVerbose, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);   
    m_ckboxTxRxThreadPriority = new wxCheckBox(sb_fifo2, wxID_ANY, wxT("txRxThreadPriority"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbDebugOptionsSizer->Add(m_ckboxTxRxThreadPriority, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);
    m_ckboxTxRxDumpTiming = new wxCheckBox(sb_fifo2, wxID_ANY, wxT("txRxDumpTiming"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbDebugOptionsSizer->Add(m_ckboxTxRxDumpTiming, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);
    
    m_ckboxTxRxDumpFifoState = new wxCheckBox(sb_fifo2, wxID_ANY, wxT("txRxDumpFifoState"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbDebugOptionsSizer2->Add(m_ckboxTxRxDumpFifoState, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);   
    m_ckboxFreeDVAPIVerbose = new wxCheckBox(sb_fifo2, wxID_ANY, wxT("APiVerbose"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbDebugOptionsSizer2->Add(m_ckboxFreeDVAPIVerbose, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);   
    
    m_experimentalFeatures = new wxCheckBox(sb_fifo2, wxID_ANY, _("Enable Experimental Features"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbDebugOptionsSizer3->Add(m_experimentalFeatures, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 5);   

    sbSizer_fifo2->Add(sbDebugOptionsSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND) | 0);
    sbSizer_fifo2->Add(sbDebugOptionsSizer2, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND) | 0);
    sbSizer_fifo2->Add(sbDebugOptionsSizer3, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND) | 0);

    sizerDebug->Add(sbSizer_fifo,0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 3);
    sizerDebug->Add(sbSizer_fifo2,0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 3);

    //------------------------------
    // Interfering tone
    //------------------------------

    wxStaticBoxSizer* sbSizer_tone;
    wxStaticBox *sb_tone = new wxStaticBox(m_debugTab, wxID_ANY, _("Simulated Interference Tone"));
    sbSizer_tone = new wxStaticBoxSizer(sb_tone, wxHORIZONTAL);

    m_ckboxTone = new wxCheckBox(sb_tone, wxID_ANY, _("Tone"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizer_tone->Add(m_ckboxTone, 0, static_cast<int>(wxALL) | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);

    wxStaticText *toneFreqLabel = new wxStaticText(sb_tone, wxID_ANY, _("Freq (Hz):"), wxDefaultPosition, wxDefaultSize, 0);
    sbSizer_tone->Add(toneFreqLabel, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_txtToneFreqHz = new wxTextCtrl(sb_tone, wxID_ANY,  "1000", wxDefaultPosition, wxSize(90,-1), 0, wxTextValidator(wxFILTER_DIGITS));
    sbSizer_tone->Add(m_txtToneFreqHz, 0, static_cast<int>(wxALL) | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);

    wxStaticText *m_staticTextta = new wxStaticText(sb_tone, wxID_ANY, _("Amplitude (pk): "), wxDefaultPosition, wxDefaultSize, 0);
    sbSizer_tone->Add(m_staticTextta, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_VERTICAL, 5);

    m_txtToneAmplitude = new wxTextCtrl(sb_tone, wxID_ANY,  "1000", wxDefaultPosition, wxSize(90,-1), 0, wxTextValidator(wxFILTER_DIGITS));
    sbSizer_tone->Add(m_txtToneAmplitude, 0, static_cast<int>(wxALL) | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);

    sizerDebug->Add(sbSizer_tone,0, static_cast<int>(wxALL)|wxEXPAND, 5);

    m_debugTab->SetSizer(sizerDebug);

    //------------------------------
    // OK - Cancel - Apply Buttons 
    //------------------------------

    wxBoxSizer* bSizer31 = new wxBoxSizer(wxHORIZONTAL);

    m_sdbSizer5OK = new wxButton(panel, wxID_OK);
    bSizer31->Add(m_sdbSizer5OK, 0, static_cast<int>(wxALL), 2);

    m_sdbSizer5Cancel = new wxButton(panel, wxID_CANCEL);
    bSizer31->Add(m_sdbSizer5Cancel, 0, static_cast<int>(wxALL), 2);

    m_sdbSizer5Apply = new wxButton(panel, wxID_APPLY);
    bSizer31->Add(m_sdbSizer5Apply, 0, static_cast<int>(wxALL), 2);

    bSizer30->Add(bSizer31, 0, static_cast<int>(wxALL) | wxALIGN_CENTER, 5);

    panel->SetSizer(bSizer30);
    
    wxBoxSizer* winSizer = new wxBoxSizer(wxVERTICAL);
    winSizer->Add(panel, 1, static_cast<int>(wxEXPAND));
    
    this->SetSizerAndFit(winSizer);
    this->Layout();
    this->Centre(wxBOTH);

    //-------------------
    // Tab ordering for accessibility
    //-------------------
#if 0
    m_ckboxReportingEnable->MoveBeforeInTabOrder(m_txt_callsign);
    m_txt_callsign->MoveBeforeInTabOrder(m_txt_grid_square);
    
    m_txt_grid_square->MoveBeforeInTabOrder(m_ckboxManualFrequencyReporting);
    m_ckboxManualFrequencyReporting->MoveBeforeInTabOrder(m_ckboxPskReporterEnable);
    m_ckboxPskReporterEnable->MoveBeforeInTabOrder(m_ckboxFreeDVReporterEnable);
    m_ckboxFreeDVReporterEnable->MoveBeforeInTabOrder(m_freedvReporterHostname);
    m_freedvReporterHostname->MoveBeforeInTabOrder(m_useMetricDistances);
    
    m_waterfallColorScheme1->MoveBeforeInTabOrder(m_waterfallColorScheme2);
    m_waterfallColorScheme2->MoveBeforeInTabOrder(m_waterfallColorScheme3);
    
    m_txtCtrlVoiceKeyerWaveFilePath->MoveBeforeInTabOrder(m_buttonChooseVoiceKeyerWaveFilePath);
    m_buttonChooseVoiceKeyerWaveFilePath->MoveBeforeInTabOrder(m_txtCtrlVoiceKeyerRxPause);
    m_txtCtrlVoiceKeyerRxPause->MoveBeforeInTabOrder(m_txtCtrlVoiceKeyerRepeats);
    
    m_ckboxSingleRxThread->MoveBeforeInTabOrder(m_statsResetTime);
    
    m_ckboxTone->MoveBeforeInTabOrder(m_txtToneFreqHz);
    m_txtToneFreqHz->MoveBeforeInTabOrder(m_txtToneAmplitude);
    
    // Tab ordering for Debug tab.    
#ifdef __WXMSW__
    sb_console->MoveBeforeInTabOrder(sb_fifo);
#endif // __WXMSW__
    sb_fifo->MoveBeforeInTabOrder(sb_fifo2);

    m_txtCtrlFifoSize->MoveBeforeInTabOrder(m_BtnFifoReset);
    m_BtnFifoReset->MoveBeforeInTabOrder(m_ckboxVerbose);

    m_ckboxVerbose->MoveBeforeInTabOrder(m_ckboxTxRxThreadPriority);
    m_ckboxTxRxThreadPriority->MoveBeforeInTabOrder(m_ckboxTxRxDumpTiming);
    m_ckboxTxRxDumpTiming->MoveBeforeInTabOrder(m_ckboxTxRxDumpFifoState);
    m_ckboxTxRxDumpFifoState->MoveBeforeInTabOrder(m_ckboxFreeDVAPIVerbose);

#ifdef __WXMSW__
    m_ckboxDebugConsole->MoveBeforeInTabOrder(m_txtCtrlFifoSize);
#endif // __WXMSW__
    
    m_rxAudioTab->MoveBeforeInTabOrder(m_txAudioTab);
    m_txAudioTab->MoveBeforeInTabOrder(m_reportingTab);
    m_reportingTab->MoveBeforeInTabOrder(m_voiceKeyerTab);
    m_voiceKeyerTab->MoveBeforeInTabOrder(m_rigControlTab);
    m_rigControlTab->MoveBeforeInTabOrder(m_displayTab);
    m_displayTab->MoveBeforeInTabOrder(m_modemTab);
    m_modemTab->MoveBeforeInTabOrder(m_debugTab);
    
    m_notebook->MoveBeforeInTabOrder(m_sdbSizer5OK);
    m_sdbSizer5OK->MoveBeforeInTabOrder(m_sdbSizer5Cancel);
    m_sdbSizer5Cancel->MoveBeforeInTabOrder(m_sdbSizer5Apply);
#endif
    
    // Connect Events -------------------------------------------------------

    this->Connect(wxEVT_INIT_DIALOG, wxInitDialogEventHandler(OptionsDlg::OnInitDialog));

    // Window names, used by GUI test scripts to find controls (see test/gui).
    SetName("settings");
    m_notebook->SetName("settings.notebook");
    m_ckHalfDuplex->SetName("settings.ckHalfDuplex");
    m_ckLeftChannelVoxTone->SetName("settings.ckLeftChannelVoxTone");
    m_ckUseHamlibPTT->SetName("settings.ckUseHamlibPTT");
    m_cbRigName->SetName("settings.cbRigName");
    m_cbSerialPort->SetName("settings.cbSerialPort");
    m_cbPttSerialPort->SetName("settings.cbPttSerialPort");
    m_cbSerialRate->SetName("settings.cbSerialRate");
    m_tcIcomCIVHex->SetName("settings.tcIcomCIVHex");
    m_cbPttMethod->SetName("settings.cbPttMethod");
    m_ckForceRTSOn->SetName("settings.ckForceRTSOn");
    m_ckForceDTROn->SetName("settings.ckForceDTROn");
    m_ckUseSerialPTT->SetName("settings.ckUseSerialPTT");
    m_cbCtlDevicePath->SetName("settings.cbCtlDevicePath");
    m_rbUseDTR->SetName("settings.rbUseDTR");
    m_ckRTSPos->SetName("settings.ckRTSPos");
    m_rbUseRTS->SetName("settings.rbUseRTS");
    m_ckDTRPos->SetName("settings.ckDTRPos");
    m_ckUsePTTInput->SetName("settings.ckUsePTTInput");
    m_ckCTSPos->SetName("settings.ckCTSPos");
    m_cbCtlDevicePathPttIn->SetName("settings.cbCtlDevicePathPttIn");
    m_buttonTestPTT->SetName("settings.buttonTestPTT");
    m_ckboxUseAnalogModes->SetName("settings.ckboxUseAnalogModes");
    m_ckboxRestoreLastFrequency->SetName("settings.ckboxRestoreLastFrequency");
    m_ckboxEnableFreqModeChanges->SetName("settings.ckboxEnableFreqModeChanges");
    m_ckboxEnableFreqChangesOnly->SetName("settings.ckboxEnableFreqChangesOnly");
    m_ckboxNoFreqModeChanges->SetName("settings.ckboxNoFreqModeChanges");
    m_ckboxEnableSpacebarForPTT->SetName("settings.ckboxEnableSpacebarForPTT");
    m_ckboxPTTMomentaryMode->SetName("settings.ckboxPTTMomentaryMode");
    m_txtPTTKeyName->SetName("settings.txtPTTKeyName");
    m_btnSetPTTKey->SetName("settings.btnSetPTTKey");
    m_txtTxRxDelayMilliseconds->SetName("settings.txtTxRxDelayMilliseconds");
    m_ckboxFrequencyEntryAsKHz->SetName("settings.ckboxFrequencyEntryAsKHz");
    m_ckboxTOTTimerEnabled->SetName("settings.ckboxTOTTimerEnabled");
    m_txtTOTTimerSecs->SetName("settings.txtTOTTimerSecs");
    m_waterfallColorScheme1->SetName("settings.waterfallColorScheme1");
    m_waterfallColorScheme2->SetName("settings.waterfallColorScheme2");
    m_waterfallColorScheme3->SetName("settings.waterfallColorScheme3");
    m_freedvReporterTxBackgroundColor->SetName("settings.freedvReporterTxBackgroundColor");
    m_freedvReporterTxForegroundColor->SetName("settings.freedvReporterTxForegroundColor");
    m_freedvReporterRxBackgroundColor->SetName("settings.freedvReporterRxBackgroundColor");
    m_freedvReporterRxForegroundColor->SetName("settings.freedvReporterRxForegroundColor");
    m_freedvReporterMsgBackgroundColor->SetName("settings.freedvReporterMsgBackgroundColor");
    m_freedvReporterMsgForegroundColor->SetName("settings.freedvReporterMsgForegroundColor");
    m_cbxNumSpectrumAveraging->SetName("settings.cbxNumSpectrumAveraging");
    m_buttonChooseVoiceKeyerWaveFilePath->SetName("settings.buttonChooseVoiceKeyerWaveFilePath");
    m_txtCtrlVoiceKeyerWaveFilePath->SetName("settings.txtCtrlVoiceKeyerWaveFilePath");
    m_txtCtrlVoiceKeyerRxPause->SetName("settings.txtCtrlVoiceKeyerRxPause");
    m_txtCtrlVoiceKeyerRepeats->SetName("settings.txtCtrlVoiceKeyerRepeats");
    m_buttonChooseQuickRecordRawPath->SetName("settings.buttonChooseQuickRecordRawPath");
    m_txtCtrlQuickRecordRawPath->SetName("settings.txtCtrlQuickRecordRawPath");
    m_buttonChooseQuickRecordDecodedPath->SetName("settings.buttonChooseQuickRecordDecodedPath");
    m_txtCtrlQuickRecordDecodedPath->SetName("settings.txtCtrlQuickRecordDecodedPath");
    m_lcSoundCard1InDevice->SetName("settings.lcSoundCard1InDevice");
    m_lcSoundCard1OutDevice->SetName("settings.lcSoundCard1OutDevice");
    m_lcSoundCard2InDevice->SetName("settings.lcSoundCard2InDevice");
    m_lcSoundCard2OutDevice->SetName("settings.lcSoundCard2OutDevice");
    m_tcSoundCard1InDevice->SetName("settings.tcSoundCard1InDevice");
    m_tcSoundCard1OutDevice->SetName("settings.tcSoundCard1OutDevice");
    m_tcSoundCard2InDevice->SetName("settings.tcSoundCard2InDevice");
    m_tcSoundCard2OutDevice->SetName("settings.tcSoundCard2OutDevice");
    m_btnSoundCard1InTest->SetName("settings.btnSoundCard1InTest");
    m_btnSoundCard1OutTest->SetName("settings.btnSoundCard1OutTest");
    m_btnSoundCard2InTest->SetName("settings.btnSoundCard2InTest");
    m_btnSoundCard2OutTest->SetName("settings.btnSoundCard2OutTest");
    m_btnRefreshRxAudio->SetName("settings.btnRefreshRxAudio");
    m_btnRefreshTxAudio->SetName("settings.btnRefreshTxAudio");
    m_ckTxReceiveOnly->SetName("settings.ckTxReceiveOnly");
    m_ckboxTone->SetName("settings.ckboxTone");
    m_txtToneFreqHz->SetName("settings.txtToneFreqHz");
    m_txtToneAmplitude->SetName("settings.txtToneAmplitude");
    m_ckboxAutoStartOnLaunch->SetName("settings.ckboxAutoStartOnLaunch");
    m_ckboxReportingEnable->SetName("settings.ckboxReportingEnable");
    m_txt_callsign->SetName("settings.txt_callsign");
    m_txt_grid_square->SetName("settings.txt_grid_square");
    m_ckboxManualFrequencyReporting->SetName("settings.ckboxManualFrequencyReporting");
    m_ckboxPskReporterEnable->SetName("settings.ckboxPskReporterEnable");
    m_ckboxFreeDVReporterEnable->SetName("settings.ckboxFreeDVReporterEnable");
    m_freedvReporterHostname->SetName("settings.freedvReporterHostname");
    m_ckboxFreeDVReporterUseTls->SetName("settings.ckboxFreeDVReporterUseTls");
    m_useMetricDistances->SetName("settings.useMetricDistances");
    m_useCardinalDirections->SetName("settings.useCardinalDirections");
    m_ckboxFreeDVReporterForceReceiveOnly->SetName("settings.ckboxFreeDVReporterForceReceiveOnly");
    m_ckboxUDPReportingEnable->SetName("settings.ckboxUDPReportingEnable");
    m_udpHostname->SetName("settings.udpHostname");
    m_udpPort->SetName("settings.udpPort");
    m_ckboxUDPBroadcastEnable->SetName("settings.ckboxUDPBroadcastEnable");
    m_udpBroadcastAddress->SetName("settings.udpBroadcastAddress");
    m_udpBroadcastPort->SetName("settings.udpBroadcastPort");
    m_txtCtrlCsvLogFilePath->SetName("settings.txtCtrlCsvLogFilePath");
    m_buttonChooseCsvLogFilePath->SetName("settings.buttonChooseCsvLogFilePath");
    m_BtnFifoReset->SetName("settings.BtnFifoReset");
    m_txtCtrlFifoSize->SetName("settings.txtCtrlFifoSize");
    m_ckboxTxRxThreadPriority->SetName("settings.ckboxTxRxThreadPriority");
    m_ckboxTxRxDumpTiming->SetName("settings.ckboxTxRxDumpTiming");
    m_ckboxTxRxDumpFifoState->SetName("settings.ckboxTxRxDumpFifoState");
    m_ckboxVerbose->SetName("settings.ckboxVerbose");
    m_ckboxFreeDVAPIVerbose->SetName("settings.ckboxFreeDVAPIVerbose");
    m_experimentalFeatures->SetName("settings.experimentalFeatures");
    m_showDecodeStats->SetName("settings.showDecodeStats");
    m_sdbSizer5OK->SetName("settings.sdbSizer5OK");
    m_sdbSizer5Cancel->SetName("settings.sdbSizer5Cancel");
    m_sdbSizer5Apply->SetName("settings.sdbSizer5Apply");
    m_statsResetTime->SetName("settings.statsResetTime");
    m_ckbox_use_utc_time->SetName("settings.ckbox_use_utc_time");
    m_freqList->SetName("settings.freqList");
    m_txtCtrlNewFrequency->SetName("settings.txtCtrlNewFrequency");
    m_freqListAdd->SetName("settings.freqListAdd");
    m_freqListRemove->SetName("settings.freqListRemove");
    m_textFifos->SetName("settings.textFifos");
    m_labelEnterFreq->SetName("settings.labelEnterFreq");
    m_stIcomCIVHex->SetName("settings.stIcomCIVHex");
#if defined(WIN32)
    m_ckboxDebugConsole->SetName("settings.ckboxDebugConsole");
    m_ckUseOmniRig->SetName("settings.ckUseOmniRig");
    m_cbOmniRigRigId->SetName("settings.cbOmniRigRigId");
#endif // defined(WIN32)

    m_sdbSizer5OK->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnOK), NULL, this);
    m_sdbSizer5Cancel->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnCancel), NULL, this);
    m_sdbSizer5Apply->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnApply), NULL, this);

#ifdef __WXMSW__
    m_ckboxDebugConsole->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxScrollEventHandler(OptionsDlg::OnDebugConsole), NULL, this);
#endif

    m_buttonChooseVoiceKeyerWaveFilePath->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnChooseVoiceKeyerWaveFilePath), NULL, this);

    m_buttonChooseQuickRecordRawPath->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnChooseQuickRecordPath), NULL, this);
    m_buttonChooseQuickRecordDecodedPath->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnChooseQuickRecordPath), NULL, this);

    m_buttonChooseCsvLogFilePath->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnChooseCsvLogFilePath), NULL, this);

    m_BtnFifoReset->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnFifoReset), NULL, this);

    m_ckboxReportingEnable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::OnReportingEnable), NULL, this);
    m_ckboxFreeDVReporterEnable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::OnReportingEnable), NULL, this);
    m_ckboxUDPReportingEnable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::OnReportingEnable), NULL, this);
    m_ckboxUDPBroadcastEnable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::OnReportingEnable), NULL, this);
    m_ckboxTone->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::OnToneStateEnable), NULL, this);
        
    m_ckboxEnableFreqModeChanges->Connect(wxEVT_RADIOBUTTON, wxCommandEventHandler(OptionsDlg::OnFreqModeChangeEnable), NULL, this);
    m_ckboxEnableFreqChangesOnly->Connect(wxEVT_RADIOBUTTON, wxCommandEventHandler(OptionsDlg::OnFreqModeChangeEnable), NULL, this);
    m_ckboxNoFreqModeChanges->Connect(wxEVT_RADIOBUTTON, wxCommandEventHandler(OptionsDlg::OnFreqModeChangeEnable), NULL, this);

    m_ckboxEnableSpacebarForPTT->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::OnEnableSpacebarForPTT), NULL, this);
    m_btnSetPTTKey->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnSetPTTKey), NULL, this);
    m_txtPTTKeyName->Bind(wxEVT_KEY_DOWN, &OptionsDlg::OnPTTKeyCapture, this);
    m_txtPTTKeyName->Bind(wxEVT_CHAR, &OptionsDlg::OnPTTKeyCapture, this);
    this->Bind(wxEVT_CHAR_HOOK, &OptionsDlg::OnDialogCharHook, this);
    
    m_freqList->Connect(wxEVT_LISTBOX, wxCommandEventHandler(OptionsDlg::OnReportingFreqSelectionChange), NULL, this);
    m_txtCtrlNewFrequency->Connect(wxEVT_TEXT, wxCommandEventHandler(OptionsDlg::OnReportingFreqTextChange), NULL, this);
    m_freqListAdd->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnReportingFreqAdd), NULL, this);
    m_freqListRemove->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnReportingFreqRemove), NULL, this);

    // Radio tab events
    m_ckUseHamlibPTT->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::PTTUseHamLibClicked), NULL, this);
    m_ckUseSerialPTT->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::PTTUseSerialClicked), NULL, this);
#if defined(WIN32)
    m_ckUseOmniRig->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::PTTUseOmniRigClicked), NULL, this);
#endif
    m_ckUsePTTInput->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::PTTUseSerialInputClicked), NULL, this);
    m_cbRigName->Connect(wxEVT_COMBOBOX, wxCommandEventHandler(OptionsDlg::HamlibRigNameChanged), NULL, this);
    m_buttonTestPTT->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnTestPTT), NULL, this);
    m_cbSerialPort->Connect(wxEVT_TEXT, wxCommandEventHandler(OptionsDlg::OnHamlibSerialPortChanged), NULL, this);
    m_cbPttMethod->Connect(wxEVT_TEXT, wxCommandEventHandler(OptionsDlg::OnHamlibPttMethodChanged), NULL, this);

    // Audio tab events
    m_btnSoundCard1InTest->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnSoundCard1InTest), NULL, this);
    m_btnSoundCard1OutTest->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnSoundCard1OutTest), NULL, this);
    m_btnSoundCard2InTest->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnSoundCard2InTest), NULL, this);
    m_btnSoundCard2OutTest->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnSoundCard2OutTest), NULL, this);
    m_lcSoundCard1InDevice->Connect(wxEVT_LIST_ITEM_SELECTED, wxListEventHandler(OptionsDlg::OnSoundCard1InDeviceSelect), NULL, this);
    m_lcSoundCard1OutDevice->Connect(wxEVT_LIST_ITEM_SELECTED, wxListEventHandler(OptionsDlg::OnSoundCard1OutDeviceSelect), NULL, this);
    m_lcSoundCard2InDevice->Connect(wxEVT_LIST_ITEM_SELECTED, wxListEventHandler(OptionsDlg::OnSoundCard2InDeviceSelect), NULL, this);
    m_lcSoundCard2OutDevice->Connect(wxEVT_LIST_ITEM_SELECTED, wxListEventHandler(OptionsDlg::OnSoundCard2OutDeviceSelect), NULL, this);
    m_ckTxReceiveOnly->Connect(wxEVT_CHECKBOX, wxCommandEventHandler(OptionsDlg::OnTxReceiveOnlyChanged), NULL, this);
    m_btnRefreshRxAudio->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnRefreshAudioDevices), NULL, this);
    m_btnRefreshTxAudio->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnRefreshAudioDevices), NULL, this);

    event_in_serial = 0;
    event_out_serial = 0;
}

//-------------------------------------------------------------------------
// ~OptionsDlg()
//-------------------------------------------------------------------------
OptionsDlg::~OptionsDlg()
{

    // Disconnect Events

    this->Disconnect(wxEVT_INIT_DIALOG, wxInitDialogEventHandler(OptionsDlg::OnInitDialog));

    m_sdbSizer5OK->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnOK), NULL, this);
    m_sdbSizer5Cancel->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnCancel), NULL, this);
    m_sdbSizer5Apply->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnApply), NULL, this);

    m_buttonChooseVoiceKeyerWaveFilePath->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnChooseVoiceKeyerWaveFilePath), NULL, this);
    m_buttonChooseQuickRecordRawPath->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnChooseQuickRecordPath), NULL, this);
    m_buttonChooseQuickRecordDecodedPath->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnChooseQuickRecordPath), NULL, this);
    m_buttonChooseCsvLogFilePath->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnChooseCsvLogFilePath), NULL, this);

    m_BtnFifoReset->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnFifoReset), NULL, this);

#ifdef __WXMSW__
    m_ckboxDebugConsole->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxScrollEventHandler(OptionsDlg::OnDebugConsole), NULL, this);
#endif
    
    m_ckboxReportingEnable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::OnReportingEnable), NULL, this);
    m_ckboxFreeDVReporterEnable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::OnReportingEnable), NULL, this);
    m_ckboxUDPReportingEnable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::OnReportingEnable), NULL, this);
    m_ckboxUDPBroadcastEnable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::OnReportingEnable), NULL, this);
    m_ckboxTone->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::OnToneStateEnable), NULL, this);
        
    m_ckboxEnableFreqModeChanges->Disconnect(wxEVT_RADIOBUTTON, wxCommandEventHandler(OptionsDlg::OnFreqModeChangeEnable), NULL, this);
    m_ckboxEnableFreqChangesOnly->Disconnect(wxEVT_RADIOBUTTON, wxCommandEventHandler(OptionsDlg::OnFreqModeChangeEnable), NULL, this);
    m_ckboxNoFreqModeChanges->Disconnect(wxEVT_RADIOBUTTON, wxCommandEventHandler(OptionsDlg::OnFreqModeChangeEnable), NULL, this);

    m_ckboxEnableSpacebarForPTT->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::OnEnableSpacebarForPTT), NULL, this);
    m_btnSetPTTKey->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnSetPTTKey), NULL, this);
    m_txtPTTKeyName->Unbind(wxEVT_KEY_DOWN, &OptionsDlg::OnPTTKeyCapture, this);
    m_txtPTTKeyName->Unbind(wxEVT_CHAR, &OptionsDlg::OnPTTKeyCapture, this);
    this->Unbind(wxEVT_CHAR_HOOK, &OptionsDlg::OnDialogCharHook, this);
    
    m_freqList->Disconnect(wxEVT_LISTBOX, wxCommandEventHandler(OptionsDlg::OnReportingFreqSelectionChange), NULL, this);
    m_txtCtrlNewFrequency->Disconnect(wxEVT_TEXT, wxCommandEventHandler(OptionsDlg::OnReportingFreqTextChange), NULL, this);
    m_freqListAdd->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnReportingFreqAdd), NULL, this);
    m_freqListRemove->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnReportingFreqRemove), NULL, this);

    // Radio tab events
    m_ckUseHamlibPTT->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::PTTUseHamLibClicked), NULL, this);
    m_ckUseSerialPTT->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::PTTUseSerialClicked), NULL, this);
    m_ckUsePTTInput->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(OptionsDlg::PTTUseSerialInputClicked), NULL, this);
    m_cbRigName->Disconnect(wxEVT_COMBOBOX, wxCommandEventHandler(OptionsDlg::HamlibRigNameChanged), NULL, this);
    m_buttonTestPTT->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnTestPTT), NULL, this);
    m_cbSerialPort->Disconnect(wxEVT_TEXT, wxCommandEventHandler(OptionsDlg::OnHamlibSerialPortChanged), NULL, this);
    m_cbPttMethod->Disconnect(wxEVT_TEXT, wxCommandEventHandler(OptionsDlg::OnHamlibPttMethodChanged), NULL, this);

    // Audio tab events
    m_btnSoundCard1InTest->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnSoundCard1InTest), NULL, this);
    m_btnSoundCard1OutTest->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnSoundCard1OutTest), NULL, this);
    m_btnSoundCard2InTest->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnSoundCard2InTest), NULL, this);
    m_btnSoundCard2OutTest->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnSoundCard2OutTest), NULL, this);
    m_lcSoundCard1InDevice->Disconnect(wxEVT_LIST_ITEM_SELECTED, wxListEventHandler(OptionsDlg::OnSoundCard1InDeviceSelect), NULL, this);
    m_lcSoundCard1OutDevice->Disconnect(wxEVT_LIST_ITEM_SELECTED, wxListEventHandler(OptionsDlg::OnSoundCard1OutDeviceSelect), NULL, this);
    m_lcSoundCard2InDevice->Disconnect(wxEVT_LIST_ITEM_SELECTED, wxListEventHandler(OptionsDlg::OnSoundCard2InDeviceSelect), NULL, this);
    m_lcSoundCard2OutDevice->Disconnect(wxEVT_LIST_ITEM_SELECTED, wxListEventHandler(OptionsDlg::OnSoundCard2OutDeviceSelect), NULL, this);
    m_ckTxReceiveOnly->Disconnect(wxEVT_CHECKBOX, wxCommandEventHandler(OptionsDlg::OnTxReceiveOnlyChanged), NULL, this);
    m_btnRefreshRxAudio->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnRefreshAudioDevices), NULL, this);
    m_btnRefreshTxAudio->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(OptionsDlg::OnRefreshAudioDevices), NULL, this);

    if (m_audioPlotThread != nullptr)
    {
        m_audioPlotThread->join();
        delete m_audioPlotThread;
        m_audioPlotThread = nullptr;
    }
}


//-------------------------------------------------------------------------
// ExchangeData()
//-------------------------------------------------------------------------
void OptionsDlg::ExchangeData(int inout, bool storePersistent)
{
    if(inout == EXCHANGE_DATA_IN)
    {
        // Radio tab
        populatePortList();
        m_ckLeftChannelVoxTone->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.leftChannelVoxTone);

        m_ckUseHamlibPTT->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.hamlibUseForPTT);
        m_cbRigName->SetSelection(wxGetApp().m_intHamlibRig);
        resetIcomCIVStatus();
        m_cbSerialPort->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialPort);
        m_cbPttSerialPort->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.hamlibPttSerialPort);

        {
            auto selectedRig = m_cbRigName->GetCurrentSelection();
            if (selectedRig >= 0)
            {
                auto minBaudRate = HamlibRigController::GetMinimumSerialBaudRate(selectedRig);
                auto maxBaudRate = HamlibRigController::GetMaximumSerialBaudRate(selectedRig);
                populateBaudRateList(minBaudRate, maxBaudRate);
            }
            else
            {
                populateBaudRateList();
            }
        }
        if (wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialRate == 0) {
            m_cbSerialRate->SetSelection(0);
        } else {
            m_cbSerialRate->SetValue(wxString::Format(wxT("%i"), wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialRate.get()));
        }

        m_tcIcomCIVHex->SetValue(wxString::Format(wxT("%02X"), wxGetApp().appConfiguration.rigControlConfiguration.hamlibIcomCIVAddress.get()));
        m_cbPttMethod->SetSelection((int)wxGetApp().appConfiguration.rigControlConfiguration.hamlibPTTType);
        m_ckForceRTSOn->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.hamlibForceRTSOn);
        m_ckForceDTROn->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.hamlibForceDTROn);

        m_ckUseSerialPTT->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.useSerialPTT);
        m_cbCtlDevicePath->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPort);
        m_rbUseRTS->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.serialPTTUseRTS);
        m_ckRTSPos->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPolarityRTS);
        m_rbUseDTR->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.serialPTTUseDTR);
        m_ckDTRPos->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPolarityDTR);

        m_ckUsePTTInput->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.useSerialPTTInput);
        m_cbCtlDevicePathPttIn->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.serialPTTInputPort);
        m_ckCTSPos->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.serialPTTInputPolarityCTS);

#if defined(WIN32)
        m_ckUseOmniRig->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.useOmniRig);
        m_cbOmniRigRigId->SetSelection(wxGetApp().appConfiguration.rigControlConfiguration.omniRigRigId);
#endif

        updateRadioControlState();

        // Populate FreeDV Reporter color settings
        wxColour rxBackgroundColor(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRxRowBackgroundColor);
        wxColour rxForegroundColor(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRxRowForegroundColor);
        wxColour txBackgroundColor(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterTxRowBackgroundColor);
        wxColour txForegroundColor(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterTxRowForegroundColor);
        wxColour msgBackgroundColor(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterMsgRowBackgroundColor);
        wxColour msgForegroundColor(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterMsgRowForegroundColor);

        m_freedvReporterRxBackgroundColor->SetColour(rxBackgroundColor);
        m_freedvReporterRxForegroundColor->SetColour(rxForegroundColor);
        m_freedvReporterTxBackgroundColor->SetColour(txBackgroundColor);
        m_freedvReporterTxForegroundColor->SetColour(txForegroundColor);
        m_freedvReporterMsgBackgroundColor->SetColour(msgBackgroundColor);
        m_freedvReporterMsgForegroundColor->SetColour(msgForegroundColor);

        // Populate reporting frequency list.
        for (auto& item : wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyList.get())
        {
            m_freqList->Append(item);
        }
        
        m_ckboxEnableSpacebarForPTT->SetValue(wxGetApp().appConfiguration.enableSpaceBarForPTT);
        m_ckboxPTTMomentaryMode->SetValue(wxGetApp().appConfiguration.pttMomentaryMode);
        m_selectedPTTKeyCode = wxGetApp().appConfiguration.pttKeyCode;
        m_capturingPTTKey = false;
        m_txtPTTKeyName->SetValue(getPTTKeyName(m_selectedPTTKeyCode));
        m_txtPTTKeyName->SetEditable(false);
        bool pttEnabled = wxGetApp().appConfiguration.enableSpaceBarForPTT;
        m_txtPTTKeyName->Enable(pttEnabled);
        m_btnSetPTTKey->Enable(pttEnabled);
        m_txtTxRxDelayMilliseconds->SetValue(wxString::Format("%d", wxGetApp().appConfiguration.txRxDelayMilliseconds.get()));

        m_ckboxTOTTimerEnabled->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.totTimerEnabled);
        m_txtTOTTimerSecs->SetValue(wxString::Format("%d", wxGetApp().appConfiguration.rigControlConfiguration.totTimerSecs.get()));
        m_txtTOTTimerSecs->Enable(wxGetApp().appConfiguration.rigControlConfiguration.totTimerEnabled);

        m_ckboxUseAnalogModes->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.hamlibUseAnalogModes);
        m_ckboxRestoreLastFrequency->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.hamlibRestoreLastFrequency);
        m_ckboxEnableFreqModeChanges->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges);
        m_ckboxEnableFreqChangesOnly->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqChangesOnly);
        m_ckboxNoFreqModeChanges->SetValue(!wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges && !wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqChangesOnly);
        m_ckboxFrequencyEntryAsKHz->SetValue(wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz);
        
        /* Plot settings */
        m_cbxNumSpectrumAveraging->SetSelection(wxGetApp().appConfiguration.currentSpectrumAveraging);
         
        /* Voice Keyer */

        m_txtCtrlVoiceKeyerWaveFilePath->SetValue(wxGetApp().appConfiguration.voiceKeyerWaveFilePath);
        m_txtCtrlVoiceKeyerRxPause->SetValue(wxString::Format(wxT("%i"), wxGetApp().appConfiguration.voiceKeyerRxPause.get()));
        m_txtCtrlVoiceKeyerRepeats->SetValue(wxString::Format(wxT("%i"), wxGetApp().appConfiguration.voiceKeyerRepeats.get()));

        m_txtCtrlQuickRecordRawPath->SetValue(wxGetApp().appConfiguration.quickRecordRawPath);
        m_txtCtrlQuickRecordDecodedPath->SetValue(wxGetApp().appConfiguration.quickRecordDecodedPath);

        // Audio device selection
        populateAudioDeviceList(m_lcSoundCard1InDevice,  IAudioEngine::AUDIO_ENGINE_IN);
        populateAudioDeviceList(m_lcSoundCard1OutDevice, IAudioEngine::AUDIO_ENGINE_OUT);
        populateAudioDeviceList(m_lcSoundCard2InDevice,  IAudioEngine::AUDIO_ENGINE_IN);
        populateAudioDeviceList(m_lcSoundCard2OutDevice, IAudioEngine::AUDIO_ENGINE_OUT);

        selectListDevice(m_lcSoundCard1InDevice, m_tcSoundCard1InDevice, wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName);
        selectListDevice(m_lcSoundCard2InDevice, m_tcSoundCard2InDevice, wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName);

        {
            wxString sc2in  = wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName;
            wxString sc2out = wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName;
            bool rxOnly = (sc2in.IsEmpty() || sc2in == "none") &&
                          (sc2out.IsEmpty() || sc2out == "none");
            m_ckTxReceiveOnly->SetValue(rxOnly);
            if (rxOnly)
            {
                // 1-card mode: SC1Out is speakers
                selectListDevice(m_lcSoundCard1OutDevice, m_tcSoundCard1OutDevice, wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName);
            }
            else
            {
                // 2-card mode: SC2Out is speakers, SC1Out is radio TX output
                selectListDevice(m_lcSoundCard1OutDevice, m_tcSoundCard1OutDevice, wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName);
                selectListDevice(m_lcSoundCard2OutDevice, m_tcSoundCard2OutDevice, wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName);
            }
            m_lcSoundCard2InDevice->Enable(!rxOnly);
            m_tcSoundCard2InDevice->Enable(!rxOnly);
            m_lcSoundCard2OutDevice->Enable(!rxOnly);
            m_tcSoundCard2OutDevice->Enable(!rxOnly);
            m_btnSoundCard2InTest->Enable(!rxOnly);
            m_btnSoundCard2OutTest->Enable(!rxOnly);
        }

        m_ckHalfDuplex->SetValue(wxGetApp().appConfiguration.halfDuplexMode);

        m_ckboxTone->SetValue(wxGetApp().m_tone);
        m_txtToneFreqHz->SetValue(wxString::Format(wxT("%i"),wxGetApp().m_tone_freq_hz.load(std::memory_order_acquire)));
        m_txtToneAmplitude->SetValue(wxString::Format(wxT("%i"),wxGetApp().m_tone_amplitude.load(std::memory_order_acquire)));

        m_txtCtrlFifoSize->SetValue(wxString::Format(wxT("%i"),wxGetApp().appConfiguration.fifoSizeMs.get()));

        m_ckboxTxRxThreadPriority->SetValue(wxGetApp().m_txRxThreadHighPriority);
        m_ckboxTxRxDumpTiming->SetValue(g_dump_timing.load(std::memory_order_relaxed));
        m_ckboxTxRxDumpFifoState->SetValue(g_dump_fifo_state.load(std::memory_order_relaxed));
        m_ckboxVerbose->SetValue(wxGetApp().appConfiguration.debugVerbose);
        m_ckboxFreeDVAPIVerbose->SetValue(g_freedv_verbose);
        m_showDecodeStats->SetValue(wxGetApp().appConfiguration.showDecodeStats);

        m_experimentalFeatures->SetValue(wxGetApp().appConfiguration.experimentalFeatures);

        m_ckboxAutoStartOnLaunch->SetValue(wxGetApp().appConfiguration.autoStartOnLaunch);

#ifdef __WXMSW__
        m_ckboxDebugConsole->SetValue(wxGetApp().appConfiguration.debugConsoleEnabled);
#endif
        
        // General reporting config
        m_ckboxReportingEnable->SetValue(wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled);
        m_txt_callsign->SetValue(wxGetApp().appConfiguration.reportingConfiguration.reportingCallsign);
        m_txt_grid_square->SetValue(wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare);
        m_ckboxManualFrequencyReporting->SetValue(wxGetApp().appConfiguration.reportingConfiguration.manualFrequencyReporting);
        
        // PSK Reporter options
        m_ckboxPskReporterEnable->SetValue(wxGetApp().appConfiguration.reportingConfiguration.pskReporterEnabled);
        
        // FreeDV Reporter options
        m_ckboxFreeDVReporterEnable->SetValue(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnabled);
        m_freedvReporterHostname->SetValue(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterHostname);
        m_ckboxFreeDVReporterUseTls->SetValue(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterUseTls);
        m_useMetricDistances->SetValue(wxGetApp().appConfiguration.reportingConfiguration.useMetricDistances);
        m_useCardinalDirections->SetValue(wxGetApp().appConfiguration.reportingConfiguration.reportingDirectionAsCardinal);
        m_ckboxFreeDVReporterForceReceiveOnly->SetValue(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterForceReceiveOnly);
        
        // UDP reporting options
        m_ckboxUDPReportingEnable->SetValue(wxGetApp().appConfiguration.reportingConfiguration.udpReportingEnabled);
        m_udpHostname->SetValue(wxGetApp().appConfiguration.reportingConfiguration.udpReportingHostname);
        m_udpPort->SetValue(wxString::Format(wxT("%i"), wxGetApp().appConfiguration.reportingConfiguration.udpReportingPort.get()));

        // UDP broadcast options (UdpReporter)
        m_ckboxUDPBroadcastEnable->SetValue(wxGetApp().appConfiguration.reportingConfiguration.udpBroadcastEnabled);
        m_udpBroadcastAddress->SetValue(wxGetApp().appConfiguration.reportingConfiguration.udpBroadcastAddress);
        m_udpBroadcastPort->SetValue(wxString::Format(wxT("%i"), wxGetApp().appConfiguration.reportingConfiguration.udpBroadcastPort.get()));

        // CSV log file path
        m_txtCtrlCsvLogFilePath->SetValue(wxGetApp().appConfiguration.reportingConfiguration.csvLogFilePath);

        // Callsign list config
        m_ckbox_use_utc_time->SetValue(wxGetApp().appConfiguration.reportingConfiguration.useUTCForReporting);
        
        // Stats reset time
        m_statsResetTime->SetValue(wxString::Format(wxT("%i"), wxGetApp().appConfiguration.statsResetTimeSecs.get()));
        
        // Waterfall color
        switch (wxGetApp().appConfiguration.waterfallColor)
        {
            case 1:
                m_waterfallColorScheme1->SetValue(false);
                m_waterfallColorScheme2->SetValue(true);
                m_waterfallColorScheme3->SetValue(false);
                break;
            case 2:
                m_waterfallColorScheme1->SetValue(false);
                m_waterfallColorScheme2->SetValue(false);
                m_waterfallColorScheme3->SetValue(true);
                break;
            case 0:
            default:
                m_waterfallColorScheme1->SetValue(true);
                m_waterfallColorScheme2->SetValue(false);
                m_waterfallColorScheme3->SetValue(false);
                break;
        };
        
        if (m_waterfallColorScheme1->GetValue())
        {
            wxGetApp().appConfiguration.waterfallColor = 0;
        }
        else if (m_waterfallColorScheme2->GetValue())
        {
            wxGetApp().appConfiguration.waterfallColor = 1;
        }
        else if (m_waterfallColorScheme3->GetValue())
        {
            wxGetApp().appConfiguration.waterfallColor = 2;
        }
        
        if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
        {
            m_labelEnterFreq->SetLabel(_("Enter frequency (kHz):"));
        }
        else
        {
            m_labelEnterFreq->SetLabel(_("Enter frequency (MHz):"));
        }
        
        // Update control state based on checkbox state.
        updateReportingState();
        updateToneState();
        updateRigControlState();

        wxCommandEvent tmpEvent;
        OnReportingFreqTextChange(tmpEvent);
    }

    if(inout == EXCHANGE_DATA_OUT)
    {
        // Populate FreeDV Reporter color settings
        wxColour rxBackgroundColor = m_freedvReporterRxBackgroundColor->GetColour();
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRxRowBackgroundColor = rxBackgroundColor.GetAsString(wxC2S_HTML_SYNTAX);

        wxColour rxForegroundColor = m_freedvReporterRxForegroundColor->GetColour();
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRxRowForegroundColor = rxForegroundColor.GetAsString(wxC2S_HTML_SYNTAX);

        wxColour txBackgroundColor = m_freedvReporterTxBackgroundColor->GetColour();
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterTxRowBackgroundColor = txBackgroundColor.GetAsString(wxC2S_HTML_SYNTAX);

        wxColour txForegroundColor = m_freedvReporterTxForegroundColor->GetColour();
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterTxRowForegroundColor = txForegroundColor.GetAsString(wxC2S_HTML_SYNTAX);

        wxColour msgBackgroundColor = m_freedvReporterMsgBackgroundColor->GetColour();
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterMsgRowBackgroundColor = msgBackgroundColor.GetAsString(wxC2S_HTML_SYNTAX);

        wxColour msgForegroundColor = m_freedvReporterMsgForegroundColor->GetColour();
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterMsgRowForegroundColor = msgForegroundColor.GetAsString(wxC2S_HTML_SYNTAX);

        // Save new reporting frequency list.
        std::vector<wxString> tmpList;
        tmpList.reserve(m_freqList->GetCount());
        for (unsigned int index = 0; index < m_freqList->GetCount(); index++)
        {
            tmpList.push_back(m_freqList->GetString(index));
        }
        wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyList = tmpList;
        
        wxGetApp().appConfiguration.enableSpaceBarForPTT = m_ckboxEnableSpacebarForPTT->GetValue();
        wxGetApp().appConfiguration.pttKeyCode = m_selectedPTTKeyCode;
        wxGetApp().appConfiguration.pttMomentaryMode = m_ckboxPTTMomentaryMode->GetValue();

        wxGetApp().appConfiguration.txRxDelayMilliseconds = wxAtoi(m_txtTxRxDelayMilliseconds->GetValue());

        wxGetApp().appConfiguration.rigControlConfiguration.totTimerEnabled = m_ckboxTOTTimerEnabled->GetValue();
        {
            long totSecs;
            m_txtTOTTimerSecs->GetValue().ToLong(&totSecs);
            if (totSecs < 1) totSecs = 1;
            wxGetApp().appConfiguration.rigControlConfiguration.totTimerSecs = (int)totSecs;
        }

        wxGetApp().appConfiguration.rigControlConfiguration.hamlibUseAnalogModes = m_ckboxUseAnalogModes->GetValue();
        wxGetApp().appConfiguration.rigControlConfiguration.hamlibRestoreLastFrequency = m_ckboxRestoreLastFrequency->GetValue();
        
        wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges = m_ckboxEnableFreqModeChanges->GetValue();
        wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqChangesOnly = m_ckboxEnableFreqChangesOnly->GetValue();
        
        wxGetApp().appConfiguration.halfDuplexMode = m_ckHalfDuplex->GetValue();
        
        /* Plot settings */
        wxGetApp().appConfiguration.currentSpectrumAveraging = m_cbxNumSpectrumAveraging->GetSelection();
        
        /* Voice Keyer */

        wxGetApp().appConfiguration.voiceKeyerWaveFilePath = m_txtCtrlVoiceKeyerWaveFilePath->GetValue();
        
        long tmp;
        m_txtCtrlVoiceKeyerRxPause->GetValue().ToLong(&tmp); if (tmp < 0) tmp = 0; wxGetApp().appConfiguration.voiceKeyerRxPause = (int)tmp;
        m_txtCtrlVoiceKeyerRepeats->GetValue().ToLong(&tmp);
        if (tmp < 0) {tmp = 0;} if (tmp > 100) {tmp = 100;}
        wxGetApp().appConfiguration.voiceKeyerRepeats = (int)tmp;
        
        wxGetApp().appConfiguration.quickRecordRawPath = m_txtCtrlQuickRecordRawPath->GetValue();
        wxGetApp().appConfiguration.quickRecordDecodedPath = m_txtCtrlQuickRecordDecodedPath->GetValue();

        // Audio device config — save names and look up default sample rates.
        // Mapping: 1-card (receive-only): SC1Out = speakers, SC2* = "none"
        //          2-card (RX+TX):        SC1Out = radio TX, SC2Out = speakers
        {
            auto audioEngine = AudioEngineFactory::GetAudioEngine();
            audioEngine->start();

            auto getSR = [&](const wxString& name, IAudioEngine::AudioDirection dir) -> int {
                if (name.IsEmpty() || name == "none") return 0;
                for (auto& dev : audioEngine->getAudioDeviceList(dir))
                    if (dev.name.IsSameAs(name)) return dev.defaultSampleRate;
                return 0;
            };

            wxString sc1in   = getSelectedAudioDeviceName(m_tcSoundCard1InDevice);
            wxString spk     = getSelectedAudioDeviceName(m_tcSoundCard1OutDevice); // "Output From Computer To Speaker/Headphones"
            wxString sc2in   = getSelectedAudioDeviceName(m_tcSoundCard2InDevice);
            wxString radioTx = getSelectedAudioDeviceName(m_tcSoundCard2OutDevice); // "Output From Computer To Radio"

            if (m_ckTxReceiveOnly->GetValue())
            {
                wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName  = sc1in;
                wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName = spk;
                wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName  = "none";
                wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName = "none";
                int r;
                r = getSR(sc1in, IAudioEngine::AUDIO_ENGINE_IN);  if (r > 0) wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate  = r;
                r = getSR(spk,   IAudioEngine::AUDIO_ENGINE_OUT); if (r > 0) wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate = r;
            }
            else
            {
                wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName  = sc1in;
                wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName = radioTx;
                wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName  = sc2in;
                wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName = spk;
                int r;
                r = getSR(sc1in,   IAudioEngine::AUDIO_ENGINE_IN);  if (r > 0) wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate  = r;
                r = getSR(radioTx, IAudioEngine::AUDIO_ENGINE_OUT); if (r > 0) wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate = r;
                r = getSR(sc2in,   IAudioEngine::AUDIO_ENGINE_IN);  if (r > 0) wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate  = r;
                r = getSR(spk,     IAudioEngine::AUDIO_ENGINE_OUT); if (r > 0) wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate = r;
            }

            audioEngine->stop();
        }

        wxGetApp().m_tone    = m_ckboxTone->GetValue();
        long tone_freq_hz, tone_amplitude;
        m_txtToneFreqHz->GetValue().ToLong(&tone_freq_hz);
        wxGetApp().m_tone_freq_hz = (int)tone_freq_hz;
        m_txtToneAmplitude->GetValue().ToLong(&tone_amplitude);
        wxGetApp().m_tone_amplitude = (int)tone_amplitude;

        long FifoSize_ms;
        m_txtCtrlFifoSize->GetValue().ToLong(&FifoSize_ms);
        wxGetApp().appConfiguration.fifoSizeMs = (int)FifoSize_ms;

        wxGetApp().m_txRxThreadHighPriority = m_ckboxTxRxThreadPriority->GetValue();
        g_dump_timing.store(m_ckboxTxRxDumpTiming->GetValue(), std::memory_order_relaxed);
        g_dump_fifo_state.store(m_ckboxTxRxDumpFifoState->GetValue(), std::memory_order_relaxed);
        wxGetApp().appConfiguration.debugVerbose = m_ckboxVerbose->GetValue();
        if (wxGetApp().appConfiguration.debugVerbose)
        {
            ulog_set_level(LOG_TRACE);
        }
        else
        {
            ulog_set_level(LOG_INFO);
        }
        g_freedv_verbose = m_ckboxFreeDVAPIVerbose->GetValue();

        wxGetApp().appConfiguration.showDecodeStats = m_showDecodeStats->GetValue();
        
        wxGetApp().appConfiguration.autoStartOnLaunch = m_ckboxAutoStartOnLaunch->GetValue();

#ifdef __WXMSW__
        wxGetApp().appConfiguration.debugConsoleEnabled = m_ckboxDebugConsole->GetValue();
#endif

        wxGetApp().appConfiguration.experimentalFeatures = m_experimentalFeatures->GetValue();

        // General reporting config
        wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled = m_ckboxReportingEnable->GetValue();
        wxGetApp().appConfiguration.reportingConfiguration.reportingCallsign = m_txt_callsign->GetValue();
        wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare = m_txt_grid_square->GetValue();
        wxGetApp().appConfiguration.reportingConfiguration.manualFrequencyReporting = m_ckboxManualFrequencyReporting->GetValue();
        
        // PSK Reporter options
        wxGetApp().appConfiguration.reportingConfiguration.pskReporterEnabled = m_ckboxPskReporterEnable->GetValue();
        
        // FreeDV Reporter options
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnabled = m_ckboxFreeDVReporterEnable->GetValue();
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterHostname = m_freedvReporterHostname->GetValue();
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterUseTls = m_ckboxFreeDVReporterUseTls->GetValue();
        wxGetApp().appConfiguration.reportingConfiguration.useMetricDistances = m_useMetricDistances->GetValue();
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterForceReceiveOnly = m_ckboxFreeDVReporterForceReceiveOnly->GetValue();
        wxGetApp().appConfiguration.reportingConfiguration.reportingDirectionAsCardinal = m_useCardinalDirections->GetValue();
        
        // UDP reporting options
        wxGetApp().appConfiguration.reportingConfiguration.udpReportingEnabled = m_ckboxUDPReportingEnable->GetValue();
        wxGetApp().appConfiguration.reportingConfiguration.udpReportingHostname = m_udpHostname->GetValue();

        long udpPort;
        m_udpPort->GetValue().ToLong(&udpPort);
        wxGetApp().appConfiguration.reportingConfiguration.udpReportingPort = (int)udpPort;

        // UDP broadcast options (UdpReporter)
        wxGetApp().appConfiguration.reportingConfiguration.udpBroadcastEnabled = m_ckboxUDPBroadcastEnable->GetValue();
        wxGetApp().appConfiguration.reportingConfiguration.udpBroadcastAddress = m_udpBroadcastAddress->GetValue();

        long udpBroadcastPort;
        m_udpBroadcastPort->GetValue().ToLong(&udpBroadcastPort);
        wxGetApp().appConfiguration.reportingConfiguration.udpBroadcastPort = (int)udpBroadcastPort;

        // CSV log file path
        wxGetApp().appConfiguration.reportingConfiguration.csvLogFilePath = m_txtCtrlCsvLogFilePath->GetValue();

        // Callsign list config
        wxGetApp().appConfiguration.reportingConfiguration.useUTCForReporting = m_ckbox_use_utc_time->GetValue();
        
        // Waterfall color
        if (m_waterfallColorScheme1->GetValue())
        {
            wxGetApp().appConfiguration.waterfallColor = 0;
        }
        else if (m_waterfallColorScheme2->GetValue())
        {
            wxGetApp().appConfiguration.waterfallColor = 1;
        }
        else if (m_waterfallColorScheme3->GetValue())
        {
            wxGetApp().appConfiguration.waterfallColor = 2;
        }
        
        // Stats reset time
        long resetTime;
        m_statsResetTime->GetValue().ToLong(&resetTime);
        wxGetApp().appConfiguration.statsResetTimeSecs = resetTime;
        
        // Radio tab settings
        wxGetApp().appConfiguration.rigControlConfiguration.leftChannelVoxTone = m_ckLeftChannelVoxTone->GetValue();

        wxGetApp().appConfiguration.rigControlConfiguration.hamlibUseForPTT = m_ckUseHamlibPTT->GetValue();
        wxGetApp().m_intHamlibRig = m_cbRigName->GetSelection();
        wxGetApp().appConfiguration.rigControlConfiguration.hamlibRigName =
            (wxGetApp().m_intHamlibRig >= 0) ? HamlibRigController::RigIndexToName(wxGetApp().m_intHamlibRig) : "";
        wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialPort = m_cbSerialPort->GetValue();
        wxGetApp().appConfiguration.rigControlConfiguration.hamlibPttSerialPort = m_cbPttSerialPort->GetValue();

        {
            long hexAddr = 0;
            m_tcIcomCIVHex->GetValue().ToLong(&hexAddr, 16);
            wxGetApp().appConfiguration.rigControlConfiguration.hamlibIcomCIVAddress = hexAddr;
        }
        {
            wxString rateStr = m_cbSerialRate->GetValue();
            if (rateStr == "default") {
                wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialRate = 0;
            } else {
                long rateTmp;
                rateStr.ToLong(&rateTmp);
                wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialRate = rateTmp;
            }
        }

        wxGetApp().appConfiguration.rigControlConfiguration.hamlibPTTType = m_cbPttMethod->GetSelection();
        wxGetApp().appConfiguration.rigControlConfiguration.hamlibForceRTSOn = m_ckForceRTSOn->GetValue();
        wxGetApp().appConfiguration.rigControlConfiguration.hamlibForceDTROn = m_ckForceDTROn->GetValue();

        wxGetApp().appConfiguration.rigControlConfiguration.useSerialPTT = m_ckUseSerialPTT->IsChecked();
        wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPort = m_cbCtlDevicePath->GetValue();
        wxGetApp().appConfiguration.rigControlConfiguration.serialPTTUseRTS = m_rbUseRTS->GetValue();
        wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPolarityRTS = m_ckRTSPos->IsChecked();
        wxGetApp().appConfiguration.rigControlConfiguration.serialPTTUseDTR = m_rbUseDTR->GetValue();
        wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPolarityDTR = m_ckDTRPos->IsChecked();

        wxGetApp().appConfiguration.rigControlConfiguration.useSerialPTTInput = m_ckUsePTTInput->IsChecked();
        wxGetApp().appConfiguration.rigControlConfiguration.serialPTTInputPort = m_cbCtlDevicePathPttIn->GetValue();
        wxGetApp().appConfiguration.rigControlConfiguration.serialPTTInputPolarityCTS = m_ckCTSPos->IsChecked();

#if defined(WIN32)
        wxGetApp().appConfiguration.rigControlConfiguration.useOmniRig = m_ckUseOmniRig->GetValue();
        wxGetApp().appConfiguration.rigControlConfiguration.omniRigRigId = m_cbOmniRigRigId->GetCurrentSelection();
#endif

        if (storePersistent) {
            wxGetApp().appConfiguration.apiVerbose = g_freedv_verbose;
            wxGetApp().appConfiguration.save(pConfig);
            
            // Save reporting frequency units last due to how the frequency list is stored.
            // Then reload the configuration to ensure that the displayed frequencies are correct.
            wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz = m_ckboxFrequencyEntryAsKHz->GetValue();
            wxGetApp().appConfiguration.save(pConfig);
            wxGetApp().appConfiguration.load(pConfig);
        }
    }
}

//-------------------------------------------------------------------------
// OnOK()
//-------------------------------------------------------------------------
void OptionsDlg::OnOK(wxCommandEvent&)
{
    ExchangeData(EXCHANGE_DATA_OUT, true);
    //this->EndModal(wxID_OK);
    EndModal(wxOK);
    
    // Clear frequency list to prevent sizing issues on re-display.
    // EXCHANGE_DATA_IN will repopulate then.
    m_freqList->Clear();
}

//-------------------------------------------------------------------------
// OnCancel()
//-------------------------------------------------------------------------
void OptionsDlg::OnCancel(wxCommandEvent&)
{
    //this->EndModal(wxID_CANCEL);
    EndModal(wxCANCEL);
    
    // Clear frequency list to prevent sizing issues on re-display.
    // EXCHANGE_DATA_IN will repopulate then.
    m_freqList->Clear();
}

//-------------------------------------------------------------------------
// OnApply()
//-------------------------------------------------------------------------
void OptionsDlg::OnApply(wxCommandEvent&)
{
    bool oldFreqAsKHz = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz;
    
    ExchangeData(EXCHANGE_DATA_OUT, true);
    
    bool khzChanged = 
        wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz != oldFreqAsKHz;
    
    // If there's something in the frequency entry textbox (i.e. if the user pushes Apply),
    // convert to the correct units.
    auto freqString = m_txtCtrlNewFrequency->GetValue();
    if (freqString.Length() > 0 && khzChanged)
    {
        double freqDouble = 0;
        wxNumberFormatter::FromString(freqString, &freqDouble);
        
        if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
        {
            freqDouble *= 1000;
            m_txtCtrlNewFrequency->SetValue(wxNumberFormatter::ToString(freqDouble, 1));
        }
        else
        {
            freqDouble /= 1000.0;
            m_txtCtrlNewFrequency->SetValue(wxNumberFormatter::ToString(freqDouble, 4));
        }
    }
        
    // Reload saved data to ensure the frequency list is properly displayed.
    m_freqList->Clear();
    ExchangeData(EXCHANGE_DATA_IN, false);
}

//-------------------------------------------------------------------------
// OnInitDialog()
//-------------------------------------------------------------------------
void OptionsDlg::OnInitDialog(wxInitDialogEvent&)
{
    ExchangeData(EXCHANGE_DATA_IN, false);
}

// immediately change flags rather using ExchangeData() so we can switch on and off at run time

void OptionsDlg::OnChooseVoiceKeyerWaveFilePath(wxCommandEvent&) {
    wxDirDialog pathDialog(
                                this,
                                _("Voice Keyer file location"),
                                wxGetApp().appConfiguration.voiceKeyerWaveFilePath
                                );
                                
    wxString path;
    if(ShowPathDialog(pathDialog, path) == wxID_CANCEL) {
        return;     // the user changed their mind...
    }
    
    m_txtCtrlVoiceKeyerWaveFilePath->SetValue(path);
}

void OptionsDlg::OnChooseQuickRecordPath(wxCommandEvent& event) {
    wxString defaultLocation = 
        (event.GetEventObject() == m_buttonChooseQuickRecordRawPath) ?
        wxGetApp().appConfiguration.quickRecordRawPath :
        wxGetApp().appConfiguration.quickRecordDecodedPath;
     wxDirDialog pathDialog(
                                 this,
                                 _("Choose Quick Record save location"),
                                 defaultLocation
                                 );
     wxString path;
     if(ShowPathDialog(pathDialog, path) == wxID_CANCEL) {
         return;     // the user changed their mind...
     }

     if (event.GetEventObject() == m_buttonChooseQuickRecordRawPath)
     {
        m_txtCtrlQuickRecordRawPath->SetValue(path);
     }
     else
     {
        m_txtCtrlQuickRecordDecodedPath->SetValue(path);
     }
}

void OptionsDlg::OnChooseCsvLogFilePath(wxCommandEvent&) {
    wxFileDialog fileDialog(
        this,
        _("Choose CSV log file location"),
        wxPathOnly(m_txtCtrlCsvLogFilePath->GetValue()),
        wxFileNameFromPath(m_txtCtrlCsvLogFilePath->GetValue()),
        _("CSV files (*.csv)") + wxT("|*.csv|") + _("All files (*.*)") + wxT("|*.*"),
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT
    );

    wxString path;
    if (ShowPathDialog(fileDialog, path) == wxID_CANCEL) {
        return;
    }

    m_txtCtrlCsvLogFilePath->SetValue(path);
}

void OptionsDlg::OnDebugConsole(wxScrollEvent&) {
    wxGetApp().appConfiguration.debugConsoleEnabled = m_ckboxDebugConsole->GetValue();
#ifdef __WXMSW__
    // somewhere to send printfs while developing, causes conmsole to pop up on Windows
    if (wxGetApp().appConfiguration.debugConsoleEnabled) {
        int ret = AllocConsole();
        freopen("CONOUT$", "w", stdout); 
        freopen("CONOUT$", "w", stderr); 
        log_info("AllocConsole: %d m_debug_console: %d", ret, wxGetApp().appConfiguration.debugConsoleEnabled.get());
    } 
#endif
}


void OptionsDlg::OnFifoReset(wxCommandEvent&)
{
    g_infifo1_full.store(0, std::memory_order_relaxed);
    g_outfifo1_empty.store(0, std::memory_order_relaxed);
    g_infifo2_full.store(0, std::memory_order_relaxed);
    g_outfifo2_empty.store(0, std::memory_order_relaxed);
    for (int i=0; i<4; i++) {
        g_AEstatus1[i].store(0, std::memory_order_relaxed);
        g_AEstatus2[i].store(0, std::memory_order_relaxed);
    }
}

void OptionsDlg::updateReportingState()
{    
    if (!sessionActive_)
    {
        m_ckbox_use_utc_time->Enable(true);
        m_ckboxReportingEnable->Enable(true);
        m_useMetricDistances->Enable(true);
        m_freedvReporterHostname->Enable(true);
        m_ckboxFreeDVReporterUseTls->Enable(true);

        if (m_ckboxReportingEnable->GetValue())
        {
            m_txt_callsign->Enable(true);
            m_txt_grid_square->Enable(true);
            m_ckboxManualFrequencyReporting->Enable(true);
            m_ckboxPskReporterEnable->Enable(true);
            m_ckboxFreeDVReporterEnable->Enable(true);
            m_ckboxFreeDVReporterForceReceiveOnly->Enable(true);
            m_useCardinalDirections->Enable(true);
            m_ckboxUDPReportingEnable->Enable(true);

            if (m_ckboxUDPReportingEnable->GetValue())
            {
                m_udpHostname->Enable(true);
                m_udpPort->Enable(true);
            }
            else
            {
                m_udpHostname->Enable(false);
                m_udpPort->Enable(false);
            }

            m_ckboxUDPBroadcastEnable->Enable(true);

            if (m_ckboxUDPBroadcastEnable->GetValue())
            {
                m_udpBroadcastAddress->Enable(true);
                m_udpBroadcastPort->Enable(true);
            }
            else
            {
                m_udpBroadcastAddress->Enable(false);
                m_udpBroadcastPort->Enable(false);
            }
        }
        else
        {
            m_txt_callsign->Enable(false);
            m_txt_grid_square->Enable(false);
            m_ckboxPskReporterEnable->Enable(false);
            m_ckboxFreeDVReporterEnable->Enable(false);
            m_ckboxManualFrequencyReporting->Enable(false);
            m_ckboxFreeDVReporterForceReceiveOnly->Enable(true);
            m_useCardinalDirections->Enable(true);
            m_udpHostname->Enable(false);
            m_udpPort->Enable(false);
            m_ckboxUDPReportingEnable->Enable(false);
            m_udpBroadcastAddress->Enable(false);
            m_udpBroadcastPort->Enable(false);
            m_ckboxUDPBroadcastEnable->Enable(false);
        }
    }
    else
    {
        // Txt Msg/Reporter options cannot be modified during a session.
        m_ckboxReportingEnable->Enable(false);
        m_txt_callsign->Enable(false);
        m_txt_grid_square->Enable(false);
        m_ckboxManualFrequencyReporting->Enable(false);
        m_ckboxPskReporterEnable->Enable(false);
        m_ckboxFreeDVReporterEnable->Enable(false);
        m_useMetricDistances->Enable(false);
        m_freedvReporterHostname->Enable(false);
        m_ckboxFreeDVReporterUseTls->Enable(false);
        m_ckboxFreeDVReporterForceReceiveOnly->Enable(false);
        m_useCardinalDirections->Enable(false);
        m_udpHostname->Enable(false);
        m_udpPort->Enable(false);
        m_ckboxUDPReportingEnable->Enable(false);
        m_udpBroadcastAddress->Enable(false);
        m_udpBroadcastPort->Enable(false);
        m_ckboxUDPBroadcastEnable->Enable(false);

        m_ckbox_use_utc_time->Enable(false);
    }
}

void OptionsDlg::updateToneState()
{
    m_txtToneFreqHz->Enable(m_ckboxTone->GetValue());
    m_txtToneAmplitude->Enable(m_ckboxTone->GetValue());
}

void OptionsDlg::updateRigControlState()
{
    if (!sessionActive_)
    {
        m_ckboxEnableFreqModeChanges->Enable(true);
        m_ckboxEnableFreqChangesOnly->Enable(true);
        m_ckboxNoFreqModeChanges->Enable(true);
        m_ckboxEnableSpacebarForPTT->Enable(true);
        m_txtTxRxDelayMilliseconds->Enable(true);
        m_ckboxUseAnalogModes->Enable(m_ckboxEnableFreqModeChanges->GetValue());
        m_ckboxRestoreLastFrequency->Enable(!m_ckboxNoFreqModeChanges->GetValue());
    }
    else
    {
        // Rig control settings cannot be updated during a session.
        m_ckboxUseAnalogModes->Enable(false);
        m_ckboxRestoreLastFrequency->Enable(false);
        m_ckboxEnableFreqModeChanges->Enable(false);
        m_ckboxEnableFreqChangesOnly->Enable(false);
        m_ckboxNoFreqModeChanges->Enable(false);
        m_ckboxEnableSpacebarForPTT->Enable(false);
        m_txtTxRxDelayMilliseconds->Enable(false);
    }
}
    
void OptionsDlg::OnReportingEnable(wxCommandEvent&)
{
    updateReportingState();
}

void OptionsDlg::OnToneStateEnable(wxCommandEvent&)
{
    updateToneState();
}

void OptionsDlg::OnFreqModeChangeEnable(wxCommandEvent&)
{
    updateRigControlState();
}

void OptionsDlg::OnEnableSpacebarForPTT(wxCommandEvent&)
{
    bool enabled = m_ckboxEnableSpacebarForPTT->GetValue();
    m_txtPTTKeyName->Enable(enabled);
    m_btnSetPTTKey->Enable(enabled);
    if (!enabled)
        exitPTTCaptureMode_(false);
}

void OptionsDlg::OnTOTTimerEnable(wxCommandEvent&)
{
    m_txtTOTTimerSecs->Enable(m_ckboxTOTTimerEnabled->GetValue());
}

void OptionsDlg::OnSetPTTKey(wxCommandEvent&)
{
    if (m_capturingPTTKey)
        exitPTTCaptureMode_(false);
    else
        enterPTTCaptureMode_();
}

void OptionsDlg::OnDialogCharHook(wxKeyEvent& event)
{
    // wxEVT_CHAR_HOOK reaches the dialog before wxEVT_KEY_DOWN reaches any child
    // control, so this is the only place to intercept Escape while in capture mode
    // — otherwise wxDialog's built-in handler closes the dialog first.
    if (m_capturingPTTKey && event.GetKeyCode() == WXK_ESCAPE)
    {
        exitPTTCaptureMode_(false);
        return; // consume — do not let the dialog treat Escape as Cancel
    }
    event.Skip();
}

void OptionsDlg::OnPTTKeyCapture(wxKeyEvent& event)
{
    if (!m_capturingPTTKey) { event.Skip(); return; }

    int keyCode = event.GetKeyCode();
    // Normalize lowercase letters to match wxEVT_KEY_DOWN uppercase convention.
    if (keyCode >= 'a' && keyCode <= 'z')
        keyCode -= ('a' - 'A');

    if (keyCode == WXK_TAB)
    {
        exitPTTCaptureMode_(false);
        event.Skip();
        return;
    }
    // Ignore bare modifier keys.
    if (keyCode == WXK_SHIFT || keyCode == WXK_CONTROL || keyCode == WXK_ALT ||
        keyCode == WXK_CAPITAL || keyCode == WXK_NUMLOCK || keyCode == WXK_SCROLL ||
        keyCode == WXK_NONE || keyCode == WXK_WINDOWS_LEFT || keyCode == WXK_WINDOWS_RIGHT ||
        keyCode == WXK_WINDOWS_MENU || keyCode == WXK_COMMAND)
    {
        return;
    }
    exitPTTCaptureMode_(true, keyCode);
    // Don't Skip() — prevents the key from typing into the text field.
}

void OptionsDlg::enterPTTCaptureMode_()
{
    m_capturingPTTKey = true;
    m_txtPTTKeyName->SetEditable(true);
    m_txtPTTKeyName->SetValue(_("Press any key..."));
    m_txtPTTKeyName->SetFocus();
    m_btnSetPTTKey->SetLabel(_("Cancel"));
}

void OptionsDlg::exitPTTCaptureMode_(bool accept, int keyCode)
{
    m_capturingPTTKey = false;
    if (accept)
        m_selectedPTTKeyCode = keyCode;
    m_txtPTTKeyName->SetValue(getPTTKeyName(m_selectedPTTKeyCode));
    m_txtPTTKeyName->SetEditable(false);
    m_btnSetPTTKey->SetLabel(_("Change..."));
}

void OptionsDlg::DisplayFifoPACounters() {
    if (IsShownOnScreen())
    {
        wxString fifo_counters = wxString::Format(wxT("Fifos: infull1: %d outempty1: %d infull2: %d outempty2: %d"), g_infifo1_full.load(std::memory_order_relaxed), g_outfifo1_empty.load(std::memory_order_relaxed), g_infifo2_full.load(std::memory_order_relaxed), g_outfifo2_empty.load(std::memory_order_relaxed));
        m_textFifos->SetLabel(fifo_counters);

        // input: underflow overflow output: underflow overflow
        wxString pa_counters_1 = wxString::Format(wxT("Audio1: inUnderflow: %d inOverflow: %d outUnderflow %d outOverflow %d"), g_AEstatus1[0].load(std::memory_order_relaxed), g_AEstatus1[1].load(std::memory_order_relaxed), g_AEstatus1[2].load(std::memory_order_relaxed), g_AEstatus1[3].load(std::memory_order_relaxed));
        m_textPA1->SetLabel(pa_counters_1);

        // input: underflow overflow output: underflow overflow
        wxString pa_counters_2 = wxString::Format(wxT("Audio2: inUnderflow: %d inOverflow: %d outUnderflow %d outOverflow %d"), g_AEstatus2[0].load(std::memory_order_relaxed), g_AEstatus2[1].load(std::memory_order_relaxed), g_AEstatus2[2].load(std::memory_order_relaxed), g_AEstatus2[3].load(std::memory_order_relaxed));
        m_textPA2->SetLabel(pa_counters_2);
    }
}

void OptionsDlg::OnReportingFreqSelectionChange(wxCommandEvent&)
{
    auto sel = m_freqList->GetSelection();
    if (sel >= 0)
    {
        m_txtCtrlNewFrequency->SetValue(m_freqList->GetString(sel));
    }
    else
    {
        m_txtCtrlNewFrequency->SetValue("");
    }
}

void OptionsDlg::OnReportingFreqTextChange(wxCommandEvent&)
{
    double tmpValue = 0.0;
    bool validNumber = wxNumberFormatter::FromString(m_txtCtrlNewFrequency->GetValue(), &tmpValue);

    auto idx = m_freqList->FindString(m_txtCtrlNewFrequency->GetValue());
    if (idx != wxNOT_FOUND)
    {
        m_freqListAdd->Enable(false);
        m_freqListRemove->Enable(true);
    }
    else if (validNumber && tmpValue > 0)
    {
        m_freqListAdd->Enable(true);
        m_freqListRemove->Enable(false);
    }
    else
    {
        m_freqListAdd->Enable(false);
        m_freqListRemove->Enable(false);
    }
}

void OptionsDlg::OnReportingFreqAdd(wxCommandEvent&)
{
    auto val = m_txtCtrlNewFrequency->GetValue();
    
    double dVal = 0;
    wxNumberFormatter::FromString(val, &dVal);
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
    {
        val = wxNumberFormatter::ToString(dVal, 1);
    }
    else
    {
        val = wxNumberFormatter::ToString(dVal, 4);
    }
    // Keep the list in ascending order: a new entry goes straight into its
    // place rather than at the end.
    auto strings = m_freqList->GetStrings();
    std::vector<wxString> list(strings.begin(), strings.end());
    m_freqList->Insert(val, ReportingConfiguration::SortedFrequencyIndex(list, dVal));
    m_txtCtrlNewFrequency->SetValue("");
}

void OptionsDlg::OnReportingFreqRemove(wxCommandEvent&)
{
    auto idx = m_freqList->FindString(m_txtCtrlNewFrequency->GetValue());
    if (idx >= 0)
    {
        m_freqList->Delete(idx);
    }
    m_txtCtrlNewFrequency->SetValue("");
}

//-------------------------------------------------------------------------
// Radio tab helper methods
//-------------------------------------------------------------------------

void OptionsDlg::populateBaudRateList(int min, int max)
{
    wxString serialRates[] = {"default", "300", "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200", "230400", "460800", "500000", "576000", "921600", "1000000", "1152000", "1500000", "2000000"};

    auto prevSelection = m_cbSerialRate->GetCurrentSelection();
    int oldBaud = 0;
    if (prevSelection > 0)
        oldBaud = wxAtoi(serialRates[prevSelection]);
    m_cbSerialRate->Clear();

    for (unsigned int i = 0; i < WXSIZEOF(serialRates); i++) {
        auto rateAsInt = wxAtoi(serialRates[i]);
        if (i > 0 && min > 0 && max > 0) {
            if (min > rateAsInt || rateAsInt > max)
                continue;
        }
        m_cbSerialRate->Append(serialRates[i]);
        if (rateAsInt == oldBaud)
            m_cbSerialRate->SetSelection(m_cbSerialRate->GetCount() - 1);
        else if (i == 0 && prevSelection == 0)
            m_cbSerialRate->SetSelection(0);
    }
}

void OptionsDlg::populatePortList()
{
    populateBaudRateList();
    m_cbSerialPort->Clear();
    m_cbCtlDevicePath->Clear();
    m_cbPttSerialPort->Clear();
    m_cbCtlDevicePathPttIn->Clear();

    std::vector<wxString> portList;

#ifdef __WXMSW__
    wxRegKey key(wxRegKey::HKLM, _T("HARDWARE\\DEVICEMAP\\SERIALCOMM"));
    if (key.Exists() && key.Open(wxRegKey::Read))
    {
        size_t subkeys, values;
        if (key.GetKeyInfo(&subkeys, NULL, &values, NULL) && key.HasValues())
        {
            wxString key_name;
            long el = 1;
            key.GetFirstValue(key_name, el);
            for (unsigned int i = 0; i < values; i++)
            {
                wxString key_data;
                key.QueryValue(key_name, key_data);
                portList.push_back(key_data);
                key.GetNextValue(key_name, el);
            }
        }
    }
#endif
#if defined(__WXGTK__) || defined(__WXOSX__)
#if defined(__FreeBSD__) || defined(__WXOSX__)
    glob_t gl;
#if defined(__FreeBSD__)
    if (glob("/dev/tty*", GLOB_MARK, NULL, &gl) == 0 ||
#else
    if (glob("/dev/tty.*", GLOB_MARK, NULL, &gl) == 0 || // NOLINT
#endif
        glob("/dev/cu.*", GLOB_MARK, NULL, &gl) == 0) { // NOLINT
        for (unsigned int i = 0; i < gl.gl_pathc; i++) {
            if (gl.gl_pathv[i][strlen(gl.gl_pathv[i])-1] == '/')
                continue;
#if defined(__FreeBSD__)
            if (gl.gl_pathv[i][8] >= 'l' && gl.gl_pathv[i][8] <= 's') continue;
            if (gl.gl_pathv[i][8] >= 'L' && gl.gl_pathv[i][8] <= 'S') continue;
            if (gl.gl_pathv[i][8] == 'v') continue;
#else
            if (!strcmp("/dev/cu.wlan-debug", gl.gl_pathv[i])) continue;
#endif
#ifndef __WXOSX__
            if (strchr(gl.gl_pathv[i], '.') != NULL) continue;
#endif
            portList.push_back(gl.gl_pathv[i]);
        }
        globfree(&gl);
    }
#else
    glob_t gl;
    if (glob("/sys/class/tty/*/device/driver", GLOB_MARK, NULL, &gl) == 0) // NOLINT
    {
        wxRegEx pathRegex("/sys/class/tty/([^/]+)");
        for (unsigned int i = 0; i < gl.gl_pathc; i++) {
            wxString path = gl.gl_pathv[i];
            if (pathRegex.Matches(path))
                portList.push_back("/dev/" + pathRegex.GetMatch(path, 1));
        }
        globfree(&gl);
    }
    if (glob("/dev/serial/by-id/*", GLOB_MARK, NULL, &gl) == 0) // NOLINT
    {
        for (unsigned int i = 0; i < gl.gl_pathc; i++)
            portList.push_back(gl.gl_pathv[i]);
        globfree(&gl);
    }
    if (glob("/dev/rfcomm*", GLOB_MARK, NULL, &gl) == 0) // NOLINT
    {
        for (unsigned int i = 0; i < gl.gl_pathc; i++)
            portList.push_back(gl.gl_pathv[i]);
        globfree(&gl);
    }
#endif
#endif

    std::sort(portList.begin(), portList.end(), [](const wxString& first, const wxString& second) {
        wxRegEx portRegex("^([^0-9]+)([0-9]+)$");
        wxString firstName, firstNumber, secondName, secondNumber;
        int firstNum = 0, secondNum = 0;
        if (portRegex.Matches(first)) {
            firstName = portRegex.GetMatch(first, 1);
            firstNumber = portRegex.GetMatch(first, 2);
            firstNum = atoi(firstNumber.c_str());
        } else { firstName = first; }
        if (portRegex.Matches(second)) {
            secondName = portRegex.GetMatch(second, 1);
            secondNumber = portRegex.GetMatch(second, 2);
            secondNum = atoi(secondNumber.c_str());
        } else { secondName = second; }
        return (firstName < secondName) || (firstName == secondName && firstNum < secondNum);
    });

    for (wxString& port : portList) {
        m_cbSerialPort->Append(port);
        m_cbPttSerialPort->Append(port);
        m_cbCtlDevicePath->Append(port);
        m_cbCtlDevicePathPttIn->Append(port);
    }
}

void OptionsDlg::resetIcomCIVStatus()
{
    auto curSel = m_cbRigName->GetCurrentSelection();
    std::string rigName = curSel >= 0 ? m_cbRigName->GetString(curSel).ToStdString() : "";
    if (rigName.find("Icom") == 0) {
        m_stIcomCIVHex->Show();
        m_tcIcomCIVHex->Show();
    } else {
        m_stIcomCIVHex->Hide();
        m_tcIcomCIVHex->Hide();
    }
    Layout();
}

void OptionsDlg::updateRadioControlState()
{
    m_ckLeftChannelVoxTone->Enable(!isTesting_);
    m_ckUseHamlibPTT->Enable(!isTesting_);
    m_ckUseSerialPTT->Enable(!isTesting_);
    m_ckUsePTTInput->Enable(!isTesting_);
#if defined(WIN32)
    m_ckUseOmniRig->Enable(!isTesting_);
#endif

    m_cbRigName->Enable(!isTesting_ && m_ckUseHamlibPTT->GetValue());
    m_cbSerialPort->Enable(!isTesting_ && m_ckUseHamlibPTT->GetValue());
    m_cbSerialRate->Enable(!isTesting_ && m_ckUseHamlibPTT->GetValue());
    m_tcIcomCIVHex->Enable(!isTesting_ && m_ckUseHamlibPTT->GetValue());
    m_cbPttMethod->Enable(!isTesting_ && m_ckUseHamlibPTT->GetValue());
    m_cbPttSerialPort->Enable(!isTesting_ && m_ckUseHamlibPTT->GetValue());

    m_cbCtlDevicePath->Enable(!isTesting_ && m_ckUseSerialPTT->GetValue());
    m_rbUseDTR->Enable(!isTesting_ && m_ckUseSerialPTT->GetValue());
    m_ckRTSPos->Enable(!isTesting_ && m_ckUseSerialPTT->GetValue());
    m_rbUseRTS->Enable(!isTesting_ && m_ckUseSerialPTT->GetValue());
    m_ckDTRPos->Enable(!isTesting_ && m_ckUseSerialPTT->GetValue());

    m_cbCtlDevicePathPttIn->Enable(!isTesting_ && m_ckUsePTTInput->GetValue());
    m_ckCTSPos->Enable(!isTesting_ && m_ckUsePTTInput->GetValue());

#if defined(WIN32)
    m_cbOmniRigRigId->Enable(!isTesting_ && m_ckUseOmniRig->GetValue());
#endif

    m_buttonTestPTT->Enable(!isTesting_ && (m_ckUseHamlibPTT->GetValue() || m_ckUseSerialPTT->GetValue()
#if defined(WIN32)
        || m_ckUseOmniRig->GetValue()
#endif
    ));
    m_sdbSizer5OK->Enable(!isTesting_);
    m_sdbSizer5Cancel->Enable(!isTesting_);
    m_sdbSizer5Apply->Enable(!isTesting_);

    // A separate serial device for PTT only applies when keying with RTS or DTR.
    if (m_cbPttMethod->GetSelection() != HamlibRigController::PTT_VIA_RTS && m_cbPttMethod->GetSelection() != HamlibRigController::PTT_VIA_DTR)
        m_cbPttSerialPort->Enable(false);
}

void OptionsDlg::PTTUseHamLibClicked(wxCommandEvent&)
{
    m_ckUseSerialPTT->SetValue(false);
#if defined(WIN32)
    m_ckUseOmniRig->SetValue(false);
#endif
    updateRadioControlState();
}

void OptionsDlg::PTTUseSerialClicked(wxCommandEvent&)
{
    m_ckUseHamlibPTT->SetValue(false);
    updateRadioControlState();
}

void OptionsDlg::PTTUseSerialInputClicked(wxCommandEvent&)
{
    updateRadioControlState();
}

#if defined(WIN32)
void OptionsDlg::PTTUseOmniRigClicked(wxCommandEvent&)
{
    m_ckUseHamlibPTT->SetValue(false);
    updateRadioControlState();
}
#endif

void OptionsDlg::HamlibRigNameChanged(wxCommandEvent&)
{
    resetIcomCIVStatus();
    auto selected = m_cbRigName->GetCurrentSelection();
    if (selected >= 0) {
        auto minBaud = HamlibRigController::GetMinimumSerialBaudRate(selected);
        auto maxBaud = HamlibRigController::GetMaximumSerialBaudRate(selected);
        populateBaudRateList(minBaud, maxBaud);
    } else {
        populateBaudRateList();
    }
}

void OptionsDlg::OnHamlibSerialPortChanged(wxCommandEvent&)
{
    if (m_cbPttMethod->GetSelection() == HamlibRigController::PTT_VIA_CAT)
        m_cbPttSerialPort->SetValue(m_cbSerialPort->GetValue());
}

void OptionsDlg::OnHamlibPttMethodChanged(wxCommandEvent&)
{
    updateRadioControlState();
}

void OptionsDlg::OnTestPTT(wxCommandEvent&)
{
    std::shared_ptr<std::mutex> mtx = std::make_shared<std::mutex>();
    std::shared_ptr<std::condition_variable> cv = std::make_shared<std::condition_variable>();

    // Says the test is over. Kept track of, rather than only signalled: a
    // port that can't be opened says so before the thread below has
    // started waiting for it, and that thread would otherwise wait forever.
    std::shared_ptr<bool> finished = std::make_shared<bool>(false);
    auto finish = [mtx, cv, finished]() {
        {
            std::unique_lock<std::mutex> lk(*mtx);
            *finished = true;
        }
        cv->notify_one();
    };

    if (m_ckLeftChannelVoxTone->GetValue()) {
        wxMessageBox(_("Testing of tone based PTT not supported; try PTT after pressing Start on main window"),
                     _("Error"), wxOK | wxICON_ERROR, this);
    }

    isTesting_ = true;
    updateRadioControlState();

    if (m_ckUseHamlibPTT->GetValue()) {
        int rig = m_cbRigName->GetSelection();
        wxString port = m_cbSerialPort->GetValue();
        wxString pttPort = m_cbPttSerialPort->GetValue();
        int serial_rate = 0;
        {
            wxString s = m_cbSerialRate->GetValue();
            if (s != "default") {
                long tmp;
                s.ToLong(&tmp);
                serial_rate = tmp;
            }
        }
        long hexAddress = 0;
        m_tcIcomCIVHex->GetValue().ToLong(&hexAddress, 16);
        auto pttType = (HamlibRigController::PttType)m_cbPttMethod->GetSelection();

        if (wxGetApp().CanAccessSerialPort((const char*)port.ToUTF8())) {
            std::shared_ptr<HamlibRigController> hamlib =
                std::make_shared<HamlibRigController>(
                    rig, (const char*)port.mb_str(wxConvUTF8), serial_rate, hexAddress, pttType,
                    (pttType == HamlibRigController::PTT_VIA_CAT) ? (const char*)port.mb_str(wxConvUTF8) : (const char*)pttPort.mb_str(wxConvUTF8),
                    false, false,
                    m_ckForceRTSOn->GetValue(),
                    m_ckForceDTROn->GetValue());

            hamlib->onRigError += [=, this](IRigController*, std::string const& error) {
                CallAfter([=, this]() {
                    // TRANSLATORS: %s is the error message returned by Hamlib.
                    wxMessageBox(wxString::Format(_("Couldn't connect to Radio with Hamlib (%s). Make sure the Hamlib serial Device, Rate, and Params match your radio"), error),
                        _("Error"), wxOK | wxICON_ERROR, this);
                    finish();
                });
            };
            hamlib->onRigConnected += [=](IRigController*) { hamlib->ptt(true); };
            hamlib->onPttChange += [=](IRigController*, bool state) {
                if (state) { std::this_thread::sleep_for(1s); hamlib->ptt(false); }
                else        { finish(); }
            };

            std::thread([=, this]() {
                hamlib->connect();
                std::unique_lock<std::mutex> lk(*mtx);
                cv->wait(lk, [finished]() { return *finished; });
                hamlib->disconnect();
                CallAfter([this]() { isTesting_ = false; updateRadioControlState(); });
            }).detach();
        } else {
            isTesting_ = false;
            updateRadioControlState();
        }
    } else if (m_ckUseSerialPTT->IsChecked()) {
        wxString ctrlport = m_cbCtlDevicePath->GetValue();
        if (wxGetApp().CanAccessSerialPort((const char*)ctrlport.ToUTF8())) {
            std::shared_ptr<SerialPortOutRigController> serialPort =
                std::make_shared<SerialPortOutRigController>(
                    (const char*)ctrlport.c_str(),
                    m_rbUseRTS->GetValue(), m_ckRTSPos->IsChecked(),
                    m_rbUseDTR->GetValue(), m_ckDTRPos->IsChecked());

            serialPort->onRigError += [=, this](IRigController*, std::string const& error) {
                CallAfter([=, this]() {
                    // TRANSLATORS: The first %s is the serial port and the second is the error message returned by the operating system.
                    wxMessageBox(wxString::Format(_("Couldn't open serial port %s (%s). This is likely due to not having permission to access the chosen port."), ctrlport, error),
                        _("Error"), wxOK | wxICON_ERROR, this);
                });
                finish();
            };
            serialPort->onRigConnected += [=](IRigController*) { finish(); };

            std::thread([=, this]() {
                serialPort->connect();
                std::unique_lock<std::mutex> lk(*mtx);
                cv->wait(lk, [finished]() { return *finished; });
                if (serialPort->isConnected()) {
                    serialPort->ptt(true);
                    wxSleep(1);
                    serialPort->ptt(false);
                    serialPort->disconnect();
                }
                CallAfter([this]() { isTesting_ = false; updateRadioControlState(); });
            }).detach();
        } else {
            isTesting_ = false;
            updateRadioControlState();
        }
    }
#if defined(WIN32)
    else if (m_ckUseOmniRig->IsChecked()) {
        std::shared_ptr<OmniRigController> omniRig =
            std::make_shared<OmniRigController>(m_cbOmniRigRigId->GetCurrentSelection());

        omniRig->onRigError += [=, this](IRigController*, std::string error) {
            CallAfter([=, this]() {
                // TRANSLATORS: %s is the error message returned by OmniRig.
                wxMessageBox(wxString::Format(_("Couldn't connect to Radio with OmniRig (%s). Make sure the rig ID and OmniRig configuration is correct."), error),
                    _("Error"), wxOK | wxICON_ERROR, this);
                finish();
            });
        };
        omniRig->onRigConnected += [=](IRigController*) { omniRig->ptt(true); };
        omniRig->onPttChange += [=](IRigController*, bool state) {
            if (state) { std::this_thread::sleep_for(1s); omniRig->ptt(false); }
            else        { finish(); }
        };

        std::thread([=, this]() {
            omniRig->connect();
            std::unique_lock<std::mutex> lk(*mtx);
            cv->wait(lk, [finished]() { return *finished; });
            omniRig->disconnect();
            CallAfter([this]() { isTesting_ = false; updateRadioControlState(); });
        }).detach();
    }
#endif
}

//-------------------------------------------------------------------------
// populateAudioDeviceList()
//-------------------------------------------------------------------------
void OptionsDlg::populateAudioDeviceList(wxListCtrl* list, IAudioEngine::AudioDirection direction)
{
    list->ClearAll();

    wxListItem col;
    col.SetAlign(wxLIST_FORMAT_LEFT);
    col.SetText(_("Device"));
    list->InsertColumn(0, col);

    col.SetAlign(wxLIST_FORMAT_LEFT);
    col.SetText(_("API"));
    list->InsertColumn(1, col);

    col.SetAlign(wxLIST_FORMAT_CENTRE);
    col.SetText(_("Default Sample Rate"));
    list->InsertColumn(2, col);

    auto engine = AudioEngineFactory::GetAudioEngine();
    engine->start();
    auto devList = engine->getAudioDeviceList(direction);
    engine->stop();

    auto& names = audioDeviceNames_[list];
    names.clear();
    for (auto& dev : devList)
    {
        long idx = list->InsertItem(list->GetItemCount(), dev.getDisplayName());
        list->SetItem(idx, 1, dev.apiName);
        list->SetItem(idx, 2, wxString::Format("%d Hz", dev.defaultSampleRate));
        names.push_back(dev.name);
    }
    // TRANSLATORS: Shown in the audio device lists to indicate that no device is selected.
    list->InsertItem(list->GetItemCount(), _("none"));
    names.push_back("none");

    list->SetColumnWidth(0, wxLIST_AUTOSIZE_USEHEADER);
    list->SetColumnWidth(1, wxLIST_AUTOSIZE_USEHEADER);
    list->SetColumnWidth(2, wxLIST_AUTOSIZE_USEHEADER);

    // Resize column 0 to use remaining space
    auto spaceUsed = list->GetColumnWidth(1) + list->GetColumnWidth(2);
    auto spaceAvailable = list->GetClientSize().GetWidth() - 50; // to ensure scrollbar doesn't appear
    auto spaceNeeded = spaceAvailable - spaceUsed;
    if (list->GetColumnWidth(0) < spaceNeeded)
    {
        list->SetColumnWidth(0, spaceNeeded);
    }
}

//-------------------------------------------------------------------------
// selectListDevice()
//-------------------------------------------------------------------------
void OptionsDlg::selectListDevice(wxListCtrl* list, wxTextCtrl* tc, const wxString& devName)
{
    wxString target = devName.IsEmpty() ? wxString("none") : devName;
    auto& names = audioDeviceNames_[list];
    for (long i = 0; i < list->GetItemCount() && i < (long)names.size(); i++)
    {
        if (names[i].IsSameAs(target))
        {
            list->SetItemState(i, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                  wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
            list->EnsureVisible(i);
            onAudioDeviceSelected(list, tc, i);
            return;
        }
    }
    tc->SetValue(wxEmptyString);
    tc->SetClientObject(nullptr);
}

//-------------------------------------------------------------------------
// onAudioDeviceSelected() - shows the user-friendly name of the selected
// device while retaining its internal name for saving to the config.
//-------------------------------------------------------------------------
void OptionsDlg::onAudioDeviceSelected(wxListCtrl* list, wxTextCtrl* tc, long index)
{
    auto& names = audioDeviceNames_[list];
    tc->SetValue(list->GetItemText(index, 0));
    tc->SetClientObject(index < (long)names.size() ? new wxStringClientData(names[index]) : nullptr);
}

wxString OptionsDlg::getSelectedAudioDeviceName(wxTextCtrl* tc)
{
    auto data = static_cast<wxStringClientData*>(tc->GetClientObject());
    return data != nullptr ? data->GetData() : wxString(wxEmptyString);
}

void OptionsDlg::OnSoundCard1InDeviceSelect(wxListEvent& evt)
{
    onAudioDeviceSelected(m_lcSoundCard1InDevice, m_tcSoundCard1InDevice, evt.GetIndex());
}

void OptionsDlg::OnSoundCard1OutDeviceSelect(wxListEvent& evt)
{
    onAudioDeviceSelected(m_lcSoundCard1OutDevice, m_tcSoundCard1OutDevice, evt.GetIndex());
}

void OptionsDlg::OnSoundCard2InDeviceSelect(wxListEvent& evt)
{
    onAudioDeviceSelected(m_lcSoundCard2InDevice, m_tcSoundCard2InDevice, evt.GetIndex());
}

void OptionsDlg::OnSoundCard2OutDeviceSelect(wxListEvent& evt)
{
    onAudioDeviceSelected(m_lcSoundCard2OutDevice, m_tcSoundCard2OutDevice, evt.GetIndex());
}

void OptionsDlg::OnTxReceiveOnlyChanged(wxCommandEvent&)
{
    bool rxOnly = m_ckTxReceiveOnly->GetValue();
    m_lcSoundCard2InDevice->Enable(!rxOnly);
    m_tcSoundCard2InDevice->Enable(!rxOnly);
    m_lcSoundCard2OutDevice->Enable(!rxOnly);
    m_tcSoundCard2OutDevice->Enable(!rxOnly);
    m_btnSoundCard2InTest->Enable(!rxOnly);
    m_btnSoundCard2OutTest->Enable(!rxOnly);
}

void OptionsDlg::OnRefreshAudioDevices(wxCommandEvent&)
{
    // Preserve the user's current selections so they survive the repopulation
    wxString sel1In  = getSelectedAudioDeviceName(m_tcSoundCard1InDevice);
    wxString sel1Out = getSelectedAudioDeviceName(m_tcSoundCard1OutDevice);
    wxString sel2In  = getSelectedAudioDeviceName(m_tcSoundCard2InDevice);
    wxString sel2Out = getSelectedAudioDeviceName(m_tcSoundCard2OutDevice);

    // Restart engine so it re-enumerates available devices
    auto engine = AudioEngineFactory::GetAudioEngine();
    engine->stop();
    engine->start();

    // Repopulate all four lists with fresh device data
    populateAudioDeviceList(m_lcSoundCard1InDevice,  IAudioEngine::AUDIO_ENGINE_IN);
    populateAudioDeviceList(m_lcSoundCard1OutDevice, IAudioEngine::AUDIO_ENGINE_OUT);
    populateAudioDeviceList(m_lcSoundCard2InDevice,  IAudioEngine::AUDIO_ENGINE_IN);
    populateAudioDeviceList(m_lcSoundCard2OutDevice, IAudioEngine::AUDIO_ENGINE_OUT);

    // Reselect previously chosen devices (falls back to empty if device disappeared)
    selectListDevice(m_lcSoundCard1InDevice,  m_tcSoundCard1InDevice,  sel1In);
    selectListDevice(m_lcSoundCard1OutDevice, m_tcSoundCard1OutDevice, sel1Out);
    selectListDevice(m_lcSoundCard2InDevice,  m_tcSoundCard2InDevice,  sel2In);
    selectListDevice(m_lcSoundCard2OutDevice, m_tcSoundCard2OutDevice, sel2Out);
}

//-------------------------------------------------------------------------
// disableAllAudioTestButtons() / enableAllAudioTestButtons() - helpers
//-------------------------------------------------------------------------
static void setAudioTestButtonsEnabled(bool enabled,
    wxButton* b1in, wxButton* b1out, wxButton* b2in, wxButton* b2out)
{
    b1in->Enable(enabled);
    b1out->Enable(enabled);
    b2in->Enable(enabled);
    b2out->Enable(enabled);
}

//-------------------------------------------------------------------------
// testAudioOutput() - plays a 400 Hz sine wave on the selected output device
//-------------------------------------------------------------------------
void OptionsDlg::testAudioOutput(const wxString& devName, wxButton* btn)
{
    if (devName.IsEmpty() || devName == "none")
    {
        wxMessageBox(_("Please select an audio output device first."), _("Audio Test"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    setAudioTestButtonsEnabled(false, m_btnSoundCard1InTest, m_btnSoundCard1OutTest, m_btnSoundCard2InTest, m_btnSoundCard2OutTest);
    btn->SetLabel(_("Playing"));

    m_audioPlotThread = new std::thread([&](wxString const& devName, wxButton* btn) {
        auto engine = AudioEngineFactory::GetAudioEngine();
        engine->start();
        auto devList = engine->getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_OUT);
        for (auto& devInfo : devList)
        {
            if (devInfo.name.IsSameAs(devName))
            {
                int n = 0;
                auto device = engine->getAudioDevice(devInfo.name, IAudioEngine::AUDIO_ENGINE_OUT, devInfo.defaultSampleRate, 1);
                if (device)
                {
                    device->setOnAudioData([](IAudioDevice& dev, void* data, size_t numSamples, void* state) FREEDV_NONBLOCKING {
                        int* np = static_cast<int*>(state);
                        if (data != nullptr)
                        {
                            short* out = static_cast<short*>(data);
                            for (size_t j = 0; j < numSamples; j++, (*np)++)
                            {
                                out[j] = (short)(2000.0 * cos(6.2832 * (*np) * 400.0 / dev.getSampleRate()));
                            }
                        }
                    }, &n);
                    device->setDescription("Options Audio Output Test");
                    device->start();
                    std::this_thread::sleep_for(std::chrono::seconds(OPTIONS_AUDIO_TEST_DURATION_SECS));
                    device->stop();
                }
                break;
            }
        }
        engine->stop();

        CallAfter([&, btn]() {
            m_audioPlotThread->join();
            delete m_audioPlotThread;
            m_audioPlotThread = nullptr;
            btn->SetLabel(_("Test"));
            setAudioTestButtonsEnabled(true, m_btnSoundCard1InTest, m_btnSoundCard1OutTest, m_btnSoundCard2InTest, m_btnSoundCard2OutTest);
        });
    }, devName, btn);
}

//-------------------------------------------------------------------------
// testAudioInput() - records 5 s on the input device then plays back on output
//-------------------------------------------------------------------------
void OptionsDlg::testAudioInput(const wxString& inDevName, const wxString& outDevName, wxButton* btn)
{
    if (inDevName.IsEmpty() || inDevName == "none")
    {
        wxMessageBox(_("Please select an audio input device first."), _("Audio Test"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    setAudioTestButtonsEnabled(false, m_btnSoundCard1InTest, m_btnSoundCard1OutTest, m_btnSoundCard2InTest, m_btnSoundCard2OutTest);
    btn->SetLabel(_("Recording"));

    m_audioPlotThread = new std::thread([&](wxString const& inDev, wxString const& outDev, wxButton* btn) {
        auto engine = AudioEngineFactory::GetAudioEngine();
        engine->start();
        auto inDevList = engine->getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_IN);
        for (auto& devInfo : inDevList)
        {
            if (!devInfo.name.IsSameAs(inDev))
                continue;

            int inSampleRate = devInfo.defaultSampleRate;
            int inTotalSamples = inSampleRate * OPTIONS_AUDIO_RECORD_DURATION_SECS;
            std::vector<short> recordBuf(inTotalSamples, 0);

            struct RecordState { std::vector<short>* buf; int pos; };
            RecordState recState = { &recordBuf, 0 };

            auto inDevice = engine->getAudioDevice(devInfo.name, IAudioEngine::AUDIO_ENGINE_IN, inSampleRate, 1);
            if (inDevice)
            {
                inDevice->setOnAudioData([](IAudioDevice&, void* data, size_t numSamples, void* state) FREEDV_NONBLOCKING {
                    auto* s = static_cast<RecordState*>(state);
                    if (data != nullptr)
                    {
                        const short* in = static_cast<const short*>(data);
                        for (size_t j = 0; j < numSamples && s->pos < (int)s->buf->size(); j++)
                            (*s->buf)[s->pos++] = in[j];
                    }
                }, &recState);
                inDevice->setDescription("Options Audio Input Test");
                inDevice->start();
                std::this_thread::sleep_for(std::chrono::seconds(OPTIONS_AUDIO_RECORD_DURATION_SECS));
                inDevice->stop();

                CallAfter([btn]() { btn->SetLabel(_("Playing")); });

                // Play back the recording on the output device
                if (!outDev.IsEmpty() && outDev != "none")
                {
                    auto outDevList = engine->getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_OUT);
                    for (auto& outDevInfo : outDevList)
                    {
                        if (!outDevInfo.name.IsSameAs(outDev))
                            continue;

                        int outSampleRate = outDevInfo.defaultSampleRate;

                        // Resample recorded buffer if input and output rates differ
                        std::vector<short> resampledBuf;
                        const std::vector<short>* playBuf = &recordBuf;

                        if (outSampleRate != inSampleRate)
                        {
                            std::vector<float> floatIn(recordBuf.size());
                            src_short_to_float_array(recordBuf.data(), floatIn.data(), (int)recordBuf.size());

                            int outSamples = (int)((double)inTotalSamples * outSampleRate / inSampleRate) + 1;
                            std::vector<float> floatOut(outSamples);

                            SRC_DATA srcData = {};
                            srcData.data_in      = floatIn.data();
                            srcData.data_out     = floatOut.data();
                            srcData.input_frames = (long)recordBuf.size();
                            srcData.output_frames = (long)outSamples;
                            srcData.end_of_input  = 1;
                            srcData.src_ratio     = (double)outSampleRate / (double)inSampleRate;

                            if (src_simple(&srcData, SRC_SINC_MEDIUM_QUALITY, 1) == 0)
                            {
                                resampledBuf.resize(srcData.output_frames_gen);
                                src_float_to_short_array(floatOut.data(), resampledBuf.data(), (int)srcData.output_frames_gen);
                                playBuf = &resampledBuf;
                            }
                        }

                        struct PlayState { const std::vector<short>* buf; int pos; };
                        PlayState playState = { playBuf, 0 };

                        auto outDevice = engine->getAudioDevice(outDevInfo.name, IAudioEngine::AUDIO_ENGINE_OUT, outSampleRate, 1);
                        if (outDevice)
                        {
                            outDevice->setOnAudioData([](IAudioDevice&, void* data, size_t numSamples, void* state) FREEDV_NONBLOCKING {
                                auto* s = static_cast<PlayState*>(state);
                                if (data != nullptr)
                                {
                                    short* out = static_cast<short*>(data);
                                    for (size_t j = 0; j < numSamples; j++)
                                        out[j] = (s->pos < (int)s->buf->size()) ? (*s->buf)[s->pos++] : 0;
                                }
                            }, &playState);
                            outDevice->setDescription("Options Audio Input Playback");
                            outDevice->start();
                            std::this_thread::sleep_for(std::chrono::seconds(OPTIONS_AUDIO_RECORD_DURATION_SECS));
                            outDevice->stop();
                        }
                        break;
                    }
                }
            }
            break;
        }
        engine->stop();

        CallAfter([&, btn]() {
            m_audioPlotThread->join();
            delete m_audioPlotThread;
            m_audioPlotThread = nullptr;
            btn->SetLabel(_("Test"));
            setAudioTestButtonsEnabled(true, m_btnSoundCard1InTest, m_btnSoundCard1OutTest, m_btnSoundCard2InTest, m_btnSoundCard2OutTest);
        });
    }, inDevName, outDevName, btn);
}

//-------------------------------------------------------------------------
// OnSoundCard1InTest()
//-------------------------------------------------------------------------
void OptionsDlg::OnSoundCard1InTest(wxCommandEvent&)
{
    // What's recorded is played back through the speaker/headphones (which,
    // in this window, is always the first output device) so that it can be heard.
    testAudioInput(getSelectedAudioDeviceName(m_tcSoundCard1InDevice), getSelectedAudioDeviceName(m_tcSoundCard1OutDevice), m_btnSoundCard1InTest);
}

//-------------------------------------------------------------------------
// OnSoundCard1OutTest()
//-------------------------------------------------------------------------
void OptionsDlg::OnSoundCard1OutTest(wxCommandEvent&)
{
    testAudioOutput(getSelectedAudioDeviceName(m_tcSoundCard1OutDevice), m_btnSoundCard1OutTest);
}

//-------------------------------------------------------------------------
// OnSoundCard2InTest()
//-------------------------------------------------------------------------
void OptionsDlg::OnSoundCard2InTest(wxCommandEvent&)
{
    // Played back through the speaker/headphones, not the radio.
    testAudioInput(getSelectedAudioDeviceName(m_tcSoundCard2InDevice), getSelectedAudioDeviceName(m_tcSoundCard1OutDevice), m_btnSoundCard2InTest);
}

//-------------------------------------------------------------------------
// OnSoundCard2OutTest()
//-------------------------------------------------------------------------
void OptionsDlg::OnSoundCard2OutTest(wxCommandEvent&)
{
    testAudioOutput(getSelectedAudioDeviceName(m_tcSoundCard2OutDevice), m_btnSoundCard2OutTest);
}
