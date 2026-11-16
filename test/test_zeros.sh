#!/bin/bash

FREEDV_TEST=$1
FREEDV_MODE=$2
FREEDV_RX_FILE=$3

# Determine sox driver to use for recording/playback
OPERATING_SYSTEM=`uname`
if [ "$OPERATING_SYSTEM" == "Darwin" ]; then
    SOX_DRIVER=coreaudio
    FREEDV_BINARY=${FREEDV_BINARY:-src/FreeDV.app/Contents/MacOS/FreeDV}
else
    SOX_DRIVER=alsa
    FREEDV_BINARY=${FREEDV_BINARY:-src/freedv}
fi

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

# For debugging--list sink info
#pactl list sinks
# If full duplex test, use correct config file and assume "rx" mode.  
FREEDV_CONF_FILE=freedv-ctest.conf 
if [ "$FREEDV_TEST" == "txrx" ]; then 
    FREEDV_TEST=rx 
    FREEDV_CONF_FILE=freedv-ctest-fullduplex.conf
    REC_DEVICE="$FREEDV_COMPUTER_TO_SPEAKER_DEVICE"

    # Generate sine wave for input
    if [ "$OPERATING_SYSTEM" == "Linux" ]; then
        sox -r 48000 -n -b 16 -c 1 -t wav - synth 120 sin 1000 vol -10dB | paplay -d "$FREEDV_MICROPHONE_TO_COMPUTER_DEVICE" &
    else
        sox --buffer 32768 -r 48000 -n -b 16 -c 1 -t $SOX_DRIVER "$FREEDV_MICROPHONE_TO_COMPUTER_DEVICE" - synth 120 sin 1000 vol -10dB &
    fi
    PLAY_PID=$!
elif [ "$FREEDV_TEST" == "rx" ]; then
    # Start playback if RX
    if [ "$OPERATING_SYSTEM" == "Linux" ]; then
        paplay -d "$FREEDV_RADIO_TO_COMPUTER_DEVICE" $FREEDV_RX_FILE &
    else
        sox --buffer 32768 $FREEDV_RX_FILE -t $SOX_DRIVER "$FREEDV_RADIO_TO_COMPUTER_DEVICE" &
    fi
    PLAY_PID=$!
    REC_DEVICE="$FREEDV_COMPUTER_TO_SPEAKER_DEVICE.monitor"
else
    REC_DEVICE="$FREEDV_COMPUTER_TO_RADIO_DEVICE.monitor"
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
if [ "$FREEDV_TEST" == "tx" ]; then
    if [ "$OPERATING_SYSTEM" == "Linux" ]; then
        # Small buffer, so audio still in flight isn't lost when parecord is stopped
        # (see test_rade_reporting.sh).
        parecord --latency-msec=20 --channels=1 --file-format=wav --rate 48000 --device "$REC_DEVICE" test.wav &
    else
        sox --buffer 32768 -t $SOX_DRIVER "$REC_DEVICE" -c 1 -t wav -r 48000 test.wav &
    fi
    RECORD_PID=$!
fi

# Start FreeDV in test mode
($FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut $FREEDV_TEST -utmode $FREEDV_MODE 2>&1 | tee tmp.log) &

FDV_PID=$!

#if [ "$OPERATING_SYSTEM" != "Linux" ]; then
#    xctrace record --window 2m --template "Audio System Trace" --output "instruments_trace_${FDV_PID}.trace" --attach $FDV_PID
#fi

#sleep 30 
#screencapture ../screenshot.png
#wpctl status
#pw-top -b -n 5
wait $FDV_PID
FREEDV_EXIT_STATUS=$?
#cat tmp.log

# Stop recording/playback and process data
if [ "$FREEDV_TEST" == "rx" ]; then
    kill $PLAY_PID || echo "Already done playing"
    NUM_RESYNCS=`grep "Sync changed" tmp.log | wc -l | xargs`
    echo "Got $NUM_RESYNCS sync changes"
else
    kill $RECORD_PID
    sox --buffer 32768 test.wav -t raw -r 8k -c 1 -b 16 -e signed-integer test.raw silence 1 0.1 0.1% reverse silence 1 0.1 0.1% reverse
    python3 $SCRIPTPATH/check-for-zeros.py test.raw
fi

exit $FREEDV_EXIT_STATUS
