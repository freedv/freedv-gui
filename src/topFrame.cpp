//==========================================================================
// Name:            topFrame.cpp
//
// Purpose:         Implements simple wxWidgets application with GUI.
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

#include <map>
#include <set>

#include <wx/regex.h>
#include <wx/wrapsizer.h>
#include <wx/aui/tabmdi.h>
#include <wx/numformatter.h>

#include "topFrame.h"
#include "defines.h"

#if !wxCHECK_VERSION(3, 3, 0)
#include <set>
#endif // !wxCHECK_VERSION(3, 3, 0)

#include "gui/util/NameOverrideAccessible.h"
#include "gui/util/LabelOverrideAccessible.h"
#include "util/logging/ulog.h"

#if defined(__WXGTK__) && defined(HAS_GTK3)
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif // GDK_WINDOWING_WAYLAND
#endif // defined(__WXGTK__) && defined(HAS_GTK3)

extern int g_playFileToMicInEventId;

wxPoint LeftOffsetContextMenuPosition(wxWindow* btn)
{
#if defined(__WXGTK__) && defined(HAS_GTK3) && defined(GDK_WINDOWING_WAYLAND)
    GdkDisplay* display = gdk_display_get_default();
    if (display != nullptr && GDK_IS_WAYLAND_DISPLAY(display))
    {
        return wxDefaultPosition;
    }
#endif // defined(__WXGTK__) && defined(HAS_GTK3) && defined(GDK_WINDOWING_WAYLAND)

    auto sz = btn->GetSize();
    return wxPoint(-sz.GetWidth() - 25, 0);
}

extern int g_recFileFromRadioEventId;
extern int g_recFileFromDecoderEventId;
extern int g_playFileFromRadioEventId;
extern int g_recFileFromModulatorEventId;
extern int g_txLevel;

#define MIC_SPKR_LEVEL_FORMAT_STR "%s%s"
#define DECIBEL_STR "dB"

#if !wxCHECK_VERSION(3, 3, 0)
// THIS IS VERY MUCH A HACK! wxTabFrame is not in the public interface and should
// not be here, even named as something else. Unfortunately this is needed to get
// the tab state loaded and saved on wxWidgets versions older than 3.3, which lack
// wxAuiNotebook::SaveLayout()/LoadLayout(). Here's hoping this interface remains stable.
//
// (Last retrieved from wxWidgets 3.0.5.1 on August 8, 2023.)
class wxTabFrameOurs : public wxWindow
{
public:

    wxTabFrameOurs()
    {
        m_tabs = NULL;
        m_rect = wxRect(0,0,200,200);
        m_tabCtrlHeight = 20;
    }

    ~wxTabFrameOurs()
    {
        wxDELETE(m_tabs);
    }

    void SetTabCtrlHeight(int h)
    {
        m_tabCtrlHeight = h;
    }

protected:
    void DoSetSize(int x, int y,
                   int width, int height,
                   int WXUNUSED(sizeFlags = wxSIZE_AUTO))
    {
        m_rect = wxRect(x, y, width, height);
        DoSizing();
    }

    void DoGetClientSize(int* x, int* y) const
    {
        *x = m_rect.width;
        *y = m_rect.height;
    }

public:
    bool Show( bool WXUNUSED(show = true) ) { return false; }

    void DoSizing()
    {
        if (!m_tabs)
            return;

        if (m_tabs->IsFrozen() || m_tabs->GetParent()->IsFrozen())
            return;

        m_tab_rect = wxRect(m_rect.x, m_rect.y, m_rect.width, m_tabCtrlHeight);
        if (m_tabs->GetFlags() & wxAUI_NB_BOTTOM)
        {
            m_tab_rect = wxRect (m_rect.x, m_rect.y + m_rect.height - m_tabCtrlHeight, m_rect.width, m_tabCtrlHeight);
            m_tabs->SetSize     (m_rect.x, m_rect.y + m_rect.height - m_tabCtrlHeight, m_rect.width, m_tabCtrlHeight);
            m_tabs->SetRect     (wxRect(0, 0, m_rect.width, m_tabCtrlHeight));
        }
        else //TODO: if (GetFlags() & wxAUI_NB_TOP)
        {
            m_tab_rect = wxRect (m_rect.x, m_rect.y, m_rect.width, m_tabCtrlHeight);
            m_tabs->SetSize     (m_rect.x, m_rect.y, m_rect.width, m_tabCtrlHeight);
            m_tabs->SetRect     (wxRect(0, 0,        m_rect.width, m_tabCtrlHeight));
        }
        // TODO: else if (GetFlags() & wxAUI_NB_LEFT){}
        // TODO: else if (GetFlags() & wxAUI_NB_RIGHT){}

        m_tabs->Refresh();
        m_tabs->Update();

        auto& pages = m_tabs->GetPages();
        size_t i, page_count = pages.GetCount();

        for (i = 0; i < page_count; ++i)
        {
            wxAuiNotebookPage& page = pages.Item(i);
            int border_space = m_tabs->GetArtProvider()->GetAdditionalBorderSpace(page.window);

            int height = m_rect.height - m_tabCtrlHeight - border_space;
            if ( height < 0 )
            {
                // avoid passing negative height to wxWindow::SetSize(), this
                // results in assert failures/GTK+ warnings
                height = 0;
            }
            int width = m_rect.width - 2 * border_space;
            if (width < 0)
                width = 0;

            if (m_tabs->GetFlags() & wxAUI_NB_BOTTOM)
            {
                page.window->SetSize(m_rect.x + border_space,
                                     m_rect.y + border_space,
                                     width,
                                     height);
            }
            else //TODO: if (GetFlags() & wxAUI_NB_TOP)
            {
                page.window->SetSize(m_rect.x + border_space,
                                     m_rect.y + m_tabCtrlHeight,
                                     width,
                                     height);
            }
            // TODO: else if (GetFlags() & wxAUI_NB_LEFT){}
            // TODO: else if (GetFlags() & wxAUI_NB_RIGHT){}
        }
    }

protected:
    void DoGetSize(int* x, int* y) const
    {
        if (x)
            *x = m_rect.GetWidth();
        if (y)
            *y = m_rect.GetHeight();
    }

public:
    void Update()
    {
        // does nothing
    }

    wxRect m_rect;
    wxRect m_tab_rect;
    wxAuiTabCtrl* m_tabs;
    int m_tabCtrlHeight;
};
#endif // !wxCHECK_VERSION(3, 3, 0)

TabFreeAuiNotebook::TabFreeAuiNotebook(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
        : wxAuiNotebook(parent, id, pos, size, style) { }

bool TabFreeAuiNotebook::AcceptsFocus() const { return false; }
bool TabFreeAuiNotebook::AcceptsFocusFromKeyboard() const { return false; }
bool TabFreeAuiNotebook::AcceptsFocusRecursively() const { return false; }

#if !wxCHECK_VERSION(3, 3, 0)
// SavePerspective and LoadPerspective below credit https://forums.kirix.com/viewtopicdafe.html?f=15&t=542
// with minor modifications to make it compile on modern wxWidgets.
wxString TabFreeAuiNotebook::SavePerspective() {
    // Build list of panes/tabs
    wxString tabs;
    wxAuiPaneInfoArray& all_panes = m_mgr.GetAllPanes();
     const size_t pane_count = all_panes.GetCount();

     for (size_t i = 0; i < pane_count; ++i)
     {
       wxAuiPaneInfo& pane = all_panes.Item(i);
       if (pane.name == wxT("dummy"))
             continue;

         wxTabFrameOurs* tabframe = (wxTabFrameOurs*)pane.window;

       if (!tabs.empty()) tabs += wxT("|");
       tabs += pane.name;
       tabs += wxT("=");
  
       // Add tabs, keyed by caption rather than position. Position (AddPage() call
       // order) isn't stable across app versions if a tab is ever added, removed, or
       // reordered, which would silently corrupt previously-saved layouts.
       size_t page_count = tabframe->m_tabs->GetPageCount();
       for (size_t p = 0; p < page_count; ++p)
       {
          wxAuiNotebookPage& page = tabframe->m_tabs->GetPage(p);
          const size_t page_idx = m_tabs.GetIdxFromWindow(page.window);

          if (p) tabs += wxT(",");

          if ((int)page_idx == m_curPage) tabs += wxT("*");
          else if ((int)p == tabframe->m_tabs->GetActivePage()) tabs += wxT("+");
          tabs += page.caption;
       }
    }
    tabs += wxT("@");

    // Add frame perspective
    tabs += m_mgr.SavePerspective();

    return tabs;
}

bool TabFreeAuiNotebook::LoadPerspective(const wxString& layout) {
    // Remove all tab ctrls (but still keep them in main index)
    const size_t tab_count = m_tabs.GetPageCount();
    std::set<wxString> readdedTabs;

    // Caption -> master index lookup, so saved entries resolve by tab identity
    // rather than by position (see SavePerspective()).
    std::map<wxString, size_t> captionToIdx;
    for (size_t i = 0; i < tab_count; ++i) {
        captionToIdx[m_tabs.GetPage(i).caption] = i;
    }

    for (size_t i = 0; i < tab_count; ++i) {
       wxWindow* wnd = m_tabs.GetWindowFromIdx(i);

       // find out which onscreen tab ctrl owns this tab
       wxAuiTabCtrl* ctrl;
       int ctrl_idx;
       if (!FindTab(wnd, &ctrl, &ctrl_idx))
          return false;

       // remove the tab from ctrl
       if (!ctrl->RemovePage(wnd))
          return false;
    }
    RemoveEmptyTabFrames();

    size_t sel_page = 0;

    // Creates a new (empty) tab group pane, docked at the bottom, named paneName.
    auto createTabGroup = [&](const wxString& paneName) -> wxAuiTabCtrl* {
        wxTabFrameOurs* new_tabs = new wxTabFrameOurs();
        new_tabs->m_tabs = new wxAuiTabCtrl(this, m_tabIdCounter++);
        new_tabs->m_tabs->SetArtProvider(m_tabs.GetArtProvider()->Clone());
        new_tabs->m_tabCtrlHeight = m_tabCtrlHeight;
        new_tabs->m_tabs->SetFlags(m_flags);

        wxAuiPaneInfo pane_info = wxAuiPaneInfo().Name(paneName).Bottom().CaptionVisible(false);
        m_mgr.AddPane(new_tabs, pane_info);

        return new_tabs->m_tabs;
    };

    wxString tabs = layout.BeforeFirst(wxT('@'));
    wxAuiTabCtrl *dest_tabs = nullptr; // last group known to hold >= 1 page
    bool anyEmptyGroupsCreated = false;
    while (1)
     {
       const wxString tab_part = tabs.BeforeFirst(wxT('|'));

       // if the string is empty, we're done parsing
         if (tab_part.empty())
             break;

       // Get pane name
       const wxString pane_name = tab_part.BeforeFirst(wxT('='));
       wxAuiTabCtrl* new_group = createTabGroup(pane_name);

       // Get list of tab id's and move them to pane
       wxString tab_list = tab_part.AfterFirst(wxT('='));
       ssize_t activePage = -1;
       while(1) {
          wxString tab = tab_list.BeforeFirst(wxT(','));
          if (tab.empty()) break;
          tab_list = tab_list.AfterFirst(wxT(','));

          // Check if this page has an 'active' marker
          const wxChar c = tab[0];
          if (c == wxT('+') || c == wxT('*')) {
             tab = tab.Mid(1);
          }

          auto captionIt = captionToIdx.find(tab);
          if (captionIt == captionToIdx.end()) continue; // tab no longer exists (e.g. removed in a newer version)
          const size_t tab_idx = captionIt->second;

          // Move tab to pane
          wxAuiNotebookPage& page = m_tabs.GetPage(tab_idx);
          const size_t newpage_idx = new_group->GetPageCount();
          new_group->InsertPage(page.window, page, newpage_idx);
          readdedTabs.insert(tab);

          if (c == wxT('+')) activePage = newpage_idx;
          else if ( c == wxT('*')) sel_page = tab_idx;
       }

       if (new_group->GetPageCount() == 0)
       {
           // None of this group's saved entries resolved to a current tab (e.g. an
           // old, pre-caption-keyed saved layout - see SavePerspective()). Leaving a
           // zero-page tab group behind confuses wxAuiNotebook's own drag-and-drop
           // handling (crashes with an assertion failure in GetPage() when the user
           // later drags a tab near it), so sweep it up below instead.
           anyEmptyGroupsCreated = true;
       }
       else
       {
           if (activePage >= 0) new_group->SetActivePage(activePage);
           new_group->DoShowHide();
           dest_tabs = new_group;
       }

       tabs = tabs.AfterFirst(wxT('|'));
    }

    if (anyEmptyGroupsCreated)
    {
        RemoveEmptyTabFrames();
    }

    // Load the frame perspective
    const wxString frames = layout.AfterFirst(wxT('@'));
    bool framesLoaded = m_mgr.LoadPerspective(frames);

    bool ok = true;
    if (dest_tabs == nullptr)
    {
        // Saved layout string parsed to zero tab groups (e.g. empty/corrupted). Rather
        // than crash on the dereference below, fall back to one default group holding
        // every tab, matching what a first-run/no-saved-layout state looks like.
        log_warn("Tab layout persistence: saved layout produced no tab groups; falling back to default layout.");
        dest_tabs = createTabGroup(wxT("default"));
        ok = false;
    }
    else if (!framesLoaded)
    {
        log_warn("Tab layout persistence: failed to restore frame perspective; layout may not match what was saved.");
        ok = false;
    }

    // Reinsert tabs that weren't persisted before
    for (size_t i = 0; i < tab_count; ++i) {
        wxAuiNotebookPage& page = m_tabs.GetPage(i);
        if (readdedTabs.find(page.caption) != readdedTabs.end())
        {
            continue;
        }
        const size_t newpage_idx = dest_tabs->GetPageCount();
        dest_tabs->InsertPage(page.window, page, newpage_idx);
    }

    // Force refresh of selection
    m_curPage = -1;
    SetSelection(sel_page);

    return ok;
}
#endif // !wxCHECK_VERSION(3, 3, 0)

//=========================================================================
// Code that lays out the main application window
//=========================================================================
TopFrame::TopFrame(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxFrame(parent, id, title, pos, size, style)
{
    WXWIDGETS_SET_LAYOUT_DIRECTION;
    
#if wxUSE_ACCESSIBILITY
    // Initialize accessibility logic
    SetAccessible(new NameOverrideAccessible([&]() {
        auto labelStr = GetLabel(); // note: should be equivalent to title.

        // Ensures NVDA reads back version numbers as "x point y ..." rather
        // than as a date.
        wxRegEx rePoint("\\.");
        // TRANSLATORS: Read aloud by screen readers in place of the "." in the version number.
        rePoint.ReplaceAll(&labelStr, _(" point "));
        
        return labelStr;
    }));
#endif // wxUSE_ACCESSIBILITY
    
    this->SetSizeHints(wxDefaultSize, wxDefaultSize);
    
    //=====================================================
    // Menubar Setup
    //=====================================================
    m_menubarMain = new wxMenuBar(wxMB_DOCKABLE);
    file = new wxMenu();

#if !defined(__WXGTK__)
    /* "On Top" isn't reliable on Linux, so there's no point in having it visible. */
    wxMenuItem* m_menuItemOnTop;
    m_menuItemOnTop = new wxMenuItem(file, wxID_ANY, wxString(_("Keep &On Top")) , _("Always keeps FreeDV above other windows"), wxITEM_CHECK);
    file->Append(m_menuItemOnTop);
    this->Connect(m_menuItemOnTop->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnTop));
#endif // !defined(__WXGTK__)

    wxMenuItem* m_menuItemExit;
    m_menuItemExit = new wxMenuItem(file, wxID_EXIT, wxString(_("E&xit")) , _("Exit Program"), wxITEM_NORMAL);
    file->Append(m_menuItemExit);

    m_menubarMain->Append(file, _("&File"));

    settings = new wxMenu();

    // wxID_PREFERENCES causes wxWidgets to move this item into the application
    // menu on macOS automatically. On other platforms it stays in Settings.
    wxMenuItem* m_menuItemOptions;
#ifdef __WXMAC__
    m_menuItemOptions = new wxMenuItem(settings, wxID_PREFERENCES, wxString(_("&Edit Settings...\tCTRL-,")) , _("Miscellaneous FreeDV configuration options"), wxITEM_NORMAL);
#else
    m_menuItemOptions = new wxMenuItem(settings, wxID_PREFERENCES, wxString(_("&Edit Settings...")) , _("Miscellaneous FreeDV configuration options"), wxITEM_NORMAL);
#endif // __WXMAC__
    settings->Append(m_menuItemOptions);

    wxMenuItem* m_menuItemSetupWizard;
    m_menuItemSetupWizard = new wxMenuItem(settings, wxID_ANY, wxString(_("Setup &Wizard...")), _("Re-run the FreeDV Setup Wizard"), wxITEM_NORMAL);
    settings->Append(m_menuItemSetupWizard);

    settings->AppendSeparator();

    m_menuItemExportConfig = new wxMenuItem(settings, wxID_ANY, wxString(_("&Export Configuration...")) , _("Exports the current FreeDV configuration to a file"), wxITEM_NORMAL);
    settings->Append(m_menuItemExportConfig);

    m_menuItemImportConfig = new wxMenuItem(settings, wxID_ANY, wxString(_("&Use Configuration...")) , _("Loads a FreeDV configuration from a file"), wxITEM_NORMAL);
    settings->Append(m_menuItemImportConfig);

    wxMenuItem* m_menuItemLoadDefaultConfig;
    m_menuItemLoadDefaultConfig = new wxMenuItem(settings, wxID_ANY, wxString(_("Load &Default Configuration")) , _("Resets FreeDV to its default configuration"), wxITEM_NORMAL);
    settings->Append(m_menuItemLoadDefaultConfig);

    m_menubarMain->Append(settings, _("&Settings"));

    tools = new wxMenu();

    wxMenuItem* m_menuItemFreeDVReporter;
    m_menuItemFreeDVReporter = new wxMenuItem(tools, wxID_ANY, wxString(_("FreeDV R&eporter")) , _("Opens browser window and displays FreeDV Reporter service."), wxITEM_NORMAL);
    tools->Append(m_menuItemFreeDVReporter);

    wxMenuItem* m_menuItemFilter;
    m_menuItemFilter = new wxMenuItem(tools, wxID_ANY, wxString(_("Filter &Audio...")) , _("Configures audio filtering"), wxITEM_NORMAL);
    tools->Append(m_menuItemFilter);

    tools->AppendSeparator();

    m_menuItemPlayFileFromRadio = new wxMenuItem(tools, wxID_ANY, wxString(_("Start &Play File - From Radio...")) , _("Pipes radio sound input from file"), wxITEM_NORMAL);
    g_playFileFromRadioEventId = m_menuItemPlayFileFromRadio->GetId();
    tools->Append(m_menuItemPlayFileFromRadio);

    m_menubarMain->Append(tools, _("&Tools"));

    help = new wxMenu();
    wxMenuItem* m_menuItemHelpUpdates;
    m_menuItemHelpUpdates = new wxMenuItem(help, wxID_ANY, wxString(_("&Check for Updates")) , _("Checks for updates to FreeDV"), wxITEM_NORMAL);
    help->Append(m_menuItemHelpUpdates);
    m_menuItemHelpUpdates->Enable(false);
    
    wxMenuItem* m_menuItemAbout;
    m_menuItemAbout = new wxMenuItem(help, wxID_ABOUT, wxString(_("&About FreeDV")) , _("About this program"), wxITEM_NORMAL);
    help->Append(m_menuItemAbout);

    wxMenuItem* m_menuItemHelpManual;
    m_menuItemHelpManual = new wxMenuItem(help, wxID_ANY, wxString(_("&User Manual...")), _("Loads the user manual"), wxITEM_NORMAL);
    help->Append(m_menuItemHelpManual);

    wxMenuItem* m_menuItemHelpGetAssistance;
    m_menuItemHelpGetAssistance = new wxMenuItem(help, wxID_ANY, wxString(_("&Get Assistance")), _("Gets assistance with FreeDV setup"), wxITEM_NORMAL);
    help->Append(m_menuItemHelpGetAssistance);
        
    m_menubarMain->Append(help, _("&Help"));

    this->SetMenuBar(m_menubarMain);

    m_panel = new wxPanel(this);

    wxBoxSizer* bSizer1;
    bSizer1 = new wxBoxSizer(wxHORIZONTAL);

    //=====================================================
    // Left side
    //=====================================================
    wxSizer* leftOuterSizer = new wxBoxSizer(wxVERTICAL);
    wxSizer* leftSizer = new wxWrapSizer(wxVERTICAL, wxREMOVE_LEADING_SPACES);

    wxStaticBoxSizer* snrSizer;
    wxStaticBox* snrBox = new wxStaticBox(m_panel, wxID_ANY, _("SNR"), wxDefaultPosition, wxSize(100,-1));
    snrSizer = new wxStaticBoxSizer(snrBox, wxVERTICAL);

    //------------------------------
    // S/N ratio Gauge (vert. bargraph)
    //------------------------------
    m_gaugeSNR = new wxGauge(snrBox, wxID_ANY, 45, wxDefaultPosition, wxSize(135,15), wxGA_SMOOTH);
    m_gaugeSNR->SetToolTip(_("Displays signal to noise ratio in dB."));
    snrSizer->Add(m_gaugeSNR, 1, wxALIGN_CENTER_HORIZONTAL|static_cast<int>(wxALL), 10);

    //------------------------------
    // Box for S/N ratio (Numeric)
    //------------------------------
    m_textSNR = new wxStaticText(snrBox, wxID_ANY, wxT("--"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE);
    m_textSNR->SetMinSize(wxSize(70,-1));
    snrSizer->Add(m_textSNR, 0, wxALIGN_CENTER_HORIZONTAL, 1);

    //------------------------------
    // S/N ratio slow Checkbox
    //------------------------------
    m_ckboxSNR = new wxCheckBox(snrBox, wxID_ANY, _("Slow"), wxDefaultPosition, wxDefaultSize, wxCHK_2STATE);
    m_ckboxSNR->SetToolTip(_("Smooth but slow SNR estimation"));
    snrSizer->Add(m_ckboxSNR, 0, wxALIGN_CENTER_HORIZONTAL, 5);

    leftSizer->Add(snrSizer, 0, static_cast<int>(wxEXPAND)|static_cast<int>(wxALL), 2);

    //------------------------------
    // Signal Level(vert. bargraph)
    //------------------------------
    wxStaticBoxSizer* levelSizer;
    wxStaticBox* levelBox = new wxStaticBox(m_panel, wxID_ANY, _("Level"), wxDefaultPosition, wxSize(100,-1));
    levelSizer = new wxStaticBoxSizer(levelBox, wxVERTICAL);

    // LED-style meter, 13x12px segments (see LEVEL_METER_* in defines.h).
    m_gaugeLevel = new LevelMeterLed(
        levelBox, wxID_ANY, -LEVEL_GAUGE_MIN_DB, 0.0f, LEVEL_METER_SEGMENT_DB,
        LEVEL_METER_AMBER_START_DBFS, LEVEL_METER_RED_START_DBFS, 13, 12);
    m_gaugeLevel->SetToolTip(_("RX: Peak level of radio's audio output, TX: Peak level of microphone audio from the sound card (before noise reduction/AGC/level settings)."));
    levelSizer->Add(m_gaugeLevel, 0, static_cast<int>(wxALIGN_CENTER_HORIZONTAL)|static_cast<int>(wxALL), 10);
    
    leftSizer->Add(levelSizer ,0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 2);

    //------------------------------
    // Sync  Indicator box
    //------------------------------
    wxStaticBoxSizer* sbSizer3_33;
    wxStaticBox* syncBox = new wxStaticBox(m_panel, wxID_ANY, _("Sync"), wxDefaultPosition, wxSize(100,-1));
    sbSizer3_33 = new wxStaticBoxSizer(syncBox, wxVERTICAL);

    m_textSync = new wxStaticText(syncBox, wxID_ANY, _("Modem"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE);
    sbSizer3_33->Add(m_textSync, 0, wxALIGN_CENTER_HORIZONTAL, 1);
    m_textSync->Disable();

    m_textCurrentDecodeMode = new wxStaticText(syncBox, wxID_ANY, wxString::Format(_("Mode: %s"), _("unk")), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE);
    sbSizer3_33->Add(m_textCurrentDecodeMode, 0, wxALIGN_CENTER_HORIZONTAL, 1);
    m_textCurrentDecodeMode->Disable();
    
    m_btnCenterRx = new wxButton(syncBox, wxID_ANY, _("C&enter RX"), wxDefaultPosition, wxDefaultSize, 0);
    sbSizer3_33->Add(m_btnCenterRx, 0, static_cast<int>(wxALL) | wxALIGN_CENTRE, 5);
    
    leftSizer->Add(sbSizer3_33,0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND)|wxFIXED_MINSIZE, 2);

    //------------------------------
    // Audio Recording/Playback
    //------------------------------
    wxStaticBox* audioBox = new wxStaticBox(m_panel, wxID_ANY, _("Audio Recording"), wxDefaultPosition, wxSize(100,-1));
    wxStaticBoxSizer* sbSizerAudioRecordPlay = new wxStaticBoxSizer(audioBox, wxVERTICAL);
    
    m_audioRecord = new wxToggleButton(audioBox, wxID_ANY, _("Record"), wxDefaultPosition, wxDefaultSize, 0);
    m_audioRecord->SetToolTip(_("Records incoming over the air signals as well as anything transmitted."));
    sbSizerAudioRecordPlay->Add(m_audioRecord, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_HORIZONTAL, 5);
   
    leftSizer->Add(sbSizerAudioRecordPlay, 0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 2);

    //------------------------------
    // QSO logging
    //------------------------------
    wxStaticBox* logBox = new wxStaticBox(m_panel, wxID_ANY, _("Logging"), wxDefaultPosition, wxSize(100,-1));
    wxStaticBoxSizer* sbSizerLogging = new wxStaticBoxSizer(logBox, wxVERTICAL);
    
    m_logQSO = new wxButton(logBox, wxID_ANY, _("Log QSO"), wxDefaultPosition, wxDefaultSize, 0);
    m_logQSO->SetToolTip(_("Logs most recent QSO."));
    m_logQSO->Disable();
    sbSizerLogging->Add(m_logQSO, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_HORIZONTAL, 5);
    
    leftSizer->Add(sbSizerLogging, 0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 2);

    //------------------------------
    // FreeDV Reporter quick options
    //------------------------------
    wxStaticBox* reporterBox = new wxStaticBox(m_panel, wxID_ANY, _("FDV Reporting"), wxDefaultPosition, wxSize(100,-1));
    wxStaticBoxSizer* sbSizerReporterBox = new wxStaticBoxSizer(reporterBox, wxVERTICAL);

    m_reporterHidden = new wxToggleButton(reporterBox, wxID_ANY, _("Turn Off"), wxDefaultPosition, wxDefaultSize, 0);
    m_reporterHidden->SetToolTip(_("Quick ON/OFF for FreeDV Reporting, when enabled in Tools->Settings->Reporting."));
    sbSizerReporterBox->Add(m_reporterHidden, 0, static_cast<int>(wxALL) | wxALIGN_CENTER_HORIZONTAL, 5);

    leftSizer->Add(sbSizerReporterBox, 0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 2);
    
    //------------------------------
    // BER Frames box
    //------------------------------

    wxStaticBoxSizer* sbSizer_ber;
    statsBox = new wxStaticBox(m_panel, wxID_ANY, _("Stats"), wxDefaultPosition, wxSize(100,-1));
    sbSizer_ber = new wxStaticBoxSizer(statsBox, wxVERTICAL);

    m_BtnBerReset = new wxButton(statsBox, wxID_ANY, _("&Reset"), wxDefaultPosition, wxDefaultSize, 0);
    sbSizer_ber->Add(m_BtnBerReset, 0, wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL|static_cast<int>(wxALL), 5);

    m_textBits = new wxStaticText(statsBox, wxID_ANY, wxString::Format(_("Bits: %d"), 0), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    sbSizer_ber->Add(m_textBits, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 1);
    m_textErrors = new wxStaticText(statsBox, wxID_ANY, wxString::Format(_("Errs: %d"), 0), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    sbSizer_ber->Add(m_textErrors, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 1);
    m_textBER = new wxStaticText(statsBox, wxID_ANY, wxString::Format(_("BER: %4.3f"), 0.0), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    sbSizer_ber->Add(m_textBER, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 1);
    m_textResyncs = new wxStaticText(statsBox, wxID_ANY, wxString::Format(_("Resyncs: %d"), 0), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    sbSizer_ber->Add(m_textResyncs, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 1);
    m_textClockOffset = new wxStaticText(statsBox, wxID_ANY, wxString::Format(_("ClkOff: %+-d"), 0), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_textClockOffset->SetMinSize(wxSize(125,-1));
    sbSizer_ber->Add(m_textClockOffset, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 1);
    m_textFreqOffset = new wxStaticText(statsBox, wxID_ANY, wxString::Format(_("FrqOff: %3.1f"), 0.0), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    sbSizer_ber->Add(m_textFreqOffset, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 1);
    m_textSyncMetric = new wxStaticText(statsBox, wxID_ANY, wxString::Format(_("Sync: %3.2f"), 0.0), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    sbSizer_ber->Add(m_textSyncMetric, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 1);
    m_textCodec2Var = new wxStaticText(statsBox, wxID_ANY, wxString::Format(_("Var: %4.1f"), 0.0), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    sbSizer_ber->Add(m_textCodec2Var, 0, static_cast<int>(wxALL) | wxALIGN_LEFT, 1);

    leftSizer->Add(sbSizer_ber,0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND)|wxFIXED_MINSIZE, 2);

    leftSizer->SetMinSize(wxSize(-1, 375));
    
#if !wxCHECK_VERSION(3,2,0)
    leftOuterSizer->Add(leftSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND) | wxFIXED_MINSIZE, 1);
#else
    leftOuterSizer->Add(leftSizer, 2, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND) | wxFIXED_MINSIZE, 1);
#endif // !wxCHECK_VERSION(3,2,0)

    bSizer1->Add(leftOuterSizer, 0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 5);

    //=====================================================
    // Center Section
    //=====================================================
    wxBoxSizer* centerSizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* upperSizer = new wxBoxSizer(wxVERTICAL);

    //=====================================================
    // Tabbed Notebook control containing display graphs
    //=====================================================

    long nb_style = wxAUI_NB_BOTTOM | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS;
    m_auiNbookCtrl = new TabFreeAuiNotebook(m_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, nb_style);
    // This line sets the fontsize for the tabs on the notebook control
    m_auiNbookCtrl->SetMinSize(wxSize(375,375));

    upperSizer->Add(m_auiNbookCtrl, 1, wxALIGN_TOP|static_cast<int>(wxEXPAND), 1);
    centerSizer->Add(upperSizer, 1, wxALIGN_TOP|static_cast<int>(wxEXPAND), 0);

    // lower middle used for user ID

    wxBoxSizer* lowerSizer;
    lowerSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* modeStatusSizer;
    modeStatusSizer = new wxBoxSizer(wxVERTICAL);
    // TRANSLATORS: "unk" = unknown.
    m_txtModeStatus = new wxStaticText(m_panel, wxID_ANY, _("unk"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_txtModeStatus->Enable(false); // enabled only if Hamlib is turned on
    m_txtModeStatus->SetMinSize(wxSize(80,-1));
    modeStatusSizer->Add(m_txtModeStatus, 0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 1);
    lowerSizer->Add(modeStatusSizer, 0, wxALIGN_CENTER_VERTICAL|static_cast<int>(wxALL), 1);

    m_BtnCallSignReset = new wxButton(m_panel, wxID_ANY, _("&Clear"), wxDefaultPosition, wxDefaultSize, 0);
    lowerSizer->Add(m_BtnCallSignReset, 0, wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL|static_cast<int>(wxALL), 1);

    wxBoxSizer* bSizer15;
    bSizer15 = new wxBoxSizer(wxVERTICAL);
    m_txtCtrlCallSign = new wxTextCtrl(m_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
    m_txtCtrlCallSign->SetToolTip(_("Call Sign of transmitting station will appear here"));
    m_txtCtrlCallSign->SetSizeHints(wxSize(100,-1));

    m_cboLastReportedCallsigns = new wxComboCtrl(m_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxCB_READONLY);
    m_lastReportedCallsignListView = new wxListViewComboPopup(m_BtnCallSignReset);
    m_cboLastReportedCallsigns->SetPopupControl(m_lastReportedCallsignListView);
    m_cboLastReportedCallsigns->SetSizeHints(wxSize(400,-1));
    m_cboLastReportedCallsigns->SetPopupMaxHeight(150);
    
    m_lastReportedCallsignListView->InsertColumn(0, _("Callsign"), wxLIST_FORMAT_LEFT, 100);
    m_lastReportedCallsignListView->InsertColumn(1, _("Frequency"), wxLIST_FORMAT_RIGHT, 75);
    m_lastReportedCallsignListView->InsertColumn(2, _("Date/Time"), wxLIST_FORMAT_LEFT, 175);
    m_lastReportedCallsignListView->InsertColumn(3, _("SNR"), wxLIST_FORMAT_RIGHT, 50);

    bSizer15->Add(m_txtCtrlCallSign, 1, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 5);
    bSizer15->Add(m_cboLastReportedCallsigns, 1, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 5);

    lowerSizer->Add(bSizer15, 1, static_cast<int>(wxEXPAND), 5);
    lowerSizer->SetMinSize(wxSize(375,-1));
    centerSizer->Add(lowerSizer, 0, static_cast<int>(wxEXPAND), 2);
    centerSizer->SetMinSize(wxSize(375,375));
    bSizer1->Add(centerSizer, 1, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 1);
    
    //=====================================================
    // Right side
    //=====================================================
    rightSizer = new wxWrapSizer(wxVERTICAL, wxREMOVE_LEADING_SPACES);

    // Transmit Level slider
    m_txLevelBox = new wxStaticBox(m_panel, wxID_ANY, _("TX &Attenuation"), wxDefaultPosition, wxSize(100,-1));
    wxBoxSizer* txLevelSizer = new wxStaticBoxSizer(m_txLevelBox, wxVERTICAL);
    
    wxBoxSizer* txBtnSizer = new wxBoxSizer(wxHORIZONTAL);
    m_btnTxLevelMM = new wxButton(m_txLevelBox, wxID_ANY, wxT("<<"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    m_btnTxLevelM  = new wxButton(m_txLevelBox, wxID_ANY, wxT("<"),  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    m_btnTxLevelP  = new wxButton(m_txLevelBox, wxID_ANY, wxT(">"),  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    m_btnTxLevelPP = new wxButton(m_txLevelBox, wxID_ANY, wxT(">>"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    m_btnTxLevelMM->SetToolTip(_("Decrease output by 1.0dB"));
    m_btnTxLevelM ->SetToolTip(_("Decrease output by 0.2dB"));
    m_btnTxLevelP ->SetToolTip(_("Increase output by 0.2dB"));
    m_btnTxLevelPP->SetToolTip(_("Increase output by 1.0dB"));
    txBtnSizer->Add(m_btnTxLevelMM, 1, static_cast<int>(wxEXPAND), 0);
    txBtnSizer->Add(m_btnTxLevelM,  1, static_cast<int>(wxEXPAND), 0);
    txBtnSizer->Add(m_btnTxLevelP,  1, static_cast<int>(wxEXPAND), 0);
    txBtnSizer->Add(m_btnTxLevelPP, 1, static_cast<int>(wxEXPAND), 0);
    wxString fmtString = wxString::Format(MIC_SPKR_LEVEL_FORMAT_STR, wxNumberFormatter::ToString((double)0, 1), DECIBEL_STR);

    m_txtTxLevelNum = new wxStaticText(m_txLevelBox, wxID_ANY, fmtString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER | wxST_NO_AUTORESIZE);
    m_txtTxLevelNum->SetToolTip(_("Use mouse scroll wheel to adjust up or down\nRight click for more options"));
    m_txtTxLevelNum->SetMinSize(wxSize(100,-1));
    txLevelSizer->Add(m_txtTxLevelNum, 0, static_cast<int>(wxEXPAND), 0);

    txLevelSizer->Add(txBtnSizer, 0, static_cast<int>(wxEXPAND), 0);

    rightSizer->Add(txLevelSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
    
    // Mic/Speaker Level slider
    micSpeakerBox = new wxStaticBox(m_panel, wxID_ANY, _("Speaker &Level"), wxDefaultPosition, wxSize(100,-1));
    wxBoxSizer* micSpeakerLevelSizer = new wxStaticBoxSizer(micSpeakerBox, wxVERTICAL);
    
    // Sliders are integer values, so we're multiplying min/max by 10 here to allow 1 decimal precision.
    m_sliderMicSpkrLevel = new wxSlider(micSpeakerBox, wxID_ANY, 0, -200, 200, wxDefaultPosition, wxDefaultSize, wxSL_AUTOTICKS);
    m_sliderMicSpkrLevel->SetMinSize(wxSize(150,-1));
    micSpeakerLevelSizer->Add(m_sliderMicSpkrLevel, 1, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sliderMicSpkrLevel->Enable(false);

#if wxUSE_ACCESSIBILITY 
    // Add accessibility class so that the values are read back correctly.
    auto micSpkrSliderAccessibility = new LabelOverrideAccessible([&]() {
        return m_txtMicSpkrLevelNum->GetLabel();
    });
    m_sliderMicSpkrLevel->SetAccessible(micSpkrSliderAccessibility);
#endif // wxUSE_ACCESSIBILITY
    
    fmtString = wxString::Format(MIC_SPKR_LEVEL_FORMAT_STR, wxNumberFormatter::ToString((double)0, 1), DECIBEL_STR);
 
    m_txtMicSpkrLevelNum = new wxStaticText(micSpeakerBox, wxID_ANY, fmtString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    m_txtMicSpkrLevelNum->SetMinSize(wxSize(100,-1));
    micSpeakerLevelSizer->Add(m_txtMicSpkrLevelNum, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    
    rightSizer->Add(micSpeakerLevelSizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
    
    // Frequency text field (PSK Reporter)
    m_freqBox = new wxStaticBox(m_panel, wxID_ANY, _("Center Freq. (MHz)"), wxDefaultPosition, wxSize(100,-1));

    wxBoxSizer* reportFrequencySizer = new wxStaticBoxSizer(m_freqBox, wxHORIZONTAL);
    
    //wxStaticText* reportFrequencyUnits = new wxStaticText(m_freqBox, wxID_ANY, wxT(" MHz"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    wxBoxSizer* txtReportFreqSizer = new wxBoxSizer(wxVERTICAL);
    
    // A wxComboCtrl with our own list (rather than a native wxComboBox), so
    // that a frequency can be deleted from the open list: with Del on the
    // highlighted one, or from its right-click menu. The list keeps the
    // configured (ascending) order, and only an item actually clicked
    // changes the frequency -- not one the pointer just passed over.
    m_cboReportFrequency = new wxComboCtrl(m_freqBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    MsgListPopup::Options freqListOptions;
    freqListOptions.sorted = false;
    freqListOptions.openAbove = false;
    freqListOptions.hoverPicks = false;
    m_reportFrequencyList = new MsgListPopup([this](int item) { OnReportFrequencyItemMenu(item); }, freqListOptions);
    m_cboReportFrequency->SetPopupControl(m_reportFrequencyList);
    m_reportFrequencyList->SetDeleteKeyCallback([this](int item) { OnReportFrequencyItemDelete(item); });
    m_cboReportFrequency->SetPopupMaxHeight(300);
    m_cboReportFrequency->SetMinSize(wxSize(150,-1));
    txtReportFreqSizer->Add(m_cboReportFrequency, 1, static_cast<int>(wxALL), 5);
    
    reportFrequencySizer->Add(txtReportFreqSizer, 1, static_cast<int>(wxEXPAND), 1);
    //reportFrequencySizer->Add(reportFrequencyUnits, 0, wxALIGN_CENTER_VERTICAL, 1);
    
    rightSizer->Add(reportFrequencySizer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 2);
    
    /* new --- */

    //=====================================================
    // Control Toggles box
    //=====================================================
    wxStaticBoxSizer* sbSizer5;
    wxStaticBox* controlBox = new wxStaticBox(m_panel, wxID_ANY, _("Control"), wxDefaultPosition, wxSize(100,-1));
    sbSizer5 = new wxStaticBoxSizer(controlBox, wxVERTICAL);

    //-------------------------------
    // Stop/Stop signal processing (rx and tx)
    //-------------------------------
    m_togBtnOnOff = new wxToggleButton(controlBox, wxID_ANY, _("&Start Modem"), wxDefaultPosition, wxDefaultSize, 0);
    m_togBtnOnOff->SetToolTip(_("Begin/End receiving data."));
    sbSizer5->Add(m_togBtnOnOff, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);

    //------------------------------
    // Analog Passthrough Toggle
    //------------------------------
    m_togBtnAnalog = new wxToggleButton(controlBox, wxID_ANY, _("Switch to A&nalog"), wxDefaultPosition, wxDefaultSize, 0);
    m_togBtnAnalog->SetToolTip(_("Toggle analog/digital operation."));
    sbSizer5->Add(m_togBtnAnalog, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);

    //------------------------------
    // Tune Toggle
    //------------------------------
    m_btnTogTune = new wxToggleButton(controlBox, wxID_ANY, _("&Tune"), wxDefaultPosition, wxDefaultSize, 0);
    m_btnTogTune->SetToolTip(_("Emits 1500 Hz carrier to enable rig/antenna tuning.\nRight click for more options"));
    sbSizer5->Add(m_btnTogTune, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);
    m_btnTogTune->Enable(false);

    //------------------------------
    // Voice Keyer Toggle
    //------------------------------
    m_togBtnVoiceKeyer = new wxToggleButton(controlBox, wxID_ANY, _("Start Voice &Keyer"), wxDefaultPosition, wxDefaultSize, 0);
    m_togBtnVoiceKeyer->SetToolTip(_("Toggle Voice Keyer. Right-click for additional options."));
    sbSizer5->Add(m_togBtnVoiceKeyer, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);

    //------------------------------
    // PTT button: Toggle Transmit/Receive mode
    //------------------------------
    m_btnTogPTT = new wxToggleButton(controlBox, wxID_ANY, _("&XMIT"), wxDefaultPosition, wxDefaultSize, 0);
    m_btnTogPTT->SetToolTip(_("Switch between Receive and Transmit. Right-click for additional options."));
    sbSizer5->Add(m_btnTogPTT, 0, static_cast<int>(wxALL) | static_cast<int>(wxEXPAND), 5);

    rightSizer->Add(sbSizer5, 0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 2);

    bSizer1->Add(rightSizer, 0, static_cast<int>(wxALL)|static_cast<int>(wxEXPAND), 3);
    
    m_panel->SetSizerAndFit(bSizer1);
    this->Layout();

    m_statusBar1 = this->CreateStatusBar(1, wxSTB_DEFAULT_STYLE, wxID_ANY);

    //=====================================================
    // End of layout
    //=====================================================
    
    //-------------------
    // Window names, used by GUI test scripts to find controls (see test/gui).
    //-------------------
    SetName("main");
    m_gaugeSNR->SetName("main.gaugeSNR");
    m_textSNR->SetName("main.textSNR");
    m_ckboxSNR->SetName("main.ckboxSNR");
    m_gaugeLevel->SetName("main.gaugeLevel");
    m_textSync->SetName("main.textSync");
    m_textCurrentDecodeMode->SetName("main.textCurrentDecodeMode");
    m_btnCenterRx->SetName("main.btnCenterRx");
    m_audioRecord->SetName("main.btnRecord");
    m_logQSO->SetName("main.btnLogQSO");
    m_reporterHidden->SetName("main.btnReporterHidden");
    m_BtnBerReset->SetName("main.btnBerReset");
    m_textBits->SetName("main.textBits");
    m_textErrors->SetName("main.textErrors");
    m_textBER->SetName("main.textBER");
    m_textResyncs->SetName("main.textResyncs");
    m_textClockOffset->SetName("main.textClockOffset");
    m_textFreqOffset->SetName("main.textFreqOffset");
    m_textSyncMetric->SetName("main.textSyncMetric");
    m_textCodec2Var->SetName("main.textCodec2Var");
    m_auiNbookCtrl->SetName("main.notebook");
    m_txtModeStatus->SetName("main.textModeStatus");
    m_BtnCallSignReset->SetName("main.btnCallSignReset");
    m_txtCtrlCallSign->SetName("main.textCallSign");
    m_cboLastReportedCallsigns->SetName("main.cboLastReportedCallsigns");
    m_lastReportedCallsignListView->SetName("main.listLastReportedCallsigns");
    m_btnTxLevelMM->SetName("main.btnTxLevelDecrBig");
    m_btnTxLevelM->SetName("main.btnTxLevelDecr");
    m_btnTxLevelP->SetName("main.btnTxLevelIncr");
    m_btnTxLevelPP->SetName("main.btnTxLevelIncrBig");
    m_txtTxLevelNum->SetName("main.textTxLevel");
    m_sliderMicSpkrLevel->SetName("main.sliderMicSpkrLevel");
    m_txtMicSpkrLevelNum->SetName("main.textMicSpkrLevel");
    m_cboReportFrequency->SetName("main.cboReportFrequency");
    m_reportFrequencyList->SetName("main.lbReportFrequencies");
    m_togBtnOnOff->SetName("main.btnOnOff");
    m_togBtnAnalog->SetName("main.btnAnalog");
    m_btnTogTune->SetName("main.btnTune");
    m_togBtnVoiceKeyer->SetName("main.btnVoiceKeyer");
    m_btnTogPTT->SetName("main.btnPTT");
    m_txLevelBox->SetName("main.boxTxLevel");
    micSpeakerBox->SetName("main.boxMicSpkrLevel");
    m_freqBox->SetName("main.boxReportFrequency");

    //-------------------
    // Tab ordering for accessibility
    //-------------------
    m_auiNbookCtrl->MoveBeforeInTabOrder(m_BtnCallSignReset);
    
    m_togBtnOnOff->MoveBeforeInTabOrder(m_togBtnAnalog);
    m_togBtnAnalog->MoveBeforeInTabOrder(m_btnTogTune);
    m_btnTogTune->MoveBeforeInTabOrder(m_togBtnVoiceKeyer);
    m_togBtnVoiceKeyer->MoveBeforeInTabOrder(m_btnTogPTT);
    
    //-------------------
    // Connect Events
    //-------------------
    this->Connect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(TopFrame::topFrame_OnClose));
    this->Connect(wxEVT_PAINT, wxPaintEventHandler(TopFrame::topFrame_OnPaint));
    this->Connect(wxEVT_SIZE, wxSizeEventHandler(TopFrame::topFrame_OnSize));
    this->Connect(wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::topFrame_OnUpdateUI));
    this->Connect(wxEVT_ACTIVATE, wxActivateEventHandler(TopFrame::OnActivateWindow));
    this->Connect(wxEVT_SYS_COLOUR_CHANGED, wxSysColourChangedEventHandler(TopFrame::OnSystemColorChanged));
    this->Connect(m_menuItemExit->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnExit));

    this->Connect(m_menuItemFreeDVReporter->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnToolsFreeDVReporter));
    this->Connect(m_menuItemFreeDVReporter->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnToolsFreeDVReporterUI));
    this->Connect(m_menuItemSetupWizard->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnToolsSetupWizard));
    this->Connect(m_menuItemSetupWizard->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnToolsSetupWizardUI));
    this->Connect(m_menuItemFilter->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnToolsFilter));
    this->Connect(m_menuItemFilter->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnToolsFilterUI));
    this->Connect(wxID_PREFERENCES, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnToolsOptions));
    this->Connect(wxID_PREFERENCES, wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnToolsOptionsUI));

    this->Connect(m_menuItemPlayFileFromRadio->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnPlayFileFromRadio));

    this->Connect(m_menuItemExportConfig->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnToolsExportConfig));
    this->Connect(m_menuItemExportConfig->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnToolsExportConfigUI));
    this->Connect(m_menuItemImportConfig->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnToolsImportConfig));
    this->Connect(m_menuItemImportConfig->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnToolsImportConfigUI));
    this->Connect(m_menuItemLoadDefaultConfig->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnToolsLoadDefaultConfig));
    this->Connect(m_menuItemLoadDefaultConfig->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnToolsLoadDefaultConfigUI));

    this->Connect(m_menuItemHelpUpdates->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnHelpCheckUpdates));
    this->Connect(m_menuItemHelpUpdates->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnHelpCheckUpdatesUI));
    this->Connect(m_menuItemAbout->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnHelpAbout));
    this->Connect(m_menuItemHelpManual->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnHelpManual));
    this->Connect(m_menuItemHelpGetAssistance->GetId(), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnHelp));
    
    
    m_ckboxSNR->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(TopFrame::OnCheckSNRClick), NULL, this);

    m_audioRecord->Connect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTogBtnRecord), NULL, this);

    m_logQSO->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnLogQSO), NULL, this);
    
    m_togBtnOnOff->Connect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTogBtnOnOff), NULL, this);
    m_togBtnAnalog->Connect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTogBtnAnalogClick), NULL, this);
    m_togBtnVoiceKeyer->Connect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTogBtnVoiceKeyerClick), NULL, this);
    m_btnTogPTT->Connect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTogBtnPTT), NULL, this);

#if defined(__WXGTK__)
    // wxGTK fires wxEVT_CONTEXT_MENU on button-press for these widgets,
    // causing GTK to dismiss PopupMenu on button release; use RIGHT_UP
    // instead. MSW/OSX are unaffected (MSW generates the event on
    // button-up already; OSX uses ctrl-click), so this is GTK-specific.
    // Confirmed present on both wxGTK 3.2 and 3.3+, unlike the windowless
    // widget case below, so no version gate here.
    m_togBtnVoiceKeyer->Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent&) { wxContextMenuEvent ctx; OnTogBtnVoiceKeyerRightClick(ctx); });
    m_btnTogPTT->Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent&) { wxContextMenuEvent ctx; OnTogBtnPTTRightClick(ctx); });
#else
    m_togBtnVoiceKeyer->Connect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(TopFrame::OnTogBtnVoiceKeyerRightClick), NULL, this);
    m_btnTogPTT->Connect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(TopFrame::OnTogBtnPTTRightClick), NULL, this);
#endif

    m_BtnCallSignReset->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnCallSignReset), NULL, this);
    m_BtnBerReset->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnBerReset), NULL, this);
    m_btnCenterRx->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnCenterRx), NULL, this);

        
    m_btnTxLevelMM->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTxLevelDecrBig), NULL, this);
    m_btnTxLevelM->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTxLevelDecr), NULL, this);
    m_btnTxLevelP->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTxLevelIncr), NULL, this);
    m_btnTxLevelPP->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTxLevelIncrBig), NULL, this);

    m_txLevelBox->Connect(wxEVT_MOUSEWHEEL, wxMouseEventHandler(TopFrame::OnTxLevelMouseWheel), NULL, this);
    m_txtTxLevelNum->Connect(wxEVT_MOUSEWHEEL, wxMouseEventHandler(TopFrame::OnTxLevelMouseWheel), NULL, this);
    m_btnTxLevelMM->Connect(wxEVT_MOUSEWHEEL, wxMouseEventHandler(TopFrame::OnTxLevelMouseWheel), NULL, this);
    m_btnTxLevelM->Connect(wxEVT_MOUSEWHEEL, wxMouseEventHandler(TopFrame::OnTxLevelMouseWheel), NULL, this);
    m_btnTxLevelP->Connect(wxEVT_MOUSEWHEEL, wxMouseEventHandler(TopFrame::OnTxLevelMouseWheel), NULL, this);
    m_btnTxLevelPP->Connect(wxEVT_MOUSEWHEEL, wxMouseEventHandler(TopFrame::OnTxLevelMouseWheel), NULL, this);
    m_btnTogTune->Connect(wxEVT_MOUSEWHEEL, wxMouseEventHandler(TopFrame::OnTxLevelMouseWheel), NULL, this);

    // Right-click menu on the frequency box (adding it to the list), in
    // place of the text entry's own. GTK opens the entry's menu on
    // button-press, so take the press and show ours on release.
    for (wxWindow* window : { static_cast<wxWindow*>(m_cboReportFrequency),
                              static_cast<wxWindow*>(m_cboReportFrequency->GetTextCtrl()) })
    {
#if defined(__WXGTK__)
        window->Bind(wxEVT_RIGHT_DOWN, [](wxMouseEvent&) { });
        window->Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent&) { wxContextMenuEvent ctx; OnReportFrequencyContextMenu(ctx); });
#else
        window->Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent& ctx) { OnReportFrequencyContextMenu(ctx); });
#endif
    }

#if wxCHECK_VERSION(3, 3, 0) && defined(__WXGTK__)
    // wxGTK 3.3+ fires wxEVT_CONTEXT_MENU on button-press for these widget types,
    // causing GTK to dismiss PopupMenu on button release; use RIGHT_UP instead.
    // MSW/OSX are unaffected (MSW generates the event on button-up already;
    // OSX uses ctrl-click), so this is GTK-specific.
    m_txLevelBox->Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent&) { wxContextMenuEvent ctx; OnTxLevelContextMenu(ctx); });
    m_txtTxLevelNum->Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent&) { wxContextMenuEvent ctx; OnTxLevelContextMenu(ctx); });
    m_btnTogTune->Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent&) { wxContextMenuEvent ctx; OnTuneAttenContextMenu(ctx); });
#else
    // wxGTK < 3.3 does not generate RIGHT_UP for windowless widget types
    // (wxStaticBox, wxStaticText); CONTEXT_MENU works without the dismiss issue.
    // (Also used as-is on MSW/OSX regardless of wx version -- see above.)
    m_txLevelBox->Connect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(TopFrame::OnTxLevelContextMenu), NULL, this);
    m_txtTxLevelNum->Connect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(TopFrame::OnTxLevelContextMenu), NULL, this);
    m_btnTogTune->Connect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(TopFrame::OnTuneAttenContextMenu), NULL, this);
#endif

    m_sliderMicSpkrLevel->Connect(wxEVT_SCROLL_TOP, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Connect(wxEVT_SCROLL_BOTTOM, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Connect(wxEVT_SCROLL_LINEUP, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Connect(wxEVT_SCROLL_LINEDOWN, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Connect(wxEVT_SCROLL_PAGEUP, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Connect(wxEVT_SCROLL_PAGEDOWN, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Connect(wxEVT_SCROLL_THUMBTRACK, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Connect(wxEVT_SCROLL_THUMBRELEASE, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Connect(wxEVT_SCROLL_BOTTOM, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Connect(wxEVT_SCROLL_CHANGED, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Connect(wxEVT_SCROLL_TOP, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Connect(wxEVT_MIDDLE_DOWN, wxMouseEventHandler(TopFrame::OnResetMicSpkrLevel), NULL, this);
    m_txtMicSpkrLevelNum->Connect(wxEVT_MIDDLE_DOWN, wxMouseEventHandler(TopFrame::OnResetMicSpkrLevel), NULL, this);

    m_cboReportFrequency->Connect(wxEVT_TEXT_ENTER, wxCommandEventHandler(TopFrame::OnChangeReportFrequency), NULL, this);
    m_cboReportFrequency->Connect(wxEVT_TEXT, wxCommandEventHandler(TopFrame::OnChangeReportFrequencyVerify), NULL, this);
    // A frequency picked from the list applies at once, as wxEVT_COMBOBOX
    // did for the native combo (typing waits for Enter or leaving the box).
    m_cboReportFrequency->Connect(wxEVT_COMBOBOX_CLOSEUP, wxCommandEventHandler(TopFrame::OnChangeReportFrequency), NULL, this);
    m_cboReportFrequency->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(TopFrame::OnReportFrequencySetFocus), NULL, this);
    m_cboReportFrequency->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(TopFrame::OnReportFrequencyKillFocus), NULL, this);
    
    m_cboLastReportedCallsigns->Connect(wxEVT_COMBOBOX_DROPDOWN, wxCommandEventHandler(TopFrame::OnOpenCallsignList), NULL, this);
    m_cboLastReportedCallsigns->Connect(wxEVT_COMBOBOX_CLOSEUP, wxCommandEventHandler(TopFrame::OnCloseCallsignList), NULL, this);
    m_cboLastReportedCallsigns->Connect(wxEVT_RIGHT_DOWN, wxMouseEventHandler(TopFrame::OnRightClickCallsignList), NULL, this);

    m_reporterHidden->Connect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnToggleReporterVisibility), NULL, this);

    m_btnTogTune->Connect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTogBtnTune), NULL, this);
}

TopFrame::~TopFrame()
{
    //-------------------
    // Disconnect Events
    //-------------------   
    this->Disconnect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(TopFrame::topFrame_OnClose));
    this->Disconnect(wxEVT_PAINT, wxPaintEventHandler(TopFrame::topFrame_OnPaint));
    this->Disconnect(wxEVT_SIZE, wxSizeEventHandler(TopFrame::topFrame_OnSize));
    this->Disconnect(wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::topFrame_OnUpdateUI));
    this->Disconnect(wxEVT_ACTIVATE, wxActivateEventHandler(TopFrame::OnActivateWindow));
    this->Disconnect(ID_EXIT, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnExit));
    this->Disconnect(wxID_ANY, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnToolsSetupWizard));
    this->Disconnect(wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnToolsSetupWizardUI));
    this->Disconnect(wxID_ANY, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnToolsFilter));
    this->Disconnect(wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnToolsFilterUI));
    this->Disconnect(wxID_PREFERENCES, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnToolsOptions));
    this->Disconnect(wxID_PREFERENCES, wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnToolsOptionsUI));

    this->Disconnect(wxID_ANY, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnPlayFileFromRadio));

    this->Disconnect(wxID_ANY, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnToolsExportConfig));
    this->Disconnect(wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnToolsExportConfigUI));
    this->Disconnect(wxID_ANY, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnToolsImportConfig));
    this->Disconnect(wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnToolsImportConfigUI));
    this->Disconnect(wxID_ANY, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnToolsLoadDefaultConfig));
    this->Disconnect(wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnToolsLoadDefaultConfigUI));

    this->Disconnect(wxID_ANY, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnHelpCheckUpdates));
    this->Disconnect(wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TopFrame::OnHelpCheckUpdatesUI));
    this->Disconnect(ID_ABOUT, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnHelpAbout));
    this->Disconnect(wxID_ANY, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TopFrame::OnHelpManual));
    
    m_togBtnOnOff->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTogBtnOnOff), NULL, this);
    m_togBtnAnalog->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTogBtnAnalogClick), NULL, this);
    m_togBtnVoiceKeyer->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTogBtnVoiceKeyerClick), NULL, this);
    m_btnTogPTT->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTogBtnPTT), NULL, this);
#if !defined(__WXGTK__)
    m_togBtnVoiceKeyer->Disconnect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(TopFrame::OnTogBtnVoiceKeyerRightClick), NULL, this);
    m_btnTogPTT->Disconnect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(TopFrame::OnTogBtnPTTRightClick), NULL, this);
#endif
    
    m_btnCenterRx->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnCenterRx), NULL, this);

    m_audioRecord->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTogBtnRecord), NULL, this);
    
    m_logQSO->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnLogQSO), NULL, this);
        
    m_btnTxLevelMM->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTxLevelDecrBig), NULL, this);
    m_btnTxLevelM->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTxLevelDecr), NULL, this);
    m_btnTxLevelP->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTxLevelIncr), NULL, this);
    m_btnTxLevelPP->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTxLevelIncrBig), NULL, this);
#if !(wxCHECK_VERSION(3, 3, 0) && defined(__WXGTK__))
    m_txLevelBox->Disconnect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(TopFrame::OnTxLevelContextMenu), NULL, this);
    m_txtTxLevelNum->Disconnect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(TopFrame::OnTxLevelContextMenu), NULL, this);
    m_btnTogTune->Disconnect(wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(TopFrame::OnTuneAttenContextMenu), NULL, this);
#endif

    m_sliderMicSpkrLevel->Disconnect(wxEVT_SCROLL_TOP, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Disconnect(wxEVT_SCROLL_BOTTOM, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Disconnect(wxEVT_SCROLL_LINEUP, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Disconnect(wxEVT_SCROLL_LINEDOWN, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Disconnect(wxEVT_SCROLL_PAGEUP, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Disconnect(wxEVT_SCROLL_PAGEDOWN, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Disconnect(wxEVT_SCROLL_THUMBTRACK, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Disconnect(wxEVT_SCROLL_THUMBRELEASE, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Disconnect(wxEVT_SCROLL_BOTTOM, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Disconnect(wxEVT_SCROLL_CHANGED, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Disconnect(wxEVT_SCROLL_TOP, wxScrollEventHandler(TopFrame::OnChangeMicSpkrLevel), NULL, this);
    m_sliderMicSpkrLevel->Disconnect(wxEVT_MIDDLE_DOWN, wxMouseEventHandler(TopFrame::OnResetMicSpkrLevel), NULL, this);
    m_txtMicSpkrLevelNum->Disconnect(wxEVT_MIDDLE_DOWN, wxMouseEventHandler(TopFrame::OnResetMicSpkrLevel), NULL, this);
    
    m_cboReportFrequency->Disconnect(wxEVT_TEXT_ENTER, wxCommandEventHandler(TopFrame::OnChangeReportFrequency), NULL, this);
    m_cboReportFrequency->Disconnect(wxEVT_TEXT, wxCommandEventHandler(TopFrame::OnChangeReportFrequencyVerify), NULL, this);
    m_cboReportFrequency->Disconnect(wxEVT_COMBOBOX_CLOSEUP, wxCommandEventHandler(TopFrame::OnChangeReportFrequency), NULL, this);
    
    m_cboReportFrequency->Disconnect(wxEVT_SET_FOCUS, wxFocusEventHandler(TopFrame::OnReportFrequencySetFocus), NULL, this);
    m_cboReportFrequency->Disconnect(wxEVT_KILL_FOCUS, wxFocusEventHandler(TopFrame::OnReportFrequencyKillFocus), NULL, this);

    m_cboLastReportedCallsigns->Disconnect(wxEVT_RIGHT_DOWN, wxMouseEventHandler(TopFrame::OnRightClickCallsignList), NULL, this);
    m_cboLastReportedCallsigns->Disconnect(wxEVT_COMBOBOX_DROPDOWN, wxCommandEventHandler(TopFrame::OnOpenCallsignList), NULL, this);
    m_cboLastReportedCallsigns->Disconnect(wxEVT_COMBOBOX_CLOSEUP, wxCommandEventHandler(TopFrame::OnCloseCallsignList), NULL, this);

    m_reporterHidden->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnToggleReporterVisibility), NULL, this);

    m_btnTogTune->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(TopFrame::OnTogBtnTune), NULL, this);
}

void TopFrame::setVoiceKeyerButtonLabel_(wxString filename)
{
    wxString vkLabel = _("Start Voice &Keyer");
    int vkLabelWidth = 0;
    int filenameWidth = 0;
    int tmp = 0;
    
    wxSize buttonSize = m_togBtnVoiceKeyer->GetSize();
    vkLabelWidth = buttonSize.GetWidth() * 0.95;
    m_togBtnVoiceKeyer->GetTextExtent(filename, &filenameWidth, &tmp);
        
    // Truncate filename as required to ensure button isn't made wider than needed.
    bool isTruncated = false;
    while (filename.size() > 1 && filenameWidth > vkLabelWidth)
    {
        isTruncated = true;
        filename = filename.Mid(0, filename.size() - 1);
        
        wxString tmpString = filename + wxT("...");
        m_togBtnVoiceKeyer->GetTextExtent(tmpString, &filenameWidth, &tmp);
    }
    
    if (filename.size() > 0)
    {
        m_togBtnVoiceKeyer->SetLabel(vkLabel + wxT("\n") + filename + (isTruncated ? wxT("...") : wxT("")));
    }
    else
    {
        m_togBtnVoiceKeyer->SetLabel(vkLabel);
    }
    
    // Resize button height as needed.
    wxSize currentSize = m_togBtnVoiceKeyer->GetSize();
    wxSize bestSize = m_togBtnVoiceKeyer->GetBestSize();
    currentSize.SetHeight(bestSize.GetHeight());
    m_togBtnVoiceKeyer->SetSize(currentSize);
    m_togBtnVoiceKeyer->Refresh();
    
    // XXX - wxWidgets doesn't handle button height changes properly until the user resizes 
    // the window (even if only by a pixel or two). As a really hacky workaround, we 
    // emulate this behavior when changing the button height.
    wxSize winSize = GetSize();
    SetSize(winSize.GetWidth(), winSize.GetHeight());
    SetSize(winSize.GetWidth(), winSize.GetHeight() - 1);
    SetSize(winSize.GetWidth(), winSize.GetHeight());
        
    log_info("Set voice keyer button label to %s", (const char*)m_togBtnVoiceKeyer->GetLabel().ToUTF8());
}
