#include <iostream>
#include <string>
#include <vector>

#include <wx/init.h>

#include "../WindowPlacement.h"
#include "../../../test/UnitTestCommon.h"

// Display areas here are usable areas, i.e. without a 40 pixel taskbar along
// the bottom, as wxDisplay::GetClientArea() gives them. The primary display
// is always first.

namespace {

const WindowPlacementLimits LIMITS = { 30, wxSize(50, 20), 20 };

// Three 1920x1080 displays as in the report this came from: the primary one
// at the bottom left, one to its right, and one above it.
const wxRect PRIMARY(0, 0, 1920, 1040);
const wxRect RIGHT(1920, 0, 1920, 1040);
const wxRect ABOVE(0, -1080, 1920, 1040);

std::string describe_(const wxRect& rect)
{
    return "(" + std::to_string(rect.x) + ", " + std::to_string(rect.y) + ", " +
        std::to_string(rect.width) + "x" + std::to_string(rect.height) + ")";
}

bool placedAt(const char* what, const wxRect& saved, const std::vector<wxRect>& displays, const wxRect& expected)
{
    wxRect actual = PlaceWindowOnDisplays(saved, displays, LIMITS);
    if (actual == expected) return true;
    std::cout << "[" << what << ": " << describe_(saved) << " was put at " << describe_(actual) <<
        ", expected " << describe_(expected) << "] ";
    return false;
}

bool keptAt(const char* what, const wxRect& saved, const std::vector<wxRect>& displays)
{
    return placedAt(what, saved, displays, saved);
}

bool keepsAPositionOnADisplayAboveThePrimaryOne()
{
    std::vector<wxRect> displays = { PRIMARY, RIGHT, ABOVE };
    bool result = true;
    result &= keptAt("middle of the display above", wxRect(500, -900, 800, 780), displays);
    result &= keptAt("top left of the display above", wxRect(0, -1080, 800, 780), displays);
    result &= keptAt("straddling the display above and the primary", wxRect(500, -400, 800, 780), displays);
    return result;
}

bool keepsAPositionOnADisplayLeftOfThePrimaryOne()
{
    std::vector<wxRect> displays = { PRIMARY, wxRect(-1920, 0, 1920, 1040) };
    bool result = true;
    result &= keptAt("middle of the display to the left", wxRect(-1500, 100, 800, 780), displays);
    result &= keptAt("left edge of the display to the left", wxRect(-1920, 0, 800, 780), displays);
    return result;
}

bool keepsAPositionFurtherRightOrDownThan2048()
{
    bool result = true;
    result &= keptAt("right-hand side of the display to the right", wxRect(3000, 100, 800, 780), { PRIMARY, RIGHT });
    result &= keptAt("bottom right of a 4K display", wxRect(3000, 1300, 800, 780), { wxRect(0, 0, 3840, 2120) });
    return result;
}

bool keepsAPositionPushedAgainstADisplayEdge()
{
    // On Windows, a window pushed against a display's edge is reported a few
    // pixels beyond it, because of its invisible resize borders.
    std::vector<wxRect> displays = { PRIMARY };
    bool result = true;
    result &= keptAt("left edge", wxRect(-7, 100, 800, 780), displays);
    result &= keptAt("top edge", wxRect(100, -7, 800, 780), displays);
    result &= keptAt("right edge", wxRect(1127, 100, 800, 780), displays);
    return result;
}

bool keepsAPositionWithOnlyEnoughOfTheTitleBarVisibleToDragIt()
{
    std::vector<wxRect> displays = { PRIMARY };
    bool result = true;
    result &= keptAt("50 pixels in from the right", wxRect(1870, 100, 800, 780), displays);
    result &= keptAt("50 pixels in from the left", wxRect(-750, 100, 800, 780), displays);
    result &= keptAt("title bar at the bottom", wxRect(100, 1010, 800, 780), displays);
    return result;
}

bool bringsBackAWindowOnADisplayThatIsNoLongerConnected()
{
    std::vector<wxRect> displays = { PRIMARY };
    bool result = true;
    result &= placedAt("on the display that was above", wxRect(500, -900, 800, 780), displays, wxRect(20, 20, 800, 780));
    result &= placedAt("on the display that was to the right", wxRect(3000, 100, 800, 780), displays, wxRect(20, 20, 800, 780));
    return result;
}

bool bringsBackAWindowWithTooLittleOfItsTitleBarVisible()
{
    std::vector<wxRect> displays = { PRIMARY };
    bool result = true;
    result &= placedAt("49 pixels in from the right", wxRect(1871, 100, 800, 780), displays, wxRect(20, 20, 800, 780));
    result &= placedAt("title bar above the top", wxRect(100, -25, 800, 780), displays, wxRect(20, 20, 800, 780));
    result &= placedAt("title bar under the taskbar", wxRect(100, 1030, 800, 780), displays, wxRect(20, 20, 800, 780));
    result &= placedAt("only the bottom on screen", wxRect(100, -700, 800, 780), displays, wxRect(20, 20, 800, 780));
    return result;
}

bool bringsBackAWindowOntoThePrimaryDisplayWhereverItIs()
{
    // With X11, the primary display needn't be at (0, 0).
    std::vector<wxRect> displays = { wxRect(1920, 0, 2560, 1400), wxRect(0, 0, 1920, 1040) };
    return placedAt("off every display", wxRect(-3000, 100, 800, 780), displays, wxRect(1940, 20, 800, 780));
}

bool keepsTheSavedSize()
{
    bool result = true;
    result &= keptAt("bigger than the display it's on", wxRect(0, -1080, 2500, 1500), { PRIMARY, ABOVE });
    result &= keptAt("across two displays", wxRect(1500, 100, 1000, 700), { PRIMARY, RIGHT });
    result &= placedAt("brought back and bigger than the primary display", wxRect(5000, 100, 2500, 1500), { PRIMARY }, wxRect(20, 20, 2500, 1500));
    return result;
}

bool leavesASizeThatWasNotSavedAlone()
{
    bool result = true;
    result &= keptAt("on screen", wxRect(500, -900, -1, -1), { PRIMARY, ABOVE });
    result &= placedAt("off screen", wxRect(500, -900, -1, -1), { PRIMARY }, wxRect(20, 20, -1, -1));
    return result;
}

bool leavesEverythingAloneWithNoDisplays()
{
    return keptAt("no displays", wxRect(500, -900, 800, 780), {});
}

} // namespace

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk())
    {
        std::cout << "Could not initialize wxWidgets" << std::endl;
        return -1;
    }

    executeTestCase("keepsAPositionOnADisplayAboveThePrimaryOne", keepsAPositionOnADisplayAboveThePrimaryOne);
    executeTestCase("keepsAPositionOnADisplayLeftOfThePrimaryOne", keepsAPositionOnADisplayLeftOfThePrimaryOne);
    executeTestCase("keepsAPositionFurtherRightOrDownThan2048", keepsAPositionFurtherRightOrDownThan2048);
    executeTestCase("keepsAPositionPushedAgainstADisplayEdge", keepsAPositionPushedAgainstADisplayEdge);
    executeTestCase("keepsAPositionWithOnlyEnoughOfTheTitleBarVisibleToDragIt", keepsAPositionWithOnlyEnoughOfTheTitleBarVisibleToDragIt);
    executeTestCase("bringsBackAWindowOnADisplayThatIsNoLongerConnected", bringsBackAWindowOnADisplayThatIsNoLongerConnected);
    executeTestCase("bringsBackAWindowWithTooLittleOfItsTitleBarVisible", bringsBackAWindowWithTooLittleOfItsTitleBarVisible);
    executeTestCase("bringsBackAWindowOntoThePrimaryDisplayWhereverItIs", bringsBackAWindowOntoThePrimaryDisplayWhereverItIs);
    executeTestCase("keepsTheSavedSize", keepsTheSavedSize);
    executeTestCase("leavesASizeThatWasNotSavedAlone", leavesASizeThatWasNotSavedAlone);
    executeTestCase("leavesEverythingAloneWithNoDisplays", leavesEverythingAloneWithNoDisplays);

    return testResult();
}
