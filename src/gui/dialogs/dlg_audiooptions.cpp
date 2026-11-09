//=========================================================================
// Name:            AudioOptsDialog.cpp
// Purpose:         Implements an Audio options selection dialog.
//
// Authors:         David Rowe, David Witten
// License:
//
//  All rights reserved.
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
//=========================================================================

#include <chrono>

#include <wx/app.h>
#include <wx/confbase.h>

#include "dlg_audiooptions.h"

#include "audio/AudioEngineFactory.h"
#include "audio/IAudioDevice.h"

#include "../../main.h"
#include "../../pipeline/ResampleStep.h"

using namespace std::chrono_literals;

// constants for test waveform plots

#define TEST_WAVEFORM_X          180
#define TEST_WAVEFORM_Y          180
#define TEST_WAVEFORM_PLOT_TIME  2.0
#define TEST_WAVEFORM_PLOT_FS    400
#define TEST_BUF_SIZE           1024
#define TEST_FS                 48000.0
#define TEST_DT                 0.1      // time between plot updates in seconds
#define TEST_WAVEFORM_PLOT_BUF  ((int)(DT*400))

extern wxConfigBase *pConfig;

struct AudioDeviceCapture
{
    GenericFIFO<short>* fifo;
    std::condition_variable* cv;
    bool* running;
    int* n;
};

void AudioOptsDialog::audioEngineInit(void)
{
    m_isPaInitialized = true;

    auto engine = AudioEngineFactory::GetAudioEngine();
    engine->setOnEngineError([this](IAudioEngine&, std::string const& error, void*)
    {
        // error is copied in: this lambda runs after the handler returns,
        // and the handler's argument is gone by then.
        CallAfter([error]() {
            wxMessageBox(wxString::Format("Sound engine failed to initialize: %s", error), wxT("Error"), wxOK);
        });
        
        m_isPaInitialized = false;
    }, nullptr);
    
    engine->start();
}


void AudioOptsDialog::buildTestControls(PlotScalar **plotScalar, wxButton **btnTest, 
                                        wxStaticBox *parentPanel, wxBoxSizer *bSizer, wxString const& buttonLabel)
{
    wxBoxSizer* bSizer1 = new wxBoxSizer(wxVERTICAL);

    //wxPanel *panel = new wxPanel(parentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
    *plotScalar = new PlotScalar(parentPanel, TEST_WAVEFORM_PLOT_TIME, 1.0/TEST_WAVEFORM_PLOT_FS, -1, 1, 1, 0.2, "", 1, "Test audio plot");
    (*plotScalar)->SetToolTip("Shows test audio waveform");
    (*plotScalar)->SetClientSize(wxSize(TEST_WAVEFORM_X,TEST_WAVEFORM_Y));
    (*plotScalar)->SetMinSize(wxSize(150,150));
    bSizer1->Add(*plotScalar, 0, wxALIGN_CENTER_HORIZONTAL|static_cast<int>(wxALL), 8);

    *btnTest = new wxButton(parentPanel, wxID_ANY, buttonLabel, wxDefaultPosition, wxDefaultSize);
    bSizer1->Add(*btnTest, 0, wxALIGN_CENTER_HORIZONTAL|static_cast<int>(wxALL), 0);

    bSizer->Add(bSizer1, 0, wxALIGN_CENTER_HORIZONTAL |static_cast<int>(wxALIGN_CENTER_VERTICAL) );
}

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// AudioOptsDialog()
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
AudioOptsDialog::AudioOptsDialog(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxDialog(parent, id, title, pos, size, style)
{
    // XXX - FreeDV only supports English but makes a best effort to at least use regional formatting
    // for e.g. numbers. Thus, we only need to override layout direction.
    SetLayoutDirection(wxLayout_LeftToRight);
    
    if (wxGetApp().customConfigFileName != "")
    {
        SetTitle(wxString::Format("%s (%s)", title, wxGetApp().customConfigFileName));
    }
    
    log_debug("pos %d %d", pos.x, pos.y);
    audioEngineInit();

    wxBoxSizer* mainSizer;
    mainSizer = new wxBoxSizer(wxVERTICAL);
    mainSizer->SetMinSize(wxSize( 800, 650 ));
    
    m_panel1 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxBoxSizer* bSizer4;
    bSizer4 = new wxBoxSizer(wxVERTICAL);
    m_notebook1 = new wxNotebook(m_panel1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_BOTTOM);
    m_panelRx = new wxPanel(m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxBoxSizer* bSizer20;
    bSizer20 = new wxBoxSizer(wxVERTICAL);
    wxGridSizer* gSizer4;
    gSizer4 = new wxGridSizer(2, 1, 0, 0);

    // Rx In -----------------------------------------------------------------------

    wxStaticBoxSizer* sbSizer2;
    wxStaticBox* panelRxInBox = new wxStaticBox(m_panelRx, wxID_ANY, _("Input To Computer From Radio"));
    sbSizer2 = new wxStaticBoxSizer(panelRxInBox, wxHORIZONTAL);

    wxBoxSizer* bSizer811a = new wxBoxSizer(wxVERTICAL);

    m_listCtrlRxInDevices = new wxListCtrl(panelRxInBox, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_HRULES|wxLC_REPORT|wxLC_VRULES);
    bSizer811a->Add(m_listCtrlRxInDevices, 1, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 1);

    wxBoxSizer* bSizer811;
    bSizer811 = new wxBoxSizer(wxHORIZONTAL);
    m_staticText51 = new wxStaticText(panelRxInBox, wxID_ANY, _("Device:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText51->Wrap(-1);
    bSizer811->Add(m_staticText51, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 5);
    m_textCtrlRxIn = new wxTextCtrl(panelRxInBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
    bSizer811->Add(m_textCtrlRxIn, 1, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 1);
    m_staticText6 = new wxStaticText(panelRxInBox, wxID_ANY, _("Sample Rate:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText6->Wrap(-1);
    bSizer811->Add(m_staticText6, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 5);
    m_cbSampleRateRxIn = new wxComboBox(panelRxInBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(100,-1), 0, NULL, wxCB_DROPDOWN);
    bSizer811->Add(m_cbSampleRateRxIn, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 1);

    bSizer811a->Add(bSizer811, 0, static_cast<int>(wxEXPAND), 5);

    sbSizer2->Add(bSizer811a, 1, static_cast<int>(wxEXPAND), 2);
    buildTestControls(&m_plotScalarRxIn, &m_btnRxInTest, panelRxInBox, sbSizer2, _("Record 2 Seconds"));

    gSizer4->Add(sbSizer2, 1, static_cast<int>(wxEXPAND), 5);

    // Rx Out -----------------------------------------------------------------------

    wxStaticBoxSizer* sbSizer3;
    wxStaticBox* panelRxOutBox = new wxStaticBox(m_panelRx, wxID_ANY, _("Output From Computer To Speaker/Headphones"));
    sbSizer3 = new wxStaticBoxSizer(panelRxOutBox, wxHORIZONTAL);

    wxBoxSizer* bSizer81a = new wxBoxSizer(wxVERTICAL);

    m_listCtrlRxOutDevices = new wxListCtrl(panelRxOutBox, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_HRULES|wxLC_REPORT|wxLC_VRULES);
    bSizer81a->Add(m_listCtrlRxOutDevices, 1, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 1);

    wxBoxSizer* bSizer81;
    bSizer81 = new wxBoxSizer(wxHORIZONTAL);
    m_staticText9 = new wxStaticText(panelRxOutBox, wxID_ANY, _("Device:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText9->Wrap(-1);
    bSizer81->Add(m_staticText9, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 5);
    m_textCtrlRxOut = new wxTextCtrl(panelRxOutBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
    bSizer81->Add(m_textCtrlRxOut, 1, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 1);
    m_staticText10 = new wxStaticText(panelRxOutBox, wxID_ANY, _("Sample Rate:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText10->Wrap(-1);
    bSizer81->Add(m_staticText10, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 5);
    m_cbSampleRateRxOut = new wxComboBox(panelRxOutBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(100,-1), 0, NULL, wxCB_DROPDOWN);
    bSizer81->Add(m_cbSampleRateRxOut, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 1);

    bSizer81a->Add(bSizer81, 0, static_cast<int>(wxEXPAND), 5);

    sbSizer3->Add(bSizer81a, 1, static_cast<int>(wxEXPAND), 2);
    buildTestControls(&m_plotScalarRxOut, &m_btnRxOutTest, panelRxOutBox, sbSizer3, _("Play 2 Seconds"));
 
    gSizer4->Add(sbSizer3, 1, static_cast<int>(wxEXPAND), 2);
    bSizer20->Add(gSizer4, 1, static_cast<int>(wxEXPAND), 1);
    m_panelRx->SetSizer(bSizer20);
    m_panelRx->Layout();
    bSizer20->Fit(m_panelRx);
    m_notebook1->AddPage(m_panelRx, _("Receive"), true);

    // Tx Tab -------------------------------------------------------------------------------

    m_panelTx = new wxPanel(m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxBoxSizer* bSizer18;
    bSizer18 = new wxBoxSizer(wxVERTICAL);
    wxGridSizer* gSizer2;
    gSizer2 = new wxGridSizer(2, 1, 0, 0);

    // Tx In ----------------------------------------------------------------------------------

    wxStaticBoxSizer* sbSizer22;
    wxStaticBox* panelTxInBox = new wxStaticBox(m_panelTx, wxID_ANY, _("Input From Microphone To Computer"));
    sbSizer22 = new wxStaticBoxSizer(panelTxInBox, wxHORIZONTAL);

    wxBoxSizer* bSizer83a = new wxBoxSizer(wxVERTICAL);

    m_listCtrlTxInDevices = new wxListCtrl(panelTxInBox, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_HRULES|wxLC_REPORT|wxLC_VRULES);
    bSizer83a->Add(m_listCtrlTxInDevices, 1, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 1);
    wxBoxSizer* bSizer83;
    bSizer83 = new wxBoxSizer(wxHORIZONTAL);
    m_staticText12 = new wxStaticText(panelTxInBox, wxID_ANY, _("Device:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText12->Wrap(-1);
    bSizer83->Add(m_staticText12, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 5);
    m_textCtrlTxIn = new wxTextCtrl(panelTxInBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
    bSizer83->Add(m_textCtrlTxIn, 1, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 1);
    m_staticText11 = new wxStaticText(panelTxInBox, wxID_ANY, _("Sample Rate:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText11->Wrap(-1);
    bSizer83->Add(m_staticText11, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 5);
    m_cbSampleRateTxIn = new wxComboBox(panelTxInBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(100,-1), 0, NULL, wxCB_DROPDOWN);
    bSizer83->Add(m_cbSampleRateTxIn, 0, static_cast<int>(wxALL), 1);

    bSizer83a->Add(bSizer83, 0, static_cast<int>(wxEXPAND), 5);

    sbSizer22->Add(bSizer83a, 1, static_cast<int>(wxEXPAND), 2);
    buildTestControls(&m_plotScalarTxIn, &m_btnTxInTest, panelTxInBox, sbSizer22, _("Record 2 Seconds"));

    gSizer2->Add(sbSizer22, 1, static_cast<int>(wxEXPAND), 5);

    // Tx Out ----------------------------------------------------------------------------------

    wxStaticBoxSizer* sbSizer21;
    wxStaticBox* panelTxOutBox = new wxStaticBox(m_panelTx, wxID_ANY, _("Output From Computer To Radio"));
    sbSizer21 = new wxStaticBoxSizer(panelTxOutBox, wxHORIZONTAL);

    wxBoxSizer* bSizer82a = new wxBoxSizer(wxVERTICAL);

    m_listCtrlTxOutDevices = new wxListCtrl(panelTxOutBox, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_HRULES|wxLC_REPORT|wxLC_VRULES);
    bSizer82a->Add(m_listCtrlTxOutDevices, 1, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 2);
    wxBoxSizer* bSizer82;
    bSizer82 = new wxBoxSizer(wxHORIZONTAL);
    m_staticText81 = new wxStaticText(panelTxOutBox, wxID_ANY, _("Device:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText81->Wrap(-1);
    bSizer82->Add(m_staticText81, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 5);
    m_textCtrlTxOut = new wxTextCtrl(panelTxOutBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
    bSizer82->Add(m_textCtrlTxOut, 1, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 1);
    m_staticText71 = new wxStaticText(panelTxOutBox, wxID_ANY, _("Sample Rate:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText71->Wrap(-1);
    bSizer82->Add(m_staticText71, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL)|static_cast<int>(wxALL), 5);
    m_cbSampleRateTxOut = new wxComboBox(panelTxOutBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(100,-1), 0, NULL, wxCB_DROPDOWN);
    bSizer82->Add(m_cbSampleRateTxOut, 0, static_cast<int>(wxALL), 1);

    bSizer82a->Add(bSizer82, 0, static_cast<int>(wxEXPAND), 5);

    sbSizer21->Add(bSizer82a, 1, static_cast<int>(wxEXPAND), 2);
    buildTestControls(&m_plotScalarTxOut, &m_btnTxOutTest, panelTxOutBox, sbSizer21, _("Play 2 Seconds"));

    gSizer2->Add(sbSizer21, 1, static_cast<int>(wxEXPAND), 5);
    bSizer18->Add(gSizer2, 1, static_cast<int>(wxEXPAND), 1);
    m_panelTx->SetSizer(bSizer18);
    m_panelTx->Layout();
    bSizer18->Fit(m_panelTx);
    m_notebook1->AddPage(m_panelTx, _("Transmit"), false);

    bSizer4->Add(m_notebook1, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 0);
    m_panel1->SetSizer(bSizer4);
    m_panel1->Layout();
    bSizer4->Fit(m_panel1);
    mainSizer->Add(m_panel1, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 1);

    wxBoxSizer* bSizer6;
    bSizer6 = new wxBoxSizer(wxHORIZONTAL);
    m_btnRefresh = new wxButton(this, wxID_ANY, _("Refresh"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer6->Add(m_btnRefresh, 0, wxALIGN_CENTER|static_cast<int>(wxALL), 2);

    m_sdbSizer1 = new wxStdDialogButtonSizer();

    m_sdbSizer1OK = new wxButton(this, wxID_OK);
    m_sdbSizer1->AddButton(m_sdbSizer1OK);

    m_sdbSizer1Cancel = new wxButton(this, wxID_CANCEL);
    m_sdbSizer1->AddButton(m_sdbSizer1Cancel);

    m_sdbSizer1Apply = new wxButton(this, wxID_APPLY);
    m_sdbSizer1->AddButton(m_sdbSizer1Apply);

    m_sdbSizer1->Realize();

    bSizer6->Add(m_sdbSizer1, 1, static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    mainSizer->Add(bSizer6, 0, static_cast<int>(wxEXPAND), 2);
    this->SetSizerAndFit(mainSizer);
    this->Layout();
    this->Centre(wxBOTH);
//    this->Centre(wxBOTH);

    m_notebook1->SetSelection(0);

    m_RxInDevices.m_listDevices   = m_listCtrlRxInDevices;
    m_RxInDevices.direction       = AUDIO_IN;
    m_RxInDevices.m_textDevice    = m_textCtrlRxIn;
    m_RxInDevices.m_cbSampleRate  = m_cbSampleRateRxIn;

    m_RxOutDevices.m_listDevices  = m_listCtrlRxOutDevices;
    m_RxOutDevices.direction      = AUDIO_OUT;
    m_RxOutDevices.m_textDevice   = m_textCtrlRxOut;
    m_RxOutDevices.m_cbSampleRate = m_cbSampleRateRxOut;

    m_TxInDevices.m_listDevices   = m_listCtrlTxInDevices;
    m_TxInDevices.direction       = AUDIO_IN;
    m_TxInDevices.m_textDevice    = m_textCtrlTxIn;
    m_TxInDevices.m_cbSampleRate  = m_cbSampleRateTxIn;

    m_TxOutDevices.m_listDevices  = m_listCtrlTxOutDevices;
    m_TxOutDevices.direction      = AUDIO_OUT;
    m_TxOutDevices.m_textDevice   = m_textCtrlTxOut;
    m_TxOutDevices.m_cbSampleRate = m_cbSampleRateTxOut;

    populateParams(m_RxInDevices);
    populateParams(m_RxOutDevices);
    populateParams(m_TxInDevices);
    populateParams(m_TxOutDevices);

    // Load previously saved window size and position
    int l = wxGetApp().appConfiguration.audioConfigWindowLeft;
    int t = wxGetApp().appConfiguration.audioConfigWindowTop;
    if (l >= 0 && t >= 0)
    {
        Move(
            l,
            t);
    }
    
    wxSize sz = GetBestSize();
    int w = wxGetApp().appConfiguration.audioConfigWindowWidth;
    int h = wxGetApp().appConfiguration.audioConfigWindowHeight;
    if (w < sz.GetWidth()) w = sz.GetWidth();
    if (h < sz.GetHeight()) h = sz.GetHeight();
    SetClientSize(w, h);
    
    m_listCtrlRxInDevices->Connect( wxEVT_COMMAND_LIST_ITEM_SELECTED, wxListEventHandler( AudioOptsDialog::OnRxInDeviceSelect ), NULL, this );
    m_listCtrlRxOutDevices->Connect( wxEVT_COMMAND_LIST_ITEM_SELECTED, wxListEventHandler( AudioOptsDialog::OnRxOutDeviceSelect ), NULL, this );
    m_listCtrlTxInDevices->Connect( wxEVT_COMMAND_LIST_ITEM_SELECTED, wxListEventHandler( AudioOptsDialog::OnTxInDeviceSelect ), NULL, this );
    m_listCtrlTxOutDevices->Connect( wxEVT_COMMAND_LIST_ITEM_SELECTED, wxListEventHandler( AudioOptsDialog::OnTxOutDeviceSelect ), NULL, this );

    // wire up test buttons
    m_btnRxInTest->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AudioOptsDialog::OnRxInTest ), NULL, this );
    m_btnRxOutTest->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AudioOptsDialog::OnRxOutTest ), NULL, this );
    m_btnTxInTest->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AudioOptsDialog::OnTxInTest ), NULL, this );
    m_btnTxOutTest->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AudioOptsDialog::OnTxOutTest ), NULL, this );

    m_btnRefresh->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AudioOptsDialog::OnRefreshClick ), NULL, this );
    m_sdbSizer1Apply->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AudioOptsDialog::OnApplyAudioParameters ), NULL, this );
    m_sdbSizer1Cancel->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AudioOptsDialog::OnCancelAudioParameters ), NULL, this );
    m_sdbSizer1OK->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AudioOptsDialog::OnOkAudioParameters ), NULL, this );
/*
        void OnClose( wxCloseEvent& event ) { event.Skip(); }
        void OnHibernate( wxActivateEvent& event ) { event.Skip(); }
        void OnIconize( wxIconizeEvent& event ) { event.Skip(); }
        void OnInitDialog( wxInitDialogEvent& event ) { event.Skip(); }
*/
//    this->Connect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(AudioOptsDialog::OnClose));
    this->Connect(wxEVT_HIBERNATE, wxActivateEventHandler(AudioOptsDialog::OnHibernate));
    this->Connect(wxEVT_ICONIZE, wxIconizeEventHandler(AudioOptsDialog::OnIconize));
    this->Connect(wxEVT_INIT_DIALOG, wxInitDialogEventHandler(AudioOptsDialog::OnInitDialog));
}

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// ~AudioOptsDialog()
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
AudioOptsDialog::~AudioOptsDialog()
{
    // Save size and position
    auto pos = GetPosition();
    auto sz = GetClientSize();
    wxGetApp().appConfiguration.audioConfigWindowLeft = pos.x;
    wxGetApp().appConfiguration.audioConfigWindowTop = pos.y;
    wxGetApp().appConfiguration.audioConfigWindowWidth = sz.GetWidth();
    wxGetApp().appConfiguration.audioConfigWindowHeight = sz.GetHeight();

    AudioEngineFactory::GetAudioEngine()->stop();

    // Disconnect Events
    this->Disconnect(wxEVT_HIBERNATE, wxActivateEventHandler(AudioOptsDialog::OnHibernate));
    this->Disconnect(wxEVT_ICONIZE, wxIconizeEventHandler(AudioOptsDialog::OnIconize));
    this->Disconnect(wxEVT_INIT_DIALOG, wxInitDialogEventHandler(AudioOptsDialog::OnInitDialog));

    m_listCtrlRxInDevices->Disconnect(wxEVT_COMMAND_LIST_ITEM_SELECTED, wxListEventHandler(AudioOptsDialog::OnRxInDeviceSelect), NULL, this);
    m_listCtrlRxOutDevices->Disconnect(wxEVT_COMMAND_LIST_ITEM_SELECTED, wxListEventHandler(AudioOptsDialog::OnRxOutDeviceSelect), NULL, this);
    m_listCtrlTxInDevices->Disconnect(wxEVT_COMMAND_LIST_ITEM_SELECTED, wxListEventHandler(AudioOptsDialog::OnTxInDeviceSelect), NULL, this);
    m_listCtrlTxOutDevices->Disconnect(wxEVT_COMMAND_LIST_ITEM_SELECTED, wxListEventHandler(AudioOptsDialog::OnTxOutDeviceSelect), NULL, this);

    m_btnRxInTest->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AudioOptsDialog::OnRxInTest ), NULL, this );
    m_btnRxOutTest->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AudioOptsDialog::OnRxOutTest ), NULL, this );
    m_btnTxInTest->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AudioOptsDialog::OnTxInTest ), NULL, this );
    m_btnTxOutTest->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AudioOptsDialog::OnTxOutTest ), NULL, this );

    m_btnRefresh->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AudioOptsDialog::OnRefreshClick), NULL, this);
    m_sdbSizer1Apply->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AudioOptsDialog::OnApplyAudioParameters), NULL, this);
    m_sdbSizer1Cancel->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AudioOptsDialog::OnCancelAudioParameters), NULL, this);
    m_sdbSizer1OK->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AudioOptsDialog::OnOkAudioParameters), NULL, this);

}

//-------------------------------------------------------------------------
// OnInitDialog()
//-------------------------------------------------------------------------
void AudioOptsDialog::OnInitDialog( wxInitDialogEvent& )
{
    ExchangeData(EXCHANGE_DATA_IN);
}

//-------------------------------------------------------------------------
// setTextCtrlIfDevNameValid()
//
// helper function to look up name of devName, and if it exists write
// name to textCtrl.  Used to trap disappearing devices.
//-------------------------------------------------------------------------
bool AudioOptsDialog::setTextCtrlIfDevNameValid(wxTextCtrl *textCtrl, wxListCtrl *listCtrl, wxString const& devName)
{
    // ignore last list entry as it is the "none" entry
    for(int i = 0; i < listCtrl->GetItemCount() - 1; i++) 
    {
        if (listCtrl->GetItemText(i, 0).IsSameAs(devName))
        {
            textCtrl->SetValue(listCtrl->GetItemText(i, 0));
            log_debug("setting focus of %d", i);
            listCtrl->SetItemState(i, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
            return true;
        }
    }

    return false;
}

//-------------------------------------------------------------------------
// ExchangeData()
//-------------------------------------------------------------------------
int AudioOptsDialog::ExchangeData(int inout)
{
    if(inout == EXCHANGE_DATA_IN)
    {
        // Map sound card device numbers to tx/rx device numbers depending
        // on number of sound cards in use

        log_debug("EXCHANGE_DATA_IN:");
        log_debug("  g_nSoundCards: %d", g_nSoundCards.load(std::memory_order_acquire));

        if (g_nSoundCards.load(std::memory_order_acquire) == 0) {
            m_textCtrlRxIn ->SetValue("none");
            m_textCtrlRxOut->SetValue("none");
            m_textCtrlTxIn ->SetValue("none");
            m_textCtrlTxOut->SetValue("none");           
        }

        if (g_nSoundCards.load(std::memory_order_acquire) == 1) {
            log_debug("  m_soundCard1InSampleRate: %d", wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate.get());
            
            setTextCtrlIfDevNameValid(m_textCtrlRxIn, 
                                      m_listCtrlRxInDevices, 
                                      wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName);

            log_debug("  m_soundCard1OutSampleRate: %d", wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate.get());
            
            setTextCtrlIfDevNameValid(m_textCtrlRxOut, 
                                      m_listCtrlRxOutDevices, 
                                      wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName);

            if ((m_textCtrlRxIn->GetValue() != "none") && (m_textCtrlRxOut->GetValue() != "none")) {
                // Build sample rate dropdown lists
                buildListOfSupportedSampleRates(m_cbSampleRateRxIn, wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName, AUDIO_IN);
                buildListOfSupportedSampleRates(m_cbSampleRateRxOut, wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName, AUDIO_OUT);
                
                m_cbSampleRateRxIn->SetValue(wxString::Format(wxT("%i"), wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate.get()));
                m_cbSampleRateRxOut->SetValue(wxString::Format(wxT("%i"), wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate.get()));
            }

            m_textCtrlTxIn->SetValue("none");
            m_textCtrlTxOut->SetValue("none");           
        }

        if (g_nSoundCards.load(std::memory_order_acquire) == 2) {
            log_debug("  m_soundCard1InSampleRate: %d", wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate.get());
            
            setTextCtrlIfDevNameValid(m_textCtrlRxIn, 
                                      m_listCtrlRxInDevices, 
                                      wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName);
            
            log_debug("  m_soundCard2OutSampleRate: %d", wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate.get());
            
            setTextCtrlIfDevNameValid(m_textCtrlRxOut, 
                                      m_listCtrlRxOutDevices, 
                                      wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName);
            
            log_debug("  m_soundCard2InDeviceName: %d", wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate.get());
            
            setTextCtrlIfDevNameValid(m_textCtrlTxIn, 
                                      m_listCtrlTxInDevices, 
                                      wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName);
            
            log_debug("  m_soundCard1OutSampleRate: %d", wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate.get());
            
            setTextCtrlIfDevNameValid(m_textCtrlTxOut, 
                                      m_listCtrlTxOutDevices, 
                                      wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName);

            if ((m_textCtrlRxIn->GetValue() != "none") && (m_textCtrlTxOut->GetValue() != "none")) {
                // Build sample rate dropdown lists
                buildListOfSupportedSampleRates(m_cbSampleRateRxIn, wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName, AUDIO_IN);
                buildListOfSupportedSampleRates(m_cbSampleRateTxOut, wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName, AUDIO_OUT);
                
                m_cbSampleRateRxIn->SetValue(wxString::Format(wxT("%i"), wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate.get()));
                m_cbSampleRateTxOut->SetValue(wxString::Format(wxT("%i"), wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate.get()));
            }

            if ((m_textCtrlTxIn->GetValue() != "none") && (m_textCtrlRxOut->GetValue() != "none")) {
                // Build sample rate dropdown lists
                buildListOfSupportedSampleRates(m_cbSampleRateTxIn, wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName, AUDIO_IN);
                buildListOfSupportedSampleRates(m_cbSampleRateRxOut, wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName, AUDIO_OUT);
                
                m_cbSampleRateTxIn->SetValue(wxString::Format(wxT("%i"), wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate.get()));
                m_cbSampleRateRxOut->SetValue(wxString::Format(wxT("%i"), wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate.get()));
            }
        }
    }

    if(inout == EXCHANGE_DATA_OUT)
    {
        int valid_one_card_config = 0;
        int valid_two_card_config = 0;
        wxString sampleRate1, sampleRate2, sampleRate3, sampleRate4;

        // ---------------------------------------------------------------
        // check we have a valid 1 or 2 sound card configuration
        // ---------------------------------------------------------------

        // one sound card config, tx device names should be set to "none"
        wxString rxInAudioDeviceName = m_textCtrlRxIn->GetValue();
        wxString rxOutAudioDeviceName = m_textCtrlRxOut->GetValue();
        wxString txInAudioDeviceName = m_textCtrlTxIn->GetValue();
        wxString txOutAudioDeviceName = m_textCtrlTxOut->GetValue();
        
        if ((rxInAudioDeviceName != "none") && (rxOutAudioDeviceName != "none") &&
            (txInAudioDeviceName == "none") && (txOutAudioDeviceName == "none")) {
 
            valid_one_card_config = 1; 
            
            sampleRate1 = m_cbSampleRateRxIn->GetValue();
            sampleRate2 = m_cbSampleRateRxOut->GetValue();
        }

        // two card configuration

        if ((rxInAudioDeviceName != "none") && (rxOutAudioDeviceName != "none") &&
            (txInAudioDeviceName != "none") && (txOutAudioDeviceName != "none")) {

            valid_two_card_config = 1; 

            // Check we haven't doubled up on sound devices

            if (rxInAudioDeviceName == txInAudioDeviceName) {
                wxMessageBox(wxT("You must use different devices for From Radio and From Microphone"), wxT(""), wxOK);
                return -1;
            }

            if (rxOutAudioDeviceName == txOutAudioDeviceName) {
                wxMessageBox(wxT("You must use different devices for To Radio and To Speaker/Headphones"), wxT(""), wxOK);
                return -1;
            }

            sampleRate1 = m_cbSampleRateRxIn->GetValue();
            sampleRate2 = m_cbSampleRateRxOut->GetValue();
            sampleRate3 = m_cbSampleRateTxIn->GetValue();
            sampleRate4 = m_cbSampleRateTxOut->GetValue();
        }

        log_debug("  valid_one_card_config: %d  valid_two_card_config: %d", valid_one_card_config, valid_two_card_config);

        if (!valid_one_card_config && !valid_two_card_config) {
            wxMessageBox(wxT("Invalid one or two sound card configuration. For RX only, both devices in 'Receive' tab must be selected. Otherwise, all devices in both 'Receive' and 'Transmit' tabs must be selected."), wxT(""), wxOK);
            return -1;
        }

        // ---------------------------------------------------------------
        // Map Rx/TX device numbers to sound card device names used
        // in callbacks.
        // ---------------------------------------------------------------
        g_nSoundCards.store(0, std::memory_order_release);

        if (valid_one_card_config) {
            g_nSoundCards.store(1, std::memory_order_release);
            wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate = wxAtoi(sampleRate1);
            wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate = wxAtoi(sampleRate2);
            
            log_debug("  m_soundCard1InSampleRate: %d", wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate.get());
            log_debug("  m_soundCard1OutSampleRate: %d", wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate.get());
        }

        if (valid_two_card_config) {
            g_nSoundCards.store(2, std::memory_order_release);
            wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate = wxAtoi(sampleRate1);
            wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate = wxAtoi(sampleRate2);
            wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate = wxAtoi(sampleRate3);
            wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate = wxAtoi(sampleRate4);
            
            log_debug("  m_soundCard1InSampleRate: %d", wxGetApp().appConfiguration.audioConfiguration.soundCard1In.sampleRate.get());
            log_debug("  m_soundCard2OutSampleRate: %d", wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.sampleRate.get());
            log_debug("  m_soundCard2InSampleRate: %d", wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate.get());
            log_debug("  m_soundCard1OutSampleRate: %d", wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.sampleRate.get());
        }

        log_debug("  g_nSoundCards: %d", g_nSoundCards.load(std::memory_order_acquire));
        
        assert (pConfig != NULL);
        
        if (valid_one_card_config)
        {
            wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName = m_textCtrlRxIn->GetValue();
            wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName = m_textCtrlRxOut->GetValue();
            wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName = "none";
            wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName = "none";
        }
        else if (valid_two_card_config)
        {
            wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName = m_textCtrlRxIn->GetValue();
            wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName = m_textCtrlTxOut->GetValue();
            wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName = m_textCtrlTxIn->GetValue();
            wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName = m_textCtrlRxOut->GetValue();
        }
        else
        {
            wxGetApp().appConfiguration.audioConfiguration.soundCard1In.deviceName = "none";
            wxGetApp().appConfiguration.audioConfiguration.soundCard1Out.deviceName = "none";
            wxGetApp().appConfiguration.audioConfiguration.soundCard2In.deviceName = "none";
            wxGetApp().appConfiguration.audioConfiguration.soundCard2Out.deviceName = "none";
        }

        wxGetApp().appConfiguration.save(pConfig);        
    }

    return 0;
}

//-------------------------------------------------------------------------
// buildListOfSupportedSampleRates()
//-------------------------------------------------------------------------
int AudioOptsDialog::buildListOfSupportedSampleRates(wxComboBox *cbSampleRate, wxString const& devName, int in_out)
{
    auto engine = AudioEngineFactory::GetAudioEngine();
    auto deviceList = engine->getAudioDeviceList(in_out == AUDIO_IN ? IAudioEngine::AUDIO_ENGINE_IN : IAudioEngine::AUDIO_ENGINE_OUT);
    wxString str;
    int numSampleRates = 0;
    
    cbSampleRate->Clear();
    for (auto& dev : deviceList)
    {
        if (dev.name.IsSameAs(devName))
        {
            auto supportedSampleRates =
                engine->getSupportedSampleRates(
                    dev.name, 
                    in_out == AUDIO_IN ? IAudioEngine::AUDIO_ENGINE_IN : IAudioEngine::AUDIO_ENGINE_OUT);
                    
            for (auto& rate : supportedSampleRates)
            {
                str.Printf("%i", rate);
                cbSampleRate->AppendString(str);
            }
            numSampleRates = supportedSampleRates.size();
        }
    }

    return numSampleRates;
}

//-------------------------------------------------------------------------
// populateParams()
//-------------------------------------------------------------------------
void AudioOptsDialog::populateParams(AudioInfoDisplay ai)
{
    wxListCtrl* ctrl    = ai.m_listDevices;
    int         in_out  = ai.direction;
    wxListItem  listItem;
    wxString    buf;
    int         col = 0, idx;

    auto engine = AudioEngineFactory::GetAudioEngine();
    auto devList = engine->getAudioDeviceList(in_out == AUDIO_IN ? IAudioEngine::AUDIO_ENGINE_IN : IAudioEngine::AUDIO_ENGINE_OUT);
    
    if(ctrl->GetColumnCount() > 0)
    {
        ctrl->ClearAll();
    }

    listItem.SetAlign(wxLIST_FORMAT_LEFT);
    listItem.SetText(wxT("Device"));
    ctrl->InsertColumn(col, listItem);
    ctrl->SetColumnWidth(col++, 300);

    listItem.SetAlign(wxLIST_FORMAT_CENTRE);
    listItem.SetText(wxT("ID"));
    ctrl->InsertColumn(col, listItem);
    ctrl->SetColumnWidth(col++, 45);

    listItem.SetAlign(wxLIST_FORMAT_LEFT);
    listItem.SetText(wxT("API"));
    ctrl->InsertColumn(col, listItem);
    ctrl->SetColumnWidth(col++, 100);

    if(in_out == AUDIO_IN)
    {
        listItem.SetAlign(wxLIST_FORMAT_CENTRE);
        listItem.SetText(wxT("Default Sample Rate"));
        ctrl->InsertColumn(col, listItem);
        ctrl->SetColumnWidth(col++, 160);
    }
    else if(in_out == AUDIO_OUT)
    {
        listItem.SetAlign(wxLIST_FORMAT_CENTRE);
        listItem.SetText(wxT("Default Sample Rate"));
        ctrl->InsertColumn(col, listItem);
        ctrl->SetColumnWidth(col++, 160);
    }

    for(auto& dev : devList)
    {
        col = 0;
        buf.Printf(wxT("%s"), dev.name);
        idx = ctrl->InsertItem(ctrl->GetItemCount(), buf);
        col++;
            
        buf.Printf(wxT("%d"), dev.deviceId);
        ctrl->SetItem(idx, col++, buf);

        buf.Printf(wxT("%s"), dev.apiName);
        ctrl->SetItem(idx, col++, buf);

        buf.Printf(wxT("%i"), dev.defaultSampleRate);
        ctrl->SetItem(idx, col++, buf);
    }

    // add "none" option at end

    buf.Printf(wxT("%s"), "none");
    ctrl->InsertItem(ctrl->GetItemCount(), buf);
    
    // Auto-size column widths to improve readability
    for (int col = 0; col < 4; col++)
    {
        ctrl->SetColumnWidth(col, wxLIST_AUTOSIZE_USEHEADER);
    }
}

//-------------------------------------------------------------------------
// OnDeviceSelect()
//
// helper function to set up "Device:" and "Sample Rate:" fields when
// we click on a line in the list of devices box
//-------------------------------------------------------------------------
void AudioOptsDialog::OnDeviceSelect(wxComboBox *cbSampleRate, 
                                     wxTextCtrl *textCtrl, 
                                     wxListCtrl *listCtrlDevices, 
                                     int         index,
                                     int         in_out)
{

    wxString devName = listCtrlDevices->GetItemText(index, 0);
     if (devName.IsSameAs("none")) {
        textCtrl->SetValue("none");
    }
    else {
        textCtrl->SetValue(devName);

        int numSampleRates = buildListOfSupportedSampleRates(cbSampleRate, devName, in_out);
        if (numSampleRates) {
            wxString defSampleRate = listCtrlDevices->GetItemText(index, 3);        
            cbSampleRate->SetValue(defSampleRate);
        }
        else {
             cbSampleRate->SetValue("None");           
        }
    }
}

//-------------------------------------------------------------------------
// OnRxInDeviceSelect()
//-------------------------------------------------------------------------
void AudioOptsDialog::OnRxInDeviceSelect(wxListEvent& evt)
{
    OnDeviceSelect(m_cbSampleRateRxIn, 
                   m_textCtrlRxIn, 
                   m_listCtrlRxInDevices, 
                   evt.GetIndex(),
                   AUDIO_IN);
}

//-------------------------------------------------------------------------
// OnRxOutDeviceSelect()
//-------------------------------------------------------------------------
void AudioOptsDialog::OnRxOutDeviceSelect(wxListEvent& evt)
{
    OnDeviceSelect(m_cbSampleRateRxOut, 
                   m_textCtrlRxOut, 
                   m_listCtrlRxOutDevices, 
                   evt.GetIndex(),
                   AUDIO_OUT);
}

//-------------------------------------------------------------------------
// OnTxInDeviceSelect()
//-------------------------------------------------------------------------
void AudioOptsDialog::OnTxInDeviceSelect(wxListEvent& evt)
{
    OnDeviceSelect(m_cbSampleRateTxIn, 
                   m_textCtrlTxIn, 
                   m_listCtrlTxInDevices, 
                   evt.GetIndex(),
                   AUDIO_IN);
}

//-------------------------------------------------------------------------
// OnTxOutDeviceSelect()
//-------------------------------------------------------------------------
void AudioOptsDialog::OnTxOutDeviceSelect(wxListEvent& evt)
{
    OnDeviceSelect(m_cbSampleRateTxOut, 
                   m_textCtrlTxOut, 
                   m_listCtrlTxOutDevices, 
                   evt.GetIndex(),
                   AUDIO_OUT);
}

void AudioOptsDialog::UpdatePlot(PlotScalar *plotScalar)
{
    plotScalar->Refresh();
    plotScalar->Update();
}

//-------------------------------------------------------------------------
// plotDeviceInputForAFewSecs()
//
// opens a record device and plots the input speech for a few seconds.  This is "modal" using
// synchronous portaudio functions, so the GUI will not respond until after test sample has been
// taken
//-------------------------------------------------------------------------
void AudioOptsDialog::plotDeviceInputForAFewSecs(wxString const& devName, PlotScalar *ps) {
    m_btnRxInTest->Enable(false);
    m_btnRxOutTest->Enable(false);
    m_btnTxInTest->Enable(false);
    m_btnTxOutTest->Enable(false);
    
    m_audioPlotThread = new std::thread([&](wxString const& devName, PlotScalar* ps) {
        std::mutex callbackFifoMutex;
        std::condition_variable callbackFifoCV;
        GenericFIFO<short>               *fifo, *callbackFifo;

        // Reset plot before starting.
        CallAfter([&]() {
            ps->clearSamples();
            ps->Refresh();
        });

        fifo = new GenericFIFO<short>((int)(DT*TEST_WAVEFORM_PLOT_FS*2)); assert(fifo != NULL);

        auto engine = AudioEngineFactory::GetAudioEngine();
        auto devList = engine->getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_IN);
        for (auto& devInfo : devList)
        {
            if (devInfo.name.IsSameAs(devName))
            {
                int sampleCount = 0;
                int sampleRate = wxAtoi(m_cbSampleRateRxIn->GetValue());
                ResampleStep resampler(sampleRate, 8000);
                auto device = engine->getAudioDevice(
                    devInfo.name, 
                    IAudioEngine::AUDIO_ENGINE_IN, 
                    sampleRate,
                    1);
                
                if (device)
                {
                    bool running = true;
                    callbackFifo = new GenericFIFO<short>(sampleRate);
                    assert(callbackFifo != nullptr);

                    AudioDeviceCapture capture { .fifo = callbackFifo, .cv = &callbackFifoCV, .running = &running, .n = nullptr };
                    device->setOnAudioData([](IAudioDevice&, void* data, size_t numSamples, void* state) FREEDV_NONBLOCKING {
                        AudioDeviceCapture* castedState = (AudioDeviceCapture*)state;

                        if (*castedState->running && data != nullptr)
                        {
                            short* in48k_short = static_cast<short*>(data);
                            castedState->fifo->write(in48k_short, numSamples);
                        }
                        castedState->cv->notify_one();
                    }, &capture);
                   
                    device->setDescription("Device Input Test");
                    device->start();

                    while(sampleCount < (TEST_WAVEFORM_PLOT_TIME * TEST_WAVEFORM_PLOT_FS))
                    {
                        short               in8k_short[TEST_BUF_SIZE];
                        short               in48k_short[TEST_BUF_SIZE];

                        memset(in8k_short, 0, sizeof(in8k_short));
                        memset(in48k_short, 0, sizeof(in48k_short));

                        {
                            if (callbackFifo->read(in48k_short, TEST_BUF_SIZE))
                            {
                                std::unique_lock<std::mutex> callbackFifoLock(callbackFifoMutex);
                                callbackFifoCV.wait_for(callbackFifoLock, 10ms);
                                continue;
                            }
                        }
                    
                        int n8k = 0;
                        short* resampled = resampler.execute(in48k_short, TEST_BUF_SIZE, &n8k);
                        short* tmp = new short[n8k];
                        assert(tmp != nullptr);
                        resample_for_plot(fifo, resampled, tmp, n8k, FS);
                        delete[] tmp;
 
                        short plotSamples[TEST_WAVEFORM_PLOT_BUF];
                        if (fifo->read(plotSamples, TEST_WAVEFORM_PLOT_BUF))
                        {
                            // come back when the fifo is refilled
                            continue;
                        }

                        std::mutex plotUpdateMtx;
                        std::condition_variable plotUpdateCV;
                        CallAfter([&]() {
                            {
                                ps->add_new_short_samples(plotSamples, TEST_WAVEFORM_PLOT_BUF, 32767);
                                UpdatePlot(ps);
                            }
                            plotUpdateCV.notify_one();
                        });
                        {
                            std::unique_lock<std::mutex> plotUpdateLock(plotUpdateMtx);
                            plotUpdateCV.wait_for(plotUpdateLock, 100ms);
                        } 
                        sampleCount += TEST_WAVEFORM_PLOT_BUF;
                    }
   
                    running = false; 
                    device->stop();
                    delete callbackFifo;
                }
                break;
            }
        }

        delete fifo;

        CallAfter([&]() {
            m_audioPlotThread->join();
            delete m_audioPlotThread;
            m_audioPlotThread = nullptr;

            m_btnRxInTest->Enable(true);
            m_btnRxOutTest->Enable(true);
            m_btnTxInTest->Enable(true);
            m_btnTxOutTest->Enable(true);
        });
    }, devName, ps);
    
}

//-------------------------------------------------------------------------
// plotDeviceOutputForAFewSecs()
//
// opens a play device and plays a tone for a few seconds.  This is "modal" using
// synchronous portaudio functions, so the GUI will not respond until after test sample has been
// taken.  Also plots a pretty picture like the record versions
//-------------------------------------------------------------------------
void AudioOptsDialog::plotDeviceOutputForAFewSecs(wxString const& devName, PlotScalar *ps) {
    m_btnRxInTest->Enable(false);
    m_btnRxOutTest->Enable(false);
    m_btnTxInTest->Enable(false);
    m_btnTxOutTest->Enable(false);
    
    m_audioPlotThread = new std::thread([&](wxString const& devName, PlotScalar* ps) {
        GenericFIFO<short>               *fifo, *callbackFifo;
        int n = 0;

        // Reset plot before starting.
        CallAfter([&]() {
            ps->clearSamples();
            ps->Refresh();
        });

        fifo = new GenericFIFO<short>((int)(DT*TEST_WAVEFORM_PLOT_FS*2)); assert(fifo != NULL);

        auto engine = AudioEngineFactory::GetAudioEngine();
        auto devList = engine->getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_OUT);
        for (auto& devInfo : devList)
        {
            if (devInfo.name.IsSameAs(devName))
            {
                int sampleCount = 0;
                int sampleRate = wxAtoi(m_cbSampleRateRxIn->GetValue());
                ResampleStep resampler(sampleRate, 8000);
                auto device = engine->getAudioDevice(
                    devInfo.name, 
                    IAudioEngine::AUDIO_ENGINE_OUT, 
                    sampleRate,
                    1);
                
                if (device)
                {
                    std::mutex callbackFifoMutex;
                    std::condition_variable callbackFifoCV;
                    bool running = true;
 
                    callbackFifo = new GenericFIFO<short>(sampleRate);
                    assert(callbackFifo != nullptr);
                   
                    AudioDeviceCapture capture { .fifo = callbackFifo, .cv = &callbackFifoCV, .running = &running, .n = &n };
                    device->setOnAudioData([](IAudioDevice& dev, void* data, size_t numSamples, void* state) FREEDV_NONBLOCKING {
                        AudioDeviceCapture* castedState = (AudioDeviceCapture*)state;

                        if (*castedState->running && data != nullptr)
                        {
                            short* out48k_short = static_cast<short*>(data);
                            for(size_t j = 0; j < numSamples; j++, (*castedState->n)++) 
                            {
                                out48k_short[j] = 2000.0*cos(6.2832*(*castedState->n)*400.0/dev.getSampleRate());
                            }
                    
                            castedState->fifo->write(out48k_short, numSamples);
                        }
                        castedState->cv->notify_one();
                    }, &capture);

                    device->setDescription("Device Output Test");
                    device->start();
                    
                    while(sampleCount < (TEST_WAVEFORM_PLOT_TIME * TEST_WAVEFORM_PLOT_FS))
                    {
                        short               out8k_short[TEST_BUF_SIZE];
                        short               out48k_short[TEST_BUF_SIZE];

                        memset(out8k_short, 0, sizeof(out8k_short));
                        memset(out48k_short, 0, sizeof(out48k_short));

                        {
                            if (callbackFifo->read(out48k_short, TEST_BUF_SIZE))
                            {
                                std::unique_lock<std::mutex> callbackFifoLock(callbackFifoMutex);
                                callbackFifoCV.wait_for(callbackFifoLock, 10ms);
                                continue;
                            }
                        }
                    
                        int n8k = 0;
                        short* resampled = resampler.execute(out48k_short, TEST_BUF_SIZE, &n8k);
                        short* tmp = new short[n8k];
                        assert(tmp != nullptr);
                        resample_for_plot(fifo, resampled, tmp, n8k, FS);
                        delete[] tmp;
 
                        short plotSamples[TEST_WAVEFORM_PLOT_BUF];
                        if (fifo->read(plotSamples, TEST_WAVEFORM_PLOT_BUF))
                        {
                            // come back when the fifo is refilled
                            continue;
                        }
    
                        std::mutex plotUpdateMtx;
                        std::condition_variable plotUpdateCV;
                        CallAfter([&]() {
                            {
                                ps->add_new_short_samples(plotSamples, TEST_WAVEFORM_PLOT_BUF, 32767);
                                UpdatePlot(ps);
                            }
                            plotUpdateCV.notify_one();
                        });
                        {
                            std::unique_lock<std::mutex> plotUpdateLock(plotUpdateMtx);
                            plotUpdateCV.wait_for(plotUpdateLock, 100ms);
                        } 
                        sampleCount += TEST_WAVEFORM_PLOT_BUF;
                    }

                    running = false;    
                    device->stop();
                    delete callbackFifo;
                }
                break;
            }
        }
        
        delete fifo;

        CallAfter([&]() {
            m_audioPlotThread->join();
            delete m_audioPlotThread;
            m_audioPlotThread = nullptr;

            m_btnRxInTest->Enable(true);
            m_btnRxOutTest->Enable(true);
            m_btnTxInTest->Enable(true);
            m_btnTxOutTest->Enable(true);
        });
    }, devName, ps);
}

//-------------------------------------------------------------------------
// OnRxInTest()
//-------------------------------------------------------------------------
void AudioOptsDialog::OnRxInTest(wxCommandEvent&)
{
    plotDeviceInputForAFewSecs(m_textCtrlRxIn->GetValue(), m_plotScalarRxIn);
}

//-------------------------------------------------------------------------
// OnRxOutTest()
//-------------------------------------------------------------------------
void AudioOptsDialog::OnRxOutTest(wxCommandEvent&)
{
    plotDeviceOutputForAFewSecs(m_textCtrlRxOut->GetValue(), m_plotScalarRxOut);
}

//-------------------------------------------------------------------------
// OnTxInTest()
//-------------------------------------------------------------------------
void AudioOptsDialog::OnTxInTest(wxCommandEvent&)
{
    plotDeviceInputForAFewSecs(m_textCtrlTxIn->GetValue(), m_plotScalarTxIn);
}

//-------------------------------------------------------------------------
// OnTxOutTest()
//-------------------------------------------------------------------------
void AudioOptsDialog::OnTxOutTest(wxCommandEvent&)
{
    plotDeviceOutputForAFewSecs(m_textCtrlTxOut->GetValue(), m_plotScalarTxOut);
}

//-------------------------------------------------------------------------
// OnRefreshClick()
//-------------------------------------------------------------------------
void AudioOptsDialog::OnRefreshClick(wxCommandEvent&)
{
    // restart audio engine, to re-sample available devices
    auto engine = AudioEngineFactory::GetAudioEngine();
    engine->stop();
    engine->start();

    m_notebook1->SetSelection(0);
    populateParams(m_RxInDevices);
    populateParams(m_RxOutDevices);
    populateParams(m_TxInDevices);
    populateParams(m_TxOutDevices);

    // some devices may have disappeared, so possibly change sound
    // card config

    ExchangeData(EXCHANGE_DATA_IN);
}

//-------------------------------------------------------------------------
// OnApplyAudioParameters()
//-------------------------------------------------------------------------
void AudioOptsDialog::OnApplyAudioParameters(wxCommandEvent&)
{
    ExchangeData(EXCHANGE_DATA_OUT);
}

//-------------------------------------------------------------------------
// OnCancelAudioParameters()
//-------------------------------------------------------------------------
void AudioOptsDialog::OnCancelAudioParameters(wxCommandEvent&)
{
    if(m_isPaInitialized)
    {
        auto engine = AudioEngineFactory::GetAudioEngine();
        engine->stop();
        engine->setOnEngineError(nullptr, nullptr);
        m_isPaInitialized = false;
    }
    EndModal(wxCANCEL);
}

//-------------------------------------------------------------------------
// OnOkAudioParameters()
//-------------------------------------------------------------------------
void AudioOptsDialog::OnOkAudioParameters(wxCommandEvent&)
{
    int status = ExchangeData(EXCHANGE_DATA_OUT);

    // We only accept OK if config successful

    log_debug("status: %d m_isPaInitialized: %d", status, m_isPaInitialized);
    if (status == 0) {
        if(m_isPaInitialized)
        {
            auto engine = AudioEngineFactory::GetAudioEngine();
            engine->stop();
            engine->setOnEngineError(nullptr, nullptr);
            m_isPaInitialized = false;
        }
        EndModal(wxOK);
    }

}
