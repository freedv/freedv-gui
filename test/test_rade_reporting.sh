#!/bin/bash

# Determine sox driver to use for recording/playback
OPERATING_SYSTEM=`uname`
if [ "$OPERATING_SYSTEM" == "Darwin" ]; then
    SOX_DRIVER=coreaudio
    FREEDV_BINARY=${FREEDV_BINARY:-src/FreeDV.app/Contents/MacOS/FreeDV}
else
    SOX_DRIVER=alsa
    FREEDV_BINARY=src/freedv
fi

SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
source "$SCRIPTPATH/virtual_audio_common.sh"

FREEDV_RADIO_TO_COMPUTER_DEVICE="${FREEDV_RADIO_TO_COMPUTER_DEVICE:-FreeDV_Radio_To_Computer}"
FREEDV_COMPUTER_TO_SPEAKER_DEVICE="${FREEDV_COMPUTER_TO_SPEAKER_DEVICE:-FreeDV_Computer_To_Speaker}"
FREEDV_MICROPHONE_TO_COMPUTER_DEVICE="${FREEDV_MICROPHONE_TO_COMPUTER_DEVICE:-FreeDV_Microphone_To_Computer}"
FREEDV_COMPUTER_TO_RADIO_DEVICE="${FREEDV_COMPUTER_TO_RADIO_DEVICE:-FreeDV_Computer_To_Radio}"

if [ ! -d "$(pwd)/codec2" ]; then
    echo "Building 'ch' tool from Codec2..."
    git clone https://github.com/drowe67/codec2.git
    cd codec2
    mkdir build_linux
    cd build_linux
    cmake ..
    make ch
    cd ../..
fi

# Automated script to help find audio dropouts.
# NOTE: this must be run from "build_*". Also assumes PulseAudio/pipewire or macOS Core Audio,
# does not work in Windows.
create_virtual_audio_cables

# Determine correct record device to retrieve TX data
if [ "$1" == "mpp" ]; then
    FREEDV_CONF_FILE=freedv-ctest-reporting-mpp.conf 
else
    FREEDV_CONF_FILE=freedv-ctest-reporting.conf 
fi

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

# Start recording
if [ "$OPERATING_SYSTEM" == "Linux" ]; then
    # Keep parecord's buffer small. By default it asks for a very large one, and on
    # PipeWire the recording then lagged 1-1.7 s behind; hamlibserver.py kills parecord
    # 80 ms after the last PTT off, so whatever was still buffered -- the end of the last
    # over, including the EOO frame carrying the callsign -- was lost.
    parecord --latency-msec=20 --channels=1 --file-format=wav --rate 48000 --device "$REC_DEVICE" test.wav &
else
    sox --buffer 32768 -t $SOX_DRIVER "$REC_DEVICE" -c 1 -t wav -r 48000 test.wav >/dev/null 2>&1 &
fi
RECORD_PID=$!

# Make sure prerequisites are available
if [ ! -d $(pwd)/rade_src ]; then
    git clone https://github.com/drowe67/radae $(pwd)/rade_src
fi

# Start "radio"
if [ "$1" == "mpp" ]; then
    TIMES_BEFORE_KILL=6
else
    TIMES_BEFORE_KILL=1
fi
python3 $SCRIPTPATH/hamlibserver.py $RECORD_PID $TIMES_BEFORE_KILL &
RADIO_PID=$!

# Start FreeDV in test mode to record TX
if [ "$1" == "mpp" ]; then
    TX_ARGS="-txtime 1 -txattempts 7 "
else
    TX_ARGS="-txtime 1 -txattempts 2 "
fi
($FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut tx -utmode RADEV1 -txfile $(pwd)/rade_src/wav/mooneer.wav $TX_ARGS 2>&1 | tee tmp.log) &

FDV_PID=$!

#if [ "$OPERATING_SYSTEM" != "Linux" ]; then
#    xctrace record --template "Audio System Trace" --window 2m --output "instruments_trace_${FDV_PID}.trace" --attach $FDV_PID
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

if [ $FREEDV_EXIT_CODE -eq 0 ]; then
    # Add noise to the recording to check reporting still decodes in a
    # degraded channel.
    #
    # ch fixes the noise density (--No), not the SNR. The recorded signal
    # level is steady, so --No -18 lands at ~5 dB SNR (ch's SNR3k). ch never
    # seeds rand(), so it adds the same noise every run; what varies is where
    # the overs fall in the recording relative to that noise.
    #
    # Measured over 30 such variations per level (callsign decodes: per
    # over / test passes, i.e. at least one of the two overs):
    #   --No -18 (~5 dB): 93% / 100%
    #   --No -17 (~4 dB): 67% /  97%
    #   --No -16 (~3 dB): 45% /  73%
    # -18 is the noisiest level that stays above 95% with margin (~99.5%
    # expected from the per-over rate). Earlier intermittent failures at
    # this level came from the recording losing the second over's EOO (see
    # parecord above), which left only one chance to decode the callsign.
    CH_EXIT_CODE=0
    if [ "$2" == "mpp" ]; then
        # The fading file has 60 s of fading in it (ch takes two complex
        # samples from it, for the direct and delayed paths, for each one
        # it sends at 8 kHz), and ch stops when it runs out: that would
        # leave only the first 60 s of the ~100 s recording, about 4 of
        # its 7 overs, to decode the callsign from. So ch is given one
        # three times as long (180 s): the file's header (the four HF gain
        # floats ch reads first), then its samples three times over. The
        # overs come every ~14 s, so they don't line up with the repeats.
        MPP_FADING_DIR=$(mktemp -d)
        MPP_FADING_FILE="$SCRIPTPATH/fading/fast_fading_samples.float"
        { head -c 16 "$MPP_FADING_FILE"; for i in 1 2 3; do tail -c +17 "$MPP_FADING_FILE"; done; } > "$MPP_FADING_DIR/fast_fading_samples.float"

        sox $(pwd)/test.wav -t raw -r 8000 -c 1 -e signed-integer -b 16 - | $1/src/ch - - --No -25 --mpp --fading_dir $MPP_FADING_DIR | sox -t raw -r 8000 -c 1 -e signed-integer -b 16 - -t wav $(pwd)/testwithnoise.wav
        CH_EXIT_CODE=${PIPESTATUS[1]}
        rm -rf "$MPP_FADING_DIR"
    elif [ "$2" == "awgn" ]; then
        sox $(pwd)/test.wav -t raw -r 8000 -c 1 -e signed-integer -b 16 - | $1/src/ch - - --No -18 | sox -t raw -r 8000 -c 1 -e signed-integer -b 16 - -t wav $(pwd)/testwithnoise.wav
        CH_EXIT_CODE=${PIPESTATUS[1]}
    fi

    if [ $CH_EXIT_CODE -ne 0 ]; then
        # ch stopped part of the way through (e.g. running out of fading):
        # what's left isn't the whole transmission, so don't receive it.
        echo "FAIL: ch exited with $CH_EXIT_CODE, so not all of the recording went through the channel"
        FREEDV_EXIT_CODE=1
    else
        mv $(pwd)/testwithnoise.wav $(pwd)/test.wav
    fi
fi

if [ $FREEDV_EXIT_CODE -eq 0 ]; then
    ($FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut rx -utmode RADEV1 -rxfile $(pwd)/test.wav 2>&1 | tee tmp.log) &
    FDV_PID=$!

    #if [ "$OPERATING_SYSTEM" != "Linux" ]; then
    #    xctrace record --template "Audio System Trace" --window 2m --output "instruments_trace_${FDV_PID}.trace" --attach $FDV_PID
    #fi
    wait $FDV_PID
    FREEDV_EXIT_CODE=$?
    #cat tmp.log
fi

# End radio process as it's no longer needed
kill $RADIO_PID

exit $FREEDV_EXIT_CODE
