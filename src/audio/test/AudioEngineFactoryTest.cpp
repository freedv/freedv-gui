#include <atomic>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <wx/init.h>

#include "../AudioEngineFactory.h"
#include "../../test/UnitTestCommon.h"

namespace {

// FreeDV asks for the engine from more than one thread: the GUI does when it
// fills in the Options dialog, a Start worker does when it sets the session
// up, and an engine's own error handler can while either is going. Every
// answer has to be the same engine, and making it has to be a single step --
// otherwise two threads can each build one, leaving two engines holding
// connections to the sound server open and half the session's devices on
// each. Built with ThreadSanitizer, this fails if the engine is ever made
// while another thread is looking at it.
bool enginesAskedForAtTheSameTimeAreOneEngine()
{
    const int kAskers = 8;

    std::shared_ptr<IAudioEngine> answers[kAskers];
    std::atomic<int> waitingToAsk{0};
    std::atomic<bool> askNow{false};
    std::vector<std::thread> askers;

    for (int asker = 0; asker < kAskers; asker++)
    {
        askers.push_back(std::thread([&, asker]() {
            waitingToAsk.fetch_add(1);
            while (!askNow.load())
            {
                std::this_thread::yield();
            }
            answers[asker] = AudioEngineFactory::GetAudioEngine();
        }));
    }

    while (waitingToAsk.load() < kAskers)
    {
        std::this_thread::yield();
    }
    askNow.store(true);

    for (auto& asker : askers)
    {
        asker.join();
    }

    for (int asker = 1; asker < kAskers; asker++)
    {
        if (answers[asker] != answers[0])
        {
            std::cout << "[asker " << asker << " was given a different engine] ";
            return false;
        }
    }

    if (answers[0] == nullptr)
    {
        std::cout << "[no engine] ";
        return false;
    }
    return true;
}

// Asking more than once gets the one engine rather than a new one each time:
// the devices the application is already using belong to it.
bool askingAgainGetsTheSameEngine()
{
    auto first = AudioEngineFactory::GetAudioEngine();
    auto again = AudioEngineFactory::GetAudioEngine();

    if (first != again || first == nullptr)
    {
        std::cout << "[a different engine on the second ask] ";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk())
    {
        std::cout << "Could not initialize wxWidgets" << std::endl;
        return -1;
    }

    executeTestCase("enginesAskedForAtTheSameTimeAreOneEngine", enginesAskedForAtTheSameTimeAreOneEngine);
    executeTestCase("askingAgainGetsTheSameEngine", askingAgainGetsTheSameEngine);

    return testResult();
}
