//==========================================================================
// Name:            test_agent.cpp
//
// Purpose:         Lets a GUI test script drive FreeDV over a local socket.
//
// License:
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU Lesser General Public License version 2.1,
//  as published by the Free Software Foundation.  This program is
//  distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
//  License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//
//==========================================================================

#include <algorithm>
#include <map>
#include <cstdlib>

#include <wx/bookctrl.h>
#include <wx/clrpicker.h>
#include <wx/combo.h>
#include <wx/dataview.h>
#include <wx/clipbrd.h>
#include <wx/evtloop.h>
#include <wx/iconbndl.h>
#include <wx/image.h>
#include <wx/listctrl.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/tglbtn.h>

#if defined(__WXMSW__)
#include <windows.h>
#endif // defined(__WXMSW__)

#include "test_agent.h"
#include "main.h"
#include "freedv_interface.h"
#include "gui/controls/LevelMeterLed.h"

extern std::atomic<bool> g_tx;
extern std::atomic<int> g_analog;
extern std::atomic<float> g_RxFreqOffsetHz;
extern std::atomic<float> g_TxFreqOffsetHz;
extern std::atomic<bool> g_loopPlayFileFromRadio;
extern std::atomic<bool> isModemRunning;
extern std::atomic<float> g_agcAppliedGainDb;
extern FreeDVInterface freedvInterface;

#if defined(__APPLE__)
// See test_agent_osx.mm.
void TestAgentActivateApp();
#endif // defined(__APPLE__)

TestAgent* TestAgent::Instance_ = nullptr;

static std::string ToUtf8_(wxString const& str)
{
    return std::string(str.utf8_str());
}

static void AddString_(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, wxString const& str)
{
    yyjson_mut_obj_add_strcpy(doc, obj, key, ToUtf8_(str).c_str());
}

// Returns the string attached to the given item (e.g. the internal name of
// the audio device that the item displays), if it has one.
static bool GetItemData_(wxItemContainer* container, unsigned int index, wxString& data)
{
    if (!container->HasClientObjectData())
    {
        return false;
    }

    auto stringData = dynamic_cast<wxStringClientData*>(container->GetClientObject(index));
    if (stringData == nullptr)
    {
        return false;
    }

    data = stringData->GetData();
    return true;
}

TestAgent::TestAgent(int port)
    : closing_(false)
    , popupChoiceArmed_(false)
    , quitAttemptsLeft_(0)
{
    Instance_ = this;

    g_wxMessageBoxTestHook = [this](const wxString& message, const wxString& caption, int style) {
        return onMessageBox_(message, caption, style);
    };
    g_popupMenuTestHook = [this](wxWindow* window, wxMenu* menu) {
        onPopupMenu_(window, menu);
    };
    g_pathDialogTestHook = [this](const wxString& title, wxString& path) {
        return onPathDialog_(title, path);
    };
    g_browserTestHook = [this](const wxString& url) {
        log_info("Web page not opened for GUI test: %s", (const char*)url.ToUTF8());
        browserUrls_.push_back(url);
    };

    log_info("Connecting to GUI test controller on port %d", port);
    connect("127.0.0.1", port, false);
}

TestAgent::~TestAgent()
{
    g_wxMessageBoxTestHook = nullptr;
    g_popupMenuTestHook = nullptr;
    g_pathDialogTestHook = nullptr;
    g_browserTestHook = nullptr;
    Instance_ = nullptr;

    // Must be done here rather than in the base class, which can no longer
    // call our handlers by the time its destructor runs.
    closing_.store(true, std::memory_order_release);
    auto fut = disconnect();
    fut.wait();
    waitForAllTasksComplete_();
}

void TestAgent::onConnect_()
{
    log_info("Connected to GUI test controller");
}

void TestAgent::onDisconnect_()
{
    if (!closing_.load(std::memory_order_acquire))
    {
        // The test script went away (or was never there). Nothing else is
        // going to close FreeDV, so don't linger.
        log_error("Lost connection to GUI test controller, exiting");
        std::_Exit(1);
    }
}

void TestAgent::onReceive_(char* buf, int length)
{
    lineBuffer_.append(buf, length);

    size_t pos = 0;
    while ((pos = lineBuffer_.find('\n')) != std::string::npos)
    {
        std::string line = lineBuffer_.substr(0, pos);
        lineBuffer_.erase(0, pos + 1);

        wxTheApp->CallAfter([line = std::move(line)]() {
            if (Instance_ != nullptr)
            {
                Instance_->handleLine_(line);
            }
        });
    }
}

int TestAgent::onMessageBox_(const wxString& message, const wxString& caption, int style)
{
    log_info("Message box suppressed for GUI test: %s", (const char*)message.ToUTF8());

    int answer = 0;
    {
        std::unique_lock<std::mutex> lk(messageBoxMutex_);
        messageBoxes_.push_back({ message, caption, style });
        if (!messageBoxAnswers_.empty())
        {
            answer = messageBoxAnswers_.front();
            messageBoxAnswers_.pop_front();
        }
    }

    // Unless the script has said otherwise, answer as if the user had pressed
    // the default button.
    if (answer != 0)
    {
        return answer;
    }
    if (style & wxYES_NO)
    {
        return (style & wxNO_DEFAULT) ? wxNO : wxYES;
    }
    return wxOK;
}

bool TestAgent::onPathDialog_(const wxString& title, wxString& path)
{
    log_info("File chooser suppressed for GUI test: %s", (const char*)title.ToUTF8());
    pathDialogs_.push_back(title);

    // Unless the script has said what to choose, it's as if the user cancelled.
    g_pathDialogTestOptions.clear();
    if (pathDialogAnswers_.empty())
    {
        return false;
    }

    auto answer = pathDialogAnswers_.front();
    pathDialogAnswers_.pop_front();
    path = answer.path;
    g_pathDialogTestOptions = answer.options;
    return answer.choose;
}

void TestAgent::onPopupMenu_(wxWindow* window, wxMenu* menu)
{
    PopupRecord record;
    record.shown = true;
    record.window = window->GetName();

    wxMenuItem* chosenItem = nullptr;
    for (auto item : menu->GetMenuItems())
    {
        if (item->IsSeparator())
        {
            continue;
        }

        wxString label = item->GetItemLabelText();
        record.items.push_back({ label, item->IsEnabled(), item->IsCheckable() && item->IsChecked() });
        if (popupChoiceArmed_ && chosenItem == nullptr && label == popupChoice_)
        {
            chosenItem = item;
        }
    }

    if (popupChoiceArmed_)
    {
        if (chosenItem == nullptr)
        {
            record.error = wxString::Format("the popup menu has no item '%s'", popupChoice_);
        }
        else if (!chosenItem->IsEnabled())
        {
            record.error = wxString::Format("popup menu item '%s' is disabled", popupChoice_);
            chosenItem = nullptr;
        }
        else
        {
            record.chosen = popupChoice_;
        }
    }
    popupChoiceArmed_ = false;

    // Recorded before making the choice, which may not return for some time
    // (e.g. if it opens a dialog).
    lastPopup_ = record;

    if (chosenItem != nullptr)
    {
        if (chosenItem->IsCheckable())
        {
            chosenItem->Check(!chosenItem->IsChecked());
        }

        // As wxWindow::PopupMenu() does, so that the event reaches the window.
        menu->SetInvokingWindow(window);
        menu->SendEvent(chosenItem->GetId(), chosenItem->IsCheckable() ? chosenItem->IsChecked() : -1);
        menu->SetInvokingWindow(nullptr);
    }
}

// Call before doing something that's expected to display a popup menu. If
// `choose` is given, that item will be selected from it.
void TestAgent::expectPopupMenu_(yyjson_val* request)
{
    lastPopup_ = PopupRecord();

    const char* choice = yyjson_get_str(yyjson_obj_get(request, "choose"));
    popupChoiceArmed_ = (choice != nullptr);
    popupChoice_ = popupChoiceArmed_ ? wxString::FromUTF8(choice) : wxString();
}

// Call once whatever was expected to display a popup menu has been done.
void TestAgent::popupMenuExpected_()
{
    if (!lastPopup_.shown)
    {
        lastPopup_.error = "no popup menu appeared";
    }
    popupChoiceArmed_ = false;
}

void TestAgent::reply_(yyjson_mut_doc* doc)
{
    size_t length = 0;
    char* json = yyjson_mut_write(doc, 0, &length);
    if (json != nullptr)
    {
        std::string line(json, length);
        line += '\n';
        send(line.c_str(), line.size());
        free(json);
    }
    yyjson_mut_doc_free(doc);
}

void TestAgent::replyOk_(yyjson_val* id, yyjson_mut_doc* doc, yyjson_mut_val* result)
{
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_val(doc, root, "id", (id != nullptr) ? yyjson_val_mut_copy(doc, id) : yyjson_mut_null(doc));
    yyjson_mut_obj_add_bool(doc, root, "ok", true);
    yyjson_mut_obj_add_val(doc, root, "result", (result != nullptr) ? result : yyjson_mut_null(doc));
    reply_(doc);
}

void TestAgent::replyError_(yyjson_val* id, wxString const& error)
{
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_val(doc, root, "id", (id != nullptr) ? yyjson_val_mut_copy(doc, id) : yyjson_mut_null(doc));
    yyjson_mut_obj_add_bool(doc, root, "ok", false);
    AddString_(doc, root, "error", error);
    reply_(doc);
}

void TestAgent::handleLine_(std::string const& line)
{
    yyjson_doc* requestDoc = yyjson_read(line.c_str(), line.size(), 0);
    yyjson_val* request = (requestDoc != nullptr) ? yyjson_doc_get_root(requestDoc) : nullptr;
    if (!yyjson_is_obj(request))
    {
        replyError_(nullptr, "request is not a JSON object");
        yyjson_doc_free(requestDoc);
        return;
    }

    yyjson_val* id = yyjson_obj_get(request, "id");
    const char* cmdStr = yyjson_get_str(yyjson_obj_get(request, "cmd"));
    std::string cmd = (cmdStr != nullptr) ? cmdStr : "";

    if (cmd == "ping")
    {
        replyOk_(id, yyjson_mut_doc_new(nullptr));
    }
    else if (cmd == "list_windows")
    {
        commandListWindows_(id);
    }
    else if (cmd == "list_widgets")
    {
        commandListWidgets_(id, request);
    }
    else if (cmd == "get")
    {
        commandGet_(id, request);
    }
    else if (cmd == "click")
    {
        commandClick_(id, request);
    }
    else if (cmd == "set_value")
    {
        commandSetValue_(id, request);
    }
    else if (cmd == "close")
    {
        commandClose_(id, request);
    }
    else if (cmd == "context_menu")
    {
        commandContextMenu_(id, request);
    }
    else if (cmd == "get_popup_menu")
    {
        commandGetPopupMenu_(id);
    }
    else if (cmd == "combo_popup")
    {
        commandComboPopup_(id, request);
    }
    else if (cmd == "data_view")
    {
        commandDataView_(id, request);
    }
    else if (cmd == "activate")
    {
        commandActivate_(id, request);
    }
    else if (cmd == "menu")
    {
        commandMenu_(id, request);
    }
    else if (cmd == "get_state")
    {
        commandGetState_(id);
    }
    else if (cmd == "key")
    {
        commandKey_(id, request);
    }
    else if (cmd == "path_dialogs")
    {
        commandPathDialogs_(id, request);
    }
    else if (cmd == "browser_urls")
    {
        commandBrowserUrls_(id, request);
    }
    else if (cmd == "mouse")
    {
        commandMouse_(id, request);
    }
    else if (cmd == "clipboard")
    {
        commandClipboard_(id);
    }
    else if (cmd == "focus")
    {
        commandFocus_(id, request);
    }
    else if (cmd == "system_colours_changed")
    {
        // As when the desktop switches between its light and dark themes.
        replyOk_(id, yyjson_mut_doc_new(nullptr));
        std::vector<wxWindow*> topLevels;
        for (auto window : wxTopLevelWindows)
        {
            topLevels.push_back(window);
        }
        for (auto window : topLevels)
        {
            wxSysColourChangedEvent event;
            event.SetEventObject(window);
            window->ProcessWindowEvent(event);
        }
    }
    else if (cmd == "message_boxes")
    {
        commandMessageBoxes_(id, request);
    }
    else if (cmd == "quit")
    {
        commandQuit_(id);
    }
    else
    {
        replyError_(id, wxString::Format("unknown command '%s'", wxString::FromUTF8(cmd.c_str())));
    }

    yyjson_doc_free(requestDoc);
}

static void AppendDescendants_(std::vector<wxWindow*>& list, wxWindow* window)
{
    for (auto child : window->GetChildren())
    {
        list.push_back(child);
        AppendDescendants_(list, child);
    }
}

// Finds a window by the name given to it with SetName(). Windows that have
// none (e.g. dialogs provided by wxWidgets) can be found with
// "@<window title>", and the controls inside them with
// "@<window title>/<label>" or "@<window title>/<class>".
static wxWindow* FindWindow_(wxString const& name)
{
    if (!name.StartsWith("@"))
    {
        // A window that's been closed can stay around (not shown) for a
        // while before it's destroyed, by which time another of the same
        // name may have been opened: that one is meant, so windows being
        // shown come first, newest first.
        for (auto iter = wxTopLevelWindows.rbegin(); iter != wxTopLevelWindows.rend(); ++iter)
        {
            wxWindow* topLevel = *iter;
            if (!topLevel->IsShown() || topLevel->IsBeingDeleted())
            {
                continue;
            }
            if (topLevel->GetName() == name)
            {
                return topLevel;
            }
            wxWindow* found = wxWindow::FindWindowByName(name, topLevel);
            if (found != nullptr)
            {
                return found;
            }
        }
        return wxWindow::FindWindowByName(name);
    }

    wxString selector;
    wxString title = name.Mid(1).BeforeFirst('/', &selector);

    wxWindow* topLevel = nullptr;
    for (auto window : wxTopLevelWindows)
    {
        auto candidate = wxDynamicCast(window, wxTopLevelWindow);
        if (candidate != nullptr && candidate->IsShown() && candidate->GetTitle() == title)
        {
            topLevel = candidate;
        }
    }
    if (topLevel == nullptr || selector.empty())
    {
        return topLevel;
    }

    std::vector<wxWindow*> descendants;
    AppendDescendants_(descendants, topLevel);
    for (auto window : descendants)
    {
        if (wxControl::RemoveMnemonics(window->GetLabel()) == selector)
        {
            return window;
        }
    }
    for (auto window : descendants)
    {
        if (selector == window->GetClassInfo()->GetClassName())
        {
            return window;
        }
    }
    return nullptr;
}

// Looks up the window named by the request, replying with an error if there
// isn't one.
wxWindow* TestAgent::findWindow_(yyjson_val* id, yyjson_val* request)
{
    const char* name = yyjson_get_str(yyjson_obj_get(request, "name"));
    if (name == nullptr)
    {
        replyError_(id, "no window name given");
        return nullptr;
    }

    wxWindow* window = FindWindow_(wxString::FromUTF8(name));
    if (window == nullptr)
    {
        replyError_(id, wxString::Format("no window named '%s'", wxString::FromUTF8(name)));
    }
    return window;
}

// Returns the rows of a list in the order in which they're displayed.
static std::vector<wxDataViewItem> GetDisplayedRows_(wxDataViewCtrl* dataView)
{
    std::vector<wxDataViewItem> rows;

    wxDataViewModel* model = dataView->GetModel();
    if (model == nullptr)
    {
        return rows;
    }

    wxDataViewItemArray children;
    model->GetChildren(wxDataViewItem(), children);
    for (auto& child : children)
    {
        rows.push_back(child);
    }

    // The control doesn't say what order it's displaying rows in, but it
    // gets that order from the model in the same way.
    wxDataViewColumn* sortColumn = dataView->GetSortingColumn();
    if (sortColumn != nullptr || model->HasDefaultCompare())
    {
        unsigned int column = (sortColumn != nullptr) ? sortColumn->GetModelColumn() : (unsigned int)wxNOT_FOUND;
        bool ascending = (sortColumn != nullptr) ? sortColumn->IsSortOrderAscending() : true;
        // Rows that compare as equal stay in the order that the model gave
        // them. (Not std::stable_sort: with some standard libraries, that
        // uses something deprecated and so doesn't build without warnings.)
        std::vector<size_t> order(rows.size());
        for (size_t index = 0; index < order.size(); index++)
        {
            order[index] = index;
        }
        std::sort(order.begin(), order.end(), [&](size_t first, size_t second) {
            int result = model->Compare(rows[first], rows[second], column, ascending);
            return (result != 0) ? (result < 0) : (first < second);
        });

        std::vector<wxDataViewItem> sorted;
        sorted.reserve(rows.size());
        for (auto index : order)
        {
            sorted.push_back(rows[index]);
        }
        rows = std::move(sorted);
    }

    return rows;
}

static void DescribeDataView_(yyjson_mut_doc* doc, yyjson_mut_val* obj, wxDataViewCtrl* dataView, bool includeRows)
{
    wxDataViewModel* model = dataView->GetModel();

    yyjson_mut_val* columns = yyjson_mut_arr(doc);
    for (unsigned int index = 0; index < dataView->GetColumnCount(); index++)
    {
        wxDataViewColumn* column = dataView->GetColumn(index);
        yyjson_mut_val* columnObj = yyjson_mut_obj(doc);
        AddString_(doc, columnObj, "title", column->GetTitle());
        yyjson_mut_obj_add_int(doc, columnObj, "id", column->GetModelColumn());
        yyjson_mut_obj_add_int(doc, columnObj, "position", dataView->GetColumnPosition(column));
        yyjson_mut_obj_add_bool(doc, columnObj, "shown", !column->IsHidden());
        yyjson_mut_obj_add_str(
            doc, columnObj, "sort",
            !column->IsSortKey() ? "none" : (column->IsSortOrderAscending() ? "ascending" : "descending"));
        yyjson_mut_arr_append(columns, columnObj);
    }
    yyjson_mut_obj_add_val(doc, obj, "columns", columns);

    auto rows = GetDisplayedRows_(dataView);
    wxDataViewItem selection = dataView->GetSelection();
    int selectedRow = wxNOT_FOUND;
    for (size_t row = 0; row < rows.size(); row++)
    {
        if (rows[row] == selection)
        {
            selectedRow = (int)row;
        }
    }
    yyjson_mut_obj_add_int(doc, obj, "selection", selectedRow);
    yyjson_mut_obj_add_int(doc, obj, "rowCount", (int)rows.size());

    if (!includeRows || model == nullptr)
    {
        return;
    }

    yyjson_mut_val* rowsArr = yyjson_mut_arr(doc);
    for (auto& row : rows)
    {
        yyjson_mut_val* rowObj = yyjson_mut_obj(doc);

        // Keyed by column ID, as columns can be reordered.
        yyjson_mut_val* cells = yyjson_mut_obj(doc);
        for (unsigned int index = 0; index < dataView->GetColumnCount(); index++)
        {
            unsigned int columnId = dataView->GetColumn(index)->GetModelColumn();
            wxVariant value;
            model->GetValue(value, row, columnId);

            std::string key = std::to_string(columnId);
            yyjson_mut_obj_add(
                cells, yyjson_mut_strcpy(doc, key.c_str()),
                yyjson_mut_strcpy(doc, ToUtf8_(value.IsNull() ? wxString() : value.MakeString()).c_str()));
        }
        yyjson_mut_obj_add_val(doc, rowObj, "cells", cells);

        wxDataViewItemAttr attr;
        if (model->GetAttr(row, 0, attr))
        {
            if (attr.HasBackgroundColour())
            {
                AddString_(doc, rowObj, "background", attr.GetBackgroundColour().GetAsString(wxC2S_HTML_SYNTAX));
            }
            if (attr.HasColour())
            {
                AddString_(doc, rowObj, "foreground", attr.GetColour().GetAsString(wxC2S_HTML_SYNTAX));
            }
        }

        yyjson_mut_arr_append(rowsArr, rowObj);
    }
    yyjson_mut_obj_add_val(doc, obj, "rows", rowsArr);
}

// The icons a window has been given: each one's size, and its pixels (red,
// green, blue and alpha) in the middle, at the top left corner, and halfway
// from the middle to the left and to the top edges -- enough to tell FreeDV's icon from
// none, another, or one that's lost its transparency.
static yyjson_mut_val* DescribeIcons_(yyjson_mut_doc* doc, wxTopLevelWindow* window)
{
    yyjson_mut_val* icons = yyjson_mut_arr(doc);
    const wxIconBundle& bundle = window->GetIcons();
    for (size_t index = 0; index < bundle.GetIconCount(); index++)
    {
        wxIcon icon = bundle.GetIconByIndex(index);
        if (!icon.IsOk())
        {
            continue;
        }
        wxBitmap bitmap;
        bitmap.CopyFromIcon(icon);
        wxImage image = bitmap.ConvertToImage();
        if (!image.IsOk())
        {
            continue;
        }

        yyjson_mut_val* obj = yyjson_mut_obj(doc);
        yyjson_mut_val* size = yyjson_mut_arr(doc);
        yyjson_mut_arr_add_int(doc, size, image.GetWidth());
        yyjson_mut_arr_add_int(doc, size, image.GetHeight());
        yyjson_mut_obj_add_val(doc, obj, "size", size);

        auto pixel = [&](const char* name, int x, int y) {
            yyjson_mut_val* rgba = yyjson_mut_arr(doc);
            yyjson_mut_arr_add_int(doc, rgba, image.GetRed(x, y));
            yyjson_mut_arr_add_int(doc, rgba, image.GetGreen(x, y));
            yyjson_mut_arr_add_int(doc, rgba, image.GetBlue(x, y));
            yyjson_mut_arr_add_int(doc, rgba, image.HasAlpha() ? image.GetAlpha(x, y) : (image.HasMask() && image.IsTransparent(x, y) ? 0 : 255));
            yyjson_mut_obj_add_val(doc, obj, name, rgba);
        };
        pixel("center", image.GetWidth() / 2, image.GetHeight() / 2);
        pixel("corner", 0, 0);
        pixel("left", image.GetWidth() / 4, image.GetHeight() / 2);
        pixel("above", image.GetWidth() / 2, image.GetHeight() / 4);
        yyjson_mut_arr_append(icons, obj);
    }
    return icons;
}

yyjson_mut_val* TestAgent::describeWindow_(yyjson_mut_doc* doc, wxWindow* window, bool includeItems)
{
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    AddString_(doc, obj, "name", window->GetName());
    AddString_(doc, obj, "class", window->GetClassInfo()->GetClassName());
    yyjson_mut_obj_add_bool(doc, obj, "enabled", window->IsEnabled());
    yyjson_mut_obj_add_bool(doc, obj, "shown", window->IsShownOnScreen());
    wxSize clientSize = window->GetClientSize();
    yyjson_mut_val* sizeArray = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_int(doc, sizeArray, clientSize.GetWidth());
    yyjson_mut_arr_add_int(doc, sizeArray, clientSize.GetHeight());
    yyjson_mut_obj_add_val(doc, obj, "clientSize", sizeArray);

    if (auto topLevel = wxDynamicCast(window, wxTopLevelWindow))
    {
        AddString_(doc, obj, "label", topLevel->GetTitle());

        auto dialog = wxDynamicCast(window, wxDialog);
        yyjson_mut_obj_add_bool(doc, obj, "modal", dialog != nullptr && dialog->IsModal());
        yyjson_mut_obj_add_bool(doc, obj, "active", topLevel->IsActive());

        auto frame = wxDynamicCast(window, wxFrame);
        if (frame != nullptr && frame->GetStatusBar() != nullptr)
        {
            AddString_(doc, obj, "status", frame->GetStatusBar()->GetStatusText());
        }
        if (includeItems)
        {
            yyjson_mut_obj_add_val(doc, obj, "icons", DescribeIcons_(doc, topLevel));
        }
        return obj;
    }

    AddString_(doc, obj, "label", window->GetLabel());

    wxItemContainer* container = nullptr;
    if (auto toggle = wxDynamicCast(window, wxToggleButton))
    {
        yyjson_mut_obj_add_bool(doc, obj, "value", toggle->GetValue());
    }
    else if (auto checkBox = wxDynamicCast(window, wxCheckBox))
    {
        yyjson_mut_obj_add_bool(doc, obj, "value", checkBox->GetValue());
    }
    else if (auto radioButton = wxDynamicCast(window, wxRadioButton))
    {
        yyjson_mut_obj_add_bool(doc, obj, "value", radioButton->GetValue());
    }
    else if (auto textCtrl = wxDynamicCast(window, wxTextCtrl))
    {
        AddString_(doc, obj, "value", textCtrl->GetValue());
    }
    else if (auto comboBox = wxDynamicCast(window, wxComboBox))
    {
        // Must be checked before wxChoice, which wxComboBox derives from
        // on some platforms.
        AddString_(doc, obj, "value", comboBox->GetValue());
        container = comboBox;
    }
    else if (auto choice = wxDynamicCast(window, wxChoice))
    {
        AddString_(doc, obj, "value", choice->GetStringSelection());
        container = choice;
    }
    else if (auto comboCtrl = wxDynamicCast(window, wxComboCtrl))
    {
        AddString_(doc, obj, "value", comboCtrl->GetValue());
        // Whether its list has finished opening: until then (while it slides
        // open, on Windows) it ignores the mouse.
        yyjson_mut_obj_add_bool(doc, obj, "popupShown", comboCtrl->IsPopupShown());
    }
    else if (auto listBox = wxDynamicCast(window, wxListBox))
    {
        AddString_(doc, obj, "value", listBox->GetStringSelection());
        container = listBox;
    }
    else if (auto spinCtrl = wxDynamicCast(window, wxSpinCtrl))
    {
        yyjson_mut_obj_add_int(doc, obj, "value", spinCtrl->GetValue());
    }
    else if (auto dataView = wxDynamicCast(window, wxDataViewCtrl))
    {
        DescribeDataView_(doc, obj, dataView, includeItems);
    }
    else if (auto book = wxDynamicCast(window, wxBookCtrlBase))
    {
        yyjson_mut_obj_add_int(doc, obj, "value", book->GetSelection());

        yyjson_mut_val* pages = yyjson_mut_arr(doc);
        for (size_t page = 0; page < book->GetPageCount(); page++)
        {
            yyjson_mut_arr_add_strcpy(doc, pages, ToUtf8_(book->GetPageText(page)).c_str());
        }
        yyjson_mut_obj_add_val(doc, obj, "pages", pages);
    }
    else if (auto colourPicker = wxDynamicCast(window, wxColourPickerCtrl))
    {
        AddString_(doc, obj, "value", colourPicker->GetColour().GetAsString(wxC2S_HTML_SYNTAX));
    }
    else if (auto listCtrl = wxDynamicCast(window, wxListCtrl))
    {
        long selection = listCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        yyjson_mut_obj_add_int(doc, obj, "selection", selection);
        AddString_(doc, obj, "value", (selection >= 0) ? listCtrl->GetItemText(selection, 0) : wxString());

        if (includeItems)
        {
            // Each item's label is what's in its first column.
            yyjson_mut_val* items = yyjson_mut_arr(doc);
            for (long item = 0; item < listCtrl->GetItemCount(); item++)
            {
                yyjson_mut_val* itemObj = yyjson_mut_obj(doc);
                AddString_(doc, itemObj, "label", listCtrl->GetItemText(item, 0));

                yyjson_mut_val* columns = yyjson_mut_arr(doc);
                for (int column = 0; column < listCtrl->GetColumnCount(); column++)
                {
                    yyjson_mut_arr_add_strcpy(doc, columns, ToUtf8_(listCtrl->GetItemText(item, column)).c_str());
                }
                yyjson_mut_obj_add_val(doc, itemObj, "columns", columns);
                yyjson_mut_arr_append(items, itemObj);
            }
            yyjson_mut_obj_add_val(doc, obj, "items", items);
        }
    }
    else if (auto gauge = wxDynamicCast(window, wxGauge))
    {
        yyjson_mut_obj_add_int(doc, obj, "value", gauge->GetValue());
    }
    else if (auto meter = dynamic_cast<LevelMeterLed*>(window))
    {
        yyjson_mut_obj_add_real(doc, obj, "value", meter->GetLevelDb());
        yyjson_mut_obj_add_real(doc, obj, "minDb", meter->GetMinDb());
        yyjson_mut_obj_add_real(doc, obj, "maxDb", meter->GetMaxDb());
        yyjson_mut_obj_add_real(doc, obj, "amberStartDb", meter->GetAmberStartDb());
        yyjson_mut_obj_add_real(doc, obj, "redStartDb", meter->GetRedStartDb());
        yyjson_mut_obj_add_bool(doc, obj, "zoneColours", meter->GetZoneColours());
    }
    else if (auto slider = wxDynamicCast(window, wxSlider))
    {
        yyjson_mut_obj_add_int(doc, obj, "value", slider->GetValue());
    }

    if (container != nullptr)
    {
        int selection = container->GetSelection();
        yyjson_mut_obj_add_int(doc, obj, "selection", selection);

        wxString data;
        if (selection != wxNOT_FOUND && GetItemData_(container, selection, data))
        {
            AddString_(doc, obj, "data", data);
        }

        if (includeItems)
        {
            yyjson_mut_val* items = yyjson_mut_arr(doc);
            for (unsigned int index = 0; index < container->GetCount(); index++)
            {
                yyjson_mut_val* item = yyjson_mut_obj(doc);
                AddString_(doc, item, "label", container->GetString(index));
                if (GetItemData_(container, index, data))
                {
                    AddString_(doc, item, "data", data);
                }
                yyjson_mut_arr_append(items, item);
            }
            yyjson_mut_obj_add_val(doc, obj, "items", items);
        }
    }

    return obj;
}

void TestAgent::commandListWindows_(yyjson_val* id)
{
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* windows = yyjson_mut_arr(doc);
    for (auto window : wxTopLevelWindows)
    {
        yyjson_mut_arr_append(windows, describeWindow_(doc, window, false));
    }
    replyOk_(id, doc, windows);
}

void TestAgent::commandListWidgets_(yyjson_val* id, yyjson_val* request)
{
    wxWindow* window = findWindow_(id, request);
    if (window == nullptr) return;

    std::vector<wxWindow*> descendants;
    AppendDescendants_(descendants, window);

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* widgets = yyjson_mut_arr(doc);
    for (auto widget : descendants)
    {
        yyjson_mut_arr_append(widgets, describeWindow_(doc, widget, false));
    }
    replyOk_(id, doc, widgets);
}

void TestAgent::commandGet_(yyjson_val* id, yyjson_val* request)
{
    wxWindow* window = findWindow_(id, request);
    if (window == nullptr) return;

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    replyOk_(id, doc, describeWindow_(doc, window, true));
}

// Returns the modal dialog that's keeping the user from interacting with the
// given window, if any.
static wxDialog* GetBlockingDialog_(wxWindow* window)
{
    auto ownDialog = wxDynamicCast(wxGetTopLevelParent(window), wxDialog);
    if (ownDialog != nullptr && ownDialog->IsModal())
    {
        return nullptr;
    }

    for (auto topLevel : wxTopLevelWindows)
    {
        auto dialog = wxDynamicCast(topLevel, wxDialog);
        if (dialog != nullptr && dialog->IsModal() && dialog->IsShown())
        {
            return dialog;
        }
    }
    return nullptr;
}

// Returns whether the user would currently be able to interact with the
// given window, providing the reason if not.
static bool CanInteract_(wxWindow* window, wxString& error)
{
    auto blockingDialog = GetBlockingDialog_(window);
    if (blockingDialog != nullptr)
    {
        error = wxString::Format(
            "'%s' cannot be used while '%s' is open", window->GetName(), blockingDialog->GetName());
        return false;
    }
    if (!window->IsShownOnScreen())
    {
        error = wxString::Format("'%s' is not visible", window->GetName());
        return false;
    }
    if (!window->IsEnabled())
    {
        error = wxString::Format("'%s' is disabled", window->GetName());
        return false;
    }
    return true;
}

static void SendCommandEvent_(wxWindow* window, wxEventType type, int intValue = 0, wxString const& stringValue = wxEmptyString)
{
    wxCommandEvent event(type, window->GetId());
    event.SetEventObject(window);
    event.SetInt(intValue);
    event.SetString(stringValue);
    window->ProcessWindowEvent(event);
}

// Does what a mouse click on the given window would do. Returns false if
// it's not something that can be clicked.
static bool Click_(wxWindow* window)
{
    if (auto toggle = wxDynamicCast(window, wxToggleButton))
    {
        toggle->SetValue(!toggle->GetValue());
        SendCommandEvent_(window, wxEVT_TOGGLEBUTTON, toggle->GetValue());
    }
    else if (auto checkBox = wxDynamicCast(window, wxCheckBox))
    {
        checkBox->SetValue(!checkBox->GetValue());
        SendCommandEvent_(window, wxEVT_CHECKBOX, checkBox->GetValue());
    }
    else if (auto radioButton = wxDynamicCast(window, wxRadioButton))
    {
        radioButton->SetValue(true);
        SendCommandEvent_(window, wxEVT_RADIOBUTTON, 1);
    }
    else if (wxDynamicCast(window, wxButton) != nullptr)
    {
        SendCommandEvent_(window, wxEVT_BUTTON);
    }
    else
    {
        return false;
    }
    return true;
}

static bool IsClickable_(wxWindow* window)
{
    return
        wxDynamicCast(window, wxToggleButton) != nullptr ||
        wxDynamicCast(window, wxCheckBox) != nullptr ||
        wxDynamicCast(window, wxRadioButton) != nullptr ||
        wxDynamicCast(window, wxButton) != nullptr;
}

// Finds somewhere inside the given item of a list box.
static bool FindListBoxItem_(wxListBox* listBox, int item, wxPoint& position)
{
    wxSize size = listBox->GetClientSize();
    for (int y = 1; y < size.GetHeight(); y += 2)
    {
        wxPoint candidate(std::min(10, size.GetWidth() / 2), y);
        if (listBox->HitTest(candidate) == item)
        {
            position = candidate;
            return true;
        }
    }
    return false;
}

void TestAgent::commandClick_(yyjson_val* id, yyjson_val* request)
{
    wxWindow* window = findWindow_(id, request);
    if (window == nullptr) return;

    wxString error;
    if (!CanInteract_(window, error))
    {
        replyError_(id, error);
        return;
    }

    // An item in a list box.
    yyjson_val* itemIndex = yyjson_obj_get(request, "item");
    yyjson_val* button = yyjson_obj_get(request, "button");
    bool rightButton = yyjson_is_str(button) && std::string(yyjson_get_str(button)) == "right";
    if (auto listCtrl = wxDynamicCast(window, wxListCtrl))
    {
        // A row of a list with columns, or (without an item) below its rows.
        // The mouse is moved there first, as lists that follow the mouse
        // rely on that.
        wxPoint position(10, std::max(1, listCtrl->GetClientSize().GetHeight() - 2));
        if (yyjson_is_int(itemIndex))
        {
            long item = yyjson_get_int(itemIndex);
            bool found = false;
            if (item >= 0 && item < listCtrl->GetItemCount())
            {
                listCtrl->EnsureVisible(item);
                for (int y = 1; y < listCtrl->GetClientSize().GetHeight() && !found; y += 2)
                {
                    int flags = 0;
                    wxPoint candidate(10, y);
                    if (listCtrl->HitTest(candidate, flags) == item)
                    {
                        position = candidate;
                        found = true;
                    }
                }
            }
            if (!found)
            {
                replyError_(id, wxString::Format("could not find item %ld of '%s'", item, window->GetName()));
                return;
            }
        }

        replyOk_(id, yyjson_mut_doc_new(nullptr));
        for (auto type : { wxEVT_MOTION, rightButton ? wxEVT_RIGHT_DOWN : wxEVT_LEFT_DOWN,
                           rightButton ? wxEVT_RIGHT_UP : wxEVT_LEFT_UP })
        {
            wxMouseEvent event(type);
            event.SetEventObject(window);
            event.SetPosition(position);
            window->ProcessWindowEvent(event);
        }
        return;
    }
    if (yyjson_is_int(itemIndex))
    {
        auto listBox = wxDynamicCast(window, wxListBox);
        wxPoint position;
        if (listBox == nullptr || !FindListBoxItem_(listBox, yyjson_get_int(itemIndex), position))
        {
            replyError_(id, wxString::Format(
                "could not find item %d of '%s'", yyjson_get_int(itemIndex), window->GetName()));
            return;
        }

        replyOk_(id, yyjson_mut_doc_new(nullptr));
        for (auto type : { wxEVT_MOTION, wxEVT_LEFT_DOWN, wxEVT_LEFT_UP })
        {
            wxMouseEvent event(type);
            event.SetEventObject(window);
            event.SetPosition(position);
            window->ProcessWindowEvent(event);
        }
        return;
    }

    if (!IsClickable_(window))
    {
        replyError_(id, wxString::Format("'%s' cannot be clicked", window->GetName()));
        return;
    }

    replyOk_(id, yyjson_mut_doc_new(nullptr));
    Click_(window);
}

void TestAgent::commandSetValue_(yyjson_val* id, yyjson_val* request)
{
    wxWindow* window = findWindow_(id, request);
    if (window == nullptr) return;

    wxString error;
    if (!CanInteract_(window, error))
    {
        replyError_(id, error);
        return;
    }

    yyjson_val* value = yyjson_obj_get(request, "value");
    yyjson_val* data = yyjson_obj_get(request, "data");
    yyjson_val* index = yyjson_obj_get(request, "index");

    bool isBoolControl =
        wxDynamicCast(window, wxToggleButton) != nullptr ||
        wxDynamicCast(window, wxCheckBox) != nullptr ||
        wxDynamicCast(window, wxRadioButton) != nullptr;
    auto comboBox = wxDynamicCast(window, wxComboBox);
    auto choice = wxDynamicCast(window, wxChoice);
    auto listBox = wxDynamicCast(window, wxListBox);
    wxItemContainer* container = comboBox;
    if (container == nullptr) container = choice;
    if (container == nullptr) container = listBox;

    if (isBoolControl)
    {
        if (!yyjson_is_bool(value))
        {
            replyError_(id, wxString::Format("'%s' needs a boolean value", window->GetName()));
            return;
        }

        bool current = false;
        if (auto toggle = wxDynamicCast(window, wxToggleButton)) current = toggle->GetValue();
        else if (auto checkBox = wxDynamicCast(window, wxCheckBox)) current = checkBox->GetValue();
        else if (auto radioButton = wxDynamicCast(window, wxRadioButton)) current = radioButton->GetValue();

        replyOk_(id, yyjson_mut_doc_new(nullptr));
        if (current != yyjson_get_bool(value))
        {
            Click_(window);
        }
    }
    else if (container != nullptr)
    {
        // Select an existing item by position, by its attached string or by
        // its label.
        int selection = wxNOT_FOUND;
        if (yyjson_is_int(index))
        {
            selection = yyjson_get_int(index);
            if (selection < 0 || selection >= (int)container->GetCount())
            {
                replyError_(id, wxString::Format("'%s' has no item %d", window->GetName(), selection));
                return;
            }
        }
        else if (yyjson_is_str(data))
        {
            wxString wanted = wxString::FromUTF8(yyjson_get_str(data));
            wxString itemData;
            for (unsigned int item = 0; item < container->GetCount(); item++)
            {
                if (GetItemData_(container, item, itemData) && itemData == wanted)
                {
                    selection = item;
                    break;
                }
            }
            if (selection == wxNOT_FOUND)
            {
                replyError_(id, wxString::Format("'%s' has no item with data '%s'", window->GetName(), wanted));
                return;
            }
        }
        else if (yyjson_is_str(value))
        {
            wxString wanted = wxString::FromUTF8(yyjson_get_str(value));
            selection = container->FindString(wanted, true);
            if (selection == wxNOT_FOUND)
            {
                // (A read-only combo box can still say it's editable: on
                // macOS, IsEditable() doesn't reflect wxCB_READONLY.)
                if (comboBox == nullptr || comboBox->HasFlag(wxCB_READONLY) || !comboBox->IsEditable())
                {
                    replyError_(id, wxString::Format("'%s' has no item labeled '%s'", window->GetName(), wanted));
                    return;
                }

                // Not in the list, so type it in instead.
                replyOk_(id, yyjson_mut_doc_new(nullptr));
                comboBox->SetValue(wanted);
                return;
            }
        }
        else
        {
            replyError_(id, wxString::Format("'%s' needs a value, data or index", window->GetName()));
            return;
        }

        replyOk_(id, yyjson_mut_doc_new(nullptr));
        container->SetSelection(selection);
        if (comboBox != nullptr)
        {
            // Choosing an item also changes the text.
            SendCommandEvent_(window, wxEVT_TEXT, 0, container->GetString(selection));
        }
        SendCommandEvent_(
            window,
            (comboBox != nullptr) ? wxEVT_COMBOBOX : ((listBox != nullptr) ? wxEVT_LISTBOX : wxEVT_CHOICE),
            selection, container->GetString(selection));
    }
    else if (auto comboCtrl = wxDynamicCast(window, wxComboCtrl))
    {
        if (!yyjson_is_str(value))
        {
            replyError_(id, wxString::Format("'%s' needs a string value", window->GetName()));
            return;
        }

        // Generates the same event that typing does.
        replyOk_(id, yyjson_mut_doc_new(nullptr));
        comboCtrl->SetValue(wxString::FromUTF8(yyjson_get_str(value)));
    }
    else if (auto spinCtrl = wxDynamicCast(window, wxSpinCtrl))
    {
        if (!yyjson_is_int(value))
        {
            replyError_(id, wxString::Format("'%s' needs an integer value", window->GetName()));
            return;
        }

        replyOk_(id, yyjson_mut_doc_new(nullptr));
        spinCtrl->SetValue(yyjson_get_int(value));
        SendCommandEvent_(window, wxEVT_SPINCTRL, spinCtrl->GetValue());
    }
    else if (auto textCtrl = wxDynamicCast(window, wxTextCtrl))
    {
        if (!yyjson_is_str(value))
        {
            replyError_(id, wxString::Format("'%s' needs a string value", window->GetName()));
            return;
        }
        if (!textCtrl->IsEditable())
        {
            replyError_(id, wxString::Format("'%s' is read-only", window->GetName()));
            return;
        }

        // Generates the same event that typing does.
        replyOk_(id, yyjson_mut_doc_new(nullptr));
        textCtrl->SetValue(wxString::FromUTF8(yyjson_get_str(value)));
    }
    else if (auto book = wxDynamicCast(window, wxBookCtrlBase))
    {
        // A page (tab), by position or by title.
        int page = wxNOT_FOUND;
        if (yyjson_is_int(value))
        {
            page = yyjson_get_int(value);
        }
        else if (yyjson_is_str(value))
        {
            for (size_t candidate = 0; candidate < book->GetPageCount(); candidate++)
            {
                if (book->GetPageText(candidate) == wxString::FromUTF8(yyjson_get_str(value)))
                {
                    page = (int)candidate;
                }
            }
        }
        if (page < 0 || page >= (int)book->GetPageCount())
        {
            replyError_(id, wxString::Format("'%s' has no such page", window->GetName()));
            return;
        }

        replyOk_(id, yyjson_mut_doc_new(nullptr));
        book->SetSelection(page);
    }
    else if (auto slider = wxDynamicCast(window, wxSlider))
    {
        if (!yyjson_is_int(value) || yyjson_get_int(value) < slider->GetMin() || yyjson_get_int(value) > slider->GetMax())
        {
            replyError_(id, wxString::Format(
                "'%s' needs a value from %d to %d", window->GetName(), slider->GetMin(), slider->GetMax()));
            return;
        }

        // As if dragged there.
        replyOk_(id, yyjson_mut_doc_new(nullptr));
        slider->SetValue(yyjson_get_int(value));
        for (auto type : { wxEVT_SCROLL_THUMBTRACK, wxEVT_SCROLL_THUMBRELEASE, wxEVT_SCROLL_CHANGED })
        {
            wxScrollEvent event(type, slider->GetId(), slider->GetValue());
            event.SetEventObject(slider);
            slider->ProcessWindowEvent(event);
        }
        SendCommandEvent_(window, wxEVT_SLIDER, slider->GetValue());
    }
    else if (auto colourPicker = wxDynamicCast(window, wxColourPickerCtrl))
    {
        wxColour colour(yyjson_is_str(value) ? wxString::FromUTF8(yyjson_get_str(value)) : wxString());
        if (!colour.IsOk())
        {
            replyError_(id, wxString::Format("'%s' needs a color such as \"#FF0000\"", window->GetName()));
            return;
        }

        replyOk_(id, yyjson_mut_doc_new(nullptr));
        colourPicker->SetColour(colour);
        wxColourPickerEvent event(colourPicker, colourPicker->GetId(), colour);
        colourPicker->ProcessWindowEvent(event);
    }
    else if (auto listCtrl = wxDynamicCast(window, wxListCtrl))
    {
        // An item, by position or by what's in its first column.
        long item = wxNOT_FOUND;
        if (yyjson_is_int(index))
        {
            item = yyjson_get_int(index);
        }
        else if (yyjson_is_str(value))
        {
            item = listCtrl->FindItem(-1, wxString::FromUTF8(yyjson_get_str(value)));
        }
        if (item < 0 || item >= listCtrl->GetItemCount())
        {
            replyError_(id, wxString::Format("'%s' has no such item", window->GetName()));
            return;
        }

        // Selecting an item this way is reported in the same way as the user
        // doing so.
        replyOk_(id, yyjson_mut_doc_new(nullptr));
        listCtrl->EnsureVisible(item);
        listCtrl->SetItemState(
            item, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    }
    else
    {
        replyError_(id, wxString::Format("the value of '%s' cannot be set", window->GetName()));
    }
}

// Does what the close button in a window's title bar would do.
void TestAgent::commandClose_(yyjson_val* id, yyjson_val* request)
{
    wxWindow* window = findWindow_(id, request);
    if (window == nullptr) return;

    wxString error;

    // Something that pops up until the user clicks elsewhere.
    if (auto popup = wxDynamicCast(window, wxPopupTransientWindow))
    {
        replyOk_(id, yyjson_mut_doc_new(nullptr));
#if defined(__WXMSW__)
        // On Windows, it goes when it's no longer the active window, which
        // is what a click elsewhere does.
        ::SendMessage((HWND)popup->GetHWND(), WM_ACTIVATE, WA_INACTIVE, 0);
#else
        // A click outside it, which is seen by whatever in it has the mouse.
        wxWindow* target = (popup->GetChildren().GetCount() == 1) ? popup->GetChildren().GetFirst()->GetData() : popup;
        wxMouseEvent event(wxEVT_LEFT_DOWN);
        event.SetEventObject(target);
        event.SetPosition(wxPoint(-10, -10));
        target->ProcessWindowEvent(event);
#endif // defined(__WXMSW__)
        return;
    }

    if (wxDynamicCast(window, wxTopLevelWindow) == nullptr)
    {
        replyError_(id, wxString::Format("'%s' is not a top-level window", window->GetName()));
        return;
    }
    if (!CanInteract_(window, error))
    {
        replyError_(id, error);
        return;
    }

    replyOk_(id, yyjson_mut_doc_new(nullptr));
    window->Close();
}

// Brings a window to the front, as clicking on it would.
void TestAgent::commandActivate_(yyjson_val* id, yyjson_val* request)
{
    wxWindow* window = findWindow_(id, request);
    if (window == nullptr) return;

    wxString error;
    if (wxDynamicCast(window, wxTopLevelWindow) == nullptr)
    {
        replyError_(id, wxString::Format("'%s' is not a top-level window", window->GetName()));
        return;
    }
    if (!CanInteract_(window, error))
    {
        replyError_(id, error);
        return;
    }

    replyOk_(id, yyjson_mut_doc_new(nullptr));

    // Not by clicking somewhere in particular, though: keep the mouse pointer
    // out of the way so that nothing in the window reacts to it being there.
    wxPoint corner = window->ScreenToClient(wxPoint(0, 0));
    window->WarpPointer(corner.x, corner.y);

#if defined(__APPLE__)
    TestAgentActivateApp();
#endif // defined(__APPLE__)
    window->Raise();
    window->SetFocus();
}

void TestAgent::commandMenu_(yyjson_val* id, yyjson_val* request)
{
    // Menu items are given as "<menu>/<item>" (or ".../<submenu>/<item>")
    // using their labels without mnemonics or shortcuts, e.g.
    // "Settings/Setup Wizard...". They're those of the main window unless
    // another window is named.
    const char* pathStr = yyjson_get_str(yyjson_obj_get(request, "path"));
    wxString path = (pathStr != nullptr) ? wxString::FromUTF8(pathStr) : wxString();

    wxWindow* window = wxTheApp->GetTopWindow();
    if (yyjson_obj_get(request, "name") != nullptr)
    {
        window = findWindow_(id, request);
        if (window == nullptr) return;
    }

    auto frame = wxDynamicCast(window, wxFrame);
    wxMenuBar* menuBar = (frame != nullptr) ? frame->GetMenuBar() : nullptr;
    if (menuBar == nullptr)
    {
        replyError_(id, "that window has no menu bar");
        return;
    }

    wxArrayString labels = wxSplit(path, '/', '\0');
    wxMenu* menu = nullptr;
    wxMenuItem* item = nullptr;
    if (labels.size() >= 2)
    {
        int menuIndex = menuBar->FindMenu(labels[0]);
        menu = (menuIndex != wxNOT_FOUND) ? menuBar->GetMenu(menuIndex) : nullptr;
#if defined(__WXOSX__)
        // macOS's menu named after the application, which is where
        // wxWidgets puts some items (e.g. File > Exit, as Quit).
        if (menu == nullptr && labels[0] == wxTheApp->GetAppDisplayName())
        {
            menu = menuBar->OSXGetAppleMenu();
        }
#endif // defined(__WXOSX__)
        for (size_t index = 1; menu != nullptr && index < labels.size(); index++)
        {
            item = nullptr;
            for (auto candidate : menu->GetMenuItems())
            {
                if (!candidate->IsSeparator() && candidate->GetItemLabelText() == labels[index])
                {
                    item = candidate;
                    break;
                }
            }
            if (item == nullptr)
            {
                break;
            }
            if (index + 1 < labels.size())
            {
                menu = item->GetSubMenu();
                item = nullptr;
            }
        }
    }
    if (item == nullptr || menu == nullptr)
    {
        replyError_(id, wxString::Format("no menu item '%s'", path));
        return;
    }

    wxString error;
    if (!CanInteract_(frame, error))
    {
        replyError_(id, error);
        return;
    }

    // Items are enabled and disabled just before their menu is displayed.
    menu->UpdateUI(frame);
    if (!item->IsEnabled())
    {
        replyError_(id, wxString::Format("menu item '%s' is disabled", path));
        return;
    }

    if (yyjson_get_bool(yyjson_obj_get(request, "query")))
    {
        // Only asked about the item, not to select it.
        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* result = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, result, "checked", item->IsCheckable() && item->IsChecked());
        replyOk_(id, doc, result);
        return;
    }

    replyOk_(id, yyjson_mut_doc_new(nullptr));

    if (item->IsCheckable())
    {
        item->Check(!item->IsChecked());
    }
    menu->SendEvent(item->GetId(), item->IsCheckable() ? item->IsChecked() : -1);
}

// Right-clicks a window, optionally choosing an item from the popup menu
// that results.
void TestAgent::commandContextMenu_(yyjson_val* id, yyjson_val* request)
{
    wxWindow* window = findWindow_(id, request);
    if (window == nullptr) return;

    wxString error;
    if (!CanInteract_(window, error))
    {
        replyError_(id, error);
        return;
    }

    // Lists handle right-clicks on their items themselves, so those need to
    // be told which one.
    wxPoint position = wxDefaultPosition;
    yyjson_val* itemIndex = yyjson_obj_get(request, "item");
    auto listBox = wxDynamicCast(window, wxListBox);
    if (yyjson_is_int(itemIndex))
    {
        if (listBox == nullptr || !FindListBoxItem_(listBox, yyjson_get_int(itemIndex), position))
        {
            replyError_(id, wxString::Format(
                "could not find item %d of '%s'", yyjson_get_int(itemIndex), window->GetName()));
            return;
        }
    }

    expectPopupMenu_(request);
    replyOk_(id, yyjson_mut_doc_new(nullptr));

    // The pointer has to get there first, which some controls wait for.
    wxMouseEvent motion(wxEVT_MOTION);
    motion.SetEventObject(window);
    motion.SetPosition(position);
    window->ProcessWindowEvent(motion);

    // Which event brings up the menu differs by platform and by control, so
    // send what a right-click does until something takes it.
    wxMouseEvent down(wxEVT_RIGHT_DOWN);
    down.SetEventObject(window);
    down.SetPosition(position);
    window->ProcessWindowEvent(down);

    wxMouseEvent up(wxEVT_RIGHT_UP);
    up.SetEventObject(window);
    up.SetPosition(position);
    if (!window->ProcessWindowEvent(up) && !lastPopup_.shown)
    {
        wxContextMenuEvent contextMenu(wxEVT_CONTEXT_MENU, window->GetId(), wxGetMousePosition());
        contextMenu.SetEventObject(window);
        window->ProcessWindowEvent(contextMenu);
    }

    popupMenuExpected_();
}

void TestAgent::commandGetPopupMenu_(yyjson_val* id)
{
    if (!lastPopup_.error.empty())
    {
        replyError_(id, lastPopup_.error);
        return;
    }

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* result = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, result, "shown", lastPopup_.shown);
    AddString_(doc, result, "window", lastPopup_.window);
    AddString_(doc, result, "chosen", lastPopup_.chosen);

    yyjson_mut_val* items = yyjson_mut_arr(doc);
    for (auto& item : lastPopup_.items)
    {
        yyjson_mut_val* itemObj = yyjson_mut_obj(doc);
        AddString_(doc, itemObj, "label", item.label);
        yyjson_mut_obj_add_bool(doc, itemObj, "enabled", item.enabled);
        yyjson_mut_obj_add_bool(doc, itemObj, "checked", item.checked);
        yyjson_mut_arr_append(items, itemObj);
    }
    yyjson_mut_obj_add_val(doc, result, "items", items);

    replyOk_(id, doc, result);
}

// Opens or closes the list belonging to a combo control.
void TestAgent::commandComboPopup_(yyjson_val* id, yyjson_val* request)
{
    wxWindow* window = findWindow_(id, request);
    if (window == nullptr) return;

    auto comboCtrl = wxDynamicCast(window, wxComboCtrl);
    wxString error;
    if (comboCtrl == nullptr)
    {
        replyError_(id, wxString::Format("'%s' has no list to open", window->GetName()));
        return;
    }
    if (!CanInteract_(window, error))
    {
        replyError_(id, error);
        return;
    }

    replyOk_(id, yyjson_mut_doc_new(nullptr));
    if (yyjson_get_bool(yyjson_obj_get(request, "open")))
    {
        comboCtrl->Popup();
    }
    else
    {
        comboCtrl->Dismiss();
    }
}

static bool SendDataViewEvent_(
    wxDataViewCtrl* dataView, wxEventType type, wxDataViewItem const& item, wxDataViewColumn* column)
{
#if wxCHECK_VERSION(3,1,1)
    wxDataViewEvent event(type, dataView, column, item);
#else
    wxDataViewEvent event(type, dataView->GetId());
    event.SetEventObject(dataView);
    event.SetModel(dataView->GetModel());
    event.SetItem(item);
    event.SetDataViewColumn(column);
    if (column != nullptr)
    {
        event.SetColumn(column->GetModelColumn());
    }
#endif // wxCHECK_VERSION(3,1,1)
    return dataView->ProcessWindowEvent(event);
}

// Moves the mouse pointer over a cell, as some of what lists do depends on
// what the pointer is over.
static bool HoverOverCell_(
    wxDataViewCtrl* dataView, wxDataViewItem const& item, wxDataViewColumn* column, wxString& error)
{
    dataView->EnsureVisible(item, column);

    wxRect rect = dataView->GetItemRect(item, column);
    if (rect.IsEmpty())
    {
        error = wxString::Format("the requested cell of '%s' is not being displayed", dataView->GetName());
        return false;
    }
    // Where the pointer ends up isn't always where it was sent (e.g. with no
    // window manager running), so correct for any difference.
    wxPoint target(rect.x + rect.width / 2, rect.y + rect.height / 2);
    wxPoint position;
    wxPoint correction(0, 0);
    for (int attempt = 0; attempt < 4; attempt++)
    {
        dataView->WarpPointer(target.x + correction.x, target.y + correction.y);
        position = wxGetMousePosition() - dataView->GetScreenPosition();
        if (position == target)
        {
            break;
        }
        correction += target - position;
    }

    wxDataViewItem itemUnderPointer;
    wxDataViewColumn* columnUnderPointer = nullptr;
    dataView->HitTest(position, itemUnderPointer, columnUnderPointer);
    if (itemUnderPointer != item || columnUnderPointer != column)
    {
        error = wxString::Format(
            "could not move the mouse pointer over the requested cell of '%s' (cell at %d,%d %dx%d, pointer at %d,%d, "
            "over %s in column %s, list %dx%d)",
            dataView->GetName(), rect.x, rect.y, rect.width, rect.height, position.x, position.y,
            !itemUnderPointer.IsOk() ? "no row" : (itemUnderPointer == item ? "that row" : "another row"),
            columnUnderPointer == nullptr ? wxString("none") : columnUnderPointer->GetTitle(),
            dataView->GetClientSize().GetWidth(), dataView->GetClientSize().GetHeight());
        return false;
    }

    wxMouseEvent motion(wxEVT_MOTION);
    motion.SetEventObject(dataView);
    motion.SetPosition(position);
    dataView->ProcessWindowEvent(motion);
    return true;
}

// Acts on a list of rows and columns. Rows are numbered from the top as
// displayed, and columns are referred to by ID (see "columns" in the list's
// state), as they can be reordered.
void TestAgent::commandDataView_(yyjson_val* id, yyjson_val* request)
{
    wxWindow* window = findWindow_(id, request);
    if (window == nullptr) return;

    auto dataView = wxDynamicCast(window, wxDataViewCtrl);
    wxString error;
    if (dataView == nullptr)
    {
        replyError_(id, wxString::Format("'%s' is not a list with columns", window->GetName()));
        return;
    }
    if (!CanInteract_(window, error))
    {
        replyError_(id, error);
        return;
    }

    const char* actionStr = yyjson_get_str(yyjson_obj_get(request, "action"));
    std::string action = (actionStr != nullptr) ? actionStr : "";
    bool isHeaderAction = (action == "header_click" || action == "header_context_menu");
    bool isRowAction = (action == "hover" || action == "select" || action == "activate" || action == "context_menu");
    if (!isHeaderAction && !isRowAction)
    {
        replyError_(id, wxString::Format("unknown list action '%s'", wxString::FromUTF8(action.c_str())));
        return;
    }

    // Defaults to the first column displayed.
    wxDataViewColumn* column = nullptr;
    yyjson_val* columnId = yyjson_obj_get(request, "column");
    for (unsigned int index = 0; index < dataView->GetColumnCount(); index++)
    {
        wxDataViewColumn* candidate = dataView->GetColumn(index);
        if (candidate->IsHidden())
        {
            continue;
        }
        if (yyjson_is_int(columnId)
                ? (int)candidate->GetModelColumn() == yyjson_get_int(columnId)
                : (column == nullptr || dataView->GetColumnPosition(candidate) < dataView->GetColumnPosition(column)))
        {
            column = candidate;
        }
    }
    if (column == nullptr)
    {
        replyError_(id, wxString::Format("'%s' is not displaying the requested column", window->GetName()));
        return;
    }

    if (isHeaderAction)
    {
        if (action == "header_click")
        {
            replyOk_(id, yyjson_mut_doc_new(nullptr));
            if (!SendDataViewEvent_(dataView, wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, wxDataViewItem(), column) &&
                column->IsSortable())
            {
                // Left alone, the list sorts by the column, reversing the
                // order if it already was. (The column is unset first as
                // wxGTK otherwise loses track of it being the sort column.)
                bool ascending = column->IsSortKey() ? !column->IsSortOrderAscending() : true;
                if (column->IsSortKey())
                {
                    column->UnsetAsSortKey();
                }

                // Only one column is sorted by at a time, which not every
                // version of wxWidgets sees to itself when asked like this.
                for (unsigned int index = 0; index < dataView->GetColumnCount(); index++)
                {
                    auto other = dataView->GetColumn(index);
                    if (other != column && other->IsSortKey())
                    {
                        other->UnsetAsSortKey();
                    }
                }
                column->SetSortOrder(ascending);
                if (dataView->GetModel() != nullptr)
                {
                    dataView->GetModel()->Resort();
                }
            }
        }
        else
        {
            expectPopupMenu_(request);
            replyOk_(id, yyjson_mut_doc_new(nullptr));
            SendDataViewEvent_(dataView, wxEVT_DATAVIEW_COLUMN_HEADER_RIGHT_CLICK, wxDataViewItem(), column);
            popupMenuExpected_();
        }
        return;
    }

    // The row is given by its position or, as that can change at any moment
    // in a list that's kept sorted, by the text in one of its columns.
    auto rows = GetDisplayedRows_(dataView);
    int row = wxNOT_FOUND;
    yyjson_val* matchColumn = yyjson_obj_get(request, "match_column");
    const char* matchText = yyjson_get_str(yyjson_obj_get(request, "match_text"));
    if (yyjson_is_int(matchColumn) && matchText != nullptr && dataView->GetModel() != nullptr)
    {
        for (size_t index = 0; index < rows.size() && row == wxNOT_FOUND; index++)
        {
            wxVariant value;
            dataView->GetModel()->GetValue(value, rows[index], yyjson_get_int(matchColumn));
            if (!value.IsNull() && value.MakeString() == wxString::FromUTF8(matchText))
            {
                row = (int)index;
            }
        }
    }
    else if (yyjson_is_int(yyjson_obj_get(request, "row")))
    {
        row = yyjson_get_int(yyjson_obj_get(request, "row"));
    }
    if (row < 0 || row >= (int)rows.size())
    {
        replyError_(id, wxString::Format("'%s' has no such row", window->GetName()));
        return;
    }
    wxDataViewItem item = rows[row];

    // Whatever's done to a row with the mouse is done with the pointer over it.
    if (!HoverOverCell_(dataView, item, column, error))
    {
        replyError_(id, error);
        return;
    }

    if (action == "context_menu")
    {
        expectPopupMenu_(request);
    }
    replyOk_(id, yyjson_mut_doc_new(nullptr));

    if (action == "select")
    {
        dataView->Select(item);
        SendDataViewEvent_(dataView, wxEVT_DATAVIEW_SELECTION_CHANGED, item, column);
    }
    else if (action == "activate")
    {
        SendDataViewEvent_(dataView, wxEVT_DATAVIEW_ITEM_ACTIVATED, item, column);
    }
    else if (action == "context_menu")
    {
        SendDataViewEvent_(dataView, wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, item, column);
        popupMenuExpected_();
    }
}

template<typename SoundDeviceType>
static yyjson_mut_val* DescribeSoundDevice_(yyjson_mut_doc* doc, SoundDeviceType& device)
{
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    AddString_(doc, obj, "deviceName", device.deviceName.get());
    yyjson_mut_obj_add_int(doc, obj, "sampleRate", device.sampleRate.get());
    return obj;
}

void TestAgent::commandGetState_(yyjson_val* id)
{
    auto& app = wxGetApp();
    auto& config = app.appConfiguration;

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* state = yyjson_mut_obj(doc);

    bool running = app.frame->m_RxRunning;
    yyjson_mut_obj_add_bool(doc, state, "running", running);
    yyjson_mut_obj_add_bool(doc, state, "modemRunning", isModemRunning.load(std::memory_order_acquire));
    yyjson_mut_obj_add_bool(doc, state, "transmitting", g_tx.load(std::memory_order_acquire));
    yyjson_mut_obj_add_int(doc, state, "numSoundCards", g_nSoundCards);
    yyjson_mut_obj_add_int(doc, state, "sync", running ? freedvInterface.getSync() : 0);
    yyjson_mut_obj_add_bool(doc, state, "analog", g_analog.load(std::memory_order_relaxed) != 0);

    // Whether this is being answered from inside an event loop other than
    // the application's own, i.e. a dialog's. One that's just been dismissed
    // can still be finishing up, with whatever FreeDV does once it has closed
    // yet to happen.
    auto mainLoop = wxTheApp->GetMainLoop();
    yyjson_mut_obj_add_bool(
        doc, state, "inNestedEventLoop", mainLoop != nullptr && wxEventLoopBase::GetActive() != mainLoop);
    yyjson_mut_obj_add_real(doc, state, "rxFreqOffsetHz", g_RxFreqOffsetHz.load(std::memory_order_relaxed));
    yyjson_mut_obj_add_real(doc, state, "txFreqOffsetHz", g_TxFreqOffsetHz.load(std::memory_order_relaxed));
    yyjson_mut_obj_add_bool(doc, state, "loopPlayFileFromRadio", g_loopPlayFileFromRadio.load(std::memory_order_relaxed));
    yyjson_mut_obj_add_real(doc, state, "monitorTxAudioVol", config.monitorTxAudioVol.get());
    yyjson_mut_obj_add_real(doc, state, "monitorVoiceKeyerAudioVol", config.monitorVoiceKeyerAudioVol.get());
    yyjson_mut_obj_add_real(doc, state, "agcAppliedGainDb", g_agcAppliedGainDb.load(std::memory_order_relaxed));

    yyjson_mut_val* audio = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, audio, "soundCard1In", DescribeSoundDevice_(doc, config.audioConfiguration.soundCard1In));
    yyjson_mut_obj_add_val(doc, audio, "soundCard1Out", DescribeSoundDevice_(doc, config.audioConfiguration.soundCard1Out));
    yyjson_mut_obj_add_val(doc, audio, "soundCard2In", DescribeSoundDevice_(doc, config.audioConfiguration.soundCard2In));
    yyjson_mut_obj_add_val(doc, audio, "soundCard2Out", DescribeSoundDevice_(doc, config.audioConfiguration.soundCard2Out));

    auto& rigConfig = config.rigControlConfiguration;
    yyjson_mut_val* rigControl = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, rigControl, "hamlibUseForPTT", rigConfig.hamlibUseForPTT.get());
    AddString_(doc, rigControl, "hamlibRigName", rigConfig.hamlibRigName.get());
    AddString_(doc, rigControl, "hamlibSerialPort", rigConfig.hamlibSerialPort.get());
    yyjson_mut_obj_add_uint(doc, rigControl, "hamlibSerialRate", rigConfig.hamlibSerialRate.get());
    yyjson_mut_obj_add_uint(doc, rigControl, "hamlibPTTType", rigConfig.hamlibPTTType.get());
    AddString_(doc, rigControl, "hamlibPttSerialPort", rigConfig.hamlibPttSerialPort.get());
    yyjson_mut_obj_add_bool(doc, rigControl, "hamlibUseAnalogModes", rigConfig.hamlibUseAnalogModes.get());
    yyjson_mut_obj_add_bool(doc, rigControl, "useSerialPTT", rigConfig.useSerialPTT.get());
    AddString_(doc, rigControl, "serialPTTPort", rigConfig.serialPTTPort.get());
    yyjson_mut_obj_add_bool(doc, rigControl, "serialPTTUseRTS", rigConfig.serialPTTUseRTS.get());
    yyjson_mut_obj_add_bool(doc, rigControl, "serialPTTPolarityRTS", rigConfig.serialPTTPolarityRTS.get());
    yyjson_mut_obj_add_bool(doc, rigControl, "serialPTTUseDTR", rigConfig.serialPTTUseDTR.get());
    yyjson_mut_obj_add_bool(doc, rigControl, "serialPTTPolarityDTR", rigConfig.serialPTTPolarityDTR.get());

    auto& reportingConfig = config.reportingConfiguration;
    yyjson_mut_val* reporting = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, reporting, "reportingEnabled", reportingConfig.reportingEnabled.get());
    AddString_(doc, reporting, "reportingCallsign", reportingConfig.reportingCallsign.get());
    AddString_(doc, reporting, "reportingGridSquare", reportingConfig.reportingGridSquare.get());
    yyjson_mut_obj_add_uint(doc, reporting, "reportingFrequency", reportingConfig.reportingFrequency.get());
    yyjson_mut_obj_add_bool(doc, reporting, "freedvReporterEnabled", reportingConfig.freedvReporterEnabled.get());
    yyjson_mut_obj_add_bool(doc, reporting, "pskReporterEnabled", reportingConfig.pskReporterEnabled.get());
    yyjson_mut_obj_add_bool(doc, reporting, "freedvReporterForcedOff", reportingConfig.freedvReporterForcedOff.get());

    yyjson_mut_val* configObj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, configObj, "audio", audio);
    yyjson_mut_obj_add_val(doc, configObj, "rigControl", rigControl);
    yyjson_mut_obj_add_val(doc, configObj, "reporting", reporting);

    auto& filterConfig = config.filterConfiguration;
    yyjson_mut_val* filter = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, filter, "noiseReductionEnable", filterConfig.noiseReductionEnable.get());
    yyjson_mut_obj_add_bool(doc, filter, "agcEnabled", filterConfig.agcEnabled.get());
    yyjson_mut_obj_add_real(doc, filter, "levelerTargetLufs", filterConfig.levelerTargetLufs.get());
    yyjson_mut_obj_add_bool(doc, filter, "bwExpandEnabled", filterConfig.bwExpandEnabled.get());
    auto describeChannel = [&](auto& channel) {
        yyjson_mut_val* obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, obj, "eqEnable", channel.eqEnable.get());
        yyjson_mut_obj_add_real(doc, obj, "volInDB", channel.volInDB.get());
        yyjson_mut_obj_add_real(doc, obj, "bassFreqHz", channel.bassFreqHz.get());
        yyjson_mut_obj_add_real(doc, obj, "bassGaindB", channel.bassGaindB.get());
        yyjson_mut_obj_add_real(doc, obj, "midFreqHz", channel.midFreqHz.get());
        yyjson_mut_obj_add_real(doc, obj, "midGainDB", channel.midGainDB.get());
        yyjson_mut_obj_add_real(doc, obj, "midQ", channel.midQ.get());
        yyjson_mut_obj_add_real(doc, obj, "trebleFreqHz", channel.trebleFreqHz.get());
        yyjson_mut_obj_add_real(doc, obj, "trebleGaindB", channel.trebleGaindB.get());
        return obj;
    };
    yyjson_mut_obj_add_val(doc, filter, "micIn", describeChannel(filterConfig.micInChannel));
    yyjson_mut_obj_add_val(doc, filter, "spkOut", describeChannel(filterConfig.spkOutChannel));
    yyjson_mut_obj_add_val(doc, configObj, "filter", filter);

    yyjson_mut_obj_add_val(doc, state, "config", configObj);

    replyOk_(id, doc, state);
}

void TestAgent::commandMessageBoxes_(yyjson_val* id, yyjson_val* request)
{
    std::vector<MessageBoxRecord> records;
    {
        std::unique_lock<std::mutex> lk(messageBoxMutex_);
        records = messageBoxes_;
        if (yyjson_get_bool(yyjson_obj_get(request, "clear")))
        {
            messageBoxes_.clear();
        }

        // How to answer the next message box, in place of its default button.
        const char* answer = yyjson_get_str(yyjson_obj_get(request, "answer"));
        if (answer != nullptr)
        {
            std::string answerStr = answer;
            messageBoxAnswers_.push_back(
                (answerStr == "yes") ? wxYES : (answerStr == "no") ? wxNO : (answerStr == "cancel") ? wxCANCEL : wxOK);
        }
    }

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* list = yyjson_mut_arr(doc);
    for (auto& record : records)
    {
        yyjson_mut_val* obj = yyjson_mut_obj(doc);
        AddString_(doc, obj, "message", record.message);
        AddString_(doc, obj, "caption", record.caption);
        yyjson_mut_obj_add_int(doc, obj, "style", record.style);
        yyjson_mut_arr_append(list, obj);
    }
    replyOk_(id, doc, list);
}

// Presses a key while the given window has the keyboard focus.
void TestAgent::commandKey_(yyjson_val* id, yyjson_val* request)
{
    wxWindow* window = findWindow_(id, request);
    if (window == nullptr) return;

    wxString error;
    if (!CanInteract_(window, error))
    {
        replyError_(id, error);
        return;
    }

    static const std::map<wxString, int> namedKeys = {
        { "Escape", WXK_ESCAPE }, { "Space", WXK_SPACE }, { "Tab", WXK_TAB }, { "Enter", WXK_RETURN },
        { "Backspace", WXK_BACK }, { "Delete", WXK_DELETE }, { "Shift", WXK_SHIFT }, { "Control", WXK_CONTROL },
        { "Up", WXK_UP }, { "Down", WXK_DOWN }, { "Left", WXK_LEFT }, { "Right", WXK_RIGHT },
        { "F1", WXK_F1 }, { "F2", WXK_F2 }, { "F3", WXK_F3 }, { "F4", WXK_F4 }, { "F5", WXK_F5 }, { "F6", WXK_F6 },
        { "F7", WXK_F7 }, { "F8", WXK_F8 }, { "F9", WXK_F9 }, { "F10", WXK_F10 }, { "F11", WXK_F11 },
        { "F12", WXK_F12 }, { "Insert", WXK_INSERT }, { "Home", WXK_HOME }, { "End", WXK_END },
        { "PageUp", WXK_PAGEUP }, { "PageDown", WXK_PAGEDOWN },
    };

    yyjson_val* keyVal = yyjson_obj_get(request, "key");
    const char* keyStr = yyjson_get_str(keyVal);
    wxString key = (keyStr != nullptr) ? wxString::FromUTF8(keyStr) : wxString();
    int keyCode = 0;
    auto namedKey = namedKeys.find(key);
    if (yyjson_is_int(keyVal))
    {
        // A key code as wxWidgets has it, for keys without a name here.
        keyCode = (int)yyjson_get_int(keyVal);
    }
    else if (namedKey != namedKeys.end())
    {
        keyCode = namedKey->second;
    }
    else if (key.length() == 1)
    {
        // Letters are reported as capitals when a key goes down or up.
        keyCode = wxToupper(key[0]);
    }
    else
    {
        replyError_(id, wxString::Format("unknown key '%s'", key));
        return;
    }

    auto sendKeyEvent = [&](wxEventType type) {
        wxKeyEvent event(type);
        event.SetEventObject(window);
        event.SetId(window->GetId());
        event.m_keyCode = keyCode;
#if wxUSE_UNICODE
        event.m_uniChar = keyCode;
#endif // wxUSE_UNICODE
        return window->ProcessWindowEvent(event);
    };

    replyOk_(id, yyjson_mut_doc_new(nullptr));

    // The window (and those that it's inside) get a chance to act on the
    // key before it's treated as input, and anything that does so stops
    // there.
    // A key can also be held down, to be released later.
    bool heldDownOnly = yyjson_get_bool(yyjson_obj_get(request, "hold"));
    bool releaseOnly = yyjson_get_bool(yyjson_obj_get(request, "release"));
    if (!releaseOnly && !sendKeyEvent(wxEVT_CHAR_HOOK) && !sendKeyEvent(wxEVT_KEY_DOWN))
    {
        sendKeyEvent(wxEVT_CHAR);
    }
    if (!heldDownOnly)
    {
        sendKeyEvent(wxEVT_KEY_UP);
    }
}

void TestAgent::commandPathDialogs_(yyjson_val* id, yyjson_val* request)
{
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* list = yyjson_mut_arr(doc);
    for (auto& title : pathDialogs_)
    {
        yyjson_mut_arr_add_strcpy(doc, list, ToUtf8_(title).c_str());
    }
    if (yyjson_get_bool(yyjson_obj_get(request, "clear")))
    {
        pathDialogs_.clear();
    }

    // What to choose from the next file or folder chooser, or to cancel it.
    const char* choose = yyjson_get_str(yyjson_obj_get(request, "choose"));
    if (choose != nullptr)
    {
        // Along with whatever else the chooser offers, e.g. { "loop": true }.
        PathDialogAnswer answer { true, wxString::FromUTF8(choose), {} };
        yyjson_val* options = yyjson_obj_get(request, "options");
        if (yyjson_is_obj(options))
        {
            yyjson_obj_iter iter = yyjson_obj_iter_with(options);
            yyjson_val* key = nullptr;
            while ((key = yyjson_obj_iter_next(&iter)) != nullptr)
            {
                answer.options[wxString::FromUTF8(yyjson_get_str(key))] = yyjson_get_bool(yyjson_obj_iter_get_val(key));
            }
        }
        pathDialogAnswers_.push_back(answer);
    }
    else if (yyjson_get_bool(yyjson_obj_get(request, "cancel")))
    {
        pathDialogAnswers_.push_back({ false, wxString(), {} });
    }

    replyOk_(id, doc, list);
}

void TestAgent::commandBrowserUrls_(yyjson_val* id, yyjson_val* request)
{
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* list = yyjson_mut_arr(doc);
    for (auto& url : browserUrls_)
    {
        yyjson_mut_arr_add_strcpy(doc, list, ToUtf8_(url).c_str());
    }
    if (yyjson_get_bool(yyjson_obj_get(request, "clear")))
    {
        browserUrls_.clear();
    }
    replyOk_(id, doc, list);
}

void TestAgent::commandClipboard_(yyjson_val* id)
{
    wxString text;
    if (wxTheClipboard->Open())
    {
        if (wxTheClipboard->IsSupported(wxDF_UNICODETEXT) || wxTheClipboard->IsSupported(wxDF_TEXT))
        {
            wxTextDataObject data;
            wxTheClipboard->GetData(data);
            text = data.GetText();
        }
        wxTheClipboard->Close();
    }

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* result = yyjson_mut_obj(doc);
    AddString_(doc, result, "text", text);
    replyOk_(id, doc, result);
}

// The keyboard focus arriving at, or (with "lost") leaving, a control.
void TestAgent::commandFocus_(yyjson_val* id, yyjson_val* request)
{
    wxWindow* window = findWindow_(id, request);
    if (window == nullptr) return;

    wxString error;
    if (!CanInteract_(window, error))
    {
        replyError_(id, error);
        return;
    }

    replyOk_(id, yyjson_mut_doc_new(nullptr));
    bool lost = yyjson_get_bool(yyjson_obj_get(request, "lost"));
    wxFocusEvent event(lost ? wxEVT_KILL_FOCUS : wxEVT_SET_FOCUS, window->GetId());
    event.SetEventObject(window);
    window->ProcessWindowEvent(event);
}

void TestAgent::commandMouse_(yyjson_val* id, yyjson_val* request)
{
    wxWindow* window = findWindow_(id, request);
    if (window == nullptr) return;

    wxString error;
    if (!CanInteract_(window, error))
    {
        replyError_(id, error);
        return;
    }

    static const std::map<std::string, wxEventType> eventTypes = {
        { "motion", wxEVT_MOTION }, { "enter", wxEVT_ENTER_WINDOW }, { "leave", wxEVT_LEAVE_WINDOW },
        { "left_down", wxEVT_LEFT_DOWN }, { "left_up", wxEVT_LEFT_UP }, { "left_dclick", wxEVT_LEFT_DCLICK },
        { "right_down", wxEVT_RIGHT_DOWN }, { "right_up", wxEVT_RIGHT_UP }, { "right_dclick", wxEVT_RIGHT_DCLICK },
        { "middle_down", wxEVT_MIDDLE_DOWN }, { "middle_up", wxEVT_MIDDLE_UP }, { "wheel", wxEVT_MOUSEWHEEL },
    };
    const char* eventName = yyjson_get_str(yyjson_obj_get(request, "event"));
    auto found = eventTypes.find(eventName != nullptr ? eventName : "");
    if (found == eventTypes.end())
    {
        replyError_(id, wxString::Format("unknown mouse event '%s'", wxString::FromUTF8(eventName != nullptr ? eventName : "")));
        return;
    }

    // Where in the window it happens, as fractions of its width and height
    // (its middle unless said otherwise).
    yyjson_val* x = yyjson_obj_get(request, "x");
    yyjson_val* y = yyjson_obj_get(request, "y");
    wxSize size = window->GetClientSize();
    wxPoint position(
        (int)((yyjson_is_num(x) ? yyjson_get_num(x) : 0.5) * size.GetWidth()),
        (int)((yyjson_is_num(y) ? yyjson_get_num(y) : 0.5) * size.GetHeight()));

    replyOk_(id, yyjson_mut_doc_new(nullptr));

    wxMouseEvent event(found->second);
    event.SetEventObject(window);
    event.SetId(window->GetId());
    event.SetPosition(position);
    if (found->second == wxEVT_MOUSEWHEEL)
    {
        // Notches: positive is away from the user.
        yyjson_val* rotation = yyjson_obj_get(request, "rotation");
        event.m_wheelDelta = 120;
        event.m_wheelRotation = 120 * (yyjson_is_int(rotation) ? yyjson_get_int(rotation) : 1);
        event.m_linesPerAction = 3;
    }
    window->ProcessWindowEvent(event);
}

void TestAgent::commandQuit_(yyjson_val* id)
{
    replyOk_(id, yyjson_mut_doc_new(nullptr));

    // The main window can't be closed from inside a dialog's own event loop,
    // and one that's only just been dismissed can still be finishing up
    // when the script's next command arrives (which is then run by that
    // loop). So keep asking, from the event loop each time, until it has
    // closed. (This can't happen to a user: the main window can't be used
    // while such a dialog is open.)
    quitAttemptsLeft_ = 500;
    quitTimer_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        wxWindow* topWindow = wxTheApp->GetTopWindow();
        if (topWindow == nullptr || topWindow->Close() || --quitAttemptsLeft_ <= 0)
        {
            quitTimer_.Stop();
        }
    });
    quitTimer_.Start(20);
}
