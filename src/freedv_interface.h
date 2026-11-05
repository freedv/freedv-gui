//==========================================================================
// Name:            freedv_interface.h
// Purpose:         Implements a wrapper around the Codec2 FreeDV interface.
// Created:         March 31, 2021
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

#ifndef FREEDV_INTERFACE_H
#define FREEDV_INTERFACE_H

#include <pthread.h>

#include <deque>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <queue>
#include <future>
#include <atomic>

extern "C" 
{
    #include "fargan_config.h"
}

#include "pipeline/modem_stats.h"

// RADE required include files
#include "rade_api.h"
#include "pipeline/rade_text.h"

// TBD - need to wrap in "extern C" to avoid linker errors
extern "C"
{
    #include "fargan.h"
    #include "lpcnet.h"
}

#include "util/IRealtimeHelper.h"
#include "freedv_sanitizers.h"
#include "util/realtime_fp.h"
#include "util/GenericFIFO.h"

class IPipelineStep;
class RADETransmitStep;
class RADEReceiveStep;

class FreeDVInterface
{
public:
    FreeDVInterface();
    virtual ~FreeDVInterface();
    
    void start(int fifoSizeMs, bool usingReliableText, bool verbose = false);
    void stop();
    bool isRunning() const { return rade_.load() != nullptr; }
    
    const char* getCurrentModeStr() const;
    const char* getCurrentTxModeStr() const;
    
    int getTotalBits();
    int getTotalBitErrors();
    
    float getVariance() const;
    
    int getErrorPattern(short** outputPattern);
    
    int getSync() const;
    void setSync(int val) FREEDV_NONBLOCKING;
    
    void setTextCallbackFn(void (*rxFunc)(void *, char), char (*txFunc)(void *));
    
    int getTxModemSampleRate() const FREEDV_NONBLOCKING;
    int getTxSpeechSampleRate() const;
    int getTxNumSpeechSamples() const;
    int getTxNNomModemSamples() const FREEDV_NONBLOCKING;
    
    int getRxModemSampleRate() const;
    int getRxNumModemSamples() const;
    int getRxNumSpeechSamples() const FREEDV_NONBLOCKING;
    int getRxSpeechSampleRate() const FREEDV_NONBLOCKING;
    
    void setCarrierAmplitude(int c, float amp);

    float getCurrentRxModemOffset();
    
    struct MODEM_STATS* getCurrentRxModemStats() { return &modemStatsList_[0]; }
    
    void resetReliableText();
    const char* getReliableText();
    void setReliableText(const char* callsign);
    
    IPipelineStep* createTransmitPipeline(
        int inputSampleRate, 
        int outputSampleRate); 
    IPipelineStep* createReceivePipeline(
        int inputSampleRate, int outputSampleRate,
        realtime_fp<std::atomic<int>*()> const& getRxStateFn,
        realtime_fp<float()> const& getFreqOffsetFn,
        realtime_fp<float*()> const& getSigPwrAvgFn
    );

    void restartTxVocoder() FREEDV_NONBLOCKING;
 
    float getSNREstimate();
    
private:
    struct ReceivePipelineState
    {
        realtime_fp<std::atomic<int>*()> getRxStateFn;
        realtime_fp<float()> getFreqOffsetFn;
        realtime_fp<float*()> getSigPwrAvgFn;
    };

    static void OnRadeTextRx_(rade_text_t rt, const char* txt_ptr, int length, void* state);
    
    static float GetMinimumSNR_(int mode);
    
    void (*textRxFunc_)(void *, char);
    
    struct MODEM_STATS* modemStatsList_;
    
    std::string receivedReliableText_;
    std::mutex reliableTextMutex_;
  
    // Atomic because isRunning() is read from the UI (e.g. the reporter
    // dialog's queued actions) while start()/stop() run on the
    // Start/Stop worker threads.
    std::atomic<struct rade*> rade_;
    FARGANState fargan_;
    LPCNetEncState *lpcnetEncState_; 
    RADETransmitStep *radeTxStep_;
    std::atomic<int> sync_;
    std::atomic<int> radeSnr_;
    std::atomic<float> rxFreqOffset_; // published by the RX thread for the GUI
    rade_text_t radeTextPtr_;
    
    static constexpr int RELIABLE_TEXT_FIFO_SIZE = 64;
    GenericFIFO<char> reliableTextFifo_;

    void radeSyncFn_(RADEReceiveStep* step) FREEDV_NONBLOCKING;
};

#endif // FREEDV_INTERFACE_H
