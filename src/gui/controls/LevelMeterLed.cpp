//==========================================================================
// Name:            LevelMeterLed.cpp
// Purpose:         A compact, custom-drawn LED-style level meter.
// Authors:         Claude Code (for Barry Jackson, G4MKT)
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

#include <cmath>
#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/settings.h>

#include "LevelMeterLed.h"

LevelMeterLed::LevelMeterLed(
    wxWindow* parent, wxWindowID id,
    float minDb, float maxDb, float segmentDb,
    float amberStartDb, float redStartDb,
    int segmentWidthPx, int segmentHeightPx,
    const wxPoint& pos)
    : wxWindow()
    , minDb_(minDb)
    , maxDb_(maxDb)
    , segmentDb_(segmentDb)
    , amberStartDb_(amberStartDb)
    , redStartDb_(redStartDb)
    , segmentWidthPx_(segmentWidthPx)
    , segmentHeightPx_(segmentHeightPx)
    , currentDb_(minDb)
    , litSegments_(0)
    , zoneColours_(false)
{
    numSegments_ = (int)std::lround((maxDb_ - minDb_) / segmentDb_);

    Create(parent, id, pos, wxSize(segmentWidthPx_ * numSegments_, segmentHeightPx_));
    SetBackgroundStyle(wxBG_STYLE_PAINT); // OnPaint() clears and draws everything
    Bind(wxEVT_PAINT, &LevelMeterLed::OnPaint, this);
}

wxSize LevelMeterLed::DoGetBestSize() const
{
    return wxSize(segmentWidthPx_ * numSegments_, segmentHeightPx_);
}

void LevelMeterLed::Reset()
{
    SetLevelDb(minDb_);
}

void LevelMeterLed::SetZoneColours(bool enabled)
{
    if (enabled != zoneColours_)
    {
        zoneColours_ = enabled;
        Refresh(false);
    }
}

void LevelMeterLed::SetLevelDb(float db)
{
    if (db < minDb_) db = minDb_;
    if (db > maxDb_) db = maxDb_;
    currentDb_ = db;

    // Segment i lights as soon as the level rises above its lower bound
    // (minDb_ + i*segmentDb_); ceil() so that exactly minDb_ lights none.
    int newLit = (int)std::ceil((currentDb_ - minDb_) / segmentDb_);
    newLit = std::max(0, std::min(numSegments_, newLit));

    if (newLit != litSegments_)
    {
        litSegments_ = newLit;
        Refresh(false); // false: OnPaint() clears and redraws everything, no need to erase first
    }
}

void LevelMeterLed::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);

    // Shows between the LEDs. Unstyled parents (e.g. a wxStaticBox on GTK)
    // don't report a usable colour.
    wxWindow* parent = GetParent();
    wxColour background = parent->UseBgCol() ? parent->GetBackgroundColour() : wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
    dc.SetBackground(wxBrush(background));
    dc.Clear();

    static const wxColour GREEN(0, 200, 0);
    static const wxColour AMBER(230, 160, 0);
    static const wxColour RED(220, 30, 30);
    static const wxColour BLUE(60, 145, 240);

    // Each LED is outlined in its own colour, so unlit LEDs (left empty)
    // still show where they are and which zone they belong to.

    // Each LED sits centred in its own segment, LED_GAP_PX narrower, so
    // neighbouring LEDs are LED_GAP_PX apart.
    constexpr int LED_GAP_PX = 2;
    int ledWidth = std::max(1, segmentWidthPx_ - LED_GAP_PX);

    for (int i = 0; i < numSegments_; i++)
    {
        float lowerDb = minDb_ + i * segmentDb_;
        wxColour zoneColour = !zoneColours_ ? BLUE :
            (lowerDb >= redStartDb_) ? RED : (lowerDb >= amberStartDb_) ? AMBER : GREEN;

        wxColour fillColour = (i < litSegments_) ? zoneColour : background;

        dc.SetBrush(wxBrush(fillColour));
        dc.SetPen(wxPen(zoneColour, 1));
        dc.DrawRectangle(i * segmentWidthPx_ + LED_GAP_PX / 2, 0, ledWidth, segmentHeightPx_);
    }
}
