//=========================================================================
// Name:            WASAPIAudioDevice.cpp
// Purpose:         Defines the interface to a Windows audio device.
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

#include "WASAPIAudioDevice.h"

#include <sstream>
#include <chrono>
#include <thread>
#include <future>
#include <cmath>
#include <avrt.h>
#include <timeapi.h>
#include <inttypes.h>
#include "../util/logging/ulog.h"

#define BLOCK_TIME_NS (10000000)

// Nanoseconds per REFERENCE_TIME unit
#define NS_PER_REFTIME (100)

namespace {
    // NtQueryTimerResolution is an undocumented native API (declared in the
    // WDK's wdm.h, not the Win32 SDK) that reports -- but does not set -- the
    // OS's current timer resolution in 100ns units. Used by stopRealTimeWork()
    // to decide how much margin a millisecond-granular wait needs before
    // switching to a spin loop for the remainder, per
    // https://www.siliceum.com/en/blog/post/windows-high-resolution-timers/.
    // Loaded dynamically since it isn't exported by any import library;
    // ntdll.dll is already mapped into every process, so this can't fail to
    // resolve the module, only (in principle) the export.
    typedef LONG (__stdcall *NtQueryTimerResolutionFn)(PULONG MinimumResolution, PULONG MaximumResolution, PULONG CurrentResolution);

    NtQueryTimerResolutionFn GetNtQueryTimerResolution_()
    {
        static NtQueryTimerResolutionFn fn = reinterpret_cast<NtQueryTimerResolutionFn>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryTimerResolution"));
        return fn;
    }

    // PKEY_Devices_AudioDevice_RawProcessingSupported from propkey.h, defined
    // locally since not every toolchain's headers provide it.
    const PROPERTYKEY PKEY_RawProcessingSupported_ = {
        { 0x8943b373, 0x388c, 0x4395, { 0xb5, 0x57, 0xbc, 0x6d, 0xba, 0xff, 0xaf, 0xdb } }, 2
    };

    // Requesting AUDCLNT_STREAMOPTIONS_RAW on an endpoint that doesn't support
    // raw mode can make IAudioClient::Initialize() fail with E_INVALIDARG, so
    // only request it when the device says it's supported.
    bool IsRawModeSupported_(ComPtr<IMMDevice> device)
    {
        ComPtr<IPropertyStore> propStore = nullptr;
        HRESULT hr = device->OpenPropertyStore(STGM_READ, propStore.GetAddressOf());
        if (FAILED(hr))
        {
            log_warn("Could not open device property store to check for raw mode support (hr = %ld)", hr);
            return false;
        }

        PROPVARIANT value;
        PropVariantInit(&value);
        hr = propStore->GetValue(PKEY_RawProcessingSupported_, &value);
        bool supported = SUCCEEDED(hr) && value.vt == VT_BOOL && value.boolVal == VARIANT_TRUE;
        PropVariantClear(&value);
        return supported;
    }
}

thread_local HANDLE WASAPIAudioDevice::HelperTask_ = nullptr;
    
WASAPIAudioDevice::WASAPIAudioDevice(ComPtr<IAudioClient3> client, ComPtr<IMMDevice> device, IAudioEngine::AudioDirection direction, int sampleRate, int numChannels)
    : Win32COMObject("WASAPIDev")
    , client_(client)
    , device_(device)
    , renderClient_(nullptr)
    , captureClient_(nullptr)
    , direction_(direction)
    , sampleRate_(sampleRate)
    , numChannels_(numChannels)
    , bufferFrameCount_(0)
    , initialized_(false)
    , latencyFrames_(0)
    , renderCaptureEvent_(nullptr)
    , isRenderCaptureRunning_(false)
    , semaphore_(nullptr)
    , highResTimer_(nullptr)
    , timePeriodSet_(false)
    , tmpBuf_(nullptr)
    , waitOvershootHns_(0)
{
    // empty
}

WASAPIAudioDevice::~WASAPIAudioDevice()
{
    stop();

    // Release IAudioClient here as it can normally be
    // reused after stop().
    auto prom = std::make_shared<std::promise<void> >(); 
    auto fut = prom->get_future();
    enqueue_([&]() {
        renderClient_ = nullptr;
        captureClient_ = nullptr;
        client_ = nullptr;
        device_ = nullptr;

        if (semaphore_ != nullptr)
        {
            CloseHandle(semaphore_);
            semaphore_ = nullptr;
        }

        if (highResTimer_ != nullptr)
        {
            CloseHandle(highResTimer_);
            highResTimer_ = nullptr;
        }

        prom->set_value();
    });
    fut.wait(); 
}

int WASAPIAudioDevice::getNumChannels() FREEDV_NONBLOCKING
{
    return numChannels_;
}

int WASAPIAudioDevice::getSampleRate() const FREEDV_NONBLOCKING
{
    return sampleRate_;
}
 
void WASAPIAudioDevice::start() 
{
    log_info("Starting device with direction %d, sample rate %d, num channels %d", direction_, sampleRate_, numChannels_);

    auto prom = std::make_shared<std::promise<void> >(); 
    auto fut = prom->get_future();
    enqueue_([&]() {
        WAVEFORMATEX* streamFormatPtr = nullptr;
        WAVEFORMATEX streamFormat;
        bool freeStreamFormat = false;

        // Set AudioClientProperties for stream. Must be done prior to Initialize().
        AudioClientProperties prop;
        prop.cbSize = sizeof(AudioClientProperties);
        prop.bIsOffload = FALSE;
        prop.eCategory = AudioCategory_Other;
        if (IsRawModeSupported_(device_))
        {
            prop.Options = AUDCLNT_STREAMOPTIONS_RAW;
        }
        else
        {
            log_info("Device does not support raw mode, using default audio processing");
            prop.Options = AUDCLNT_STREAMOPTIONS_NONE;
        }
        HRESULT hr = client_->SetClientProperties(&prop);
        if (FAILED(hr))
        {
            // Non-critical error, can continue without setting properties.
            std::stringstream ss;
            ss << "Could not set AudioClient properties (hr = " << hr << ")";
            log_warn("%s", ss.str().c_str());
        }
        
        // Populate stream format based on requested sample
        // rate/number of channels.
        // NOTE: this should already have been determined valid
        // by the audio engine!
        hr = client_->GetMixFormat(&streamFormatPtr);
        if (SUCCEEDED(hr))
        {
            freeStreamFormat = true;
            streamFormatPtr->nChannels = numChannels_;
            streamFormatPtr->nSamplesPerSec = sampleRate_;
            streamFormatPtr->nBlockAlign = (numChannels_ * streamFormatPtr->wBitsPerSample) / 8;
            streamFormatPtr->nAvgBytesPerSec = streamFormatPtr->nSamplesPerSec * streamFormatPtr->nBlockAlign;
        }
        else
        {
            streamFormatPtr = &streamFormat;
            streamFormat.wFormatTag = WAVE_FORMAT_PCM;
            streamFormat.wBitsPerSample = 16;
            streamFormat.nChannels = numChannels_;
            streamFormat.nSamplesPerSec = sampleRate_;
            streamFormat.nBlockAlign = (numChannels_ * streamFormat.wBitsPerSample) / 8;
            streamFormat.nAvgBytesPerSec = streamFormat.nSamplesPerSec * streamFormat.nBlockAlign;
            streamFormat.cbSize = 0;
        }

        // Set up for conversion to mix format
        if (streamFormatPtr->wFormatTag == WAVE_FORMAT_PCM)
        {
            containerBits_ = streamFormatPtr->wBitsPerSample;
            validBits_ = streamFormatPtr->wBitsPerSample;
            isFloatingPoint_ = false;

            log_info("Mix format is integer (container bits: %d, valid bits: %d)", containerBits_, validBits_);
        }
        else if (streamFormatPtr->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        {
            containerBits_ = streamFormatPtr->wBitsPerSample;
            validBits_ = streamFormatPtr->wBitsPerSample;
            isFloatingPoint_ = true;
            log_info("Mix format is floating point (container bits: %d, valid bits: %d)", containerBits_, validBits_);
        }
        else if (streamFormatPtr->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            WAVEFORMATEXTENSIBLE* extFormat = (WAVEFORMATEXTENSIBLE*)streamFormatPtr;
            containerBits_ = streamFormatPtr->wBitsPerSample;
            validBits_ = extFormat->Samples.wValidBitsPerSample;
            if (extFormat->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
            {
                isFloatingPoint_ = false;
                log_info("Mix format is integer (container bits: %d, valid bits: %d)", containerBits_, validBits_);
            }
            else if (extFormat->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
            {
                isFloatingPoint_ = true;
                log_info("Mix format is floating point (container bits: %d, valid bits: %d)", containerBits_, validBits_);
            }
            else
            {
                std::stringstream ss;
                ss << "Unknown mix format found: " << GuidToString_(&extFormat->SubFormat);
                log_error("%s", ss.str().c_str());
                if (onAudioErrorFunction)
                {
                    onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
                }
                if (freeStreamFormat)
                {
                    CoTaskMemFree(streamFormatPtr);
                }
                prom->set_value();
                return;
            }
        }
        else
        {
            std::stringstream ss;
            ss << "Unknown mix format found: " << streamFormatPtr->wFormatTag;
            log_error("%s", ss.str().c_str());
            if (onAudioErrorFunction)
            {
                onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
            }
            if (freeStreamFormat)
            {
                CoTaskMemFree(streamFormatPtr);
            }
            prom->set_value();
            return;
        }

        if (!initialized_)
        {
            REFERENCE_TIME desiredRefTime = BLOCK_TIME_NS / NS_PER_REFTIME; // REFERENCE_TIME is in 100ns units
            hr = client_->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK | 
                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                desiredRefTime,
                0,
                streamFormatPtr,
                nullptr);

            if (freeStreamFormat)
            {
                CoTaskMemFree(streamFormatPtr);
            }
            if (FAILED(hr))
            {
                std::stringstream ss;
                ss << "Could not initialize AudioClient (hr = " << hr << ")";
                log_error("%s", ss.str().c_str());
                if (onAudioErrorFunction)
                {
                    onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
                }
                prom->set_value();
                return;
            }
            initialized_ = true;
        }
        else if (freeStreamFormat)
        {
            CoTaskMemFree(streamFormatPtr);
        }

        // Create render/capture event
        renderCaptureEvent_ = CreateEvent(nullptr, false, false, nullptr);
        if (renderCaptureEvent_ == nullptr)
        {
            std::stringstream ss;
            ss << "Could not create event (hr = " << GetLastError() << ")";
            log_error("%s", ss.str().c_str());
            if (onAudioErrorFunction)
            {
                onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
            }
            prom->set_value();
            return;
        }

        // Assign render/capture event
        hr = client_->SetEventHandle(renderCaptureEvent_);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "Could not assign event handle (hr = " << hr << ")";
            log_error("%s", ss.str().c_str());
            if (onAudioErrorFunction)
            {
                onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
            }
            CloseHandle(renderCaptureEvent_);
            prom->set_value();
            return;
        }

        // Get actual allocated buffer size
        hr = client_->GetBufferSize(&bufferFrameCount_);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "Could not get buffer size (hr = " << hr << ")";
            log_error("%s", ss.str().c_str());
            if (onAudioErrorFunction)
            {
                onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
            }
            prom->set_value();
            return;
        }
        log_info("Allocated %d frames for audio buffers", bufferFrameCount_);
        
        // Get latency
        latencyFrames_ = bufferFrameCount_;
                
        REFERENCE_TIME latency = 0;
        hr = client_->GetStreamLatency(&latency);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "Could not get latency (hr = " << hr << ")";
            log_warn("%s", ss.str().c_str());
        }
        else
        {
            latencyFrames_ += sampleRate_ * ((double)(NS_PER_REFTIME * latency) / 1e9);
        }

        // Get capture/render client
        if (direction_ == IAudioEngine::AUDIO_ENGINE_IN)
        {
            hr = client_->GetService(
                IID_IAudioCaptureClient,
                (void**)captureClient_.GetAddressOf());
        }
        else
        {
            hr = client_->GetService(
                IID_IAudioRenderClient,
                (void**)renderClient_.GetAddressOf());
        }
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "Could not get render/capture client (hr = " << hr << ")";
            log_error("%s", ss.str().c_str());
            if (onAudioErrorFunction)
            {
                onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
            }
            prom->set_value();
            return;
        }

        // Allocate temporary buffer
        tmpBuf_ = new short[sampleRate_];
        assert(tmpBuf_ != nullptr);
        memset(tmpBuf_, 0, sizeof(short) * sampleRate_);

        if (direction_ == IAudioEngine::AUDIO_ENGINE_OUT)
        {
            // Perform initial population of audio buffer
            BYTE* data = nullptr;
            hr = renderClient_->GetBuffer(bufferFrameCount_, &data);
            if (FAILED(hr))
            {
                std::stringstream ss;
                ss << "Could not get render buffer (hr = " << hr << ")";
                log_error("%s", ss.str().c_str());
                if (onAudioErrorFunction)
                {
                    onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
                }
                renderClient_ = nullptr;

                delete[] tmpBuf_;
                tmpBuf_ = nullptr;

                prom->set_value();
                return;
            }

            if (onAudioDataFunction)
            {
                onAudioDataFunction(*this, tmpBuf_, bufferFrameCount_, onAudioDataState);
            }

            copyToWindowsBuffer_(data, bufferFrameCount_);

            hr = renderClient_->ReleaseBuffer(bufferFrameCount_, 0);
            if (FAILED(hr))
            {
                std::stringstream ss;
                ss << "Could not release render buffer (hr = " << hr << ")";
                log_error("%s", ss.str().c_str());
                if (onAudioErrorFunction)
                {
                    onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
                }
                renderClient_ = nullptr;

                delete[] tmpBuf_;
                tmpBuf_ = nullptr;

                prom->set_value();
                return;
            }
        }

        // Create semaphore, or clear the wakeup left in it by a previous stop().
        if (semaphore_ != nullptr)
        {
            WaitForSingleObject(semaphore_, 0);
        }
        else
        {
            semaphore_ = CreateSemaphore(nullptr, 0, 1, nullptr);
        }
        if (semaphore_ == nullptr)
        {
            std::stringstream ss;
            ss << "Could not create semaphore (err = " << GetLastError() << ")";
            log_warn("%s", ss.str().c_str());
        }

        // Create a high-resolution waitable timer for the debt-compensated
        // wait in stopRealTimeWork(), so that wait isn't limited to
        // WaitForSingleObject's millisecond-granular timeout. The high-res
        // flag requires Windows 10 1803+; fall back to a regular (still
        // usable, just lower-resolution) waitable timer if unavailable.
        if (highResTimer_ == nullptr)
        {
            highResTimer_ = CreateWaitableTimerEx(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        }
        if (highResTimer_ == nullptr)
        {
            highResTimer_ = CreateWaitableTimerEx(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
        if (highResTimer_ == nullptr)
        {
            std::stringstream ss;
            ss << "Could not create waitable timer (err = " << GetLastError() << ")";
            log_warn("%s", ss.str().c_str());
        }

        // Reduce Windows timer resolution to 1ms to improve WaitForSingleObject
        // precision in the audio loop.
        if (!timePeriodSet_)
        {
            timeBeginPeriod(1);
            timePeriodSet_ = true;
        }

        // Start render/capture
        hr = client_->Start();
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "Could not start audio device (hr = " << hr << ")";
            log_error("%s", ss.str().c_str());
            if (onAudioErrorFunction)
            {
                onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
            }
            renderClient_ = nullptr;
            captureClient_ = nullptr;

            delete[] tmpBuf_;
            tmpBuf_ = nullptr;

            prom->set_value();
            return;
        }

        // Start render/capture thread.
        isRenderCaptureRunning_ = true;
        renderCaptureThread_ = std::thread([this]() {
            log_info("Starting render/capture thread");
            
            // Capture references for use by this thread.
            ComPtr<IAudioRenderClient> renderClientRef = renderClient_;
            ComPtr<IAudioCaptureClient> captureClientRef = captureClient_;
            ComPtr<IAudioClient3> clientRef = client_;

            HRESULT res = CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
            if (FAILED(res))
            {
                log_warn("Could not initialize COM (res = %d)", res);
            }
            
            // Temporarily raise priority of task
            setHelperRealTime();

            // The render/capture event fires once per buffer period. If it
            // fails to fire within 2 periods, the device is stalled: log the
            // stall (once per episode) instead of silently underflowing, and
            // poll at the faster cadence so we resume as soon as the driver
            // recovers. Clamp to [25, 100] ms so devices with unusually large
            // or small buffers still get a sane timeout.
            UINT32 eventTimeoutMs = (2000 * bufferFrameCount_) / sampleRate_;
            if (eventTimeoutMs < 25) eventTimeoutMs = 25;
            if (eventTimeoutMs > 100) eventTimeoutMs = 100;
            UINT32 consecutiveTimeouts = 0;

            while (isRenderCaptureRunning_)
            {
                DWORD waitResult = WaitForSingleObject(renderCaptureEvent_, eventTimeoutMs);
                if (waitResult == WAIT_TIMEOUT)
                {
                    if (++consecutiveTimeouts == 1)
                    {
                        log_warn("WASAPI render/capture event did not fire within %d ms (device may be stalled)", eventTimeoutMs);
                    }
                }
                else if (consecutiveTimeouts > 0)
                {
                    log_warn("WASAPI render/capture event resumed after %d timeout(s)", consecutiveTimeouts);
                    consecutiveTimeouts = 0;
                }
                if (isRenderCaptureRunning_)
                {
                    if (direction_ == IAudioEngine::AUDIO_ENGINE_OUT)
                    {
                        renderAudio_(renderClientRef);
                    }
                    else
                    {
                        captureAudio_(captureClientRef);
                    }
                }
            }

            log_info("Exiting render/capture thread");

            clearHelperRealTime();                  
            CoUninitialize();
        });

        prom->set_value();
    });

    fut.wait();
}

void WASAPIAudioDevice::stop()
{
    log_info("Stopping device with direction %d, sample rate %d, num channels %d", direction_, sampleRate_, numChannels_);

    auto prom = std::make_shared<std::promise<void> >(); 
    auto fut = prom->get_future();
    enqueue_([&]() {
        isRenderCaptureRunning_ = false;
        if (renderCaptureThread_.joinable())
        {
            renderCaptureThread_.join();
        }
        
        if (renderClient_ || captureClient_)
        {
            HRESULT hr = client_->Stop();
            if (FAILED(hr))
            {
                std::stringstream ss;
                ss << "Could not stop audio device (hr = " << hr << ")";
                log_error("%s", ss.str().c_str());
                if (onAudioErrorFunction)
                {
                    onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
                }
            }
        }
        
        renderClient_ = nullptr;
        captureClient_ = nullptr;

        // stop() also runs from the destructor, so only undo start()'s once.
        if (timePeriodSet_)
        {
            timeEndPeriod(1);
            timePeriodSet_ = false;
        }

        if (renderCaptureEvent_ != nullptr)
        {
            CloseHandle(renderCaptureEvent_);
            renderCaptureEvent_ = nullptr;
        }
        
        // Wake anyone still waiting in stopRealTimeWork(). The handles stay
        // open until the destructor: closing them here could pull them out
        // from under a waiter (and the handle values could be reused).
        if (semaphore_ != nullptr)
        {
            ReleaseSemaphore(semaphore_, 1, nullptr);
        }

        if (highResTimer_ != nullptr)
        {
            LARGE_INTEGER dueTime;
            dueTime.QuadPart = 0;
            SetWaitableTimer(highResTimer_, &dueTime, 0, nullptr, nullptr, FALSE);
        }

        if (tmpBuf_ != nullptr)
        {
            delete[] tmpBuf_;
            tmpBuf_ = nullptr;
        }

        prom->set_value();
    });
    fut.wait();
}

bool WASAPIAudioDevice::isRunning()
{
    return (renderClient_) || (captureClient_);
}

int64_t WASAPIAudioDevice::getLatencyInMicroseconds()
{
    // Note: latencyFrames_ isn't expected to change, so we don't need to 
    // wrap this call in an enqueue_() like with the other public methods.
    return (int64_t)1000000 * (int64_t)latencyFrames_ / sampleRate_;
}

void WASAPIAudioDevice::setHelperRealTime()
{
    DWORD taskIndex = 0;
    HelperTask_ = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
    if (HelperTask_ == nullptr)
    {
        log_warn("Could not increase thread priority");
        return;
    }

    // AvSetMmThreadCharacteristics() alone only enrolls the thread in the
    // "Pro Audio" MMCSS class at that class's default (Normal) priority
    // band. This thread's wait in stopRealTimeWork() is on the critical
    // path for audio timing (it's what TxRxThread's wait/TX/RX stats
    // measure), so bump it to the top of the band to cut down on how long
    // it sits ready-but-not-running behind other MMCSS-scheduled threads
    // after the semaphore/timer wakes it -- that scheduling delay is what
    // shows up as wait jitter (stdev/max) rather than the wait target
    // itself being wrong.
    if (!AvSetMmThreadPriority(HelperTask_, AVRT_PRIORITY_CRITICAL))
    {
        log_warn("Could not raise MMCSS thread priority to critical (err = %lu)", GetLastError());
    }
}

void WASAPIAudioDevice::startRealTimeWork() 
{
    startTime_ = std::chrono::steady_clock::now();
}

void WASAPIAudioDevice::stopRealTimeWork(bool fastMode)
{
    if (semaphore_ == nullptr || highResTimer_ == nullptr)
    {
        // Fallback to base class behavior
        IAudioDevice::stopRealTimeWork();
        return;
    }

    // Nominal period in 100ns units -- matches what SetWaitableTimer expects,
    // and lets the compensation below track sub-millisecond amounts instead
    // of being rounded down to whole milliseconds.
    int64_t nominalHns = ((10000000LL * bufferFrameCount_) / sampleRate_) >> (fastMode ? 1 : 0);

    // Compensate for how much of the period THIS cycle's own processing
    // already used, measured directly against startTime_ (set by
    // startRealTimeWork() right before processing began) rather than against
    // a debt figure copied from the *previous* cycle. The previous approach
    // lagged by one cycle: it corrected this wait for last cycle's overrun
    // instead of this cycle's own, which overcorrects/undercorrects whenever
    // processing time varies cycle to cycle instead of holding steady.
    // waitOvershootHns_ separately tracks only the wait itself running long
    // (the one thing that genuinely can't be known until after it happens),
    // so a systematic scheduling overshoot still can't accumulate into drift.
    auto elapsedHns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - startTime_).count() / 100;
    int64_t hns = nominalHns - elapsedHns - waitOvershootHns_;
    if (hns <= 0)
    {
        waitOvershootHns_ = 0;
        return;
    }

    auto waitStartTime = std::chrono::steady_clock::now();

    // Sleep for everything except the last tick of the OS's current timer
    // resolution, then spin-wait the remainder in userspace. A blocking wait
    // (WaitForSingleObject/WaitForMultipleObjects) can only wake up on a
    // scheduler tick, and once woken still has to wait to actually be
    // scheduled -- both add jitter on top of the wait target itself. Spinning
    // through the last tick avoids that second trip through the scheduler.
    // We still poll semaphore_ throughout (both during the sleep and the
    // spin) so a stop() request still aborts the wait immediately rather than
    // running it to completion. See
    // https://www.siliceum.com/en/blog/post/windows-high-resolution-timers/.
    ULONG minResHns = 0, maxResHns = 0, curResHns = 0;
    auto ntQueryTimerResolution = GetNtQueryTimerResolution_();
    bool signaledEarly = false;

    if (ntQueryTimerResolution != nullptr &&
        ntQueryTimerResolution(&minResHns, &maxResHns, &curResHns) >= 0 &&
        curResHns > 0)
    {
        int64_t sleepHns = hns - (int64_t)curResHns;
        if (sleepHns > 0)
        {
            DWORD sleepMs = (DWORD)(sleepHns / 10000); // 100ns units -> ms, rounded down
            if (sleepMs > 0 && WaitForSingleObject(semaphore_, sleepMs) == WAIT_OBJECT_0)
            {
                signaledEarly = true;
            }
        }

        if (!signaledEarly)
        {
            auto dueTime = waitStartTime + std::chrono::nanoseconds(hns * 100);
            while (std::chrono::steady_clock::now() < dueTime)
            {
                if (WaitForSingleObject(semaphore_, 0) == WAIT_OBJECT_0)
                {
                    signaledEarly = true;
                    break;
                }
                YieldProcessor();
            }
        }
    }
    else
    {
        // NtQueryTimerResolution unavailable for some reason -- fall back to
        // the high-resolution waitable timer instead of a spin loop with no
        // idea how fine the OS's scheduling granularity actually is.
        LARGE_INTEGER dueTime;
        dueTime.QuadPart = -hns;
        SetWaitableTimer(highResTimer_, &dueTime, 0, nullptr, nullptr, FALSE);

        HANDLE waitHandles[2] = { semaphore_, highResTimer_ };
        DWORD result = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        signaledEarly = (result == WAIT_OBJECT_0);

        if (result != WAIT_OBJECT_0 && result != WAIT_OBJECT_0 + 1)
        {
            // Fallback to a simple sleep.
            IAudioDevice::stopRealTimeWork(fastMode);
            return;
        }
    }

    auto actualWaitHns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - waitStartTime).count() / 100;
    waitOvershootHns_ = std::max((int64_t)0, actualWaitHns - hns); // cap at >= 0; an early (semaphore) wake isn't overshoot.
}

void WASAPIAudioDevice::clearHelperRealTime()
{
    if (HelperTask_ != nullptr)
    {
        AvRevertMmThreadCharacteristics(HelperTask_);
        HelperTask_ = nullptr;
    }
}

void WASAPIAudioDevice::renderAudio_(ComPtr<IAudioRenderClient> renderClient)
{
    // If client is no longer available, abort
    if (!renderClient)
    {
        return;
    }

    // Get available buffer space
    UINT32 padding = 0;
    UINT32 framesAvailable = 0;
    BYTE* data = nullptr;
    HRESULT hr = client_->GetCurrentPadding(&padding);
    if (FAILED(hr))
    {
        // Note: don't call to event handler to avoid annoying the user
        // with popups.
        std::stringstream ss;
        ss << "Could not get current padding (hr = " << hr << ")";
        log_error("%s", ss.str().c_str());
        return;
    }

    framesAvailable = bufferFrameCount_ - padding;
    hr = renderClient->GetBuffer(framesAvailable, &data);
    if (FAILED(hr))
    {
        // Note: don't call to event handler to avoid annoying the user
        // with popups.
        std::stringstream ss;
        ss << "Could not get render buffer (hr = " << hr << ")";
        log_error("%s", ss.str().c_str());
        return;
    }

    // Grab audio data from higher level code
    if (framesAvailable > 0 && data != nullptr)
    {
        memset(tmpBuf_, 0, framesAvailable * numChannels_ * sizeof(short));
        if (onAudioDataFunction)
        {
            onAudioDataFunction(*this, tmpBuf_, framesAvailable, onAudioDataState);
        }
        copyToWindowsBuffer_(data, framesAvailable);
    }

    // Release render buffer
    hr = renderClient->ReleaseBuffer(framesAvailable, 0);
    if (FAILED(hr))
    {
        // Note: don't call to event handler to avoid annoying the user
        // with popups.
        std::stringstream ss;
        ss << "Could not release render buffer (hr = " << hr << ")";
        log_error("%s", ss.str().c_str());
        return;
    }
}

void WASAPIAudioDevice::captureAudio_(ComPtr<IAudioCaptureClient> captureClient)
{
    // If client is no longer available, abort
    if (!captureClient)
    {
        return;
    }

    // Get packet length
    UINT32 packetLength = 0;
    HRESULT hr = captureClient->GetNextPacketSize(&packetLength);
    if (FAILED(hr))
    {
        // Note: don't call to event handler to avoid annoying the user
        // with popups.
        std::stringstream ss;
        ss << "Could not get packet length (hr = " << hr << ")";
        log_error("%s", ss.str().c_str());
        return;
    }

    while(packetLength != 0)
    {
        BYTE* data = nullptr;
        UINT32 numFramesAvailable = 0;
        DWORD flags = 0;

        hr = captureClient->GetBuffer(
            &data,
            &numFramesAvailable,
            &flags,
            nullptr,
            nullptr);
        if (FAILED(hr))
        {
            // Note: don't call to event handler to avoid annoying the user
            // with popups.
            std::stringstream ss;
            ss << "Could not get capture buffer (hr = " << hr << ")";
            log_error("%s", ss.str().c_str());
            return;
        }

        if (numFramesAvailable > 0 && data != nullptr)
        {
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
            {
                memset(tmpBuf_, 0, numFramesAvailable * numChannels_ * sizeof(short));
            }
            else
            {
                copyFromWindowsBuffer_(data, numFramesAvailable);
            }

            if (onAudioDataFunction)
            {
                onAudioDataFunction(*this, tmpBuf_, numFramesAvailable, onAudioDataState);
            }
        }

        // Release buffer
        hr = captureClient->ReleaseBuffer(numFramesAvailable);
        if (FAILED(hr))
        {
            // Note: don't call to event handler to avoid annoying the user
            // with popups.
            std::stringstream ss;
            ss << "Could not release capture buffer (hr = " << hr << ")";
            log_error("%s", ss.str().c_str());
            return;
        }

        hr = captureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr))
        {
            // Note: don't call to event handler to avoid annoying the user
            // with popups.
            std::stringstream ss;
            ss << "Could not get packet length (hr = " << hr << ")";
            log_error("%s", ss.str().c_str());
            return;
        }
    }

    if (semaphore_ != nullptr)
    {
        // Notify worker threads
        ReleaseSemaphore(semaphore_, 1, nullptr);
    }
}

void WASAPIAudioDevice::copyFromWindowsBuffer_(void* buf, int numFrames)
{
    if (isFloatingPoint_)
    {
        if (validBits_ == sizeof(float) * 8)
        {
            copyFloatToShort_<float>((float*)buf, numFrames);
        }
        else if (validBits_ == sizeof(double) * 8)
        {
            copyFloatToShort_<double>((double*)buf, numFrames);
        }
    }
    else
    {
        if (containerBits_ == 8)
        {
            copyIntToShort_<char>((char*)buf, numFrames);
        }
        else if (containerBits_ == 16 && validBits_ == 16)
        {
            // Shortcut -- can just memcpy into tmpBuf_.
            memcpy(tmpBuf_, buf, numFrames * numChannels_ * sizeof(short));
        }
        else if (containerBits_ == 16)
        {
            copyIntToShort_<short>((short*)buf, numFrames);
        }
        else if (containerBits_ == 32)
        {
            copyIntToShort_<int32_t>((int32_t*)buf, numFrames);
        }
    }
}

void WASAPIAudioDevice::copyToWindowsBuffer_(void* buf, int numFrames)
{
    if (isFloatingPoint_)
    {
        if (validBits_ == sizeof(float) * 8)
        {
            copyShortToFloat_<float>((float*)buf, numFrames);
        }
        else if (validBits_ == sizeof(double) * 8)
        {
            copyShortToFloat_<double>((double*)buf, numFrames);
        }
    }
    else
    {
        if (containerBits_ == 8)
        {
            copyShortToInt_<char>((char*)buf, numFrames);
        }
        else if (containerBits_ == 16 && validBits_ == 16)
        {
            // Shortcut -- can just memcpy from tmpBuf_.
            memcpy(buf, tmpBuf_, numFrames * numChannels_ * sizeof(short));
        }
        else if (containerBits_ == 16)
        {
            copyShortToInt_<short>((short*)buf, numFrames);
        }
        else if (containerBits_ == 32)
        {
            copyShortToInt_<int32_t>((int32_t*)buf, numFrames);
        }
    }
}

std::string WASAPIAudioDevice::GuidToString_(GUID *guid)
{
    char guid_string[37]; // 32 hex chars + 4 hyphens + null terminator
    snprintf(
          guid_string, sizeof(guid_string),
          "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
          (unsigned int)guid->Data1, (unsigned int)guid->Data2, (unsigned int)guid->Data3,
          guid->Data4[0], guid->Data4[1], guid->Data4[2],
          guid->Data4[3], guid->Data4[4], guid->Data4[5],
          guid->Data4[6], guid->Data4[7]);
    return guid_string;
}
