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

createVirtualAudioCable () {
    CABLE_NAME=$1
    pactl load-module module-null-sink sink_name=$CABLE_NAME sink_properties=device.description=$CABLE_NAME 
}

waitForCableUp () {
    CABLE_NAME=$1
    until (pactl list short sinks | grep -E "^[0-9]+\\s+${CABLE_NAME}\\s+" >/dev/null)
    do
        echo "Waiting for $CABLE_NAME to come up..."
        sleep 1;
    done
}

FREEDV_RADIO_TO_COMPUTER_DEVICE="${FREEDV_RADIO_TO_COMPUTER_DEVICE:-FreeDV_Radio_To_Computer}"
FREEDV_COMPUTER_TO_SPEAKER_DEVICE="${FREEDV_COMPUTER_TO_SPEAKER_DEVICE:-FreeDV_Computer_To_Speaker}"
FREEDV_MICROPHONE_TO_COMPUTER_DEVICE="${FREEDV_MICROPHONE_TO_COMPUTER_DEVICE:-FreeDV_Microphone_To_Computer}"
FREEDV_COMPUTER_TO_RADIO_DEVICE="${FREEDV_COMPUTER_TO_RADIO_DEVICE:-FreeDV_Computer_To_Radio}"

# Automated script to help find audio dropouts.
# NOTE: this must be run from "build_*". Also assumes PulseAudio/pipewire or macOS Core Audio,
# does not work in Windows.
if [ "$OPERATING_SYSTEM" == "Linux" ]; then
    DRIVER_INDEX_FREEDV_RADIO_TO_COMPUTER=$(createVirtualAudioCable FreeDV_Radio_To_Computer)
    waitForCableUp FreeDV_Radio_To_Computer
    DRIVER_INDEX_FREEDV_COMPUTER_TO_SPEAKER=$(createVirtualAudioCable FreeDV_Computer_To_Speaker)
    waitForCableUp FreeDV_Computer_To_Speaker
    DRIVER_INDEX_FREEDV_MICROPHONE_TO_COMPUTER=$(createVirtualAudioCable FreeDV_Microphone_To_Computer)
    waitForCableUp FreeDV_Microphone_To_Computer
    DRIVER_INDEX_FREEDV_COMPUTER_TO_RADIO=$(createVirtualAudioCable FreeDV_Computer_To_Radio)
    waitForCableUp FreeDV_Computer_To_Radio
    DRIVER_INDEX_LOOPBACK=`pactl load-module module-loopback source="FreeDV_Computer_To_Radio.monitor" sink="FreeDV_Radio_To_Computer"`
fi

# Stopping the test part way through (e.g. ctest's timeout) would otherwise
# leave these devices loaded in PulseAudio/PipeWire, and FreeDV and the
# recorder still writing to them, for the rest of the session.
cleanup_virtual_audio_cables () {
    [ -n "$FDV_PID" ] && kill $FDV_PID 2>/dev/null
    [ -n "$RECORD_PID" ] && kill $RECORD_PID 2>/dev/null
    if [ "$OPERATING_SYSTEM" == "Linux" ]; then
        for index in $DRIVER_INDEX_LOOPBACK $DRIVER_INDEX_FREEDV_RADIO_TO_COMPUTER \
                     $DRIVER_INDEX_FREEDV_COMPUTER_TO_SPEAKER $DRIVER_INDEX_FREEDV_MICROPHONE_TO_COMPUTER \
                     $DRIVER_INDEX_FREEDV_COMPUTER_TO_RADIO; do
            [ -n "$index" ] && pactl unload-module $index 2>/dev/null
        done
    fi
}
trap cleanup_virtual_audio_cables EXIT
trap 'exit 143' INT TERM

# Determine correct record device to retrieve TX data
FREEDV_CONF_FILE=freedv-ctest-loss.conf 

PLAY_DEVICE="$FREEDV_RADIO_TO_COMPUTER_DEVICE"
if [ "$OPERATING_SYSTEM" == "Linux" ]; then
    REC_DEVICE="$FREEDV_COMPUTER_TO_RADIO_DEVICE.monitor"
else
    REC_DEVICE="$FREEDV_COMPUTER_TO_RADIO_DEVICE"
fi

# Generate config file
SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
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
    parecord --latency-msec=20 --channels=1 --file-format=wav --device "$REC_DEVICE" --rate 48000 test.wav &
else
    sox --buffer 32768 -t $SOX_DRIVER "$REC_DEVICE" -c 1 -t wav -r 48000 test.wav >/dev/null 2>&1 &
fi
RECORD_PID=$!

# Start FreeDV in test mode to record TX
TX_ARGS="-txfile $(pwd)/tx_in.wav -txfeaturefile $(pwd)/txfeatures.f32 "
($FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut tx -utmode RADEV1 $TX_ARGS 2>&1 | tee tmp.log tx.log) &

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

if [ $FREEDV_EXIT_CODE -eq 0 ]; then
    ($FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut rx -utmode RADEV1 -rxfile $(pwd)/test.wav -rxfeaturefile $(pwd)/rxfeatures.f32 2>&1 | tee tmp.log rx.log) &
    FDV_PID=$!

    #if [ "$OPERATING_SYSTEM" != "Linux" ]; then
    #    xctrace record --template "Audio System Trace" --instrument "Time Profiler" --window 3m --output "instruments_trace_rx_${FDV_PID}.trace" --attach $FDV_PID
    #fi
    wait $FDV_PID
    FREEDV_EXIT_CODE=$?
    #cat tmp.log

    # Run feature files through loss tool
    $PYTHON_BINARY $(pwd)/rade_src/loss.py txfeatures.f32 rxfeatures.f32 --loss_test 0.15 2>&1 | tee loss.txt

    # On failure, decode the same recording again: if it fails the same way,
    # the damage is in the recording (the transmitting half, or what carried
    # it); if not, the receiving half lost something the first time. Either
    # way, test.wav, the feature files and both halves' logs are left behind
    # for looking at. (The result of the test is still that of the first
    # decode.)
    if grep -q "FAIL" loss.txt; then
        echo "----- Decoding the same recording a second time -----"
        $FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut rx -utmode RADEV1 -rxfile $(pwd)/test.wav -rxfeaturefile $(pwd)/rxfeatures2.f32 > rx2.log 2>&1
        grep -E "Sync changed|Sound card FIFOs" rx2.log
        $PYTHON_BINARY $(pwd)/rade_src/loss.py txfeatures.f32 rxfeatures2.f32 --loss_test 0.15 2>&1 | sed "s/^/second decode: /" | sed "s/PASS/OK/; s/FAIL/BAD/"
    fi
fi

exit $FREEDV_EXIT_CODE
