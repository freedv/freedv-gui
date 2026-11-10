/*
    util.h
    
    Miscellaneous utility functions
*/

#include "main.h"
#include "codec2_fdmdv.h"

#ifdef _WIN32
#include <strsafe.h>
#endif

// Callback from plot_spectrum & plot_waterfall.  would be nice to
// work out a way to do this without globals.
extern std::atomic<float> g_RxFreqOffsetHz;
extern std::atomic<float> g_TxFreqOffsetHz;
extern FreeDVInterface freedvInterface;
extern std::atomic<bool>             g_tx;

void clickTune(float freq) {

    // The demod is hard-wired to expect a centre frequency of
    // FDMDV_FCENTRE.  So we want to take the signal centered on the
    // click tune freq and re-centre it on FDMDV_FCENTRE.  For example
    // if the click tune freq is 1500Hz, and FDMDV_CENTRE is 1200 Hz,
    // we need to shift the input signal centred on 1500Hz down to
    // 1200Hz, an offset of -300Hz.

    // Bit of an "indent" as we are often trying to get it back
    // exactly in the centre

    if (fabs(FDMDV_FCENTRE - freq) < 10.0) {
        log_info("Requested frequency close to center, just using center.");
        freq = FDMDV_FCENTRE;
    }

    g_TxFreqOffsetHz.store(freq - FDMDV_FCENTRE, std::memory_order_relaxed);
    g_RxFreqOffsetHz.store(FDMDV_FCENTRE - freq, std::memory_order_relaxed);
    log_info("g_TxFreqOffsetHz: %f g_RxFreqOffsetHz: %f", g_TxFreqOffsetHz.load(std::memory_order_relaxed), g_RxFreqOffsetHz.load(std::memory_order_relaxed));
}

bool MainApp::CanAccessSerialPort(std::string const& portName)
{
    bool couldOpen = true;
    com_handle_t com_handle = COM_HANDLE_INVALID;
    
#ifdef _WIN32
    {
        if (portName.substr(0, 3) != "COM")
        {
            // assume we can open if we don't have a valid port name.
            return couldOpen;
        }
        
        TCHAR  nameWithStrangePrefix[100];
        StringCchPrintf(nameWithStrangePrefix, 100, TEXT("\\\\.\\%hs"), portName.c_str());
	
        if((com_handle=CreateFile(nameWithStrangePrefix
                                   ,GENERIC_READ | GENERIC_WRITE/* Access */
                                   ,0				/* Share mode */
                                   ,NULL		 	/* Security attributes */
                                   ,OPEN_EXISTING		/* Create access */
                                   ,0                           /* File attributes */
                                   ,NULL		        /* Template */
                                   ))==INVALID_HANDLE_VALUE) {
           couldOpen = false;
    	}
        else
        {
            CloseHandle(com_handle);
        }
    }
#else
	{
        if (portName.substr(0, 5) != "/dev/")
        {
            // assume we can open if we don't have a valid port name.
            return couldOpen;
        }
        
		if((com_handle=open(portName.c_str(), O_NONBLOCK|O_RDWR))== COM_HANDLE_INVALID)
        {
            couldOpen = false;
        }
        else
        {
            close(com_handle);
        }
	}
#endif
    
    if (!couldOpen)
    {
        std::string errorMessage = "Could not open serial port " + portName + ".";
        
        #ifdef _WIN32
        errorMessage += " Please ensure that no other applications are accessing the port.";
        #elif __linux
        errorMessage += " Please ensure that you have permission to access the port. Adding yourself to the 'dialout' group (and logging out/back in) along with reattaching your radio to your PC will typically ensure this.";
        #else
        errorMessage += " Please ensure that you have permission to access the port.";
        #endif
         
        CallAfter([&, errorMessage]() {
            wxMessageBox(
                errorMessage, 
                wxT("Error"), wxOK | wxICON_ERROR, GetTopWindow());
        });
    }
    
    return couldOpen;
}

//----------------------------------------------------------------
// isReceiveOnly()
//----------------------------------------------------------------

bool MainFrame::isReceiveOnly()
{
    return 
        wxGetApp().appConfiguration.reportingConfiguration.freedvReporterForceReceiveOnly || 
        g_nSoundCards.load(std::memory_order_acquire) <= 1;
}

//----------------------------------------------------------------
// OpenSerialPort()
//----------------------------------------------------------------

void MainFrame::OpenSerialPort(void)
{
    if(!wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPort->IsEmpty()) 
    {
        if (wxGetApp().CanAccessSerialPort((const char*)wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPort->ToUTF8()))
        {
            wxGetApp().rigPttController = std::make_shared<SerialPortOutRigController>(
                    (const char*)wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPort->c_str(),
                    wxGetApp().appConfiguration.rigControlConfiguration.serialPTTUseRTS,
                    wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPolarityRTS,
                    wxGetApp().appConfiguration.rigControlConfiguration.serialPTTUseDTR,
                    wxGetApp().appConfiguration.rigControlConfiguration.serialPTTPolarityDTR);
            wxGetApp().rigFrequencyController = nullptr;
            
            wxGetApp().rigPttController->onRigError += [&](IRigController*, std::string const& err) {
                std::string fullErrMsg = "Couldn't open serial port for PTT output: " + err; 
                CallAfter([&]() 
                {
                    wxMessageBox(fullErrMsg, wxT("Error"), wxOK | wxICON_ERROR, this);
                });
            };

            wxGetApp().rigPttController->onPttChange += [&](IRigPttController*, bool state) {
                onRigPttChange_(state);
            };

            wxGetApp().rigPttController->connect();
        }
    }
}

//----------------------------------------------------------------
// OpenPTTInPort()
//----------------------------------------------------------------

void MainFrame::OpenPTTInPort(void)
{
    if(!wxGetApp().appConfiguration.rigControlConfiguration.serialPTTInputPort->IsEmpty()) 
    {
        if (wxGetApp().CanAccessSerialPort((const char*)wxGetApp().appConfiguration.rigControlConfiguration.serialPTTInputPort->ToUTF8()))
        {
            wxGetApp().m_pttInSerialPort = std::make_shared<SerialPortInRigController>(
                (const char*)wxGetApp().appConfiguration.rigControlConfiguration.serialPTTInputPort->c_str(),
                wxGetApp().appConfiguration.rigControlConfiguration.serialPTTInputPolarityCTS);
            
            wxGetApp().m_pttInSerialPort->onRigError += [&](IRigController*, std::string const& err)
            {
                std::string fullErr = "Couldn't open PTT input port: " + err;
                CallAfter([&]() 
                {
                    wxMessageBox(fullErr, wxT("Error"), wxOK | wxICON_ERROR, this);
                });
            };

            wxGetApp().m_pttInSerialPort->onPttChange += [&](IRigController*, bool pttState)
            {
                log_info("PTT input state is now %d", pttState);
                GetEventHandler()->CallAfter([this, pttState]() {
                    if (pttState != m_btnTogPTT->GetValue())
                    {
                        m_btnTogPTT->SetValue(pttState);                        
                        togglePTT(); 
                    }
                });
            };

            wxGetApp().m_pttInSerialPort->connect();
        }
    }
}


//----------------------------------------------------------------
// ClosePTTInPort()
//----------------------------------------------------------------

void MainFrame::ClosePTTInPort(void)
{
    if (wxGetApp().m_pttInSerialPort)
    {
        wxGetApp().m_pttInSerialPort->disconnect();
        wxGetApp().m_pttInSerialPort = nullptr;
    }
}

extern std::atomic<GenericFIFO<short>*> g_txDataInFifo;
extern std::atomic<GenericFIFO<short>*> g_rxDataOutFifo;

char my_get_next_tx_char(void *) {
    short ch = 0;

    auto tmpFifo = g_txDataInFifo.load(std::memory_order_acquire);
    tmpFifo->read(&ch, 1);
    return (char)ch;
}

void my_put_next_rx_char(void *, char c) {
    short ch = (short)((unsigned char)c);
    auto tmpFifo = g_rxDataOutFifo.load(std::memory_order_acquire);
    if (tmpFifo != nullptr)
    {
        tmpFifo->write(&ch, 1);
    }
}

void freq_shift_coh(COMP rx_fdm_fcorr[], COMP rx_fdm[], float foff, float Fs, COMP *foff_phase_rect, int nin) FREEDV_NONBLOCKING
{
    COMP  foff_rect;
    float mag;
    int   i;

    foff_rect.real = cosf(2.0*M_PI*foff/Fs);
    foff_rect.imag = sinf(2.0*M_PI*foff/Fs);
    for(i=0; i<nin; i++) {
	*foff_phase_rect = cmult(*foff_phase_rect, foff_rect);
	rx_fdm_fcorr[i] = cmult(rx_fdm[i], *foff_phase_rect);
    }

    /* normalise digital oscillator as the magnitude can drift over time */

    mag = cabsolute(*foff_phase_rect);
    foff_phase_rect->real /= mag;
    foff_phase_rect->imag /= mag;
}

// Decimates samples using an algorithm that produces nice plots of
// speech signals at a low sample rate.  We want a low sample rate so
// we don't hammer the graphics system too hard.  Saves decimated data
// to a fifo for plotting on screen.

void resample_for_plot(GenericFIFO<short> *plotFifo, short buf[], short* dec_samples, int length, int fs) FREEDV_NONBLOCKING
{
    int decimation = fs/WAVEFORM_PLOT_FS;
    int nSamples, sample;
    int i, st, en, max, min;

    nSamples = length/decimation;
    if (nSamples % 2) nSamples++; // dec_samples is populated in groups of two

    for(sample = 0; sample < nSamples; sample += 2)
    {
        st = decimation*sample;
        en = decimation*(sample+2);
        max = min = 0;
        for(i=st; i<en && i<length; i++ )
        {
            if (max < buf[i]) max = buf[i];
            if (min > buf[i]) min = buf[i];
        }
        dec_samples[sample] = max;
        dec_samples[sample+1] = min;
    }
    plotFifo->write(dec_samples, nSamples);
}

void MainFrame::executeOnUiThreadAndWait_(std::function<void()> fn)
{
    // Already on the UI thread: run it here. Queuing it and waiting would
    // deadlock -- what's queued only runs when the UI thread next turns
    // round the loop, and this call is what's keeping it from doing that.
    if (wxIsMainThread())
    {
        fn();
        return;
    }

    std::mutex funcMutex;
    std::condition_variable funcConditionVariable;
    bool funcDone = false; // guarded by funcMutex
    std::unique_lock<std::mutex> funcLock(funcMutex);

    CallAfter([&]() {
        std::unique_lock<std::mutex> guiLock(funcMutex);

        fn();

        funcDone = true;
        funcConditionVariable.notify_one();
    });
    
    // Waited out rather than timed out: CallAfter reports nothing, so
    // there's no way to learn the work was dropped (a window destroyed with
    // it still queued, say), and returning while it's still queued would
    // pull the ground out from under it -- the work refers to the caller's
    // locals. Say when it's taking that long, which is what such a stall
    // looks like from the outside. Both waits check funcDone: a wait can
    // wake without a notify, and returning then would be returning early.
    if (!funcConditionVariable.wait_for(funcLock, std::chrono::seconds(5), [&]() { return funcDone; }))
    {
        log_warn("Still waiting for the UI thread to run work handed to it");
        funcConditionVariable.wait(funcLock, [&]() { return funcDone; });
    }
}
