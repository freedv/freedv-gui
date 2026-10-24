//==========================================================================
// Name:            MsgListPopup.cpp
// Purpose:         Custom wxComboPopup with per-item right-click context menus
// Created:         April 2026
// Authors:         Barry Jackson
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

#include "MsgListPopup.h"
#include <algorithm>
#include <wx/textctrl.h>

MsgListPopup::MsgListPopup(ContextMenuCallback onRightClick)
    : onRightClick_(std::move(onRightClick)), deselTimer_(this)
{
    Bind(wxEVT_TIMER, &MsgListPopup::OnDeselect, this, deselTimer_.GetId());
}

bool MsgListPopup::Create(wxWindow* parent)
{
    if (!wxListBox::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SORT))
        return false;
    Connect(wxEVT_LEFT_DOWN,  wxMouseEventHandler(MsgListPopup::OnLeftDown),  nullptr, this);
    Connect(wxEVT_LEFT_UP,    wxMouseEventHandler(MsgListPopup::OnLeftUp),    nullptr, this);
    Connect(wxEVT_MOTION,     wxMouseEventHandler(MsgListPopup::OnMouseMove), nullptr, this);
    Connect(wxEVT_RIGHT_DOWN, wxMouseEventHandler(MsgListPopup::OnRightDown), nullptr, this);
    Connect(wxEVT_RIGHT_UP,   wxMouseEventHandler(MsgListPopup::OnRightUp),   nullptr, this);
    SetToolTip(_("Right click for more options"));
    return true;
}

void MsgListPopup::SetStringValue(const wxString& s)
{
    int idx = FindString(s, true);
    SetSelection(idx != wxNOT_FOUND ? idx : wxNOT_FOUND);
}

wxString MsgListPopup::GetStringValue() const
{
    // The combo takes this whenever the list closes. With nothing
    // highlighted, keep what's already in the box rather than emptying it.
    int sel = GetSelection();
    return sel != wxNOT_FOUND ? GetString(sel) : m_combo->GetValue();
}

void MsgListPopup::OnPopup()
{
    SetStringValue(GetComboCtrl()->GetValue());

    // Reposition popup above the combo: it sits near the bottom of the
    // dialog so there is more room upward.  OnPopup() is called before
    // the popup window is made visible, so there is no flicker.
    auto combo = GetComboCtrl();
    wxWindow* popupWin = combo->GetPopupWindow();
    if (popupWin)
    {
        wxPoint screenPos = combo->GetScreenPosition();
        wxSize  popupSz   = popupWin->GetSize();
        popupWin->Move(screenPos.x, screenPos.y - popupSz.GetHeight());
    }
}

wxSize MsgListPopup::GetAdjustedSize(int minWidth, int /*prefHeight*/, int maxHeight)
{
    int count = std::max(1, (int)GetCount());
    int rowH = GetCharHeight() + 8;
    return wxSize(minWidth, std::min(count * rowH + 4, maxHeight));
}

void MsgListPopup::OnLeftDown(wxMouseEvent& event)
{
    mouseDownPos_ = event.GetPosition();
    // Do not Skip(): forwarding to GTK's native handler would steal focus from
    // the GtkEntry, causing a focus-in on Dismiss() that re-selects all text.
}

void MsgListPopup::OnLeftUp(wxMouseEvent& event)
{
    wxPoint pos = event.GetPosition();
    int dx = std::abs(pos.x - mouseDownPos_.x);
    int dy = std::abs(pos.y - mouseDownPos_.y);
    mouseDownPos_ = wxDefaultPosition;

    // If the mouse moved significantly between down and up the user was
    // scrolling (e.g. dragging the horizontal scrollbar thumb), not clicking.
    // Do not Skip(): prevents native handler from altering GTK focus state,
    // which would cause a delayed focus-in that re-selects text after the
    // next item pick.
    if (dx > 4 || dy > 4)
        return;

    int item = HitTest(event.GetPosition());
    if (item != wxNOT_FOUND)
    {
        SetSelection(item);
        Dismiss();
        deselTimer_.StartOnce(20);
    }
    else
    {
        event.Skip();
    }
}

void MsgListPopup::OnMouseMove(wxMouseEvent& event)
{
    int item = HitTest(event.GetPosition());
    if (item != wxNOT_FOUND)
        SetSelection(item);
    event.Skip();
}

void MsgListPopup::OnRightDown(wxMouseEvent& event)
{
    // Select the item under the cursor for visual feedback; defer
    // the menu to RIGHT_UP so the button-release doesn't immediately
    // activate the first menu item.
    int item = HitTest(event.GetPosition());
    if (item == wxNOT_FOUND)
    {
        event.Skip();
        return;
    }
    SetSelection(item);
    pendingContextItem_ = item;
}

void MsgListPopup::OnRightUp(wxMouseEvent& event)
{
    if (pendingContextItem_ == wxNOT_FOUND)
    {
        event.Skip();
        return;
    }
    int item = pendingContextItem_;
    pendingContextItem_ = wxNOT_FOUND;
    Dismiss();
    onRightClick_(item);
}


void MsgListPopup::OnDeselect(wxTimerEvent&)
{
    auto tc = GetComboCtrl()->GetTextCtrl();
    if (tc)
        tc->SetSelection(0, 0);
}
