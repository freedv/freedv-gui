//=========================================================================
// Name:            RecordStep.cpp
// Purpose:         Describes a record step in the audio pipeline.
//
// Authors:         Mooneer Salem
// License:
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// - Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// - Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
// OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//=========================================================================

#include "RecordStep.h"

#include <chrono>
#include <mutex>
#include "wx/thread.h"

#if defined(__APPLE__)
#include <pthread.h>
#endif // defined(__APPLE__)

#include "../os/os_interface.h"
#include "../util/logging/ulog.h"

extern std::mutex g_mutexProtectingRecordFiles;

using namespace std::chrono_literals;

RecordStep::RecordStep(
    int inputSampleRate, std::function<SNDFILE*()> getSndFileFn, 
    std::function<void(int)> isFileCompleteFn)
    : inputSampleRate_(inputSampleRate)
    , getSndFileFn_(std::move(getSndFileFn))
    , isFileCompleteFn_(std::move(isFileCompleteFn))
    , inputFifo_(inputSampleRate_)
    , fileIoThreadEnding_(false)
    , consecutiveWriteDrops_(0)
{
    fileIoThread_ = std::thread(std::bind(&RecordStep::fileIoThreadEntry_, this));
}

RecordStep::~RecordStep()
{
    fileIoThreadEnding_.store(true, std::memory_order_release);
    fileIoThreadSem_.signal();
    if (fileIoThread_.joinable())
    {
        fileIoThread_.join();
    }
}

int RecordStep::getInputSampleRate() const FREEDV_NONBLOCKING
{
    return inputSampleRate_;
}

int RecordStep::getOutputSampleRate() const FREEDV_NONBLOCKING
{
    return inputSampleRate_;
}

short* RecordStep::execute(short* inputSamples, int numInputSamples, int* numOutputSamples) FREEDV_NONBLOCKING
{
    // What's queued here belongs to the file being recorded to. If that has
    // changed since the last block was queued -- one recording stopped, or
    // another started while the first's samples were still waiting to be
    // written -- what's left in the queue is the tail of the recording that
    // just ended and can no longer be written to its file, so it goes here
    // rather than turning up at the front of the next recording. A file that
    // has only just appeared doesn't count as a change: audio queued before
    // it was opened is meant to be recorded.
    auto recordFile = getSndFileFn_();
    auto queuedFor = queuedForFile_.load(std::memory_order_relaxed);
    if (queuedFor != nullptr && recordFile != queuedFor)
    {
        inputFifo_.reset();
    }
    queuedForFile_.store(recordFile, std::memory_order_release);

    // write() is all-or-nothing, so a full FIFO means this entire chunk is missing from
    // the resulting file. That silently makes the recording an unfaithful record of what
    // actually flowed through the pipeline, so say so rather than dropping it quietly.
    // The FIFO is drained by a Utility-QoS thread, which is exactly what gets starved
    // first when the machine is loaded.
    if (inputFifo_.write(inputSamples, numInputSamples) != 0)
    {
        if (consecutiveWriteDrops_ == 0)
        {
            FREEDV_BEGIN_VERIFIED_SAFE
            log_warn("Record FIFO full, dropping %d samples from recording (free=%d)", numInputSamples, inputFifo_.numFree());
            FREEDV_END_VERIFIED_SAFE
        }
        consecutiveWriteDrops_++;
    }
    else if (consecutiveWriteDrops_ > 0)
    {
        FREEDV_BEGIN_VERIFIED_SAFE
        log_warn("Record FIFO drops ended after %d call(s); recording is missing audio", consecutiveWriteDrops_);
        FREEDV_END_VERIFIED_SAFE
        consecutiveWriteDrops_ = 0;
    }

    fileIoThreadSem_.signal();

    *numOutputSamples = 0;
    return nullptr;
}

void RecordStep::reset() FREEDV_NONBLOCKING
{
    inputFifo_.reset();
}

void RecordStep::drainFifoToFile_(short* buf)
{
    // The mutex the application takes when it detaches a record file, so
    // that a close can't leave this writing into a freed SNDFILE. It's
    // deliberately not g_mutexProtectingCallbackData: the write below blocks
    // on the disk, and that mutex is what the UI thread takes to touch the
    // callback data, so holding it here lets a slow disk stall the window.
    std::unique_lock<std::mutex> lkRecordFiles(g_mutexProtectingRecordFiles);
    auto recordFile = getSndFileFn_();
    auto queuedFor = queuedForFile_.load(std::memory_order_acquire);
    if (recordFile != nullptr && queuedFor != nullptr && queuedFor != recordFile)
    {
        // What's queued was queued for another file: the tail of a
        // recording that has since ended, with the next one attached before
        // execute() has run again to notice. Writing it would put the old
        // recording's audio at the front of the new one, so it's left for
        // execute() to drop when it next runs.
        drainHadFile_ = true;
    }
    else if (recordFile != nullptr)
    {
        int numInputSamples = inputFifo_.numUsed();
        if (numInputSamples > 0)
        {
            // read() is all-or-nothing and leaves buf untouched when it fails (e.g. if a
            // concurrent reset() lands between numUsed() and here). Writing regardless
            // would splice uninitialized or stale audio into the file, which is worse
            // than a short recording -- it fabricates content that never went through
            // the pipeline.
            if (inputFifo_.read(buf, numInputSamples) == 0)
            {
                sf_write_short(recordFile, buf, numInputSamples);
                isFileCompleteFn_(numInputSamples);
            }
            else
            {
                log_warn("Could not read %d samples from record FIFO; recording is missing audio", numInputSamples);
            }
        }

        drainHadFile_ = true;
    }
    else if (drainHadFile_)
    {
        // The recording this thread was writing has ended. What's still
        // queued is its tail, and can't go to its file any more. execute()
        // can't always tell -- the next recording's SNDFILE can land at the
        // same address as the last one's -- so it's dropped here instead,
        // rather than turning up at the front of the next recording.
        inputFifo_.reset();
        drainHadFile_ = false;
    }
}

void RecordStep::fileIoThreadEntry_()
{
    // Zero-initialized so that a partially filled buffer can never leak heap contents
    // into a recording, even if the guards in drainFifoToFile_() are ever bypassed.
    short* buf = new short[inputSampleRate_]();
    assert(buf != nullptr);

#if defined(__APPLE__)
    // Downgrade thread QoS to Utility to avoid thread contention issues.
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif // defined(__APPLE__)

    SetThreadName("RecordStep");

    while (!fileIoThreadEnding_.load(std::memory_order_acquire))
    {
        drainFifoToFile_(buf);

        // Woken by execute(), and also every so often without it: once a
        // recording stops, execute() isn't called again until the next one
        // starts, and this has to notice the file going away before then.
        fileIoThreadSem_.waitFor(FILE_CHECK_INTERVAL_MS);
    }

    // Record whatever's left in the FIFO, if anything.
    drainFifoToFile_(buf);

    delete[] buf;
}
