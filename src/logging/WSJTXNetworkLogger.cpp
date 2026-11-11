//==========================================================================
// Name:            WSJTXNetworkLogger.cpp
//
// Purpose:         Implements logging using the WSJT-X network protocol
// Created:         December 12, 2025
// Authors:         Mooneer Salem
//
// License:
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU Lesser General Public License version 2.1,
//  as published by the Free Software Foundation.  This program is
//  distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
//  License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//
//==========================================================================

#include "WSJTXNetworkLogger.h"
#include "AdifRecord.h"
#include "git_version.h"

#include "../util/logging/ulog.h"
#include <inttypes.h>
#include <cstring>
#include <sstream>

// For htonl etc.
#if defined(WIN32) || defined(__MINGW32__)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif // !_WIN32_WINNT

#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2def.h>
#else
#include <arpa/inet.h>
#endif // defined(WIN32) || defined(__MINGW32__)

const std::string WSJTXNetworkLogger::UNIQUE_ID("FreeDV");
const std::string WSJTXNetworkLogger::LOG_MODE("DIGITALVOICE");
const std::string WSJTXNetworkLogger::LOG_SUBMODE("FREEDV");

template<>
WSJTXNetworkLogger::PacketBuilder& WSJTXNetworkLogger::PacketBuilder::serialize_<uint32_t>(const uint32_t& obj)
{
    char* ptr = reallocPacket_(sizeof(uint32_t));
    assert(ptr != nullptr);
    
    auto tmp = htonl(obj);
    memcpy(ptr, &tmp, sizeof(uint32_t));

    return *this;
}

template<>
WSJTXNetworkLogger::PacketBuilder& WSJTXNetworkLogger::PacketBuilder::serialize_<char>(const char& obj)
{
    char* ptr = reallocPacket_(sizeof(char));
    assert(ptr != nullptr);
    
    *((char*)ptr) = obj;
    return *this;
}

template<>
WSJTXNetworkLogger::PacketBuilder& WSJTXNetworkLogger::PacketBuilder::serialize_<signed char>(const signed char& obj)
{
    char* ptr = reallocPacket_(sizeof(signed char));
    assert(ptr != nullptr);
    
    *((signed char*)ptr) = obj;
    return *this;
}

template<>
WSJTXNetworkLogger::PacketBuilder& WSJTXNetworkLogger::PacketBuilder::serialize_<unsigned char>(const unsigned char& obj)
{
    char* ptr = reallocPacket_(sizeof(unsigned char));
    assert(ptr != nullptr);
    
    *((unsigned char*)ptr) = obj;
    return *this;
}

template<>
WSJTXNetworkLogger::PacketBuilder& WSJTXNetworkLogger::PacketBuilder::serialize_<bool>(const bool& obj)
{
    return serialize_((char)(obj ? 1 : 0));
}

template<>
WSJTXNetworkLogger::PacketBuilder& WSJTXNetworkLogger::PacketBuilder::serialize_<uint16_t>(const uint16_t& obj)
{
    char* ptr = reallocPacket_(sizeof(uint16_t));
    assert(ptr != nullptr);
    
    auto tmp = htons(obj);
    memcpy(ptr, &tmp, sizeof(uint16_t));

    return *this;
}

template<>
WSJTXNetworkLogger::PacketBuilder& WSJTXNetworkLogger::PacketBuilder::serialize_<uint64_t>(const uint64_t& obj)
{
    char* ptr = reallocPacket_(sizeof(uint64_t));
    assert(ptr != nullptr);
    
    auto tmp = __builtin_bswap64(obj);
    memcpy(ptr, &tmp, sizeof(uint64_t));
    
    return *this;
}

template<>
WSJTXNetworkLogger::PacketBuilder& WSJTXNetworkLogger::PacketBuilder::serialize_<double>(const double& obj)
{
    // Doubles are sent as 64 bit IEEE values in network byte order,
    // same as the integer types.
    static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64 bits");
    uint64_t tmp;
    memcpy(&tmp, &obj, sizeof(double));

    return serialize_(tmp);
}

template<>
WSJTXNetworkLogger::PacketBuilder& WSJTXNetworkLogger::PacketBuilder::serialize_<int32_t>(const int32_t& obj)
{
    char* ptr = reallocPacket_(sizeof(int32_t));
    assert(ptr != nullptr);
    
    auto tmp = htonl((uint32_t)obj);
    memcpy(ptr, &tmp, sizeof(int32_t));

    return *this;
}

template<>
WSJTXNetworkLogger::PacketBuilder& WSJTXNetworkLogger::PacketBuilder::serialize_<int16_t>(const int16_t& obj)
{
    char* ptr = reallocPacket_(sizeof(int16_t));
    assert(ptr != nullptr);
    
    auto tmp = htons((uint16_t)obj);
    memcpy(ptr, &tmp, sizeof(int16_t));
    
    return *this;
}

template<>
WSJTXNetworkLogger::PacketBuilder& WSJTXNetworkLogger::PacketBuilder::serialize_<int64_t>(const int64_t& obj)
{
    char* ptr = reallocPacket_(sizeof(int64_t));
    assert(ptr != nullptr);
    
    auto tmp = __builtin_bswap64((uint64_t)obj);
    memcpy(ptr, &tmp, sizeof(int64_t));

    return *this;
}

template<>
WSJTXNetworkLogger::PacketBuilder& WSJTXNetworkLogger::PacketBuilder::serialize_<std::string>(const std::string& obj)
{
    char* ptr = reallocPacket_(obj.size() + sizeof(uint32_t));
    assert(ptr != nullptr);
    
    auto tmp = htonl(obj.size());
    memcpy(ptr, &tmp, sizeof(uint32_t));
    memcpy(ptr + sizeof(uint32_t), obj.c_str(), obj.size());
    
    return *this;
}

template<>
WSJTXNetworkLogger::PacketBuilder& WSJTXNetworkLogger::PacketBuilder::serialize_<WSJTXNetworkLogger::jdate_clock::time_point>(const WSJTXNetworkLogger::jdate_clock::time_point& obj)
{
    int64_t julianDay = 0;
    uint32_t msSinceMidnight = 0;
    splitJulianDate_(obj, julianDay, msSinceMidnight);
        
    *this   << julianDay
            << msSinceMidnight
            << (unsigned char)1     // UTC
                ;
    return *this;
}

void WSJTXNetworkLogger::splitJulianDate_(jdate_clock::time_point const& julianDate, int64_t& julianDay, uint32_t& msSinceMidnight)
{
    // Not using C++20, so we need to manually define this.
    using day_duration = std::chrono::duration<int64_t, std::ratio<86400>>;
    const int64_t MS_PER_DAY = 86400000;

    auto sinceEpoch = julianDate.time_since_epoch();
    auto julianDays = std::chrono::floor<day_duration>(sinceEpoch);
    auto julianFracDay = sinceEpoch - julianDays;

    // Dates are held as a (floating point) number of days, which can't
    // represent most times of day exactly. Round to the nearest millisecond
    // rather than truncating, or a time on the second gets sent as the
    // millisecond before it (and so appears as the previous second).
    julianDay = julianDays.count();
    int64_t ms = std::chrono::round<std::chrono::milliseconds>(julianFracDay).count();
    if (ms >= MS_PER_DAY)
    {
        ms -= MS_PER_DAY;
        julianDay++;
    }
    msSinceMidnight = (uint32_t)ms;
}

WSJTXNetworkLogger::WSJTXNetworkLogger(std::string hostname, int port, int heartbeatIntervalMs)
    : heartbeatTimer_(heartbeatIntervalMs, [&](ThreadedTimer&) {
        sendHeartbeat_();
    }, true)
    , reportHostname_(std::move(hostname))
    , reportPort_(port)
{
    open("", 0, reportHostname_.c_str(), reportPort_);
    heartbeatTimer_.start();
    sendHeartbeat_();
}

WSJTXNetworkLogger::~WSJTXNetworkLogger()
{
    heartbeatTimer_.stop();
    close();
}

void WSJTXNetworkLogger::logContact(std::chrono::time_point<std::chrono::system_clock> logTime, std::string dxCall, std::string dxGrid, std::string myCall, std::string myGrid, uint64_t freqHz, std::string reportRx, std::string reportTx, std::string name, std::string comments, int snr)
{
    auto currentTimeAsJulian = sys_to_jdate(logTime);

    // Send status message so loggers have the correct RX frequency
    PacketBuilder statusBuilder;
    statusBuilder << (uint32_t)1
                  << UNIQUE_ID
                  << freqHz
                  << LOG_MODE
                  << dxCall
                  << std::string("") // report
                  << LOG_MODE        // TX mode
                  << true            // TX enabled
                  << false           // transmitting - TBD
                  << true            // decoding - TBD
                  << (uint32_t)0     // RX DF
                  << (uint32_t)0     // TX DF
                  << myCall
                  << myGrid
                  << dxGrid
                  << false           // TX watchdog - TBD
                  << LOG_SUBMODE
                  << false           // fast mode - TBD
                  << (uint8_t)0      // Special Operation Mode - TBD
                  << (uint32_t)0     // Frequency tolderance - TBD
                  << (uint32_t)0     // T/R period - TBD
                  << std::string("") // config name - TBD
                  << std::string("") // TX message - TBD
                  ;
    send(reportHostname_.c_str(), reportPort_, statusBuilder.getPacket(), statusBuilder.getPacketSize()).wait();
    
    int64_t julianDay = 0;
    uint32_t msSinceMidnight = 0;
    splitJulianDate_(currentTimeAsJulian, julianDay, msSinceMidnight);
    
    // Send decode message for SNR
    if (snr > UNKNOWN_SNR)
    {
        PacketBuilder decodeBuilder;
        decodeBuilder << (uint32_t)2
                      << UNIQUE_ID
                      << false
                      << msSinceMidnight // time
                      << (int32_t)snr
                      << (double)0.0 // delta time
                      << (uint32_t)0 // delta freq
                      << LOG_MODE
                      << std::string("") // message
                      << false // low confidence 
                      << false // off air
                      ;
        send(reportHostname_.c_str(), reportPort_, decodeBuilder.getPacket(), decodeBuilder.getPacketSize()).wait();
    }
    
    // Send actual log request
    PacketBuilder builder;
    builder << (uint32_t)5
            << UNIQUE_ID
            << currentTimeAsJulian // time off
            << dxCall
            << dxGrid
            << freqHz
            << LOG_MODE
            << reportTx            // report TX
            << reportRx            // report RX
            << std::string("")     // TX power
            << comments            // comments
            << name                // name
            << currentTimeAsJulian // time on
            << myCall              // operator call
            << myCall
            << myGrid
            << std::string("")     // exchange sent
            << std::string("")     // exchange RX
            << std::string("")     // ADIF propagation mode
                ;

    send(reportHostname_.c_str(), reportPort_, builder.getPacket(), builder.getPacketSize()).wait();

    // And the same contact as an ADIF record, as WSJT-X also sends when a
    // QSO is logged. Some logging programs (e.g. Log4OM) save the contact
    // from this message rather than from QSO Logged above, which on its own
    // leaves the user to finish logging it by hand. It also carries what
    // QSO Logged has no field for, such as the FREEDV submode.
    AdifContact adifContact;
    adifContact.logTime = logTime;
    adifContact.dxCall = dxCall;
    adifContact.dxGrid = dxGrid;
    adifContact.myCall = myCall;
    adifContact.myGrid = myGrid;
    adifContact.freqHz = freqHz;
    adifContact.reportRx = reportRx;
    adifContact.reportTx = reportTx;
    adifContact.name = name;
    adifContact.comments = comments;

    PacketBuilder adifBuilder;
    adifBuilder << (uint32_t)12
                << UNIQUE_ID
                << BuildLoggedAdif(adifContact, GetFreeDVVersion())
                ;
    send(reportHostname_.c_str(), reportPort_, adifBuilder.getPacket(), adifBuilder.getPacketSize()).wait();
}

void WSJTXNetworkLogger::sendHeartbeat_()
{
    PacketBuilder builder;
    builder << (uint32_t)0;
    builder << UNIQUE_ID;
    builder << MAX_SCHEMA_VER;
    builder << GetFreeDVVersion();
    builder << std::string("");
    
    send(reportHostname_.c_str(), reportPort_, builder.getPacket(), builder.getPacketSize()).wait();
}

WSJTXNetworkLogger::PacketBuilder::PacketBuilder()
    : packet_(nullptr)
    , packetSize_(0)
{
    // Initialize with header
    *this << (uint32_t)0xadbccbda;     // magic number
    *this << MAX_SCHEMA_VER;           // schema version
}

WSJTXNetworkLogger::PacketBuilder::~PacketBuilder()
{
    delete[] packet_;
}

char* WSJTXNetworkLogger::PacketBuilder::getPacket() const
{
    return packet_;
}

int WSJTXNetworkLogger::PacketBuilder::getPacketSize() const
{
    return packetSize_;
}

char* WSJTXNetworkLogger::PacketBuilder::reallocPacket_(int addSize)
{
    char* newPacket = new char[packetSize_ + addSize];
    assert(newPacket != nullptr);
        
    char *returnVal = newPacket + packetSize_;
    if (packet_ != nullptr)
    {
        // Copy existing data to new packet.
        memcpy(newPacket, packet_, packetSize_);
        
        delete[] packet_;
    }
    
    packetSize_ += addSize;
    packet_ = newPacket;
    return returnVal;
}
