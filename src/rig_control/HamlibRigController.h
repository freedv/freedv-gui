//=========================================================================
// Name:            HamlibRigController.h
// Purpose:         Controls radios using Hamlib library.
//
// Authors:         Mooneer Salem
// License:
//
//  All rights reserved.
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

#ifndef HAMLIB_RIG_CONTROLLER_H
#define HAMLIB_RIG_CONTROLLER_H

#include <string>
#include <vector>
#include <mutex>
#include <atomic>

#include "ThreadedObject.h"
#include "IRigFrequencyController.h"
#include "IRigPttController.h"

extern "C" 
{
    #include <hamlib/rig.h>
}

class HamlibRigController : public ThreadedObject, public IRigFrequencyController, public IRigPttController
{
public:
    enum PttType 
    {
        PTT_VIA_CAT = 0,
        PTT_VIA_RTS,
        PTT_VIA_DTR,
        PTT_VIA_NONE,
        PTT_VIA_CAT_DATA,
    };
    
    HamlibRigController(std::string rigName, std::string serialPort, const int serialRate, const int civHex, const PttType pttType, std::string pttSerialPort, bool restoreFreqModeOnDisconnect, bool freqOnly, bool forceRtsOn, bool forceDtrOn);
    HamlibRigController(int rigIndex, std::string serialPort, const int serialRate, const int civHex, const PttType pttType, std::string pttSerialPort, bool restoreFreqModeOnDisconnect, bool freqOnly, bool forceRtsOn, bool forceDtrOn);
    virtual ~HamlibRigController();
    
    virtual void connect() override;
    virtual void disconnect() override;
    virtual bool isConnected() override;
    virtual void ptt(bool state) override;
    virtual void setFrequency(uint64_t frequency) override;
    virtual void setMode(IRigFrequencyController::Mode mode) override;
    virtual void requestCurrentFrequencyMode() override;

    static void InitializeHamlibLibrary();
    static int RigNameToIndex(std::string const& rigName);
    static std::string RigIndexToName(unsigned int rigIndex);
    static int GetNumberSupportedRadios();
    static int GetMinimumSerialBaudRate(unsigned int rigIndex);
    static int GetMaximumSerialBaudRate(unsigned int rigIndex);

    enum PortType { PORT_SERIAL, PORT_NETWORK, PORT_USB, PORT_OTHER };
    static PortType GetRigPortType(unsigned int rigIndex);
    static std::string GetDefaultRigPathname(unsigned int rigIndex);
    
    virtual int getRigResponseTimeMicroseconds() override;

private:
    // Lets unit tests reach the (simulated) radio behind the controller.
    friend struct HamlibRigControllerTestAccess;

    using RigList = std::vector<const struct rig_caps *>;
    using RigNameList = std::vector<std::string>;
    
    std::string rigName_;
    std::string serialPort_;
    const int serialRate_;
    const int civHex_;
    const PttType pttType_;
    std::string pttSerialPort_;
    const bool forceRtsOn_;
    const bool forceDtrOn_;

    std::atomic<RIG*> rig_;
    bool multipleVfos_;
    bool pttSet_;
    uint64_t currFreq_;
    rmode_t currMode_;
    IRigFrequencyController::Mode pendingMode_;
    bool restoreOnDisconnect_;
    uint64_t origFreq_;
    rmode_t origMode_;
    bool freqOnly_;
    std::atomic<bool> destroying_;
    
    std::atomic<int> rigResponseTime_;
  
    // Tracks errors encountered during/after rig_open() so that
    // we only display the error box once.
    bool errorEncountered_;

    // Number of frequency/mode retrieval errors seen.
    // This is so that we have a bit of leeway before showing the error
    // box, as errors can sometimes be emitted yet no problems exist
    // (example: rapidly spinning the dial on the radio side)
    int getFreqModeErrorCount_;

    // 2 or more errors while retrieving freq/mode should cause the popup to appear
    const int MAX_GET_FREQUENCY_ERR_COUNT = 1;

    // How long to wait before connecting to rigctld again when it drops the
    // connection straight away (see connectImpl_()).
    static constexpr int RIGCTLD_RETRY_DELAY_MS = 500;
    
    vfo_t getCurrentVfo_();
    vfo_t getWritableVfo_();
    void setFrequencyHelper_(vfo_t currVfo, uint64_t frequencyHz);
    void setModeHelper_(vfo_t currVfo, rmode_t mode);
    
    void connectImpl_();
    void disconnectImpl_();
    void pttImpl_(bool state);
    void setFrequencyImpl_(uint64_t frequencyHz);
    void setModeImpl_(IRigFrequencyController::Mode mode);
    void requestCurrentFrequencyModeImpl_();
    
    static RigList RigList_;
    static RigNameList RigNameList_;
    static std::mutex RigListMutex_;

    static bool RigCompare_(const struct rig_caps *rig1, const struct rig_caps *rig2);

#if RIGCAPS_NOT_CONST && !HAMLIB_CONST_WORKAROUND
    static int BuildRigList_(struct rig_caps *rig, rig_ptr_t);
#else
    static int BuildRigList_(const struct rig_caps *rig, rig_ptr_t);
#endif // RIGCAPS_NOT_CONST && !HAMLIB_CONST_WORKAROUND
};

#endif // HAMLIB_RIG_CONTROLLER_H
