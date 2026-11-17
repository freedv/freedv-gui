//=========================================================================
// Name:            AudioEngineFactory.h
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

#ifndef AUDIO_ENGINE_FACTORY_H
#define AUDIO_ENGINE_FACTORY_H

#include "IAudioEngine.h"

class AudioEngineFactory
{
public:
    static std::shared_ptr<IAudioEngine> GetAudioEngine();
    
private:
    AudioEngineFactory() = delete;
    AudioEngineFactory(const AudioEngineFactory&) = delete;
    ~AudioEngineFactory() = delete;
};

#endif // AUDIO_ENGINE_FACTORY_H