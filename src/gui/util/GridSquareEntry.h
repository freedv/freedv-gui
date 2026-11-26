//==========================================================================
// Name:            GridSquareEntry.h
// Purpose:         Shows what the dialogs' grid square boxes hold, and
//                  whether it will be saved.
//
// License:
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2.1,
//  as published by the Free Software Foundation.  This program is
//  distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
//  for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//
//==========================================================================

#ifndef GRID_SQUARE_ENTRY_H
#define GRID_SQUARE_ENTRY_H

#include <wx/colour.h>
#include <wx/string.h>
#include <wx/textctrl.h>

#include "GridSquare.h"

// Shows a dialog's grid square box as what it holds: in red, with a tool tip
// saying why, when what it holds isn't a grid square (an empty box is
// allowed). Called whenever the box's contents change, and matched by
// GridSquare::AcceptedValue, which is what keeps such a value out of the
// configuration.
inline void ShowGridSquareValidity(wxTextCtrl* gridSquareCtrl)
{
    wxString value = gridSquareCtrl->GetValue();
    value.Trim(true).Trim(false);
    bool valid = value.IsEmpty() || GridSquare::IsValid(value);
    gridSquareCtrl->SetForegroundColour(valid ? wxNullColour : *wxRED);
    if (valid)
    {
        gridSquareCtrl->UnsetToolTip();
    }
    else
    {
        gridSquareCtrl->SetToolTip(
            wxT("Not a valid grid square/locator, so it will not be saved. A locator is two letters A-R, "
                "optionally followed by two digits, two letters A-X and two more digits (e.g. FN31 or FN31pr)."));
    }
    gridSquareCtrl->Refresh();
}

#endif // GRID_SQUARE_ENTRY_H
