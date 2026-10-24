#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include "../SerialPortInRigController.h"
#include "../SerialPortOutRigController.h"
#include "util/logging/ulog.h"
#include "../../test/UnitTestCommon.h"

using namespace std::chrono_literals;

// These controllers key the radio (or watch for the operator keying it) using
// a serial port's control lines. A pseudo-terminal stands in for the serial
// port. Pseudo-terminals have no control lines, so what's checked here is
// everything around them: opening and setting up the port, the events raised
// and the handling of errors.

namespace {

constexpr auto TIMEOUT = 5s;
const std::string MISSING_PORT = "/nonexistent/serial/port";

// Long enough for the PTT input controller to have looked at its port several times.
constexpr auto SEVERAL_POLLS = 100ms;

class PseudoTerminal
{
public:
    PseudoTerminal()
        : controlFd_(posix_openpt(O_RDWR | O_NOCTTY))
    {
        if (controlFd_ >= 0 && grantpt(controlFd_) == 0 && unlockpt(controlFd_) == 0)
        {
            const char* name = ptsname(controlFd_);
            if (name != nullptr) portName_ = name;
        }
    }

    ~PseudoTerminal()
    {
        if (controlFd_ >= 0) close(controlFd_);
    }

    bool isValid() const { return !portName_.empty(); }
    std::string const& portName() const { return portName_; }

    // The settings of the port as an application opening it would see them.
    bool getSettings(struct termios& settings)
    {
        int fd = open(portName_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) return false;
        bool result = tcgetattr(fd, &settings) == 0;
        close(fd);
        return result;
    }

    // Puts the port into the state a terminal's normally found in (line at a
    // time, echoing what's typed), which is of no use for a radio interface.
    bool makeInteractive()
    {
        int fd = open(portName_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) return false;

        struct termios settings;
        bool result = tcgetattr(fd, &settings) == 0;
        if (result)
        {
            settings.c_lflag |= ICANON | ECHO;
            settings.c_oflag |= OPOST;
            result = tcsetattr(fd, TCSANOW, &settings) == 0;
        }
        close(fd);
        return result;
    }

private:
    int controlFd_;
    std::string portName_;
};

// Records the events raised by a controller. These are raised from the
// controller's own thread, so tests have to wait for them to show up.
struct Events
{
    std::mutex mtx;
    std::condition_variable cv;
    int numConnects = 0;
    int numDisconnects = 0;
    std::vector<std::string> errors;
    std::vector<bool> pttChanges;

    template<typename Controller>
    void listenTo(Controller& controller)
    {
        controller.onRigConnected += [this](IRigController*) {
            record_([this]() { numConnects++; });
        };
        controller.onRigDisconnected += [this](IRigController*) {
            record_([this]() { numDisconnects++; });
        };
        controller.onRigError += [this](IRigController*, std::string const& err) {
            record_([&]() { errors.push_back(err); });
        };
        controller.onPttChange += [this](IRigPttController*, bool state) {
            record_([&]() { pttChanges.push_back(state); });
        };
    }

    // Waits until the given condition (evaluated with the lock held) is true.
    bool waitFor(std::function<bool()> const& condition)
    {
        std::unique_lock<std::mutex> lk(mtx);
        return cv.wait_for(lk, TIMEOUT, condition);
    }

    bool pttChangesAre(std::vector<bool> const& expected)
    {
        bool result = waitFor([&]() { return pttChanges.size() >= expected.size(); });

        std::unique_lock<std::mutex> lk(mtx);
        if (!result || pttChanges != expected)
        {
            std::cout << "[PTT changes were";
            for (bool state : pttChanges) std::cout << " " << state;
            std::cout << "; expected";
            for (bool state : expected) std::cout << " " << state;
            std::cout << "] ";
            return false;
        }
        return true;
    }

    size_t numPttChanges()
    {
        std::unique_lock<std::mutex> lk(mtx);
        return pttChanges.size();
    }

    bool reportedOneErrorNaming(std::string const& portName)
    {
        waitFor([this]() { return !errors.empty(); });

        std::unique_lock<std::mutex> lk(mtx);
        if (errors.size() != 1 || errors[0].find(portName) == std::string::npos)
        {
            std::cout << "[" << errors.size() << " error(s)";
            for (auto& err : errors) std::cout << ": \"" << err << "\"";
            std::cout << "; expected one naming " << portName << "] ";
            return false;
        }
        return true;
    }

    bool noErrors()
    {
        std::unique_lock<std::mutex> lk(mtx);
        if (!errors.empty())
        {
            std::cout << "[unexpected error: \"" << errors[0] << "\"] ";
            return false;
        }
        return true;
    }

private:
    void record_(std::function<void()> const& fn)
    {
        {
            std::unique_lock<std::mutex> lk(mtx);
            fn();
        }
        cv.notify_all();
    }
};

// A controller that keys the radio, connected (unless told otherwise) to a pseudo-terminal.
struct PttOutput
{
    PseudoTerminal port;
    Events events;

    // Declared last so that it's destroyed (and its thread stopped) first.
    NeverReused<SerialPortOutRigController> controllerStorage;
    SerialPortOutRigController& controller;

    explicit PttOutput(std::string const& portName = "", bool useRts = true, bool useDtr = true)
        : controllerStorage(portName.empty() ? port.portName() : portName, useRts, true, useDtr, true)
        , controller(*controllerStorage)
    {
        events.listenTo(controller);
    }

    bool connect()
    {
        if (!port.isValid())
        {
            std::cout << "[could not create a pseudo-terminal] ";
            return false;
        }
        controller.connect();
        return
            events.waitFor([this]() { return events.numConnects == 1 || !events.errors.empty(); }) &&
            events.noErrors();
    }
};

// A controller that watches for the operator keying the radio.
struct PttInput
{
    PseudoTerminal port;
    Events events;

    // Declared last so that it's destroyed (and its thread stopped) first.
    NeverReused<SerialPortInRigController> controllerStorage;
    SerialPortInRigController& controller;

    explicit PttInput(bool ctsPos, std::string const& portName = "")
        : controllerStorage(portName.empty() ? port.portName() : portName, ctsPos)
        , controller(*controllerStorage)
    {
        events.listenTo(controller);
    }

    bool connect()
    {
        if (!port.isValid())
        {
            std::cout << "[could not create a pseudo-terminal] ";
            return false;
        }
        controller.connect();
        return
            events.waitFor([this]() { return events.numConnects >= 1 || !events.errors.empty(); }) &&
            events.noErrors();
    }
};

// A file that exists but isn't a serial port.
struct OrdinaryFile
{
    std::string path;

    OrdinaryFile()
    {
        char fileTemplate[] = "/tmp/freedv-serial-test-XXXXXX";
        int fd = mkstemp(fileTemplate);
        if (fd >= 0)
        {
            path = fileTemplate;
            close(fd);
        }
    }

    ~OrdinaryFile()
    {
        if (!path.empty()) remove(path.c_str());
    }
};

bool outputConnects()
{
    PttOutput f;
    if (f.controller.isConnected()) return false;

    return f.connect() && f.controller.isConnected();
}

bool outputStartsWithPttOff()
{
    // The radio mustn't be left transmitting by whatever state the port's lines were in.
    PttOutput f;
    return f.connect() && f.events.pttChangesAre({ false });
}

bool outputReportsPttChanges(bool useRts, bool useDtr)
{
    PttOutput f("", useRts, useDtr);
    if (!f.connect()) return false;

    f.controller.ptt(true);
    f.controller.ptt(false);
    f.controller.ptt(true);
    return
        f.events.pttChangesAre({ false, true, false, true }) &&
        f.controller.getRigResponseTimeMicroseconds() >= 0 &&
        f.events.noErrors();
}

bool outputSetsPortUpForRawData()
{
    PttOutput f;
    if (!f.port.isValid() || !f.port.makeInteractive())
    {
        std::cout << "[could not set up a pseudo-terminal] ";
        return false;
    }
    if (!f.connect()) return false;

    struct termios settings;
    if (!f.port.getSettings(settings)) return false;

    // Nothing sent to or from the radio interface gets reinterpreted, 8 bits
    // at a time, and without waiting for a modem that isn't there.
    bool result =
        (settings.c_lflag & (ICANON | ECHO | ISIG)) == 0 &&
        (settings.c_oflag & OPOST) == 0 &&
        (settings.c_iflag & (ICRNL | IXON | IXOFF | ISTRIP)) == 0 &&
        (settings.c_cflag & CSIZE) == CS8 &&
        (settings.c_cflag & (CREAD | CLOCAL)) == (CREAD | CLOCAL);
    if (!result)
    {
        std::cout << std::hex << "[iflag " << settings.c_iflag << ", oflag " << settings.c_oflag
                  << ", cflag " << settings.c_cflag << ", lflag " << settings.c_lflag << std::dec << "] ";
    }
    return result;
}

bool outputReportsErrorForMissingPort()
{
    PttOutput f(MISSING_PORT);
    f.controller.connect();
    return
        f.events.reportedOneErrorNaming(MISSING_PORT) &&
        !f.controller.isConnected() && f.events.numConnects == 0;
}

bool outputReportsErrorIfPortIsNotSerial()
{
    OrdinaryFile file;
    if (file.path.empty()) return false;

    PttOutput f(file.path);
    f.controller.connect();
    return
        f.events.reportedOneErrorNaming(file.path) &&
        !f.controller.isConnected() && f.events.numConnects == 0;
}

bool outputIgnoresPttWhenNotConnected()
{
    PttOutput f;
    f.controller.ptt(true);

    // Requests are handled in order, so once connected (which reports PTT as
    // being off) we know that the above was dealt with.
    return f.connect() && f.events.pttChangesAre({ false });
}

bool outputDisconnects()
{
    PttOutput f;
    if (!f.connect() || !f.events.pttChangesAre({ false })) return false;

    f.controller.disconnect();
    if (!f.events.waitFor([&]() { return f.events.numDisconnects == 1; }) || f.controller.isConnected()) return false;

    // The port's no longer there to key.
    f.controller.ptt(true);
    f.controller.connect();
    return
        f.events.waitFor([&]() { return f.events.numConnects == 2; }) &&
        f.events.pttChangesAre({ false, false }) && f.events.noErrors();
}

bool outputDisconnectWithoutConnectingIsHarmless()
{
    PttOutput f;
    f.controller.disconnect();
    return f.connect() && f.events.numDisconnects == 0 && f.controller.isConnected();
}

bool outputDestroysCleanlyWhileConnected()
{
    for (int i = 0; i < 2; i++)
    {
        PttOutput f;
        if (!f.connect()) return false;
        if (i % 2 == 0) f.controller.ptt(true);
    }
    return true;
}

// A controller for a port that was never opened. Its control-line helpers
// must do nothing at all rather than touch a handle that isn't there.
struct UnopenedLineController : public SerialPortOutRigController
{
    explicit UnopenedLineController(std::string const& portName)
        : SerialPortRigController(portName)
        , SerialPortOutRigController(portName, true, true, true, true) {}

    void cycleLines()
    {
        lowerDTR_();
        raiseDTR_();
        lowerRTS_();
        raiseRTS_();
    }
};

bool outputIgnoresControlLinesWhilePortUnopened()
{
    Events events;
    UnopenedLineController controller(MISSING_PORT);
    events.listenTo(controller);

    controller.cycleLines();          // never connected: silently does nothing

    controller.connect();
    if (!events.reportedOneErrorNaming(MISSING_PORT)) return false;

    // The port still isn't there, so keying the lines is still a no-op, and
    // nothing is reported on the radio's behalf.
    controller.cycleLines();
    std::this_thread::sleep_for(SEVERAL_POLLS);
    return !controller.isConnected() && events.numPttChanges() == 0;
}

// A pseudo-terminal never asserts CTS, so which way round the interface is
// wired decides whether that reads as the operator transmitting.
bool inputReportsStateOnConnecting(bool ctsPos)
{
    PttInput f(ctsPos);
    if (!f.connect() || !f.controller.isConnected()) return false;
    if (!f.events.pttChangesAre({ !ctsPos })) return false;

    // It's only reported again once it changes.
    std::this_thread::sleep_for(SEVERAL_POLLS);
    return f.events.pttChangesAre({ !ctsPos }) && f.events.noErrors();
}

bool inputLeavesPortSettingsAlone()
{
    // Some virtual serial port drivers lock up if their settings are queried or changed.
    PttInput f(true);
    struct termios before;
    struct termios after;
    if (!f.port.isValid() || !f.port.makeInteractive() || !f.port.getSettings(before))
    {
        std::cout << "[could not set up a pseudo-terminal] ";
        return false;
    }

    return
        f.connect() && f.events.pttChangesAre({ false }) && f.port.getSettings(after) &&
        before.c_iflag == after.c_iflag && before.c_oflag == after.c_oflag &&
        before.c_cflag == after.c_cflag && before.c_lflag == after.c_lflag;
}

bool inputReportsNothingWhileSuspended()
{
    // FreeDV suspends this while it keys the radio itself.
    PttInput f(true);
    f.controller.suspendChanges(true);
    if (!f.connect()) return false;

    std::this_thread::sleep_for(SEVERAL_POLLS);
    if (f.events.numPttChanges() != 0)
    {
        std::cout << "[PTT input reported while suspended] ";
        return false;
    }

    f.controller.suspendChanges(false);
    return f.events.pttChangesAre({ false });
}

bool inputCannotKeyRadio()
{
    PttInput f(true);
    if (!f.connect() || !f.events.pttChangesAre({ false })) return false;

    f.controller.ptt(true);
    std::this_thread::sleep_for(SEVERAL_POLLS);
    return f.events.pttChangesAre({ false }) && f.controller.getRigResponseTimeMicroseconds() == 0;
}

bool inputReportsErrorForMissingPort()
{
    PttInput f(true, MISSING_PORT);
    f.controller.connect();
    if (!f.events.reportedOneErrorNaming(MISSING_PORT) || f.controller.isConnected()) return false;

    std::this_thread::sleep_for(SEVERAL_POLLS);
    return f.events.numConnects == 0 && f.events.numPttChanges() == 0;
}

bool inputStopsWatchingOnDisconnect()
{
    PttInput f(true);
    if (!f.connect() || !f.events.pttChangesAre({ false })) return false;

    f.controller.disconnect();
    if (!f.events.waitFor([&]() { return f.events.numDisconnects == 1; }) || f.controller.isConnected()) return false;

    std::this_thread::sleep_for(SEVERAL_POLLS);
    if (!f.events.pttChangesAre({ false })) return false;

    // Connecting again starts over, so the current state is reported afresh.
    f.controller.connect();
    return
        f.events.waitFor([&]() { return f.events.numConnects == 2; }) &&
        f.events.pttChangesAre({ false, false }) && f.events.noErrors();
}

bool inputDisconnectWithoutConnectingIsHarmless()
{
    // e.g. the port couldn't be opened and the user then stops FreeDV.
    PttInput f(true, MISSING_PORT);
    f.controller.connect();
    if (!f.events.reportedOneErrorNaming(MISSING_PORT)) return false;

    f.controller.disconnect();
    f.controller.disconnect();
    f.controller.suspendChanges(false);
    std::this_thread::sleep_for(SEVERAL_POLLS);
    return f.events.numDisconnects == 0 && !f.controller.isConnected();
}

bool inputDestroysCleanlyWhileConnected()
{
    for (int i = 0; i < 3; i++)
    {
        PttInput f(i % 2 == 0);
        if (!f.connect()) return false;
        if (i > 0) std::this_thread::sleep_for(SEVERAL_POLLS);
    }
    return true;
}

// This is how FreeDV stops watching for PTT input: it asks for a disconnect
// and lets go of the controller without waiting for that to happen.
bool inputDestroysCleanlyRightAfterDisconnect()
{
    for (int i = 0; i < 50; i++)
    {
        PttInput f(true);
        if (!f.connect()) return false;
        if (i % 2 == 0 && !f.events.pttChangesAre({ false })) return false;
        f.controller.disconnect();
    }
    return true;
}

}

int main()
{
    ulog_set_quiet(true);

    executeTestCase("outputConnects", outputConnects);
    executeTestCase("outputStartsWithPttOff", outputStartsWithPttOff);
    executeTestCase("outputReportsPttChanges (RTS and DTR)", []() { return outputReportsPttChanges(true, true); });
    executeTestCase("outputReportsPttChanges (RTS)", []() { return outputReportsPttChanges(true, false); });
    executeTestCase("outputReportsPttChanges (DTR)", []() { return outputReportsPttChanges(false, true); });
    executeTestCase("outputSetsPortUpForRawData", outputSetsPortUpForRawData);
    executeTestCase("outputReportsErrorForMissingPort", outputReportsErrorForMissingPort);
    executeTestCase("outputReportsErrorIfPortIsNotSerial", outputReportsErrorIfPortIsNotSerial);
    executeTestCase("outputIgnoresPttWhenNotConnected", outputIgnoresPttWhenNotConnected);
    executeTestCase("outputDisconnects", outputDisconnects);
    executeTestCase("outputDisconnectWithoutConnectingIsHarmless", outputDisconnectWithoutConnectingIsHarmless);
    executeTestCase("outputDestroysCleanlyWhileConnected", outputDestroysCleanlyWhileConnected);
    executeTestCase("outputIgnoresControlLinesWhilePortUnopened", outputIgnoresControlLinesWhilePortUnopened);
    executeTestCase("inputReportsStateOnConnecting (CTS means transmit)", []() { return inputReportsStateOnConnecting(true); });
    executeTestCase("inputReportsStateOnConnecting (CTS means receive)", []() { return inputReportsStateOnConnecting(false); });
    executeTestCase("inputLeavesPortSettingsAlone", inputLeavesPortSettingsAlone);
    executeTestCase("inputReportsNothingWhileSuspended", inputReportsNothingWhileSuspended);
    executeTestCase("inputCannotKeyRadio", inputCannotKeyRadio);
    executeTestCase("inputReportsErrorForMissingPort", inputReportsErrorForMissingPort);
    executeTestCase("inputStopsWatchingOnDisconnect", inputStopsWatchingOnDisconnect);
    executeTestCase("inputDisconnectWithoutConnectingIsHarmless", inputDisconnectWithoutConnectingIsHarmless);
    executeTestCase("inputDestroysCleanlyWhileConnected", inputDestroysCleanlyWhileConnected);
    executeTestCase("inputDestroysCleanlyRightAfterDisconnect", inputDestroysCleanlyRightAfterDisconnect);
    return testResult();
}
