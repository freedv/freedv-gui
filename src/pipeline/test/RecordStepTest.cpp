#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

#include <sndfile.h>

#include <wx/init.h>
#include <wx/thread.h>

#include "../RecordStep.h"
#include "../../util/logging/ulog.h"
#include "../../test/UnitTestCommon.h"

// Normally provided by the main application.
wxMutex g_mutexProtectingCallbackData;
std::mutex g_mutexProtectingRecordFiles;

namespace {

constexpr int SAMPLE_RATE = 8000;
constexpr int BLOCK_SIZE = 160;

std::vector<short> ramp(int numSamples, int start)
{
    std::vector<short> result(numSamples);
    for (int index = 0; index < numSamples; index++)
    {
        result[index] = (short)(start + index);
    }
    return result;
}

// A recording that's in progress, along with the step doing the recording.
// The file's removed once the test case is done.
class Recording
{
public:
    explicit Recording(bool fileAvailable = true)
    {
        path_ = unitTestTempPath("RecordStepTest", "-" + std::to_string(NextId_++) + ".wav");

        SF_INFO info = {};
        info.samplerate = SAMPLE_RATE;
        info.channels = 1;
        info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
        file_ = sf_open(path_.c_str(), SFM_WRITE, &info);
        if (fileAvailable) activeFile_ = file_;

        step_ = std::make_unique<RecordStep>(
            SAMPLE_RATE,
            [this]() { return activeFile_.load(); },
            [this](int numSamples) { 
                numCallbacks++; 
                numSamplesReported += numSamples; 
            });
    }

    ~Recording()
    {
        finish();
        remove(path_.c_str());
    }

    RecordStep& step() { return *step_; }

    // Equivalent to the user starting or stopping recording; as in the
    // application, this is done with the record-file lock held.
    void setFileAvailable(bool available)
    {
        std::unique_lock<std::mutex> lkRecordFiles(g_mutexProtectingRecordFiles);
        activeFile_ = available ? file_ : nullptr;
    }

    // Queues samples and then stops recording before the file thread can
    // write them out: it's held at the record-file lock throughout.
    void recordThenStop(std::vector<short> samples)
    {
        std::unique_lock<std::mutex> lkRecordFiles(g_mutexProtectingRecordFiles);
        record(std::move(samples));
        activeFile_ = nullptr;
    }

    // Returns true if the step behaved as a pipeline sink.
    bool record(std::vector<short> samples)
    {
        short empty = 0;
        int numOutput = -1;
        short* output = step_->execute(samples.empty() ? &empty : samples.data(), samples.size(), &numOutput);
        return output == nullptr && numOutput == 0;
    }

    // Stops recording and returns what ended up in the file.
    std::vector<short> finish()
    {
        step_ = nullptr;
        if (file_ != nullptr)
        {
            sf_close(file_);
            file_ = nullptr;
            activeFile_ = nullptr;
        }

        SF_INFO info = {};
        SNDFILE* file = sf_open(path_.c_str(), SFM_READ, &info);
        if (file == nullptr) return {};

        std::vector<short> result(info.frames);
        result.resize(sf_read_short(file, result.data(), result.size()));
        sf_close(file);
        return result;
    }

    std::atomic<int> numCallbacks { 0 };
    std::atomic<int> numSamplesReported { 0 };

private:
    static int NextId_;
    std::string path_;
    SNDFILE* file_ = nullptr;
    std::atomic<SNDFILE*> activeFile_ { nullptr };
    std::unique_ptr<RecordStep> step_;
};

int Recording::NextId_ = 0;

bool samplesAre(std::vector<short> const& actual, std::vector<short> const& expected)
{
    if (actual == expected) return true;
    std::cout << "[recorded " << actual.size() << " samples";
    if (!actual.empty()) std::cout << " starting with " << actual[0];
    std::cout << ", expected " << expected.size();
    if (!expected.empty()) std::cout << " starting with " << expected[0];
    std::cout << "] ";
    return false;
}

bool sampleRatesMatchConstructor()
{
    Recording recording;
    return
        recording.step().getInputSampleRate() == SAMPLE_RATE &&
        recording.step().getOutputSampleRate() == SAMPLE_RATE;
}

bool producesNoOutput()
{
    Recording recording;
    return recording.record(ramp(BLOCK_SIZE, 1)) && recording.record({});
}

bool recordsSamplesInOrder()
{
    Recording recording;

    // Let each burst reach the file before queuing the next so that this is
    // about ordering rather than how much can be queued at once (a fixed sleep
    // isn't enough when the writer thread is starved, as on Windows runners).
    std::vector<short> expected;
    for (int block = 0; block < 100; block++)
    {
        auto samples = ramp(BLOCK_SIZE, block * BLOCK_SIZE);
        expected.insert(expected.end(), samples.begin(), samples.end());
        recording.record(samples);
        if (block % 10 == 9)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (recording.numSamplesReported < (int)expected.size() &&
                   std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    return samplesAre(recording.finish(), expected);
}

bool recordsBlocksOfVaryingSize()
{
    Recording recording;

    std::vector<short> expected;
    int start = 0;
    for (int size : { 1, 160, 7, 1024, 320, 3, 2000, 480 })
    {
        auto samples = ramp(size, start);
        start += size;
        expected.insert(expected.end(), samples.begin(), samples.end());
        recording.record(samples);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return samplesAre(recording.finish(), expected);
}

bool flushesRemainderWhenDestroyed()
{
    // Nothing gives the file thread time to run here, so most of this is
    // still queued when the step goes away.
    for (int iteration = 0; iteration < 20; iteration++)
    {
        Recording recording;
        auto samples = ramp(BLOCK_SIZE * 10, iteration);
        recording.record(samples);
        if (!samplesAre(recording.finish(), samples)) return false;
    }
    return true;
}

bool reportsNumberOfSamplesWritten()
{
    Recording recording;
    for (int block = 0; block < 20; block++)
    {
        recording.record(ramp(BLOCK_SIZE, block));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    auto recorded = recording.finish();
    if (recording.numSamplesReported != (int)recorded.size() || recording.numCallbacks == 0)
    {
        std::cout << "[reported " << recording.numSamplesReported << " samples in " << recording.numCallbacks
                  << " call(s), file has " << recorded.size() << "] ";
        return false;
    }
    return recorded.size() == 20 * BLOCK_SIZE;
}

bool nothingReportedWhenNothingRecorded()
{
    Recording recording;
    recording.record({});
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return recording.finish().empty() && recording.numCallbacks == 0;
}

bool writesNothingWithoutAFile()
{
    Recording recording(false);
    recording.record(ramp(BLOCK_SIZE, 1));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return recording.numCallbacks == 0;
}

bool audioQueuedBeforeFileIsAvailableIsRecorded()
{
    Recording recording(false);

    // Half a second's worth.
    auto samples = ramp(SAMPLE_RATE / 2, 1);
    recording.record(samples);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    recording.setFileAvailable(true);
    return samplesAre(recording.finish(), samples);
}

bool dropsWholeBlocksWhenQueueIsFull()
{
    Recording recording(false);

    // With nowhere to write to, a second and a half of audio can't all be held on to.
    std::vector<short> sent;
    for (int block = 0; block < SAMPLE_RATE * 3 / 2 / BLOCK_SIZE; block++)
    {
        auto samples = ramp(BLOCK_SIZE, block * BLOCK_SIZE);
        sent.insert(sent.end(), samples.begin(), samples.end());
        recording.record(samples);
    }

    // What was kept is recorded intact: the audio from before the queue
    // filled up, with no partial blocks.
    recording.setFileAvailable(true);
    auto recorded = recording.finish();
    if (recorded.empty() || recorded.size() >= sent.size() || recorded.size() % BLOCK_SIZE != 0)
    {
        std::cout << "[recorded " << recorded.size() << " of " << sent.size() << " samples] ";
        return false;
    }
    return samplesAre(recorded, std::vector<short>(sent.begin(), sent.begin() + recorded.size()));
}

bool recordsAgainAfterQueueWasFull()
{
    Recording recording(false);
    for (int block = 0; block < SAMPLE_RATE * 3 / 2 / BLOCK_SIZE; block++)
    {
        recording.record(ramp(BLOCK_SIZE, 0));
    }

    recording.setFileAvailable(true);
    recording.record({});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Distinguishable from everything queued earlier.
    auto later = ramp(BLOCK_SIZE, 20000);
    recording.record(later);

    auto recorded = recording.finish();
    if (recorded.size() < later.size()) return false;
    return samplesAre(std::vector<short>(recorded.end() - later.size(), recorded.end()), later);
}

bool resetDiscardsQueuedAudio()
{
    Recording recording(false);
    recording.record(ramp(BLOCK_SIZE * 10, 1));
    recording.step().reset();

    recording.setFileAvailable(true);
    auto later = ramp(BLOCK_SIZE, 20000);
    recording.record(later);
    return samplesAre(recording.finish(), later);
}

bool stopsWritingWhenFileGoesAway()
{
    Recording recording;
    auto before = ramp(BLOCK_SIZE * 5, 1);
    recording.record(before);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    recording.setFileAvailable(false);
    recording.record(ramp(BLOCK_SIZE * 5, 10000));
    return samplesAre(recording.finish(), before);
}

bool destroysCleanly()
{
    for (int iteration = 0; iteration < 50; iteration++)
    {
        Recording recording(iteration % 2 == 0);
        if (iteration % 3 == 0) recording.record(ramp(BLOCK_SIZE, 1));
    }
    return true;
}

// A new recording holds what was recorded for it, not the tail of the one
// before. The samples queued for the previous file can't be written to it
// any more -- the application closes that file when the recording stops --
// and they're no part of the new recording either.
bool staleAudioFromAPreviousRecordingIsLeftOutOfTheNextOne()
{
    auto firstPath = unitTestTempPath("RecordStepTest", "-stale-first.wav");
    auto secondPath = unitTestTempPath("RecordStepTest", "-stale-second.wav");

    SF_INFO info = {};
    info.samplerate = SAMPLE_RATE;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE* firstFile = sf_open(firstPath.c_str(), SFM_WRITE, &info);
    SNDFILE* secondFile = sf_open(secondPath.c_str(), SFM_WRITE, &info);
    if (firstFile == nullptr || secondFile == nullptr)
    {
        std::cout << "[could not open the test files] ";
        return false;
    }

    std::atomic<SNDFILE*> activeFile{firstFile};
    auto step = std::make_unique<RecordStep>(
        SAMPLE_RATE, [&activeFile]() { return activeFile.load(); }, [](int) {});

    auto record = [&](int start) {
        auto samples = ramp(BLOCK_SIZE, start);
        int numOutput = 0;
        step->execute(samples.data(), samples.size(), &numOutput);
    };

    // A recording in progress, stopped with samples of its own still
    // queued: the file thread is held at the lock the application takes
    // when it detaches a record file, so it can't write them out first.
    {
        std::unique_lock<std::mutex> lkRecordFiles(g_mutexProtectingRecordFiles);
        for (int block = 0; block < 3; block++)
        {
            record(block * BLOCK_SIZE);
        }

        // The user stops recording, and the application detaches the file.
        activeFile.store(nullptr);
    }

    // The file thread runs, finds no file to write to, and leaves them
    // where they are.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // A new recording starts, with a new file.
    activeFile.store(secondFile);
    std::vector<short> expected;
    for (int block = 0; block < 3; block++)
    {
        auto samples = ramp(BLOCK_SIZE, 2000 + block * BLOCK_SIZE);
        expected.insert(expected.end(), samples.begin(), samples.end());
        record(2000 + block * BLOCK_SIZE);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Ends the file thread, which writes whatever's still queued.
    step = nullptr;

    sf_close(firstFile);
    sf_close(secondFile);

    SF_INFO readInfo = {};
    SNDFILE* recorded = sf_open(secondPath.c_str(), SFM_READ, &readInfo);
    if (recorded == nullptr)
    {
        std::cout << "[the second file could not be read back] ";
        return false;
    }
    std::vector<short> actual(readInfo.frames);
    actual.resize(sf_read_short(recorded, actual.data(), actual.size()));
    sf_close(recorded);

    remove(firstPath.c_str());
    remove(secondPath.c_str());

    return samplesAre(actual, expected);
}

// As above, but with the file thread never having seen the first file, and
// the next one attached a while before the next block of audio: the file
// thread has a file to write to, and the last recording's samples queued,
// before execute() has had a chance to drop them.
bool staleAudioIsLeftOutWhenTheNextFileIsAttachedBeforeMoreAudio()
{
    auto firstPath = unitTestTempPath("RecordStepTest", "-early-first.wav");
    auto secondPath = unitTestTempPath("RecordStepTest", "-early-second.wav");

    SF_INFO info = {};
    info.samplerate = SAMPLE_RATE;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE* firstFile = sf_open(firstPath.c_str(), SFM_WRITE, &info);
    SNDFILE* secondFile = sf_open(secondPath.c_str(), SFM_WRITE, &info);
    if (firstFile == nullptr || secondFile == nullptr)
    {
        std::cout << "[could not open the test files] ";
        return false;
    }

    // No file yet, so the file thread's first look finds none.
    std::atomic<SNDFILE*> activeFile{nullptr};
    auto step = std::make_unique<RecordStep>(
        SAMPLE_RATE, [&activeFile]() { return activeFile.load(); }, [](int) {});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto record = [&](int start) {
        auto samples = ramp(BLOCK_SIZE, start);
        int numOutput = 0;
        step->execute(samples.data(), samples.size(), &numOutput);
    };

    // A whole recording goes by with the file thread held at the lock, so
    // it never sees the first file.
    {
        std::unique_lock<std::mutex> lkRecordFiles(g_mutexProtectingRecordFiles);
        activeFile.store(firstFile);
        for (int block = 0; block < 3; block++)
        {
            record(block * BLOCK_SIZE);
        }
        activeFile.store(nullptr);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // The next recording's file is attached, and the file thread looks
    // again (it does every FILE_CHECK_INTERVAL_MS) before any more audio.
    activeFile.store(secondFile);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<short> expected;
    for (int block = 0; block < 3; block++)
    {
        auto samples = ramp(BLOCK_SIZE, 2000 + block * BLOCK_SIZE);
        expected.insert(expected.end(), samples.begin(), samples.end());
        record(2000 + block * BLOCK_SIZE);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Ends the file thread, which writes whatever's still queued.
    step = nullptr;

    sf_close(firstFile);
    sf_close(secondFile);

    SF_INFO readInfo = {};
    SNDFILE* recorded = sf_open(secondPath.c_str(), SFM_READ, &readInfo);
    if (recorded == nullptr)
    {
        std::cout << "[the second file could not be read back] ";
        return false;
    }
    std::vector<short> actual(readInfo.frames);
    actual.resize(sf_read_short(recorded, actual.data(), actual.size()));
    sf_close(recorded);

    remove(firstPath.c_str());
    remove(secondPath.c_str());

    return samplesAre(actual, expected);
}

// As above, but the next recording's SNDFILE lands at the address the last
// one had -- as sf_open() after sf_close() is apt to do, since it asks for a
// block of the same size. The two can't be told apart by their handles.
bool staleAudioIsLeftOutEvenWhenTheNextFileHasTheSameHandle()
{
    Recording recording;

    // Recorded and written out while the first recording is going.
    auto first = ramp(BLOCK_SIZE * 3, 1);
    recording.record(first);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // The tail of that recording, still queued when it stops.
    recording.recordThenStop(ramp(BLOCK_SIZE * 3, 5000));

    // Between recordings: nothing calls execute(), as in the application,
    // where the tap is switched off while there's no file.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // The next recording, with the same handle.
    recording.setFileAvailable(true);
    auto second = ramp(BLOCK_SIZE * 3, 10000);
    recording.record(second);

    std::vector<short> expected = first;
    expected.insert(expected.end(), second.begin(), second.end());
    return samplesAre(recording.finish(), expected);
}

// Recording doesn't take the mutex the UI thread uses for the callback
// data: the write to disk it does would otherwise hold up the window for as
// long as the disk takes. The recording has to keep going while the UI
// thread is holding that one.
bool recordingDoesNotWaitForTheUiCallbackMutex()
{
    Recording recording;

    g_mutexProtectingCallbackData.Lock();

    int expectedSamples = 0;
    for (int block = 0; block < 10; block++)
    {
        auto samples = ramp(BLOCK_SIZE, block * BLOCK_SIZE);
        expectedSamples += samples.size();
        recording.record(samples);
    }

    // Whatever the file thread managed to write while the UI thread was
    // holding its mutex.
    auto giveUpAt = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (recording.numSamplesReported < expectedSamples &&
           std::chrono::steady_clock::now() < giveUpAt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    bool keptGoing = recording.numSamplesReported >= expectedSamples;

    g_mutexProtectingCallbackData.Unlock();

    if (!keptGoing)
    {
        std::cout << "[wrote " << recording.numSamplesReported << " of " << expectedSamples
                  << " samples while the UI mutex was held] ";
    }
    return keptGoing;
}

}

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk())
    {
        std::cout << "Could not initialize wxWidgets" << std::endl;
        return -1;
    }

    ulog_set_quiet(true);

    // RecordStep::execute() logs when its queue fills up. The first log call on a
    // thread can allocate that thread's local storage (it does on macOS), which
    // RealtimeSanitizer rejects inside a real-time function. Log once from here so
    // that's already done by the time execute() runs on this thread.
    log_info("Starting RecordStepTest");

    executeTestCase("sampleRatesMatchConstructor", sampleRatesMatchConstructor);
    executeTestCase("producesNoOutput", producesNoOutput);
    executeTestCase("recordsSamplesInOrder", recordsSamplesInOrder);
    executeTestCase("recordsBlocksOfVaryingSize", recordsBlocksOfVaryingSize);
    executeTestCase("flushesRemainderWhenDestroyed", flushesRemainderWhenDestroyed);
    executeTestCase("reportsNumberOfSamplesWritten", reportsNumberOfSamplesWritten);
    executeTestCase("nothingReportedWhenNothingRecorded", nothingReportedWhenNothingRecorded);
    executeTestCase("writesNothingWithoutAFile", writesNothingWithoutAFile);
    executeTestCase("audioQueuedBeforeFileIsAvailableIsRecorded", audioQueuedBeforeFileIsAvailableIsRecorded);
    executeTestCase("dropsWholeBlocksWhenQueueIsFull", dropsWholeBlocksWhenQueueIsFull);
    executeTestCase("recordsAgainAfterQueueWasFull", recordsAgainAfterQueueWasFull);
    executeTestCase("resetDiscardsQueuedAudio", resetDiscardsQueuedAudio);
    executeTestCase("stopsWritingWhenFileGoesAway", stopsWritingWhenFileGoesAway);
    executeTestCase("staleAudioFromAPreviousRecordingIsLeftOutOfTheNextOne",
                    staleAudioFromAPreviousRecordingIsLeftOutOfTheNextOne);
    executeTestCase("staleAudioIsLeftOutWhenTheNextFileIsAttachedBeforeMoreAudio",
                    staleAudioIsLeftOutWhenTheNextFileIsAttachedBeforeMoreAudio);
    executeTestCase("staleAudioIsLeftOutEvenWhenTheNextFileHasTheSameHandle",
                    staleAudioIsLeftOutEvenWhenTheNextFileHasTheSameHandle);
    executeTestCase("recordingDoesNotWaitForTheUiCallbackMutex", recordingDoesNotWaitForTheUiCallbackMutex);
    executeTestCase("destroysCleanly", destroysCleanly);
    return testResult();
}
