//=========================================================================
// Name:            WASAPIRealtimeWait.h
// Purpose:         The wait between audio thread cycles on Windows: a
//                  high-resolution one-shot timer, ended early by a
//                  semaphore when the device is stopped.
//
// Authors:         Mooneer Salem
// License:
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

#ifndef WASAPI_REALTIME_WAIT_H
#define WASAPI_REALTIME_WAIT_H

#include <cstdint>
#include <windows.h>

// Creates the timer RealtimeWaitForPeriod() waits on. The high-resolution
// flag requires Windows 10 1803+; falls back to a regular (still usable,
// just lower-resolution) waitable timer if unavailable. Returns nullptr if
// neither can be created.
inline HANDLE CreateRealtimeWaitTimer()
{
    HANDLE timer = CreateWaitableTimerEx(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer == nullptr)
    {
        timer = CreateWaitableTimerEx(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }
    return timer;
}

enum class RealtimeWaitResult
{
    PeriodElapsed,
    Stopped,
    Failed,
};

// Waits hns (100ns units) on timer, or until stopSemaphore is released.
// This blocks for the whole wait, so it isn't limited to
// WaitForSingleObject's millisecond-granular timeout and uses no CPU while
// waiting: spinning through the last timer tick instead burned 1-2 ms of
// CPU per cycle on each audio thread (PR #1617).
inline RealtimeWaitResult RealtimeWaitForPeriod(HANDLE stopSemaphore, HANDLE timer, int64_t hns)
{
    LARGE_INTEGER dueTime;
    dueTime.QuadPart = -hns; // negative = relative time
    SetWaitableTimer(timer, &dueTime, 0, nullptr, nullptr, FALSE);

    HANDLE waitHandles[2] = { stopSemaphore, timer };
    switch (WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE))
    {
        case WAIT_OBJECT_0:
            return RealtimeWaitResult::Stopped;
        case WAIT_OBJECT_0 + 1:
            return RealtimeWaitResult::PeriodElapsed;
        default:
            return RealtimeWaitResult::Failed;
    }
}

#endif // WASAPI_REALTIME_WAIT_H
