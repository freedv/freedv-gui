//=========================================================================
// Name:            MacAudioDevice.h
// Purpose:         Defines the interface to a Core Audio device.
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

#ifndef MAC_AUDIO_DEVICE_H
#define MAC_AUDIO_DEVICE_H

#include <thread>
#include <chrono>
#include <dispatch/dispatch.h>
#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "../util/ThreadedObject.h"
#include "freedv_sanitizers.h"
#include "IAudioEngine.h"
#include "IAudioDevice.h"

class MacAudioEngine;

class MacAudioDevice : public ThreadedObject, public IAudioDevice
{
public:
    virtual ~MacAudioDevice();
    
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
    friend class MacAudioEngine;
    
    MacAudioDevice(MacAudioEngine* parent, std::string deviceName, int coreAudioId, IAudioEngine::AudioDirection direction, int numChannels, int sampleRate);
    
private:
    // Only used on the device's worker thread (see setCoreAudioId_()).
    int coreAudioId_;

    // The same ID, for the real-time thread: its workgroup follows the
    // device, which can change to the new default device while it runs.
    std::atomic<int> rtCoreAudioId_;
    IAudioEngine::AudioDirection direction_;
    int numChannels_;
    int sampleRate_;
    std::string deviceName_;
    void* observer_;
    short* inputFrames_;
    dispatch_semaphore_t sem_;
    bool isDefaultDevice_;
    MacAudioEngine* parent_;
    AudioComponentInstance auHAL_;
    AudioBufferList* bufferList_;
    // Read by isRunning() on the application's threads while start()/stop()
    // set it on the device's worker thread.
    std::atomic<bool> running_;
    int chosenFrameSize_;
    std::atomic<int> numRealTimeWorkers_;

    // Tracks only how long the *wait itself* overshot its requested duration
    // last cycle (microseconds), separate from processing time -- which
    // stopRealTimeWork() measures directly each cycle against startTime_
    // rather than inferring it from this. See stopRealTimeWork() for why;
    // matches WASAPIAudioDevice's equivalent fields/fix (36db96e5).
    int64_t waitOvershootUs_ = 0;
    std::chrono::time_point<std::chrono::steady_clock> startTime_;

    void stopImpl_();
    void joinWorkgroup_();
    void leaveWorkgroup_();

    // CoreAudio's notifications can't do their own work: they'd have to
    // wait for this device's worker thread while holding CoreAudio's lock.
    // They hand it to a thread instead -- a thread that uses this object, so
    // the object holds on to it and waits for it rather than letting it
    // outlive the device.
    void runNotificationThread_(std::function<void()> work);

    // Changes the device this object uses, on its worker thread, where
    // start() and stop() use it.
    void setCoreAudioId_(int coreAudioId);

    std::mutex notificationMtx_;
    std::vector<std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>> notificationThreads_;
    bool beingDestroyed_ = false;
    
    UInt32 nextPowerOfTwo_(UInt32 val);

    static thread_local void* Workgroup_;
    static thread_local void* JoinToken_;
    static thread_local int CurrentCoreAudioId_;

    static int DeviceIsAliveCallback_(
        AudioObjectID                       inObjectID,
        UInt32                              inNumberAddresses,
        const AudioObjectPropertyAddress    inAddresses[],
        void*                               inClientData);

    static int DeviceOverloadCallback_(
        AudioObjectID                       inObjectID,
        UInt32                              inNumberAddresses,
        const AudioObjectPropertyAddress    inAddresses[],
        void*                               inClientData);
        
    static OSStatus InputProc_(
                void *inRefCon,
                AudioUnitRenderActionFlags *ioActionFlags,
                const AudioTimeStamp *inTimeStamp,
                UInt32 inBusNumber,
                UInt32 inNumberFrames,
                AudioBufferList * ioData) FREEDV_NONBLOCKING;
                
    static OSStatus OutputProc_(
                void *inRefCon,
                AudioUnitRenderActionFlags *ioActionFlags,
                const AudioTimeStamp *inTimeStamp,
                UInt32 inBusNumber,
                UInt32 inNumberFrames,
                AudioBufferList * ioData) FREEDV_NONBLOCKING;
};

#endif // MAC_AUDIO_DEVICE_H
