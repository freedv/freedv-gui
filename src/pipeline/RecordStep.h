//=========================================================================
// Name:            RecordStep.h
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

#ifndef AUDIO_PIPELINE__RECORD_STEP_H
#define AUDIO_PIPELINE__RECORD_STEP_H

#include "IPipelineStep.h"

#include <functional>
#include <sndfile.h>
#include <thread>
#include <atomic>
#include "../util/Semaphore.h"
#include "../util/GenericFIFO.h"

class RecordStep : public IPipelineStep
{
public:
    RecordStep(
        int inputSampleRate, std::function<SNDFILE*()> getSndFileFn, std::function<void(int)> isFileCompleteFn);
    virtual ~RecordStep();
    
    virtual int getInputSampleRate() const FREEDV_NONBLOCKING override;
    virtual int getOutputSampleRate() const FREEDV_NONBLOCKING override;
    virtual short* execute(short* inputSamples, int numInputSamples, int* numOutputSamples) FREEDV_NONBLOCKING override;
    virtual void reset() FREEDV_NONBLOCKING override;
    
private:
    int inputSampleRate_;
    std::function<int()> fileSampleRateFn_;
    std::function<SNDFILE*()> getSndFileFn_;
    std::function<void(int)> isFileCompleteFn_;
    std::thread fileIoThread_;
    GenericFIFO<short> inputFifo_;
    std::atomic<bool> fileIoThreadEnding_;
    Semaphore fileIoThreadSem_;

    // Tracks consecutive execute() calls whose samples couldn't be queued, so we log
    // once per drop event (start + duration) rather than once per audio callback.
    int consecutiveWriteDrops_;

    // The file the samples currently in inputFifo_ were queued for. Set by
    // execute() on the pipeline thread, and read by the file thread so it
    // doesn't write them to a different file; used only to tell files
    // apart, so it's never dereferenced -- it can outlive the SNDFILE it
    // names.
    std::atomic<SNDFILE*> queuedForFile_{nullptr};

    // Whether the file thread found a file to write to the last time it
    // looked, so it can tell when a recording has ended. Only touched by the
    // file thread.
    bool drainHadFile_ = false;

    // How long the file thread waits for execute() before checking on the
    // file by itself.
    static constexpr int FILE_CHECK_INTERVAL_MS = 50;

    void fileIoThreadEntry_();

    // Writes whatever's currently queued to the record file. Must only be
    // called from the file I/O thread; takes g_mutexProtectingRecordFiles
    // itself.
    void drainFifoToFile_(short* buf);
};

#endif // AUDIO_PIPELINE__RECORD_STEP_H
