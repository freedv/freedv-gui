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

#include <chrono>
#include <cstring>
#include <sstream>
using namespace std::chrono_literals;

#include "freedv_sanitizers.h"

// WebRTC uses FS, which is defined in defines.h. Thus, it needs to be included
// first.
#include "AgcStep.h"

// This forces us to use freedv-gui's version rather than another one.
// TBD -- may not be needed once we fully switch over to the audio pipeline.
#include "../defines.h"

#include "TxRxThread.h"
#include "paCallbackData.h"

#include "PlaybackStep.h"
#include "EitherOrStep.h"
#include "RNNoiseStep.h"
#include "EqualizerStep.h"
#include "ResamplePlotStep.h"
#include "ResampleStep.h"
#include "TapStep.h"
#include "LevelAdjustStep.h"
#include "FreeDVTransmitStep.h"
#include "RecordStep.h"
#include "ToneInterfererStep.h"
#include "ComputeRfSpectrumStep.h"
#include "FreeDVReceiveStep.h"
#include "MuteStep.h"
#include "LinkStep.h"
#include "BeepStep.h"
#include "MixStep.h"

#include "util/logging/ulog.h"
#include "os/os_interface.h"

#include "codec2_alloc.h"

// Experimental options for potential future release:
//
// * ENABLE_FASTER_PLOTS: This uses a faster resampling algorithm to reduce the CPU
//   usage required to generate various plots in the user interface. (Tech note: When
//   enabled, r8brain uses a wider transition band for the plot resampling.)
// * ENABLE_PROCESSING_STATS: This causes execution statistics to be collected for RX and TX
//   processing and output in the log after the user pushes Stop. (Define in .h file.)

#define ENABLE_FASTER_PLOTS

// External globals
// TBD -- work on fully removing the need for these.
extern paCallBackData* g_rxUserdata;
extern std::atomic<int> g_analog;
extern std::atomic<int> g_nSoundCards;
extern std::atomic<bool> g_half_duplex;
extern std::atomic<bool> g_tx;
extern std::atomic<int> g_dump_fifo_state;
extern std::atomic<bool> endingTx;
extern std::atomic<bool> g_playFileToMicIn;
extern std::atomic<int> g_sfTxFs;
extern std::atomic<bool> g_loopPlayFileToMicIn;
extern std::atomic<float> g_TxFreqOffsetHz;
extern GenericFIFO<short> g_plotSpeechInFifo;
extern GenericFIFO<short> g_plotDemodInFifo;
extern GenericFIFO<short> g_plotSpeechOutFifo;
extern int g_mode;
extern int g_txLevel;
extern std::atomic<float> g_txLevelScale;
extern std::atomic<int> g_dump_timing;
extern std::atomic<bool> g_queueResync;
extern std::atomic<int> g_resyncs;
extern std::atomic<bool> g_recFileFromRadio;
extern std::atomic<unsigned int> g_recFromRadioSamples;
extern std::atomic<bool> g_playFileFromRadio;
extern std::atomic<int> g_sfFs;
extern std::atomic<bool>     g_totBeepActive;
extern std::atomic<bool> g_loopPlayFileFromRadio;
extern std::atomic<int> g_SquelchActive;
extern std::atomic<float> g_SquelchLevel;
extern std::atomic<float> g_tone_phase;
extern GenericFIFO<float> g_avmag;
extern std::atomic<int> g_State;
extern std::atomic<int> g_channel_noise;
extern std::atomic<float> g_RxFreqOffsetHz;
extern float g_sig_pwr_av;
extern std::atomic<bool> g_voice_keyer_tx;
extern std::atomic<bool> g_eoo_enqueued;
extern std::atomic<bool> g_agcEnabled;
extern std::atomic<int64_t> g_rxMutedUntilNs;
extern std::atomic<bool> g_noiseReductionEnabled;
extern std::atomic<bool> g_monitorTxAudio;
extern std::atomic<bool> g_monitorVoiceKeyerAudio;
extern std::atomic<float> g_monitorTxAudioVol;
extern std::atomic<float> g_monitorVoiceKeyerAudioVol;
extern std::atomic<int> g_noiseSNR;

#include "../freedv_interface.h"
extern FreeDVInterface freedvInterface;


#include <sndfile.h>
extern std::atomic<SNDFILE*> g_sfPlayFile;
extern std::atomic<SNDFILE*>            g_sfRecFileFromModulator;
extern std::atomic<bool>                g_recFileFromModulator;
extern std::atomic<SNDFILE*> g_sfRecFile;
extern std::atomic<SNDFILE*> g_sfRecMicFile;
extern std::atomic<SNDFILE*> g_sfRecDecoderFile;
extern std::atomic<SNDFILE*> g_sfPlayFileFromRadio;

extern std::atomic<bool> g_recFileFromMic;
extern std::atomic<bool> g_recVoiceKeyerFile;
extern std::atomic<bool> g_recFileFromDecoder;

#include "sox_biquad.h"

void TxRxThread::initializePipeline_()
{
    if (m_tx)
    {
        pipeline_ = std::make_unique<AudioPipeline>(inputSampleRate_, outputSampleRate_);

        // Record from mic step (optional)
        auto recordMicStep = new RecordStep(
            inputSampleRate_, 
            []() { return g_sfRecMicFile.load(std::memory_order_acquire); }, 
            [](int) {
                // Recording stops when the user explicitly tells us to,
                // no action required here.
            }
        );
        auto recordMicPipeline = new AudioPipeline(inputSampleRate_, inputSampleRate_);
        recordMicPipeline->appendPipelineStep(recordMicStep);
        
        auto recordMicTap = new TapStep(inputSampleRate_, recordMicPipeline);
        auto bypassRecordMic = new AudioPipeline(inputSampleRate_, inputSampleRate_);
        
        auto eitherOrRecordMic = new EitherOrStep(
            +[]() FREEDV_NONBLOCKING { return (g_recVoiceKeyerFile.load(std::memory_order_relaxed) || g_recFileFromMic.load(std::memory_order_relaxed)) && (g_sfRecMicFile.load(std::memory_order_acquire) != NULL); },
            recordMicTap,
            bypassRecordMic
        );
        pipeline_->appendPipelineStep(eitherOrRecordMic);
        
        // Mic In playback step (optional)
        auto eitherOrBypassPlay = new AudioPipeline(inputSampleRate_, inputSampleRate_);
        auto eitherOrPlayMicIn = new AudioPipeline(inputSampleRate_, inputSampleRate_);
        auto playMicIn = new PlaybackStep(
            inputSampleRate_, 
            []() { return g_sfTxFs.load(std::memory_order_acquire); },
            []() { return g_playFileToMicIn.load(std::memory_order_acquire) ? g_sfPlayFile.load(std::memory_order_acquire) : nullptr; },
            [this]() {
                if (g_loopPlayFileToMicIn.load(std::memory_order_relaxed))
                    sf_seek(g_sfPlayFile.load(std::memory_order_acquire), 0, SEEK_SET);
                else {
                    log_info("playFileFromRadio finished, issuing event!");
                    // Fire-and-forget: blocking this thread on the UI thread
                    // would stall file I/O and drain the playback FIFO.
                    uiHooks_.playFileToMicInFinished();
                }
            }
            );
        eitherOrPlayMicIn->appendPipelineStep(playMicIn);
        
        auto eitherOrPlayStep = new EitherOrStep(
            +[]() FREEDV_NONBLOCKING { return g_playFileToMicIn.load(std::memory_order_acquire) && (g_sfPlayFile.load(std::memory_order_acquire) != NULL); },
            eitherOrPlayMicIn,
            eitherOrBypassPlay);
        pipeline_->appendPipelineStep(eitherOrPlayStep);
        
        // RNNoise step (optional)
        auto eitherOrProcessRNNoise = new AudioPipeline(inputSampleRate_, inputSampleRate_);
        auto eitherOrBypassRNNoise = new AudioPipeline(inputSampleRate_, inputSampleRate_);
        
        auto rnnoiseStep = new RNNoiseStep();
        eitherOrProcessRNNoise->appendPipelineStep(rnnoiseStep);
        
        auto eitherOrRNNoiseStep = new EitherOrStep(
            +[]() FREEDV_NONBLOCKING { return g_noiseReductionEnabled.load(std::memory_order_acquire); },
            eitherOrProcessRNNoise,
            eitherOrBypassRNNoise);
        pipeline_->appendPipelineStep(eitherOrRNNoiseStep);

        // AGC step (optional)
        auto eitherOrProcessAgc = new AudioPipeline(inputSampleRate_, inputSampleRate_);
        auto eitherOrBypassAgc = new AudioPipeline(inputSampleRate_, inputSampleRate_);

        auto agcStep = new AgcStep(inputSampleRate_);
        eitherOrProcessAgc->appendPipelineStep(agcStep);

        auto eitherOrAgcStep = new EitherOrStep(
            +[]() FREEDV_NONBLOCKING { return g_agcEnabled.load(std::memory_order_acquire); },
            eitherOrProcessAgc,
            eitherOrBypassAgc);
        pipeline_->appendPipelineStep(eitherOrAgcStep); 

        // Equalizer step (optional based on filter state)
        auto equalizerStep = new EqualizerStep(
            inputSampleRate_, 
            &g_rxUserdata->micInEQEnable,
            &g_rxUserdata->sbqMicInBass,
            &g_rxUserdata->sbqMicInMid,
            &g_rxUserdata->sbqMicInTreble,
            &g_rxUserdata->sbqMicInVol,
            g_rxUserdata->micEqLock);
        pipeline_->appendPipelineStep(equalizerStep);
        
        // Take TX audio post-equalizer and send it to RX for possible monitoring use.
        if (equalizedMicAudioLink_ != nullptr)
        {
            auto micAudioPipeline = new AudioPipeline(inputSampleRate_, equalizedMicAudioLink_->getSampleRate());
            micAudioPipeline->appendPipelineStep(equalizedMicAudioLink_->getInputPipelineStep());
        
            auto micAudioTap = new TapStep(inputSampleRate_, micAudioPipeline);
            pipeline_->appendPipelineStep(micAudioTap);
        }
                
        // Resample for plot step
        auto resampleForPlotStep = new ResampleForPlotStep(&g_plotSpeechInFifo);
        auto resampleForPlotPipeline = new AudioPipeline(inputSampleRate_, resampleForPlotStep->getOutputSampleRate());
#if defined(ENABLE_FASTER_PLOTS)
        auto resampleForPlotResampler = new ResampleStep(inputSampleRate_, resampleForPlotStep->getInputSampleRate(), true); // need to create manually to get access to "plot only" optimizations
        resampleForPlotPipeline->appendPipelineStep(resampleForPlotResampler);
#endif // defined(ENABLE_FASTER_PLOTS)
        resampleForPlotPipeline->appendPipelineStep(resampleForPlotStep);

        auto resampleForPlotTap = new TapStep(inputSampleRate_, resampleForPlotPipeline);
        pipeline_->appendPipelineStep(resampleForPlotTap);
      
        // FreeDV TX step (analog leg)
        auto doubleLevelStep = new LevelAdjustStep(inputSampleRate_, +[]() FREEDV_NONBLOCKING { return (float)2.0; });
        auto analogTxPipeline = new AudioPipeline(inputSampleRate_, outputSampleRate_);
        analogTxPipeline->appendPipelineStep(doubleLevelStep);
        
        auto digitalTxStep = freedvInterface.createTransmitPipeline(
            inputSampleRate_, 
            outputSampleRate_, 
            +[]() FREEDV_NONBLOCKING { return g_TxFreqOffsetHz.load(std::memory_order_relaxed); },
            helper_);
        auto digitalTxPipeline = new AudioPipeline(inputSampleRate_, outputSampleRate_); 
        digitalTxPipeline->appendPipelineStep(digitalTxStep);
        
        auto eitherOrDigitalAnalog = new EitherOrStep(
            +[]() FREEDV_NONBLOCKING { return g_analog.load(std::memory_order_relaxed) != 0; },
            analogTxPipeline,
            digitalTxPipeline);
        pipeline_->appendPipelineStep(eitherOrDigitalAnalog);

        // Record modulated output (optional)
        auto recordModulatedStep = new RecordStep(
            RECORD_FILE_SAMPLE_RATE, 
            []() { return g_sfRecFileFromModulator.load(std::memory_order_acquire); },
            [](int) {
                // empty
            });
        auto recordModulatedPipeline = new AudioPipeline(outputSampleRate_, recordModulatedStep->getOutputSampleRate());
        recordModulatedPipeline->appendPipelineStep(recordModulatedStep);
        
        auto recordModulatedTap = new TapStep(outputSampleRate_, recordModulatedPipeline);
        auto recordModulatedTapPipeline = new AudioPipeline(outputSampleRate_, outputSampleRate_);
        recordModulatedTapPipeline->appendPipelineStep(recordModulatedTap);
        
        auto bypassRecordModulated = new AudioPipeline(outputSampleRate_, outputSampleRate_);
        
        auto eitherOrRecordModulated = new EitherOrStep(
            +[]() FREEDV_NONBLOCKING { return g_recFileFromModulator.load(std::memory_order_acquire) && (g_sfRecFileFromModulator.load(std::memory_order_acquire) != NULL); },
            recordModulatedTapPipeline,
            bypassRecordModulated);
        pipeline_->appendPipelineStep(eitherOrRecordModulated);
        
        // TX attenuation step
        auto txAttenuationStep = new LevelAdjustStep(outputSampleRate_, +[]() FREEDV_NONBLOCKING {
            return g_txLevelScale.load(std::memory_order_acquire);
        });
        pipeline_->appendPipelineStep(txAttenuationStep);
    }
    else
    {
        pipeline_ = std::make_unique<AudioPipeline>(inputSampleRate_, outputSampleRate_);

        auto activeRxPipeline = new AudioPipeline(inputSampleRate_, outputSampleRate_);

        // Record from radio step (optional)
        auto recordRadioStep = new RecordStep(
            RECORD_FILE_SAMPLE_RATE, 
            []() { return g_sfRecFile.load(std::memory_order_acquire); }, 
            [this](int numSamples) {
                // The budget is unsigned, so stop it at zero rather than letting a write
                // that runs past the end wrap it around. Report the end only once.
                unsigned int left = g_recFromRadioSamples.load(std::memory_order_relaxed);
                unsigned int newLeft;
                do
                {
                    newLeft = left > (unsigned int)numSamples ? left - numSamples : 0;
                } while (!g_recFromRadioSamples.compare_exchange_weak(left, newLeft, std::memory_order_relaxed));

                if (left > 0 && newLeft == 0)
                {
                    // call stop record menu item, should be thread safe
                    uiHooks_.recFileFromRadioFinished();
                }
            }
        );
        auto recordRadioPipeline = new AudioPipeline(inputSampleRate_, recordRadioStep->getOutputSampleRate());
        recordRadioPipeline->appendPipelineStep(recordRadioStep);
        
        auto recordRadioTap = new TapStep(inputSampleRate_, recordRadioPipeline);
        auto bypassRecordRadio = new AudioPipeline(inputSampleRate_, inputSampleRate_);
        
        auto eitherOrRecordRadio = new EitherOrStep(
            +[]() FREEDV_NONBLOCKING { return g_recFileFromRadio.load(std::memory_order_acquire) && (g_sfRecFile.load(std::memory_order_acquire) != NULL); },
            recordRadioTap,
            bypassRecordRadio
        );
        activeRxPipeline->appendPipelineStep(eitherOrRecordRadio);
        
        // Play from radio step (optional)
        auto eitherOrBypassPlayRadio = new AudioPipeline(inputSampleRate_, inputSampleRate_);
        auto eitherOrPlayRadio = new AudioPipeline(inputSampleRate_, inputSampleRate_);
        auto playRadio = new PlaybackStep(
            inputSampleRate_, 
            []() { return g_sfFs.load(std::memory_order_acquire); },
            []() { return g_playFileFromRadio.load(std::memory_order_acquire) ? g_sfPlayFileFromRadio.load(std::memory_order_acquire) : nullptr; },
            [this]() {
                if (g_loopPlayFileFromRadio.load(std::memory_order_relaxed))
                    sf_seek(g_sfPlayFileFromRadio.load(std::memory_order_acquire), 0, SEEK_SET);
                else {
                    log_info("playFileFromRadio finished, issuing event!");
                    // Fire-and-forget: blocking this thread on the UI thread
                    // would stall file I/O and drain the playback FIFO.
                    uiHooks_.playFileFromRadioFinished();
                }
            }
        );
        eitherOrPlayRadio->appendPipelineStep(playRadio);
        
        auto eitherOrPlayRadioStep = new EitherOrStep(
            +[]() FREEDV_NONBLOCKING { 
                auto result = g_playFileFromRadio.load(std::memory_order_acquire) && (g_sfPlayFileFromRadio.load(std::memory_order_acquire) != NULL);
                return result;
            },
            eitherOrPlayRadio,
            eitherOrBypassPlayRadio);
        activeRxPipeline->appendPipelineStep(eitherOrPlayRadioStep);
        
        // Resample for plot step (demod in)
        auto resampleForPlotStep = new ResampleForPlotStep(&g_plotDemodInFifo);
        auto resampleForPlotPipeline = new AudioPipeline(inputSampleRate_, resampleForPlotStep->getOutputSampleRate());
#if defined(ENABLE_FASTER_PLOTS)
        auto resampleForPlotResampler = new ResampleStep(inputSampleRate_, resampleForPlotStep->getInputSampleRate(), true); // need to create manually to get access to "plot only" optimizations
        resampleForPlotPipeline->appendPipelineStep(resampleForPlotResampler);
#endif // defined(ENABLE_FASTER_PLOTS)
        resampleForPlotPipeline->appendPipelineStep(resampleForPlotStep);

        auto resampleForPlotTap = new TapStep(inputSampleRate_, resampleForPlotPipeline);
        activeRxPipeline->appendPipelineStep(resampleForPlotTap);

        // Tone interferer step (optional)
        auto bypassToneInterferer = new AudioPipeline(inputSampleRate_, inputSampleRate_);
        auto toneInterfererStep = new ToneInterfererStep(
            inputSampleRate_,
            uiHooks_.toneFreqHz,
            uiHooks_.toneAmplitude,
            +[]() FREEDV_NONBLOCKING { return &g_tone_phase; }
        );
        auto eitherOrToneInterferer = new EitherOrStep(
            uiHooks_.toneEnabled,
            toneInterfererStep,
            bypassToneInterferer
        );
        activeRxPipeline->appendPipelineStep(eitherOrToneInterferer);
        
        // RF spectrum computation step
        auto computeRfSpectrumStep = new ComputeRfSpectrumStep(
            +[]() FREEDV_NONBLOCKING { return freedvInterface.getCurrentRxModemStats(); },
            +[]() FREEDV_NONBLOCKING { return &g_avmag; }
        );
        auto computeRfSpectrumPipeline = new AudioPipeline(
            inputSampleRate_, computeRfSpectrumStep->getOutputSampleRate());
#if defined(ENABLE_FASTER_PLOTS)
        auto resampleForRfSpectrum = new ResampleStep(inputSampleRate_, computeRfSpectrumStep->getInputSampleRate(), true); // need to create manually to get access to "plot only" optimizations
        computeRfSpectrumPipeline->appendPipelineStep(resampleForRfSpectrum);
#endif // defined(ENABLE_FASTER_PLOTS)
        computeRfSpectrumPipeline->appendPipelineStep(computeRfSpectrumStep);
        
        auto computeRfSpectrumTap = new TapStep(inputSampleRate_, computeRfSpectrumPipeline);
        activeRxPipeline->appendPipelineStep(computeRfSpectrumTap);
        
        // RX demodulation step
        auto bypassRfDemodulationPipeline = new AudioPipeline(inputSampleRate_, outputSampleRate_);
        auto rfDemodulationPipeline = new AudioPipeline(inputSampleRate_, outputSampleRate_);
        auto rfDemodulationStep = freedvInterface.createReceivePipeline(
            inputSampleRate_, outputSampleRate_,
            +[]() FREEDV_NONBLOCKING { return &g_State; },
            +[]() FREEDV_NONBLOCKING { return g_channel_noise.load(std::memory_order_acquire); },
            +[]() FREEDV_NONBLOCKING { return g_noiseSNR.load(std::memory_order_acquire); },
            +[]() FREEDV_NONBLOCKING { return g_RxFreqOffsetHz.load(std::memory_order_relaxed); },
            +[]() FREEDV_NONBLOCKING { return &g_sig_pwr_av; },
            helper_
        );
        rfDemodulationPipeline->appendPipelineStep(rfDemodulationStep);

        // Resample for plot step (speech out)
        auto resampleForPlotOutStep = new ResampleForPlotStep(&g_plotSpeechOutFifo);
        auto resampleForPlotOutPipeline = new AudioPipeline(outputSampleRate_, resampleForPlotOutStep->getOutputSampleRate());
#if defined(ENABLE_FASTER_PLOTS)
        auto resampleForPlotOutResampler = new ResampleStep(outputSampleRate_, resampleForPlotOutStep->getInputSampleRate(), true); // need to create manually to get access to "plot only" optimizations
        resampleForPlotOutPipeline->appendPipelineStep(resampleForPlotOutResampler);
#endif // defined(ENABLE_FASTER_PLOTS)
        resampleForPlotOutPipeline->appendPipelineStep(resampleForPlotOutStep);

        auto resampleForPlotOutTap = new TapStep(outputSampleRate_, resampleForPlotOutPipeline);
        rfDemodulationPipeline->appendPipelineStep(resampleForPlotOutTap);
        
        // Replace received audio with microphone audio if we're monitoring TX/voice keyer recording.
        if (equalizedMicAudioLink_ != nullptr)
        {
            auto bypassMonitorAudio = new AudioPipeline(inputSampleRate_, outputSampleRate_);
            auto mutePipeline = new AudioPipeline(inputSampleRate_, outputSampleRate_);
            
            auto monitorPipeline = new AudioPipeline(inputSampleRate_, outputSampleRate_);
            monitorPipeline->appendPipelineStep(equalizedMicAudioLink_->getOutputPipelineStep());
            
            auto monitorLevelStep = new LevelAdjustStep(outputSampleRate_, +[]() FREEDV_NONBLOCKING {
                float volInDb = 0;
                if (g_voice_keyer_tx.load(std::memory_order_acquire) && g_monitorVoiceKeyerAudio.load(std::memory_order_acquire))
                {
                    volInDb = g_monitorVoiceKeyerAudioVol.load(std::memory_order_acquire);
                }
                else
                {
                    volInDb = g_monitorTxAudioVol.load(std::memory_order_acquire);
                }
                
                return std::exp(volInDb/20.0f * std::log(10.0f));
            });
            monitorPipeline->appendPipelineStep(monitorLevelStep);

            auto muteStep = new MuteStep(inputSampleRate_, outputSampleRate_);
            
            mutePipeline->appendPipelineStep(muteStep);
            
            auto eitherOrMuteStep = new EitherOrStep(
                +[]() FREEDV_NONBLOCKING { return g_recVoiceKeyerFile.load(std::memory_order_relaxed); },
                mutePipeline,
                bypassMonitorAudio
            );

            auto eitherOrMicMonitorStep = new EitherOrStep(
                +[]() FREEDV_NONBLOCKING { return 
                    (g_voice_keyer_tx.load(std::memory_order_acquire) && g_monitorVoiceKeyerAudio.load(std::memory_order_acquire)) || 
                    (g_tx.load(std::memory_order_acquire) && g_monitorTxAudio.load(std::memory_order_acquire)); },
                monitorPipeline,
                eitherOrMuteStep
            );
            bypassRfDemodulationPipeline->appendPipelineStep(eitherOrMicMonitorStep);
        }
        
        EitherOrStep* eitherOrRfDemodulationStep = nullptr;
        if (equalizedMicAudioLink_ != nullptr)
        {
            eitherOrRfDemodulationStep = new EitherOrStep(
                +[]() FREEDV_NONBLOCKING { return g_analog.load(std::memory_order_relaxed) ||
                    (
                        (g_recVoiceKeyerFile.load(std::memory_order_relaxed)) ||
                        (g_voice_keyer_tx.load(std::memory_order_acquire) && g_monitorVoiceKeyerAudio.load(std::memory_order_acquire)) ||
                        (g_tx.load(std::memory_order_acquire) && g_monitorTxAudio.load(std::memory_order_acquire))
                    ); 
                },
                bypassRfDemodulationPipeline,
                rfDemodulationPipeline);
        }
        else
        {
            eitherOrRfDemodulationStep = new EitherOrStep(
                +[]() FREEDV_NONBLOCKING { return g_analog.load(std::memory_order_relaxed) != 0; },
                bypassRfDemodulationPipeline,
                rfDemodulationPipeline);
        }

        activeRxPipeline->appendPipelineStep(eitherOrRfDemodulationStep);

        // Equalizer step (optional based on filter state)
        auto equalizerStep = new EqualizerStep(
            outputSampleRate_, 
            &g_rxUserdata->spkOutEQEnable,
            &g_rxUserdata->sbqSpkOutBass,
            &g_rxUserdata->sbqSpkOutMid,
            &g_rxUserdata->sbqSpkOutTreble,
            &g_rxUserdata->sbqSpkOutVol,
            g_rxUserdata->spkEqLock);
        activeRxPipeline->appendPipelineStep(equalizerStep);

        // Record from decoder step (optional)
        auto recordDecoderStep = new RecordStep(
            outputSampleRate_, 
            []() { return g_sfRecDecoderFile.load(std::memory_order_acquire); }, 
            [](int) {
                // Recording stops when the user explicitly tells us to,
                // no action required here.
            }
        );
        auto recordDecoderPipeline = new AudioPipeline(outputSampleRate_, outputSampleRate_);
        recordDecoderPipeline->appendPipelineStep(recordDecoderStep);
        
        auto recordDecoderTap = new TapStep(outputSampleRate_, recordDecoderPipeline);
        auto bypassRecordDecoder = new AudioPipeline(outputSampleRate_, outputSampleRate_);
        
        auto eitherOrRecordDecoder = new EitherOrStep(
            +[]() FREEDV_NONBLOCKING { return (g_recFileFromDecoder.load(std::memory_order_acquire)) && (g_sfRecDecoderFile.load(std::memory_order_acquire) != NULL); },
            recordDecoderTap,
            bypassRecordDecoder
        );
        activeRxPipeline->appendPipelineStep(eitherOrRecordDecoder);

        auto activeRxMutePipeline = new AudioPipeline(inputSampleRate_, outputSampleRate_);
        auto activeRxMuteStep = new MuteStep(inputSampleRate_, outputSampleRate_);
        activeRxMutePipeline->appendPipelineStep(activeRxMuteStep);

        // TOT beep step: emits a warning beep during countdown
        auto totBeepBypass = new AudioPipeline(inputSampleRate_, outputSampleRate_);
        auto totBeepMuteStep = new MuteStep(inputSampleRate_, outputSampleRate_);
        totBeepBypass->appendPipelineStep(totBeepMuteStep);

        auto totBeepActivePath = new AudioPipeline(inputSampleRate_, outputSampleRate_);
        beepStep_ = new BeepStep(
            outputSampleRate_, 750, 80, 5, 
            +[]() FREEDV_NONBLOCKING {
                return g_totBeepActive.load(std::memory_order_acquire);
            },
            +[](BeepStep& thisStep) FREEDV_NONBLOCKING {
                g_totBeepActive.store(false, std::memory_order_release);
                thisStep.reset();
            }
        );
        totBeepActivePath->appendPipelineStep(beepStep_);
        auto totBeepEitherOr = new EitherOrStep(
            +[]() FREEDV_NONBLOCKING {
                return g_totBeepActive.load(std::memory_order_acquire);
            },
            totBeepActivePath,
            totBeepBypass
        );

        auto activeRxEitherOr = new EitherOrStep(
            +[]() FREEDV_NONBLOCKING {
                bool tmpTx = g_tx.load(std::memory_order_acquire);
                bool tmpVkTx = g_voice_keyer_tx.load(std::memory_order_acquire);
                bool tmpHalfDuplex = g_half_duplex.load(std::memory_order_acquire);
                
                // After TX, stay muted until the radio has actually returned to RX.
                bool tmpWaitingForRadioRx = 
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() <
                    g_rxMutedUntilNs.load(std::memory_order_acquire);
                return
                    (tmpVkTx && g_monitorVoiceKeyerAudio.load(std::memory_order_acquire)) ||
                    (tmpTx && g_monitorTxAudio.load(std::memory_order_acquire)) ||
                    (!tmpVkTx && ((tmpHalfDuplex && !tmpTx && !tmpWaitingForRadioRx) || !tmpHalfDuplex));
            },
            activeRxPipeline,
            activeRxMutePipeline
        );
        
        auto totMixStep = new MixStep(
            activeRxEitherOr,
            totBeepEitherOr
        );
        pipeline_->appendPipelineStep(totMixStep);
       
        // Clear anything in the FIFO before resuming decode.
        clearFifos_();
    }
}

void* TxRxThread::Entry() noexcept
{
    // Get raw pointer so we don't need to constantly access the shared_ptr
    // and thus constantly increment/decrement refcounts.
    IRealtimeHelper* helper = helper_.get();

    // Ensure that O(1) memory allocator is used for Codec2
    // instead of standard malloc().
    codec2_initialize_realtime(CODEC2_REAL_TIME_MEMORY_SIZE);
    
    initializePipeline_();
    
    // Request real-time scheduling from the operating system.
    helper->setHelperRealTime();

#if defined(ENABLE_PROCESSING_STATS)
    processingStats_.reset();
    waitStats_.reset();
#endif // defined(ENABLE_PROCESSING_STATS)

    // Set thread name for debugging
    const char* threadName = nullptr;
    if (m_tx) threadName = "txThread";
    else threadName = "rxThread";
    SetThreadName(threadName);

    // Make sure we don't start processing until
    // the main thread is ready.
    readySem_.signal();
    startSem_.wait();
    clearFifos_();

    while (m_run.load(std::memory_order_acquire))
    {
        runOnce_(helper);
    }

#if defined(ENABLE_PROCESSING_STATS)
    processingStats_.report(m_tx, "processing");
    waitStats_.report(m_tx, "wait");
#endif // defined(ENABLE_PROCESSING_STATS)

    // Force pipeline to delete itself when we're done with the thread.
    pipeline_ = nullptr;
    
    // Return to normal scheduling
    helper->clearHelperRealTime();
    
    codec2_disable_realtime();
    
    return NULL;
}

void TxRxThread::runOnce_(IRealtimeHelper* helper) FREEDV_NONBLOCKING
{
    if (!m_run.load(std::memory_order_acquire)) return;

#if defined(ENABLE_PROCESSING_STATS)
    // Closes out the interval opened by waitStats_.start() below, at
    // the end of the *previous* iteration -- i.e. how long this thread
    // actually spent inside stopRealTimeWork()'s semaphore wait (a
    // scheduling delay would show up here even if processing itself,
    // measured separately in txProcessing_/rxProcessing_, looks fine).
    waitStats_.end();
#endif // defined(ENABLE_PROCESSING_STATS)

    //log_info("thread woken up: m_tx=%d", (int)m_tx);
    helper->startRealTimeWork();

    if (m_tx) txProcessing_(helper);
    else rxProcessing_(helper);

    // Determine whether we need to pause for a shorter amount
    // of time to avoid dropouts.
    paCallBackData  *cbData = g_rxUserdata;
    auto outFifo = cbData->outfifo1;
    if (!m_tx)
    {
        outFifo = (g_nSoundCards.load(std::memory_order_acquire) == 1) ? cbData->outfifo1 : cbData->outfifo2;
    }
    auto totalFifoCapacity = outFifo->capacity();
    auto fifoUsed = outFifo->numUsed();

#if defined(ENABLE_PROCESSING_STATS)
    waitStats_.start();
#endif // defined(ENABLE_PROCESSING_STATS)
    helper->stopRealTimeWork(fifoUsed < totalFifoCapacity / 2);
}

#if defined(ENABLE_PROCESSING_STATS)
void TxRxThread::TimingStats::reset()
{
    started = false;
    numSamples = 0;
    minDuration = 1e9;
    maxDuration = 0;
    sumDuration = 0;
    sumDoubleDuration = 0;
    memset(histogramCounts, 0, sizeof(histogramCounts));
}

void TxRxThread::TimingStats::start()
{
    started = true;
    timeStart = std::chrono::high_resolution_clock::now();
}

void TxRxThread::TimingStats::end()
{
    if (!started) return;
    started = false;

    auto e = std::chrono::high_resolution_clock::now();
    auto d = std::chrono::duration_cast<std::chrono::nanoseconds>(e - timeStart).count();
    numSamples++;
    if (d < minDuration)
    {
        minDuration = d;
        minTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    }
    if (d > maxDuration)
    {
        maxDuration = d;
        maxTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    }
    sumDuration += d; sumDoubleDuration += pow(d, 2);

    double dMs = d / 1.0e6;
    int bucket = 0;
    while (bucket < NUM_HISTOGRAM_BUCKETS - 1 && dMs >= HISTOGRAM_BUCKET_BOUNDS_MS[bucket])
    {
        bucket++;
    }
    histogramCounts[bucket]++;
}

void TxRxThread::TimingStats::report(bool m_tx, const char* label) const
{
    if (numSamples > 0)
    {
        // localtime() isn't thread-safe (shared static buffer); use the
        // reentrant variant since TX and RX threads can call this concurrently.
        std::tm minTm{};
        std::tm maxTm{};
#if defined(_WIN32)
        localtime_s(&minTm, &minTime);
        localtime_s(&maxTm, &maxTime);
#else
        localtime_r(&minTime, &minTm);
        localtime_r(&maxTime, &maxTm);
#endif // defined(_WIN32)
        char bufMin[32];
        char bufMax[32];
        std::strftime(bufMin, 32, "%H:%M:%S", &minTm);
        std::strftime(bufMax, 32, "%H:%M:%S", &maxTm);

        log_info("m_tx = %d, %s: min = %f ns [%s], max = %f ns [%s], mean = %f ns, stdev = %f ns (n = %d)", m_tx, label, minDuration, bufMin, maxDuration, bufMax, sumDuration / numSamples, sqrt((sumDoubleDuration - pow(sumDuration, 2)/numSamples) / (numSamples - 1)), numSamples);

        // Histogram bucketed by upper bound in ms. Distinguishes "one freak
        // outlier" (a lone hit in a high bucket) from "a real cluster of
        // slow samples" (many hits spread across the middle/high buckets)
        // -- neither is visible in the min/max/mean/stdev line above once
        // averaged over a large n.
        std::stringstream histSs;
        for (int i = 0; i < NUM_HISTOGRAM_BUCKETS; i++)
        {
            if (i > 0) histSs << " ";
            if (i < NUM_HISTOGRAM_BUCKETS - 1)
            {
                histSs << "<" << HISTOGRAM_BUCKET_BOUNDS_MS[i] << "ms:" << histogramCounts[i];
            }
            else
            {
                histSs << ">=" << HISTOGRAM_BUCKET_BOUNDS_MS[i - 1] << "ms:" << histogramCounts[i];
            }
        }
        log_info("m_tx = %d, %s histogram: %s", m_tx, label, histSs.str().c_str());
    }
}
#endif // defined(ENABLE_PROCESSING_STATS)

void TxRxThread::clearFifos_() FREEDV_NONBLOCKING
{
    paCallBackData  *cbData = g_rxUserdata;
    
    if (equalizedMicAudioLink_ != nullptr)
    {
        equalizedMicAudioLink_->clearFifo();
    }
    
    if (m_tx)
    {
        cbData->outfifo1->reset();
        cbData->infifo2->reset();
    }
    else
    {
        cbData->infifo1->reset();

        auto outFifo = (g_nSoundCards.load(std::memory_order_acquire) == 1) ? cbData->outfifo1 : cbData->outfifo2;
        outFifo->reset();
    }
}

//---------------------------------------------------------------------------------------------
// Main real time processing for tx and rx of FreeDV signals, run in its own threads
//---------------------------------------------------------------------------------------------

void TxRxThread::txProcessing_(IRealtimeHelper* helper) FREEDV_NONBLOCKING
{
    paCallBackData  *cbData = g_rxUserdata;

    // Buffers reused by tx and rx processing.  We take samples from
    // the sound card, and resample them for the freedv modem input
    // sample rate.  Typically the sound card is running at 48 or 44.1
    // kHz, and the modem at 8kHz

    //
    //  TX side processing --------------------------------------------
    //

    bool tmpHalfDuplex = g_half_duplex.load(std::memory_order_acquire);
    if (((g_nSoundCards.load(std::memory_order_acquire) == 2) && ((tmpHalfDuplex && g_tx.load(std::memory_order_acquire)) || !tmpHalfDuplex || g_voice_keyer_tx.load(std::memory_order_acquire) || g_recVoiceKeyerFile.load(std::memory_order_relaxed) || g_recFileFromMic.load(std::memory_order_relaxed)))) {        
        if (deferReset_)
        {
            // We just entered TX from RX.
            // Reset pipeline and wipe anything in the FIFO.
            deferReset_ = false;
            pipeline_->reset();
            clearFifos_();
            pendingEooCount_ = 0;
            reachedMinimumTxInput_ = false;

            // return out and begin processing on the next loop
            return;
        }

        // This while loop locks the modulator to the sample rate of
        // the input sound card.  We want to make sure that modulator samples
        // are uninterrupted by differences in sample rate between
        // this sound card and the output sound card.

        // Run code inside this while loop as soon as we have enough
        // room for one frame of modem samples.  Aim is to keep
        // outfifo1 nice and full so we don't have any gaps in tx
        // signal.

        unsigned int nsam_one_modem_frame = (freedvInterface.getTxNNomModemSamples() * outputSampleRate_) / freedvInterface.getTxModemSampleRate();

        if (g_dump_fifo_state.load(std::memory_order_relaxed)) {
             // If this drops to zero we have a problem as we will run out of output samples
             // to send to the sound driver
             FREEDV_BEGIN_VERIFIED_SAFE
                 log_debug("outfifo1 used: %6d free: %6d nsam_one_modem_frame: %d",
                           cbData->outfifo1->numUsed(), cbData->outfifo1->numFree(), nsam_one_modem_frame);
             FREEDV_END_VERIFIED_SAFE
        }

        int nsam_in_48 = (inputSampleRate_ * FRAME_DURATION_MS) / MS_TO_SEC;
        assert(nsam_in_48 > 0);

        int             nout;

        while(!helper->mustStopWork() && (unsigned)cbData->outfifo1->numFree() >= nsam_one_modem_frame) {
            // OK to generate a frame of modem output samples we need
            // an input frame of speech samples from the microphone.

            constexpr int MINIMUM_AUDIO_MS = 200;
            int minimumAudioNumSamples = std::min(MINIMUM_AUDIO_MS * inputSampleRate_ / 1000, cbData->infifo2->capacity() * 90 / 100);
            if (!reachedMinimumTxInput_ && cbData->infifo2->numUsed() < minimumAudioNumSamples) break;
            reachedMinimumTxInput_ = true;

#if defined(ENABLE_PROCESSING_STATS)
            processingStats_.start();
#endif // defined(ENABLE_PROCESSING_STATS)

            // infifo2 is written to by another sound card so it may
            // over or underflow, but we don't really care.  It will
            // just result in a short interruption in audio being fed
            // to codec2_enc, possibly making a click every now and
            // again in the decoded audio at the other end.

            // There may be recorded audio left to encode while ending TX. To handle this,
            // we keep reading from the FIFO until we have less than nsam_in_48 samples available.
            auto inputPtr = inputSamples_.get();
            int nread = cbData->infifo2->read(inputPtr, nsam_in_48);
            if (nread != 0 && endingTx.load(std::memory_order_acquire))
            {
                if (freedvInterface.getCurrentMode() >= FREEDV_MODE_RADE)
                {
                    if (!hasEooBeenSent_)
                    {
                        // Special case for handling RADE EOT
                        freedvInterface.restartTxVocoder();
                        hasEooBeenSent_ = true;
                    }

                    // Only pull a fresh batch of EOO samples out of the pipeline if we don't
                    // already have an unwritten batch left over from a previous callback --
                    // the pipeline hands back (and discards from its own internal queue) the
                    // entire EOO block in one shot, so if we asked again here, any samples that
                    // failed to make it into outfifo1 last time would be lost for good.
                    if (pendingEooCount_ == 0)
                    {
                        auto outputSamples = pipeline_->execute(inputPtr, 0, &nout);
                        if (nout > 0 && outputSamples != nullptr)
                        {
                            assert(nout <= outputSampleRate_);
                            memcpy(pendingEooSamples_.get(), outputSamples, nout * sizeof(short));
                            pendingEooCount_ = nout;
                        }
                        else
                        {
                            // Nothing left buffered upstream and nothing pending here --
                            // the EOO has been fully handed off to outfifo1.
                            g_eoo_enqueued.store(true, std::memory_order_release);
                        }    
                    }

                    if (pendingEooCount_ > 0)
                    {
                        if (cbData->outfifo1->write(pendingEooSamples_.get(), pendingEooCount_) == 0)
                        {
                            pendingEooCount_ = 0;
                            g_eoo_enqueued.store(true, std::memory_order_release);
                        }
                        else
                        {
                            FREEDV_BEGIN_VERIFIED_SAFE
                            log_warn("Could not inject resampled EOO samples (space remaining in FIFO = %d), will retry", cbData->outfifo1->numFree());
                            FREEDV_END_VERIFIED_SAFE
                        }
                    }
                }
                break;
            }
            else
            {
                g_eoo_enqueued.store(false, std::memory_order_release);
                hasEooBeenSent_ = false;
                pendingEooCount_ = 0;
            }

            if (nread != 0)
            {
                break;
            }

            auto outputSamples = pipeline_->execute(inputPtr, nsam_in_48, &nout);
            
            if (g_dump_fifo_state.load(std::memory_order_relaxed)) {
                FREEDV_BEGIN_VERIFIED_SAFE
                log_info("  nout: %d", nout);
                FREEDV_END_VERIFIED_SAFE
            }
            
            if (outputSamples != nullptr)
            {
                if (cbData->outfifo1->write(outputSamples, nout) != 0)
                {
                    FREEDV_BEGIN_VERIFIED_SAFE
                    log_warn("TX outfifo1 full, dropped %d samples (free=%d)", nout, cbData->outfifo1->numFree());
                    FREEDV_END_VERIFIED_SAFE
                }
            }
            
#if defined(ENABLE_PROCESSING_STATS)
            processingStats_.end();
#endif // defined(ENABLE_PROCESSING_STATS)
        }
    }
    else
    {
        // Defer reset until next time we go into TX.
        deferReset_ = true;
    }
}

void TxRxThread::rxProcessing_(IRealtimeHelper* helper) FREEDV_NONBLOCKING
{
    paCallBackData  *cbData = g_rxUserdata;

    // Buffers reused by tx and rx processing.  We take samples from
    // the sound card, and resample them for the freedv modem input
    // sample rate.  Typically the sound card is running at 48 or 44.1
    // kHz, and the modem at 8kHz.

    //
    //  RX side processing --------------------------------------------
    //
    
    if (g_queueResync.load(std::memory_order_acquire))
    {
        g_queueResync.store(false, std::memory_order_release);
        freedvInterface.setSync(FREEDV_SYNC_UNSYNC);
        g_resyncs.fetch_add(1, std::memory_order_relaxed);
    }

    // Make sure we reset 
    if (!g_totBeepActive.load(std::memory_order_acquire))
    {
        beepStep_->reset();
    }
    
    // Attempt to read one processing frame (about 20ms) of receive samples,  we 
    // keep this frame duration constant across modes and sound card sample rates
    int nsam = (inputSampleRate_ * FRAME_DURATION_MS) / MS_TO_SEC;
    assert(nsam > 0);

    int             nout;

    int nsam_one_speech_frame = (freedvInterface.getRxNumSpeechSamples() * outputSampleRate_) / freedvInterface.getRxSpeechSampleRate();
    auto outFifo = (g_nSoundCards.load(std::memory_order_acquire) == 1) ? cbData->outfifo1 : cbData->outfifo2;

    // while we have enough space in the output FIFO ...
    while (!helper->mustStopWork() && outFifo->numFree() >= nsam_one_speech_frame) {
        // ... and enough input samples are available.
        if (cbData->infifo1->read(inputSamples_.get(), nsam) != 0)
        {
            break;
        }

#if defined(ENABLE_PROCESSING_STATS)
        processingStats_.start();
#endif // defined(ENABLE_PROCESSING_STATS)
        
        // send latest squelch level to FreeDV API, as it handles squelch internally
        freedvInterface.setSquelch(g_SquelchActive.load(std::memory_order_acquire), g_SquelchLevel.load(std::memory_order_acquire));

        auto outputSamples = pipeline_->execute(inputSamples_.get(), nsam, &nout);

        if (nout > 0 && outputSamples != nullptr)
        {
            if (outFifo->write(outputSamples, nout) != 0)
            {
                FREEDV_BEGIN_VERIFIED_SAFE
                log_warn("RX outFifo full, dropped %d samples (free=%d)", nout, outFifo->numFree());
                FREEDV_END_VERIFIED_SAFE
            }
        }

#if defined(ENABLE_PROCESSING_STATS)
        processingStats_.end();
#endif // defined(ENABLE_PROCESSING_STATS)
    }
}
