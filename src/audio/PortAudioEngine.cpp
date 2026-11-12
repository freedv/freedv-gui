//=========================================================================
// Name:            PortAudioEngine.cpp
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

#include <sstream>
#include <wx/string.h>
#include "portaudio.h"
#include "PortAudioDevice.h"
#include "PortAudioEngine.h"

PortAudioEngine::PortAudioEngine()
    : initialized_(false)
{
    portAudioLibrary_ = std::make_shared<PortAudioInterface>();
}

PortAudioEngine::~PortAudioEngine()
{
    if (initialized_)
    {
        stop();
    }
}

void PortAudioEngine::start()
{
    auto error = portAudioLibrary_->Initialize().get();
    if (error != paNoError)
    {
        std::string errText = portAudioLibrary_->GetErrorText(error).get();
        if (error == paUnanticipatedHostError)
        {
            std::stringstream ss;
            auto errInfo = portAudioLibrary_->GetLastHostErrorInfo().get();
            ss << " (error code " << std::hex << errInfo->errorCode << " - " << std::string(errInfo->errorText) << ")";
            errText += ss.str();
        }

        reportEngineError(errText);
    }
    else
    {
        initialized_ = true;
    }
}

void PortAudioEngine::stop()
{
    portAudioLibrary_->Terminate().wait();
    initialized_ = false;
}

std::vector<AudioDeviceSpecification> PortAudioEngine::getAudioDeviceList(AudioDirection direction)
{
    int numDevices = portAudioLibrary_->GetDeviceCount().get();
    std::vector<AudioDeviceSpecification> result;
    
    for (int index = 0; index < numDevices; index++)
    {
        const PaDeviceInfo *deviceInfo = portAudioLibrary_->GetDeviceInfo(index).get();
        
        std::string hostApiName = portAudioLibrary_->GetHostApiInfo(deviceInfo->hostApi).get()->name;
        if (hostApiName.find("DirectSound") != std::string::npos ||
            hostApiName.find("surround") != std::string::npos ||
            //hostApiName.find("Windows WASAPI") != std::string::npos ||
            hostApiName.find("MME") != std::string::npos ||
            hostApiName.find("Windows WDM-KS") != std::string::npos)
        {
            // Skip non-MME/Core Audio devices as that was the old behavior.
            // Note: DirectSound in particular hasn't been shown 
            //       due to poor real-time performance.
            continue;
        }
        
        if ((direction == AUDIO_ENGINE_IN && deviceInfo->maxInputChannels > 0) || 
            (direction == AUDIO_ENGINE_OUT && deviceInfo->maxOutputChannels > 0))
        {
            // Detect the minimum number of channels available as PortAudio doesn't
            // provide this info. This should in theory be 1 but at least one device 
            // (Focusrite Scarlett) will not accept anything less than 4 channels 
            // on Windows.
            PaStreamParameters streamParameters;
            streamParameters.device = index;
            streamParameters.channelCount = 1; 
            streamParameters.sampleFormat = paInt16;
            streamParameters.suggestedLatency = portAudioLibrary_->GetDeviceInfo(index).get()->defaultLowInputLatency;
            streamParameters.hostApiSpecificStreamInfo = NULL;

            // On Linux, the below logic causes the device lookup process to take MUCH	
            // longer than it does on other platforms, mainly because of the special devices	
            // it provides to PortAudio. For these, we're just going to assume the minimum	
            // valid channels is 1.            
            int maxChannels = direction == AUDIO_ENGINE_IN ? deviceInfo->maxInputChannels : deviceInfo->maxOutputChannels;
            bool isDeviceWithKnownMinimum = IsDeviceWhitelisted_(deviceInfo->name);
            if (!isDeviceWithKnownMinimum)
            {
                while (streamParameters.channelCount < maxChannels)
                {
                    PaError err = portAudioLibrary_->IsFormatSupported(
                        direction == AUDIO_ENGINE_IN ? &streamParameters : NULL, 
                        direction == AUDIO_ENGINE_OUT ? &streamParameters : NULL, 
                        deviceInfo->defaultSampleRate).get();
                
                    if (err == paFormatIsSupported)
                    {
                        break;
                    }

                    streamParameters.channelCount++;
                }
            }

            // Add information about this device to the result array.
            AudioDeviceSpecification device;
            device.deviceId = index;
            device.name = wxString::FromUTF8(deviceInfo->name);
            device.cardName = device.name;
            device.apiName = hostApiName;

            // For "whitelisted" devices, also assume channel counts
            if (IsDeviceWhitelisted_(deviceInfo->name))
            {
                device.maxChannels = 2;
            }
            else
            {
                device.minChannels = streamParameters.channelCount;
                device.maxChannels = 
                    direction == AUDIO_ENGINE_IN ? deviceInfo->maxInputChannels : deviceInfo->maxOutputChannels;
            }
            device.defaultSampleRate = deviceInfo->defaultSampleRate;
            
            result.push_back(device);
        }
    }
    
    return result;
}

std::vector<int> PortAudioEngine::getSupportedSampleRates(wxString deviceName, AudioDirection direction)
{
    std::vector<int> result;
    auto devInfo = getAudioDeviceList(direction);
    
    for (auto& device : devInfo)
    {
        if (device.name.IsSameAs(deviceName))
        {
            PaStreamParameters streamParameters;
            
            streamParameters.device = device.deviceId;
            streamParameters.channelCount = device.minChannels;
            streamParameters.sampleFormat = paInt16;
            streamParameters.suggestedLatency = portAudioLibrary_->GetDeviceInfo(device.deviceId).get()->defaultLowInputLatency;
            streamParameters.hostApiSpecificStreamInfo = NULL;
            
            int rateIndex = 0;
            while (IAudioEngine::StandardSampleRates[rateIndex] != -1)
            {
                PaError err = paFormatIsSupported;

                bool isDeviceWithKnownMinimum = IsDeviceWhitelisted_(deviceName);
                if (!isDeviceWithKnownMinimum)
                {
                    err = portAudioLibrary_->IsFormatSupported(
                        direction == AUDIO_ENGINE_IN ? &streamParameters : NULL, 
                        direction == AUDIO_ENGINE_OUT ? &streamParameters : NULL, 
                        IAudioEngine::StandardSampleRates[rateIndex]).get();
                }

                if (err == paFormatIsSupported)
                {
                    result.push_back(IAudioEngine::StandardSampleRates[rateIndex]);
                }
                
                rateIndex++;
            }

            // If we can't find a supported sample rate, just assume that the
            // default sample rate is supported. If that can't actually be used,
            // we can deal with it later.
            if (result.size() == 0)
            {
                result.push_back(device.defaultSampleRate);
            }
            break;
        }
    }
    
    return result;
}

AudioDeviceSpecification PortAudioEngine::getDefaultAudioDevice(AudioDirection direction)
{
    auto devices = getAudioDeviceList(direction);
    PaDeviceIndex defaultDeviceIndex = 
        (direction == AUDIO_ENGINE_IN ? 
            portAudioLibrary_->GetDefaultInputDevice() : 
            portAudioLibrary_->GetDefaultOutputDevice()).get();
    
    if (defaultDeviceIndex != paNoDevice)
    {
        for (auto& device : devices)
        {
            if (device.deviceId == defaultDeviceIndex)
            {
                return device;
            }
        }
    }
    
    return AudioDeviceSpecification::GetInvalidDevice();
}

std::shared_ptr<IAudioDevice> PortAudioEngine::getAudioDevice(wxString deviceName, AudioDirection direction, int sampleRate, int numChannels)
{
    auto deviceList = getAudioDeviceList(direction);
    
    auto supportedSampleRates = getSupportedSampleRates(deviceName, direction);
    bool found = false;
    for (auto& rate : supportedSampleRates)
    {
        if (rate == sampleRate)
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        // Zero out the input sample rate. The device object will use the default sample rate
        // instead.
        sampleRate = 0;
    }
    
    for (auto& dev : deviceList)
    {
        if (dev.name.IsSameAs(deviceName))
        {
            // Ensure that the passed-in number of channels is within the allowed range.
            numChannels = std::max(numChannels, dev.minChannels);
            numChannels = std::min(numChannels, dev.maxChannels);

            // Create device object.
            auto devObj = new PortAudioDevice(portAudioLibrary_, dev.deviceId, direction, sampleRate, numChannels);
            return std::shared_ptr<IAudioDevice>(devObj);
        }
    }

    return nullptr;
}

bool PortAudioEngine::IsDeviceWhitelisted_(const char* devName)
{
    return
        !strcmp(devName, "sysdefault") ||            
        !strcmp(devName, "front") ||            
        !strcmp(devName, "surround") ||            
        !strcmp(devName, "samplerate") ||            
        !strcmp(devName, "speexrate") ||            
        !strcmp(devName, "pulse") ||            
        !strcmp(devName, "upmix") ||            
        !strcmp(devName, "vdownmix") ||            
        !strcmp(devName, "dmix") ||            
        !strcmp(devName, "default");
}
