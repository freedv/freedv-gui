//=========================================================================
// Name:            waterfall_scale.h
// Purpose:         Turns the waterfall's sample rate into the bin range and
//                  row width used to draw one row of spectrum.
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
//=========================================================================

#ifndef WATERFALL_SCALE_H
#define WATERFALL_SCALE_H

#include <algorithm>

// The waterfall shows 200-2800 Hz of the spectrum whatever the sample rate.
constexpr int WATERFALL_BAND_MIN_F_HZ = 200;
constexpr int WATERFALL_BAND_MAX_F_HZ = 2800;

struct WaterfallScale
{
    int firstBin;   // first spectrum bin of the displayed band
    int lastBin;    // one past the last bin of the displayed band
    int rowWidthPx; // pixels one row of spectrum occupies before scaling
};

// One row of spectrum holds binsPerRow bins covering [0, maxFHertz) Hz, and
// is stretched to fill a displayMaxFHertz wide axis, so every index and width
// derived from the sample rate has to stay inside the row. A small maxFHertz
// runs the band and the row width past the end of the row, and a zero or
// negative one turns the divide into a float to int conversion that overflows
// int. Clamping the scaled values keeps the drawing in bounds for whatever
// sample rate the caller is handed.
inline WaterfallScale waterfallScaleFor(int maxFHertz, int binsPerRow, int displayMaxFHertz)
{
    float binsPerHz = (maxFHertz > 0)
                          ? (float)binsPerRow / (float)maxFHertz
                          : (float)binsPerRow;

    WaterfallScale scale;
    scale.firstBin = (int)std::clamp((float)WATERFALL_BAND_MIN_F_HZ * binsPerHz,
                                     0.0f, (float)binsPerRow);
    scale.lastBin = (int)std::clamp((float)WATERFALL_BAND_MAX_F_HZ * binsPerHz,
                                    (float)scale.firstBin, (float)binsPerRow);
    // Never zero: the row's pixel buffer is allocated and drawn from this.
    scale.rowWidthPx = (int)std::clamp((float)displayMaxFHertz * binsPerHz,
                                       1.0f, (float)binsPerRow);
    return scale;
}

#endif // WATERFALL_SCALE_H
