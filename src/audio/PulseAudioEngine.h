//=========================================================================
// Name:            PulseAudioEngine.h
// Purpose:         Defines the interface to the PortAudio audio engine.
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

#ifndef PULSE_AUDIO_ENGINE_H
#define PULSE_AUDIO_ENGINE_H

#include <mutex>
#include <string>

#include <pulse/pulseaudio.h>
#include "IAudioEngine.h"

class PulseAudioEngine : public IAudioEngine
{
public:
    PulseAudioEngine();
    virtual ~PulseAudioEngine();

    virtual void start();
    virtual void stop();
    virtual std::vector<AudioDeviceSpecification> getAudioDeviceList(AudioDirection direction);
    virtual AudioDeviceSpecification getDefaultAudioDevice(AudioDirection direction);
    virtual std::shared_ptr<IAudioDevice> getAudioDevice(wxString deviceName, AudioDirection direction, int sampleRate, int numChannels);
    virtual std::vector<int> getSupportedSampleRates(wxString deviceName, AudioDirection direction);

    // Creates a threaded main loop and a context connected to the default server and, if
    // requestRealtime is set, raises the main loop thread's priority via rtkit where
    // available. On success, fills in mainloop and context and returns an empty string;
    // otherwise returns an error.
    static std::string CreateConnection(pa_threaded_mainloop** mainloop, pa_context** context, bool requestRealtime);

    // Disconnects and frees a connection made by CreateConnection().
    static void DestroyConnection(pa_threaded_mainloop* mainloop, pa_context* context);

private:
    bool initialized_ = false;

    // Used for device enumeration only; each PulseAudioDevice has its own
    // connection. The queries take it too, so that a stop() can't free the
    // main loop and context out from under one; it's recursive because they
    // call one another.
    pa_threaded_mainloop *mainloop_ = nullptr;
    pa_context* context_ = nullptr;
    std::recursive_mutex startStopMtx_;

    void stopImpl_();
};

#endif // PULSE_AUDIO_ENGINE_H
