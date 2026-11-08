//=========================================================================
// Name:            MacAudioDevice.mm
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

#include "MacAudioDevice.h"
#include "MacAudioEngine.h"
#include "../util/logging/ulog.h"

#include <future>
#include <sstream>
#include <chrono>
#include <thread>

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/mach_init.h>
#include <mach/thread_policy.h>
#include <sched.h>
#include <pthread.h>

using namespace std::placeholders;
using namespace std::chrono_literals;

thread_local void* MacAudioDevice::Workgroup_ = nullptr;
thread_local void* MacAudioDevice::JoinToken_ = nullptr;
thread_local int MacAudioDevice::CurrentCoreAudioId_ = 0;

// Conversion factors.
constexpr static int MS_TO_SEC = 1000;

// The I/O interval time in seconds.
constexpr static int AUDIO_SAMPLE_BLOCK_MSEC = 20;
constexpr static int AUDIO_SAMPLE_BLOCK_WIRELESS_MSEC = 40;

static OSStatus GetIsWirelessDevice(AudioObjectID inDeviceID, bool *isWireless)
{
    AudioObjectPropertyAddress theAddress = { kAudioDevicePropertyTransportType,
                                              kAudioObjectPropertyScopeGlobal,
                                              kAudioObjectPropertyElementMaster };

    UInt32 transportType = 0;
    UInt32 theDataSize = sizeof(transportType);
    OSStatus theError = AudioObjectGetPropertyData(inDeviceID,
                                                   &theAddress,
                                                   0,
                                                   NULL,
                                                   &theDataSize,
                                                   &transportType);
    if(theError == 0)
    {
        *isWireless = 
            transportType == kAudioDeviceTransportTypeAirPlay ||
            transportType == kAudioDeviceTransportTypeBluetooth ||
            transportType == kAudioDeviceTransportTypeBluetoothLE;
    }
    return theError;
}

static OSStatus GetIOBufferFrameSizeRange(AudioObjectID inDeviceID,
                                          UInt32* outMinimum,
                                          UInt32* outMaximum)
{
    AudioObjectPropertyAddress theAddress = { kAudioDevicePropertyBufferFrameSizeRange,
                                              kAudioObjectPropertyScopeGlobal,
                                              kAudioObjectPropertyElementMaster };

    AudioValueRange theRange = { 0, 0 };
    UInt32 theDataSize = sizeof(AudioValueRange);
    OSStatus theError = AudioObjectGetPropertyData(inDeviceID,
                                                   &theAddress,
                                                   0,
                                                   NULL,
                                                   &theDataSize,
                                                   &theRange);
    if(theError == 0)
    {
        *outMinimum = theRange.mMinimum;
        *outMaximum = theRange.mMaximum;
    }
    return theError;
}

static OSStatus SetCurrentIOBufferFrameSize(AudioObjectID inDeviceID,
                                            UInt32 inIOBufferFrameSize)
{
    AudioObjectPropertyAddress theAddress = { kAudioDevicePropertyBufferFrameSize,
                                              kAudioObjectPropertyScopeGlobal,
                                              kAudioObjectPropertyElementMaster };

    return AudioObjectSetPropertyData(inDeviceID,
                                      &theAddress,
                                      0,
                                      NULL,
                                      sizeof(UInt32), &inIOBufferFrameSize);
}

MacAudioDevice::MacAudioDevice(MacAudioEngine* parent, std::string deviceName, int coreAudioId, IAudioEngine::AudioDirection direction, int numChannels, int sampleRate)
    : ThreadedObject("MacDevice")
    , coreAudioId_(coreAudioId)
    , rtCoreAudioId_(coreAudioId)
    , direction_(direction)
    , numChannels_(numChannels)
    , sampleRate_(sampleRate)
    , deviceName_(std::move(deviceName))
    , inputFrames_(nullptr)
    , isDefaultDevice_(false)
    , parent_(parent)
    , auHAL_(nullptr)
    , bufferList_(nullptr)
    , running_(false)
{
    log_info("Create MacAudioDevice \"%s\" with ID %d, channels %d and sample rate %d", deviceName_.c_str(), coreAudioId, numChannels, sampleRate);
    
    sem_ = dispatch_semaphore_create(0);
}

MacAudioDevice::~MacAudioDevice()
{
    // First the threads CoreAudio's notifications started: they use this
    // object, so they have to be finished with it before it goes.
    std::vector<std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>> workers;
    {
        std::lock_guard<std::mutex> lk(notificationMtx_);
        beingDestroyed_ = true;
        workers.swap(notificationThreads_);
    }
    for (auto& worker : workers)
    {
        if (worker.first.joinable())
        {
            worker.first.join();
        }
    }

    // Also cleans up after a start() that failed partway through.
    stopImpl_();
    
    // Tasks this object put on its own queue have to finish while the rest
    // of the device is still there for them.
    waitForAllTasksComplete_();
    
    dispatch_release(sem_);
}
    
int MacAudioDevice::getNumChannels() FREEDV_NONBLOCKING
{
    return numChannels_;
}

int MacAudioDevice::getSampleRate() const FREEDV_NONBLOCKING
{
    return sampleRate_;
}

void MacAudioDevice::start()
{
    std::shared_ptr<std::promise<void>> prom = std::make_shared<std::promise<void> >();
    auto fut = prom->get_future();
    enqueue_([&, prom]() {
        // Set sample rate of device.
#if 0
        AudioObjectPropertyAddress propertyAddress = {
            .mSelector = kAudioDevicePropertyNominalSampleRate,
            .mScope = kAudioObjectPropertyScopeGlobal,
            .mElement = kAudioObjectPropertyElementMain
        };

        propertyAddress.mScope = 
            (direction_ == IAudioEngine::AUDIO_ENGINE_IN) ?
            kAudioDevicePropertyScopeInput :
            kAudioDevicePropertyScopeOutput;

        Float64 sampleRateAsFloat = sampleRate_;

        log_info("Attempting to set sample rate to %f for device %d", sampleRateAsFloat, coreAudioId_);
        OSStatus error = AudioObjectSetPropertyData(
            coreAudioId_,
            &propertyAddress,
            0,
            nil,
            sizeof(Float64),
            &sampleRateAsFloat);
        if (error != noErr)
        {
            std::stringstream ss;
            ss << "Could not set sample rate for device \"" << deviceName_ << "\" (err " << error << ")";
            if (onAudioErrorFunction)
            {
                onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
            }
            prom->set_value();
            return;
        }
#endif // 0
        
        // Attempt to set the IO frame size to an optimal value. This hopefully 
        // reduces dropouts on marginal hardware.
        UInt32 minFrameSize = 0;
        UInt32 maxFrameSize = 0;
        UInt32 desiredFrameSize = AUDIO_SAMPLE_BLOCK_MSEC * sampleRate_ / MS_TO_SEC;
        
        // Calculate next power of two above desiredFrameSize if not already power of two
        desiredFrameSize = nextPowerOfTwo_(desiredFrameSize);
        
        GetIOBufferFrameSizeRange(coreAudioId_, &minFrameSize, &maxFrameSize);
        if (minFrameSize != 0 && maxFrameSize != 0)
        {
            log_info("Frame sizes of %d to %d are supported for audio device ID %d", minFrameSize, maxFrameSize, coreAudioId_);
            desiredFrameSize = std::min(maxFrameSize, desiredFrameSize);
            desiredFrameSize = std::max(minFrameSize, desiredFrameSize);
            log_info("Device %d: calculated frame size of %d", coreAudioId_, desiredFrameSize);
            
            // Detect whether this is a Bluetooth device. If so, automatically use the maxFrameSize
            // to avoid dropouts.
            bool isWireless = false;
            if (GetIsWirelessDevice(coreAudioId_, &isWireless) == noErr && isWireless)
            {
                desiredFrameSize = sampleRate_ * AUDIO_SAMPLE_BLOCK_WIRELESS_MSEC / MS_TO_SEC;
                desiredFrameSize = nextPowerOfTwo_(desiredFrameSize);
                desiredFrameSize = std::min(maxFrameSize, desiredFrameSize);
                desiredFrameSize = std::max(minFrameSize, desiredFrameSize);
                
                log_info("Device %d: detected wireless device, using frame size of %d instead", coreAudioId_, desiredFrameSize);
            }
            
            log_info("Set frame size for device %d to %d", coreAudioId_, desiredFrameSize);
            if (SetCurrentIOBufferFrameSize(coreAudioId_, desiredFrameSize) != noErr)
            {
                log_warn("Could not set IO frame size to %d for audio device ID %d", desiredFrameSize, coreAudioId_);
            }
        }
        else
        {
            // Set maxFrameSize to something reasonable for further down.
            maxFrameSize = 4096;
        }
        
        chosenFrameSize_ = desiredFrameSize;

        // Create AUHAL object
        AudioComponent comp;
        AudioComponentDescription desc;

        //There are several different types of Audio Units.
        //Some audio units serve as Outputs, Mixers, or DSP
        //units. See AUComponent.h for listing
        desc.componentType = kAudioUnitType_Output;

        //Every Component has a subType, which will give a clearer picture
        //of what this components function will be.
        desc.componentSubType = kAudioUnitSubType_HALOutput;

         //all Audio Units in AUComponent.h must use
         //"kAudioUnitManufacturer_Apple" as the Manufacturer
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        desc.componentFlags = 0;
        desc.componentFlagsMask = 0;

        //Finds a component that meets the desc spec's
        log_info("Device %d: loading audio component", coreAudioId_);
        comp = AudioComponentFindNext(NULL, &desc);
        if (comp == nullptr)
        {
            std::stringstream ss;
            ss << "Device " << coreAudioId_ << ": Could not find audio component. Note: should never happen!";
            log_error("%s", ss.str().c_str());
            if (onAudioErrorFunction)
            {
                onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
            }
            prom->set_value();
            return;
        }

         //gains access to the services provided by the component
        AudioComponentInstanceNew(comp, &auHAL_);
        
        // Enables input or output depending on the direction
        UInt32 enableIO;
        UInt32 size=0;

        log_info("Device %d: enabling I/O", coreAudioId_);
        enableIO = (direction_ == IAudioEngine::AUDIO_ENGINE_IN) ? 1 : 0;
        AudioUnitSetProperty(auHAL_,
                    kAudioOutputUnitProperty_EnableIO,
                    kAudioUnitScope_Input,
                    1, // input element
                    &enableIO,
                    sizeof(enableIO));

        enableIO = (direction_ == IAudioEngine::AUDIO_ENGINE_OUT) ? 1 : 0;
        AudioUnitSetProperty(auHAL_,
                    kAudioOutputUnitProperty_EnableIO,
                    kAudioUnitScope_Output,
                    0,   //output element
                    &enableIO,
                    sizeof(enableIO));
    
        
        // Set associated device for AUHAL object
        log_info("Device %d: setting AUHAL device to current", coreAudioId_);
        OSStatus error = AudioUnitSetProperty(auHAL_,
                                     kAudioOutputUnitProperty_CurrentDevice,
                                     kAudioUnitScope_Global,
                                     0,
                                     &coreAudioId_,
                                     sizeof(coreAudioId_));
        if (error != noErr)
        {
            std::stringstream ss;
            ss << "Could not assign device \"" << deviceName_ << "\" to AUHAL (err " << error << ")";
            log_error("%s", ss.str().c_str());
            if (onAudioErrorFunction)
            {
                onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
            }
            prom->set_value();
            return;
        }
        
        // Set sample rate of device
        log_info("Device %d: setting stream format", coreAudioId_);
        AudioStreamBasicDescription deviceFormat;
        memset(&deviceFormat, 0, sizeof(deviceFormat));
        size = sizeof(AudioStreamBasicDescription);

        deviceFormat.mSampleRate = sampleRate_;
        deviceFormat.mFormatID = kAudioFormatLinearPCM;
        deviceFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved; // macOS only accepts floats
        deviceFormat.mFramesPerPacket = 1;
        deviceFormat.mChannelsPerFrame = numChannels_;
        deviceFormat.mBitsPerChannel = sizeof(Float32) * 8;
        deviceFormat.mBytesPerFrame = sizeof(Float32);
        deviceFormat.mBytesPerPacket = deviceFormat.mBytesPerFrame;

        // Get the device's format
        auto setScope = (direction_ == IAudioEngine::AUDIO_ENGINE_OUT) ? kAudioUnitScope_Input : kAudioUnitScope_Output;
        auto bus = (direction_ == IAudioEngine::AUDIO_ENGINE_IN) ? 1 : 0;
        
        error = AudioUnitSetProperty (auHAL_,
                                       kAudioUnitProperty_StreamFormat,
                                       setScope,
                                       bus,
                                       &deviceFormat,
                                       size);
        if (error != noErr)
        {
            std::stringstream ss;
            ss << "Could not set stream format for device \"" << deviceName_ << "\" (err " << error << ")";
            log_error("%s", ss.str().c_str());
            if (onAudioErrorFunction)
            {
                onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
            }
            prom->set_value();
            return;
        }

        // Allocate buffers
        inputFrames_ = new short[(maxFrameSize + 1) * numChannels_];
        assert(inputFrames_ != nullptr);
        memset(inputFrames_, 0, sizeof(short) * (maxFrameSize + 1) * numChannels_);

        if (direction_ == IAudioEngine::AUDIO_ENGINE_IN)
        {
            //calculate number of buffers from channels       
            auto propsize = offsetof(AudioBufferList, mBuffers[0]) + (sizeof(AudioBuffer) * numChannels_);

            //malloc buffer lists
            bufferList_ = (AudioBufferList *)malloc(propsize);
            bufferList_->mNumberBuffers = numChannels_;
                                   
            //pre-malloc buffers for AudioBufferLists          
            for(UInt32 i =0; i< bufferList_->mNumberBuffers ; i++) 
            {
                bufferList_->mBuffers[i].mNumberChannels = 1;                   
                bufferList_->mBuffers[i].mDataByteSize = (maxFrameSize + 1)  * sizeof(Float32);
                bufferList_->mBuffers[i].mData = malloc((maxFrameSize + 1) * sizeof(Float32));
            }
        }
        else
        {
            bufferList_ = nullptr;
        }
        
        // Set callbacks
        log_info("Device %d: setting callbacks", coreAudioId_);
        if (direction_ == IAudioEngine::AUDIO_ENGINE_IN)
        {
            AURenderCallbackStruct input;
            input.inputProc = InputProc_;
            input.inputProcRefCon = this;

            AudioUnitSetProperty(
                    auHAL_,
                    kAudioOutputUnitProperty_SetInputCallback,
                    kAudioUnitScope_Global,
                    0,
                    &input,
                    sizeof(input));
        }
        else
        {
            AURenderCallbackStruct output;
            output.inputProc = OutputProc_;
            output.inputProcRefCon = this;

            AudioUnitSetProperty(
                    auHAL_,
                    kAudioUnitProperty_SetRenderCallback,
                    kAudioUnitScope_Input,
                    0,
                    &output,
                    sizeof(output));
        }
        
        // Start audio
        log_info("Device %d: initializing audio unit", coreAudioId_);
        error = AudioUnitInitialize(auHAL_);
        if (error != noErr)
        {
            std::stringstream ss;
            ss << "Could not initialize audio unit for \"" << deviceName_ << "\" (err " << error << ")";
            log_error("%s", ss.str().c_str());
            if (onAudioErrorFunction)
            {
                onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
            }
            prom->set_value();
            return;
        }
        
        log_info("Device %d: starting audio unit", coreAudioId_);
        error = AudioOutputUnitStart(auHAL_);
        if (error != noErr)
        {
            std::stringstream ss;
            ss << "Could not start audio unit for \"" << deviceName_ << "\" (err " << error << ")";
            log_error("%s", ss.str().c_str());
            if (onAudioErrorFunction)
            {
                onAudioErrorFunction(*this, ss.str(), onAudioErrorState);
            }
            prom->set_value();
            return;
        }
        
        running_ = true;

        // add listener for detecting when a device is removed
        const AudioObjectPropertyAddress aliveAddress =
        {
            kAudioDevicePropertyDeviceIsAlive,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMaster
        };
        AudioObjectAddPropertyListener(coreAudioId_, &aliveAddress, DeviceIsAliveCallback_, this);

        // Add listener for processor overloads
        const AudioObjectPropertyAddress overloadAddress =
        {
            kAudioDeviceProcessorOverload,
            (direction_ == IAudioEngine::AUDIO_ENGINE_OUT) ? kAudioDevicePropertyScopeOutput : kAudioDevicePropertyScopeInput,
            0
        };
        AudioObjectAddPropertyListener(coreAudioId_, &overloadAddress, DeviceOverloadCallback_, this);

        // See if we're the default audio device. If we are and we're disconnected, we want to
        // switch to the *new* default audio device.
        AudioDeviceID defaultDevice = 0;
        UInt32 defaultSize = sizeof(AudioDeviceID);

        const AudioObjectPropertyAddress defaultAddr = {
            (direction_ == IAudioEngine::AUDIO_ENGINE_OUT) ? kAudioHardwarePropertyDefaultOutputDevice : kAudioHardwarePropertyDefaultInputDevice,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMaster
        };

        AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddr, 0, NULL, &defaultSize, &defaultDevice); 
        if ((int)defaultDevice == coreAudioId_)
        {
            isDefaultDevice_ = true;
        }
        else
        {
            isDefaultDevice_ = false;
        }

        prom->set_value();
    });
    fut.wait();
}

void MacAudioDevice::stop()
{
    stopImpl_();
}

void MacAudioDevice::stopImpl_()
{
    std::shared_ptr<std::promise<void>> prom = std::make_shared<std::promise<void> >();
    auto fut = prom->get_future();
    enqueue_([&, prom]() {
        // Safe to call more than once, or without a successful start():
        // only undo what start() actually did.
        if (running_)
        {
            log_info("Device %d: removing listeners", coreAudioId_);
            
            const AudioObjectPropertyAddress aliveAddress =
            {
                kAudioDevicePropertyDeviceIsAlive,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMaster
            };
            AudioObjectRemovePropertyListener(coreAudioId_, &aliveAddress, DeviceIsAliveCallback_, this);

            const AudioObjectPropertyAddress overloadAddress =
            {
                kAudioDeviceProcessorOverload,
                (direction_ == IAudioEngine::AUDIO_ENGINE_OUT) ? kAudioDevicePropertyScopeOutput : kAudioDevicePropertyScopeInput,
                0
            };
            AudioObjectRemovePropertyListener(coreAudioId_, &overloadAddress, DeviceOverloadCallback_, this);

            log_info("Device %d: stopping audio unit", coreAudioId_);
            running_ = false;
            AudioOutputUnitStop(auHAL_);
            
            // Wait for audio to stop
            Boolean isRunning = 1;
            while (isRunning)
            {
                log_info("[Device %d] Waiting for audio unit to stop", coreAudioId_);
                
                UInt32 s = sizeof( isRunning );
                auto err = AudioUnitGetProperty( 
                    auHAL_, 
                    kAudioOutputUnitProperty_IsRunning, 
                    kAudioUnitScope_Global, 
                    0,  &isRunning, &s );
                if (err != noErr)
                {
                    log_warn("[Device %d] Could not get kAudioOutputUnitProperty_IsRunning value (err %d)", coreAudioId_, err);
                    break;
                }
                std::this_thread::sleep_for(10ms);
            }
        }
        
        if (auHAL_ != nullptr)
        {
            AudioUnitUninitialize(auHAL_);
            AudioComponentInstanceDispose(auHAL_);
            auHAL_ = nullptr;
        }
        if (bufferList_ != nullptr)
        {
            for(UInt32 i =0; i< bufferList_->mNumberBuffers ; i++) 
            {
                free(bufferList_->mBuffers[i].mData);
            }
            free(bufferList_);
            bufferList_ = nullptr;
        }
        delete[] inputFrames_;
        inputFrames_ = nullptr;
        
        prom->set_value();
    });
    
    fut.wait();
}

bool MacAudioDevice::isRunning()
{
    return running_;
}

int64_t MacAudioDevice::getLatencyInMicroseconds()
{
    std::shared_ptr<std::promise<int64_t>> prom = std::make_shared<std::promise<int64_t> >();
    auto fut = prom->get_future();
    enqueue_([&, prom]() {        
        // Total latency is based on the formula:
        //     kAudioDevicePropertyLatency + kAudioDevicePropertySafetyOffset + 
        //     kAudioDevicePropertyBufferFrameSize + kAudioStreamPropertyLatency.
        // This is in terms of the number of samples. Divide by sample rate and number of channels to get number of seconds.
        // More info:
        //     https://stackoverflow.com/questions/65600996/avaudioengine-reconcile-sync-input-output-timestamps-on-macos-ios
        //     https://forum.juce.com/t/macos-round-trip-latency/45278
        auto scope = 
            (direction_ == IAudioEngine::AUDIO_ENGINE_IN) ? 
            kAudioDevicePropertyScopeInput :
            kAudioDevicePropertyScopeOutput;
            
        // Get audio device latency
        AudioObjectPropertyAddress propertyAddress = {
              kAudioDevicePropertyLatency, 
              scope,
              kAudioObjectPropertyElementMaster};
        UInt32 deviceLatencyFrames = 0;
        UInt32 size = sizeof(deviceLatencyFrames);
        OSStatus result = AudioObjectGetPropertyData(
                  coreAudioId_, 
                  &propertyAddress, 
                  0,
                  nullptr, 
                  &size, 
                  &deviceLatencyFrames); // assume 0 if we can't retrieve for some reason
         
        UInt32 deviceSafetyOffset = 0;
        propertyAddress.mSelector = kAudioDevicePropertySafetyOffset;
        result = AudioObjectGetPropertyData(
                  coreAudioId_, 
                  &propertyAddress, 
                  0,
                  nullptr, 
                  &size, 
                  &deviceSafetyOffset);
                  
        UInt32 bufferFrameSize = 0;
        propertyAddress.mSelector = kAudioDevicePropertyBufferFrameSize;
        result = AudioObjectGetPropertyData(
                  coreAudioId_, 
                  &propertyAddress, 
                  0,
                  nullptr, 
                  &size, 
                  &bufferFrameSize);
    
        propertyAddress.mSelector = kAudioDevicePropertyStreams;
        size = 0;
        result = AudioObjectGetPropertyDataSize(
                  coreAudioId_, 
                  &propertyAddress, 
                  0,
                  nullptr, 
                  &size);
        UInt32 streamLatency = 0;
        if (result == noErr)
        {
            AudioStreamID* streams = new AudioStreamID[size / sizeof(AudioStreamID)];
            assert(streams != nullptr);

            result = AudioObjectGetPropertyData(
                      coreAudioId_, 
                      &propertyAddress, 
                      0,
                      nullptr, 
                      &size, 
                      streams);
            if (result == noErr)
            {
                propertyAddress.mSelector = kAudioStreamPropertyLatency;
                size = sizeof(streamLatency);
                result = AudioObjectGetPropertyData(
                          streams[0], 
                          &propertyAddress, 
                          0,
                          nullptr, 
                          &size, 
                          &streamLatency);
            }

            delete[] streams;
        }
        
        int64_t ioLatency = streamLatency + deviceLatencyFrames + deviceSafetyOffset;
        int64_t frameSize = bufferFrameSize;
        prom->set_value(1000000 * (ioLatency + frameSize) / sampleRate_);
    });
    return fut.get();
}

void MacAudioDevice::setHelperRealTime()
{
    numRealTimeWorkers_.fetch_add(1, std::memory_order_release);

    // Set thread QoS to "user interactive"
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

    // Adapted from Chromium project. May need to adjust parameters
    // depending on behavior.
    
    // Get current thread ID
    auto currentThreadId = pthread_mach_thread_np(pthread_self());
    
    // Increase thread priority to real-time.
    // Please note that the thread_policy_set() calls may fail in
    // rare cases if the kernel decides the system is under heavy load
    // and is unable to handle boosting the thread priority.
    // In these cases we just return early and go on with life.
    // Make thread fixed priority.
    thread_extended_policy_data_t policy;
    policy.timeshare = 0;  // Set to 1 for a non-fixed thread.
    kern_return_t result =
        thread_policy_set(currentThreadId,
                          THREAD_EXTENDED_POLICY,
                          reinterpret_cast<thread_policy_t>(&policy),
                          THREAD_EXTENDED_POLICY_COUNT);
    if (result != KERN_SUCCESS)
    {
        log_warn("Could not set current thread to real-time: %d", result);
    }

    // Set to relatively high priority.
    thread_precedence_policy_data_t precedence;
    precedence.importance = 63;
    result = thread_policy_set(currentThreadId,
                               THREAD_PRECEDENCE_POLICY,
                               reinterpret_cast<thread_policy_t>(&precedence),
                               THREAD_PRECEDENCE_POLICY_COUNT);
    if (result != KERN_SUCCESS)
    {
        log_warn("Could not increase thread priority: %d", result);
    }
    
    // Most important, set real-time constraints.
    // Define the guaranteed and max fraction of time for the audio thread.
    // These "duty cycle" values can range from 0 to 1.  A value of 0.5
    // means the scheduler would give half the time to the thread.
    // These values have empirically been found to yield good behavior.
    // Good means that audio performance is high and other threads won't starve.
    const double kGuaranteedAudioDutyCycle = 0.75;
    const double kMaxAudioDutyCycle = 0.85;
    
    // Define constants determining how much time the audio thread can
    // use in a given time quantum.  All times are in milliseconds.
    const double kTimeQuantum = 60; // 60ms, calculated by AUDIO_SAMPLE_BLOCK_WIRELESS_MSEC / kGuaranteedAudioDutyCycle + a bit extra.
    
    // Time guaranteed each quantum.
    const double kAudioTimeNeeded = kGuaranteedAudioDutyCycle * kTimeQuantum;
    
    // Maximum time each quantum.
    const double kMaxTimeAllowed = kMaxAudioDutyCycle * kTimeQuantum;
    
    // Get the conversion factor from milliseconds to absolute time
    // which is what the time-constraints call needs.
    mach_timebase_info_data_t tbInfo;
    mach_timebase_info(&tbInfo);
    double ms_to_abs_time =
        (static_cast<double>(tbInfo.denom) / tbInfo.numer) * 1000000;
    thread_time_constraint_policy_data_t timeConstraints;
    timeConstraints.period = kTimeQuantum * ms_to_abs_time;
    timeConstraints.computation = kAudioTimeNeeded * ms_to_abs_time;
    timeConstraints.constraint = kMaxTimeAllowed * ms_to_abs_time;
    timeConstraints.preemptible = 1;
    
    result =
        thread_policy_set(currentThreadId,
                          THREAD_TIME_CONSTRAINT_POLICY,
                          reinterpret_cast<thread_policy_t>(&timeConstraints),
                          THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    if (result != KERN_SUCCESS)
    {
        log_warn("Could not set time constraint policy: %d", result);
    }

    // Workgroup joining improves scheduling even if time constraint policy
    // could not be set above.
    joinWorkgroup_();
}

OSStatus MacAudioDevice::InputProc_(
            void *inRefCon,
            AudioUnitRenderActionFlags *ioActionFlags,
            const AudioTimeStamp *inTimeStamp,
            UInt32 inBusNumber,
            UInt32 inNumberFrames,
            AudioBufferList *) FREEDV_NONBLOCKING
{
    MacAudioDevice* thisObj = (MacAudioDevice*)inRefCon;
    OSStatus err = noErr;
 
    err = AudioUnitRender(thisObj->auHAL_,
                    ioActionFlags,
                    inTimeStamp,
                    inBusNumber,     //will be '1' for input data
                    inNumberFrames, //# of frames requested
                    thisObj->bufferList_);

    if (err == noErr)
    {
        if (thisObj->onAudioDataFunction)
        {
            for (UInt32 index = 0; index < inNumberFrames; index++)
            {
                for (UInt32 chan = 0; chan < thisObj->bufferList_->mNumberBuffers; chan++)
                {
                    thisObj->inputFrames_[thisObj->numChannels_ * index + chan] = ((float*)thisObj->bufferList_->mBuffers[chan].mData)[index] * 32767;
                }
            }
            
            thisObj->onAudioDataFunction(*thisObj, thisObj->inputFrames_, inNumberFrames, thisObj->onAudioDataState);
        }
          
        auto numWorkers = thisObj->numRealTimeWorkers_.load(std::memory_order_acquire);
        for (; numWorkers > 0; numWorkers--)
        { 
            // Note: assuming that semaphore signalling is safe. This mechanism will need to revisited
            // if that turns out not to be the case.
            FREEDV_BEGIN_VERIFIED_SAFE
            dispatch_semaphore_signal(thisObj->sem_);
            FREEDV_END_VERIFIED_SAFE
        }
    }
    else
    {
        // Note: this is definitely unsafe. However, if we get to this point, the audio will likely glitch anyway.
        FREEDV_BEGIN_VERIFIED_SAFE
        log_warn("Device %d: got error in render func (%d)", thisObj->coreAudioId_, err);
        FREEDV_END_VERIFIED_SAFE
    }
    
    return err;
}
            
OSStatus MacAudioDevice::OutputProc_(
            void *inRefCon,
            AudioUnitRenderActionFlags *,
            const AudioTimeStamp *,
            UInt32,
            UInt32 inNumberFrames,
            AudioBufferList * ioData) FREEDV_NONBLOCKING
{
    MacAudioDevice* thisObj = (MacAudioDevice*)inRefCon;

    if (thisObj->onAudioDataFunction)
    {
        // Whatever the callback doesn't fill in (e.g. the radio's output
        // with nothing to send) is silence, as on Windows, rather than the
        // last block it did fill in over again.
        memset(thisObj->inputFrames_, 0, sizeof(short) * inNumberFrames * thisObj->numChannels_);
        thisObj->onAudioDataFunction(*thisObj, thisObj->inputFrames_, inNumberFrames, thisObj->onAudioDataState);

        for (UInt32 index = 0; index < inNumberFrames; index++)
        {
            for (UInt32 chan = 0; chan < ioData->mNumberBuffers; chan++)
            {
                ((float*)ioData->mBuffers[chan].mData)[index] = thisObj->inputFrames_[thisObj->numChannels_ * index + chan] / 32767.0;
            }
        }
    }
    
    return OSStatus(noErr);
}

UInt32 MacAudioDevice::nextPowerOfTwo_(UInt32 val)
{
    if ((val & (val - 1)) != 0)
    {
        val--;
        val |= val >> 1;
        val |= val >> 2;
        val |= val >> 4;
        val |= val >> 8;
        val |= val >> 16;
        val++;
    }
    return val;
}

void MacAudioDevice::joinWorkgroup_()
{
    // Join Core Audio workgroup
    Workgroup_ = nullptr;
    JoinToken_ = nullptr;
    
    UInt32 size = sizeof(os_workgroup_t);
    os_workgroup_t* wgMem = new os_workgroup_t;
    assert(wgMem != nullptr);
    os_workgroup_join_token_s* wgToken = new os_workgroup_join_token_s;
    assert(wgToken != nullptr);
    
    AudioObjectPropertyAddress propertyAddress = {
        .mSelector = kAudioDevicePropertyIOThreadOSWorkgroup,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };

    // Runs on the real-time thread, so it has the ID that thread can read.
    int coreAudioId = rtCoreAudioId_.load(std::memory_order_acquire);
    OSStatus osResult = AudioObjectGetPropertyData(
              coreAudioId,
              &propertyAddress,
              0,
              nullptr,
              &size,
              wgMem);
    if (osResult != noErr)
    {
        Workgroup_ = nullptr;
        JoinToken_ = nullptr;
        CurrentCoreAudioId_ = 0;

        delete wgMem;
        delete wgToken;
        return;
    }
    else
    {
        Workgroup_ = wgMem;
        JoinToken_ = wgToken;
        CurrentCoreAudioId_ = coreAudioId;
    }
  
    auto workgroupResult = os_workgroup_join(*wgMem, wgToken);
    if (workgroupResult != 0)
    {
        Workgroup_ = nullptr;
        JoinToken_ = nullptr;
        CurrentCoreAudioId_ = 0;
        delete wgMem;
        delete wgToken;
    }
}

void MacAudioDevice::startRealTimeWork()
{
    startTime_ = std::chrono::steady_clock::now();

    // If the audio ID changes on us, join the new workgroup
    if (CurrentCoreAudioId_ != 0 && CurrentCoreAudioId_ != rtCoreAudioId_.load(std::memory_order_acquire) && Workgroup_ != nullptr)
    {
        leaveWorkgroup_();
        joinWorkgroup_();
    }
}

void MacAudioDevice::stopRealTimeWork(bool fastMode)
{
    int64_t nominalUs = ((1000000LL * chosenFrameSize_) / sampleRate_) >> (fastMode ? 1 : 0);

    // Compensate for how much of the period THIS cycle's own processing
    // already used, measured directly against startTime_ (set by
    // startRealTimeWork() right before processing began) rather than a debt
    // figure copied from the *previous* cycle. The previous approach lagged
    // by one cycle: it corrected this wait for last cycle's overrun instead
    // of this cycle's own, which overcorrects/undercorrects whenever
    // processing time varies cycle to cycle instead of holding steady.
    // waitOvershootUs_ separately tracks only the wait itself running long
    // (the one thing that genuinely can't be known until after it happens),
    // so a systematic scheduling overshoot still can't accumulate into
    // drift. Matches WASAPIAudioDevice's stopRealTimeWork() fix (36db96e5).
    auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime_).count();
    int64_t waitUs = nominalUs - elapsedUs - waitOvershootUs_;
    if (waitUs <= 0)
    {
        waitOvershootUs_ = 0;
        return;
    }

    auto waitStartTime = std::chrono::steady_clock::now();
    dispatch_semaphore_wait(sem_, dispatch_time(DISPATCH_TIME_NOW, 1000 * waitUs));

    auto actualWaitUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - waitStartTime).count();
    waitOvershootUs_ = std::max((int64_t)0, actualWaitUs - waitUs); // cap at >= 0; an early (semaphore) wake isn't overshoot.
}

void MacAudioDevice::clearHelperRealTime()
{
    numRealTimeWorkers_.fetch_sub(1, std::memory_order_release);
    leaveWorkgroup_();
}

void MacAudioDevice::leaveWorkgroup_()
{
    if (Workgroup_ != nullptr)
    {
        os_workgroup_t* wgMem = (os_workgroup_t*)Workgroup_;
        os_workgroup_join_token_s* wgToken = (os_workgroup_join_token_s*)JoinToken_;
        
        os_workgroup_leave(*wgMem, wgToken);
        
        delete wgMem;
        delete wgToken;
        
        Workgroup_ = nullptr;
        JoinToken_ = nullptr;
        CurrentCoreAudioId_ = 0;
    }
}

// Runs work that one of CoreAudio's notifications can't do itself, on a
// thread the device keeps: the work uses the device, so the device has to
// outlive it (see ~MacAudioDevice).
void MacAudioDevice::runNotificationThread_(std::function<void()> work)
{
    std::unique_lock<std::mutex> lk(notificationMtx_);

    if (beingDestroyed_)
    {
        // Nothing left for the notification to tell anyone about.
        return;
    }

    // Tidy up the ones that are done, so a run of notifications doesn't
    // leave a thread behind each time.
    for (auto finished = notificationThreads_.begin(); finished != notificationThreads_.end();)
    {
        if (finished->second->load(std::memory_order_acquire))
        {
            if (finished->first.joinable())
            {
                finished->first.join();
            }
            finished = notificationThreads_.erase(finished);
        }
        else
        {
            ++finished;
        }
    }

    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread worker([work, done]() {
        work();
        done->store(true, std::memory_order_release);
    });
    notificationThreads_.push_back(std::make_pair(std::move(worker), done));
}

void MacAudioDevice::setCoreAudioId_(int coreAudioId)
{
    auto prom = std::make_shared<std::promise<void> >();
    auto fut = prom->get_future();
    enqueue_([this, coreAudioId, prom]() {
        coreAudioId_ = coreAudioId;
        rtCoreAudioId_.store(coreAudioId, std::memory_order_release);
        prom->set_value();
    });
    fut.wait();
}

int MacAudioDevice::DeviceIsAliveCallback_(
        AudioObjectID,
        UInt32,
        const AudioObjectPropertyAddress    inAddresses[],
        void*                               inClientData)
{
    (void)inAddresses;

    MacAudioDevice* thisObj = (MacAudioDevice*)inClientData;
    if (thisObj->isDefaultDevice_)
    {
        // Get new default device ID
        AudioDeviceID defaultDevice = 0;
        UInt32 defaultSize = sizeof(AudioDeviceID);

        const AudioObjectPropertyAddress defaultAddr = {
            (thisObj->direction_ == IAudioEngine::AUDIO_ENGINE_OUT) ? kAudioHardwarePropertyDefaultOutputDevice : kAudioHardwarePropertyDefaultInputDevice,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMaster
        };

        AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddr, 0, NULL, &defaultSize, &defaultDevice); 

        // Stop/start device with new ID in a separate thread to avoid deadlocks
        thisObj->runNotificationThread_([thisObj, defaultDevice]() {
            thisObj->stop();
            thisObj->setCoreAudioId_(defaultDevice);
            thisObj->start();
        });
    } 
    else 
    {
        // Possible GUI stuff really shouldn't be happening on the audio thread.
        thisObj->runNotificationThread_([thisObj]() {
            std::stringstream ss;
            ss << "The device " << thisObj->deviceName_ << " may have been removed from the system. Press 'Stop' and then verify your audio settings.";
            if (thisObj->onAudioErrorFunction)
            {
                thisObj->onAudioErrorFunction(*thisObj, ss.str(), thisObj->onAudioErrorState);
            }
        });
    }

    return noErr;
}

int MacAudioDevice::DeviceOverloadCallback_(
        AudioObjectID,
        UInt32,
        const AudioObjectPropertyAddress    inAddresses[],
        void*                               inClientData)
{
    (void)inAddresses;

    MacAudioDevice* thisObj = (MacAudioDevice*)inClientData;

    // Possible GUI stuff really shouldn't be happening on the audio thread.
    thisObj->runNotificationThread_([thisObj]() {
        if (thisObj->onAudioUnderflowFunction)
        {
            thisObj->onAudioUnderflowFunction(*thisObj, thisObj->onAudioUnderflowState);
        }
    });

    return noErr;
}
