//=========================================================================
// Name:            SerialPortRigController.h
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

#ifndef SERIAL_PORT_RIG_CONTROLLER_H
#define SERIAL_PORT_RIG_CONTROLLER_H

#include <atomic>
#include <string>
#include <vector>
#include <mutex>

#include "ThreadedObject.h"
#include "IRigController.h"

#ifdef _WIN32
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <dlfcn.h>
#endif

#ifdef _WIN32
#define COM_HANDLE_INVALID			INVALID_HANDLE_VALUE
typedef HANDLE      com_handle_t;
#else
#define COM_HANDLE_INVALID			-1
typedef int         com_handle_t;
#endif

class SerialPortRigController : public ThreadedObject, virtual public IRigController
{
public:
    virtual ~SerialPortRigController();
    
    virtual void connect() override;
    virtual void disconnect() override;
    virtual bool isConnected() override;

protected:
    SerialPortRigController(std::string serialPort);

    std::atomic<com_handle_t> serialPortHandle_;

    // Virtual method to allow derived classes to skip termios configuration
    // This is to work around a bug in the tty0tty kernel driver that causes
    // deadlocks if tcgetattr or tcsetattr are called.
    virtual bool shouldConfigureTermios_() const { return true; }

    // Called on the controller's thread once the port is open, just before
    // onRigConnected's handlers, and once it's closed, just before
    // onRigDisconnected's. For a derived controller's own setup and teardown:
    // the handler lists are the application's, which can replace or clear
    // them. (Not reached from ~SerialPortRigController(), by which time the
    // derived part is gone; a derived destructor does its own teardown.)
    virtual void onConnected_() {}
    virtual void onDisconnected_() {}

    void raiseDTR_(void);
    void lowerDTR_(void);
    void raiseRTS_(void);
    void lowerRTS_(void);
    
    bool getCTS_(void);

private:
    std::string serialPort_;

    void connectImpl_();
    void disconnectImpl_();
};

#endif // SERIAL_PORT_RIG_CONTROLLER_H
