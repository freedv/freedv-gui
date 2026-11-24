//==========================================================================
// Name:            dlg_setup_wizard.cpp
// Purpose:         First-run setup wizard dialog
// Authors:         Mooneer Salem
//
// License:
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2.1,
//  as published by the Free Software Foundation.
//
//==========================================================================

#include <algorithm>
#include <climits>
#include <vector>

#include "dlg_setup_wizard.h"

#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/log.h>

#ifdef __WXMSW__
#include <wx/msw/registry.h>
#else
#include <glob.h>
#include <string.h>
#endif

#include "audio/AudioEngineFactory.h"
#include "audio/IAudioDevice.h"
#include "rig_control/HamlibRigController.h"

extern wxConfigBase* pConfig;

// --------------------------------------------------------------------------
// helpers
// --------------------------------------------------------------------------

static wxSizer* makePageHeader(wxWindow* parent, const wxString& title, const wxString& desc)
{
    wxBoxSizer* hs = new wxBoxSizer(wxVERTICAL);
    wxStaticText* t = new wxStaticText(parent, wxID_ANY, title);
    wxFont f = t->GetFont();
    f.SetWeight(wxFONTWEIGHT_BOLD);
    f.SetPointSize(f.GetPointSize() + 2);
    t->SetFont(f);
    hs->Add(t, 0, wxBOTTOM, 4);
    hs->Add(new wxStaticText(parent, wxID_ANY, desc), 0, wxBOTTOM, 8);
    hs->Add(new wxStaticLine(parent), 0, static_cast<int>(wxEXPAND) | wxBOTTOM, 8);
    return hs;
}

// --------------------------------------------------------------------------
// SetupWizard constructor
// --------------------------------------------------------------------------

SetupWizard::SetupWizard(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _("FreeDV Setup Wizard"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_currentPage(0)
{
    WXWIDGETS_SET_LAYOUT_DIRECTION;
    
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    // Look for other programs' settings before building page 0, since
    // the import controls are only shown if something is found.
    findImportSources();

    // Book
    m_book = new wxSimplebook(this, wxID_ANY);
    m_book->AddPage(makeReceivePage(),   wxEmptyString);
    m_book->AddPage(makeTxPage(),        wxEmptyString);
    m_book->AddPage(makeRadioPage(),     wxEmptyString);
    m_book->AddPage(makeReportingPage(), wxEmptyString);
    topSizer->Add(m_book, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 10);

    // Navigation bar
    topSizer->Add(new wxStaticLine(this), 0, static_cast<int>(wxEXPAND) | wxLEFT | wxRIGHT, 10);
    wxBoxSizer* navSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* btnCancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
    btnCancel->SetName("wizard.btnCancel");
    m_btnPrev   = new wxButton(this, wxID_ANY, _("< Previous"));
    m_btnNext   = new wxButton(this, wxID_ANY, _("Next >"));
    m_btnFinish = new wxButton(this, wxID_ANY, _("Finish"));
    navSizer->Add(btnCancel,   0, wxRIGHT, 4);
    navSizer->AddStretchSpacer();
    navSizer->Add(m_btnPrev,   0, wxRIGHT, 4);
    navSizer->Add(m_btnNext,   0, wxRIGHT, 4);
    navSizer->Add(m_btnFinish, 0);
    topSizer->Add(navSizer, 0, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 10);

    SetSizerAndFit(topSizer);

    // Window names, used by GUI test scripts to find controls (see test/gui).
    SetName("wizard");
    m_book->SetName("wizard.book");
    m_btnPrev->SetName("wizard.btnPrev");
    m_btnNext->SetName("wizard.btnNext");
    m_btnFinish->SetName("wizard.btnFinish");
    m_cbRadioIn->SetName("wizard.cbRadioIn");
    m_cbSpeakerOut->SetName("wizard.cbSpeakerOut");
    m_ckReceiveOnly->SetName("wizard.ckReceiveOnly");
    m_cbMicIn->SetName("wizard.cbMicIn");
    m_cbRadioOut->SetName("wizard.cbRadioOut");
    m_ckHamlib->SetName("wizard.ckHamlib");
    m_cbRigName->SetName("wizard.cbRigName");
    m_cbSerialPort->SetName("wizard.cbSerialPort");
    m_cbSerialRate->SetName("wizard.cbSerialRate");
    m_cbPttMethod->SetName("wizard.cbPttMethod");
    m_cbPttSerialPort->SetName("wizard.cbPttSerialPort");
    m_ckSerialPTT->SetName("wizard.ckSerialPTT");
    m_cbCtlDevicePath->SetName("wizard.cbCtlDevicePath");
    m_rbUseRTS->SetName("wizard.rbUseRTS");
    m_ckRTSPos->SetName("wizard.ckRTSPos");
    m_rbUseDTR->SetName("wizard.rbUseDTR");
    m_ckDTRPos->SetName("wizard.ckDTRPos");
    m_ckUseAnalogModes->SetName("wizard.ckUseAnalogModes");
    m_ckReportingEnable->SetName("wizard.ckReportingEnable");
    m_txtCallsign->SetName("wizard.txtCallsign");
    m_txtGridSquare->SetName("wizard.txtGridSquare");
    if (m_btnImport != nullptr)
    {
        m_chImportSource->SetName("wizard.chImportSource");
        m_btnImport->SetName("wizard.btnImport");
        m_stImportStatus->SetName("wizard.stImportStatus");
    }
#if defined(WIN32)
    m_ckOmniRig->SetName("wizard.ckOmniRig");
    m_cbOmniRigRigId->SetName("wizard.cbOmniRigRigId");
#endif

    // Populate serial ports and load saved config
    populateSerialPorts();
    loadConfig();
    updateNavButtons();
    updateTxState();
    updateRadioState();
    updateReportingState();

    // Wire events
    m_btnPrev->Bind(wxEVT_BUTTON,   &SetupWizard::OnPrev,   this);
    m_btnNext->Bind(wxEVT_BUTTON,   &SetupWizard::OnNext,   this);
    m_btnFinish->Bind(wxEVT_BUTTON, &SetupWizard::OnFinish, this);

    if (m_btnImport != nullptr)
        m_btnImport->Bind(wxEVT_BUTTON, &SetupWizard::OnImport, this);

    m_ckReceiveOnly->Bind(wxEVT_CHECKBOX, &SetupWizard::OnReceiveOnlyChanged, this);

    m_ckHamlib->Bind(wxEVT_CHECKBOX,    &SetupWizard::OnHamlibChanged,    this);
    m_ckSerialPTT->Bind(wxEVT_CHECKBOX, &SetupWizard::OnSerialPTTChanged, this);
    m_cbRigName->Bind(wxEVT_COMBOBOX,   &SetupWizard::OnRigNameChanged,   this);
    // Whether the PTT serial device applies depends on the PTT method.
    m_cbPttMethod->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { updateRadioState(); });
#if defined(WIN32)
    m_ckOmniRig->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        m_ckHamlib->SetValue(false);
        m_ckSerialPTT->SetValue(false);
        updateRadioState();
    });
#endif

    m_ckReportingEnable->Bind(wxEVT_CHECKBOX, &SetupWizard::OnReportingEnableChanged, this);
}

// --------------------------------------------------------------------------
// Page factories
// --------------------------------------------------------------------------

wxPanel* SetupWizard::makeReceivePage()
{
    wxPanel* page = new wxPanel(m_book);
    wxBoxSizer* vs = new wxBoxSizer(wxVERTICAL);

    vs->Add(makePageHeader(page,
        _("Step 1 of 4: Receive Audio"),
        _("Select the audio devices used for receiving transmissions.")),
        0, static_cast<int>(wxEXPAND));

    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 2, 8, 8);
    grid->AddGrowableCol(1, 1);

    grid->Add(new wxStaticText(page, wxID_ANY, _("Input To Computer From Radio:")),
              0, static_cast<int>(wxALIGN_CENTER_VERTICAL));
    m_cbRadioIn = new wxComboBox(page, wxID_ANY, wxEmptyString,
                                  wxDefaultPosition, wxDefaultSize, 0, nullptr,
                                  wxCB_DROPDOWN | wxCB_READONLY);
    populateAudioCombo(m_cbRadioIn, IAudioEngine::AUDIO_ENGINE_IN);
    grid->Add(m_cbRadioIn, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALIGN_CENTER_VERTICAL));

    grid->Add(new wxStaticText(page, wxID_ANY, _("Output From Computer To Speaker/Headphones:")),
              0, static_cast<int>(wxALIGN_CENTER_VERTICAL));
    m_cbSpeakerOut = new wxComboBox(page, wxID_ANY, wxEmptyString,
                                     wxDefaultPosition, wxDefaultSize, 0, nullptr,
                                     wxCB_DROPDOWN | wxCB_READONLY);
    populateAudioCombo(m_cbSpeakerOut, IAudioEngine::AUDIO_ENGINE_OUT);
    grid->Add(m_cbSpeakerOut, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALIGN_CENTER_VERTICAL));

    vs->Add(grid, 0, static_cast<int>(wxEXPAND));

    m_chImportSource = nullptr;
    m_btnImport      = nullptr;
    m_stImportStatus = nullptr;
    if (!m_importSources.empty())
    {
        wxStaticBoxSizer* importBox = new wxStaticBoxSizer(wxVERTICAL,
            page, _("Import From Another Program"));
        wxStaticBox* importSB = importBox->GetStaticBox();

        importBox->Add(new wxStaticText(importSB, wxID_ANY,
            _("Copy radio audio devices, rig control and callsign/grid square settings\nfrom another program installed on this computer.")),
            0, static_cast<int>(wxALL), 4);

        wxBoxSizer* importRow = new wxBoxSizer(wxHORIZONTAL);
        m_chImportSource = new wxChoice(importSB, wxID_ANY);
        for (auto& source : m_importSources)
            m_chImportSource->Append(source.appName);
        m_chImportSource->SetSelection(0);
        importRow->Add(m_chImportSource, 0, wxRIGHT | static_cast<int>(wxALIGN_CENTER_VERTICAL), 8);
        m_btnImport = new wxButton(importSB, wxID_ANY, _("Import"));
        importRow->Add(m_btnImport, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL));
        importBox->Add(importRow, 0, static_cast<int>(wxALL), 4);

        m_stImportStatus = new wxStaticText(importSB, wxID_ANY, wxEmptyString);
        importBox->Add(m_stImportStatus, 0, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 4);

        vs->Add(importBox, 0, static_cast<int>(wxEXPAND) | wxTOP, 12);
    }

    page->SetSizer(vs);
    return page;
}

wxPanel* SetupWizard::makeTxPage()
{
    wxPanel* page = new wxPanel(m_book);
    wxBoxSizer* vs = new wxBoxSizer(wxVERTICAL);

    vs->Add(makePageHeader(page,
        _("Step 2 of 4: Transmit Audio"),
        _("Select the audio devices used for transmitting. Skip if you\nonly want to receive.")),
        0, static_cast<int>(wxEXPAND));

    m_ckReceiveOnly = new wxCheckBox(page, wxID_ANY,
        _("Receive only (I will not be transmitting)"));
    vs->Add(m_ckReceiveOnly, 0, wxBOTTOM, 12);

    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 2, 8, 8);
    grid->AddGrowableCol(1, 1);

    m_stMicIn = new wxStaticText(page, wxID_ANY, _("Input From Microphone To Computer:"));
    grid->Add(m_stMicIn, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL));
    m_cbMicIn = new wxComboBox(page, wxID_ANY, wxEmptyString,
                                wxDefaultPosition, wxDefaultSize, 0, nullptr,
                                wxCB_DROPDOWN | wxCB_READONLY);
    populateAudioCombo(m_cbMicIn, IAudioEngine::AUDIO_ENGINE_IN);
    grid->Add(m_cbMicIn, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALIGN_CENTER_VERTICAL));

    m_stRadioOut = new wxStaticText(page, wxID_ANY, _("Output From Computer To Radio:"));
    grid->Add(m_stRadioOut, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL));
    m_cbRadioOut = new wxComboBox(page, wxID_ANY, wxEmptyString,
                                   wxDefaultPosition, wxDefaultSize, 0, nullptr,
                                   wxCB_DROPDOWN | wxCB_READONLY);
    populateAudioCombo(m_cbRadioOut, IAudioEngine::AUDIO_ENGINE_OUT);
    grid->Add(m_cbRadioOut, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALIGN_CENTER_VERTICAL));

    vs->Add(grid, 0, static_cast<int>(wxEXPAND));
    page->SetSizer(vs);
    return page;
}

wxPanel* SetupWizard::makeRadioPage()
{
    wxPanel* page = new wxPanel(m_book);
    wxBoxSizer* vs = new wxBoxSizer(wxVERTICAL);

    vs->Add(makePageHeader(page,
        _("Step 3 of 4: Radio Control"),
        _("Configure PTT (Push-To-Talk) control for your radio.\nLeave all options unchecked if you do not need PTT control.")),
        0, static_cast<int>(wxEXPAND));

    // --- Hamlib section ---
    wxStaticBoxSizer* hamlibBox = new wxStaticBoxSizer(wxVERTICAL,
        page, _("CAT Control via Hamlib"));
    wxStaticBox* hamlibSB = hamlibBox->GetStaticBox();

    m_ckHamlib = new wxCheckBox(hamlibSB, wxID_ANY, _("Enable CAT control via Hamlib"));
    hamlibBox->Add(m_ckHamlib, 0, static_cast<int>(wxALL), 4);

    wxFlexGridSizer* hGrid = new wxFlexGridSizer(5, 2, 6, 8);
    hGrid->AddGrowableCol(1, 1);

    m_stRigName = new wxStaticText(hamlibSB, wxID_ANY, _("Rig Model:"));
    hGrid->Add(m_stRigName, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT);
    m_cbRigName = new wxComboBox(hamlibSB, wxID_ANY, wxEmptyString,
                                  wxDefaultPosition, wxSize(220, -1), 0, nullptr,
                                  wxCB_DROPDOWN | wxCB_READONLY);
    {
        int numRigs = HamlibRigController::GetNumberSupportedRadios();
        for (int i = 0; i < numRigs; i++)
            m_cbRigName->Append(HamlibRigController::RigIndexToName(i));
    }
    hGrid->Add(m_cbRigName, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALIGN_CENTER_VERTICAL));

    m_stSerialPort = new wxStaticText(hamlibSB, wxID_ANY, _("Serial Device:"));
    hGrid->Add(m_stSerialPort, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT);
    m_cbSerialPort = new wxComboBox(hamlibSB, wxID_ANY, wxEmptyString,
                                     wxDefaultPosition, wxDefaultSize, 0, nullptr,
                                     wxCB_DROPDOWN);
    m_cbSerialPort->SetMinSize(wxSize(140, -1));
    hGrid->Add(m_cbSerialPort, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALIGN_CENTER_VERTICAL));

    m_stSerialRate = new wxStaticText(hamlibSB, wxID_ANY, _("Serial Rate:"));
    hGrid->Add(m_stSerialRate, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT);
    m_cbSerialRate = new wxComboBox(hamlibSB, wxID_ANY, wxEmptyString,
                                     wxDefaultPosition, wxSize(110, -1), 0, nullptr,
                                     wxCB_DROPDOWN);
    hGrid->Add(m_cbSerialRate, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL));

    m_stPttMethod = new wxStaticText(hamlibSB, wxID_ANY, _("PTT uses:"));
    hGrid->Add(m_stPttMethod, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT);
    m_cbPttMethod = new wxComboBox(hamlibSB, wxID_ANY, wxEmptyString,
                                    wxDefaultPosition, wxSize(120, -1), 0, nullptr,
                                    wxCB_DROPDOWN | wxCB_READONLY);
    m_cbPttMethod->Append(wxT("CAT"));
    m_cbPttMethod->Append(wxT("RTS"));
    m_cbPttMethod->Append(wxT("DTR"));
    m_cbPttMethod->Append(_("None"));
    m_cbPttMethod->Append(_("CAT via Data port"));
    hGrid->Add(m_cbPttMethod, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL));

    m_stPttSerialPort = new wxStaticText(hamlibSB, wxID_ANY, _("PTT Serial Device:"));
    hGrid->Add(m_stPttSerialPort, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT);
    m_cbPttSerialPort = new wxComboBox(hamlibSB, wxID_ANY, wxEmptyString,
                                        wxDefaultPosition, wxDefaultSize, 0, nullptr,
                                        wxCB_DROPDOWN);
    m_cbPttSerialPort->SetMinSize(wxSize(140, -1));
    hGrid->Add(m_cbPttSerialPort, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALIGN_CENTER_VERTICAL));

    hamlibBox->Add(hGrid, 0, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 4);
    vs->Add(hamlibBox, 0, static_cast<int>(wxEXPAND) | wxBOTTOM, 8);

    // --- Serial PTT section ---
    wxStaticBoxSizer* serialBox = new wxStaticBoxSizer(wxVERTICAL,
        page, _("Serial Port PTT"));
    wxStaticBox* serialSB = serialBox->GetStaticBox();

    m_ckSerialPTT = new wxCheckBox(serialSB, wxID_ANY, _("Enable serial port PTT"));
    serialBox->Add(m_ckSerialPTT, 0, static_cast<int>(wxALL), 4);

    wxFlexGridSizer* sGrid = new wxFlexGridSizer(2, 2, 6, 8);
    sGrid->AddGrowableCol(1, 1);

    m_stCtlDevice = new wxStaticText(serialSB, wxID_ANY, _("Control Device:"));
    sGrid->Add(m_stCtlDevice, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT);
    m_cbCtlDevicePath = new wxComboBox(serialSB, wxID_ANY, wxEmptyString,
                                        wxDefaultPosition, wxDefaultSize, 0, nullptr,
                                        wxCB_DROPDOWN);
    m_cbCtlDevicePath->SetMinSize(wxSize(140, -1));
    sGrid->Add(m_cbCtlDevicePath, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALIGN_CENTER_VERTICAL));

    sGrid->Add(new wxStaticText(serialSB, wxID_ANY, _("Signal:")),
               0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT);
    wxBoxSizer* signalRow = new wxBoxSizer(wxHORIZONTAL);
    m_rbUseRTS = new wxRadioButton(serialSB, wxID_ANY, wxT("RTS"), wxDefaultPosition,
                                    wxDefaultSize, wxRB_GROUP);
    m_ckRTSPos = new wxCheckBox(serialSB, wxID_ANY, _("Inverted"));
    m_rbUseDTR = new wxRadioButton(serialSB, wxID_ANY, wxT("DTR"));
    m_ckDTRPos = new wxCheckBox(serialSB, wxID_ANY, _("Inverted"));
    signalRow->Add(m_rbUseRTS, 0, wxRIGHT | static_cast<int>(wxALIGN_CENTER_VERTICAL), 4);
    signalRow->Add(m_ckRTSPos, 0, wxRIGHT | static_cast<int>(wxALIGN_CENTER_VERTICAL), 12);
    signalRow->Add(m_rbUseDTR, 0, wxRIGHT | static_cast<int>(wxALIGN_CENTER_VERTICAL), 4);
    signalRow->Add(m_ckDTRPos, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL));
    sGrid->Add(signalRow, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL));

    serialBox->Add(sGrid, 0, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 4);
    vs->Add(serialBox, 0, static_cast<int>(wxEXPAND) | wxBOTTOM, 8);

#if defined(WIN32)
    // --- OmniRig section (Windows only) ---
    wxStaticBoxSizer* omniBox = new wxStaticBoxSizer(wxVERTICAL,
        page, _("OmniRig"));
    wxStaticBox* omniSB = omniBox->GetStaticBox();

    m_ckOmniRig = new wxCheckBox(omniSB, wxID_ANY, _("Enable OmniRig"));
    omniBox->Add(m_ckOmniRig, 0, static_cast<int>(wxALL), 4);

    wxFlexGridSizer* oGrid = new wxFlexGridSizer(1, 2, 6, 8);
    m_stOmniRigId = new wxStaticText(omniSB, wxID_ANY, _("Rig:"));
    oGrid->Add(m_stOmniRigId, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL) | wxALIGN_RIGHT);
    m_cbOmniRigRigId = new wxComboBox(omniSB, wxID_ANY, wxEmptyString,
                                       wxDefaultPosition, wxDefaultSize, 0, nullptr,
                                       wxCB_DROPDOWN | wxCB_READONLY);
    m_cbOmniRigRigId->Append(_("Rig 1"));
    m_cbOmniRigRigId->Append(_("Rig 2"));
    oGrid->Add(m_cbOmniRigRigId, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL));
    omniBox->Add(oGrid, 0, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), 4);
    vs->Add(omniBox, 0, static_cast<int>(wxEXPAND) | wxBOTTOM, 8);
#endif

    // Applies to both Hamlib and OmniRig.
    m_ckUseAnalogModes = new wxCheckBox(page, wxID_ANY, _("Use USB instead of DIGU"));
    vs->Add(m_ckUseAnalogModes, 0, static_cast<int>(wxALL), 4);

    page->SetSizer(vs);
    return page;
}

wxPanel* SetupWizard::makeReportingPage()
{
    wxPanel* page = new wxPanel(m_book);
    wxBoxSizer* vs = new wxBoxSizer(wxVERTICAL);

    vs->Add(makePageHeader(page,
        _("Step 4 of 4: Reporting"),
        _("Optionally report your station to FreeDV Reporter and PSK Reporter.\nA valid callsign and grid square are required.")),
        0, static_cast<int>(wxEXPAND));

    m_ckReportingEnable = new wxCheckBox(page, wxID_ANY, _("Enable station reporting"));
    vs->Add(m_ckReportingEnable, 0, wxBOTTOM, 10);

    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 2, 8, 8);
    grid->AddGrowableCol(1, 1);

    m_stCallsign = new wxStaticText(page, wxID_ANY, _("Callsign:"));
    grid->Add(m_stCallsign, 0, wxALIGN_CENTER_VERTICAL);
    wxTextValidator callsignValidator(wxFILTER_INCLUDE_CHAR_LIST);
    callsignValidator.SetCharIncludes(wxT("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/"));
    m_txtCallsign = new wxTextCtrl(page, wxID_ANY, wxEmptyString,
                                    wxDefaultPosition, wxSize(160, -1), 0, callsignValidator);
    m_txtCallsign->SetMaxLength(REPORTING_CALLSIGN_MAX_LENGTH);
    grid->Add(m_txtCallsign, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxALIGN_CENTER_VERTICAL));

    m_stGridSquare = new wxStaticText(page, wxID_ANY, _("Grid Square:"));
    grid->Add(m_stGridSquare, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL));
    m_txtGridSquare = new wxTextCtrl(page, wxID_ANY, wxEmptyString,
                                      wxDefaultPosition, wxSize(100, -1));
    grid->Add(m_txtGridSquare, 0, static_cast<int>(wxALIGN_CENTER_VERTICAL));

    vs->Add(grid, 0, static_cast<int>(wxEXPAND) | wxBOTTOM, 10);

    page->SetSizer(vs);
    return page;
}

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

void SetupWizard::populateAudioCombo(wxComboBox* combo, IAudioEngine::AudioDirection dir)
{
    combo->Clear();
    auto engine = AudioEngineFactory::GetAudioEngine();
    engine->start();
    for (auto& dev : engine->getAudioDeviceList(dir))
        combo->Append(dev.getDisplayName(), new wxStringClientData(dev.name));
    // TRANSLATORS: Shown in the audio device lists to indicate that no device is selected.
    combo->Append(_("none"), new wxStringClientData("none"));
    engine->stop();
}

// Selects the entry for the given internal (config) device name; the combo
// box itself displays the device's user-friendly name.
void SetupWizard::setAudioComboDevice(wxComboBox* combo, const wxString& devName)
{
    for (unsigned int i = 0; i < combo->GetCount(); i++)
    {
        auto data = static_cast<wxStringClientData*>(combo->GetClientObject(i));
        if (data != nullptr && data->GetData().IsSameAs(devName))
        {
            combo->SetSelection(i);
            return;
        }
    }
    combo->SetValue(devName);
}

wxString SetupWizard::getAudioComboDevice(wxComboBox* combo)
{
    int sel = combo->GetSelection();
    if (sel != wxNOT_FOUND)
    {
        auto data = static_cast<wxStringClientData*>(combo->GetClientObject(sel));
        if (data != nullptr) return data->GetData();
    }
    return combo->GetValue();
}

// When no radio device is configured, tries to find a known radio sound device (e.g. the
// built-in USB codec found in many radios, FlexRadio DAX, QMX) and
// selects it for both radio RX and TX. The first matching input device
// wins; the output device must then belong to the same radio type.
void SetupWizard::autoSelectRadioDevices(IAudioEngine* engine)
{
    struct RadioDeviceMatch
    {
        wxString inputMatch;
        wxString inputMustContain;
        wxString outputMatch;
    };

    // All comparisons are case-insensitive.
    static const std::vector<RadioDeviceMatch> knownDevices = {
        { "USB AUDIO CODEC",   "",   "USB AUDIO CODEC" },
        { "USB AUDIO DEVICE",  "",   "USB AUDIO DEVICE" },
        { "DAX",               "RX", "DAX TX" },
        { "QMX TRANSCEIVER",   "",   "QMX TRANSCEIVER" },
    };

    auto deviceText = [](const AudioDeviceSpecification& dev) {
        return (dev.name + " " + dev.displayName).Upper();
    };

    // Skip loopback/monitor sources (e.g. PulseAudio "Monitor of ...")
    // and DAX IQ streams, neither of which carry receive audio.
    auto isInputCandidate = [&](const AudioDeviceSpecification& dev, const RadioDeviceMatch& known) {
        wxString text = deviceText(dev);
        if (text.Contains("MONITOR") || text.Contains("DAX IQ")) return false;
        if (!text.Contains(known.inputMatch)) return false;
        return known.inputMustContain.IsEmpty() || text.Contains(known.inputMustContain);
    };

    // Returns the channel number following "RX" (e.g. 1 for "DAX Audio RX 1"),
    // or INT_MAX if there isn't one.
    auto rxChannel = [&](const AudioDeviceSpecification& dev) {
        wxString text = deviceText(dev);
        int pos = text.Find("RX");
        if (pos == wxNOT_FOUND) return INT_MAX;
        wxString rest = text.Mid(pos + 2).Trim(false);
        wxString digits;
        for (auto ch : rest)
        {
            if (!wxIsdigit(ch)) break;
            digits += ch;
        }
        long channel = 0;
        return digits.ToLong(&channel) ? (int)channel : INT_MAX;
    };

    auto inputDevices  = engine->getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_IN);
    auto outputDevices = engine->getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_OUT);

    for (auto& dev : inputDevices)
    {
        for (auto& known : knownDevices)
        {
            if (!isInputCandidate(dev, known)) continue;

            // Radios with multiple RX channels (e.g. DAX) may not enumerate
            // them in order; prefer the lowest-numbered channel.
            const AudioDeviceSpecification* inDevPtr = &dev;
            if (!known.inputMustContain.IsEmpty())
            {
                for (auto& other : inputDevices)
                {
                    if (isInputCandidate(other, known) && rxChannel(other) < rxChannel(*inDevPtr))
                        inDevPtr = &other;
                }
            }
            auto& inDev = *inDevPtr;

            for (auto& outDev : outputDevices)
            {
                if (deviceText(outDev).Contains(known.outputMatch))
                {
                    log_info("Setup wizard: auto-selecting radio devices %s (RX) and %s (TX)",
                             (const char*)inDev.name.ToUTF8(), (const char*)outDev.name.ToUTF8());
                    setAudioComboDevice(m_cbRadioIn, inDev.name);
                    setAudioComboDevice(m_cbRadioOut, outDev.name);
                    m_ckReceiveOnly->SetValue(false);
                    return;
                }
            }
        }
    }
}

// Looks for settings files from other digital mode programs whose settings
// can be imported. WSJT-X and its derivatives (JTDX, JS8Call, WS) all store
// their settings in the same QSettings INI format, named after the program.
void SetupWizard::findImportSources()
{
    static const struct { const char* appName; const char* displayName; } apps[] = {
        { "WSJT-X", "WSJT-X" },
        { "WS", "WS (WSJT-X Improved)" },
        { "JTDX", "JTDX" },
        { "JS8Call", "JS8Call" },
    };

    for (auto& app : apps)
    {
        wxString appName = app.appName;
        wxFileName path;
#if defined(__WXMSW__)
        wxString localAppData;
        if (!wxGetEnv("LOCALAPPDATA", &localAppData)) break;
        path.Assign(localAppData + wxFILE_SEP_PATH + appName, appName + ".ini");
#elif defined(__WXOSX__)
        path.Assign(wxGetHomeDir() + "/Library/Preferences", appName + ".ini");
#else
        wxString configDir;
        if (!wxGetEnv("XDG_CONFIG_HOME", &configDir) || configDir.IsEmpty())
            configDir = wxGetHomeDir() + "/.config";
        path.Assign(configDir, appName + ".ini");
#endif
        if (path.FileExists())
        {
            log_info("Setup wizard: found %s settings at %s",
                     (const char*)appName.ToUTF8(), (const char*)path.GetFullPath().ToUTF8());
            m_importSources.push_back({ app.displayName, path.GetFullPath() });
        }
    }
}

// Selects the device in the combo box matching a device name saved by another
// program. Names may not match exactly (e.g. older Qt versions on Windows
// truncate names to 31 characters), so fall back to a prefix match.
bool SetupWizard::selectImportedAudioDevice(wxComboBox* combo, const wxString& importedName,
                                            const std::vector<AudioDeviceSpecification>& devices)
{
    wxString name = importedName.Upper().Trim().Trim(false);
    if (name.IsEmpty()) return false;

    for (auto& dev : devices)
    {
        if (dev.name.Upper().Trim() == name || dev.getDisplayName().Upper().Trim() == name)
        {
            setAudioComboDevice(combo, dev.name);
            return true;
        }
    }

    if (name.Length() < 8) return false; // too short to safely prefix match
    for (auto& dev : devices)
    {
        if (dev.name.Upper().StartsWith(name) || dev.getDisplayName().Upper().StartsWith(name))
        {
            setAudioComboDevice(combo, dev.name);
            return true;
        }
    }

    return false;
}

// Reads a string value written by Qt's QSettings, which quotes values
// containing special characters (e.g. commas) and escapes backslashes
// and quotes within them.
static wxString readQtIniString(wxFileConfig& ini, const wxString& key)
{
    wxString value = ini.Read(key, wxEmptyString).Trim().Trim(false);
    if (value.Length() >= 2 && value.StartsWith("\"") && value.EndsWith("\""))
        value = value.Mid(1, value.Length() - 2);
    value.Replace("\\\"", "\"");
    value.Replace("\\\\", "\\");
    return value.Trim().Trim(false);
}

void SetupWizard::importSettings(const ImportSource& source)
{
    wxLogNull suppressLogs; // don't pop up errors for Qt-specific syntax

    wxFileConfig ini(wxEmptyString, wxEmptyString, source.path, wxEmptyString,
                     wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_NO_ESCAPE_CHARACTERS);
    ini.SetExpandEnvVars(false);
    ini.SetPath("/Configuration");

    wxArrayString imported;
    wxArrayString notImported;

    // Audio devices
    auto engine = AudioEngineFactory::GetAudioEngine();
    engine->start();
    auto inputDevices  = engine->getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_IN);
    auto outputDevices = engine->getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_OUT);
    engine->stop();

    wxString radioInName  = readQtIniString(ini, "SoundInName");
    wxString radioOutName = readQtIniString(ini, "SoundOutName");
    bool radioInFound  = selectImportedAudioDevice(m_cbRadioIn, radioInName, inputDevices);
    bool radioOutFound = selectImportedAudioDevice(m_cbRadioOut, radioOutName, outputDevices);
    if (radioOutFound)
    {
        m_ckReceiveOnly->SetValue(false);
        updateTxState();
    }
    if (radioInFound || radioOutFound)
        imported.Add(_("radio audio devices"));
    if ((!radioInName.IsEmpty() && !radioInFound) || (!radioOutName.IsEmpty() && !radioOutFound))
        notImported.Add(_("radio audio devices (not currently connected)"));

    // Rig control. PTTMethod may be stored as a plain enum name or wrapped
    // in a Qt @Variant(), so just look for the enum name.
    wxString rigName   = readQtIniString(ini, "Rig");
    wxString pttMethod = readQtIniString(ini, "PTTMethod");
    wxString pttPort   = readQtIniString(ini, "PTTport");
    // Like PTTMethod, these are enums that may be wrapped in @Variant().
    bool txAudioRear   = readQtIniString(ini, "TXAudioSource").Contains("TX_audio_source_rear");
    wxString dataMode  = readQtIniString(ini, "DataMode");
    auto importDataMode = [this](const wxString& mode) {
        // WSJT-X's "None" mode setting doesn't change the mode at all,
        // so leave the checkbox as-is in that case.
        if (mode.Contains("data_mode_USB"))
            m_ckUseAnalogModes->SetValue(true);
        else if (mode.Contains("data_mode_data"))
            m_ckUseAnalogModes->SetValue(false);
    };
    int rigIndex       = HamlibRigController::RigNameToIndex(std::string(rigName.ToUTF8()));

    // WSJT-X keeps serial, USB and network port settings around, so use
    // whichever one applies to the selected rig's Hamlib port type (as
    // WSJT-X itself does).
    auto portType = (rigIndex >= 0)
        ? HamlibRigController::GetRigPortType(rigIndex)
        : HamlibRigController::PORT_SERIAL;
    wxString catPort;
    long catRate = 0;
    if (portType == HamlibRigController::PORT_NETWORK)
    {
        catPort = readQtIniString(ini, "CATNetworkPort");

        // WSJT-X leaves this blank to use Hamlib's default (e.g. localhost:4532),
        // but FreeDV always passes the port to Hamlib.
        if (catPort.IsEmpty())
            catPort = wxString::FromUTF8(HamlibRigController::GetDefaultRigPathname(rigIndex).c_str());
    }
    else if (portType == HamlibRigController::PORT_USB)
    {
        catPort = readQtIniString(ini, "CATUSBPort");
    }
    else
    {
        catPort = readQtIniString(ini, "CATSerialPort");
        catRate = ini.ReadLong("CATSerialRate", 0);
    }

    bool pttCat = pttMethod.Contains("PTT_method_CAT");
    bool pttDtr = pttMethod.Contains("PTT_method_DTR");
    bool pttRts = pttMethod.Contains("PTT_method_RTS");

    // "CAT" as the PTT port means the same port as CAT control.
    if (pttPort.IsSameAs("CAT", false) || pttPort == catPort)
        pttPort = wxEmptyString;

    if (rigIndex >= 0 && rigName != "None")
    {
        m_ckHamlib->SetValue(true);
        m_ckSerialPTT->SetValue(false);
#if defined(WIN32)
        m_ckOmniRig->SetValue(false);
#endif
        m_cbRigName->SetSelection(rigIndex);
        populateBaudRates(rigIndex);
        m_cbSerialPort->SetValue(catPort);
        // Not a ?: with the Format in it: the coverage job doesn't count the
        // branches on a Format line.
        wxString rate("default");
        if (catRate > 0)
        {
            rate = wxString::Format("%ld", catRate);
        }
        m_cbSerialRate->SetValue(rate);

        HamlibRigController::PttType pttType = HamlibRigController::PTT_VIA_NONE;
        // Rear/Data TX audio source means PTT has to key the data port so
        // the radio takes audio from there.
        if (pttCat) pttType = txAudioRear ? HamlibRigController::PTT_VIA_CAT_DATA : HamlibRigController::PTT_VIA_CAT;
        else if (pttDtr) pttType = HamlibRigController::PTT_VIA_DTR;
        else if (pttRts) pttType = HamlibRigController::PTT_VIA_RTS;
        m_cbPttMethod->SetSelection((int)pttType);
        m_cbPttSerialPort->SetValue((pttDtr || pttRts) ? pttPort : wxString(wxEmptyString));

        importDataMode(dataMode);
        imported.Add(wxString::Format(_("Hamlib rig control (%s)"), rigName));
    }
#if defined(WIN32)
    else if (rigName.StartsWith("OmniRig Rig "))
    {
        m_ckOmniRig->SetValue(true);
        m_ckHamlib->SetValue(false);
        m_ckSerialPTT->SetValue(false);
        m_cbOmniRigRigId->SetSelection(rigName.EndsWith("2") ? 1 : 0);
        importDataMode(dataMode);
        imported.Add(wxString::Format(_("OmniRig rig control (%s)"), rigName.Mid(8)));
    }
#endif
    else if (rigName.IsEmpty() || rigName == "None")
    {
        // No CAT control, but PTT may still be keyed via a serial port.
        if ((pttDtr || pttRts) && !pttPort.IsEmpty())
        {
            m_ckSerialPTT->SetValue(true);
            m_ckHamlib->SetValue(false);
#if defined(WIN32)
            m_ckOmniRig->SetValue(false);
#endif
            m_cbCtlDevicePath->SetValue(pttPort);
            m_rbUseRTS->SetValue(pttRts);
            m_rbUseDTR->SetValue(pttDtr);
            m_ckRTSPos->SetValue(false);
            m_ckDTRPos->SetValue(false);
            imported.Add(wxString::Format(_("serial port PTT (%s)"), pttPort));
        }
    }
    else
    {
        notImported.Add(wxString::Format(_("rig control (%s is not supported by FreeDV)"), rigName));
    }
    updateRadioState();

    // Reporting
    wxString callsign = readQtIniString(ini, "MyCall");
    wxString grid     = readQtIniString(ini, "MyGrid");
    if (!callsign.IsEmpty())
    {
        m_txtCallsign->SetValue(callsign);
        imported.Add(_("callsign"));
    }
    if (!grid.IsEmpty())
    {
        m_txtGridSquare->SetValue(grid);
        imported.Add(_("grid square"));
    }
    if (readQtIniString(ini, "PSKReporter").IsSameAs("true", false))
    {
        m_ckReportingEnable->SetValue(true);
        updateReportingState();
        imported.Add(_("reporting enabled"));
    }

    auto joinList = [](const wxArrayString& items) {
        wxString result;
        for (size_t i = 0; i < items.GetCount(); i++)
            result += (i > 0 ? _(", ") : wxString()) + items[i];
        return result;
    };

    wxString status;
    if (imported.IsEmpty())
        status = wxString::Format(_("No usable settings were found in %s."), source.appName);
    else
        status = wxString::Format(_("Imported: %s."), joinList(imported));
    if (!notImported.IsEmpty())
        status += "\n" + wxString::Format(_("Not imported: %s."), joinList(notImported));
    status += "\n" + _("Review each page before clicking Finish.");

    log_info("Setup wizard: %s import: %s", (const char*)source.appName.ToUTF8(), (const char*)status.ToUTF8());
    m_stImportStatus->SetLabel(status);
    m_stImportStatus->Wrap(m_stImportStatus->GetParent()->GetClientSize().GetWidth() - 16);
    fitToCurrentPage();
}

void SetupWizard::populateSerialPorts()
{
    std::vector<wxString> portList;

#ifdef __WXMSW__
    wxRegKey key(wxRegKey::HKLM, _T("HARDWARE\\DEVICEMAP\\SERIALCOMM"));
    if (key.Exists() && key.Open(wxRegKey::Read))
    {
        size_t subkeys, values;
        if (key.GetKeyInfo(&subkeys, nullptr, &values, nullptr) && key.HasValues())
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
    if (glob("/dev/tty*", GLOB_MARK, nullptr, &gl) == 0 ||
#else
    if (glob("/dev/tty.*", GLOB_MARK, nullptr, &gl) == 0 || // NOLINT
#endif
        glob("/dev/cu.*", GLOB_MARK, nullptr, &gl) == 0) { // NOLINT
        for (unsigned int i = 0; i < gl.gl_pathc; i++) {
            if (gl.gl_pathv[i][strlen(gl.gl_pathv[i]) - 1] == '/')
                continue;
#if defined(__FreeBSD__)
            if (gl.gl_pathv[i][8] >= 'l' && gl.gl_pathv[i][8] <= 's') continue;
            if (gl.gl_pathv[i][8] >= 'L' && gl.gl_pathv[i][8] <= 'S') continue;
            if (gl.gl_pathv[i][8] == 'v') continue;
#else
            if (!strcmp("/dev/cu.wlan-debug", gl.gl_pathv[i])) continue;
#endif
#ifndef __WXOSX__
            if (strchr(gl.gl_pathv[i], '.') != nullptr) continue;
#endif
            portList.push_back(gl.gl_pathv[i]);
        }
        globfree(&gl);
    }
#else
    glob_t gl;
    // The GCOVR_EXCL_BR_LINE below: whether these entries exist is a property
    // of the machine, and the coverage job makes sure two of them do.
    if (glob("/sys/class/tty/*/device/driver", GLOB_MARK, nullptr, &gl) == 0) // NOLINT GCOVR_EXCL_BR_LINE
    {
        wxRegEx pathRegex("/sys/class/tty/([^/]+)");
        for (unsigned int i = 0; i < gl.gl_pathc; i++) {
            wxString path = gl.gl_pathv[i];
            if (pathRegex.Matches(path)) // GCOVR_EXCL_BR_LINE: see the globs above
                portList.push_back("/dev/" + pathRegex.GetMatch(path, 1));
        }
        globfree(&gl);
    }
    if (glob("/dev/serial/by-id/*", GLOB_MARK, nullptr, &gl) == 0) // NOLINT GCOVR_EXCL_BR_LINE
    {
        for (unsigned int i = 0; i < gl.gl_pathc; i++)
            portList.push_back(gl.gl_pathv[i]);
        globfree(&gl);
    }
    if (glob("/dev/rfcomm*", GLOB_MARK, nullptr, &gl) == 0) // NOLINT GCOVR_EXCL_BR_LINE
    {
        for (unsigned int i = 0; i < gl.gl_pathc; i++)
            portList.push_back(gl.gl_pathv[i]);
        globfree(&gl);
    }
#endif
#endif

    std::sort(portList.begin(), portList.end(),
        [](const wxString& a, const wxString& b) {
            wxRegEx re("^([^0-9]+)([0-9]+)$");
            wxString an, bn;
            int ai = 0, bi = 0;
            if (re.Matches(a)) { an = re.GetMatch(a, 1); ai = wxAtoi(re.GetMatch(a, 2)); }
            else { an = a; }
            if (re.Matches(b)) { bn = re.GetMatch(b, 1); bi = wxAtoi(re.GetMatch(b, 2)); }
            else { bn = b; }
            return (an < bn) || (an == bn && ai < bi);
        });

    for (auto& p : portList) {
        m_cbSerialPort->Append(p);
        m_cbPttSerialPort->Append(p);
        m_cbCtlDevicePath->Append(p);
    }
}

void SetupWizard::populateBaudRates(int rigIndex)
{
    wxString rates[] = {
        "default", "300", "1200", "2400", "4800", "9600",
        "19200", "38400", "57600", "115200", "230400",
        "460800", "500000", "576000", "921600", "1000000",
        "1152000", "1500000", "2000000"
    };

    int minRate = 0, maxRate = 0;
    if (rigIndex >= 0) {
        minRate = HamlibRigController::GetMinimumSerialBaudRate(rigIndex);
        maxRate = HamlibRigController::GetMaximumSerialBaudRate(rigIndex);
    }

    wxString prev = m_cbSerialRate->GetValue();
    m_cbSerialRate->Clear();

    for (unsigned int i = 0; i < WXSIZEOF(rates); i++) {
        int r = wxAtoi(rates[i]);
        if (i > 0 && minRate > 0 && maxRate > 0)
            if (r < minRate || r > maxRate) continue;
        m_cbSerialRate->Append(rates[i]);
        if (rates[i] == prev)
            m_cbSerialRate->SetSelection(m_cbSerialRate->GetCount() - 1);
        else if (i == 0 && prev.IsEmpty())
            m_cbSerialRate->SetSelection(0);
    }
    if (m_cbSerialRate->GetCurrentSelection() == wxNOT_FOUND)
        m_cbSerialRate->SetSelection(0);
}

void SetupWizard::loadConfig()
{
    auto& cfg = wxGetApp().appConfiguration;

    auto audioEngine = AudioEngineFactory::GetAudioEngine();
    audioEngine->start();

    // Page 0: Receive Audio
    setAudioComboDevice(m_cbRadioIn, cfg.audioConfiguration.soundCard1In.deviceName);

    // Page 1: Transmit Audio
    // Mapping: 1-card (receive-only): SC1Out = speakers, SC2* = "none"
    //          2-card (RX+TX):        SC1Out = radio TX, SC2Out = speakers
    wxString sc2in  = cfg.audioConfiguration.soundCard2In.deviceName;
    wxString sc2out = cfg.audioConfiguration.soundCard2Out.deviceName;
    bool rxOnly = (sc2in.IsEmpty() || sc2in == "none") &&
                  (sc2out.IsEmpty() || sc2out == "none");
    m_ckReceiveOnly->SetValue(rxOnly);
    if (rxOnly)
    {
        // 1-card: SC1Out is speakers
        wxString spk = cfg.audioConfiguration.soundCard1Out.deviceName;
        if (spk.IsEmpty() || spk == "none")
        {
            auto def = audioEngine->getDefaultAudioDevice(IAudioEngine::AUDIO_ENGINE_OUT);
            if (def.isValid()) spk = def.name;
        }
        setAudioComboDevice(m_cbSpeakerOut, spk);
        // Pre-fill microphone with system default so the user sees a sensible suggestion
        auto def = audioEngine->getDefaultAudioDevice(IAudioEngine::AUDIO_ENGINE_IN);
        if (def.isValid()) setAudioComboDevice(m_cbMicIn, def.name);
    }
    else
    {
        // 2-card: SC2Out is speakers, SC1Out is radio TX
        setAudioComboDevice(m_cbSpeakerOut, sc2out);
        setAudioComboDevice(m_cbMicIn, sc2in);
        setAudioComboDevice(m_cbRadioOut, cfg.audioConfiguration.soundCard1Out.deviceName);
    }

    // If no radio device has been selected yet (e.g. new installation),
    // try to find one automatically. Receive-only setups intentionally
    // have no radio output device, so that doesn't count as missing.
    auto isUnset = [](const wxString& name) { return name.IsEmpty() || name == "none"; };
    bool radioInMissing  = isUnset(cfg.audioConfiguration.soundCard1In.deviceName);
    bool radioOutMissing = !rxOnly && isUnset(cfg.audioConfiguration.soundCard1Out.deviceName);
    if (radioInMissing || radioOutMissing)
    {
        autoSelectRadioDevices(audioEngine.get());
    }

    // Page 2: Radio Control — Hamlib
    m_ckHamlib->SetValue(cfg.rigControlConfiguration.hamlibUseForPTT);
    m_cbRigName->SetSelection(wxGetApp().m_intHamlibRig);
    m_cbSerialPort->SetValue(cfg.rigControlConfiguration.hamlibSerialPort);
    m_cbPttSerialPort->SetValue(cfg.rigControlConfiguration.hamlibPttSerialPort);
    {
        int rig = m_cbRigName->GetCurrentSelection();
        populateBaudRates(rig >= 0 ? rig : -1);
        if (cfg.rigControlConfiguration.hamlibSerialRate == 0)
            m_cbSerialRate->SetSelection(0);
        else
            m_cbSerialRate->SetValue(
                wxString::Format("%i", cfg.rigControlConfiguration.hamlibSerialRate.get()));
    }
    m_cbPttMethod->SetSelection((int)cfg.rigControlConfiguration.hamlibPTTType);

    // Serial PTT
    m_ckSerialPTT->SetValue(cfg.rigControlConfiguration.useSerialPTT);
    m_cbCtlDevicePath->SetValue(cfg.rigControlConfiguration.serialPTTPort);
    m_rbUseRTS->SetValue(cfg.rigControlConfiguration.serialPTTUseRTS);
    m_ckRTSPos->SetValue(cfg.rigControlConfiguration.serialPTTPolarityRTS);
    m_rbUseDTR->SetValue(cfg.rigControlConfiguration.serialPTTUseDTR);
    m_ckDTRPos->SetValue(cfg.rigControlConfiguration.serialPTTPolarityDTR);

#if defined(WIN32)
    m_ckOmniRig->SetValue(cfg.rigControlConfiguration.useOmniRig);
    m_cbOmniRigRigId->SetSelection(cfg.rigControlConfiguration.omniRigRigId);
#endif
    m_ckUseAnalogModes->SetValue(cfg.rigControlConfiguration.hamlibUseAnalogModes);

    // Page 3: Reporting
    m_ckReportingEnable->SetValue(cfg.reportingConfiguration.reportingEnabled);
    m_txtCallsign->SetValue(cfg.reportingConfiguration.reportingCallsign);
    m_txtGridSquare->SetValue(cfg.reportingConfiguration.reportingGridSquare);

    audioEngine->stop();
}

void SetupWizard::saveConfig()
{
    auto& cfg = wxGetApp().appConfiguration;
    auto  audioEngine = AudioEngineFactory::GetAudioEngine();
    audioEngine->start();

    // Helper: look up default sample rate for a named device
    auto getSampleRate = [&](const wxString& name, IAudioEngine::AudioDirection dir) -> int {
        if (name.IsEmpty() || name == "none") return 0;
        for (auto& dev : audioEngine->getAudioDeviceList(dir))
            if (dev.name.IsSameAs(name)) return dev.defaultSampleRate;
        return 0;
    };

    // Mapping: 1-card (receive-only): SC1Out = speakers, SC2* = "none"
    //          2-card (RX+TX):        SC1Out = radio TX, SC2Out = speakers
    wxString sc1in = getAudioComboDevice(m_cbRadioIn);
    wxString spk   = getAudioComboDevice(m_cbSpeakerOut);

    cfg.audioConfiguration.soundCard1In.deviceName = sc1in;
    int r;
    r = getSampleRate(sc1in, IAudioEngine::AUDIO_ENGINE_IN);
    if (r > 0) cfg.audioConfiguration.soundCard1In.sampleRate = r;

    if (m_ckReceiveOnly->GetValue())
    {
        // 1-card: SC1Out = speakers, SC2* = "none"
        cfg.audioConfiguration.soundCard1Out.deviceName = spk;
        r = getSampleRate(spk, IAudioEngine::AUDIO_ENGINE_OUT);
        if (r > 0) cfg.audioConfiguration.soundCard1Out.sampleRate = r;
        cfg.audioConfiguration.soundCard2In.deviceName  = "none";
        cfg.audioConfiguration.soundCard2Out.deviceName = "none";
    }
    else
    {
        // 2-card: SC1Out = radio TX, SC2Out = speakers
        wxString radioTx = getAudioComboDevice(m_cbRadioOut);
        wxString sc2in   = getAudioComboDevice(m_cbMicIn);
        cfg.audioConfiguration.soundCard1Out.deviceName = radioTx;
        r = getSampleRate(radioTx, IAudioEngine::AUDIO_ENGINE_OUT);
        if (r > 0) cfg.audioConfiguration.soundCard1Out.sampleRate = r;
        cfg.audioConfiguration.soundCard2In.deviceName  = sc2in;
        r = getSampleRate(sc2in, IAudioEngine::AUDIO_ENGINE_IN);
        if (r > 0) cfg.audioConfiguration.soundCard2In.sampleRate = r;
        cfg.audioConfiguration.soundCard2Out.deviceName = spk;
        r = getSampleRate(spk, IAudioEngine::AUDIO_ENGINE_OUT);
        if (r > 0) cfg.audioConfiguration.soundCard2Out.sampleRate = r;
    }

    // Page 2: Radio Control — Hamlib
    cfg.rigControlConfiguration.hamlibUseForPTT  = m_ckHamlib->GetValue();
    wxGetApp().m_intHamlibRig = m_cbRigName->GetCurrentSelection();
    cfg.rigControlConfiguration.hamlibRigName =
        (wxGetApp().m_intHamlibRig >= 0)
            ? HamlibRigController::RigIndexToName(wxGetApp().m_intHamlibRig)
            : "";
    cfg.rigControlConfiguration.hamlibSerialPort    = m_cbSerialPort->GetValue();
    cfg.rigControlConfiguration.hamlibPttSerialPort = m_cbPttSerialPort->GetValue();
    {
        wxString rateStr = m_cbSerialRate->GetValue();
        if (rateStr == "default")
            cfg.rigControlConfiguration.hamlibSerialRate = 0;
        else {
            // (The list only offers rates, but the box can be typed into:
            // what isn't one is taken as Hamlib's default rather than
            // wrapped into some other number.)
            long r = 0;
            cfg.rigControlConfiguration.hamlibSerialRate =
                (rateStr.Strip(wxString::both).ToLong(&r) && r > 0) ? (unsigned int)r : 0;
        }
    }
    cfg.rigControlConfiguration.hamlibPTTType = m_cbPttMethod->GetCurrentSelection();

    // Serial PTT
    cfg.rigControlConfiguration.useSerialPTT         = m_ckSerialPTT->GetValue();
    cfg.rigControlConfiguration.serialPTTPort         = m_cbCtlDevicePath->GetValue();
    cfg.rigControlConfiguration.serialPTTUseRTS       = m_rbUseRTS->GetValue();
    cfg.rigControlConfiguration.serialPTTPolarityRTS  = m_ckRTSPos->GetValue();
    cfg.rigControlConfiguration.serialPTTUseDTR       = m_rbUseDTR->GetValue();
    cfg.rigControlConfiguration.serialPTTPolarityDTR  = m_ckDTRPos->GetValue();

#if defined(WIN32)
    cfg.rigControlConfiguration.useOmniRig   = m_ckOmniRig->GetValue();
    cfg.rigControlConfiguration.omniRigRigId = m_cbOmniRigRigId->GetCurrentSelection();
#endif
    cfg.rigControlConfiguration.hamlibUseAnalogModes = m_ckUseAnalogModes->GetValue();

    // Page 3: Reporting
    bool reportingOn = m_ckReportingEnable->GetValue();
    cfg.reportingConfiguration.reportingEnabled      = reportingOn;
    cfg.reportingConfiguration.reportingCallsign     = m_txtCallsign->GetValue();
    cfg.reportingConfiguration.reportingGridSquare   = m_txtGridSquare->GetValue();
    cfg.reportingConfiguration.freedvReporterEnabled = reportingOn;
    cfg.reportingConfiguration.pskReporterEnabled    = reportingOn;

    cfg.save(pConfig);

    audioEngine->stop();
}

// --------------------------------------------------------------------------
// Navigation
// --------------------------------------------------------------------------

void SetupWizard::updateNavButtons()
{
    m_btnPrev->Enable(m_currentPage > 0);
    m_btnNext->Show(m_currentPage < NUM_PAGES - 1);
    m_btnFinish->Show(m_currentPage == NUM_PAGES - 1);
    Layout();
    fitToCurrentPage();
}

void SetupWizard::fitToCurrentPage()
{
    wxWindow* page = m_book->GetCurrentPage();
    if (!page) return;
    page->Layout();
    wxSize pageSize = page->GetBestSize();
    // SetMinSize with an explicit size makes GetEffectiveMinSize() return exactly
    // pageSize, bypassing wxSimplebook's max-of-all-pages GetBestSize() calculation.
    m_book->SetMinSize(pageSize);
    Fit();
    m_book->SetMinSize(wxDefaultSize);
}

void SetupWizard::updateTxState()
{
    bool rxOnly = m_ckReceiveOnly->GetValue();
    m_stMicIn->Enable(!rxOnly);
    m_cbMicIn->Enable(!rxOnly);
    m_stRadioOut->Enable(!rxOnly);
    m_cbRadioOut->Enable(!rxOnly);
}

void SetupWizard::updateRadioState()
{
    bool hl = m_ckHamlib->GetValue();
    m_stRigName->Enable(hl);
    m_cbRigName->Enable(hl);
    m_stSerialPort->Enable(hl);
    m_cbSerialPort->Enable(hl);
    m_stSerialRate->Enable(hl);
    m_cbSerialRate->Enable(hl);
    m_stPttMethod->Enable(hl);
    m_cbPttMethod->Enable(hl);
    bool pttNeedsSep = hl &&
        (m_cbPttMethod->GetSelection() == HamlibRigController::PTT_VIA_RTS ||
         m_cbPttMethod->GetSelection() == HamlibRigController::PTT_VIA_DTR);
    m_stPttSerialPort->Enable(pttNeedsSep);
    m_cbPttSerialPort->Enable(pttNeedsSep);

    bool sp = m_ckSerialPTT->GetValue();
    m_stCtlDevice->Enable(sp);
    m_cbCtlDevicePath->Enable(sp);
    m_rbUseRTS->Enable(sp);
    m_ckRTSPos->Enable(sp);
    m_rbUseDTR->Enable(sp);
    m_ckDTRPos->Enable(sp);

    bool omni = false;
#if defined(WIN32)
    omni = m_ckOmniRig->GetValue();
    m_stOmniRigId->Enable(omni);
    m_cbOmniRigRigId->Enable(omni);
#endif
    m_ckUseAnalogModes->Enable(hl || omni);
}

void SetupWizard::updateReportingState()
{
    bool en = m_ckReportingEnable->GetValue();
    m_stCallsign->Enable(en);
    m_txtCallsign->Enable(en);
    m_stGridSquare->Enable(en);
    m_txtGridSquare->Enable(en);
}

// --------------------------------------------------------------------------
// Event handlers
// --------------------------------------------------------------------------

void SetupWizard::OnNext(wxCommandEvent&)
{
    if (m_currentPage < NUM_PAGES - 1) {
        m_currentPage++;
        m_book->SetSelection(m_currentPage);
        updateNavButtons();
    }
}

void SetupWizard::OnPrev(wxCommandEvent&)
{
    if (m_currentPage > 0) {
        m_currentPage--;
        m_book->SetSelection(m_currentPage);
        updateNavButtons();
    }
}

void SetupWizard::OnFinish(wxCommandEvent&)
{
    saveConfig();
    EndModal(wxID_OK);
}

void SetupWizard::OnReceiveOnlyChanged(wxCommandEvent&)
{
    updateTxState();
}

void SetupWizard::OnHamlibChanged(wxCommandEvent&)
{
    if (m_ckHamlib->GetValue()) {
        m_ckSerialPTT->SetValue(false);
#if defined(WIN32)
        m_ckOmniRig->SetValue(false);
#endif
    }
    updateRadioState();
}

void SetupWizard::OnSerialPTTChanged(wxCommandEvent&)
{
    if (m_ckSerialPTT->GetValue()) {
        m_ckHamlib->SetValue(false);
#if defined(WIN32)
        m_ckOmniRig->SetValue(false);
#endif
    }
    updateRadioState();
}

void SetupWizard::OnRigNameChanged(wxCommandEvent&)
{
    populateBaudRates(m_cbRigName->GetCurrentSelection());
    updateRadioState();
}

void SetupWizard::OnReportingEnableChanged(wxCommandEvent&)
{
    updateReportingState();
}

void SetupWizard::OnImport(wxCommandEvent&)
{
    int sel = m_chImportSource->GetSelection();
    if (sel == wxNOT_FOUND) return;
    importSettings(m_importSources[sel]);
}
