//==========================================================================
// Name:            plot_spectrum.h
// Purpose:         Defines a spectrum plot derived from plot class.
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
#ifndef __FDMDV2_PLOT_SPECTRUM__
#define __FDMDV2_PLOT_SPECTRUM__

#include <wx/graphics.h>

#include "plot.h"
#include "../../defines.h"

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// Class Waterfall
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
class PlotSpectrum : public PlotPanel
{
    public:
        enum MistuneState {
            TOO_LOW = -1,
            UNKNOWN = 0,
            TOO_HIGH = 1
        };

        PlotSpectrum(wxWindow* parent, float *magdB, int n_magdB, 
                 float min_mag_db=MIN_MAG_DB, float max_mag_db=MAX_MAG_DB, bool clickTune=true);
        ~PlotSpectrum();
        void setRxFreq(float rxFreq) { m_rxFreq = rxFreq; }
        void setFreqScale(int n_magdB) { m_n_magdB = n_magdB; }
        void setMistuneState(MistuneState state) { m_mistuneState = state; }

        void setNumAveraging(int n) { m_numSampleAveraging = n; }
        virtual void refreshData() override;

    protected:
        void        OnSize(wxSizeEvent& event) override;
        void        OnShow(wxShowEvent& event) override;
        void        drawGraticuleFast(wxGraphicsContext* ctx, bool repaintDataOnly);
        void        drawGraticuleLines_(wxGraphicsContext* ctx);
        void        drawGraticuleLabels_(wxGraphicsContext* ctx);
        void        draw(wxGraphicsContext* ctx, bool repaintDataOnly = false) override;
        void        OnMouseLeftDoubleClick(wxMouseEvent& event);
        void        OnMouseRightDoubleClick(wxMouseEvent& event);
        void        OnMouseWheelMoved(wxMouseEvent& event) override;
        void        OnMouseMiddleDown(wxMouseEvent& event);
        void        OnKeyDown(wxKeyEvent& event);

   private:
        MistuneState m_mistuneState;
        float       m_rxFreq;
        float       m_max_mag_db;
        float       m_min_mag_db;
        float      *m_magdB;
        float      *m_prevMagDB;
        float      *m_nextPrevMagDB;
        int         m_n_magdB;  
        bool        m_clickTune;
        int         m_numSampleAveraging;
        int         leftOffset_;
        int         bottomOffset_;

#if wxCHECK_VERSION(3,2,0)
        // The graticule's gridlines and ticks rendered once into a transparent bitmap,
        // and what they were rendered for; see drawGraticuleFast().
        struct GraticuleKey
        {
            wxSize size;
            double scale = 0;
            wxColour lineColour;
            float minMagDb = 0;
            float maxMagDb = 0;
            int leftOffset = 0;
            int bottomOffset = 0;

            bool operator==(const GraticuleKey& other) const
            {
                return size == other.size && scale == other.scale && lineColour == other.lineColour &&
                    minMagDb == other.minMagDb && maxMagDb == other.maxMagDb &&
                    leftOffset == other.leftOffset && bottomOffset == other.bottomOffset;
            }
        };
        GraticuleKey graticuleKey_;
        wxGraphicsBitmap graticuleBitmap_;
#endif // wxCHECK_VERSION(3,2,0)

        void        OnDoubleClickCommon(wxMouseEvent& event);

        virtual bool repaintAll_(wxPaintEvent& evt) override;
        
        DECLARE_EVENT_TABLE()
};

#endif //__FDMDV2_PLOT_SPECTRUM__
