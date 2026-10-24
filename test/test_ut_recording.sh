#!/bin/bash

# Exercises FreeDV's unit-test audio file options: -txfile and -txoutfile
# (a file through the transmit pipeline, its modulation recorded) and
# -rxfile and -rxoutfile (a radio recording through the receive pipeline,
# the decoded audio recorded). The RX half is fed what the TX half recorded,
# so the decode is of a real FreeDV signal.

# Determine sox driver to use for recording/playback
OPERATING_SYSTEM=`uname`
if [ "$OPERATING_SYSTEM" == "Darwin" ]; then
    SOX_DRIVER=coreaudio
    FREEDV_BINARY=${FREEDV_BINARY:-src/FreeDV.app/Contents/MacOS/FreeDV}
else
    SOX_DRIVER=alsa
    FREEDV_BINARY=${FREEDV_BINARY:-src/freedv}
fi

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

FREEDV_CONF_FILE=freedv-ctest-ut-recording.conf

# NOTE: this must be run from "build_*". Also assumes PulseAudio/pipewire or
# macOS Core Audio, does not work in Windows.
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

# Generate config file
SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 1>&2 ; pwd -P )"
if [ "$FREEDV_RADIO_TO_COMPUTER_DEVICE" == "FreeDV_Radio_To_Computer" ] && [ "$OPERATING_SYSTEM" == "Linux" ]; then
    sed "s/@FREEDV_RADIO_TO_COMPUTER_DEVICE@/$FREEDV_RADIO_TO_COMPUTER_DEVICE.monitor/g" $SCRIPTPATH/freedv-ctest.conf.tmpl > $(pwd)/$FREEDV_CONF_FILE
else
    sed "s/@FREEDV_RADIO_TO_COMPUTER_DEVICE@/$FREEDV_RADIO_TO_COMPUTER_DEVICE/g" $SCRIPTPATH/freedv-ctest.conf.tmpl > $(pwd)/$FREEDV_CONF_FILE
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

check_output_file () {
    FILE=$1
    if [ ! -s "$FILE" ]; then
        echo "FAIL: $FILE is missing or empty"
        return 1
    fi
}

check_log () {
    PATTERN=$1
    if ! grep -q "$PATTERN" tmp.log; then
        echo "FAIL: no '$PATTERN' in FreeDV's output"
        return 1
    fi
}

# Each run's status is FreeDV's, not tee's: the subshell exits with it.

# Transmit: a tone file through the transmit pipeline, with the modulator's
# output recorded.
sox -r 16000 -n -b 16 -c 1 -t wav txin.wav synth 2 sine 1000 vol -10dB
($FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut tx -utmode RADEV1 -txfile txin.wav -txoutfile txout.wav -txattempts 1 2>&1 | tee tmp.log; exit ${PIPESTATUS[0]}) &
FDV_PID=$!
wait $FDV_PID
TX_EXIT_STATUS=$?

# Receive: what the transmit run recorded, through the receive pipeline,
# with the decoder's output recorded.
($FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut rx -utmode RADEV1 -rxfile txout.wav -rxoutfile rxout.wav 2>&1 | tee tmp-rx.log; exit ${PIPESTATUS[0]}) &
FDV_PID=$!
wait $FDV_PID
RX_EXIT_STATUS=$?
cat tmp-rx.log >> tmp.log

# Transmit for a given time and number of attempts, with no files in play,
# capturing RADE's TX features.
($FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut tx -utmode RADEV1 -txtime 2 -txattempts 2 -txfeaturefile txfeat.raw 2>&1 | tee tmp-tx2.log; exit ${PIPESTATUS[0]}) &
FDV_PID=$!
wait $FDV_PID
TX2_EXIT_STATUS=$?
cat tmp-tx2.log >> tmp.log

# Receive for a given time with nothing fed in, capturing RADE's RX features.
($FREEDV_BINARY -f $(pwd)/$FREEDV_CONF_FILE -ut rx -utmode RADEV1 -txtime 2 -rxfeaturefile rxfeat.raw 2>&1 | tee tmp-rx2.log; exit ${PIPESTATUS[0]}) &
FDV_PID=$!
wait $FDV_PID
RX2_EXIT_STATUS=$?
cat tmp-rx2.log >> tmp.log

RESULT=0
if [ $TX_EXIT_STATUS != 0 ]; then echo "FAIL: the transmit run exited with $TX_EXIT_STATUS"; RESULT=1; fi
if [ $RX_EXIT_STATUS != 0 ]; then echo "FAIL: the receive run exited with $RX_EXIT_STATUS"; RESULT=1; fi
check_log "Piping txin.wav through TX pipeline" || RESULT=1
check_log "Recording TX output to txout.wav" || RESULT=1
check_log "Piping txout.wav through RX pipeline" || RESULT=1
check_log "Recording RX output to rxout.wav" || RESULT=1
check_output_file txout.wav || RESULT=1
check_output_file rxout.wav || RESULT=1
if [ $TX2_EXIT_STATUS != 0 ]; then echo "FAIL: the timed transmit run exited with $TX2_EXIT_STATUS"; RESULT=1; fi
if [ $RX2_EXIT_STATUS != 0 ]; then echo "FAIL: the timed receive run exited with $RX2_EXIT_STATUS"; RESULT=1; fi
check_log "Will transmit for 2 seconds" || RESULT=1
check_log "Will transmit 2 time(s)" || RESULT=1
check_log "Sleeping for 2 seconds" || RESULT=1
check_log "Capturing RADE TX features into file txfeat.raw" || RESULT=1
check_log "Capturing RADE RX features into file rxfeat.raw" || RESULT=1
if ! grep -q "Sync changed" tmp-rx.log; then
    echo "FAIL: the recording piped through the receive pipeline never synced"
    RESULT=1
fi

# Clean up PulseAudio virtual devices
if [ "$OPERATING_SYSTEM" == "Linux" ]; then
    pactl unload-module $DRIVER_INDEX_LOOPBACK
    pactl unload-module $DRIVER_INDEX_FREEDV_RADIO_TO_COMPUTER
    pactl unload-module $DRIVER_INDEX_FREEDV_COMPUTER_TO_SPEAKER
    pactl unload-module $DRIVER_INDEX_FREEDV_COMPUTER_TO_RADIO
    pactl unload-module $DRIVER_INDEX_FREEDV_MICROPHONE_TO_COMPUTER
fi

if [ $RESULT == 0 ]; then
    echo "PASS: file input and output through the transmit and receive pipelines"
fi
exit $RESULT
