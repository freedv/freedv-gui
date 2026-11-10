//=========================================================================
// Name:            waterfall_row.h
// Purpose:         Tells the waterfall's blanked TX placeholder rows apart
//                  from real spectrum data.
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

#ifndef WATERFALL_ROW_H
#define WATERFALL_ROW_H

// A row with every bin in [fromBin, toBin) sitting exactly at the display
// floor is the synthetic "blanked" placeholder fed in during half-duplex TX
// (see main.cpp's "Assume zero spectrum to avoid waterfall artifacts"
// handling), not real spectrum data. Testing only the row's peak against
// the floor isn't enough: weak but real signals (e.g. with the radio's
// audio level turned well down) can peak below the floor and must still be
// drawn (PR #1618).
inline bool IsBlankedWaterfallRow(const float* magDb, int fromBin, int toBin, float floorDb)
{
    for (int i = fromBin; i < toBin; i++)
    {
        if (magDb[i] != floorDb)
        {
            return false;
        }
    }
    return true;
}

#endif // WATERFALL_ROW_H
