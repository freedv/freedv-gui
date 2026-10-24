//=========================================================================
// Name:            FreeDVInterfaceTest.cpp
// Purpose:         Unit tests for FreeDVInterface: what it reports about
//                  RADE, the transmit and receive pipelines it builds,
//                  and the callsign that travels with the voice.
//=========================================================================

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../../defines.h"
#include "../../freedv_interface.h"
#include "pipeline/IPipelineStep.h"
#include "UtilStubs.h"
#include "../../test/UnitTestCommon.h"

// Defined by main.cpp in the application.
std::string utTxFeatureFile;
std::string utRxFeatureFile;
wxString utFreeDVMode;
std::atomic<bool> g_bwExpandEnabled(false);

namespace {

constexpr int MODEM_SAMPLE_RATE = 8000;
constexpr int RADE_SPEECH_RATE = 16000;

template<typename T>
bool check(const char* description, T actual, T expected)
{
    if (actual == expected) return true;
    std::cout << "[" << description << " was " << actual << ", expected " << expected << "] ";
    return false;
}

bool checkText(const char* description, std::string const& actual, std::string const& expected)
{
    if (actual == expected) return true;
    std::cout << "[" << description << " was \"" << actual << "\", expected \"" << expected << "\"] ";
    return false;
}

bool checkTrue(const char* description, bool condition)
{
    if (condition) return true;
    std::cout << "[" << description << "] ";
    return false;
}

// What the receive pipeline reads its settings from and reports sync to.
std::atomic<int> g_rxState(0);
float g_sigPwrAvg = 0.0f;

// How far the user has asked for the received signal to be moved, in Hz.
std::atomic<float> g_rxFreqOffset(0.0f);

// A started FreeDVInterface that's stopped again once the test case is done.
class Interface
{
public:
    explicit Interface(bool usingReliableText = false)
    {
        g_rxState = 0;
        g_sigPwrAvg = 0.0f;
        g_rxFreqOffset = 0.0f;
        g_bwExpandEnabled = false;

        dv.start(100, usingReliableText);
    }

    ~Interface()
    {
        dv.stop();
    }

    std::unique_ptr<IPipelineStep> transmitPipeline(int speechSampleRate = RADE_SPEECH_RATE)
    {
        return std::unique_ptr<IPipelineStep>(dv.createTransmitPipeline(speechSampleRate, MODEM_SAMPLE_RATE));
    }

    std::unique_ptr<IPipelineStep> receivePipeline(int speechSampleRate = RADE_SPEECH_RATE)
    {
        return std::unique_ptr<IPipelineStep>(dv.createReceivePipeline(
            MODEM_SAMPLE_RATE, speechSampleRate,
            +[]() FREEDV_NONBLOCKING { return &g_rxState; },
            +[]() FREEDV_NONBLOCKING { return g_rxFreqOffset.load(std::memory_order_acquire); },
            +[]() FREEDV_NONBLOCKING { return &g_sigPwrAvg; }));
    }

    FreeDVInterface dv;
};

std::vector<short> tone(int sampleRate, double seconds, double freqHz = 1000.0)
{
    std::vector<short> samples((size_t)(sampleRate * seconds));
    for (size_t i = 0; i < samples.size(); i++)
    {
        samples[i] = (short)(3000.0 * std::sin(2.0 * M_PI * freqHz * i / sampleRate));
    }
    return samples;
}

// Feeds samples to a pipeline 20 ms at a time, as TxRxThread does, and
// returns everything that came out of it.
template<typename AfterChunkFn>
std::vector<short> feed(IPipelineStep& step, std::vector<short> const& samples, AfterChunkFn afterChunk)
{
    const size_t chunkSize = step.getInputSampleRate() / 50;
    std::vector<short> output;
    std::vector<short> chunk(chunkSize);

    for (size_t offset = 0; offset + chunkSize <= samples.size(); offset += chunkSize)
    {
        memcpy(chunk.data(), &samples[offset], chunkSize * sizeof(short));

        int numOutput = 0;
        short* produced = step.execute(chunk.data(), chunkSize, &numOutput);
        if (produced != nullptr && numOutput > 0)
        {
            output.insert(output.end(), produced, produced + numOutput);
        }
        afterChunk();
    }

    return output;
}

std::vector<short> feed(IPipelineStep& step, std::vector<short> const& samples)
{
    return feed(step, samples, []() {});
}

// A tone with noise on top, so that what's encoded varies from frame to
// frame as speech does.
std::vector<short> noisyTone(int sampleRate, double seconds)
{
    auto samples = tone(sampleRate, seconds);
    unsigned int seed = 1;
    for (auto& sample : samples)
    {
        seed = seed * 1103515245u + 12345u;
        sample += (short)((int)((seed >> 16) % 4000) - 2000);
    }
    return samples;
}

// Returns the power in each of 256 evenly spaced frequency bands (up to
// half the sample rate), averaged over the signal in blocks of 512 samples.
std::vector<double> powerSpectrum(std::vector<short> const& samples)
{
    const size_t blockSize = 512;
    std::vector<double> power(blockSize / 2, 0.0);

    for (size_t start = 0; start + blockSize <= samples.size(); start += blockSize)
    {
        for (size_t bin = 1; bin < blockSize / 2; bin++)
        {
            double re = 0.0;
            double im = 0.0;
            for (size_t i = 0; i < blockSize; i++)
            {
                double angle = 2.0 * M_PI * bin * i / blockSize;
                re += samples[start + i] * std::cos(angle);
                im -= samples[start + i] * std::sin(angle);
            }
            power[bin] += re * re + im * im;
        }
    }

    return power;
}

// Returns how much of a signal's energy is above the given frequency (0 to 1).
double shareOfEnergyAbove(std::vector<short> const& samples, int sampleRate, double cutoffHz)
{
    auto power = powerSpectrum(samples);
    double above = 0.0;
    double total = 0.0;

    for (size_t bin = 0; bin < power.size(); bin++)
    {
        if (bin * sampleRate / (2.0 * power.size()) > cutoffHz) above += power[bin];
        total += power[bin];
    }

    return total > 0.0 ? above / total : 0.0;
}

std::string takeReliableText(FreeDVInterface& dv)
{
    const char* text = dv.getReliableText();
    std::string result = text;
    delete[] text;
    return result;
}

// What goes on the air for a few seconds of speech followed by the end of
// over, which is where the callsign (if any) is sent.
std::vector<short> transmitOver(Interface& iface, IPipelineStep& tx, double seconds, bool* endOfOverProduced = nullptr)
{
    auto onAir = feed(tx, noisyTone(RADE_SPEECH_RATE, seconds));

    // The end of over is handed back when the pipeline is next asked for
    // output without being given any input.
    iface.dv.restartTxVocoder();
    short nothing[1] = { 0 };
    int numEndOfOver = 0;
    short* endOfOver = tx.execute(nothing, 0, &numEndOfOver);
    if (endOfOver != nullptr && numEndOfOver > 0)
    {
        onAir.insert(onAir.end(), endOfOver, endOfOver + numEndOfOver);
    }
    if (endOfOverProduced != nullptr) *endOfOverProduced = numEndOfOver > 0;

    onAir.insert(onAir.end(), MODEM_SAMPLE_RATE, 0);
    return onAir;
}

// Decoding the callsign may finish slightly after the audio has.
std::string waitForReliableText(FreeDVInterface& dv)
{
    std::string callsign;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((callsign = takeReliableText(dv)) == "" && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return callsign;
}

bool notRunningUntilStarted()
{
    FreeDVInterface dv;

    bool result = true;
    result &= check("running before start", (int)dv.isRunning(), 0);

    dv.start(100, false);
    result &= check("running after start", (int)dv.isRunning(), 1);

    dv.stop();
    result &= check("running after stop", (int)dv.isRunning(), 0);

    // It can be started again afterwards.
    dv.start(100, true);
    result &= check("running after second start", (int)dv.isRunning(), 1);
    dv.stop();
    result &= check("running after second stop", (int)dv.isRunning(), 0);
    return result;
}

bool reportsNamesAndSizes()
{
    Interface iface;

    bool result = true;
    result &= check("running", (int)iface.dv.isRunning(), 1);
    result &= checkText("receive mode name", iface.dv.getCurrentModeStr(), "RADEV1");
    result &= checkText("transmit mode name", iface.dv.getCurrentTxModeStr(), "RADEV1");

    result &= check("transmit modem sample rate", iface.dv.getTxModemSampleRate(), MODEM_SAMPLE_RATE);
    result &= check("transmit speech sample rate", iface.dv.getTxSpeechSampleRate(), RADE_SPEECH_RATE);
    result &= check("receive modem sample rate", iface.dv.getRxModemSampleRate(), MODEM_SAMPLE_RATE);
    result &= check("receive speech sample rate", iface.dv.getRxSpeechSampleRate(), RADE_SPEECH_RATE);
    result &= checkTrue("no transmit speech samples per frame", iface.dv.getTxNumSpeechSamples() > 0);
    result &= check("receive speech samples per frame", iface.dv.getRxNumSpeechSamples(), iface.dv.getTxNumSpeechSamples());
    result &= checkTrue("no receive modem samples per frame", iface.dv.getRxNumModemSamples() > 0);

    // Room is needed for whichever is longer of a frame of voice and the end of over.
    int withoutPipeline = iface.dv.getTxNNomModemSamples();
    result &= checkTrue("no transmit modem samples per frame", withoutPipeline > 0);
    {
        auto tx = iface.transmitPipeline();
        result &= checkTrue("transmit modem samples per frame shrank once transmitting", iface.dv.getTxNNomModemSamples() >= withoutPipeline);
    }

    // RADE has no bit error statistics; fixed values keep the display sensible.
    result &= check("total bits", iface.dv.getTotalBits(), 1);
    result &= check("total bit errors", iface.dv.getTotalBitErrors(), 0);
    result &= check("variance", iface.dv.getVariance(), 0.0f);
    result &= check("sync before receiving anything", iface.dv.getSync(), 0);
    result &= check("SNR without sync", iface.dv.getSNREstimate(), 0.0f);
    return result;
}

bool receivesItsOwnTransmissionAndCallsign()
{
    Interface iface(true);
    iface.dv.setReliableText("K6AQ");

    auto tx = iface.transmitPipeline();
    auto rx = iface.receivePipeline();

    bool result = true;
    result &= checkText("callsign before receiving anything", takeReliableText(iface.dv), "");

    bool endOfOverProduced = false;
    auto onAir = transmitOver(iface, *tx, 4.0, &endOfOverProduced);
    result &= checkTrue("no end of over was produced", endOfOverProduced);

    bool everSynced = false;
    bool syncReportedToOwner = false;
    float bestSnr = -100.0f;
    float offsetWhileSynced = 1000.0f;
    auto heard = feed(*rx, onAir, [&]() {
        if (iface.dv.getSync() != 0)
        {
            everSynced = true;
            syncReportedToOwner |= g_rxState.load() != 0;
            bestSnr = std::max(bestSnr, iface.dv.getSNREstimate());
            offsetWhileSynced = iface.dv.getCurrentRxModemOffset();
        }
    });

    result &= checkTrue("never synced to its own signal", everSynced);
    result &= checkTrue("sync was never reported to pipeline's owner", syncReportedToOwner);
    result &= checkTrue("SNR of a clean signal was not above 10 dB", bestSnr > 10.0f);
    result &= checkTrue("frequency offset of a signal with none was 5 Hz or more", std::fabs(offsetWhileSynced) < 5.0f);
    result &= checkTrue("no speech was produced", !heard.empty());
    result &= check("sync once the over has ended", iface.dv.getSync(), 0);
    result &= check("sync reported to pipeline's owner once the over has ended", g_rxState.load(), 0);
    result &= check("SNR once the over has ended", iface.dv.getSNREstimate(), 0.0f);

    result &= checkText("callsign received", waitForReliableText(iface.dv), "K6AQ");

    // It's remembered until asked to be forgotten.
    result &= checkText("callsign when asked again", takeReliableText(iface.dv), "K6AQ");
    iface.dv.resetReliableText();
    result &= checkText("callsign after reset", takeReliableText(iface.dv), "");
    return result;
}

bool noCallsignIsReceivedUnlessReliableTextIsInUse()
{
    // The other station sends its callsign, but this one isn't looking for it.
    std::vector<short> onAir;
    {
        Interface sender(true);
        sender.dv.setReliableText("K6AQ");
        auto tx = sender.transmitPipeline();
        onAir = transmitOver(sender, *tx, 4.0);
    }

    Interface iface(false);
    iface.dv.setReliableText("W1AW"); // has nothing to apply to
    auto rx = iface.receivePipeline();

    bool everSynced = false;
    feed(*rx, onAir, [&]() { everSynced |= iface.dv.getSync() != 0; });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    bool result = true;
    result &= checkTrue("never synced to the other station's signal", everSynced);
    result &= checkText("callsign received", takeReliableText(iface.dv), "");
    return result;
}

bool staysUnsyncedWhenNothingIsReceived()
{
    Interface iface;
    auto rx = iface.receivePipeline();

    bool everSynced = false;
    feed(*rx, std::vector<short>(MODEM_SAMPLE_RATE * 2, 0), [&]() { everSynced |= iface.dv.getSync() != 0; });

    bool result = true;
    result &= checkTrue("synced to silence", !everSynced);
    result &= check("sync reported to pipeline's owner", g_rxState.load(), 0);
    result &= check("SNR", iface.dv.getSNREstimate(), 0.0f);
    return result;
}

bool receiveFrequencyOffsetMovesSignalBeforeDemodulating()
{
    const float OFFSET_HZ = 20.0f;

    std::vector<short> onAir;
    {
        Interface sender;
        auto tx = sender.transmitPipeline();
        onAir = feed(*tx, noisyTone(RADE_SPEECH_RATE, 4.0));
    }

    // The demodulator reports how far off frequency it finds the signal,
    // which shows whether it was moved before reaching it.
    auto offsetSeenByDemodulator = [&](float requestedOffset) {
        Interface iface;
        g_rxFreqOffset = requestedOffset;
        auto rx = iface.receivePipeline();

        float offsetWhileSynced = 1000.0f;
        feed(*rx, onAir, [&]() {
            if (iface.dv.getSync() != 0) offsetWhileSynced = iface.dv.getCurrentRxModemOffset();
        });
        return offsetWhileSynced;
    };

    float without = offsetSeenByDemodulator(0.0f);
    float with = offsetSeenByDemodulator(OFFSET_HZ);

    bool result = true;
    result &= checkTrue("did not sync without an offset", without != 1000.0f);
    result &= checkTrue("did not sync with an offset", with != 1000.0f);
    if (std::fabs(std::fabs(with - without) - OFFSET_HZ) > 5.0f)
    {
        std::cout << "[demodulator saw the signal move by " << (with - without) << " Hz, expected " << OFFSET_HZ << " either way] ";
        result = false;
    }
    return result;
}

bool bandwidthExpansionAddsHighFrequencies()
{
    constexpr int SPEAKER_SAMPLE_RATE = 48000;

    // RADE produces speech sampled at 16 kHz, which has nothing above 8 kHz
    // unless bandwidth expansion puts something there.
    auto shareAbove8kHz = [](bool expand) {
        Interface iface;
        g_bwExpandEnabled = expand;

        auto tx = iface.transmitPipeline();
        auto rx = iface.receivePipeline(SPEAKER_SAMPLE_RATE);

        auto heard = feed(*rx, feed(*tx, noisyTone(RADE_SPEECH_RATE, 4.0)));
        return heard.empty() ? -1.0 : shareOfEnergyAbove(heard, SPEAKER_SAMPLE_RATE, 9000.0);
    };

    double without = shareAbove8kHz(false);
    double with = shareAbove8kHz(true);

    bool result = true;
    result &= checkTrue("nothing was heard without expansion", without >= 0.0);
    result &= checkTrue("nothing was heard with expansion", with >= 0.0);
    if (without > 0.001)
    {
        std::cout << "[share of energy above 9 kHz without expansion was " << without << "] ";
        result = false;
    }
    if (with < without * 10.0 || with < 0.001)
    {
        std::cout << "[share of energy above 9 kHz was " << with << " with expansion and " << without << " without] ";
        result = false;
    }
    return result;
}

// FreeDV's shutdown paths let go of the interface while it's still running
// (the main window's on close, for one): the destructor has to do the
// stopping, and stopping an already stopped interface again has to be
// harmless too.
bool destroyingARunningInterfaceStopsIt()
{
    FreeDVInterface* dv = new FreeDVInterface();
    dv->start(100, false);
    if (!dv->isRunning())
    {
        std::cout << "[start() left the interface not running] ";
        delete dv;
        return false;
    }

    // Deleted while it's running: the destructor stops it.
    delete dv;

    // And an interface stopped the usual way stops once more on the way out
    // without anything going wrong.
    FreeDVInterface stopped;
    stopped.start(100, false);
    stopped.stop();
    return true;
}

// Starting with verbose on (what FreeDV does with its debug console option)
// brings the interface up with RADE's own commentary left in.
bool startingVerboseBringsTheInterfaceUp()
{
    FreeDVInterface dv;
    dv.start(100, false, true);
    bool running = dv.isRunning();
    dv.stop();
    if (!running)
    {
        std::cout << "[verbose start() did not bring the interface up] ";
        return false;
    }
    return true;
}

// Reliable text can be asked for at any time, including before there's an
// interface to carry it: there's nothing to change and nothing to fall over.
bool reliableTextAskedForBeforeStartingIsIgnored()
{
    FreeDVInterface dv;
    if (dv.isRunning())
    {
        std::cout << "[an interface that was never started reported as running] ";
        return false;
    }
    dv.setReliableText("TESTCALL");
    if (dv.isRunning())
    {
        std::cout << "[setReliableText() started the interface] ";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    executeTestCase("notRunningUntilStarted", notRunningUntilStarted);
    executeTestCase("reportsNamesAndSizes", reportsNamesAndSizes);
    executeTestCase("receivesItsOwnTransmissionAndCallsign", receivesItsOwnTransmissionAndCallsign);
    executeTestCase("noCallsignIsReceivedUnlessReliableTextIsInUse", noCallsignIsReceivedUnlessReliableTextIsInUse);
    executeTestCase("staysUnsyncedWhenNothingIsReceived", staysUnsyncedWhenNothingIsReceived);
    executeTestCase("receiveFrequencyOffsetMovesSignalBeforeDemodulating", receiveFrequencyOffsetMovesSignalBeforeDemodulating);
    executeTestCase("bandwidthExpansionAddsHighFrequencies", bandwidthExpansionAddsHighFrequencies);
    executeTestCase("destroyingARunningInterfaceStopsIt", destroyingARunningInterfaceStopsIt);
    executeTestCase("startingVerboseBringsTheInterfaceUp", startingVerboseBringsTheInterfaceUp);
    executeTestCase("reliableTextAskedForBeforeStartingIsIgnored", reliableTextAskedForBeforeStartingIsIgnored);

    return testResult();
}
