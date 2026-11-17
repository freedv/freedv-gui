//==========================================================================
// Name:            dlg_easy_setup.cpp
// Purpose:         Dialog for easier initial setup of FreeDV.
// Created:         Nov 13, 2021
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


#include <map>
#include <climits>
#include <cmath>

#include <wx/wx.h>
#include <wx/valnum.h>

#include "../../main.h"
#include "../util/SerialPortList.h"
#include "../util/NumberEntry.h"
#include "dlg_easy_setup.h"
#include "dlg_audiooptions.h"
#include "dlg_ptt.h"
#include "audio/AudioEngineFactory.h"

#ifdef __WIN32__
#include <wx/msw/registry.h>
#else
#include <glob.h>
#include <string.h>
#endif

#ifndef PI
#define PI 3.14159
#endif // PI

#define RX_ONLY_STRING "None (receive only)"
#define MULTIPLE_DEVICES_STRING "(multiple)"

extern wxConfigBase *pConfig;

EasySetupDialog::EasySetupDialog(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) 
    : wxDialog(parent, id, title, pos, size, style)
    , hamlibTestObject_(nullptr)
    , serialPortTestObject_(nullptr)
    , hasAppliedChanges_(false)
    , testAudioDeviceStartFailed_(false)
{
    // XXX - FreeDV only supports English but makes a best effort to at least use regional formatting
    // for e.g. numbers. Thus, we only need to override layout direction.
    SetLayoutDirection(wxLayout_LeftToRight);
    
    if (wxGetApp().customConfigFileName != "")
    {
        SetTitle(wxString::Format("%s (%s)", title, wxGetApp().customConfigFileName));
    }
    
    // Create top-level of control hierarchy.
    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* sectionSizer = new wxBoxSizer(wxVERTICAL);
    
    // Step 1: sound device selection
    // ==============================
    wxStaticBox *selectSoundDeviceBox = new wxStaticBox(panel, wxID_ANY, _("Step 1: Select Sound Device"));
    wxStaticBoxSizer* setupSoundDeviceBoxSizer = new wxStaticBoxSizer( selectSoundDeviceBox, wxVERTICAL);
    
    wxFlexGridSizer* gridSizerSoundDevice = new wxFlexGridSizer(3, 2, 5, 0);
    
    wxStaticText* labelRadioDevice = new wxStaticText(selectSoundDeviceBox, wxID_ANY, wxT("Radio Device:"), wxDefaultPosition, wxDefaultSize, 0);
    gridSizerSoundDevice->Add(labelRadioDevice, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);

    m_radioDevice = new wxComboBox(selectSoundDeviceBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_DROPDOWN | wxCB_READONLY);
    m_radioDevice->SetMinSize(wxSize(250, -1));
    gridSizerSoundDevice->Add(m_radioDevice, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
    
    wxStaticText* labelAnalogPlayDevice = new wxStaticText(selectSoundDeviceBox, wxID_ANY, wxT("Decoded audio plays back through:"), wxDefaultPosition, wxDefaultSize, 0);
    gridSizerSoundDevice->Add(labelAnalogPlayDevice, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);
    
    m_analogDevicePlayback = new wxComboBox(selectSoundDeviceBox, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_DROPDOWN | wxCB_READONLY);
    gridSizerSoundDevice->Add(m_analogDevicePlayback, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
    
    wxStaticText* labelAnalogRecordDevice = new wxStaticText(selectSoundDeviceBox, wxID_ANY, wxT("Transmitted audio records through:"), wxDefaultPosition, wxDefaultSize, 0);
    gridSizerSoundDevice->Add(labelAnalogRecordDevice, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);
    
    m_analogDeviceRecord = new wxComboBox(selectSoundDeviceBox, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_DROPDOWN | wxCB_READONLY);
    gridSizerSoundDevice->Add(m_analogDeviceRecord, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
        
    wxBoxSizer* advancedSoundSetupSizer = new wxBoxSizer(wxHORIZONTAL);
    m_advancedSoundSetup = new wxButton(selectSoundDeviceBox, wxID_ANY, wxT("Advanced"),  wxDefaultPosition, wxDefaultSize, 0);
    advancedSoundSetupSizer->Add(m_advancedSoundSetup, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
    
    setupSoundDeviceBoxSizer->Add(gridSizerSoundDevice, 0, static_cast<int>(wxEXPAND) | static_cast<int>(wxALIGN_LEFT), 2);
    setupSoundDeviceBoxSizer->Add(advancedSoundSetupSizer, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_HORIZONTAL | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    sectionSizer->Add(setupSoundDeviceBoxSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
    
    // Step 2: setup CAT control
    // =========================
    wxStaticBox *setupCatControlBox = new wxStaticBox(panel, wxID_ANY, _("Step 2: Setup Radio Control"));
    wxStaticBoxSizer* setupCatControlBoxSizer = new wxStaticBoxSizer( setupCatControlBox, wxVERTICAL);
    
    wxBoxSizer* catTypeSizer = new wxBoxSizer(wxHORIZONTAL);
    setupCatControlBoxSizer->Add(catTypeSizer, 0, static_cast<int>(wxEXPAND)|static_cast<int>(wxALIGN_LEFT), 2);

    /* No CAT Control radio button */

    m_ckNoPTT = new wxRadioButton(setupCatControlBox, wxID_ANY, _("No PTT/CAT Control"), wxDefaultPosition, wxSize(-1, -1), 0);
    m_ckNoPTT->SetValue(true);
    catTypeSizer->Add(m_ckNoPTT, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
        
    /* Use Hamlib for PTT radio button. */

    m_ckUseHamlibPTT = new wxRadioButton(setupCatControlBox, wxID_ANY, _("Hamlib CAT Control"), wxDefaultPosition, wxSize(-1, -1), 0);
    m_ckUseHamlibPTT->SetValue(false);
    catTypeSizer->Add(m_ckUseHamlibPTT, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    /* Use serial port for PTT radio button. */
    m_ckUseSerialPTT = new wxRadioButton(setupCatControlBox, wxID_ANY, _("Serial PTT"), wxDefaultPosition, wxSize(-1, -1), 0);
    m_ckUseSerialPTT->SetValue(false);
    catTypeSizer->Add(m_ckUseSerialPTT, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    /* Hamlib box */
    m_hamlibBox = new wxStaticBox(setupCatControlBox, wxID_ANY, _("Hamlib CAT Control"));
    m_hamlibBox->Hide();
    wxStaticBoxSizer* hamlibBoxSizer = new wxStaticBoxSizer(m_hamlibBox, wxVERTICAL);
    wxGridSizer* gridSizerhl = new wxGridSizer(6, 2, 0, 0);
    hamlibBoxSizer->Add(gridSizerhl);

    /* Hamlib Rig Type combobox. */

    gridSizerhl->Add(new wxStaticText(m_hamlibBox, wxID_ANY, _("Rig Model:"), wxDefaultPosition, wxDefaultSize, 0), 
                      0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 20);
    m_cbRigName = new wxComboBox(m_hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(250, -1), 0, NULL, wxCB_DROPDOWN | wxCB_READONLY);
    gridSizerhl->Add(m_cbRigName, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);

    /* Hamlib Serial Port combobox. */

    gridSizerhl->Add(new wxStaticText(m_hamlibBox, wxID_ANY, _("Serial Device (or hostname:port):"), wxDefaultPosition, wxDefaultSize, 0), 
                      0, static_cast<int>(wxALIGN_CENTER_VERTICAL) |  wxALIGN_RIGHT, 20);
    m_cbSerialPort = new wxComboBox(m_hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_DROPDOWN);
    m_cbSerialPort->SetMinSize(wxSize(140, -1));
    gridSizerhl->Add(m_cbSerialPort, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);

    /* Hamlib Icom CI-V address text box. */
    m_stIcomCIVHex = new wxStaticText(m_hamlibBox, wxID_ANY, _("Radio Address:"), wxDefaultPosition, wxDefaultSize, 0);
    gridSizerhl->Add(m_stIcomCIVHex, 
                      0, static_cast<int>(wxALIGN_CENTER_VERTICAL) |  wxALIGN_RIGHT, 5);
    m_tcIcomCIVHex = new wxTextCtrl(m_hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(35, -1), 0, wxNumericPropertyValidator(wxNumericPropertyValidator::Unsigned, 16));
    m_tcIcomCIVHex->SetMaxLength(2);
    gridSizerhl->Add(m_tcIcomCIVHex, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    /* Hamlib Serial Rate combobox. */

    gridSizerhl->Add(new wxStaticText(m_hamlibBox, wxID_ANY, _("Serial Rate:"), wxDefaultPosition, wxDefaultSize, 0), 
                      0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 20);
    m_cbSerialRate = new wxComboBox(m_hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(140, -1), 0, NULL, wxCB_DROPDOWN);
    gridSizerhl->Add(m_cbSerialRate, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    /* Hamlib PTT Method combobox. */

    gridSizerhl->Add(new wxStaticText(m_hamlibBox, wxID_ANY, _("PTT uses:"), wxDefaultPosition, wxDefaultSize, 0), 
                      0, static_cast<int>(wxALIGN_CENTER_VERTICAL) |  wxALIGN_RIGHT, 20);
    m_cbPttMethod = new wxComboBox(m_hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_DROPDOWN | wxCB_READONLY);
    m_cbPttMethod->SetSize(wxSize(140, -1));
    gridSizerhl->Add(m_cbPttMethod, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    // Add valid PTT options to combo box.
    m_cbPttMethod->Append(wxT("CAT"));
    m_cbPttMethod->Append(wxT("RTS"));
    m_cbPttMethod->Append(wxT("DTR"));
    m_cbPttMethod->Append(wxT("None"));
    m_cbPttMethod->Append(wxT("CAT via Data port"));
    m_cbPttMethod->SetSelection(0);

    /* Force RTS / Force DTR checkboxes on one row */
    wxBoxSizer* forceRtsSizer = new wxBoxSizer(wxHORIZONTAL);
    m_ckForceRTSOn = new wxCheckBox(m_hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, -1), 0);
    m_ckForceRTSOn->SetToolTip(_("Always assert RTS on the Hamlib serial port (e.g. to power a radio interface)"));
    forceRtsSizer->Add(m_ckForceRTSOn, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 0);
    forceRtsSizer->Add(new wxStaticText(m_hamlibBox, wxID_ANY, _("Force RTS"), wxDefaultPosition, wxDefaultSize, 0),
                       0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxLEFT, 5);
    gridSizerhl->Add(forceRtsSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);

    wxBoxSizer* forceDtrSizer = new wxBoxSizer(wxHORIZONTAL);
    m_ckForceDTROn = new wxCheckBox(m_hamlibBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, -1), 0);
    m_ckForceDTROn->SetToolTip(_("Always assert DTR on the Hamlib serial port (e.g. to power a radio interface)"));
    forceDtrSizer->Add(m_ckForceDTROn, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxLEFT, 10);
    forceDtrSizer->Add(new wxStaticText(m_hamlibBox, wxID_ANY, _("Force DTR"), wxDefaultPosition, wxDefaultSize, 0),
                       0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxLEFT, 5);
    gridSizerhl->Add(forceDtrSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    setupCatControlBoxSizer->Add(hamlibBoxSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);

    /* Serial port box */
    m_serialBox = new wxStaticBox(setupCatControlBox, wxID_ANY, _("Serial PTT"));
    m_serialBox->Hide();
    wxStaticBoxSizer* serialBoxSizer = new wxStaticBoxSizer(m_serialBox, wxVERTICAL);
    wxGridSizer* gridSizerSerial = new wxGridSizer(4, 2, 0, 0);

    /* Serial device combo box */
    wxStaticText* serialDeviceLabel = new wxStaticText(m_serialBox, wxID_ANY, _("Serial Device:"), wxDefaultPosition, wxDefaultSize, 0);
    serialDeviceLabel->Wrap(-1);
    gridSizerSerial->Add(serialDeviceLabel, 0, static_cast<int>(wxALL) | wxALIGN_RIGHT | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    m_cbCtlDevicePath = new wxComboBox(m_serialBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_DROPDOWN);
    m_cbCtlDevicePath->SetMinSize(wxSize(140, -1));
    gridSizerSerial->Add(m_cbCtlDevicePath, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);

    /* DTR/RTS options */
   
    m_rbUseDTR = new wxRadioButton(m_serialBox, wxID_ANY, _("Use DTR"), wxDefaultPosition, wxSize(-1,-1), wxRB_GROUP);
    m_rbUseDTR->SetToolTip(_("Toggle DTR line for PTT"));
    m_rbUseDTR->SetValue(1);
    gridSizerSerial->Add(m_rbUseDTR, 0, static_cast<int>(wxALL) | wxALIGN_CENTER|static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    m_rbUseRTS = new wxRadioButton(m_serialBox, wxID_ANY, _("Use RTS"), wxDefaultPosition, wxSize(-1,-1), 0);
    m_rbUseRTS->SetToolTip(_("Toggle the RTS pin for PTT"));
    m_rbUseRTS->SetValue(1);
    gridSizerSerial->Add(m_rbUseRTS, 0, static_cast<int>(wxALL) | wxALIGN_CENTER, 2);
    
    m_ckDTRPos = new wxCheckBox(m_serialBox, wxID_ANY, _("DTR = +V"), wxDefaultPosition, wxSize(-1,-1), 0);
    m_ckDTRPos->SetToolTip(_("Set Polarity of the DTR line"));
    m_ckDTRPos->SetValue(false);
    gridSizerSerial->Add(m_ckDTRPos, 0, static_cast<int>(wxALL) | wxALIGN_CENTER, 2);

    m_ckRTSPos = new wxCheckBox(m_serialBox, wxID_ANY, _("RTS = +V"), wxDefaultPosition, wxSize(-1,-1), 0);
    m_ckRTSPos->SetValue(false);
    m_ckRTSPos->SetToolTip(_("Set Polarity of the RTS line"));
    gridSizerSerial->Add(m_ckRTSPos, 0, static_cast<int>(wxALL) | wxALIGN_CENTER, 2);

    // Override tab ordering for Polarity area
    m_rbUseDTR->MoveBeforeInTabOrder(m_rbUseRTS);
    m_rbUseRTS->MoveBeforeInTabOrder(m_ckDTRPos);
    m_ckDTRPos->MoveBeforeInTabOrder(m_ckRTSPos);
    
    serialBoxSizer->Add(gridSizerSerial, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
    setupCatControlBoxSizer->Add(serialBoxSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);

    wxBoxSizer* pttButtonSizer = new wxBoxSizer(wxHORIZONTAL);
    
    /* Advanced/test buttons */

    m_advancedPTTSetup = new wxButton(setupCatControlBox, wxID_ANY, wxT("Advanced"),  wxDefaultPosition, wxDefaultSize, 0);
    pttButtonSizer->Add(m_advancedPTTSetup, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);

    m_buttonTest = new wxButton(setupCatControlBox, wxID_ANY, wxT("Test"),  wxDefaultPosition, wxDefaultSize, 0);
    pttButtonSizer->Add(m_buttonTest, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
    
    setupCatControlBoxSizer->Add(pttButtonSizer, 0, static_cast<int>(wxALL) | wxALIGN_CENTER, 2);
    
    sectionSizer->Add(setupCatControlBoxSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
    
    // Step 3: setup PSK Reporter
    // ==========================
    wxStaticBox *setupPskReporterBox = new wxStaticBox(panel, wxID_ANY, _("Step 3: Setup Reporting"));
    
    wxStaticBoxSizer* sbSizer_psk;
    sbSizer_psk = new wxStaticBoxSizer(setupPskReporterBox, wxHORIZONTAL);
    m_ckbox_psk_enable = new wxCheckBox(setupPskReporterBox, wxID_ANY, _("Enable Reporting"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    sbSizer_psk->Add(m_ckbox_psk_enable, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    wxStaticText* labelPskCallsign = new wxStaticText(setupPskReporterBox, wxID_ANY, wxT("Callsign: "), wxDefaultPosition, wxDefaultSize, 0);
    sbSizer_psk->Add(labelPskCallsign, 0,  static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    m_txt_callsign = new wxTextCtrl(setupPskReporterBox, wxID_ANY,  wxEmptyString, wxDefaultPosition, wxSize(180,-1), 0, wxTextValidator(wxFILTER_ALPHANUMERIC));
    sbSizer_psk->Add(m_txt_callsign, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    wxStaticText* labelPskGridSquare = new wxStaticText(setupPskReporterBox, wxID_ANY, wxT("Grid Square/Locator: "), wxDefaultPosition, wxDefaultSize, 0);
    sbSizer_psk->Add(labelPskGridSquare, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    m_txt_grid_square = new wxTextCtrl(setupPskReporterBox, wxID_ANY,  wxEmptyString, wxDefaultPosition, wxSize(180,-1), 0, wxTextValidator(wxFILTER_ALPHANUMERIC));
    sbSizer_psk->Add(m_txt_grid_square, 0,  static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    sectionSizer->Add(sbSizer_psk, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
    
    // Step 4: save or cancel changes
    // =============================
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

    m_buttonOK = new wxButton(panel, wxID_OK);
    buttonSizer->Add(m_buttonOK, 0, static_cast<int>(wxALL), 2);

    m_buttonCancel = new wxButton(panel, wxID_CANCEL);
    buttonSizer->Add(m_buttonCancel, 0, static_cast<int>(wxALL), 2);

    m_buttonApply = new wxButton(panel, wxID_APPLY);
    buttonSizer->Add(m_buttonApply, 0, static_cast<int>(wxALL), 2);

    sectionSizer->Add(buttonSizer, 0, static_cast<int>(wxALL) | wxALIGN_CENTER, 2);
    
    // Trigger auto-layout of window.
    // ==============================
    panel->SetSizer(sectionSizer);
    
    wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
    panelSizer->Add(panel, 0, static_cast<int>(wxEXPAND), 0);
    this->SetSizerAndFit(panelSizer);
    
    this->Layout();
    this->Centre(wxBOTH);
    this->SetMinSize(GetBestSize());
    
    // Hook in events
    this->Connect(wxEVT_INIT_DIALOG, wxInitDialogEventHandler(EasySetupDialog::OnInitDialog));
    this->Connect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(EasySetupDialog::OnClose));
    
    m_ckUseHamlibPTT->Connect(wxEVT_RADIOBUTTON, wxCommandEventHandler(EasySetupDialog::PTTUseHamLibClicked), NULL, this);
    m_ckNoPTT->Connect(wxEVT_RADIOBUTTON, wxCommandEventHandler(EasySetupDialog::PTTUseHamLibClicked), NULL, this);
    m_ckUseSerialPTT->Connect(wxEVT_RADIOBUTTON, wxCommandEventHandler(EasySetupDialog::PTTUseHamLibClicked), NULL, this);
    
    m_cbRigName->Connect(wxEVT_COMBOBOX, wxCommandEventHandler(EasySetupDialog::HamlibRigNameChanged), NULL, this);
    
    m_advancedSoundSetup->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(EasySetupDialog::OnAdvancedSoundSetup), NULL, this);
    m_advancedPTTSetup->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(EasySetupDialog::OnAdvancedPTTSetup), NULL, this);
    m_ckbox_psk_enable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(EasySetupDialog::OnPSKReporterChecked), NULL, this);
    m_buttonTest->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(EasySetupDialog::OnTest), NULL, this);
    
    m_buttonOK->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(EasySetupDialog::OnOK), NULL, this);
    m_buttonCancel->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(EasySetupDialog::OnCancel), NULL, this);
    m_buttonApply->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(EasySetupDialog::OnApply), NULL, this);
}

EasySetupDialog::~EasySetupDialog()
{
    hamlibTestObject_ = nullptr;
    serialPortTestObject_ = nullptr;
    
    this->Disconnect(wxEVT_INIT_DIALOG, wxInitDialogEventHandler(EasySetupDialog::OnInitDialog));
    this->Disconnect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(EasySetupDialog::OnClose));
    
    m_ckUseHamlibPTT->Disconnect(wxEVT_RADIOBUTTON, wxCommandEventHandler(EasySetupDialog::PTTUseHamLibClicked), NULL, this);
    m_ckNoPTT->Disconnect(wxEVT_RADIOBUTTON, wxCommandEventHandler(EasySetupDialog::PTTUseHamLibClicked), NULL, this);
    m_ckUseSerialPTT->Disconnect(wxEVT_RADIOBUTTON, wxCommandEventHandler(EasySetupDialog::PTTUseHamLibClicked), NULL, this);

    m_cbRigName->Disconnect(wxEVT_COMBOBOX, wxCommandEventHandler(EasySetupDialog::HamlibRigNameChanged), NULL, this);
    
    m_advancedSoundSetup->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(EasySetupDialog::OnAdvancedSoundSetup), NULL, this);
    m_advancedPTTSetup->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(EasySetupDialog::OnAdvancedPTTSetup), NULL, this);
    m_ckbox_psk_enable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(EasySetupDialog::OnPSKReporterChecked), NULL, this);
    m_buttonTest->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(EasySetupDialog::OnTest), NULL, this);
    
    m_buttonOK->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(EasySetupDialog::OnOK), NULL, this);
    m_buttonCancel->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(EasySetupDialog::OnCancel), NULL, this);
    m_buttonApply->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(EasySetupDialog::OnApply), NULL, this);
}

void EasySetupDialog::ExchangeData(int inout)
{
    ExchangeSoundDeviceData(inout);
    ExchangePttDeviceData(inout);
    ExchangeReportingData(inout);
}

void EasySetupDialog::ExchangeSoundDeviceData(int inout)
{
    if (inout == EXCHANGE_DATA_IN)
    {
        wxString soundCard1InDeviceName = wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName;
        int soundCard1InSampleRate = wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate;
        wxString soundCard1OutDeviceName = wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName;
        int soundCard1OutSampleRate = wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate;
        wxString soundCard2InDeviceName = wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName;
        wxString soundCard2OutDeviceName = wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName;
        wxString radioSoundDevice;
        
        if (soundCard1InDeviceName != "none" && soundCard1OutDeviceName != "none")
        {
            // Previous existing setup, determine what it is
            if (soundCard2InDeviceName == "none" && soundCard2OutDeviceName == "none")
            {
                // RX-only setup
                auto index = m_analogDeviceRecord->FindString(RX_ONLY_STRING);
                if (index != wxNOT_FOUND)
                {
                    m_analogDeviceRecord->SetSelection(index);
                }
                
                index = m_analogDevicePlayback->FindString(soundCard1OutDeviceName);
                if (index != wxNOT_FOUND)
                {
                    m_analogDevicePlayback->SetSelection(index);
                }

                radioSoundDevice = soundCard1InDeviceName;
            }
            else 
            {
                // RX and TX setup
                auto index = m_analogDeviceRecord->FindString(soundCard2InDeviceName);
                if (index != wxNOT_FOUND)
                {
                    m_analogDeviceRecord->SetSelection(index);
                }

                index = m_analogDevicePlayback->FindString(soundCard2OutDeviceName);
                if (index != wxNOT_FOUND)
                {
                    m_analogDevicePlayback->SetSelection(index);
                }                
            
                if (soundCard1OutDeviceName == soundCard1InDeviceName)
                {
                    // We're not on a setup with different sound devices on the radio side (e.g. SDRs)
                    radioSoundDevice = soundCard1InDeviceName;

                    // Remove multiple devices entry if it's in there.
                    int index = m_radioDevice->FindString(MULTIPLE_DEVICES_STRING);
                    if (index != wxNOT_FOUND)
                    {
                        m_radioDevice->Delete(index);
                    }
                }
                else
                {
                    radioSoundDevice = MULTIPLE_DEVICES_STRING;
                }
            }
        
            if (radioSoundDevice == MULTIPLE_DEVICES_STRING)
            {
                for (unsigned int index = 0; index < m_radioDevice->GetCount(); index++)
                {
                    SoundDeviceData* data = (SoundDeviceData*)m_radioDevice->GetClientObject(index);
                    if (data != nullptr)
                    {
                        bool rxDeviceNameMatches = data->rxDeviceName == soundCard1InDeviceName;
                        bool isRxOnly = m_analogDeviceRecord->GetStringSelection() == RX_ONLY_STRING;
                        bool txDeviceNameMatches = data->txDeviceName == soundCard1OutDeviceName;
                    
                        if (rxDeviceNameMatches && (isRxOnly || txDeviceNameMatches))
                        {
                            m_radioDevice->SetSelection(index);
                            return;
                        }
                    }
                }
            }

            int index = m_radioDevice->FindString(radioSoundDevice);
            if (index != wxNOT_FOUND)
            {
                m_radioDevice->SetSelection(index);
            }
            else if (radioSoundDevice == MULTIPLE_DEVICES_STRING)
            {
                SoundDeviceData* data = new SoundDeviceData();
                assert(data != nullptr);

                data->rxDeviceName = soundCard1InDeviceName;
                data->rxSampleRate = soundCard1InSampleRate;
                data->txDeviceName = soundCard1OutDeviceName;
                data->txSampleRate = soundCard1OutSampleRate;

                m_radioDevice->Insert(MULTIPLE_DEVICES_STRING, 0, data);
                m_radioDevice->SetSelection(0);
            }
        }
    }
    else if (inout == EXCHANGE_DATA_OUT)
    {
        int index = m_radioDevice->GetSelection();
        wxString selectedString = m_radioDevice->GetString(index);
        
        // DO NOT update the radio devices if the user used advanced settings to set them.
        bool updateRadioDevices = false;
        if (selectedString != MULTIPLE_DEVICES_STRING)
        {
            updateRadioDevices = true;
        }
        
        SoundDeviceData* deviceData = (SoundDeviceData*)m_radioDevice->GetClientObject(index);
            
        SoundDeviceData* analogPlaybackDeviceData = 
            (SoundDeviceData*)m_analogDevicePlayback->GetClientObject(
                m_analogDevicePlayback->GetSelection()
                    );

        SoundDeviceData* analogRecordDeviceData = 
            (SoundDeviceData*)m_analogDeviceRecord->GetClientObject(
                m_analogDeviceRecord->GetSelection()
                    );
            
        if (analogRecordDeviceData->txDeviceName == "none")
        {
            wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName = "none";
            wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate = -1;
            wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName = "none";
            wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate = -1;
            
            if (updateRadioDevices)
            {
                wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName = deviceData->rxDeviceName;
                wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate = deviceData->rxSampleRate;
            }
            
            wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName = analogPlaybackDeviceData->rxDeviceName;
            wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate = analogPlaybackDeviceData->rxSampleRate;

            g_nSoundCards.store(1, std::memory_order_release);
        }
        else
        {
            wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName = analogRecordDeviceData->txDeviceName;
            wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate = analogRecordDeviceData->txSampleRate;
            wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName = analogPlaybackDeviceData->rxDeviceName;
            wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate = analogPlaybackDeviceData->rxSampleRate;
            
            if (updateRadioDevices)
            {
                wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName = deviceData->rxDeviceName;
                wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate = deviceData->rxSampleRate;
                wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName = deviceData->txDeviceName;
                wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate = deviceData->txSampleRate;
            }

            g_nSoundCards.store(2, std::memory_order_release);
        }
        
        wxGetApp().appConfiguration.save(pConfig);
    }
}

void EasySetupDialog::ExchangePttDeviceData(int inout)
{
    if (inout == EXCHANGE_DATA_IN)
    {
        m_ckUseHamlibPTT->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.hamlibUseForPTT);
        m_ckUseSerialPTT->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.useSerialPTT);

        if (wxGetApp().appConfiguration.rigControlConfiguration.hamlibUseForPTT)
        {
            m_hamlibBox->Show();
            m_serialBox->Hide();

            if (wxGetApp().m_intHamlibRig >= 0)
            {
                m_cbRigName->SetSelection(wxGetApp().m_intHamlibRig);
            }
            resetIcomCIVStatus_();
            
            auto selected = m_cbRigName->GetCurrentSelection();
            if (selected >= 0)
            {
                auto minBaudRate = HamlibRigController::GetMinimumSerialBaudRate(selected);
                auto maxBaudRate = HamlibRigController::GetMaximumSerialBaudRate(selected);
                updateHamlibSerialRates_(minBaudRate, maxBaudRate);
            }
            else
            {
                updateHamlibSerialRates_();
            }
            m_cbSerialPort->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialPort);

            if (wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialRate == 0) {
                m_cbSerialRate->SetSelection(0);
            } else {
                m_cbSerialRate->SetValue(wxString::Format(wxT("%i"), wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialRate.get()));
            }

            m_tcIcomCIVHex->SetValue(wxString::Format(wxT("%02X"), wxGetApp().appConfiguration.rigControlConfiguration.hamlibIcomCIVAddress.get()));
            
            m_cbPttMethod->SetSelection((int)wxGetApp().appConfiguration.rigControlConfiguration.hamlibPTTType);

            m_ckForceRTSOn->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.hamlibForceRTSOn);
            m_ckForceDTROn->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.hamlibForceDTROn);
        }
        else if (wxGetApp().appConfiguration.rigControlConfiguration.useSerialPTT)
        {
            m_hamlibBox->Hide();
            m_serialBox->Show();

            auto str = wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPort;
            m_cbCtlDevicePath->SetValue(str);
            m_rbUseRTS->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.serialPTTUseRTS);
            m_ckRTSPos->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPolarityRTS);
            m_rbUseDTR->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.serialPTTUseDTR);
            m_ckDTRPos->SetValue(wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPolarityDTR);
        }
        else
        {
            m_hamlibBox->Hide();
            m_serialBox->Hide();
        }

        Layout();
        SetSize(GetBestSize());
        SetMinSize(GetBestSize());
    }
    else if (inout == EXCHANGE_DATA_OUT)
    {
        wxGetApp().appConfiguration.rigControlConfiguration.hamlibUseForPTT = m_ckUseHamlibPTT->GetValue();
        
        wxGetApp().appConfiguration.rigControlConfiguration.useSerialPTT = m_ckUseSerialPTT->GetValue();

        if (m_ckUseHamlibPTT->GetValue())
        {
            wxGetApp().m_intHamlibRig = m_cbRigName->GetSelection();
            wxGetApp().appConfiguration.rigControlConfiguration.hamlibRigName = (wxGetApp().m_intHamlibRig >= 0) ? HamlibRigController::RigIndexToName(wxGetApp().m_intHamlibRig) : "";
            wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialPort = m_cbSerialPort->GetValue();
            wxGetApp().appConfiguration.rigControlConfiguration.hamlibPttSerialPort = m_cbSerialPort->GetValue();
            
            // What isn't an address or a rate leaves the setting as it was,
            // rather than saving whatever was on the stack.
            unsigned int hexAddress = 0;
            if (ReadCIVAddress(m_tcIcomCIVHex->GetValue(), hexAddress))
            {
                wxGetApp().appConfiguration.rigControlConfiguration.hamlibIcomCIVAddress = hexAddress;
            }
            
            unsigned int serialRate = 0;
            if (ReadSerialRate(m_cbSerialRate->GetValue(), serialRate))
            {
                wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialRate = serialRate;
            }
            log_debug("serial rate: %d", wxGetApp().appConfiguration.rigControlConfiguration.hamlibSerialRate.get());
            
            wxGetApp().appConfiguration.rigControlConfiguration.hamlibPTTType = m_cbPttMethod->GetSelection();

            wxGetApp().appConfiguration.rigControlConfiguration.hamlibForceRTSOn = m_ckForceRTSOn->GetValue();
            wxGetApp().appConfiguration.rigControlConfiguration.hamlibForceDTROn = m_ckForceDTROn->GetValue();
        }
        else if (m_ckUseSerialPTT->GetValue())
        {
            wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPort             = m_cbCtlDevicePath->GetValue();
            wxGetApp().appConfiguration.rigControlConfiguration.serialPTTUseRTS                 = m_rbUseRTS->GetValue();
            wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPolarityRTS                 = m_ckRTSPos->IsChecked();
            wxGetApp().appConfiguration.rigControlConfiguration.serialPTTUseDTR                 = m_rbUseDTR->GetValue();
            wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPolarityDTR                 = m_ckDTRPos->IsChecked();
        }
        
        wxGetApp().appConfiguration.save(pConfig);
    }
}

void EasySetupDialog::ExchangeReportingData(int inout)
{
    if (inout == EXCHANGE_DATA_IN)
    {
        m_ckbox_psk_enable->SetValue(wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled);
        m_txt_callsign->SetValue(wxGetApp().appConfiguration.reportingConfiguration.reportingCallsign);
        m_txt_grid_square->SetValue(wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare);
    }
    else if (inout == EXCHANGE_DATA_OUT)
    {
        if (wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled != m_ckbox_psk_enable->GetValue())
        {
            wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled = m_ckbox_psk_enable->GetValue();
            if (wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled)
            {
                // Enable both PSK Reporter and FreeDV Reporter by default.
                wxGetApp().appConfiguration.reportingConfiguration.pskReporterEnabled = wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled;
                wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnabled = wxGetApp().appConfiguration.reportingConfiguration.reportingEnabled;
                
                if (wxGetApp().appConfiguration.reportingConfiguration.freedvReporterHostname == "")
                {
                    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterHostname = FREEDV_REPORTER_DEFAULT_HOSTNAME;
                }
            }
        }
        
        wxGetApp().appConfiguration.reportingConfiguration.reportingCallsign = m_txt_callsign->GetValue();
        wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare = m_txt_grid_square->GetValue();

        wxGetApp().appConfiguration.save(pConfig);
    }
}

void EasySetupDialog::OnPSKReporterChecked(wxCommandEvent&)
{
    m_txt_callsign->Enable(m_ckbox_psk_enable->GetValue());
    m_txt_grid_square->Enable(m_ckbox_psk_enable->GetValue());
}

void EasySetupDialog::HamlibRigNameChanged(wxCommandEvent&)
{
    resetIcomCIVStatus_();
    
    auto selected = m_cbRigName->GetCurrentSelection();
    if (selected >= 0)
    {
        auto minBaudRate = HamlibRigController::GetMinimumSerialBaudRate(selected);
        auto maxBaudRate = HamlibRigController::GetMaximumSerialBaudRate(selected);
        updateHamlibSerialRates_(minBaudRate, maxBaudRate);
    }
    else
    {
        updateHamlibSerialRates_();
    }
}

void EasySetupDialog::resetIcomCIVStatus_()
{
    bool icomFound = false;
    if (m_cbRigName->GetCurrentSelection() >= 0)
    {
        std::string rigName = m_cbRigName->GetString(m_cbRigName->GetCurrentSelection()).ToStdString();
        if (rigName.find("Icom") == 0)
        {
            icomFound = true;
        }
    }

    if (icomFound)
    {
        m_stIcomCIVHex->Show();
        m_tcIcomCIVHex->Show();

        // Load existing CIV address so we can make sure we get the default.
        if (m_tcIcomCIVHex->GetValue().length() == 0)
        {
            m_tcIcomCIVHex->SetValue(wxString::Format(wxT("%02X"), wxGetApp().appConfiguration.rigControlConfiguration.hamlibIcomCIVAddress.get()));
        }
    }
    else
    {
        m_stIcomCIVHex->Hide();
        m_tcIcomCIVHex->Hide();
    }
    Layout();
}

void EasySetupDialog::OnInitDialog(wxInitDialogEvent&)
{
    updateAudioDevices_();
    updateHamlibDevices_();
    ExchangeData(EXCHANGE_DATA_IN);
}

void EasySetupDialog::OnOK(wxCommandEvent& event)
{
    OnApply(event);
    if (canSaveSettings_())
    {
        this->EndModal(wxOK);
    }
}

void EasySetupDialog::OnCancel(wxCommandEvent&)
{
    if (m_buttonTest->GetLabel() == "Stop Test")
    {
        stopTest_();
    }
    this->EndModal(hasAppliedChanges_ ? wxOK : wxCANCEL);
}

void EasySetupDialog::OnClose(wxCloseEvent&)
{
    if (m_buttonTest->GetLabel() == "Stop Test")
    {
        stopTest_();
    }
    this->EndModal(hasAppliedChanges_ ? wxOK : wxCANCEL);
}

void EasySetupDialog::OnApply(wxCommandEvent&)
{
    if (canSaveSettings_())
    {
        ExchangeData(EXCHANGE_DATA_OUT);
        hasAppliedChanges_ = true;
    }
    else
    {
        wxMessageBox(
            "Some settings have not been configured. Please check your radio and audio device settings and try again.", 
            wxT("Error"), wxOK, this);
    }
}

void EasySetupDialog::OnAdvancedSoundSetup(wxCommandEvent& event)
{
    wxUnusedVar(event);
    
    AudioOptsDialog *dlg = new AudioOptsDialog(this);
    int rv = dlg->ShowModal();
    
    // If the user pushed Refresh in the window, update device list here just in case
    // there were any device changes.
    updateAudioDevices_();
    
    if(rv == wxOK)
    {
        dlg->ExchangeData(EXCHANGE_DATA_OUT);
        ExchangeSoundDeviceData(EXCHANGE_DATA_IN);
    }
    delete dlg;
}

void EasySetupDialog::OnAdvancedPTTSetup(wxCommandEvent& event)
{
    wxUnusedVar(event);
    
    ComPortsDlg *dlg = new ComPortsDlg(this);
    int rv = dlg->ShowModal();
    if(rv == wxID_OK)
    {
        ExchangePttDeviceData(EXCHANGE_DATA_IN);
    }
    delete dlg;
}

void EasySetupDialog::OnTest(wxCommandEvent&)
{
    if (canTestRadioSettings_())
    {
        if (m_buttonTest->GetLabel() == "Stop Test")
        {
            stopTest_();
        }
        else
        {
            int index = m_radioDevice->GetSelection();
            wxString selectedString = m_radioDevice->GetString(index);
            wxString radioOutDeviceName = "none";
            int radioOutSampleRate = -1;
        
            // Use the global settings if we're using multiple sound devices on the radio side.
            if (m_analogDeviceRecord->GetStringSelection() != RX_ONLY_STRING)
            {
                if (selectedString == MULTIPLE_DEVICES_STRING)
                {
                    radioOutDeviceName = wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName;
                    radioOutSampleRate = wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate;
                }
                else
                {
                    SoundDeviceData* deviceData = (SoundDeviceData*)m_radioDevice->GetClientObject(index);
                    radioOutDeviceName = deviceData->txDeviceName;
                    radioOutSampleRate = deviceData->txSampleRate;
                }
            }
        
            // Turn on transmit if Hamlib is enabled.
            if (m_ckUseHamlibPTT->GetValue())
            {
                int rig = m_cbRigName->GetSelection();
                wxString serialPort = m_cbSerialPort->GetValue();
            
                // (What isn't an address or a rate is tried as Hamlib's own, 0.)
                unsigned int address = 0;
                long civHexAddress = ReadCIVAddress(m_tcIcomCIVHex->GetValue(), address) ? (long)address : 0;
            
                unsigned int serialRate = 0;
                long rate = ReadSerialRate(m_cbSerialRate->GetValue(), serialRate) ? (long)serialRate : 0;
            
                if (!wxGetApp().CanAccessSerialPort((const char*)serialPort.ToUTF8()))
                {
                    return;
                }
                
                auto pttType = (HamlibRigController::PttType)m_cbPttMethod->GetSelection();

                hamlibTestObject_ = std::make_shared<HamlibRigController>(rig, (const char*)serialPort.ToUTF8(), rate, civHexAddress, pttType,
                    std::string(), false, false, m_ckForceRTSOn->GetValue(), m_ckForceDTROn->GetValue());
                hamlibTestObject_->onRigError += [this](IRigController*, std::string error) {
                    CallAfter([this, error = std::move(error)]() {
                        wxMessageBox(
                            wxString::Format("Couldn't connect to Radio with Hamlib (%s).  Make sure the Hamlib serial Device, Rate, and Params match your radio", error), 
                            wxT("Error"), wxOK | wxICON_ERROR, this);
                    });
                };

                hamlibTestObject_->onRigConnected += [&](IRigController*) {
                    hamlibTestObject_->ptt(true);
                };

                hamlibTestObject_->connect();
            }
            else if (m_ckUseSerialPTT->GetValue())
            {
                wxString serialPort = m_cbCtlDevicePath->GetValue();

                if (!wxGetApp().CanAccessSerialPort((const char*)serialPort.ToUTF8()))
                {
                    CallAfter([this]() {
                        wxMessageBox(
                            "Couldn't connect to Radio.  Make sure the serial Device and Params match your radio", 
                            wxT("Error"), wxOK | wxICON_ERROR, this);
                    });
                    return;
                }
                
                bool useRTS = m_rbUseRTS->GetValue();
                bool RTSPos = m_ckRTSPos->IsChecked();
                bool useDTR = m_rbUseDTR->GetValue();
                bool DTRPos = m_ckDTRPos->IsChecked();

                serialPortTestObject_ = std::make_shared<SerialPortOutRigController>(
                    (const char*)serialPort.ToUTF8(),
                    useRTS,
                    RTSPos,
                    useDTR,
                    DTRPos
                );
                serialPortTestObject_->onRigError += [this](IRigController*, std::string error) {
                    CallAfter([this, error = std::move(error)]() {
                        wxMessageBox(
                            wxString::Format("Couldn't connect to Radio (%s).  Make sure the serial Device and Params match your radio", error), 
                            wxT("Error"), wxOK | wxICON_ERROR, this);
                    });
                };
                serialPortTestObject_->onRigConnected += [&](IRigController*) {
                    serialPortTestObject_->ptt(true);
                };          
                serialPortTestObject_->connect();      
            }
        
            // Get analog playback device info
            int analogIndex = m_analogDevicePlayback->GetSelection();
            SoundDeviceData* analogPlaybackData = (SoundDeviceData*)m_analogDevicePlayback->GetClientObject(analogIndex);
            wxString analogOutDeviceName = (analogPlaybackData != nullptr) ? analogPlaybackData->rxDeviceName : wxString("none");
            int analogOutSampleRate = (analogPlaybackData != nullptr) ? analogPlaybackData->rxSampleRate : 44100;

            // Start playing sine waves through the radio and analog playback devices
            if (radioOutDeviceName != "none" || analogOutDeviceName != "none")
            {
                auto audioEngine = AudioEngineFactory::GetAudioEngine();
                audioEngine->start();

                if (radioOutDeviceName != "none")
                {
                    txTestAudioDevice_ = audioEngine->getAudioDevice(radioOutDeviceName, IAudioEngine::AUDIO_ENGINE_OUT, radioOutSampleRate, 1);

                    if (txTestAudioDevice_ == nullptr)
                    {
                        wxMessageBox(
                            "Error opening radio sound device. Please double-check configuration and try again.",
                            wxT("Error"), wxOK | wxICON_ERROR, this);

                        stopTest_();
                        return;
                    }

                    sineWaveSampleNumber_ = 0;

                    txTestAudioDevice_->setOnAudioData([](IAudioDevice& dev, void* data, size_t size, void* state) FREEDV_NONBLOCKING {
                        auto sr = dev.getSampleRate();
                        EasySetupDialog* castedThis = (EasySetupDialog*)state;
                        short* audioData = static_cast<short*>(data);

                        for (unsigned long index = 0; index < size; index++)
                        {
                            *audioData++ = (SHRT_MAX) * sin(2 * PI * (1500) * castedThis->sineWaveSampleNumber_ / sr);
                            castedThis->sineWaveSampleNumber_ = (castedThis->sineWaveSampleNumber_ + 1) % sr;
                        }

                    }, this);
                    txTestAudioDevice_->setOnAudioError(&EasySetupDialog::audioTestErrorCallback_, this);

                    // start() blocks until the device has either fully started or
                    // failed, so testAudioDeviceStartFailed_ is safe to check
                    // immediately afterwards -- see its declaration for why.
                    testAudioDeviceStartFailed_ = false;
                    txTestAudioDevice_->start();

                    if (testAudioDeviceStartFailed_)
                    {
                        // audioTestErrorCallback_() already queued an error message;
                        // just tear the (non-functional) test back down.
                        stopTest_();
                        return;
                    }
                }

                if (analogOutDeviceName != "none")
                {
                    analogPlaybackTestAudioDevice_ = audioEngine->getAudioDevice(analogOutDeviceName, IAudioEngine::AUDIO_ENGINE_OUT, analogOutSampleRate, 1);

                    if (analogPlaybackTestAudioDevice_ != nullptr)
                    {
                        analogSineWaveSampleNumber_ = 0;

                        analogPlaybackTestAudioDevice_->setOnAudioData([](IAudioDevice& dev, void* data, size_t size, void* state) FREEDV_NONBLOCKING {
                            auto sr = dev.getSampleRate();
                            EasySetupDialog* castedThis = (EasySetupDialog*)state;
                            short* audioData = static_cast<short*>(data);

                            for (unsigned long index = 0; index < size; index++)
                            {
                                *audioData++ = (SHRT_MAX / 4) * sin(2 * PI * (1500) * castedThis->analogSineWaveSampleNumber_ / sr);
                                castedThis->analogSineWaveSampleNumber_ = (castedThis->analogSineWaveSampleNumber_ + 1) % sr;
                            }

                        }, this);
                        analogPlaybackTestAudioDevice_->setOnAudioError(&EasySetupDialog::audioTestErrorCallback_, this);

                        testAudioDeviceStartFailed_ = false;
                        analogPlaybackTestAudioDevice_->start();

                        if (testAudioDeviceStartFailed_)
                        {
                            // Non-fatal -- radio/TX audio may still be fine. Just tear
                            // this device back down; audioTestErrorCallback_() already
                            // queued an error message.
                            analogPlaybackTestAudioDevice_->stop();
                            analogPlaybackTestAudioDevice_ = nullptr;
                        }
                    }
                    else
                    {
                        wxMessageBox(
                            "Error opening analog playback sound device. Please double-check configuration and try again.",
                            wxT("Error"), wxOK | wxICON_ERROR, this);
                    }
                }
            }
        
            // Disable all UI except the Stop button.
            m_radioDevice->Enable(false);
            m_advancedSoundSetup->Enable(false);
            m_ckUseHamlibPTT->Enable(false);
            m_cbRigName->Enable(false);
            m_cbSerialPort->Enable(false);
            m_cbSerialRate->Enable(false);
            m_tcIcomCIVHex->Enable(false);
            m_advancedPTTSetup->Enable(false);
            m_ckbox_psk_enable->Enable(false);
            m_txt_callsign->Enable(false);
            m_txt_grid_square->Enable(false);
            m_buttonOK->Enable(false);
            m_buttonCancel->Enable(false);
            m_buttonApply->Enable(false);
        
            m_buttonTest->SetLabel("Stop Test");
        }
    }
    else
    {
        wxMessageBox(
            "Some settings have not been configured. Please check your radio and audio device settings and try again.",
            wxT("Error"), wxOK, this);
    }
}

void EasySetupDialog::audioTestErrorCallback_(IAudioDevice&, std::string const& error, void* state)
{
    auto castedThis = (EasySetupDialog*)state;
    castedThis->testAudioDeviceStartFailed_ = true;
    castedThis->CallAfter([castedThis, error]() {
        wxMessageBox(
            wxString::Format("Error opening sound device (%s). Please double-check configuration and try again.", error),
            wxT("Error"), wxOK | wxICON_ERROR, castedThis);
    });
}

void EasySetupDialog::stopTest_()
{
    // Stop the currently running test.
    if (txTestAudioDevice_ != nullptr)
    {
        txTestAudioDevice_->stop();
        txTestAudioDevice_ = nullptr;
    }

    if (analogPlaybackTestAudioDevice_ != nullptr)
    {
        analogPlaybackTestAudioDevice_->stop();
        analogPlaybackTestAudioDevice_ = nullptr;
    }

    // Always stop the (shared) audio engine here too, even if neither test
    // device object above was non-null -- OnTest() may have already called
    // audioEngine->start() before failing to open/start a device, and this
    // is the only place left to balance that call in such cases. Safe to
    // call even if start() was never actually called.
    auto audioEngine = AudioEngineFactory::GetAudioEngine();
    audioEngine->stop();

    if (hamlibTestObject_ != nullptr && hamlibTestObject_->isConnected())
    {
        hamlibTestObject_->ptt(false);
        hamlibTestObject_->disconnect();
    }
    else if (serialPortTestObject_ != nullptr && serialPortTestObject_->isConnected())
    {
        serialPortTestObject_->ptt(false);
        serialPortTestObject_->disconnect();
    }

    hamlibTestObject_ = nullptr;
    serialPortTestObject_ = nullptr;

    m_radioDevice->Enable(true);
    m_advancedSoundSetup->Enable(true);
    m_ckUseHamlibPTT->Enable(true);
    m_cbRigName->Enable(true);
    m_cbSerialPort->Enable(true);
    m_cbSerialRate->Enable(true);
    m_tcIcomCIVHex->Enable(true);
    m_advancedPTTSetup->Enable(true);
    m_ckbox_psk_enable->Enable(true);
    m_txt_callsign->Enable(true);
    m_txt_grid_square->Enable(true);
    m_buttonOK->Enable(true);
    m_buttonCancel->Enable(true);
    m_buttonApply->Enable(true);

    m_buttonTest->SetLabel("Test");
}

void EasySetupDialog::PTTUseHamLibClicked(wxCommandEvent&)
{
    if (m_ckUseHamlibPTT->GetValue())
    {
        m_hamlibBox->Show();
        resetIcomCIVStatus_();
    }
    else
    {
        m_hamlibBox->Hide();
    }

    if (m_ckUseSerialPTT->GetValue())
    {
        m_serialBox->Show();
    }
    else
    {
        m_serialBox->Hide();
    }

    Layout();
    SetSize(GetBestSize());
    SetMinSize(GetBestSize());
}

void EasySetupDialog::updateHamlibSerialRates_(int min, int max)
{
    wxString serialRates[] = {"default", "300", "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200", "230400", "460800", "500000", "576000", "921600", "1000000", "1152000", "1500000", "2000000"};

    auto prevSelection = m_cbSerialRate->GetCurrentSelection();
    int oldBaud = 0;
    if (prevSelection > 0)
    {
        oldBaud = wxAtoi(serialRates[prevSelection]);
    }
    m_cbSerialRate->Clear();

    unsigned int i;
    for(i=0; i<WXSIZEOF(serialRates); i++) {
        auto rateAsInt = wxAtoi(serialRates[i]);

        if (i > 0 && min > 0 && max > 0)
        {
            if (min > rateAsInt || rateAsInt > max)
            {
                continue;
            }
        }
        m_cbSerialRate->Append(serialRates[i]);

        if (rateAsInt == oldBaud)
        {
            auto newSelection = m_cbSerialRate->GetCount() - 1;
            m_cbSerialRate->SetSelection(newSelection);
        }
        else if (i == 0 && prevSelection == 0)
        {
            m_cbSerialRate->SetSelection(0);
        }
    }
}

void EasySetupDialog::updateHamlibDevices_()
{
    auto numHamlibDevices = HamlibRigController::GetNumberSupportedRadios();
    for (auto index = 0; index < numHamlibDevices; index++)
    {
        m_cbRigName->Append(HamlibRigController::RigIndexToName(index));
    }
    if (wxGetApp().m_intHamlibRig >= 0)
    {
        m_cbRigName->SetSelection(wxGetApp().m_intHamlibRig);
    }
    
    /* populate Hamlib serial rate combo box */
    updateHamlibSerialRates_();
    m_cbSerialRate->SetSelection(0);
    m_cbSerialPort->Clear();
    
    std::vector<wxString> portList = GetSerialPortList();

    for (wxString& port : portList)
    {
        m_cbSerialPort->Append(port);
        m_cbCtlDevicePath->Append(port);
    }
}

void EasySetupDialog::updateAudioDevices_()
{
    std::map<wxString, SoundDeviceData*> finalRadioDeviceList;
    std::map<wxString, SoundDeviceData*> finalAnalogRxDeviceList;
    std::map<wxString, SoundDeviceData*> finalAnalogTxDeviceList;

    // Clear device list first
    m_radioDevice->Clear();
    m_analogDeviceRecord->Clear();
    m_analogDevicePlayback->Clear();

    auto audioEngine = AudioEngineFactory::GetAudioEngine();
    audioEngine->start();

    auto inputDevices = audioEngine->getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_IN);
    auto outputDevices = audioEngine->getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_OUT);
    
    for (auto& dev : inputDevices)
    {
        if (dev.cardName.Find(_("Microsoft Sound Mapper")) != -1 ||
            dev.cardName.Find(_(" [Loopback]")) != -1)
        {
            // Sound mapper and loopback devices should be skipped.
            continue;
        }

        wxString cleanedDeviceName = dev.cardName;
        
        SoundDeviceData* soundData = new SoundDeviceData();
        assert(soundData != nullptr);
        
        soundData->rxDeviceName = dev.name;
        soundData->rxSampleRate = dev.defaultSampleRate;
        
        finalRadioDeviceList[cleanedDeviceName] = soundData;

        if (!cleanedDeviceName.StartsWith("DAX "))
        {
            SoundDeviceData* txSoundData = new SoundDeviceData();
            assert(txSoundData != nullptr);

            txSoundData->txDeviceName = dev.name;
            txSoundData->txSampleRate = dev.defaultSampleRate;

            finalAnalogTxDeviceList[dev.name] = txSoundData;
        }
    }
    
    finalAnalogTxDeviceList[RX_ONLY_STRING] = new SoundDeviceData();
    assert(finalAnalogTxDeviceList[RX_ONLY_STRING] != nullptr);
    finalAnalogTxDeviceList[RX_ONLY_STRING]->txDeviceName = "none";
    finalAnalogTxDeviceList[RX_ONLY_STRING]->txSampleRate = 0;

    for (auto& dev : outputDevices)
    {
        if (dev.cardName.Find(_("Microsoft Sound Mapper")) != -1 ||
            dev.cardName.Find(_(" [Loopback]")) != -1)
        {
            // Sound mapper and loopback devices should be skipped.
            continue;
        }

        wxString cleanedDeviceName = dev.cardName;

        SoundDeviceData* soundData = finalRadioDeviceList[cleanedDeviceName];
        if (soundData == nullptr)
        {
            soundData = new SoundDeviceData();
            assert(soundData != nullptr);
            
            finalRadioDeviceList[cleanedDeviceName] = soundData;
        }
        
        soundData->txDeviceName = dev.name;
        soundData->txSampleRate = dev.defaultSampleRate;

        if (!cleanedDeviceName.StartsWith("DAX "))
        {
            SoundDeviceData* rxSoundData = new SoundDeviceData();
            assert(rxSoundData != nullptr);

            rxSoundData->rxDeviceName = dev.name;
            rxSoundData->rxSampleRate = dev.defaultSampleRate;
            
            finalAnalogRxDeviceList[dev.name] = rxSoundData;
        }
    }
    
    // FlexRadio shortcut: all devices starting with "DAX Audio RX" should be linked
    // to the "DAX Audio TX" device (or "DAX RX" / "DAX TX" respectively in SmartSDR 4.2+). 
    // There's only one TX device for all digital mode applications intended to be used 
    // on a Flex radio.
    wxString fullTxDeviceName;
    int flexTxDeviceSampleRate = -1;
    for (auto& kvp : finalRadioDeviceList)
    {
        if ((kvp.first.StartsWith("DAX Audio TX") || kvp.first.StartsWith("DAX TX")) && 
            kvp.second->txSampleRate > 0 && kvp.second->txDeviceName != "none")
        {
            fullTxDeviceName = kvp.second->txDeviceName;
            flexTxDeviceSampleRate = kvp.second->txSampleRate;
            
            // Suppress the TX device from appearing in the list.
            kvp.second->txDeviceName = "none";
            kvp.second->txSampleRate = -1;
        }
        else if (kvp.first.StartsWith("DAX RESERVED") || kvp.first.StartsWith("DAX IQ") || kvp.first.StartsWith("DAX MIC"))
        {
            // Suppress all reserved and IQ devices from the list.
            kvp.second->txDeviceName = "none";
            kvp.second->txSampleRate = -1;
            kvp.second->rxDeviceName = "none";
            kvp.second->rxSampleRate = -1;
        }
    }
    
    if (fullTxDeviceName != "")
    {
        for (auto& kvp : finalRadioDeviceList)
        {
            if ((kvp.first.StartsWith("DAX Audio RX") && fullTxDeviceName.StartsWith("DAX Audio TX")) || 
                (kvp.first.StartsWith("DAX RX") && fullTxDeviceName.StartsWith("DAX TX")))
            {
                kvp.second->txDeviceName = fullTxDeviceName;
                kvp.second->txSampleRate = flexTxDeviceSampleRate;
            }
        }
    }
    
    // Add the ones we found to the device list.
    for (auto& kvp : finalRadioDeviceList)
    {
        // Only include devices that we have both RX and TX IDs for.
        if (kvp.second->rxDeviceName == "none" ||
            kvp.second->txDeviceName == "none")
        {
            delete kvp.second;
            continue;
        }
        
        // Note: m_radioDevice owns kvp.second after this call and is responsible
        // for deleting it when the window is closed by the user.
        m_radioDevice->Append(kvp.first, kvp.second);
    }    
    
    if (m_radioDevice->GetCount() > 0)
    {
        // Default to the first found device.
        m_radioDevice->SetSelection(0);
    }
    
    for (auto& kvp : finalAnalogRxDeviceList)
    {
        m_analogDevicePlayback->Append(kvp.first, kvp.second);
    }

    if (m_analogDevicePlayback->GetCount() > 0)
    {
        // Default to the first found device.
        m_analogDevicePlayback->SetSelection(0);
    }

    for (auto& kvp : finalAnalogTxDeviceList)
    {
        m_analogDeviceRecord->Append(kvp.first, kvp.second);
    }
    
    if (m_analogDeviceRecord->GetCount() > 0)
    {
        // Default to the first found device.
        m_analogDeviceRecord->SetSelection(0);
    }

    auto defaultInputDevice = audioEngine->getDefaultAudioDevice(IAudioEngine::AUDIO_ENGINE_IN);
    if (defaultInputDevice.isValid())
    {
        wxString devName(defaultInputDevice.name);

        auto index = m_analogDeviceRecord->FindString(devName);
        if (index != wxNOT_FOUND)
        {
            m_analogDeviceRecord->SetSelection(index);
        }
    }
    else
    {
        auto index = m_analogDeviceRecord->FindString(RX_ONLY_STRING);
        if (index != wxNOT_FOUND)
        {
            m_analogDeviceRecord->SetSelection(index);
        }
    }
    
    auto defaultOutputDevice = audioEngine->getDefaultAudioDevice(IAudioEngine::AUDIO_ENGINE_OUT);
    if (defaultOutputDevice.isValid())
    {
        wxString devName(defaultOutputDevice.name);

        auto index = m_analogDevicePlayback->FindString(devName);
        if (index != wxNOT_FOUND)
        {
            m_analogDevicePlayback->SetSelection(index);
        }
    }

    audioEngine->stop();
}

bool EasySetupDialog::canTestRadioSettings_()
{   
    bool radioDeviceSelected = m_radioDevice->GetSelection() >= 0;
    bool analogPlayDeviceSelected = m_analogDevicePlayback->GetSelection() >= 0;
    bool analogRecordDeviceSelected = m_analogDeviceRecord->GetSelection() >= 0;
    bool soundDevicesConfigured = radioDeviceSelected && analogPlayDeviceSelected && analogRecordDeviceSelected;
    
    bool noPttSelected = m_ckNoPTT->GetValue();
    bool hamlibSelected = m_ckUseHamlibPTT->GetValue();
    bool serialSelected = m_ckUseSerialPTT->GetValue();
    
    bool hamlibSerialPortEntered = m_cbSerialPort->GetValue().Length() > 0;
    bool icomRadioSelected = HamlibRigController::RigIndexToName(m_cbRigName->GetSelection()).find("Icom") != std::string::npos;
    bool icomCIVEntered = m_tcIcomCIVHex->GetValue().Length() > 0;
    bool hamlibValid = hamlibSerialPortEntered && (!icomRadioSelected || icomCIVEntered);
    
    bool serialPttPortEntered = m_cbCtlDevicePath->GetValue().Length() > 0;
    bool useDtrSelected = m_rbUseDTR->GetValue();
    bool useRtsSelected = m_rbUseRTS->GetValue();
    bool serialPttValid = serialPttPortEntered && (useDtrSelected || useRtsSelected);
    
    bool radioCommValid = 
        noPttSelected || (hamlibSelected && hamlibValid) || 
        (serialSelected && serialPttValid);
    
    return soundDevicesConfigured && radioCommValid;
}

bool EasySetupDialog::canSaveSettings_()
{
    bool radioSettingsValid = canTestRadioSettings_();

    bool isPSKReporterEnabled = m_ckbox_psk_enable->GetValue();
    bool callsignValid = m_txt_callsign->GetValue().Length() > 0;
    bool gridSquareValid = m_txt_grid_square->GetValue().Length() > 0;
    
    return radioSettingsValid && (!isPSKReporterEnabled || (callsignValid && gridSquareValid));
}
