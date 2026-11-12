//=========================================================================
// Name:            IAudioEngine.h
// Purpose:         Defines the main interface to the selected audio engine.
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

#ifndef I_AUDIO_ENGINE_H
#define I_AUDIO_ENGINE_H

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include "AudioDeviceSpecification.h"

class IAudioDevice;

class IAudioEngine
{
public:
    typedef std::function<void(IAudioEngine&, std::string, void*)> AudioErrorCallbackFn;
    
    enum AudioDirection { AUDIO_ENGINE_IN, AUDIO_ENGINE_OUT };
    
    virtual ~IAudioEngine() = default;
    
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual std::vector<AudioDeviceSpecification> getAudioDeviceList(AudioDirection direction) = 0;
    virtual AudioDeviceSpecification getDefaultAudioDevice(AudioDirection direction) = 0;
    virtual std::shared_ptr<IAudioDevice> getAudioDevice(wxString deviceName, AudioDirection direction, int sampleRate, int numChannels) = 0;
    virtual std::vector<int> getSupportedSampleRates(wxString deviceName, AudioDirection direction) = 0;
    
    // Set error callback.
    // Callback must take the following parameters:
    //    1. Audio engine.
    //    2. String representing the error encountered.
    //    3. Pointer to user-provided state object (typically onAudioUnderflowState, defined below).
    // Safe to call while another thread is reporting to a previous one: the
    // application clears this on the UI thread (stopRxStream(), ~MainFrame())
    // while an On/Off worker may be inside start() on its own. It waits for
    // reports other threads are making to finish, so once it returns no call
    // to the previous handler is still running, and that handler's state can
    // be freed. (A handler may call it itself: the report it's in is the
    // only one it doesn't wait for.)
    void setOnEngineError(AudioErrorCallbackFn fn, void* state);

protected:
    static int StandardSampleRates[];

    // Reports an error to whatever is registered, if anything is. The handler
    // and the state it's handed are one registration, so they're read
    // together, under the lock that keeps a replacement on another thread
    // from leaving half of one and half of the other; the call itself is made
    // once the lock is off, so a handler is never run holding it. The report
    // counts as in flight until the handler returns, which is what
    // setOnEngineError() waits on.
    void reportEngineError(std::string const& error);

private:
    std::mutex errorCallbacksMutex_;
    std::condition_variable errorReportsDone_;
    int errorReportsInFlight_ = 0;
    AudioErrorCallbackFn onAudioErrorFunction;
    void* onAudioErrorState = nullptr;

    // The engine this thread is reporting an error from, and how many reports
    // deep, so that a handler replacing the registration doesn't wait for the
    // report it's being called from.
    static thread_local IAudioEngine* reportingEngine_;
    static thread_local int reportingDepth_;
};

#endif // AUDIO_ENGINE_H