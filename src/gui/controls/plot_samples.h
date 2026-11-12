#ifndef PLOT_SAMPLES_H
#define PLOT_SAMPLES_H

#include <algorithm>

#include "../../util/GenericFIFO.h"

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
// Returning nothing when the FIFO is empty is deliberate. An empty FIFO
// usually just means the next burst hasn't landed yet, so drawing silence
// would put a dropout in the trace that isn't in the audio; leaving the trace
// alone for a tick does that no damage.

template <typename T>
int DrainPlotFifo(GenericFIFO<T>& fifo, T* buffer, int steadySamples, int maxSamples)
{
    int numUsed = fifo.numUsed();
    int numToRead = std::min(numUsed, (numUsed > 2 * steadySamples) ? maxSamples : steadySamples);

    if ((numToRead <= 0) || (fifo.read(buffer, numToRead) != 0))
    {
        return 0;
    }

    return numToRead;
}

#endif // PLOT_SAMPLES_H
