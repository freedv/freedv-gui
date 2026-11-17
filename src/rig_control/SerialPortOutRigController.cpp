//=========================================================================
// Name:            SerialPortOutRigController.cpp
// Purpose:         Controls PTT on radios using serial port.
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

#include <future>

#include "SerialPortOutRigController.h"

SerialPortOutRigController::SerialPortOutRigController(std::string serialPort, bool useRTS, bool RTSPos, bool useDTR, bool DTRPos)
    : SerialPortRigController(std::move(serialPort))
    , useRTS_(useRTS)
    , rtsPos_(RTSPos)
    , useDTR_(useDTR)
    , dtrPos_(DTRPos)
    , rigResponseTime_(0)
{
    // empty
}

void SerialPortOutRigController::onConnected_()
{
    // Ensure that PTT is disabled on successful connect.
    ptt(false);
}

SerialPortOutRigController::~SerialPortOutRigController()
{
    // Disable PTT before shutdown.
    std::shared_ptr<std::promise<void>> prom = std::make_shared<std::promise<void> >();
    auto fut = prom->get_future();

    enqueue_([&, prom]() {
        pttImpl_(false);
        prom->set_value();
    });
    fut.wait();
    
    waitForAllTasksComplete_();
}

void SerialPortOutRigController::ptt(bool state)
{
    enqueue_(std::bind(&SerialPortOutRigController::pttImpl_, this, state));
}

int SerialPortOutRigController::getRigResponseTimeMicroseconds()
{
    return rigResponseTime_;
}

void SerialPortOutRigController::pttImpl_(bool state)
{
       /*  Truth table:

          g_tx   RTSPos   RTS
          -------------------
          0      1        0
          1      1        1
          0      0        1
          1      0        0

          exclusive NOR
    */

    if (serialPortHandle_ != COM_HANDLE_INVALID) 
    {
        auto oldTime = std::chrono::steady_clock::now();
        if (useRTS_) {
            if (state == rtsPos_)
                raiseRTS_();
            else
                lowerRTS_();
        }
        if (useDTR_) {
            if (state == dtrPos_)
                raiseDTR_();
            else
                lowerDTR_();
        }
        auto newTime = std::chrono::steady_clock::now();
        auto totalTimeMicroseconds = (int)std::chrono::duration_cast<std::chrono::microseconds>(newTime - oldTime).count();
        rigResponseTime_ = std::max(rigResponseTime_.load(), totalTimeMicroseconds);
 
        onPttChange(this, state);
    }
}
