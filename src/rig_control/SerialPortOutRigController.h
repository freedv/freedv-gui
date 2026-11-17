//=========================================================================
// Name:            SerialPortOutRigController.h
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

#ifndef SERIAL_PORT_OUT_RIG_CONTROLLER_H
#define SERIAL_PORT_OUT_RIG_CONTROLLER_H

#include "IRigPttController.h"
#include "SerialPortRigController.h"

class SerialPortOutRigController : virtual public IRigPttController, virtual public SerialPortRigController
{
public:
    SerialPortOutRigController(std::string serialPort, bool useRTS, bool RTSPos, bool useDTR, bool DTRPos);
    virtual ~SerialPortOutRigController();

    virtual void ptt(bool state) override;
    
    virtual int getRigResponseTimeMicroseconds() override;

protected:
    virtual void onConnected_() override;

private:
    bool useRTS_;
    bool rtsPos_;
    bool useDTR_;
    bool dtrPos_;
    std::atomic<int> rigResponseTime_;

    void pttImpl_(bool state);
};

#endif // SERIAL_PORT_OUT_RIG_CONTROLLER_H