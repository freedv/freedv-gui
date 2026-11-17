#!/bin/bash

# Determine sox driver to use for recording/playback
OPERATING_SYSTEM=`uname`
if [ "$OPERATING_SYSTEM" == "Darwin" ]; then
    SOX_DRIVER=coreaudio
    FREEDV_BINARY=${FREEDV_BINARY:-src/FreeDV.app/Contents/MacOS/FreeDV}
else
    SOX_DRIVER=alsa
    FREEDV_BINARY=${FREEDV_BINARY:-src/freedv}
fi
PYTHON_BINARY=${PYTHON_BINARY:-python3}

SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
source "$SCRIPTPATH/virtual_audio_common.sh"

FREEDV_RADIO_TO_COMPUTER_DEVICE="${FREEDV_RADIO_TO_COMPUTER_DEVICE:-FreeDV_Radio_To_Computer}"
FREEDV_COMPUTER_TO_SPEAKER_DEVICE="${FREEDV_COMPUTER_TO_SPEAKER_DEVICE:-FreeDV_Computer_To_Speaker}"
FREEDV_MICROPHONE_TO_COMPUTER_DEVICE="${FREEDV_MICROPHONE_TO_COMPUTER_DEVICE:-FreeDV_Microphone_To_Computer}"
FREEDV_COMPUTER_TO_RADIO_DEVICE="${FREEDV_COMPUTER_TO_RADIO_DEVICE:-FreeDV_Computer_To_Radio}"

# Automated script to help find audio dropouts.
# NOTE: this must be run from "build_*". Also assumes PulseAudio/pipewire or macOS Core Audio,
# does not work in Windows.
create_virtual_audio_cables

# Determine correct record device to retrieve TX data
FREEDV_CONF_FILE=freedv-ctest-loss.conf 

PLAY_DEVICE="$FREEDV_RADIO_TO_COMPUTER_DEVICE"
if [ "$OPERATING_SYSTEM" == "Linux" ]; then
    REC_DEVICE="$FREEDV_COMPUTER_TO_RADIO_DEVICE.monitor"
else
    REC_DEVICE="$FREEDV_COMPUTER_TO_RADIO_DEVICE"
fi

# Generate config file
if [ "$FREEDV_RADIO_TO_COMPUTER_DEVICE" == "FreeDV_Radio_To_Computer" ] && [ "$OPERATING_SYSTEM" == "Linux" ]; then
    sed "s/@FREEDV_RADIO_TO_COMPUTER_DEVICE@/$FREEDV_RADIO_TO_COMPUTER_DEVICE.monitor/g" $SCRIPTPATH/$FREEDV_CONF_FILE.tmpl > $(pwd)/$FREEDV_CONF_FILE
else
    sed "s/@FREEDV_RADIO_TO_COMPUTER_DEVICE@/$FREEDV_RADIO_TO_COMPUTER_DEVICE/g" $SCRIPTPATH/$FREEDV_CONF_FILE.tmpl > $(pwd)/$FREEDV_CONF_FILE
fi

sed "s/@FREEDV_COMPUTER_TO_RADIO_DEVICE@/$FREEDV_COMPUTER_TO_RADIO_DEVICE/g" $(pwd)/$FREEDV_CONF_FILE > $(pwd)/$FREEDV_CONF_FILE.tmp
mv $(pwd)/$FREEDV_CONF_FILE.tmp $(pwd)/$FREEDV_CONF_FILE
sed "s/@FREEDV_COMPUTER_TO_SPEAKER_DEVICE@/$FREEDV_COMPUTER_TO_SPEAKER_DEVICE/g" $(pwd)/$FREEDV_CONF_FILE > $(pwd)/$FREEDV_CONF_FILE.tmp
mv $(pwd)/$FREEDV_CONF_FILE.tmp $(pwd)/$FREEDV_CONF_FILE

if [ "$FREEDV_MICROPHONE_TO_COMPUTER_DEVICE" == "FreeDV_Microphone_To_Computer" ] && [ "$OPERATING_SYSTEM" == "Linux" ]; then
    sed "s/@FREEDV_MICROPHONE_TO_COMPUTER_DEVICE@/$FREEDV_MICROPHONE_TO_COMPUTER_DEVICE.monitor/g" $(pwd)/$FREEDV_CONF_FILE > $(pwd)/$FREEDV_CONF_FILE.tmp
else
    sed "s/@FREEDV_MICROPHONE_TO_COMPUTER_DEVICE@/$FREEDV_MICROPHONE_TO_COMPUTER_DEVICE/g" $(pwd)/$FREEDV_CONF_FILE > $(pwd)/$FREEDV_CONF_FILE.tmp
fi
mv $(pwd)/$FREEDV_CONF_FILE.tmp $(pwd)/$FREEDV_CONF_FILE

# Resample test file to 48 kHz. Needed for CI environment to reduce CPU usage.
if [ ! -d "$(pwd)/rade_src" ]; then
    git clone -b main https://github.com/drowe67/radae.git rade_src
fi
sox $(pwd)/rade_src/wav/all.wav -r 48000 $(pwd)/tx_in.wav

# Start recording
if [ "$OPERATING_SYSTEM" == "Linux" ]; then
    # Small buffer, so audio still in flight isn't lost when parecord is stopped
    # (see test_rade_reporting.sh).
    parecord --latency-msec=20 --channels=1 --file-format=wav --device "$REC_DEVICE" --rate 48000 --format s16le test.wav &
else
    sox --buffer 128000 -t $SOX_DRIVER "$REC_DEVICE" -c 1 -t wav -r 48000 -b 16 -e signed-integer test.wav >/dev/null 2>&1 &
fi
RECORD_PID=$!

# Start FreeDV in test mode to record TX.
# Tee via process substitution (not a plain pipe) so the FreeDV log shows up in
# CI output while $! stays FreeDV's PID -> FREEDV_EXIT_CODE below is FreeDV's, not tee's.
TX_ARGS="-txfile $(pwd)/tx_in.wav -txfeaturefile $(pwd)/txfeatures.f32 "
$FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut tx -utmode RADEV2 $TX_ARGS > >(tee tmp.log) 2>&1 &

FDV_PID=$!

#if [ "$OPERATING_SYSTEM" != "Linux" ]; then
#    xctrace record --template "Audio System Trace" --instrument "Time Profiler" --window 3m --output "instruments_trace_tx_${FDV_PID}.trace" --attach $FDV_PID
#fi

#sleep 30 
#screencapture ../screenshot.png
#wpctl status
#pw-top -b -n 5
wait $FDV_PID
FREEDV_EXIT_CODE=$?
#cat tmp.log

# Stop recording, play back in RX mode
kill $RECORD_PID
#cp $(pwd)/gmon.out $(pwd)/gmon.out.tx

# Workaround/performance improvement: strip silence at beginning and end of recording
# As well as reducing the amount of audio that needs to be played back, it also helps
# ensure we don't accidentally run into a potential RADEV2 bug (https://github.com/freedv/rade_c/issues/8)
# Note: commands adapted from https://digitalcardboard.com/blog/2009/08/25/the-sox-of-silence/
sox test.wav test_stripped.wav silence 1 0.1 1% reverse
sox test_stripped.wav test.wav silence 1 0.1 1% reverse

# Maximum RADE feature loss we allow for the round trip through real audio hardware.
#
# Instead of hard-coding a magic number, derive it from a software-only baseline as
# described in the RADE integration verification procedure:
#   https://github.com/drowe67/radae/blob/dr-tx-bpf/doc/verification/verification_procedure.md
# We push the same test file (all.wav) through rade_tx_wav + rade_rx_wav from the
# rade_c build under test, measure the feature loss with loss.py, and require the
# hardware round trip to stay within +10% of that baseline. This keeps the test
# tracking the model/build actually being exercised rather than a stale constant.
FALLBACK_LOSS_THRESHOLD=0.0891
LOSS_TOLERANCE=1.10

# Locate rade_tx_wav / rade_rx_wav from the rade_c build. Overridable via
# RADE_C_TOOLS_DIR for unusual layouts.
if [ -z "$RADE_C_TOOLS_DIR" ]; then
    for candidate in \
        "$(pwd)/_deps/freedv_backend-build/rade_build/src" \
        "$(pwd)"/build*/_deps/freedv_backend-build/rade_build/src; do
        if [ -x "$candidate/rade_tx_wav" ] && [ -x "$candidate/rade_rx_wav" ]; then
            RADE_C_TOOLS_DIR="$candidate"
            break
        fi
    done
fi
if [ -z "$RADE_C_TOOLS_DIR" ] || [ ! -x "$RADE_C_TOOLS_DIR/rade_tx_wav" ]; then
    RADE_TX_WAV_FOUND=$(find "$(pwd)" -name rade_tx_wav -type f 2>/dev/null | head -1)
    if [ -n "$RADE_TX_WAV_FOUND" ]; then
        RADE_C_TOOLS_DIR="$(dirname "$RADE_TX_WAV_FOUND")"
    fi
fi

# Run all.wav through the rade_c software-only TX/RX path and print baseline * tolerance.
# Runs in a subshell so the library-path exports don't leak into the FreeDV runs below.
compute_loss_threshold () (
    all_wav="$(pwd)/rade_src/wav/all.wav"
    tx_wav="$RADE_C_TOOLS_DIR/rade_tx_wav"
    rx_wav="$RADE_C_TOOLS_DIR/rade_rx_wav"

    [ -f "$all_wav" ] || { echo "baseline: $all_wav not found" >&2; return 1; }
    if [ ! -x "$tx_wav" ] || [ ! -x "$rx_wav" ]; then
        echo "baseline: rade_tx_wav/rade_rx_wav not found (set RADE_C_TOOLS_DIR)" >&2
        return 1
    fi

    # rade_tx_wav requires 16 kHz mono 16-bit PCM; all.wav already is, but
    # normalise defensively in case the test corpus changes.
    sox "$all_wav" -r 16000 -c 1 -b 16 -e signed-integer "$(pwd)/baseline_in.wav" || return 1

    # librade sits next to the tools; make sure the loader can find it.
    export LD_LIBRARY_PATH="$RADE_C_TOOLS_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export DYLD_LIBRARY_PATH="$RADE_C_TOOLS_DIR${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"

    # --v2 to match the mode FreeDV runs: src/freedv_interface.cpp opens RADE with
    # RADE_MODE_V2 unconditionally on this branch, regardless of the -utmode label.
    "$tx_wav" --v2 -f "$(pwd)/baseline_txfeatures.f32" "$(pwd)/baseline_in.wav" "$(pwd)/baseline_tx.wav" >baseline_tx.log 2>&1 \
        || { cat baseline_tx.log >&2; return 1; }
    "$rx_wav" --v2 -f "$(pwd)/baseline_rxfeatures.f32" "$(pwd)/baseline_tx.wav" "$(pwd)/baseline_decoded.wav" >baseline_rx.log 2>&1 \
        || { cat baseline_rx.log >&2; return 1; }

    baseline_output=$($PYTHON_BINARY "$(pwd)/rade_src/loss.py" \
        "$(pwd)/baseline_txfeatures.f32" "$(pwd)/baseline_rxfeatures.f32" \
        --clip_start 100 --clip_end 300 2>&1)
    echo "software-only baseline: $baseline_output" >&2

    baseline_loss=$(printf '%s\n' "$baseline_output" | sed -n 's/.*loss: *\([0-9][0-9.]*\).*/\1/p' | head -1)
    [ -n "$baseline_loss" ] || return 1

    awk -v b="$baseline_loss" -v t="$LOSS_TOLERANCE" \
        'BEGIN { if (b + 0 <= 0) exit 1; printf "%.4f\n", b * t }'
)

if [ -n "$RADE_LOSS_THRESHOLD" ]; then
    # Precomputed by the caller (e.g. CI runners that can't build the wav tools).
    LOSS_THRESHOLD=$RADE_LOSS_THRESHOLD
    echo "RADE loss threshold: $LOSS_THRESHOLD (supplied via RADE_LOSS_THRESHOLD)"
else
    LOSS_THRESHOLD=$(compute_loss_threshold)
    if [ -z "$LOSS_THRESHOLD" ]; then
        echo "WARNING: could not compute RADE loss baseline from rade_tx_wav/rade_rx_wav; using fallback threshold $FALLBACK_LOSS_THRESHOLD" >&2
        LOSS_THRESHOLD=$FALLBACK_LOSS_THRESHOLD
    fi
    echo "RADE loss threshold: $LOSS_THRESHOLD (software-only baseline x $LOSS_TOLERANCE)"
fi

run_rade_loss_attempt () {
    local playback_file="$1"

    # Tee via process substitution: FreeDV log visible in CI, $! still FreeDV's PID.
    $FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut rx -utmode RADEV2 -txtime 70 -rxfeaturefile $(pwd)/rxfeatures.f32 > >(tee tmp.log) 2>&1 &
    FDV_PID=$!

    #if [ "$OPERATING_SYSTEM" != "Linux" ]; then
    #    xctrace record --template "Audio System Trace" --instrument "Time Profiler" --window 3m --output "instruments_trace_rx_${FDV_PID}.trace" --attach $FDV_PID
    #fi

    sleep 5

    if [ "$OPERATING_SYSTEM" == "Linux" ]; then
        paplay --file-format=wav --device "$PLAY_DEVICE" "$playback_file" &
    else
        sox --buffer 128000 -t wav "$playback_file" -t $SOX_DRIVER "$PLAY_DEVICE" >/dev/null 2>&1 &
    fi
    # (So that the cleanup stops it however the test ends: see
    # virtual_audio_common.sh.)
    PLAY_PID=$!

    wait $FDV_PID
    FREEDV_EXIT_CODE=$?
    #cat tmp.log

    # Run feature files through loss tool
    LOSS_OUTPUT=$($PYTHON_BINARY $(pwd)/rade_src/loss.py txfeatures.f32 rxfeatures.f32 --loss_test $LOSS_THRESHOLD --clip_start 100 --clip_end 300)
    echo "$LOSS_OUTPUT"
}

if [ $FREEDV_EXIT_CODE -eq 0 ]; then
    run_rade_loss_attempt test.wav
fi

exit $FREEDV_EXIT_CODE
