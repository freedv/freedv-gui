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
    parecord --latency-msec=20 --channels=1 --file-format=wav --rate 48000 --device "$REC_DEVICE" --format s16le test.wav &
else
    sox --buffer 32768 -t $SOX_DRIVER "$REC_DEVICE" -c 1 -t wav -r 48000 -b 16 -e signed-integer test.wav >/dev/null 2>&1 &
fi
RECORD_PID=$!

# Make sure prerequisites are available
if [ ! -d $(pwd)/rade_src ]; then
    git clone https://github.com/drowe67/radae $(pwd)/rade_src
fi

# Start FreeDV in test mode to record TX
if [ "$1" == "mpp" ]; then
    TX_ARGS="-txtime 30 -txattempts 1 "
else
    TX_ARGS="-txtime 30 -txattempts 1 "
fi
# Tee via process substitution (not a plain pipe) so the FreeDV log shows up in
# CI output while $! stays FreeDV's PID -> FREEDV_EXIT_CODE below is FreeDV's, not tee's.
$FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut tx -utmode RADEV2 -txfile $(pwd)/rade_src/wav/all.wav $TX_ARGS > >(tee tmp.log) 2>&1 &

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

# Workaround/performance improvement: strip silence at beginning and end of recording
# As well as reducing the amount of audio that needs to be played back, it also helps
# ensure we don't accidentally run into a potential RADEV2 bug (https://github.com/freedv/rade_c/issues/8)
# Note: commands adapted from https://digitalcardboard.com/blog/2009/08/25/the-sox-of-silence/
sox test.wav test_stripped.wav silence 1 0.1 1% reverse
sox test_stripped.wav test.wav silence 1 0.1 1% reverse

if [ $FREEDV_EXIT_CODE -eq 0 ]; then
    FADING_DIR="$SCRIPTPATH/fading"

    # Add noise to the recording to check reporting still decodes in a
    # degraded channel.
    CH_EXIT_CODE=0
    if [ "$1" == "mpp" ]; then
        sox $(pwd)/test.wav -t raw -r 8000 -c 1 -e signed-integer -b 16 - | $(pwd)/codec2/build_linux/src/ch - - --No -18 --mpp --fading_dir $FADING_DIR | sox -t raw -r 8000 -c 1 -e signed-integer -b 16 - -t wav $(pwd)/testwithnoise.wav
        CH_EXIT_CODE=${PIPESTATUS[1]}
    elif [ "$1" == "awgn" ]; then
        sox $(pwd)/test.wav -t raw -r 8000 -c 1 -e signed-integer -b 16 - | $(pwd)/codec2/build_linux/src/ch - - --No -14 | sox -t raw -r 8000 -c 1 -e signed-integer -b 16 - -t wav $(pwd)/testwithnoise.wav
        CH_EXIT_CODE=${PIPESTATUS[1]}
    fi

    if [ $CH_EXIT_CODE -ne 0 ]; then
        # ch stopped part of the way through (e.g. running out of fading,
        # which lasts 60 s): what's left isn't the whole transmission, so
        # don't receive it.
        echo "FAIL: ch exited with $CH_EXIT_CODE, so not all of the recording went through the channel"
        FREEDV_EXIT_CODE=1
    else
        mv $(pwd)/testwithnoise.wav $(pwd)/test.wav
    fi
fi

if [ $FREEDV_EXIT_CODE -eq 0 ]; then
    # Tee via process substitution: FreeDV log visible in CI, $! still FreeDV's PID.
    $FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut rx -utmode RADEV2 -rxfile $(pwd)/test.wav > >(tee tmp.log) 2>&1 &
    FDV_PID=$!

    #if [ "$OPERATING_SYSTEM" != "Linux" ]; then
    #    xctrace record --template "Audio System Trace" --window 2m --output "instruments_trace_${FDV_PID}.trace" --attach $FDV_PID
    #fi
    wait $FDV_PID
    FREEDV_EXIT_CODE=$?
    #cat tmp.log
fi

exit $FREEDV_EXIT_CODE
