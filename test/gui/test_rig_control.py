#!/usr/bin/env python3
"""Tests FreeDV controlling a radio through Hamlib while it's running:
following and setting its frequency, keying it, and what happens when the
radio can't be reached or goes away.

Each scenario below launches its own copy of FreeDV. With no arguments all of
them are run; otherwise, only those named:

    python3 test_rig_control.py [scenario...]

Run from the build folder (or set FREEDV_BINARY) with a window manager
running, on Linux or macOS (see freedvtest/audio.py for the audio devices it
needs). Hamlib's rigctld stands in for the radio; without it ($RIGCTLD, the
PATH or the build folder's copy), these are skipped.
"""

import array
import os
import shutil
import sys
import tempfile
import time
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import freedvtest
import freedvtest.audio
from freedvtest.audio import Tone, VirtualAudioDevices, play, record
from freedvtest.pulse import RecordLatency
from freedvtest.rigctld import Rigctld, find_rigctld
from freedvtest.serial import SerialPorts
from test_main_window import (CALLSIGN, MICROPHONE, RADIO_RX, RADIO_TX, audio_devices, check, check_equal, settings,
                              start_modem, stop_modem, transmitting, wait_for_transmit)

SKIPPED = 77


def rig_settings(address, **extra):
    """Returns settings for FreeDV to control the radio at the given address."""
    result = dict(Hamlib__UseForPTT=True, Hamlib__RigNameStr=Rigctld.RIG_NAME, Hamlib__SerialPort=address,
                  Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare="DM12",
                  Reporting__ManualFrequencyReporting=False)
    result.update(extra)
    return settings(**result)


def quit_and_check_log(app):
    check_equal(app.message_boxes(), [], "Errors displayed")
    check_equal(app.quit(), 0, "FreeDV's exit code")
    # (Hamlib has its own idea of what's worth calling an error.)
    errors = [line for line in app.log().splitlines()
              if (" ERROR " in line or " FATAL " in line) and "HamlibRigController.cpp" not in line]
    check_equal(errors, [], "Errors logged by FreeDV")


def displayed_frequency(app):
    return app.get("main.cboReportFrequency")["value"].replace(",", "")


def wait_for_frequency(app, expected, what):
    try:
        app.wait_until(lambda: displayed_frequency(app) == expected, timeout=15)
    except freedvtest.FreeDVError:
        check_equal(displayed_frequency(app), expected, what)


def wait_for(condition, what, timeout=10):
    """Waits for condition() to be true. what says what went wrong if it
    isn't: text, or a function returning it as things are by then."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if condition():
            return
        time.sleep(0.2)
    check(False, what() if callable(what) else what)


def errors(app):
    return [box["message"] for box in app.message_boxes()]


# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------

def test_control():
    """Frequency, mode and PTT, in both directions."""
    with Rigctld() as radio, audio_devices():
        radio.set_frequency(14236000)
        with freedvtest.start(settings=rig_settings(radio.address)) as app:
            app.wait_for_window("main")
            start_modem(app)

            # FreeDV shows where the radio is, and follows it.
            wait_for_frequency(app, "14.2360", "Frequency displayed once connected")
            radio.set_frequency(7177000)
            wait_for_frequency(app, "7.1770", "Frequency displayed after turning the radio's dial")

            # Changing it in FreeDV changes the radio, mode included.
            app.set_value("main.cboReportFrequency", "14.2400")
            wait_for(lambda: radio.frequency() == 14240000,
                     lambda: "The radio is on %d Hz, not 14240000" % radio.frequency())
            wait_for(lambda: "USB" in radio.mode(), lambda: "The radio's mode on 20m is %s" % radio.mode())
            app.set_value("main.cboReportFrequency", "3.6250")
            wait_for(lambda: radio.frequency() == 3625000,
                     lambda: "The radio is on %d Hz, not 3625000" % radio.frequency())
            # (FreeDV uses upper sideband whatever the band.)
            wait_for(lambda: "USB" in radio.mode(), lambda: "The radio's mode on 80m is %s" % radio.mode())
            check_equal(errors(app), [], "Errors displayed")

            # Transmitting keys it.
            check_equal(radio.transmitting(), False, "Radio transmitting while FreeDV receives")
            app.click("main.btnPTT")
            wait_for_transmit(app)
            wait_for(radio.transmitting, "The radio wasn't keyed")
            app.click("main.btnPTT")
            wait_for_transmit(app, False)
            wait_for(lambda: not radio.transmitting(), "The radio was left keyed")

            # Analog mode is a different mode on the radio, too.
            digital = radio.mode()
            app.click("main.btnAnalog")
            time.sleep(2)
            check_equal(errors(app), [], "Errors displayed in analog mode")
            app.click("main.btnAnalog")

            # Stopping lets go of the radio, with it not transmitting.
            app.click("main.btnPTT")
            wait_for_transmit(app)
            wait_for(radio.transmitting, "The radio wasn't keyed")
            app.click("main.btnPTT")
            wait_for_transmit(app, False)
            stop_modem(app)
            check_equal(radio.transmitting(), False, "Radio transmitting after stopping")
            radio.set_frequency(21313000)
            time.sleep(2)
            check_equal(displayed_frequency(app), "3.6250", "Frequency displayed after the radio moved while stopped")

            # On the next start, the radio's put back where FreeDV was.
            start_modem(app)
            wait_for(lambda: radio.frequency() == 3625000,
                     lambda: "The radio is on %d Hz after starting again, not 3625000" % radio.frequency())
            check_equal(displayed_frequency(app), "3.6250", "Frequency displayed after starting again")
            stop_modem(app)
            quit_and_check_log(app)


def test_frequency_only():
    """Told to leave the radio's mode alone, FreeDV does."""
    with Rigctld() as radio, audio_devices():
        radio.set_frequency(14236000)
        radio.command("M AM 6000")
        extra = dict(Hamlib__EnableFreqModeChanges=False, Hamlib__EnableFreqChangesOnly=True)
        with freedvtest.start(settings=rig_settings(radio.address, **extra)) as app:
            app.wait_for_window("main")
            start_modem(app)
            wait_for_frequency(app, "14.2360", "Frequency displayed once connected")
            app.set_value("main.cboReportFrequency", "7.1770")
            wait_for(lambda: radio.frequency() == 7177000,
                     lambda: "The radio is on %d Hz, not 7177000" % radio.frequency())
            time.sleep(1)
            check_equal(radio.mode(), "AM", "Radio's mode")
            stop_modem(app)
            quit_and_check_log(app)

        # Told to leave both alone, it only keys the radio.
        radio.set_frequency(14236000)
        extra = dict(Hamlib__EnableFreqModeChanges=False, Hamlib__EnableFreqChangesOnly=False,
                     Reporting__ManualFrequencyReporting=True, Reporting__Frequency=7177000)
        with freedvtest.start(settings=rig_settings(radio.address, **extra)) as app:
            app.wait_for_window("main")
            start_modem(app)
            time.sleep(3)
            check_equal((radio.frequency(), radio.mode()), (14236000, "AM"), "Radio's frequency and mode")
            app.click("main.btnPTT")
            wait_for_transmit(app)
            wait_for(radio.transmitting, "The radio wasn't keyed")
            app.click("main.btnPTT")
            wait_for_transmit(app, False)
            wait_for(lambda: not radio.transmitting(), "The radio was left keyed")
            stop_modem(app)
            quit_and_check_log(app)


def test_save_restore_frequency():
    """By default, Start puts the radio on the frequency FreeDV last used and
    Stop puts it back where it was before. Told not to, FreeDV shows where the
    radio is on Start and leaves it where it is on Stop."""
    for save_restore in [True, False]:
        with Rigctld() as radio, audio_devices():
            radio.set_frequency(14236000)
            radio.command("M AM 6000")
            extra = dict(Hamlib__SaveRestoreFrequency=save_restore, Reporting__Frequency=7177000)
            with freedvtest.start(settings=rig_settings(radio.address, **extra)) as app:
                app.wait_for_window("main")
                start_modem(app)
                if save_restore:
                    wait_for(lambda: radio.frequency() == 7177000,
                             lambda: "The radio is on %d Hz after starting, not 7177000" % radio.frequency())
                    wait_for_frequency(app, "7.1770", "Frequency displayed after starting")
                else:
                    wait_for_frequency(app, "14.2360", "Frequency displayed after starting")
                    check_equal(radio.frequency(), 14236000, "Radio's frequency after starting")

                    # Moving it from FreeDV still works; that's where it stays.
                    app.set_value("main.cboReportFrequency", "7.1770")
                    wait_for(lambda: radio.frequency() == 7177000,
                             lambda: "The radio is on %d Hz, not 7177000" % radio.frequency())
                wait_for(lambda: "USB" in radio.mode(), lambda: "The radio's mode after starting is %s" % radio.mode())

                stop_modem(app)
                if save_restore:
                    wait_for(lambda: (radio.frequency(), radio.mode()) == (14236000, "AM"),
                             lambda: "The radio is on %d Hz, %s after stopping, not 14236000 Hz, AM" %
                             (radio.frequency(), radio.mode()))
                else:
                    time.sleep(3)
                    check_equal(radio.frequency(), 7177000, "Radio's frequency after stopping")
                    check("USB" in radio.mode(), "The radio's mode after stopping is %s" % radio.mode())
                quit_and_check_log(app)


def test_mode_display():
    """FreeDV shows the mode that the radio's in."""
    def label(app):
        return app.get("main.textModeStatus")["label"]

    with Rigctld() as radio, audio_devices():
        radio.set_frequency(14236000)
        # (Left to itself, FreeDV would put the radio back in its own mode.)
        extra = dict(Hamlib__EnableFreqModeChanges=False, Hamlib__EnableFreqChangesOnly=True)
        with freedvtest.start(settings=rig_settings(radio.address, **extra)) as app:
            app.wait_for_window("main")
            start_modem(app)
            wait_for_frequency(app, "14.2360", "Frequency displayed once connected")

            for mode, displayed in [("USB", "USB"), ("PKTUSB", "USB-D"), ("LSB", "LSB"), ("PKTLSB", "LSB-D"),
                                    ("FM", "FM"), ("PKTFM", "FM-D"), ("AM", "AM"), ("CW", "unk")]:
                radio.command("M %s 0" % mode)
                try:
                    app.wait_until(lambda: label(app) == displayed, timeout=15)
                except freedvtest.FreeDVError:
                    check_equal(label(app), displayed, "Mode displayed with the radio in " + mode)
                check_equal(app.get("main.textModeStatus")["enabled"], displayed != "unk",
                            "Mode's display enabled with the radio in " + mode)

            # Stopped, it doesn't know.
            radio.command("M USB 0")
            app.wait_until(lambda: label(app) == "USB", timeout=15, description="the radio's mode to be displayed")
            stop_modem(app)
            app.wait_until(lambda: label(app) == "unk", timeout=10,
                           description="the mode's display to be cleared on stopping")
            quit_and_check_log(app)


def test_analog_sideband():
    """Analog mode uses the sideband that's usual for the band, and stays on
    the same audio when that means changing sideband."""
    with Rigctld() as radio, audio_devices():
        radio.set_frequency(14236000)
        with freedvtest.start(settings=rig_settings(radio.address)) as app:
            app.wait_for_window("main")
            start_modem(app)
            wait_for_frequency(app, "14.2360", "Frequency displayed once connected")

            def tune(displayed, frequency):
                app.set_value("main.cboReportFrequency", displayed)
                wait_for(lambda: radio.frequency() == frequency,
                         lambda: "The radio is on %d Hz, not %d" % (radio.frequency(), frequency))

            def analog(expected_mode, expected_frequency, what):
                app.click("main.btnAnalog")
                wait_for(lambda: expected_mode == radio.mode(),
                         lambda: "The radio's mode %s is %s, not %s" % (what, radio.mode(), expected_mode))
                wait_for(lambda: radio.frequency() == expected_frequency,
                         lambda: "The radio %s is on %d Hz, not %d" % (what, radio.frequency(), expected_frequency))

            tune("14.2400", 14240000)
            wait_for(lambda: "USB" in radio.mode(), lambda: "The radio's mode for FreeDV is %s" % radio.mode())
            digital = radio.mode()

            # 20m: upper sideband either way, so the radio stays put.
            analog("USB", 14240000, "in analog mode on 20m")
            analog(digital, 14240000, "back in digital mode on 20m")

            # 40m: lower sideband for analog, 3 kHz up so that the same
            # stretch of the band is heard.
            tune("7.1770", 7177000)
            analog("LSB", 7180000, "in analog mode on 40m")
            wait_for_frequency(app, "7.1800", "Frequency displayed in analog mode on 40m")
            analog(digital, 7177000, "back in digital mode on 40m")
            wait_for_frequency(app, "7.1770", "Frequency displayed back in digital mode on 40m")

            # 60m is the exception below 10 MHz: upper sideband.
            tune("5.3715", 5371500)
            analog("USB", 5371500, "in analog mode on 60m")
            analog(digital, 5371500, "back in digital mode on 60m")

            # A radio that's between the 100 Hz steps FreeDV displays is
            # shown at the nearest one.
            radio.set_frequency(14236060)
            wait_for_frequency(app, "14.2361", "Frequency displayed with the radio on 14.236060")
            radio.set_frequency(14236040)
            wait_for_frequency(app, "14.2360", "Frequency displayed with the radio on 14.236040")
            check_equal(errors(app), [], "Errors displayed")
            stop_modem(app)
            quit_and_check_log(app)

        # Left to choose its own mode in analog, the radio's shown as being
        # in the right one or not for where it is.
        extra = dict(Hamlib__EnableFreqModeChanges=False, Hamlib__EnableFreqChangesOnly=True)
        with freedvtest.start(settings=rig_settings(radio.address, **extra)) as app:
            app.wait_for_window("main")
            start_modem(app)
            app.click("main.btnAnalog")
            for frequency in [14236000, 7177000, 5371500]:
                radio.set_frequency(frequency)
                wait_for_frequency(app, "%.4f" % (frequency / 1e6), "Frequency displayed")
                for mode, displayed in [("USB", "USB"), ("LSB", "LSB"), ("PKTUSB", "USB-D")]:
                    radio.command("M %s 0" % mode)
                    try:
                        app.wait_until(lambda: app.get("main.textModeStatus")["label"] == displayed, timeout=15)
                    except freedvtest.FreeDVError:
                        check_equal(app.get("main.textModeStatus")["label"], displayed,
                                    "Mode displayed with the radio in %s on %d" % (mode, frequency))
            check_equal(errors(app), [], "Errors displayed")
            stop_modem(app)
            quit_and_check_log(app)


def test_editing_frequency():
    """While the frequency's being typed in, the radio doesn't change what's
    there; once it's been left alone, it goes back to showing where the
    radio is."""
    with Rigctld() as radio, audio_devices():
        radio.set_frequency(14236000)
        with freedvtest.start(settings=rig_settings(radio.address)) as app:
            app.wait_for_window("main")
            start_modem(app)
            wait_for_frequency(app, "14.2360", "Frequency displayed once connected")

            app.focus("main.cboReportFrequency")
            radio.set_frequency(7177000)
            time.sleep(4)
            check_equal(displayed_frequency(app), "14.2360", "Frequency displayed while it's being edited")

            # Left as it was, nothing's sent to the radio...
            app.focus("main.cboReportFrequency", lost=True)
            wait_for_frequency(app, "7.1770", "Frequency displayed once it's no longer being edited")
            check_equal(radio.frequency(), 7177000, "Radio's frequency")

            # ...and typing one in sends it there.
            app.focus("main.cboReportFrequency")
            app.set_value("main.cboReportFrequency", "21.3130")
            app.focus("main.cboReportFrequency", lost=True)
            wait_for(lambda: radio.frequency() == 21313000,
                     lambda: "The radio is on %d Hz, not 21313000" % radio.frequency())
            wait_for_frequency(app, "21.3130", "Frequency displayed after typing one in")

            # Clearing it doesn't send anywhere near zero to the radio; the
            # frequency it's actually on goes back into the box.
            app.focus("main.cboReportFrequency")
            app.set_value("main.cboReportFrequency", "")
            app.focus("main.cboReportFrequency", lost=True)
            time.sleep(2)
            check_equal(radio.frequency(), 21313000, "Radio's frequency after clearing the box")
            wait_for_frequency(app, "21.3130", "Frequency displayed after clearing the box")
            check_equal(errors(app), [], "Errors displayed after clearing the box")
            check_equal(errors(app), [], "Errors displayed")
            stop_modem(app)
            quit_and_check_log(app)


def test_unreachable():
    """Starting without the radio there to be controlled."""
    with Rigctld() as radio, audio_devices():
        radio.stop()
        with freedvtest.start(settings=rig_settings(radio.address)) as app:
            app.wait_for_window("main")
            app.click("main.btnOnOff")
            app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to start")
            app.wait_until(lambda: errors(app), timeout=30, description="FreeDV to say that it can't reach the radio")
            messages = errors(app)
            check(all(message.startswith("Couldn't connect to Radio with hamlib") for message in messages),
                  "Messages displayed are %r" % messages)
            app.message_boxes(clear=True)

            # It carries on without.
            check_equal(app.state()["running"], True, "Running without the radio")
            app.click("main.btnPTT")
            wait_for_transmit(app)
            app.click("main.btnPTT")
            wait_for_transmit(app, False)

            # Tuning without the radio there still sends a tuning tone, and
            # there's nothing to key.
            app.click("main.btnTune")
            app.wait_until(lambda: app.get("main.btnTune")["value"], timeout=10, description="tuning to start")
            app.click("main.btnTune")
            app.wait_until(lambda: not app.get("main.btnTune")["value"] and app.get("main.btnOnOff")["enabled"],
                           timeout=15, description="tuning to stop")
            stop_modem(app)
            app.message_boxes(clear=True)

            # With the radio back, so is control of it.
            radio.start()
            radio.set_frequency(7177000)
            start_modem(app)
            wait_for_frequency(app, "7.1770", "Frequency displayed once the radio's back")
            check_equal(errors(app), [], "Errors displayed with the radio back")
            stop_modem(app)
            check_equal(app.quit(), 0, "FreeDV's exit code")

        # A radio that FreeDV doesn't know about (any more).
        with freedvtest.start(settings=rig_settings(radio.address, Hamlib__RigNameStr="No Such Radio")) as app:
            app.wait_for_window("main")
            app.click("main.btnOnOff")
            app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to start")
            app.wait_until(lambda: errors(app), timeout=10, description="FreeDV to say that there's no such radio")
            check(errors(app)[0].startswith("The radio's model is empty."), "Messages displayed are %r" % errors(app))
            app.message_boxes(clear=True)
            check_equal(app.state()["running"], True, "Running without a radio")
            stop_modem(app)
            check_equal(app.quit(), 0, "FreeDV's exit code")

def test_missing_port():
    """Starting with the radio's serial port (or the one for keying) not
    there: FreeDV says so and carries on without radio control."""
    # FreeDV only checks for names of ports that could be there: COM ones on
    # Windows, ones in /dev elsewhere.
    NO_SUCH_PORT = "COM250" if sys.platform == "win32" else "/dev/ttyFreeDVNoSuchPort"
    with Rigctld() as radio, audio_devices():
        # The port itself isn't there.
        with freedvtest.start(settings=rig_settings(NO_SUCH_PORT)) as app:
            app.wait_for_window("main")
            app.click("main.btnOnOff")
            app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to start")
            app.wait_until(lambda: errors(app), timeout=10, description="FreeDV to say that the port isn't there")
            messages = errors(app)
            check(all(message.startswith("Could not open serial port %s." % NO_SUCH_PORT)
                      for message in messages),
                  "Messages displayed are %r" % messages)
            app.message_boxes(clear=True)
            check_equal(app.state()["running"], True, "Running without the radio's port")
            stop_modem(app)
            check_equal(app.quit(), 0, "FreeDV's exit code")

        # The radio's port is there, but the one named for keying isn't.
        with freedvtest.start(settings=rig_settings(
                radio.address, Hamlib__PttType=1, Hamlib__PttSerialPort=NO_SUCH_PORT)) as app:
            app.wait_for_window("main")
            app.click("main.btnOnOff")
            app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to start")
            app.wait_until(lambda: errors(app), timeout=10,
                           description="FreeDV to say that the keying port isn't there")
            messages = errors(app)
            check(all(message.startswith("Could not open serial port %s." % NO_SUCH_PORT)
                      for message in messages),
                  "Messages displayed are %r" % messages)
            app.message_boxes(clear=True)
            check_equal(app.state()["running"], True, "Running without the keying port")
            stop_modem(app)
            check_equal(app.quit(), 0, "FreeDV's exit code")


def test_serial_ptt():
    """Hamlib control of the radio with a serial port named for keying:
    its RTS or DTR line, or no PTT line at all."""
    with Rigctld() as radio, audio_devices():
        radio.set_frequency(14236000)
        for ptt_type in (1, 2, 3):
            with SerialPorts() as ports:
                ptt = ports.add()
                with freedvtest.start(settings=rig_settings(
                        radio.address, Hamlib__PttType=ptt_type, Hamlib__PttSerialPort=ptt.name),
                        env=ports.env) as app:
                    app.wait_for_window("main")
                    start_modem(app)
                    wait_for_frequency(app, "14.2360", "Frequency displayed once connected")

                    # FreeDV opens the keying port it was given and keys the
                    # radio through the connection it has.
                    app.click("main.btnPTT")
                    wait_for_transmit(app)
                    if ptt_type == 3:
                        # With no PTT line, nothing is sent to key the radio.
                        check_equal(radio.transmitting(), False, "Radio keyed with the PTT line set to None")
                    else:
                        wait_for(radio.transmitting, "The radio wasn't keyed")
                    check_equal(errors(app), [], "Errors displayed with PTT type %d" % ptt_type)
                    app.click("main.btnPTT")
                    wait_for_transmit(app, False)
                    stop_modem(app)
                    quit_and_check_log(app)


def test_receive_after_transmit():
    """Once the radio has confirmed it's stopped transmitting, FreeDV goes
    back to receiving, even if the sound system says its input is a very long
    way behind. Some PipeWire versions say about 2055 seconds, which kept RX
    muted for that long, through Stop and Start too (#1601)."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        reply = os.path.join(folder, "reply.wav")
        with Rigctld() as radio, audio_devices(), RecordLatency(2055085278) as latency:
            with freedvtest.start(settings=rig_settings(radio.address), env=latency.env) as app:
                app.wait_for_window("main")
                start_modem(app)

                def answer(what):
                    """Plays the reply to FreeDV as if from the radio."""
                    player = play(RADIO_RX, reply)
                    try:
                        app.wait_until(lambda: app.state()["sync"] == 1, timeout=15,
                                       description="FreeDV to sync on a reply %s" % what)
                    finally:
                        player.kill()
                        player.wait()
                    app.wait_until(lambda: app.state()["sync"] == 0, timeout=15, description="FreeDV to lose sync")

                # Someone to answer: FreeDV's own transmission, kept for later.
                with Tone(MICROPHONE):
                    app.click("main.btnPTT")
                    wait_for_transmit(app)
                    wait_for(radio.transmitting, "The radio wasn't keyed")
                    time.sleep(1)
                    samples = record(VirtualAudioDevices.input_name(RADIO_TX), 8, rate=48000)[0]
                    app.click("main.btnPTT")
                    wait_for_transmit(app, False)
                    wait_for(lambda: not radio.transmitting(), "The radio was left keyed")
                with wave.open(reply, "wb") as f:
                    f.setnchannels(1)
                    f.setsampwidth(2)
                    f.setframerate(48000)
                    f.writeframes(array.array("h", samples).tobytes())
                if sys.platform.startswith("linux") and not latency.reported():
                    print("NOTE: FreeDV didn't ask PulseAudio for its input's latency", flush=True)

                answer("after transmitting")

                # Once more, after stopping and starting.
                app.click("main.btnPTT")
                wait_for_transmit(app)
                app.click("main.btnPTT")
                wait_for_transmit(app, False)
                stop_modem(app)
                start_modem(app)
                answer("after stopping and starting")

                stop_modem(app)
                quit_and_check_log(app)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_lost():
    """The radio going away while FreeDV is running."""
    with Rigctld() as radio, audio_devices():
        radio.set_frequency(14236000)
        with freedvtest.start(settings=rig_settings(radio.address)) as app:
            app.wait_for_window("main")
            start_modem(app)
            wait_for_frequency(app, "14.2360", "Frequency displayed once connected")

            radio.stop()
            time.sleep(3)

            # FreeDV keeps going. Transmitting still works as far as it's
            # concerned, with or without complaint about the radio.
            check_equal(app.state()["running"], True, "Running after the radio went away")
            app.click("main.btnPTT")
            wait_for_transmit(app)
            app.click("main.btnPTT")
            wait_for_transmit(app, False)
            time.sleep(2)
            messages = errors(app)
            print("Messages after losing the radio: %r" % messages, flush=True)
            check(all("hamlib" in message.lower() for message in messages), "Messages displayed are %r" % messages)
            app.message_boxes(clear=True)

            # Nor does stopping get stuck on it.
            stop_modem(app)
            app.message_boxes(clear=True)

            # Once it's back, the next start finds it and puts it where
            # FreeDV was.
            radio.start()
            radio.set_frequency(7177000)
            start_modem(app)
            wait_for(lambda: radio.frequency() == 14236000,
                     lambda: "The radio is on %d Hz once it's back, not 14236000" % radio.frequency())
            check_equal(displayed_frequency(app), "14.2360", "Frequency displayed once the radio's back")
            app.click("main.btnPTT")
            wait_for_transmit(app)
            wait_for(radio.transmitting, "The radio wasn't keyed")
            app.click("main.btnPTT")
            wait_for_transmit(app, False)
            stop_modem(app)
            check_equal(app.quit(), 0, "FreeDV's exit code")


SCENARIOS = {
    "control": test_control,
    "frequency_only": test_frequency_only,
    "mode_display": test_mode_display,
    "analog_sideband": test_analog_sideband,
    "save_restore_frequency": test_save_restore_frequency,
    "editing_frequency": test_editing_frequency,
    "unreachable": test_unreachable,
    "serial_ptt": test_serial_ptt,
    "missing_port": test_missing_port,
    "receive_after_transmit": test_receive_after_transmit,
    "lost": test_lost,
}


def main():
    names = sys.argv[1:] or list(SCENARIOS)
    for scenario in names:
        if scenario not in SCENARIOS:
            sys.exit("Unknown scenario '%s' (available: %s)" % (scenario, ", ".join(SCENARIOS)))
    if find_rigctld() is None:
        print("SKIPPED: rigctld was not found")
        sys.exit(SKIPPED)
    for scenario in names:
        print("=== %s ===" % scenario, flush=True)
        try:
            SCENARIOS[scenario]()
        except freedvtest.audio.Unavailable as e:
            # What it needs of the audio devices can't be had here (see freedvtest.audio).
            print("SKIPPED: %s" % e, flush=True)
            sys.exit(77)
        print("PASS: %s" % scenario, flush=True)


if __name__ == "__main__":
    main()
