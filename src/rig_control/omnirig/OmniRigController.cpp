//=========================================================================
// Name:            OmniRigController.cpp
// Purpose:         Interface for control via OmniRig.
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

#include <iostream>
#include <sstream>
#include <chrono>
#include <cassert>
#include <future>
#include "OmniRigController.h"

#include "../../util/logging/ulog.h"

using namespace std::chrono_literals;

#define OMNI_RIG_WAIT_TIME (200ms)

OmniRigController::OmniRigController(int rigId, bool restoreOnDisconnect, bool freqOnly)
    : ThreadedObject("OmniRig")
    , rigId_(rigId)
    , omniRig_(nullptr)
    , rig_(nullptr)
    , origFreq_(0)
    , origMode_(PM_UNKNOWN)
    , currFreq_(0)
    , currMode_(UNKNOWN)
    , restoreOnDisconnect_(restoreOnDisconnect)
    , writableParams_(0)
    , freqOnly_(freqOnly)
    , rigResponseTime_(0)
    , destroying_(false)
    , pttSet_(false)
{
    // empty
}

OmniRigController::~OmniRigController()
{
    destroying_ = true;

    // Disconnect in a synchronous fashion before killing our thread.
    std::shared_ptr<std::promise<void>> prom = std::make_shared<std::promise<void>>();
    auto fut = prom->get_future();

    enqueue_([this, prom]() {
        if (rig_.load(std::memory_order_acquire) != nullptr)
        {
            disconnectImpl_();
        }
        prom->set_value();
    });

    fut.wait();
    
    waitForAllTasksComplete_();
}

void OmniRigController::connect()
{
    enqueue_(std::bind(&OmniRigController::connectImpl_, this));
}

void OmniRigController::disconnect()
{
    enqueue_(std::bind(&OmniRigController::disconnectImpl_, this));
}

bool OmniRigController::isConnected()
{
    return rig_.load(std::memory_order_acquire) != nullptr;
}

void OmniRigController::ptt(bool state)
{
    enqueue_(std::bind(&OmniRigController::pttImpl_, this, state));
}

void OmniRigController::setFrequency(uint64_t frequency)
{
    noteRequestedFrequency_(frequency);
    enqueue_(std::bind(&OmniRigController::setFrequencyImpl_, this, frequency));
}

void OmniRigController::setMode(IRigFrequencyController::Mode mode)
{
    enqueue_(std::bind(&OmniRigController::setModeImpl_, this, mode));
}

void OmniRigController::requestCurrentFrequencyMode()
{
    enqueue_(std::bind(&OmniRigController::requestCurrentFrequencyModeImpl_, this));
}

int OmniRigController::getRigResponseTimeMicroseconds()
{
    return rigResponseTime_;
}

void OmniRigController::connectImpl_()
{
    // Ensure that COM is properly initialized.
    CoInitializeEx (nullptr, 0 /*COINIT_APARTMENTTHREADED*/);

    // Attempt to create OmniRig COM object.
    auto rv = CoCreateInstance(
        CLSID_OmniRigX,
        NULL,
        CLSCTX_LOCAL_SERVER,
        IID_IOmniRigX,
        (LPVOID*)&omniRig_);
    if (rv == S_OK)
    {
        IRigX* rig = nullptr;
        if (rigId_ == 0)
        {
            omniRig_->get_Rig1(&rig);
        } 
        else if (rigId_ == 1)
        {
            omniRig_->get_Rig2(&rig);
        }
        else
        {
            assert(0);
        }
        rig_.store(rig, std::memory_order_release);

        onRigConnected(this);

        // Anything requested prior to connect is held in currFreq_/currMode_.
        // Save it off as retrieving the radio's current state overwrites both.
        auto pendingFreq = currFreq_;
        auto pendingMode = currMode_;

        origFreq_ = 0;
        origMode_ = PM_UNKNOWN;
        currFreq_ = 0;
        currMode_ = UNKNOWN;

        // Get list of writable parameters.
        rig->get_WriteableParams(&writableParams_);

        // Make sure radio starts in RX mode.
        rig->put_Tx(PM_RX);

        // Get current frequency and mode when we first connect so we can
        // revert on close.
        requestCurrentFrequencyModeImpl_();
        
        // If a freq/mode was set prior to connect, push those changes now.
        // (These do nothing if the radio's already there.)
        if (pendingFreq > 0)
        {
            setFrequencyImpl_(pendingFreq);
        }
        
        if (pendingMode != UNKNOWN)
        {
            setModeImpl_(pendingMode);
        }
    }
    else
    {
        omniRig_ = nullptr;
        onRigError(this, "Could not create COM object for OmniRig.");
    }
}

void OmniRigController::disconnectImpl_()
{
    if (omniRig_ != nullptr)
    {
        IRigX* rig = rig_.load(std::memory_order_acquire);

        // If we're told to restore on disconnect, do so.
        if (restoreOnDisconnect_)
        {
            setFrequencyImpl_(origFreq_);
            
            if (!freqOnly_)
            {
                rig->put_Mode(origMode_);
            }
        }
        
        rig_.store(nullptr, std::memory_order_release);
        rig->Release();
        omniRig_->Release();
        omniRig_ = nullptr;

        // Only requests made from here on should be pushed on the next connect.
        currFreq_ = 0;
        currMode_ = UNKNOWN;

        onRigDisconnected(this);
    }

    // Uninitialize COM.
    CoUninitialize ();
}

void OmniRigController::pttImpl_(bool state)
{
    IRigX* rig = rig_.load(std::memory_order_acquire);
    if (rig != nullptr)
    {
        auto oldTime = std::chrono::steady_clock::now();
        rig->put_Tx(state ? PM_TX : PM_RX);
        auto newTime = std::chrono::steady_clock::now();
        auto totalTimeMicroseconds = (int)std::chrono::duration_cast<std::chrono::microseconds>(newTime - oldTime).count();
        rigResponseTime_ = std::max(rigResponseTime_, totalTimeMicroseconds);
        
        bool changed = false;
        if (pttSet_ != state)
        {
            changed = true;
            onPttChange(this, state);
        }
        
        pttSet_ = state;
        
        if (changed && !state && !destroying_)
        {
            requestCurrentFrequencyMode();
        }
    }
}

void OmniRigController::setFrequencyImpl_(uint64_t frequencyHz)
{
    IRigX* rig = rig_.load(std::memory_order_acquire);
    if (rig != nullptr && frequencyHz != currFreq_)
    {
        log_info("Set frequency to %lld", frequencyHz);
        
        HRESULT result = E_FAIL;

        // Get current VFO first and try to explicitly set that VFO.
        RigParamX vfo;
        long tmpFreq = 0;
        result = rig->get_Vfo(&vfo);
        if (result == S_OK)
        {
            log_info("Got VFO = %d", (int)vfo);

            if (vfo == PM_UNKNOWN)
            {
                log_warn("Forcing VFO to VFOA");
                rig->put_Vfo(PM_VFOA);
                vfo = PM_VFOA;
            }

            if (vfo == PM_VFOA || vfo == PM_VFOAA || vfo == PM_VFOAB)
            {
                log_info("Setting frequency on VFOA");
                result = rig->put_FreqA(frequencyHz);
            }
            else if (vfo == PM_VFOB || vfo == PM_VFOBB || vfo == PM_VFOBA)
            {
                log_info("Setting frequency on VFOB");
                result = rig->put_FreqB(frequencyHz);
            }
            else
            {
                log_error("Neither VFOA nor VFOB are writable");
                result = E_FAIL; // force use of fallback
            }
        
            // Note: wait required to ensure that change takes effect.
            if (result == S_OK)
            {
                std::this_thread::sleep_for(OMNI_RIG_WAIT_TIME);

                if (vfo == PM_VFOA || vfo == PM_VFOAA || vfo == PM_VFOAB)
                {
                    result = rig->get_FreqA(&tmpFreq);
                    log_info("Got freq for VFOA: %lld", tmpFreq);
                }
                else if (vfo == PM_VFOB || vfo == PM_VFOBB || vfo == PM_VFOBA)
                {
                    result = rig->get_FreqB(&tmpFreq);
                    log_info("Got freq for VFOB: %lld", tmpFreq);
                }
            }
        }

        if (result != S_OK || (uint64_t)tmpFreq != frequencyHz)
        {
            // Try VFO-less set in case the first one didn't work.
            log_info("Trying frequency set fallback");
            result = rig->put_Freq(frequencyHz);
            std::this_thread::sleep_for(OMNI_RIG_WAIT_TIME);
        }

        if (result == S_OK)
        {
            currFreq_ = frequencyHz;
            if (!destroying_)
            {
                requestCurrentFrequencyMode();
            }
        }
        else
        {
            std::stringstream errMsg;
            log_error("Could not change frequency to %lld Hz (HRESULT = %d)", frequencyHz, result);
            onRigError(this, errMsg.str());
        }
    }
    else
    {
        currFreq_ = frequencyHz;
    }
}

void OmniRigController::setModeImpl_(IRigFrequencyController::Mode mode)
{
    IRigX* rig = rig_.load(std::memory_order_acquire);
    if (mode == currMode_)
    {
        return;
    }

    RigParamX omniRigMode;

    switch(mode)
    {
        case IRigFrequencyController::USB:
            omniRigMode = PM_SSB_U;
            break;
        case IRigFrequencyController::DIGU:
            omniRigMode = PM_DIG_U;
            break;
        case IRigFrequencyController::LSB:
            omniRigMode = PM_SSB_L;
            break;
        case IRigFrequencyController::DIGL:
            omniRigMode = PM_DIG_L;
            break;
        case IRigFrequencyController::FM:
        case IRigFrequencyController::DIGFM:
            omniRigMode = PM_FM;
            break;
        case IRigFrequencyController::AM:
            omniRigMode = PM_AM;
            break;
        default:
            onRigError(this, "Unknown mode passed into OmniRig.");
            return;
    }

    if (rig != nullptr)
    {
        auto result = rig->put_Mode(omniRigMode);
        if (result == S_OK)
        {
            currMode_ = mode;

            // Note: wait required to ensure that change takes effect.
            std::this_thread::sleep_for(OMNI_RIG_WAIT_TIME);

            if (!destroying_)
            {
                requestCurrentFrequencyMode();
            }
        }
        else
        {
            std::stringstream errMsg;
            errMsg << "Could not change mode to " << omniRigMode << " (HRESULT = " << result << ")";
            onRigError(this, errMsg.str());
        }
    }
    else
    {
        currMode_ = mode;
    }
}

void OmniRigController::requestCurrentFrequencyModeImpl_()
{
    IRigX* rig = rig_.load(std::memory_order_acquire);
    RigParamX omniRigMode;
    long freq;

    // Note: we ignore attempts to retrieve frequency/mode during
    // TX for several reasons:
    //   a. Some radios do not respond to commands during TX (ex: FT-817)
    //   b. Some radios on their own accord will briefly stop TX to handle the command,
    //      causing distortion of the transmitted signal (ex: Red Pitaya with Thetis).
    if (rig != nullptr && !pttSet_)
    {
        RigParamX vfo;
        HRESULT result = rig->get_Vfo(&vfo);
        
        if (result == S_OK)
        {
            log_info("Got VFO = %d", (int)vfo);

            if (vfo == PM_UNKNOWN)
            {
                log_warn("Forcing VFO to VFOA");
                rig->put_Vfo(PM_VFOA);
                vfo = PM_VFOA;
            }
            
            if (vfo == PM_VFOA || vfo == PM_VFOAA || vfo == PM_VFOAB)
            {
                log_debug("Getting frequency on VFOA");
                result = rig->get_FreqA(&freq);
            }
            else if (vfo == PM_VFOB || vfo == PM_VFOBB || vfo == PM_VFOBA)
            {
                log_debug("Getting frequency on VFOB");
                result = rig->get_FreqB(&freq);
            }
            else
            {
                log_error("Neither VFOA nor VFOB are writable");
                result = E_FAIL; // force use of fallback
            }
        }
        
        if (result != S_OK || freq == 0)
        {
            // FREQA/FREQB aren't being set. Use FREQ instead.
            rig->get_Freq(&freq);
        }
        
        log_info("Got frequency as %ld", freq);
        
        // Mode isn't dependent on VFO in the current OmniRig API (!)        
        rig->get_Mode(&omniRigMode);

        // Convert OmniRig mode to our mode
        IRigFrequencyController::Mode mode;
        switch (omniRigMode)
        {
            case PM_DIG_U:
                mode = IRigFrequencyController::DIGU;
                break;
            case PM_DIG_L:
                mode = IRigFrequencyController::DIGL;
                break;
            case PM_SSB_U:
                mode = IRigFrequencyController::USB;
                break;
            case PM_SSB_L:
                mode = IRigFrequencyController::LSB;
                break;
            case PM_AM:
                mode = IRigFrequencyController::AM;
                break;
            case PM_FM:
                mode = IRigFrequencyController::FM;
                break;
            default:
                mode = IRigFrequencyController::UNKNOWN;
                break;
        }

        // Publish frequency/mode to subscribers
        currFreq_ = freq;
        currMode_ = mode;
        onFreqModeChange(this, freq, mode);

        if (origFreq_ == 0)
        {
            // Save first freq/mode for restore
            origFreq_ = freq;
            origMode_ = omniRigMode;
        }
    }
}
