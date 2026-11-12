//=========================================================================
// Name:            IAudioEngine.cpp
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

#include "IAudioEngine.h"

int IAudioEngine::StandardSampleRates[] =
{
    16000,    22050,
    24000,    32000,
    44100,    48000,
    88200,    96000,
    176400,   192000,
    352800,   384000,
    -1          // negative terminated  list
};

thread_local IAudioEngine* IAudioEngine::reportingEngine_ = nullptr;
thread_local int IAudioEngine::reportingDepth_ = 0;

void IAudioEngine::setOnEngineError(AudioErrorCallbackFn fn, void* state)
{
    std::unique_lock<std::mutex> lk(errorCallbacksMutex_);

    // Swapping the pair under the lock is what keeps a report from reading
    // half of one registration and half of another, but the report makes its
    // call after letting go of the lock. Until the reports already under way
    // on other threads have returned, the old handler can still be running
    // with the old state -- the MainFrame being destroyed, say. A handler
    // replacing the registration from inside a report isn't waited for.
    int ownReports = (reportingEngine_ == this) ? reportingDepth_ : 0;
    errorReportsDone_.wait(lk, [&]() { return errorReportsInFlight_ <= ownReports; });

    onAudioErrorFunction = std::move(fn);
    onAudioErrorState = state;
}

void IAudioEngine::reportEngineError(std::string const& error)
{
    // The handler and the state go together. Taking them under the lock is
    // what keeps a thread reporting an error from calling a handler another
    // thread has already let go, or from handing a handler the state some
    // other registration installed. The call itself is made with the lock
    // off, so a handler is free to talk back to the engine.
    AudioErrorCallbackFn fn;
    void* state;
    {
        std::unique_lock<std::mutex> lk(errorCallbacksMutex_);
        fn = onAudioErrorFunction;
        state = onAudioErrorState;
        if (!fn) return;
        errorReportsInFlight_++;
    }

    IAudioEngine* outerEngine = reportingEngine_;
    int outerDepth = reportingDepth_;
    reportingDepth_ = (outerEngine == this) ? outerDepth + 1 : 1;
    reportingEngine_ = this;

    fn(*this, error, state);

    reportingEngine_ = outerEngine;
    reportingDepth_ = outerDepth;

    {
        std::unique_lock<std::mutex> lk(errorCallbacksMutex_);
        errorReportsInFlight_--;
    }
    errorReportsDone_.notify_all();
}
