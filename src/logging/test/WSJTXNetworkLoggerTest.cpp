#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#if defined(WIN32) || defined(__MINGW32__)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif // defined(WIN32) || defined(__MINGW32__)

#include "../WSJTXNetworkLogger.h"
#include "git_version.h"
#include "util/logging/ulog.h"
#include "../../test/UnitTestCommon.h"

// Sends real packets over the loopback interface and decodes them the way a
// WSJT-X compatible logging program would. See NetworkMessage.hpp in the
// WSJT-X source code for the message formats being checked here.

using namespace std::chrono_literals;

namespace {

using Bytes = std::vector<unsigned char>;

constexpr uint32_t MAGIC_NUMBER = 0xadbccbda;
constexpr uint32_t SCHEMA_VERSION = 2;

constexpr uint32_t MSG_HEARTBEAT = 0;
constexpr uint32_t MSG_STATUS = 1;
constexpr uint32_t MSG_DECODE = 2;
constexpr uint32_t MSG_QSO_LOGGED = 5;

constexpr int RECEIVE_TIMEOUT_MS = 2000;
constexpr int NOTHING_MORE_TIMEOUT_MS = 250;

// Qt's Julian day number for January 1, 1970 (i.e. QDate(1970, 1, 1).toJulianDay()).
constexpr int64_t UNIX_EPOCH_JULIAN_DAY = 2440588;
constexpr int64_t MS_PER_DAY = 86400000;
constexpr unsigned char QT_TIMESPEC_UTC = 1;

// Stands in for the logging program: listens on a port chosen by the OS.
class Receiver
{
public:
    Receiver()
    {
        socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(socket_, (struct sockaddr*)&addr, sizeof(addr)) == 0)
        {
            socklen_t addrLen = sizeof(addr);
            if (getsockname(socket_, (struct sockaddr*)&addr, &addrLen) == 0)
            {
                port_ = ntohs(addr.sin_port);
            }
        }
    }

    ~Receiver()
    {
#if defined(WIN32) || defined(__MINGW32__)
        closesocket(socket_);
#else
        close(socket_);
#endif // defined(WIN32) || defined(__MINGW32__)
    }

    int port() const { return port_; }

    std::optional<Bytes> receive(int timeoutMs = RECEIVE_TIMEOUT_MS)
    {
#if defined(WIN32) || defined(__MINGW32__)
        DWORD timeout = timeoutMs;
#else
        struct timeval timeout;
        timeout.tv_sec = timeoutMs / 1000;
        timeout.tv_usec = (timeoutMs % 1000) * 1000;
#endif // defined(WIN32) || defined(__MINGW32__)
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        unsigned char buf[2048];
        auto numBytes = recv(socket_, (char*)buf, sizeof(buf), 0);
        if (numBytes < 0) return std::nullopt;
        return Bytes(buf, buf + numBytes);
    }

private:
#if defined(WIN32) || defined(__MINGW32__)
    SOCKET socket_;
#else
    int socket_;
#endif // defined(WIN32) || defined(__MINGW32__)
    int port_ = 0;
};

// Independent decoder for Qt's QDataStream format: everything is big-endian
// and strings are a 32 bit length followed by that many bytes.
class Reader
{
public:
    explicit Reader(Bytes bytes) : bytes_(std::move(bytes)) { }

    uint64_t unsignedInt(int numBytes)
    {
        uint64_t result = 0;
        for (int i = 0; i < numBytes; i++)
        {
            result = (result << 8) | next_();
        }
        return result;
    }

    unsigned char u8() { return (unsigned char)unsignedInt(1); }
    bool boolean() { return u8() != 0; }
    uint32_t u32() { return (uint32_t)unsignedInt(4); }
    int32_t i32() { return (int32_t)u32(); }
    uint64_t u64() { return unsignedInt(8); }
    int64_t i64() { return (int64_t)u64(); }

    double f64()
    {
        uint64_t bits = u64();
        double result;
        memcpy(&result, &bits, sizeof(result));
        return result;
    }

    std::string str()
    {
        uint32_t length = u32();
        if (length > bytes_.size() - std::min(pos_, bytes_.size()))
        {
            ok_ = false;
            return "";
        }

        std::string result(bytes_.begin() + pos_, bytes_.begin() + pos_ + length);
        pos_ += length;
        return result;
    }

    // True if nothing ran off the end of the packet and nothing's left over.
    bool finished() const { return ok_ && pos_ == bytes_.size(); }

private:
    Bytes bytes_;
    size_t pos_ = 0;
    bool ok_ = true;

    unsigned char next_()
    {
        if (pos_ >= bytes_.size())
        {
            ok_ = false;
            return 0;
        }
        return bytes_[pos_++];
    }
};

template<typename T>
bool check(const char* field, T const& actual, T const& expected)
{
    if (actual == expected) return true;
    std::cout << "[" << field << " was " << actual << ", expected " << expected << "] ";
    return false;
}

bool check(const char* field, std::string const& actual, const char* expected)
{
    return check(field, actual, std::string(expected));
}

bool checkTrue(const char* description, bool value)
{
    if (!value) std::cout << "[" << description << "] ";
    return value;
}

// Receives the next packet and checks the fields common to every message.
std::optional<Reader> receiveMessage(Receiver& receiver, uint32_t expectedType)
{
    auto packet = receiver.receive();
    if (!packet)
    {
        std::cout << "[no packet received for message type " << expectedType << "] ";
        return std::nullopt;
    }

    Reader reader(*packet);
    bool result = true;
    result &= check("magic number", reader.u32(), MAGIC_NUMBER);
    result &= check("schema version", reader.u32(), SCHEMA_VERSION);
    result &= check("message type", reader.u32(), expectedType);
    result &= check("id", reader.str(), "FreeDV");
    if (!result) return std::nullopt;
    return reader;
}

bool nothingMoreReceived(Receiver& receiver)
{
    return checkTrue("unexpected extra packet", !receiver.receive(NOTHING_MORE_TIMEOUT_MS));
}

// A QDateTime as it appears on the wire.
struct WireDateTime
{
    int64_t julianDay;
    uint32_t msSinceMidnight;
    unsigned char timeSpec;

    WireDateTime() : julianDay(0), msSinceMidnight(0), timeSpec(0) { }

    explicit WireDateTime(Reader& reader)
        : julianDay(reader.i64())
        , msSinceMidnight(reader.u32())
        , timeSpec(reader.u8())
    {
    }

    int64_t msSinceUnixEpoch() const
    {
        return (julianDay - UNIX_EPOCH_JULIAN_DAY) * MS_PER_DAY + msSinceMidnight;
    }
};

std::chrono::system_clock::time_point fromUnixMs(int64_t ms)
{
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::milliseconds(ms)));
}

struct Contact
{
    std::chrono::system_clock::time_point logTime = fromUnixMs(946728000000LL); // 2000-01-01 12:00:00 UTC
    std::string dxCall = "K6AQ";
    std::string dxGrid = "DM12";
    std::string myCall = "K6ABC";
    std::string myGrid = "CM97";
    uint64_t freqHz = 14236000;
    std::string reportRx = "59";
    std::string reportTx = "57";
    std::string name = "Mooneer";
    std::string comments = "Nice QSO";
    int snr = 12;

    void logTo(WSJTXNetworkLogger& logger) const
    {
        logger.logContact(logTime, dxCall, dxGrid, myCall, myGrid, freqHz, reportRx, reportTx, name, comments, snr);
    }
};

struct Fixture
{
    Receiver receiver;
    NeverReused<WSJTXNetworkLogger> loggerStorage { "127.0.0.1", receiver.port() };
    WSJTXNetworkLogger& logger = *loggerStorage;

    // The logger announces itself as soon as it's created.
    bool skipHeartbeat()
    {
        return checkTrue("no heartbeat received", receiver.receive().has_value());
    }
};

bool checkStatus(Receiver& receiver, Contact const& contact)
{
    auto reader = receiveMessage(receiver, MSG_STATUS);
    if (!reader) return false;

    bool result = true;
    result &= check("dial frequency", reader->u64(), contact.freqHz);
    result &= check("mode", reader->str(), "DIGITALVOICE");
    result &= check("DX call", reader->str(), contact.dxCall);
    result &= check("report", reader->str(), "");
    result &= check("TX mode", reader->str(), "DIGITALVOICE");
    result &= check("TX enabled", reader->boolean(), true);
    result &= check("transmitting", reader->boolean(), false);
    result &= check("decoding", reader->boolean(), true);
    result &= check("RX DF", reader->u32(), (uint32_t)0);
    result &= check("TX DF", reader->u32(), (uint32_t)0);
    result &= check("DE call", reader->str(), contact.myCall);
    result &= check("DE grid", reader->str(), contact.myGrid);
    result &= check("DX grid", reader->str(), contact.dxGrid);
    result &= check("TX watchdog", reader->boolean(), false);
    result &= check("submode", reader->str(), "FREEDV");
    result &= check("fast mode", reader->boolean(), false);
    result &= check("special operation mode", (int)reader->u8(), 0);
    result &= check("frequency tolerance", reader->u32(), (uint32_t)0);
    result &= check("T/R period", reader->u32(), (uint32_t)0);
    result &= check("configuration name", reader->str(), "");
    result &= check("TX message", reader->str(), "");
    result &= checkTrue("status message is the wrong length", reader->finished());
    return result;
}

bool checkDecode(Receiver& receiver, Contact const& contact, uint32_t* msSinceMidnight = nullptr)
{
    auto reader = receiveMessage(receiver, MSG_DECODE);
    if (!reader) return false;

    bool result = true;
    result &= check("new", reader->boolean(), false);
    uint32_t time = reader->u32();
    result &= checkTrue("decode time is not a time of day", time < MS_PER_DAY);
    result &= check("SNR", reader->i32(), (int32_t)contact.snr);
    result &= check("delta time", reader->f64(), 0.0);
    result &= check("delta frequency", reader->u32(), (uint32_t)0);
    result &= check("mode", reader->str(), "DIGITALVOICE");
    result &= check("message", reader->str(), "");
    result &= check("low confidence", reader->boolean(), false);
    result &= check("off air", reader->boolean(), false);
    result &= checkTrue("decode message is the wrong length", reader->finished());

    if (msSinceMidnight != nullptr) *msSinceMidnight = time;
    return result;
}

bool checkQsoLogged(Receiver& receiver, Contact const& contact, WireDateTime* timeOff = nullptr)
{
    auto reader = receiveMessage(receiver, MSG_QSO_LOGGED);
    if (!reader) return false;

    bool result = true;
    WireDateTime off(*reader);
    result &= check("DX call", reader->str(), contact.dxCall);
    result &= check("DX grid", reader->str(), contact.dxGrid);
    result &= check("TX frequency", reader->u64(), contact.freqHz);
    result &= check("mode", reader->str(), "DIGITALVOICE");
    result &= check("report sent", reader->str(), contact.reportTx);
    result &= check("report received", reader->str(), contact.reportRx);
    result &= check("TX power", reader->str(), "");
    result &= check("comments", reader->str(), contact.comments);
    result &= check("name", reader->str(), contact.name);
    WireDateTime on(*reader);
    result &= check("operator call", reader->str(), contact.myCall);
    result &= check("my call", reader->str(), contact.myCall);
    result &= check("my grid", reader->str(), contact.myGrid);
    result &= check("exchange sent", reader->str(), "");
    result &= check("exchange received", reader->str(), "");
    result &= check("ADIF propagation mode", reader->str(), "");
    result &= checkTrue("QSO logged message is the wrong length", reader->finished());

    result &= check("time off timespec", (int)off.timeSpec, (int)QT_TIMESPEC_UTC);
    result &= check("time on timespec", (int)on.timeSpec, (int)QT_TIMESPEC_UTC);
    result &= checkTrue("time off is not a time of day", off.msSinceMidnight < MS_PER_DAY);
    result &= check("time on", on.msSinceUnixEpoch(), off.msSinceUnixEpoch());

    if (timeOff != nullptr) *timeOff = off;
    return result;
}

bool sendsHeartbeatWhenCreated()
{
    Fixture f;
    auto reader = receiveMessage(f.receiver, MSG_HEARTBEAT);
    if (!reader) return false;

    bool result = true;
    result &= check("maximum schema number", reader->u32(), SCHEMA_VERSION);
    result &= check("version", reader->str(), GetFreeDVVersion());
    result &= check("revision", reader->str(), "");
    result &= checkTrue("heartbeat is the wrong length", reader->finished());
    result &= nothingMoreReceived(f.receiver);
    return result;
}

bool logContactSendsStatusDecodeAndQsoLogged()
{
    Fixture f;
    if (!f.skipHeartbeat()) return false;

    Contact contact;
    contact.logTo(f.logger);

    // Loggers rely on the order: the QSO is logged against the frequency
    // and SNR most recently reported.
    return
        checkStatus(f.receiver, contact) &&
        checkDecode(f.receiver, contact) &&
        checkQsoLogged(f.receiver, contact) &&
        nothingMoreReceived(f.receiver);
}

bool omitsDecodeWhenSnrUnknown()
{
    Fixture f;
    if (!f.skipHeartbeat()) return false;

    Contact contact;
    contact.snr = ILogger::UNKNOWN_SNR;
    contact.logTo(f.logger);

    return
        checkStatus(f.receiver, contact) &&
        checkQsoLogged(f.receiver, contact) &&
        nothingMoreReceived(f.receiver);
}

bool sendsNegativeSnr()
{
    Fixture f;
    if (!f.skipHeartbeat()) return false;

    Contact contact;
    contact.snr = -5;
    contact.logTo(f.logger);

    return
        checkStatus(f.receiver, contact) &&
        checkDecode(f.receiver, contact) &&
        checkQsoLogged(f.receiver, contact);
}

bool sendsOptionalFieldsWhenEmpty()
{
    Fixture f;
    if (!f.skipHeartbeat()) return false;

    Contact contact;
    contact.dxGrid = "";
    contact.reportRx = "";
    contact.reportTx = "";
    contact.name = "";
    contact.comments = "";
    contact.logTo(f.logger);

    return
        checkStatus(f.receiver, contact) &&
        checkDecode(f.receiver, contact) &&
        checkQsoLogged(f.receiver, contact);
}

bool sendsFrequenciesAbove4GHz()
{
    Fixture f;
    if (!f.skipHeartbeat()) return false;

    // Doesn't fit in 32 bits.
    Contact contact;
    contact.freqHz = 10368100000ULL;
    contact.logTo(f.logger);

    return
        checkStatus(f.receiver, contact) &&
        checkDecode(f.receiver, contact) &&
        checkQsoLogged(f.receiver, contact);
}

bool sendsLongAndNonAsciiText()
{
    Fixture f;
    if (!f.skipHeartbeat()) return false;

    Contact contact;
    contact.name = "Jos\xc3\xa9 \xe5\xa4\xaa\xe9\x83\x8e"; // UTF-8
    contact.comments = std::string(500, 'x');
    contact.logTo(f.logger);

    return
        checkStatus(f.receiver, contact) &&
        checkDecode(f.receiver, contact) &&
        checkQsoLogged(f.receiver, contact);
}

// Logs a contact at the given time and checks the date and time that a
// logging program would see.
bool logTimeIs(int64_t unixMs, int64_t expectedJulianDay, uint32_t expectedMsSinceMidnight)
{
    Fixture f;
    if (!f.skipHeartbeat()) return false;

    Contact contact;
    contact.logTime = fromUnixMs(unixMs);
    contact.logTo(f.logger);

    uint32_t decodeTime = 0;
    WireDateTime timeOff;
    if (!checkStatus(f.receiver, contact) ||
        !checkDecode(f.receiver, contact, &decodeTime) ||
        !checkQsoLogged(f.receiver, contact, &timeOff))
    {
        return false;
    }

    // The time is converted using floating point but has to come out to the
    // millisecond: one short is the previous second as far as a logging
    // program is concerned. (Absolute times are compared so that an error
    // which crosses midnight is seen as a millisecond and not a day.)
    int64_t expectedMs = (expectedJulianDay - UNIX_EPOCH_JULIAN_DAY) * MS_PER_DAY + expectedMsSinceMidnight;
    int64_t errorMs = timeOff.msSinceUnixEpoch() - expectedMs;
    if (errorMs != 0)
    {
        std::cout << "[logged Julian day " << timeOff.julianDay << " + " << timeOff.msSinceMidnight
                  << " ms, expected " << expectedJulianDay << " + " << expectedMsSinceMidnight << " ms] ";
        return false;
    }

    return check("decode time", decodeTime, timeOff.msSinceMidnight);
}

bool sendsLogTimeAsQtDateTime()
{
    bool result = true;

    // Julian days below are what QDate::toJulianDay() returns for each date.
    result &= logTimeIs(0, 2440588, 0);                              // 1970-01-01 00:00:00.000
    result &= logTimeIs(946728000000LL, 2451545, 43200000);          // 2000-01-01 12:00:00.000
    result &= logTimeIs(1765497600000LL, 2461022, 0);                // 2025-12-12 00:00:00.000
    result &= logTimeIs(1765583999500LL, 2461022, 86399500);         // 2025-12-12 23:59:59.500
    result &= logTimeIs(1765542896789LL, 2461022, 45296789);         // 2025-12-12 12:34:56.789
    result &= logTimeIs(1709164800000LL + 3723004, 2460370, 3723004); // 2024-02-29 01:02:03.004
    return result;
}

bool logTimeRoundsUpIntoTheNextJulianDay()
{
    Fixture f;
    if (!f.skipHeartbeat()) return false;

    // One microsecond short of midnight (2025-12-12 23:59:59.999999):
    // rounded to the nearest millisecond it belongs to the next day, and
    // has to be sent that way rather than cut off at the old one.
    Contact contact;
    contact.logTime = std::chrono::time_point<std::chrono::system_clock>(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::milliseconds(1765583999999LL) + std::chrono::microseconds(999)));
    contact.logTo(f.logger);

    uint32_t decodeTime = 0;
    WireDateTime timeOff;
    if (!checkStatus(f.receiver, contact) ||
        !checkDecode(f.receiver, contact, &decodeTime) ||
        !checkQsoLogged(f.receiver, contact, &timeOff))
    {
        return false;
    }

    bool result = true;
    result &= check("Julian day rounded up", timeOff.julianDay, (int64_t)2461023);
    result &= check("milliseconds since midnight", timeOff.msSinceMidnight, (uint32_t)0);
    result &= check("decode time", decodeTime, timeOff.msSinceMidnight);
    return result;
}

bool eachContactIsSentSeparately()
{
    Fixture f;
    if (!f.skipHeartbeat()) return false;

    Contact first;
    Contact second;
    second.dxCall = "VK5DGR";
    second.dxGrid = "PF95";
    second.freqHz = 7177000;
    second.snr = ILogger::UNKNOWN_SNR;

    first.logTo(f.logger);
    second.logTo(f.logger);

    return
        checkStatus(f.receiver, first) &&
        checkDecode(f.receiver, first) &&
        checkQsoLogged(f.receiver, first) &&
        checkStatus(f.receiver, second) &&
        checkQsoLogged(f.receiver, second) &&
        nothingMoreReceived(f.receiver);
}

bool checkHeartbeat(Receiver& receiver)
{
    auto reader = receiveMessage(receiver, MSG_HEARTBEAT);
    if (!reader) return false;

    bool result = true;
    result &= check("maximum schema number", reader->u32(), SCHEMA_VERSION);
    result &= check("version", reader->str(), GetFreeDVVersion());
    result &= check("revision", reader->str(), "");
    result &= checkTrue("heartbeat is the wrong length", reader->finished());
    return result;
}

bool heartbeatDefaultsToEveryFifteenSeconds()
{
    // What WSJT-X itself does; loggers use it to notice that we've gone away.
    return check("interval", WSJTXNetworkLogger::HEARTBEAT_INTERVAL_MS, 15000);
}

bool sendsHeartbeatsPeriodically()
{
    const int INTERVAL_MS = 100;
    const int NUM_HEARTBEATS = 5;

    Receiver receiver;
    auto start = std::chrono::steady_clock::now();
    NeverReused<WSJTXNetworkLogger> logger(std::string("127.0.0.1"), receiver.port(), INTERVAL_MS);

    // One straight away, then one per interval.
    for (int count = 0; count < NUM_HEARTBEATS; count++)
    {
        if (!checkHeartbeat(receiver)) return false;
    }

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    int expectedMs = INTERVAL_MS * (NUM_HEARTBEATS - 1);
    if (elapsedMs < expectedMs - INTERVAL_MS / 2 || elapsedMs > expectedMs + 1000)
    {
        std::cout << "[" << NUM_HEARTBEATS << " heartbeats took " << elapsedMs << " ms, expected about "
                  << expectedMs << " ms] ";
        return false;
    }
    return true;
}

bool heartbeatsContinueAroundLoggedContacts()
{
    const int INTERVAL_MS = 100;

    Receiver receiver;
    NeverReused<WSJTXNetworkLogger> loggerStorage(std::string("127.0.0.1"), receiver.port(), INTERVAL_MS);
    WSJTXNetworkLogger& logger = *loggerStorage;
    if (!checkHeartbeat(receiver)) return false;

    Contact contact;
    contact.logTo(logger);

    // Heartbeats may land anywhere among the contact's messages, but each
    // message arrives whole and in order.
    uint32_t expected[] = { MSG_STATUS, MSG_DECODE, MSG_QSO_LOGGED };
    size_t numContactMessages = 0;
    int numHeartbeats = 0;
    while (numContactMessages < 3 || numHeartbeats < 2)
    {
        auto packet = receiver.receive();
        if (!packet)
        {
            std::cout << "[only received " << numContactMessages << " contact message(s) and "
                      << numHeartbeats << " heartbeat(s)] ";
            return false;
        }

        Reader reader(*packet);
        reader.u32();
        reader.u32();
        uint32_t type = reader.u32();
        if (type == MSG_HEARTBEAT)
        {
            numHeartbeats++;
        }
        else if (numContactMessages < 3 && type == expected[numContactMessages])
        {
            numContactMessages++;
        }
        else
        {
            std::cout << "[unexpected message type " << type << "] ";
            return false;
        }
    }
    return true;
}

bool stopsSendingHeartbeatsWhenDestroyed()
{
    Receiver receiver;
    {
        NeverReused<WSJTXNetworkLogger> logger(std::string("127.0.0.1"), receiver.port(), 50);
        if (!checkHeartbeat(receiver) || !checkHeartbeat(receiver)) return false;
    }

    // Anything already on its way is fine; after that there should be silence.
    while (receiver.receive(100)) { }
    return nothingMoreReceived(receiver);
}

bool destroysCleanly()
{
    // Destruction has to stop the heartbeat timer and the socket's thread,
    // whether or not anything's been logged.
    for (int iteration = 0; iteration < 20; iteration++)
    {
        Fixture f;
        if (iteration % 2 == 0)
        {
            Contact().logTo(f.logger);
        }
    }
    return true;
}

bool nothingListeningIsHarmless()
{
    // Find a port that nothing's listening on.
    int port;
    {
        Receiver receiver;
        port = receiver.port();
    }

    NeverReused<WSJTXNetworkLogger> loggerStorage(std::string("127.0.0.1"), port);
    WSJTXNetworkLogger& logger = *loggerStorage;
    Contact().logTo(logger);
    Contact().logTo(logger);
    return true;
}

}

int main()
{
#if defined(WIN32) || defined(__MINGW32__)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif // defined(WIN32) || defined(__MINGW32__)

    ulog_set_quiet(true);

    executeTestCase("sendsHeartbeatWhenCreated", sendsHeartbeatWhenCreated);
    executeTestCase("logContactSendsStatusDecodeAndQsoLogged", logContactSendsStatusDecodeAndQsoLogged);
    executeTestCase("omitsDecodeWhenSnrUnknown", omitsDecodeWhenSnrUnknown);
    executeTestCase("sendsNegativeSnr", sendsNegativeSnr);
    executeTestCase("sendsOptionalFieldsWhenEmpty", sendsOptionalFieldsWhenEmpty);
    executeTestCase("sendsFrequenciesAbove4GHz", sendsFrequenciesAbove4GHz);
    executeTestCase("sendsLongAndNonAsciiText", sendsLongAndNonAsciiText);
    executeTestCase("sendsLogTimeAsQtDateTime", sendsLogTimeAsQtDateTime);
    executeTestCase("logTimeRoundsUpIntoTheNextJulianDay", logTimeRoundsUpIntoTheNextJulianDay);
    executeTestCase("eachContactIsSentSeparately", eachContactIsSentSeparately);
    executeTestCase("heartbeatDefaultsToEveryFifteenSeconds", heartbeatDefaultsToEveryFifteenSeconds);
    executeTestCase("sendsHeartbeatsPeriodically", sendsHeartbeatsPeriodically);
    executeTestCase("heartbeatsContinueAroundLoggedContacts", heartbeatsContinueAroundLoggedContacts);
    executeTestCase("stopsSendingHeartbeatsWhenDestroyed", stopsSendingHeartbeatsWhenDestroyed);
    executeTestCase("destroysCleanly", destroysCleanly);
    executeTestCase("nothingListeningIsHarmless", nothingListeningIsHarmless);
    return testResult();
}
