//=========================================================================
// Name:            PulseAudioDevice.cpp
// Purpose:         Defines the interface to a PulseAudio device.
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

#include <cstring>
#include <cstdio>
#include <chrono>
#include <sched.h>
#include <alloca.h>
#include <sys/resource.h>
#include <signal.h>

#include "PulseAudioDevice.h"
#include "PulseAudioEngine.h"
#include "../util/timespec.h"

#if defined(USE_RTKIT)
#include "rtkit.h"
#endif // defined(USE_RTKIT)

#include "../util/logging/ulog.h"

using namespace std::chrono_literals;

// Target latency. This controls e.g. how long it takes for
// TX audio to reach the radio.
#define PULSE_TARGET_LATENCY_US 20000

#if 0
thread_local bool PulseAudioDevice::MustStopWork_ = false;
#endif // 0
 
PulseAudioDevice::PulseAudioDevice(wxString const& devName, IAudioEngine::AudioDirection direction, int sampleRate, int numChannels)
    : context_(nullptr)
    , mainloop_(nullptr)
    , stream_(nullptr)
    , devName_(devName)
    , direction_(direction)
    , sampleRate_(sampleRate)
    , numChannels_(numChannels)
    , extraTimeNs_(0)
{
    // Set default description
    setDescription("PulseAudio Device");
}

PulseAudioDevice::~PulseAudioDevice()
{
    stopImpl_();
}

bool PulseAudioDevice::isRunning()
{
    return stream_ != nullptr;
}

void PulseAudioDevice::start()
{
    std::unique_lock<std::mutex> lk(objLock_);

    pa_sample_spec sample_specification;
    sample_specification.format = PA_SAMPLE_S16LE;
    sample_specification.rate = sampleRate_;
    sample_specification.channels = numChannels_;

    auto error = PulseAudioEngine::CreateConnection(&mainloop_, &context_, true);
    if (!error.empty())
    {
        if (onAudioErrorFunction)
        {
            onAudioErrorFunction(*this, error + " (" + (const char*)devName_.ToUTF8() + ")", onAudioErrorState);
        }
        return;
    }
    
    pa_threaded_mainloop_lock(mainloop_);
    stream_ = pa_stream_new(context_, description.c_str(), &sample_specification, nullptr);
    if (stream_ == nullptr)
    {
        if (onAudioErrorFunction)
        {
            onAudioErrorFunction(*this, std::string("Could not create PulseAudio stream for ") + (const char*)devName_.ToUTF8(), onAudioErrorState);
        }
        pa_threaded_mainloop_unlock(mainloop_);

        PulseAudioEngine::DestroyConnection(mainloop_, context_);
        mainloop_ = nullptr;
        context_ = nullptr;
        return;
    }
    
    pa_stream_set_underflow_callback(stream_, &PulseAudioDevice::StreamUnderflowCallback_, this);
    pa_stream_set_overflow_callback(stream_, &PulseAudioDevice::StreamOverflowCallback_, this);
    pa_stream_set_moved_callback(stream_, &PulseAudioDevice::StreamMovedCallback_, this);
    pa_stream_set_state_callback(stream_, &PulseAudioDevice::StreamStateCallback_, this);
#if 0
    pa_stream_set_latency_update_callback(stream_, &PulseAudioDevice::StreamLatencyCallback_, this);
#endif // 0

    // recommended settings, i.e. server uses sensible values
    pa_buffer_attr buffer_attr; 
    buffer_attr.tlength = pa_usec_to_bytes(PULSE_TARGET_LATENCY_US, &sample_specification);
    buffer_attr.maxlength = (uint32_t)-1;
    buffer_attr.prebuf = 0; // Ensure that we can recover during an underrun
    buffer_attr.minreq = buffer_attr.tlength;
    buffer_attr.fragsize = buffer_attr.tlength;
    
    // Stream flags
    auto flags = 
         PA_STREAM_INTERPOLATE_TIMING |
         PA_STREAM_AUTO_TIMING_UPDATE | 
         PA_STREAM_ADJUST_LATENCY;
    
    int result = 0;
    if (direction_ == IAudioEngine::AUDIO_ENGINE_OUT)
    {
        log_info("Connecting to playback device %s", (const char*)devName_.ToUTF8());
        
        pa_stream_set_write_callback(stream_, &PulseAudioDevice::StreamWriteCallback_, this);
        result = pa_stream_connect_playback(
            stream_, devName_.c_str(), &buffer_attr, 
            (pa_stream_flags_t)flags, NULL, NULL);
        if (result == 0) pa_stream_trigger(stream_, nullptr, nullptr);
    }
    else
    {
        log_info("Connecting to record device %s", (const char*)devName_.ToUTF8());
        
        pa_stream_set_read_callback(stream_, &PulseAudioDevice::StreamReadCallback_, this);
        result = pa_stream_connect_record(
            stream_, devName_.c_str(), &buffer_attr, (pa_stream_flags_t)flags);
    }
    
    if (result != 0)
    {
        if (onAudioErrorFunction)
        {
            onAudioErrorFunction(*this, std::string("Could not connect PulseAudio stream to ") + (const char*)devName_.ToUTF8(), onAudioErrorState);
        }
    }
    else
    {
        // Set up semaphore for signaling workers
        if (sem_init(&sem_, 0, 0) < 0)
        {
            log_warn("Could not set up semaphore (errno = %d)", errno);
        }
    }

    pa_threaded_mainloop_unlock(mainloop_);
}

void PulseAudioDevice::stop()
{
    stopImpl_();
}

void PulseAudioDevice::stopImpl_()
{
    std::unique_lock<std::mutex> lk(objLock_);
    if (stream_ != nullptr)
    {
        pa_threaded_mainloop_lock(mainloop_);

        // Disconnect stream and wait for response from mainloop. A stream
        // that's already failed (e.g. because the sound server went away)
        // has nothing left to disconnect, so there'd be no response.
        if (PA_STREAM_IS_GOOD(pa_stream_get_state(stream_)) && pa_stream_disconnect(stream_) == 0)
        {
            pa_threaded_mainloop_wait(mainloop_);
        }

        // Deallocate stream.
        pa_stream_unref(stream_);
        stream_ = nullptr;

        pa_threaded_mainloop_unlock(mainloop_);

        sem_destroy(&sem_);
    }

    // Only after the stream has terminated, as waiting for that needs the main loop.
    PulseAudioEngine::DestroyConnection(mainloop_, context_);
    mainloop_ = nullptr;
    context_ = nullptr;
}

int64_t PulseAudioDevice::getLatencyInMicroseconds()
{
    // stopImpl_() destroys mainloop_ while holding objLock_.
    std::unique_lock<std::mutex> lk(objLock_);
    if (mainloop_ == nullptr)
    {
        return 0;
    }

    pa_threaded_mainloop_lock(mainloop_);
    pa_usec_t latency = 0;
    if (stream_ != nullptr)
    {
        pa_stream_get_latency(stream_, &latency, nullptr); // ignore error and assume 0
    }
    pa_threaded_mainloop_unlock(mainloop_);
    return latency;
}

void PulseAudioDevice::setHelperRealTime()
{
    // XXX: We can't currently enable RT scheduling on Linux
    // due to unreliable behavior surrounding how long it takes to
    // go through a single RX or TX cycle. This unreliability is 
    // likely due to the use of Python for some parts of RADE. Since
    // timing is so unreliable and due to the fact that Linux actually
    // kills processes that it deems as using "too much" CPU while in
    // real-time, it's better just to use normal scheduling for now.
#if 0
    // Set RLIMIT_RTTIME, required for rtkit
    struct rlimit rlim;
    memset(&rlim, 0, sizeof(rlim));
    rlim.rlim_cur = 10000ULL; // 10ms
    rlim.rlim_max = 200000ULL; // 200ms

    if ((setrlimit(RLIMIT_RTTIME, &rlim) < 0))
    {
        log_warn("Could not set RLIMIT_RTTIME (errno = %d)", errno);
    }

    // Prerequisite: SCHED_RR and SCHED_RESET_ON_FORK need to be set.
    struct sched_param p;
    memset(&p, 0, sizeof(p));

    p.sched_priority = 3;
    if (sched_setscheduler(0, SCHED_RR|SCHED_RESET_ON_FORK, &p) < 0 && errno == EPERM)
    {
#if defined(USE_RTKIT)
        DBusError error;
        DBusConnection* bus = nullptr;
        int result = 0;

        dbus_error_init(&error);
        if (!(bus = dbus_bus_get(DBUS_BUS_SYSTEM, &error)))
        {
            log_warn("Could not connect to system bus: %s", error.message);
        }
        else if ((result = rtkit_make_realtime(bus, 0, p.sched_priority)) < 0)
        {
            log_warn("rtkit could not make real-time: %s", strerror(-result));
        }

        if (bus != nullptr)
        {
            dbus_connection_unref(bus);
        }
#else
        log_warn("No permission to make real-time");
#endif // defined(USE_RTKIT)
    }

    // Set up signal handling for SIGXCPU
    struct sigaction action;
    action.sa_flags = SA_SIGINFO;     
    action.sa_sigaction = HandleXCPU_;
    sigaction(SIGXCPU, &action, NULL);

    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGXCPU);
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
#endif // 0

#if defined(USE_RTKIT)
    DBusError error;
    DBusConnection* bus = nullptr;
    int result = 0;

    dbus_error_init(&error);
    if (!(bus = dbus_bus_get(DBUS_BUS_SYSTEM, &error)))
    {
        log_warn("Could not connect to system bus: %s", error.message);
    }
    else
    {
        int minNiceLevel = 0;
        constexpr int ERROR_BUFFER_SIZE = 1024;
        char tmpBuf[ERROR_BUFFER_SIZE];
        if ((result = rtkit_get_min_nice_level(bus, &minNiceLevel)) < 0)
        {
#if (_POSIX_C_SOURCE >= 200112L) && !_GNU_SOURCE
            auto rv = strerror_r(-result, tmpBuf, ERROR_BUFFER_SIZE);
            if (rv != 0)
            {
                strncpy(tmpBuf, "(null)", 7);
            }
            log_warn("rtkit could not get minimum nice level: %s", tmpBuf);
#else
            auto ptr = strerror_r(-result, tmpBuf, ERROR_BUFFER_SIZE);
            if (ptr == nullptr)
            {
                strncpy(tmpBuf, "(null)", 7);
            }
            else
            {
                memmove(tmpBuf, ptr, strlen(ptr) + 1);
            }
            log_warn("rtkit could not get minimum nice level: %s", tmpBuf);
#endif // (_POSIX_C_SOURCE >= 200112L) && !_GNU_SOURCE
        }
        else if ((result = rtkit_make_high_priority(bus, 0, minNiceLevel)) < 0)
        {
#if (_POSIX_C_SOURCE >= 200112L) && !_GNU_SOURCE
            auto rv = strerror_r(-result, tmpBuf, ERROR_BUFFER_SIZE);
            if (rv != 0)
            {
                strncpy(tmpBuf, "(null)", 7);
            }
            log_warn("rtkit could not make high priority: %s", tmpBuf);
#else
            auto ptr = strerror_r(-result, tmpBuf, ERROR_BUFFER_SIZE);
            if (ptr == nullptr)
            {
                strncpy(tmpBuf, "(null)", 7);
            }
            else
            {
                memmove(tmpBuf, ptr, strlen(ptr) + 1);
            }
            log_warn("rtkit could not make high priority: %s", tmpBuf);
#endif // (_POSIX_C_SOURCE >= 200112L) && !_GNU_SOURCE
        }
    }
    
    if (bus != nullptr)
    {
        dbus_connection_unref(bus);
    }
#endif // defined(USE_RTKIT)
}

void PulseAudioDevice::startRealTimeWork()
{
    sleepFallback_ = false;
    if (clock_gettime(CLOCK_MONOTONIC, &ts_) == -1)
    {
        sleepFallback_ = true;
    }

    startTime_ = std::chrono::steady_clock::now();
}

void PulseAudioDevice::stopRealTimeWork(bool fastMode)
{
    constexpr int64_t NSEC_TO_SEC = 1000000000;

    if (sleepFallback_)
    {
        // Fallback to simple sleep.
        IAudioDevice::stopRealTimeWork(fastMode);
        return;
    }

    int64_t latency = PULSE_TARGET_LATENCY_US;
    latency >>= fastMode ? 1 : 0;

    // Skip wait if we spent too much time last time
    int64_t nsec = latency * 1000 - extraTimeNs_;
    if (nsec <= 0)
    {
        extraTimeNs_ = 0;
        return;
    }
    //log_info("wait %" PRIu64 " ns", (uint64_t)nsec);
    struct timespec ts2;
    ts2.tv_nsec = nsec % NSEC_TO_SEC;
    ts2.tv_sec = nsec / NSEC_TO_SEC;
    ts_ = timespec_add(ts_, ts2);

    int rv = 0;
    while ((rv = sem_clockwait(&sem_, CLOCK_MONOTONIC, &ts_)) == -1 && errno == EINTR)
    {
        // empty
    }

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime_).count() - nsec;
    extraTimeNs_ = std::max((int64_t)0, (int64_t)duration); // cap extra time to >= 0.

    if (rv == -1 && errno != ETIMEDOUT)
    {
        // Fallback to simple sleep.
        sleepFallback_ = true;
        IAudioDevice::stopRealTimeWork();
    }

#if 0
    MustStopWork_ = false;
#endif // 0
}

void PulseAudioDevice::clearHelperRealTime()
{
    IAudioDevice::clearHelperRealTime();
}

// Disabled for now as thread-local variables are apparently not RT-safe.
#if 0
bool PulseAudioDevice::mustStopWork() FREEDV_NONBLOCKING
{
    return MustStopWork_;
}
#endif // 0
 
void PulseAudioDevice::StreamReadCallback_(pa_stream *s, size_t length, void *userdata)
{
    const void* data = nullptr;
    PulseAudioDevice* thisObj = static_cast<PulseAudioDevice*>(userdata);

    do
    {
        pa_stream_peek(s, &data, &length);
        if (!data || length == 0) 
        {
            return;
        }

        if (thisObj->onAudioDataFunction)
        {
            thisObj->onAudioDataFunction(*thisObj, const_cast<void*>(data), length / thisObj->getNumChannels() / sizeof(short), thisObj->onAudioDataState);
        }
        pa_stream_drop(s);
    } while (pa_stream_readable_size(s) > 0);
    sem_post(&thisObj->sem_);
}

void PulseAudioDevice::StreamWriteCallback_(pa_stream *s, size_t length, void *userdata)
{
    PulseAudioDevice* thisObj = static_cast<PulseAudioDevice*>(userdata);
    
    while ((length = pa_stream_writable_size(s)) > 0 && (length != (uint32_t)-1))
    {
        size_t writeLength = length;

        // Note that PulseAudio gives us lengths in terms of number of bytes, not samples.
        short *data = nullptr;
        if (pa_stream_begin_write(s, (void**)&data, &writeLength) != 0 || data == nullptr)
        {
            // PulseAudio didn't give us a block to write to for some reason.
            log_warn("No memory block available to write audio to, will try again later");
            break;
        }

        assert(data != nullptr);
        memset(data, 0, writeLength);
        int numSamples = writeLength / sizeof(short);

        if (thisObj->onAudioDataFunction)
        {
            thisObj->onAudioDataFunction(*thisObj, data, numSamples / thisObj->getNumChannels(), thisObj->onAudioDataState);
        } 

        if (pa_stream_write(s, &data[0], writeLength, NULL, 0LL, PA_SEEK_RELATIVE) != 0)
        {
            log_warn("Could not write audio to stream");
            break;
        }
    }
}

void PulseAudioDevice::StreamStateCallback_(pa_stream *p, void *userdata)
{
    PulseAudioDevice* thisObj = static_cast<PulseAudioDevice*>(userdata);

    // This method is used for termination to ensure that it's synchronous and 
    // does not accidentally refer to already freed memory.
    auto state = pa_stream_get_state(p);
    if (state == PA_STREAM_TERMINATED)
    {
        pa_threaded_mainloop_signal(thisObj->mainloop_, 0);
    }
    else if (state == PA_STREAM_READY)
    {
        // Streams aren't connected with PA_STREAM_DONT_MOVE, so that they
        // can be moved to another device later. That also lets the sound
        // server put one somewhere other than where it was asked to if that
        // isn't there (PipeWire plays to the default device, for instance).
        // Note it in the log when that happens.
        const char* actualName = pa_stream_get_device_name(p);
        if (actualName != nullptr && !thisObj->devName_.IsEmpty() &&
            thisObj->devName_ != wxString::FromUTF8(actualName))
        {
            log_info("Stream for %s was connected to %s instead", (const char*)thisObj->devName_.ToUTF8(), actualName);
        }
    }
    else if (state == PA_STREAM_FAILED)
    {
        // The stream's gone without being asked to, e.g. because the sound
        // server has. Nothing more will come of it, so say so, and don't
        // leave anything waiting for it to terminate.
        pa_threaded_mainloop_signal(thisObj->mainloop_, 0);

        auto context = pa_stream_get_context(p);
        std::string reason = (context != nullptr) ? pa_strerror(pa_context_errno(context)) : "unknown error";
        log_warn("Stream for %s failed: %s", (const char*)thisObj->devName_.ToUTF8(), reason.c_str());
        if (thisObj->onAudioErrorFunction)
        {
            thisObj->onAudioErrorFunction(
                *thisObj, std::string("Lost connection to ") + (const char*)thisObj->devName_.ToUTF8() + " (" + reason + ")",
                thisObj->onAudioErrorState);
        }
    }
}

void PulseAudioDevice::StreamUnderflowCallback_(pa_stream *, void *userdata)
{
    PulseAudioDevice* thisObj = static_cast<PulseAudioDevice*>(userdata);
    
    if (thisObj->onAudioUnderflowFunction) 
    {
        thisObj->onAudioUnderflowFunction(*thisObj, thisObj->onAudioUnderflowState);
    }
}

void PulseAudioDevice::StreamOverflowCallback_(pa_stream *, void *userdata)
{
    PulseAudioDevice* thisObj = static_cast<PulseAudioDevice*>(userdata);
    
    if (thisObj->onAudioOverflowFunction) 
    {
        thisObj->onAudioOverflowFunction(*thisObj, thisObj->onAudioOverflowState);
    }
}

void PulseAudioDevice::StreamMovedCallback_(pa_stream *p, void *userdata)
{
    auto newDevName = pa_stream_get_device_name(p);
    PulseAudioDevice* thisObj = static_cast<PulseAudioDevice*>(userdata);

    log_info("Stream named %s has been moved to %s", (const char*)thisObj->devName_.ToUTF8(), newDevName);
    
    thisObj->devName_ = newDevName;
    
    if (thisObj->onAudioDeviceChangedFunction) 
    {
        thisObj->onAudioDeviceChangedFunction(*thisObj, (const char*)thisObj->devName_.ToUTF8(), thisObj->onAudioDeviceChangedState);
    }
}

#if 0
void PulseAudioDevice::StreamLatencyCallback_(pa_stream *p, void *userdata)
{
    PulseAudioDevice* thisObj = static_cast<PulseAudioDevice*>(userdata);
    pa_usec_t latency;
    int isNeg;

    pa_stream_get_latency(p, &latency, &isNeg);

    fprintf(stderr, "Current target buffer size for %s: %d\n", (const char*)thisObj->devName_.ToUTF8(), thisObj->targetOutputPendingLength_);
}
#endif // 0

void PulseAudioDevice::HandleXCPU_(int, siginfo_t *, void *)
{
    // Notify thread that it has to stop work immediately and sleep.
    log_warn("Taking too much CPU handling real-time tasks, pausing for a bit");
#if 0
    MustStopWork_ = true;
#endif // 0
}
