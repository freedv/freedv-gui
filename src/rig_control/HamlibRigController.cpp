//=========================================================================
// Name:            HamlibRigController.cpp
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

#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>
#include <strings.h>

#include "HamlibRigController.h"

#include "util/logging/ulog.h"

#if defined(HAMLIB_USE_SEPARATE_RIG_STATE_H)
#include <hamlib/rig_state.h>
#endif // defined(HAMLIB_USE_SEPARATE_RIG_STATE_H)

#if defined(HAMLIB_USE_FRIENDLY_ERRORS)
#define HAMLIB_FRIENDLY_ERROR_FN rigerror2
#else
#define HAMLIB_FRIENDLY_ERROR_FN rigerror
#endif // defined(HAMLIB_USE_FRIENDLY_ERRORS)

HamlibRigController::RigList HamlibRigController::RigList_;
HamlibRigController::RigNameList HamlibRigController::RigNameList_;
std::mutex HamlibRigController::RigListMutex_;

#if RIGCAPS_NOT_CONST && !HAMLIB_CONST_WORKAROUND
int HamlibRigController::BuildRigList_(struct rig_caps *rig, rig_ptr_t rigList) {
#else
int HamlibRigController::BuildRigList_(const struct rig_caps *rig, rig_ptr_t rigList) {    
#endif // RIGCAPS_NOT_CONST && !HAMLIB_CONST_WORKAROUND
    ((HamlibRigController::RigList *)rigList)->push_back(rig); 
    return 1;
}

bool HamlibRigController::RigCompare_(const struct rig_caps *rig1, const struct rig_caps *rig2) {
    /* Compare manufacturer. */
    int r = strcasecmp(rig1->mfg_name, rig2->mfg_name);
    if (r != 0)
        return r < 0;

    /* Compare model. */
    r = strcasecmp(rig1->model_name, rig2->model_name);
    if (r != 0)
        return r < 0;

    /* Compare rig ID. */
    return rig1->rig_model < rig2->rig_model;
}

HamlibRigController::HamlibRigController(std::string rigName, std::string serialPort, const int serialRate, const int civHex, const PttType pttType, std::string pttSerialPort, bool restoreFreqModeOnDisconnect, bool freqOnly, bool forceRtsOn, bool forceDtrOn)
    : ThreadedObject("hamlib")
    , rigName_(std::move(rigName))
    , serialPort_(std::move(serialPort))
    , serialRate_(serialRate)
    , civHex_(civHex)
    , pttType_(pttType)
    , pttSerialPort_(std::move(pttSerialPort))
    , forceRtsOn_(forceRtsOn)
    , forceDtrOn_(forceDtrOn)
    , rig_(nullptr)
    , multipleVfos_(false)
    , pttSet_(false)
    , currFreq_(0)
    , currMode_(RIG_MODE_NONE)
    , pendingMode_(UNKNOWN)
    , restoreOnDisconnect_(restoreFreqModeOnDisconnect)
    , origFreq_(0)
    , origMode_(RIG_MODE_NONE)
    , freqOnly_(freqOnly)
    , destroying_(false)
    , rigResponseTime_(0)
    , errorEncountered_(false)
    , getFreqModeErrorCount_(0)
{
    // Perform initial load of rig list if this is our first time being created.
    InitializeHamlibLibrary();
}

HamlibRigController::HamlibRigController(int rigIndex, std::string serialPort, const int serialRate, const int civHex, const PttType pttType, std::string pttSerialPort, bool restoreFreqModeOnDisconnect, bool freqOnly, bool forceRtsOn, bool forceDtrOn)
    : ThreadedObject("hamlib")
    , rigName_(RigIndexToName(rigIndex))
    , serialPort_(std::move(serialPort))
    , serialRate_(serialRate)
    , civHex_(civHex)
    , pttType_(pttType)
    , pttSerialPort_(std::move(pttSerialPort))
    , forceRtsOn_(forceRtsOn)
    , forceDtrOn_(forceDtrOn)
    , rig_(nullptr)
    , multipleVfos_(false)
    , pttSet_(false)
    , currFreq_(0)
    , currMode_(RIG_MODE_NONE)
    , pendingMode_(UNKNOWN)
    , restoreOnDisconnect_(restoreFreqModeOnDisconnect)
    , origFreq_(0)
    , origMode_(RIG_MODE_NONE)
    , freqOnly_(freqOnly)
    , destroying_(false)
    , rigResponseTime_(0)
    , errorEncountered_(false)
    , getFreqModeErrorCount_(0)
{
    // Perform initial load of rig list if this is our first time being created.
    InitializeHamlibLibrary();
}

HamlibRigController::~HamlibRigController()
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

static int LogHamlibErrors_(
    enum rig_debug_level_e debug_level,
    rig_ptr_t,
    const char *fmt,
    va_list ap)
{
    char buf[1024];
    vsnprintf (buf, sizeof(buf), fmt, ap);

    // Strip newlines from end of log messages
    std::string msg = buf;
    if (!msg.empty() && msg[msg.length() - 1] == '\n') 
    {
        msg.erase(msg.length() - 1);
    }

    if (!msg.empty() && msg[msg.length() - 1] == '\r') 
    {
        msg.erase(msg.length() - 1);
    }

    switch (debug_level)
    {
        case RIG_DEBUG_BUG:
            log_fatal("%s", msg.c_str());
            break;
        case RIG_DEBUG_ERR:
            log_error("%s", msg.c_str());
            break;
        case RIG_DEBUG_WARN:
            log_warn("%s", msg.c_str());
            break;
        case RIG_DEBUG_VERBOSE:
            log_debug("%s", msg.c_str());
            break;
        default:
            log_trace("%s", msg.c_str());
            break;                
    }
    
    return RIG_OK;
}

void HamlibRigController::InitializeHamlibLibrary()
{
    std::unique_lock<std::mutex> lk(RigListMutex_);
    if (RigList_.size() == 0)
    {
        /* Stop hamlib from spewing info to stderr. */
        rig_set_debug(RIG_DEBUG_NONE);

        /* Create sorted list of rigs. */
        rig_load_all_backends();
        rig_list_foreach(&HamlibRigController::BuildRigList_, &RigList_);
        std::sort(RigList_.begin(), RigList_.end(), &HamlibRigController::RigCompare_);
        
        // Capture names of rigs for configuration use.
        for (auto& rig : RigList_)
        {
            std::string rigName = 
                std::string(rig->mfg_name) + std::string(" ") + std::string(rig->model_name);
            RigNameList_.push_back(rigName);
        }

        /* Reset debug output. */
        rig_set_debug(RIG_DEBUG_VERBOSE);
        
        /* Ensure that we use ulog to log hamlib messages. */
        rig_set_debug_callback(&LogHamlibErrors_, nullptr);
    }
}

void HamlibRigController::connect()
{
    enqueue_(std::bind(&HamlibRigController::connectImpl_, this));
}

void HamlibRigController::disconnect()
{
    enqueue_(std::bind(&HamlibRigController::disconnectImpl_, this));
}

bool HamlibRigController::isConnected()
{
    return rig_.load(std::memory_order_acquire) != nullptr;
}

void HamlibRigController::ptt(bool state)
{
    enqueue_(std::bind(&HamlibRigController::pttImpl_, this, state));
}

void HamlibRigController::setFrequency(uint64_t frequency)
{
    noteRequestedFrequency_(frequency);
    enqueue_(std::bind(&HamlibRigController::setFrequencyImpl_, this, frequency));
}

void HamlibRigController::setMode(IRigFrequencyController::Mode mode)
{
    enqueue_(std::bind(&HamlibRigController::setModeImpl_, this, mode));
}

void HamlibRigController::requestCurrentFrequencyMode()
{
    enqueue_(std::bind(&HamlibRigController::requestCurrentFrequencyModeImpl_, this));
}

int HamlibRigController::getRigResponseTimeMicroseconds()
{
    return rigResponseTime_;
}

int HamlibRigController::RigNameToIndex(std::string const& rigName)
{
    InitializeHamlibLibrary();

    std::string trimmedRigName = rigName;
    trimmedRigName.erase(trimmedRigName.find_last_not_of(" \n\r\t") + 1);
    int index = 0;
    for (auto& entry : RigNameList_)
    {
        std::string trimmedEntry = entry;
        trimmedEntry.erase(trimmedEntry.find_last_not_of(" \n\r\t") + 1);
        if (trimmedRigName == trimmedEntry)
        {
            return index;
        }
        
        index++;
    }
    
    return -1;
}

std::string HamlibRigController::RigIndexToName(unsigned int rigIndex)
{
    InitializeHamlibLibrary();
    if (rigIndex >= RigNameList_.size())
    {
        return "";
    }
    return RigNameList_[rigIndex];
}

int HamlibRigController::GetMinimumSerialBaudRate(unsigned int rigIndex)
{
    InitializeHamlibLibrary();
    return RigList_[rigIndex]->serial_rate_min;
}

int HamlibRigController::GetMaximumSerialBaudRate(unsigned int rigIndex)
{
    InitializeHamlibLibrary();
    return RigList_[rigIndex]->serial_rate_max;
}

int HamlibRigController::GetNumberSupportedRadios()
{
    InitializeHamlibLibrary();
    return RigList_.size();
}

// Note: these execute in the thread created by ThreadedObject on object instantiation.

void HamlibRigController::connectImpl_()
{
    if (rig_.load(std::memory_order_acquire) != nullptr)
    {
        std::string errMsg = "Already connected to radio.";
        onRigError(this, errMsg);
        return;
    }
    
    auto rigIndex = RigNameToIndex(rigName_);
    if (rigIndex == -1)
    {
        std::string errMsg = "Could not find rig " + rigName_ + " in Hamlib database.";
        onRigError(this, errMsg);
        return;
    }
    
    /* Initialise, configure and open. */
    origFreq_ = 0;
    origMode_ = RIG_MODE_NONE;
    errorEncountered_ = false;
    getFreqModeErrorCount_ = 0;

    auto tmpRig = rig_init(RigList_[rigIndex]->rig_model);
    if (!tmpRig) 
    {
        log_debug("rig_init() failed, returning");
        
        std::string errMsg = "Hamlib could not initialize rig " + rigName_;
        onRigError(this, errMsg);
        return;
    }
    log_info ("rigName -%s- via port -%s-\n", rigName_.c_str(), serialPort_.c_str() );
    
    rig_.store(tmpRig, std::memory_order_release);
    log_debug("rig_init() OK ....");

    /* Set CI-V address if necessary. */
    if (!strncmp(RigList_[rigIndex]->mfg_name, "Icom", 4))
    {
        char civAddr[5];
        snprintf(civAddr, 5, "0x%0X", civHex_);
        log_debug("hamlib: setting CI-V address to: %s", civAddr);
        rig_set_conf(rig_, rig_token_lookup(rig_, "civaddr"), civAddr);
    }
    else
    {
        log_debug("hamlib: ignoring CI-V configuration due to non-Icom radio\r");
    }

    rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "rig_pathname"), serialPort_.c_str());

    if (pttSerialPort_.size() > 0 && 
        pttSerialPort_ != serialPort_ &&
        (pttType_ == PTT_VIA_RTS || pttType_ == PTT_VIA_DTR))
    {
        std::string fixedPttSerialPort = pttSerialPort_;
#if defined(WIN32)
        // WSJT-X logic, not sure it's needed here.
        fixedPttSerialPort = std::string("\\\\.\\") + fixedPttSerialPort;
#endif // defined(WIN32)
        rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "ptt_pathname"), fixedPttSerialPort.c_str());
    }
    
    if (serialRate_ > 0) 
    {
        log_debug("hamlib: setting serial rate: %d", serialRate_);
        std::stringstream ss;
        ss << serialRate_;
        rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "serial_speed"), ss.str().c_str());
    }

    // Set PTT type as needed.
    switch(pttType_)
    {
        case PTT_VIA_RTS:
            rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "ptt_type"), "RTS");
            break;
        case PTT_VIA_DTR:
            rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "ptt_type"), "DTR");
            break;
        case PTT_VIA_NONE:
            rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "ptt_type"), "None");
            break;
        case PTT_VIA_CAT:
            rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "ptt_type"), "RIG");
            break;
        case PTT_VIA_CAT_DATA:
            // Need to explicitly use RIGMICDATA in order for RIG_PTT_ON_DATA to work.
            rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "ptt_type"), "RIGMICDATA");
            break;
        default:
            break;
    }
    
    if (pttType_ == PTT_VIA_RTS || pttType_ == PTT_VIA_DTR)
    {
        rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "ptt_share"), "1");
    }

    // Force RTS/DTR on if requested (e.g. to power a radio interface via the serial port lines).
    if (forceRtsOn_)
    {
        log_debug("hamlib: forcing RTS on");
        rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "rts_state"), "ON");
    }
    if (forceDtrOn_)
    {
        log_debug("hamlib: forcing DTR on");
        rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "dtr_state"), "ON");
    }
    
    // Icom workaround from WSJT-X. Not sure it's needed here as
    // FreeDV doesn't do split.
    rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "no_xchg"), "1");

    // Set timeouts so that we don't wait an extremely long time to begin TX.
    // However, only do so if the default timeout is larger than 625ms.
    auto limitTimeout = [tmpRig]() {
        const char* MAX_TIMEOUT = "625";
        const char* HAMLIB_TIMEOUT_TOKEN_NAME = "timeout";
        constexpr int TIMEOUT_BUF_LEN = 1024;
        char currentTimeout[TIMEOUT_BUF_LEN] = { 0 };
#if defined(HAMLIB_USE_FRIENDLY_ERRORS)
        // Hamlib 4.6+ has rig_get_conf2. rig_get_conf is officially deprecated in 5.0+ and 
        // causes compile errors in FreeDV due to -Werror.
        auto result = rig_get_conf2(tmpRig, rig_token_lookup(tmpRig, HAMLIB_TIMEOUT_TOKEN_NAME), currentTimeout, TIMEOUT_BUF_LEN);
#else
        auto result = rig_get_conf(tmpRig, rig_token_lookup(tmpRig, HAMLIB_TIMEOUT_TOKEN_NAME), currentTimeout);
#endif // defined(HAMLIB_USE_FRIENDLY_ERRORS)
        log_info("Current rig timeout: %s ms", currentTimeout);
        if (result != RIG_OK || (atoi(currentTimeout) >= atoi(MAX_TIMEOUT)))
        {
            log_info("Setting rig timeout to %s ms", MAX_TIMEOUT);
            rig_set_conf(tmpRig, rig_token_lookup(tmpRig, HAMLIB_TIMEOUT_TOKEN_NAME), MAX_TIMEOUT);
        }
    };
    limitTimeout();

    // Initially allow two retries if we time out while sending commands. This is needed
    // to better support Icom marine radios due to their ability to power themselves on
    // when rig_open is called (they're unable to immediately respond to commands while
    // powering up and thus results in spurious Hamlib errors being displayed to users).
    rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "retry"), "2");
    rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "timeout_retry"), "2");
            
    auto result = rig_open(tmpRig);
    if ((result == -RIG_EIO || result == RIG_EIO) && RigList_[rigIndex]->rig_model == RIG_MODEL_NETRIGCTL)
    {
        // rigctld before Hamlib commit a31705698e (after 4.7.2) closes each
        // client's socket more than once when it goes, and if another client
        // connects meanwhile, can close that one's instead: it's dropped as
        // soon as it's accepted. Any other program sharing the rigctld can
        // disconnect at just that moment, so give it one more go.
        log_warn("Couldn't connect to rigctld (%s); trying once more", HAMLIB_FRIENDLY_ERROR_FN(result));

        // Some versions of Hamlib (4.3 and 4.4, at least) leave the radio
        // marked open, with its port open, when the open fails in the
        // radio's own code (as it does here), and then refuse to open it
        // again. Closing it first sorts that out; versions that have tidied
        // up already just say there's nothing to close.
        rig_close(tmpRig);
        std::this_thread::sleep_for(std::chrono::milliseconds(RIGCTLD_RETRY_DELAY_MS));
        result = rig_open(tmpRig);
    }
    if (result == RIG_OK)
    {
        log_debug("hamlib: rig_open() OK");

        // Some versions of Hamlib replace the timeout with the radio's
        // default on opening it, so make sure that it's still limited.
        limitTimeout();

        onRigConnected(this);
        
        // Determine whether we have multiple VFOs.
        multipleVfos_ = false;
        vfo_t vfo;
        if (rig_get_vfo (tmpRig, &vfo) == RIG_OK && 
#if HAMLIB_CONST_WORKAROUND 
            /* 4.6+ */
            (HAMLIB_STATE(tmpRig)->vfo_list & RIG_VFO_B))
#else
            (tmpRig->state.vfo_list & RIG_VFO_B))
#endif // HAMLIB_CONST_WORKAROUND
        {
            multipleVfos_ = true;
        }

        // Make sure PTT is not enabled as there have been reports of some 
        // radios starting off in this state.
        result = rig_set_ptt(tmpRig, RIG_VFO_CURR, RIG_PTT_OFF);
        if (result != RIG_OK)
        {
            log_warn("Could not ensure that radio starts with PTT off: %s", rigerror(result));
        }
        
        // Get current frequency and mode when we first connect so we can 
        // revert on close.
        requestCurrentFrequencyModeImpl_();
        
        // If a freq/mode was set prior to connect, push those changes now.
        if (currFreq_ > 0)
        {
            auto tmpFreq = currFreq_;
            currFreq_ = 0; // to make setFrequencyImpl_ actually run
            setFrequencyImpl_(tmpFreq);
        }
        
        if (currMode_ != RIG_MODE_NONE)
        {
            currMode_ = RIG_MODE_NONE; // to make setModeImpl_ actually run
            setModeImpl_(pendingMode_);
        }

        // Disable timeouts. Now that we're able to connect and send the initial
        // commands required by FreeDV, if we somehow have an issue sending commands 
        // later we want to let the user know ASAP.
        rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "retry"), "0");
        rig_set_conf(tmpRig, rig_token_lookup(tmpRig, "timeout_retry"), "0");
    
        return;
    }
    else
    {
        std::string errMsg = std::string("Could not connect to radio: ") + HAMLIB_FRIENDLY_ERROR_FN(result);
        onRigError(this, errMsg);
        errorEncountered_ = true;
    }
    log_debug("hamlib: rig_open() failed: %s", rigerror(result));

    tmpRig = rig_.load(std::memory_order_acquire);
    rig_.store(nullptr, std::memory_order_release);
    rig_cleanup(tmpRig);
}

void HamlibRigController::disconnectImpl_()
{
    auto tmpRig = rig_.load(std::memory_order_acquire);
    if (tmpRig) 
    {
        // Stop transmitting.
        if (pttSet_)
        {
            pttImpl_(false);
        }
 
        // If we're told to restore on disconnect, do so.
        if (restoreOnDisconnect_)
        {
            vfo_t currVfo = getCurrentVfo_(); 
            setFrequencyHelper_(currVfo, origFreq_);
            if (!freqOnly_)
            {
                setModeHelper_(currVfo, origMode_);
            }

        }
        
        origFreq_ = 0;
        origMode_ = RIG_MODE_NONE;

        // Only requests made from here on should be pushed on the next connect.
        currFreq_ = 0;
        currMode_ = RIG_MODE_NONE;
        pendingMode_ = UNKNOWN;
        
        rig_.store(nullptr, std::memory_order_release);
        rig_close(tmpRig);
        rig_cleanup(tmpRig);
        
        onRigDisconnected(this);
    }
}

void HamlibRigController::pttImpl_(bool state)
{
    auto tmpRig = rig_.load(std::memory_order_acquire);
    log_debug("Hamlib::ptt: %d", state);

    if (tmpRig == nullptr)
    {
        return;
    }

    ptt_t on = state ? RIG_PTT_ON : RIG_PTT_OFF;
    if (pttType_ == PTT_VIA_CAT_DATA && on != RIG_PTT_OFF)
    {
        on = RIG_PTT_ON_DATA;
    }
    
    auto oldTime = std::chrono::steady_clock::now();
    int result = RIG_OK;
    if (pttType_ != PTT_VIA_NONE)
    {
        result = rig_set_ptt(tmpRig, RIG_VFO_CURR, on);
    }
    auto newTime = std::chrono::steady_clock::now();
    auto totalTimeMicroseconds = (int)std::chrono::duration_cast<std::chrono::microseconds>(newTime - oldTime).count();
    rigResponseTime_ = std::max(rigResponseTime_.load(), totalTimeMicroseconds);
    
    if (result != RIG_OK) 
    {
        log_debug("rig_set_ptt: error = %s ", rigerror(result));
        
        if (!errorEncountered_)
        {
            std::string errMsg = "Cannot set PTT: " + std::string(HAMLIB_FRIENDLY_ERROR_FN(result));
            onRigError(this, errMsg);
            errorEncountered_ = true;
        }
    }
    else
    {
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

void HamlibRigController::setFrequencyImpl_(uint64_t frequencyHz)
{
    auto tmpRig = rig_.load(std::memory_order_acquire);
    if (tmpRig == nullptr)
    {
        // Not connected yet; this will be pushed to the radio on connect.
        currFreq_ = frequencyHz;
        return;
    }

    if (currFreq_ == frequencyHz)
    {
        log_warn("Attempting to set to same frequency (%" PRIu64 " Hz), ignoring", frequencyHz);
        return;
    }
    
    if (pttSet_)
    {
        // If transmitting, temporarily stop transmitting so we can change the mode.
        int result = rig_set_ptt(tmpRig, RIG_VFO_CURR, RIG_PTT_OFF);
        if (result != RIG_OK) 
        {
            // If we can't stop transmitting, we shouldn't try to change the mode
            // as it'll fail on some radios.
            log_debug("rig_set_ptt: error = %s ", rigerror(result));

            if (!errorEncountered_)
            {
                std::string errMsg = std::string("Could not disable PTT prior to frequency change: ") + HAMLIB_FRIENDLY_ERROR_FN(result);
                onRigError(this, errMsg);
                errorEncountered_ = true;
            }
            
            return;
        }
    }

    vfo_t currVfo = getWritableVfo_();
    setFrequencyHelper_(currVfo, frequencyHz);

    if (pttSet_)
    {
        // If transmitting, temporarily stop transmitting so we can change the mode.
        int result = rig_set_ptt(tmpRig, RIG_VFO_CURR, pttType_ == PTT_VIA_CAT_DATA ? RIG_PTT_ON_DATA : RIG_PTT_ON);
        if (result != RIG_OK) 
        {
            // If we can't stop transmitting, we shouldn't try to change the mode
            // as it'll fail on some radios.
            log_debug("rig_set_ptt: error = %s ", rigerror(result));
            
            if (!errorEncountered_)
            {
                std::string errMsg = std::string("Could not enable PTT after frequency change: ") + HAMLIB_FRIENDLY_ERROR_FN(result);
                onRigError(this, errMsg);
                errorEncountered_ = true;
            }
        }
    }
}

void HamlibRigController::setModeImpl_(IRigFrequencyController::Mode mode)
{
    rmode_t hamlibMode;
    
    switch (mode)
    {
        case USB:
            hamlibMode = RIG_MODE_USB;
            break;
        case LSB:
            hamlibMode = RIG_MODE_LSB;
            break;
        case DIGU:
            hamlibMode = RIG_MODE_PKTUSB;
            break;
        case DIGL:
            hamlibMode = RIG_MODE_PKTLSB;
            break;
        case FM:
            hamlibMode = RIG_MODE_FM;
            break;
        case DIGFM:
            hamlibMode = RIG_MODE_PKTFM;
            break;
        case AM:
            hamlibMode = RIG_MODE_AM;
            break;
        default:
            onRigError(this, "Cannot set mode: mode not recognized");
            return;
    }
    
    auto tmpRig = rig_.load(std::memory_order_acquire);
    if (tmpRig == nullptr || currMode_ == hamlibMode)
    {
        currMode_ = hamlibMode;
        pendingMode_ = mode;
        return;
    }

    if (pttSet_)
    {
        // If transmitting, temporarily stop transmitting so we can change the mode.
        int result = rig_set_ptt(tmpRig, RIG_VFO_CURR, RIG_PTT_OFF);
        if (result != RIG_OK) 
        {
            // If we can't stop transmitting, we shouldn't try to change the mode
            // as it'll fail on some radios.
            log_debug("rig_set_ptt: error = %s ", rigerror(result));
            
            if (!errorEncountered_)
            {
                std::string errMsg = std::string("Could not disable PTT prior to mode change: ") + HAMLIB_FRIENDLY_ERROR_FN(result);
                onRigError(this, errMsg);
                errorEncountered_ = true;
            }
            
            return;
        }
    }

    vfo_t currVfo = getWritableVfo_();
    setModeHelper_(currVfo, hamlibMode);

    if (pttSet_)
    {
        // If transmitting, temporarily stop transmitting so we can change the mode.
        int result = rig_set_ptt(tmpRig, RIG_VFO_CURR, pttType_ == PTT_VIA_CAT_DATA ? RIG_PTT_ON_DATA : RIG_PTT_ON);
        if (result != RIG_OK) 
        {
            // If we can't stop transmitting, we shouldn't try to change the mode
            // as it'll fail on some radios.
            log_debug("rig_set_ptt: error = %s ", rigerror(result));
            
            if (!errorEncountered_)
            {
                std::string errMsg = std::string("Could not enable PTT after mode change: ") + HAMLIB_FRIENDLY_ERROR_FN(result);
                onRigError(this, errMsg);
                errorEncountered_ = true;
            }
        }
    }
}

void HamlibRigController::requestCurrentFrequencyModeImpl_()
{
    auto tmpRig = rig_.load(std::memory_order_acquire);
    if (tmpRig == nullptr || pttSet_)
    {
        // Note: we ignore attempts to retrieve frequency/mode during
        // TX for several reasons:
        //   a. Some radios do not respond to commands during TX (ex: FT-817)
        //   b. Some radios on their own accord will briefly stop TX to handle the command,
        //      causing distortion of the transmitted signal (ex: Red Pitaya with Thetis).
        return;
    }
    
    rmode_t mode = RIG_MODE_NONE;
    pbwidth_t passband = 0;
    vfo_t currVfo = getCurrentVfo_();    

modeAttempt:
    int result = rig_get_mode(tmpRig, currVfo, &mode, &passband);
    if (result != RIG_OK && currVfo == RIG_VFO_CURR)
    {
        log_debug("rig_get_mode: error = %s ", rigerror(result));
        getFreqModeErrorCount_++;
        if (!errorEncountered_ && getFreqModeErrorCount_ > MAX_GET_FREQUENCY_ERR_COUNT)
        {
            std::string errMsg = std::string("Could not retrieve current radio mode: ") + HAMLIB_FRIENDLY_ERROR_FN(result);
            onRigError(this, errMsg);
            errorEncountered_ = true;
        }
    }
    else if (result != RIG_OK)
    {
        // We supposedly have multiple VFOs but ran into problems 
        // trying to get information about the supposed active VFO.
        // Make a last ditch effort using RIG_VFO_CURR before fully failing.
        currVfo = RIG_VFO_CURR;
        goto modeAttempt;
    }
    else
    {
freqAttempt:
        freq_t freq = 0;
        result = rig_get_freq(tmpRig, currVfo, &freq);
        if (result != RIG_OK && currVfo == RIG_VFO_CURR)
        {
            log_debug("rig_get_freq: error = %s ", rigerror(result));
            
            getFreqModeErrorCount_++;

            if (!errorEncountered_ && getFreqModeErrorCount_ > MAX_GET_FREQUENCY_ERR_COUNT)
            {
                std::string errMsg = std::string("Could not get current frequency: ") + HAMLIB_FRIENDLY_ERROR_FN(result);
                onRigError(this, errMsg);
                errorEncountered_ = true;
            }
        }
        else if (result != RIG_OK)
        {
            // We supposedly have multiple VFOs but ran into problems 
            // trying to get information about the supposed active VFO.
            // Make a last ditch effort using RIG_VFO_CURR before fully failing.
            currVfo = RIG_VFO_CURR;
            goto freqAttempt;
        }
        else
        {
            IRigFrequencyController::Mode currMode;
            
            switch (mode)
            {
                case RIG_MODE_USB:
                    currMode = USB;
                    break;
                case RIG_MODE_LSB:
                    currMode = LSB;
                    break;
                case RIG_MODE_PKTUSB:
                    currMode = DIGU;
                    break;
                case RIG_MODE_PKTLSB:
                    currMode = DIGL;
                    break;
                case RIG_MODE_FM:
                    currMode = FM;
                    break;
                case RIG_MODE_PKTFM:
                    currMode = DIGFM;
                    break;
                case RIG_MODE_AM:
                    currMode = AM;
                    break;
                default:
                    currMode = UNKNOWN;
                    break;
            }
            
            // If this is the first time we're retrieving the current frequency/mode,
            // store it for later restore on disconnect.
            if (origFreq_ == 0)
            {
                origFreq_ = freq;
                origMode_ = mode;
            }
            else
            {
                // currFreq_ should only be set after initial retrieval of frequency/mode
                // as the user could specify a frequency to change to on startup. Otherwise,
                // that would be overwritten and the frequency change would not happen.
                currFreq_ = freq;
                currMode_ = mode;
            }
            
            // Reset get freq/mode error count since we don't want intermittent errors
            // to cause a popup.
            getFreqModeErrorCount_ = 0;

            onFreqModeChange(this, freq, currMode);
        }
    }
}

vfo_t HamlibRigController::getCurrentVfo_()
{
    vfo_t vfo = RIG_VFO_CURR;
    
    auto tmpRig = rig_.load(std::memory_order_acquire);
    if (tmpRig == nullptr)
    {
        return vfo;
    }
    
    if (multipleVfos_)
    {
        int result = rig_get_vfo(tmpRig, &vfo);
        if (result != RIG_OK && result != -RIG_ENAVAIL)
        {
            // Note: we'll still attempt operation using RIG_VFO_CURR just in case.
            log_debug("rig_get_vfo: error = %s ", rigerror(result));
            vfo = RIG_VFO_CURR;
        }
    }
    
    return vfo;
}

vfo_t HamlibRigController::getWritableVfo_()
{
    vfo_t vfo = getCurrentVfo_();

    if (vfo == RIG_VFO_MEM)
    {
        // Hamlib can't set frequency/mode directly on a memory channel --
        // switch to VFO A first so the subsequent set_freq/set_mode call
        // succeeds instead of failing with "Invalid parameter".
        auto tmpRig = rig_.load(std::memory_order_acquire);
        if (tmpRig != nullptr)
        {
            int result = rig_set_vfo(tmpRig, RIG_VFO_A);
            if (result == RIG_OK)
            {
                vfo = RIG_VFO_A;
            }
            else
            {
                log_debug("rig_set_vfo: error = %s ", rigerror(result));
            }
        }
    }

    return vfo;
}

void HamlibRigController::setFrequencyHelper_(vfo_t currVfo, uint64_t frequencyHz)
{
    bool setOkay = false;
    
    auto tmpRig = rig_.load(std::memory_order_acquire);
    if (tmpRig == nullptr)
    {
        currFreq_ = frequencyHz;
        return;
    }

    // Avoid setting invalid frequencies as hamlib sometimes doesn't handle this well.
    if (currFreq_ == frequencyHz || frequencyHz == 0)
    {
        return;
    }
    
freqAttempt:
    int result = rig_set_freq(tmpRig, currVfo, frequencyHz);
    if (result != RIG_OK && currVfo == RIG_VFO_CURR)
    {
        log_debug("rig_set_freq: error = %s ", rigerror(result));
        
        if (!errorEncountered_)
        {
            std::string errMsg = std::string("Could not set current frequency: ") + HAMLIB_FRIENDLY_ERROR_FN(result);
            onRigError(this, errMsg);
            errorEncountered_ = true;
        }
    }
    else if (result != RIG_OK)
    {
        // We supposedly have multiple VFOs but ran into problems 
        // trying to get information about the supposed active VFO.
        // Make a last ditch effort using RIG_VFO_CURR before fully failing.
        currVfo = RIG_VFO_CURR;
        goto freqAttempt;
    }
    else
    {
        setOkay = true;
    }
    
    // If we're able to set either frequency or mode, we should update the current state accordingly.
    // The actual configuration of the radio (in case of failure) will auto-update within
    // a few seconds after this update.
    if (setOkay)
    {
        currFreq_ = frequencyHz;
        if (!destroying_)
        {
            requestCurrentFrequencyModeImpl_();
        }
    }
}

void HamlibRigController::setModeHelper_(vfo_t currVfo, rmode_t mode)
{
    bool setOkay = false;
    
    auto tmpRig = rig_.load(std::memory_order_acquire);
    if (tmpRig == nullptr)
    {
        currMode_ = mode;
        return;
    }
    
    // Avoid setting invalid mode as hamlib sometimes doesn't handle this well.
    if (currMode_ == mode || mode == RIG_MODE_NONE)
    {
        return;
    }

modeAttempt:
    int result = rig_set_mode(tmpRig, currVfo, mode, RIG_PASSBAND_NOCHANGE);
    if (result != RIG_OK && currVfo == RIG_VFO_CURR)
    {
        log_debug("rig_set_mode: error = %s ", rigerror(result));
        
        if (!errorEncountered_)
        {
            std::string errMsg = std::string("Could not set mode frequency: ") + HAMLIB_FRIENDLY_ERROR_FN(result);
            onRigError(this, errMsg);
            errorEncountered_ = true;
        }
    }
    else if (result != RIG_OK)
    {
        // We supposedly have multiple VFOs but ran into problems 
        // trying to get information about the supposed active VFO.
        // Make a last ditch effort using RIG_VFO_CURR before fully failing.
        currVfo = RIG_VFO_CURR;
        goto modeAttempt;
    }
    else
    {
        setOkay = true;
    }

    // If we're able to set either frequency or mode, we should update the current state accordingly.
    // The actual configuration of the radio (in case of failure) will auto-update within
    // a few seconds after this update.
    if (setOkay)
    {
        currMode_ = mode;

        // We request the frequency to be set again to work around an issue with the 
        // FTDX10 where mode changes between LSB and USB cause the VFO frequency to shift
        // by 700 Hz. This ensures that the frequency we're currently set to is in sync
        // with the VFO frequency.
        if (currFreq_ > 0)
        {
            result = rig_set_freq(tmpRig, currVfo, currFreq_);
            if (result != RIG_OK)
            {
                log_debug("rig_set_freq: error = %s ", rigerror(result));
            }
        }

        if (!destroying_)
        {
            requestCurrentFrequencyModeImpl_();
        }
    }
}
