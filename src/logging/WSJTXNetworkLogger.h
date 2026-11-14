//==========================================================================
// Name:            WSJTXNetworkLogger.h
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

#ifndef WSJTX_NETWORK_LOGGER_H
#define WSJTX_NETWORK_LOGGER_H

#include <cassert>
#include <chrono>

#include "ILogger.h"
#include "UdpHandler.h"
#include "ThreadedTimer.h"

class WSJTXNetworkLogger : protected UdpHandler, public ILogger
{
public:
    // How often loggers are told that we're still here.
    static constexpr int HEARTBEAT_INTERVAL_MS = 15000;

    class PacketBuilder
    {
    public:
        PacketBuilder();
        virtual ~PacketBuilder();
        
        template<typename T> 
        friend PacketBuilder& operator<<(PacketBuilder& builder, const T& obj);

        char* getPacket() const;
        int getPacketSize() const;
    private:
        char* packet_;
        int packetSize_;
        
        template<typename T> 
        PacketBuilder& serialize_(const T& obj); 

        char* reallocPacket_(int addSize);
    };
    
    WSJTXNetworkLogger(std::string hostname, int port, int heartbeatIntervalMs = HEARTBEAT_INTERVAL_MS);
    virtual ~WSJTXNetworkLogger();
    
    virtual void logContact(std::chrono::time_point<std::chrono::system_clock> logTime, std::string dxCall, std::string dxGrid, std::string myCall, std::string myGrid, uint64_t freqHz, std::string reportRx, std::string reportTx, std::string name, std::string comments, int snr) override;

protected:
    virtual void onReceive_(const char*, int, char*, int) override { /* not used */ }
    
private:
    static const std::string UNIQUE_ID;
    static const std::string LOG_MODE;
    static const std::string LOG_SUBMODE;
    static constexpr uint32_t MAX_SCHEMA_VER = 2;

    // Message types, as numbered in WSJT-X's NetworkMessage.hpp.
    static constexpr uint32_t MSG_HEARTBEAT = 0;
    static constexpr uint32_t MSG_STATUS = 1;
    static constexpr uint32_t MSG_DECODE = 2;
    static constexpr uint32_t MSG_QSO_LOGGED = 5;
    static constexpr uint32_t MSG_LOGGED_ADIF = 12;
    
    // Source: https://stackoverflow.com/questions/33964461/handling-julian-days-in-c11-14
    struct jdate_clock
    {
        using rep        = double;
        using period     = std::ratio<86400>;
        using duration   = std::chrono::duration<rep, period>;
        using time_point = std::chrono::time_point<jdate_clock>;

        static constexpr bool is_steady = false;

        static constexpr auto julianConversionFactor_()
        {
            // Note: the constant is supposed to be 58574100h but this is 12 hours behind
            // what the various WSJT-X compatible logging tools are expecting.
            using namespace std::chrono_literals;
            return std::chrono::duration<double>(58574112h);
        }
        
        static time_point now() noexcept
        {
            using namespace std::chrono;
            return time_point{duration{system_clock::now().time_since_epoch()} + julianConversionFactor_()};
        }
    };
    
    template <class Duration>
    static constexpr auto
    sys_to_jdate(std::chrono::time_point<std::chrono::system_clock, Duration> tp) noexcept
    {
        using namespace std::chrono;
        //static_assert(jdate_clock::duration{jdate_clock::julianConversionFactor_()} < Duration::max(),
        //              "Overflow in sys_to_jdate");
        const auto d = std::chrono::duration_cast<jdate_clock::duration>(tp.time_since_epoch() + jdate_clock::julianConversionFactor_());
        return time_point<jdate_clock, std::remove_cv_t<decltype(d)>>{d};
    }
    
    template <class Duration>
    static constexpr auto
    jdate_to_sys(std::chrono::time_point<jdate_clock, Duration> tp) noexcept
    {
        using namespace std::chrono;
        static_assert(jdate_clock::duration{-jdate_clock::julianConversionFactor_()} > Duration::min(),
                      "Overflow in jdate_to_sys");
        const auto d = tp.time_since_epoch() - jdate_clock::julianConversionFactor_();
        return time_point<system_clock, std::remove_cv_t<decltype(d)>>{d};
    }
    
    ThreadedTimer heartbeatTimer_;
    std::string reportHostname_;
    int reportPort_;
    
    void sendHeartbeat_();

    // Splits a date into what's sent for it: the day and the time of day.
    static void splitJulianDate_(jdate_clock::time_point const& julianDate, int64_t& julianDay, uint32_t& msSinceMidnight);
};

template<typename T> 
WSJTXNetworkLogger::PacketBuilder& operator<<(WSJTXNetworkLogger::PacketBuilder& builder, const T& obj)
{
    return builder.serialize_(obj);
}

#endif // WSJTX_NETWORK_LOGGER_H
