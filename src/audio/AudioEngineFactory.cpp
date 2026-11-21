//=========================================================================
// Name:            AudioEngineFactory.cpp
// Purpose:         Creates AudioEngines for the current platform.
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

#include "AudioEngineFactory.h"
#if defined(AUDIO_ENGINE_NATIVE_ENABLE)
#if defined(__linux)
#include "PulseAudioEngine.h"
#elif __APPLE__
#include "MacAudioEngine.h"
#elif _WIN32
#include "WASAPIAudioEngine.h"
#else
#error No native audio support available for this platform
#endif // __linux || __APPLE__ || _WIN32
#else
#include "PortAudioEngine.h"
#endif // defined(AUDIO_ENGINE_NATIVE_ENABLE)

std::shared_ptr<IAudioEngine> AudioEngineFactory::GetAudioEngine()
{
    // Made once, the first time it's asked for. A local static does that in
    // a single step even when two threads ask at once -- the GUI does when
    // it fills in the Options dialog and a Start worker does when it sets
    // the session up -- whereas checking a shared pointer and then building
    // it lets both build one: the loser is freed while something is still
    // using it, and the sound server is left with two engines' connections.
    static std::shared_ptr<IAudioEngine> systemEngine = []() {
#if defined(AUDIO_ENGINE_NATIVE_ENABLE)
#if defined(__linux)
        return std::shared_ptr<IAudioEngine>(new PulseAudioEngine());
#elif defined(__APPLE__)
        return std::shared_ptr<IAudioEngine>(new MacAudioEngine());
#elif defined(_WIN32)
        return std::shared_ptr<IAudioEngine>(new WASAPIAudioEngine());
#endif // __linux || __APPLE__ || _WIN32
#else
        return std::shared_ptr<IAudioEngine>(new PortAudioEngine());
#endif // defined(AUDIO_ENGINE_NATIVE_ENABLE)
    }();

    return systemEngine;
}
