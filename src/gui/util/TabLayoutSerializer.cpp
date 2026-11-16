#include "TabLayoutSerializer.h"

#if wxCHECK_VERSION(3, 3, 0)

#include <wx/tokenzr.h>

#include "util/logging/ulog.h"

namespace
{
    const wxChar* RECORD_SEP = wxT(";");
    const wxChar* FIELD_SEP = wxT(":");
    const wxChar* LIST_SEP = wxT(",");
    const int NUM_FIELDS = 9;

    wxString JoinInts(const std::vector<int>& values)
    {
        wxString result;
        for (auto it = values.begin(); it != values.end(); ++it)
        {
            if (it != values.begin()) result += LIST_SEP;
            result += wxString::Format(wxT("%d"), *it);
        }
        return result;
    }

    // Page indices. wxAuiNotebook::LoadLayout() skips one past its last
    // page (and a repeated one), but indexes its pages with a negative one,
    // reading outside them: a negative index in the config file is dropped.
    std::vector<int> SplitPageIndices(const wxString& str)
    {
        std::vector<int> result;

        wxStringTokenizer tok(str, LIST_SEP);
        while (tok.HasMoreTokens())
        {
            int index = wxAtoi(tok.GetNextToken());
            if (index >= 0)
            {
                result.push_back(index);
            }
        }
        return result;
    }
}

void TabLayoutSerializer::BeforeSaveNotebook(const wxString& WXUNUSED(name))
{
    m_layout.clear();
}

void TabLayoutSerializer::SaveNotebookTabControl(const wxAuiTabLayoutInfo& tab)
{
    if (!m_layout.empty()) m_layout += RECORD_SEP;

    m_layout += wxString::Format(
        wxT("%d:%d:%d:%d:%d:%d:%d:%s:%s"),
        tab.dock_direction, tab.dock_layer, tab.dock_row, tab.dock_pos,
        tab.dock_proportion, tab.dock_size, tab.active,
        JoinInts(tab.pages), JoinInts(tab.pinned));
}

TabLayoutDeserializer::TabLayoutDeserializer(const wxString& layout)
    : m_layout(layout)
{
    // empty
}

std::vector<wxAuiTabLayoutInfo> TabLayoutDeserializer::LoadNotebookTabs(const wxString& WXUNUSED(name))
{
    std::vector<wxAuiTabLayoutInfo> result;

    wxStringTokenizer recordTok(m_layout, RECORD_SEP);
    while (recordTok.HasMoreTokens())
    {
        wxString record = recordTok.GetNextToken();

        wxStringTokenizer fieldTok(record, FIELD_SEP, wxTOKEN_RET_EMPTY_ALL);
        wxArrayString fields;
        while (fieldTok.HasMoreTokens())
        {
            fields.Add(fieldTok.GetNextToken());
        }

        if ((int)fields.GetCount() != NUM_FIELDS)
        {
            log_warn("Ignoring malformed tab layout record: %s", (const char*)record.ToUTF8());
            continue;
        }

        wxAuiTabLayoutInfo tab;
        tab.dock_direction = wxAtoi(fields[0]);
        tab.dock_layer = wxAtoi(fields[1]);
        tab.dock_row = wxAtoi(fields[2]);
        tab.dock_pos = wxAtoi(fields[3]);
        tab.dock_proportion = wxAtoi(fields[4]);
        tab.dock_size = wxAtoi(fields[5]);
        tab.active = wxAtoi(fields[6]);
        tab.pages = SplitPageIndices(fields[7]);
        tab.pinned = SplitPageIndices(fields[8]);

        result.push_back(tab);
    }

    return result;
}

#endif // wxCHECK_VERSION(3, 3, 0)
