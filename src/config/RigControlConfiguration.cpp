//==========================================================================
// Name:            RigControlConfiguration.cpp
// Purpose:         Implements the rig control configuration for FreeDV
// Created:         July 2, 2023
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

#include "RigControlConfiguration.h"

RigControlConfiguration::RigControlConfiguration()
    : hamlibUseForPTT("/Hamlib/UseForPTT", false)
    , hamlibEnableFreqModeChanges("/Hamlib/EnableFreqModeChanges", true)
    , hamlibEnableFreqChangesOnly("/Hamlib/EnableFreqChangesOnly", false)
    , hamlibUseAnalogModes("/Hamlib/UseAnalogModes", false)
    , hamlibSaveRestoreFrequency("/Hamlib/SaveRestoreFrequency", true)
    , hamlibIcomCIVAddress("/Hamlib/IcomCIVHex", 0)
    , hamlibRigName("/Hamlib/RigNameStr", "")
    , hamlibPTTType("/Hamlib/PttType", 0)
    , hamlibSerialRate("/Hamlib/SerialRate", 0)
    , hamlibSerialPort("/Hamlib/SerialPort", "")
    , hamlibPttSerialPort("/Hamlib/PttSerialPort", "")
    , hamlibForceRTSOn("/Hamlib/ForceRTSOn", false)
    , hamlibForceDTROn("/Hamlib/ForceDTROn", false)
        
    , useSerialPTT("/Rig/UseSerialPTT", false)
    , serialPTTPort("/Rig/Port", "")
    , serialPTTUseRTS("/Rig/UseRTS", true)
    , serialPTTPolarityRTS("/Rig/RTSPolarity", true)
    , serialPTTUseDTR("/Rig/UseDTR", false)
    , serialPTTPolarityDTR("/Rig/DTRPolarity", false)

#if defined(WIN32)
    , useOmniRig("/Rig/UseOmniRig", false)
    , omniRigRigId("/Rig/OmniRigRigId", 0)
#endif // defined(WIN32)
                
    , useSerialPTTInput("/Rig/UseSerialPTTInput", false)
    , serialPTTInputPort("/Rig/PttInPort", "")
    , serialPTTInputPolarityCTS("/Rig/CTSPolarity", false)
        
    , leftChannelVoxTone("/Rig/leftChannelVoxTone",  false)
    , totTimerEnabled("/Rig/TotTimerEnabled", true)
    , totTimerSecs("/Rig/TotTimerSecs", 180)
{
    // empty
}

void RigControlConfiguration::load(wxConfigBase* config)
{
    load_(config, hamlibUseForPTT);
    load_(config, hamlibEnableFreqModeChanges);
    load_(config, hamlibEnableFreqChangesOnly);
    load_(config, hamlibUseAnalogModes);
    load_(config, hamlibSaveRestoreFrequency);
    load_(config, hamlibIcomCIVAddress);
    load_(config, hamlibRigName);

    load_(config, hamlibPTTType);
    load_(config, hamlibSerialRate);
    load_(config, hamlibSerialPort);
    
    // By default, the PTT serial port should be the same as the 
    // regular serial port.
    auto tmp = hamlibSerialPort.getWithoutProcessing();
    hamlibPttSerialPort.setDefaultVal(tmp);
    load_(config, hamlibPttSerialPort);
    load_(config, hamlibForceRTSOn);
    load_(config, hamlibForceDTROn);

    load_(config, useSerialPTT);
    load_(config, serialPTTPort);
    load_(config, serialPTTUseRTS);
    load_(config, serialPTTPolarityRTS);
    load_(config, serialPTTUseDTR);
    load_(config, serialPTTPolarityDTR);
    
#if defined(WIN32)
    load_(config, useOmniRig);
    load_(config, omniRigRigId);
#endif // defined(WIN32)
    
    load_(config, useSerialPTTInput);
    load_(config, serialPTTInputPort);
    load_(config, serialPTTInputPolarityCTS);
    
    load_(config, leftChannelVoxTone);

    load_(config, totTimerEnabled);
    load_(config, totTimerSecs);
}

void RigControlConfiguration::save(wxConfigBase* config)
{
    save_(config, hamlibUseForPTT);
    save_(config, hamlibEnableFreqModeChanges);
    save_(config, hamlibEnableFreqChangesOnly);
    save_(config, hamlibUseAnalogModes);
    save_(config, hamlibSaveRestoreFrequency);
    save_(config, hamlibIcomCIVAddress);
    save_(config, hamlibRigName);
    save_(config, hamlibPTTType);
    save_(config, hamlibSerialRate);
    save_(config, hamlibSerialPort);
    save_(config, hamlibPttSerialPort);
    save_(config, hamlibForceRTSOn);
    save_(config, hamlibForceDTROn);

    save_(config, useSerialPTT);
    save_(config, serialPTTPort);
    save_(config, serialPTTUseRTS);
    save_(config, serialPTTPolarityRTS);
    save_(config, serialPTTUseDTR);
    save_(config, serialPTTPolarityDTR);
    
#if defined(WIN32)
    save_(config, useOmniRig);
    save_(config, omniRigRigId);
#endif // defined(WIN32)
        
    save_(config, useSerialPTTInput);
    save_(config, serialPTTInputPort);
    save_(config, serialPTTInputPolarityCTS);
    
    save_(config, leftChannelVoxTone);

    save_(config, totTimerEnabled);
    save_(config, totTimerSecs);
}
