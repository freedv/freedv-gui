//=========================================================================
// Name:            TxRxThread.h
// Purpose:         Implements the main processing thread for audio I/O.
//
// Authors:         Mooneer Salem
// License:
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// - Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// - Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
// OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//=========================================================================

#ifndef AUDIO_PIPELINE__TX_RX_THREAD_H
#define AUDIO_PIPELINE__TX_RX_THREAD_H

#include <assert.h>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <ctime>

#include "AudioPipeline.h"
#include "util/IRealtimeHelper.h"
#include "util/realtime_fp.h"
#include "util/Semaphore.h"
#include "freedv_sanitizers.h"

// Forward declarations
class LinkStep;
class BeepStep;
class AgcStep;

//#define ENABLE_PROCESSING_STATS

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// class txRxThread - tx/rx processing thread
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=

// Callbacks that replace TxRxThread reaching directly into the wx UI
// (g_parent/MainFrame) and MainApp's tone-interferer settings. The app wires
// these to MainFrame::CallAfter() and MainApp members; unit tests wire them
// to counters. All are called from the real-time processing thread.
struct TxRxThreadUiHooks
{
    std::function<void()> playFileToMicInFinished;
    std::function<void()> recFileFromRadioFinished;
    std::function<void()> playFileFromRadioFinished;

    // Tone getters are realtime_fp (raw function pointers): ToneInterfererStep
    // and EitherOrStep only accept RT-safe function pointers, so the app wires
    // these to captureless lambdas reading MainApp's atomics.
    realtime_fp<float()> toneFreqHz;
    realtime_fp<float()> toneAmplitude;
    realtime_fp<bool()> toneEnabled;

    // Leveler state from the previous session and the loudness target,
    // read when the thread is created. saveLevelerState is called at the
    // end of Entry() so the next session resumes from the current gain.
    float levelerGainDb = 0.0f;
    float levelerIntegralErrorDb = 0.0f;
    float levelerTargetLufs = -26.0f;
    std::function<void(float gainDb, float integralErrorDb)> saveLevelerState;
};

class TxRxThread
{
public:
    TxRxThread(bool tx, int inputSampleRate, int outputSampleRate, std::shared_ptr<LinkStep> micAudioLink, std::shared_ptr<IRealtimeHelper> helper, TxRxThreadUiHooks uiHooks)
        : m_tx(tx)
        , m_run(1)
        , pipeline_(nullptr)
        , inputSampleRate_(inputSampleRate)
        , outputSampleRate_(outputSampleRate)
        , equalizedMicAudioLink_(std::move(micAudioLink))
        , uiHooks_(std::move(uiHooks))
        , beepStep_(nullptr)
        , agcStep_(nullptr)
        , hasEooBeenSent_(false)
        , pendingEooCount_(0)
        , helper_(std::move(helper))
        , deferReset_(false)
        , reachedMinimumTxInput_(false)
        , coveredInputSamples_(0)
        , totalCoveredInputSamples_(0)
    { 
        assert(inputSampleRate_ > 0);
        assert(outputSampleRate_ > 0);

        auto numSamples = std::max(inputSampleRate_, outputSampleRate_);
        inputSamples_ = std::make_unique<short[]>(numSamples);
        assert(inputSamples_ != nullptr);
        inputSamplesZeros_ = std::make_unique<short[]>(numSamples);
        assert(inputSamplesZeros_ != nullptr);
        memset(inputSamplesZeros_.get(), 0, numSamples * sizeof(short));

        // Holds resampled EOO output that couldn't be written to outfifo1 yet
        // (e.g. because it was momentarily full) so it can be retried on a
        // later call instead of being silently dropped. The pipeline hands
        // back the entire EOO block in a single execute() call, and its
        // resampling stage caps output at one second's worth of samples, so
        // sizing this to outputSampleRate_ is sufficient headroom.
        pendingEooSamples_ = std::make_unique<short[]>(outputSampleRate_);
        assert(pendingEooSamples_ != nullptr);
    }
    
    virtual ~TxRxThread()
    {
        // Free allocated buffer
        stop();
        inputSamples_ = nullptr;
    }

    void start()
    {
        thread_ = std::thread(std::bind(&TxRxThread::Entry, this));
    }

    void stop()
    {
        m_run.store(false, std::memory_order_release);
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    // thread execution starts here
    void *Entry() noexcept;

    void waitForReady() { readySem_.wait(); }
    void signalToStart() { startSem_.signal(); }

private:
    bool  m_tx;
    std::atomic<bool>  m_run;
    std::unique_ptr<AudioPipeline> pipeline_;
    int inputSampleRate_;
    int outputSampleRate_;
    std::shared_ptr<LinkStep> equalizedMicAudioLink_;
    TxRxThreadUiHooks uiHooks_;
    BeepStep* beepStep_;
    // Non-owning (pipeline_ owns it), TX side only. Polled for the AGC
    // gain plot, and used at the end of Entry() to save the leveler's gain
    // state to config.
    AgcStep* agcStep_;
    bool hasEooBeenSent_;
    std::unique_ptr<short[]> pendingEooSamples_;
    int pendingEooCount_;
    std::shared_ptr<IRealtimeHelper> helper_;
    std::unique_ptr<short[]> inputSamples_;
    std::unique_ptr<short[]> inputSamplesZeros_;
    bool deferReset_;
    std::thread thread_;

    Semaphore readySem_;
    Semaphore startSem_;

    bool reachedMinimumTxInput_;

    // Microphone audio that was late, covered with silence so the signal
    // going to the radio doesn't break (see txProcessing_()). Counted in
    // input samples: how much of the current shortfall has been covered,
    // which is capped, and how much in all since this thread was made.
    int coveredInputSamples_;
    long totalCoveredInputSamples_;

#if defined(ENABLE_PROCESSING_STATS)
    // Tracks min/max/mean/stdev plus a histogram for one timed interval.
    // Used for two separate things: actual per-frame processing time
    // (pipeline_->execute()), and the gap between wake cycles that the
    // thread spends inside helper->stopRealTimeWork()'s semaphore wait --
    // the latter previously wasn't measured at all, so a scheduling delay
    // (the thread taking longer than expected to be woken/resumed, as
    // opposed to processing taking longer than expected once running)
    // would have been completely invisible.
    //
    // min/max/mean/stdev alone can't distinguish "one freak outlier" from
    // "a real cluster of slow samples" -- a rare but non-negligible tail
    // gets averaged away by mean/stdev over a large sample count, and only
    // the single worst sample shows up in max. The histogram makes that
    // distribution visible.
    struct TimingStats
    {
        bool started = false;
        int numSamples = 0;
        double minDuration = 1e9;
        std::time_t minTime = 0;
        double maxDuration = 0;
        std::time_t maxTime = 0;
        double sumDuration = 0;
        double sumDoubleDuration = 0;
        std::chrono::time_point<std::chrono::high_resolution_clock> timeStart;

        // Upper bound (in ms) of each histogram bucket except the last,
        // which catches everything at or above the final bound.
        static constexpr double HISTOGRAM_BUCKET_BOUNDS_MS[] =
            { 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000 };
        static constexpr int NUM_HISTOGRAM_BUCKETS =
            sizeof(HISTOGRAM_BUCKET_BOUNDS_MS) / sizeof(HISTOGRAM_BUCKET_BOUNDS_MS[0]) + 1;
        int histogramCounts[NUM_HISTOGRAM_BUCKETS] = {};

        void reset();
        void start();
        // No-op if start() wasn't called first (e.g. the very first wake
        // cycle has no preceding wait to measure).
        void end();
        void report(bool m_tx, const char* label) const;
    };

    TimingStats processingStats_;
    TimingStats waitStats_;
#endif // defined(ENABLE_PROCESSING_STATS)
    
    void initializePipeline_();
    // One iteration of the processing loop (startRealTimeWork ->
    // tx/rxProcessing_ -> stopRealTimeWork). Extracted from Entry() so unit
    // tests can drive a full cycle without spawning the thread.
    void runOnce_(IRealtimeHelper* helper) FREEDV_NONBLOCKING;
    void txProcessing_(IRealtimeHelper* helper) FREEDV_NONBLOCKING;
    void rxProcessing_(IRealtimeHelper* helper) FREEDV_NONBLOCKING;
    void clearFifos_() FREEDV_NONBLOCKING;

    // Lets TxRxThreadTest drive runOnce_/txProcessing_/rxProcessing_/
    // initializePipeline_ directly.
    friend class TxRxThreadTest;
};

#endif // AUDIO_PIPELINE__TX_RX_THREAD_H
