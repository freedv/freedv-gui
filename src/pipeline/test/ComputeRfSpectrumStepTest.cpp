#include <cmath>
#include <vector>

#include "../ComputeRfSpectrumStep.h"
#include "../../test/UnitTestCommon.h"

namespace {

constexpr int SAMPLE_RATE = 8000;
constexpr int BLOCK_SIZE = 160;
constexpr int NUM_BLOCKS = 20; // enough to fill the FFT's input buffer

// realtime_fp only accepts plain function pointers, so state is kept here.
struct MODEM_STATS g_stats;
GenericFIFO<float>* g_avMagFifo;

struct Fixture
{
    GenericFIFO<float> fifo { MODEM_STATS_NSPEC * NUM_BLOCKS };
    ComputeRfSpectrumStep step {
        +[]() FREEDV_NONBLOCKING { return &g_stats; },
        +[]() FREEDV_NONBLOCKING { return g_avMagFifo; }
    };

    Fixture()
    {
        modem_stats_open(&g_stats);
        g_avMagFifo = &fifo;
    }

    ~Fixture()
    {
        modem_stats_close(&g_stats);
    }

    // Feeds a tone through the step and returns the last spectrum it produced.
    std::vector<float> spectrumForTone(double freqHz, double amplitude)
    {
        for (int block = 0; block < NUM_BLOCKS; block++)
        {
            short input[BLOCK_SIZE];
            for (int i = 0; i < BLOCK_SIZE; i++)
            {
                input[i] = (short)(amplitude * std::cos(2 * M_PI * freqHz * (block * BLOCK_SIZE + i) / SAMPLE_RATE));
            }

            int numOutput = -1;
            step.execute(input, BLOCK_SIZE, &numOutput);
        }

        std::vector<float> spectrum(MODEM_STATS_NSPEC);
        while (fifo.numUsed() >= MODEM_STATS_NSPEC)
        {
            fifo.read(spectrum.data(), MODEM_STATS_NSPEC);
        }
        return spectrum;
    }
};

int peakBin(std::vector<float> const& spectrum)
{
    int result = 0;
    for (int bin = 1; bin < (int)spectrum.size(); bin++)
    {
        if (spectrum[bin] > spectrum[result]) result = bin;
    }
    return result;
}

bool sampleRatesAreModemRate()
{
    Fixture f;
    return f.step.getInputSampleRate() == SAMPLE_RATE && f.step.getOutputSampleRate() == SAMPLE_RATE;
}

bool isTapWithNoOutput()
{
    Fixture f;
    short input[BLOCK_SIZE] = {};
    int numOutput = -1;
    return f.step.execute(input, BLOCK_SIZE, &numOutput) == nullptr && numOutput == 0;
}

bool writesOneSpectrumPerCall()
{
    Fixture f;
    short input[BLOCK_SIZE] = {};
    int numOutput = 0;

    for (int call = 1; call <= 3; call++)
    {
        f.step.execute(input, BLOCK_SIZE, &numOutput);
        if (f.fifo.numUsed() != MODEM_STATS_NSPEC * call)
        {
            std::cout << "[" << f.fifo.numUsed() << " values after " << call << " call(s)] ";
            return false;
        }
    }
    return true;
}

bool spectrumPeaksAtToneFrequency()
{
    // Spectrum covers 0 to MODEM_STATS_MAX_F_HZ.
    const double HZ_PER_BIN = (double)MODEM_STATS_MAX_F_HZ / MODEM_STATS_NSPEC;

    for (double freqHz : { 500.0, 1500.0, 2800.0 })
    {
        Fixture f;
        auto spectrum = f.spectrumForTone(freqHz, 10000);

        double peakHz = peakBin(spectrum) * HZ_PER_BIN;
        if (std::fabs(peakHz - freqHz) > 2 * HZ_PER_BIN)
        {
            std::cout << "[" << freqHz << " Hz tone has peak at " << peakHz << " Hz] ";
            return false;
        }
    }
    return true;
}

bool spectrumLevelFollowsSignalLevel()
{
    // Only one fixture can exist at a time as they share the modem stats object.
    auto peakForAmplitude = [](double amplitude) {
        Fixture f;
        auto spectrum = f.spectrumForTone(1500, amplitude);
        return spectrum[peakBin(spectrum)];
    };
    float loudPeak = peakForAmplitude(10000);
    float quietPeak = peakForAmplitude(1000);

    // 10x amplitude = 20 dB.
    if (std::fabs((loudPeak - quietPeak) - 20) > 0.5)
    {
        std::cout << "[loud peak " << loudPeak << " dB, quiet peak " << quietPeak << " dB] ";
        return false;
    }
    return true;
}

// modem_stats_get_rx_spectrum() slides a 2 * MODEM_STATS_NSPEC sample window
// along by nin, so a block bigger than that has to be broken up before the
// call: otherwise the copy runs off the end of the window's buffer, into the
// fft configuration stored right behind it (found by fuzzing the step).
bool oversizedBlocksProduceOneSpectrumPerWindow()
{
    const int WINDOW = 2 * MODEM_STATS_NSPEC;
    const int cases[][2] = {
        {160, 1}, {WINDOW, 1}, {WINDOW + 1, 2}, {2 * WINDOW, 2}, {3000, 3}, {8000, 8}
    };

    for (const int (&testCase)[2] : cases)
    {
        Fixture f;
        std::vector<short> input(testCase[0], 0);
        int numOutput = -1;
        f.step.execute(input.data(), testCase[0], &numOutput);

        int expected = MODEM_STATS_NSPEC * testCase[1];
        if (f.fifo.numUsed() != expected)
        {
            std::cout << "[" << testCase[0] << " samples yielded " << f.fifo.numUsed()
                      << " values, expected " << expected << "] ";
            return false;
        }
    }
    return true;
}

// The spectra the pieces produce have to be real spectra, not whatever the
// window holds after a copy that ran past the end of it.
bool toneSurvivesAnOversizedBlock()
{
    const double HZ_PER_BIN = (double)MODEM_STATS_MAX_F_HZ / MODEM_STATS_NSPEC;
    const int NUM_SAMPLES = 3200;   // more than one window of a 1500 Hz tone

    Fixture f;
    std::vector<short> input(NUM_SAMPLES);
    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        input[i] = (short)(10000 * std::cos(2 * M_PI * 1500.0 * i / SAMPLE_RATE));
    }

    int numOutput = -1;
    f.step.execute(input.data(), NUM_SAMPLES, &numOutput);

    std::vector<float> spectrum(MODEM_STATS_NSPEC);
    while (f.fifo.numUsed() >= MODEM_STATS_NSPEC)
    {
        f.fifo.read(spectrum.data(), MODEM_STATS_NSPEC);
    }

    double peakHz = peakBin(spectrum) * HZ_PER_BIN;
    if (std::fabs(peakHz - 1500.0) > 2 * HZ_PER_BIN)
    {
        std::cout << "[1500 Hz tone in one oversized block peaks at " << peakHz << " Hz] ";
        return false;
    }
    return true;
}

// An empty block still gets one spectrum, as it always did: the window is left
// where it is and transformed again.
bool emptyBlockStillProducesOneSpectrum()
{
    Fixture f;
    int numOutput = -1;
    f.step.execute(nullptr, 0, &numOutput);

    if (f.fifo.numUsed() != MODEM_STATS_NSPEC)
    {
        std::cout << "[empty block yielded " << f.fifo.numUsed() << " values, expected "
                  << MODEM_STATS_NSPEC << "] ";
        return false;
    }
    return true;
}

// A negative count is treated as an empty block. Passed through, it would
// make the window's shift loop read before fft_buf and write past its end.
bool negativeBlockIsAnEmptyOne()
{
    Fixture f;
    short samples[16] = {};
    int numOutput = -1;
    f.step.execute(samples, -100, &numOutput);

    if (numOutput != 0 || f.fifo.numUsed() != MODEM_STATS_NSPEC)
    {
        std::cout << "[negative block: " << numOutput << " output sample(s), "
                  << f.fifo.numUsed() << " spectrum values, expected 0 and "
                  << MODEM_STATS_NSPEC << "] ";
        return false;
    }
    return true;
}

}

int main()
{
    executeTestCase("sampleRatesAreModemRate", sampleRatesAreModemRate);
    executeTestCase("isTapWithNoOutput", isTapWithNoOutput);
    executeTestCase("writesOneSpectrumPerCall", writesOneSpectrumPerCall);
    executeTestCase("spectrumPeaksAtToneFrequency", spectrumPeaksAtToneFrequency);
    executeTestCase("spectrumLevelFollowsSignalLevel", spectrumLevelFollowsSignalLevel);
    executeTestCase("oversizedBlocksProduceOneSpectrumPerWindow", oversizedBlocksProduceOneSpectrumPerWindow);
    executeTestCase("toneSurvivesAnOversizedBlock", toneSurvivesAnOversizedBlock);
    executeTestCase("emptyBlockStillProducesOneSpectrum", emptyBlockStillProducesOneSpectrum);
    executeTestCase("negativeBlockIsAnEmptyOne", negativeBlockIsAnEmptyOne);
    return testResult();
}
