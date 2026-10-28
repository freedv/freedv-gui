//==========================================================================
// Name:            freedv_interface.cpp
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

#if defined(__clang__)
#if defined(__has_feature) && __has_feature(realtime_sanitizer)
#include <sanitizer/rtsan_interface.h>
#endif // defined(__has_feature) && __has_feature(realtime_sanitizer)
#endif // defined(__clang__)

#include <chrono>
#include <future>
#include <atomic>
#include <memory>
#include <vector>

#include "main.h"
#include "lpcnet.h"
#include "pipeline/RADEReceiveStep.h"
#include "pipeline/RADETransmitStep.h"
#include "pipeline/AudioPipeline.h"
#include "pipeline/BandwidthExpandStep.h"
#include "pipeline/EitherOrStep.h"

#include "util/logging/ulog.h"

using namespace std::placeholders;

#include <wx/string.h>
extern wxString utFreeDVMode;

extern std::atomic<bool> g_bwExpandEnabled;

static const char* GetCurrentModeStrImpl_()
{
    return "RADEV2";
}

FreeDVInterface::FreeDVInterface() :
    textRxFunc_(nullptr),
    modemStatsList_(nullptr),
    rade_(nullptr),
    lpcnetEncState_(nullptr),
    radeTxStep_(nullptr),
    sync_(0),
    rxFreqOffset_(0.0f),
    radeTextPtr_(nullptr),
    reliableTextFifo_(RELIABLE_TEXT_FIFO_SIZE + 1)
{
    // empty
}

FreeDVInterface::~FreeDVInterface()
{
    if (isRunning()) stop();
}

void FreeDVInterface::OnRadeTextRx_(rade_text_t, const char* txt_ptr, int, void* state) 
{
    log_info("FreeDVInterface::OnRadeTextRx_: received %s", txt_ptr);
    
    FreeDVInterface* obj = (FreeDVInterface*)state;
    assert(obj != nullptr);
    
    char tmpBuf[RELIABLE_TEXT_FIFO_SIZE];
    memset(tmpBuf, 0, RELIABLE_TEXT_FIFO_SIZE);
    strncpy(tmpBuf, txt_ptr, RELIABLE_TEXT_FIFO_SIZE - 1);
    obj->reliableTextFifo_.write(tmpBuf, RELIABLE_TEXT_FIFO_SIZE);
}

void FreeDVInterface::resetReliableText()
{
    std::unique_lock<std::mutex> lock(reliableTextMutex_);
    receivedReliableText_ = "";
    reliableTextFifo_.reset();
}

const char* FreeDVInterface::getReliableText()
{    
    std::unique_lock<std::mutex> lock(reliableTextMutex_);
    
    if (reliableTextFifo_.numUsed() > 0)
    {
        char tmpBuf[RELIABLE_TEXT_FIFO_SIZE] = { 0 };
        if (reliableTextFifo_.read(tmpBuf, RELIABLE_TEXT_FIFO_SIZE) == 0)
        {
            receivedReliableText_ = tmpBuf;
        }
    }
    
    char* ret = new char[receivedReliableText_.size() + 1];
    assert(ret != nullptr);
    
    if (receivedReliableText_.size() > 0)
    {
        strncpy(ret, receivedReliableText_.c_str(), receivedReliableText_.size());
    }
    ret[receivedReliableText_.size()] = 0;
    return ret;
}

void FreeDVInterface::start(int, bool usingReliableText, bool verbose)
{
    sync_.store(0, std::memory_order_release);

    modemStatsList_ = new MODEM_STATS[1];
    modem_stats_open(&modemStatsList_[0]);
    
    // Suppress const string warning.
    // TBD - modelFile may be used by RADE in the future!
    char modelFile[1];
    modelFile[0] = 0;
    rade_ = rade_open(modelFile, RADE_USE_C_ENCODER | RADE_USE_C_DECODER | RADE_MODE_V2 | (verbose ? 0 : RADE_VERBOSE_0));
    assert(rade_ != nullptr);

    // rade_tx()/rade_rx() are the first code paths to actually read every byte of
    // RADE's large statically-compiled neural net weight tables -- rade_open() above
    // only points internal structs at that memory, it doesn't touch it. On a loaded
    // machine those pages can still need to be faulted in from scratch, which has been
    // observed to stall the real-time TX/RX thread by hundreds of ms on its very first
    // live frame (right after PTT, or right after initial sync).
    //
    // That includes the decoder and FARGAN, which only run once the receiver has
    // sync: on macOS CI the first synced frame took ~1 s every time while silence
    // was all that had been decoded. So run a short over through the encoder and
    // receiver until the decoder produces features, then synthesize them once.
    //
    // Do this on throwaway instances, NOT rade_ or fargan_: rade_rx()'s
    // sync detector keeps IIR-smoothed state (Ry_smooth, frame_sync_odd/even, etc.)
    // inside struct rade that rade_open() zeroes but a live call does not -- feeding
    // it one warmup frame nudges that state off its pristine zeroed start and was
    // confirmed via CI to cause intermittent loss of sync during real acquisition.
    // The weight tables a throwaway instance touches are the same static, read-only
    // memory the real instances read, so paging them in this way still avoids
    // the first-frame stalls without perturbing any state they will actually use.
    {
        auto warmupStart = std::chrono::steady_clock::now();
        struct rade* warmupRade = rade_open(modelFile, RADE_USE_C_ENCODER | RADE_USE_C_DECODER | RADE_MODE_V2 | RADE_VERBOSE_0);
        if (warmupRade != nullptr)
        {
            int numFeatures = rade_n_features_in_out(warmupRade);
            std::vector<float> warmupFeatures(numFeatures, 0.0f);
            std::vector<RADE_COMP> warmupTxOut(rade_n_tx_out(warmupRade));
            std::vector<RADE_COMP> warmupSignal;
            std::vector<float> warmupRxFeatures(numFeatures, 0.0f);
            size_t signalPos = 0;
            int decodedFrames = 0;

            // Acquisition takes ~1.5 s of signal; give up after 10 s worth.
            constexpr size_t MAX_WARMUP_SAMPLES = 10 * RADE_MODEM_SAMPLE_RATE;
            while (decodedFrames == 0 && signalPos < MAX_WARMUP_SAMPLES)
            {
                int nin = rade_nin(warmupRade);
                while (warmupSignal.size() - signalPos < (size_t)nin)
                {
                    int ntx = rade_tx(warmupRade, warmupTxOut.data(), warmupFeatures.data());
                    warmupSignal.insert(warmupSignal.end(), warmupTxOut.begin(), warmupTxOut.begin() + ntx);
                }

                int warmupHasEoo = 0;
                if (rade_rx(warmupRade, warmupRxFeatures.data(), &warmupHasEoo, nullptr, warmupSignal.data() + signalPos) > 0)
                {
                    decodedFrames++;
                }
                signalPos += nin;
            }
            rade_close(warmupRade);

            if (decodedFrames == 0)
            {
                log_warn("RADE decoder warmup didn't get sync; first synced frame may be slow");
            }
            else
            {
                log_info("RADE decoder warmed up after %d ms of signal", (int)(signalPos * 1000 / RADE_MODEM_SAMPLE_RATE));
            }

            auto warmupFargan = std::make_unique<FARGANState>();
            float warmupPcm0[320] = {0};
            float warmupContFeatures[5*NB_TOTAL_FEATURES] = {0};
            float warmupSynthFeatures[NB_TOTAL_FEATURES] = {0};
            float warmupPcm[LPCNET_FRAME_SIZE];
            fargan_init(warmupFargan.get());
            fargan_cont(warmupFargan.get(), warmupPcm0, warmupContFeatures);
            fargan_synthesize(warmupFargan.get(), warmupPcm, warmupSynthFeatures);

            log_info("RADE warmup took %d ms", (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - warmupStart).count());
        }
    }

    if (usingReliableText)
    {
        log_info("creating RADE text object");
        radeTextPtr_ = rade_text_create();
        assert(radeTextPtr_ != nullptr);

        rade_text_set_rx_callback(radeTextPtr_, &FreeDVInterface::OnRadeTextRx_, this);

        if (utFreeDVMode != "")
        {
            rade_text_enable_stats_output(radeTextPtr_, true);
        }
    }

    float zeros[320] = {0};
    float in_features[5*NB_TOTAL_FEATURES] = {0};
    fargan_init(&fargan_);
    fargan_cont(&fargan_, zeros, in_features);

    lpcnetEncState_ = lpcnet_encoder_create();
    assert(lpcnetEncState_ != nullptr);
}

void FreeDVInterface::stop()
{
    modem_stats_close(&modemStatsList_[0]);
    delete[] modemStatsList_;
    modemStatsList_ = nullptr;
    
    if (radeTextPtr_ != nullptr)
    {
        rade_text_destroy(radeTextPtr_);
        radeTextPtr_ = nullptr;
    }
    
    modemStatsList_ = nullptr;

    if (rade_ != nullptr)
    {
        rade_close(rade_);
    }
    rade_ = nullptr;

    if (lpcnetEncState_ != nullptr)
    {
        lpcnet_encoder_destroy(lpcnetEncState_);
    }
    lpcnetEncState_ = nullptr;
    radeTxStep_ = nullptr;
}

int FreeDVInterface::getTotalBits()
{
    // Special case for RADE.
    return 1;
}

int FreeDVInterface::getTotalBitErrors()
{
    // Special case for RADE.
    return 0;
}

float FreeDVInterface::getVariance() const
{
    // Special case for RADE.
    return 0.0;
}

const char* FreeDVInterface::getCurrentModeStr() const
{
    return GetCurrentModeStrImpl_();
}

const char* FreeDVInterface::getCurrentTxModeStr() const
{
    return GetCurrentModeStrImpl_();
}

int FreeDVInterface::getSync() const
{
    return sync_.load(std::memory_order_acquire);
}

int FreeDVInterface::getTxModemSampleRate() const FREEDV_NONBLOCKING
{
    return RADE_MODEM_SAMPLE_RATE;
}

int FreeDVInterface::getTxSpeechSampleRate() const
{
    return RADE_SPEECH_SAMPLE_RATE;
}

int FreeDVInterface::getTxNumSpeechSamples() const
{
    return rade_n_features_in_out(rade_) * LPCNET_FRAME_SIZE / NB_TOTAL_FEATURES;
}

int FreeDVInterface::getTxNNomModemSamples() const FREEDV_NONBLOCKING
{
    // Verified that rade_api.c from librade has no unbounded operations
    // as of 2025-10-03.
    FREEDV_BEGIN_VERIFIED_SAFE
    return std::max(rade_n_tx_out(rade_), radeTxStep_ != nullptr ? radeTxStep_->eooLengthInSamples() : rade_n_tx_eoo_out(rade_));
    FREEDV_END_VERIFIED_SAFE
}

int FreeDVInterface::getRxModemSampleRate() const
{
    return RADE_MODEM_SAMPLE_RATE;
}

int FreeDVInterface::getRxNumModemSamples() const
{
    return rade_nin_max(rade_);
}

int FreeDVInterface::getRxNumSpeechSamples() const FREEDV_NONBLOCKING
{
    return rade_n_features_in_out(rade_) * LPCNET_FRAME_SIZE / NB_TOTAL_FEATURES;
}

int FreeDVInterface::getRxSpeechSampleRate() const FREEDV_NONBLOCKING
{
    return RADE_SPEECH_SAMPLE_RATE;
}

void FreeDVInterface::setReliableText(const char* callsign)
{
    if (rade_ != nullptr && radeTextPtr_ != nullptr)
    {
        log_info("generating RADE text string");
        rade_text_generate_tx_string(radeTextPtr_, callsign, strlen(callsign));
    }
}

float FreeDVInterface::getSNREstimate()
{
    return (getSync() ? radeSnr_.load(std::memory_order_acquire) : 0);
}

float FreeDVInterface::getCurrentRxModemOffset()
{
    // Read from the GUI thread, so use the copy the RX thread publishes
    // rather than reading the modem state it's updating.
    return rxFreqOffset_.load(std::memory_order_acquire);
}

IPipelineStep* FreeDVInterface::createTransmitPipeline(
    int inputSampleRate, 
    int outputSampleRate) 
{
    // Special handling for RADE. Note that ParallelStep is not being used
    // as it has known issues with Bluetooth and macOS (but only with RADE;
    // analog and legacy modes appear to function properly).
    radeTxStep_ = new RADETransmitStep(rade_, lpcnetEncState_, radeTextPtr_);
        
    auto pipeline = new AudioPipeline(inputSampleRate, outputSampleRate);
    pipeline->appendPipelineStep(radeTxStep_);
    return pipeline;
}

IPipelineStep* FreeDVInterface::createReceivePipeline(
    int inputSampleRate, int outputSampleRate,
    realtime_fp<std::atomic<int>*()> const& getRxStateFn,
    realtime_fp<float()> const& getFreqOffsetFn,
    realtime_fp<float*()> const& getSigPwrAvgFn)
{
    std::shared_ptr<ReceivePipelineState> state = std::make_shared<ReceivePipelineState>();
    assert(state != nullptr);

    state->getRxStateFn = getRxStateFn;
    state->getFreqOffsetFn = getFreqOffsetFn;
    state->getSigPwrAvgFn = getSigPwrAvgFn;
   
    auto rxStep = new RADEReceiveStep(rade_, &fargan_, radeTextPtr_, +[](RADEReceiveStep* step) FREEDV_NONBLOCKING {
        FreeDVInterface* state = (FreeDVInterface*)step->getStateObj();
        auto finalSync = step->getSync();
        (*step->getRxStateFn()()).store(finalSync, std::memory_order_release);
        state->sync_.store(finalSync, std::memory_order_release);
        state->radeSnr_.store(step->getSnr(), std::memory_order_release);

        // Verified that rade_freq_offset() is a simple getter as of 2026-10-22.
        FREEDV_BEGIN_VERIFIED_SAFE
        state->rxFreqOffset_.store(rade_freq_offset(state->rade_), std::memory_order_release);
        FREEDV_END_VERIFIED_SAFE
    }, getFreqOffsetFn);
    rxStep->setStateObj(this);
    rxStep->setRxStateFn(getRxStateFn);

    auto pipeline = new AudioPipeline(inputSampleRate, outputSampleRate);
    pipeline->appendPipelineStep(rxStep);
     
    auto bwExpandStep = new BandwidthExpandStep();
    auto bwExpandBypass = new AudioPipeline(bwExpandStep->getInputSampleRate(), bwExpandStep->getOutputSampleRate());
        
    auto eitherOrBwExpandStep = new EitherOrStep(
        +[]() FREEDV_NONBLOCKING { return g_bwExpandEnabled.load(std::memory_order_acquire); },
        bwExpandStep,
        bwExpandBypass
    );
    pipeline->appendPipelineStep(eitherOrBwExpandStep);

    return pipeline;
}

void FreeDVInterface::restartTxVocoder() FREEDV_NONBLOCKING
{ 
    radeTxStep_->restartVocoder(); 
}
