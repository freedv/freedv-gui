//==========================================================================
// Name:            RigControlConfiguration.h
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

#ifndef RIG_CONTROL_CONFIGURATION_H
#define RIG_CONTROL_CONFIGURATION_H

#include "WxWidgetsConfigStore.h"
#include "ConfigurationDataElement.h"

class RigControlConfiguration : public WxWidgetsConfigStore
{
public:
    RigControlConfiguration();
    virtual ~RigControlConfiguration() = default;
    
    ConfigurationDataElement<bool> hamlibUseForPTT;
    ConfigurationDataElement<bool> hamlibEnableFreqModeChanges;
    ConfigurationDataElement<bool> hamlibEnableFreqChangesOnly;
    ConfigurationDataElement<bool> hamlibUseAnalogModes;
    ConfigurationDataElement<bool> hamlibSaveRestoreFrequency;
    ConfigurationDataElement<unsigned int> hamlibIcomCIVAddress;
    ConfigurationDataElement<wxString> hamlibRigName;
    ConfigurationDataElement<unsigned int> hamlibPTTType;
    ConfigurationDataElement<unsigned int> hamlibSerialRate;
    ConfigurationDataElement<wxString> hamlibSerialPort;
    ConfigurationDataElement<wxString> hamlibPttSerialPort;
    ConfigurationDataElement<bool> hamlibForceRTSOn;
    ConfigurationDataElement<bool> hamlibForceDTROn;
    
    ConfigurationDataElement<bool> useSerialPTT;
    ConfigurationDataElement<wxString> serialPTTPort;
    ConfigurationDataElement<bool> serialPTTUseRTS;
    ConfigurationDataElement<bool> serialPTTPolarityRTS;
    ConfigurationDataElement<bool> serialPTTUseDTR;
    ConfigurationDataElement<bool> serialPTTPolarityDTR;
    
#if defined(WIN32)
    ConfigurationDataElement<bool> useOmniRig;
    ConfigurationDataElement<unsigned int> omniRigRigId;
#endif // defined(WIN32)
    
    ConfigurationDataElement<bool> useSerialPTTInput;
    ConfigurationDataElement<wxString> serialPTTInputPort;
    ConfigurationDataElement<bool> serialPTTInputPolarityCTS;
    
    ConfigurationDataElement<bool> leftChannelVoxTone;

    // Time-Out Timer (TOT): limits how long FreeDV can stay in transmit.
    ConfigurationDataElement<bool> totTimerEnabled;
    ConfigurationDataElement<int>  totTimerSecs;

    virtual void load(wxConfigBase* config) override;
    virtual void save(wxConfigBase* config) override;
};

#endif // RIG_CONTROL_CONFIGURATION_H
