#include <vector>

#include "../plot_samples.h"
#include "../../../test/UnitTestCommon.h"

// FreeDV's waveform plots (Frm Radio, Frm Mic, Frm Decoder) used to take
// exactly WAVEFORM_PLOT_BUF samples from their FIFO on each 100ms timer tick,
// and to paint a chunk of zeros when that read failed. The samples are
// written by the audio pipeline on the sound card's clock, a burst at a time,
// so the read failed whenever the audio clock was momentarily behind the UI
// timer -- and each failure drew an audio dropout that no audio actually
// suffered. A fixed chunk per tick also never pays off a backlog, so the FIFO
// drifts towards full and then really does lose audio.

namespace {

// As in defines.h and main.cpp.
constexpr int WAVEFORM_PLOT_BUF = 40;         // samples wanted per 100ms tick
constexpr int PLOT_BUF_MULTIPLIER = 8;        // chunks a plot FIFO holds
constexpr int PLOT_SAMPLES = PLOT_BUF_MULTIPLIER * WAVEFORM_PLOT_BUF;
constexpr int BLOCK = 8;                      // samples written at a time

GenericFIFO<short> makePlotFifo()
{
    return GenericFIFO<short>(PLOT_SAMPLES);
}

// One plot's state between ticks, as MainFrame keeps it.
int emptyTicks = 0;

int drain(GenericFIFO<short>& fifo, std::vector<short>& buffer)
{
    return DrainPlotFifo(fifo, buffer.data(), WAVEFORM_PLOT_BUF, PLOT_SAMPLES, emptyTicks);
}

// Writes `numSamples` samples all carrying the value `tag`.
bool writeBlock(GenericFIFO<short>& fifo, int tag, int numSamples = BLOCK)
{
    std::vector<short> samples(numSamples, (short)tag);
    return fifo.write(samples.data(), numSamples) == 0;
}

bool sameSamples(const std::vector<short>& expected, const std::vector<short>& actual, const char* what)
{
    if (expected != actual)
    {
        std::cout << "[" << what << ": got " << actual.size() << " samples"
                  << ", expected " << expected.size() << "] ";
        return false;
    }
    return true;
}

// What the plots used to do wrong: a tick that finds fewer samples than it
// wants used to read nothing at all and paint zeros.
bool aPartialTickIsPlottedRatherThanZeroed()
{
    auto fifo = makePlotFifo();
    emptyTicks = 0;
    std::vector<short> buffer(PLOT_SAMPLES);

    // 32 samples: less than the 40 a fixed read demanded.
    if (!writeBlock(fifo, 7, 32))
    {
        std::cout << "[write failed] ";
        return false;
    }

    int numRead = drain(fifo, buffer);
    std::vector<short> expected(32, 7);
    return (numRead == 32) && sameSamples(expected, std::vector<short>(buffer.begin(), buffer.begin() + numRead),
                                          "32 samples arriving");
}

// The bursts measured on Linux: a modem frame's worth of decimated samples
// lands every few ticks, so some ticks find none and some find a lot. Every
// sample has to reach the trace, once, in order, and no silence has to be
// invented in between.
bool aDriftingAudioClockLosesNothing()
{
    auto fifo = makePlotFifo();
    emptyTicks = 0;
    std::vector<short> buffer(PLOT_SAMPLES);
    std::vector<short> plotted;
    std::vector<short> expected;

    int nextTag = 0;
    for (int tick = 0; tick < 100; tick++)
    {
        if (tick % 7 < 2)
        {
            for (int block = 0; block < 6; block++)
            {
                if (!writeBlock(fifo, nextTag))
                {
                    std::cout << "[write dropped, tick " << tick << "] ";
                    return false;
                }
                for (int i = 0; i < BLOCK; i++) expected.push_back((short)nextTag);
                nextTag++;
            }
        }

        int numRead = drain(fifo, buffer);
        plotted.insert(plotted.end(), buffer.begin(), buffer.begin() + numRead);
    }

    // Anything still queued is plotted by later ticks.
    while (fifo.numUsed() > 0)
    {
        int numQueued = drain(fifo, buffer);
        plotted.insert(plotted.end(), buffer.begin(), buffer.begin() + numQueued);
    }

    return sameSamples(expected, plotted, "bursts arriving between ticks");
}

// A backlog has to be paid off, not left to grow until the FIFO is full and
// starts throwing audio away.
bool aBacklogIsPaidOff()
{
    auto fifo = makePlotFifo();
    emptyTicks = 0;
    std::vector<short> buffer(PLOT_SAMPLES);

    int written = 0;
    while (writeBlock(fifo, written % 100))
    {
        written++;
    }

    int numRead = drain(fifo, buffer);
    if (numRead != written * BLOCK)
    {
        std::cout << "[backlog of " << written * BLOCK << " samples: read " << numRead << "] ";
        return false;
    }

    // The FIFO is empty again, so the pipeline can carry on writing.
    return writeBlock(fifo, 1);
}

// A burst slightly bigger than one tick doesn't make the trace jump: the
// scroll stays one tick's worth until the backlog is more than a couple of
// ticks deep.
bool aSmallBacklogKeepsTheScrollSteady()
{
    auto fifo = makePlotFifo();
    emptyTicks = 0;
    std::vector<short> buffer(PLOT_SAMPLES);

    int queued = 0;
    for (int tick = 0; tick < 5; tick++)
    {
        if (!writeBlock(fifo, tick, 48))
        {
            std::cout << "[write failed] ";
            return false;
        }
        queued += 48;

        int numRead = drain(fifo, buffer);
        if (numRead != WAVEFORM_PLOT_BUF)
        {
            std::cout << "[with " << queued << " queued, read " << numRead
                      << ", wanted " << WAVEFORM_PLOT_BUF << "] ";
            return false;
        }
        queued -= numRead;
    }

    return true;
}

// The caller's buffer is the limit, however much is queued.
bool theBufferIsNotOverrun()
{
    auto fifo = makePlotFifo();
    emptyTicks = 0;
    std::vector<short> buffer(100);

    for (int tag = 0; tag < 25; tag++)
    {
        if (!writeBlock(fifo, tag))
        {
            std::cout << "[write failed] ";
            return false;
        }
    }

    int numRead = DrainPlotFifo(fifo, buffer.data(), WAVEFORM_PLOT_BUF, (int)buffer.size(), emptyTicks);
    return (numRead == 100) && (fifo.numUsed() == 100);
}

// Nothing to read is left alone: the trace doesn't advance, rather than
// drawing silence for a burst that simply hasn't landed yet.
bool anEmptyFifoPlotsNothing()
{
    auto fifo = makePlotFifo();
    emptyTicks = 0;
    std::vector<short> buffer(PLOT_SAMPLES, 123);

    if (drain(fifo, buffer) != 0)
    {
        std::cout << "[empty FIFO read] ";
        return false;
    }

    if (!writeBlock(fifo, 5, 32))
    {
        std::cout << "[write failed] ";
        return false;
    }

    int numRead = drain(fifo, buffer);
    std::vector<short> expected(32, 5);
    return (numRead == 32) && sameSamples(expected, std::vector<short>(buffer.begin(), buffer.begin() + numRead),
                                          "audio arriving after an empty tick");
}

// An input that has stopped -- Frm Mic while receiving in half duplex, where
// the transmit pipeline doesn't run -- mustn't freeze the trace on the last
// audio it showed. After a second of nothing, the trace scrolls silence.
bool aStoppedInputScrollsSilence()
{
    auto fifo = makePlotFifo();
    emptyTicks = 0;
    std::vector<short> buffer(PLOT_SAMPLES, 123);

    for (int tick = 0; tick < PLOT_FIFO_IDLE_TICKS; tick++)
    {
        if (drain(fifo, buffer) != 0)
        {
            std::cout << "[silence drawn after only " << (tick + 1) << " empty ticks] ";
            return false;
        }
    }

    // From here on, one tick's worth of silence per tick.
    for (int tick = 0; tick < 3; tick++)
    {
        std::fill(buffer.begin(), buffer.end(), 123);
        int numRead = drain(fifo, buffer);
        std::vector<short> expected(WAVEFORM_PLOT_BUF, 0);
        if ((numRead != WAVEFORM_PLOT_BUF) ||
            !sameSamples(expected, std::vector<short>(buffer.begin(), buffer.begin() + numRead), "idle input"))
        {
            std::cout << "[idle tick read " << numRead << "] ";
            return false;
        }
    }

    // Audio coming back is plotted as it is, and the next gap is held again
    // rather than filled.
    if (!writeBlock(fifo, 9, 32))
    {
        std::cout << "[write failed] ";
        return false;
    }
    int numRead = drain(fifo, buffer);
    std::vector<short> expected(32, 9);
    if ((numRead != 32) ||
        !sameSamples(expected, std::vector<short>(buffer.begin(), buffer.begin() + numRead), "audio resuming"))
    {
        return false;
    }
    return drain(fifo, buffer) == 0;
}

}

int main()
{
    executeTestCase("aPartialTickIsPlottedRatherThanZeroed", aPartialTickIsPlottedRatherThanZeroed);
    executeTestCase("aDriftingAudioClockLosesNothing", aDriftingAudioClockLosesNothing);
    executeTestCase("aBacklogIsPaidOff", aBacklogIsPaidOff);
    executeTestCase("aSmallBacklogKeepsTheScrollSteady", aSmallBacklogKeepsTheScrollSteady);
    executeTestCase("theBufferIsNotOverrun", theBufferIsNotOverrun);
    executeTestCase("anEmptyFifoPlotsNothing", anEmptyFifoPlotsNothing);
    executeTestCase("aStoppedInputScrollsSilence", aStoppedInputScrollsSilence);
    return testResult();
}
