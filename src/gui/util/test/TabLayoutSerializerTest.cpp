#include <vector>

#include <wx/init.h>
#include <wx/version.h>

#include "../TabLayoutSerializer.h"
#include "util/logging/ulog.h"
#include "../../../test/UnitTestCommon.h"

// The layout of the main window's tabs is saved as a single string in the
// configuration file: one record per tab control, separated by semicolons.

#if wxCHECK_VERSION(3, 3, 0)

namespace {

using TabList = std::vector<wxAuiTabLayoutInfo>;

wxAuiTabLayoutInfo makeTab(
    int direction, int layer, int row, int pos, int proportion, int size, int active,
    std::vector<int> pages, std::vector<int> pinned)
{
    wxAuiTabLayoutInfo tab;
    tab.dock_direction = direction;
    tab.dock_layer = layer;
    tab.dock_row = row;
    tab.dock_pos = pos;
    tab.dock_proportion = proportion;
    tab.dock_size = size;
    tab.active = active;
    tab.pages = std::move(pages);
    tab.pinned = std::move(pinned);
    return tab;
}

void print(std::vector<int> const& list)
{
    std::cout << "{";
    for (auto item : list) std::cout << " " << item;
    std::cout << " }";
}

void print(wxAuiTabLayoutInfo const& tab)
{
    std::cout << tab.dock_direction << "/" << tab.dock_layer << "/" << tab.dock_row << "/" << tab.dock_pos
              << "/" << tab.dock_proportion << "/" << tab.dock_size << "/" << tab.active << " pages ";
    print(tab.pages);
    std::cout << " pinned ";
    print(tab.pinned);
}

bool sameTab(wxAuiTabLayoutInfo const& a, wxAuiTabLayoutInfo const& b)
{
    return
        a.dock_direction == b.dock_direction && a.dock_layer == b.dock_layer && a.dock_row == b.dock_row &&
        a.dock_pos == b.dock_pos && a.dock_proportion == b.dock_proportion && a.dock_size == b.dock_size &&
        a.active == b.active && a.pages == b.pages && a.pinned == b.pinned;
}

bool tabsAre(TabList const& actual, TabList const& expected)
{
    if (actual.size() != expected.size())
    {
        std::cout << "[got " << actual.size() << " tab control(s), expected " << expected.size() << "] ";
        return false;
    }

    for (size_t index = 0; index < expected.size(); index++)
    {
        if (!sameTab(actual[index], expected[index]))
        {
            std::cout << "[tab control " << index << " was ";
            print(actual[index]);
            std::cout << ", expected ";
            print(expected[index]);
            std::cout << "] ";
            return false;
        }
    }
    return true;
}

wxString serialize(TabList const& tabs)
{
    TabLayoutSerializer serializer;
    serializer.BeforeSaveNotebook("notebook");
    for (auto& tab : tabs)
    {
        serializer.SaveNotebookTabControl(tab);
    }
    return serializer.GetLayout();
}

TabList deserialize(wxString const& layout)
{
    return TabLayoutDeserializer(layout).LoadNotebookTabs("notebook");
}

bool layoutIs(wxString const& actual, const char* expected)
{
    if (actual == expected) return true;
    std::cout << "[layout was " << (const char*)actual.utf8_str() << ", expected " << expected << "] ";
    return false;
}

bool roundTrips(TabList const& tabs)
{
    return tabsAre(deserialize(serialize(tabs)), tabs);
}

bool nothingSavedGivesEmptyLayout()
{
    return layoutIs(TabLayoutSerializer().GetLayout(), "") && layoutIs(serialize({}), "");
}

bool emptyLayoutGivesNoTabs()
{
    // wxWidgets then leaves the tabs the way they were created.
    return tabsAre(deserialize(""), {});
}

bool singleTabControlRoundTrips()
{
    return roundTrips({ makeTab(5, 0, 0, 0, 100000, 0, 2, { 0, 1, 2, 3 }, { 0 }) });
}

bool splitTabControlsRoundTrip()
{
    return roundTrips({
        makeTab(5, 0, 0, 0, 100000, 0, 0, { 0, 2 }, {}),
        makeTab(2, 1, 0, 0, 100000, 412, 3, { 3, 1 }, { 3 }),
        makeTab(3, 1, 1, 2, 50000, 300, 4, { 4 }, { 4 }),
    });
}

bool emptyPageListsRoundTrip()
{
    // An empty page list means "all pages in their natural order".
    return roundTrips({
        makeTab(5, 0, 0, 0, 100000, 0, 0, {}, {}),
        makeTab(2, 1, 0, 0, 100000, 400, 1, { 1 }, {}),
        makeTab(4, 1, 0, 0, 100000, 400, 1, {}, { 2 }),
    });
}

bool negativeValuesRoundTrip()
{
    return roundTrips({ makeTab(-1, -2, -3, -4, -5, -6, -7, { 0 }, {}) });
}

bool savedFormatIsStable()
{
    // Layouts saved by existing releases need to keep loading.
    wxString layout = serialize({
        makeTab(5, 0, 0, 0, 100000, 0, 2, { 0, 1, 2 }, { 0 }),
        makeTab(2, 1, 0, 0, 100000, 412, 3, { 3 }, {}),
    });
    if (!layoutIs(layout, "5:0:0:0:100000:0:2:0,1,2:0;2:1:0:0:100000:412:3:3:")) return false;

    return tabsAre(deserialize("5:0:0:0:100000:0:2:0,1,2:0;2:1:0:0:100000:412:3:3:"), {
        makeTab(5, 0, 0, 0, 100000, 0, 2, { 0, 1, 2 }, { 0 }),
        makeTab(2, 1, 0, 0, 100000, 412, 3, { 3 }, {}),
    });
}

bool savingAgainReplacesPreviousLayout()
{
    TabLayoutSerializer serializer;
    serializer.BeforeSaveNotebook("notebook");
    serializer.SaveNotebookTabControl(makeTab(5, 0, 0, 0, 100000, 0, 0, { 0, 1 }, {}));
    serializer.SaveNotebookTabControl(makeTab(2, 1, 0, 0, 100000, 400, 2, { 2 }, {}));

    serializer.BeforeSaveNotebook("notebook");
    serializer.SaveNotebookTabControl(makeTab(5, 0, 0, 0, 100000, 0, 1, { 0, 1, 2 }, {}));
    return layoutIs(serializer.GetLayout(), "5:0:0:0:100000:0:1:0,1,2:");
}

bool malformedRecordsAreIgnored()
{
    auto good = makeTab(5, 0, 0, 0, 100000, 0, 2, { 0, 1 }, {});

    bool result = true;
    result &= tabsAre(deserialize("garbage"), {});
    result &= tabsAre(deserialize("5:0:0:0:100000:0:2:0,1"), {});          // too few fields
    result &= tabsAre(deserialize("5:0:0:0:100000:0:2:0,1::7"), {});       // too many fields
    result &= tabsAre(deserialize(":::"), {});

    // The remainder of the layout is still used.
    result &= tabsAre(deserialize("garbage;5:0:0:0:100000:0:2:0,1:"), { good });
    result &= tabsAre(deserialize("5:0:0:0:100000:0:2:0,1:;1:2:3"), { good });
    result &= tabsAre(deserialize("5:0:0:0:100000:0:2:0,1:;;"), { good });
    return result;
}

bool nonNumericFieldsLoadAsZero()
{
    return tabsAre(
        deserialize("x:1:y:2:z:3:w:a,4,b:c"),
        { makeTab(0, 1, 0, 2, 0, 3, 0, { 0, 4, 0 }, { 0 }) });
}

bool negativePageIndicesAreDropped()
{
    // wxWidgets would index its pages with them.
    return tabsAre(
        deserialize("5:0:0:0:100000:0:2:0,-1,1:-3,1"),
        { makeTab(5, 0, 0, 0, 100000, 0, 2, { 0, 1 }, { 1 }) });
}

}

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk())
    {
        std::cout << "Could not initialize wxWidgets" << std::endl;
        return -1;
    }

    ulog_set_quiet(true);

    executeTestCase("nothingSavedGivesEmptyLayout", nothingSavedGivesEmptyLayout);
    executeTestCase("emptyLayoutGivesNoTabs", emptyLayoutGivesNoTabs);
    executeTestCase("singleTabControlRoundTrips", singleTabControlRoundTrips);
    executeTestCase("splitTabControlsRoundTrip", splitTabControlsRoundTrip);
    executeTestCase("emptyPageListsRoundTrip", emptyPageListsRoundTrip);
    executeTestCase("negativeValuesRoundTrip", negativeValuesRoundTrip);
    executeTestCase("savedFormatIsStable", savedFormatIsStable);
    executeTestCase("savingAgainReplacesPreviousLayout", savingAgainReplacesPreviousLayout);
    executeTestCase("malformedRecordsAreIgnored", malformedRecordsAreIgnored);
    executeTestCase("nonNumericFieldsLoadAsZero", nonNumericFieldsLoadAsZero);
    executeTestCase("negativePageIndicesAreDropped", negativePageIndicesAreDropped);
    return testResult();
}

#else

int main()
{
    // Saving the tab layout needs wxWidgets 3.3 or later.
    std::cout << "Skipped: not supported by this version of wxWidgets" << std::endl;
    return 0;
}

#endif // wxCHECK_VERSION(3, 3, 0)
