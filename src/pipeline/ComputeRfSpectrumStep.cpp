//=========================================================================
// Name:            ComputeRfSpectrumStep.cpp
// Purpose:         Describes a RF spectrum computation step step in the audio pipeline.
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

#include <algorithm>

#include "ComputeRfSpectrumStep.h"
#include "../defines.h"

ComputeRfSpectrumStep::ComputeRfSpectrumStep(
    realtime_fp<struct MODEM_STATS*()> const& modemStatsFn,
    realtime_fp<GenericFIFO<float>*()> const& getAvMagFn)
    : modemStatsFn_(modemStatsFn)
    , getAvMagFn_(getAvMagFn)
{
    rxSpectrum_ = new float[MODEM_STATS_NSPEC];
    assert(rxSpectrum_ != nullptr);

    // modem_stats_get_rx_spectrum() only holds a 2 * MODEM_STATS_NSPEC sample
    // window, so blocks are broken into pieces that size before the call.
    rxFdm_ = new RADE_COMP[2 * MODEM_STATS_NSPEC];
    assert(rxFdm_ != nullptr);
}

ComputeRfSpectrumStep::~ComputeRfSpectrumStep()
{
    delete[] rxSpectrum_;
    delete[] rxFdm_;
}

int ComputeRfSpectrumStep::getInputSampleRate() const FREEDV_NONBLOCKING
{
    return FS;
}

int ComputeRfSpectrumStep::getOutputSampleRate() const FREEDV_NONBLOCKING
{
    return FS;
}

short* ComputeRfSpectrumStep::execute(short* inputSamples, int numInputSamples, int* numOutputSamples) FREEDV_NONBLOCKING
{
    // modem_stats_get_rx_spectrum() slides its 2 * MODEM_STATS_NSPEC sample
    // window along by nin, so it can't be handed more samples than the window
    // holds: beyond that the copy runs off the end of fft_buf, into the fft
    // configuration pointer stored right behind it. Feed it the block in
    // window-sized pieces, one spectrum per piece. A block smaller than the
    // window -- what the audio callback delivers -- still yields exactly one
    // spectrum per call, an empty one included.
    constexpr int MAX_SAMPLES_PER_SPECTRUM = 2 * MODEM_STATS_NSPEC;
    auto avMagPtr = getAvMagFn_();

    // A negative count is an empty block. Handed on, it would run the
    // window's shift loop off both ends of fft_buf.
    numInputSamples = std::max(numInputSamples, 0);

    for (int offset = 0; ; offset += MAX_SAMPLES_PER_SPECTRUM)
    {
        int chunk = std::min(numInputSamples - offset, MAX_SAMPLES_PER_SPECTRUM);

        for (int i = 0; i < chunk; i++)
        {
            rxFdm_[i].real = inputSamples[offset + i];
        }

        modem_stats_get_rx_spectrum(modemStatsFn_(), rxSpectrum_, rxFdm_, chunk);

        // Average rx spectrum data using a simple IIR low pass filter
        avMagPtr->write(rxSpectrum_, MODEM_STATS_NSPEC);

        if (offset + chunk >= numInputSamples)
        {
            break;
        }
    }

    // Tap only, no output.
    *numOutputSamples = 0;

    return nullptr;
}
