//=========================================================================
// Name:            AppIcon.h
// Purpose:         Gives FreeDV's windows its icon.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License version 2.1,
// as published by the Free Software Foundation.  This program is
// distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
// License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//
//=========================================================================

#ifndef APP_ICON_H
#define APP_ICON_H

#include <wx/iconbndl.h>

class wxTopLevelWindow;

// FreeDV's icon in each size there is of it (see contrib/icon): on Windows,
// from the program's own resources (contrib/freedv.rc.in), and elsewhere from
// images built into it. Empty on macOS, where the Dock, the Finder and the
// window menus show the one in the app bundle.
wxIconBundle GetFreeDVIcons();

// Gives the window FreeDV's icon, for its title bar and the taskbar or
// window switcher, where it has one.
void SetFreeDVIcons(wxTopLevelWindow* window);

#endif // APP_ICON_H
