#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <semaphore.h>

#include <wx/init.h>

#include "../PulseAudioDevice.h"
#include "../PulseAudioEngine.h"
#include "../AudioDeviceSpecification.h"
#include "../../test/UnitTestCommon.h"
#include "../../util/logging/ulog.h"

// PulseAudioDevice is only created by PulseAudioEngine, and its stream
// callbacks are called by PulseAudio on a stream's thread. Both are needed
// to test either.
struct PulseAudioDeviceTestAccess
{
    static std::unique_ptr<PulseAudioDevice> create(
        wxString const& devName, IAudioEngine::AudioDirection direction, int sampleRate, int numChannels)
    {
        return std::unique_ptr<PulseAudioDevice>(
            new PulseAudioDevice(devName, direction, sampleRate, numChannels));
    }

    static void underflow(IAudioDevice& device)
    {
        PulseAudioDevice::StreamUnderflowCallback_(nullptr, &device);
    }

    static void overflow(IAudioDevice& device)
    {
        PulseAudioDevice::StreamOverflowCallback_(nullptr, &device);
    }

    // What a device currently considers itself connected to. A running
    // device's stream callbacks change this on PulseAudio's thread while
    // holding the mainloop lock, so it has to be read under that lock too.
    static wxString deviceName(const PulseAudioDevice& device)
    {
        if (device.mainloop_ == nullptr)
        {
            return device.devName_;
        }

        pa_threaded_mainloop_lock(device.mainloop_);
        wxString name = device.devName_;
        pa_threaded_mainloop_unlock(device.mainloop_);
        return name;
    }

    // What starting a device does, once its stream is connected, for the
    // thread that works to the stream's timing to wait on.
    static void prepareToPaceWork(PulseAudioDevice& device)
    {
        sem_init(&device.sem_, 0, 0);
    }

    static void finishedPacingWork(PulseAudioDevice& device)
    {
        sem_destroy(&device.sem_);
    }
};

namespace {

// A PulseAudio server address that nothing could be listening at. Connecting
// to one fails immediately, as it does when there's no sound server at all,
// and without PulseAudio trying to start one in the background.
const char* const NO_SOUND_SERVER = "unix:/nonexistent/freedv-test-pulse-socket";

const char* const MISSING_DEVICE = "FreeDVTestMissingDevice";

// Where this was told to find the sound server when it was run, if anywhere.
const char* const originalSoundServer = getenv("PULSE_SERVER") != nullptr ? strdup(getenv("PULSE_SERVER")) : nullptr;

void useNoSoundServer()
{
    setenv("PULSE_SERVER", NO_SOUND_SERVER, 1);
}

// The one that FreeDV itself would use, if there is one.
void useUsualSoundServer()
{
    if (originalSoundServer != nullptr)
    {
        setenv("PULSE_SERVER", originalSoundServer, 1);
    }
    else
    {
        unsetenv("PULSE_SERVER");
    }
}

// What PulseAudioEngine::start() and PulseAudioDevice::start() report when
// there's no sound server to connect to.
bool saysThereIsNoSoundServer(std::string const& error)
{
    return error.find("Could not connect PulseAudio context") != std::string::npos;
}

void printErrors(std::vector<std::string> const& errors)
{
    for (auto& error : errors)
    {
        std::cout << "[\"" << error << "\"] ";
    }
}

// What the engine or a device reported to the application. The state is
// where it's recorded, as it is for FreeDV's own handlers.
void recordEngineError(IAudioEngine&, std::string const& error, void* state)
{
    static_cast<std::vector<std::string>*>(state)->push_back(error);
}

void recordDeviceError(IAudioDevice&, std::string const& error, void* state)
{
    static_cast<std::vector<std::string>*>(state)->push_back(error);
}

// The engine says that it couldn't connect, rather than leaving the
// application to find out when nothing it asks for comes back.
bool engineStartSaysThereIsNoSoundServer()
{
    useNoSoundServer();

    PulseAudioEngine engine;
    std::vector<std::string> reported;
    engine.setOnEngineError(&recordEngineError, &reported);

    engine.start();
    engine.stop();

    if (reported.size() != 1 || !saysThereIsNoSoundServer(reported[0]))
    {
        std::cout << "[start() reported " << reported.size() << " error(s) ";
        printErrors(reported);
        std::cout << "] ";
        return false;
    }
    return true;
}

// With no sound server to ask, there's nothing to list, no default device,
// and nothing to open. FreeDV's settings and startup rely on all three being
// empty rather than on there being something there.
bool engineWithoutASoundServerHasNoDevices()
{
    useNoSoundServer();

    PulseAudioEngine engine;
    engine.start();

    std::vector<std::string> problems;
    for (auto direction : {IAudioEngine::AUDIO_ENGINE_IN, IAudioEngine::AUDIO_ENGINE_OUT})
    {
        auto devices = engine.getAudioDeviceList(direction);
        if (!devices.empty())
        {
            problems.push_back("listed " + std::to_string(devices.size()) + " device(s)");
        }
        if (engine.getDefaultAudioDevice(direction).isValid())
        {
            problems.push_back("returned a default device");
        }
        if (engine.getAudioDevice(MISSING_DEVICE, direction, 48000, 2) != nullptr)
        {
            problems.push_back("returned a device that isn't there");
        }
        if (!engine.getSupportedSampleRates(MISSING_DEVICE, direction).empty())
        {
            problems.push_back("returned sample rates for a device that isn't there");
        }
    }

    engine.stop();

    if (!problems.empty())
    {
        std::cout << "[";
        for (size_t index = 0; index < problems.size(); index++)
        {
            std::cout << (index ? ", " : "") << problems[index];
        }
        std::cout << "] ";
        return false;
    }
    return true;
}

// A device that couldn't connect reports the failure along with the name of
// the device it was for, which is what FreeDV shows the user.
bool deviceStartReportsTheFailureAndWhichDeviceItWas()
{
    useNoSoundServer();

    auto device = PulseAudioDeviceTestAccess::create(MISSING_DEVICE, IAudioEngine::AUDIO_ENGINE_OUT, 48000, 2);

    std::vector<std::string> reported;
    device->setOnAudioError(&recordDeviceError, &reported);

    device->start();
    device->stop();

    bool namedTheDevice = reported.size() == 1 && saysThereIsNoSoundServer(reported[0]) &&
        reported[0].find(std::string("(") + MISSING_DEVICE + ")") != std::string::npos;
    if (!namedTheDevice)
    {
        std::cout << "[start() reported " << reported.size() << " error(s) ";
        printErrors(reported);
        std::cout << "] ";
    }
    return namedTheDevice;
}

// What a device reports when it's started with a sound server running (the
// one that FreeDV would use), or nothing if there isn't one to try it with.
bool startAgainstSoundServer(const char* deviceName, IAudioEngine::AudioDirection direction, int sampleRate,
                             int numChannels, std::vector<std::string>& reported)
{
    useUsualSoundServer();

    auto device = PulseAudioDeviceTestAccess::create(deviceName, direction, sampleRate, numChannels);
    device->setOnAudioError(&recordDeviceError, &reported);
    device->start();
    device->stop();

    if (reported.size() == 1 && saysThereIsNoSoundServer(reported[0]))
    {
        std::cout << "[no sound server to try this with] ";
        reported.clear();
        useNoSoundServer();
        return false;
    }

    // It copes with having nobody to report to as well, and is left with
    // nothing running either way.
    auto unheard = PulseAudioDeviceTestAccess::create(deviceName, direction, sampleRate, numChannels);
    unheard->start();
    unheard->stop();

    useNoSoundServer();
    if (device->isRunning() || unheard->isRunning())
    {
        reported.push_back("still running after stop()");
    }
    return true;
}

bool reportedOnly(std::vector<std::string> const& reported, std::string const& expected)
{
    bool matches = reported.size() == 1 && reported[0] == expected;
    if (!matches)
    {
        std::cout << "[start() reported " << reported.size() << " error(s) ";
        printErrors(reported);
        std::cout << "] ";
    }
    return matches;
}

// The sound server won't make a stream of something that has no channels.
// That's reported rather than left for whatever uses the stream to find out.
bool streamThatCannotBeCreatedIsReported()
{
    std::vector<std::string> reported;
    if (!startAgainstSoundServer("FreeDVTestRadio", IAudioEngine::AUDIO_ENGINE_OUT, 48000, 0, reported))
    {
        return reported.empty();
    }
    return reportedOnly(reported, "Could not create PulseAudio stream for FreeDVTestRadio");
}

// A device whose start() failed is left with nothing to stop: FreeDV stops
// and frees every device it created when one of them fails, including the
// ones that never started. No error callback is registered here either, as
// the device must cope with that too.
bool failedDeviceStartLeavesNothingToStop()
{
    useNoSoundServer();

    auto device = PulseAudioDeviceTestAccess::create(MISSING_DEVICE, IAudioEngine::AUDIO_ENGINE_IN, 8000, 1);

    device->start();
    bool wasRunning = device->isRunning();
    device->stop();
    bool runningAfterStop = device->isRunning();

    // And again, as FreeDV's cleanup and the destructor both do.
    device->stop();

    if (wasRunning || runningAfterStop)
    {
        std::cout << "[running after start(): " << wasRunning << ", after stop(): " << runningAfterStop << "] ";
        return false;
    }
    return true;
}

// What a stream of one's own reported to the application.
struct XrunReport
{
    IAudioDevice* device = nullptr;
    void* state = nullptr;
    int numTimes = 0;
};

// Records what a device reported, in the state registered with its handler:
// how FreeDV's own handlers get to the counters they're for.
void recordXrun(IAudioDevice& device, void* state)
{
    XrunReport* report = static_cast<XrunReport*>(state);
    report->device = &device;
    report->state = state;
    report->numTimes++;
}

// An underflow or overflow is reported to the handler registered for that
// device, with the state registered with it, and not to any other device's:
// FreeDV counts these separately for what it sends to the radio and what it
// plays, so a callback reaching the wrong one counts against the wrong side.
bool xrunsAreReportedToTheDeviceTheyBelongTo()
{
    auto mic = PulseAudioDeviceTestAccess::create("FreeDVTestMic", IAudioEngine::AUDIO_ENGINE_IN, 48000, 1);
    auto radio = PulseAudioDeviceTestAccess::create("FreeDVTestRadio", IAudioEngine::AUDIO_ENGINE_OUT, 48000, 2);

    XrunReport micUnderflow, micOverflow, radioUnderflow, radioOverflow;

    mic->setOnAudioUnderflow(&recordXrun, &micUnderflow);
    mic->setOnAudioOverflow(&recordXrun, &micOverflow);
    radio->setOnAudioUnderflow(&recordXrun, &radioUnderflow);
    radio->setOnAudioOverflow(&recordXrun, &radioOverflow);

    PulseAudioDeviceTestAccess::underflow(*mic);
    PulseAudioDeviceTestAccess::overflow(*mic);

    auto expected = [](XrunReport const& report, IAudioDevice* device, void* state)
    {
        return report.numTimes == 1 && report.device == device && report.state == state;
    };

    bool reported =
        expected(micUnderflow, mic.get(), &micUnderflow) &&
        expected(micOverflow, mic.get(), &micOverflow) &&
        radioUnderflow.numTimes == 0 &&
        radioOverflow.numTimes == 0;

    if (!reported)
    {
        std::cout << "[mic underflow: " << micUnderflow.numTimes << ", mic overflow: " << micOverflow.numTimes
                  << ", radio underflow: " << radioUnderflow.numTimes << ", radio overflow: " << radioOverflow.numTimes
                  << "] ";
    }
    return reported;
}

// A device that nobody's asked to hear about underflows and overflows from
// (the microphone and speaker tests in Settings, for one) has nothing to call
// when they happen.
bool xrunsWithNobodyListeningAreIgnored()
{
    auto device = PulseAudioDeviceTestAccess::create("FreeDVTestSpeaker", IAudioEngine::AUDIO_ENGINE_OUT, 48000, 2);
    PulseAudioDeviceTestAccess::underflow(*device);
    PulseAudioDeviceTestAccess::overflow(*device);

    // The same once a handler has been taken away again.
    XrunReport report;
    device->setOnAudioUnderflow(&recordXrun, &report);
    device->setOnAudioUnderflow(nullptr, nullptr);
    PulseAudioDeviceTestAccess::underflow(*device);
    return report.numTimes == 0;
}

// How long one round of work takes, from starting it to having waited for
// the next: the work itself (if any), then stopRealTimeWork().
double millisecondsForRoundOfWork(PulseAudioDevice& device, int millisecondsWorking, bool fastMode = false)
{
    auto begin = std::chrono::steady_clock::now();
    device.startRealTimeWork();
    if (millisecondsWorking > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(millisecondsWorking));
    }
    device.stopRealTimeWork(fastMode);
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}

// The thread that feeds a device does a round of work every 20 ms, the
// latency FreeDV asks PulseAudio for (10 ms when it's catching up): however
// long the work took, the wait afterwards makes the round up to that.
bool workIsPacedToTheStreamsLatency()
{
    auto device = PulseAudioDeviceTestAccess::create("FreeDVTestRadio", IAudioEngine::AUDIO_ENGINE_OUT, 48000, 2);
    PulseAudioDeviceTestAccess::prepareToPaceWork(*device);

    double idle = millisecondsForRoundOfWork(*device, 0);
    double busy = millisecondsForRoundOfWork(*device, 5);
    double fast = millisecondsForRoundOfWork(*device, 0, true);

    PulseAudioDeviceTestAccess::finishedPacingWork(*device);

    // Generous at the top end: the machine may be busy with other things.
    // At the bottom end it's the rounds so far that are checked rather than
    // each by itself, as a wait that ends late is made up for by shortening
    // the one after.
    bool paced = idle >= 19 && idle < 200 && idle + busy >= 39 && busy < 200 && idle + busy + fast >= 49 && fast < 200;
    if (!paced)
    {
        std::cout << "[idle: " << idle << " ms, busy: " << busy << " ms, fast: " << fast << " ms] ";
    }
    return paced;
}

// Work that overruns isn't waited after, and what it overran by comes out
// of the next wait too, so that the thread catches up rather than falling
// further and further behind what the device needs.
bool overrunningWorkIsNotWaitedAfter()
{
    auto device = PulseAudioDeviceTestAccess::create("FreeDVTestRadio", IAudioEngine::AUDIO_ENGINE_OUT, 48000, 2);
    PulseAudioDeviceTestAccess::prepareToPaceWork(*device);

    double overrun = millisecondsForRoundOfWork(*device, 60);   // 40 ms over
    double catchingUp = millisecondsForRoundOfWork(*device, 0); // no wait at all
    double caughtUp = millisecondsForRoundOfWork(*device, 0);   // back to normal

    PulseAudioDeviceTestAccess::finishedPacingWork(*device);

    bool caught = overrun >= 60 && overrun < 75 + 100 && catchingUp < 10 && caughtUp >= 19 && caughtUp < 200;
    if (!caught)
    {
        std::cout << "[overrun: " << overrun << " ms, catching up: " << catchingUp << " ms, caught up: " << caughtUp
                  << " ms] ";
    }
    return caught;
}

// The same goes for work that's held up for seconds, as when the machine
// is too busy to run it: the next round isn't made to wait even longer.
bool workHeldUpForSecondsIsNotWaitedAfter()
{
    auto device = PulseAudioDeviceTestAccess::create("FreeDVTestRadio", IAudioEngine::AUDIO_ENGINE_OUT, 48000, 2);
    PulseAudioDeviceTestAccess::prepareToPaceWork(*device);

    millisecondsForRoundOfWork(*device, 2300);
    double catchingUp = millisecondsForRoundOfWork(*device, 0);
    double caughtUp = millisecondsForRoundOfWork(*device, 0);

    PulseAudioDeviceTestAccess::finishedPacingWork(*device);

    bool caught = catchingUp < 10 && caughtUp >= 19 && caughtUp < 200;
    if (!caught)
    {
        std::cout << "[catching up: " << catchingUp << " ms, caught up: " << caughtUp << " ms] ";
    }
    return caught;
}


// Runs one pactl command and returns what it printed, trimmed (empty if
// it failed). The cases below use the server's own tools to set up and
// take away the devices they need.
std::string runPactl(std::string const& arguments)
{
    std::string command = "pactl " + arguments + " 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) return "";

    std::string output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
    pclose(pipe);

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r' ||
                               output.back() == ' ' || output.back() == '\t'))
    {
        output.pop_back();
    }
    return output;
}

// Errors as devices and engines report them, kept where the reporting
// thread and the test thread can both get at them.
struct ErrorLog
{
    std::mutex mtx;
    std::vector<std::string> errors;

    bool contains(std::string const& piece)
    {
        std::unique_lock<std::mutex> lk(mtx);
        for (auto& error : errors)
            if (error.find(piece) != std::string::npos) return true;
        return false;
    }

    size_t size()
    {
        std::unique_lock<std::mutex> lk(mtx);
        return errors.size();
    }

    void print()
    {
        std::unique_lock<std::mutex> lk(mtx);
        for (auto& error : errors) std::cout << "\"" << error << "\" ";
    }
};

void logDeviceError(IAudioDevice&, std::string const& error, void* state)
{
    ErrorLog* log = static_cast<ErrorLog*>(state);
    std::unique_lock<std::mutex> lk(log->mtx);
    log->errors.push_back(error);
}

void logEngineError(IAudioEngine&, std::string const& error, void* state)
{
    ErrorLog* log = static_cast<ErrorLog*>(state);
    std::unique_lock<std::mutex> lk(log->mtx);
    log->errors.push_back(error);
}

// Whether the sound server FreeDV itself would use is there to try things
// against. Cases that need one say so and pass when there isn't.
bool soundServerReachable()
{
    useUsualSoundServer();

    PulseAudioEngine engine;
    ErrorLog heard;
    engine.setOnEngineError(&logEngineError, &heard);
    engine.start();
    bool reachable = heard.size() == 0;
    engine.stop();

    useNoSoundServer();
    return reachable;
}

bool waitUntilReported(ErrorLog& log, std::string const& piece, int milliseconds)
{
    for (int waited = 0; waited < milliseconds; waited += 100)
    {
        if (log.contains(piece)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return log.contains(piece);
}

// A throwaway sink, loaded and unloaded by the tests below (the module
// index pactl prints, empty if it couldn't be loaded).
std::string loadNullSink(std::string const& sinkName)
{
    return runPactl("load-module module-null-sink sink_name=" + sinkName);
}

void unloadModule(std::string const& moduleIndex)
{
    if (!moduleIndex.empty()) runPactl("unload-module " + moduleIndex);
}

// The server-side index of the sink with this name, or -1.
int sinkIndexNamed(std::string const& sinkName)
{
    std::istringstream lines(runPactl("list sinks short"));
    std::string line;
    while (std::getline(lines, line))
    {
        std::istringstream parts(line);
        std::string index, name;
        if (parts >> index >> name && name == sinkName) return std::stoi(index);
    }
    return -1;
}

// The id of the sink input that appeared between two listings of them, or -1.
int newlyAddedSinkInputId(std::string const& before, std::string const& after)
{
    auto idsOf = [](std::string const& listing)
    {
        std::vector<int> ids;
        std::istringstream lines(listing);
        std::string line;
        while (std::getline(lines, line))
        {
            std::istringstream parts(line);
            int id = -1;
            if (parts >> id && id >= 0) ids.push_back(id);
        }
        return ids;
    };

    std::vector<int> alreadyThere = idsOf(before);
    for (int id : idsOf(after))
        if (std::find(alreadyThere.begin(), alreadyThere.end(), id) == alreadyThere.end()) return id;
    return -1;
}

// The index of the sink a sink input is playing to, or -1.
int sinkOfSinkInput(int sinkInputId)
{
    std::istringstream lines(runPactl("list sink-inputs short"));
    std::string line;
    while (std::getline(lines, line))
    {
        std::istringstream parts(line);
        int id = -1, sink = -1;
        if (parts >> id >> sink && id == sinkInputId) return sink;
    }
    return -1;
}

// A recording device pointed at a speaker is usually refused (there's
// nothing to record there), and the refusal names the device it was about.
// Servers word and time their refusal differently: native PulseAudio can
// fail the connect itself ("Could not connect PulseAudio stream to ..."),
// PipeWire's PulseAudio emulator lets the connect through and fails the
// stream afterwards ("Lost connection to ..."). Some versions of PipeWire
// (1.6.2) don't refuse it at all, but record from another device instead,
// which FreeDV leaves be. Either way, the device comes down cleanly, with
// or without anybody registered to hear about it.
bool aRecordingDeviceNamedAfterASpeakerIsRefusedOrRecordsElsewhere()
{
    if (!soundServerReachable())
    {
        std::cout << "[no sound server to try this with] ";
        return true;
    }
    useUsualSoundServer();

    const char* name = "FreeDV_Computer_To_Speaker";
    std::string refusedAtOnce = std::string("Could not connect PulseAudio stream to ") + name;
    std::string refusedAfterwards = std::string("Lost connection to ") + name;

    std::string outputsBefore = runPactl("list source-outputs short");
    ErrorLog heard;
    auto device = PulseAudioDeviceTestAccess::create(name, IAudioEngine::AUDIO_ENGINE_IN, 48000, 2);
    device->setOnAudioError(&logDeviceError, &heard);
    device->start();
    if (!waitUntilReported(heard, refusedAtOnce, 1500) &&
        !waitUntilReported(heard, refusedAfterwards, 1500))
    {
        // Not refused: then the server has to have put the stream somewhere
        // (newlyAddedSinkInputId() goes by the ids at the start of any
        // pactl listing), and nothing else may have been reported.
        bool recording = newlyAddedSinkInputId(outputsBefore, runPactl("list source-outputs short")) >= 0;
        if (!recording || heard.size() != 0)
        {
            std::cout << "[" << name << ": ";
            heard.print();
            std::cout << (recording ? "" : "not recording anywhere either; ")
                      << "expected \"" << refusedAtOnce << "\" or \"" << refusedAfterwards << "\"] ";
            device->stop();
            useNoSoundServer();
            return false;
        }
        std::cout << "[recording somewhere else instead] ";
    }
    device->stop();

    // The same with nobody registered to hear about it: no fall-over, and
    // the device stops cleanly.
    auto unheard = PulseAudioDeviceTestAccess::create(name, IAudioEngine::AUDIO_ENGINE_IN, 48000, 2);
    unheard->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    unheard->stop();
    if (unheard->isRunning())
    {
        std::cout << "[the unheard device did not come down] ";
        useNoSoundServer();
        return false;
    }

    useNoSoundServer();
    return true;
}

// A device that never got a stream says it is 0 microseconds behind, both
// with no sound server to have tried at all and with one that refused the
// stream it asked for.
bool latencyOfAStreamlessDeviceIsZero()
{
    useNoSoundServer();
    {
        auto device = PulseAudioDeviceTestAccess::create(MISSING_DEVICE, IAudioEngine::AUDIO_ENGINE_OUT, 8000, 1);
        if (device->getLatencyInMicroseconds() != 0)
        {
            std::cout << "[latency before start(): " << device->getLatencyInMicroseconds() << "] ";
            return false;
        }
        device->start();
        int64_t during = device->getLatencyInMicroseconds();
        device->stop();
        if (during != 0)
        {
            std::cout << "[latency after a start() that couldn't connect: " << during << "] ";
            return false;
        }
    }

    if (soundServerReachable())
    {
        useUsualSoundServer();
        auto device = PulseAudioDeviceTestAccess::create("FreeDVTestNoSuchSink", IAudioEngine::AUDIO_ENGINE_OUT, 48000, 2);
        ErrorLog heard;
        device->setOnAudioError(&logDeviceError, &heard);
        device->start();
        int64_t refused = device->getLatencyInMicroseconds();
        device->stop();
        useNoSoundServer();
        if (refused != 0)
        {
            std::cout << "[latency on a stream the server refused: " << refused << "] ";
            return false;
        }
    }
    return true;
}

// A recording device with no data handler drops what it hears without
// falling over: what watches a stream just for errors (FreeDV's microphone
// test in Settings, for one) still has to cope with audio arriving.
bool aRecordingDeviceWithNoDataHandlerIgnoresWhatItHears()
{
    if (!soundServerReachable())
    {
        std::cout << "[no sound server to try this with] ";
        return true;
    }
    useUsualSoundServer();

    std::string module = loadNullSink("FreeDVTestMonitorSource");
    if (module.empty())
    {
        std::cout << "[could not load a throwaway sink] ";
        useNoSoundServer();
        return true;
    }

    // A playback device keeps the sink busy by itself: the server pulls
    // silence out of it even with nobody giving it audio, which is enough
    // for the sink's monitor to have something to record.
    ErrorLog playerErrors;
    auto player = PulseAudioDeviceTestAccess::create("FreeDVTestMonitorSource", IAudioEngine::AUDIO_ENGINE_OUT, 48000, 2);
    player->setOnAudioError(&logDeviceError, &playerErrors);
    player->start();

    ErrorLog recorderErrors;
    auto recorder = PulseAudioDeviceTestAccess::create("FreeDVTestMonitorSource.monitor", IAudioEngine::AUDIO_ENGINE_IN, 48000, 2);
    recorder->setOnAudioError(&logDeviceError, &recorderErrors);
    recorder->start();

    // Long enough for several rounds of the sink's audio to come round.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    bool playerUp = player->isRunning();
    bool recorderUp = recorder->isRunning();
    recorder->stop();
    player->stop();
    unloadModule(module);
    useNoSoundServer();

    bool quiet = !playerErrors.contains("Could not") && !recorderErrors.contains("Could not") &&
        !recorderErrors.contains("Lost connection");
    if (!playerUp || !recorderUp || !quiet)
    {
        std::cout << "[player running: " << (playerUp ? "yes" : "no")
                  << ", recorder running: " << (recorderUp ? "yes" : "no") << "; ";
        playerErrors.print();
        recorderErrors.print();
        std::cout << "] ";
        return false;
    }
    return true;
}

// A stream whose sink is taken away under it has to come down cleanly
// afterwards whatever the server does about it (native PulseAudio reports
// the loss to whoever registered for it; PipeWire's PulseAudio emulator
// just ends the stream).
bool aSinkRemovedUnderADeviceLeavesItStopable()
{
    if (!soundServerReachable())
    {
        std::cout << "[no sound server to try this with] ";
        return true;
    }
    useUsualSoundServer();

    std::string moduleA = loadNullSink("FreeDVTestUnloadA");
    std::string moduleB = loadNullSink("FreeDVTestUnloadB");
    if (moduleA.empty() || moduleB.empty())
    {
        unloadModule(moduleA);
        unloadModule(moduleB);
        std::cout << "[could not load the throwaway sinks] ";
        useNoSoundServer();
        return true;
    }

    ErrorLog heard;
    auto watched = PulseAudioDeviceTestAccess::create("FreeDVTestUnloadA", IAudioEngine::AUDIO_ENGINE_OUT, 48000, 2);
    watched->setOnAudioError(&logDeviceError, &heard);
    watched->start();

    auto unwatched = PulseAudioDeviceTestAccess::create("FreeDVTestUnloadB", IAudioEngine::AUDIO_ENGINE_OUT, 48000, 2);
    unwatched->start();

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    unloadModule(moduleA);
    unloadModule(moduleB);

    bool reported = waitUntilReported(heard, "Lost connection to FreeDVTestUnloadA", 5000);

    // A little longer for the unwatched one to get its (silent) hint too.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    watched->stop();
    unwatched->stop();
    bool stoppedCleanly = !watched->isRunning() && !unwatched->isRunning();
    useNoSoundServer();

    // The devices have to come down without hanging, waiting for a
    // response that a vanished sink can no longer give.
    if (!stoppedCleanly)
    {
        std::cout << "[stopped cleanly: no; reported: " << (reported ? "yes" : "no") << "; ";
        heard.print();
        std::cout << "] ";
        return false;
    }
    return true;
}

// A stream moved to another sink carries straight on, now under the new
// sink's name, whether or not anybody registered to hear about the move.
bool aMovedStreamCarriesOnWithTheNewDeviceName()
{
    if (!soundServerReachable())
    {
        std::cout << "[no sound server to try this with] ";
        return true;
    }
    useUsualSoundServer();

    std::string moduleA = loadNullSink("FreeDVTestMoveA");
    std::string moduleB = loadNullSink("FreeDVTestMoveB");
    int moveSink = sinkIndexNamed("FreeDVTestMoveB");
    if (moduleA.empty() || moduleB.empty() || moveSink < 0)
    {
        unloadModule(moduleA);
        unloadModule(moduleB);
        std::cout << "[could not set up the sinks to move between] ";
        useNoSoundServer();
        return true;
    }

    std::string inputsBefore = runPactl("list sink-inputs short");

    // Nobody registered for device changes: the stream has to keep going
    // and keep its new name all the same.
    ErrorLog heard;
    auto device = PulseAudioDeviceTestAccess::create("FreeDVTestMoveA", IAudioEngine::AUDIO_ENGINE_OUT, 48000, 2);
    device->setOnAudioError(&logDeviceError, &heard);
    device->start();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    int streamId = newlyAddedSinkInputId(inputsBefore, runPactl("list sink-inputs short"));
    if (streamId < 0)
    {
        device->stop();
        unloadModule(moduleA);
        unloadModule(moduleB);
        std::cout << "[the device's stream did not show up to be moved] ";
        useNoSoundServer();
        return true;
    }

    // PipeWire sometimes accepts a move without carrying it out, so ask
    // again until the stream is actually on the other sink.
    bool moved = false;
    for (int attempt = 0; attempt < 5 && !moved; attempt++)
    {
        runPactl("move-sink-input " + std::to_string(streamId) + " " + std::to_string(moveSink));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        moved = sinkOfSinkInput(streamId) == moveSink;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    wxString nowAt = PulseAudioDeviceTestAccess::deviceName(*device);
    bool running = device->isRunning();
    device->stop();
    unloadModule(moduleA);
    unloadModule(moduleB);
    useNoSoundServer();

    if (!moved)
    {
        std::cout << "[the sound server never moved the stream] ";
        return false;
    }

    if (nowAt != "FreeDVTestMoveB" || !running || heard.contains("Lost connection"))
    {
        std::cout << "[stream now at \"" << (const char*)nowAt.ToUTF8()
                  << "\", running: " << (running ? "yes" : "no") << "; ";
        heard.print();
        std::cout << "] ";
        return false;
    }
    return true;
}

// An engine let go of while still running (the application is shutting down
// and doesn't think to stop it first) is stopped by its own destructor,
// leaving the sound server usable for the next one.
bool destroyingTheEngineWithoutStoppingItLeavesTheServerUsable()
{
    if (!soundServerReachable())
    {
        std::cout << "[no sound server to try this with] ";
        return true;
    }
    useUsualSoundServer();

    {
        PulseAudioEngine engine;
        engine.start();
        // Deliberately no stop(): the destructor has to do it.
    }

    ErrorLog heard;
    PulseAudioEngine next;
    next.setOnEngineError(&logEngineError, &heard);
    next.start();
    auto devices = next.getAudioDeviceList(IAudioEngine::AUDIO_ENGINE_OUT);
    bool listed = !devices.empty();
    next.stop();
    useNoSoundServer();

    if (heard.size() != 0 || !listed)
    {
        std::cout << "[" << heard.size() << " error(s) ";
        heard.print();
        std::cout << (listed ? "" : "and nothing listed") << "] ";
        return false;
    }
    return true;
}

// A device specification with no user-friendly name shows its server name
// instead; with one, it shows that.
bool deviceSpecificationsFallBackToTheirServerNamesForDisplay()
{
    AudioDeviceSpecification unnamed = {
        .deviceId = 1234,
        .name = "FreeDVTestUnnamedDevice",
        .displayName = "",
        .cardName = "FreeDV Test Card",
        .cardIndex = 0,
        .portName = "",
        .apiName = "PulseAudio",
        .defaultSampleRate = 48000,
        .maxChannels = 2,
        .minChannels = 1,
    };
    AudioDeviceSpecification named = {
        .deviceId = 1235,
        .name = "FreeDVTestNamedDevice",
        .displayName = "FreeDV Test Named Device",
        .cardName = "FreeDV Test Card",
        .cardIndex = 0,
        .portName = "",
        .apiName = "PulseAudio",
        .defaultSampleRate = 48000,
        .maxChannels = 2,
        .minChannels = 1,
    };

    if (unnamed.getDisplayName() != unnamed.name || named.getDisplayName() != named.displayName)
    {
        std::cout << "[unnamed shows \"" << (const char*)unnamed.getDisplayName().ToUTF8()
                  << "\", named shows \"" << (const char*)named.getDisplayName().ToUTF8() << "\"] ";
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

    ulog_set_quiet(true);

    executeTestCase("engineStartSaysThereIsNoSoundServer", engineStartSaysThereIsNoSoundServer);
    executeTestCase("engineWithoutASoundServerHasNoDevices", engineWithoutASoundServerHasNoDevices);
    executeTestCase("deviceStartReportsTheFailureAndWhichDeviceItWas", deviceStartReportsTheFailureAndWhichDeviceItWas);
    executeTestCase("failedDeviceStartLeavesNothingToStop", failedDeviceStartLeavesNothingToStop);
    executeTestCase("streamThatCannotBeCreatedIsReported", streamThatCannotBeCreatedIsReported);
    executeTestCase("xrunsAreReportedToTheDeviceTheyBelongTo", xrunsAreReportedToTheDeviceTheyBelongTo);
    executeTestCase("xrunsWithNobodyListeningAreIgnored", xrunsWithNobodyListeningAreIgnored);
    executeTestCase("workIsPacedToTheStreamsLatency", workIsPacedToTheStreamsLatency);
    executeTestCase("overrunningWorkIsNotWaitedAfter", overrunningWorkIsNotWaitedAfter);
    executeTestCase("workHeldUpForSecondsIsNotWaitedAfter", workHeldUpForSecondsIsNotWaitedAfter);
    executeTestCase("aRecordingDeviceNamedAfterASpeakerIsRefusedOrRecordsElsewhere", aRecordingDeviceNamedAfterASpeakerIsRefusedOrRecordsElsewhere);
    executeTestCase("latencyOfAStreamlessDeviceIsZero", latencyOfAStreamlessDeviceIsZero);
    executeTestCase("aRecordingDeviceWithNoDataHandlerIgnoresWhatItHears", aRecordingDeviceWithNoDataHandlerIgnoresWhatItHears);
    executeTestCase("aSinkRemovedUnderADeviceLeavesItStopable", aSinkRemovedUnderADeviceLeavesItStopable);
    executeTestCase("aMovedStreamCarriesOnWithTheNewDeviceName", aMovedStreamCarriesOnWithTheNewDeviceName);
    executeTestCase("destroyingTheEngineWithoutStoppingItLeavesTheServerUsable", destroyingTheEngineWithoutStoppingItLeavesTheServerUsable);
    executeTestCase("deviceSpecificationsFallBackToTheirServerNamesForDisplay", deviceSpecificationsFallBackToTheirServerNamesForDisplay);

    return testResult();
}
