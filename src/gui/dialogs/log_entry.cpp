//==========================================================================
// Name:            log_entry.cpp
// Purpose:         Dialog for confirming log entry.
// Created:         Dec 17, 2025
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

#include "../../main.h"
#include "../../logging/WSJTXNetworkLogger.h"
#include <wx/wx.h>
#include <wx/valnum.h>
#include "log_entry.h"

LogEntryDialog::LogEntryDialog(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) 
    : wxDialog(parent, id, title, pos, size, style)
{    
    WXWIDGETS_SET_LAYOUT_DIRECTION;
       
    // Create top-level of control hierarchy.
    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* sectionSizer = new wxBoxSizer(wxVERTICAL);
    
    wxStaticBox* logEntryBox = new wxStaticBox(panel, wxID_ANY, _("Log Entry"));
    wxStaticBoxSizer* logEntryBoxSizer = new wxStaticBoxSizer(logEntryBox, wxVERTICAL);

    wxFlexGridSizer* gridSizerLogEntry = new wxFlexGridSizer(10, 2, 5, 0);

    // Log entry fields
    wxStaticText* labelMyCall = new wxStaticText(logEntryBox, wxID_ANY, _("Your Call:"), wxDefaultPosition, wxSize(125,-1), 0);
    gridSizerLogEntry->Add(labelMyCall, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);

    wxStaticText* labelMyCallVal = new wxStaticText(logEntryBox, wxID_ANY, wxGetApp().appConfiguration.reportingConfiguration.reportingCallsign, wxDefaultPosition, wxDefaultSize, 0);
    gridSizerLogEntry->Add(labelMyCallVal, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    wxStaticText* labelMyLocator = new wxStaticText(logEntryBox, wxID_ANY, _("Your Locator:"), wxDefaultPosition, wxSize(125,-1), 0);
    gridSizerLogEntry->Add(labelMyLocator, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);

    wxStaticText* labelMyLocatorVal = new wxStaticText(logEntryBox, wxID_ANY, wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare, wxDefaultPosition, wxDefaultSize, 0);
    gridSizerLogEntry->Add(labelMyLocatorVal, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    wxStaticText* labelTime = new wxStaticText(logEntryBox, wxID_ANY, _("Time (UTC):"), wxDefaultPosition, wxSize(125,-1), 0);
    gridSizerLogEntry->Add(labelTime, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);

    labelTimeVal_ = new wxStaticText(logEntryBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    gridSizerLogEntry->Add(labelTimeVal_, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    wxStaticText* labelDxCall = new wxStaticText(logEntryBox, wxID_ANY, _("DX Call:"), wxDefaultPosition, wxSize(125,-1), 0);
    gridSizerLogEntry->Add(labelDxCall, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);

    dxCall_ = new wxTextCtrl(logEntryBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(100, -1), 0);
    gridSizerLogEntry->Add(dxCall_, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    wxStaticText* labelDxGrid = new wxStaticText(logEntryBox, wxID_ANY, _("DX Locator:"), wxDefaultPosition, wxSize(125,-1), 0);
    gridSizerLogEntry->Add(labelDxGrid, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);

    dxGrid_ = new wxTextCtrl(logEntryBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(100, -1), 0);
    gridSizerLogEntry->Add(dxGrid_, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    labelFrequency_ = new wxStaticText(logEntryBox, wxID_ANY, _("Frequency (Hz):"), wxDefaultPosition, wxSize(125,-1), 0);
    gridSizerLogEntry->Add(labelFrequency_, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);

    frequency_ = new wxTextCtrl(logEntryBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(100, -1), 0);
    gridSizerLogEntry->Add(frequency_, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);

    logEntryBoxSizer->Add(gridSizerLogEntry, 0, static_cast<int>(wxEXPAND) | wxALIGN_LEFT, 2);
    sectionSizer->Add(logEntryBoxSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
    
    wxStaticText* labelTxReport = new wxStaticText(logEntryBox, wxID_ANY, _("TX Report:"), wxDefaultPosition, wxSize(125,-1), 0);
    gridSizerLogEntry->Add(labelTxReport, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);

    txReport_ = new wxTextCtrl(logEntryBox, wxID_ANY, _("59"), wxDefaultPosition, wxSize(50, -1), 0);
    gridSizerLogEntry->Add(txReport_, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    wxStaticText* labelRxReport = new wxStaticText(logEntryBox, wxID_ANY, _("RX Report:"), wxDefaultPosition, wxSize(125,-1), 0);
    gridSizerLogEntry->Add(labelRxReport, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);

    rxReport_ = new wxTextCtrl(logEntryBox, wxID_ANY, _("59"), wxDefaultPosition, wxSize(50, -1), 0);
    gridSizerLogEntry->Add(rxReport_, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    wxStaticText* labelName = new wxStaticText(logEntryBox, wxID_ANY, _("Name:"), wxDefaultPosition, wxSize(125,-1), 0);
    gridSizerLogEntry->Add(labelName, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);

    name_ = new wxTextCtrl(logEntryBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(125, -1), 0);
    gridSizerLogEntry->Add(name_, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    wxStaticText* labelComments = new wxStaticText(logEntryBox, wxID_ANY, _("Comments:"), wxDefaultPosition, wxSize(125,-1), 0);
    gridSizerLogEntry->Add(labelComments, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT, 2);

    comments_ = new wxTextCtrl(logEntryBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(125, -1), 0);
    gridSizerLogEntry->Add(comments_, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    // OK/Cancel buttons
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

    m_buttonOK = new wxButton(panel, wxID_OK);
    buttonSizer->Add(m_buttonOK, 0, static_cast<int>(wxALL), 2);

    m_buttonCancel = new wxButton(panel, wxID_CANCEL);
    buttonSizer->Add(m_buttonCancel, 0, static_cast<int>(wxALL), 2);

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

    // Names for automated GUI testing.
    SetName("logEntry");
    labelMyCallVal->SetName("logEntry.myCall");
    labelMyLocatorVal->SetName("logEntry.myLocator");
    labelTimeVal_->SetName("logEntry.time");
    dxCall_->SetName("logEntry.dxCall");
    dxGrid_->SetName("logEntry.dxLocator");
    labelFrequency_->SetName("logEntry.labelFrequency");
    frequency_->SetName("logEntry.frequency");
    txReport_->SetName("logEntry.txReport");
    rxReport_->SetName("logEntry.rxReport");
    name_->SetName("logEntry.name");
    comments_->SetName("logEntry.comments");
    m_buttonOK->SetName("logEntry.btnOK");
    m_buttonCancel->SetName("logEntry.btnCancel");
    
    // Hook in events
    this->Connect(wxEVT_INIT_DIALOG, wxInitDialogEventHandler(LogEntryDialog::OnInitDialog));
    this->Connect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(LogEntryDialog::OnClose));
       
    m_buttonOK->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(LogEntryDialog::OnOK), NULL, this);
    m_buttonCancel->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(LogEntryDialog::OnCancel), NULL, this);
}

LogEntryDialog::~LogEntryDialog()
{
    logger_ = nullptr;

    this->Disconnect(wxEVT_INIT_DIALOG, wxInitDialogEventHandler(LogEntryDialog::OnInitDialog));
    this->Disconnect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(LogEntryDialog::OnClose));
       
    m_buttonOK->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(LogEntryDialog::OnOK), NULL, this);
    m_buttonCancel->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(LogEntryDialog::OnCancel), NULL, this);
}

void LogEntryDialog::OnInitDialog(wxInitDialogEvent&)
{
    // empty
}

void LogEntryDialog::ShowDialog(wxString const& dxCall, wxString const& dxGrid, wxDateTime const& logTime, int64_t freqHz, int snr)
{
    logTime_ = logTime;
    labelTimeVal_->SetLabel(logTime_.ToUTC().FormatISOTime());
    snr_ = snr;
    
    logger_ = wxGetApp().logger;
    dxCall_->SetValue(dxCall);
    dxGrid_->SetValue(dxGrid);
    
    double freqDouble = freqHz;
    int precision = 0;
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
    {
        freqDouble /= 1000;
        precision = 1;
        labelFrequency_->SetLabel(_("Frequency (kHz):"));
    }
    else
    {
        freqDouble /= 1000000;
        precision = 4;
        labelFrequency_->SetLabel(_("Frequency (MHz):"));
    }

    wxString freqString = wxNumberFormatter::ToString(freqDouble, precision);
    frequency_->SetValue(freqString);

    ShowModal();
}

void LogEntryDialog::OnOK(wxCommandEvent&)
{
    int64_t freqHz = 0;

    if (frequency_->GetValue() == "" ||
        dxCall_->GetValue() == "")
    {
        wxMessageBox( _("Frequency and DX Call must be filled in in order to log the contact."), _("Error"), wxICON_ERROR);
    }
    else
    {
        // Forward to logger
        double freqDouble = 0;
        wxNumberFormatter::FromString(frequency_->GetValue(), &freqDouble);
        
        if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
        {
            freqDouble *= 1000;
        }
        else
        {
            freqDouble *= 1000000;
        }
        freqHz = (int64_t)freqDouble;
        
        std::time_t timeSinceUnixEpoch = logTime_.GetTicks();

        auto logger = logger_;
        if (logger == nullptr && wxGetApp().appConfiguration.reportingConfiguration.udpReportingEnabled)
        {
            logger = std::make_shared<WSJTXNetworkLogger>(
                (const char*)wxGetApp().appConfiguration.reportingConfiguration.udpReportingHostname.get().ToUTF8(),
                wxGetApp().appConfiguration.reportingConfiguration.udpReportingPort);
        }
        if (logger != nullptr)
        {
            logger->logContact(
                std::chrono::system_clock::from_time_t(timeSinceUnixEpoch),
                (const char*)dxCall_->GetValue().ToUTF8(),
                (const char*)dxGrid_->GetValue().ToUTF8(),
                (const char*)wxGetApp().appConfiguration.reportingConfiguration.reportingCallsign.get().ToUTF8(),
                (const char*)wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare.get().ToUTF8(),
                freqHz,
                (const char*)rxReport_->GetValue().ToUTF8(),
                (const char*)txReport_->GetValue().ToUTF8(),
                (const char*)name_->GetValue().ToUTF8(),
                (const char*)comments_->GetValue().ToUTF8(),
                snr_);
        }
        this->EndModal(wxOK);
    }
}

void LogEntryDialog::OnCancel(wxCommandEvent&)
{
    this->EndModal(wxCANCEL);
}

void LogEntryDialog::OnClose(wxCloseEvent&)
{
    this->EndModal(wxCANCEL);
}
