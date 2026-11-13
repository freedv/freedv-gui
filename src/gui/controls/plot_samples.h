#ifndef PLOT_SAMPLES_H
#define PLOT_SAMPLES_H

#include <algorithm>

#include "GenericFIFO.h"

// The waveform plots (Frm Radio, Frm Mic, Frm Decoder) are fed from a FIFO:
// the audio pipeline writes decimated samples as it produces them, and the
// plot timer reads them once every WAVEFORM_PLOT_BUF samples' worth of time.
//
// Reading a fixed WAVEFORM_PLOT_BUF per tick only works while the two clocks
// agree to the sample, and they don't. The samples arrive in bursts -- a
// modem frame's worth at a time -- and the UI timer runs on its own clock, so
// a tick regularly finds fewer samples than a fixed read demands. The old code
// treated that as "read nothing" and drew a chunk of zeros instead: an audio
// dropout in the trace, with no dropout in the audio. Measured on Linux over
// 6.7 minutes of steady reception, 34% of the ticks took that path; the rate
// varies with load, but it's never zero while audio is flowing.
//
// A fixed chunk per tick never pays off a backlog either, so the FIFO drifts
// towards full and then throws audio away at the producer: a 10 minute
// receive-only run drifted the Frm Mic FIFO to 316 of its 319 sample capacity
// and logged 243 drops.
//
// DrainPlotFifo() takes what's there, up to the buffer. The trace advances by
// exactly the samples read, so it stays continuous, catches up when it has
// fallen behind, and loses nothing. One tick's worth is taken at a time while
// the backlog is small, which keeps the trace scrolling at a steady rate; only
// once more than a couple of ticks have piled up does it take everything.
//
// An empty FIFO usually just means the next burst hasn't landed yet, so for a
// short while it returns nothing and the trace holds: drawing silence then
// would put a dropout in the trace that isn't in the audio. But a FIFO can
// also stay empty because nothing feeds it any more -- in half duplex the
// transmit pipeline, and so Frm Mic, only runs while transmitting -- and the
// trace mustn't freeze on the last audio it saw. Once the FIFO has been empty
// for more than `idleTicksBeforeSilence` ticks in a row, each tick plots one
// tick's worth of silence, as the old code did. `emptyTicks` carries that
// count from one tick to the next; it starts at 0.

// Ticks a plot FIFO may stay empty before its trace scrolls silence: 1s at the
// plots' 100ms tick, longer than any modem frame.
constexpr int PLOT_FIFO_IDLE_TICKS = 10;

template <typename T>
int DrainPlotFifo(GenericFIFO<T>& fifo, T* buffer, int steadySamples, int maxSamples, int& emptyTicks,
                  int idleTicksBeforeSilence = PLOT_FIFO_IDLE_TICKS)
{
    int numUsed = fifo.numUsed();
    if (numUsed <= 0)
    {
        emptyTicks = std::min(emptyTicks + 1, idleTicksBeforeSilence + 1);
        if (emptyTicks <= idleTicksBeforeSilence)
        {
            return 0;
        }

        int numSilent = std::min(steadySamples, maxSamples);
        std::fill_n(buffer, numSilent, T());
        return numSilent;
    }

    emptyTicks = 0;
    int numToRead = std::min(numUsed, (numUsed > 2 * steadySamples) ? maxSamples : steadySamples);

    if ((numToRead <= 0) || (fifo.read(buffer, numToRead) != 0))
    {
        return 0;
    }

    return numToRead;
}

#endif // PLOT_SAMPLES_H
