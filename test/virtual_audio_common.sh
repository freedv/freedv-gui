# Shared by the test_*.sh scripts that run FreeDV against virtual audio
# cables; they source it before starting anything.
#
# Whatever the script records in FDV_PID, RECORD_PID, PLAY_PID and RADIO_PID,
# and whatever cables create_virtual_audio_cables loaded, are stopped and
# unloaded however the script ends: normally, on an error or on SIGINT/SIGTERM
# (e.g. ctest's timeout: see TIMEOUT_SIGNAL_NAME in CMakeLists.txt). Left
# behind, they'd hold the cables for the rest of the session, a stray recorder
# would write into the next test's recording, and hamlibserver.py would keep
# its fixed UDP port from the next test that wants it.

createVirtualAudioCable () {
    CABLE_NAME=$1
    pactl load-module module-null-sink sink_name=$CABLE_NAME sink_properties=device.description=$CABLE_NAME
}

waitForCableUp () {
    CABLE_NAME=$1
    WAITED=0
    until (pactl list short sinks | grep -E "^[0-9]+\\s+${CABLE_NAME}\\s+" >/dev/null)
    do
        if [ $WAITED -ge 30 ]; then
            echo "FAIL: $CABLE_NAME didn't come up"
            exit 1
        fi
        echo "Waiting for $CABLE_NAME to come up..."
        sleep 1
        WAITED=$((WAITED + 1))
    done
}

# The four cables FreeDV is configured with, plus a loopback from what it
# sends the radio to what it hears from it. (Linux only: on macOS they're
# loopback devices that already exist.)
create_virtual_audio_cables () {
    if [ "$OPERATING_SYSTEM" == "Linux" ]; then
        DRIVER_INDEX_FREEDV_RADIO_TO_COMPUTER=$(createVirtualAudioCable FreeDV_Radio_To_Computer)
        waitForCableUp FreeDV_Radio_To_Computer
        DRIVER_INDEX_FREEDV_COMPUTER_TO_SPEAKER=$(createVirtualAudioCable FreeDV_Computer_To_Speaker)
        waitForCableUp FreeDV_Computer_To_Speaker
        DRIVER_INDEX_FREEDV_MICROPHONE_TO_COMPUTER=$(createVirtualAudioCable FreeDV_Microphone_To_Computer)
        waitForCableUp FreeDV_Microphone_To_Computer
        DRIVER_INDEX_FREEDV_COMPUTER_TO_RADIO=$(createVirtualAudioCable FreeDV_Computer_To_Radio)
        waitForCableUp FreeDV_Computer_To_Radio
        DRIVER_INDEX_LOOPBACK=$(pactl load-module module-loopback source="FreeDV_Computer_To_Radio.monitor" sink="FreeDV_Radio_To_Computer")
    fi
}

# Kills a process and everything under it, its children first. FreeDV is run
# as "( $FREEDV_BINARY ... | tee ... ) &", so FDV_PID is the subshell's:
# killing only that would leave FreeDV and tee running.
kill_process_tree () {
    local child
    for child in $(pgrep -P "$1" 2>/dev/null); do
        kill_process_tree "$child"
    done
    kill "$1" 2>/dev/null
}

stop_test_processes_and_cables () {
    local pid index
    for pid in $FDV_PID $RECORD_PID $PLAY_PID $RADIO_PID; do
        kill_process_tree "$pid"
    done
    if [ "$OPERATING_SYSTEM" == "Linux" ]; then
        for index in $DRIVER_INDEX_LOOPBACK $DRIVER_INDEX_FREEDV_RADIO_TO_COMPUTER \
                     $DRIVER_INDEX_FREEDV_COMPUTER_TO_SPEAKER $DRIVER_INDEX_FREEDV_MICROPHONE_TO_COMPUTER \
                     $DRIVER_INDEX_FREEDV_COMPUTER_TO_RADIO; do
            pactl unload-module $index 2>/dev/null
        done
    fi
}

# Set before any cable is loaded, so that one loaded before the script is
# stopped is unloaded too.
trap stop_test_processes_and_cables EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
