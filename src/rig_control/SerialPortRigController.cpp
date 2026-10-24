//=========================================================================
// Name:            SerialPortRigController.cpp
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

#include "SerialPortRigController.h"

#include "../util/logging/ulog.h"

SerialPortRigController::SerialPortRigController(std::string serialPort)
    : ThreadedObject("SerialController")
    , serialPortHandle_(COM_HANDLE_INVALID)
    , serialPort_(std::move(serialPort))
{
    // empty
}

SerialPortRigController::~SerialPortRigController()
{
    // Disconnect in a synchronous fashion before killing our thread.
    std::shared_ptr<std::promise<void>> prom = std::make_shared<std::promise<void>>();
    auto fut = prom->get_future();

    enqueue_([this, prom]() {
        disconnectImpl_();
        prom->set_value();
    });
    
    fut.wait();
    
    waitForAllTasksComplete_();
}

void SerialPortRigController::connect()
{
    enqueue_(std::bind(&SerialPortRigController::connectImpl_, this));
}

void SerialPortRigController::disconnect()
{
    enqueue_(std::bind(&SerialPortRigController::disconnectImpl_, this));
}

bool SerialPortRigController::isConnected()
{
    return serialPortHandle_ != COM_HANDLE_INVALID;
}

//----------------------------------------------------------------
// (raise|lower)(RTS|DTR)()
//
// Raises/lowers the specified signal
//----------------------------------------------------------------

void SerialPortRigController::raiseDTR_(void)
{
    if(serialPortHandle_ == COM_HANDLE_INVALID)
        return;
#ifdef _WIN32
    EscapeCommFunction(serialPortHandle_, SETDTR);
#else
    {    // For C89 happiness
        int flags = TIOCM_DTR;
        ioctl(serialPortHandle_, TIOCMBIS, &flags);
    }
#endif
}

void SerialPortRigController::raiseRTS_(void)
{
    if(serialPortHandle_ == COM_HANDLE_INVALID)
        return;
#ifdef _WIN32
    EscapeCommFunction(serialPortHandle_, SETRTS);
#else
    {    // For C89 happiness
        int flags = TIOCM_RTS;
        ioctl(serialPortHandle_, TIOCMBIS, &flags);
    }
#endif
}

void SerialPortRigController::lowerDTR_(void)
{
    if(serialPortHandle_ == COM_HANDLE_INVALID)
        return;
#ifdef _WIN32
    EscapeCommFunction(serialPortHandle_, CLRDTR);
#else
    {    // For C89 happiness
        int flags = TIOCM_DTR;
        ioctl(serialPortHandle_, TIOCMBIC, &flags);
    }
#endif
}

void SerialPortRigController::lowerRTS_(void)
{
    if(serialPortHandle_ == COM_HANDLE_INVALID)
        return;
#ifdef _WIN32
    EscapeCommFunction(serialPortHandle_, CLRRTS);
#else
    {    // For C89 happiness
        int flags = TIOCM_RTS;
        ioctl(serialPortHandle_, TIOCMBIC, &flags);
    }
#endif
}

// These operate in the context of the object's thread, not e.g. the GUI thread.

void SerialPortRigController::connectImpl_()
{
#ifdef _WIN32
    {
        COMMTIMEOUTS timeouts;
        DCB    dcb;
        TCHAR  lpszFunction[100];
    
        // As per:
        //   [1] https://support.microsoft.com/en-us/help/115831/howto-specify-serial-ports-larger-than-com9
        //   [2] Hamlib lib/termios.c, win32_serial_open()

        /*  
                To test change of COM port for USB serial device on Windows

                1/ Run->devmgmnt.msc
                2/ Change COM port Ports (COM & LPT) -> Serial Device -> Properties Tab -> Advanced
                3/ Unplug USB serial device and plug in again.  This is really important.  FreeDV won't recognise
                   new COM port number until this is done.
                4/ Test PTT on FreeDV Tools->PTT
        */

        TCHAR  nameWithStrangePrefix[100];
        StringCchPrintf(nameWithStrangePrefix, 100, TEXT("\\\\.\\%hs"), serialPort_.c_str());
        log_debug("nameWithStrangePrefix: \\\\.\\%s", serialPort_.c_str());
    
        if((serialPortHandle_=CreateFile(nameWithStrangePrefix
                                   ,GENERIC_READ | GENERIC_WRITE/* Access */
                                   ,0                /* Share mode */
                                   ,NULL             /* Security attributes */
                                   ,OPEN_EXISTING        /* Create access */
                                   ,0                           /* File attributes */
                                   ,NULL                /* Template */
                                   ))==INVALID_HANDLE_VALUE) {
            StringCchPrintf(lpszFunction, 100, TEXT("%s"), TEXT("CreateFile"));
            goto error;
        }
        log_debug("CreateFileA OK");
    
        if(GetCommTimeouts(serialPortHandle_, &timeouts)) {
            log_debug("GetCommTimeouts OK");
        
            timeouts.ReadIntervalTimeout=MAXDWORD;
            timeouts.ReadTotalTimeoutMultiplier=0;
            timeouts.ReadTotalTimeoutConstant=0;        // No-wait read timeout
            timeouts.WriteTotalTimeoutMultiplier=0;
            timeouts.WriteTotalTimeoutConstant=5000;    // 5 seconds
            if (!SetCommTimeouts(serialPortHandle_,&timeouts)) {
                StringCchPrintf(lpszFunction, 100, TEXT("%s"), TEXT("SetCommTimeouts"));
                goto error;          
            }
            log_debug("SetCommTimeouts OK");
        } else {
            StringCchPrintf(lpszFunction, 100, TEXT("%s"), TEXT("GetCommTimeouts"));
            goto error;
        }

        /* Force N-8-1 mode: */
        if(GetCommState(serialPortHandle_, &dcb)==TRUE) {
        log_debug("GetCommState OK");
        
            dcb.ByteSize        = 8;
            dcb.Parity            = NOPARITY;
            dcb.StopBits        = ONESTOPBIT;
            dcb.DCBlength        = sizeof(DCB);
            dcb.fBinary            = TRUE;
            dcb.fOutxCtsFlow    = FALSE;
            dcb.fOutxDsrFlow    = FALSE;
            dcb.fDtrControl        = DTR_CONTROL_DISABLE;
            dcb.fDsrSensitivity    = FALSE;
            dcb.fTXContinueOnXoff= TRUE;
            dcb.fOutX            = FALSE;
            dcb.fInX            = FALSE;
            dcb.fRtsControl        = RTS_CONTROL_DISABLE;
            dcb.fAbortOnError    = FALSE;
            if (!SetCommState(serialPortHandle_, &dcb)) {
                  StringCchPrintf(lpszFunction, 100, TEXT("%s"), TEXT("SetCommState"));
                goto error;           
            }
        log_debug("SetCommState OK");
        } else {
            StringCchPrintf(lpszFunction, 100, TEXT("%s"), TEXT("GetCommState"));
            goto error;           
        }

        onRigConnected(this);
        return;
    
error:
#ifdef UNICODE
        std::vector<char> buffer;
        std::string errFn = "[Unknown Function]";
        int size = WideCharToMultiByte(CP_UTF8, 0, lpszFunction, -1, NULL, 0, NULL, NULL);
        if (size > 0) 
        {
            buffer.resize(size);
            WideCharToMultiByte(CP_UTF8, 0, lpszFunction, -1, static_cast<LPSTR>(&buffer[0]), buffer.size(), NULL, NULL);
            errFn = std::string(&buffer[0]);
        }
        else 
        {
            // Error handling, probably shouldn't reach here
        }
        log_error("%s failed", errFn.c_str());
#else
        log_error("%s failed", lpszFunction);
#endif // UNICODE

        // Retrieve the system error message for the last-error code

        LPVOID lpMsgBuf;
        LPVOID lpDisplayBuf;
        DWORD dw = GetLastError(); 

        FormatMessage(
                      FORMAT_MESSAGE_ALLOCATE_BUFFER | 
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL,
                      dw,
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      (LPTSTR) &lpMsgBuf,
                      0, NULL );

        // Display the error message

        lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, 
                                          (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR)); 
        StringCchPrintf((LPTSTR)lpDisplayBuf, 
                        LocalSize(lpDisplayBuf) / sizeof(TCHAR),
                        TEXT("%s failed with error %d: %s"), 
                        lpszFunction, dw, lpMsgBuf); 

        onRigError(this, (LPCTSTR)lpDisplayBuf);

        LocalFree(lpMsgBuf);
        LocalFree(lpDisplayBuf);

        return;
    }
#else
    {
        struct termios t;

        if((serialPortHandle_=open(serialPort_.c_str(), O_NONBLOCK|O_RDWR|O_NOCTTY))== COM_HANDLE_INVALID)
        {
            std::string errMsg = "Could not open serial port " + serialPort_;
            onRigError(this, errMsg);
            return;
        }

        // Skip termios configuration for PTT Input to avoid tcgetattr/tcsetattr hang on tty0tty driver
        if (shouldConfigureTermios_())
        {
            if(tcgetattr(serialPortHandle_, &t)==-1)
            {
                close(serialPortHandle_);
                serialPortHandle_ = COM_HANDLE_INVALID;

                std::string errMsg = "Could not open serial port " + serialPort_;
                onRigError(this, errMsg);
                return;
            }

            t.c_iflag = (
                          IGNBRK   /* ignore BREAK condition */
                        | IGNPAR   /* ignore (discard) parity errors */
                        );
            t.c_oflag = 0;    /* No output processing */
            t.c_cflag = (
                          CS8         /* 8 bits */
                        | CREAD       /* enable receiver */
            /*
            Fun snippet from the FreeBSD manpage:
                If CREAD is set, the receiver is enabled.  Otherwise, no character is
                received.  Not all hardware supports this bit.  In fact, this flag is
                pretty silly and if it were not part of the termios specification it
                would be omitted.
            */
                        | CLOCAL      /* ignore modem status lines */
                        );
            t.c_lflag = 0;    /* No local modes */

            if(tcsetattr(serialPortHandle_, TCSANOW, &t)==-1)
            {
                close(serialPortHandle_);
                serialPortHandle_ = COM_HANDLE_INVALID;

                std::string errMsg = "Could not open serial port " + serialPort_;
                onRigError(this, errMsg);
                return;
            }
        }

        onRigConnected(this);

    }
#endif

}


void SerialPortRigController::disconnectImpl_()
{
    if (serialPortHandle_ == COM_HANDLE_INVALID)
    {
        // Not connected (e.g. the port couldn't be opened), so there's 
        // nothing to close and no disconnection to report.
        return;
    }

#ifdef _WIN32
    CloseHandle(serialPortHandle_);
#else
    close(serialPortHandle_);
#endif
    serialPortHandle_ = COM_HANDLE_INVALID;

    onRigDisconnected(this);
}
