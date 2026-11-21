//==========================================================================
// Name:            WindowPlacement.cpp
// Purpose:         Keeps a saved window position on the displays that are
//                  connected now.
// Created:         Nov 21, 2026
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

#include "WindowPlacement.h"

#include <algorithm>
#include <wx/display.h>
#include <wx/window.h>

namespace
{

// Reduces `length` to `available` if it's bigger, leaving one that isn't
// positive (i.e. "as it is now") alone.
int fitWithin_(int length, int available)
{
    if (length > 0 && length > available)
    {
        return std::max(available, 1);
    }
    return length;
}

} // namespace

wxRect PlaceWindowOnDisplays(const wxRect& saved, const std::vector<wxRect>& displayAreas, const WindowPlacementLimits& limits)
{
    if (displayAreas.empty())
    {
        return saved;
    }

    // The strip along the top of the window that's taken to be its title
    // bar. With no saved width, only as wide as has to be visible.
    int titleBarWidth = (saved.width > 0) ? saved.width : limits.minimumVisible.GetWidth();
    wxRect titleBar(saved.x, saved.y, titleBarWidth, limits.titleBarHeight);

    // The display with the most of the title bar on it.
    const wxRect* bestArea = nullptr;
    long bestOverlap = 0;
    for (const auto& area : displayAreas)
    {
        // (Intersect() changes the rectangle it's called on.)
        wxRect overlap = titleBar;
        overlap.Intersect(area);
        if (overlap.width >= limits.minimumVisible.GetWidth() &&
            overlap.height >= limits.minimumVisible.GetHeight() &&
            (long)overlap.width * overlap.height > bestOverlap)
        {
            bestArea = &area;
            bestOverlap = (long)overlap.width * overlap.height;
        }
    }

    wxRect result = saved;
    if (bestArea != nullptr)
    {
        result.width = fitWithin_(saved.width, bestArea->width);
        result.height = fitWithin_(saved.height, bestArea->height);
    }
    else
    {
        const wxRect& primary = displayAreas.front();
        result.x = primary.x + limits.margin;
        result.y = primary.y + limits.margin;
        result.width = fitWithin_(saved.width, primary.width - 2 * limits.margin);
        result.height = fitWithin_(saved.height, primary.height - 2 * limits.margin);
    }
    return result;
}

wxRect PlaceWindowOnScreen(wxWindow* window, const wxRect& saved)
{
    std::vector<wxRect> displayAreas;
    unsigned int count = wxDisplay::GetCount();
    for (unsigned int i = 0; i < count; i++)
    {
        wxDisplay display(i);
        if (display.IsPrimary())
        {
            displayAreas.insert(displayAreas.begin(), display.GetClientArea());
        }
        else
        {
            displayAreas.push_back(display.GetClientArea());
        }
    }

    // Window positions and display areas are both in the display's own
    // pixels on Windows, so these are scaled to suit. (wxWidgets 3.0, which
    // only Linux builds use, has no FromDIP(), and needs none there.)
    WindowPlacementLimits limits;
#if wxCHECK_VERSION(3,1,0)
    limits.titleBarHeight = window->FromDIP(30);
    limits.minimumVisible = window->FromDIP(wxSize(50, 20));
    limits.margin = window->FromDIP(20);
#else
    (void)window;
    limits.titleBarHeight = 30;
    limits.minimumVisible = wxSize(50, 20);
    limits.margin = 20;
#endif // wxCHECK_VERSION(3,1,0)

    return PlaceWindowOnDisplays(saved, displayAreas, limits);
}
