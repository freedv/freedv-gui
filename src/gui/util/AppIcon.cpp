//=========================================================================
// Name:            AppIcon.cpp
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

#include "AppIcon.h"

#include <wx/toplevel.h>

#if defined(__WXMSW__)
#include <wx/app.h>
#elif !defined(__WXOSX__)
#include <wx/image.h>
#include <wx/mstream.h>
#include "freedv_icon_images.h"
#endif

wxIconBundle GetFreeDVIcons()
{
    wxIconBundle icons;
#if defined(__WXMSW__)
    // All of the sizes in the icon resource "freedv".
    icons = wxIconBundle(wxT("freedv"), wxGetInstance());
#elif !defined(__WXOSX__)
    if (wxImage::FindHandler(wxBITMAP_TYPE_PNG) == nullptr)
    {
        wxImage::AddHandler(new wxPNGHandler());
    }
    for (const auto& image : FREEDV_ICON_IMAGES)
    {
        wxMemoryInputStream stream(image.data, image.size);
        wxImage decoded(stream, wxBITMAP_TYPE_PNG);
        if (decoded.IsOk())
        {
            wxIcon icon;
            icon.CopyFromBitmap(wxBitmap(decoded));
            icons.AddIcon(icon);
        }
    }
#endif
    return icons;
}

void SetFreeDVIcons(wxTopLevelWindow* window)
{
    auto icons = GetFreeDVIcons();
    if (!icons.IsEmpty())
    {
        window->SetIcons(icons);
    }
}
