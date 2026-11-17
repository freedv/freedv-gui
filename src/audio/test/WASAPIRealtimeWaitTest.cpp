#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include "../WASAPIRealtimeWait.h"
#include <timeapi.h>
#include "../../test/UnitTestCommon.h"

// The wait between audio thread cycles on Windows (PR #1617): it has to
// last about as long as asked, end early when the device is stopped, and
// block rather than spin while it waits.

namespace {

constexpr int64_t HNS_PER_MS = 10000;

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// CPU time (user + kernel) this thread has used, in milliseconds.
double threadCpuMs()
{
    FILETIME creation, exitTime, kernel, user;
    GetThreadTimes(GetCurrentThread(), &creation, &exitTime, &kernel, &user);
    auto toHns = [](FILETIME const& ft) {
        return ((int64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    };
    return (double)(toHns(kernel) + toHns(user)) / HNS_PER_MS;
}

struct Handles
{
    HANDLE semaphore = CreateSemaphore(nullptr, 0, 1, nullptr);
    HANDLE timer = CreateRealtimeWaitTimer();

    ~Handles()
    {
        if (semaphore != nullptr) CloseHandle(semaphore);
        if (timer != nullptr) CloseHandle(timer);
    }

    bool ok() const
    {
        if (semaphore == nullptr || timer == nullptr)
        {
            std::cout << "[could not create the semaphore or the timer (err = " << GetLastError() << ")] ";
            return false;
        }
        return true;
    }
};

bool timerIsCreated()
{
    Handles handles;
    return handles.ok();
}

// The audio threads wait 5 ms (10 ms when not in fast mode) a cycle.
bool waitLastsAboutAsLongAsAsked()
{
    Handles handles;
    if (!handles.ok()) return false;

    timeBeginPeriod(1);
    std::vector<double> waits;
    bool result = true;
    for (int i = 0; i < 50; i++)
    {
        auto start = Clock::now();
        auto waitResult = RealtimeWaitForPeriod(handles.semaphore, handles.timer, 5 * HNS_PER_MS);
        waits.push_back(msSince(start));
        if (waitResult != RealtimeWaitResult::PeriodElapsed)
        {
            std::cout << "[wait " << i << " didn't end with the timer] ";
            result = false;
        }
    }
    timeEndPeriod(1);

    std::sort(waits.begin(), waits.end());
    double shortest = waits.front();
    double median = waits[waits.size() / 2];
    if (shortest < 4.0 || median > 10.0)
    {
        std::cout << "[5 ms waits: shortest " << shortest << " ms, median " << median << " ms] ";
        result = false;
    }
    return result;
}

// #1617: spinning through the end of each wait used 1-2 ms of CPU every
// 5-10 ms cycle on each audio thread. Blocking uses next to none.
bool waitBlocksRatherThanSpins()
{
    Handles handles;
    if (!handles.ok()) return false;

    timeBeginPeriod(1);
    auto start = Clock::now();
    double cpuBefore = threadCpuMs();
    for (int i = 0; i < 200; i++)
    {
        RealtimeWaitForPeriod(handles.semaphore, handles.timer, 5 * HNS_PER_MS);
    }
    double cpuUsed = threadCpuMs() - cpuBefore;
    double wall = msSince(start);
    timeEndPeriod(1);

    // Thread CPU time is only counted in scheduler ticks (~15.6 ms), so
    // this is coarse, but a spinning wait would be well over it.
    if (cpuUsed > wall * 0.10)
    {
        std::cout << "[used " << cpuUsed << " ms of CPU over " << wall << " ms of waiting] ";
        return false;
    }
    return true;
}

bool stopEndsTheWaitEarly()
{
    Handles handles;
    if (!handles.ok()) return false;

    std::thread stopper([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ReleaseSemaphore(handles.semaphore, 1, nullptr);
    });

    auto start = Clock::now();
    auto waitResult = RealtimeWaitForPeriod(handles.semaphore, handles.timer, 2000 * HNS_PER_MS);
    double waited = msSince(start);
    stopper.join();

    bool result = true;
    if (waitResult != RealtimeWaitResult::Stopped)
    {
        std::cout << "[a 2 s wait stopped after 50 ms didn't report being stopped] ";
        result = false;
    }
    if (waited > 1000.0)
    {
        std::cout << "[a 2 s wait stopped after 50 ms lasted " << waited << " ms] ";
        result = false;
    }
    return result;
}

// A stop that comes while the thread is still processing (before it starts
// waiting) still ends the next wait straight away.
bool stopBeforeTheWaitEndsItAtOnce()
{
    Handles handles;
    if (!handles.ok()) return false;

    ReleaseSemaphore(handles.semaphore, 1, nullptr);
    auto start = Clock::now();
    auto waitResult = RealtimeWaitForPeriod(handles.semaphore, handles.timer, 2000 * HNS_PER_MS);
    double waited = msSince(start);

    if (waitResult != RealtimeWaitResult::Stopped || waited > 500.0)
    {
        std::cout << "[wait after an earlier stop lasted " << waited << " ms] ";
        return false;
    }
    return true;
}

}

int main()
{
    executeTestCase("timerIsCreated", timerIsCreated);
    executeTestCase("waitLastsAboutAsLongAsAsked", waitLastsAboutAsLongAsAsked);
    executeTestCase("waitBlocksRatherThanSpins", waitBlocksRatherThanSpins);
    executeTestCase("stopEndsTheWaitEarly", stopEndsTheWaitEarly);
    executeTestCase("stopBeforeTheWaitEndsItAtOnce", stopBeforeTheWaitEndsItAtOnce);
    return testResult();
}
