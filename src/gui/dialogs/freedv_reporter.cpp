//==========================================================================
// Name:            freedv_reporter.cpp
// Purpose:         Dialog that displays current FreeDV Reporter spots
// Created:         Jun 12, 2023
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

#include <sstream>
#include <string>
#include <set>
#include <memory>
#include <algorithm>
#include <math.h>
#include <wx/datetime.h>
#include <wx/display.h>
#include <wx/clipbrd.h>
#include <wx/wrapsizer.h>
#include <wx/menuitem.h>
#include <wx/numdlg.h>
#include <wx/textdlg.h>
#include "../controls/MsgListPopup.h"
#include "../util/FrequencyOps.h"
#include "../util/GridSquare.h"
#include "../util/WindowPositionRestore.h"

#if wxCHECK_VERSION(3,2,0)
#include <wx/uilocale.h>
#endif // wxCHECK_VERSION(3,2,0)

#if defined(WIN32)
#include <wx/headerctrl.h>
#endif // defined(WIN32)

#include "freedv_reporter.h"
#include "freedv_interface.h"

extern FreeDVInterface freedvInterface;
extern wxConfigBase* pConfig;
extern std::atomic<int> g_analog;

#define CALLSIGN_COL (0)
#define GRID_SQUARE_COL (1)
#define DISTANCE_COL (2)
#define HEADING_COL (3)
#define VERSION_COL (4)
#define FREQUENCY_COL (5)
#define TX_MODE_COL (6)
#define STATUS_COL (7)
#define USER_MESSAGE_COL (8)
#define LAST_TX_DATE_COL (9)
#define LAST_RX_CALLSIGN_COL (10)
#define SNR_COL (11)
#define LAST_UPDATE_DATE_COL (12)
#define RIGHTMOST_COL (LAST_UPDATE_DATE_COL + 1)

#define UNKNOWN_SNR_VAL (-99)

#define NUM_COLS (LAST_UPDATE_DATE_COL + 1)
#define RX_ONLY_STATUS "RX Only"
#define RX_COLORING_LONG_TIMEOUT_SEC (20)
#define RX_COLORING_SHORT_TIMEOUT_SEC (5)
#define MSG_COLORING_TIMEOUT_SEC (5)
#define STATUS_MESSAGE_MRU_MAX_SIZE (15)
#define MESSAGE_COLUMN_ID (6)

using namespace std::placeholders;

// Whether the list is sorted by the given model column.
static bool isSortingColumn_(const wxDataViewColumn* sortingColumn, unsigned int modelCol)
{
    return sortingColumn != nullptr && sortingColumn->GetModelColumn() == modelCol;
}

void FreeDVReporterDialog::createColumn_(int col, bool visible)
{
    wxDataViewColumn* colObj = nullptr;
    int minWidth = 0;
    int alignment = wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL;
    wxString colName = "";
    bool ellipsize = false;

    switch (col)
    {
        case CALLSIGN_COL:
            colName = _("Callsign");
            minWidth = 70;
            break;
        case GRID_SQUARE_COL:
            colName = _("Locator");
            minWidth = 65;
            break;
        case DISTANCE_COL:
            colName = _("km");
            minWidth = 60;
            alignment = wxALIGN_RIGHT | wxALIGN_CENTRE_VERTICAL;
            break;
        case HEADING_COL:
            // TRANSLATORS: Abbreviation of "Heading".
            colName = _("Hdg");
            minWidth = 60;
            alignment = wxALIGN_RIGHT | wxALIGN_CENTRE_VERTICAL;
            break;
        case VERSION_COL:
            colName = _("Version");
            minWidth = 70;
            break;
        case FREQUENCY_COL:
            colName = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz ? wxT("kHz") : wxT("MHz");
            minWidth = 60;
            alignment = wxALIGN_RIGHT | wxALIGN_CENTRE_VERTICAL;
            break;
        case TX_MODE_COL:
            colName = _("Mode");
            minWidth = 65;
            break;
        case STATUS_COL:
            colName = _("Status");
            minWidth = 60;
            break;
        case USER_MESSAGE_COL:
            // Note: there's Windows specific logic here, so we create the column
            // here rather than farther down.
#if defined(WIN32)
            // Use ReportMessageRenderer only on Windows so that we can render emojis in color.
            colObj = new wxDataViewColumn(_("Msg"), new ReportMessageRenderer(), col, wxCOL_WIDTH_DEFAULT, wxALIGN_CENTER, wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_REORDERABLE);
            m_listSpots->AppendColumn(colObj);
#else
            colObj = m_listSpots->AppendTextColumn(_("Msg"), col, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_DEFAULT, wxALIGN_CENTER, wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_REORDERABLE);
#endif // defined(WIN32)
            colObj->SetWidth(wxGetApp().appConfiguration.reportingUserMsgColWidth);
            minWidth = 130;
            ellipsize = true;
            break;
        case LAST_TX_DATE_COL:
            colName = _("Last TX");
            // Same minWidth as Last Update -- both show the same date/time
            // format, but Last TX is frequently still empty for every row
            // when the dialog first opens (many spotted stations haven't
            // transmitted yet this session), so autosizing against its own
            // content alone leaves it too narrow until real data arrives.
            minWidth = 100;
            break;
        case LAST_RX_CALLSIGN_COL:
            colName = _("RX Call");
            minWidth = 65;
            break;
        case SNR_COL:
            colName = _("SNR");
            minWidth = 60;
            alignment = wxALIGN_RIGHT | wxALIGN_CENTRE_VERTICAL;
            break;
        case LAST_UPDATE_DATE_COL:
            colName = _("Last Update");
            minWidth = 100;
            break;
        default:
            return;
    }

    if (colObj == nullptr)
    {
        colObj = m_listSpots->AppendTextColumn(colName, col, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_CENTER, wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_REORDERABLE);
    }
    auto renderer = colObj->GetRenderer();
    renderer->SetAlignment(alignment);
    if (col == SNR_COL)
        colObj->SetAlignment(wxALIGN_RIGHT);
    if (ellipsize)
    {
        renderer->EnableEllipsize(wxELLIPSIZE_END);
    }
    else
    {
        renderer->DisableEllipsize();
    }
    colObj->SetMinWidth(minWidth);
    if (col == wxGetApp().appConfiguration.reporterWindowCurrentSort)
    {
        colObj->SetSortOrder(wxGetApp().appConfiguration.reporterWindowCurrentSortDirection);
    }

    if (!visible)
    {
        colObj->SetHidden(true);
    }
}

FreeDVReporterDialog::FreeDVReporterDialog(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) 
    : wxFrame(parent, id, title, pos, size, style)
#if !wxCHECK_VERSION(3,3,2)
    , tipWindow_(nullptr)
#endif // !wxCHECK_VERSION(3,3,2)
    , UNKNOWN_STR("")
    , MS_REMOVAL_RGX("\\.[^+-]+")
    , TIMEZONE_RGX("([+-])([0-9]+):([0-9]+)$")
    , TZ_OFFSET_STR("-")
    , EMPTY_STR("")
    , TIME_FORMAT_STR("%x %X")
{
    WXWIDGETS_SET_LAYOUT_DIRECTION;
    
    if (wxGetApp().customConfigFileName != "")
    {
        SetTitle(wxString::Format("%s (%s)", _("FreeDV Reporter"), wxGetApp().customConfigFileName));
    }

    // Create top-level of control hierarchy.
    wxFlexGridSizer* sectionSizer = new wxFlexGridSizer(2, 1, 0, 0);
    sectionSizer->AddGrowableRow(0);
    sectionSizer->AddGrowableCol(0);
        
    // Main list box
    // =============================
    m_listSpots = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_SINGLE);

    // Associate data model.
    spotsDataModel_ = new FreeDVReporterDataModel(this);
    m_listSpots->AssociateModel(spotsDataModel_.get());

    {
        // Validate and repair the persisted column ordering unconditionally
        // (not just when its length looks wrong) -- checking size() alone
        // isn't reliable, since a config value saved by a build with a
        // different NUM_COLS (e.g. master vs. v3.0-dev, which has one fewer
        // column) can happen to still have the right *length* for this
        // build's NUM_COLS while containing an out-of-range entry from the
        // old scheme and missing a valid index that entry crowded out --
        // size() == NUM_COLS looks fine, but the content is still wrong, so
        // that column can never be created and any lookup for it later
        // asserts/crashes.
        auto& order = wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnOrder;
        std::set<int> presentIndices;
        auto iter = order->begin();
        while (iter != order->end())
        {
            // Strip anything out of range for this build's NUM_COLS, and any
            // duplicate of an index already kept.
            if (*iter >= RIGHTMOST_COL || !presentIndices.insert(*iter).second)
            {
                iter = order->erase(iter);
            }
            else
            {
                iter++;
            }
        }

        if ((int)order->size() != NUM_COLS)
        {
            log_info("Generating missing column ordering");
            for (auto index = 0; index < NUM_COLS; index++)
            {
                if (presentIndices.find(index) == presentIndices.end())
                {
                    order->push_back(index);
                }
            }
        }
    }

    while (wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnVisibility->size() < NUM_COLS)
    {
        // Generate default column visibility
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnVisibility->push_back(true);
    }

    // Windows seems to have the model column ID equal to the actual column ID regardless of the 
    // actual ordering, so we just use wxHeaderCtrl to save/restore the column ordering.
#if defined(WIN32)
    for (auto col = 0; col < NUM_COLS; col++)
#else    
    for (auto& col : wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnOrder.get())
#endif // defined(WIN32)
    {
        if (col < NUM_COLS)
        {
            log_info("Creating col %d", col);
            auto visible = (bool)wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnVisibility->at(col);
            createColumn_(col, visible);
        }
    }
    m_listSpots->AppendTextColumn(wxT(" "), RIGHTMOST_COL, wxDATAVIEW_CELL_INERT, 1, wxALIGN_CENTER, wxDATAVIEW_COL_RESIZABLE);

    // Apply column filter indicators if filters were loaded from config
    // (called after all columns are created so getColumnForModelColId_ works)
    updateColumnFilterIndicators_();

#if defined(WIN32)
    auto headerCtrl = m_listSpots->GenericGetHeader();
    wxArrayInt wxColumnOrder;
    for (auto& col : wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnOrder.get())
    {
        if (col < NUM_COLS)
        {
            wxColumnOrder.Add(col);
        }
    }
    wxColumnOrder.Add(RIGHTMOST_COL); // All columns need to be in the list to actually take effect.
    headerCtrl->SetColumnsOrder(wxColumnOrder);
#endif // defined(WIN32)
    
    sectionSizer->Add(m_listSpots, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
    
    // Bottom buttons
    // =============================
    auto reportingSettingsSizer = new wxWrapSizer(wxHORIZONTAL);

    wxBoxSizer* leftButtonSizer = new wxBoxSizer(wxHORIZONTAL);

    m_buttonOK = new wxButton(this, wxID_OK, _("Close"));
    m_buttonOK->SetToolTip(_("Closes FreeDV Reporter window."));
    leftButtonSizer->Add(m_buttonOK, 0, static_cast<int>(wxALL), 5);

    m_buttonSendQSY = new wxButton(this, wxID_ANY, _("Request QSY"));
    m_buttonSendQSY->SetToolTip(_("Asks selected user to switch to your frequency."));
    m_buttonSendQSY->Enable(false); // disable by default unless we get a valid selection
    leftButtonSizer->Add(m_buttonSendQSY, 0, static_cast<int>(wxALL), 5);

    m_buttonDisplayWebpage = new wxButton(this, wxID_ANY, _("Website"));
    m_buttonDisplayWebpage->SetToolTip(_("Opens FreeDV Reporter in your Web browser."));
    leftButtonSizer->Add(m_buttonDisplayWebpage, 0, static_cast<int>(wxALL), 5);

    reportingSettingsSizer->Add(leftButtonSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 0);

    // Band filter list
    wxBoxSizer* bandFilterSizer = new wxBoxSizer(wxHORIZONTAL);

    wxString bandList[] = {
        _("All"),
        _("160 m"),
        _("80 m"),
        _("60 m"),
        _("40 m"),
        _("30 m"),
        _("20 m"),
        _("17 m"),
        _("15 m"),
        _("12 m"),
        _("10 m"),
        _(">= 6 m"),
        _("Other"),
    };
    
    bandFilterSizer->Add(new wxStaticText(this, wxID_ANY, _("Band:"), wxDefaultPosition, wxDefaultSize, 0), 
                          0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 5);
    
    m_bandFilter = new wxComboBox(
        this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 
        sizeof(bandList) / sizeof(wxString), bandList, wxCB_DROPDOWN | wxCB_READONLY);
    m_bandFilter->SetSelection(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilter);    
    bandFilterSizer->Add(m_bandFilter, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 2);
    
    m_trackFrequency = new wxCheckBox(this, wxID_ANY, _("Track:"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    m_trackFrequency->SetToolTip(_("Filters FreeDV Reporter list based on radio's current frequency or band."));
    bandFilterSizer->Add(m_trackFrequency, 0, static_cast<int>(wxALL) | wxALIGN_LEFT | static_cast<int>(wxALIGN_CENTER_VERTICAL), 5);
    
    m_trackFreqBand = new wxRadioButton(this, wxID_ANY, _("band"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    bandFilterSizer->Add(m_trackFreqBand, 0, static_cast<int>(wxALL) | wxALIGN_LEFT | static_cast<int>(wxALIGN_CENTER_VERTICAL), 5);
    m_trackFreqBand->Enable(false);
    
    m_trackExactFreq = new wxRadioButton(this, wxID_ANY, _("freq."), wxDefaultPosition, wxDefaultSize, 0);
    bandFilterSizer->Add(m_trackExactFreq, 0, static_cast<int>(wxALL) | wxALIGN_LEFT | static_cast<int>(wxALIGN_CENTER_VERTICAL), 5);
    m_trackExactFreq->Enable(false);
    
    m_filterStatus = new wxStaticText(this, wxID_ANY, _("Idle Off"));
    bandFilterSizer->Add(m_filterStatus, 0, static_cast<int>(wxALL) | wxALIGN_LEFT | static_cast<int>(wxALIGN_CENTER_VERTICAL), 5);
    
    m_trackFrequency->SetValue(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksFrequency);
    reportingSettingsSizer->Add(bandFilterSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 0);
    
    setBandFilter((FilterFrequency)wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilter.get());

    wxBoxSizer* statusMessageSizer = new wxBoxSizer(wxHORIZONTAL);

    auto statusMessageLabel = new wxStaticText(this, wxID_ANY, _("Message:"), wxDefaultPosition, wxDefaultSize);
    statusMessageSizer->Add(statusMessageLabel, 0, static_cast<int>(wxALL) | wxALIGN_LEFT | static_cast<int>(wxALIGN_CENTER_VERTICAL), 5);

    m_statusMessage = new wxComboCtrl(this, wxID_ANY, wxT(""), wxDefaultPosition, wxSize(180, -1), wxTE_PROCESS_ENTER);
    m_statusMessage->SetPopupControl(new MsgListPopup([this](int idx){ ShowMsgItemContextMenu(idx); }));
    statusMessageSizer->Add(m_statusMessage, 0, static_cast<int>(wxALL) | wxALIGN_LEFT | static_cast<int>(wxALIGN_CENTER_VERTICAL), 5);

    m_buttonSend = new wxButton(this, wxID_ANY, _("Send"));
    m_buttonSend->SetToolTip(_("Sends message to FreeDV Reporter. Right-click for additional options."));
    statusMessageSizer->Add(m_buttonSend, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 5);

    m_buttonClear = new wxButton(this, wxID_ANY, _("Clear"));
    m_buttonClear->SetToolTip(_("Clears message from FreeDV Reporter. Right-click for additional options."));
    statusMessageSizer->Add(m_buttonClear, 0, static_cast<int>(wxALL) | static_cast<int>(wxALIGN_CENTER_VERTICAL), 5);

    reportingSettingsSizer->Add(statusMessageSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 0);
    sectionSizer->Add(reportingSettingsSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND) | wxFIXED_MINSIZE, 2);
    
    this->SetMinSize(GetBestSize());

    // Menu bar
    menuBar_ = new wxMenuBar();
    this->SetMenuBar(menuBar_);

    showMenu_ = new wxMenu();
    menuBar_->Append(showMenu_, _("Show"));

    std::vector<std::pair<int, wxString> > menus {
        {CALLSIGN_COL, _("Callsign")},
        {GRID_SQUARE_COL, _("Locator")},
        {DISTANCE_COL, _("Distance")},
        {VERSION_COL, _("Version")},
        {HEADING_COL, _("Heading")},
        {FREQUENCY_COL, _("Frequency")},
        {TX_MODE_COL, _("TX Mode")},
        {STATUS_COL, _("Status")},
        {USER_MESSAGE_COL, _("User Message")},
        {LAST_TX_DATE_COL, _("Last TX Date")},
        {LAST_RX_CALLSIGN_COL, _("Last RX Callsign")},
        {SNR_COL, _("SNR")},
        {LAST_UPDATE_DATE_COL, _("Last Update")},
    };

    for (auto& item : menus)
    {
        auto menuItem = showMenu_->Append(wxID_HIGHEST + 100 + item.first, item.second, wxEmptyString, wxITEM_CHECK);
        menuItem->Check(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnVisibility->at(item.first));
        this->Connect(wxID_HIGHEST + 100 + item.first, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(FreeDVReporterDialog::OnShowColumn));
    }

    std::vector<int> idleLongerThanMinutes { 30, 60, 90, 120 };
    filterMenu_ = new wxMenu();
    idleLongerThanMenu_ = new wxMenu();
    menuBar_->Append(filterMenu_, _("Filter"));
    filterMenu_->Append(wxID_ANY, _("Idle more than (minutes)..."), idleLongerThanMenu_);

    auto menuItem = idleLongerThanMenu_->Append(wxID_ANY, _("Disabled"), wxEmptyString, wxITEM_CHECK);
    menuItem->Check(!wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnableMaxIdleFilter);
    bool foundChecked = menuItem->IsChecked();
    this->Connect(menuItem->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(FreeDVReporterDialog::OnIdleFilter));

    for (auto& item : idleLongerThanMinutes)
    {
        auto menuItem = idleLongerThanMenu_->Append(wxID_HIGHEST + 200 + item, wxString::Format("%d", item), wxEmptyString, wxITEM_CHECK);
        menuItem->Check(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnableMaxIdleFilter && wxGetApp().appConfiguration.reportingConfiguration.freedvReporterMaxIdleMinutes == item);
        foundChecked |= menuItem->IsChecked();
        this->Connect(wxID_HIGHEST + 200 + item, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(FreeDVReporterDialog::OnIdleFilter));
    }

    menuItem = idleLongerThanMenu_->Append(wxID_HIGHEST + 200, _("Custom..."), wxEmptyString, wxITEM_CHECK);
    menuItem->Check(!foundChecked);
    this->Connect(wxID_HIGHEST + 200, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(FreeDVReporterDialog::OnIdleFilter));

    // Trigger auto-layout of window.
    // ==============================
    this->SetSizerAndFit(sectionSizer);
    this->Layout();
    
    // Move FreeDV Reporter window back into last saved position
    SetSize(wxSize(
        wxGetApp().appConfiguration.reporterWindowWidth,
        wxGetApp().appConfiguration.reporterWindowHeight));
    SetPosition(wxPoint(
        wxGetApp().appConfiguration.reporterWindowLeft,
        wxGetApp().appConfiguration.reporterWindowTop));

    // Make sure we didn't end up placing it off the screen in a location that can't
    // easily be brought back.
#if wxCHECK_VERSION(3,2,0)
    wxDisplay currentDisplay(this);
#else
    auto displayNo = wxDisplay::GetFromWindow(this);
    if (displayNo == wxNOT_FOUND)
    {
        displayNo = 0;
    }
    wxDisplay currentDisplay(displayNo);
#endif // wxCHECK_VERSION(3,2,0)
    
    auto displayGeometry = currentDisplay.GetClientArea();
    wxPoint actualPos = GetPosition();
    wxSize actualWindowSize = GetSize();
    if (actualPos.x < (displayGeometry.x - (0.9 * actualWindowSize.GetWidth())) ||
        actualPos.x > (displayGeometry.x + 0.9 * displayGeometry.width))
    {
        actualPos.x = displayGeometry.x;
    }
    if (actualPos.y < displayGeometry.y ||
        actualPos.y > (displayGeometry.y + 0.9 * displayGeometry.height))
    {
        actualPos.y = 0;
    }
    wxGetApp().appConfiguration.reporterWindowLeft = actualPos.x;
    wxGetApp().appConfiguration.reporterWindowTop = actualPos.y;
    RestoreWindowPosition(this, actualPos.x, actualPos.y);

    // Set up timers. Highlight clear timer has a slightly longer interval
    // to reduce CPU usage.
    m_highlightClearTimer = new wxTimer(this);
    m_highlightClearTimer->Start(250);
    m_resortTimer = new wxTimer(this);
    m_resortTimer->Start(100);
    m_deleteTimer = new wxTimer(this);
    m_deleteTimer->Start(1000);
    
    // Create Set popup menu
    setPopupMenu_ = new wxMenu();
    assert(setPopupMenu_ != nullptr);
    
    setSaveMenuItem_ = setPopupMenu_->Append(wxID_ANY, _("Send and save message"));
    setPopupMenu_->Connect(
        setSaveMenuItem_->GetId(), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(FreeDVReporterDialog::OnStatusTextSendAndSaveMessage),
        NULL,
        this);

    saveMenuItem_ = setPopupMenu_->Append(wxID_ANY, _("Only save message"));
    setPopupMenu_->Connect(
        saveMenuItem_->GetId(), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(FreeDVReporterDialog::OnStatusTextSaveMessage),
        NULL,
        this);

    // Create Clear popup menu
    clearPopupMenu_ = new wxMenu();
    assert(clearPopupMenu_ != nullptr);

    auto clearSelectedMenuItem = clearPopupMenu_->Append(wxID_ANY, _("Remove selected from list"));
    clearPopupMenu_->Connect(
        clearSelectedMenuItem->GetId(), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(FreeDVReporterDialog::OnStatusTextClearSelected),
        NULL,
        this
    );

    auto clearAllMenuItem = clearPopupMenu_->Append(wxID_ANY, _("Clear all messages from list"));
    clearPopupMenu_->Connect(
        clearAllMenuItem->GetId(), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(FreeDVReporterDialog::OnStatusTextClearAll),
        NULL,
        this
    );
        
    // Create context menu for individual saved message items
    msgItemPopupMenu_ = new wxMenu();
    assert(msgItemPopupMenu_ != nullptr);
    contextMenuSelectedIndex_ = wxNOT_FOUND;

    auto editMsgMenuItem = msgItemPopupMenu_->Append(wxID_ANY, _("Edit message"));
    msgItemPopupMenu_->Connect(
        editMsgMenuItem->GetId(), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(FreeDVReporterDialog::OnStatusMessageContextMenuEdit),
        NULL,
        this);

    auto deleteMsgMenuItem = msgItemPopupMenu_->Append(wxID_ANY, _("Delete message"));
    msgItemPopupMenu_->Connect(
        deleteMsgMenuItem->GetId(), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(FreeDVReporterDialog::OnStatusMessageContextMenuDelete),
        NULL,
        this);

    msgItemPopupMenu_->AppendSeparator();

    addMsgMenuItem_ = msgItemPopupMenu_->Append(wxID_ANY, _("Add new message"));
    msgItemPopupMenu_->Connect(
        addMsgMenuItem_->GetId(), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(FreeDVReporterDialog::OnStatusMessageContextMenuAdd),
        NULL,
        this);

    // Create popup menu for spots list
    spotsPopupMenu_ = new wxMenu();
    assert(spotsPopupMenu_ != nullptr);

    auto copyUserMessageMenuItem = spotsPopupMenu_->Append(wxID_ANY, _("Copy message"));
    spotsPopupMenu_->Connect(
        copyUserMessageMenuItem->GetId(), wxEVT_COMMAND_MENU_SELECTED, 
        wxCommandEventHandler(FreeDVReporterDialog::OnCopyUserMessage),
        NULL,
        this);
        
    // Create popup menu for callsign lookups
    callsignPopupMenu_ = new wxMenu();
    assert(callsignPopupMenu_ != nullptr);
    
    auto qrzMenuItem = callsignPopupMenu_->Append(wxID_ANY, _("Lookup via QRZ"));
    callsignPopupMenu_->Connect(
        qrzMenuItem->GetId(), wxEVT_COMMAND_MENU_SELECTED, 
        wxCommandEventHandler(FreeDVReporterDialog::OnQRZLookup),
        NULL,
        this);
        
    auto hamQthMenuItem = callsignPopupMenu_->Append(wxID_ANY, _("Lookup via HamQTH"));
    callsignPopupMenu_->Connect(
        hamQthMenuItem->GetId(), wxEVT_COMMAND_MENU_SELECTED, 
        wxCommandEventHandler(FreeDVReporterDialog::OnHamQTHLookup),
        NULL,
        this);
        
    auto hamCallMenuItem = callsignPopupMenu_->Append(wxID_ANY, _("Lookup via HamCall"));
    callsignPopupMenu_->Connect(
        hamCallMenuItem->GetId(), wxEVT_COMMAND_MENU_SELECTED, 
        wxCommandEventHandler(FreeDVReporterDialog::OnHamCallLookup),
        NULL,
        this);
    
    // Hook in events
    this->Connect(wxEVT_TIMER, wxTimerEventHandler(FreeDVReporterDialog::OnTimer), NULL, this);
    this->Connect(wxEVT_INIT_DIALOG, wxInitDialogEventHandler(FreeDVReporterDialog::OnInitDialog));
    this->Connect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(FreeDVReporterDialog::OnClose));
    this->Connect(wxEVT_SIZE, wxSizeEventHandler(FreeDVReporterDialog::OnSize));
    this->Connect(wxEVT_MOVE, wxMoveEventHandler(FreeDVReporterDialog::OnMove));
    this->Connect(wxEVT_SHOW, wxShowEventHandler(FreeDVReporterDialog::OnShow));
    this->Connect(wxEVT_SYS_COLOUR_CHANGED, wxSysColourChangedEventHandler(FreeDVReporterDialog::OnSystemColorChanged));
    this->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(FreeDVReporterDialog::DeselectItem), NULL, this);
    
    m_listSpots->Connect(wxEVT_DATAVIEW_SELECTION_CHANGED, wxDataViewEventHandler(FreeDVReporterDialog::OnItemSelectionChanged), NULL, this);
    m_listSpots->Connect(wxEVT_DATAVIEW_ITEM_ACTIVATED, wxDataViewEventHandler(FreeDVReporterDialog::OnItemDoubleClick), NULL, this);
    m_listSpots->Connect(wxEVT_MOTION, wxMouseEventHandler(FreeDVReporterDialog::AdjustToolTip), NULL, this);
    m_listSpots->Connect(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, wxDataViewEventHandler(FreeDVReporterDialog::OnItemRightClick), NULL, this);
    m_listSpots->Connect(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, wxDataViewEventHandler(FreeDVReporterDialog::OnColumnClick), NULL, this);
    m_listSpots->Connect(wxEVT_DATAVIEW_COLUMN_HEADER_RIGHT_CLICK, wxDataViewEventHandler(FreeDVReporterDialog::OnColumnHeaderRightClick), NULL, this);
    m_listSpots->Connect(wxEVT_DATAVIEW_COLUMN_REORDERED, wxDataViewEventHandler(FreeDVReporterDialog::OnColumnReordered), NULL, this);
    m_listSpots->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(FreeDVReporterDialog::OnSetFocus), NULL, this);

    m_statusMessage->Connect(wxEVT_TEXT, wxCommandEventHandler(FreeDVReporterDialog::OnStatusTextChange), NULL, this);
    m_buttonSend->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(FreeDVReporterDialog::OnStatusTextSend), NULL, this);
    m_buttonClear->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(FreeDVReporterDialog::OnStatusTextClear), NULL, this);
#if defined(__WXGTK__)
    // wxGTK fires wxEVT_CONTEXT_MENU on button-press, causing GTK to
    // dismiss PopupMenu on button release (see topFrame.cpp for the same
    // workaround on other widgets); use RIGHT_UP instead. MSW/OSX are
    // unaffected, so this is GTK-specific. Confirmed present on both
    // wxGTK 3.2 and 3.3+, so no version gate here.
    m_buttonSend->Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent&) { wxContextMenuEvent ctx; OnStatusTextSendContextMenu(ctx); });
    m_buttonClear->Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent&) { wxContextMenuEvent ctx; OnStatusTextClearContextMenu(ctx); });
#else
    m_buttonSend->Connect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(FreeDVReporterDialog::OnStatusTextSendContextMenu), NULL, this);
    m_buttonClear->Connect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(FreeDVReporterDialog::OnStatusTextClearContextMenu), NULL, this);
#endif

    m_buttonOK->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(FreeDVReporterDialog::OnOK), NULL, this);
    m_buttonSendQSY->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(FreeDVReporterDialog::OnSendQSY), NULL, this);
    m_buttonDisplayWebpage->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(FreeDVReporterDialog::OnOpenWebsite), NULL, this);
    
    m_bandFilter->Connect(wxEVT_TEXT, wxCommandEventHandler(FreeDVReporterDialog::OnBandFilterChange), NULL, this);
    m_trackFrequency->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FreeDVReporterDialog::OnFilterTrackingEnable), NULL, this);
    m_trackFreqBand->Connect(wxEVT_RADIOBUTTON, wxCommandEventHandler(FreeDVReporterDialog::OnFilterTrackingEnable), NULL, this);
    m_trackExactFreq->Connect(wxEVT_RADIOBUTTON, wxCommandEventHandler(FreeDVReporterDialog::OnFilterTrackingEnable), NULL, this);
    
    // Window names, used by GUI test scripts to find controls (see test/gui).
    SetName("reporter");
    m_listSpots->SetName("reporter.listSpots");
    m_buttonOK->SetName("reporter.btnClose");
    m_buttonSendQSY->SetName("reporter.btnSendQSY");
    m_buttonDisplayWebpage->SetName("reporter.btnWebsite");
    m_bandFilter->SetName("reporter.cbBandFilter");
    m_trackFrequency->SetName("reporter.ckTrackFrequency");
    m_trackFreqBand->SetName("reporter.rbTrackFreqBand");
    m_trackExactFreq->SetName("reporter.rbTrackExactFreq");
    m_filterStatus->SetName("reporter.stFilterStatus");
    m_statusMessage->SetName("reporter.cbStatusMessage");
    getMsgPopup()->SetName("reporter.lbSavedMessages");
    m_buttonSend->SetName("reporter.btnSend");
    m_buttonClear->SetName("reporter.btnClear");

    // Update status message and MRU list
    m_statusMessage->SetValue(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterStatusText);
    for (auto& msg : wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts.get())
    {
        getMsgPopup()->Append(msg);
    }

    // Trigger filter update if we're starting with tracking enabled
    m_trackFreqBand->SetValue(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksFreqBand);
    m_trackExactFreq->SetValue(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksExactFreq);
    if (wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksFrequency)
    {
        m_bandFilter->Enable(false);
        m_trackFreqBand->Enable(true);
        m_trackExactFreq->Enable(true);
        
        FilterFrequency freq = 
            getFilterForFrequency_(wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency);
        setBandFilter(freq);
    }
}

void FreeDVReporterDialog::stopTrackingPosition()
{
    this->Disconnect(wxEVT_MOVE, wxMoveEventHandler(FreeDVReporterDialog::OnMove));
}

FreeDVReporterDialog::~FreeDVReporterDialog()
{
    m_highlightClearTimer->Stop();
    delete m_highlightClearTimer;
    
    m_resortTimer->Stop();
    delete m_resortTimer;
    
    m_deleteTimer->Stop();
    delete m_deleteTimer;

    m_trackFrequency->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FreeDVReporterDialog::OnFilterTrackingEnable), NULL, this);
    m_trackFreqBand->Disconnect(wxEVT_RADIOBUTTON, wxCommandEventHandler(FreeDVReporterDialog::OnFilterTrackingEnable), NULL, this);
    m_trackExactFreq->Disconnect(wxEVT_RADIOBUTTON, wxCommandEventHandler(FreeDVReporterDialog::OnFilterTrackingEnable), NULL, this);
    
    this->Disconnect(wxEVT_TIMER, wxTimerEventHandler(FreeDVReporterDialog::OnTimer), NULL, this);
    this->Disconnect(wxEVT_SIZE, wxSizeEventHandler(FreeDVReporterDialog::OnSize));
    this->Disconnect(wxEVT_INIT_DIALOG, wxInitDialogEventHandler(FreeDVReporterDialog::OnInitDialog));
    this->Disconnect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(FreeDVReporterDialog::OnClose));
    this->Disconnect(wxEVT_MOVE, wxMoveEventHandler(FreeDVReporterDialog::OnMove));
    this->Disconnect(wxEVT_SHOW, wxShowEventHandler(FreeDVReporterDialog::OnShow));
    this->Disconnect(wxEVT_LEFT_DOWN, wxMouseEventHandler(FreeDVReporterDialog::DeselectItem), NULL, this);
    
    m_listSpots->Disconnect(wxEVT_DATAVIEW_SELECTION_CHANGED, wxDataViewEventHandler(FreeDVReporterDialog::OnItemSelectionChanged), NULL, this);
    m_listSpots->Disconnect(wxEVT_DATAVIEW_ITEM_ACTIVATED, wxDataViewEventHandler(FreeDVReporterDialog::OnItemDoubleClick), NULL, this);
    m_listSpots->Disconnect(wxEVT_MOTION, wxMouseEventHandler(FreeDVReporterDialog::AdjustToolTip), NULL, this);
    m_listSpots->Disconnect(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, wxDataViewEventHandler(FreeDVReporterDialog::OnItemRightClick), NULL, this);
    m_listSpots->Disconnect(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, wxDataViewEventHandler(FreeDVReporterDialog::OnColumnClick), NULL, this);
    m_listSpots->Disconnect(wxEVT_DATAVIEW_COLUMN_HEADER_RIGHT_CLICK, wxDataViewEventHandler(FreeDVReporterDialog::OnColumnHeaderRightClick), NULL, this);
    m_listSpots->Disconnect(wxEVT_DATAVIEW_COLUMN_REORDERED, wxDataViewEventHandler(FreeDVReporterDialog::OnColumnReordered), NULL, this);
    m_listSpots->Disconnect(wxEVT_SET_FOCUS, wxFocusEventHandler(FreeDVReporterDialog::OnSetFocus), NULL, this);

    m_statusMessage->Disconnect(wxEVT_TEXT, wxCommandEventHandler(FreeDVReporterDialog::OnStatusTextChange), NULL, this);
    m_buttonSend->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(FreeDVReporterDialog::OnStatusTextSend), NULL, this);
    m_buttonClear->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(FreeDVReporterDialog::OnStatusTextClear), NULL, this);
#if !defined(__WXGTK__)
    m_buttonSend->Disconnect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(FreeDVReporterDialog::OnStatusTextSendContextMenu), NULL, this);
    m_buttonClear->Disconnect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(FreeDVReporterDialog::OnStatusTextClearContextMenu), NULL, this);
#endif

    m_buttonOK->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(FreeDVReporterDialog::OnOK), NULL, this);
    m_buttonSendQSY->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(FreeDVReporterDialog::OnSendQSY), NULL, this);
    m_buttonDisplayWebpage->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(FreeDVReporterDialog::OnOpenWebsite), NULL, this);
    
    m_bandFilter->Disconnect(wxEVT_TEXT, wxCommandEventHandler(FreeDVReporterDialog::OnBandFilterChange), NULL, this);
}

bool FreeDVReporterDialog::isTextMessageFieldInFocus()
{
    return m_statusMessage->HasFocus();
}

wxDataViewColumn* FreeDVReporterDialog::getColumnForModelColId_(unsigned int col)
{
    wxDataViewColumn* item = nullptr;

    for (unsigned int index = 0; index < m_listSpots->GetColumnCount(); index++)
    {
        item = m_listSpots->GetColumn(index);
        if (item->GetModelColumn() == col)
        {
            break;
        }
        item = nullptr;
    }
    assert(item != nullptr);
    return item;
}

void FreeDVReporterDialog::refreshLayout()
{
    // Update row colors
    msgRowBackgroundColor = wxColour(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterMsgRowBackgroundColor);
    msgRowForegroundColor = wxColour(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterMsgRowForegroundColor);
    txRowBackgroundColor = wxColour(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterTxRowBackgroundColor);
    txRowForegroundColor = wxColour(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterTxRowForegroundColor);
    rxRowBackgroundColor = wxColour(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRxRowBackgroundColor);
    rxRowForegroundColor = wxColour(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRxRowForegroundColor);
 
    wxDataViewColumn* item = getColumnForModelColId_(DISTANCE_COL);

    if (wxGetApp().appConfiguration.reportingConfiguration.useMetricDistances)
    {
        item->SetTitle(_("km"));
    }
    else
    {
        item->SetTitle(_("Miles"));
    }
    
    // Refresh frequency units as appropriate.
    item = getColumnForModelColId_(FREQUENCY_COL);
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
    {
        item->SetTitle("kHz");
    }
    else
    {
        item->SetTitle("MHz");
    }

    // Change direction/heading column label based on preferences
    item = getColumnForModelColId_(HEADING_COL);
    auto renderer = item->GetRenderer();
    if (wxGetApp().appConfiguration.reportingConfiguration.reportingDirectionAsCardinal)
    {
        // TRANSLATORS: Abbreviation of "Direction".
        item->SetTitle(_("Dir"));
        item->SetAlignment(wxALIGN_LEFT);
        renderer->SetAlignment(wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);
    }
    else
    {
        item->SetTitle(_("Hdg"));
        item->SetAlignment(wxALIGN_RIGHT);
        renderer->SetAlignment(wxALIGN_RIGHT | wxALIGN_CENTRE_VERTICAL);
    }

    // Update filter status in window
    updateFilterStatus_();

    // Update column filter indicators (must happen after dynamic titles are set above)
    updateColumnFilterIndicators_();

    // Refresh all data based on current settings and filters.
    FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();
    model->requestColumnAutosize();
    model->refreshAllRows();

    // Update status controls.
    wxCommandEvent tmpEvent;
    OnStatusTextChange(tmpEvent);
}

void FreeDVReporterDialog::updateFilterStatus_()
{
    wxString labelText;

    if (wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnableMaxIdleFilter)
    {
        // TRANSLATORS: %d is a number of minutes.
        labelText = wxString::Format(_("Idle %d"), (int)wxGetApp().appConfiguration.reportingConfiguration.freedvReporterMaxIdleMinutes);
    }
    else
    {
        labelText = _("Idle Off");
    }
    m_filterStatus->SetLabel(labelText);

#if wxCHECK_VERSION(3,1,3)
    if (wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnableMaxIdleFilter)
    {
        auto appearance = wxSystemSettings::GetAppearance();
        if (appearance.IsDark())
        {
            m_filterStatus->SetForegroundColour(wxTheColourDatabase->Find("SALMON"));
        }
        else
        {
            m_filterStatus->SetForegroundColour(wxTheColourDatabase->Find("FIREBRICK"));
        }
    }
    else
    {
        m_filterStatus->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    }
#endif // wxCHECK_VERSION(3,1,3)
}

void FreeDVReporterDialog::setReporter(std::shared_ptr<FreeDVReporter> const& reporter)
{
    FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();
    model->setReporter(reporter);
    
    if (reporter)
    {
        // Update status message
        auto statusMsg = m_statusMessage->GetValue();
        reporter->updateMessage((const char*)statusMsg.utf8_str());
    }
}

void FreeDVReporterDialog::OnShowColumn(wxCommandEvent& event)
{
    // Invert visibility value
    auto columnId = event.GetId() - wxID_HIGHEST - 100;
    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnVisibility->at(columnId) = 
        !wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnVisibility->at(columnId);

    auto newColValue =
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnVisibility->at(columnId);

    // Note: the event's object is the menu, not the item that was chosen.
    wxMenuItem* menuItem = showMenu_->FindItem(event.GetId());
    if (menuItem != nullptr)
    {
        menuItem->Check(newColValue);
    }

    // Set column visibility in wxDataViewCtl.
    auto col = getColumnForModelColId_(columnId);
    col->SetHidden(!newColValue);

    // A newly shown column may have been hidden across content changes.
    FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();
    model->requestColumnAutosize();
}

void FreeDVReporterDialog::OnIdleFilter(wxCommandEvent& event)
{
    if (event.GetId() >= (wxID_HIGHEST + 200))
    {
        auto numMinutes = event.GetId() - wxID_HIGHEST - 200;
        if (numMinutes == 0)
        {
            // need to ask user for number of minutes
            numMinutes = 
                wxGetNumberFromUser(wxEmptyString, _("Enter number of minutes: "), _("Max Idle Time"), wxGetApp().appConfiguration.reportingConfiguration.freedvReporterMaxIdleMinutes, 1, 1440, this);
            if (numMinutes > -1)
            {
                auto menuItems = idleLongerThanMenu_->GetMenuItems();
                bool found = false;
                for (auto& item : menuItems)
                {
                    if (item->GetId() == (wxID_HIGHEST + 200 + numMinutes))
                    {
                        found = true;
                        item->Check(true);
                    }
                    else
                    {
                        item->Check(false);
                    }
                }
                if (!found)
                {
                    auto item = idleLongerThanMenu_->FindItem(event.GetId());
                    item->Check(true);
                }

                wxGetApp().appConfiguration.reportingConfiguration.freedvReporterMaxIdleMinutes =
                    numMinutes;
                wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnableMaxIdleFilter = true;
            }
            else
            {
                // No changes needed, other than putting the check marks back to
                // reflect what's in effect (selecting "Custom..." changed them).
                bool filterEnabled = wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnableMaxIdleFilter;
                auto currentMinutes = wxGetApp().appConfiguration.reportingConfiguration.freedvReporterMaxIdleMinutes;
                auto menuItems = idleLongerThanMenu_->GetMenuItems();
                bool found = false;
                for (auto& item : menuItems)
                {
                    // "Disabled" is the only item without an ID of our own choosing.
                    bool isCurrent = filterEnabled ?
                        (item->GetId() == (wxID_HIGHEST + 200 + currentMinutes)) :
                        (item->GetId() < (wxID_HIGHEST + 200));
                    found |= isCurrent;
                    item->Check(isCurrent);
                }

                if (!found)
                {
                    auto item = idleLongerThanMenu_->FindItem(event.GetId());
                    item->Check(true);
                }
                return;
            }
        }
        else
        {
            auto menuItems = idleLongerThanMenu_->GetMenuItems();
            for (auto& item : menuItems)
            {
                if (item->GetId() == (wxID_HIGHEST + 200 + numMinutes))
                {
                    item->Check(true);
                }
                else
                {
                    item->Check(false);
                }
            }

            wxGetApp().appConfiguration.reportingConfiguration.freedvReporterMaxIdleMinutes =
                numMinutes;
            wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnableMaxIdleFilter = true;
        }
    }
    else
    {
        auto menuItems = idleLongerThanMenu_->GetMenuItems();
        for (auto& item : menuItems)
        {
            if (item->GetId() != event.GetId())
            {
                item->Check(false);
            }
            else
            {
                item->Check(true);
            }
        }

        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnableMaxIdleFilter = false;
    }

    // Update filter status in window
    updateFilterStatus_();

    // Allow changes to take effect.
    FreeDVReporterDataModel* model = static_cast<FreeDVReporterDataModel*>(m_listSpots->GetModel());
    model->refreshAllRows();
}

void FreeDVReporterDialog::DeselectItem(wxMouseEvent& event)
{
    DeselectItem();
    event.Skip();
}


void FreeDVReporterDialog::DeselectItem()
{
    wxDataViewItem item = m_listSpots->GetSelection();
    if (item.IsOk())
    {
        wxGetApp().lastSelectedLoggingRow = MainApp::UNSELECTED;
        m_listSpots->Unselect(item);
    }

    // Deselecting from here isn't reported as a selection change, so the
    // button that depends on there being a selection has to be told.
    m_buttonSendQSY->Enable(false);
}

void FreeDVReporterDialog::OnSystemColorChanged(wxSysColourChangedEvent& event)
{
    // Works around issues on wxWidgets with certain controls not changing backgrounds
    // when the user switches between light and dark mode.

    // TBD - see if this workaround is still necessary
#if 0
    wxColour currentControlBackground = wxTransparentColour;
    m_listSpots->SetBackgroundColour(currentControlBackground);
#if !defined(WIN32)
    ((wxWindow*)m_listSpots->m_headerWin)->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
#endif //!defined(WIN32)
#endif

    updateFilterStatus_();
    event.Skip();
}

void FreeDVReporterDialog::OnShow(wxShowEvent&)
{
    wxGetApp().appConfiguration.reporterWindowVisible = true;

    auto selected = m_listSpots->GetSelection();
    if (selected.IsOk())
    {
        m_listSpots->Unselect(selected);
    }
}

void FreeDVReporterDialog::OnSize(wxSizeEvent& event)
{
    auto sz = event.GetSize();
    
    wxGetApp().appConfiguration.reporterWindowWidth = sz.GetWidth();
    wxGetApp().appConfiguration.reporterWindowHeight = sz.GetHeight();

    Layout();
    
#if defined(WIN32)
    // Only auto-resize columns on Windows due to known rendering bugs. Trying to do so on other
    // platforms causes excessive CPU usage for no benefit.
    autosizeColumns();
#endif // defined(WIN32)
}

void FreeDVReporterDialog::OnMove(wxMoveEvent& event)
{
    auto pos = event.GetPosition();

    wxGetApp().appConfiguration.reporterWindowLeft = pos.x;
    wxGetApp().appConfiguration.reporterWindowTop = pos.y;
    
    Layout();
    
#if defined(WIN32)
    // Only auto-resize columns on Windows due to known rendering bugs. Trying to do so on other
    // platforms causes excessive CPU usage for no benefit.
    autosizeColumns();
#endif // defined(WIN32)
}

void FreeDVReporterDialog::OnOK(wxCommandEvent&)
{
    // Preserve sort column/ordering
    for (unsigned int index = 0; index < m_listSpots->GetColumnCount(); index++)
    {
        auto colObj = getColumnForModelColId_(index);
        if (colObj != nullptr && colObj->IsSortKey())
        {
            wxGetApp().appConfiguration.reporterWindowCurrentSort = colObj->GetModelColumn();
            wxGetApp().appConfiguration.reporterWindowCurrentSortDirection = colObj->IsSortOrderAscending();
            break;
        }
    }
   
    // Preserve column ordering
#if !defined(WIN32)
    wxDataViewEvent tmp;
    OnColumnReordered(tmp);
#endif // !defined(WIN32)
 
    // Preserve Msg column width
    auto userMsgCol = getColumnForModelColId_(USER_MESSAGE_COL);
    wxGetApp().appConfiguration.reportingUserMsgColWidth = userMsgCol->GetWidth();

    wxGetApp().appConfiguration.reporterWindowVisible = false;
    Hide();
}

void FreeDVReporterDialog::OnSendQSY(wxCommandEvent&)
{
    auto selected = m_listSpots->GetSelection();
    if (selected.IsOk())
    {
        FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();
        model->requestQSY(selected, wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency, ""); // Custom message TBD
        
        wxString fullMessage = wxString::Format(_("QSY request sent to %s"), model->getCallsign(selected));
        wxMessageBox(fullMessage, _("FreeDV Reporter"), wxOK | wxICON_INFORMATION, this);

        DeselectItem();
    }
}

void FreeDVReporterDialog::OnOpenWebsite(wxCommandEvent&)
{
    std::string url = std::string("https://") + (const char*)wxGetApp().appConfiguration.reportingConfiguration.freedvReporterHostname->utf8_str() + "/";
    LaunchBrowser(url);
    DeselectItem();
}

void FreeDVReporterDialog::OnClose(wxCloseEvent&)
{
    // Preserve sort column/ordering
    bool found = false;
    for (unsigned int index = 0; index < m_listSpots->GetColumnCount(); index++)
    {
        auto colObj = getColumnForModelColId_(index);
        if (colObj != nullptr && colObj->IsSortKey())
        {
            found = true;
            wxGetApp().appConfiguration.reporterWindowCurrentSort = colObj->GetModelColumn();
            wxGetApp().appConfiguration.reporterWindowCurrentSortDirection = colObj->IsSortOrderAscending();
            break;
        }
    }
   
    if (!found)
    {
        wxGetApp().appConfiguration.reporterWindowCurrentSort = -1;
        wxGetApp().appConfiguration.reporterWindowCurrentSortDirection = true;
    }

    // Preserve column ordering
#if !defined(WIN32)
    wxDataViewEvent tmp;
    OnColumnReordered(tmp);
#endif // !defined(WIN32)
 
    // Preserve Msg column width
    auto userMsgCol = getColumnForModelColId_(USER_MESSAGE_COL);
    wxGetApp().appConfiguration.reportingUserMsgColWidth = userMsgCol->GetWidth();
    
    wxGetApp().appConfiguration.reporterWindowVisible = false;
    Hide();
}

void FreeDVReporterDialog::OnItemSelectionChanged(wxDataViewEvent& event)
{
    if (event.GetItem().IsOk() 
#if !(defined(WIN32) || defined(__APPLE__))
&& isSelectionPossible_
#endif // !(defined(WIN32) || defined(__APPLE__)
        )
    {
        refreshQSYButtonState();

        // Bring up tooltip for longer reporting messages if the user happened to click on that column.
        wxMouseEvent dummyEvent;
        AdjustToolTip(dummyEvent);

        // For logging: make sure FreeDV Reporter information is used
        wxGetApp().lastSelectedLoggingRow = MainApp::FREEDV_REPORTER;
    }
    else
    {
        m_buttonSendQSY->Enable(false);
        DeselectItem();
    }
}

void FreeDVReporterDialog::OnBandFilterChange(wxCommandEvent&)
{
    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilter = 
        m_bandFilter->GetSelection();
    
    FilterFrequency freq = 
        (FilterFrequency)wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilter.get();
    setBandFilter(freq);

    // Defer deselection until after UI updates
    CallAfter([this]() { DeselectItem(); });
}

void FreeDVReporterDialog::FreeDVReporterDataModel::deallocateRemovedItems()
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);
    CallbackHandler handler;
    handler.fn = [this](CallbackHandler&) {
        std::unique_lock<std::recursive_mutex> lk(dataMtx_);
        
        std::vector<std::string> keysToRemove;
        auto curDate = wxDateTime::Now().ToUTC();
        
        for (auto& item : allReporterData_)
        {
            if (
                item.second->isPendingDelete &&
                !item.second->deleteTime.ToUTC().IsEqualUpTo(curDate, wxTimeSpan(0, 0, 1)))
            {
                keysToRemove.push_back(item.first);
                delete item.second;
            }
        }
        
        for (auto& key : keysToRemove)
        {
            allReporterData_.erase(key);
        }
    };

    fnQueue_.push_back(std::move(handler));
    parent_->CallAfter(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::execQueuedAction_, this));
}

void FreeDVReporterDialog::FreeDVReporterDataModel::triggerResort()
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);
    CallbackHandler handler;
    handler.fn = [this](CallbackHandler&) {
        Resort();
    };
    fnQueue_.push_back(std::move(handler));
    parent_->CallAfter(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::execQueuedAction_, this));
}

#if defined(__APPLE__)
void FreeDVReporterDialog::FreeDVReporterDataModel::setColumnAutosize_(bool autosize)
#else
void FreeDVReporterDialog::FreeDVReporterDataModel::setColumnAutosize_(bool)
#endif // defined(__APPLE__)
{
#if defined(__APPLE__)
    if (autosize == columnsAutosized_)
    {
        return;
    }
    columnsAutosized_ = autosize;

    if (autosize)
    {
        // Re-enable autosizing
        for (unsigned int index = 0; index < parent_->m_listSpots->GetColumnCount(); index++)
        {
            auto col = parent_->m_listSpots->GetColumn(index);
            if (col->GetModelColumn() != USER_MESSAGE_COL && index != RIGHTMOST_COL)
            {
                col->SetWidth(wxCOL_WIDTH_AUTOSIZE);
            }
        }
    }
    else
    {
        // Temporarily disable autosizing prior to item updates.
        // This is due to performance issues on macOS -- see https://github.com/wxWidgets/wxWidgets/issues/25972
        for (unsigned int index = 0; index < parent_->m_listSpots->GetColumnCount(); index++)
        {
            auto col = parent_->m_listSpots->GetColumn(index);
            col->SetWidth(col->GetWidth()); // GetWidth doesn't return AUTOSIZE
        }
    }
#endif // defined(__APPLE__)
}

#if defined(__APPLE__)
// Refits just the columns whose widest text changed at the last check (see
// maxTextWidthsChanged_()). Fitting a column measures every row, so this is several
// times cheaper than refitting all of them, and the result is the same: a column's
// fitted width only depends on its widest text and its header.
void FreeDVReporterDialog::FreeDVReporterDataModel::refitChangedColumns_()
{
    for (unsigned int index = 0; index < parent_->m_listSpots->GetColumnCount(); index++)
    {
        auto col = parent_->m_listSpots->GetColumn(index);
        auto modelCol = col->GetModelColumn();
        if (modelCol == USER_MESSAGE_COL || index == RIGHTMOST_COL ||
            modelCol >= maxTextWidthChanged_.size() || !maxTextWidthChanged_[modelCol])
        {
            continue;
        }

        // Fits the column right away (wxDataViewColumn::SetWidth() on macOS).
        col->SetWidth(wxCOL_WIDTH_AUTOSIZE);
        if (!columnsAutosized_)
        {
            // Keep it at a fixed width like the others until they're all refitted.
            col->SetWidth(col->GetWidth());
        }
    }
}
#endif // defined(__APPLE__)

#if defined(__APPLE__) || defined(WIN32)
bool FreeDVReporterDialog::FreeDVReporterDataModel::maxTextWidthsChanged_()
{
    std::vector<int> maxWidths(NUM_COLS, 0);
    std::unique_ptr<wxClientDC> dc; // only created if a row needs measuring

    for (auto& item : allReporterData_)
    {
        auto row = item.second;
        if (!row->isVisible || row->isPendingDelete)
        {
            continue;
        }

        if (row->cellTexts.size() != NUM_COLS)
        {
            row->cellTexts.assign(NUM_COLS, wxString());
            row->cellTextWidths.assign(NUM_COLS, -1); // not measured yet
            row->cellTextsStale = true;
        }

        if (row->cellTextsStale)
        {
            // An update usually changes just one or two of a row's cells, so only
            // measure the ones whose text actually changed.
            row->cellTextsStale = false;
            for (int col = 0; col < NUM_COLS; col++)
            {
                // USER_MESSAGE_COL isn't autosized.
                if (col == USER_MESSAGE_COL)
                {
                    continue;
                }

                wxString text = getColumnDisplayValue_(row, col);
                if (row->cellTextWidths[col] >= 0 && text == row->cellTexts[col])
                {
                    continue;
                }

                if (!dc)
                {
                    dc = std::make_unique<wxClientDC>(parent_->m_listSpots);
                    dc->SetFont(parent_->m_listSpots->GetFont());
                }
                row->cellTextWidths[col] = dc->GetTextExtent(text).GetWidth();
                row->cellTexts[col] = std::move(text);
            }
        }

        for (int col = 0; col < NUM_COLS; col++)
        {
            // (USER_MESSAGE_COL is never measured and stays at -1.)
            maxWidths[col] = std::max(maxWidths[col], row->cellTextWidths[col]);
        }
    }

    bool firstCheck = maxTextWidths_.size() != (size_t)NUM_COLS;
    maxTextWidthChanged_.assign(NUM_COLS, false);
    bool changed = false;
    for (int col = 0; col < NUM_COLS; col++)
    {
        if (firstCheck || maxWidths[col] != maxTextWidths_[col])
        {
            maxTextWidthChanged_[col] = true;
            changed = true;
        }
    }
    maxTextWidths_ = std::move(maxWidths);
    return changed;
}
#endif // defined(__APPLE__) || defined(WIN32)

void FreeDVReporterDialog::FreeDVReporterDataModel::updateHighlights()
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);
    CallbackHandler handler;
    
    handler.fn = [this](CallbackHandler&) {
        std::unique_lock<std::recursive_mutex> lk(dataMtx_);

        // Iterate across all visible rows. If a row is currently highlighted
        // green and it's been more than >20 seconds, clear coloring.
        auto curDate = wxDateTime::Now().ToUTC();

        wxDataViewItem currentSelection = parent_->m_listSpots->GetSelection();

        // Looked up once per pass rather than for every row.
        const wxColour windowColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
        const wxColour windowTextColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
        
        wxDataViewItemArray itemsAdded;
        wxDataViewItemArray itemsChanged;
        wxDataViewItemArray itemsDeleted;
        bool contentChanged = false;
        for (auto& item : allReporterData_)
        {
            if (item.second->isPendingDelete)
            {
                if (item.second->isVisible)
                {
                    setColumnAutosize_(false);
                    contentChanged = true;

                    wxDataViewItem dvi(item.second);
                    ItemDeleted(wxDataViewItem(nullptr), dvi);
                    item.second->isVisible = false;
                    parent_->Unselect(dvi);
                    itemsDeleted.Add(dvi);
                    
                    if (currentSelection.IsOk() && dvi.GetID() == currentSelection.GetID())
                    {
                        // Selection is no longer valid.
                        currentSelection = wxDataViewItem(nullptr);
                    }
                }
                continue;
            }
            auto reportData = item.second;

            bool isTransmitting = reportData->transmitting;
            bool isReceivingValidCallsign = 
                reportData->lastRxDate.IsValid() && 
                reportData->lastRxCallsign != "" &&
                reportData->lastRxDate.ToUTC().IsEqualUpTo(curDate, wxTimeSpan(0, 0, RX_COLORING_LONG_TIMEOUT_SEC));
            bool isReceivingNotValidCallsign = 
                reportData->lastRxDate.IsValid() && 
                reportData->lastRxCallsign == "" &&
                reportData->lastRxDate.ToUTC().IsEqualUpTo(curDate, wxTimeSpan(0, 0, RX_COLORING_SHORT_TIMEOUT_SEC));
            bool isMessaging = 
                reportData->lastUpdateUserMessage.IsValid() && 
                reportData->lastUpdateUserMessage.ToUTC().IsEqualUpTo(curDate, wxTimeSpan(0, 0, MSG_COLORING_TIMEOUT_SEC));

            // Messaging notifications take highest priority.
            wxColour backgroundColor = windowColor;
            wxColour foregroundColor = windowTextColor;

            if (isMessaging)
            {
                backgroundColor = parent_->msgRowBackgroundColor;
                foregroundColor = parent_->msgRowForegroundColor;
            }
            else if (isTransmitting)
            {
                backgroundColor = parent_->txRowBackgroundColor;
                foregroundColor = parent_->rxRowForegroundColor;
            }
            else if (isReceivingValidCallsign || isReceivingNotValidCallsign)
            {
                backgroundColor = parent_->rxRowBackgroundColor;
                foregroundColor = parent_->rxRowForegroundColor;
            }
    #if defined(__APPLE__)
            else
            {
                // To ensure that the columns don't have a different color than the rest of the control.
                // Needed mainly for macOS.
                backgroundColor = wxColour(wxTransparentColour);
            }
    #endif // defined(__APPLE__)

            bool isHighlightUpdated = 
                backgroundColor != reportData->backgroundColor ||
                foregroundColor != reportData->foregroundColor;
            if (isHighlightUpdated)
            {
                reportData->backgroundColor = backgroundColor;
                reportData->foregroundColor = foregroundColor;
            }

            bool newVisibility = !isFiltered_(reportData);
            if (newVisibility != reportData->isVisible)
            {
                setColumnAutosize_(false);
                contentChanged = true;
                
                reportData->isVisible = newVisibility;
                if (newVisibility)
                {
                    // Text may have changed while the row was hidden.
                    reportData->cellTextsStale = true;

                    wxDataViewItem dvi(reportData);
                    ItemAdded(wxDataViewItem(nullptr), dvi);
                    itemsAdded.Add(dvi);
                }
                else
                {
                    wxDataViewItem dvi(reportData);
                    ItemDeleted(wxDataViewItem(nullptr), dvi);
                    itemsDeleted.Add(dvi);
                    if (currentSelection.IsOk() && currentSelection.GetID() == dvi.GetID())
                    {
                        // Selection no longer valid
                        currentSelection = wxDataViewItem(nullptr);
                    }
                }
                sortOnNextTimerInterval = true;
            }
            else
            {
                if (isHighlightUpdated || reportData->isPendingUpdate)
                {
                    if (reportData->isVisible)
                    {
                        if (reportData->isPendingUpdate)
                        {
                            contentChanged = true;
                            reportData->cellTextsStale = true;
                        }
                        reportData->isPendingUpdate = false;

                        wxDataViewItem dvi(reportData);
                        itemsChanged.Add(dvi);
                    }
                }
            }
        }
            
        if (itemsChanged.size() > 0)
        {
            setColumnAutosize_(false);
            ItemsChanged(itemsChanged);
        }
        
        if (contentChanged || columnsNeedWidthCheck_ || columnsNeedAutosize_)
        {
#if defined(__APPLE__) || defined(WIN32)
            // Refitting measures every row of every column, so only do it when the
            // widest text in some column has actually changed.
            bool refitAll = columnsNeedAutosize_;
            bool refit = maxTextWidthsChanged_() || refitAll;
#else
            // GTK sizes autosized columns itself; there's nothing to refit.
            bool refit = false;
#endif // defined(__APPLE__) || defined(WIN32)

            columnsNeedAutosize_ = false;
            columnsNeedWidthCheck_ = false;
            if (refit)
            {
#if defined(WIN32)
                // Only auto-resize columns on Windows due to known rendering bugs. Trying to do so on other
                // platforms causes excessive CPU usage for no benefit.
                parent_->autosizeColumns();
#elif defined(__APPLE__)
                if (refitAll)
                {
                    setColumnAutosize_(true);
                }
                else
                {
                    refitChangedColumns_();
                }
#else
                setColumnAutosize_(true);
#endif // defined(WIN32)
            }
        }
    };
    fnQueue_.push_back(std::move(handler));
    parent_->CallAfter(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::execQueuedAction_, this));
}

void FreeDVReporterDialog::OnTimer(wxTimerEvent& event)
{
    // Keep Last TX at least as wide as Last Update's actual current width --
    // both show the same date/time format, but Last TX is frequently still
    // empty for every row when the dialog first opens (many spotted
    // stations haven't transmitted yet this session), so its own autosize
    // alone leaves it too narrow until real content exists. Checked here
    // (a timer that's already running continuously) rather than once at
    // construction time, since neither column's real autosized width is
    // known reliably that early -- the dialog isn't shown yet and no real
    // spot data has arrived, so querying GetWidth() then only captures a
    // premature, header-only value for both columns instead of Last
    // Update's genuine one.
    auto lastUpdateWidth = getColumnForModelColId_(LAST_UPDATE_DATE_COL)->GetWidth();
    auto lastTxCol = getColumnForModelColId_(LAST_TX_DATE_COL);
    if (lastTxCol->GetWidth() < lastUpdateWidth)
    {
        lastTxCol->SetWidth(lastUpdateWidth);
    }

    FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();
    if (event.GetTimer().GetId() == m_highlightClearTimer->GetId())
    {
        model->updateHighlights();
    }
    else if (event.GetTimer().GetId() == m_deleteTimer->GetId())
    {
        model->deallocateRemovedItems();
    }
    else
    {
        if (model->sortOnNextTimerInterval)
        {
            model->triggerResort();
            model->sortOnNextTimerInterval = false;
        }
    }
}

void FreeDVReporterDialog::OnFilterTrackingEnable(wxCommandEvent&)
{
    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksFrequency
        = m_trackFrequency->GetValue();
    m_bandFilter->Enable(
        !wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksFrequency);
    
    m_trackFreqBand->Enable(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksFrequency);
    m_trackExactFreq->Enable(wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksFrequency);
    
    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksFreqBand =
        m_trackFreqBand->GetValue();
    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksExactFreq =
        m_trackExactFreq->GetValue();
    
    FilterFrequency freq;
    if (wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksFrequency)
    {
        freq = 
            getFilterForFrequency_(wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency);
    }
    else
    {
        freq = 
            (FilterFrequency)wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilter.get();
    }

    setBandFilter(freq);

    // Defer deselection until after UI updates
    CallAfter([this]() { DeselectItem(); });
}

void FreeDVReporterDialog::OnItemDoubleClick(wxDataViewEvent& event)
{
    if (event.GetItem().IsOk())
    {
        FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();
        auto frequency = model->getFrequency(event.GetItem());
        if (wxGetApp().rigFrequencyController &&
            (wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges ||
            wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqChangesOnly))
        {
            // The spot's frequency is the remote station's centre frequency;
            // convert to dial frequency before commanding the rig. The
            // offset depends only on g_analog (real analog voice mode), not
            // on the hamlibUseAnalogModes label preference.
            bool useAnalog = g_analog.load(std::memory_order_relaxed);
            auto mode = GetModeForFrequency(
                frequency,
                wxGetApp().appConfiguration.rigControlConfiguration.hamlibUseAnalogModes,
                useAnalog);
            wxGetApp().rigFrequencyController->setFrequency(DisplayToDialFreq(frequency, mode, useAnalog));

            if (wxGetApp().appConfiguration.rigControlConfiguration.hamlibEnableFreqModeChanges)
            {
                wxGetApp().rigFrequencyController->setMode(mode);
            }
        }
        DeselectItem();
    }
}

void FreeDVReporterDialog::AdjustToolTip(wxMouseEvent&)
{
    const wxPoint pt = wxGetMousePosition();
    int mouseX = pt.x - m_listSpots->GetScreenPosition().x;
    int mouseY = pt.y - m_listSpots->GetScreenPosition().y;
    
    wxRect rect;
    wxDataViewItem item;
    wxDataViewColumn* col;
    m_listSpots->HitTest(wxPoint(mouseX, mouseY), item, col);
    if (item.IsOk() && IsActive())
    {
        // Linux workaround to avoid inadvertent selections.
        isSelectionPossible_ = true;

        // Show popup corresponding to the full message.
        FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();
        tempUserMessage_ = wxT("");
        tempCallsign_ = wxT("");
    
        if (col->GetModelColumn() == USER_MESSAGE_COL)
        {
            tempUserMessage_ = model->getUserMessage(item);
            rect = m_listSpots->GetItemRect(item, col);
#if wxCHECK_VERSION(3,3,2)
            if (IsActive() && !tipWindow_ && tempUserMessage_ != wxT(""))
#else
            if (IsActive() && tipWindow_ == nullptr && tempUserMessage_ != wxT(""))
#endif  // wxCHECK_VERSION(3,3,2)
            {
                // Use screen coordinates to determine bounds.
                auto pos = rect.GetPosition();
                rect.SetPosition(ClientToScreen(pos));

#if wxCHECK_VERSION(3,3,2)
                tipWindow_ = wxTipWindow::New(m_listSpots, tempUserMessage_, 1000, &rect);
#else
                tipWindow_ = new wxTipWindow(m_listSpots, tempUserMessage_, 1000, &tipWindow_, &rect);
#endif // wxCHECK_VERSION(3,3,2)
                
                tipWindow_->Connect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(FreeDVReporterDialog::OnRightClickSpotsList), NULL, this);
                tipWindow_->Connect(wxEVT_RIGHT_DOWN, wxMouseEventHandler(FreeDVReporterDialog::SkipMouseEvent), NULL, this);
                tipWindow_->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(FreeDVReporterDialog::OnLeftClickTooltip), NULL, this);
                tipWindow_->Connect(wxEVT_MOUSEWHEEL, wxMouseEventHandler(FreeDVReporterDialog::OnMouseWheelTooltip), NULL, this);
            
                // Make sure we actually override behavior of needed events inside the tooltip.
                for (auto& child : tipWindow_->GetChildren())
                {
                    child->Connect(wxEVT_RIGHT_DOWN, wxMouseEventHandler(FreeDVReporterDialog::SkipMouseEvent), NULL, this);
                    child->Connect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(FreeDVReporterDialog::OnRightClickSpotsList), NULL, this);
                    child->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(FreeDVReporterDialog::OnLeftClickTooltip), NULL, this);
                    child->Connect(wxEVT_MOUSEWHEEL, wxMouseEventHandler(FreeDVReporterDialog::OnMouseWheelTooltip), NULL, this);
                }
            }
        }
        else if (col->GetModelColumn() == CALLSIGN_COL)
        {
            tempCallsign_ = model->getCallsign(item);
        }
        else if (col->GetModelColumn() == LAST_RX_CALLSIGN_COL)
        {
            tempCallsign_ = model->getRxCallsign(item);
        }
    }
    else
    {
        tempUserMessage_ = wxT("");
        tempCallsign_ = wxT("");
    }
}

void FreeDVReporterDialog::OnRightClickSpotsList(wxContextMenuEvent&)
{
    wxDataViewEvent contextEvent;
    OnItemRightClick(contextEvent);
}

void FreeDVReporterDialog::SkipMouseEvent(wxMouseEvent&)
{
    wxDataViewEvent contextEvent;
    OnItemRightClick(contextEvent);
}

void FreeDVReporterDialog::OnMouseWheelTooltip(wxMouseEvent& event)
{
    if (tipWindow_)
    {
        tipWindow_->Close();
    }

    event.Skip();
}

void FreeDVReporterDialog::OnLeftClickTooltip(wxMouseEvent& event)
{
    // Ensure that item is selected after tooltip closes
    CallAfter([this]() {
        const wxPoint pt = wxGetMousePosition();
        int mouseX = pt.x - m_listSpots->GetScreenPosition().x;
        int mouseY = pt.y - m_listSpots->GetScreenPosition().y;
    
        wxDataViewItem item;
        wxDataViewColumn* col;
        m_listSpots->HitTest(wxPoint(mouseX, mouseY), item, col);
        if (item.IsOk() && IsActive())
        {
            m_listSpots->Select(item);
        }
    });
    
    // Allow tip window to handle event
    event.Skip();
}

void FreeDVReporterDialog::OnSetFocus(wxFocusEvent& event)
{
    CallAfter([this]() {
        const wxPoint pt = wxGetMousePosition();
        int mouseX = pt.x - m_listSpots->GetScreenPosition().x;
        int mouseY = pt.y - m_listSpots->GetScreenPosition().y;

        wxDataViewItem item;
        wxDataViewColumn* col;
        m_listSpots->HitTest(wxPoint(mouseX, mouseY), item, col);
        if (item.IsOk())
        {
            m_listSpots->Select(item);
        }
    });
    
    event.Skip();
}

void FreeDVReporterDialog::OnColumnReordered(wxDataViewEvent&)
{
    // Preserve new column ordering
    // Note: Windows uses the same indices for model column and GetColumn()
    // so we need to use an alternate implementation for that platform.
#if defined(WIN32)
    CallAfter([this]() {
        std::vector<int> newColPositions;
        std::stringstream ss;
        auto headerCtrl = m_listSpots->GenericGetHeader();
        wxArrayInt wxColumnOrder = headerCtrl->GetColumnsOrder();
        for (unsigned int index = 0; index < wxColumnOrder.GetCount(); index++)
        {
            auto col = wxColumnOrder.Item(index);
            if (col < NUM_COLS)
            {
                newColPositions.push_back(col);
                ss << col << " ";
            }
        }

        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnOrder = newColPositions;
        log_info("New column ordering: %s", ss.str().c_str());
    });
#else
    // Walk every column actually present (not just the first NUM_COLS
    // positions) and keep only real model columns -- nothing pins the
    // trailing sentinel spacer column (model ID RIGHTMOST_COL, added purely
    // to soak up leftover header width) to the last visual position during a
    // drag reorder, so it can end up among the first NUM_COLS positions.
    // Assuming it never would silently dropped a real column off the end of
    // the saved order and persisted the sentinel's ID in its place instead --
    // the source of a "new empty column" appearing after reordering.
    std::stringstream ss;
    std::vector<int> newColPositions;
    for (unsigned int index = 0; index < m_listSpots->GetColumnCount(); index++)
    {
        auto dvc = m_listSpots->GetColumn(index);
        auto modelCol = dvc->GetModelColumn();
        if (modelCol < (unsigned int)NUM_COLS)
        {
            newColPositions.push_back(modelCol);
            ss << modelCol << " ";
        }
    }

    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnOrder = newColPositions;
    log_info("New column ordering: %s", ss.str().c_str());
#endif // defined(WIN32)
}

void FreeDVReporterDialog::saveColumnFilters_()
{
    FreeDVReporterDataModel* model = static_cast<FreeDVReporterDataModel*>(m_listSpots->GetModel());

    std::vector<int> ops(NUM_COLS, FreeDVReporterDialog::FILTER_NONE);
    std::vector<wxString> vals(NUM_COLS, "");

    for (int col = 0; col < NUM_COLS; col++)
    {
        int op;
        wxString val;
        if (model->getColumnFilter(col, op, val))
        {
            ops[col] = op;
            vals[col] = val;
        }
    }

    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnFilterOperators = ops;
    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnFilterValues = vals;
}

void FreeDVReporterDialog::updateColumnFilterIndicators_()
{
    FreeDVReporterDataModel* model = static_cast<FreeDVReporterDataModel*>(spotsDataModel_.get());
    if (!model) return;

    for (int col = 0; col < NUM_COLS; col++)
    {
        wxDataViewColumn* colObj = nullptr;
        for (unsigned int index = 0; index < m_listSpots->GetColumnCount(); index++)
        {
            auto c = m_listSpots->GetColumn(index);
            if (c && (int)c->GetModelColumn() == col)
            {
                colObj = c;
                break;
            }
        }
        if (!colObj) continue;

        int op;
        wxString val;
        bool hasFilter = model->getColumnFilter(col, op, val);

        wxString title = colObj->GetTitle();
        // Strip any existing filter indicator
        if (title.EndsWith(" *"))
        {
            title = title.Left(title.Length() - 2);
        }
        if (hasFilter)
        {
            title += " *";
        }
        colObj->SetTitle(title);
    }
}

void FreeDVReporterDialog::OnColumnHeaderRightClick(wxDataViewEvent& event)
{
    auto col = event.GetDataViewColumn();
    if (col == nullptr) { event.Skip(); return; }

    int modelCol = (int)col->GetModelColumn();
    if (modelCol >= NUM_COLS) { event.Skip(); return; }

    FreeDVReporterDataModel* model = static_cast<FreeDVReporterDataModel*>(m_listSpots->GetModel());

    int currentOp = FreeDVReporterDialog::FILTER_NONE;
    wxString currentVal;
    bool hasFilter = model->getColumnFilter(modelCol, currentOp, currentVal);

    wxMenu menu;

    static const struct {
        ColumnFilterOperator op;
        const char* label;
    } opItems[] = {
        {FILTER_GTE, wxTRANSLATE(">= (Greater or equal to)")},
        {FILTER_GT,  wxTRANSLATE(">  (Greater than)")},
        {FILTER_EQ,  wxTRANSLATE("=  (Equal to)")},
        {FILTER_NEQ, wxTRANSLATE("!= (Not equal to)")},
        {FILTER_LT,  wxTRANSLATE("<  (Less than)")},
        {FILTER_LTE, wxTRANSLATE("<= (Less or equal to)")},
    };

    for (auto& opItem : opItems)
    {
        auto menuItem = menu.Append(wxID_ANY, wxGetTranslation(opItem.label), wxEmptyString, wxITEM_CHECK);
        if (hasFilter && currentOp == opItem.op)
        {
            menuItem->Check(true);
        }
        ColumnFilterOperator op = opItem.op;
        menu.Bind(wxEVT_MENU, [this, modelCol, op, currentVal](wxCommandEvent&) {
            wxTextEntryDialog dlg(this, _("Enter value for column filter:"),
                _("Set Column Filter"), currentVal);
            if (dlg.ShowModal() == wxID_OK)
            {
                FreeDVReporterDataModel* m = static_cast<FreeDVReporterDataModel*>(m_listSpots->GetModel());
                m->setColumnFilter(modelCol, op, dlg.GetValue());
                saveColumnFilters_();
                updateColumnFilterIndicators_();
                updateFilterStatus_();
                m->refreshAllRows();
            }
        }, menuItem->GetId());
    }

    menu.AppendSeparator();
    auto clearItem = menu.Append(wxID_ANY, _("Clear Column Filter"));
    clearItem->Enable(hasFilter);
    menu.Bind(wxEVT_MENU, [this, modelCol](wxCommandEvent&) {
        FreeDVReporterDataModel* m = static_cast<FreeDVReporterDataModel*>(m_listSpots->GetModel());
        m->clearColumnFilter(modelCol);
        saveColumnFilters_();
        updateColumnFilterIndicators_();
        updateFilterStatus_();
        m->refreshAllRows();
    }, clearItem->GetId());

    ShowPopupMenu(m_listSpots, &menu);
    event.Skip();
}

void FreeDVReporterDialog::OnColumnClick(wxDataViewEvent& event)
{
    DeselectItem();
    event.Skip();

#if 0
    auto col = event.GetDataViewColumn();
    if (col != nullptr)
    {
        if (col->IsSortKey() && !col->IsSortOrderAscending())
        {
            col->UnsetAsSortKey();
        }
        else if (!col->IsSortKey())
        {
            col->SetSortOrder(true);
        }
        else
        {
            col->SetSortOrder(false);
        }
        event.StopPropagation();
    }
#endif // 0
}

void FreeDVReporterDialog::OnItemRightClick(wxDataViewEvent&)
{
    // Make sure item's deselected as it should only be selected on
    // left-click.
    DeselectItem();

    if (tipWindow_ != nullptr)
    {
        tipWindow_->Close();
        tipWindow_ = nullptr;
    }

    // Only show the popup if we're already hovering over a message or callsign.
    const wxPoint pt = wxGetMousePosition();
    int mouseX = pt.x - m_listSpots->GetScreenPosition().x;
    int mouseY = pt.y - m_listSpots->GetScreenPosition().y;
    
    if (tempUserMessage_ != wxT(""))
    {
        // 170 here has been determined via experimentation to avoid an issue 
        // on some KDE installations where the popup menu immediately closes after
        // right-clicking. This in effect causes the popup to open to the left of
        // the mouse pointer.
        ShowPopupMenu(m_listSpots, spotsPopupMenu_, wxPoint(mouseX - 170, mouseY));
    }
    else if (tempCallsign_ != wxT(""))
    {
        ShowPopupMenu(m_listSpots, callsignPopupMenu_, wxPoint(mouseX, mouseY));
    }
}

void FreeDVReporterDialog::OnCopyUserMessage(wxCommandEvent&)
{
    if (wxTheClipboard->Open())
    {
        // This data objects are held by the clipboard,
        // so do not delete them in the app.
        wxTheClipboard->SetData( new wxTextDataObject(tempUserMessage_) );
        wxTheClipboard->Close();
    }
    DeselectItem();
}

void FreeDVReporterDialog::OnQRZLookup(wxCommandEvent&)
{
    if (tempCallsign_ != wxT(""))
    {
        LaunchBrowser(wxString::Format("https://www.qrz.com/db/%s", tempCallsign_));
    }
}

void FreeDVReporterDialog::OnHamQTHLookup(wxCommandEvent&)
{
    if (tempCallsign_ != wxT(""))
    {
        LaunchBrowser(wxString::Format("https://www.hamqth.com/%s", tempCallsign_));
    }
}

void FreeDVReporterDialog::OnHamCallLookup(wxCommandEvent&)
{
    if (tempCallsign_ != wxT(""))
    {
        LaunchBrowser(wxString::Format("https://hamcall.net/call?callsign=%s", tempCallsign_));
    }
}

// The characters in a string, whole: on Windows, wxString holds UTF-16, in
// which those beyond U+FFFF (such as the ones flags are made of) each take
// two places.
static std::u32string ToCodePoints_(const wxString& str)
{
    const std::wstring& wide = str.ToStdWstring();
    std::u32string result;
    for (size_t i = 0; i < wide.size(); i++)
    {
        char32_t chr = (char32_t)wide[i];
        if (sizeof(wchar_t) == 2 && chr >= 0xD800 && chr <= 0xDBFF && i + 1 < wide.size() &&
            wide[i + 1] >= 0xDC00 && wide[i + 1] <= 0xDFFF)
        {
            chr = 0x10000 + ((chr - 0xD800) << 10) + ((char32_t)wide[i + 1] - 0xDC00);
            i++;
        }
        result += chr;
    }
    return result;
}

static wxString FromCodePoints_(const std::u32string& text)
{
    std::wstring wide;
    for (char32_t chr : text)
    {
        if (sizeof(wchar_t) == 2 && chr >= 0x10000)
        {
            chr -= 0x10000;
            wide += (wchar_t)(0xD800 + (chr >> 10));
            wide += (wchar_t)(0xDC00 + (chr & 0x3FF));
        }
        else
        {
            wide += (wchar_t)chr;
        }
    }
    return wxString(wide);
}

void FreeDVReporterDialog::OnStatusTextChange(wxCommandEvent&)
{
    auto statusMsg = m_statusMessage->GetValue();
    long insertPoint = m_statusMessage->GetInsertionPoint();

    log_debug("Status message currently %s", (const char*)statusMsg.ToUTF8());

    // Linux workaround: it's possible for some emoji flags to backspace
    // and not fully remove the flag. This logic scans the current value
    // of statusMsg for the appropriate Unicode sequence 
    // (U+1F3F4 + ... + U+E007F). If the beginning character is there
    // but not the end character, the intermediate tag characters are
    // removed along with the beginng.
    //
    // Note: it still takes a couple of backspaces before the delete process
    // begins so this is not a full workaround. Still, this significantly
    // reduces the number of times one must backspace in order to completely
    // remove the flag.
    std::u32string text = ToCodePoints_(statusMsg);
    bool changed = false;
    size_t index = 0;
    while (index < text.size())
    {
        char32_t chr = text[index];
        if (chr == 0x1F3F4)
        {
            log_debug("Found start char at index %d", (int)index);
            auto endIndex = index + 1;
            bool foundEnd = false;
            while (endIndex < text.size())
            {
                auto endChar = text[endIndex];
                if (endChar == 0xE007F)
                {
                    foundEnd = true;
                    break;
                }
                else if (!(endChar >= 0xE0061 && endChar <= 0xE007A))
                {
                    // Not lowercase tag characters; missing end character.
                    endIndex--;
                    break;
                }
                endIndex++;
            }
            log_debug("Found end at %d", (int)endIndex);

            if (!foundEnd && endIndex != index)
            {
                // Strip tag characters and beginning
                text.erase(index, endIndex + 1 - index);
                changed = true;
                // (Where the control counts positions in the same units as
                // wxString does.)
                insertPoint = FromCodePoints_(text.substr(0, index)).length();
                log_debug("status msg is now %s", (const char*)FromCodePoints_(text).ToUTF8());
                continue;
            }
            else
            {
                index = endIndex;
            }
        }
        else if (chr >= 0xE0000 && chr <= 0xE007F)
        {
            // Remove any stray tag characters that might still remain.
            text.erase(index, 1);
            changed = true;
            continue;
        }

        index++;
    }

    if (changed)
    {
        m_statusMessage->SetValue(FromCodePoints_(text));

        // Reset cursor position to end of new character
        m_statusMessage->SetInsertionPoint(insertPoint);
    }
}

void FreeDVReporterDialog::OnStatusTextSend(wxCommandEvent&)
{
    auto statusMsg = m_statusMessage->GetValue(); 

    FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();
    model->updateMessage(statusMsg);

    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterStatusText = statusMsg;

    // If already on the list, re-insert so wxLB_SORT keeps it in place.
    auto popup = getMsgPopup();
    auto location = popup->FindString(statusMsg, true);
    if (location >= 0)
    {
        popup->Delete(location);
        popup->Append(statusMsg);
        popup->SetSelection(popup->FindString(statusMsg, true));

        // Preserve current state of the MRU list.
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts->clear();
        for (unsigned int index = 0; index < popup->GetCount(); index++)
        {
            wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts->push_back(popup->GetString(index));
        }
    }

    DeselectItem();
}

void FreeDVReporterDialog::OnStatusTextSendContextMenu(wxContextMenuEvent&)
{
    DeselectItem();
    auto popup = getMsgPopup();
    auto statusMsg = m_statusMessage->GetValue();
    bool canSave = !statusMsg.IsEmpty()
                   && popup->FindString(statusMsg, true) == wxNOT_FOUND
                   && (int)popup->GetCount() < STATUS_MESSAGE_MRU_MAX_SIZE;
    setPopupMenu_->Enable(setSaveMenuItem_->GetId(), canSave);
    setPopupMenu_->Enable(saveMenuItem_->GetId(), canSave);
    ShowPopupMenu(m_buttonSend, setPopupMenu_);
}

void FreeDVReporterDialog::OnStatusTextSendAndSaveMessage(wxCommandEvent& event)
{
    OnStatusTextSend(event);
    OnStatusTextSaveMessage(event);
}

void FreeDVReporterDialog::OnStatusTextSaveMessage(wxCommandEvent&)
{
    auto statusMsg = m_statusMessage->GetValue();
    if (statusMsg.size() > 0)
    {
        auto popup = getMsgPopup();
        auto location = popup->FindString(statusMsg, true);
        if (location >= 0)
        {
            // Don't save if already in the list.
            return;
        }
        popup->Append(statusMsg);
        popup->SetSelection(popup->FindString(statusMsg, true));

        // Preserve current state of the MRU list.
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts->clear();
        for (unsigned int index = 0; index < popup->GetCount(); index++)
        {
            wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts->push_back(popup->GetString(index));
        }
    }
}

void FreeDVReporterDialog::OnStatusTextClear(wxCommandEvent&)
{
    FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();
    model->updateMessage("");

    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterStatusText = "";
    m_statusMessage->SetValue("");

    DeselectItem();
}

void FreeDVReporterDialog::OnStatusTextClearContextMenu(wxContextMenuEvent&)
{
    ShowPopupMenu(m_buttonClear, clearPopupMenu_);
}

void FreeDVReporterDialog::OnStatusTextClearSelected(wxCommandEvent&)
{
    auto statusMsg = m_statusMessage->GetValue();
    if (statusMsg.size() > 0)
    {
        auto popup = getMsgPopup();
        auto location = popup->FindString(statusMsg, true);
        if (location >= 0)
        {
            popup->Delete(location);
        }

        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts->clear();
        for (unsigned int index = 0; index < popup->GetCount(); index++)
        {
            wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts->push_back(popup->GetString(index));
        }
    }
}

void FreeDVReporterDialog::OnStatusTextClearAll(wxCommandEvent&)
{
    getMsgPopup()->Clear();
    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts->clear();
}

wxListBox* FreeDVReporterDialog::getMsgPopup() const
{
    auto ctrl = m_statusMessage->GetPopupControl();
    return ctrl ? static_cast<wxListBox*>(ctrl->GetControl()) : nullptr;
}

void FreeDVReporterDialog::ShowMsgItemContextMenu(int itemIdx)
{
    contextMenuSelectedIndex_ = itemIdx;
    msgItemPopupMenu_->Enable(addMsgMenuItem_->GetId(),
        getMsgPopup()->GetCount() < STATUS_MESSAGE_MRU_MAX_SIZE);
    ShowPopupMenu(m_statusMessage, msgItemPopupMenu_);
}

void FreeDVReporterDialog::OnStatusMessageContextMenuEdit(wxCommandEvent&)
{
    auto popup = getMsgPopup();
    if (contextMenuSelectedIndex_ == wxNOT_FOUND ||
        contextMenuSelectedIndex_ >= (int)popup->GetCount())
    {
        return;
    }

    auto oldText = popup->GetString(contextMenuSelectedIndex_);
    wxTextEntryDialog dlg(this, _("Edit the saved message:"), _("Edit Message"), oldText);
    long savedW = wxGetApp().appConfiguration.msgEditDialogWidth;
    if (savedW > 0)
        dlg.SetSize(wxSize(savedW, dlg.GetSize().GetHeight()));
    if (dlg.ShowModal() != wxID_OK)
        return;
    wxGetApp().appConfiguration.msgEditDialogWidth = dlg.GetSize().GetWidth();

    auto newText = dlg.GetValue().Trim();
    if (newText.IsEmpty() || newText == oldText)
        return;

    // If the edited item is currently shown in the text field, update it.
    if (m_statusMessage->GetValue() == oldText)
        m_statusMessage->SetValue(newText);

    popup->Delete(contextMenuSelectedIndex_);
    popup->Append(newText);
    contextMenuSelectedIndex_ = wxNOT_FOUND;

    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts->clear();
    for (unsigned int index = 0; index < popup->GetCount(); index++)
    {
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts->push_back(popup->GetString(index));
    }
    wxGetApp().appConfiguration.save(pConfig);
}

void FreeDVReporterDialog::OnStatusMessageContextMenuDelete(wxCommandEvent&)
{
    auto popup = getMsgPopup();
    if (contextMenuSelectedIndex_ == wxNOT_FOUND ||
        contextMenuSelectedIndex_ >= (int)popup->GetCount())
    {
        return;
    }

    popup->Delete(contextMenuSelectedIndex_);
    contextMenuSelectedIndex_ = wxNOT_FOUND;

    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts->clear();
    for (unsigned int index = 0; index < popup->GetCount(); index++)
    {
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts->push_back(popup->GetString(index));
    }
    wxGetApp().appConfiguration.save(pConfig);
}

void FreeDVReporterDialog::OnStatusMessageContextMenuAdd(wxCommandEvent&)
{
    wxTextEntryDialog dlg(this, _("Enter new message:"), _("Add Message"), wxEmptyString);
    long savedW = wxGetApp().appConfiguration.msgEditDialogWidth;
    if (savedW > 0)
        dlg.SetSize(wxSize(savedW, dlg.GetSize().GetHeight()));
    if (dlg.ShowModal() != wxID_OK)
        return;
    wxGetApp().appConfiguration.msgEditDialogWidth = dlg.GetSize().GetWidth();

    auto newText = dlg.GetValue().Trim();
    if (newText.IsEmpty())
        return;

    auto popup = getMsgPopup();
    if (popup->FindString(newText, true) != wxNOT_FOUND)
        return; // already in the list

    popup->Append(newText);

    wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts->clear();
    for (unsigned int index = 0; index < popup->GetCount(); index++)
    {
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterRecentStatusTexts->push_back(popup->GetString(index));
    }
    wxGetApp().appConfiguration.save(pConfig);
}

void FreeDVReporterDialog::refreshQSYButtonState()
{
    FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();

    // Update filter if the frequency's changed.
    if (wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksFrequency)
    {
        FilterFrequency freq = 
            getFilterForFrequency_(wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency);
        
        if (model->getCurrentBandFilter() != freq || wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksExactFreq)
        {
            setBandFilter(freq);
        }
    }
    
    bool enabled = false;
    auto selected = m_listSpots->GetSelection();
    if (selected.IsOk())
    {
        auto selectedCallsign = model->getCallsign(selected);
    
        if (model->isValidForReporting() &&
            selectedCallsign != wxGetApp().appConfiguration.reportingConfiguration.reportingCallsign && 
            wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency > 0 &&
            freedvInterface.isRunning())
        {
            uint64_t theirFreq = model->getFrequency(selected);
            enabled = theirFreq != wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency;
        }
    }
    
    m_buttonSendQSY->Enable(enabled);
}

void FreeDVReporterDialog::setBandFilter(FilterFrequency freq)
{
    FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();
    model->setBandFilter(freq);

    // Update filter status in window
    updateFilterStatus_();
}

#if defined(WIN32)
void FreeDVReporterDialog::autosizeColumns()
{
    for (unsigned int index = 0; index < m_listSpots->GetColumnCount(); index++)
    {
        if (index != USER_MESSAGE_COL)
        {
            // USER_MESSAGE_COL width is preserved and should not be messed with.
            auto col = getColumnForModelColId_(index);
            col->SetWidth(wxCOL_WIDTH_DEFAULT);
            col->SetWidth(wxCOL_WIDTH_AUTOSIZE);
        }
    }

    // Then pin each column at the width it was just fitted to. After any item change,
    // wxDataViewCtrl re-measures every row of wxCOL_WIDTH_AUTOSIZE columns at idle
    // time, which with the Reporter's frequent updates cost as much as refitting here
    // did; fixed-width columns skip that. FreeDVReporterDataModel calls this again
    // when the widest text in a column changes.
    for (unsigned int index = 0; index < m_listSpots->GetColumnCount(); index++)
    {
        if (index != USER_MESSAGE_COL)
        {
            auto col = getColumnForModelColId_(index);
            col->SetWidth(col->GetWidth());
        }
    }
}
#endif // defined(WIN32)

void FreeDVReporterDialog::FreeDVReporterDataModel::setBandFilter(FilterFrequency freq)
{
    filteredFrequency_ = wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency;
    currentBandFilter_ = freq;

    refreshAllRows();
}

wxString FreeDVReporterDialog::FreeDVReporterDataModel::formatSnr_(float snr) const
{
    // SNRs are well under 1000, so there's no thousands separator to worry about.
    wxString result = wxString::Format(wxT("%.1f"), snr);
    if (decimalSeparator_ != wxT('.'))
    {
        result.Replace(wxT("."), wxString(decimalSeparator_));
    }
    return result;
}

wxString FreeDVReporterDialog::FreeDVReporterDataModel::makeValidTime_(std::string const& timeStr, wxDateTime& timeObj)
{
    wxRegEx& millisecondsRemoval = millisecondsRgx_;
    wxString tmp = timeStr;

    // ParseISOCombined() can't take the fraction of a second (the server
    // sends microseconds, and none at all when there are exactly zero), so
    // it's taken off here and its milliseconds added back once parsed.
    // Otherwise, e.g. stations that connected within the same second
    // can't be told apart when sorting.
    int milliseconds = 0;
    if (millisecondsRemoval.Matches(tmp))
    {
        wxString fraction = millisecondsRemoval.GetMatch(tmp); // e.g. ".123456"
        int digits = 0;
        for (size_t i = 1; i < fraction.length() && digits < 3 && wxIsdigit(fraction[i]); i++, digits++)
        {
            milliseconds = milliseconds * 10 + (int)(fraction[i].GetValue() - '0');
        }
        for (; digits < 3; digits++)
        {
            milliseconds *= 10;
        }
    }
    millisecondsRemoval.Replace(&tmp, parent_->EMPTY_STR);
    
    wxRegEx& timezoneRgx = timezoneRgx_;
    wxDateTime::TimeZone timeZone(0); // assume UTC by default
    if (timezoneRgx.Matches(tmp))
    {
        auto tzOffset = timezoneRgx.GetMatch(tmp, 1);
        auto hours = timezoneRgx.GetMatch(tmp, 2);
        auto minutes = timezoneRgx.GetMatch(tmp, 3);
        
        int tzMinutes = wxAtoi(hours) * 60;
        tzMinutes += wxAtoi(minutes);
        
        if (tzOffset == parent_->TZ_OFFSET_STR)
        {
            tzMinutes = -tzMinutes;
        }
        
        timezoneRgx.Replace(&tmp, parent_->EMPTY_STR);
        
        timeZone = wxDateTime::TimeZone(tzMinutes);
    }
    
    wxDateTime tmpDate(wxInvalidDateTime);
    if (tmpDate.ParseISOCombined(tmp))
    {
        tmpDate.MakeFromTimezone(timeZone);
        tmpDate += wxTimeSpan::Milliseconds(milliseconds);
        timeObj = tmpDate;
        if (wxGetApp().appConfiguration.reportingConfiguration.useUTCForReporting)
        {
            timeZone = wxDateTime::TimeZone(wxDateTime::TZ::UTC);
        }
        else
        {
            timeZone = wxDateTime::TimeZone(wxDateTime::TZ::Local);
        }

#if __APPLE__
        // Workaround for weird macOS bug preventing .Format from working properly when double-clicking
        // on the .app in Finder. Running the app from Terminal seems to work fine with .Format for 
        // some reason. O_o
        struct tm tmpTm;
        auto tmpDateTm = tmpDate.GetTm(timeZone);
        
        tmpTm.tm_sec = tmpDateTm.sec;
        tmpTm.tm_min = tmpDateTm.min;
        tmpTm.tm_hour = tmpDateTm.hour;
        tmpTm.tm_mday = tmpDateTm.mday;
        tmpTm.tm_mon = tmpDateTm.mon;
        tmpTm.tm_year = tmpDateTm.year - 1900;
        tmpTm.tm_wday = tmpDateTm.GetWeekDay();
        tmpTm.tm_yday = tmpDateTm.yday;
        tmpTm.tm_isdst = -1;
        
        char buf[4096];
#if wxCHECK_VERSION(3,2,0)
        wxString sysLocale = wxUILocale::GetCurrent().GetName();
        auto sysLocaleT = newlocale(LC_TIME_MASK, (const char*)sysLocale.ToUTF8(), NULL);
        if (sysLocaleT != nullptr)
        {
            strftime_l(buf, sizeof(buf), (const char*)parent_->TIME_FORMAT_STR.ToUTF8(), &tmpTm, sysLocaleT);
            freelocale(sysLocaleT);
        }
        else
#endif // wxCHECK_VERSION(3,2,0)
        {
            strftime(buf, sizeof(buf), (const char*)parent_->TIME_FORMAT_STR.ToUTF8(), &tmpTm);
        }
        return buf;
#else
        return tmpDate.Format(parent_->TIME_FORMAT_STR, timeZone);
#endif // __APPLE__
    }
    else
    {
        timeObj = wxDateTime(wxInvalidDateTime);
        return parent_->UNKNOWN_STR;
    }
}

void FreeDVReporterDialog::FreeDVReporterDataModel::execQueuedAction_()
{
    // This ensures that we handle server events in the order they're received.
    std::unique_lock<std::mutex> lk(fnQueueMtx_, std::defer_lock_t());
    lk.lock();
    auto size = fnQueue_.size();
    lk.unlock();

    while(size > 0)
    {
        lk.lock();
        auto handler = std::move(fnQueue_[0]);
        lk.unlock();

        handler.fn(handler);

        lk.lock();
        fnQueue_.pop_front();
        size = fnQueue_.size();
        lk.unlock();
    }
}

FreeDVReporterDialog::FilterFrequency FreeDVReporterDialog::getFilterForFrequency_(uint64_t freq)
{
    return GetFilterForFrequency(freq);
}

void FreeDVReporterDialog::FreeDVReporterDataModel::setColumnFilter(int col, int op, const wxString& val)
{
    if (col >= 0 && col < NUM_COLS)
    {
        columnFilterOperators_[col] = op;
        columnFilterValues_[col] = val;
    }
}

void FreeDVReporterDialog::FreeDVReporterDataModel::clearColumnFilter(int col)
{
    if (col >= 0 && col < NUM_COLS)
    {
        columnFilterOperators_[col] = FreeDVReporterDialog::FILTER_NONE;
        columnFilterValues_[col] = "";
    }
}

bool FreeDVReporterDialog::FreeDVReporterDataModel::getColumnFilter(int col, int& op, wxString& val) const
{
    if (col >= 0 && col < NUM_COLS && columnFilterOperators_[col] != FreeDVReporterDialog::FILTER_NONE)
    {
        op = columnFilterOperators_[col];
        val = columnFilterValues_[col];
        return true;
    }
    return false;
}

bool FreeDVReporterDialog::FreeDVReporterDataModel::isNumericColumn_(int col)
{
    return col == DISTANCE_COL || col == HEADING_COL || col == FREQUENCY_COL || col == SNR_COL;
}

wxString FreeDVReporterDialog::FreeDVReporterDataModel::getColumnDisplayValue_(ReporterData* data, int col) const
{
    switch (col)
    {
        case CALLSIGN_COL:       return data->callsign;
        case GRID_SQUARE_COL:    return data->gridSquare;
        case DISTANCE_COL:       return data->distance;
        case HEADING_COL:        return data->heading;
        case VERSION_COL:        return data->version;
        case FREQUENCY_COL:      return data->freqString;
        case TX_MODE_COL:        return data->txMode;
        case STATUS_COL:         return data->status;
        case USER_MESSAGE_COL:   return data->userMessage;
        case LAST_TX_DATE_COL:   return data->lastTx;
        case LAST_RX_CALLSIGN_COL: return data->lastRxCallsign;
        case SNR_COL:            return data->snr;
        case LAST_UPDATE_DATE_COL: return data->lastUpdate;
        default:                 return "";
    }
}

bool FreeDVReporterDialog::FreeDVReporterDataModel::isFiltered_(ReporterData* data)
{
    bool isHidden = false;

    if (currentBandFilter_ != FilterFrequency::BAND_ALL)
    {
        auto freq = data->frequency;
        auto bandForFreq = parent_->getFilterForFrequency_(freq);

        isHidden =  
            (bandForFreq != currentBandFilter_) ||
            (wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksFrequency &&
                wxGetApp().appConfiguration.reportingConfiguration.freedvReporterBandFilterTracksExactFreq &&
                freq != filteredFrequency_);
    }

    // If enabled, hide rows that exceed the max idle time.
    if (!isHidden && wxGetApp().appConfiguration.reportingConfiguration.freedvReporterEnableMaxIdleFilter)
    {
        // "Idle" is defined as someone who either:
        // * Connected more than X minutes ago and has yet to transmit, or
        // * Last transmitted more than X minutes ago.
        int maxIdleTimeMins = wxGetApp().appConfiguration.reportingConfiguration.freedvReporterMaxIdleMinutes;
        wxDateTime maxIdleDate;
        wxDateTime origDate;
        if (data->lastTxDate.IsValid())
        {
            origDate = data->lastTxDate;
        }
        else
        {
            origDate = data->connectTime;
        }

        maxIdleDate = origDate.Add(wxTimeSpan(maxIdleTimeMins / 60, maxIdleTimeMins % 60));
        isHidden = maxIdleDate.IsEarlierThan(wxDateTime::Now());
    }

    // Apply per-column filters
    for (int col = 0; col < NUM_COLS && !isHidden; col++)
    {
        int op = columnFilterOperators_[col];
        if (op == FreeDVReporterDialog::FILTER_NONE) continue;

        wxString filterVal = columnFilterValues_[col];

        int cmpResult = 0;
        if (isNumericColumn_(col))
        {
            double filterNum = 0.0;
            if (!wxNumberFormatter::FromString(filterVal, &filterNum))
            {
                // Can't parse filter value as a number; skip this filter
                continue;
            }

            double rowNum = 0.0;
            switch (col)
            {
                case DISTANCE_COL:
                    rowNum = data->distanceVal;
                    break;
                case HEADING_COL:
                    rowNum = data->headingVal;
                    break;
                case SNR_COL:
                    rowNum = data->snrVal;
                    break;
                case FREQUENCY_COL:
                    // Filter value is entered in the same display units as the column (kHz or MHz)
                    if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
                    {
                        rowNum = data->frequency / 1000.0;
                    }
                    else
                    {
                        rowNum = data->frequency / 1000000.0;
                    }
                    break;
                default:
                    break;
            }

            if (rowNum < filterNum) cmpResult = -1;
            else if (rowNum > filterNum) cmpResult = 1;
            else cmpResult = 0;
        }
        else
        {
            wxString rowVal = getColumnDisplayValue_(data, col);
            cmpResult = rowVal.CmpNoCase(filterVal);
        }

        bool passesFilter = false;
        switch (op)
        {
            case FreeDVReporterDialog::FILTER_GTE: passesFilter = (cmpResult >= 0); break;
            case FreeDVReporterDialog::FILTER_GT:  passesFilter = (cmpResult > 0);  break;
            case FreeDVReporterDialog::FILTER_EQ:  passesFilter = (cmpResult == 0); break;
            case FreeDVReporterDialog::FILTER_NEQ: passesFilter = (cmpResult != 0); break;
            case FreeDVReporterDialog::FILTER_LT:  passesFilter = (cmpResult < 0);  break;
            case FreeDVReporterDialog::FILTER_LTE: passesFilter = (cmpResult <= 0); break;
            default: passesFilter = true; break;
        }

        isHidden |= !passesFilter;
    }

    return isHidden;
}

FreeDVReporterDialog::FreeDVReporterDataModel::FreeDVReporterDataModel(FreeDVReporterDialog* parent)
    : sortOnNextTimerInterval(false)
    , isConnected_(false)
    , parent_(parent)
    , currentBandFilter_(BAND_ALL)
    , filterSelfMessageUpdates_(false)
    , filteredFrequency_(0)
#if defined(__APPLE__)
    , columnsAutosized_(true)
#endif // defined(__APPLE__)
    , columnsNeedAutosize_(false)
    , columnsNeedWidthCheck_(false)
    , millisecondsRgx_(parent->MS_REMOVAL_RGX)
    , timezoneRgx_(parent->TIMEZONE_RGX)
    , decimalSeparator_(wxNumberFormatter::GetDecimalSeparator())
{
    // Initialize column filter state from config
    columnFilterOperators_.resize(NUM_COLS, FreeDVReporterDialog::FILTER_NONE);
    columnFilterValues_.resize(NUM_COLS, "");

    auto savedOps = wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnFilterOperators.get();
    auto savedVals = wxGetApp().appConfiguration.reportingConfiguration.freedvReporterColumnFilterValues.get();

    for (int i = 0; i < NUM_COLS && i < (int)savedOps.size(); i++)
    {
        columnFilterOperators_[i] = savedOps[i];
    }
    for (int i = 0; i < NUM_COLS && i < (int)savedVals.size(); i++)
    {
        columnFilterValues_[i] = savedVals[i];
    }
}

FreeDVReporterDialog::FreeDVReporterDataModel::~FreeDVReporterDataModel()
{
    setReporter(nullptr);
    
    for (auto& kvp : allReporterData_)
    {
        delete kvp.second;
    }
    allReporterData_.clear();
}

void FreeDVReporterDialog::FreeDVReporterDataModel::setReporter(std::shared_ptr<FreeDVReporter> const& reporter)
{
    if (reporter_)
    {
        reporter_->setOnReporterConnectFn(FreeDVReporter::ReporterConnectionFn());
        reporter_->setOnReporterDisconnectFn(FreeDVReporter::ReporterConnectionFn());
        
        reporter_->setOnUserConnectFn(FreeDVReporter::ConnectionDataFn());
        reporter_->setOnUserDisconnectFn(FreeDVReporter::ConnectionDataFn());
        reporter_->setOnFrequencyChangeFn(FreeDVReporter::FrequencyChangeFn());
        reporter_->setOnTransmitUpdateFn(FreeDVReporter::TxUpdateFn());
        reporter_->setOnReceiveUpdateFn(FreeDVReporter::RxUpdateFn());

        reporter_->setMessageUpdateFn(FreeDVReporter::MessageUpdateFn());
        reporter_->setConnectionSuccessfulFn(FreeDVReporter::ConnectionSuccessfulFn());
        reporter_->setAboutToShowSelfFn(FreeDVReporter::AboutToShowSelfFn());
        
        reporter_->setRecvEndFn(FreeDVReporter::RecvEndFn());
    }
    
    auto isChanged = reporter_ != reporter;
    reporter_ = reporter;
    
    if (isChanged)
    {
        if (reporter_)
        {
            reporter_->setOnReporterConnectFn(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::onReporterConnect_, this));
            reporter_->setOnReporterDisconnectFn(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::onReporterDisconnect_, this));
    
            reporter_->setOnUserConnectFn(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::onUserConnectFn_, this, _1, _2, _3, _4, _5, _6, _7));
            reporter_->setOnUserDisconnectFn(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::onUserDisconnectFn_, this, _1, _2, _3, _4, _5, _6, _7));
            reporter_->setOnFrequencyChangeFn(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::onFrequencyChangeFn_, this, _1, _2, _3, _4, _5));
            reporter_->setOnTransmitUpdateFn(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::onTransmitUpdateFn_, this, _1, _2, _3, _4, _5, _6, _7));
            reporter_->setOnReceiveUpdateFn(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::onReceiveUpdateFn_, this, _1, _2, _3, _4, _5, _6, _7));

            reporter_->setMessageUpdateFn(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::onMessageUpdateFn_, this, _1, _2, _3));
            reporter_->setConnectionSuccessfulFn(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::onConnectionSuccessfulFn_, this));
            reporter_->setAboutToShowSelfFn(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::onAboutToShowSelfFn_, this));
            reporter_->setRecvEndFn(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::onRecvEndFn_, this));
        }
        else
        {
            // Spot list no longer valid, delete the items currently on there.
            // In case there's anything else on the queue prior to this, let those
            // actions fully execute before clearing entries.
            log_debug("Reporter object set to null");
            std::unique_lock<std::mutex> lk(fnQueueMtx_);
            CallbackHandler handler;
            handler.fn = [this](CallbackHandler&) {
                clearAllEntries_();
            };

            fnQueue_.push_back(std::move(handler));
            parent_->CallAfter(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::execQueuedAction_, this));
        }
    }
}

void FreeDVReporterDialog::FreeDVReporterDataModel::clearAllEntries_()
{
    std::unique_lock<std::recursive_mutex> lk(dataMtx_);
    assert(wxThread::IsMain());

    columnsNeedWidthCheck_ = true;

    for (auto& row : allReporterData_)
    {
        if (row.second->isVisible)
        {
#if !defined(__linux__)
            // For non-Linux/GTK, isVisible must be set to false prior to removal
            // to avoid referencing deallocated memory during table updates (i.e.
            // by macOS when adjusting column widths).
            row.second->isVisible = false;
#endif // !defined(__linux__)
                
            wxDataViewItem dvi(row.second);
            ItemDeleted(wxDataViewItem(nullptr), dvi);
                
#if defined(__linux__)
            // For Linux/GTK, isVisible must still remain true during
            // actual removal from the GUI or else assertion failures
            // are thrown. We can set it to false once the item's removed.
            row.second->isVisible = false;
#endif // defined(__linux__)
        }

        row.second->isPendingDelete = true;
        row.second->deleteTime = wxDateTime::Now();
    }
}

bool FreeDVReporterDialog::FreeDVReporterDataModel::HasDefaultCompare() const
{
    // Will compare by connect time if nothing is selected for sorting
    return true;
}

int FreeDVReporterDialog::FreeDVReporterDataModel::Compare (const wxDataViewItem &item1, const wxDataViewItem &item2, unsigned int column, bool ascending) const
{
    std::unique_lock<std::recursive_mutex> lk(const_cast<std::recursive_mutex&>(dataMtx_));
    assert(wxThread::IsMain());

    if (!item1.IsOk() || !item2.IsOk())
    {
        int result = 0;
        if (!item1.IsOk())
        {
            result = 1;
        }
        else
        {
            result = -1;
        }

        result *= ascending ? 1 : -1;
        return result;

    }
    auto leftData = (ReporterData*)item1.GetID();
    auto rightData = (ReporterData*)item2.GetID();

    // Numbers are compared rather than subtracted: the difference between
    // two of them needn't fit in (or survive being rounded to) an int.
    // Frequencies more than 2^31 Hz apart came out in the wrong order.
    auto compareNumbers = [](auto left, auto right) {
        return (left < right) ? -1 : ((left > right) ? 1 : 0);
    };

    int result = 0;
    switch(column)
    {
        case CALLSIGN_COL:
            result = leftData->callsign.CmpNoCase(rightData->callsign);
            break;
        case GRID_SQUARE_COL:
            result = leftData->gridSquare.CmpNoCase(rightData->gridSquare);
            break;
        case DISTANCE_COL:
            result = compareNumbers(leftData->distanceVal, rightData->distanceVal);
            break;
        case HEADING_COL:
            result = compareNumbers(leftData->headingVal, rightData->headingVal);
            break;
        case VERSION_COL:
            result = leftData->version.CmpNoCase(rightData->version);
            break;
        case FREQUENCY_COL:
            result = compareNumbers(leftData->frequency, rightData->frequency);
            break;
        case TX_MODE_COL:
            result = leftData->txMode.CmpNoCase(rightData->txMode);
            break;
        case STATUS_COL:
            result = leftData->status.CmpNoCase(rightData->status);
            break;
        case USER_MESSAGE_COL:
        {
            // Blank entries should drop to the bottom when sorting in ascending order.
            wxString leftCopy(leftData->userMessage);
            wxString rightCopy(rightData->userMessage);
            leftCopy.Trim(false);
            leftCopy.Trim(true);
            rightCopy.Trim(false);
            rightCopy.Trim(true);

            if (leftCopy == "")
            {
                result = 1;
            }
            else if (rightCopy == "")
            {
                result = -1;
            }
            else
            {
                result = leftCopy.CmpNoCase(rightCopy);
            }
            break;
        }
        case LAST_TX_DATE_COL:
            if (leftData->lastTxDate.IsValid() && rightData->lastTxDate.IsValid())
            {
                if (leftData->lastTxDate.IsEarlierThan(rightData->lastTxDate))
                {
                    result = -1;
                }
                else if (leftData->lastTxDate.IsLaterThan(rightData->lastTxDate))
                {
                    result = 1;
                }
                else
                {
                    result = 0;
                }
            }
            else if (!leftData->lastTxDate.IsValid() && rightData->lastTxDate.IsValid())
            {
                result = -1;
            }
            else if (leftData->lastTxDate.IsValid() && !rightData->lastTxDate.IsValid())
            {
                result = 1;
            }
            else
            {
                result = 0;
            }
            break;
        case LAST_RX_CALLSIGN_COL:
            result = leftData->lastRxCallsign.CmpNoCase(rightData->lastRxCallsign);
            break;
        case SNR_COL:
            result = compareNumbers(leftData->snrVal, rightData->snrVal);
            break;
        case LAST_UPDATE_DATE_COL:
            if (leftData->lastUpdateDate.IsValid() && rightData->lastUpdateDate.IsValid())
            {
                if (leftData->lastUpdateDate.IsEarlierThan(rightData->lastUpdateDate))
                {
                    result = -1;
                }
                else if (leftData->lastUpdateDate.IsLaterThan(rightData->lastUpdateDate))
                {
                    result = 1;
                }
                else
                {
                    result = 0;
                }
            }
            else if (!leftData->lastUpdateDate.IsValid() && rightData->lastUpdateDate.IsValid())
            {
                result = -1;
            }
            else if (leftData->lastUpdateDate.IsValid() && !rightData->lastUpdateDate.IsValid())
            {
                result = 1;
            }
            else
            {
                result = 0;
            }
            break;
        case (unsigned)-1:
            if (leftData->connectTime.IsEarlierThan(rightData->connectTime))
            {
                result = -1;
            }
            else if (leftData->connectTime.IsLaterThan(rightData->connectTime))
            {
                result = 1;
            }
            else
            {
                result = 0;
            }
            break;
        default:
            assert(false);
            break;
    }

    if (result == 0 && column != (unsigned)-1)
    {
        // Also compare by connect time to ensure that we have a sort that
        // appears stable.
        if (leftData->connectTime.IsEarlierThan(rightData->connectTime))
        {
            result = -1;
        }
        else if (leftData->connectTime.IsLaterThan(rightData->connectTime))
        {
            result = 1;
        }
        else
        {
            result = 0;
        }
    }

    if (result == 0)
    {
        // As a last-ditch effort to prevent returning 0, use the pointer
        // values. Recommended by wxWidgets documentation.
        wxUIntPtr id1 = wxPtrToUInt(item1.GetID());
        wxUIntPtr id2 = wxPtrToUInt(item2.GetID());
        result = (ascending == (id1 > id2)) ? 1 : -1;
    }

    if (!ascending)
    {
        result = -result;
    }
    
    return result;
}

bool FreeDVReporterDialog::FreeDVReporterDataModel::GetAttr (const wxDataViewItem &item, unsigned int col, wxDataViewItemAttr &attr) const
{
    std::unique_lock<std::recursive_mutex> lk(const_cast<std::recursive_mutex&>(dataMtx_));
    bool result = false;
    assert(wxThread::IsMain());
    
    if (item.IsOk())
    {
        auto row = (ReporterData*)item.GetID();
        
        if (col < RIGHTMOST_COL)
        {
            if (row->backgroundColor.IsOk())
            {
                attr.SetBackgroundColour(row->backgroundColor);
                result = true;
            }
            if (row->foregroundColor.IsOk())
            {
                attr.SetColour(row->foregroundColor);
                result = true;
            }
        }
    }

    return result;
}

unsigned int FreeDVReporterDialog::FreeDVReporterDataModel::GetChildren (const wxDataViewItem &item, wxDataViewItemArray &children) const
{
    assert(wxThread::IsMain());
    if (item.IsOk())
    {
        // No children.
        return 0;
    }
    else
    {
        std::unique_lock<std::recursive_mutex> lk(const_cast<std::recursive_mutex&>(dataMtx_));
        int count = 0;
        for (auto& row : allReporterData_)
        {
            if (row.second->isVisible && !row.second->isPendingDelete)
            {
                count++;
                
                wxDataViewItem newItem(row.second);
                children.Add(newItem);
            }
        }

        return count;
    }
}

wxDataViewItem FreeDVReporterDialog::FreeDVReporterDataModel::GetParent (const wxDataViewItem &) const
{
    // Return root item
    return wxDataViewItem(nullptr);
}

#if !wxCHECK_VERSION(3,2,0)
unsigned int FreeDVReporterDialog::FreeDVReporterDataModel::GetColumnCount () const
{
    return RIGHTMOST_COL + 1;
}

wxString FreeDVReporterDialog::FreeDVReporterDataModel::GetColumnType (unsigned int) const
{
    wxVariant tmp("");
    return tmp.GetType();   
}
#endif // !wxCHECK_VERSION(3,2,0)


void FreeDVReporterDialog::FreeDVReporterDataModel::GetValue (wxVariant &variant, const wxDataViewItem &item, unsigned int col) const
{
    std::unique_lock<std::recursive_mutex> lk(const_cast<std::recursive_mutex&>(dataMtx_));
    assert(wxThread::IsMain());
    if (item.IsOk())
    {
        auto row = (ReporterData*)item.GetID();
        switch (col)
        {
            case CALLSIGN_COL:
                variant = wxVariant(row->callsign);
                break;
            case GRID_SQUARE_COL:
                variant = wxVariant(row->gridSquare);
                break;
            case DISTANCE_COL:
                variant = wxVariant(row->distance);
                break;
            case HEADING_COL:
                variant = wxVariant(row->heading);
                break;
            case VERSION_COL:
                variant = wxVariant(row->version);
                break;
            case FREQUENCY_COL:
                variant = wxVariant(row->freqString);
                break;
            case STATUS_COL:
                variant = wxVariant(row->status);
                break;
            case USER_MESSAGE_COL:
                variant = wxVariant(row->userMessage);
                break;
            case LAST_TX_DATE_COL:
                variant = wxVariant(row->lastTx);
                break;
            case LAST_RX_CALLSIGN_COL:
                variant = wxVariant(row->lastRxCallsign);
                break;
            case SNR_COL:
                variant = wxVariant(row->snr);
                break;
            case LAST_UPDATE_DATE_COL:
                variant = wxVariant(row->lastUpdate);
                break;
            case TX_MODE_COL:
                variant = wxVariant(row->txMode);
                break;
            case RIGHTMOST_COL:
                variant = wxVariant("");
                break;
            default:
                variant = wxVariant("NOT VALID");
                break;
        }
    }
}

bool FreeDVReporterDialog::FreeDVReporterDataModel::IsContainer (const wxDataViewItem &item) const
{
    // Single-level (i.e. no children)
    return !item.IsOk();
}

bool FreeDVReporterDialog::FreeDVReporterDataModel::SetValue (const wxVariant &, const wxDataViewItem &, unsigned int)
{
    // I think this can just return false without changing anything as this is read-only (other than what comes from the server).
    return false;
}

void FreeDVReporterDialog::FreeDVReporterDataModel::refreshAllRows()
{
    std::unique_lock<std::recursive_mutex> lk(dataMtx_);

#if defined(WIN32)
    bool doAutoSizeColumns = false;
#endif // defined(WIN32)
    
    wxDataViewItemArray itemsAdded;
    wxDataViewItemArray itemsDeleted;
    wxDataViewItem currentSelection = parent_->m_listSpots->GetSelection();
    
    for (auto& kvp : allReporterData_)
    {
    	if (kvp.second->isPendingDelete)
    	{
    	    continue;
    	}

        bool updated = false;
        double frequencyUserReadable = kvp.second->frequency / 1000.0;
        wxString frequencyString;
        
        if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
        {
            frequencyString = wxNumberFormatter::ToString(frequencyUserReadable, 1);
        }
        else
        {
            frequencyUserReadable /= 1000.0;
            frequencyString = wxNumberFormatter::ToString(frequencyUserReadable, 4);
        }
        
        updated |= kvp.second->freqString != frequencyString;
        kvp.second->freqString = frequencyString;

        // Refresh cardinal vs. degree directions. (There's no direction to
        // or from somewhere that isn't a usable grid square.)
        if (wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare != kvp.second->gridSquare &&
            GridSquare::IsValid(kvp.second->gridSquare) &&
            GridSquare::IsValid(wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare))
        {
            wxString newHeading;
            if (wxGetApp().appConfiguration.reportingConfiguration.reportingDirectionAsCardinal)
            {
                newHeading = GridSquare::CardinalDirection(kvp.second->headingVal);
            }
            else
            {
                newHeading = wxNumberFormatter::ToString(kvp.second->headingVal, 0);
            }

            updated |= kvp.second->heading != newHeading;
            kvp.second->heading = newHeading;
        }

        // Refresh filter state
        bool newVisibility = !isFiltered_(kvp.second);
        if (newVisibility != kvp.second->isVisible)
        {
            kvp.second->isVisible = newVisibility;
            if (newVisibility)
            {
                // Text may have changed while the row was hidden.
                kvp.second->cellTextsStale = true;

                itemsAdded.Add(wxDataViewItem(kvp.second));
#if defined(WIN32)
                doAutoSizeColumns = true;
#endif // defined(WIN32)
            }
            else
            {
                wxDataViewItem dvi(kvp.second);
                itemsDeleted.Add(dvi);
                
                if (currentSelection.IsOk() && currentSelection.GetID() == dvi.GetID())
                {
                    // Selection no longer valid
                    currentSelection = wxDataViewItem(nullptr);
                }
            }
            sortOnNextTimerInterval = true;
        }
        else if (updated && kvp.second->isVisible)
        {
            kvp.second->isPendingUpdate = true;
        }
    }
    
    if (itemsDeleted.size() > 0 || itemsAdded.size() > 0)
    {
        Cleared(); // avoids spurious errors on macOS
        columnsNeedWidthCheck_ = true;
        if (currentSelection.IsOk())
        {
            // Reselect after redraw
            parent_->CallAfter([&, currentSelection]() {
                parent_->m_listSpots->Select(currentSelection);
            });
        }
    }
    
#if defined(WIN32)
    if (doAutoSizeColumns && maxTextWidthsChanged_())
    {
        // Only auto-resize columns on Windows due to known rendering bugs. Trying to do so on other
        // platforms causes excessive CPU usage for no benefit.
        parent_->autosizeColumns();
    }
#endif // defined(WIN32)
}

void FreeDVReporterDialog::FreeDVReporterDataModel::requestQSY(wxDataViewItem selectedItem, uint64_t, wxString const& customText)
{
    if (reporter_ && selectedItem.IsOk())
    {
        auto row = (ReporterData*)selectedItem.GetID();
        reporter_->requestQSY(row->sid, wxGetApp().appConfiguration.reportingConfiguration.reportingFrequency, (const char*)customText.ToUTF8());
    }
}

// =================================================================================
// Note: these methods below do not run under the UI thread, so we need to make sure
// UI actions happen there.
// =================================================================================

void FreeDVReporterDialog::FreeDVReporterDataModel::onReporterConnect_()
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);
    CallbackHandler handler;
    handler.fn = [this](CallbackHandler&) {
        log_debug("Connected to server");
        filterSelfMessageUpdates_ = false;
        clearAllEntries_();
    };

    fnQueue_.push_back(std::move(handler));
    parent_->CallAfter(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::execQueuedAction_, this));
}

void FreeDVReporterDialog::FreeDVReporterDataModel::onReporterDisconnect_()
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);
    CallbackHandler handler;
    handler.fn = [this](CallbackHandler&) {
        log_debug("Disconnected from server");
        isConnected_ = false;
        filterSelfMessageUpdates_ = false;
        clearAllEntries_();
    };

    fnQueue_.push_back(std::move(handler));
    parent_->CallAfter(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::execQueuedAction_, this));
}

void FreeDVReporterDialog::FreeDVReporterDataModel::onUserConnectFn_(std::string sid, std::string lastUpdate, std::string callsign, std::string gridSquare, std::string version, bool rxOnly, std::string connectTime)
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);
    CallbackHandler handler;
    handler.sid = std::move(sid);
    handler.lastUpdate = std::move(lastUpdate);
    handler.callsign = std::move(callsign);
    handler.gridSquare = std::move(gridSquare);
    handler.version = std::move(version);
    handler.rxOnly = rxOnly;
    handler.connectTime = std::move(connectTime);

    handler.fn = [this](CallbackHandler& handler) {
        std::unique_lock<std::recursive_mutex> lk(const_cast<std::recursive_mutex&>(dataMtx_));
        assert(wxThread::IsMain());

        std::string sid = std::move(handler.sid);
        std::string lastUpdate = std::move(handler.lastUpdate);
        std::string callsign = std::move(handler.callsign);
        std::string gridSquare = std::move(handler.gridSquare);
        std::string version = std::move(handler.version);
        std::string connectTime = std::move(handler.connectTime);
        bool rxOnly = handler.rxOnly;

        log_debug("User connected: %s (%s) with SID %s", callsign.c_str(), gridSquare.c_str(), sid.c_str());

        // Initially populate stored report data, but don't add to the viewable list just yet. 
        // We only add on frequency update and only if the filters check out.
        ReporterData* temp = new ReporterData;
        assert(temp != nullptr);
        
        // Limit grid square display to six characters.
        wxString gridSquareWxString = wxString(gridSquare).Left(6);
        
        temp->sid = sid;
        temp->callsign = wxString(callsign).Upper();
        temp->gridSquare = gridSquareWxString.Left(2).Upper() + gridSquareWxString.Mid(2, 2);

        // Lowercase final letters of grid square per standard.
        if (gridSquareWxString.Length() >= 6)
        {
            temp->gridSquare += gridSquareWxString.Mid(4, 2).Lower();
        }

        // Both ends need a usable grid square. (The user's own is whatever
        // was typed into the options and so may be incomplete.)
        bool validCharactersInGridSquare = GridSquare::IsValid(temp->gridSquare);
        bool validOwnGridSquare = GridSquare::IsValid(wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare);

        if (!validOwnGridSquare || !validCharactersInGridSquare)
        {
            // Invalid grid square means we can't calculate a distance.
            temp->distance = parent_->UNKNOWN_STR;
            temp->distanceVal = 0;
            temp->heading = parent_->UNKNOWN_STR;
            temp->headingVal = 0;
        }
        else
        {
            temp->distanceVal = GridSquare::DistanceInKm(wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare, gridSquareWxString);

            if (!wxGetApp().appConfiguration.reportingConfiguration.useMetricDistances)
            {
                // Convert to miles for those who prefer it
                // (GridSquare::DistanceInKm() returns distance in km).
                temp->distanceVal *= 0.621371;
            }

            temp->distance = wxNumberFormatter::ToString(temp->distanceVal, 0);

            if (wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare == gridSquareWxString)
            {
                temp->headingVal = 0;
                temp->heading = parent_->UNKNOWN_STR;
            }
            else
            {
                temp->headingVal = GridSquare::BearingInDegrees(wxGetApp().appConfiguration.reportingConfiguration.reportingGridSquare, gridSquareWxString);
                if (wxGetApp().appConfiguration.reportingConfiguration.reportingDirectionAsCardinal)
                {
                    temp->heading = GridSquare::CardinalDirection(temp->headingVal);
                }
                else
                {
                    temp->heading = wxNumberFormatter::ToString(temp->headingVal, 0);
                }
            }
        }

        temp->version = version;
        temp->freqString = parent_->UNKNOWN_STR;
        temp->userMessage = parent_->UNKNOWN_STR;
        temp->transmitting = false;
        
        if (rxOnly)
        {
            temp->status = RX_ONLY_STATUS;
            temp->txMode = parent_->UNKNOWN_STR;
            temp->lastTx = parent_->UNKNOWN_STR;
        }
        else
        {
            temp->status = parent_->UNKNOWN_STR;
            temp->txMode = parent_->UNKNOWN_STR;
            temp->lastTx = parent_->UNKNOWN_STR;
        }
        
        temp->lastRxCallsign = parent_->UNKNOWN_STR;
        temp->lastRxMode = parent_->UNKNOWN_STR;
        temp->snrVal = UNKNOWN_SNR_VAL;
        temp->snr = parent_->UNKNOWN_STR + wxT("  ");
        
        // Default to sane colors for rows. If we need to highlight, the timer will change
        // these later. 
        //
#if defined(__APPLE__)
        // To ensure that the columns don't have a different color than the rest of the control.
        // Needed mainly for macOS.
        temp->backgroundColor = wxColour(wxTransparentColour);
#else
        temp->backgroundColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
#endif // defined(__APPLE__)
        temp->foregroundColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);

        auto lastUpdateTime = makeValidTime_(lastUpdate, temp->lastUpdateDate);
        temp->lastUpdate = lastUpdateTime;
        makeValidTime_(connectTime, temp->connectTime);
        // defer visibility until timer update
        temp->isVisible = false;
        temp->isPendingUpdate = false;

        if (allReporterData_.find(sid) != allReporterData_.end() && !isConnected_)
        {
            log_warn("Received duplicate user during connection process");
            return;
        }
        
        temp->isPendingDelete = false;
        
        auto existsIter = allReporterData_.find(sid);
        if (existsIter != allReporterData_.end())
        {
            // Pending deletion prior to reconnect, so go ahead and delete now.
            delete existsIter->second;
        }
        allReporterData_[sid] = temp;
    };

    fnQueue_.push_back(std::move(handler));
}

void FreeDVReporterDialog::FreeDVReporterDataModel::onConnectionSuccessfulFn_()
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);

    CallbackHandler handler;
    handler.fn = [this](CallbackHandler&) {
        std::unique_lock<std::recursive_mutex> lk(const_cast<std::recursive_mutex&>(dataMtx_));

        log_debug("Fully connected to server");

        // Enable highlighting now that we're fully connected.
        isConnected_ = true;
    };

    fnQueue_.push_back(std::move(handler));
}

void FreeDVReporterDialog::FreeDVReporterDataModel::onUserDisconnectFn_(std::string sid, std::string const&, std::string const&, std::string const&, std::string const&, bool, std::string const&)
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);

    CallbackHandler handler;
    handler.sid = std::move(sid);
    handler.fn = [this](CallbackHandler& handler) {
        std::unique_lock<std::recursive_mutex> lk(const_cast<std::recursive_mutex&>(dataMtx_));
        assert(wxThread::IsMain());

        std::string sid = std::move(handler.sid);
        log_debug("User with SID %s disconnected", sid.c_str());

        auto iter = allReporterData_.find(sid);
        if (iter != allReporterData_.end())
        {
            auto item = iter->second;
            if (item->isVisible)
            {
#if !defined(__linux__)
                // For non-Linux/GTK, isVisible must be set to false prior to removal
                // to avoid referencing deallocated memory during table updates (i.e.
                // by macOS when adjusting column widths).
                item->isVisible = false;
#endif // !defined(__linux__)
                
                wxDataViewItem dvi(item);
                ItemDeleted(wxDataViewItem(nullptr), dvi);
                
#if defined(__linux__)
                // For Linux/GTK, isVisible must still remain true during
                // actual removal from the GUI or else assertion failures
                // are thrown. We can set it to false once the item's removed.
                item->isVisible = false;
#endif // defined(__linux__)

                columnsNeedWidthCheck_ = true;
            }
            item->isPendingDelete = true;
            item->deleteTime = wxDateTime::Now();
        }
    };

    fnQueue_.push_back(std::move(handler));
}

void FreeDVReporterDialog::FreeDVReporterDataModel::onFrequencyChangeFn_(std::string sid, std::string lastUpdate, std::string const&, std::string const&, uint64_t frequencyHz)
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);
    CallbackHandler handler;
    handler.sid = std::move(sid);
    handler.frequencyHz = frequencyHz;
    handler.lastUpdate = std::move(lastUpdate);
    handler.fn = [this](CallbackHandler& handler) {
        std::unique_lock<std::recursive_mutex> lk(const_cast<std::recursive_mutex&>(dataMtx_));
       
        std::string sid = std::move(handler.sid);
        uint64_t frequencyHz = handler.frequencyHz;
        std::string lastUpdate = std::move(handler.lastUpdate);
 
        auto iter = allReporterData_.find(sid);
        if (iter != allReporterData_.end())
        {
            if (iter->second->isPendingDelete)
            {
                log_warn("Got frequency change for user pending deletion (%s/%s, SID %s)", (const char*)iter->second->callsign.ToUTF8(), (const char*)iter->second->gridSquare.ToUTF8(), iter->second->sid.c_str());
                return;
            }

            double frequencyUserReadable = frequencyHz / 1000.0;
            wxString frequencyString;
            
            if (wxGetApp().appConfiguration.reportingConfiguration.reportingFrequencyAsKhz)
            {
                frequencyString = wxNumberFormatter::ToString(frequencyUserReadable, 1);
            }
            else
            {
                frequencyUserReadable /= 1000.0;
                frequencyString = wxNumberFormatter::ToString(frequencyUserReadable, 4);
            }
            auto lastUpdateTime = makeValidTime_(lastUpdate, iter->second->lastUpdateDate);

            auto sortingColumn = parent_->m_listSpots->GetSortingColumn();
            bool isChanged = 
                (isSortingColumn_(sortingColumn, FREQUENCY_COL) && iter->second->frequency != frequencyHz) ||
                (isSortingColumn_(sortingColumn, LAST_UPDATE_DATE_COL) && iter->second->lastUpdate != lastUpdateTime);
            bool isDataChanged = 
                (iter->second->frequency != frequencyHz ||
                 iter->second->lastUpdate != lastUpdateTime);
            
            iter->second->frequency = frequencyHz;
            iter->second->freqString = frequencyString;
            iter->second->lastUpdate = lastUpdateTime;

            if (iter->second->isVisible)
            {            
                iter->second->isPendingUpdate = isDataChanged;
                sortOnNextTimerInterval |= isChanged;
            }
        }
    };

    fnQueue_.push_back(std::move(handler));
}

void FreeDVReporterDialog::FreeDVReporterDataModel::onTransmitUpdateFn_(std::string sid, std::string lastUpdate, std::string const&, std::string const&, std::string txMode, bool transmitting, std::string lastTxDate)
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);
    CallbackHandler handler;
    handler.sid = std::move(sid);
    handler.txMode = std::move(txMode);
    handler.transmitting = transmitting;
    handler.lastTxDate = std::move(lastTxDate);
    handler.lastUpdate = std::move(lastUpdate);
    handler.fn = [this](CallbackHandler& handler) {
        std::unique_lock<std::recursive_mutex> lk(const_cast<std::recursive_mutex&>(dataMtx_));
   
        std::string sid = std::move(handler.sid);
        std::string txMode = std::move(handler.txMode);
        bool transmitting = handler.transmitting;
        std::string lastTxDate = std::move(handler.lastTxDate);
        std::string lastUpdate = std::move(handler.lastUpdate); 

        auto iter = allReporterData_.find(sid);
        if (iter != allReporterData_.end())
        {
            if (iter->second->isPendingDelete)
            {
                log_warn("Got TX change for user pending deletion (%s/%s, SID %s)", (const char*)iter->second->callsign.ToUTF8(), (const char*)iter->second->gridSquare.ToUTF8(), iter->second->sid.c_str());
                return;
            }

            bool isChanged = iter->second->transmitting != transmitting;
            bool isDataChanged = isChanged;
            
            iter->second->transmitting = transmitting;
        
            auto sortingColumn = parent_->m_listSpots->GetSortingColumn();
            std::string txStatus = "RX";
            if (transmitting)
            {
                txStatus = "TX";
            }
        
            if (iter->second->status != RX_ONLY_STATUS)
            {
                isChanged |=
                    (isSortingColumn_(sortingColumn, STATUS_COL) && iter->second->status != txStatus) ||
                    (isSortingColumn_(sortingColumn, TX_MODE_COL) && iter->second->txMode != txMode);
                isDataChanged |=
                    iter->second->status != txStatus ||
                    iter->second->txMode != txMode;
                
                iter->second->status = txStatus;
                iter->second->txMode = txMode;
            
                auto lastTxTime = makeValidTime_(lastTxDate, iter->second->lastTxDate);
                isChanged |= (isSortingColumn_(sortingColumn, LAST_TX_DATE_COL) && iter->second->lastTx != lastTxTime);
                isDataChanged |= iter->second->lastTx != lastTxTime;
                iter->second->lastTx = lastTxTime;
            }
        
            auto lastUpdateTime = makeValidTime_(lastUpdate, iter->second->lastUpdateDate);
            iter->second->lastUpdate = lastUpdateTime;

            if (iter->second->isVisible)
            { 
                iter->second->isPendingUpdate = isDataChanged;
                sortOnNextTimerInterval |= isChanged;
            }
        }
    };

    fnQueue_.push_back(std::move(handler));
}

void FreeDVReporterDialog::FreeDVReporterDataModel::onReceiveUpdateFn_(std::string sid, std::string lastUpdate, std::string const&, std::string const&, std::string receivedCallsign, float snr, std::string rxMode)
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);
    CallbackHandler handler;
    handler.sid = std::move(sid);
    handler.lastUpdate = std::move(lastUpdate);
    handler.receivedCallsign = std::move(receivedCallsign);
    handler.snr = snr;
    handler.rxMode = std::move(rxMode);
    handler.fn = [this](CallbackHandler& handler) {
        std::unique_lock<std::recursive_mutex> lk(const_cast<std::recursive_mutex&>(dataMtx_));
   
        std::string sid = std::move(handler.sid);
        std::string lastUpdate = std::move(handler.lastUpdate);
        std::string receivedCallsign = std::move(handler.receivedCallsign);
        float snr = handler.snr;
        std::string rxMode = std::move(handler.rxMode);
 
        auto iter = allReporterData_.find(sid);
        if (iter != allReporterData_.end())
        {            
            if (iter->second->isPendingDelete)
            {
                log_warn("Got RX change for user pending deletion (%s/%s, SID %s)", (const char*)iter->second->callsign.ToUTF8(), (const char*)iter->second->gridSquare.ToUTF8(), iter->second->sid.c_str());
                return;
            }

            wxString receivedCallsignWx(receivedCallsign);
            wxString rxModeWx(rxMode);

            auto sortingColumn = parent_->m_listSpots->GetSortingColumn();
            bool isChanged = 
                (isSortingColumn_(sortingColumn, LAST_RX_CALLSIGN_COL) && iter->second->lastRxCallsign != receivedCallsignWx);
            bool isDataChanged =
                iter->second->lastRxCallsign != receivedCallsignWx ||
                iter->second->lastRxMode != rxModeWx;
            
            iter->second->lastRxCallsign = receivedCallsignWx;
            iter->second->lastRxMode = rxModeWx;

            wxString snrString = formatSnr_(snr) + wxT("  ");
            if (receivedCallsign == "" && rxMode == "")
            {
                // Frequency change--blank out SNR too.
                isChanged |=
                    (isSortingColumn_(sortingColumn, LAST_RX_CALLSIGN_COL) && iter->second->lastRxCallsign != parent_->UNKNOWN_STR) ||
                    (isSortingColumn_(sortingColumn, SNR_COL) && iter->second->snr != parent_->UNKNOWN_STR) ||
                    iter->second->lastRxDate.IsValid();
                isDataChanged |=
                    iter->second->lastRxCallsign != parent_->UNKNOWN_STR ||
                    iter->second->lastRxMode != parent_->UNKNOWN_STR ||
                    iter->second->snr != parent_->UNKNOWN_STR ||
                    iter->second->lastRxDate.IsValid();
                
                iter->second->lastRxCallsign = parent_->UNKNOWN_STR;
                iter->second->lastRxMode = parent_->UNKNOWN_STR;
                iter->second->snrVal = UNKNOWN_SNR_VAL;
                iter->second->snr = parent_->UNKNOWN_STR;
                iter->second->lastRxDate = wxDateTime(wxInvalidDateTime);
            }
            else
            {
                isChanged |=
                    (isSortingColumn_(sortingColumn, SNR_COL) && iter->second->snr != snrString);
                isDataChanged |=
                    iter->second->snr != snrString;
                
                iter->second->snrVal = snr;
                iter->second->snr = snrString;
                iter->second->lastRxDate = wxDateTime::Now();
            }
       
            auto lastUpdateTime = makeValidTime_(lastUpdate, iter->second->lastUpdateDate);
            iter->second->lastUpdate = lastUpdateTime;
        
            if (iter->second->isVisible)
            { 
                iter->second->isPendingUpdate = isDataChanged;
                sortOnNextTimerInterval |= isChanged;
            }
        }
    };

    fnQueue_.push_back(std::move(handler));
}

void FreeDVReporterDialog::FreeDVReporterDataModel::onMessageUpdateFn_(std::string sid, std::string lastUpdate, std::string message)
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);
    CallbackHandler handler;
    handler.sid = std::move(sid);
    handler.lastUpdate = std::move(lastUpdate);
    handler.message = std::move(message);
    handler.fn = [this](CallbackHandler& handler) {
        std::unique_lock<std::recursive_mutex> lk(const_cast<std::recursive_mutex&>(dataMtx_));
   
        std::string sid = std::move(handler.sid);
        std::string lastUpdate = std::move(handler.lastUpdate); 
        std::string message = std::move(handler.message);

        auto iter = allReporterData_.find(sid);
        if (iter != allReporterData_.end())
        {        
            if (iter->second->isPendingDelete)
            {
                log_warn("Got message change for user pending deletion (%s/%s, SID %s)", (const char*)iter->second->callsign.ToUTF8(), (const char*)iter->second->gridSquare.ToUTF8(), iter->second->sid.c_str());
                return;
            }

            auto sortingColumn = parent_->m_listSpots->GetSortingColumn();
            bool isChanged = false;
            if (message.size() == 0)
            {
                isChanged |= (isSortingColumn_(sortingColumn, USER_MESSAGE_COL) && iter->second->userMessage != parent_->UNKNOWN_STR);
                iter->second->userMessage = parent_->UNKNOWN_STR;
            }
            else
            {
                auto msgAsWxString = wxString::FromUTF8(message.c_str());
                isChanged |= (isSortingColumn_(sortingColumn, USER_MESSAGE_COL) && iter->second->userMessage != msgAsWxString);
                iter->second->userMessage = msgAsWxString;
            }
        
            auto lastUpdateTime = makeValidTime_(lastUpdate, iter->second->lastUpdateDate);
            iter->second->lastUpdate = lastUpdateTime;

            // Only highlight on non-empty messages.
            bool ourCallsign = iter->second->callsign == wxGetApp().appConfiguration.reportingConfiguration.reportingCallsign;
            bool filteringSelf = ourCallsign && filterSelfMessageUpdates_;
        
            if (message.size() > 0 && !filteringSelf)
            {
                iter->second->lastUpdateUserMessage = iter->second->lastUpdateDate;
            }
            else if (ourCallsign && filteringSelf)
            {
                // Filter only until we show up again, then return to normal behavior.
                filterSelfMessageUpdates_ = false;
            }
       
            if (iter->second->isVisible)
            {
                iter->second->isPendingUpdate = true;
                sortOnNextTimerInterval |= isChanged;
            }
        }
    };

    fnQueue_.push_back(std::move(handler));
}

void FreeDVReporterDialog::FreeDVReporterDataModel::onAboutToShowSelfFn_()
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);

    CallbackHandler handler;
    handler.fn = [this](CallbackHandler&) {
        filterSelfMessageUpdates_ = true;
    };

    fnQueue_.push_back(std::move(handler));
    parent_->CallAfter(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::execQueuedAction_, this));
}

void FreeDVReporterDialog::FreeDVReporterDataModel::onRecvEndFn_()
{
    std::unique_lock<std::mutex> lk(fnQueueMtx_);
    if (fnQueue_.size() > 0)
    {
        parent_->CallAfter(std::bind(&FreeDVReporterDialog::FreeDVReporterDataModel::execQueuedAction_, this));
    }
}
