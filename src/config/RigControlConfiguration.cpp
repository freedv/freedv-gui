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
    : hamlibUseForPTT("/Hamlib/UseForPTT", false, ConfigChange::RECONNECTS_RADIO)
    , hamlibEnableFreqModeChanges("/Hamlib/EnableFreqModeChanges", true, ConfigChange::RECONNECTS_RADIO)
    , hamlibEnableFreqChangesOnly("/Hamlib/EnableFreqChangesOnly", false, ConfigChange::RECONNECTS_RADIO)
    , hamlibUseAnalogModes("/Hamlib/UseAnalogModes", false)
    , hamlibSaveRestoreFrequency("/Hamlib/SaveRestoreFrequency", true, ConfigChange::RECONNECTS_RADIO)
    , hamlibIcomCIVAddress("/Hamlib/IcomCIVHex", 0, ConfigChange::RECONNECTS_RADIO)
    , hamlibRigName("/Hamlib/RigNameStr", "", ConfigChange::RECONNECTS_RADIO)
    , hamlibPTTType("/Hamlib/PttType", 0, ConfigChange::RECONNECTS_RADIO)
    , hamlibSerialRate("/Hamlib/SerialRate", 0, ConfigChange::RECONNECTS_RADIO)
    , hamlibSerialPort("/Hamlib/SerialPort", "", ConfigChange::RECONNECTS_RADIO)
    , hamlibPttSerialPort("/Hamlib/PttSerialPort", "", ConfigChange::RECONNECTS_RADIO)
    , hamlibForceRTSOn("/Hamlib/ForceRTSOn", false, ConfigChange::RECONNECTS_RADIO)
    , hamlibForceDTROn("/Hamlib/ForceDTROn", false, ConfigChange::RECONNECTS_RADIO)
        
    , useSerialPTT("/Rig/UseSerialPTT", false, ConfigChange::RECONNECTS_RADIO)
    , serialPTTPort("/Rig/Port", "", ConfigChange::RECONNECTS_RADIO)
    , serialPTTUseRTS("/Rig/UseRTS", true, ConfigChange::RECONNECTS_RADIO)
    , serialPTTPolarityRTS("/Rig/RTSPolarity", true, ConfigChange::RECONNECTS_RADIO)
    , serialPTTUseDTR("/Rig/UseDTR", false, ConfigChange::RECONNECTS_RADIO)
    , serialPTTPolarityDTR("/Rig/DTRPolarity", false, ConfigChange::RECONNECTS_RADIO)

#if defined(WIN32)
    , useOmniRig("/Rig/UseOmniRig", false, ConfigChange::RECONNECTS_RADIO)
    , omniRigRigId("/Rig/OmniRigRigId", 0, ConfigChange::RECONNECTS_RADIO)
#endif // defined(WIN32)
                
    , useSerialPTTInput("/Rig/UseSerialPTTInput", false, ConfigChange::REOPENS_PTT_INPUT)
    , serialPTTInputPort("/Rig/PttInPort", "", ConfigChange::REOPENS_PTT_INPUT)
    , serialPTTInputPolarityCTS("/Rig/CTSPolarity", false, ConfigChange::REOPENS_PTT_INPUT)
        
    , leftChannelVoxTone("/Rig/leftChannelVoxTone",  false, ConfigChange::UPDATES_RUNNING_AUDIO)
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
