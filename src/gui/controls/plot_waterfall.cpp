//==========================================================================
// Name:            plot_waterfall.cpp
// Purpose:         Implements a waterfall plot derivative of plot.
// Created:         June 22, 2012
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
#include <string.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

#include <wx/wx.h>
#include "os/os_interface.h"

#include "defines.h"
#include "plot_waterfall.h"

#if defined(__APPLE__)
wxGraphicsBitmap CreateWaterfallBitmapInWindowColorSpace(wxGraphicsContext* gc, wxWindow* window, const wxImage& image);
WaterfallCanvas* CreateWaterfallCanvas(wxWindow* window, int width, int height);
void DestroyWaterfallCanvas(WaterfallCanvas* canvas);
bool WaterfallCanvasHasSize(const WaterfallCanvas* canvas, int width, int height);
void WaterfallCanvasPushBlock(WaterfallCanvas* canvas, const wxImage& block);
wxGraphicsBitmap WaterfallCanvasGetBitmap(WaterfallCanvas* canvas, wxGraphicsContext* gc);
#endif // defined(__APPLE__)

// Tweak accordingly
#define Y_PER_SECOND (30) 

constexpr int STR_LENGTH = 15;

extern std::atomic<float> g_RxFreqOffsetHz;
void clickTune(float frequency); // callback to pass new click freq

BEGIN_EVENT_TABLE(PlotWaterfall, PlotPanel)
    EVT_PAINT           (PlotWaterfall::OnPaint)
    EVT_MOTION          (PlotWaterfall::OnMouseMove)
    EVT_LEFT_DCLICK     (PlotWaterfall::OnMouseLeftDoubleClick)
    EVT_LEFT_UP         (PlotWaterfall::OnMouseLeftUp)
    EVT_RIGHT_DCLICK    (PlotWaterfall::OnMouseRightDoubleClick)
    EVT_MIDDLE_DOWN     (PlotWaterfall::OnMouseMiddleDown)
    EVT_MOUSEWHEEL      (PlotWaterfall::OnMouseWheelMoved)
    EVT_SIZE            (PlotWaterfall::OnSize)
    EVT_SHOW            (PlotWaterfall::OnShow)
    EVT_KEY_DOWN        (PlotWaterfall::OnKeyDown)
    EVT_SYS_COLOUR_CHANGED(PlotWaterfall::OnSysColourChanged)
END_EVENT_TABLE()

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// Class WaterfallPlot
//
// @class   WaterfallPlot
// @author  David Witten
// @date    $(Date)
// @file    $(CurrentFileName).$(CurrentFileExt)
// @brief
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
PlotWaterfall::PlotWaterfall(wxWindow* parent, float* magDb, bool graticule, int colour): PlotPanel(parent)
{
    WXWIDGETS_SET_LAYOUT_DIRECTION;
    
    // Note the bound: plotPixelData() clamps intensity to 255 and reaches it whenever a
    // bin sits at the top of the current range, which happens on essentially every frame
    // carrying signal. Filling only 0..254 left the last entry holding indeterminate
    // memory, and the hottest pixels took their colour from it.
    for(int i = 0; i < 256; i++)
    {
        m_heatmap_lut[i] = heatmap((float)i, 0.0, 255.0);
    }
    m_magDb         = magDb;
    m_graticule     = graticule;
    m_colour        = colour;
    m_Bufsz         = GetMaxClientSize();
    m_newdata       = false;
    m_firstPass     = true;
    m_rxFreq = 0;
    m_line_color    = 0;
    m_modem_stats_max_f_hz = MODEM_STATS_MAX_F_HZ;
    dyImageData_ = nullptr;
    dy_ = 0;
    tmpImage_ = nullptr;
#if defined(__APPLE__)
    canvas_ = nullptr;
#endif // defined(__APPLE__)
    leftOffset_ = 0;
    graticuleLabelsValid_ = false;
    graticuleBitmapScale_ = 0;
    graticuleBitmapValid_ = false;

    SetLabelSize(10.0);

    m_max_mag = MAX_MAG_DB;
    m_min_mag = MIN_MAG_DB;
    sync_ = false;
}

// When the window size gets set we can work outthe size of the window
// we plot in and allocate a bit map of the correct size
void PlotWaterfall::OnSize(wxSizeEvent& event) 
{
    // resize bit map
    m_rCtrl  = GetClientRect();

    // Calculate maximum text width and use that for left offset
    int text_w = 0;
    int text_h = 0;
    int y = 0;
    float time = 0;
    leftOffset_ = 0;
    for(y = PLOT_BORDER + YBOTTOM_TEXT_OFFSET, time=0.0f;
        y < m_rCtrl.GetHeight();
        time += WATERFALL_SECS_STEP, y += Y_PER_SECOND * WATERFALL_SECS_STEP)
    {
        char buf[STR_LENGTH];
        snprintf(buf, STR_LENGTH, "%3.0fs", time);
        GetTextExtent(buf, &text_w, &text_h);
        leftOffset_ = std::max(leftOffset_, text_w);
    }

    // m_rGrid is coords of inner window we actually plot to.  We deflate it a bit
    // to leave room for axis labels.

    m_rGrid  = m_rCtrl;
    m_rGrid = m_rGrid.Deflate(PLOT_BORDER + (leftOffset_/2), (PLOT_BORDER + (YBOTTOM_OFFSET/2)));

    // we want a bit map the size of m_rGrid

    m_imgHeight = std::max(1,m_rGrid.GetHeight());
    m_imgWidth = std::max(1,m_rGrid.GetWidth());

    // Reset waterfall to black.
    cleanupSlices_();

    // Label positions depend on the geometry we just recalculated.
    graticuleLabelsValid_ = false;
    graticuleBitmapValid_ = false;

    m_dT = DT;

    event.Skip();
}

//----------------------------------------------------------------
// OnShow()
//----------------------------------------------------------------
void PlotWaterfall::OnShow(wxShowEvent&)
{
}

//----------------------------------------------------------------
// ~PlotWaterfall()
//----------------------------------------------------------------
PlotWaterfall::~PlotWaterfall()
{
    cleanupSlices_();
}

//----------------------------------------------------------------
// heatmap()
// map val to a rgb colour
// from http://eddiema.ca/2011/01/21/c-sharp-heatmaps/
//----------------------------------------------------------------
unsigned PlotWaterfall::heatmap(float val, float min, float max)
{
    unsigned r = 0;
    unsigned g = 0;
    unsigned b = 0;

    val = (val - min) / (max - min);
    if(val <= 0.2)
    {
        b = (unsigned)((val / 0.2) * 255);
    }
    else if(val >  0.2 &&  val <= 0.7)
    {
        b = (unsigned)((1.0 - ((val - 0.2) / 0.5)) * 255);
    }
    if(val >= 0.2 &&  val <= 0.6)
    {
        g = (unsigned)(((val - 0.2) / 0.4) * 255);
    }
    else if(val >  0.6 &&  val <= 0.9)
    {
        g = (unsigned)((1.0 - ((val - 0.6) / 0.3)) * 255);
    }
    if(val >= 0.5)
    {
        r = (unsigned)(((val - 0.5) / 0.5) * 255);
    }
    //printf("%f %x %x %x\n", val, r, g, b);
    return  (b << 16) + (g << 8) + r;
}

bool PlotWaterfall::checkDT(void)
{
    // Check dY is > 1 pixel before proceeding. For small screens
    // and large WATERFALL_SECS_Y we might have less than one
    // block per pixel.  In this case increase m_dT and perform draw
    // less often

    float px_per_sec = Y_PER_SECOND;
    float dy = m_dT * px_per_sec;
    
    if (dy < 1.0) {
        m_dT += DT;
        return false;
    }
    else
        return true;
}

bool PlotWaterfall::repaintAll_(wxPaintEvent&)
{
    wxRect waterfallRegion(PLOT_BORDER + leftOffset_, PLOT_BORDER + YBOTTOM_OFFSET, m_imgWidth, m_imgHeight);
    wxRegionIterator upd(GetUpdateRegion());
    while (upd)
    {   
        wxRect rect(upd.GetRect());
        if (!waterfallRegion.Contains(rect))
        {
            return true;
        }       
        upd++;
    }
    return false;
}               
    
void PlotWaterfall::refreshData()
{
    // Force redraw of entire control if not using RADE. This is determined
    // by rxFreq == 0. We need to do this so that the frequency indicators
    // redraw properly.
    if (m_rxFreq == 0)
    {
        int screenX = PLOT_BORDER + leftOffset_;
        int screenY = PLOT_BORDER + YBOTTOM_OFFSET;
        RefreshRect(wxRect(screenX, screenY, m_imgWidth, m_imgHeight), false);
    }
    else
    {
        Refresh();
    }   
}

//----------------------------------------------------------------
// draw()
//----------------------------------------------------------------
void PlotWaterfall::draw(wxGraphicsContext* gc, bool repaintDataOnly)
{
    m_rCtrl  = GetClientRect();

    // m_rGrid is coords of inner window we actually plot to.  We deflate it a bit
    // to leave room for axis labels.

    m_rGrid = m_rCtrl;
    m_rGrid = m_rGrid.Deflate(PLOT_BORDER + (leftOffset_/2), (PLOT_BORDER + (YBOTTOM_OFFSET/2)));

    // we want a bit map the size of m_rGrid
    wxBrush ltGraphBkgBrush = wxBrush(BLACK_COLOR);
    
    if(m_newdata)
    {
        m_newdata = false;
        plotPixelData(gc);
    }

    int yOffset = 0;
#if defined(__APPLE__)
    if (canvas_ != nullptr)
    {
        wxGraphicsBitmap bitmap = WaterfallCanvasGetBitmap(canvas_, gc);
        if (!bitmap.IsNull())
        {
            // The canvas is opaque, so copy it rather than blending it over what's
            // underneath: CoreGraphics then does a straight memory copy.
            gc->SetCompositionMode(wxCOMPOSITION_SOURCE);
            gc->DrawBitmap(bitmap, PLOT_BORDER + leftOffset_, PLOT_BORDER + YBOTTOM_OFFSET, m_imgWidth, m_imgHeight);
            gc->SetCompositionMode(wxCOMPOSITION_OVER);
            yOffset = m_imgHeight;
        }
    }
#endif // defined(__APPLE__)
    for (auto& slice : waterfallSlices_)
    {
        gc->DrawBitmap(slice.gfxBitmap, PLOT_BORDER + leftOffset_, yOffset + PLOT_BORDER + YBOTTOM_OFFSET, m_imgWidth, slice.height);
        yOffset += slice.height;
    }

    if (yOffset < m_imgHeight)
    {
        // Reset waterfall to black
        wxBrush ltGraphBkgBrush = wxBrush(BLACK_COLOR);
        gc->SetBrush(ltGraphBkgBrush);
        gc->SetPen(wxPen(BLACK_COLOR, 0));
        gc->DrawRectangle(PLOT_BORDER + leftOffset_, PLOT_BORDER + YBOTTOM_OFFSET + yOffset, m_imgWidth, m_imgHeight - yOffset); 
    }

    if (!repaintDataOnly)
    {
        drawGraticule(gc);
    }
}

//-------------------------------------------------------------------------
// rebuildGraticuleLabels_()
//
// Lays out the axis labels and caches where each one goes. Only the control's
// geometry decides that, so this runs on resize rather than on every frame --
// GetTextExtent() is expensive enough (wxWindowMac::DoGetTextExtent builds and
// tears down a wxGraphicsContext per call) to be worth keeping out of the
// per-frame path.
//-------------------------------------------------------------------------
void PlotWaterfall::rebuildGraticuleLabels_()
{
    int   text_w, text_h;
    char  buf[STR_LENGTH];
    float f, time;

    freqLabels_.clear();
    timeLabels_.clear();

    float freq_hz_to_px = (float)m_imgWidth/(MAX_F_HZ-MIN_F_HZ);

    // Check if small screen size means text will overlap

    float centreFreqHz = (MAX_F_HZ - MIN_F_HZ) / 2.0f;

    int textXStep = STEP_F_HZ * freq_hz_to_px;
    snprintf(buf, STR_LENGTH, "%.1fk", (STEP_F_HZ - centreFreqHz)/1000.0f);
    GetTextExtent(buf, &text_w, &text_h);
    bool overlappedX = (text_w > textXStep);

    if (!overlappedX)
    {
        for(f=STEP_F_HZ; f<MAX_F_HZ; f+=STEP_F_HZ)
        {
            int x = f*freq_hz_to_px;
            x += PLOT_BORDER + leftOffset_;

            snprintf(buf, STR_LENGTH, "%.1fk", (f - centreFreqHz)/1000.0f);
            GetTextExtent(buf, &text_w, &text_h);
            freqLabels_.push_back({
                wxString(buf),
                x - text_w/2,
                (PLOT_BORDER + YBOTTOM_OFFSET / 2 - text_h) / 2 });
        }
    }

    // Pick the coarsest Y label interval that still shows at least 4 labels
    static const int labelStepPresets[] = {5, 2, 1};
    int labelSecs = labelStepPresets[2]; // default finest
    for (int preset : labelStepPresets)
    {
        if (m_imgHeight / (preset * Y_PER_SECOND) >= 4)
        {
            labelSecs = preset;
            break;
        }
    }

    int dataY0   = PLOT_BORDER + YBOTTOM_OFFSET;
    int dataYEnd = dataY0 + m_imgHeight;
    for(int y = dataY0, t = 0; y < dataYEnd; t++, y += Y_PER_SECOND)
    {
        time = (float)t;
        if ((int)(time + 0.5f) % labelSecs != 0) continue;

        snprintf(buf, STR_LENGTH, "%3.0fs", time);
        GetTextExtent(buf, &text_w, &text_h);
        timeLabels_.push_back({
            wxString(buf),
            PLOT_BORDER + leftOffset_ - text_w - XLEFT_TEXT_OFFSET,
            y - text_h / 2 });
    }

    graticuleLabelsValid_ = true;
}

//-------------------------------------------------------------------------
// OnSysColourChanged()
//-------------------------------------------------------------------------
void PlotWaterfall::OnSysColourChanged(wxSysColourChangedEvent& event)
{
    // The cached graticule uses the system text colour (e.g. light/dark mode).
    graticuleBitmapValid_ = false;
    Refresh();
    event.Skip();
}

//-------------------------------------------------------------------------
// cropImage()
//
// Stands in for wxImage::GetSubImage(), which offsets the image's alpha pointer
// even when it's null (i.e. the image has no alpha) -- undefined behaviour that
// UBSan reports. The rectangle is clamped to the image.
//-------------------------------------------------------------------------
static wxImage cropImage(const wxImage& src, wxRect rect)
{
    rect.Intersect(wxRect(0, 0, src.GetWidth(), src.GetHeight()));
    int width = std::max(1, rect.GetWidth());
    int height = std::max(1, rect.GetHeight());

    wxImage dst(width, height, true);
    if (rect.IsEmpty())
    {
        return dst;
    }

    const unsigned char* srcData = src.GetData();
    unsigned char* dstData = dst.GetData();
    for (int y = 0; y < height; y++)
    {
        memcpy(dstData + y * width * 3, srcData + ((rect.GetTop() + y) * src.GetWidth() + rect.GetLeft()) * 3, width * 3);
    }

    if (src.HasAlpha())
    {
        dst.SetAlpha();
        const unsigned char* srcAlpha = src.GetAlpha();
        unsigned char* dstAlpha = dst.GetAlpha();
        for (int y = 0; y < height; y++)
        {
            memcpy(dstAlpha + y * width, srcAlpha + (rect.GetTop() + y) * src.GetWidth() + rect.GetLeft(), width);
        }
    }

    return dst;
}

//-------------------------------------------------------------------------
// rebuildGraticuleBitmaps_()
//-------------------------------------------------------------------------
void PlotWaterfall::rebuildGraticuleBitmaps_(wxGraphicsContext* ctx)
{
    double scale = GetContentScaleFactor();
    wxSize size = GetClientSize();
    size.SetWidth(std::max(1, size.GetWidth()));
    size.SetHeight(std::max(1, size.GetHeight()));

    // Render the whole graticule once, on the same background OnPaint() clears to
    // and at the display's pixel density, then keep just the margins.
    // (CreateScaled() rather than CreateWithDIPSize(), which needs wxWidgets 3.1.6+.)
    wxBitmap bitmap;
    bitmap.CreateScaled(size.GetWidth(), size.GetHeight(), wxBITMAP_SCREEN_DEPTH, scale);
    {
        wxMemoryDC dc(bitmap);
        dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)));
        dc.Clear();

        // Create the label font from the window's context: CreateFont() sizes it for its
        // own context's DPI, and on Windows a memory DC's is the default 96 rather than
        // the window's, which drew the labels too small on scaled displays.
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        drawStaticGraticule_(gc.get(), ctx->CreateFont(GetFont(), GetForegroundColour()));
    }
    wxImage image = bitmap.ConvertToImage();

    int dataX0 = PLOT_BORDER + leftOffset_;
    int dataY0 = PLOT_BORDER + YBOTTOM_OFFSET;
    auto toPixels = [&](int v, int limit) { return std::clamp((int)std::lround(v * scale), 1, limit); };
    int topHeightPx = toPixels(dataY0, image.GetHeight());
    wxImage top = cropImage(image, wxRect(0, 0, image.GetWidth(), topHeightPx));
    wxImage left = cropImage(image, wxRect(
        0, topHeightPx,
        toPixels(dataX0, image.GetWidth()), std::max(1, image.GetHeight() - topHeightPx)));

#if defined(__APPLE__)
    graticuleTop_ = CreateWaterfallBitmapInWindowColorSpace(ctx, this, top);
    graticuleLeft_ = CreateWaterfallBitmapInWindowColorSpace(ctx, this, left);
#else
    graticuleTop_ = ctx->CreateBitmapFromImage(top);
    graticuleLeft_ = ctx->CreateBitmapFromImage(left);
#endif // defined(__APPLE__)

    graticuleBitmapScale_ = scale;
    graticuleBitmapValid_ = true;
}

//-------------------------------------------------------------------------
// drawGraticule()
//-------------------------------------------------------------------------
void PlotWaterfall::drawGraticule(wxGraphicsContext* ctx)
{
    // The scale changes when the window moves to a display with a different density.
    if (!graticuleBitmapValid_ || graticuleBitmapScale_ != GetContentScaleFactor())
    {
        rebuildGraticuleBitmaps_(ctx);
    }

    wxSize size = GetClientSize();
    int dataX0 = PLOT_BORDER + leftOffset_;
    int dataY0 = PLOT_BORDER + YBOTTOM_OFFSET;
    ctx->DrawBitmap(graticuleTop_, 0, 0, size.GetWidth(), dataY0);
    ctx->DrawBitmap(graticuleLeft_, 0, dataY0, dataX0, std::max(1, size.GetHeight() - dataY0));

    drawDataGridlines_(ctx);

    float freq_hz_to_px = (float)m_imgWidth/(MAX_F_HZ-MIN_F_HZ);
    float verticalBarLength = PLOT_BORDER + YBOTTOM_TEXT_OFFSET + 5;
    
    float sum = 0.0;
    for (auto& f : rxOffsets_)
    {
        sum += f;
    }
    float averageOffset = rxOffsets_.size() == 0 ? 0 : sum / rxOffsets_.size();
    
    if (m_rxFreq != 0.0) {
        int x;

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

//-------------------------------------------------------------------------
// drawDataGridlines_()
//
// The parts of drawStaticGraticule_() that lie over the waterfall itself, which
// changes every frame. Clipped to the waterfall so the margins keep their cached
// pixels exactly.
//-------------------------------------------------------------------------
void PlotWaterfall::drawDataGridlines_(wxGraphicsContext* ctx)
{
    int dataX0   = PLOT_BORDER + leftOffset_;
    int dataY0   = PLOT_BORDER + YBOTTOM_OFFSET;
    int dataYEnd = dataY0 + m_imgHeight;
    float freq_hz_to_px = (float)m_imgWidth/(MAX_F_HZ-MIN_F_HZ);
    wxColour foregroundColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);

    ctx->PushState();
    ctx->Clip(dataX0, dataY0, m_imgWidth, m_imgHeight);

    // Major vertical gridlines and the Y axis ticks share a pen, so stroke them as one path.
    wxGraphicsPath path = ctx->CreatePath();
    if (m_graticule)
    {
        for (float f = STEP_F_HZ; f < MAX_F_HZ; f += STEP_F_HZ)
        {
            int x = f*freq_hz_to_px + dataX0;
            path.MoveToPoint(x, m_imgHeight + PLOT_BORDER);
            path.AddLineToPoint(x, PLOT_BORDER);
        }
    }
    float time = 0;
    for (int y = dataY0; y < dataYEnd; time += 1.0f, y += Y_PER_SECOND)
    {
        bool isMajor = (fmodf(time, (float)WATERFALL_SECS_STEP) < 0.5f);
        path.MoveToPoint(dataX0, y);
        path.AddLineToPoint(dataX0 + (isMajor ? 8 : 4), y);
    }
    ctx->SetPen(wxPen(foregroundColor, 1));
    ctx->StrokePath(path);

    // Horizontal gridlines at 5s intervals (graticule mode only). Stroked separately
    // so each starts its dash pattern afresh, as before.
    if (m_graticule)
    {
        ctx->SetPen(m_penDotDash);
        for (int y = dataY0; y < dataYEnd; y += Y_PER_SECOND * WATERFALL_SECS_STEP)
        {
            ctx->StrokeLine(dataX0, y, m_rGrid.GetWidth() + dataX0, y);
        }
    }

    ctx->PopState();
}

//-------------------------------------------------------------------------
// drawStaticGraticule_()
//
// Everything in the graticule that doesn't move; the margins of it are cached
// (see graticuleTop_/graticuleLeft_) and drawDataGridlines_() redraws the rest.
//-------------------------------------------------------------------------
void PlotWaterfall::drawStaticGraticule_(wxGraphicsContext* ctx, const wxGraphicsFont& font)
{
    int      x, y;
    float    f, time, freq_hz_to_px;

    wxBrush ltGraphBkgBrush;
    wxColour foregroundColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    ltGraphBkgBrush.SetStyle(wxBRUSHSTYLE_TRANSPARENT);
    ltGraphBkgBrush.SetColour(foregroundColor);
    ctx->SetBrush(ltGraphBkgBrush);
    ctx->SetPen(wxPen(foregroundColor, 1));

    ctx->SetFont(font);

    if (!graticuleLabelsValid_)
    {
        rebuildGraticuleLabels_();
    }

    freq_hz_to_px = (float)m_imgWidth/(MAX_F_HZ-MIN_F_HZ);

    // upper LH coords of plot area are (PLOT_BORDER + leftOffset_, PLOT_BORDER)
    // lower RH coords of plot area are (PLOT_BORDER + leftOffset_ + m_rGrid.GetWidth(),
    //                                   PLOT_BORDER + m_rGrid.GetHeight())

    // Major Vertical gridlines
    for(f=STEP_F_HZ; f<MAX_F_HZ; f+=STEP_F_HZ)
    {
        x = f*freq_hz_to_px;
        x += PLOT_BORDER + leftOffset_;

        if (m_graticule)
            ctx->StrokeLine(x, m_imgHeight + PLOT_BORDER, x, PLOT_BORDER);
        else
            ctx->StrokeLine(x, PLOT_BORDER + YBOTTOM_OFFSET, x, PLOT_BORDER + YBOTTOM_OFFSET / 2);
    }

    for(f=STEP_MINOR_F_HZ; f<MAX_F_HZ; f+=STEP_MINOR_F_HZ)
    {
        x = f*freq_hz_to_px;
        x += PLOT_BORDER + leftOffset_;
        ctx->StrokeLine(x, PLOT_BORDER + YBOTTOM_OFFSET, x, PLOT_BORDER + YBOTTOM_OFFSET * 3 / 4);
    }

    // Frequency legend
    for (const auto& label : freqLabels_)
    {
        ctx->DrawText(label.text, label.x, label.y);
    }

    int dataY0   = PLOT_BORDER + YBOTTOM_OFFSET;
    int dataYEnd = dataY0 + m_imgHeight;

    // Horizontal gridlines at 5s intervals (graticule mode only)
    ctx->SetPen(m_penDotDash);
    for(y = dataY0, time=0;
        y < dataYEnd;
        time += WATERFALL_SECS_STEP, y += Y_PER_SECOND * WATERFALL_SECS_STEP)
    {
        if (m_graticule)
            ctx->StrokeLine(PLOT_BORDER + leftOffset_, y,
                        (m_rGrid.GetWidth() + PLOT_BORDER + leftOffset_), y);
    }

    // Y axis ticks at 1s intervals
    ctx->SetPen(wxPen(foregroundColor, 1));
    for(y = dataY0, time=0;
        y < dataYEnd;
        time += 1.0f, y += Y_PER_SECOND)
    {
        bool isMajor = (fmodf(time, (float)WATERFALL_SECS_STEP) < 0.5f);
        ctx->StrokeLine(PLOT_BORDER + leftOffset_, y,
                        PLOT_BORDER + leftOffset_ + (isMajor ? 8 : 4), y);
    }

    // Y axis labels
    for (const auto& label : timeLabels_)
    {
        ctx->DrawText(label.text, label.x, label.y);
    }
}

//-------------------------------------------------------------------------
// plotPixelData()
//-------------------------------------------------------------------------
void PlotWaterfall::plotPixelData(wxGraphicsContext* gc)
{
    float       intensity_per_dB;
    float       px_per_sec;
    int         index;
    int         dy;
    int         px;
    int         intensity;

    /*
      Design Notes:

      The height in pixels represents WATERFALL_SECS_Y of data.  Every DT
      seconds we get a vector of MODEM_STATS_NSPEC spectrum samples which we use
      to update the last row.  The height of each row is dy pixels, which
      maps to DT seconds.  We call each dy high rectangle of pixels a
      block.

    */

    // determine dy, the height of one "block"
    px_per_sec = Y_PER_SECOND;
    dy = m_dT * px_per_sec;

    // update min and max amplitude estimates
    float max_mag = MIN_MAG_DB;

    int min_fft_bin=((float)200/m_modem_stats_max_f_hz)*MODEM_STATS_NSPEC;
    int max_fft_bin=((float)2800/m_modem_stats_max_f_hz)*MODEM_STATS_NSPEC;

    for(int i=min_fft_bin; i<max_fft_bin; i++) 
    {
        if (m_magDb[i] > max_mag)
        {
            max_mag = m_magDb[i];
        }
    }

    // A row with every bin sitting exactly at the display floor is the
    // synthetic "blanked" placeholder fed in during half-duplex TX (see
    // main.cpp's "Assume zero spectrum to avoid waterfall artifacts"
    // handling), not real spectrum data -- real RX/RADE spectra essentially
    // never have their loudest bin sitting exactly at MIN_MAG_DB. Render it
    // as plain black and leave the auto-ranging baseline (m_max_mag)
    // untouched, rather than running it through the normal relative
    // intensity scale below, which treats *any* perfectly flat row as "at
    // its own peak" and paints it full-scale regardless of the row's
    // actual absolute level -- confirmed live: filling the placeholder with
    // MIN_MAG_DB alone still produced a colour ramp (green fading to red)
    // as m_max_mag caught up to the new flat level, not the intended blank.
    bool silentRow = (max_mag <= MIN_MAG_DB);
    if (!silentRow)
    {
        m_max_mag = BETA*m_max_mag + (1 - BETA)*max_mag;
    }
    m_min_mag = max_mag - 20.0;
    intensity_per_dB  = (float)256 /(m_max_mag - m_min_mag);

    // Draw last line of blocks using latest amplitude data ------------------
    int baseRowWidthPixels = ((float)MODEM_STATS_NSPEC / (float)m_modem_stats_max_f_hz) * MAX_F_HZ;
    if (dy_ != dy && dyImageData_ != nullptr)
    {
        delete[] dyImageData_;
        dyImageData_ = nullptr;

        delete tmpImage_;
    }
    if (dyImageData_ == nullptr)
    {
        dyImageData_ = new unsigned char[(dy > 0 ? dy : 1) * 3 * baseRowWidthPixels];
        assert(dyImageData_ != nullptr);

        memset(dyImageData_, 0, (dy > 0 ? dy : 1) * 3 * baseRowWidthPixels);
        tmpImage_ = new wxImage(baseRowWidthPixels, dy, (unsigned char*)dyImageData_, true);
    }
    dy_ = dy;

    for(px = 0; px < baseRowWidthPixels; px++)
    {
        index = px;
        assert(index < MODEM_STATS_NSPEC);

        intensity = silentRow ? 0 : (int)std::clamp((intensity_per_dB * (m_magDb[index] - m_min_mag)), 0.f, 255.f);

        int pixelPos = (px * 3);
            
        switch (m_colour) {
        case 0:
            dyImageData_[pixelPos] = m_heatmap_lut[intensity] & 0xff;
            dyImageData_[pixelPos + 1] = (m_heatmap_lut[intensity] >> 8) & 0xff;
            dyImageData_[pixelPos + 2] = (m_heatmap_lut[intensity] >> 16) & 0xff;
            break;
        case 1:
            dyImageData_[pixelPos] = intensity;
            dyImageData_[pixelPos + 1] = intensity;
            dyImageData_[pixelPos + 2] = intensity;       
            break;
        case 2:
            dyImageData_[pixelPos] = intensity;
            dyImageData_[pixelPos + 1] = intensity;
            if (intensity < 127)
                dyImageData_[pixelPos + 2] = intensity*2;
            else
                dyImageData_[pixelPos + 2] = 255;
                    
            break;
        }
    }
    
    for (int row = 1; row < dy; row++)
    {
        memcpy(&dyImageData_[row * 3 * baseRowWidthPixels], &dyImageData_[0], 3 * baseRowWidthPixels);
    }

#if 0
    // Force main window's color space to be the same as what wxWidgets uses. This only has an effect
    // on macOS due to how it handles color spaces.
    // XXX - causes noticeable flicker on certain monitors/OS versions, so disabled for now.
    ResetMainWindowColorSpace();
#endif // 0

    if (dy > 0)
    {
        tmpImage_->SetData(dyImageData_, true);

        // Build the block at the display's pixel density rather than at 1x. A 1x block
        // gets upscaled by the renderer on HiDPI displays (e.g. GTK3 under fractional
        // scaling, which renders at 2x), which left dark horizontal seams between
        // blocks. Upscaling the whole plot on every paint was also ~40x slower than
        // drawing it 1:1.
        double scale = GetContentScaleFactor();
        wxImage scaledImage = tmpImage_->Scale(
            std::max(1, (int)std::lround(m_imgWidth * scale)),
            std::max(1, (int)std::lround(dy * scale)),
            wxIMAGE_QUALITY_NEAREST);

#if defined(__APPLE__)
        int canvasWidth = std::max(1, (int)std::lround(m_imgWidth * scale));
        int canvasHeight = std::max(1, (int)std::lround(m_imgHeight * scale));
        if (!WaterfallCanvasHasSize(canvas_, canvasWidth, canvasHeight))
        {
            DestroyWaterfallCanvas(canvas_);
            canvas_ = CreateWaterfallCanvas(this, canvasWidth, canvasHeight);
        }
        if (canvas_ != nullptr)
        {
            WaterfallCanvasPushBlock(canvas_, scaledImage);
            return;
        }
#endif // defined(__APPLE__)

        // Convert once, here, rather than on every paint: a block's pixels never change
        // again, and it will be composited on each of the frames it spends scrolling down
        // the screen.
        WaterfallSlice slice;
        slice.height = dy;
#if defined(__APPLE__)
        slice.gfxBitmap = CreateWaterfallBitmapInWindowColorSpace(gc, this, scaledImage);
#else
        slice.gfxBitmap = gc->CreateBitmapFromImage(scaledImage);
#endif // defined(__APPLE__)

        if (waterfallSlices_.size() >= (size_t)(m_imgHeight / dy))
        {
            waterfallSlices_.pop_back();
        }
        waterfallSlices_.push_front(slice);
    }
}

//-------------------------------------------------------------------------
// OnDoubleClickCommon()
//-------------------------------------------------------------------------
void PlotWaterfall::OnDoubleClickCommon(wxMouseEvent& event)
{
    m_mouseDown = true;
    wxClientDC dc(this);

    wxPoint pt(event.GetLogicalPosition(dc));

    // map x coord to edges of actual plot
    pt.x -= PLOT_BORDER + leftOffset_;
    pt.y -= PLOT_BORDER;

    // valid click if inside of plot
    if ((pt.x >= 0) && (pt.x <= m_imgWidth) && (pt.y >=0)) 
    {
        float freq_hz_to_px = (float)m_imgWidth/(MAX_F_HZ-MIN_F_HZ);
        int clickFreq = (int)((float)pt.x/freq_hz_to_px);
        clickFreq -= clickFreq % 10;
        
        // communicate back to other threads
        clickTune(clickFreq);
    }
}

//-------------------------------------------------------------------------
// OnMouseWheelMoved()
//-------------------------------------------------------------------------
void PlotWaterfall::OnMouseWheelMoved(wxMouseEvent& event)
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
void PlotWaterfall::OnKeyDown(wxKeyEvent& event)
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
// OnMouseLeftDown()
//-------------------------------------------------------------------------
void PlotWaterfall::OnMouseLeftDoubleClick(wxMouseEvent& event)
{
    OnDoubleClickCommon(event);
}

//-------------------------------------------------------------------------
// OnMouseRightDown()
//-------------------------------------------------------------------------
void PlotWaterfall::OnMouseRightDoubleClick(wxMouseEvent& event)
{
    OnDoubleClickCommon(event);
}

//-------------------------------------------------------------------------
// OnMouseMiddleDown()
//-------------------------------------------------------------------------
void PlotWaterfall::OnMouseMiddleDown(wxMouseEvent&)
{
    clickTune(FDMDV_FCENTRE);
}

void PlotWaterfall::cleanupSlices_()
{
    waterfallSlices_.clear();
#if defined(__APPLE__)
    DestroyWaterfallCanvas(canvas_);
    canvas_ = nullptr;
#endif // defined(__APPLE__)

    dy_ = 0;
    if (dyImageData_ != nullptr)
    {
        delete[] dyImageData_;
        dyImageData_ = nullptr;
    }

    delete tmpImage_;
    tmpImage_ = nullptr;
}
