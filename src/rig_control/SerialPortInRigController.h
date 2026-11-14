//=========================================================================
// Name:            SerialPortInRigController.h
// Purpose:         Monitors PTT input using serial port.
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

#ifndef SERIAL_PORT_IN_RIG_CONTROLLER_H
#define SERIAL_PORT_IN_RIG_CONTROLLER_H

#include <atomic>
#include <thread>
#include "IRigPttController.h"
#include "SerialPortRigController.h"

class SerialPortInRigController : virtual public IRigPttController, virtual public SerialPortRigController
{
public:
    SerialPortInRigController(std::string serialPort, bool ctsPos);
    virtual ~SerialPortInRigController();

    virtual void ptt(bool) override { /* does not support output */ }
    virtual int getRigResponseTimeMicroseconds() override { return 0; /* no support for output */ }

    void suspendChanges(bool suspend);
    
private:
    std::thread pollThread_;

    std::atomic<bool> threadExiting_;
    bool ctsPos_;
    bool currentPttInputState_;
    bool firstPoll_;
    bool suspendChanges_;

    // Override to skip termios configuration.
    // This is to work around a bug in the tty0tty kernel driver that causes
    // deadlocks if tcgetattr or tcsetattr are called.
    virtual bool shouldConfigureTermios_() const override { return false; }

    virtual void onConnected_() override;
    virtual void onDisconnected_() override;

    bool getCTS_(void);

    void pollThreadEntry_();

    void pollSerialPort_();
};

#endif // SERIAL_PORT_IN_RIG_CONTROLLER_H
