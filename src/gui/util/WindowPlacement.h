//==========================================================================
// Name:            WindowPlacement.h
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

#ifndef WINDOW_PLACEMENT_H
#define WINDOW_PLACEMENT_H

#include <vector>
#include <wx/gdicmn.h>

class wxWindow;

struct WindowPlacementLimits
{
    // How much of the window's top edge is taken to be its title bar.
    int titleBarHeight;

    // How much of the title bar has to be on a display for the saved
    // position to be kept: enough to grab it and drag the window.
    wxSize minimumVisible;

    // How far in from the primary display's top left a window that's
    // brought back goes.
    int margin;
};

// Where to put a window that was saved at `saved` (its position and outer
// size), given the usable area of each display connected now, the primary
// display's first. Display coordinates can be negative: a display above or
// to the left of the primary one has them.
//
// The saved position is kept if enough of the window's title bar is on one
// of the displays. Otherwise, e.g. because the display it was on has been
// disconnected, the window goes `margin` in from the top left of the
// primary display. The size is never changed: a width or height that isn't
// positive (wxWidgets takes it as "as it is now") only means that just
// `minimumVisible`'s width of the title bar is checked.
wxRect PlaceWindowOnDisplays(const wxRect& saved, const std::vector<wxRect>& displayAreas, const WindowPlacementLimits& limits);

// PlaceWindowOnDisplays() with the displays connected now, and limits scaled
// to suit `window`'s display.
wxRect PlaceWindowOnScreen(wxWindow* window, const wxRect& saved);

#endif // WINDOW_PLACEMENT_H
