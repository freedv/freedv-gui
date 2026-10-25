#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <future>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <unistd.h>

#include "../HamlibRigController.h"
#include "util/logging/ulog_async.h"
#include "util/logging/ulog.h"
#include "../../test/UnitTestCommon.h"

using namespace std::chrono_literals;

// Hamlib provides a simulated radio that needs no hardware, which lets us
// exercise the controller all the way through the real library.

// Stands in for the operator: changes things on the radio itself, behind the
// controller's back. This runs on the controller's thread as Hamlib isn't
// meant to be used from more than one thread at a time.
struct HamlibRigControllerTestAccess
{
    static void onRadio(HamlibRigController& controller, std::function<void(RIG*)> const& fn)
    {
        std::promise<void> done;
        controller.enqueue_([&]() {
            fn(controller.rig_.load());
            done.set_value();
        });
        done.get_future().wait();
    }

    // Adds a radio to the list that the controller got from Hamlib.
    static void addRadio(const struct rig_caps* caps)
    {
        HamlibRigController::InitializeHamlibLibrary();

        std::unique_lock<std::mutex> lk(HamlibRigController::RigListMutex_);
        HamlibRigController::RigList_.push_back(caps);
        HamlibRigController::RigNameList_.push_back(std::string(caps->mfg_name) + " " + caps->model_name);
    }

    // The order rule the controller uses when it builds its list of radios.
    static bool radioSortsBefore(const struct rig_caps* rig1, const struct rig_caps* rig2)
    {
        return HamlibRigController::RigCompare_(rig1, rig2);
    }
};

namespace {

const std::string DUMMY_RIG_NAME = "Hamlib Dummy";
constexpr auto TIMEOUT = 5s;

// A request that the simulated radio can be told to fail (see RadioFaults).
struct RadioFault
{
    static constexpr int ALWAYS = -1;

    std::atomic<int> numToPass { 0 };
    std::atomic<int> numToFail { 0 };

    // Fails every request from now on.
    void always() { failAfter(0, ALWAYS); }

    // Lets the given number of requests through and then fails the given number.
    void failAfter(int pass, int fail)
    {
        numToPass = pass;
        numToFail = fail;
    }

    void clear() { failAfter(0, 0); }

    bool shouldFail()
    {
        if (numToPass > 0)
        {
            numToPass--;
            return false;
        }
        if (numToFail == ALWAYS) return true;
        if (numToFail > 0)
        {
            numToFail--;
            return true;
        }
        return false;
    }
};

// Makes the simulated radio fail requests on demand. Hamlib calls into each
// radio's driver through a table of functions; while a test is running, that
// table is swapped for a copy which can be told to report an error instead of
// passing the request on to the simulated radio.
struct RadioFaults
{
    using Fault = RadioFault;
    static constexpr int ALWAYS = RadioFault::ALWAYS;

    static inline Fault setPtt, setFreq, getFreq, setMode, getMode, getVfo, setVfo;

    // Whether a faulted get_vfo reports that VFO information isn't available
    // right now (-RIG_ENAVAIL) rather than outright refusing the request.
    static inline std::atomic<bool> getVfoUnavailable { false };

    static inline struct rig_caps hookedCaps;
    static inline const struct rig_caps* realCaps = nullptr;

    static void clearAll()
    {
        for (auto fault : { &setPtt, &setFreq, &getFreq, &setMode, &getMode, &getVfo, &setVfo })
        {
            fault->clear();
        }
        getVfoUnavailable = false;
    }

    // Must be called on the controller's thread (see HamlibRigControllerTestAccess).
    static void install(RIG* rig)
    {
        realCaps = rig->caps;
        hookedCaps = *realCaps;
        hookedCaps.set_ptt = [](RIG* r, vfo_t vfo, ptt_t ptt) -> int {
            return setPtt.shouldFail() ? -RIG_EIO : realCaps->set_ptt(r, vfo, ptt);
        };
        hookedCaps.set_freq = [](RIG* r, vfo_t vfo, freq_t freq) -> int {
            return setFreq.shouldFail() ? -RIG_EIO : realCaps->set_freq(r, vfo, freq);
        };
        hookedCaps.get_freq = [](RIG* r, vfo_t vfo, freq_t* freq) -> int {
            return getFreq.shouldFail() ? -RIG_EIO : realCaps->get_freq(r, vfo, freq);
        };
        hookedCaps.set_mode = [](RIG* r, vfo_t vfo, rmode_t mode, pbwidth_t width) -> int {
            return setMode.shouldFail() ? -RIG_EIO : realCaps->set_mode(r, vfo, mode, width);
        };
        hookedCaps.get_mode = [](RIG* r, vfo_t vfo, rmode_t* mode, pbwidth_t* width) -> int {
            return getMode.shouldFail() ? -RIG_EIO : realCaps->get_mode(r, vfo, mode, width);
        };
        hookedCaps.get_vfo = [](RIG* r, vfo_t* vfo) -> int {
            if (getVfo.shouldFail()) return getVfoUnavailable ? -RIG_ENAVAIL : -RIG_EIO;
            return realCaps->get_vfo(r, vfo);
        };
        hookedCaps.set_vfo = [](RIG* r, vfo_t vfo) -> int {
            return setVfo.shouldFail() ? -RIG_EIO : realCaps->set_vfo(r, vfo);
        };
        rig->caps = &hookedCaps;

        // Otherwise Hamlib answers some requests from memory without asking the radio.
        rig_set_cache_timeout_ms(rig, HAMLIB_CACHE_ALL, 0);
    }
};

// Lets tests see the settings Hamlib was given for a connection. The simulated
// radio ignores most of them, so stand-ins for radios that use a serial port
// are added to Hamlib's list. These note down their settings as Hamlib
// discards them, which the controller has it do once the connection fails --
// and it always does, as no radio's attached.
struct SettingsProbe
{
    static inline const std::string SERIAL_RIG_NAME = "Yaesu FreeDV Test Radio";
    static inline const std::string ICOM_RIG_NAME = "Icom FreeDV Test Radio";
    static inline const std::string SERIAL_PORT = "/nonexistent/serial/port";

    // A simulated radio that, left alone, is given a long time to answer.
    static inline const std::string SLOW_RIG_NAME = "Hamlib FreeDV Test Slow Radio";
    static constexpr int SLOW_RIG_TIMEOUT_MS = 3000;

    using Settings = std::map<std::string, std::string>;
    static inline Settings captured;

    static inline struct rig_caps serialCaps;
    static inline struct rig_caps icomCaps;
    static inline struct rig_caps slowCaps;
    static inline int (*realSerialCleanup)(RIG*) = nullptr;
    static inline int (*realIcomCleanup)(RIG*) = nullptr;

    // Note: Hamlib forgets about these if its list of radios is loaded again, 
    // so this has to follow the controller doing so.
    static bool registerRadios()
    {
        HamlibRigController::InitializeHamlibLibrary();

        auto serialSource = rig_get_caps(RIG_MODEL_FT817);
        auto icomSource = rig_get_caps(RIG_MODEL_IC7300);
        auto slowSource = rig_get_caps(RIG_MODEL_DUMMY);
        if (serialSource == nullptr || icomSource == nullptr || slowSource == nullptr) return false;

        slowCaps = *slowSource;
        slowCaps.rig_model = RIG_MAKE_MODEL(RIG_DUMMY, 97);
        slowCaps.model_name = "FreeDV Test Slow Radio";
        slowCaps.timeout = SLOW_RIG_TIMEOUT_MS;

        serialCaps = *serialSource;
        serialCaps.rig_model = RIG_MAKE_MODEL(RIG_DUMMY, 98);
        serialCaps.model_name = "FreeDV Test Radio";
        realSerialCleanup = serialCaps.rig_cleanup;
        serialCaps.rig_cleanup = [](RIG* rig) -> int {
            capture(rig);
            return realSerialCleanup != nullptr ? realSerialCleanup(rig) : RIG_OK;
        };

        icomCaps = *icomSource;
        icomCaps.rig_model = RIG_MAKE_MODEL(RIG_DUMMY, 99);
        icomCaps.model_name = "FreeDV Test Radio";
        realIcomCleanup = icomCaps.rig_cleanup;
        icomCaps.rig_cleanup = [](RIG* rig) -> int {
            capture(rig);
            return realIcomCleanup != nullptr ? realIcomCleanup(rig) : RIG_OK;
        };

        if (rig_register(&serialCaps) != RIG_OK || rig_register(&icomCaps) != RIG_OK ||
            rig_register(&slowCaps) != RIG_OK) 
        {
            return false;
        }
        HamlibRigControllerTestAccess::addRadio(&slowCaps);

        HamlibRigControllerTestAccess::addRadio(&serialCaps);
        HamlibRigControllerTestAccess::addRadio(&icomCaps);
        return true;
    }

    static void capture(RIG* rig)
    {
        captured.clear();
        for (auto name : { 
            "rig_pathname", "serial_speed", "ptt_type", "ptt_pathname", "ptt_share", 
            "rts_state", "dtr_state", "civaddr" })
        {
            std::string value;
            if (read(rig, name, value)) captured[name] = value;
        }
    }

    // Reads one of the settings that Hamlib currently has for a radio.
    static bool read(RIG* rig, const char* name, std::string& value)
    {
        auto token = rig_token_lookup(rig, name);
        if (token == RIG_CONF_END) return false;

        constexpr int BUF_LEN = 1024;
        char buf[BUF_LEN] = { 0 };
#if defined(HAMLIB_USE_FRIENDLY_ERRORS)
        int result = rig_get_conf2(rig, token, buf, BUF_LEN);
#else
        int result = rig_get_conf(rig, token, buf);
#endif // defined(HAMLIB_USE_FRIENDLY_ERRORS)
        value = buf;
        return result == RIG_OK;
    }
};

// Stand-ins for radios with unusual traits, so that the controller's handling
// of them can be checked without owning the hardware. All are based on
// Hamlib's simulated radio, and apart from the one quirk each was given they
// behave normally.
struct CustomRadios
{
    // Radios whose full name (manufacturer + model) is identical. They have
    // to be registered before Hamlib's list is loaded into the controller for
    // the first time, so that the controller sorts them and has to decide how
    // to order equal names.
    static inline struct rig_caps tieCaps[3];
    static inline std::string tieRigName;

    // Known only to the controller, not to Hamlib: the controller can describe
    // these radios but can never connect to them.
    static inline struct rig_caps noPortCaps;
    static inline struct rig_caps usbPortCaps;
    static inline struct rig_caps udpPortCaps;
    static inline const std::string NO_PORT_RIG_NAME = "Hamlib FreeDV Test Radio With No Port";
    static inline const std::string USB_PORT_RIG_NAME = "Hamlib FreeDV Test Radio On USB";
    static inline const std::string UDP_PORT_RIG_NAME = "Hamlib FreeDV Test Radio On UDP";

    // A model from a backend family Hamlib doesn't know at all; asking it for
    // its default port is the one way to see a model Hamlib can't open at all.
    static inline struct rig_caps unknownBackendCaps;
    static inline const std::string UNKNOWN_BACKEND_RIG_NAME = "Hamlib FreeDV Test Radio From Unknown Backend";

    // A radio that claims it can't be unkeyed the first time it's asked
    // (as some radios that were already transmitting do).
    static inline struct rig_caps startsTransmittingCaps;
    static inline const std::string STARTS_TRANSMITTING_RIG_NAME = "Hamlib FreeDV Test Radio That Starts Transmitting";
    static inline int (*realSetPtt)(RIG*, vfo_t, ptt_t) = nullptr;
    static inline int numUnkeyRequests = 0;

    static bool addTieRadios()
    {
        // Hamlib's table has to be loaded to copy the dummy radio's
        // capabilities from, and the controller's list already built: adding
        // to that list is the only way these radios survive, as loading
        // Hamlib's table (which the controller does when it builds its list)
        // forgets everything registered into it beforehand.
        HamlibRigController::InitializeHamlibLibrary();

        auto source = rig_get_caps(RIG_MODEL_DUMMY);
        if (source == nullptr) return false;

        for (int index = 0; index < 3; index++)
        {
            tieCaps[index] = *source;
            tieCaps[index].rig_model = RIG_MAKE_MODEL(RIG_DUMMY, 900 + index);
            tieCaps[index].model_name = "FreeDV Test Tie Radio";
            HamlibRigControllerTestAccess::addRadio(&tieCaps[index]);
        }

        tieRigName = std::string(tieCaps[0].mfg_name) + " " + tieCaps[0].model_name;
        return true;
    }

    static bool addVariantRadios()
    {
        auto source = rig_get_caps(RIG_MODEL_DUMMY);
        if (source == nullptr) return false;

        usbPortCaps = *source;
        usbPortCaps.rig_model = RIG_MAKE_MODEL(RIG_DUMMY, 903);
        usbPortCaps.model_name = "FreeDV Test Radio On USB";
        usbPortCaps.port_type = RIG_PORT_USB;
        HamlibRigControllerTestAccess::addRadio(&usbPortCaps);

        udpPortCaps = *source;
        udpPortCaps.rig_model = RIG_MAKE_MODEL(RIG_DUMMY, 906);
        udpPortCaps.model_name = "FreeDV Test Radio On UDP";
        udpPortCaps.port_type = RIG_PORT_UDP_NETWORK;
        HamlibRigControllerTestAccess::addRadio(&udpPortCaps);

        noPortCaps = *source;
        noPortCaps.rig_model = RIG_MAKE_MODEL(RIG_DUMMY, 904);
        noPortCaps.model_name = "FreeDV Test Radio With No Port";
        noPortCaps.port_type = RIG_PORT_NONE;
        HamlibRigControllerTestAccess::addRadio(&noPortCaps);

        unknownBackendCaps = *source;
        unknownBackendCaps.rig_model = 99 * MAX_MODELS_PER_BACKEND + 1;
        unknownBackendCaps.model_name = "FreeDV Test Radio From Unknown Backend";
        HamlibRigControllerTestAccess::addRadio(&unknownBackendCaps);

        startsTransmittingCaps = *source;
        startsTransmittingCaps.rig_model = RIG_MAKE_MODEL(RIG_DUMMY, 905);
        startsTransmittingCaps.model_name = "FreeDV Test Radio That Starts Transmitting";
        realSetPtt = startsTransmittingCaps.set_ptt;
        startsTransmittingCaps.set_ptt = [](RIG* rig, vfo_t vfo, ptt_t ptt) -> int {
            if (ptt == RIG_PTT_OFF && numUnkeyRequests++ == 0) return -RIG_EIO;
            return realSetPtt(rig, vfo, ptt);
        };
        if (rig_register(&startsTransmittingCaps) != RIG_OK) return false;
        HamlibRigControllerTestAccess::addRadio(&startsTransmittingCaps);

        return true;
    }
};

// Records the events raised by a controller. These are raised from the
// controller's own thread, so tests have to wait for them to show up.
struct Fixture
{
    std::mutex mtx;
    std::condition_variable cv;
    int numConnects = 0;
    int numDisconnects = 0;
    std::vector<std::string> errors;
    std::vector<bool> pttChanges;
    std::vector<uint64_t> frequencies;
    std::vector<IRigFrequencyController::Mode> modes;

    // Declared last so that it's destroyed (and its thread stopped) first.
    NeverReused<HamlibRigController> controllerStorage;
    HamlibRigController& controller;

    explicit Fixture(
        std::string const& rigName = DUMMY_RIG_NAME, 
        HamlibRigController::PttType pttType = HamlibRigController::PTT_VIA_CAT,
        bool restoreOnDisconnect = false, bool freqOnly = false, std::string const& serialPort = "",
        int serialRate = 0, int civHex = 0, std::string const& pttSerialPort = "",
        bool forceRtsOn = false, bool forceDtrOn = false)
        : controllerStorage(rigName, serialPort, serialRate, civHex, pttType, pttSerialPort, restoreOnDisconnect, freqOnly, forceRtsOn, forceDtrOn)
        , controller(*controllerStorage)
    {
        RadioFaults::clearAll();

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
        controller.onFreqModeChange += [this](IRigFrequencyController*, uint64_t freq, IRigFrequencyController::Mode mode) {
            record_([&]() { 
                frequencies.push_back(freq); 
                modes.push_back(mode);
            });
        };
    }

    // Waits until the given condition (evaluated with the lock held) is true.
    bool waitFor(std::function<bool()> const& condition)
    {
        std::unique_lock<std::mutex> lk(mtx);
        return cv.wait_for(lk, TIMEOUT, condition);
    }

    bool connect()
    {
        controller.connect();
        return waitFor([this]() { return numConnects == 1 || !errors.empty(); }) && errors.empty();
    }

    // Connects to a radio that can then be made to fail requests (see RadioFaults).
    bool connectToFaultyRadio()
    {
        if (!connect()) return false;
        onRadio([](RIG* rig) { RadioFaults::install(rig); });
        return true;
    }

    // Connects and puts the radio on a known frequency and mode.
    bool connectToFaultyRadioOn(uint64_t freq, IRigFrequencyController::Mode mode)
    {
        if (!connectToFaultyRadio()) return false;
        controller.setFrequency(freq);
        controller.setMode(mode);
        return radioReports(freq, mode);
    }

    // Waits for exactly one error mentioning the given text.
    bool reportsOneError(std::string const& text)
    {
        waitFor([this]() { return !errors.empty(); });
        waitUntilIdle();

        std::unique_lock<std::mutex> lk(mtx);
        if (errors.size() != 1 || errors[0].find(text) == std::string::npos)
        {
            std::cout << "[" << errors.size() << " error(s)";
            for (auto& err : errors) std::cout << ": \"" << err << "\"";
            std::cout << "; expected one mentioning \"" << text << "\"] ";
            return false;
        }
        return true;
    }

    bool noErrors()
    {
        waitUntilIdle();

        std::unique_lock<std::mutex> lk(mtx);
        if (!errors.empty())
        {
            std::cout << "[unexpected error: \"" << errors[0] << "\"] ";
            return false;
        }
        return true;
    }

    // What the radio itself is on, regardless of what the controller has reported.
    bool radioIsOn(uint64_t expectedFreq, rmode_t expectedMode)
    {
        freq_t freq = 0;
        rmode_t mode = RIG_MODE_NONE;
        onRadio([&](RIG* rig) {
            pbwidth_t width;
            rig_get_freq(rig, RIG_VFO_CURR, &freq);
            rig_get_mode(rig, RIG_VFO_CURR, &mode, &width);
        });
        if ((uint64_t)freq != expectedFreq || mode != expectedMode)
        {
            std::cout << "[radio is on " << (uint64_t)freq << " Hz (" << rig_strrmode(mode) << "); expected "
                      << expectedFreq << " Hz (" << rig_strrmode(expectedMode) << ")] ";
            return false;
        }
        return true;
    }

    // Asks the radio for its frequency/mode until it reports the expected ones.
    bool radioReports(uint64_t freq, IRigFrequencyController::Mode mode)
    {
        controller.requestCurrentFrequencyMode();
        bool result = waitFor([&]() { 
            return !frequencies.empty() && frequencies.back() == freq && modes.back() == mode; 
        });
        if (!result)
        {
            std::unique_lock<std::mutex> lk(mtx);
            std::cout << "[radio last reported ";
            if (frequencies.empty()) std::cout << "nothing";
            else std::cout << frequencies.back() << " Hz, mode " << modes.back();
            std::cout << "; expected " << freq << " Hz, mode " << mode << "] ";
        }
        return result;
    }

    // Waits for everything asked of the controller so far to be dealt with.
    void waitUntilIdle()
    {
        HamlibRigControllerTestAccess::onRadio(controller, [](RIG*) { });
    }

    void onRadio(std::function<void(RIG*)> const& fn)
    {
        HamlibRigControllerTestAccess::onRadio(controller, fn);
    }

    size_t numReports()
    {
        std::unique_lock<std::mutex> lk(mtx);
        return frequencies.size();
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

bool rigListIsPopulated()
{
    return 
        HamlibRigController::GetNumberSupportedRadios() > 0 &&
        HamlibRigController::RigNameToIndex(DUMMY_RIG_NAME) >= 0;
}

bool rigNamesMapBackToThemselves()
{
    int numRadios = HamlibRigController::GetNumberSupportedRadios();
    for (int index = 0; index < numRadios; index++)
    {
        // Note: names aren't guaranteed to be unique, so the index found may
        // be that of an earlier radio with the same name.
        auto name = HamlibRigController::RigIndexToName(index);
        int foundIndex = HamlibRigController::RigNameToIndex(name);
        if (name.empty() || foundIndex < 0 || foundIndex > index || 
            HamlibRigController::RigIndexToName(foundIndex) != name)
        {
            std::cout << "[index " << index << " (\"" << name << "\") maps to index " << foundIndex << "] ";
            return false;
        }
    }
    return true;
}

bool rigNameLookupIgnoresTrailingWhitespace()
{
    // Older configuration files may have these.
    int expected = HamlibRigController::RigNameToIndex(DUMMY_RIG_NAME);
    return 
        HamlibRigController::RigNameToIndex(DUMMY_RIG_NAME + " ") == expected &&
        HamlibRigController::RigNameToIndex(DUMMY_RIG_NAME + " \t\r\n") == expected;
}

bool unknownRigsAreReported()
{
    int numRadios = HamlibRigController::GetNumberSupportedRadios();
    return 
        HamlibRigController::RigNameToIndex("Not A Real Radio") == -1 &&
        HamlibRigController::RigNameToIndex("") == -1 &&
        HamlibRigController::RigIndexToName(numRadios) == "" &&
        HamlibRigController::RigIndexToName(numRadios + 1000) == "";
}

bool baudRateRangesAreSane()
{
    int numRadios = HamlibRigController::GetNumberSupportedRadios();
    for (int index = 0; index < numRadios; index++)
    {
        int minRate = HamlibRigController::GetMinimumSerialBaudRate(index);
        int maxRate = HamlibRigController::GetMaximumSerialBaudRate(index);
        if (minRate < 0 || maxRate < minRate)
        {
            std::cout << "[" << HamlibRigController::RigIndexToName(index) << ": " << minRate << " to " << maxRate << "] ";
            return false;
        }
    }
    return true;
}

bool canBeConstructedFromRigIndex()
{
    int index = HamlibRigController::RigNameToIndex(DUMMY_RIG_NAME);
    NeverReused<HamlibRigController> controllerStorage(
        index, std::string(""), 0, 0, HamlibRigController::PTT_VIA_CAT, std::string(""), false, false, false, false);
    HamlibRigController& controller = *controllerStorage;

    std::mutex mtx;
    std::condition_variable cv;
    bool connected = false;
    controller.onRigConnected += [&](IRigController*) {
        std::unique_lock<std::mutex> lk(mtx);
        connected = true;
        cv.notify_all();
    };
    controller.connect();

    std::unique_lock<std::mutex> lk(mtx);
    return cv.wait_for(lk, TIMEOUT, [&]() { return connected; });
}

bool connectsAndReportsCurrentFrequency()
{
    Fixture f;
    if (f.controller.isConnected()) return false;

    return 
        f.connect() && 
        f.controller.isConnected() &&
        f.waitFor([&]() { return !f.frequencies.empty() && f.frequencies.back() > 0; });
}

bool disconnects()
{
    Fixture f;
    if (!f.connect()) return false;

    f.controller.disconnect();
    return f.waitFor([&]() { return f.numDisconnects == 1; }) && !f.controller.isConnected();
}

bool canReconnectAfterDisconnecting()
{
    Fixture f;
    if (!f.connect()) return false;

    f.controller.disconnect();
    f.controller.connect();
    return 
        f.waitFor([&]() { return f.numConnects == 2; }) && 
        f.numDisconnects == 1 && f.controller.isConnected() && f.errors.empty();
}

bool reportsErrorForUnknownRig()
{
    Fixture f("Not A Real Radio");
    f.controller.connect();
    return 
        f.waitFor([&]() { return !f.errors.empty(); }) && 
        f.errors[0].find("Not A Real Radio") != std::string::npos &&
        !f.controller.isConnected() && f.numConnects == 0;
}

bool reportsErrorWhenAlreadyConnected()
{
    Fixture f;
    if (!f.connect()) return false;

    f.controller.connect();
    return 
        f.waitFor([&]() { return !f.errors.empty(); }) && 
        f.controller.isConnected() && f.numConnects == 1;
}

bool setsFrequencyAndMode()
{
    Fixture f;
    if (!f.connect()) return false;

    f.controller.setFrequency(14236000);
    f.controller.setMode(IRigFrequencyController::DIGU);
    if (!f.radioReports(14236000, IRigFrequencyController::DIGU)) return false;

    f.controller.setFrequency(7177000);
    f.controller.setMode(IRigFrequencyController::LSB);
    return f.radioReports(7177000, IRigFrequencyController::LSB) && f.errors.empty();
}

bool setsEachSupportedMode()
{
    Fixture f;
    if (!f.connect()) return false;
    f.controller.setFrequency(14236000);

    for (auto mode : { 
        IRigFrequencyController::USB, IRigFrequencyController::DIGU, 
        IRigFrequencyController::LSB, IRigFrequencyController::DIGL, 
        IRigFrequencyController::FM, IRigFrequencyController::DIGFM,
        IRigFrequencyController::AM })
    {
        f.controller.setMode(mode);
        if (!f.radioReports(14236000, mode)) return false;
    }
    return f.errors.empty();
}

// A frequency and/or mode requested before the radio's connected is meant to 
// be applied once it is.
bool appliesModeSetBeforeConnecting()
{
    Fixture f;
    f.controller.setMode(IRigFrequencyController::DIGL);
    if (!f.connect()) return false;

    f.controller.requestCurrentFrequencyMode();
    return f.waitFor([&]() { return !f.modes.empty() && f.modes.back() == IRigFrequencyController::DIGL; });
}

bool appliesFrequencySetBeforeConnecting()
{
    Fixture f;
    f.controller.setFrequency(14236000);
    if (!f.connect()) return false;

    f.controller.requestCurrentFrequencyMode();
    bool result = f.waitFor([&]() { return !f.frequencies.empty() && f.frequencies.back() == 14236000; });
    if (!result)
    {
        std::unique_lock<std::mutex> lk(f.mtx);
        std::cout << "[radio is on " << (f.frequencies.empty() ? 0 : f.frequencies.back()) << " Hz] ";
    }
    return result;
}

bool reportsPttChanges(HamlibRigController::PttType pttType)
{
    Fixture f(DUMMY_RIG_NAME, pttType);
    if (!f.connect()) return false;

    f.controller.ptt(true);
    if (!f.waitFor([&]() { return f.pttChanges.size() == 1; }) || !f.pttChanges[0]) return false;

    // Asking for the same state again isn't a change.
    f.controller.ptt(true);
    f.controller.ptt(false);
    return 
        f.waitFor([&]() { return f.pttChanges.size() >= 2; }) && 
        f.pttChanges == std::vector<bool> { true, false } && 
        f.controller.getRigResponseTimeMicroseconds() >= 0 &&
        f.errors.empty();
}

bool doesNotQueryRadioWhileTransmitting()
{
    // Some radios glitch their transmit audio (or don't respond at all) if
    // asked for their frequency during TX.
    Fixture f;
    if (!f.connect()) return false;
    f.controller.setFrequency(14236000);
    f.controller.setMode(IRigFrequencyController::USB);
    if (!f.radioReports(14236000, IRigFrequencyController::USB)) return false;

    f.controller.ptt(true);
    if (!f.waitFor([&]() { return f.pttChanges.size() == 1; })) return false;

    size_t numReports;
    {
        std::unique_lock<std::mutex> lk(f.mtx);
        numReports = f.frequencies.size();
    }
    f.controller.requestCurrentFrequencyMode();
    f.controller.requestCurrentFrequencyMode();

    // Requests are handled in order, so once PTT is seen to drop we know
    // that the above requests have been dealt with. Dropping PTT itself
    // triggers one query.
    f.controller.ptt(false);
    if (!f.waitFor([&]() { return f.pttChanges.size() == 2 && f.frequencies.size() > numReports; })) return false;

    std::unique_lock<std::mutex> lk(f.mtx);
    return f.frequencies.size() == numReports + 1;
}

bool changesFrequencyWhileTransmitting()
{
    Fixture f;
    if (!f.connect()) return false;
    f.controller.setFrequency(14236000);
    f.controller.setMode(IRigFrequencyController::USB);
    if (!f.radioReports(14236000, IRigFrequencyController::USB)) return false;

    f.controller.ptt(true);
    f.controller.setFrequency(14240000);
    f.controller.ptt(false);

    // PTT should be seen to change exactly twice (i.e. the controller briefly
    // unkeying the radio to change frequency isn't reported).
    return 
        f.radioReports(14240000, IRigFrequencyController::USB) &&
        f.pttChanges == std::vector<bool> { true, false } &&
        f.errors.empty();
}

// Checks what happens to the radio when FreeDV is done with it. The radio's
// last report before the connection closes tells us how it was left.
bool restoreOnDisconnect(bool restore, bool freqOnly)
{
    Fixture f(DUMMY_RIG_NAME, HamlibRigController::PTT_VIA_CAT, restore, freqOnly);
    if (!f.connect() || !f.waitFor([&]() { return !f.frequencies.empty(); })) return false;

    uint64_t origFreq;
    IRigFrequencyController::Mode origMode;
    {
        std::unique_lock<std::mutex> lk(f.mtx);
        origFreq = f.frequencies[0];
        origMode = f.modes[0];
    }

    // Something other than what the radio starts out on.
    const uint64_t NEW_FREQ = 14236000;
    auto newMode = origMode == IRigFrequencyController::DIGU ? IRigFrequencyController::LSB : IRigFrequencyController::DIGU;
    if (origFreq == NEW_FREQ || origMode == IRigFrequencyController::UNKNOWN)
    {
        std::cout << "[simulated radio starts on " << origFreq << " Hz, mode " << origMode << "] ";
        return false;
    }

    f.controller.setFrequency(NEW_FREQ);
    f.controller.setMode(newMode);
    if (!f.radioReports(NEW_FREQ, newMode)) return false;

    f.controller.disconnect();
    if (!f.waitFor([&]() { return f.numDisconnects == 1; })) return false;

    uint64_t expectedFreq = restore ? origFreq : NEW_FREQ;
    auto expectedMode = (restore && !freqOnly) ? origMode : newMode;

    std::unique_lock<std::mutex> lk(f.mtx);
    if (f.frequencies.back() != expectedFreq || f.modes.back() != expectedMode)
    {
        std::cout << "[radio left on " << f.frequencies.back() << " Hz, mode " << f.modes.back()
                  << "; expected " << expectedFreq << " Hz, mode " << expectedMode << "] ";
        return false;
    }
    return f.errors.empty();
}

bool restoresOriginalSettingsEachTimeItConnects()
{
    Fixture f(DUMMY_RIG_NAME, HamlibRigController::PTT_VIA_CAT, true, false);
    if (!f.connect() || !f.waitFor([&]() { return !f.frequencies.empty(); })) return false;
    uint64_t origFreq;
    IRigFrequencyController::Mode origMode;
    {
        std::unique_lock<std::mutex> lk(f.mtx);
        origFreq = f.frequencies[0];
        origMode = f.modes[0];
    }
    if (origMode == IRigFrequencyController::UNKNOWN)
    {
        std::cout << "[simulated radio starts in mode " << origMode << "] ";
        return false;
    }
    auto newMode = origMode == IRigFrequencyController::DIGU ? IRigFrequencyController::LSB : IRigFrequencyController::DIGU;

    for (uint64_t freq : { (uint64_t)14236000, (uint64_t)7177000 })
    {
        f.controller.setFrequency(freq);
        f.controller.setMode(newMode);
        f.controller.requestCurrentFrequencyMode();
        if (!f.waitFor([&]() { return f.frequencies.back() == freq && f.modes.back() == newMode; })) return false;

        int disconnectsSoFar = f.numDisconnects;
        f.controller.disconnect();
        if (!f.waitFor([&]() { return f.numDisconnects == disconnectsSoFar + 1; })) return false;
        {
            std::unique_lock<std::mutex> lk(f.mtx);
            if (f.frequencies.back() != origFreq || f.modes.back() != origMode)
            {
                std::cout << "[radio left on " << f.frequencies.back() << " Hz, mode " << f.modes.back()
                          << "; expected " << origFreq << " Hz, mode " << origMode << "] ";
                return false;
            }
        }

        f.controller.connect();
        if (!f.waitFor([&]() { return f.numConnects == disconnectsSoFar + 2; })) return false;
        f.waitUntilIdle();

        // The new connection must not reapply the previous session's mode.
        {
            std::unique_lock<std::mutex> lk(f.mtx);
            if (f.frequencies.back() != origFreq || f.modes.back() != origMode)
            {
                std::cout << "[reconnect reported " << f.frequencies.back() << " Hz, mode " << f.modes.back()
                          << "; expected " << origFreq << " Hz, mode " << origMode << "] ";
                return false;
            }
        }
    }

    std::unique_lock<std::mutex> lk(f.mtx);
    if (!f.errors.empty())
    {
        std::cout << "[error: " << f.errors[0] << "] ";
        return false;
    }
    return f.frequencies.back() == origFreq && f.modes.back() == origMode;
}

bool reportsErrorWhenRadioCannotBeOpened()
{
    // Any radio that's controlled over a serial port will do.
    std::string rigName;
    int numRadios = HamlibRigController::GetNumberSupportedRadios();
    for (int index = 0; index < numRadios && rigName.empty(); index++)
    {
        auto name = HamlibRigController::RigIndexToName(index);
        if (name.find("FT-817") != std::string::npos) rigName = name;
    }
    if (rigName.empty())
    {
        std::cout << "[no suitable radio in Hamlib's list] ";
        return false;
    }

    Fixture f(rigName, HamlibRigController::PTT_VIA_CAT, false, false, "/nonexistent/serial/port");
    f.controller.connect();
    if (!f.waitFor([&]() { return !f.errors.empty(); }))
    {
        std::cout << "[no error reported] ";
        return false;
    }

    f.waitUntilIdle();
    if (f.controller.isConnected() || f.numConnects != 0)
    {
        std::cout << "[reported as connected] ";
        return false;
    }

    // Nothing's left behind that would stop the user from trying again
    // (e.g. after plugging the radio in), and requests in the meantime are harmless.
    f.controller.ptt(true);
    f.controller.setFrequency(14236000);
    f.controller.requestCurrentFrequencyMode();
    f.controller.connect();
    if (!f.waitFor([&]() { return f.errors.size() >= 2; })) return false;
    f.waitUntilIdle();

    std::unique_lock<std::mutex> lk(f.mtx);
    for (auto& err : f.errors)
    {
        if (err.find("Already connected") != std::string::npos)
        {
            std::cout << "[failed connection was left open] ";
            return false;
        }
    }
    return !f.controller.isConnected() && f.numConnects == 0 && f.numDisconnects == 0 && f.pttChanges.empty();
}

bool reportsUnknownForOtherModes()
{
    Fixture f;
    if (!f.connect()) return false;
    f.controller.setFrequency(14236000);
    f.controller.setMode(IRigFrequencyController::USB);
    if (!f.radioReports(14236000, IRigFrequencyController::USB)) return false;

    // The operator switches to something FreeDV has no use for.
    f.onRadio([](RIG* rig) { rig_set_mode(rig, RIG_VFO_CURR, RIG_MODE_CW, RIG_PASSBAND_NOCHANGE); });
    if (!f.radioReports(14236000, IRigFrequencyController::UNKNOWN)) return false;

    // FreeDV can still put it back.
    f.controller.setMode(IRigFrequencyController::USB);
    return f.radioReports(14236000, IRigFrequencyController::USB) && f.errors.empty();
}

bool ignoresRequestForCurrentFrequency()
{
    Fixture f;
    if (!f.connect()) return false;
    f.controller.setFrequency(14236000);
    f.controller.setMode(IRigFrequencyController::USB);
    if (!f.radioReports(14236000, IRigFrequencyController::USB)) return false;
    f.waitUntilIdle();

    // Setting a frequency normally results in the radio being asked to confirm it.
    size_t numReports = f.numReports();
    f.controller.setFrequency(14236000);
    f.controller.setMode(IRigFrequencyController::USB);
    f.waitUntilIdle();
    if (f.numReports() != numReports)
    {
        std::cout << "[radio was sent a frequency/mode it was already on] ";
        return false;
    }

    f.controller.setFrequency(14240000);
    f.waitUntilIdle();
    return f.numReports() > numReports && f.radioReports(14240000, IRigFrequencyController::USB);
}

bool followsFrequencyChangedOnRadio()
{
    Fixture f;
    if (!f.connect()) return false;
    f.controller.setFrequency(14236000);
    f.controller.setMode(IRigFrequencyController::USB);
    if (!f.radioReports(14236000, IRigFrequencyController::USB)) return false;

    // The operator turns the dial...
    f.onRadio([](RIG* rig) { rig_set_freq(rig, RIG_VFO_CURR, 14240000); });
    if (!f.radioReports(14240000, IRigFrequencyController::USB)) return false;

    // ...so going back to the earlier frequency is a real change again.
    f.controller.setFrequency(14236000);
    return f.radioReports(14236000, IRigFrequencyController::USB) && f.errors.empty();
}

bool tellsWhenAReportedFrequencyPredatesTheOneAskedFor()
{
    Fixture f;
    if (!f.connect()) return false;
    f.controller.setFrequency(14236000);
    f.controller.setMode(IRigFrequencyController::USB);
    if (!f.radioReports(14236000, IRigFrequencyController::USB)) return false;

    // Nothing's been asked for that hasn't been seen.
    if (f.controller.isFrequencyChangePending(14236000) || f.controller.isFrequencyChangePending(7177000))
    {
        std::cout << "[a change was pending with none asked for] ";
        return false;
    }

    // Once a new frequency's been asked for, the old one (read from the
    // radio before the change was made) is out of date...
    f.controller.setFrequency(7177000);
    if (!f.controller.isFrequencyChangePending(14236000))
    {
        std::cout << "[the frequency before the one asked for was taken as current] ";
        return false;
    }

    // ...until the radio's seen to be on the new one, after which anything
    // goes again (the operator turning the dial, say).
    if (f.controller.isFrequencyChangePending(7177000))
    {
        std::cout << "[the frequency asked for was taken as out of date] ";
        return false;
    }
    if (f.controller.isFrequencyChangePending(14236000))
    {
        std::cout << "[a change was still pending once the radio was on the frequency asked for] ";
        return false;
    }
    return f.radioReports(7177000, IRigFrequencyController::USB) && f.errors.empty();
}

bool stopsWaitingForAFrequencyTheRadioNeverReports()
{
    Fixture f;
    if (!f.connect()) return false;

    // A radio that can't be tuned exactly where it was asked to be, or is
    // tuned elsewhere straight away, isn't ignored for long.
    f.controller.setFrequency(14236000);
    if (!f.controller.isFrequencyChangePending(14240000))
    {
        std::cout << "[no change was pending just after asking for one] ";
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(3500));
    if (f.controller.isFrequencyChangePending(14240000))
    {
        std::cout << "[still waiting for the frequency asked for after 3.5 s] ";
        return false;
    }
    return true;
}

bool usesWhicheverVfoIsSelected()
{
    Fixture f;
    if (!f.connect()) return false;
    f.controller.setFrequency(14236000);
    f.controller.setMode(IRigFrequencyController::USB);
    if (!f.radioReports(14236000, IRigFrequencyController::USB)) return false;

    int result = RIG_OK;
    f.onRadio([&](RIG* rig) { result = rig_set_vfo(rig, RIG_VFO_B); });
    if (result != RIG_OK)
    {
        std::cout << "[simulated radio would not select VFO B] ";
        return false;
    }

    f.controller.setFrequency(7177000);
    f.controller.setMode(IRigFrequencyController::LSB);
    if (!f.radioReports(7177000, IRigFrequencyController::LSB)) return false;

    // VFO A should be as it was.
    freq_t freqA = 0;
    freq_t freqB = 0;
    rmode_t modeA = RIG_MODE_NONE;
    vfo_t vfo = RIG_VFO_NONE;
    f.onRadio([&](RIG* rig) {
        pbwidth_t width;
        rig_get_vfo(rig, &vfo);
        rig_get_freq(rig, RIG_VFO_A, &freqA);
        rig_get_freq(rig, RIG_VFO_B, &freqB);
        rig_get_mode(rig, RIG_VFO_A, &modeA, &width);
    });
    // (Some versions of Hamlib call the simulated radio's second VFO "Sub".)
    if ((vfo != RIG_VFO_B && vfo != RIG_VFO_SUB) || freqA != 14236000 || freqB != 7177000 || modeA != RIG_MODE_USB)
    {
        std::cout << "[VFO " << rig_strvfo(vfo) << " selected, A = " << (uint64_t)freqA << " Hz ("
                  << rig_strrmode(modeA) << "), B = " << (uint64_t)freqB << " Hz] ";
        return false;
    }
    return f.errors.empty();
}

bool leavesMemoryChannelToChangeFrequency()
{
    Fixture f;
    if (!f.connect()) return false;

    // Note: Hamlib reports an error here as it can't read back the memory
    // channel's frequency, but the simulated radio does switch over.
    vfo_t vfoBefore = RIG_VFO_NONE;
    f.onRadio([&](RIG* rig) {
        rig_set_vfo(rig, RIG_VFO_MEM);
        rig_get_vfo(rig, &vfoBefore);
    });
    if (vfoBefore != RIG_VFO_MEM)
    {
        std::cout << "[simulated radio would not select a memory channel] ";
        return false;
    }

    // A memory channel's frequency can't be changed, so the radio is switched to VFO A first.
    f.controller.setFrequency(14236000);
    f.controller.setMode(IRigFrequencyController::DIGU);
    if (!f.radioReports(14236000, IRigFrequencyController::DIGU)) return false;

    freq_t freqA = 0;
    vfo_t vfo = RIG_VFO_NONE;
    f.onRadio([&](RIG* rig) {
        rig_get_vfo(rig, &vfo);
        rig_get_freq(rig, RIG_VFO_A, &freqA);
    });
    // (Some versions of Hamlib call the simulated radio's first VFO "Main".)
    if ((vfo != RIG_VFO_A && vfo != RIG_VFO_MAIN) || freqA != 14236000)
    {
        std::cout << "[VFO " << rig_strvfo(vfo) << " selected, A = " << (uint64_t)freqA << " Hz] ";
        return false;
    }
    return f.errors.empty();
}

bool ignoresRequestsWhenNotConnected()
{
    Fixture f;
    f.controller.ptt(true);
    f.controller.requestCurrentFrequencyMode();

    // Requests are handled in order, so by the time the connect attempt is
    // done, we know that the above have been dealt with.
    return f.connect() && f.pttChanges.empty();
}

bool changesModeWhileTransmitting()
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;

    f.controller.ptt(true);
    f.controller.setMode(IRigFrequencyController::DIGU);
    f.waitUntilIdle();

    // The radio's left transmitting, and the controller briefly unkeying it
    // to change mode isn't reported.
    ptt_t ptt = RIG_PTT_OFF;
    f.onRadio([&](RIG* rig) { rig_get_ptt(rig, RIG_VFO_CURR, &ptt); });
    if (ptt == RIG_PTT_OFF)
    {
        std::cout << "[radio was left receiving] ";
        return false;
    }

    f.controller.ptt(false);
    return 
        f.radioReports(14236000, IRigFrequencyController::DIGU) &&
        f.pttChanges == std::vector<bool> { true, false } &&
        f.noErrors();
}

bool reportsErrorWhenPttFails()
{
    Fixture f;
    if (!f.connectToFaultyRadio()) return false;

    RadioFaults::setPtt.always();
    f.controller.ptt(true);
    if (!f.reportsOneError("Cannot set PTT")) return false;

    // The user's told once, not every time (PTT may be retried many times a second).
    f.controller.ptt(true);
    f.controller.ptt(true);
    if (!f.reportsOneError("Cannot set PTT")) return false;

    // Nothing's reported as having changed until the radio actually does.
    {
        std::unique_lock<std::mutex> lk(f.mtx);
        if (!f.pttChanges.empty())
        {
            std::cout << "[PTT reported as changed despite the error] ";
            return false;
        }
    }

    RadioFaults::setPtt.clear();
    f.controller.ptt(true);
    return f.waitFor([&]() { return f.pttChanges == std::vector<bool> { true }; });
}

bool reportsErrorWhenFrequencyCannotBeSet()
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;

    RadioFaults::setFreq.always();
    f.controller.setFrequency(7177000);
    if (!f.reportsOneError("Could not set current frequency")) return false;

    // The radio's still where it was, and that's what gets reported.
    RadioFaults::setFreq.clear();
    return f.radioIsOn(14236000, RIG_MODE_USB) && f.radioReports(14236000, IRigFrequencyController::USB);
}

bool reportsErrorWhenModeCannotBeSet()
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;

    RadioFaults::setMode.always();
    f.controller.setMode(IRigFrequencyController::LSB);
    if (!f.reportsOneError("Could not set mode")) return false;

    RadioFaults::setMode.clear();
    return f.radioIsOn(14236000, RIG_MODE_USB) && f.radioReports(14236000, IRigFrequencyController::USB);
}

// Some radios reject requests that name a specific VFO. When that happens the
// controller tries again without naming one before giving up.
bool retriesFrequencyAndModeWithoutNamingVfo()
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;

    RadioFaults::setFreq.failAfter(0, 1);
    f.controller.setFrequency(7177000);
    if (!f.radioReports(7177000, IRigFrequencyController::USB)) return false;

    RadioFaults::setMode.failAfter(0, 1);
    f.controller.setMode(IRigFrequencyController::LSB);
    if (!f.radioReports(7177000, IRigFrequencyController::LSB)) return false;

    RadioFaults::getMode.failAfter(0, 1);
    f.controller.setFrequency(14236000);
    if (!f.radioReports(14236000, IRigFrequencyController::LSB)) return false;

    RadioFaults::getFreq.failAfter(0, 1);
    f.controller.setFrequency(14240000);
    return f.radioReports(14240000, IRigFrequencyController::LSB) && f.noErrors();
}

// The radio's polled every few seconds, so a single failure isn't worth
// bothering the user with -- but a radio that's stopped responding is.
bool reportsErrorOnceRadioStopsAnswering(RadioFaults::Fault& fault, std::string const& expectedError)
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;
    f.waitUntilIdle();
    size_t numReports = f.numReports();

    // One failure, then the radio's fine again.
    fault.always();
    f.controller.requestCurrentFrequencyMode();
    if (!f.noErrors()) return false;
    fault.clear();
    f.controller.requestCurrentFrequencyMode();
    if (!f.waitFor([&]() { return f.frequencies.size() == numReports + 1; })) return false;

    // That success means the next failure is the first again.
    fault.always();
    f.controller.requestCurrentFrequencyMode();
    if (!f.noErrors()) return false;

    f.controller.requestCurrentFrequencyMode();
    if (!f.reportsOneError(expectedError)) return false;

    // Nothing was reported for the requests that failed.
    if (f.numReports() != numReports + 1)
    {
        std::cout << "[" << (f.numReports() - numReports) << " frequency report(s); expected 1] ";
        return false;
    }

    f.controller.requestCurrentFrequencyMode();
    return f.reportsOneError(expectedError);
}

bool usesCurrentVfoWhenRadioWontSayWhichIsSelected()
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;

    RadioFaults::getVfo.always();
    f.controller.setFrequency(7177000);
    f.controller.setMode(IRigFrequencyController::LSB);
    f.waitUntilIdle();
    RadioFaults::getVfo.clear();

    return f.radioIsOn(7177000, RIG_MODE_LSB) && f.noErrors();
}

// The radio has to stop transmitting for its frequency/mode to be changed.
// If it won't, the change isn't attempted.
bool doesNotChangeFrequencyOrModeIfRadioWontStopTransmitting()
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;
    f.controller.ptt(true);
    if (!f.waitFor([&]() { return f.pttChanges.size() == 1; })) return false;

    RadioFaults::setPtt.always();
    f.controller.setFrequency(7177000);
    if (!f.reportsOneError("Could not disable PTT prior to frequency change")) return false;

    f.controller.setMode(IRigFrequencyController::LSB);
    f.waitUntilIdle();
    RadioFaults::setPtt.clear();

    return f.radioIsOn(14236000, RIG_MODE_USB) && f.reportsOneError("Could not disable PTT prior to frequency change");
}

bool reportsErrorIfModeChangeBlockedByTransmit()
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;
    f.controller.ptt(true);
    if (!f.waitFor([&]() { return f.pttChanges.size() == 1; })) return false;

    RadioFaults::setPtt.always();
    f.controller.setMode(IRigFrequencyController::LSB);
    bool result = f.reportsOneError("Could not disable PTT prior to mode change");
    RadioFaults::setPtt.clear();

    return result && f.radioIsOn(14236000, RIG_MODE_USB);
}

// Here the radio stops transmitting and changes frequency/mode, but then
// won't start transmitting again.
bool reportsErrorIfTransmitCannotResume(bool changeMode)
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;
    f.controller.ptt(true);
    if (!f.waitFor([&]() { return f.pttChanges.size() == 1; })) return false;

    RadioFaults::setPtt.failAfter(1, RadioFaults::ALWAYS);
    bool result;
    if (changeMode)
    {
        f.controller.setMode(IRigFrequencyController::LSB);
        result = f.reportsOneError("Could not enable PTT after mode change");
    }
    else
    {
        f.controller.setFrequency(7177000);
        result = f.reportsOneError("Could not enable PTT after frequency change");
    }
    RadioFaults::setPtt.clear();

    return result && f.radioIsOn(changeMode ? 14236000 : 7177000, changeMode ? RIG_MODE_LSB : RIG_MODE_USB);
}

// Changing mode is followed by the frequency being sent again (some radios
// shift frequency on a mode change). That's a nicety; its failure isn't an error.
bool modeChangeSurvivesFailedFrequencyResend()
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;

    RadioFaults::setFreq.always();
    f.controller.setMode(IRigFrequencyController::LSB);
    f.waitUntilIdle();
    RadioFaults::setFreq.clear();

    return f.radioReports(14236000, IRigFrequencyController::LSB) && f.noErrors();
}

// Attempts a connection with the given settings and returns what Hamlib was told.
SettingsProbe::Settings settingsGivenToHamlib(
    HamlibRigController::PttType pttType = HamlibRigController::PTT_VIA_CAT, int serialRate = 0, 
    std::string const& pttSerialPort = "", bool forceRtsOn = false, bool forceDtrOn = false, 
    std::string const& rigName = SettingsProbe::SERIAL_RIG_NAME, int civHex = 0)
{
    SettingsProbe::captured.clear();

    Fixture f(rigName, pttType, false, false, SettingsProbe::SERIAL_PORT, serialRate, civHex, pttSerialPort, forceRtsOn, forceDtrOn);
    f.controller.connect();
    f.waitFor([&]() { return !f.errors.empty(); });
    f.waitUntilIdle();
    return SettingsProbe::captured;
}

bool hasSetting(SettingsProbe::Settings const& settings, std::string const& name, std::string const& expected)
{
    auto iter = settings.find(name);
    std::string actual = iter != settings.end() ? iter->second : "(not available)";

#if defined(_WIN32)
    // On Windows, Hamlib puts this in front of the names of some ports so
    // that COM10 and above can be opened.
    const std::string devicePrefix = "\\\\.\\";
    if (actual.rfind(devicePrefix, 0) == 0 && expected.rfind(devicePrefix, 0) != 0)
    {
        actual = actual.substr(devicePrefix.size());
    }
#endif // defined(_WIN32)

    if (actual != expected)
    {
        std::cout << "[" << name << " was \"" << actual << "\", expected \"" << expected << "\"] ";
        return false;
    }
    return true;
}

bool passesSerialPortAndSpeedToHamlib()
{
    auto defaults = settingsGivenToHamlib();
    auto settings = settingsGivenToHamlib(HamlibRigController::PTT_VIA_CAT, 19200);

    // A speed of zero means "use the radio's default", so there has to be one.
    return 
        hasSetting(settings, "rig_pathname", SettingsProbe::SERIAL_PORT) &&
        hasSetting(settings, "serial_speed", "19200") &&
        hasSetting(defaults, "rig_pathname", SettingsProbe::SERIAL_PORT) &&
        defaults["serial_speed"] != "19200" && atoi(defaults["serial_speed"].c_str()) > 0;
}

bool passesPttTypeToHamlib()
{
    bool result = true;
    result &= hasSetting(settingsGivenToHamlib(HamlibRigController::PTT_VIA_CAT), "ptt_type", "RIG");
    result &= hasSetting(settingsGivenToHamlib(HamlibRigController::PTT_VIA_CAT_DATA), "ptt_type", "RIGMICDATA");
    result &= hasSetting(settingsGivenToHamlib(HamlibRigController::PTT_VIA_NONE), "ptt_type", "None");
    result &= hasSetting(settingsGivenToHamlib(HamlibRigController::PTT_VIA_RTS), "ptt_type", "RTS");
    result &= hasSetting(settingsGivenToHamlib(HamlibRigController::PTT_VIA_DTR), "ptt_type", "DTR");
    return result;
}

bool sharesSerialPortWhenKeyingWithControlLines()
{
    // The same port carries CAT commands, so Hamlib mustn't open it exclusively for PTT.
    auto cat = settingsGivenToHamlib(HamlibRigController::PTT_VIA_CAT);
    return 
        hasSetting(settingsGivenToHamlib(HamlibRigController::PTT_VIA_RTS), "ptt_share", "1") &&
        hasSetting(settingsGivenToHamlib(HamlibRigController::PTT_VIA_DTR), "ptt_share", "1") &&
        cat["ptt_share"] != "1";
}

bool usesSeparatePttPortOnlyForControlLines()
{
    const std::string PTT_PORT = "/nonexistent/ptt/port";
    auto withoutPttPort = settingsGivenToHamlib(HamlibRigController::PTT_VIA_RTS);

    return 
        hasSetting(settingsGivenToHamlib(HamlibRigController::PTT_VIA_RTS, 0, PTT_PORT), "ptt_pathname", PTT_PORT) &&
        hasSetting(settingsGivenToHamlib(HamlibRigController::PTT_VIA_DTR, 0, PTT_PORT), "ptt_pathname", PTT_PORT) &&
        // CAT commands only ever go to the radio's own port...
        hasSetting(settingsGivenToHamlib(HamlibRigController::PTT_VIA_CAT, 0, PTT_PORT), "ptt_pathname", withoutPttPort["ptt_pathname"]) &&
        // ...and naming the radio's port again isn't a separate port.
        hasSetting(settingsGivenToHamlib(HamlibRigController::PTT_VIA_RTS, 0, SettingsProbe::SERIAL_PORT), "ptt_pathname", withoutPttPort["ptt_pathname"]);
}

bool forcesControlLinesOnWhenAsked()
{
    auto neither = settingsGivenToHamlib();
    auto rts = settingsGivenToHamlib(HamlibRigController::PTT_VIA_CAT, 0, "", true, false);
    auto dtr = settingsGivenToHamlib(HamlibRigController::PTT_VIA_CAT, 0, "", false, true);
    auto both = settingsGivenToHamlib(HamlibRigController::PTT_VIA_CAT, 0, "", true, true);

    return 
        neither["rts_state"] != "ON" && neither["dtr_state"] != "ON" &&
        hasSetting(rts, "rts_state", "ON") && hasSetting(rts, "dtr_state", neither["dtr_state"]) &&
        hasSetting(dtr, "dtr_state", "ON") && hasSetting(dtr, "rts_state", neither["rts_state"]) &&
        hasSetting(both, "rts_state", "ON") && hasSetting(both, "dtr_state", "ON");
}

bool passesCivAddressToIcomRadios()
{
    auto settings = settingsGivenToHamlib(
        HamlibRigController::PTT_VIA_CAT, 0, "", false, false, SettingsProbe::ICOM_RIG_NAME, 0x7C);
    return hasSetting(settings, "civaddr", "124");
}

// Otherwise it can take a long time to begin transmitting (or to tell the
// user that something's wrong) if the radio isn't answering.
bool limitsTimeToWaitForRadio()
{
    Fixture f(SettingsProbe::SLOW_RIG_NAME);
    if (!f.connect()) return false;

    // What matters is the limit in force once connected: Hamlib may put the
    // radio's own default back while opening it.
    std::string timeout;
    bool couldRead = false;
    f.onRadio([&](RIG* rig) { couldRead = SettingsProbe::read(rig, "timeout", timeout); });

    int timeoutMs = atoi(timeout.c_str());
    if (!couldRead || timeoutMs <= 0 || timeoutMs > 625)
    {
        std::cout << "[timeout was \"" << timeout << "\" ms] ";
        return false;
    }
    return f.noErrors();
}

bool destroysCleanlyWhileConnected()
{
    for (int i = 0; i < 2; i++)
    {
        Fixture f;
        if (!f.connect()) return false;
        if (i % 2 == 0) f.controller.ptt(true);
    }
    return true;
}

// Hamlib's own log messages all end up in FreeDV's log (see
// InitializeHamlibLibrary). Every level it can raise, in any shape of
// message, has to turn into a log line without tripping anything up.
bool logsHamlibDebugMessagesAtEachLevel()
{
    // Hamlib's rig_debug macro only adds bookkeeping FreeDV doesn't use;
    // call the function whose messages FreeDV's logging callback receives.
#ifdef rig_debug
#undef rig_debug
#endif

    fflush(stdout);
    fflush(stderr);
    // In binary mode, so that Windows doesn't add carriage returns of its
    // own to what's captured.
    auto path = (std::filesystem::temp_directory_path() / ("freedv-hamlib-logs-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt")).string();
    FILE* capture = fopen(path.c_str(), "w+b");
    if (capture == nullptr)
    {
        std::cout << "[could not create " << path << "] ";
        return false;
    }
    int savedOut = dup(fileno(stdout));
    int savedErr = dup(fileno(stderr));
    dup2(fileno(capture), fileno(stdout));
    dup2(fileno(capture), fileno(stderr));

    // The async logger's thread reads the quiet flag, which isn't atomic;
    // wait for it to go idle first so that turning logging back on doesn't
    // race with it.
    ulog_async_flush();
    ulog_set_quiet(false);
    rig_debug(RIG_DEBUG_BUG, "FreeDV unit test: a bug\n");
    rig_debug(RIG_DEBUG_WARN, "FreeDV unit test: a warning\r");
    rig_debug(RIG_DEBUG_VERBOSE, "FreeDV unit test: no line ending\r");
    rig_debug(RIG_DEBUG_ERR, "FreeDV unit test: an error\n");
    rig_debug(RIG_DEBUG_ERR, "");

    // RIG_DEBUG_TRACE is above the level Hamlib normally runs at; FreeDV
    // logs anything it hasn't put in a category of its own at that level.
    rig_set_debug(RIG_DEBUG_TRACE);
    rig_debug(RIG_DEBUG_TRACE, "FreeDV unit test: a trace");
    rig_set_debug(RIG_DEBUG_VERBOSE);
    ulog_async_flush();
    ulog_set_quiet(true);
    fflush(stdout);
    fflush(stderr);

    dup2(savedOut, fileno(stdout));
    dup2(savedErr, fileno(stderr));
    close(savedOut);
    close(savedErr);

    std::string logs;
    {
        std::ifstream file(path, std::ios::binary);
        logs.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        fclose(capture);
        remove(path.c_str());
    }

    // Every level has to produce a log line, whatever shape the message was.
    for (const char* message : {"a bug", "a warning", "no line ending", "an error", "a trace"})
    {
        if (logs.find(std::string("FreeDV unit test: ") + message) == std::string::npos)
        {
            std::cout << "[no log line for \"" << message << "\"; captured " << logs.size() << " byte(s): \""
                      << logs.substr(0, 200) << "\"] ";
            return false;
        }
    }

    // The message tidying mustn't let stray control characters through.
    if (logs.find('\r') != std::string::npos)
    {
        std::cout << "[carriage return left in a log line] ";
        return false;
    }

    // Logging must leave the library usable.
    Fixture f;
    return f.connect();
}

// Radios can share a full name. The controller sorts by name and settles
// ties by model number, so identically named radios end up side by side.
bool sortsIdenticallyNamedRadiosTogether()
{
    int index = HamlibRigController::RigNameToIndex(CustomRadios::tieRigName);
    if (index < 0)
    {
        std::cout << "[identically named radios missing from the list] ";
        int hamlibCount = 0;
        rig_list_foreach([](const rig_caps*, void* count) -> int {
            (*static_cast<int*>(count))++; return 1; }, &hamlibCount);
        std::cout << "[want \"" << CustomRadios::tieRigName << "\" total "
                  << HamlibRigController::GetNumberSupportedRadios()
                  << " hamlibCount " << hamlibCount << "; found:";
        for (int i = 0; i < HamlibRigController::GetNumberSupportedRadios(); i++)
            if (HamlibRigController::RigIndexToName(i).find("Tie") != std::string::npos)
                std::cout << " [" << i << "]\"" << HamlibRigController::RigIndexToName(i) << "\"";
        std::cout << "] ";
        return false;
    }

    // All three were named the same, so the list must hold them together.
    if (HamlibRigController::RigIndexToName(index + 1) != CustomRadios::tieRigName ||
        HamlibRigController::RigIndexToName(index + 2) != CustomRadios::tieRigName)
    {
        std::cout << "[identically named radios were not sorted together] ";
        return false;
    }

    // Equal names are settled by model number: the first sorts before the
    // last, but the last never before the first.
    return HamlibRigControllerTestAccess::radioSortsBefore(&CustomRadios::tieCaps[0], &CustomRadios::tieCaps[2]) &&
           !HamlibRigControllerTestAccess::radioSortsBefore(&CustomRadios::tieCaps[2], &CustomRadios::tieCaps[0]);
}

// Radios with unusual (or no) ports have to be recognised as such, as the
// settings dialogs show different options for them.
bool reportsPortTypesForUnusualRadios()
{
    bool sawUsb = false, sawUdp = false, sawNoPort = false;

    for (int index = 0; index < HamlibRigController::GetNumberSupportedRadios(); index++)
    {
        std::string name = HamlibRigController::RigIndexToName(index);
        if (name == CustomRadios::USB_PORT_RIG_NAME)
            sawUsb = HamlibRigController::GetRigPortType(index) == HamlibRigController::PORT_USB;
        if (name == CustomRadios::UDP_PORT_RIG_NAME)
            sawUdp = HamlibRigController::GetRigPortType(index) == HamlibRigController::PORT_NETWORK;
        if (name == CustomRadios::NO_PORT_RIG_NAME)
            sawNoPort = HamlibRigController::GetRigPortType(index) == HamlibRigController::PORT_OTHER;
    }

    return sawUsb && sawUdp && sawNoPort;
}

// A radio's default port is whatever its model knows about; a model Hamlib
// has never heard of has no default to give.
bool leavesDefaultPathnameEmptyForUnknownRadios()
{
    int index = HamlibRigController::RigNameToIndex(CustomRadios::UNKNOWN_BACKEND_RIG_NAME);
    if (index < 0)
    {
        std::cout << "[unknown backend radio missing from the list] ";
        return false;
    }

    if (!HamlibRigController::GetDefaultRigPathname(index).empty())
    {
        std::cout << "[unknown radio reported a default port] ";
        return false;
    }

    // A model that knows its own serial port gives it as the default.
    std::string knownPathname = HamlibRigController::GetDefaultRigPathname(
        HamlibRigController::RigNameToIndex("Yaesu FT-817"));
    if (knownPathname.empty())
    {
        std::cout << "[FT-817 reported no default port] ";
        return false;
    }
    return true;
}

// Some radios claim they can't be unkeyed when asked to connect because
// they're already transmitting. That's worth a log line, not an error box,
// and shouldn't stop the connection.
bool connectsEvenIfRadioRefusesToUnkeyFirst()
{
    Fixture f(CustomRadios::STARTS_TRANSMITTING_RIG_NAME);
    if (!f.connect()) return false;
    if (!f.noErrors()) return false;

    // Having connected anyway, the controller can key it normally.
    f.controller.ptt(true);
    if (!f.waitFor([&]() { return !f.pttChanges.empty(); })) return false;

    return f.pttChanges == std::vector<bool> { true } && f.noErrors();
}

// Frequency and mode changes unkey the radio, apply the change, then re-key
// it. Whichever step fails, a user already told about a problem mustn't be
// told again for every further attempt.
bool reportsPttFaultDuringChangesOnlyOnce()
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;

    f.controller.ptt(true);
    if (!f.waitFor([&]() { return f.pttChanges.size() == 1; })) return false;

    // Unkeying fails: one error however often a change is then attempted.
    RadioFaults::setPtt.always();
    f.controller.setFrequency(7177000);
    f.controller.setFrequency(7100000);
    f.controller.setMode(IRigFrequencyController::LSB);
    if (!f.reportsOneError("Could not disable PTT prior to frequency change")) return false;

    // Unkeying works but re-keying afterwards doesn't; the change is applied,
    // but still nothing new is reported.
    RadioFaults::setPtt.failAfter(1, RadioFaults::ALWAYS);
    f.controller.setFrequency(7074000);
    f.waitUntilIdle();

    RadioFaults::setPtt.failAfter(1, RadioFaults::ALWAYS);
    f.controller.setMode(IRigFrequencyController::DIGU);
    f.waitUntilIdle();

    return f.radioIsOn(7074000, RIG_MODE_PKTUSB) &&
           f.reportsOneError("Could not disable PTT prior to frequency change");
}

// Keying through the data channel survives the brief unkeying needed to
// change frequency or mode: the radio has to come back up on the data
// channel it was keyed with.
bool changesFrequencyAndModeWhileKeyingDataChannel()
{
    Fixture f(DUMMY_RIG_NAME, HamlibRigController::PTT_VIA_CAT_DATA);
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;

    f.controller.ptt(true);
    if (!f.waitFor([&]() { return f.pttChanges.size() == 1; })) return false;

    f.controller.setFrequency(7177000);
    f.controller.setMode(IRigFrequencyController::LSB);
    if (!f.radioIsOn(7177000, RIG_MODE_LSB)) return false;

    ptt_t ptt = RIG_PTT_OFF;
    f.onRadio([&](RIG* rig) { rig_get_ptt(rig, RIG_VFO_CURR, &ptt); });
    if (ptt != RIG_PTT_ON_DATA)
    {
        std::cout << "[radio was keyed as " << int(ptt) << " instead of the data channel] ";
        return false;
    }
    return f.noErrors();
}

// A radio that says VFO information is currently unavailable is a different
// situation from one that outright refuses the request; either way the
// change has to be made using the VFO the controller already knows.
bool setsFrequencyDespiteUnavailableVfoInformation()
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;

    RadioFaults::getVfoUnavailable = true;
    RadioFaults::getVfo.always();
    f.controller.setFrequency(7177000);
    bool reported = f.radioReports(7177000, IRigFrequencyController::USB);
    RadioFaults::getVfo.clear();
    RadioFaults::getVfoUnavailable = false;

    return reported && f.noErrors();
}

// The frequency still has to be changed when the radio refuses to leave a
// memory channel, where its frequency can't be adjusted.
bool stillChangesFrequencyWhenRadioWontLeaveMemory()
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;

    f.onRadio([](RIG* rig) { rig_set_vfo(rig, RIG_VFO_MEM); });
    RadioFaults::setVfo.always();

    f.controller.setFrequency(7177000);
    f.waitUntilIdle();
    RadioFaults::setVfo.clear();

    freq_t freq = 0;
    f.onRadio([&](RIG* rig) { rig_get_freq(rig, RIG_VFO_CURR, &freq); });
    if ((uint64_t)freq != 7177000)
    {
        std::cout << "[radio still on " << (uint64_t)freq << " Hz; expected 7177000 Hz] ";
        return false;
    }
    return true;
}

// Zero isn't a frequency; the radio is to be left exactly as it was.
bool leavesFrequencyAloneWhenNoneIsAskedFor()
{
    Fixture f;
    if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;

    f.controller.setFrequency(0);
    f.waitUntilIdle();

    return f.radioIsOn(14236000, RIG_MODE_USB) && f.noErrors();
}

// A radio that won't accept changes reports each failed knob once per
// connection, however many attempts are made.
bool reportsFrequencyAndModeFaultsOnceEach()
{
    {
        Fixture f;
        if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;

        RadioFaults::setFreq.always();
        f.controller.setFrequency(7177000);
        f.controller.setFrequency(7100000);
        f.controller.setFrequency(7074000);
        if (!f.reportsOneError("Could not set current frequency")) return false;
    }

    {
        Fixture f;
        if (!f.connectToFaultyRadioOn(14236000, IRigFrequencyController::USB)) return false;

        RadioFaults::setMode.always();
        f.controller.setMode(IRigFrequencyController::LSB);
        f.controller.setMode(IRigFrequencyController::DIGU);
        if (!f.reportsOneError("Could not set mode frequency")) return false;

        // The connection has reported its one failure already; a different
        // kind of failure after that is dealt with quietly.
        RadioFaults::setMode.clear();
        RadioFaults::setFreq.always();
        f.controller.setFrequency(7177000);
        f.waitUntilIdle();

        std::unique_lock<std::mutex> lk(f.mtx);
        if (f.errors.size() != 1 || f.errors[0].find("Could not set mode") == std::string::npos)
        {
            std::cout << "[" << f.errors.size() << " error(s):";
            for (auto& err : f.errors) std::cout << " \"" << err << "\"";
            std::cout << "; expected only the mode error] ";
            return false;
        }
    }
    return true;
}

}

int main()
{
    // Hamlib's own debug output is routed through the logger and is very verbose.
    ulog_set_quiet(true);
    if (!CustomRadios::addTieRadios())
    {
        std::cout << "Could not add identically named test radios" << std::endl;
        return -1;
    }
    if (!SettingsProbe::registerRadios())
    {
        std::cout << "Could not add test radios to Hamlib" << std::endl;
        return -1;
    }

    if (!CustomRadios::addVariantRadios())
    {
        std::cout << "Could not add variant test radios to Hamlib" << std::endl;
        return -1;
    }

    executeTestCase("rigListIsPopulated", rigListIsPopulated);
    executeTestCase("rigNamesMapBackToThemselves", rigNamesMapBackToThemselves);
    executeTestCase("rigNameLookupIgnoresTrailingWhitespace", rigNameLookupIgnoresTrailingWhitespace);
    executeTestCase("unknownRigsAreReported", unknownRigsAreReported);
    executeTestCase("baudRateRangesAreSane", baudRateRangesAreSane);
    executeTestCase("canBeConstructedFromRigIndex", canBeConstructedFromRigIndex);
    executeTestCase("connectsAndReportsCurrentFrequency", connectsAndReportsCurrentFrequency);
    executeTestCase("disconnects", disconnects);
    executeTestCase("canReconnectAfterDisconnecting", canReconnectAfterDisconnecting);
    executeTestCase("reportsErrorForUnknownRig", reportsErrorForUnknownRig);
    executeTestCase("reportsErrorWhenAlreadyConnected", reportsErrorWhenAlreadyConnected);
    executeTestCase("setsFrequencyAndMode", setsFrequencyAndMode);
    executeTestCase("setsEachSupportedMode", setsEachSupportedMode);
    executeTestCase("appliesModeSetBeforeConnecting", appliesModeSetBeforeConnecting);
    executeTestCase("appliesFrequencySetBeforeConnecting", appliesFrequencySetBeforeConnecting);
    executeTestCase("reportsPttChanges (CAT)", []() { return reportsPttChanges(HamlibRigController::PTT_VIA_CAT); });
    executeTestCase("reportsPttChanges (CAT data)", []() { return reportsPttChanges(HamlibRigController::PTT_VIA_CAT_DATA); });
    executeTestCase("reportsPttChanges (none)", []() { return reportsPttChanges(HamlibRigController::PTT_VIA_NONE); });
    executeTestCase("doesNotQueryRadioWhileTransmitting", doesNotQueryRadioWhileTransmitting);
    executeTestCase("changesFrequencyWhileTransmitting", changesFrequencyWhileTransmitting);
    executeTestCase("leavesRadioAsIsOnDisconnect", []() { return restoreOnDisconnect(false, false); });
    executeTestCase("restoresFrequencyAndModeOnDisconnect", []() { return restoreOnDisconnect(true, false); });
    executeTestCase("restoresOnlyFrequencyOnDisconnect", []() { return restoreOnDisconnect(true, true); });
    executeTestCase("restoresOriginalSettingsEachTimeItConnects", restoresOriginalSettingsEachTimeItConnects);
    executeTestCase("reportsErrorWhenRadioCannotBeOpened", reportsErrorWhenRadioCannotBeOpened);
    executeTestCase("reportsUnknownForOtherModes", reportsUnknownForOtherModes);
    executeTestCase("ignoresRequestForCurrentFrequency", ignoresRequestForCurrentFrequency);
    executeTestCase("followsFrequencyChangedOnRadio", followsFrequencyChangedOnRadio);
    executeTestCase("tellsWhenAReportedFrequencyPredatesTheOneAskedFor", tellsWhenAReportedFrequencyPredatesTheOneAskedFor);
    executeTestCase("stopsWaitingForAFrequencyTheRadioNeverReports", stopsWaitingForAFrequencyTheRadioNeverReports);
    executeTestCase("usesWhicheverVfoIsSelected", usesWhicheverVfoIsSelected);
    executeTestCase("leavesMemoryChannelToChangeFrequency", leavesMemoryChannelToChangeFrequency);
    executeTestCase("ignoresRequestsWhenNotConnected", ignoresRequestsWhenNotConnected);
    executeTestCase("changesModeWhileTransmitting", changesModeWhileTransmitting);
    executeTestCase("reportsErrorWhenPttFails", reportsErrorWhenPttFails);
    executeTestCase("reportsErrorWhenFrequencyCannotBeSet", reportsErrorWhenFrequencyCannotBeSet);
    executeTestCase("reportsErrorWhenModeCannotBeSet", reportsErrorWhenModeCannotBeSet);
    executeTestCase("retriesFrequencyAndModeWithoutNamingVfo", retriesFrequencyAndModeWithoutNamingVfo);
    executeTestCase("reportsErrorOnceRadioStopsAnswering (mode)", []() { 
        return reportsErrorOnceRadioStopsAnswering(RadioFaults::getMode, "Could not retrieve current radio mode"); 
    });
    executeTestCase("reportsErrorOnceRadioStopsAnswering (frequency)", []() { 
        return reportsErrorOnceRadioStopsAnswering(RadioFaults::getFreq, "Could not get current frequency"); 
    });
    executeTestCase("usesCurrentVfoWhenRadioWontSayWhichIsSelected", usesCurrentVfoWhenRadioWontSayWhichIsSelected);
    executeTestCase("doesNotChangeFrequencyOrModeIfRadioWontStopTransmitting", doesNotChangeFrequencyOrModeIfRadioWontStopTransmitting);
    executeTestCase("reportsErrorIfModeChangeBlockedByTransmit", reportsErrorIfModeChangeBlockedByTransmit);
    executeTestCase("reportsErrorIfTransmitCannotResume (frequency)", []() { return reportsErrorIfTransmitCannotResume(false); });
    executeTestCase("reportsErrorIfTransmitCannotResume (mode)", []() { return reportsErrorIfTransmitCannotResume(true); });
    executeTestCase("modeChangeSurvivesFailedFrequencyResend", modeChangeSurvivesFailedFrequencyResend);
    executeTestCase("passesSerialPortAndSpeedToHamlib", passesSerialPortAndSpeedToHamlib);
    executeTestCase("passesPttTypeToHamlib", passesPttTypeToHamlib);
    executeTestCase("sharesSerialPortWhenKeyingWithControlLines", sharesSerialPortWhenKeyingWithControlLines);
    executeTestCase("usesSeparatePttPortOnlyForControlLines", usesSeparatePttPortOnlyForControlLines);
    executeTestCase("forcesControlLinesOnWhenAsked", forcesControlLinesOnWhenAsked);
    executeTestCase("passesCivAddressToIcomRadios", passesCivAddressToIcomRadios);
    executeTestCase("limitsTimeToWaitForRadio", limitsTimeToWaitForRadio);
    executeTestCase("destroysCleanlyWhileConnected", destroysCleanlyWhileConnected);
    executeTestCase("logsHamlibDebugMessagesAtEachLevel", logsHamlibDebugMessagesAtEachLevel);
    executeTestCase("sortsIdenticallyNamedRadiosTogether", sortsIdenticallyNamedRadiosTogether);
    executeTestCase("reportsPortTypesForUnusualRadios", reportsPortTypesForUnusualRadios);
    executeTestCase("leavesDefaultPathnameEmptyForUnknownRadios", leavesDefaultPathnameEmptyForUnknownRadios);
    executeTestCase("connectsEvenIfRadioRefusesToUnkeyFirst", connectsEvenIfRadioRefusesToUnkeyFirst);
    executeTestCase("reportsPttFaultDuringChangesOnlyOnce", reportsPttFaultDuringChangesOnlyOnce);
    executeTestCase("changesFrequencyAndModeWhileKeyingDataChannel", changesFrequencyAndModeWhileKeyingDataChannel);
    executeTestCase("setsFrequencyDespiteUnavailableVfoInformation", setsFrequencyDespiteUnavailableVfoInformation);
    executeTestCase("stillChangesFrequencyWhenRadioWontLeaveMemory", stillChangesFrequencyWhenRadioWontLeaveMemory);
    executeTestCase("leavesFrequencyAloneWhenNoneIsAskedFor", leavesFrequencyAloneWhenNoneIsAskedFor);
    executeTestCase("reportsFrequencyAndModeFaultsOnceEach", reportsFrequencyAndModeFaultsOnceEach);
    return testResult();
}
