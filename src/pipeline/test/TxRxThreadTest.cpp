//=========================================================================
// Name:            TxRxThreadTest.cpp
// Purpose:         Unit tests for TxRxThread's processing loop and the UI
//                  hooks injected through TxRxThreadUiHooks.
//
// The real-time thread itself is only exercised for its start/stop
// lifecycle; the pipeline stages are driven synchronously through the
// runOnce_/txProcessing_/rxProcessing_/clearFifos_ seams (TxRxThreadTest
// is a friend of TxRxThread).
//=========================================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

// First, as it brings in wxWidgets, which on Windows has to include
// winsock2.h before anything else includes windows.h.
#include "UtilStubs.h"

#include <sndfile.h>

#include "../TxRxThread.h"
#include "pipeline/LinkStep.h"
#include "pipeline/ResampleStep.h"
#include "pipeline/test/SpeechSample.h"
#include "../../freedv_interface.h"
#include "../../test/UnitTestCommon.h"

// ---------------------------------------------------------------------------
// Globals normally defined by main.cpp. TxRxThread.cpp and the pipeline
// steps it pulls in read these; the test cases set them per case.
// UtilStubs.h already provides g_nSoundCards, g_RxFreqOffsetHz,
// g_TxFreqOffsetHz and the MainApp/MainFrame stubs.
// ---------------------------------------------------------------------------
wxMutex g_mutexProtectingCallbackData;

FreeDVInterface freedvInterface;

std::string utTxFeatureFile;
std::string utRxFeatureFile;
wxString utFreeDVMode;
std::mutex g_mutexProtectingPlayFiles;

paCallBackData* g_rxUserdata = nullptr;

std::atomic<bool>   g_half_duplex(true);
std::atomic<bool>   g_tx(false);
std::atomic<bool>   endingTx(false);
std::atomic<bool>   g_voice_keyer_tx(false);
std::atomic<bool>   g_agcEnabled(false);
std::atomic<bool>   g_noiseReductionEnabled(false);
std::atomic<bool>   g_monitorTxAudio(false);
std::atomic<bool>   g_monitorVoiceKeyerAudio(false);
std::atomic<bool>   g_totBeepActive(false);
std::atomic<bool>   g_eoo_enqueued(false);
std::atomic<bool>   g_recFileFromModulator(false);
std::atomic<bool>   g_recFileFromMic(false);
std::atomic<bool>   g_recVoiceKeyerFile(false);
std::atomic<bool>   g_playFileToMicIn(false);
std::atomic<bool>   g_loopPlayFileToMicIn(false);
std::atomic<bool>   g_playFileFromRadio(false);
std::atomic<bool>   g_loopPlayFileFromRadio(false);
std::atomic<bool>   g_bwExpandEnabled(false);

std::atomic<int>    g_analog(0);
std::atomic<int>    g_State(0);
std::atomic<int>    g_channel_noise(0);
std::atomic<int>    g_noiseSNR(0);
std::atomic<int>    g_sfFs(8000);
std::atomic<int>    g_sfTxFs(8000);
std::atomic<int>    g_outfifo1_empty(0);
std::atomic<int64_t> g_rxMutedUntilNs(0);
std::atomic<float>  g_txLevelScale(1.0f);
std::atomic<float>  g_monitorTxAudioVol(0.0f);
std::atomic<float>  g_monitorVoiceKeyerAudioVol(0.0f);
std::atomic<float>  g_tone_phase(0.0f);

int g_txLevel = 0;
std::atomic<int> g_dump_timing(0);
std::atomic<int> g_dump_fifo_state(0);
std::atomic<int> g_resyncs(0);

std::atomic<int>   g_SquelchActive(0);
std::atomic<float> g_SquelchLevel(0.0f);
float g_sig_pwr_av = 0.0f;

GenericFIFO<float> g_avmag(2000);
GenericFIFO<short> g_plotSpeechInFifoBeforeEQ(8000);
GenericFIFO<short> g_plotSpeechInFifoAfterAGC(8000);
GenericFIFO<short> g_plotDemodInFifo(8000);
GenericFIFO<short> g_plotSpeechOutFifo(8000);

std::atomic<SNDFILE*> g_sfPlayFile(nullptr);
std::atomic<SNDFILE*> g_sfPlayFileFromRadio(nullptr);
std::atomic<SNDFILE*> g_sfRecFileFromModulator(nullptr);
std::atomic<SNDFILE*> g_sfRecMicFile(nullptr);
std::atomic<SNDFILE*> g_sfRecFile(nullptr);
std::atomic<SNDFILE*> g_sfRecDecoderFile(nullptr);
std::atomic<bool> g_recFileFromRadio(false);
std::atomic<bool> g_recFileFromDecoder(false);
std::atomic<unsigned int> g_recFromRadioSamples(0);

namespace {

constexpr int TEST_SAMPLE_RATE = 8000;
constexpr int FIFO_CAPACITY = 16000;

// What the tone hooks given to the thread under test report.
std::atomic<float> g_testToneFreq(1000.0f);
std::atomic<float> g_testToneAmplitude(0.0f);
std::atomic<bool> g_testToneEnabled(false);

// Prints a bracketed mismatch on failure, like the other pipeline tests' helper.
template<typename T>
bool check(const char* description, T actual, T expected)
{
    if (actual == expected) return true;
    std::cout << "[" << description << " was " << actual << ", expected " << expected << "] ";
    return false;
}

void resetGlobals()
{
    g_nSoundCards = 2;
    g_half_duplex = true;
    g_tx = false;
    endingTx = false;
    g_voice_keyer_tx = false;
    g_agcEnabled = false;
    g_noiseReductionEnabled = false;
    g_monitorTxAudio = false;
    g_monitorVoiceKeyerAudio = false;
    g_monitorTxAudioVol = 0.0f;
    g_monitorVoiceKeyerAudioVol = 0.0f;
    g_totBeepActive = false;
    g_eoo_enqueued = false;
    g_outfifo1_empty = 0;
    g_analog = 0;
    g_State = 0;
    g_channel_noise = 0;
    g_noiseSNR = 0;
    g_rxMutedUntilNs = 0;
    g_SquelchActive = 0;
    g_SquelchLevel = 0.0f;
    g_txLevelScale = 1.0f;
    g_playFileToMicIn = false;
    g_loopPlayFileToMicIn = false;
    g_playFileFromRadio = false;
    g_loopPlayFileFromRadio = false;
    g_recFileFromMic = false;
    g_recVoiceKeyerFile = false;
    g_recFileFromModulator = false;
    g_recFileFromRadio = false;
    g_sfPlayFile = nullptr;
    g_recFileFromDecoder = false;
    g_sfPlayFileFromRadio = nullptr;
    g_sfRecFileFromModulator = nullptr;
    g_sfRecMicFile = nullptr;
    g_sfFs = TEST_SAMPLE_RATE;
    g_sfTxFs = TEST_SAMPLE_RATE;
    g_resyncs = 0;
    g_sig_pwr_av = 0.0f;
    g_sfRecFile = nullptr;
    g_recFromRadioSamples = 0;
    g_dump_fifo_state = 0;
    g_sfRecDecoderFile = nullptr;
    g_RxFreqOffsetHz = 0.0f;
    g_TxFreqOffsetHz = 0.0f;
    g_tone_phase = 0.0f;
    g_testToneFreq = 1000.0f;
    g_testToneAmplitude = 0.0f;
    g_testToneEnabled = false;
}

// Owns a fresh set of sound-card FIFOs for the duration of a test case.
class FifoGuard
{
public:
    FifoGuard()
    {
        g_rxUserdata = new paCallBackData();
        g_rxUserdata->infifo1 = new GenericFIFO<short>(FIFO_CAPACITY);
        g_rxUserdata->outfifo1 = new GenericFIFO<short>(FIFO_CAPACITY);
        g_rxUserdata->infifo2 = new GenericFIFO<short>(FIFO_CAPACITY);
        g_rxUserdata->outfifo2 = new GenericFIFO<short>(FIFO_CAPACITY);
    }

    ~FifoGuard()
    {
        delete g_rxUserdata->infifo1;
        delete g_rxUserdata->outfifo1;
        delete g_rxUserdata->infifo2;
        delete g_rxUserdata->outfifo2;
        delete g_rxUserdata;
        g_rxUserdata = nullptr;
    }

    FifoGuard(const FifoGuard&) = delete;
    FifoGuard& operator=(const FifoGuard&) = delete;
};

// IRealtimeHelper that just records what the thread asked it to do.
class FakeRealtimeHelper : public IRealtimeHelper
{
public:
    void setHelperRealTime() FREEDV_NONBLOCKING override { setRealTimeCalls++; }
    void startRealTimeWork() FREEDV_NONBLOCKING override { startCalls++; }
    void stopRealTimeWork(bool fastMode) FREEDV_NONBLOCKING override { stopCalls++; lastFastMode = fastMode; }
    void clearHelperRealTime() FREEDV_NONBLOCKING override { clearRealTimeCalls++; }
    bool mustStopWork() FREEDV_NONBLOCKING override
    {
        if (stopWork.load(std::memory_order_acquire)) return true;

        int allowed = framesAllowed.load(std::memory_order_acquire);
        if (allowed < 0) return false;
        if (allowed == 0) return true;
        framesAllowed.store(allowed - 1, std::memory_order_release);
        return false;
    }

    // What the thread is told when it asks whether it has run out of time.
    std::atomic<bool> stopWork{false};

    // If not negative, the number of frames the thread may still process
    // before being told that it has run out of time.
    std::atomic<int> framesAllowed{-1};

    // Atomic as the thread under test updates these while the test reads them.
    std::atomic<int> setRealTimeCalls{0};
    std::atomic<int> startCalls{0};
    std::atomic<int> stopCalls{0};
    std::atomic<int> clearRealTimeCalls{0};
    std::atomic<bool> lastFastMode{false};
};

// IRealtimeHelper for a thread that runs on a clock of its own: it takes a
// turn, waits out the rest of one, and always has time for the next. The
// wait follows what the app's real helpers do (MinimalRealtimeHelper,
// MacAudioDevice): a cycle whose processing already ran past its turn
// doesn't sleep at all, and a fastMode turn waits only half a turn --
// when the transmitter's FIFO is low the helper tries to refill at twice
// the rate instead of sticking to its usual cadence.
class ClockRealtimeHelper : public IRealtimeHelper
{
public:
    explicit ClockRealtimeHelper(std::chrono::milliseconds turn)
        : turn_(turn)
    {
        // empty
    }

    void setHelperRealTime() FREEDV_NONBLOCKING override {}
    void startRealTimeWork() FREEDV_NONBLOCKING override
    {
        turns.fetch_add(1, std::memory_order_relaxed);
        startTime_ = std::chrono::steady_clock::now();
    }
    // A clock's whole job is to block until the next turn, and sleep_for is
    // a nanosleep. Called from the annotated runOnce_ loop under RTSan, that
    // blocking is exactly what RealtimeSanitizer is there to catch, so it is
    // declared with the project's sanctioned disable/enable pair (see
    // RADEReceiveStep.cpp for the same pattern in the backend).
    void stopRealTimeWork(bool fastMode) override
    {
        auto period = fastMode ? turn_ / 2 : turn_;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime_);
        auto wait = period - elapsed - overshoot_;
        if (wait <= std::chrono::milliseconds(0))
        {
            overshoot_ = std::chrono::milliseconds(0);
            return; // this cycle already spent its turn's time
        }
        auto before = std::chrono::steady_clock::now();
        FREEDV_BEGIN_REALTIME_UNSAFE
        std::this_thread::sleep_for(wait);
        FREEDV_END_REALTIME_UNSAFE
        overshoot_ = std::max(
            std::chrono::milliseconds(0),
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - before) - wait);
    }
    void clearHelperRealTime() FREEDV_NONBLOCKING override {}
    bool mustStopWork() FREEDV_NONBLOCKING override { return false; }

    std::atomic<long> turns{0};

private:
    std::chrono::milliseconds turn_;
    std::chrono::steady_clock::time_point startTime_{};
    std::chrono::milliseconds overshoot_{0};
};

// Tone getters: realtime_fp only accepts raw function pointers, so these are
// captureless file-scope functions (the app's equivalents live in main.cpp).
float testToneFreq() FREEDV_NONBLOCKING { return g_testToneFreq.load(std::memory_order_acquire); }
float testToneAmplitude() FREEDV_NONBLOCKING { return g_testToneAmplitude.load(std::memory_order_acquire); }
bool testToneEnabled() FREEDV_NONBLOCKING { return g_testToneEnabled.load(std::memory_order_acquire); }

TxRxThreadUiHooks makeHooks(std::atomic<int>& playFileToMicInFinished, std::atomic<int>& recFileFromRadioFinished, std::atomic<int>& playFileFromRadioFinished)
{
    TxRxThreadUiHooks hooks;
    hooks.playFileToMicInFinished = [&playFileToMicInFinished]() { playFileToMicInFinished++; };
    hooks.recFileFromRadioFinished = [&recFileFromRadioFinished]() { recFileFromRadioFinished++; };
    hooks.playFileFromRadioFinished = [&playFileFromRadioFinished]() { playFileFromRadioFinished++; };
    hooks.toneFreqHz = testToneFreq;
    hooks.toneAmplitude = testToneAmplitude;
    hooks.toneEnabled = testToneEnabled;
    return hooks;
}

void seedSine(GenericFIFO<short>* fifo, int numSamples, double freqHz = 440.0)
{
    std::vector<short> samples(numSamples);
    for (int i = 0; i < numSamples; i++)
    {
        samples[i] = (short)(8000.0 * std::sin(2.0 * M_PI * freqHz * i / TEST_SAMPLE_RATE));
    }
    fifo->write(samples.data(), numSamples);
}

std::vector<short> drain(GenericFIFO<short>* fifo)
{
    std::vector<short> out(fifo->numUsed());
    if (!out.empty())
    {
        fifo->read(out.data(), out.size());
    }
    return out;
}

int countNonZero(std::vector<short> const& samples)
{
    int count = 0;
    for (auto sample : samples)
    {
        if (sample != 0) count++;
    }
    return count;
}

int peak(std::vector<short> const& samples)
{
    int result = 0;
    for (auto sample : samples)
    {
        result = std::max(result, std::abs((int)sample));
    }
    return result;
}

bool checkBetween(const char* description, int actual, int lowest, int highest)
{
    if (actual >= lowest && actual <= highest) return true;
    std::cout << "[" << description << " was " << actual << ", expected " << lowest << " to " << highest << "] ";
    return false;
}

std::vector<short> sine(int numSamples, double freqHz = 440.0, double amplitude = 4000.0)
{
    std::vector<short> samples(numSamples);
    for (int i = 0; i < numSamples; i++)
    {
        samples[i] = (short)(amplitude * std::sin(2.0 * M_PI * freqHz * i / TEST_SAMPLE_RATE));
    }
    return samples;
}

void seedSilence(GenericFIFO<short>* fifo, int numSamples)
{
    std::vector<short> samples(numSamples, 0);
    fifo->write(samples.data(), numSamples);
}

double rms(std::vector<short> const& samples)
{
    if (samples.empty()) return 0.0;

    double sum = 0.0;
    for (auto sample : samples)
    {
        sum += (double)sample * sample;
    }
    return std::sqrt(sum / samples.size());
}

// Returns the frequency of a tone from how often it crosses zero.
double toneFrequency(std::vector<short> const& samples, int sampleRate)
{
    int crossings = 0;
    for (size_t i = 1; i < samples.size(); i++)
    {
        if ((samples[i - 1] < 0) != (samples[i] < 0)) crossings++;
    }
    return samples.empty() ? 0.0 : crossings / 2.0 * sampleRate / samples.size();
}

std::vector<short> noise(int numSamples, int amplitude)
{
    std::vector<short> samples(numSamples);
    unsigned int seed = 1;
    for (auto& sample : samples)
    {
        seed = seed * 1103515245u + 12345u;
        sample = (short)((int)((seed >> 16) % (2 * amplitude)) - amplitude);
    }
    return samples;
}

// A sound file being recorded to, removed once the test case is done.
class RecordingFile
{
public:
    explicit RecordingFile(int sampleRate)
    {
        path_ = unitTestTempPath("TxRxThreadTest", "-recording.wav");

        SF_INFO info = {};
        info.samplerate = sampleRate;
        info.channels = 1;
        info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
        file_ = sf_open(path_.c_str(), SFM_WRITE, &info);
    }

    ~RecordingFile()
    {
        close();
        remove(path_.c_str());
    }

    SNDFILE* get() const { return file_; }

    // Returns how much has been recorded so far. RecordStep holds this lock
    // while writing, so the file isn't touched while it's being asked.
    int samplesSoFar()
    {
        g_mutexProtectingCallbackData.Lock();
        int result = (int)sf_seek(file_, 0, SEEK_CUR);
        g_mutexProtectingCallbackData.Unlock();
        return result;
    }

    // Recording happens on other threads; gives them time to catch up.
    void waitForSamples(int numSamples)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (samplesSoFar() < numSamples && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // Closes the file and returns what ended up in it.
    std::vector<short> finish()
    {
        close();

        SF_INFO info = {};
        std::vector<short> samples;
        SNDFILE* recorded = sf_open(path_.c_str(), SFM_READ, &info);
        if (recorded != nullptr)
        {
            samples.resize(info.frames);
            sf_read_short(recorded, samples.data(), samples.size());
            sf_close(recorded);
        }
        return samples;
    }

    RecordingFile(const RecordingFile&) = delete;
    RecordingFile& operator=(const RecordingFile&) = delete;

private:
    void close()
    {
        if (file_ != nullptr)
        {
            sf_close(file_);
            file_ = nullptr;
        }
    }

    std::string path_;
    SNDFILE* file_ = nullptr;
};

bool anyNonZero(std::vector<short> const& samples)
{
    for (auto sample : samples)
    {
        if (sample != 0) return true;
    }
    return false;
}

// A sound file on disk that's removed once the test case is done
// (same shape as PlaybackStepTest's helper).
class TestFile
{
public:
    TestFile(std::vector<short> const& samples, int sampleRate, int channels = 1)
    {
        path_ = unitTestTempPath("TxRxThreadTest", "-" + std::to_string(NextId_++) + ".wav");

        SF_INFO info = {};
        info.samplerate = sampleRate;
        info.channels = channels;
        info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

        SNDFILE* file = sf_open(path_.c_str(), SFM_WRITE, &info);
        if (file != nullptr)
        {
            sf_write_short(file, samples.data(), samples.size());
            sf_close(file);
        }

        info = {};
        file_ = sf_open(path_.c_str(), SFM_READ, &info);
    }

    ~TestFile()
    {
        if (file_ != nullptr)
        {
            sf_close(file_);
            file_ = nullptr;
        }
        remove(path_.c_str());
    }

    SNDFILE* get() const { return file_; }

    TestFile(const TestFile&) = delete;
    TestFile& operator=(const TestFile&) = delete;

private:
    std::string path_;
    SNDFILE* file_ = nullptr;
    static int NextId_;
};

int TestFile::NextId_ = 0;

// Sound card FIFOs sized as the application sizes them: plenty of room for
// what comes in, and 640 ms (FreeDV's default) of what goes out.
class SoundCardFifos
{
public:
    explicit SoundCardFifos(int sampleRate)
    {
        g_rxUserdata = new paCallBackData();
        g_rxUserdata->infifo1 = new GenericFIFO<short>(5 * sampleRate);
        g_rxUserdata->outfifo1 = new GenericFIFO<short>(640 * sampleRate / 1000);
        g_rxUserdata->infifo2 = new GenericFIFO<short>(5 * sampleRate);
        g_rxUserdata->outfifo2 = new GenericFIFO<short>(640 * sampleRate / 1000);
    }

    ~SoundCardFifos()
    {
        delete g_rxUserdata->infifo1;
        delete g_rxUserdata->outfifo1;
        delete g_rxUserdata->infifo2;
        delete g_rxUserdata->outfifo2;
        delete g_rxUserdata;
        g_rxUserdata = nullptr;
    }

    SoundCardFifos(const SoundCardFifos&) = delete;
    SoundCardFifos& operator=(const SoundCardFifos&) = delete;
};

// What the sound cards did that the thread between them couldn't help.
struct SoundCardCounts
{
    long inputOverflows = 0;        // blocks from a sound card with nowhere to go
    long outputUnderruns = 0;       // blocks a sound card wanted and didn't quite get
    long outputShortfallSamples = 0;// samples it wanted that there weren't
};

// One turn of a pair of sound cards running off the same clock, as the
// application's callbacks do it: so many samples arrive from one, the thread
// gets to run, and the other takes away whatever is there, silence for the
// rest of its block. The radio's card is served the way
// MainFrame::OnTxOutAudioData_() serves it, whatever the thread has made:
// the thread primes itself before it makes the first samples of an over,
// but the card asking for them is fed what is ready and no more.
template<typename ProcessFn>
std::vector<short> soundCardsTurn(
    GenericFIFO<short>* in, GenericFIFO<short>* out, const short* samples, int numSamples,
    ProcessFn process, SoundCardCounts& counts, bool outputTaken = true)
{
    if (numSamples > 0 && in->write(const_cast<short*>(samples), numSamples) != 0)
    {
        counts.inputOverflows++;
    }

    process();

    std::vector<short> taken(outputTaken ? numSamples : 0, 0);
    if (outputTaken && numSamples > 0)
    {
        int available = std::min(out->numUsed(), numSamples);
        if (available < numSamples)
        {
            counts.outputUnderruns++;
            counts.outputShortfallSamples += numSamples - available;
        }
        if (available > 0)
        {
            out->read(taken.data(), available);
        }
    }
    return taken;
}

// Real speech at the given sample rate, or nothing if the recording that the
// build fetches along with RADE can't be found.
std::vector<short> speechAt(int sampleRate, double seconds)
{
    auto speech = loadSpeech16k(seconds);
    if (speech.empty() || sampleRate == 16000)
    {
        return speech;
    }

    ResampleStep resampler(16000, sampleRate);
    std::vector<short> result;
    for (size_t offset = 0; offset < speech.size(); offset += 160)
    {
        int numOut = 0;
        auto out = resampler.execute(&speech[offset], (int)std::min<size_t>(160, speech.size() - offset), &numOut);
        if (out != nullptr && numOut > 0)
        {
            result.insert(result.end(), out, out + numOut);
        }
    }
    return result;
}

// The vocoder features that the RADE steps write when asked to (as they are
// for the rade_loss test): 36 numbers for every 10 ms of speech, of which the
// first 20 are what's sent.
constexpr int FEATURES_PER_FRAME = 36;

std::vector<float> loadFeatures(std::string const& path)
{
    std::vector<float> features;
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp != nullptr)
    {
        float frame[FEATURES_PER_FRAME];
        while (fread(frame, sizeof(float), FEATURES_PER_FRAME, fp) == FEATURES_PER_FRAME)
        {
            features.insert(features.end(), frame, frame + FEATURES_PER_FRAME);
        }
        fclose(fp);
    }
    return features;
}

// How far one frame of received features is from what was sent: RADE's own
// measure (distortion_loss() in radae_base.py), which the rade_loss test
// applies with loss.py.
double frameLoss(const float* sent, const float* received)
{
    double cepstrum = 0.0;
    for (int i = 0; i < 18; i++)
    {
        double error = received[i] - sent[i];
        cepstrum += error * error;
    }
    double pitchError = 2.0 * (received[18] - sent[18]);
    double correlationError = received[19] - sent[19];
    double pitchWeight = std::max(0.0, sent[19] + 0.5);
    pitchWeight *= pitchWeight;
    return cepstrum / 18.0 + 3.0 * (10.0 / 18.0) * std::fabs(pitchError) * pitchWeight +
        (1.0 / 18.0) * correlationError * correlationError;
}

// The loss between everything sent and everything received, lined up
// wherever within a second either way makes it smallest, as loss.py does:
// the receiver doesn't start at the first frame. A negative result means
// there was nothing to compare.
double featureLoss(std::vector<float> const& sent, std::vector<float> const& received, int* startFrame = nullptr)
{
    constexpr int PAD_FRAMES = 100;
    int sentFrames = (int)(sent.size() / FEATURES_PER_FRAME) + 2 * PAD_FRAMES;
    int receivedFrames = (int)(received.size() / FEATURES_PER_FRAME);
    if (receivedFrames == 0 || receivedFrames >= sentFrames)
    {
        return -1.0;
    }

    std::vector<float> padded((size_t)sentFrames * FEATURES_PER_FRAME, 0.0f);
    std::copy(sent.begin(), sent.end(), padded.begin() + PAD_FRAMES * FEATURES_PER_FRAME);

    double best = -1.0;
    for (int start = 0; start <= sentFrames - receivedFrames; start++)
    {
        double total = 0.0;
        for (int frame = 0; frame < receivedFrames; frame++)
        {
            total += frameLoss(&padded[(size_t)(start + frame) * FEATURES_PER_FRAME],
                               &received[(size_t)frame * FEATURES_PER_FRAME]);
        }
        double loss = total / receivedFrames;
        if (best < 0.0 || loss < best)
        {
            best = loss;
            if (startFrame != nullptr) *startFrame = start - PAD_FRAMES;
        }
    }
    return best;
}

// Hands out how many samples a sound card delivers or asks for next: always
// the same, or (given a seed) anything from a handful to a tenth of a
// second, as a busy machine's callbacks arrive.
class BlockSizes
{
public:
    BlockSizes(int sampleRate, unsigned int seed)
        : fixed_(sampleRate / 50)
        , most_(sampleRate / 10)
        , random_(seed != 0)
        , generator_(seed)
    {
        // empty
    }

    // (Worked out from the generator's numbers here rather than by a
    // standard distribution, which gives different sizes from one C++
    // library to the next: these are the same everywhere.)
    int next() { return random_ ? (int)(16 + generator_() % (unsigned int)(most_ - 15)) : fixed_; }

private:
    int fixed_;
    int most_;
    bool random_;
    std::mt19937 generator_;
};

} // namespace

// Friend of TxRxThread: drives the private processing seams directly.
class TxRxThreadTest
{
public:
    static bool txProcessingSkippedWhenHalfDuplexPttOff()
    {
        resetGlobals(); // half duplex, PTT off
        FifoGuard fifos;
        seedSine(g_rxUserdata->infifo2, 1600);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));

        FakeRealtimeHelper helper;
        thread.txProcessing_(&helper);

        bool result = true;
        result &= check("outfifo1 used", g_rxUserdata->outfifo1->numUsed(), 0);
        result &= check("infifo2 left untouched", g_rxUserdata->infifo2->numUsed(), 1600);
        result &= check("deferReset_ set for next TX entry", (int)thread.deferReset_, 1);
        return result;
    }

    static bool txProcessingGeneratesModemOutputWhenPttOn()
    {
        resetGlobals();
        g_tx = true;
        FifoGuard fifos;
        seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE); // 1s of speech

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        FakeRealtimeHelper helper;
        thread.txProcessing_(&helper);

        auto modemOut = drain(g_rxUserdata->outfifo1);

        bool result = true;
        result &= check("infifo2 drained", g_rxUserdata->infifo2->numUsed(), 0);
        if (modemOut.size() < 320)
        {
            std::cout << "[outfifo1 got " << modemOut.size() << " samples, expected at least 320] ";
            result = false;
        }
        result &= !anyNonZero(modemOut) ? (std::cout << "[outfifo1 output was all zeros]" << std::endl, false) : true;
        return result;
    }

    static bool enteringTxAfterRxResetsPipelineAndSkipsOneCycle()
    {
        resetGlobals();
        g_tx = true;
        FifoGuard fifos;
        seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE);
        seedSine(g_rxUserdata->outfifo1, 1000); // stale RX-side data in the TX output FIFO

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();
        thread.deferReset_ = true;

        FakeRealtimeHelper helper;

        // First entry after RX: reset everything and produce nothing.
        thread.txProcessing_(&helper);
        bool result = true;
        result &= check("outfifo1 cleared on entry", g_rxUserdata->outfifo1->numUsed(), 0);
        result &= check("infifo2 cleared on entry", g_rxUserdata->infifo2->numUsed(), 0);
        result &= check("deferReset_ consumed", (int)thread.deferReset_, 0);

        seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE);
        // Second entry: normal TX processing resumes.
        thread.txProcessing_(&helper);
        result &= g_rxUserdata->outfifo1->numUsed() >= 320 ? true
            : (std::cout << "[outfifo1 got " << g_rxUserdata->outfifo1->numUsed() << " samples after re-entry, expected >= 320]" << std::endl, false);
        return result;
    }

    // Transmits the given number of seconds of speech-like audio and returns
    // what was sent to the radio.
    static std::vector<short> transmittedSignal(int seconds)
    {
        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        // A tone with noise on top, so that it varies from frame to frame as speech does.
        auto mic = sine(TEST_SAMPLE_RATE, 1000.0, 3000.0);
        auto hiss = noise(TEST_SAMPLE_RATE, 2000);
        for (size_t i = 0; i < mic.size(); i++)
        {
            mic[i] += hiss[i];
        }

        // Transmitting needs a sound card of its own.
        int numSoundCards = g_nSoundCards;
        g_nSoundCards = 2;
        bool wasTransmitting = g_tx.exchange(true);
        FakeRealtimeHelper helper;
        std::vector<short> signal;
        for (int second = 0; second < seconds; second++)
        {
            g_rxUserdata->infifo2->write(mic.data(), mic.size());
            thread.txProcessing_(&helper);

            auto sent = drain(g_rxUserdata->outfifo1);
            signal.insert(signal.end(), sent.begin(), sent.end());
        }
        g_tx = wasTransmitting;
        g_nSoundCards = numSoundCards;
        return signal;
    }

    // Has a receive thread demodulate four seconds of signal, enough for it
    // to find the signal and start decoding speech, and returns what reached
    // each of the two places the speech can be sent to.
    static void receiveSignal(std::vector<short>& toOutfifo1, std::vector<short>& toOutfifo2)
    {
        auto signal = transmittedSignal(4);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        // initializePipeline_ clears the RX FIFOs, so seed after it. The
        // FIFOs only have room for two seconds, so hand it over a second
        // at a time.
        FakeRealtimeHelper helper;
        for (size_t offset = 0; offset < signal.size(); offset += TEST_SAMPLE_RATE)
        {
            g_rxUserdata->infifo1->write(&signal[offset], std::min((size_t)TEST_SAMPLE_RATE, signal.size() - offset));
            thread.rxProcessing_(&helper);

            auto out1 = drain(g_rxUserdata->outfifo1);
            auto out2 = drain(g_rxUserdata->outfifo2);
            toOutfifo1.insert(toOutfifo1.end(), out1.begin(), out1.end());
            toOutfifo2.insert(toOutfifo2.end(), out2.begin(), out2.end());
        }
    }

    static bool rxProcessingDemodulatesToOutfifo1ForSingleSoundCard()
    {
        resetGlobals();
        g_nSoundCards = 1;
        FifoGuard fifos;

        std::vector<short> toOutfifo1;
        std::vector<short> toOutfifo2;
        receiveSignal(toOutfifo1, toOutfifo2);

        bool result = true;
        result &= check("decoder in sync", g_State.load(), 1);
        if (toOutfifo1.empty())
        {
            std::cout << "[outfifo1 empty after demodulating 4s of signal] ";
            result = false;
        }
        result &= !anyNonZero(toOutfifo1) ? (std::cout << "[outfifo1 output was all zeros]" << std::endl, false) : true;
        result &= check("outfifo2 unused (single sound card)", (int)toOutfifo2.size(), 0);
        return result;
    }

    static bool rxProcessingWritesToOutfifo2ForDualSoundCards()
    {
        resetGlobals(); // g_nSoundCards = 2
        FifoGuard fifos;

        std::vector<short> toOutfifo1;
        std::vector<short> toOutfifo2;
        receiveSignal(toOutfifo1, toOutfifo2);

        bool result = true;
        result &= check("decoder in sync", g_State.load(), 1);
        result &= check("outfifo1 unused (dual sound cards)", (int)toOutfifo1.size(), 0);
        result &= !anyNonZero(toOutfifo2) ? (std::cout << "[nothing but silence in outfifo2 after demodulating 4s of signal]" << std::endl, false) : true;
        return result;
    }

    static bool runOnceRequestsFastModeWhenOutputFifoLow()
    {
        resetGlobals(); // PTT off: txProcessing_ produces nothing, so the FIFO state below is what runOnce_ sees
        FifoGuard fifos;

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));

        FakeRealtimeHelper helper;

        // Empty output FIFO -> the thread should ask to be woken sooner.
        thread.runOnce_(&helper);
        bool result = true;
        result &= check("fastMode with empty outfifo1", (int)helper.lastFastMode.load(), 1);

        // More than half full -> normal wake interval.
        std::vector<short> junk(FIFO_CAPACITY * 3 / 4, 1);
        g_rxUserdata->outfifo1->write(junk.data(), junk.size());
        thread.runOnce_(&helper);
        result &= check("no fastMode with outfifo1 mostly full", (int)helper.lastFastMode.load(), 0);

        result &= check("startRealTimeWork calls", helper.startCalls.load(), 2);
        result &= check("stopRealTimeWork calls", helper.stopCalls.load(), 2);
        return result;
    }

    static bool clearFifosResetsTxFifosAndSelectedRxFifo()
    {
        resetGlobals(); // g_nSoundCards = 2
        FifoGuard fifos;

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread txThread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        TxRxThread rxThread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));

        bool result = true;

        seedSine(g_rxUserdata->outfifo1, 1000);
        seedSine(g_rxUserdata->infifo2, 1000);
        txThread.clearFifos_();
        result &= check("TX outfifo1 cleared", g_rxUserdata->outfifo1->numUsed(), 0);
        result &= check("TX infifo2 cleared", g_rxUserdata->infifo2->numUsed(), 0);

        seedSine(g_rxUserdata->infifo1, 1000);
        seedSine(g_rxUserdata->outfifo1, 1000);
        seedSine(g_rxUserdata->outfifo2, 1000);
        rxThread.clearFifos_();
        result &= check("RX infifo1 cleared", g_rxUserdata->infifo1->numUsed(), 0);
        result &= check("RX outfifo2 cleared", g_rxUserdata->outfifo2->numUsed(), 0);
        // With two sound cards the RX output FIFO is outfifo2; outfifo1 is
        // the TX-side FIFO and must not be touched by RX cleanup.
        result &= check("TX-side outfifo1 left alone", g_rxUserdata->outfifo1->numUsed(), 1000);
        return result;
    }

    static bool playFileToMicInFinishedHookFiresWhenFileEnds()
    {
        resetGlobals();
        g_tx = true; // playing to mic-in only flows while transmitting
        g_playFileToMicIn = true;
        g_loopPlayFileToMicIn = false;
        FifoGuard fifos;

        std::vector<short> fileSamples(TEST_SAMPLE_RATE / 10); // 0.1s of tone
        for (size_t i = 0; i < fileSamples.size(); i++)
        {
            fileSamples[i] = (short)(4000.0 * std::sin(2.0 * M_PI * 440.0 * i / TEST_SAMPLE_RATE));
        }
        TestFile file(fileSamples, TEST_SAMPLE_RATE);
        if (file.get() == nullptr)
        {
            std::cout << "[could not open test sound file] ";
            return false;
        }
        g_sfPlayFile.store(file.get(), std::memory_order_release);

        seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE); // 1s of mic audio, longer than the file

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        FakeRealtimeHelper helper;
        thread.txProcessing_(&helper);

        // PlaybackStep detects EOF on its file I/O thread, which only runs
        // when execute() signals it and drains its output FIFO. Keep pumping
        // the pipeline so the buffered file samples get consumed and the
        // completion hook can fire.
        short zeros[160] = {};
        int numOut = 0;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (micInDone.load(std::memory_order_acquire) == 0 &&
               std::chrono::steady_clock::now() < deadline)
        {
            thread.pipeline_->execute(zeros, 160, &numOut);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }


        g_sfPlayFile.store(nullptr, std::memory_order_release);

        bool result = true;
        if (micInDone.load(std::memory_order_acquire) < 1)
        {
            std::cout << "[playFileToMicInFinished hook fired " << micInDone.load() << " times, expected at least 1] ";
            result = false;
        }
        return result;
    }

    // Runs one second of microphone audio through a transmit thread and
    // returns the loudest sample the receive thread then sends to the speaker
    // while the radio itself is silent.
    static int peakOfMicAudioHeardThroughSpeaker()
    {
        FifoGuard fifos;
        auto link = std::make_shared<LinkStep>(TEST_SAMPLE_RATE);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread txThread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, link, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        TxRxThread rxThread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, link, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        txThread.initializePipeline_();
        rxThread.initializePipeline_();

        // Both initializations clear the FIFOs, so seed after them.
        seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE);
        seedSilence(g_rxUserdata->infifo1, TEST_SAMPLE_RATE);

        FakeRealtimeHelper helper;
        txThread.txProcessing_(&helper);
        rxThread.rxProcessing_(&helper);

        return peak(drain(g_rxUserdata->outfifo2));
    }

    static bool monitoringTransmitAudioPlaysMicThroughSpeaker()
    {
        resetGlobals();
        g_tx = true;
        g_monitorTxAudio = true;

        // seedSine() produces a tone with a peak of 8000, which mixing with
        // the (silent) time-out beep brings down to about 5600.
        return checkBetween("loudest speaker sample", peakOfMicAudioHeardThroughSpeaker(), 5400, 5800);
    }

    static bool monitoredTransmitAudioFollowsItsVolumeSetting()
    {
        resetGlobals();
        g_tx = true;
        g_monitorTxAudio = true;
        g_monitorTxAudioVol = -20.0f; // a tenth of the amplitude
        g_monitorVoiceKeyerAudioVol = 0.0f;

        return checkBetween("loudest speaker sample", peakOfMicAudioHeardThroughSpeaker(), 540, 590);
    }

    static bool monitoredVoiceKeyerAudioFollowsItsOwnVolumeSetting()
    {
        resetGlobals();
        g_voice_keyer_tx = true;
        g_monitorVoiceKeyerAudio = true;
        g_monitorVoiceKeyerAudioVol = -20.0f;
        g_monitorTxAudioVol = 0.0f;

        return checkBetween("loudest speaker sample", peakOfMicAudioHeardThroughSpeaker(), 540, 590);
    }

    static bool micAudioStaysOutOfSpeakerWhenNotMonitoring()
    {
        resetGlobals();
        g_tx = true;
        g_half_duplex = false; // so that the receiver isn't simply muted
        g_analog = 1; // so that the radio's silence reaches the speaker as is

        return check("loudest speaker sample", peakOfMicAudioHeardThroughSpeaker(), 0);
    }

    static bool speakerIsSilentWhileRecordingVoiceKeyerFile()
    {
        resetGlobals();
        g_analog = 1; // would otherwise pass the radio's audio straight through
        g_recVoiceKeyerFile = true;
        FifoGuard fifos;
        auto link = std::make_shared<LinkStep>(TEST_SAMPLE_RATE);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, link, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();
        seedSine(g_rxUserdata->infifo1, TEST_SAMPLE_RATE);

        FakeRealtimeHelper helper;
        thread.rxProcessing_(&helper);

        auto speakerOut = drain(g_rxUserdata->outfifo2);

        bool result = true;
        result &= check("speaker samples", (int)speakerOut.size(), TEST_SAMPLE_RATE);
        result &= check("loudest speaker sample", peak(speakerOut), 0);
        return result;
    }

    static bool clearFifosAlsoClearsMonitoredMicAudio()
    {
        resetGlobals();
        FifoGuard fifos;
        auto link = std::make_shared<LinkStep>(TEST_SAMPLE_RATE);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, link, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));

        seedSine(&link->getFifo(), 1000);
        thread.clearFifos_();

        return check("monitored mic samples waiting", link->getFifo().numUsed(), 0);
    }

    // Keeps a thread's pipeline moving, since PlaybackStep only reads more of
    // its file as its output is consumed, until enough has come out of it or
    // the condition is met.
    template<typename DoneFn>
    static std::vector<short> pumpUntil(TxRxThread& thread, DoneFn done)
    {
        std::vector<short> collected;
        short zeros[160] = {};
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!done(collected) && std::chrono::steady_clock::now() < deadline)
        {
            int numOut = 0;
            auto out = thread.pipeline_->execute(zeros, 160, &numOut);
            if (out != nullptr && numOut > 0)
            {
                collected.insert(collected.end(), out, out + numOut);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return collected;
    }

    static bool playFileToMicInStartsOverWhenLooping()
    {
        resetGlobals();
        g_tx = true;
        g_analog = 1; // so that what's played comes out of the pipeline as is
        g_playFileToMicIn = true;
        g_loopPlayFileToMicIn = true;
        g_dump_fifo_state = 1; // only adds logging
        FifoGuard fifos;

        auto fileSamples = sine(TEST_SAMPLE_RATE / 10); // 0.1s
        TestFile file(fileSamples, TEST_SAMPLE_RATE);
        if (file.get() == nullptr)
        {
            std::cout << "[could not open test sound file] ";
            return false;
        }
        g_sfPlayFile.store(file.get(), std::memory_order_release);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();
        seedSilence(g_rxUserdata->infifo2, 1600);

        FakeRealtimeHelper helper;
        thread.txProcessing_(&helper);

        // More than twice the file's worth of sound can only come from playing it again.
        int wanted = (int)fileSamples.size() * 5 / 2;
        int heard = countNonZero(drain(g_rxUserdata->outfifo1));
        auto rest = pumpUntil(thread, [&](std::vector<short> const& out) { return heard + countNonZero(out) >= wanted; });
        heard += countNonZero(rest);

        g_playFileToMicIn = false;
        g_sfPlayFile.store(nullptr, std::memory_order_release);

        bool result = true;
        if (heard < wanted)
        {
            std::cout << "[only " << heard << " samples of sound came out, expected at least " << wanted << "] ";
            result = false;
        }
        result &= check("times playback was reported finished", micInDone.load(), 0);
        return result;
    }

    static bool playFileFromRadioFinishedHookFiresWhenFileEnds()
    {
        resetGlobals();
        g_analog = 1;
        g_playFileFromRadio = true;
        FifoGuard fifos;

        auto fileSamples = sine(TEST_SAMPLE_RATE / 10);
        TestFile file(fileSamples, TEST_SAMPLE_RATE);
        if (file.get() == nullptr)
        {
            std::cout << "[could not open test sound file] ";
            return false;
        }
        g_sfPlayFileFromRadio.store(file.get(), std::memory_order_release);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();
        seedSilence(g_rxUserdata->infifo1, 1600);

        FakeRealtimeHelper helper;
        thread.rxProcessing_(&helper);

        int heard = countNonZero(drain(g_rxUserdata->outfifo2));
        auto rest = pumpUntil(thread, [&](std::vector<short> const&) { return playRadioDone.load() > 0; });
        heard += countNonZero(rest);

        g_playFileFromRadio = false;
        g_sfPlayFileFromRadio.store(nullptr, std::memory_order_release);

        bool result = true;
        result &= check("times playback was reported finished", playRadioDone.load() > 0 ? 1 : 0, 1);
        result &= check("times mic playback was reported finished", micInDone.load(), 0);

        // The file replaces what the radio sent (silence here), so sound
        // can only have come from the file.
        result &= checkBetween("samples of sound heard", heard, (int)fileSamples.size() * 9 / 10, (int)fileSamples.size());
        return result;
    }

    static bool playFileFromRadioStartsOverWhenLooping()
    {
        resetGlobals();
        g_analog = 1;
        g_playFileFromRadio = true;
        g_loopPlayFileFromRadio = true;
        FifoGuard fifos;

        auto fileSamples = sine(TEST_SAMPLE_RATE / 10);
        TestFile file(fileSamples, TEST_SAMPLE_RATE);
        if (file.get() == nullptr)
        {
            std::cout << "[could not open test sound file] ";
            return false;
        }
        g_sfPlayFileFromRadio.store(file.get(), std::memory_order_release);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();
        seedSilence(g_rxUserdata->infifo1, 1600);

        FakeRealtimeHelper helper;
        thread.rxProcessing_(&helper);

        int wanted = (int)fileSamples.size() * 5 / 2;
        int heard = countNonZero(drain(g_rxUserdata->outfifo2));
        auto rest = pumpUntil(thread, [&](std::vector<short> const& out) { return heard + countNonZero(out) >= wanted; });
        heard += countNonZero(rest);

        g_playFileFromRadio = false;
        g_sfPlayFileFromRadio.store(nullptr, std::memory_order_release);

        bool result = true;
        if (heard < wanted)
        {
            std::cout << "[only " << heard << " samples of sound came out, expected at least " << wanted << "] ";
            result = false;
        }
        result &= check("times playback was reported finished", playRadioDone.load(), 0);
        return result;
    }

    static bool recordingFromRadioContinuesUntilStopped()
    {
        // Recording is done at RECORD_FILE_SAMPLE_RATE, so receive at that
        // rate to have what arrives from the radio end up in the file as is.
        constexpr int RADIO_SAMPLE_RATE = RECORD_FILE_SAMPLE_RATE;

        // What's received reaches the file by way of a buffer with room for a
        // quarter of a second, anything beyond which is discarded, and up to
        // a tenth of a second is held back in it until more arrives. Audio
        // arrives here far faster than it would from a radio, so it has to
        // be handed over a little at a time, each round only once there's
        // little more than that tenth of a second still waiting.
        constexpr int SAMPLES_PER_ROUND = RADIO_SAMPLE_RATE / 10; // 0.1s
        constexpr int ROUNDS = 6;
        constexpr int MAX_SAMPLES_WAITING = RADIO_SAMPLE_RATE * 13 / 100;
        constexpr unsigned int UNTIL_STOPPED = UINT32_MAX; // what the application asks for

        resetGlobals();
        g_analog = 1;
        FifoGuard fifos;

        std::string path = unitTestTempPath("TxRxThreadTest", "-rec.wav");
        SF_INFO info = {};
        info.samplerate = RADIO_SAMPLE_RATE;
        info.channels = 1;
        info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
        g_sfRecFile = sf_open(path.c_str(), SFM_WRITE, &info);
        if (g_sfRecFile == nullptr)
        {
            std::cout << "[could not create file to record to] ";
            return false;
        }
        g_recFromRadioSamples = UNTIL_STOPPED;
        g_recFileFromRadio = true;

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        {
            TxRxThread thread(false, RADIO_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
            thread.initializePipeline_();

            FakeRealtimeHelper helper;
            for (int round = 0; round < ROUNDS; round++)
            {
                seedSine(g_rxUserdata->infifo1, SAMPLES_PER_ROUND);
                thread.rxProcessing_(&helper);
                drain(g_rxUserdata->outfifo2);

                // Recording happens on other threads; give them time to catch up.
                int expectedSoFar = SAMPLES_PER_ROUND * (round + 1) - MAX_SAMPLES_WAITING;
                auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while ((int)(UNTIL_STOPPED - g_recFromRadioSamples.load()) < expectedSoFar &&
                       std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        }

        g_recFileFromRadio = false;
        sf_close(g_sfRecFile);
        g_sfRecFile = nullptr;

        info = {};
        SNDFILE* recorded = sf_open(path.c_str(), SFM_READ, &info);
        int recordedSamples = recorded != nullptr ? (int)info.frames : -1;
        if (recorded != nullptr) sf_close(recorded);
        remove(path.c_str());

        bool result = true;

        // The last tenth of a second or so may still have been waiting to be recorded.
        result &= checkBetween("samples in recorded file", recordedSamples, SAMPLES_PER_ROUND * ROUNDS - MAX_SAMPLES_WAITING, SAMPLES_PER_ROUND * ROUNDS);
        result &= check("samples counted as recorded", (int)(UNTIL_STOPPED - g_recFromRadioSamples.load()), recordedSamples);
        result &= check("times recording was reported finished", recRadioDone.load(), 0);
        return result;
    }

    static bool radioRecordingStopsWhenItsBudgetIsUsedUp()
    {
        // As in recordingFromRadioContinuesUntilStopped.
        constexpr int RADIO_SAMPLE_RATE = RECORD_FILE_SAMPLE_RATE;
        constexpr int SAMPLES_PER_ROUND = RADIO_SAMPLE_RATE / 10; // 0.1s
        constexpr int ROUNDS = 7;
        constexpr int MAX_SAMPLES_WAITING = RADIO_SAMPLE_RATE * 13 / 100;

        resetGlobals();
        g_analog = 1;
        FifoGuard fifos;
        RecordingFile recordFile(RECORD_FILE_SAMPLE_RATE);
        g_sfRecFile = recordFile.get();

        // Leave the budget running out partway through a round, so that it
        // ends in the middle of what's written rather than right after it.
        const unsigned int startBudget = SAMPLES_PER_ROUND * (ROUNDS - 1) - SAMPLES_PER_ROUND / 2 - 1;
        g_recFromRadioSamples = startBudget;
        g_recFileFromRadio = true;

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        {
            TxRxThread thread(false, RADIO_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
            thread.initializePipeline_();

            FakeRealtimeHelper helper;
            for (int round = 0; round < ROUNDS; round++)
            {
                seedSine(g_rxUserdata->infifo1, SAMPLES_PER_ROUND);
                thread.rxProcessing_(&helper);
                drain(g_rxUserdata->outfifo2);

                // Recording happens on other threads; give them time to catch up.
                unsigned int expectedSoFar = std::min<unsigned int>(startBudget, std::max(0, SAMPLES_PER_ROUND * (round + 1) - MAX_SAMPLES_WAITING));
                auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (recRadioDone.load() == 0 &&
                       startBudget - g_recFromRadioSamples.load() < expectedSoFar &&
                       std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        }

        // Audio kept arriving after the budget ran out, as it would until the
        // application's finished handler turns recording off.
        g_recFileFromRadio = false;
        g_sfRecFile = nullptr;

        bool result = true;
        result &= check("times recording was reported finished", recRadioDone.load(), 1);
        result &= check("samples left in the recording budget", (int)g_recFromRadioSamples.load(), 0);
        return result;
    }

    // Fills what's left of a FIFO with a value that the pipeline wouldn't
    // produce, and returns how much that was.
    static int fillWithMarker(GenericFIFO<short>* fifo, short marker)
    {
        std::vector<short> samples(fifo->numFree(), marker);
        if (!samples.empty())
        {
            fifo->write(samples.data(), (int)samples.size());
        }
        return (int)samples.size();
    }

    static bool onlyContains(std::vector<short> const& samples, short marker)
    {
        for (auto sample : samples)
        {
            if (sample != marker) return false;
        }
        return true;
    }

    static bool txProcessingDropsOutputWhenRadioFifoIsFull()
    {
        constexpr short MARKER = 12345;

        resetGlobals();
        g_tx = true;
        FifoGuard fifos;
        seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE); // 1s of speech
        int queued = fillWithMarker(g_rxUserdata->outfifo1, MARKER);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        FakeRealtimeHelper helper;
        thread.txProcessing_(&helper);

        // There's nowhere for anything new to go, so it's dropped rather than
        // waited for (this is the audio path) or written over what's queued.
        auto radioOut = drain(g_rxUserdata->outfifo1);

        bool result = true;
        result &= check("samples still queued for the radio", (int)radioOut.size(), queued);
        result &= onlyContains(radioOut, MARKER) ? true : (std::cout << "[what was queued for the radio was overwritten] ", false);
        return result;
    }

    static bool timeOutWarningBeepsThenTurnsItselfOff()
    {
        resetGlobals();
        g_tx = true; // half duplex and transmitting: nothing else reaches the speaker
        g_totBeepActive = true;
        FifoGuard fifos;

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();
        seedSine(g_rxUserdata->infifo1, TEST_SAMPLE_RATE);

        FakeRealtimeHelper helper;
        thread.rxProcessing_(&helper);

        auto speakerOut = drain(g_rxUserdata->outfifo2);

        bool result = true;

        // Five 80 ms beeps at 8 kHz are 3200 samples; a few of those are zero crossings.
        result &= checkBetween("samples of beep heard", countNonZero(speakerOut), 2800, 3200);
        result &= check("beep still wanted once finished", (int)g_totBeepActive.load(), 0);

        // Nothing more is heard until the beep is asked for again.
        seedSine(g_rxUserdata->infifo1, TEST_SAMPLE_RATE);
        thread.rxProcessing_(&helper);
        result &= check("loudest speaker sample after beep", peak(drain(g_rxUserdata->outfifo2)), 0);

        g_totBeepActive = true;
        seedSine(g_rxUserdata->infifo1, TEST_SAMPLE_RATE);
        thread.rxProcessing_(&helper);
        result &= checkBetween("samples of second beep heard", countNonZero(drain(g_rxUserdata->outfifo2)), 2800, 3200);
        return result;
    }

    // Runs a second of silence from the radio through a receive thread and
    // returns what reaches the speaker.
    static std::vector<short> speakerOutputForSilentRadio()
    {
        FifoGuard fifos;

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();
        seedSilence(g_rxUserdata->infifo1, TEST_SAMPLE_RATE);

        FakeRealtimeHelper helper;
        thread.rxProcessing_(&helper);

        return drain(g_rxUserdata->outfifo2);
    }

    static bool interferingToneIsAddedToReceivedAudio()
    {
        resetGlobals();
        g_analog = 1; // so that what's received reaches the speaker as is
        g_testToneEnabled = true;
        g_testToneFreq = 1000.0f;
        g_testToneAmplitude = 8000.0f;

        auto speakerOut = speakerOutputForSilentRadio();

        bool result = true;

        // Mixing with the (silent) time-out beep brings a peak of 8000 down to about 5600.
        result &= checkBetween("loudest speaker sample", peak(speakerOut), 5400, 5800);
        result &= checkBetween("tone frequency", (int)std::lround(toneFrequency(speakerOut, TEST_SAMPLE_RATE)), 995, 1005);
        return result;
    }

    static bool interferingToneFollowsItsFrequencyAndAmplitudeSettings()
    {
        resetGlobals();
        g_analog = 1;
        g_testToneEnabled = true;
        g_testToneFreq = 400.0f;
        g_testToneAmplitude = 800.0f;

        auto speakerOut = speakerOutputForSilentRadio();

        bool result = true;
        result &= checkBetween("loudest speaker sample", peak(speakerOut), 540, 590);
        result &= checkBetween("tone frequency", (int)std::lround(toneFrequency(speakerOut, TEST_SAMPLE_RATE)), 395, 405);
        return result;
    }

    static bool noInterferingToneUnlessEnabled()
    {
        resetGlobals();
        g_analog = 1;
        g_testToneFreq = 1000.0f;
        g_testToneAmplitude = 8000.0f;

        return check("loudest speaker sample", peak(speakerOutputForSilentRadio()), 0);
    }

    // Runs a second of tone from the radio through a receive thread in
    // analog mode (where what's received is what's played) and returns the
    // loudest of what reaches the speaker.
    static int peakOfSpeakerOutputForToneFromRadio()
    {
        FifoGuard fifos;

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();
        seedSine(g_rxUserdata->infifo1, TEST_SAMPLE_RATE);

        FakeRealtimeHelper helper;
        thread.rxProcessing_(&helper);

        return peak(drain(g_rxUserdata->outfifo2));
    }

    static int64_t nanosecondsFromNow(int seconds)
    {
        auto when = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        return std::chrono::duration_cast<std::chrono::nanoseconds>(when.time_since_epoch()).count();
    }

    static bool speakerPlaysWhatIsReceived()
    {
        resetGlobals();
        g_analog = 1;
        return checkBetween("loudest speaker sample", peakOfSpeakerOutputForToneFromRadio(), 1000, 32767);
    }

    // With one radio that can't hear while it's sending, there's nothing
    // worth playing during a transmission.
    static bool speakerIsMutedWhileTransmittingHalfDuplex()
    {
        resetGlobals();
        g_analog = 1;
        g_tx = true;
        return check("loudest speaker sample", peakOfSpeakerOutputForToneFromRadio(), 0);
    }

    // In full duplex there is: the other station, on the receiver.
    static bool speakerKeepsPlayingWhileTransmittingFullDuplex()
    {
        resetGlobals();
        g_analog = 1;
        g_half_duplex = false;
        g_tx = true;
        return checkBetween("loudest speaker sample", peakOfSpeakerOutputForToneFromRadio(), 1000, 32767);
    }

    // After a transmission the speaker stays off until the radio's had time
    // to go back to receiving, so that the changeover isn't heard.
    static bool speakerStaysMutedUntilRadioIsBackOnReceive()
    {
        resetGlobals();
        g_analog = 1;

        bool result = true;
        g_rxMutedUntilNs = nanosecondsFromNow(60);
        result &= check("loudest speaker sample while the radio changes over", peakOfSpeakerOutputForToneFromRadio(), 0);

        g_rxMutedUntilNs = nanosecondsFromNow(-1);
        result &= checkBetween("loudest speaker sample once it has", peakOfSpeakerOutputForToneFromRadio(), 1000, 32767);
        return result;
    }

    // That wait is for one radio changing over. In full duplex nothing does.
    static bool speakerDoesNotWaitForRadioInFullDuplex()
    {
        resetGlobals();
        g_analog = 1;
        g_half_duplex = false;
        g_rxMutedUntilNs = nanosecondsFromNow(60);
        return checkBetween("loudest speaker sample", peakOfSpeakerOutputForToneFromRadio(), 1000, 32767);
    }

    // While the voice keyer is sending, what's received isn't played even in
    // full duplex, unless the voice keyer is being monitored (in which case
    // it's the voice keyer's audio that's heard: see
    // monitoredVoiceKeyerAudioFollowsItsOwnVolumeSetting).
    static bool speakerIsMutedWhileVoiceKeyerTransmits()
    {
        resetGlobals();
        g_analog = 1;
        g_half_duplex = false;
        g_tx = true;
        g_voice_keyer_tx = true;
        return check("loudest speaker sample", peakOfSpeakerOutputForToneFromRadio(), 0);
    }

    // Recordings are fed through buffers sized for audio arriving as it's
    // spoken or received, so these hand a second of tone over a tenth of a
    // second at a time and let the recording keep up.
    static constexpr int RECORD_ROUNDS = 10;
    static constexpr int SAMPLES_PER_ROUND = TEST_SAMPLE_RATE / RECORD_ROUNDS;
    static constexpr int FRAMES_PER_ROUND = SAMPLES_PER_ROUND / (TEST_SAMPLE_RATE * FRAME_DURATION_MS / MS_TO_SEC);

    static void waitForRound(RecordingFile& recording, int round, int fileSampleRate)
    {
        // Up to a tenth of a second is held back until more arrives. The
        // buffer has room for a quarter of a second, so the next round is
        // only safe to hand over once there's little more than that waiting.
        int expectedSoFar = fileSampleRate * (round + 1) / RECORD_ROUNDS;
        recording.waitForSamples(expectedSoFar - fileSampleRate * 13 / 100);
    }

    static void transmitSecondOfTone(RecordingFile& recording, int fileSampleRate)
    {
        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        auto mic = sine(TEST_SAMPLE_RATE, 440.0, 8000.0);
        FakeRealtimeHelper helper;
        for (int round = 0; round < RECORD_ROUNDS; round++)
        {
            g_rxUserdata->infifo2->write(&mic[round * SAMPLES_PER_ROUND], SAMPLES_PER_ROUND);

            // Stop once the microphone audio is used up; carrying on would
            // send (and record) silence in its place.
            helper.framesAllowed = FRAMES_PER_ROUND;
            thread.txProcessing_(&helper);
            drain(g_rxUserdata->outfifo1);
            waitForRound(recording, round, fileSampleRate);
        }
    }

    static void receiveSecondOfTone(RecordingFile& recording, int fileSampleRate)
    {
        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        auto radio = sine(TEST_SAMPLE_RATE, 440.0, 8000.0);
        FakeRealtimeHelper helper;
        for (int round = 0; round < RECORD_ROUNDS; round++)
        {
            g_rxUserdata->infifo1->write(&radio[round * SAMPLES_PER_ROUND], SAMPLES_PER_ROUND);
            thread.rxProcessing_(&helper);
            drain(g_rxUserdata->outfifo2);
            waitForRound(recording, round, fileSampleRate);
        }
    }

    static bool isRecordingOfTone(std::vector<short> const& recorded, int fileSampleRate, int lowestPeak, int highestPeak)
    {
        bool result = true;

        // The last couple of tenths of a second may still have been waiting to be recorded.
        result &= checkBetween("samples recorded", (int)recorded.size(), fileSampleRate * 3 / 4, fileSampleRate);
        result &= checkBetween("loudest recorded sample", peak(recorded), lowestPeak, highestPeak);
        result &= checkBetween("recorded tone frequency", (int)std::lround(toneFrequency(recorded, fileSampleRate)), 435, 445);
        return result;
    }

    static bool recordsMicrophoneAudio()
    {
        resetGlobals();
        g_tx = true;
        g_analog = 1;
        FifoGuard fifos;

        RecordingFile recording(TEST_SAMPLE_RATE);
        if (recording.get() == nullptr)
        {
            std::cout << "[could not create file to record to] ";
            return false;
        }
        g_sfRecMicFile = recording.get();
        g_recFileFromMic = true;

        transmitSecondOfTone(recording, TEST_SAMPLE_RATE);

        g_recFileFromMic = false;
        g_sfRecMicFile = nullptr;

        // What's recorded is the microphone as is, not what was then done to it.
        return isRecordingOfTone(recording.finish(), TEST_SAMPLE_RATE, 7900, 8000);
    }

    static bool recordsWhatIsSentToRadio()
    {
        resetGlobals();
        g_tx = true;
        g_analog = 1; // sends the microphone at twice its level
        FifoGuard fifos;

        RecordingFile recording(RECORD_FILE_SAMPLE_RATE);
        if (recording.get() == nullptr)
        {
            std::cout << "[could not create file to record to] ";
            return false;
        }
        g_sfRecFileFromModulator = recording.get();
        g_recFileFromModulator = true;

        transmitSecondOfTone(recording, RECORD_FILE_SAMPLE_RATE);

        g_recFileFromModulator = false;
        g_sfRecFileFromModulator = nullptr;

        return isRecordingOfTone(recording.finish(), RECORD_FILE_SAMPLE_RATE, 15800, 16200);
    }

    static bool recordsWhatIsSentToSpeaker()
    {
        resetGlobals();
        g_analog = 1;
        FifoGuard fifos;

        RecordingFile recording(RECORD_FILE_SAMPLE_RATE);
        if (recording.get() == nullptr)
        {
            std::cout << "[could not create file to record to] ";
            return false;
        }
        g_sfRecDecoderFile = recording.get();
        g_recFileFromDecoder = true;

        receiveSecondOfTone(recording, RECORD_FILE_SAMPLE_RATE);

        g_recFileFromDecoder = false;
        g_sfRecDecoderFile = nullptr;

        // Converting to the file's sample rate can overshoot slightly.
        return isRecordingOfTone(recording.finish(), RECORD_FILE_SAMPLE_RATE, 7900, 8100);
    }

    static bool nothingIsRecordedUnlessAskedFor()
    {
        resetGlobals();
        g_tx = true;
        g_half_duplex = false; // so that both directions run
        g_analog = 1;
        FifoGuard fifos;

        // A file is open in all four places, but recording isn't turned on for any.
        RecordingFile mic(TEST_SAMPLE_RATE);
        if (mic.get() == nullptr)
        {
            std::cout << "[could not create file to record to] ";
            return false;
        }
        g_sfRecMicFile = mic.get();
        g_sfRecFileFromModulator = mic.get();
        g_sfRecFile = mic.get();
        g_sfRecDecoderFile = mic.get();

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        {
            TxRxThread txThread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
            TxRxThread rxThread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
            txThread.initializePipeline_();
            rxThread.initializePipeline_();
            seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE);
            seedSine(g_rxUserdata->infifo1, TEST_SAMPLE_RATE);

            FakeRealtimeHelper helper;
            txThread.txProcessing_(&helper);
            rxThread.rxProcessing_(&helper);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }

        g_sfRecMicFile = nullptr;
        g_sfRecFileFromModulator = nullptr;
        g_sfRecFile = nullptr;
        g_sfRecDecoderFile = nullptr;

        return check("samples recorded", (int)mic.finish().size(), 0);
    }

    // Runs four seconds of the given microphone audio through a transmit
    // thread in analog mode and returns what was sent to the radio during
    // the last of them.
    static std::vector<short> lastSecondSentToRadio(std::vector<short> const& oneSecondOfMic)
    {
        FifoGuard fifos;

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        FakeRealtimeHelper helper;
        std::vector<short> sent;
        for (int second = 0; second < 4; second++)
        {
            g_rxUserdata->infifo2->write(const_cast<short*>(oneSecondOfMic.data()), oneSecondOfMic.size());
            thread.txProcessing_(&helper);
            sent = drain(g_rxUserdata->outfifo1);
        }
        return sent;
    }

    static bool automaticGainControlRaisesQuietMicrophone()
    {
        // Loud enough that the leveler treats it as speech rather than
        // silence, but well below its loudness target.
        auto quietSpeech = sine(TEST_SAMPLE_RATE, 440.0, 1000.0);

        resetGlobals();
        g_tx = true;
        g_analog = 1;
        double without = rms(lastSecondSentToRadio(quietSpeech));

        resetGlobals();
        g_tx = true;
        g_analog = 1;
        g_agcEnabled = true;
        double with = rms(lastSecondSentToRadio(quietSpeech));

        bool result = true;
        result &= checkBetween("level sent without gain control", (int)without, 1350, 1480); // 1000 peak, doubled
        if (with < without * 1.15)
        {
            std::cout << "[level sent with gain control was " << with << ", expected well above " << without << "] ";
            result = false;
        }
        return result;
    }

    static bool noiseReductionQuietensNoise()
    {
        auto hiss = noise(TEST_SAMPLE_RATE, 3000);

        resetGlobals();
        g_tx = true;
        g_analog = 1;
        double without = rms(lastSecondSentToRadio(hiss));

        resetGlobals();
        g_tx = true;
        g_analog = 1;
        g_noiseReductionEnabled = true;
        double with = rms(lastSecondSentToRadio(hiss));

        bool result = true;
        if (without < 1000.0)
        {
            std::cout << "[level sent without noise reduction was only " << without << "] ";
            result = false;
        }
        if (with > without / 2.0)
        {
            std::cout << "[level sent with noise reduction was " << with << ", expected at most half of " << without << "] ";
            result = false;
        }
        return result;
    }

    static bool stopsProcessingWhenToldTimeIsUp()
    {
        resetGlobals();
        g_tx = true;
        g_half_duplex = false;
        g_analog = 1;
        FifoGuard fifos;

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread txThread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        TxRxThread rxThread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        txThread.initializePipeline_();
        rxThread.initializePipeline_();
        seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE);
        seedSine(g_rxUserdata->infifo1, TEST_SAMPLE_RATE);

        FakeRealtimeHelper helper;
        helper.stopWork = true;
        txThread.txProcessing_(&helper);
        rxThread.rxProcessing_(&helper);

        bool result = true;

        // Nothing is lost; it's all still there for next time.
        result &= check("microphone samples still waiting", g_rxUserdata->infifo2->numUsed(), TEST_SAMPLE_RATE);
        result &= check("radio samples still waiting", g_rxUserdata->infifo1->numUsed(), TEST_SAMPLE_RATE);
        result &= check("samples sent to radio", g_rxUserdata->outfifo1->numUsed(), 0);
        result &= check("samples sent to speaker", g_rxUserdata->outfifo2->numUsed(), 0);

        helper.stopWork = false;
        txThread.txProcessing_(&helper);
        rxThread.rxProcessing_(&helper);
        result &= check("microphone samples waiting once there's time", g_rxUserdata->infifo2->numUsed(), 0);
        result &= check("radio samples waiting once there's time", g_rxUserdata->infifo1->numUsed(), 0);
        result &= checkBetween("samples sent to radio once there's time", g_rxUserdata->outfifo1->numUsed(), TEST_SAMPLE_RATE * 9 / 10, TEST_SAMPLE_RATE * 11 / 10);
        result &= checkBetween("samples sent to speaker once there's time", g_rxUserdata->outfifo2->numUsed(), TEST_SAMPLE_RATE * 9 / 10, TEST_SAMPLE_RATE * 11 / 10);
        return result;
    }

    static bool endOfOverIsSentOnceWhenTransmitEnds()
    {
        resetGlobals();
        g_tx = true;
        FifoGuard fifos;

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        FakeRealtimeHelper helper;
        bool result = true;

        seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE);
        thread.txProcessing_(&helper);
        result &= checkBetween("loudest sample of voice sent", peak(drain(g_rxUserdata->outfifo1)), 1, 32767);
        result &= check("end of over reported as queued while still talking", (int)g_eoo_enqueued.load(), 0);

        // The microphone has nothing left and transmit is being ended.
        endingTx = true;
        thread.txProcessing_(&helper);

        auto endOfOver = drain(g_rxUserdata->outfifo1);
        result &= check("samples of end of over", (int)endOfOver.size(), freedvInterface.getTxNNomModemSamples());
        result &= checkBetween("loudest sample of end of over", peak(endOfOver), 1, 32767);
        result &= check("end of over reported as queued", (int)g_eoo_enqueued.load(), 1);

        // However long it then takes for transmit to actually end, it isn't sent again.
        thread.txProcessing_(&helper);
        thread.txProcessing_(&helper);
        result &= check("samples sent after end of over", g_rxUserdata->outfifo1->numUsed(), 0);
        result &= check("end of over still reported as queued", (int)g_eoo_enqueued.load(), 1);

        // The next over gets an end of its own.
        endingTx = false;
        seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE);
        thread.txProcessing_(&helper);
        drain(g_rxUserdata->outfifo1);
        result &= check("end of over reported as queued during next over", (int)g_eoo_enqueued.load(), 0);

        endingTx = true;
        thread.txProcessing_(&helper);
        result &= check("samples of second end of over", g_rxUserdata->outfifo1->numUsed(), freedvInterface.getTxNNomModemSamples());
        return result;
    }

    static bool startStopRunsRealTimeHelperLifecycle()
    {
        resetGlobals();
        g_nSoundCards = 1;
        FifoGuard fifos;

        auto helper = std::make_shared<FakeRealtimeHelper>();

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        NeverReused<TxRxThread> thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, helper, makeHooks(micInDone, recRadioDone, playRadioDone));

        (*thread).start();
        (*thread).waitForReady();
        (*thread).signalToStart();

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (helper->startCalls == 0 && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }

        (*thread).stop();

        bool result = true;
        result &= check("setHelperRealTime calls", helper->setRealTimeCalls.load(), 1);
        result &= check("clearHelperRealTime calls", helper->clearRealTimeCalls.load(), 1);
        result &= helper->startCalls >= 1 ? true : (std::cout << "[startRealTimeWork never called] " << std::endl, false);
        result &= helper->stopCalls >= 1 ? true : (std::cout << "[stopRealTimeWork never called] " << std::endl, false);
        return result;
    }

    // ---------------------------------------------------------------------
    // A whole transmission, sent and received with no sound cards and no
    // clock: what the rade_loss test does through real audio devices in real
    // time, with those two things taken away. Samples arrive and are taken
    // away at the same rate, and the thread always gets its turn in between,
    // so whatever goes wrong here is FreeDV's doing rather than the machine's.
    // ---------------------------------------------------------------------
    struct RoundTrip
    {
        bool ran = false;           // false if there was no speech to send
        double secondsToSync = -1;  // of signal received before sync was found
        int syncLosses = 0;         // times sync went away before the signal ended
        double loss = -1;           // between the features sent and received
        double lossOnceGoing = -1;  // ...leaving out the first two seconds received
        int framesSent = 0;
        int framesReceived = 0;
        int framesOfSpeech = 0;     // what there was to send
        SoundCardCounts tx, rx;
        long txLateUnderruns = 0;  // short blocks after the priming window had passed
        long silentSamplesSent = 0; // within the signal, once it had started
        long silentSamplesSentOnceGoing = 0; // ...and was two seconds in
        double lastSilenceAt = -1;  // seconds into the signal
    };

    // Gives a file's thread time between the sound cards' turns, as it has
    // when this is for real. A sleep can last far longer than asked for
    // (several times over on some CI machines), so what one oversleeps by
    // is taken off the turns that follow rather than added to them all.
    class TimeBetweenTurns
    {
    public:
        void wait()
        {
            owed_ += std::chrono::milliseconds(1);
            if (owed_ <= std::chrono::steady_clock::duration::zero()) return;
            auto before = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(owed_);
            owed_ -= std::chrono::steady_clock::now() - before;
        }

    private:
        std::chrono::steady_clock::duration owed_{};
    };

    // Sends seconds of speech and receives it again, with both sound cards
    // at sampleRate. The samples are handed over in blocks of sizes chosen
    // by the two seeds (zero meaning 20 ms every time). With playedToMic,
    // the speech comes from a sound file rather than the microphone, and
    // with fromFile the receiver plays the signal from one rather than
    // getting it from the radio: both as rade_loss does it.
    static RoundTrip sendAndReceiveSpeech(
        int sampleRate, double seconds, unsigned int txSeed, unsigned int rxSeed, bool playedToMic, bool fromFile)
    {
        RoundTrip result;

        resetGlobals();
        g_sfFs = sampleRate;
        g_sfTxFs = sampleRate;
        SoundCardFifos fifos(sampleRate);

        auto speech = speechAt(sampleRate, seconds);
        if (speech.empty())
        {
            return result;
        }
        result.ran = true;
        result.framesOfSpeech = (int)(speech.size() * 100 / sampleRate);

        std::string txFeatures = unitTestTempPath("TxRxThreadTest", "-tx-features.f32");
        std::string rxFeatures = unitTestTempPath("TxRxThreadTest", "-rx-features.f32");
        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        FakeRealtimeHelper helper;

        // Transmit. (The modem's started afresh for each half, as when
        // FreeDV is run once to send and again to receive.)
        std::vector<short> signal;
        freedvInterface.stop();
        freedvInterface.start(100, false);
        {
            std::unique_ptr<TestFile> file;
            if (playedToMic)
            {
                file = std::make_unique<TestFile>(speech, sampleRate);
                if (file->get() == nullptr)
                {
                    result.ran = false;
                    return result;
                }
                g_sfPlayFile.store(file->get(), std::memory_order_release);
                g_playFileToMicIn = true;
            }

            utTxFeatureFile = txFeatures;
            TxRxThread thread(true, sampleRate, sampleRate, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
            thread.initializePipeline_();
            utTxFeatureFile = "";

            g_tx = true;
            BlockSizes blocks(sampleRate, txSeed);
            std::vector<short> silence(sampleRate / 10, 0);
            size_t offset = 0;
            TimeBetweenTurns timeBetweenTurns;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
            while (std::chrono::steady_clock::now() < deadline)
            {
                int block = blocks.next();
                const short* samples = silence.data();
                if (playedToMic)
                {
                    // The microphone carries on delivering whatever it hears
                    // (nothing); the file takes its place further along.
                    if (micInDone.load() > 0) break;
                }
                else
                {
                    if (offset >= speech.size()) break;
                    block = (int)std::min<size_t>(block, speech.size() - offset);
                    samples = &speech[offset];
                    offset += block;
                }

                long shortfallBefore = result.tx.outputShortfallSamples;
                auto sent = soundCardsTurn(
                    g_rxUserdata->infifo2, g_rxUserdata->outfifo1, samples, block,
                    [&]() {
                        thread.txProcessing_(&helper);

                        // The file's read by a thread of its own, which in
                        // real life has a whole block's time before the
                        // card asks again; here the next ask comes as soon
                        // as the machine gets to it, sooner than that
                        // thread can be relied on to keep up on a slow or
                        // busy machine (or under a sanitizer). So once the
                        // transmission's going, give it the time it would
                        // have had before the card takes its block.
                        if (!playedToMic || signal.size() < (size_t)(sampleRate * 30 / 100)) return;
                        auto giveUpAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
                        while (g_rxUserdata->outfifo1->numUsed() < block && micInDone.load() == 0 &&
                               std::chrono::steady_clock::now() < giveUpAt)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            thread.txProcessing_(&helper);
                        }
                    }, result.tx);
                // Shortfalls while the thread is still priming just mean the
                // card's ask ran ahead of the production backlog; after that,
                // only ones big enough to put a hole in the signal count —
                // a sound card's callback can arrive a few samples early in
                // real life, and main.cpp pads those every day.
                long shortBy = result.tx.outputShortfallSamples - shortfallBefore;
                // Padding after the source has run out is just the silence
                // that follows a transmission, not a hole in it; a shortfall
                // while there's still speech to send is one.
                bool moreSource = playedToMic ? (micInDone.load() == 0) : (offset < speech.size());
                if (shortBy >= 16 && signal.size() >= (size_t)(sampleRate * 30 / 100) && moreSource)
                {
                    result.txLateUnderruns++;
                }
                signal.insert(signal.end(), sent.begin(), sent.end());

                if (playedToMic)
                {
                    // ...and some time between turns as well, as the file
                    // thread has when this is for real.
                    timeBetweenTurns.wait();
                }
            }

            // ...and what's still on its way out when the speech ends.
            while (g_rxUserdata->outfifo1->numUsed() > 0)
            {
                auto sent = drain(g_rxUserdata->outfifo1);
                signal.insert(signal.end(), sent.begin(), sent.end());
            }
            g_tx = false;
            g_playFileToMicIn = false;
            g_sfPlayFile.store(nullptr, std::memory_order_release);
        }

        // Silence within the signal is a gap in it: the modem's output is
        // never exactly zero for long once it's going.
        size_t first = 0, last = signal.size();
        while (first < signal.size() && signal[first] == 0) first++;
        while (last > first && signal[last - 1] == 0) last--;
        int run = 0;
        for (size_t i = first; i < last; i++)
        {
            run = (signal[i] == 0) ? run + 1 : 0;
            long silent = (run == 16) ? 16 : (run > 16 ? 1 : 0);
            result.silentSamplesSent += silent;
            if (silent != 0)
            {
                result.lastSilenceAt = (double)(i - first) / sampleRate;
                if (i - first > 2 * (size_t)sampleRate) result.silentSamplesSentOnceGoing += silent;
            }
        }

        // Receive, after a moment of nothing for the receiver to settle on.
        freedvInterface.stop();
        freedvInterface.start(100, false);
        {
            std::unique_ptr<TestFile> file;
            if (fromFile)
            {
                file = std::make_unique<TestFile>(signal, sampleRate);
                if (file->get() == nullptr)
                {
                    result.ran = false;
                    return result;
                }
            }

            utRxFeatureFile = rxFeatures;
            TxRxThread thread(false, sampleRate, sampleRate, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
            thread.initializePipeline_();
            utRxFeatureFile = "";

            BlockSizes blocks(sampleRate, rxSeed);
            std::vector<short> silence(sampleRate / 10, 0);
            auto process = [&]() { thread.rxProcessing_(&helper); };
            for (int quiet = 0; quiet < sampleRate / 2; quiet += sampleRate / 50)
            {
                soundCardsTurn(g_rxUserdata->infifo1, g_rxUserdata->outfifo2, silence.data(), sampleRate / 50, process, result.rx);
            }
            result.rx = SoundCardCounts();

            if (fromFile)
            {
                g_sfPlayFileFromRadio.store(file->get(), std::memory_order_release);
                g_playFileFromRadio = true;
            }

            bool inSync = false;
            size_t received = 0;
            TimeBetweenTurns timeBetweenTurns;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
            while (std::chrono::steady_clock::now() < deadline)
            {
                int block = blocks.next();
                const short* samples = silence.data();
                if (fromFile)
                {
                    if (playRadioDone.load() > 0) break;
                }
                else
                {
                    if (received >= signal.size()) break;
                    block = (int)std::min<size_t>(block, signal.size() - received);
                    samples = &signal[received];
                }
                received += block;

                soundCardsTurn(g_rxUserdata->infifo1, g_rxUserdata->outfifo2, samples, block, process, result.rx);

                bool nowInSync = freedvInterface.getSync() != 0;
                if (nowInSync && result.secondsToSync < 0)
                {
                    result.secondsToSync = (double)received / sampleRate;
                }
                if (inSync && !nowInSync && (fromFile || received + sampleRate / 2 < signal.size()))
                {
                    result.syncLosses++;
                }
                inSync = nowInSync;

                if (fromFile)
                {
                    timeBetweenTurns.wait();
                }
            }

            g_playFileFromRadio = false;
            g_sfPlayFileFromRadio.store(nullptr, std::memory_order_release);
        }

        auto sentFeatures = loadFeatures(txFeatures);
        auto receivedFeatures = loadFeatures(rxFeatures);
        remove(txFeatures.c_str());
        remove(rxFeatures.c_str());
        result.framesSent = (int)(sentFeatures.size() / FEATURES_PER_FRAME);
        result.framesReceived = (int)(receivedFeatures.size() / FEATURES_PER_FRAME);
        int startFrame = 0;
        result.loss = featureLoss(sentFeatures, receivedFeatures, &startFrame);
        if (result.loss >= 0)
        {
            // Second by second, for seeing where any loss is.
            static const float nothing[FEATURES_PER_FRAME] = {0};
            bool verbose = getenv("FREEDV_TEST_VERBOSE") != nullptr;
            if (verbose) std::cout << std::endl << "      loss by second (from frame " << startFrame << " sent):";
            double totalOnceGoing = 0.0;
            int secondsOnceGoing = 0;
            for (int frame = 0; frame + 100 <= result.framesReceived; frame += 100)
            {
                double total = 0.0;
                for (int i = frame; i < frame + 100; i++)
                {
                    int sentFrame = startFrame + i;
                    const float* sent = (sentFrame >= 0 && sentFrame < result.framesSent)
                        ? &sentFeatures[(size_t)sentFrame * FEATURES_PER_FRAME] : nothing;
                    total += frameLoss(sent, &receivedFeatures[(size_t)i * FEATURES_PER_FRAME]);
                }
                if (verbose && frame < 1500) std::cout << " " << (int)(total + 0.5) / 100.0;
                if (frame >= 200)
                {
                    totalOnceGoing += total / 100.0;
                    secondsOnceGoing++;
                }
            }
            if (secondsOnceGoing > 0) result.lossOnceGoing = totalOnceGoing / secondsOnceGoing;
        }
        return result;
    }

    // mostLoss is what's allowed between what was sent and what came back:
    // rade_loss allows 0.15.
    static bool checkRoundTrip(const char* what, RoundTrip const& roundTrip, double mostLoss = 0.15)
    {
        if (!roundTrip.ran)
        {
            std::cout << "[skipped: no speech recording to send] ";
            return true;
        }

        if (getenv("FREEDV_TEST_VERBOSE") != nullptr)
        {
            std::cout << std::endl << "    " << what << ": sync after " << roundTrip.secondsToSync << " s, lost "
                      << roundTrip.syncLosses << " times; " << roundTrip.framesOfSpeech << " frames of speech, "
                      << roundTrip.framesSent << " sent, " << roundTrip.framesReceived << " received; loss " << roundTrip.loss
                      << " (" << roundTrip.lossOnceGoing << " once going); " << roundTrip.silentSamplesSent
                      << " silent samples in the signal (last at " << roundTrip.lastSilenceAt << " s); tx in/out " << roundTrip.tx.inputOverflows << "/"
                      << roundTrip.tx.outputUnderruns << ", rx in/out " << roundTrip.rx.inputOverflows << "/"
                      << roundTrip.rx.outputUnderruns << " ";
        }

        bool result = true;
        auto fail = [&]() -> std::ostream& { result = false; return std::cout << "[" << what << ": "; };

        // Nothing was lost on the way in; once the thread is past its
        // priming the radio's sound card always got its whole block; and the
        // signal has no holes in it.
        if (roundTrip.tx.inputOverflows != 0) fail() << roundTrip.tx.inputOverflows << " blocks from the microphone had nowhere to go] ";
        if (roundTrip.rx.inputOverflows != 0) fail() << roundTrip.rx.inputOverflows << " blocks from the radio had nowhere to go] ";
        if (roundTrip.txLateUnderruns != 0) fail() << "the radio's sound card went short " << roundTrip.txLateUnderruns << " times after the thread had primed] ";
        if (roundTrip.silentSamplesSent != 0) fail() << roundTrip.silentSamplesSent << " samples of silence within the signal sent, the last " << roundTrip.lastSilenceAt << " s into it] ";

        // What's sent is the speech and no more: nothing's put in to make up
        // for the microphone being behind.
        int extraFrames = roundTrip.framesSent - roundTrip.framesOfSpeech;
        if (extraFrames < -4 || extraFrames > 1)
        {
            fail() << roundTrip.framesSent << " frames sent for " << roundTrip.framesOfSpeech << " of speech] ";
        }

        // The receiver finds it promptly and stays with it.
        if (roundTrip.secondsToSync < 0 || roundTrip.secondsToSync > 3.0) fail() << "sync after " << roundTrip.secondsToSync << " s of signal, expected within 3] ";
        if (roundTrip.syncLosses != 0) fail() << "lost sync " << roundTrip.syncLosses << " time(s) part of the way through] ";

        // Most of what was sent comes back (the receiver takes a moment to
        // find the signal), and stays that good from then on: the overall
        // figure carries the demodulator's lock-on — a good second of
        // rough frames right after sync, wherever the cards happen to put
        // it — so mostLoss is the bar for the once-going measure.
        if (roundTrip.framesReceived < roundTrip.framesOfSpeech - 300) fail() << roundTrip.framesReceived << " frames received of " << roundTrip.framesOfSpeech << " of speech] ";
        if (roundTrip.lossOnceGoing < 0 || roundTrip.lossOnceGoing > mostLoss) fail() << "loss " << roundTrip.lossOnceGoing << " once going, expected " << mostLoss << " at most] ";
        if (roundTrip.loss < 0 || roundTrip.loss > mostLoss + 0.05) fail() << "loss " << roundTrip.loss << " overall, expected " << mostLoss + 0.05 << " at most] ";
        return result;
    }

    // All of the recording for the cases that go about it as rade_loss does,
    // and less of it for the variations on how the sound cards behave.
    static constexpr double WHOLE_RECORDING_SECONDS = 45.0;
    static constexpr double ROUND_TRIP_SECONDS = 20.0;

    static bool speechSurvivesRoundTrip()
    {
        return checkRoundTrip("48 kHz, 20 ms blocks", sendAndReceiveSpeech(48000, ROUND_TRIP_SECONDS, 0, 0, false, false));
    }

    // However the sound cards happen to chop it up: it's the same samples in
    // the same order, so what comes back should be as good.
    static bool speechSurvivesRoundTripInBlocksOfAnySize()
    {
        bool result = true;
        result &= checkRoundTrip("48 kHz, uneven blocks sent", sendAndReceiveSpeech(48000, ROUND_TRIP_SECONDS, 1, 0, false, false));
        result &= checkRoundTrip("48 kHz, uneven blocks received", sendAndReceiveSpeech(48000, ROUND_TRIP_SECONDS, 0, 2, false, false));
        result &= checkRoundTrip("48 kHz, uneven blocks both ways", sendAndReceiveSpeech(48000, ROUND_TRIP_SECONDS, 3, 4, false, false));
        result &= checkRoundTrip("44.1 kHz, uneven blocks both ways", sendAndReceiveSpeech(44100, ROUND_TRIP_SECONDS, 5, 6, false, false));
        // (Sound cards that only do telephone quality cost some of the speech
        // before FreeDV sees it, and what's sent is that much further from
        // what the vocoder was made for.)
        result &= checkRoundTrip("8 kHz, uneven blocks both ways", sendAndReceiveSpeech(8000, ROUND_TRIP_SECONDS, 7, 8, false, false), 0.3);
        return result;
    }

    // As the rade_loss test does it: the speech played from a file in place
    // of the microphone, and received from a recording of what was sent.
    static bool speechSurvivesRoundTripThroughFiles()
    {
        bool result = true;
        result &= checkRoundTrip("files, 20 ms blocks", sendAndReceiveSpeech(48000, WHOLE_RECORDING_SECONDS, 0, 0, true, true));
        result &= checkRoundTrip("files, uneven blocks", sendAndReceiveSpeech(48000, WHOLE_RECORDING_SECONDS, 9, 10, true, true));
        return result;
    }

    // ---------------------------------------------------------------------
    // A steady tone through the thread while the sound card that takes its
    // output stalls: the tone carries on unbroken, other than where there
    // was nowhere to put it or nothing to give.
    // ---------------------------------------------------------------------
    struct Continuity
    {
        long delivered = 0;         // samples of tone that came out
        long breaks = 0;            // places where it wasn't the same tone carrying on
        long unexplainedBreaks = 0; // ...with nothing just before to account for it
        long lateEvents = 0;        // things that account for breaks, once it had got going
        long lateUnderruns = 0;     // short blocks once past the priming window
        SoundCardCounts counts;
    };

    // Puts a minute of tone through one direction of the thread in analog
    // mode, in blocks of uneven sizes. With stalls, the sound card taking
    // the output sometimes stops taking it for a while.
    //
    // A break in the tone is accounted for if, no longer before it than it
    // takes to get through everything waiting either side of the thread, a
    // block from a sound card had nowhere to go, or a sound card wanted more
    // than was there or had stopped taking any.
    static Continuity toneThrough(bool tx, unsigned int seed, bool stalls)
    {
        Continuity result;

        resetGlobals();
        g_analog = 1;
        g_tx = tx;
        SoundCardFifos fifos(TEST_SAMPLE_RATE);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(tx, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        auto in = tx ? g_rxUserdata->infifo2 : g_rxUserdata->infifo1;
        auto out = tx ? g_rxUserdata->outfifo1 : g_rxUserdata->outfifo2;

        constexpr double TONE_HZ = 500.0;
        constexpr long TOTAL = 60L * TEST_SAMPLE_RATE;
        constexpr long GOING = 10L * TEST_SAMPLE_RATE;
        // Everything the FIFOs either side of the thread can hold, and a second.
        const long WITHIN = in->capacity() + out->capacity() + TEST_SAMPLE_RATE;
        const double step = 2.0 * M_PI * TONE_HZ / TEST_SAMPLE_RATE;
        std::mt19937 generator(seed);
        auto blockSize = [&]() { return (int)(1 + generator() % (TEST_SAMPLE_RATE / 10)); };
        auto percent = [&]() { return (int)(generator() % 100); };

        FakeRealtimeHelper helper;

        // A tone carries on from its last two samples, whatever's been done
        // to its level on the way: x[n] = 2 cos(w) x[n-1] - x[n-2].
        const double carry = 2.0 * std::cos(step);
        double before = 0.0, last = 0.0;
        int settled = 0;        // samples since the tone last (re)started

        long written = 0;
        long lastEventAt = 0;   // in samples written; starting up counts as one
        int stalledFor = 0;
        std::vector<short> block;
        while (written < TOTAL)
        {
            int numSamples = (int)std::min<long>(blockSize(), TOTAL - written);
            block.resize(numSamples);
            for (int i = 0; i < numSamples; i++)
            {
                block[i] = (short)(8000.0 * std::sin(step * (written + i)));
            }
            written += numSamples;

            if (stalls && stalledFor == 0 && percent() < 2)
            {
                stalledFor = 10 + percent();
            }
            bool taken = stalledFor == 0;
            if (stalledFor > 0) stalledFor--;

            bool event = !taken;
            auto process = [&]() { if (tx) thread.txProcessing_(&helper); else thread.rxProcessing_(&helper); };

            auto countsBefore = result.counts;
            auto turn = soundCardsTurn(in, out, block.data(), numSamples, process, result.counts, taken);
            event |= result.counts.inputOverflows != countsBefore.inputOverflows ||
                     result.counts.outputUnderruns != countsBefore.outputUnderruns;
            if (written > TEST_SAMPLE_RATE * 30 / 100 &&
                result.counts.outputShortfallSamples > countsBefore.outputShortfallSamples)
            {
                result.lateUnderruns++;
            }
            if (event)
            {
                lastEventAt = written;
                if (written > GOING) result.lateEvents++;
            }

            for (auto sample : turn)
            {
                double expected = carry * last - before;
                bool silent = sample == 0 && std::fabs(last) < 2 && std::fabs(before) < 2;
                if (settled >= 2 && !silent && std::fabs(sample - expected) > 400.0)
                {
                    result.breaks++;
                    if (written - lastEventAt > WITHIN) result.unexplainedBreaks++;
                    settled = 0;
                }
                else if (!silent)
                {
                    settled++;
                    result.delivered++;
                }
                before = last;
                last = sample;
            }
        }
        return result;
    }

    static bool reportContinuity(const char* what, Continuity const& continuity, bool transmitting, bool stalls)
    {
        if (getenv("FREEDV_TEST_VERBOSE") != nullptr)
        {
            std::cout << std::endl << "    " << what << ": " << continuity.delivered << " samples of tone out; " << continuity.breaks
                      << " breaks (" << continuity.unexplainedBreaks << " unexplained); " << continuity.counts.inputOverflows
                      << " input overflows, " << continuity.counts.outputUnderruns << " output underruns; "
                      << continuity.lateEvents << " such things after the first 10 s ";
        }

        bool result = true;
        if (continuity.delivered < 10L * TEST_SAMPLE_RATE)
        {
            std::cout << "[" << what << ": only " << continuity.delivered << " samples of tone came out] ";
            result = false;
        }
        if (continuity.unexplainedBreaks != 0)
        {
            std::cout << "[" << what << ": the tone was broken in " << continuity.unexplainedBreaks
                      << " places with nothing to account for it] ";
            result = false;
        }

        // With sound cards that keep time, nothing's lost on the way in.
        if (!stalls && continuity.counts.inputOverflows != 0)
        {
            std::cout << "[" << what << ": " << continuity.counts.inputOverflows << " blocks had nowhere to go] ";
            result = false;
        }

        // Receiving, the speaker gets silence when it asks for more than has
        // been decoded, which leaves that much more in hand for the next
        // time: it soon stops happening. Transmitting, the radio's sound
        // card is fed whatever the thread has made: it comes up short only
        // while the thread is priming for its first modem frame, which
        // lasts a fifth of a second however the blocks are sized.
        if (!stalls && !transmitting && continuity.counts.outputUnderruns > 16)
        {
            std::cout << "[" << what << ": the speaker went short " << continuity.counts.outputUnderruns
                      << " times, expected 16 at most] ";
            result = false;
        }
        if (!stalls && transmitting && (continuity.breaks != 0 || continuity.lateUnderruns != 0))
        {
            std::cout << "[" << what << ": the tone was broken in " << continuity.breaks << " places and the radio's sound card went short "
                      << continuity.lateUnderruns << " times after priming] ";
            result = false;
        }
        return result;
    }

    static bool toneIsUnbrokenWhenSoundCardsKeepTime()
    {
        bool result = true;
        for (unsigned int seed = 1; seed <= 3; seed++)
        {
            result &= reportContinuity("received", toneThrough(false, seed, false), false, false);
            result &= reportContinuity("transmitted", toneThrough(true, seed, false), true, false);
        }
        return result;
    }

    static bool toneOnlyBreaksWhereASoundCardStalled()
    {
        bool result = true;
        for (unsigned int seed = 11; seed <= 13; seed++)
        {
            result &= reportContinuity("received, with stalls", toneThrough(false, seed, true), false, true);
            result &= reportContinuity("transmitted, with stalls", toneThrough(true, seed, true), true, true);
        }
        return result;
    }

    // ---------------------------------------------------------------------
    // What the thread promises the radio's sound card at each end of a
    // transmission, with the cards kept in step so that only FreeDV's own
    // behaviour can break it. Priming: the thread makes nothing for the
    // radio until a fifth of a second of microphone audio is in hand, so
    // what starts a transmission is speech and not silence filled in for
    // audio that isn't there. And when the microphone goes dry part of the
    // way through an over, the thread sends what the microphone's audio
    // made and then stops: the carrier goes down with it rather than the
    // thread inventing samples to keep it up. (Keeping the carrier up
    // through a dry microphone means mixing in fabricated audio, which is
    // the very complaint #1567 set out to fix; whether the holes are worse
    // than the fabrication is for a station on the air to judge, so this
    // pins the honest behaviour and leaves that question open.)
    // ---------------------------------------------------------------------
    static bool transmitterPrimesWithSpeechBeforeItsFirstFrame()
    {
        resetGlobals();
        g_tx = true;
        g_analog = 1; // what goes in is what comes out, so the tone can be followed
        SoundCardFifos fifos(TEST_SAMPLE_RATE);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        FakeRealtimeHelper helper;
        SoundCardCounts counts;
        constexpr int BLOCK = TEST_SAMPLE_RATE / 50; // 20 ms at a time
        constexpr int SPEAKING_TURNS = 50;           // the microphone speaks for a second
        constexpr int DRY_TURNS = 25;                // ...and the card asks on for half a second
        const double step = 2.0 * M_PI * 500.0 / TEST_SAMPLE_RATE;

        std::vector<short> sent;
        long written = 0;
        for (int turn = 0; turn < SPEAKING_TURNS + DRY_TURNS; turn++)
        {
            // The card asks for its block every turn; the microphone only
            // hands anything over while it's speaking.
            int numSamples = turn < SPEAKING_TURNS ? BLOCK : 0;
            std::vector<short> block(numSamples);
            for (int i = 0; i < numSamples; i++)
            {
                // Phase offset keeps every tone sample non-zero, so the
                // zero/non-zero accounting can't trip over a crossing.
                block[i] = (short)(8000.0 * std::sin(step * (written + i) + 0.25));
            }
            written += numSamples;
            if (numSamples > 0 && g_rxUserdata->infifo2->write(block.data(), numSamples) != 0) counts.inputOverflows++;

            thread.txProcessing_(&helper);

            int available = std::min(g_rxUserdata->outfifo1->numUsed(), BLOCK);
            std::vector<short> taken(BLOCK, 0);
            if (available > 0) g_rxUserdata->outfifo1->read(taken.data(), available);
            sent.insert(sent.end(), taken.begin(), taken.end());
        }

        bool result = true;
        result &= check("blocks from the microphone had nowhere to go", counts.inputOverflows, 0L);

        // Nothing was given to the radio while the thread primed, and the
        // waiting was for about the length of the priming: a fifth of a
        // second of microphone audio, give or take a frame's worth.
        size_t firstTone = 0;
        while (firstTone < sent.size() && sent[firstTone] == 0) firstTone++;
        if (firstTone < (size_t)(TEST_SAMPLE_RATE * 16 / 100) || firstTone > (size_t)(TEST_SAMPLE_RATE * 28 / 100))
        {
            std::cout << "[the first samples out came " << firstTone * 1000 / TEST_SAMPLE_RATE
                      << " ms in, expected about 200 ms of priming first] ";
            result = false;
        }
        // Once tone was going it kept going while the microphone spoke:
        // nothing but the tone's own crossing of zero for its first
        // second's worth, no filled-in silence anywhere in it.
        int run = 0;
        for (size_t i = firstTone; i < firstTone + (size_t)TEST_SAMPLE_RATE && i < sent.size(); i++)
        {
            run = (sent[i] == 0) ? run + 1 : 0;
            if (run == 2)
            {
                std::cout << "[samples of silence filled in " << (i - 1) * 1000 / TEST_SAMPLE_RATE
                          << " ms into the transmission] ";
                result = false;
                break;
            }
        }
        return result;
    }

    static bool transmitterSendsOnlyWhatTheMicrophoneMade()
    {
        resetGlobals();
        g_tx = true;
        g_analog = 1;
        SoundCardFifos fifos(TEST_SAMPLE_RATE);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr, makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        FakeRealtimeHelper helper;
        SoundCardCounts counts;
        constexpr int BLOCK = TEST_SAMPLE_RATE / 50;
        constexpr int SPEAKING_TURNS = 20;           // the microphone speaks for 400 ms, then goes dry
        constexpr int DRY_TURNS = 40;                // ...the card asks on for eight times as long
        const double step = 2.0 * M_PI * 500.0 / TEST_SAMPLE_RATE;

        std::vector<short> sent;
        long written = 0;
        for (int turn = 0; turn < SPEAKING_TURNS + DRY_TURNS; turn++)
        {
            int numSamples = turn < SPEAKING_TURNS ? BLOCK : 0;
            std::vector<short> block(numSamples);
            for (int i = 0; i < numSamples; i++)
            {
                block[i] = (short)(8000.0 * std::sin(step * (written + i) + 0.25));
            }
            written += numSamples;
            if (numSamples > 0 && g_rxUserdata->infifo2->write(block.data(), numSamples) != 0) counts.inputOverflows++;

            thread.txProcessing_(&helper);

            int available = std::min(g_rxUserdata->outfifo1->numUsed(), BLOCK);
            std::vector<short> taken(BLOCK, 0);
            if (available > 0) g_rxUserdata->outfifo1->read(taken.data(), available);
            sent.insert(sent.end(), taken.begin(), taken.end());
        }

        bool result = true;
        result &= check("blocks from the microphone had nowhere to go", counts.inputOverflows, 0L);

        // What the radio was given, from the first sample to the last, is
        // exactly the speech the microphone spoke: nothing made up for the
        // silence, and nothing of the speech lost along the way.
        size_t first = 0, last = sent.size();
        while (first < sent.size() && sent[first] == 0) first++;
        while (last > first && sent[last - 1] == 0) last--;
        long expected = (long)SPEAKING_TURNS * BLOCK;
        if ((long)(last - first) != expected)
        {
            std::cout << "[" << (last - first) << " samples of signal sent for " << expected
                      << " of speech] ";
            result = false;
        }
        // And once that was all spent, nothing more: the card is left with
        // silence for the rest of its asking rather than fabricated audio.
        for (size_t i = last; i < sent.size(); i++)
        {
            if (sent[i] != 0)
            {
                std::cout << "[samples were still being sent " << i * 1000 / TEST_SAMPLE_RATE
                          << " ms in, after all the speech had been sent] ";
                result = false;
                break;
            }
        }
        return result;
    }

    // ---------------------------------------------------------------------
    // A radio's sound card whose callback buffer is bigger than the FIFO the
    // transmission is made ready in: it can never be given a whole block, so
    // if "not a whole block" means "nothing" then the transmission never
    // starts at all. It has to be given what there is, and silence for the
    // rest of its block.
    //
    // The card asks in real time, so its rate is what the test asks of the
    // machine: 24 kHz, a rate the rig's sound card actually runs at, costs
    // it a quarter of what the same ask at 48 kHz did.
    // ---------------------------------------------------------------------
    static bool transmitterFeedsASoundCardAskingForMoreThanTheFifoHolds()
    {
        resetGlobals();
        g_tx = true;

        constexpr int CARD_RATE = 24000; // what the radio's sound card runs at
        constexpr int TURN_MS = 10;
        constexpr int BLOCK = CARD_RATE * TURN_MS / 1000;
        constexpr int RADIO_MS = 2000;                       // 48000 samples: more than outfifo1 can ever hold
        constexpr int RADIO_BLOCK = CARD_RATE * RADIO_MS / 1000;
        constexpr int RADIO_TURNS = 4;                       // two asks may land before priming finishes, leaving it four seconds of real time
        constexpr int MIC_TURNS = (5 * CARD_RATE) / BLOCK;   // five seconds of microphone: the full room of its FIFO, no more

        SoundCardFifos fifos(CARD_RATE);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        auto helper = std::make_shared<ClockRealtimeHelper>(std::chrono::milliseconds(TURN_MS));
        NeverReused<TxRxThread> thread(true, CARD_RATE, CARD_RATE, nullptr, helper, makeHooks(micInDone, recRadioDone, playRadioDone));

        (*thread).start();
        (*thread).waitForReady();
        (*thread).signalToStart();

        // The microphone, at real time.
        std::thread mic([&]() {
            std::vector<short> block(BLOCK);
            long sent = 0;
            auto nextTurn = std::chrono::steady_clock::now();
            for (int turn = 0; turn < MIC_TURNS; turn++)
            {
                for (int i = 0; i < BLOCK; i++)
                {
                    block[i] = (short)(8000.0 * std::sin(2.0 * M_PI * 1000.0 * (sent + i) / CARD_RATE));
                }
                sent += BLOCK;
                g_rxUserdata->infifo2->write(block.data(), BLOCK);
                // Microphone audio arrives at real time, deadline by deadline.
                nextTurn += std::chrono::milliseconds(TURN_MS);
                std::this_thread::sleep_until(nextTurn);
            }
        });

        // The radio's sound card, asking for more each time than the FIFO can
        // be filled with, and going by the same decisions the application's
        // callback makes.
        struct RadioTurns
        {
            long asked = 0;       // asks it was given samples for
            long shortTurns = 0;  // asks it was given nothing for
            long withSignal = 0;  // asks it was given something to send
            int peak = 0;         // the loudest sample it was given
        } radio;

        std::thread radioCard([&]() {
            std::vector<short> block(RADIO_BLOCK);
            auto nextTurn = std::chrono::steady_clock::now();
            for (int turn = 0; turn < RADIO_TURNS; turn++)
            {
                // (What MainFrame::OnTxOutAudioData_() does with a block
                // this big: whatever is there, and silence for the rest of
                // its buffer.)
                int available = std::min(g_rxUserdata->outfifo1->numUsed(), RADIO_BLOCK);
                if (available < RADIO_BLOCK)
                {
                    g_outfifo1_empty.fetch_add(1, std::memory_order_relaxed);
                }
                if (available == 0)
                {
                    radio.shortTurns++;
                }
                else if (g_rxUserdata->outfifo1->read(block.data(), available) == 0)
                {
                    int turnPeak = 0;
                    for (int i = 0; i < available; i++)
                    {
                        turnPeak = std::max(turnPeak, std::abs((int)block[i]));
                    }
                    radio.asked++;
                    radio.peak = std::max(radio.peak, turnPeak);
                    if (turnPeak > 1000) radio.withSignal++;
                }
                // The sound card asks at hardware pace regardless of how
                // busy the machine is: turns are deadline-driven.
                nextTurn += std::chrono::milliseconds(RADIO_MS);
                std::this_thread::sleep_until(nextTurn);
            }
        });

        mic.join();
        radioCard.join();
        (*thread).stop();

        if (getenv("FREEDV_TEST_VERBOSE") != nullptr)
        {
            std::cout << std::endl << "    " << radio.asked << " of " << RADIO_TURNS << " asks given samples, "
                      << radio.shortTurns << " given nothing, " << radio.withSignal << " with something to send; peak "
                      << radio.peak << std::endl;
        }

        bool result = true;

        // The asks that landed after the transmission was primed were given
        // the most they could be given, rather than being told there wasn't
        // a whole block and given nothing. The first lands while it's still
        // being primed; a busy machine can keep the priming running into
        // the second ask as well, which is what the allowance of two is for.
        if (radio.asked < RADIO_TURNS - 2)
        {
            std::cout << "[" << RADIO_TURNS - radio.asked << " of " << RADIO_TURNS
                      << " asks were given nothing, expected 2 at most] ";
            result = false;
        }
        // And what it was given was the transmission, not silence.
        if (radio.withSignal < RADIO_TURNS - 2)
        {
            std::cout << "[only " << radio.withSignal << " of " << RADIO_TURNS
                      << " asks were given something to send] ";
            result = false;
        }
        return result;
    }


    // Asked to record and play files that were never opened: nothing is
    // recorded, nothing is played, and the real-time pass carries on.
    static bool playAndRecordFileRequestsWithoutFilesAreIgnored()
    {
        resetGlobals();
        g_half_duplex = true;
        g_tx = false;                      // so the gate is opened by the record flags
        g_recVoiceKeyerFile = true;        // g_sfRecMicFile stays NULL
        g_playFileToMicIn = true;          // g_sfPlayFile stays NULL
        g_recFileFromModulator = true;     // g_sfRecFileFromModulator stays NULL
        FifoGuard fifos;
        seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr,
                          makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        FakeRealtimeHelper helper;
        thread.txProcessing_(&helper);

        bool result = true;
        result &= check("play file reported finished", micInDone.load(), 0);
        result &= check("microphone recording left open", (int)(g_sfRecMicFile.load() != nullptr), 0);
        result &= check("modulator recording left open", (int)(g_sfRecFileFromModulator.load() != nullptr), 0);
        result &= checkBetween("samples still sent to the radio",
                               (int)drain(g_rxUserdata->outfifo1).size(), 1, TEST_SAMPLE_RATE);
        return result;
    }

    static bool microphoneRecordingWithoutFileIsIgnored()
    {
        resetGlobals();
        g_half_duplex = true;
        g_tx = false;
        g_recFileFromMic = true;           // g_sfRecMicFile stays NULL
        FifoGuard fifos;
        seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr,
                          makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        FakeRealtimeHelper helper;
        thread.txProcessing_(&helper);

        bool result = true;
        result &= check("play file reported finished", micInDone.load(), 0);
        result &= check("microphone recording left open", (int)(g_sfRecMicFile.load() != nullptr), 0);
        result &= checkBetween("samples still sent to the radio",
                               (int)drain(g_rxUserdata->outfifo1).size(), 1, TEST_SAMPLE_RATE);
        return result;
    }

    static bool transmitIsSkippedWhenThereIsNoSecondSoundCard()
    {
        resetGlobals();
        g_nSoundCards = 1;
        g_tx = true;
        FifoGuard fifos;
        seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr,
                          makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        FakeRealtimeHelper helper;
        thread.txProcessing_(&helper);

        bool result = true;
        result &= check("samples queued for the radio", g_rxUserdata->outfifo1->numUsed(), 0);
        g_nSoundCards = 2;
        return result;
    }

    static bool stoppedThreadDoesNoRealTimeWork()
    {
        resetGlobals();
        FifoGuard fifos;
        seedSine(g_rxUserdata->infifo1, TEST_SAMPLE_RATE);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr,
                          makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();
        thread.stop();    // clears m_run; no thread was ever started to join

        FakeRealtimeHelper helper;
        thread.runOnce_(&helper);

        bool result = true;
        result &= check("real time work started", helper.startCalls.load(), 0);
        result &= check("real time work stopped", helper.stopCalls.load(), 0);
        result &= check("samples sent to the speaker", g_rxUserdata->outfifo2->numUsed(), 0);
        return result;
    }

    static bool radioRecordAndPlayWithoutFilesAreIgnored()
    {
        resetGlobals();
        g_recFileFromRadio = true;         // g_sfRecFile stays NULL
        g_playFileFromRadio = true;        // g_sfPlayFileFromRadio stays NULL
        g_recFileFromDecoder = true;       // g_sfRecDecoderFile stays NULL
        FifoGuard fifos;

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr,
                          makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        // initializePipeline_ clears the RX FIFOs, so the demodulator is fed
        // afterwards, one second at a time (the FIFOs hold two).
        auto signal = transmittedSignal(4);
        FakeRealtimeHelper helper;
        for (size_t offset = 0; offset < signal.size(); offset += TEST_SAMPLE_RATE)
        {
            g_rxUserdata->infifo1->write(&signal[offset], std::min((size_t)TEST_SAMPLE_RATE, signal.size() - offset));
            thread.rxProcessing_(&helper);
        }

        bool result = true;
        result &= check("radio recording reported finished", recRadioDone.load(), 0);
        result &= check("radio play file reported finished", playRadioDone.load(), 0);
        result &= check("recording handles left open",
                        (int)((g_sfRecFile.load() != nullptr) || (g_sfRecDecoderFile.load() != nullptr)), 0);
        result &= check("speaker still fed", (int)(drain(g_rxUserdata->outfifo2).size() > 0), 1);
        return result;
    }

    static bool voiceKeyerAudioIsNotMonitoredWhenMonitoringIsOff()
    {
        resetGlobals();
        auto micLink = std::make_shared<LinkStep>(TEST_SAMPLE_RATE);
        g_tx = true;                    // keeps the monitor chain live
        g_monitorTxAudio = true;
        g_voice_keyer_tx = true;
        g_monitorVoiceKeyerAudio = false;
        SoundCardFifos fifos(TEST_SAMPLE_RATE);
        seedSine(g_rxUserdata->infifo2, TEST_SAMPLE_RATE);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread txThread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, micLink, nullptr,
                            makeHooks(micInDone, recRadioDone, playRadioDone));
        TxRxThread rxThread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, micLink, nullptr,
                            makeHooks(micInDone, recRadioDone, playRadioDone));
        txThread.initializePipeline_();
        rxThread.initializePipeline_();

        FakeRealtimeHelper helper;
        txThread.txProcessing_(&helper);
        rxThread.rxProcessing_(&helper);

        auto speakerOut = drain(g_rxUserdata->outfifo2);
        bool result = true;
        result &= check("voice keyer audio heard while unmonitored", peak(speakerOut), 0);
        return result;
    }

    static bool radioAudioReachesTheMicrophoneChainWhenRecordingIt()
    {
        resetGlobals();
        auto micLink = std::make_shared<LinkStep>(TEST_SAMPLE_RATE);
        std::vector<short> signal;

        // Recording the voice keyer's file keeps received audio in the mic chain.
        g_recVoiceKeyerFile = true;
        {
            SoundCardFifos fifos(TEST_SAMPLE_RATE);
            signal = transmittedSignal(4);
            std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
            TxRxThread thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, micLink, nullptr,
                              makeHooks(micInDone, recRadioDone, playRadioDone));
            thread.initializePipeline_();
            FakeRealtimeHelper helper;
            for (size_t offset = 0; offset < signal.size(); offset += TEST_SAMPLE_RATE)
            {
                g_rxUserdata->infifo1->write(&signal[offset], std::min((size_t)TEST_SAMPLE_RATE, signal.size() - offset));
                thread.rxProcessing_(&helper);
            }
            g_recVoiceKeyerFile = false;
        }

        // Voice keyer off, not transmitting, analog off, nothing recording:
        // every group of the keep-it-out predicate is false, so the radio
        // audio is demodulated and heard.
        g_analog = 0;
        g_voice_keyer_tx = false;
        g_monitorVoiceKeyerAudio = false;
        g_tx = false;
        g_recVoiceKeyerFile = false;
        SoundCardFifos fifos(TEST_SAMPLE_RATE);

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, micLink, nullptr,
                          makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();
        FakeRealtimeHelper helper;
        for (size_t offset = 0; offset < signal.size(); offset += TEST_SAMPLE_RATE)
        {
            g_rxUserdata->infifo1->write(&signal[offset], std::min((size_t)TEST_SAMPLE_RATE, signal.size() - offset));
            thread.rxProcessing_(&helper);
        }

        auto speakerOut = drain(g_rxUserdata->outfifo2);
        bool result = true;
        result &= check("received audio demodulated", (int)(peak(speakerOut) > 0), 1);
        return result;
    }

    static bool analogAudioReachesTheSpeakerWithoutAMicLink()
    {
        resetGlobals();
        g_analog = 1;                    // the no-mic-link predicate's only operand
        FifoGuard fifos;

        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(false, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr,
                          makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();

        // initializePipeline_ clears the RX FIFOs, so seed after it.
        seedSine(g_rxUserdata->infifo1, TEST_SAMPLE_RATE);

        FakeRealtimeHelper helper;
        thread.rxProcessing_(&helper);

        bool result = true;
        result &= checkBetween("loudest speaker sample of analog audio",
                               peak(drain(g_rxUserdata->outfifo2)), 1, 32767);
        g_analog = 0;
        return result;
    }

    static bool endOfOverIsKeptWhenThereIsNoRoomForIt()
    {
        constexpr short MARKER = 12345;

        resetGlobals();
        g_tx = true;
        endingTx = true;
        FifoGuard fifos;


        std::atomic<int> micInDone{0}, recRadioDone{0}, playRadioDone{0};
        TxRxThread thread(true, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE, nullptr, nullptr,
                          makeHooks(micInDone, recRadioDone, playRadioDone));
        thread.initializePipeline_();
        const int nsam = (freedvInterface.getTxNNomModemSamples() * TEST_SAMPLE_RATE)
                         / freedvInterface.getTxModemSampleRate();
        const int minimumAudio = std::min(200 * TEST_SAMPLE_RATE / 1000,
                                          (int)(g_rxUserdata->infifo2->capacity() * 90 / 100));

        // Fill the radio FIFO with markers, leaving room for a few speech
        // frames plus the waiting end-of-over batch.
        const int room = 4 * nsam + (nsam + 100);
        std::vector<short> queued(g_rxUserdata->outfifo1->capacity() - room, MARKER);
        g_rxUserdata->outfifo1->write(queued.data(), (int)queued.size());
        seedSine(g_rxUserdata->infifo2, minimumAudio);
        // More end-of-over waiting than the room left after those frames.
        thread.pendingEooCount_ = nsam + 100;
        std::fill(thread.pendingEooSamples_.get(),
                  thread.pendingEooSamples_.get() + thread.pendingEooCount_, MARKER);

        FakeRealtimeHelper helper;
        helper.stopWork = true;
        thread.txProcessing_(&helper);       // the radio FIFO isn't drained, so nothing is sent

        bool result = true;
        result &= check("end of over reported as queued", (int)g_eoo_enqueued.load(), 0);
        result &= check("end of over still waiting", (int)(thread.pendingEooCount_ > 0), 1);
        result &= onlyContains(drain(g_rxUserdata->outfifo1), MARKER)
                      ? true : (std::cout << "[speech written over the queued markers] ", false);

        // With room again it is sent, once, from the pending batch.
        drain(g_rxUserdata->outfifo1);
        helper.stopWork = false;
        thread.txProcessing_(&helper);
        result &= check("end of over reported as queued now", (int)g_eoo_enqueued.load(), 1);
        result &= check("end of over no longer waiting", (int)thread.pendingEooCount_, 0);
        endingTx = false;
        return result;
    }
};

int main()
{
    // Logging allocates memory the first time it's used on a thread, which
    // isn't allowed on the real-time paths that some of these tests then log
    // from (with g_dump_fifo_state set). Get that over with here.
    log_info("Starting TxRxThread tests");

    freedvInterface.start(100, false);

    executeTestCase("txProcessingSkippedWhenHalfDuplexPttOff", &TxRxThreadTest::txProcessingSkippedWhenHalfDuplexPttOff);
    executeTestCase("txProcessingGeneratesModemOutputWhenPttOn", &TxRxThreadTest::txProcessingGeneratesModemOutputWhenPttOn);
    executeTestCase("enteringTxAfterRxResetsPipelineAndSkipsOneCycle", &TxRxThreadTest::enteringTxAfterRxResetsPipelineAndSkipsOneCycle);
    executeTestCase("rxProcessingDemodulatesToOutfifo1ForSingleSoundCard", &TxRxThreadTest::rxProcessingDemodulatesToOutfifo1ForSingleSoundCard);
    executeTestCase("rxProcessingWritesToOutfifo2ForDualSoundCards", &TxRxThreadTest::rxProcessingWritesToOutfifo2ForDualSoundCards);
    executeTestCase("runOnceRequestsFastModeWhenOutputFifoLow", &TxRxThreadTest::runOnceRequestsFastModeWhenOutputFifoLow);
    executeTestCase("clearFifosResetsTxFifosAndSelectedRxFifo", &TxRxThreadTest::clearFifosResetsTxFifosAndSelectedRxFifo);
    executeTestCase("playFileToMicInFinishedHookFiresWhenFileEnds", &TxRxThreadTest::playFileToMicInFinishedHookFiresWhenFileEnds);
    executeTestCase("monitoringTransmitAudioPlaysMicThroughSpeaker", &TxRxThreadTest::monitoringTransmitAudioPlaysMicThroughSpeaker);
    executeTestCase("monitoredTransmitAudioFollowsItsVolumeSetting", &TxRxThreadTest::monitoredTransmitAudioFollowsItsVolumeSetting);
    executeTestCase("monitoredVoiceKeyerAudioFollowsItsOwnVolumeSetting", &TxRxThreadTest::monitoredVoiceKeyerAudioFollowsItsOwnVolumeSetting);
    executeTestCase("micAudioStaysOutOfSpeakerWhenNotMonitoring", &TxRxThreadTest::micAudioStaysOutOfSpeakerWhenNotMonitoring);
    executeTestCase("speakerIsSilentWhileRecordingVoiceKeyerFile", &TxRxThreadTest::speakerIsSilentWhileRecordingVoiceKeyerFile);
    executeTestCase("clearFifosAlsoClearsMonitoredMicAudio", &TxRxThreadTest::clearFifosAlsoClearsMonitoredMicAudio);
    executeTestCase("playFileToMicInStartsOverWhenLooping", &TxRxThreadTest::playFileToMicInStartsOverWhenLooping);
    executeTestCase("playFileFromRadioFinishedHookFiresWhenFileEnds", &TxRxThreadTest::playFileFromRadioFinishedHookFiresWhenFileEnds);
    executeTestCase("playFileFromRadioStartsOverWhenLooping", &TxRxThreadTest::playFileFromRadioStartsOverWhenLooping);
    executeTestCase("recordingFromRadioContinuesUntilStopped", &TxRxThreadTest::recordingFromRadioContinuesUntilStopped);
    executeTestCase("txProcessingDropsOutputWhenRadioFifoIsFull", &TxRxThreadTest::txProcessingDropsOutputWhenRadioFifoIsFull);
    executeTestCase("radioRecordingStopsWhenItsBudgetIsUsedUp", &TxRxThreadTest::radioRecordingStopsWhenItsBudgetIsUsedUp);
    executeTestCase("timeOutWarningBeepsThenTurnsItselfOff", &TxRxThreadTest::timeOutWarningBeepsThenTurnsItselfOff);
    executeTestCase("interferingToneIsAddedToReceivedAudio", &TxRxThreadTest::interferingToneIsAddedToReceivedAudio);
    executeTestCase("interferingToneFollowsItsFrequencyAndAmplitudeSettings", &TxRxThreadTest::interferingToneFollowsItsFrequencyAndAmplitudeSettings);
    executeTestCase("noInterferingToneUnlessEnabled", &TxRxThreadTest::noInterferingToneUnlessEnabled);
    executeTestCase("speakerPlaysWhatIsReceived", &TxRxThreadTest::speakerPlaysWhatIsReceived);
    executeTestCase("speakerIsMutedWhileTransmittingHalfDuplex", &TxRxThreadTest::speakerIsMutedWhileTransmittingHalfDuplex);
    executeTestCase("speakerKeepsPlayingWhileTransmittingFullDuplex", &TxRxThreadTest::speakerKeepsPlayingWhileTransmittingFullDuplex);
    executeTestCase("speakerStaysMutedUntilRadioIsBackOnReceive", &TxRxThreadTest::speakerStaysMutedUntilRadioIsBackOnReceive);
    executeTestCase("speakerDoesNotWaitForRadioInFullDuplex", &TxRxThreadTest::speakerDoesNotWaitForRadioInFullDuplex);
    executeTestCase("speakerIsMutedWhileVoiceKeyerTransmits", &TxRxThreadTest::speakerIsMutedWhileVoiceKeyerTransmits);
    executeTestCase("recordsMicrophoneAudio", &TxRxThreadTest::recordsMicrophoneAudio);
    executeTestCase("recordsWhatIsSentToRadio", &TxRxThreadTest::recordsWhatIsSentToRadio);
    executeTestCase("recordsWhatIsSentToSpeaker", &TxRxThreadTest::recordsWhatIsSentToSpeaker);
    executeTestCase("nothingIsRecordedUnlessAskedFor", &TxRxThreadTest::nothingIsRecordedUnlessAskedFor);
    executeTestCase("automaticGainControlRaisesQuietMicrophone", &TxRxThreadTest::automaticGainControlRaisesQuietMicrophone);
    executeTestCase("noiseReductionQuietensNoise", &TxRxThreadTest::noiseReductionQuietensNoise);
    executeTestCase("stopsProcessingWhenToldTimeIsUp", &TxRxThreadTest::stopsProcessingWhenToldTimeIsUp);
    executeTestCase("endOfOverIsSentOnceWhenTransmitEnds", &TxRxThreadTest::endOfOverIsSentOnceWhenTransmitEnds);
    executeTestCase("startStopRunsRealTimeHelperLifecycle", &TxRxThreadTest::startStopRunsRealTimeHelperLifecycle);
    executeTestCase("transmitterPrimesWithSpeechBeforeItsFirstFrame", &TxRxThreadTest::transmitterPrimesWithSpeechBeforeItsFirstFrame);
    executeTestCase("transmitterSendsOnlyWhatTheMicrophoneMade", &TxRxThreadTest::transmitterSendsOnlyWhatTheMicrophoneMade);
    executeTestCase("transmitterFeedsASoundCardAskingForMoreThanTheFifoHolds", &TxRxThreadTest::transmitterFeedsASoundCardAskingForMoreThanTheFifoHolds);
    executeTestCase("toneIsUnbrokenWhenSoundCardsKeepTime", &TxRxThreadTest::toneIsUnbrokenWhenSoundCardsKeepTime);
    executeTestCase("toneOnlyBreaksWhereASoundCardStalled", &TxRxThreadTest::toneOnlyBreaksWhereASoundCardStalled);
    executeTestCase("speechSurvivesRoundTrip", &TxRxThreadTest::speechSurvivesRoundTrip);
    executeTestCase("speechSurvivesRoundTripInBlocksOfAnySize", &TxRxThreadTest::speechSurvivesRoundTripInBlocksOfAnySize);
    executeTestCase("speechSurvivesRoundTripThroughFiles", &TxRxThreadTest::speechSurvivesRoundTripThroughFiles);
    executeTestCase("playAndRecordFileRequestsWithoutFilesAreIgnored", &TxRxThreadTest::playAndRecordFileRequestsWithoutFilesAreIgnored);
    executeTestCase("microphoneRecordingWithoutFileIsIgnored", &TxRxThreadTest::microphoneRecordingWithoutFileIsIgnored);
    executeTestCase("transmitIsSkippedWhenThereIsNoSecondSoundCard", &TxRxThreadTest::transmitIsSkippedWhenThereIsNoSecondSoundCard);
    executeTestCase("stoppedThreadDoesNoRealTimeWork", &TxRxThreadTest::stoppedThreadDoesNoRealTimeWork);
    executeTestCase("radioRecordAndPlayWithoutFilesAreIgnored", &TxRxThreadTest::radioRecordAndPlayWithoutFilesAreIgnored);
    executeTestCase("voiceKeyerAudioIsNotMonitoredWhenMonitoringIsOff", &TxRxThreadTest::voiceKeyerAudioIsNotMonitoredWhenMonitoringIsOff);
    executeTestCase("radioAudioReachesTheMicrophoneChainWhenRecordingIt", &TxRxThreadTest::radioAudioReachesTheMicrophoneChainWhenRecordingIt);
    executeTestCase("analogAudioReachesTheSpeakerWithoutAMicLink", &TxRxThreadTest::analogAudioReachesTheSpeakerWithoutAMicLink);
    executeTestCase("endOfOverIsKeptWhenThereIsNoRoomForIt", &TxRxThreadTest::endOfOverIsKeptWhenThereIsNoRoomForIt);

    freedvInterface.stop();

    return testResult();
}
