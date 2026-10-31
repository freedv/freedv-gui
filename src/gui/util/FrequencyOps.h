//==========================================================================
// Name:            FrequencyOps.h
// Purpose:         Common code to handle frequency/mode related operations.
// Created:         May 13, 2026
// Authors:         Mooneer Salem
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

#ifndef FREQUENCY_OPS_H
#define FREQUENCY_OPS_H

#include "../../rig_control/HamlibRigController.h"

inline HamlibRigController::Mode GetModeForFrequency(uint64_t frequency, bool useAnalogModes, bool inAnalog)
{
    if (!inAnalog)
    {
        // Always DIGU or USB if Analog decode isn't active.
        return (useAnalogModes) ? HamlibRigController::USB : HamlibRigController::DIGU;
    }

    // Widest 60 meter allocation is 5.250-5.450 MHz per https://en.wikipedia.org/wiki/60-meter_band.
    bool is60MeterBand = 
        frequency >= 5250000 && 
        frequency <= 5450000;
    
    HamlibRigController::Mode lsbMode = HamlibRigController::LSB;
    HamlibRigController::Mode usbMode = HamlibRigController::USB;
    
    HamlibRigController::Mode newMode;
    if (frequency < 10000000 &&
        !is60MeterBand)
    {
        newMode = lsbMode;
    }
    else
    {
        newMode = usbMode;
    }

    return newMode;
}

// centre = dial + offset. Offset is 0 whenever analogActive is true (SSB
// analog has no fixed-tone convention to reference), and otherwise depends
// on sideband only. Callers must derive `mode` via GetModeForFrequency (or
// equivalent) -- this function intentionally has no band-threshold logic of
// its own, so any future change to mode selection (e.g. always-USB digital)
// doesn't require touching this conversion.
inline int32_t GetCentreFreqOffsetHz(HamlibRigController::Mode mode, bool analogActive)
{
    if (analogActive) return 0;
    switch (mode)
    {
        case HamlibRigController::USB:
        case HamlibRigController::DIGU:
            return 1500;
        case HamlibRigController::LSB:
        case HamlibRigController::DIGL:
            return -1500;
        default:
            return 0;
    }
}

inline uint64_t DialToDisplayFreq(uint64_t dialFreq, HamlibRigController::Mode mode, bool analogActive)
{
    return (uint64_t)((int64_t)dialFreq + GetCentreFreqOffsetHz(mode, analogActive));
}

inline uint64_t DisplayToDialFreq(uint64_t displayFreq, HamlibRigController::Mode mode, bool analogActive)
{
    return (uint64_t)((int64_t)displayFreq - GetCentreFreqOffsetHz(mode, analogActive));
}

#endif // FREQUENCY_OPS_H