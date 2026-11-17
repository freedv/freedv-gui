//=========================================================================
// Name:            WASAPIAudioDevice.h
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

#ifndef WASAPI_AUDIO_DEVICE_H
#define WASAPI_AUDIO_DEVICE_H

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <thread>
#include <initguid.h>
#include <atomic>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include "IAudioEngine.h"
#include "IAudioDevice.h"
#include "../util/Win32COMObject.h"
#include "../util/Win32COMPointer.h"

class WASAPIAudioDevice : public Win32COMObject, public IAudioDevice
{
public:
    virtual ~WASAPIAudioDevice();

    virtual int getNumChannels() FREEDV_NONBLOCKING override;
    virtual int getSampleRate() const FREEDV_NONBLOCKING override;
    
    virtual void start() override;
    virtual void stop() override;

    virtual bool isRunning() override;

    virtual int64_t getLatencyInMicroseconds() override;

    // Configures current thread for real-time priority. This should be
    // called from the thread that will be operating on received audio.
    virtual void setHelperRealTime() override;

    // Lets audio system know that we're beginning to do work with the
    // received audio.
    virtual void startRealTimeWork() override;

    // Lets audio system know that we're done with the work on the received
    // audio.
    virtual void stopRealTimeWork(bool fastMode = false) override;

    // Reverts real-time priority for current thread.
    virtual void clearHelperRealTime() override;
    
protected:
    friend class WASAPIAudioEngine;

    WASAPIAudioDevice(ComPtr<IAudioClient3> client, ComPtr<IMMDevice> device, IAudioEngine::AudioDirection direction, int sampleRate, int numChannels);

private:
    ComPtr<IAudioClient3> client_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioRenderClient> renderClient_;
    ComPtr<IAudioCaptureClient> captureClient_;
    IAudioEngine::AudioDirection direction_;
    int sampleRate_;
    int numChannels_;
    UINT32 bufferFrameCount_;
    bool initialized_;
    int latencyFrames_;
    std::thread renderCaptureThread_;
    HANDLE renderCaptureEvent_;
    // Set by start()/stop() and tested by the render/capture thread on
    // every pass, so it has to be one the two threads agree on.
    std::atomic<bool> isRenderCaptureRunning_;
    // Created on first start() and only closed in the destructor, so the
    // processing thread never sees a handle being closed under it.
    HANDLE semaphore_;
    HANDLE highResTimer_;
    bool timePeriodSet_;

    // For mix<->int16 conversions
    int containerBits_;
    int validBits_;
    bool isFloatingPoint_;
    short* tmpBuf_;

    // Tracks only how long the *wait itself* overshot its requested duration
    // last cycle (100ns units), separate from processing time -- which
    // stopRealTimeWork() measures directly each cycle against startTime_
    // rather than inferring it from this. See stopRealTimeWork() for why.
    int64_t waitOvershootHns_;
    std::chrono::time_point<std::chrono::steady_clock> startTime_;

    void renderAudio_(ComPtr<IAudioRenderClient> renderClient);
    void captureAudio_(ComPtr<IAudioCaptureClient> captureClient);
    void copyFromWindowsBuffer_(void* buf, int numFrames);
    void copyToWindowsBuffer_(void* buf, int numFrames);

    template<typename T>
    void copyFloatToShort_(T* source, int numFrames);
    
    template<typename T>
    void copyShortToFloat_(T* dest, int numFrames);
    
    template<typename T>
    void copyIntToShort_(T* source, int numFrames);

    template<typename T>
    void copyShortToInt_(T* dest, int numFrames);

    static std::string GuidToString_(GUID* guid);
    static thread_local HANDLE HelperTask_;
};

template<typename T>
void WASAPIAudioDevice::copyFloatToShort_(T* source, int numFrames)
{
    for (int index = 0; index < numFrames; index++)
    {
        for (int innerIndex = 0; innerIndex < numChannels_; innerIndex++)
        {
            tmpBuf_[index * numChannels_ + innerIndex] = source[index * numChannels_ + innerIndex] * (std::numeric_limits<short>::max());
        }
    }
}

template<typename T>
void WASAPIAudioDevice::copyShortToFloat_(T* dest, int numFrames)
{
    for (int index = 0; index < numFrames; index++)
    {
        for (int innerIndex = 0; innerIndex < numChannels_; innerIndex++)
        {
            dest[index * numChannels_ + innerIndex] = (T)tmpBuf_[index * numChannels_ + innerIndex] / (std::numeric_limits<short>::max());
        }
    }
}

template<typename T>
void WASAPIAudioDevice::copyIntToShort_(T* source, int numFrames)
{
    for (int index = 0; index < numFrames; index++)
    {
        for (int innerIndex = 0; innerIndex < numChannels_; innerIndex++)
        {
            T temp = source[index * numChannels_ + innerIndex] >> (containerBits_ - validBits_);
            tmpBuf_[index * numChannels_ + innerIndex] = temp >> (validBits_ >= 16 ? validBits_ - 16 : 0);
        }
    }
}

template<typename T>
void WASAPIAudioDevice::copyShortToInt_(T* dest, int numFrames)
{
    for (int index = 0; index < numFrames; index++)
    {
        for (int innerIndex = 0; innerIndex < numChannels_; innerIndex++)
        {
            T temp = tmpBuf_[index * numChannels_ + innerIndex] << (validBits_ >= 16 ? validBits_ - 16 : 0);
            dest[index * numChannels_ + innerIndex] = temp << (containerBits_ - validBits_);
        }
    }
}

#endif // WASAPI_AUDIO_DEVICE_H
