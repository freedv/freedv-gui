//==========================================================================
// Name:            plot_waterfall.cpp
// Purpose:         Implements a waterfall plot derivative of plot.
// Created:         June 23, 2012
// Authors:         David Rowe, David Witten
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

#include <atomic>
#include <string.h>
#include <wx/wx.h>

#include "defines.h"
#include "plot_spectrum.h"

#define HZ_GRANULARITY 10

void clickTune(float frequency); // callback to pass new click freq
extern std::atomic<float> g_RxFreqOffsetHz;

constexpr int STR_LENGTH = 15;
    
BEGIN_EVENT_TABLE(PlotSpectrum, PlotPanel)
    EVT_MOTION          (PlotSpectrum::OnMouseMove)
    EVT_LEFT_DOWN       (PlotSpectrum::OnMouseLeftDown)
    EVT_LEFT_DCLICK     (PlotSpectrum::OnMouseLeftDoubleClick)
    EVT_LEFT_UP         (PlotSpectrum::OnMouseLeftUp)
    EVT_MIDDLE_DOWN     (PlotSpectrum::OnMouseMiddleDown)
    EVT_RIGHT_DCLICK    (PlotSpectrum::OnMouseRightDoubleClick)
    EVT_MOUSEWHEEL      (PlotSpectrum::OnMouseWheelMoved)
    EVT_PAINT           (PlotSpectrum::OnPaint)
    EVT_SHOW            (PlotSpectrum::OnShow)
    EVT_KEY_DOWN        (PlotSpectrum::OnKeyDown)
END_EVENT_TABLE()

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// Class PlotSpectrum
//
// @class $(Name)
// @author $(User)
// @date $(Date)
// @file $(CurrentFileName).$(CurrentFileExt)
// @brief
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
PlotSpectrum::PlotSpectrum(wxWindow* parent, float *magdB, int n_magdB, 
                           float min_mag_db, float max_mag_db, bool clickTune): PlotPanel(parent)
{
    WXWIDGETS_SET_LAYOUT_DIRECTION;
    
    m_greyscale     = 0;
    m_Bufsz         = GetMaxClientSize();
    m_newdata       = false;
    m_firstPass     = true;
    m_line_color    = 0;
    m_numSampleAveraging = 1;
    leftOffset_ = 0;
    bottomOffset_ = 0;
    SetLabelSize(10.0);

    m_magdB         = magdB;
    m_n_magdB       = n_magdB;     // number of points in magdB that covers 0 ... MAX_F_HZ of spectrum
    m_max_mag_db    = max_mag_db;
    m_min_mag_db    = min_mag_db;
    m_rxFreq        = 0.0;
    m_clickTune     = clickTune;
    
    m_prevMagDB = new float[n_magdB];
    assert(m_prevMagDB != nullptr);
    
    m_nextPrevMagDB = new float[n_magdB];
    assert(m_nextPrevMagDB != nullptr);
    
    for (int index = 0; index < n_magdB; index++)
    {
        m_prevMagDB[index] = 0;
        m_nextPrevMagDB[index] = 0;
    }
}

//----------------------------------------------------------------
// ~PlotSpectrum()
//----------------------------------------------------------------
PlotSpectrum::~PlotSpectrum()
{
    delete[] m_prevMagDB;
    m_prevMagDB = nullptr;
    
    delete[] m_nextPrevMagDB;
    m_nextPrevMagDB = nullptr;
}

//----------------------------------------------------------------
// OnSize()
//----------------------------------------------------------------
void PlotSpectrum::OnSize(wxSizeEvent&) {
    // Determine correct left offset based on font size
    int text_w = 0;
    int text_h = 0;
    leftOffset_ = 0;
    bottomOffset_ = 0;
    for(float mag=m_min_mag_db; mag<=m_max_mag_db; mag+=STEP_MAG_DB) {
        char buf[STR_LENGTH];
        snprintf(buf, STR_LENGTH, "%3.0fdB", mag);
        GetTextExtent(buf, &text_w, &text_h);
        leftOffset_ = std::max(leftOffset_, text_w);
        bottomOffset_ = std::max(bottomOffset_, text_h);
    }
    bottomOffset_ = std::max(bottomOffset_, (int)YBOTTOM_OFFSET);
}

//----------------------------------------------------------------
// OnShow()
//----------------------------------------------------------------
void PlotSpectrum::OnShow(wxShowEvent&)
{
}

//----------------------------------------------------------------
// draw()
//----------------------------------------------------------------
void PlotSpectrum::draw(wxGraphicsContext* ctx, bool repaintDataOnly)
{
    m_rCtrl  = GetClientRect();

    // m_rGrid is coords of inner window we actually plot to.  We deflate it a bit
    // to leave room for axis labels.  We need to work this out every time we draw
    // as OnSize() may not be called before OnPaint(), for example when a new tab
    // is selected

    m_rGrid  = m_rCtrl;
    m_rGrid = m_rGrid.Deflate(PLOT_BORDER + (leftOffset_/2), (PLOT_BORDER + (bottomOffset_/2)));

    // black background

    wxBrush ltGraphBkgBrush = wxBrush(BLACK_COLOR);
    ctx->SetBrush(ltGraphBkgBrush);
    ctx->SetPen(wxPen(BLACK_COLOR, 0));
    ctx->DrawRectangle(PLOT_BORDER + leftOffset_, PLOT_BORDER + bottomOffset_, m_rGrid.GetWidth(), m_rGrid.GetHeight());

    // draw spectrum

    int   x, y, prev_x, index;
    float index_to_px, mag_dB_to_py, mag;

    m_newdata = false;

    wxPen pen;
    pen.SetColour(DARK_GREEN_COLOR);
    pen.SetWidth(1);
    ctx->SetPen(pen);

    index_to_px = (float)m_rGrid.GetWidth()/m_n_magdB;
    mag_dB_to_py = (float)m_rGrid.GetHeight()/(m_max_mag_db - m_min_mag_db);

    prev_x = PLOT_BORDER + leftOffset_;

    auto freq_hz_to_px = (float)m_rGrid.GetWidth()/(MAX_F_HZ-MIN_F_HZ);

    wxGraphicsPath path = ctx->CreatePath();
    for(index = 0; index < m_n_magdB; index++)
    {
        x = index*index_to_px;

        switch(m_numSampleAveraging)
        {
            case 1:
                mag = m_magdB[index];
                break;
            case 2:
                mag = (m_magdB[index] + m_prevMagDB[index]) / 2;
                break;
            case 3:
                mag = (m_magdB[index] + m_prevMagDB[index] + m_nextPrevMagDB[index]) / 3;
                break;
            default:
                assert(0);
                mag = m_magdB[index]; // assume m_numSampleAveraging = 1
                break;
        }
        
        m_nextPrevMagDB[index] = m_prevMagDB[index];
        m_prevMagDB[index] = m_magdB[index];
        
        if (mag > m_max_mag_db) mag = m_max_mag_db;
        if (mag < m_min_mag_db) mag = m_min_mag_db;
        y = -(mag - m_max_mag_db) * mag_dB_to_py;

        x += PLOT_BORDER + leftOffset_;
        y += PLOT_BORDER + bottomOffset_;

        // Measure the spacing from the last point drawn rather than the previous bin: bins
        // can be closer together than HZ_GRANULARITY, and then on a wide enough plot no
        // bin was ever far enough from the one before it and the trace disappeared.
        if (index && (int)abs(x - prev_x) >= (int)(HZ_GRANULARITY*freq_hz_to_px))
        {
            path.AddLineToPoint(x, y);
            prev_x = x;
        }
        if (!index)
        {
            path.MoveToPoint(x, y);
            prev_x = x;
        }
    }
    ctx->StrokePath(path);

    // and finally draw Graticule
    drawGraticuleFast(ctx, repaintDataOnly);
}

//-------------------------------------------------------------------------
// drawGraticuleLines_()
//-------------------------------------------------------------------------
void PlotSpectrum::drawGraticuleLines_(wxGraphicsContext* ctx)
{
    int      x, y;
    float    f, mag, freq_hz_to_px, mag_dB_to_py;

    wxBrush ltGraphBkgBrush;
    wxColour foregroundColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    ltGraphBkgBrush.SetStyle(wxBRUSHSTYLE_TRANSPARENT);
    ltGraphBkgBrush.SetColour(foregroundColor);
    ctx->SetBrush(ltGraphBkgBrush);

    freq_hz_to_px = (float)m_rGrid.GetWidth()/(MAX_F_HZ-MIN_F_HZ);
    mag_dB_to_py = (float)m_rGrid.GetHeight()/(m_max_mag_db - m_min_mag_db);

    // upper LH coords of plot area are (PLOT_BORDER + leftOffset_, PLOT_BORDER)
    // lower RH coords of plot area are (PLOT_BORDER + leftOffset_ + m_rGrid.GetWidth(), 
    //                                   PLOT_BORDER + m_rGrid.GetHeight())

    // Vertical gridlines

    ctx->SetPen(m_penShortDash);
    for(f=STEP_F_HZ; f<MAX_F_HZ; f+=STEP_F_HZ) {
        x = f*freq_hz_to_px;
        x += PLOT_BORDER + leftOffset_;
        ctx->StrokeLine(x, m_rGrid.GetHeight() + PLOT_BORDER + bottomOffset_, x, PLOT_BORDER + bottomOffset_);
    }

    ctx->SetPen(wxPen(foregroundColor, 1));
    for(f=STEP_MINOR_F_HZ; f<MAX_F_HZ; f+=STEP_MINOR_F_HZ)
    {
        x = f*freq_hz_to_px;
        x += PLOT_BORDER + leftOffset_;
        int tickLen = (fmodf(f, (float)STEP_F_HZ) < 1.0f) ? YBOTTOM_TEXT_OFFSET * 2 : YBOTTOM_TEXT_OFFSET;
        ctx->StrokeLine(x, PLOT_BORDER + bottomOffset_, x, PLOT_BORDER + bottomOffset_ - tickLen);
    }

    // Horizontal gridlines

    ctx->SetPen(m_penDotDash);
    for(mag=m_min_mag_db; mag<=m_max_mag_db; mag+=STEP_MAG_DB) {
        y = -(mag - m_max_mag_db) * mag_dB_to_py;
        y += PLOT_BORDER + bottomOffset_;
        ctx->StrokeLine(PLOT_BORDER + leftOffset_, y,
                (m_rGrid.GetWidth() + PLOT_BORDER + leftOffset_), y);
    }
}

//-------------------------------------------------------------------------
// drawGraticuleLabels_()
//-------------------------------------------------------------------------
void PlotSpectrum::drawGraticuleLabels_(wxGraphicsContext* ctx)
{
    int      x, y, text_w, text_h;
    char     buf[STR_LENGTH];
    float    f, mag, freq_hz_to_px, mag_dB_to_py;

    wxGraphicsFont tmpFont = ctx->CreateFont(GetFont(), GetForegroundColour());
    ctx->SetFont(tmpFont);

    freq_hz_to_px = (float)m_rGrid.GetWidth()/(MAX_F_HZ-MIN_F_HZ);
    mag_dB_to_py = (float)m_rGrid.GetHeight()/(m_max_mag_db - m_min_mag_db);

    // Check if small screen size means text will overlap

    float centreFreqHz = (MAX_F_HZ - MIN_F_HZ) / 2.0f;

    int textXStep = STEP_F_HZ*freq_hz_to_px;
    int textYStep = STEP_MAG_DB*mag_dB_to_py;
    snprintf(buf, STR_LENGTH, "%.1fk", (STEP_F_HZ - centreFreqHz)/1000.0f);
    GetTextExtent(buf, &text_w, &text_h);
    int overlappedX = (text_w > textXStep);
    int overlappedY = (text_h > textYStep);

    if (!overlappedX)
    {
        for(f=STEP_F_HZ; f<MAX_F_HZ; f+=STEP_F_HZ) {
            x = f*freq_hz_to_px;
            x += PLOT_BORDER + leftOffset_;

            snprintf(buf, STR_LENGTH, "%.1fk", (f - centreFreqHz)/1000.0f);
            GetTextExtent(buf, &text_w, &text_h);
            ctx->DrawText(buf, x - text_w/2, (PLOT_BORDER + bottomOffset_ - YBOTTOM_TEXT_OFFSET * 2 - text_h) / 2);
        }
    }

    if (!overlappedY)
    {
        for(mag=m_min_mag_db; mag<=m_max_mag_db; mag+=STEP_MAG_DB) {
            y = -(mag - m_max_mag_db) * mag_dB_to_py;
            y += PLOT_BORDER + bottomOffset_;

            snprintf(buf, STR_LENGTH, "%3.0fdB", mag);
            GetTextExtent(buf, &text_w, &text_h);
            ctx->DrawText(buf, PLOT_BORDER + leftOffset_ - text_w - XLEFT_TEXT_OFFSET, y - text_h / 2);
        }
    }
}

//-------------------------------------------------------------------------
// drawGraticuleFast()
//-------------------------------------------------------------------------
void PlotSpectrum::drawGraticuleFast(wxGraphicsContext* ctx, bool repaintDataOnly)
{
    int      x;
    float    freq_hz_to_px;

#if wxCHECK_VERSION(3,2,0)
    // The gridlines and ticks only change when the control's size, colors or magnitude
    // range do, but drawing them was most of the time spent drawing the spectrum (the
    // dashed gridlines especially). So they're rendered once into a transparent bitmap
    // that's then drawn over the spectrum on every repaint. The axis labels are drawn
    // directly instead: on Windows, GDI+ antialiases text in a transparent bitmap against
    // black and makes the edge pixels opaque, so dark text came out looking bold.
    GraticuleKey key;
    key.size = GetClientSize();
    // Logical to physical pixels: 2 on a Retina Mac, but always 1 on Windows, where window
    // sizes are already in physical pixels (GetDPIScaleFactor() would make the bitmap twice
    // too big there, and it'd be drawn at half size).
    key.scale = GetContentScaleFactor();
    key.lineColour = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    key.minMagDb = m_min_mag_db;
    key.maxMagDb = m_max_mag_db;
    key.leftOffset = leftOffset_;
    key.bottomOffset = bottomOffset_;
    if (graticuleBitmap_.IsNull() || !(key == graticuleKey_))
    {
        graticuleBitmap_ = wxGraphicsBitmap();
        if (key.size.GetWidth() > 0 && key.size.GetHeight() > 0)
        {
            wxBitmap bitmap;
            bitmap.CreateWithDIPSize(key.size, key.scale, 32);
            bitmap.UseAlpha();
            {
                wxMemoryDC dc(bitmap);
                wxGraphicsContext* bitmapCtx = wxGraphicsContext::Create(dc);
                if (bitmapCtx != nullptr)
                {
                    bitmapCtx->SetCompositionMode(wxCOMPOSITION_CLEAR);
                    bitmapCtx->DrawRectangle(0, 0, key.size.GetWidth(), key.size.GetHeight());
                    bitmapCtx->SetCompositionMode(wxCOMPOSITION_OVER);
                    drawGraticuleLines_(bitmapCtx);
                    delete bitmapCtx;
                }
            }
            graticuleBitmap_ = ctx->CreateBitmap(bitmap);
        }
        graticuleKey_ = key;
    }
    if (!graticuleBitmap_.IsNull())
    {
        ctx->DrawBitmap(graticuleBitmap_, 0, 0, key.size.GetWidth(), key.size.GetHeight());
    }
#else
    // Older wxWidgets can't create a bitmap with an alpha channel to cache the graticule
    // in, so draw it directly every time.
    drawGraticuleLines_(ctx);
#endif // wxCHECK_VERSION(3,2,0)

    // Outside the plot area, the paint buffer is only cleared when the whole control is
    // repainted, so only draw the labels then (drawing them again on top of themselves
    // would thicken their antialiased edges).
    if (!repaintDataOnly)
    {
        drawGraticuleLabels_(ctx);
    }

    freq_hz_to_px = (float)m_rGrid.GetWidth()/(MAX_F_HZ-MIN_F_HZ);

    // red rx tuning line
    if (!repaintDataOnly)
    {
        if (m_rxFreq != 0.0) {
            float verticalBarLength = PLOT_BORDER + YBOTTOM_TEXT_OFFSET + 5;
            float sum = 0.0;
            for (auto& f : rxOffsets_)
            {
                sum += f;
            }

            float averageOffset = rxOffsets_.size() == 0 ? 0 : sum / rxOffsets_.size();
   
            // get average offset and draw sync tuning line
            ctx->SetPen(wxPen(sync_ ? GREEN_COLOR : ORANGE_COLOR, 3));
            x = (m_rxFreq + averageOffset) * freq_hz_to_px;
            x += PLOT_BORDER + leftOffset_;
            ctx->StrokeLine(x, 0, x, verticalBarLength);
   
            // red rx tuning line
            ctx->SetPen(wxPen(RED_COLOR, 3));
            x = m_rxFreq*freq_hz_to_px;
            x += PLOT_BORDER + leftOffset_;
            ctx->StrokeLine(x, 0, x, 2 * verticalBarLength / 3);
        }
    }
}

//-------------------------------------------------------------------------
// OnDoubleClickCommon()
//-------------------------------------------------------------------------
void PlotSpectrum::OnDoubleClickCommon(wxMouseEvent& event)
{
    m_mouseDown = true;
    wxClientDC dc(this);

    wxPoint pt(event.GetLogicalPosition(dc));

    // map x coord to edges of actual plot
    pt.x -= PLOT_BORDER + leftOffset_;
    pt.y -= PLOT_BORDER;

    // valid click if inside of plot
    if ((pt.x >= 0) && (pt.x <= m_rGrid.GetWidth()) && (pt.y >=0) && m_clickTune) {
        float freq_hz_to_px = (float)m_rGrid.GetWidth()/(MAX_F_HZ-MIN_F_HZ);
        int clickFreq = (int)((float)pt.x/freq_hz_to_px);
        clickFreq -= clickFreq % 10;

        // see PlotWaterfall::OnMouseDown()

        clickTune(clickFreq);
    }
}

//-------------------------------------------------------------------------
// OnLeftMouseDown()
//-------------------------------------------------------------------------
void PlotSpectrum::OnMouseLeftDoubleClick(wxMouseEvent& event)
{
    OnDoubleClickCommon(event);
}

//-------------------------------------------------------------------------
// OnRightMouseDown()
//-------------------------------------------------------------------------
void PlotSpectrum::OnMouseRightDoubleClick(wxMouseEvent& event)
{
    OnDoubleClickCommon(event);
}

//-------------------------------------------------------------------------
// OnMouseWheelMoved()
//-------------------------------------------------------------------------
void PlotSpectrum::OnMouseWheelMoved(wxMouseEvent& event)
{
    float currRxFreq = FDMDV_FCENTRE - g_RxFreqOffsetHz.load(std::memory_order_relaxed);
    float direction = 1.0;
    if (event.GetWheelRotation() < 0)
    {
        direction = -1.0;
    }
    
    currRxFreq += direction * 10;
    if (currRxFreq < MIN_F_HZ)
    {
        currRxFreq = MIN_F_HZ;
    }
    else if (currRxFreq > MAX_F_HZ)
    {
        currRxFreq = MAX_F_HZ;
    }
    clickTune(currRxFreq);
}

//-------------------------------------------------------------------------
// OnKeyDown()
//-------------------------------------------------------------------------
void PlotSpectrum::OnKeyDown(wxKeyEvent& event)
{
    float currRxFreq = FDMDV_FCENTRE - g_RxFreqOffsetHz.load(std::memory_order_relaxed);
    float direction = 0;
    
    switch (event.GetKeyCode())
    {
        case WXK_DOWN:
            direction = -1.0;
            break;
        case WXK_UP:
            direction = 1.0;
            break;
    }
    
    if (direction)
    {
        currRxFreq += direction * 10;
        if (currRxFreq < MIN_F_HZ)
        {
            currRxFreq = MIN_F_HZ;
        }
        else if (currRxFreq > MAX_F_HZ)
        {
            currRxFreq = MAX_F_HZ;
        }
        clickTune(currRxFreq);
    }
}

//-------------------------------------------------------------------------
// OnMouseMiddleDown()
//-------------------------------------------------------------------------
void PlotSpectrum::OnMouseMiddleDown(wxMouseEvent&)
{
    clickTune(FDMDV_FCENTRE);
}

void PlotSpectrum::refreshData()
{
    // Force redraw of entire control if not using RADE. This is determined
    // by rxFreq == 0. We need to do this so that the frequency indicators
    // redraw properly.
    /*if (m_rxFreq == 0)
    {
        int screenX = PLOT_BORDER + leftOffset_;
        int screenY = PLOT_BORDER;
        RefreshRect(wxRect(screenX, screenY, m_rGrid.GetWidth(), m_rGrid.GetHeight()), false);
    }
    else*/
    {
        Refresh();
    }
}

bool PlotSpectrum::repaintAll_(wxPaintEvent&)
{       
    wxRect plotRegion(
        PLOT_BORDER + leftOffset_,
        PLOT_BORDER,
        m_rGrid.GetWidth(),
        m_rGrid.GetHeight());
    wxRegionIterator upd(GetUpdateRegion());
    while (upd)
    {   
        wxRect rect(upd.GetRect());
        if (!plotRegion.Contains(rect))
        {
            return true;
        }
        upd++;
    }
    return false;
}
