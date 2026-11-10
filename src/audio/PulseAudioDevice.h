//=========================================================================
// Name:            PulseAudioDevice.h
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

#ifndef PULSE_AUDIO_DEVICE_H
#define PULSE_AUDIO_DEVICE_H

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <wx/string.h>
#include <pulse/pulseaudio.h>
#include <semaphore.h>

#include "IAudioEngine.h"
#include "IAudioDevice.h"

class PulseAudioDevice : public IAudioDevice
{
public:
    virtual ~PulseAudioDevice();
    
    virtual int getNumChannels() FREEDV_NONBLOCKING override { return numChannels_; }
    virtual int getSampleRate() const FREEDV_NONBLOCKING override { return sampleRate_; }
    
    virtual void start() override;
    virtual void stop() override;

    virtual bool isRunning() override;
    
    virtual int64_t getLatencyInMicroseconds() override;

    // Configures current thread for real-time priority. This should be
    // called from the thread that will be operating on received audio.
    virtual void setHelperRealTime() override;

    // Lets audio system know that we're starting work on received audio.
    virtual void startRealTimeWork() override;

    // Lets audio system know that we're done with the work on the received
    // audio.
    virtual void stopRealTimeWork(bool fastMode = false) override;

    // Reverts real-time priority for current thread.
    virtual void clearHelperRealTime() override;

#if 0
    // Returns true if real-time thread MUST sleep ASAP. Failure to do so
    // may result in SIGKILL being sent to the process by the kernel.
    // XXX - disabled as current implementation uses thread_local (not RT safe).
    virtual bool mustStopWork() FREEDV_NONBLOCKING override;
#endif // 0
 
protected:
    // PulseAudioDevice cannot be created directly, only via PulseAudioEngine.
    friend class PulseAudioEngine;

    // Lets the unit tests create one directly, and call the callbacks that
    // PulseAudio calls on a stream's thread.
    friend struct PulseAudioDeviceTestAccess;

    PulseAudioDevice(wxString const& devName, IAudioEngine::AudioDirection direction, int sampleRate, int numChannels);
    
private:
    std::mutex objLock_;

    // Each device has its own connection (main loop and context) rather than sharing the
    // engine's, created in start() and destroyed in stop(). With PipeWire, streams that
    // share a connection can stall: e.g. when a loopback connects a sink FreeDV plays to
    // with the sink whose monitor it records, pipewire-pulse stopped delivering the
    // recorded audio for seconds to minutes at a time.
    pa_context* context_ = nullptr;
    pa_threaded_mainloop* mainloop_ = nullptr;
    pa_stream* stream_ = nullptr;

    // Whether the stream was actually connected, and whether there's a
    // semaphore to pace work to it. Both are set up by a successful start()
    // and torn down by stop(); a start() that fails part way through leaves
    // one or the other missing, and nothing else may use it.
    std::atomic<bool> streamConnected_{false};
    std::atomic<bool> semReady_{false};

    wxString devName_;
    IAudioEngine::AudioDirection direction_;
    int sampleRate_;
    int numChannels_;
    
    thread_local static bool MustStopWork_;

    sem_t sem_;
    struct timespec ts_ = {};
    bool sleepFallback_ = true;

    int64_t extraTimeNs_;
    std::chrono::time_point<std::chrono::steady_clock> startTime_;

    void stopImpl_();

    static void StreamReadCallback_(pa_stream *s, size_t length, void *userdata);
    static void StreamWriteCallback_(pa_stream *s, size_t length, void *userdata);
    static void StreamUnderflowCallback_(pa_stream *p, void *userdata);
    static void StreamOverflowCallback_(pa_stream *p, void *userdata);
    static void StreamMovedCallback_(pa_stream *p, void *userdata);
    static void StreamStateCallback_(pa_stream *p, void *userdata);
#if 0
    static void StreamLatencyCallback_(pa_stream *p, void *userdata);
#endif // 0

    static void HandleXCPU_(int signum, siginfo_t *info, void *extra);
};

#endif // PULSE_AUDIO_DEVICE_H
