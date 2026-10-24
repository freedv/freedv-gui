#!/usr/bin/env python3
"""Tests FreeDV's main window.

Each scenario below launches its own copy (or copies) of FreeDV. With no
arguments all of them are run; otherwise, only those named:

    python3 test_main_window.py [scenario...]

Run from the build folder (or set FREEDV_BINARY) with a window manager
running, as the key used for PTT only works while the window is active.
Linux or macOS: see freedvtest/audio.py for the audio devices it needs.
"""

import array
import datetime
import glob
import json
import math
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import freedvtest
import freedvtest.audio
from freedvtest.audio import (Loopback, Tone, VirtualAudioDevices, device, file_level, play, record, record_level,
                              tone_share, write_tone)

RADIO_RX = device("FreeDVGuiTest_Radio_To_Computer")
RADIO_TX = device("FreeDVGuiTest_Computer_To_Radio")
MICROPHONE = device("FreeDVGuiTest_Microphone_To_Computer")
SPEAKER = device("FreeDVGuiTest_Computer_To_Speaker")

# Where to listen for what FreeDV sends to the radio and to the speaker.
RADIO_TX_MONITOR = VirtualAudioDevices.input_name(RADIO_TX)
SPEAKER_MONITOR = VirtualAudioDevices.input_name(SPEAKER)

TABS = ["Waterfall", "Spectrum", "Frm Radio", "Frm Mic", "Frm Decoder", "SNR"]

# File > Exit: on macOS, Quit FreeDV, in the menu named after it.
EXIT_MENU_ITEM = "FreeDV/Quit FreeDV" if sys.platform == "darwin" else "File/Exit"

# Somewhere for FreeDV to report to that isn't the public FreeDV Reporter
# server (nothing's listening there).
NOWHERE = "127.0.0.1:9"

CALLSIGN = "AA1ZZZ"

# Levels (see freedvtest.audio.record_level()) below and above which a device
# is taken to be silent or to have something playing through it.
SILENT = 50
AUDIBLE = 500

WXK_F5 = 344


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def check_equal(actual, expected, what):
    check(actual == expected, "%s is %r, expected %r" % (what, actual, expected))


def label(app, name):
    return app.get(name)["label"].replace("&", "")


def audio_devices():
    return VirtualAudioDevices(RADIO_RX, RADIO_TX, MICROPHONE, SPEAKER)


def settings(**extra):
    """Returns settings for FreeDV to use the devices from audio_devices(),
    plus whatever else is given (with "/" in names written as "__")."""
    result = {}
    for card, device in [("soundCard1In", VirtualAudioDevices.input_name(RADIO_RX)), ("soundCard1Out", RADIO_TX),
                         ("soundCard2In", VirtualAudioDevices.input_name(MICROPHONE)), ("soundCard2Out", SPEAKER)]:
        result["/Audio/%sDeviceName" % card] = device
        result["/Audio/%sSampleRate" % card] = 48000
    # Leave what goes into the microphone alone: the tones used here are just
    # the kind of steady noise that noise reduction exists to remove.
    result["/Filter/speexpp_enable"] = False
    result["/Filter/agcEnable"] = False
    # Keep FreeDV off the public FreeDV Reporter server.
    result["/Reporting/FreeDV/Hostname"] = NOWHERE
    result["/Reporting/FreeDV/UseTls"] = False
    result["/Reporting/PSKReporter/Enable"] = False
    for name, value in extra.items():
        result["/" + name.replace("__", "/")] = value
    return result


def start_modem(app):
    app.click("main.btnOnOff")
    app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to start")
    check_equal(app.message_boxes(), [], "Errors displayed on start")
    check(app.state()["running"], "FreeDV is not running after pressing Start")


def stop_modem(app):
    app.click("main.btnOnOff")
    app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to stop")
    check_equal(app.state()["running"], False, "Running after pressing Stop")


def transmitting(app):
    return app.state()["transmitting"]


def wait_for_transmit(app, expected=True, timeout=15):
    try:
        app.wait_until(lambda: transmitting(app) == expected, timeout)
    except freedvtest.FreeDVError:
        check_equal(transmitting(app), expected, "Transmitting")
    if not expected:
        # Going back to receive takes a moment longer to finish.
        app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], timeout, "transmit to finish ending")


def level(device, seconds=0.75):
    return record_level(device, seconds)


def check_silent(device, what):
    measured = level(device, 0.5)
    check(measured < SILENT, "%s (level %d)" % (what, measured))


def check_audible(device, what):
    measured = level(device)
    check(measured > AUDIBLE, "%s (level %d)" % (what, measured))


def quit_and_check_log(app):
    check_equal(app.message_boxes(), [], "Errors displayed")
    check_equal(app.quit(), 0, "FreeDV's exit code")
    errors = [line for line in app.log().splitlines() if " ERROR " in line or " FATAL " in line]
    check_equal(errors, [], "Errors logged by FreeDV")


# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------

def test_transmit():
    """The XMIT button, and what else changes while transmitting."""
    with audio_devices():
        with freedvtest.start(settings=settings()) as app:
            app.wait_for_window("main")
            check_equal(app.get("main.notebook")["pages"], TABS, "Tabs")

            # Nothing to do with transmitting can be used until started.
            for button in ["main.btnPTT", "main.btnTune", "main.btnVoiceKeyer", "main.btnAnalog"]:
                check_equal(app.get(button)["enabled"], False, button + " enabled before starting")

            start_modem(app)
            for button in ["main.btnPTT", "main.btnTune", "main.btnVoiceKeyer", "main.btnAnalog"]:
                check_equal(app.get(button)["enabled"], True, button + " enabled once started")
            check_equal(label(app, "main.btnPTT"), "XMIT", "PTT button's label")
            check_silent(RADIO_TX_MONITOR, "Something is being sent to the radio while receiving")

            # Each of the tabs can be looked at.
            for tab in TABS:
                app.set_value("main.notebook", tab)
                check_equal(app.get("main.notebook")["value"], TABS.index(tab), "Tab displayed")
            app.set_value("main.notebook", "Spectrum")

            app.click("main.btnPTT")
            wait_for_transmit(app)
            check_equal(app.get("main.btnPTT")["value"], True, "PTT button pressed")
            check_audible(RADIO_TX_MONITOR, "Nothing is being sent to the radio while transmitting")

            # While transmitting, FreeDV can't be stopped, tuned or retuned,
            # and what the microphone's picking up is displayed.
            for control in ["main.btnOnOff", "main.btnTune", "main.cboReportFrequency"]:
                check_equal(app.get(control)["enabled"], False, control + " enabled while transmitting")
            check_equal(app.get("main.notebook")["value"], TABS.index("Frm Mic"), "Tab displayed while transmitting")

            app.click("main.btnPTT")
            wait_for_transmit(app, False)
            check_equal(app.get("main.btnPTT")["value"], False, "PTT button pressed")
            check_equal(label(app, "main.btnPTT"), "XMIT", "PTT button's label")
            for control in ["main.btnOnOff", "main.btnTune", "main.cboReportFrequency"]:
                check_equal(app.get(control)["enabled"], True, control + " enabled after transmitting")
            check_equal(app.get("main.notebook")["value"], TABS.index("Spectrum"), "Tab displayed after transmitting")
            check_silent(RADIO_TX_MONITOR, "Something is still being sent to the radio")

            # The PTT button's menu.
            items = app.context_menu("main.btnPTT")
            check_equal([(item["label"], item["enabled"], item["checked"]) for item in items],
                        [("Monitor transmitted audio", True, False), ("Adjust Monitor Volume...", False, False)],
                        "PTT button's menu")
            app.context_menu("main.btnPTT", choose="Monitor transmitted audio")
            items = app.context_menu("main.btnPTT")
            check_equal([(item["label"], item["enabled"], item["checked"]) for item in items],
                        [("Monitor transmitted audio", True, True), ("Adjust Monitor Volume...", True, False)],
                        "PTT button's menu when monitoring")

            # With monitoring on, what's said into the microphone is heard.
            with Tone(MICROPHONE):
                app.click("main.btnPTT")
                wait_for_transmit(app)
                check_audible(SPEAKER_MONITOR, "Transmitted audio isn't being monitored")
                app.click("main.btnPTT")
                wait_for_transmit(app, False)
            app.context_menu("main.btnPTT", choose="Monitor transmitted audio")

            # Closing FreeDV while it's transmitting stops everything first.
            app.click("main.btnPTT")
            wait_for_transmit(app)
            check_equal(app.message_boxes(), [], "Errors displayed")
            check_equal(app.quit(), 0, "FreeDV's exit code")
            check_silent(RADIO_TX_MONITOR, "Something is still being sent to the radio after closing")


def test_ptt_key():
    """Transmitting with a key on the keyboard."""
    with audio_devices():
        # By default, the space bar switches between transmit and receive.
        with freedvtest.start(settings=settings()) as app:
            app.wait_for_window("main")
            app.activate("main")
            app.key("main.notebook", "Space")
            time.sleep(0.5)
            check_equal(app.state()["running"], False, "Started by the PTT key")

            start_modem(app)
            app.activate("main")
            app.key("main.notebook", "Space")
            wait_for_transmit(app)
            check_equal(app.get("main.btnPTT")["value"], True, "PTT button pressed")

            # A key held down and repeating does nothing more until it's let
            # go and pressed again for real.
            app.key("main.notebook", "Space", hold=True)
            wait_for_transmit(app, False)
            check_equal(app.get("main.btnPTT")["value"], False, "PTT button pressed after the key pressed again")
            app.key("main.notebook", "Space", hold=True)  # as a held key repeats
            time.sleep(0.5)
            check_equal(transmitting(app), False, "Transmitting after the held PTT key repeated")
            app.key("main.notebook", "Space", release=True)
            time.sleep(0.5)
            check_equal(transmitting(app), False, "Transmitting after letting go of the held key")
            app.key("main.notebook", "Space")
            wait_for_transmit(app)
            check_equal(app.get("main.btnPTT")["value"], True, "PTT button pressed again")
            # Any other key is just a key.
            app.key("main.notebook", "F5")
            time.sleep(0.5)
            check_equal(transmitting(app), True, "Transmitting after pressing another key")
            app.key("main.notebook", "Space")
            wait_for_transmit(app, False)
            check_equal(app.get("main.btnPTT")["value"], False, "PTT button pressed")

            stop_modem(app)
            quit_and_check_log(app)

        # Another key, which has to be held down.
        with freedvtest.start(settings=settings(Rig__PttKeyCode=WXK_F5, Rig__PttMomentaryMode=True)) as app:
            app.wait_for_window("main")
            start_modem(app)
            app.activate("main")
            app.key("main.notebook", "Space")
            time.sleep(0.5)
            check_equal(transmitting(app), False, "Transmitting after pressing a key that isn't the PTT key")

            app.key("main.notebook", "F5", hold=True)
            wait_for_transmit(app)
            time.sleep(1)
            check_equal(transmitting(app), True, "Transmitting while the PTT key is held")
            app.key("main.notebook", "F5", release=True)
            wait_for_transmit(app, False)

            stop_modem(app)
            quit_and_check_log(app)

        # No key at all.
        with freedvtest.start(settings=settings(Rig__EnableSpacebarForPTT=False)) as app:
            app.wait_for_window("main")
            start_modem(app)
            app.activate("main")
            app.key("main.notebook", "Space")
            time.sleep(1)
            check_equal(transmitting(app), False, "Transmitting with the PTT key turned off")
            stop_modem(app)
            quit_and_check_log(app)


def test_ptt_key_elsewhere():
    """Where the PTT key works other than the main window, and when it
    doesn't work at all."""
    reporting = dict(Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare="DM12",
                     Reporting__FreeDV__Enable=True)

    def open_reporter(app):
        app.menu("Tools/FreeDV Reporter")
        app.wait_for_window("reporter")
        app.activate("reporter")

    def press_and_stay_receiving(app, widget, what):
        app.key(widget, "Space")
        time.sleep(1)
        check_equal(transmitting(app), False, "Transmitting after pressing the PTT key %s" % what)
        check_equal(app.get("main.btnPTT")["value"], False, "PTT button pressed after the PTT key %s" % what)

    with audio_devices():
        with freedvtest.start(settings=settings(**reporting)) as app:
            app.wait_for_window("main")
            start_modem(app)

            # In the FreeDV Reporter window it works as in the main one...
            open_reporter(app)
            app.key("reporter.listSpots", "Space")
            wait_for_transmit(app)
            app.key("reporter.listSpots", "Space")
            wait_for_transmit(app, False)

            # ...other than while typing a message, when it's a space.
            app.focus("reporter.cbStatusMessage")
            time.sleep(1.5)  # for the window system to have moved the focus there
            press_and_stay_receiving(app, "reporter.cbStatusMessage", "in the status message")
            app.focus("reporter.cbStatusMessage", lost=True)

            # It does nothing while tuning.
            app.activate("main")
            app.click("main.btnTune")
            app.wait_until(lambda: app.get("main.btnTune")["value"], timeout=10, description="tuning to start")
            time.sleep(1)
            app.key("main.notebook", "Space")
            time.sleep(1)
            check_equal((app.get("main.btnTune")["value"], app.get("main.btnPTT")["value"]), (True, False),
                        "Tuning and PTT pressed after the PTT key while tuning")
            app.click("main.btnTune")
            app.wait_until(lambda: not app.get("main.btnTune")["value"] and app.get("main.btnOnOff")["enabled"],
                           timeout=15, description="tuning to stop")

            stop_modem(app)
            quit_and_check_log(app)

        # Held down in the FreeDV Reporter window, for those who have it that way.
        with freedvtest.start(settings=settings(Rig__PttMomentaryMode=True, **reporting)) as app:
            app.wait_for_window("main")
            start_modem(app)
            open_reporter(app)
            app.key("reporter.listSpots", "Space", hold=True)
            wait_for_transmit(app)
            app.key("reporter.listSpots", "Space", hold=True)  # as a held key repeats
            time.sleep(0.5)
            check_equal(transmitting(app), True, "Transmitting while the PTT key is held")
            app.key("reporter.listSpots", "Space", release=True)
            wait_for_transmit(app, False)
            app.key("reporter.listSpots", "Space", release=True)  # released with nothing to stop
            time.sleep(0.5)
            check_equal(transmitting(app), False, "Transmitting after the PTT key was released twice")
            stop_modem(app)
            quit_and_check_log(app)

        # Receiving only, there's nothing for it to do.
        for extra in [dict(Reporting__FreeDV__ForceReceiveOnly=True),
                      {"Audio__soundCard2InDeviceName": "none", "Audio__soundCard2OutDeviceName": "none"}]:
            with freedvtest.start(settings=settings(Rig__PttMomentaryMode=("Audio__soundCard2InDeviceName" in extra),
                                                    **reporting, **extra)) as app:
                app.wait_for_window("main")
                start_modem(app)
                app.activate("main")
                app.key("main.notebook", "Space", hold=True)
                time.sleep(1)
                check_equal(transmitting(app), False, "Transmitting after pressing the PTT key while receive only")
                app.key("main.notebook", "Space", release=True)
                stop_modem(app)
                quit_and_check_log(app)


def test_tx_level():
    """TX attenuation and Tune."""
    def tx_level(app):
        return label(app, "main.textTxLevel")

    with audio_devices():
        with freedvtest.start(settings=settings(Reporting__Frequency=14236000)) as app:
            app.wait_for_window("main")
            check_equal(label(app, "main.boxTxLevel"), "TX Attenuation", "Level box's title")
            check_equal(tx_level(app), "0.0dB", "TX attenuation")

            # Small and large steps down and up, stopping at either end.
            for button, expected in [("Incr", "0.0dB"), ("Decr", "-0.2dB"), ("DecrBig", "-1.2dB"),
                                     ("Incr", "-1.0dB"), ("IncrBig", "0.0dB"), ("IncrBig", "0.0dB")]:
                app.click("main.btnTxLevel" + button)
                check_equal(tx_level(app), expected, "TX attenuation after %s" % button)
            for _ in range(31):
                app.click("main.btnTxLevelDecrBig")
            check_equal(tx_level(app), "-30.0dB", "TX attenuation at its lowest")
            for _ in range(24):
                app.click("main.btnTxLevelIncrBig")
            check_equal(tx_level(app), "-6.0dB", "TX attenuation")

            # Tune sends a tone to the radio at a level of its own.
            start_modem(app)
            app.click("main.btnTune")
            check_equal(label(app, "main.boxTxLevel"), "Tune Attenuation", "Level box's title while tuning")
            check_equal(tx_level(app), "0.0dB", "Tune attenuation")
            for control in ["main.btnPTT", "main.btnVoiceKeyer", "main.cboReportFrequency"]:
                check_equal(app.get(control)["enabled"], False, control + " enabled while tuning")
            full_level = level(RADIO_TX_MONITOR)
            check(full_level > AUDIBLE, "No tone is being sent to the radio (level %d)" % full_level)

            app.click("main.btnTxLevelDecrBig")
            app.click("main.btnTxLevelDecrBig")
            check_equal(tx_level(app), "-2.0dB", "Tune attenuation")

            # Its menu can take it straight to the bottom.
            items = app.context_menu("main.btnTune", choose="Set tune output to minimum (-30 dB)")
            check_equal([item["label"] for item in items],
                        ["Set tune output to minimum (-30 dB)", "Enable auto-save of tune atten for 20m",
                         "Restore tune atten level for 20m"], "Tune button's menu")
            check_equal(tx_level(app), "-30.0dB", "Tune attenuation")
            time.sleep(0.5)
            low_level = level(RADIO_TX_MONITOR)
            check(low_level < full_level / 10, "The tone isn't much quieter at -30 dB (level %d, was %d)"
                  % (low_level, full_level))

            app.click("main.btnTune")
            check_equal(label(app, "main.boxTxLevel"), "TX Attenuation", "Level box's title")
            check_equal(tx_level(app), "-6.0dB", "TX attenuation after tuning")
            for control in ["main.btnPTT", "main.btnVoiceKeyer", "main.cboReportFrequency"]:
                check_equal(app.get(control)["enabled"], True, control + " enabled after tuning")
            time.sleep(1)  # for the last of the tone to be sent
            check_silent(RADIO_TX_MONITOR, "Something is still being sent to the radio")

            # Attenuation can be kept separately for each band, with a way
            # back to what it was when the band was last switched to.
            items = app.context_menu("main.textTxLevel", choose="Enable auto-save of TX atten for 20m")
            check_equal([(item["label"], item["enabled"]) for item in items],
                        [("Enable auto-save of TX atten for 20m", True), ("Restore TX atten level for 20m", False)],
                        "TX attenuation's menu")
            app.click("main.btnTxLevelDecrBig")
            check_equal(tx_level(app), "-7.0dB", "TX attenuation")
            items = app.context_menu("main.textTxLevel", choose="Restore TX atten level for 20m")
            check_equal([(item["label"], item["enabled"]) for item in items],
                        [("Disable auto-save of TX atten for 20m", True), ("Restore TX atten level for 20m", True)],
                        "TX attenuation's menu")
            check_equal(tx_level(app), "-6.0dB", "TX attenuation after restoring")
            app.context_menu("main.textTxLevel", choose="Disable auto-save of TX atten for 20m")
            items = app.context_menu("main.textTxLevel", choose="Enable auto-save of TX atten for 20m")
            check_equal([(item["label"], item["enabled"]) for item in items],
                        [("Enable auto-save of TX atten for 20m", True), ("Restore TX atten level for 20m", False)],
                        "TX attenuation's menu after turning auto-save off")

            stop_modem(app)
            quit_and_check_log(app)
            saved = freedvtest.read_config(app.config_path)
            check_equal(saved.get("/Audio/transmitLevelByBand/20m"), "-60", "Saved TX attenuation for 20m")
            check_equal((saved.get("/Audio/transmitLevel"), saved.get("/Audio/tuneLevel")), ("-60", "-300"),
                        "Saved TX and tune attenuation")


def test_receive():
    """What's displayed while receiving a signal. FreeDV receives its own
    transmission here: full duplex, with what it sends to the radio fed back
    into what it hears from it."""
    reporting = dict(Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare="DM12")
    with audio_devices(), Loopback(RADIO_TX, RADIO_RX):
        with freedvtest.start(settings=settings(Rig__HalfDuplex=False, Debug__showDecodeStats=True,
                                                **reporting)) as app:
            app.wait_for_window("main")
            check_equal(label(app, "main.textSNR"), "--", "SNR before starting")
            check_equal(label(app, "main.textCurrentDecodeMode"), "Mode: unk", "Mode before starting")
            check_equal(app.get("main.cboLastReportedCallsigns")["value"], "", "Last callsign received")

            start_modem(app)
            check_equal(app.state()["sync"], 0, "Sync with nothing being received")

            app.click("main.btnPTT")
            wait_for_transmit(app)
            app.wait_until(lambda: app.state()["sync"] == 1, timeout=20, description="FreeDV to sync")
            def snr():
                text = label(app, "main.textSNR")
                return int(text[:-2]) if re.match(r"^-?\d+dB$", text) else None

            try:
                app.wait_until(lambda: snr() is not None and snr() > 10, timeout=15)
            except freedvtest.FreeDVError:
                check(False, "SNR of a clean signal is " + label(app, "main.textSNR"))
            check(app.get("main.gaugeSNR")["value"] > 0, "The SNR gauge isn't showing anything")
            check_equal(label(app, "main.textCurrentDecodeMode"), "Mode: RADEV1", "Mode while receiving")
            time.sleep(3)
            check_equal(app.state()["sync"], 1, "Sync a few seconds on")

            # Controls that go with receiving.
            app.set_value("main.ckboxSNR", True)
            app.click("main.btnCenterRx")
            app.click("main.btnBerReset")

            # The callsign arrives as the transmission ends.
            app.click("main.btnPTT")
            wait_for_transmit(app, False)
            app.wait_until(lambda: app.get("main.cboLastReportedCallsigns")["value"] == CALLSIGN, timeout=15,
                           description="the callsign to be received")
            app.wait_until(lambda: app.state()["sync"] == 0, timeout=15, description="sync to be lost")

            app.click("main.btnCallSignReset")
            check_equal(app.get("main.cboLastReportedCallsigns")["value"], "", "Last callsign after clearing")

            stop_modem(app)
            quit_and_check_log(app)
            check_equal(freedvtest.read_config(app.config_path).get("/Audio/snrSlow"), "1", "Saved slow SNR setting")


def test_analog():
    """Analog mode, and the speaker level."""
    with audio_devices():
        with freedvtest.start(settings=settings()) as app:
            app.wait_for_window("main")
            check_equal(app.get("main.sliderMicSpkrLevel")["enabled"], False, "Speaker level enabled before starting")
            start_modem(app)
            check_equal(label(app, "main.btnAnalog"), "Switch to Analog", "Analog button's label")
            check_equal(app.state()["analog"], False, "Analog mode")
            check_equal(app.get("main.sliderMicSpkrLevel")["enabled"], True, "Speaker level enabled")

            with Tone(RADIO_RX):
                # A tone isn't a FreeDV signal, so there's nothing to hear...
                check_silent(SPEAKER_MONITOR, "Something that isn't FreeDV is being played through the speaker")

                # ...until it's passed straight through.
                app.click("main.btnAnalog")
                check_equal(label(app, "main.btnAnalog"), "Switch to Digital", "Analog button's label")
                check_equal(app.state()["analog"], True, "Analog mode")
                loud = level(SPEAKER_MONITOR)
                check(loud > AUDIBLE, "The radio isn't being played through the speaker (level %d)" % loud)

                # Speaker level
                check_equal(label(app, "main.textMicSpkrLevel"), "0.0dB", "Speaker level")
                app.set_value("main.sliderMicSpkrLevel", -200)
                check_equal(label(app, "main.textMicSpkrLevel"), "-20.0dB", "Speaker level")
                time.sleep(0.5)
                quiet = level(SPEAKER_MONITOR)
                check(SILENT < quiet < loud / 5, "Turning the speaker down 20 dB took it from %d to %d" % (loud, quiet))
                app.set_value("main.sliderMicSpkrLevel", 0)
                check_equal(label(app, "main.textMicSpkrLevel"), "0.0dB", "Speaker level")

            # Transmitting in analog mode sends the microphone to the radio.
            app.click("main.btnPTT")
            wait_for_transmit(app)
            # (Not when both pairs are shared, as on Windows CI (see
            # freedvtest.audio.shared()): what's sent to the radio is then
            # received, played through the speaker, heard by the microphone
            # and sent again, round and round.)
            if not (freedvtest.audio.shared(RADIO_RX, RADIO_TX) and freedvtest.audio.shared(MICROPHONE, SPEAKER)):
                check_silent(RADIO_TX_MONITOR, "Something is being sent to the radio with nothing said")
            with Tone(MICROPHONE):
                check_audible(RADIO_TX_MONITOR, "The microphone isn't being sent to the radio")
            app.click("main.btnPTT")
            wait_for_transmit(app, False)

            app.click("main.btnAnalog")
            check_equal(label(app, "main.btnAnalog"), "Switch to Analog", "Analog button's label")
            check_equal(app.state()["analog"], False, "Analog mode")
            with Tone(RADIO_RX):
                check_silent(SPEAKER_MONITOR, "The radio is still being played through the speaker")

            stop_modem(app)
            quit_and_check_log(app)

        # The FreeDV Reporter visibility button, including while in analog
        # mode, and resetting the statistics before anything is running.
        reporting = dict(Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare="DM12",
                         Reporting__FreeDV__Enable=True)
        with freedvtest.start(settings=settings(Debug__showDecodeStats=True, **reporting)) as app:
            app.wait_for_window("main")
            app.click("main.btnBerReset")
            start_modem(app)
            before = label(app, "main.btnReporterHidden")
            app.click("main.btnReporterHidden")
            after = label(app, "main.btnReporterHidden")
            check(before != after, "The Reporter visibility button's label didn't change (%r then %r)" % (before, after))
            app.click("main.btnAnalog")
            app.click("main.btnReporterHidden")
            check(label(app, "main.btnReporterHidden") != after,
                  "The Reporter visibility button's label didn't change in analog mode")
            app.click("main.btnAnalog")
            stop_modem(app)
            quit_and_check_log(app)


def test_play_file():
    """Tools > Play File - From Radio, which stands a file in for the radio."""
    start_item = "Tools/Start Play File - From Radio..."
    stop_item = "Tools/Stop Play File - From Radio..."
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        tone = os.path.join(folder, "tone.wav")
        stereo = os.path.join(folder, "stereo.wav")
        garbage = os.path.join(folder, "garbage.wav")
        write_tone(tone, seconds=3, rate=8000)
        write_tone(stereo, seconds=1, rate=8000, channels=2)
        with open(garbage, "w") as f:
            f.write("This is not a sound file.\n")

        with audio_devices():
            with freedvtest.start(settings=settings()) as app:
                app.wait_for_window("main")
                start_modem(app)
                # Analog mode, so that what's in the file can be heard.
                app.click("main.btnAnalog")
                check_silent(SPEAKER_MONITOR, "Something is being played through the speaker")

                # Changing one's mind
                app.menu(start_item)
                check_equal(app.path_dialogs(clear=True), ["Play File - From Radio"], "File choosers displayed")
                check_equal(app.get("main")["status"], "", "Status after cancelling")

                # Files that can't be played
                for path, caption in [(garbage, "Couldn't open sound file"), (stereo, "Too Many Channels")]:
                    app.choose_next_path(path)
                    app.menu(start_item)
                    check_equal([box["caption"] for box in app.message_boxes(clear=True)], [caption],
                                "Message displayed for %s" % os.path.basename(path))
                    check_equal(app.get("main")["status"], "", "Status after a file couldn't be played")

                # One that can
                app.choose_next_path(tone)
                app.menu(start_item)
                check_equal(app.get("main")["status"], "Playing file %s as radio input" % tone, "Status while playing")
                check_audible(SPEAKER_MONITOR, "The file isn't being played")

                # Starting a transmission cuts the file short.
                app.click("main.btnPTT")
                wait_for_transmit(app)
                check_equal(app.get("main")["status"], "", "Status after a transmission cut the file short")
                app.click("main.btnPTT")
                wait_for_transmit(app, False)

                # ...which stops by itself when it runs out,
                app.wait_until(lambda: app.get("main")["status"] == "", timeout=15, description="the file to finish")
                time.sleep(1.5)  # for the last of it to be played
                check_silent(SPEAKER_MONITOR, "Something is still being played through the speaker")

                # or when it's told to.
                app.choose_next_path(tone)
                app.menu(start_item)
                check_audible(SPEAKER_MONITOR, "The file isn't being played")
                app.menu(stop_item)
                check_equal(app.get("main")["status"], "", "Status after stopping")
                time.sleep(1.5)
                check_silent(SPEAKER_MONITOR, "Something is still being played through the speaker")
                app.menu(start_item)  # back to its original name; cancelled this time

                # A sound file doesn't have to be called one.
                unnamed = os.path.join(folder, "tone")
                shutil.copyfile(tone, unnamed)
                app.choose_next_path(unnamed)
                app.menu(start_item)
                check_equal(app.get("main")["status"], "Playing file %s as radio input" % unnamed,
                            "Status while playing a file with no extension")
                app.menu(stop_item)
                app.menu(start_item)

                # A file of nothing but samples is taken to be at the rate
                # that FreeDV receives at.
                raw = os.path.join(folder, "tone.raw")
                with wave.open(tone, "rb") as f, open(raw, "wb") as out:
                    out.write(f.readframes(f.getnframes()))
                app.choose_next_path(raw)
                app.menu(start_item)
                check(app.get("main")["status"].startswith("Playing raw file %s as radio input (assuming Fs=" % raw),
                      "Status while playing a raw file is %r" % app.get("main")["status"])
                check_audible(SPEAKER_MONITOR, "The raw file isn't being played")
                app.menu(stop_item)
                check_equal(app.get("main")["status"], "", "Status after stopping")
                app.menu(start_item)

                stop_modem(app)
                quit_and_check_log(app)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def count_transmissions(app, seconds):
    """Returns how many times FreeDV starts transmitting in the given time."""
    count = 0
    was_transmitting = False
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        now_transmitting = transmitting(app)
        if now_transmitting and not was_transmitting:
            count += 1
        was_transmitting = now_transmitting
        time.sleep(0.1)
    return count


def test_voice_keyer():
    """The voice keyer: transmitting a recording, with pauses to listen."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        write_tone(os.path.join(folder, "cq.wav"), seconds=3, rate=16000)
        keyer = dict(VoiceKeyer__WaveFilePath=folder, VoiceKeyer__WaveFile="cq.wav", VoiceKeyer__Repeats=1,
                     VoiceKeyer__RxPause=2)

        with audio_devices():
            with freedvtest.start(settings=settings(**keyer)) as app:
                app.wait_for_window("main")
                check_equal(label(app, "main.btnVoiceKeyer"), "Start Voice Keyer\ncq", "Voice keyer button's label")
                start_modem(app)
                # Analog mode, so that what's sent to the radio is what's in the file.
                app.click("main.btnAnalog")

                def check_idle():
                    check_equal(transmitting(app), False, "Transmitting")
                    check_equal(app.get("main.btnVoiceKeyer")["value"], False, "Voice keyer button pressed")
                    check_equal(label(app, "main.btnVoiceKeyer"), "Start Voice Keyer\ncq", "Voice keyer button's label")
                    check_equal(app.get("main")["status"], "", "Status")

                # The file is transmitted, then again after a pause.
                app.click("main.btnVoiceKeyer")
                wait_for_transmit(app)
                check_equal(app.get("main.btnVoiceKeyer")["value"], True, "Voice keyer button pressed")
                check_equal(label(app, "main.btnVoiceKeyer"), "Stop Voice Keyer\ncq", "Voice keyer button's label")
                check_equal(app.get("main.btnPTT")["value"], True, "PTT button pressed")
                check_equal(app.get("main")["status"],
                            "Voice Keyer: Playing file %s to mic input" % os.path.join(folder, "cq.wav"), "Status")
                check_audible(RADIO_TX_MONITOR, "The file isn't being sent to the radio")
                wait_for_transmit(app, False)
                check_equal(app.get("main.btnVoiceKeyer")["value"], True, "Voice keyer button pressed during the pause")
                check_equal(count_transmissions(app, 5), 1, "Transmissions after the first")
                app.wait_until(lambda: not transmitting(app) and not app.get("main.btnVoiceKeyer")["value"],
                               timeout=15, description="the voice keyer to finish")
                app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="transmit to finish ending")
                check_idle()
                check_equal(count_transmissions(app, 4), 0, "Transmissions after the voice keyer finished")

                # It can be stopped part of the way through with its own
                # button, the XMIT button or the PTT key.
                for stop in [lambda: app.click("main.btnVoiceKeyer"),
                             lambda: app.click("main.btnPTT"),
                             lambda: app.key("main.notebook", "Space")]:
                    app.activate("main")
                    app.click("main.btnVoiceKeyer")
                    wait_for_transmit(app)
                    time.sleep(0.5)
                    stop()
                    wait_for_transmit(app, False)
                    check_idle()
                    check_equal(count_transmissions(app, 4), 0, "Transmissions after the voice keyer was stopped")

                stop_modem(app)
                quit_and_check_log(app)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_voice_keyer_reply():
    """The voice keyer stops calling once someone answers: a few seconds of
    a FreeDV signal while it's listening."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        write_tone(os.path.join(folder, "cq.wav"), seconds=2, rate=16000)
        reply = os.path.join(folder, "reply.wav")
        keyer = dict(VoiceKeyer__WaveFilePath=folder, VoiceKeyer__WaveFile="cq.wav", VoiceKeyer__Repeats=5,
                     VoiceKeyer__RxPause=12)

        def answer(seconds):
            """Plays the start of the reply to FreeDV as if from the radio."""
            player = play(RADIO_RX, reply)
            try:
                app.wait_until(lambda: app.state()["sync"] == 1, timeout=15, description="FreeDV to sync on the reply")
                time.sleep(seconds)
            finally:
                player.kill()
                player.wait()

        def call():
            app.activate("main")
            app.click("main.btnVoiceKeyer")
            wait_for_transmit(app)
            wait_for_transmit(app, False)
            check_equal(app.get("main.btnVoiceKeyer")["value"], True, "Voice keyer button pressed while listening")

        def wait_for_keyer_to_stop(what):
            app.wait_until(lambda: not app.get("main.btnVoiceKeyer")["value"], timeout=15,
                           description="the voice keyer to stop after %s" % what)
            check_equal(transmitting(app), False, "Transmitting after %s" % what)
            check_equal(label(app, "main.btnVoiceKeyer"), "Start Voice Keyer\ncq", "Voice keyer button's label")

        with audio_devices():
            with freedvtest.start(settings=settings(**keyer)) as app:
                app.wait_for_window("main")
                start_modem(app)

                # Someone to answer: FreeDV's own transmission, kept for later.
                with Tone(MICROPHONE):
                    app.click("main.btnPTT")
                    wait_for_transmit(app)
                    time.sleep(1)
                    samples = record(VirtualAudioDevices.input_name(RADIO_TX), 12, rate=48000)[0]
                    app.click("main.btnPTT")
                    wait_for_transmit(app, False)
                with wave.open(reply, "wb") as f:
                    f.setnchannels(1)
                    f.setsampwidth(2)
                    f.setframerate(48000)
                    f.writeframes(array.array("h", samples).tobytes())

                # Answered for long enough, it stops calling.
                call()
                answer(8)
                wait_for_keyer_to_stop("being answered")
                check_equal(count_transmissions(app, 14), 0, "Transmissions after being answered")

                # A burst of something that comes to nothing doesn't stop it.
                call()
                answer(1)
                app.wait_until(lambda: app.state()["sync"] == 0, timeout=15, description="FreeDV to lose sync")
                check_equal(app.get("main.btnVoiceKeyer")["value"], True, "Voice keyer button pressed after a burst")
                wait_for_transmit(app, timeout=20)
                wait_for_transmit(app, False)

                # The PTT key stops it while it's listening, answered or not.
                app.key("main.notebook", "Space")
                wait_for_keyer_to_stop("the PTT key while listening")
                call()
                player = play(RADIO_RX, reply)
                try:
                    app.wait_until(lambda: app.state()["sync"] == 1, timeout=15,
                                   description="FreeDV to sync on the reply")
                    time.sleep(1)
                    app.key("main.notebook", "Space")
                    wait_for_keyer_to_stop("the PTT key while being answered")
                finally:
                    player.kill()
                    player.wait()
                check_equal(count_transmissions(app, 14), 0, "Transmissions after the PTT key stopped the keyer")

                stop_modem(app)
                quit_and_check_log(app)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_voice_keyer_files():
    """Choosing and recording the voice keyer's file."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        good = os.path.join(folder, "good.wav")
        low_rate = os.path.join(folder, "low_rate.wav")
        stereo = os.path.join(folder, "stereo.wav")
        recorded = os.path.join(folder, "recorded.wav")
        write_tone(good, seconds=2, rate=16000)
        write_tone(low_rate, seconds=1, rate=4000)
        write_tone(stereo, seconds=1, rate=16000, channels=2)
        long_name = os.path.join(folder, "calling_cq_on_twenty_metres_with_a_very_long_file_name_indeed_" * 3 + ".wav")
        write_tone(long_name, seconds=1, rate=16000)

        with audio_devices():
            # No file's been chosen yet (the default one doesn't exist).
            with freedvtest.start(settings=settings()) as app:
                app.wait_for_window("main")
                start_modem(app)

                # So the voice keyer asks for one, and does nothing without.
                app.click("main.btnVoiceKeyer")
                check_equal(app.path_dialogs(clear=True), ["Select Voice Keyer File"], "File choosers displayed")
                check_equal(transmitting(app), False, "Transmitting without a file")
                check_equal(app.get("main.btnVoiceKeyer")["value"], False, "Voice keyer button pressed")

                # Asked again, it's given a file that it can't use. That's
                # still the file chosen, so it isn't asked for again.
                app.choose_next_path(low_rate)
                for attempt in range(2):
                    app.click("main.btnVoiceKeyer")
                    check_equal([box["caption"] for box in app.message_boxes(clear=True)], ["Sample Rate Too Low"],
                                "Message displayed for low_rate.wav")
                    check_equal(transmitting(app), False, "Transmitting a file that can't be used")
                    check_equal(app.get("main.btnVoiceKeyer")["value"], False, "Voice keyer button pressed")
                check_equal(app.path_dialogs(clear=True), ["Select Voice Keyer File"], "File choosers displayed")
                check_equal(label(app, "main.btnVoiceKeyer"), "Start Voice Keyer\nlow_rate",
                            "Voice keyer button's label")

                # An empty sound file: it can't be opened at all.
                broken = os.path.join(folder, "broken.wav")
                open(broken, "wb").close()
                app.choose_next_path(broken)
                app.context_menu("main.btnVoiceKeyer", choose="Use another voice keyer file...")
                app.click("main.btnVoiceKeyer")
                boxes = app.message_boxes(clear=True)
                check(len(boxes) == 1 and boxes[0]["caption"].startswith("Couldn't open:"),
                      "Message for a file that isn't sound: %r" % [b["caption"] for b in boxes])
                check_equal(transmitting(app), False, "Transmitting a file that isn't sound")
                check_equal(app.get("main.btnVoiceKeyer")["value"], False, "Voice keyer button pressed")

                # Another that it can't use, chosen from its menu this time.
                app.choose_next_path(stereo)
                app.context_menu("main.btnVoiceKeyer", choose="Use another voice keyer file...")
                app.click("main.btnVoiceKeyer")
                check_equal([box["caption"] for box in app.message_boxes(clear=True)], ["Too Many Channels"],
                            "Message displayed for stereo.wav")
                check_equal(transmitting(app), False, "Transmitting a file that can't be used")
                app.path_dialogs(clear=True)

                # Its menu
                items = app.context_menu("main.btnVoiceKeyer")
                check_equal([(item["label"], item["enabled"], item["checked"]) for item in items],
                            [("Use another voice keyer file...", True, False),
                             ("Record new voice keyer file...", True, False),
                             ("Monitor transmitted audio", True, False), ("Adjust Monitor Volume...", False, False)],
                            "Voice keyer button's menu")

                # Using another file
                app.choose_next_path(good)
                app.context_menu("main.btnVoiceKeyer", choose="Use another voice keyer file...")
                check_equal(label(app, "main.btnVoiceKeyer"), "Start Voice Keyer\ngood", "Voice keyer button's label")
                app.click("main.btnVoiceKeyer")
                wait_for_transmit(app)
                app.click("main.btnVoiceKeyer")
                wait_for_transmit(app, False)

                # A file whose name is too long for the button is cut short.
                app.choose_next_path(long_name)
                app.context_menu("main.btnVoiceKeyer", choose="Use another voice keyer file...")
                first_line, shown = label(app, "main.btnVoiceKeyer").split("\n")
                check(first_line == "Start Voice Keyer" and shown.endswith("...") and len(shown) > 5 and
                      os.path.basename(long_name).startswith(shown[:-3]),
                      "Voice keyer button's label for a long file name is %r" % label(app, "main.btnVoiceKeyer"))
                app.choose_next_path(good)
                app.context_menu("main.btnVoiceKeyer", choose="Use another voice keyer file...")
                check_equal(label(app, "main.btnVoiceKeyer"), "Start Voice Keyer\ngood", "Voice keyer button's label")

                # Recording a new one from the microphone, which carries on
                # until the button's pressed.
                app.choose_next_path(recorded)
                with Tone(MICROPHONE):
                    app.context_menu("main.btnVoiceKeyer", choose="Record new voice keyer file...")
                    check_equal(app.get("main")["status"], "Recording file %s from microphone" % recorded, "Status")
                    check_equal(app.get("main.btnVoiceKeyer")["value"], True, "Voice keyer button pressed")
                    check_equal(label(app, "main.btnVoiceKeyer"), "Start Voice Keyer\nrecorded",
                                "Voice keyer button's label")
                    check_equal(app.get("main.btnAnalog")["enabled"], False, "Analog button enabled while recording")
                    check_equal(app.get("main.notebook")["value"], TABS.index("Frm Mic"), "Tab displayed")
                    check_equal(transmitting(app), False, "Transmitting while recording")
                    time.sleep(2)
                    app.click("main.btnVoiceKeyer")
                check_equal(app.get("main")["status"], "", "Status after recording")
                check_equal(app.get("main.btnVoiceKeyer")["value"], False, "Voice keyer button pressed")
                check_equal(app.get("main.btnAnalog")["enabled"], True, "Analog button enabled after recording")
                check_equal(transmitting(app), False, "Transmitting after recording")
                recorded_level, recorded_seconds = file_level(recorded)
                check(recorded_level > AUDIBLE and recorded_seconds > 1,
                      "The recording is %.1f s at level %d" % (recorded_seconds, recorded_level))

                # Recording it can't: the file can't be created, and it
                # says so.
                app.choose_next_path(os.path.join(folder, "nowhere", "x.wav"))
                app.context_menu("main.btnVoiceKeyer", choose="Record new voice keyer file...")
                check_equal([box["caption"] for box in app.message_boxes(clear=True)], ["Couldn't open sound file"],
                            "Message displayed for an unwritable recording file")
                check_equal(app.get("main.btnVoiceKeyer")["value"], False, "Voice keyer button pressed")

                # A name without .wav gets the extension added.
                odd = os.path.join(folder, "recorded2.wave")
                app.choose_next_path(odd)
                with Tone(MICROPHONE):
                    app.context_menu("main.btnVoiceKeyer", choose="Record new voice keyer file...")
                    time.sleep(1)
                    app.click("main.btnVoiceKeyer")
                check(os.path.exists(odd + ".wav"), "recorded2.wave wasn't saved as recorded2.wave.wav")

                # Changing its mind about recording leaves everything as it was.
                app.choose_next_path(None)
                app.context_menu("main.btnVoiceKeyer", choose="Record new voice keyer file...")
                check_equal(app.get("main.btnVoiceKeyer")["value"], False, "Voice keyer button pressed")
                check(not os.path.exists(os.path.join(folder, "abandoned.wav")), "Abandoned recording file exists")
                app.choose_next_path(recorded)
                app.context_menu("main.btnVoiceKeyer", choose="Use another voice keyer file...")

                # ...and is then what's transmitted.
                app.click("main.btnAnalog")
                app.click("main.btnVoiceKeyer")
                wait_for_transmit(app)
                check_audible(RADIO_TX_MONITOR, "The recording isn't being sent to the radio")
                app.click("main.btnVoiceKeyer")
                wait_for_transmit(app, False)

                stop_modem(app)
                quit_and_check_log(app)
                saved = freedvtest.read_config(app.config_path)
                check_equal((saved.get("/VoiceKeyer/WaveFilePath"), saved.get("/VoiceKeyer/WaveFile")),
                            (folder, "recorded.wav"), "Saved voice keyer file")

            # The configured file has gone missing: the keyer asks for
            # another, and refuses to do anything without one.
            with freedvtest.start(settings=settings(**dict(VoiceKeyer__WaveFilePath=folder,
                                                           VoiceKeyer__WaveFile="gone.wav"))) as app:
                app.wait_for_window("main")
                start_modem(app)
                app.click("main.btnVoiceKeyer")
                check_equal(app.path_dialogs(clear=True), ["Select Voice Keyer File"],
                            "Chooser for the missing configured file")
                check_equal(app.get("main.btnVoiceKeyer")["value"], False,
                            "Voice keyer started without a replacement")
                check_equal(transmitting(app), False, "Transmitting after refusing to choose a file")

                # Given a replacement when asked, it plays that.
                app.choose_next_path(good)
                app.click("main.btnVoiceKeyer")
                wait_for_transmit(app)
                check_audible(RADIO_TX_MONITOR, "The replacement file isn't being sent to the radio")
                app.click("main.btnVoiceKeyer")
                wait_for_transmit(app, False)
                stop_modem(app)
                quit_and_check_log(app)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_recording():
    """The Record button."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        raw_folder = os.path.join(folder, "raw")
        decoded_folder = os.path.join(folder, "decoded")
        os.mkdir(raw_folder)
        os.mkdir(decoded_folder)

        def recordings():
            return (sorted(os.listdir(raw_folder)), sorted(os.listdir(decoded_folder)))

        paths = dict(QuickRecord__SavePath=raw_folder, QuickRecord__SaveDecodedPath=decoded_folder)
        with audio_devices():
            with freedvtest.start(settings=settings(**paths)) as app:
                app.wait_for_window("main")
                start_modem(app)

                # Changing one's mind
                app.click("main.btnRecord")
                dialog = app.wait_for_window("beginRecording")
                check_equal(dialog["label"], "Start Recording", "Window title")
                check_equal(app.get("beginRecording.rawRecording")["value"], True, "Off Air chosen")
                check_equal(app.get("beginRecording.formatWav")["value"], True, "WAV chosen")
                check_equal(app.get("beginRecording.formatMp3")["enabled"], False, "MP3 available off air")
                check_equal(app.get("beginRecording.recordingSuffix")["value"], "", "Recording suffix")
                app.click("beginRecording.btnCancel")
                app.wait_for_window_closed("beginRecording")
                check_equal(app.get("main.btnRecord")["value"], False, "Record button pressed after cancelling")
                check_equal(recordings(), ([], []), "Recordings after cancelling")

                # What's coming from the radio
                with Tone(RADIO_RX):
                    app.click("main.btnRecord")
                    app.wait_for_window("beginRecording")
                    app.set_value("beginRecording.recordingSuffix", "K1ABC")
                    app.click("beginRecording.btnStart")
                    app.wait_for_window_closed("beginRecording")
                    check_equal(app.get("main.btnRecord")["value"], True, "Record button pressed")
                    raw, decoded = recordings()
                    check(len(raw) == 1 and re.match(r"^FDV_FromRadio_\d{8}-\d{6}_K1ABC\.wav$", raw[0]),
                          "Off air recordings are %r" % raw)
                    check_equal(decoded, [], "Decoded recordings")
                    check_equal(app.get("main")["status"],
                                "Recording file %s from radio" % os.path.join(raw_folder, raw[0]), "Status")
                    time.sleep(3)
                    app.click("main.btnRecord")
                check_equal(app.get("main.btnRecord")["value"], False, "Record button pressed after stopping")
                check_equal(app.get("main")["status"], "", "Status after stopping")
                recorded_level, recorded_seconds = file_level(os.path.join(raw_folder, raw[0]))
                check(recorded_level > AUDIBLE and recorded_seconds > 2,
                      "The off air recording is %.1f s at level %d" % (recorded_seconds, recorded_level))

                # Both that and what's decoded from it (which here is nothing).
                time.sleep(1.1)  # for a different time in the file's name
                app.click("main.btnRecord")
                app.wait_for_window("beginRecording")
                app.click("beginRecording.bothRecording")
                check_equal(app.get("beginRecording.formatMp3")["enabled"], False, "MP3 available for both")
                app.click("beginRecording.btnStart")
                app.wait_for_window_closed("beginRecording")
                time.sleep(2)
                app.click("main.btnRecord")
                raw, decoded = recordings()
                check(len(raw) == 2 and len(decoded) == 1 and
                      re.match(r"^FDV_FromDecoder_\d{8}-\d{6}\.wav$", decoded[0]),
                      "Recordings are %r and %r" % (raw, decoded))
                check_equal(app.get("main.btnRecord")["value"], False, "Record button pressed after stopping")

                # Transmitting while recording takes the recording along.
                time.sleep(1.1)
                recordings_before = recordings()
                app.click("main.btnRecord")
                app.wait_for_window("beginRecording")
                app.click("beginRecording.rawRecording")
                app.click("beginRecording.btnStart")
                app.wait_for_window_closed("beginRecording")
                time.sleep(1)
                app.click("main.btnPTT")
                wait_for_transmit(app)
                time.sleep(1)
                app.click("main.btnPTT")
                wait_for_transmit(app, False)
                time.sleep(1)
                app.click("main.btnRecord")
                check_equal(app.get("main.btnRecord")["value"], False, "Record button pressed after stopping")
                check(len(recordings()[0]) > len(recordings_before[0]), "Nothing was recorded while transmitting")

                # Started while transmitting, what's recorded is what's sent.
                time.sleep(1.1)
                app.click("main.btnPTT")
                wait_for_transmit(app)
                app.click("main.btnRecord")
                app.wait_for_window("beginRecording")
                app.click("beginRecording.rawRecording")
                app.click("beginRecording.btnStart")
                app.wait_for_window_closed("beginRecording")
                check_equal(app.get("main.btnRecord")["value"], True, "Record button pressed while transmitting")
                time.sleep(2)
                app.click("main.btnRecord")
                app.click("main.btnPTT")
                wait_for_transmit(app, False)
                check_equal(len(recordings()[0]), 4, "Off air recordings after recording while transmitting")

                # Recording stops with FreeDV.
                app.click("main.btnRecord")
                app.wait_for_window("beginRecording")
                app.click("beginRecording.btnStart")
                app.wait_for_window_closed("beginRecording")
                check_equal(app.get("main.btnRecord")["value"], True, "Record button pressed")

                stop_modem(app)
                quit_and_check_log(app)
                for path in glob.glob(os.path.join(raw_folder, "*.wav")):
                    file_level(path)  # all of them are sound files
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_recording_options():
    """The rest of what can be chosen when starting a recording."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        raw_folder = os.path.join(folder, "raw")
        decoded_folder = os.path.join(folder, "decoded")
        os.mkdir(raw_folder)
        os.mkdir(decoded_folder)

        def recordings():
            return (sorted(os.listdir(raw_folder)), sorted(os.listdir(decoded_folder)))

        # Someone that's been heard before, for FreeDV to list.
        csv_log = os.path.join(folder, "heard.csv")
        with open(csv_log, "w") as f:
            f.write("date,time,callsign,mode,frequency_hz,snr_db\n")
            # The last one written is the one on top: FreeDV puts each
            # heard station at the top of the list.
            f.write("2026-03-03,04:05:06,vk5dgr/p,RADEV1,14236000,6\n")
            f.write("2026-03-04,05:06:07,K1ABC/P,RADEV1,14236000,7\n")

        extra = dict(QuickRecord__SavePath=raw_folder, QuickRecord__SaveDecodedPath=decoded_folder,
                     Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare="DM12",
                     Reporting__CSV__LogFilePath=csv_log)
        with audio_devices():
            with freedvtest.start(settings=settings(**extra)) as app:
                app.wait_for_window("main")
                start_modem(app)

                # With someone chosen, the recording is named after them, as
                # far as a file's name allows.
                app.combo_popup("main.cboLastReportedCallsigns", open=True)
                app.wait_until(lambda: app.get("main.listLastReportedCallsigns")["shown"])
                app.click("main.listLastReportedCallsigns", item=0)
                app.wait_until(lambda: not app.get("main.listLastReportedCallsigns")["shown"])
                check_equal(app.get("main.cboLastReportedCallsigns")["value"], "K1ABC/P", "Callsign chosen")

                app.click("main.btnRecord")
                app.wait_for_window("beginRecording")
                check_equal(app.get("beginRecording.recordingSuffix")["value"], "K1ABC_P", "Recording suffix")

                # MP3 is only for what's decoded.
                mp3 = app.get("beginRecording.formatMp3")["shown"]
                app.click("beginRecording.decodedRecording")
                if mp3:
                    check_equal(app.get("beginRecording.formatMp3")["enabled"], True, "MP3 available for decoded")
                    app.click("beginRecording.formatMp3")
                    app.click("beginRecording.rawRecording")
                    check_equal((app.get("beginRecording.formatMp3")["enabled"],
                                 app.get("beginRecording.formatWav")["value"]), (False, True),
                                "MP3 available and WAV chosen off air")
                    app.click("beginRecording.decodedRecording")
                    app.click("beginRecording.formatMp3")
                extension = "mp3" if mp3 else "wav"

                app.click("beginRecording.btnStart")
                app.wait_for_window_closed("beginRecording")
                check_equal(app.message_boxes(), [], "Errors displayed")
                raw, decoded = recordings()
                check(raw == [] and len(decoded) == 1 and
                      re.match(r"^FDV_FromDecoder_\d{8}-\d{6}_K1ABC_P\.%s$" % extension, decoded[0]),
                      "Recordings are %r and %r" % (raw, decoded))
                check_equal(app.get("main")["status"],
                            "Recording file %s from decoder" % os.path.join(decoded_folder, decoded[0]), "Status")
                time.sleep(2)
                app.click("main.btnRecord")
                check_equal(app.get("main.btnRecord")["value"], False, "Record button pressed after stopping")
                check_equal(app.get("main")["status"], "", "Status after stopping")
                check(os.path.getsize(os.path.join(decoded_folder, decoded[0])) >= 0, "The recording has gone")

                # Someone heard with a name that's already partly lowercase
                # keeps those letters; only what a file name can't have is
                # replaced.
                app.combo_popup("main.cboLastReportedCallsigns", open=True)
                app.wait_until(lambda: app.get("main.listLastReportedCallsigns")["shown"],
                               description="the list of callsigns to open")
                app.click("main.listLastReportedCallsigns", item=1)
                app.wait_until(lambda: not app.get("main.listLastReportedCallsigns")["shown"],
                               description="the list of callsigns to close")
                check_equal(app.get("main.cboLastReportedCallsigns")["value"], "vk5dgr/p", "Lowercase callsign chosen")
                app.click("main.btnRecord")
                app.wait_for_window("beginRecording")
                check_equal(app.get("beginRecording.recordingSuffix")["value"], "vk5dgr_p",
                            "Recording suffix for a lowercase callsign")
                app.click("beginRecording.btnCancel")
                app.wait_for_window_closed("beginRecording")

                # Whatever ends up in the suffix, the recording stays where
                # recordings go.
                time.sleep(1.1)  # for a different time in the file's name
                app.click("main.btnRecord")
                app.wait_for_window("beginRecording")
                # Typed in, anything that a file's name can't have is
                # replaced as it's typed; the rest is left to go in as usual.
                # (Some keys never get as far as being typed, depending on
                # the version of wxWidgets: those leave the suffix as it was.
                # A space isn't among them: it's the PTT key, and in CI, where
                # the main window still counts as the active one, it keys
                # the transmitter instead.)
                suffix = "beginRecording.recordingSuffix"
                app.set_value(suffix, "")
                replaced = 0
                for typed in ["/", "+", ".", "*"]:
                    before = app.get(suffix)["value"]
                    app.key(suffix, typed)
                    time.sleep(0.3)
                    after = app.get(suffix)["value"]
                    check(after in (before, before + "_"),
                          "Typing %r changed the recording suffix from %r to %r" % (typed, before, after))
                    replaced += len(after) - len(before)
                check(replaced >= 2, "Only %d of the characters typed were replaced" % replaced)
                for typed in ["K", "5", "_", "Backspace", "Delete", "Left"]:
                    before = app.get(suffix)["value"]
                    app.key(suffix, typed)
                    time.sleep(0.2)
                    check_equal(app.get(suffix)["value"], before,
                                "Recording suffix after %r, which is left to go in as usual" % typed)

                app.set_value("beginRecording.recordingSuffix", "../up one/K1ABC")
                app.click("beginRecording.btnStart")
                app.wait_for_window_closed("beginRecording")
                check_equal(app.message_boxes(), [], "Errors displayed")
                check_equal(app.get("main.btnRecord")["value"], True, "Record button pressed")
                raw, decoded = recordings()
                check(len(raw) == 1 and re.match(r"^FDV_FromRadio_\d{8}-\d{6}____up_one_K1ABC\.wav$", raw[0]),
                      "Off air recordings are %r" % raw)
                check_equal(sorted(os.listdir(folder)), ["decoded", "heard.csv", "raw"], "What's been created")
                app.click("main.btnRecord")

                # A recording started after clearing the list of heard
                # stations is named for nobody, even though someone had been
                # chosen from it.
                app.click("main.btnCallSignReset")
                time.sleep(1.1)  # for a different time in the file's name
                app.click("main.btnRecord")
                app.wait_for_window("beginRecording")
                check_equal(app.get("beginRecording.recordingSuffix")["value"], "", "Recording suffix after clearing")
                app.click("beginRecording.btnCancel")
                app.wait_for_window_closed("beginRecording")
                check_equal(recordings(), (raw, decoded), "Recordings after cancelling")

                stop_modem(app)
                quit_and_check_log(app)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def set_writable(folder, writable):
    """Lets a folder be written to, or not. (On Windows, which doesn't apply
    the read-only attribute to folders, by denying the user permission to.)"""
    if sys.platform == "win32":
        user = os.environ["USERNAME"]
        if writable:
            subprocess.call(["icacls", folder, "/remove:d", user], stdout=subprocess.DEVNULL)
        else:
            # (Only adding files and folders: icacls' "W" takes away what
            # listing what's there needs too.)
            subprocess.check_call(["icacls", folder, "/deny", user + ":(OI)(CI)(WD,AD)"], stdout=subprocess.DEVNULL)
    else:
        os.chmod(folder, 0o755 if writable else 0o555)


def test_unwritable_files():
    """Recordings and voice keyer files that can't be written or read."""
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        print("Skipped: nothing is unwritable for root", flush=True)
        return

    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        writable = os.path.join(folder, "writable")
        read_only = os.path.join(folder, "read_only")
        os.mkdir(writable)
        os.mkdir(read_only)
        not_sound = os.path.join(writable, "not_sound.wav")
        with open(not_sound, "w") as f:
            f.write("This is not a sound file.\n")
        set_writable(read_only, False)

        def start_recording(what):
            app.click("main.btnRecord")
            app.wait_for_window("beginRecording")
            app.click("beginRecording.%sRecording" % what)
            app.click("beginRecording.btnStart")
            app.wait_for_window_closed("beginRecording")

        def check_not_recording(what):
            check_equal([box["caption"] for box in app.message_boxes(clear=True)], ["Couldn't open sound file"],
                        "Messages displayed for %s" % what)
            check_equal(app.get("main.btnRecord")["value"], False, "Record button pressed after %s" % what)
            check_equal(os.listdir(read_only), [], "Files in the folder that can't be written to")

        with audio_devices():
            # Nowhere to put what comes from the radio.
            paths = dict(QuickRecord__SavePath=read_only, QuickRecord__SaveDecodedPath=writable,
                         VoiceKeyer__WaveFilePath=writable, VoiceKeyer__WaveFile="not_sound.wav")
            with freedvtest.start(settings=settings(**paths)) as app:
                app.wait_for_window("main")
                start_modem(app)
                start_recording("raw")
                check_not_recording("an off air recording")
                start_recording("both")
                check_not_recording("both recordings")
                check_equal([name for name in os.listdir(writable) if name != "not_sound.wav"], [],
                            "Decoded recordings started")

                # A new voice keyer file can't go there either.
                app.choose_next_path(os.path.join(read_only, "new.wav"))
                app.context_menu("main.btnVoiceKeyer", choose="Record new voice keyer file...")
                check_equal([box["caption"] for box in app.message_boxes(clear=True)], ["Couldn't open sound file"],
                            "Messages displayed for a new voice keyer file")
                check_equal(app.get("main.btnVoiceKeyer")["value"], False, "Voice keyer button pressed")
                check_equal(app.get("main")["status"], "", "Status")
                stop_modem(app)
                quit_and_check_log(app)

            # Nowhere to put what's decoded from it. What comes from the
            # radio had somewhere to go, but isn't recorded by itself.
            paths = dict(QuickRecord__SavePath=writable, QuickRecord__SaveDecodedPath=read_only,
                         VoiceKeyer__WaveFilePath=writable, VoiceKeyer__WaveFile="not_sound.wav")
            with freedvtest.start(settings=settings(**paths)) as app:
                app.wait_for_window("main")
                start_modem(app)
                start_recording("decoded")
                check_not_recording("a decoded recording")
                start_recording("both")
                check_not_recording("both recordings")
                check_equal(app.get("main")["status"], "", "Status")

                # A voice keyer file that isn't a sound file isn't transmitted.
                app.click("main.btnVoiceKeyer")
                app.wait_until(lambda: app.message_boxes(), timeout=10, description="a message to be displayed")
                check_equal([box["caption"] for box in app.message_boxes(clear=True)],
                            ["Couldn't open: %s" % not_sound], "Messages displayed for the voice keyer's file")
                app.wait_until(lambda: not app.get("main.btnVoiceKeyer")["value"], timeout=10,
                               description="the voice keyer button to be released")
                check_equal(transmitting(app), False, "Transmitting a file that isn't a sound file")

                # Recording still works once there's somewhere to put it.
                set_writable(read_only, True)
                start_recording("both")
                check_equal(app.message_boxes(), [], "Errors displayed")
                check_equal(app.get("main.btnRecord")["value"], True, "Record button pressed")
                app.click("main.btnRecord")
                check_equal(len(os.listdir(read_only)), 1, "Decoded recordings")
                stop_modem(app)
                quit_and_check_log(app)
    finally:
        if os.path.isdir(os.path.join(folder, "read_only")):
            set_writable(os.path.join(folder, "read_only"), True)
        shutil.rmtree(folder, ignore_errors=True)


def test_monitor_volume():
    """How loudly what's transmitted is played back, for the microphone and
    for the voice keyer."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        message = os.path.join(folder, "message.wav")
        write_tone(message, seconds=20, rate=16000)
        voice_keyer = dict(VoiceKeyer__WaveFilePath=folder, VoiceKeyer__WaveFile="message.wav", VoiceKeyer__Repeats=0)

        def decibels(before, after):
            return 20 * math.log10(after / before)

        def popped_up(app):
            try:
                return app.get("monitorVolume")["shown"]
            except freedvtest.CommandError:
                return False  # there isn't one

        def adjust(app, button, setting, to):
            app.context_menu(button, choose="Adjust Monitor Volume...")
            app.wait_until(lambda: popped_up(app), description="the volume to pop up")
            check_equal(app.get("monitorVolume.slider")["value"], int(app.state()[setting]), "Volume shown")
            app.set_value("monitorVolume.slider", to)
            check_equal(app.state()[setting], to, setting)
            # Clicking elsewhere puts it away.
            app.close("monitorVolume")
            app.wait_until(lambda: not popped_up(app), description="the volume to go away")

        def monitored(app, button):
            """Returns how loud what's transmitted is when played back."""
            app.click(button)
            wait_for_transmit(app)
            time.sleep(0.75)
            heard = level(SPEAKER_MONITOR)
            app.click(button)
            wait_for_transmit(app, False)
            return heard

        with audio_devices():
            with freedvtest.start(settings=settings(**voice_keyer)) as app:
                app.wait_for_window("main")
                start_modem(app)
                check_equal((app.state()["monitorTxAudioVol"], app.state()["monitorVoiceKeyerAudioVol"]), (0, 0),
                            "Volumes to begin with")

                # The microphone
                app.context_menu("main.btnPTT", choose="Monitor transmitted audio")
                # (When the microphone and speaker are the same device, as on
                # Windows CI (see freedvtest.audio.shared()), the tone played
                # into the one drowns out what's monitored through the other,
                # so only the volume is changed.)
                measure = not freedvtest.audio.shared(MICROPHONE, SPEAKER)
                with Tone(MICROPHONE):
                    if measure:
                        loud = monitored(app, "main.btnPTT")
                        check(loud > AUDIBLE, "Transmitted audio isn't being monitored (level %d)" % loud)
                    adjust(app, "main.btnPTT", "monitorTxAudioVol", -20)
                    if measure:
                        quiet = monitored(app, "main.btnPTT")
                        check(-23 < decibels(loud, quiet) < -17,
                              "Turning the monitor down 20 dB took it from %d to %d" % (loud, quiet))
                    adjust(app, "main.btnPTT", "monitorTxAudioVol", -6)
                    if measure:
                        half = monitored(app, "main.btnPTT")
                        check(-8.5 < decibels(loud, half) < -3.5,
                              "The monitor at -6 dB took it from %d to %d" % (loud, half))
                app.context_menu("main.btnPTT", choose="Monitor transmitted audio")

                # The voice keyer, which has a volume of its own
                app.context_menu("main.btnVoiceKeyer", choose="Monitor transmitted audio")
                check_equal(app.state()["monitorVoiceKeyerAudioVol"], 0, "Voice keyer's volume")
                loud = monitored(app, "main.btnVoiceKeyer")
                check(loud > AUDIBLE, "The voice keyer isn't being monitored (level %d)" % loud)
                adjust(app, "main.btnVoiceKeyer", "monitorVoiceKeyerAudioVol", -20)
                quiet = monitored(app, "main.btnVoiceKeyer")
                check(-23 < decibels(loud, quiet) < -17,
                      "Turning the voice keyer's monitor down 20 dB took it from %d to %d" % (loud, quiet))
                check_equal(app.state()["monitorTxAudioVol"], -6, "Microphone's monitor volume")

                stop_modem(app)
                quit_and_check_log(app)
                saved = freedvtest.read_config(app.config_path)
                check_equal((float(saved.get("/Monitor/TransmitAudioVol")), float(saved.get("/Monitor/VoiceKeyerAudioVol"))),
                            (-6.0, -20.0), "Saved volumes")
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_mouse_controls():
    """Things that are done with the mouse wheel and its other buttons."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        csv_log = os.path.join(folder, "heard.csv")
        with open(csv_log, "w") as f:
            f.write("date,time,callsign,mode,frequency_hz,snr_db\n")
            f.write("2026-03-04,05:06:07,K1ABC,RADEV1,14236000,7\n")
        extra = dict(Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare="DM12",
                     Reporting__CSV__LogFilePath=csv_log)

        def tx_level(app):
            return label(app, "main.textTxLevel")

        with audio_devices():
            with freedvtest.start(settings=settings(**extra)) as app:
                app.wait_for_window("main")
                start_modem(app)

                # The wheel over anything to do with TX attenuation is a small
                # step up or down.
                app.click("main.btnTxLevelDecrBig")
                start = tx_level(app)
                app.click("main.btnTxLevelDecr")
                one_down = tx_level(app)
                app.click("main.btnTxLevelIncr")
                check_equal(tx_level(app), start, "TX attenuation")
                for control in ["main.textTxLevel", "main.btnTxLevelDecr", "main.btnTxLevelIncrBig", "main.btnTune",
                                "main.boxTxLevel"]:
                    app.mouse(control, "wheel", rotation=-1)
                    check_equal(tx_level(app), one_down, "TX attenuation after wheeling down over " + control)
                    app.mouse(control, "wheel", rotation=1)
                    check_equal(tx_level(app), start, "TX attenuation after wheeling up over " + control)

                # While tuning, it's Tune's level that changes.
                app.click("main.btnTune")
                check_equal(tx_level(app), "0.0dB", "Tune attenuation")
                app.mouse("main.textTxLevel", "wheel", rotation=-1)
                check(tx_level(app) != "0.0dB", "The wheel didn't change Tune's attenuation")
                app.mouse("main.textTxLevel", "wheel", rotation=1)
                check_equal(tx_level(app), "0.0dB", "Tune attenuation")
                # The +/- buttons follow Tune too, not just the wheel.
                app.click("main.btnTxLevelDecr")
                check(tx_level(app) != "0.0dB", "Decrementing while tuning didn't change Tune's attenuation")
                app.click("main.btnTxLevelIncr")
                check_equal(tx_level(app), "0.0dB", "Tune attenuation after incrementing while tuning")
                app.click("main.btnTxLevelIncrBig")
                check_equal(tx_level(app), "0.0dB", "Tune attenuation after a big increment at the top")
                app.click("main.btnTxLevelDecrBig")
                check(tx_level(app) != "0.0dB", "Big decrement while tuning didn't change Tune's attenuation")
                app.click("main.btnTxLevelIncrBig")
                check_equal(tx_level(app), "0.0dB", "Tune attenuation back to the top")
                app.click("main.btnTune")
                check_equal(tx_level(app), start, "TX attenuation after tuning")

                # The middle button puts the speaker level back to normal.
                for control in ["main.sliderMicSpkrLevel", "main.textMicSpkrLevel"]:
                    app.set_value("main.sliderMicSpkrLevel", -80)
                    check_equal(label(app, "main.textMicSpkrLevel"), "-8.0dB", "Speaker level")
                    app.mouse(control, "middle_down")
                    check_equal((label(app, "main.textMicSpkrLevel"), app.get("main.sliderMicSpkrLevel")["value"]),
                                ("0.0dB", 0), "Speaker level after middle-clicking " + control)

                # Pressing on PTT and thinking better of it isn't transmitting.
                app.mouse("main.btnPTT", "left_down")
                app.mouse("main.btnPTT", "leave")
                time.sleep(0.5)
                check_equal((transmitting(app), app.get("main.btnPTT")["value"]), (False, False),
                            "Transmitting and PTT pressed")

                # Right-clicking the callsign that's been chosen unchooses it.
                app.combo_popup("main.cboLastReportedCallsigns", open=True)
                app.wait_until(lambda: app.get("main.listLastReportedCallsigns")["shown"])
                app.click("main.listLastReportedCallsigns", item=0)
                app.wait_until(lambda: not app.get("main.listLastReportedCallsigns")["shown"])
                check_equal(app.get("main.listLastReportedCallsigns")["selection"], 0, "Callsign selected")
                app.mouse("main.cboLastReportedCallsigns", "right_down")
                app.wait_until(lambda: app.get("main.listLastReportedCallsigns")["selection"] == -1,
                               description="the callsign to be unchosen")

                stop_modem(app)
                quit_and_check_log(app)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_play_file_loop():
    """Tools > Play File - From Radio with its Loop option."""
    start_item = "Tools/Start Play File - From Radio..."
    stop_item = "Tools/Stop Play File - From Radio..."
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        tone = os.path.join(folder, "tone.wav")
        write_tone(tone, seconds=1, rate=8000)

        with audio_devices():
            with freedvtest.start(settings=settings()) as app:
                app.wait_for_window("main")
                start_modem(app)
                app.click("main.btnAnalog")

                # A second of sound, over and over.
                app.choose_next_path(tone, loop=True)
                app.menu(start_item)
                check_equal(app.state()["loopPlayFileFromRadio"], True, "Looping")
                check_equal(app.get("main")["status"], "Playing file %s as radio input" % tone, "Status while playing")
                time.sleep(4)
                check_equal(app.get("main")["status"], "Playing file %s as radio input" % tone,
                            "Status well after the file's end")
                check_audible(SPEAKER_MONITOR, "The file isn't still being played")

                # Until it's stopped.
                app.menu(stop_item)
                check_equal(app.get("main")["status"], "", "Status after stopping")
                time.sleep(1.5)
                check_silent(SPEAKER_MONITOR, "Something is still being played through the speaker")

                # The next file isn't looped just because the last one was.
                app.choose_next_path(tone)
                app.menu(start_item)
                check_equal(app.state()["loopPlayFileFromRadio"], False, "Looping")
                app.wait_until(lambda: app.get("main")["status"] == "", timeout=15, description="the file to finish")

                stop_modem(app)
                quit_and_check_log(app)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_help_and_restore():
    """The Help menu's web pages and Tools > Restore defaults."""
    start = {"/Reporting/Callsign": CALLSIGN, "/Reporting/GridSquare": "DM12",
             "/Reporting/FreeDV/Hostname": NOWHERE, "/Reporting/FreeDV/UseTls": False}
    with freedvtest.start(settings=start) as app:
        app.wait_for_window("main")

        for item, url in [("Help/Check for Updates", "https://github.com/freedv/freedv-gui/releases"),
                          ("Help/User Manual...", "https://github.com/freedv/freedv-gui/blob/master/USER_MANUAL.pdf"),
                          ("Help/Get Assistance", "https://freedv.org/#getting-help")]:
            app.menu(item)
            check_equal(app.browser_urls(clear=True), [url], "Web pages opened by " + item)

        def callsign():
            return app.state()["config"]["reporting"]["reportingCallsign"]

        # Restoring defaults asks first.
        app.answer_next_message_box("no")
        app.menu("Tools/Restore defaults")
        check_equal([(box["caption"], box["message"]) for box in app.message_boxes(clear=True)],
                    [("Restore Defaults", "Would you like to restore configuration to defaults?")], "Message displayed")
        check_equal(callsign(), CALLSIGN, "Callsign after declining")

        app.answer_next_message_box("yes")
        app.menu("Tools/Restore defaults")
        app.wait_until(lambda: callsign() == "", timeout=10, description="the configuration to be restored")
        app.wait_until(lambda: len(app.message_boxes()) == 2, timeout=10, description="FreeDV to say that it's done")
        check_equal([box["message"] for box in app.message_boxes(clear=True)],
                    ["Would you like to restore configuration to defaults?",
                     "Config file/registry key successfully deleted."], "Messages displayed")
        check_equal(app.state()["config"]["reporting"]["reportingGridSquare"], "", "Grid square after restoring")
        check_equal(app.quit(), 0, "FreeDV's exit code")


def test_tune_level_per_band():
    """Tune's own attenuation can be kept separately for each band, like TX
    attenuation can."""
    def tx_level(app):
        return label(app, "main.textTxLevel")

    with audio_devices():
        with freedvtest.start(settings=settings(Reporting__Frequency=14236000)) as app:
            app.wait_for_window("main")
            start_modem(app)
            app.click("main.btnTune")
            check_equal(tx_level(app), "0.0dB", "Tune attenuation")
            app.click("main.btnTxLevelDecrBig")
            check_equal(tx_level(app), "-1.0dB", "Tune attenuation")

            enable, disable, restore = ("Enable auto-save of tune atten for 20m", "Disable auto-save of tune atten for 20m",
                                        "Restore tune atten level for 20m")
            items = app.context_menu("main.btnTune", choose=enable)
            check_equal([(item["label"], item["enabled"]) for item in items][1:], [(enable, True), (restore, False)],
                        "Tune button's menu")

            # Having wandered off, there's a way back to what it was.
            app.click("main.btnTxLevelDecrBig")
            app.click("main.btnTxLevelDecrBig")
            check_equal(tx_level(app), "-3.0dB", "Tune attenuation")
            items = app.context_menu("main.btnTune", choose=restore)
            check_equal([(item["label"], item["enabled"]) for item in items][1:], [(disable, True), (restore, True)],
                        "Tune button's menu with auto-save on")
            check_equal(tx_level(app), "-1.0dB", "Tune attenuation after restoring")

            # Off and on again
            app.context_menu("main.btnTune", choose=disable)
            items = app.context_menu("main.btnTune", choose=enable)
            check_equal([(item["label"], item["enabled"]) for item in items][1:], [(enable, True), (restore, False)],
                        "Tune button's menu after turning auto-save off")

            app.click("main.btnTune")
            stop_modem(app)
            quit_and_check_log(app)
            saved = freedvtest.read_config(app.config_path)
            check_equal(saved.get("/Audio/tuneLevelByBand/20m"), "-10", "Saved tune attenuation for 20m")


def test_reporting_without_callsign():
    """Reporting turned on without saying who's reporting."""
    for missing in ["Callsign", "GridSquare"]:
        who = dict(Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare="DM12")
        who["Reporting__" + missing] = ""
        with audio_devices():
            with freedvtest.start(settings=settings(**who)) as app:
                app.wait_for_window("main")
                app.click("main.btnOnOff")
                app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to start")
                app.wait_until(lambda: app.message_boxes(), timeout=10, description="FreeDV to say what's missing")
                check_equal([box["message"] for box in app.message_boxes(clear=True)],
                            ["Reporting requires a valid callsign and grid square in Tools->Settings. Reporting will "
                             "be disabled."], "Messages displayed without a %s" % missing)

                # It's only reporting that's given up on.
                check_equal(app.state()["running"], True, "Running without a %s" % missing)
                app.click("main.btnPTT")
                wait_for_transmit(app)
                app.click("main.btnPTT")
                wait_for_transmit(app, False)
                stop_modem(app)
                check_equal(app.quit(), 0, "FreeDV's exit code")


def test_band_names():
    """What's kept for each band goes by the band that FreeDV is on."""
    extra = dict(Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare="DM12",
                 Reporting__ManualFrequencyReporting=True, Reporting__Frequency=14236000)
    with audio_devices():
        with freedvtest.start(settings=settings(**extra)) as app:
            app.wait_for_window("main")
            start_modem(app)
            for frequency, band in [("1.8700", "160m"), ("3.6250", "80m"), ("5.4035", "60m"), ("7.1770", "40m"),
                                    ("10.1300", "30m"), ("14.2360", "20m"), ("18.1180", "17m"), ("21.3130", "15m"),
                                    ("24.9330", "12m"), ("28.3300", "10m"), ("144.2000", "vhfuhf")]:
                app.set_value("main.cboReportFrequency", frequency)
                items = app.context_menu("main.textTxLevel")
                check_equal([item["label"] for item in items],
                            ["Enable auto-save of TX atten for " + band, "Restore TX atten level for " + band],
                            "TX attenuation's menu on " + frequency)

            # Each band's is kept apart from the others'.
            app.set_value("main.cboReportFrequency", "7.1770")
            app.click("main.btnTxLevelDecrBig")
            app.context_menu("main.textTxLevel", choose="Enable auto-save of TX atten for 40m")
            app.set_value("main.cboReportFrequency", "14.2360")
            app.click("main.btnTxLevelDecrBig")
            app.context_menu("main.textTxLevel", choose="Enable auto-save of TX atten for 20m")
            check_equal(label(app, "main.textTxLevel"), "-2.0dB", "TX attenuation on 20m")
            app.set_value("main.cboReportFrequency", "7.1770")
            app.wait_until(lambda: label(app, "main.textTxLevel") == "-1.0dB", timeout=5,
                           description="40m's TX attenuation to come back")
            app.set_value("main.cboReportFrequency", "14.2360")
            app.wait_until(lambda: label(app, "main.textTxLevel") == "-2.0dB", timeout=5,
                           description="20m's TX attenuation to come back")

            # Somewhere that isn't in any band has nothing kept for it.
            app.set_value("main.cboReportFrequency", "0.5000")
            try:
                items = app.context_menu("main.textTxLevel")
                check(False, "TX attenuation has a menu outside the bands: %r" % items)
            except freedvtest.CommandError as e:
                check("no popup menu appeared" in str(e), "Right-clicking TX attenuation failed: %s" % e)

            # Tune's menu still exists outside the bands, it just has
            # nothing band-specific in it.
            items = app.context_menu("main.btnTune")
            check_equal([item["label"] for item in items], ["Set tune output to minimum (-30 dB)"],
                        "Tune button's menu outside the bands")

            stop_modem(app)
            quit_and_check_log(app)
            saved = freedvtest.read_config(app.config_path)
            check_equal((saved.get("/Audio/transmitLevelByBand/40m"), saved.get("/Audio/transmitLevelByBand/20m")),
                        ("-10", "-20"), "Saved TX attenuation for 40m and 20m")


def test_exit_menu():
    """File > Exit, which has to stop everything first."""
    with audio_devices():
        # With nothing going on
        with freedvtest.start(settings=settings()) as app:
            app.wait_for_window("main")
            app.menu(EXIT_MENU_ITEM)
            check_equal(app.wait_for_exit(), 0, "FreeDV's exit code")

        # While transmitting
        with freedvtest.start(settings=settings()) as app:
            app.wait_for_window("main")
            start_modem(app)
            app.click("main.btnPTT")
            wait_for_transmit(app)
            app.menu(EXIT_MENU_ITEM)
            check_equal(app.wait_for_exit(), 0, "FreeDV's exit code having been transmitting")
            errors = [line for line in app.log().splitlines() if " ERROR " in line or " FATAL " in line]
            check_equal(errors, [], "Errors logged by FreeDV")
        check_silent(RADIO_TX_MONITOR, "Something is still being sent to the radio")


def test_frequency_units():
    """Changing between MHz and kHz changes what's already displayed."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        csv_log = os.path.join(folder, "heard.csv")
        with open(csv_log, "w") as f:
            f.write("date,time,callsign,mode,frequency_hz,snr_db\n")
            f.write("2026-03-04,05:06:07,K1ABC,RADEV1,14236000,7\n")
            f.write("2026-03-04,05:16:07,VK5DGR,RADEV1,7177500,3\n")
        extra = dict(Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare="DM12",
                     Reporting__CSV__LogFilePath=csv_log, Reporting__Frequency=14236000)

        def heard(app):
            return [(item["columns"][0], item["columns"][1].replace(",", ""))
                    for item in app.get("main.listLastReportedCallsigns")["items"]]

        def set_units(app, as_khz):
            app.menu("Settings/Edit Settings...")
            app.wait_for_window("settings")
            app.set_value("settings.notebook", "Rig Control")
            app.set_value("settings.ckboxFrequencyEntryAsKHz", as_khz)
            app.click("settings.sdbSizer5OK")
            app.wait_for_window_closed("settings")

        with freedvtest.start(settings=settings(**extra)) as app:
            app.wait_for_window("main")
            check_equal(heard(app), [("VK5DGR", "7.1775"), ("K1ABC", "14.2360")], "Callsigns heard")
            check_equal(app.get("main.cboReportFrequency")["value"].replace(",", ""), "14.2360", "Report frequency")

            set_units(app, True)
            check_equal(heard(app), [("VK5DGR", "7177.5"), ("K1ABC", "14236.0")], "Callsigns heard, in kHz")
            check_equal(app.get("main.cboReportFrequency")["value"].replace(",", ""), "14236.0",
                        "Report frequency, in kHz")

            set_units(app, False)
            check_equal(heard(app), [("VK5DGR", "7.1775"), ("K1ABC", "14.2360")], "Callsigns heard, back in MHz")
            check_equal(app.get("main.cboReportFrequency")["value"].replace(",", ""), "14.2360",
                        "Report frequency, back in MHz")
            check_equal(app.quit(), 0, "FreeDV's exit code")
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_default_config():
    """Started without being told which configuration to use, as it is for
    a user: FreeDV finds its own, and goes back to whichever other one was
    last chosen."""
    home = tempfile.mkdtemp(prefix="freedvtest-home-")
    try:
        other = os.path.join(home, "other.conf")
        quiet = {"/Reporting/FreeDV/Hostname": NOWHERE, "/Reporting/FreeDV/UseTls": False}
        freedvtest.write_config(freedvtest.default_config_path(home), dict(quiet, **{"/Reporting/Callsign": "K1ABC"}))
        freedvtest.write_config(other, dict(quiet, **{"/Reporting/Callsign": "VK5DGR"}))

        def callsign(app):
            return app.state()["config"]["reporting"]["reportingCallsign"]

        def launch():
            return freedvtest.start(home=home, default_config=True)

        with launch() as app:
            app.wait_for_window("main")
            check_equal(callsign(app), "K1ABC", "Callsign from the default configuration")
            check(".conf)" not in app.get("main")["label"], "Window title is %r" % app.get("main")["label"])

            # Changes are kept there.
            app.menu("Settings/Edit Settings...")
            app.wait_for_window("settings")
            app.set_value("settings.notebook", "Reporting")
            app.set_value("settings.ckboxReportingEnable", True)
            app.set_value("settings.txt_callsign", "K1ABC/P")
            app.set_value("settings.txt_grid_square", "FN42")
            app.click("settings.sdbSizer5OK")
            app.wait_for_window_closed("settings")
            check_equal(app.quit(), 0, "FreeDV's exit code")

        with launch() as app:
            app.wait_for_window("main")
            check_equal(callsign(app), "K1ABC/P", "Callsign the next time")

            # Another configuration is remembered as the one in use...
            app.choose_next_path(other)
            app.menu("Settings/Use Configuration...")
            app.wait_until(lambda: callsign(app) == "VK5DGR", timeout=10,
                           description="the other configuration to be in use")
            check_equal(app.quit(), 0, "FreeDV's exit code")

        with launch() as app:
            app.wait_for_window("main")
            check_equal(callsign(app), "VK5DGR", "Callsign the next time")
            check("(other.conf)" in app.get("main")["label"], "Window title is %r" % app.get("main")["label"])
            check_equal(app.quit(), 0, "FreeDV's exit code")

        # ...for as long as it exists.
        os.rename(other, other + ".gone")
        with launch() as app:
            app.wait_for_window("main")
            check_equal(callsign(app), "K1ABC/P", "Callsign with the other configuration gone")
            check(".conf)" not in app.get("main")["label"], "Window title is %r" % app.get("main")["label"])
            check_equal(app.quit(), 0, "FreeDV's exit code")
        os.rename(other + ".gone", other)
        with launch() as app:
            app.wait_for_window("main")
            check_equal(callsign(app), "K1ABC/P", "Callsign with the other configuration back but forgotten")

            # Loading the default configuration forgets it as well.
            app.choose_next_path(other)
            app.menu("Settings/Use Configuration...")
            app.wait_until(lambda: callsign(app) == "VK5DGR", timeout=10,
                           description="the other configuration to be in use")
            app.answer_next_message_box("yes")
            app.menu("Settings/Load Default Configuration")
            try:
                app.wait_until(lambda: callsign(app) == "K1ABC/P", timeout=10)
            except freedvtest.FreeDVError:
                check_equal(callsign(app), "K1ABC/P", "Callsign after loading the default configuration (messages: %r)"
                            % app.message_boxes())
            app.message_boxes(clear=True)
            check_equal(app.quit(), 0, "FreeDV's exit code")

        with launch() as app:
            app.wait_for_window("main")
            check_equal(callsign(app), "K1ABC/P", "Callsign after loading the default configuration")
            check_equal(app.quit(), 0, "FreeDV's exit code")
    finally:
        shutil.rmtree(home, ignore_errors=True)

def test_config_migration_failure():
    """When FreeDV can't move an older configuration file into the newer
    place, it says so in its log and carries on without migrating."""
    if not sys.platform.startswith("linux"):
        # Only Linux has an older place for it to move from (and only with
        # wxWidgets 3.3 or later; see below).
        raise freedvtest.audio.Unavailable("configuration files are only migrated on Linux")
    quiet = {"/Reporting/FreeDV/Hostname": NOWHERE, "/Reporting/FreeDV/UseTls": False}

    def old_file(home):
        return os.path.join(home, ".freedv")

    def migrated(home):
        return os.path.join(home, ".config", "freedv", "freedv.conf")

    # The folder it wants to copy into can't be created: the one above it
    # isn't writable.
    home = tempfile.mkdtemp(prefix="freedvtest-home-")
    try:
        freedvtest.write_config(old_file(home), dict(quiet, **{"/Reporting/Callsign": "K1ABC"}))
        os.mkdir(os.path.join(home, ".config"))
        os.chmod(os.path.join(home, ".config"), 0o500)
        with freedvtest.start(home=home, default_config=True) as app:
            app.wait_for_window("main")
            output = open(app.log_path, errors="replace").read()
            if "Determining if we need to migrate" not in output:
                # Built with a wxWidgets older than 3.3, which has no newer
                # place to move it to.
                raise freedvtest.audio.Unavailable("this FreeDV doesn't migrate configuration files")
            check("Could not create folder" in output,
                  "FreeDV's output when its new settings folder can't be created")
            check_equal(app.quit(), 0, "FreeDV's exit code (unreadable folder)")
        check(not os.path.exists(migrated(home)), "Migrated configuration file exists (unreadable folder)")
    finally:
        os.chmod(os.path.join(home, ".config"), 0o700)
        shutil.rmtree(home, ignore_errors=True)

    # The folder is there, but the copy itself is refused.
    home = tempfile.mkdtemp(prefix="freedvtest-home-")
    try:
        freedvtest.write_config(old_file(home), dict(quiet, **{"/Reporting/Callsign": "K1ABC"}))
        os.makedirs(os.path.join(home, ".config", "freedv"))
        os.chmod(os.path.join(home, ".config", "freedv"), 0o500)
        with freedvtest.start(home=home, default_config=True) as app:
            app.wait_for_window("main")
            check("Could not copy" in open(app.log_path, errors="replace").read(),
                  "FreeDV's output when the configuration copy is refused")
            check_equal(app.quit(), 0, "FreeDV's exit code (unwritable folder)")
        check(not os.path.exists(migrated(home)), "Migrated configuration file exists (unwritable folder)")
    finally:
        os.chmod(os.path.join(home, ".config", "freedv"), 0o700)
        shutil.rmtree(home, ignore_errors=True)


def test_appearance_change():
    """The desktop switching between light and dark while FreeDV is running."""
    with audio_devices():
        with freedvtest.start(settings=settings()) as app:
            app.wait_for_window("main")
            app.menu("Tools/FreeDV Reporter")
            app.wait_for_window("reporter")
            app.menu("Tools/Filter Audio...")
            app.wait_for_window("filter")
            start_modem(app)
            for tab in TABS:
                app.set_value("main.notebook", tab)
                app.system_colours_changed()
                time.sleep(0.3)
            check_equal(sorted(window["name"] for window in app.windows() if window["shown"]),
                        ["filter", "main", "reporter"], "Windows displayed")
            check_equal(app.state()["running"], True, "Running")
            stop_modem(app)
            app.system_colours_changed()
            quit_and_check_log(app)

        # With a frequency chosen, which is drawn in the new colours too. One
        # that's typed in to more places than FreeDV works in is rounded to
        # the nearest 100 Hz.
        with freedvtest.start(settings=settings(Reporting__Frequency=14236000)) as app:
            app.wait_for_window("main")
            app.system_colours_changed()
            check_equal(app.get("main.cboReportFrequency")["value"], "14.2360", "Frequency displayed")
            app.set_value("main.cboReportFrequency", "14.23606")
            app.focus("main.cboReportFrequency", lost=True)
            app.system_colours_changed()
            quit_and_check_log(app)
            check_equal(int(freedvtest.read_config(app.config_path)["/Reporting/Frequency"]), 14236100,
                        "Frequency saved after typing in 14.23606")


def test_time_out_timer():
    """The time-out timer, which stops a transmission that's gone on too long."""
    with audio_devices():
        # A warning appears when there's 15 seconds left.
        with freedvtest.start(settings=settings(Rig__TotTimerSecs=18)) as app:
            app.wait_for_window("main")
            start_modem(app)

            app.click("main.btnPTT")
            wait_for_transmit(app)
            check(not app.window_exists("totWarning"), "The time-out warning appeared straight away")
            warning = app.wait_for_window("totWarning", timeout=10)
            check_equal(warning["label"], "Time-Out Timer Warning", "Warning's title")
            check(re.match(r"^1[0-5] seconds remaining$", label(app, "totWarning.countdownText")),
                  "Countdown is %r" % label(app, "totWarning.countdownText"))
            first = int(label(app, "totWarning.countdownText").split()[0])
            time.sleep(2.5)
            second = int(label(app, "totWarning.countdownText").split()[0])
            check(second < first, "The countdown went from %d to %d" % (first, second))

            # It can be put off...
            app.click("totWarning.btnExtend")
            app.wait_for_window_closed("totWarning")
            check_equal(transmitting(app), True, "Transmitting after resetting the timer")

            # ...in which case it comes around again, and then does stop
            # the transmission.
            app.wait_for_window("totWarning", timeout=10)
            wait_for_transmit(app, False, timeout=25)
            check(not app.window_exists("totWarning"), "The time-out warning is still there")
            check_equal(app.get("main.btnPTT")["value"], False, "PTT button pressed after timing out")

            # Ending the transmission oneself gets rid of the warning too.
            app.click("main.btnPTT")
            wait_for_transmit(app)
            app.wait_for_window("totWarning", timeout=10)
            app.click("main.btnPTT")
            wait_for_transmit(app, False)
            check(not app.window_exists("totWarning"), "The time-out warning is still there")

            stop_modem(app)
            quit_and_check_log(app)

        # The PTT key while the warning window has the focus ends the
        # transmission in latch mode.
        with freedvtest.start(settings=settings(Rig__TotTimerSecs=18)) as app:
            app.wait_for_window("main")
            app.activate("main")
            start_modem(app)
            app.click("main.btnPTT")
            wait_for_transmit(app)
            app.wait_for_window("totWarning", timeout=10)
            app.key("main.notebook", "Space")
            wait_for_transmit(app, False)
            check(not app.window_exists("totWarning"), "The time-out warning is still there")
            stop_modem(app)
            quit_and_check_log(app)

        # It can be turned off.
        with freedvtest.start(settings=settings(Rig__TotTimerSecs=5, Rig__TotTimerEnabled=False)) as app:
            app.wait_for_window("main")
            start_modem(app)
            app.click("main.btnPTT")
            wait_for_transmit(app)
            time.sleep(8)
            check_equal(transmitting(app), True, "Transmitting past a time-out that's turned off")
            check(not app.window_exists("totWarning"), "The time-out warning appeared with the timer turned off")
            app.click("main.btnPTT")
            wait_for_transmit(app, False)
            stop_modem(app)
            quit_and_check_log(app)

        # With no length given, there's no time-out to arm.
        with freedvtest.start(settings=settings(Rig__TotTimerEnabled=True, Rig__TotTimerSecs=0)) as app:
            app.wait_for_window("main")
            start_modem(app)
            app.click("main.btnPTT")
            wait_for_transmit(app)
            time.sleep(4)
            check_equal(transmitting(app), True, "Transmitting past a time-out of no length")
            check(not app.window_exists("totWarning"), "The time-out warning appeared with no length to count down")
            app.click("main.btnPTT")
            wait_for_transmit(app, False)
            stop_modem(app)
            quit_and_check_log(app)


def test_voice_keyer_time_out():
    """The time-out timer stops the voice keyer too, when what it's sending
    goes on for too long."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        write_tone(os.path.join(folder, "long.wav"), seconds=20, rate=16000)
        extra = dict(VoiceKeyer__WaveFilePath=folder, VoiceKeyer__WaveFile="long.wav", VoiceKeyer__Repeats=3,
                     VoiceKeyer__RxPause=2, Rig__TotTimerSecs=6)
        with audio_devices():
            with freedvtest.start(settings=settings(**extra)) as app:
                app.wait_for_window("main")
                start_modem(app)
                app.click("main.btnVoiceKeyer")
                wait_for_transmit(app)
                app.wait_for_window("totWarning", timeout=10)
                wait_for_transmit(app, False, timeout=15)
                check(not app.window_exists("totWarning"), "The time-out warning is still there")
                app.wait_until(lambda: not app.get("main.btnVoiceKeyer")["value"], timeout=10,
                               description="the voice keyer to stop")
                check_equal(label(app, "main.btnVoiceKeyer"), "Start Voice Keyer\nlong", "Voice keyer button's label")
                check_equal(count_transmissions(app, 5), 0, "Transmissions after the voice keyer timed out")
                stop_modem(app)
                quit_and_check_log(app)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_ptt_key_time_out():
    """A PTT key held through the time-out timer, and one let go too early."""
    with audio_devices():
        # A key that's still being held when the time-out timer stops TX must
        # not key the rig again until it's actually released -- a finger
        # resting on the key would otherwise defeat the time-out completely.
        with freedvtest.start(settings=settings(Rig__PttMomentaryMode=True, Rig__PttKeyCode=WXK_F5,
                                                Rig__TotTimerSecs=8)) as app:
            app.wait_for_window("main")
            app.activate("main")
            start_modem(app)

            app.key("main.notebook", "F5", hold=True)
            wait_for_transmit(app)
            wait_for_transmit(app, False, timeout=25)
            check_equal(app.get("main.btnPTT")["value"], False, "PTT button pressed after timing out")

            # The key is still being held, so pressing it again must not
            # restart TX.  The warning window that the time-out put up is
            # gone by now, so take the keyboard back first.
            app.activate("main")
            app.key("main.notebook", "F5", hold=True)
            time.sleep(2)
            check_equal(transmitting(app), False,
                        "Transmitting after pressing the PTT key again while it was still held")

            # Letting go re-arms it, and the next press works as it always did.
            app.key("main.notebook", "F5", release=True)
            time.sleep(1)
            app.key("main.notebook", "F5", hold=True)
            wait_for_transmit(app)
            app.key("main.notebook", "F5", release=True)
            wait_for_transmit(app, False)

            stop_modem(app)
            quit_and_check_log(app)

        # Clicking the PTT button, with the key in momentary mode, still
        # runs the time-out out to the end.
        with freedvtest.start(settings=settings(Rig__PttMomentaryMode=True, Rig__TotTimerSecs=8)) as app:
            app.wait_for_window("main")
            start_modem(app)
            app.click("main.btnPTT")
            wait_for_transmit(app)
            wait_for_transmit(app, False, timeout=25)
            check_equal(app.get("main.btnPTT")["value"], False, "PTT button pressed after timing out")
            stop_modem(app)
            quit_and_check_log(app)

        # Letting go of the key while TX is still starting must not leave the
        # rig keyed. A delay before TX begins makes that window easy to hit.
        with freedvtest.start(settings=settings(Rig__PttMomentaryMode=True, Rig__PttKeyCode=WXK_F5,
                                                Audio__TxRxDelayMilliseconds=2000)) as app:
            app.wait_for_window("main")
            app.activate("main")
            start_modem(app)

            app.key("main.notebook", "F5", hold=True)
            # The button goes down as soon as the key is pressed, but TX
            # itself is still waiting out the delay before it starts.
            app.wait_until(lambda: app.get("main.btnPTT")["value"], description="the PTT button to go down")
            check(not transmitting(app), "TX started before the delay for it to start had passed")

            app.key("main.notebook", "F5", release=True)

            # TX starts anyway, and then stops by itself because the key went
            # up while it was starting.
            wait_for_transmit(app)
            wait_for_transmit(app, False, timeout=25)
            check_equal(app.get("main.btnPTT")["value"], False, "PTT button pressed after letting go early")

            stop_modem(app)
            quit_and_check_log(app)


def test_menus():
    """The Settings and Help menus, other than what opens other windows.
    (File > Keep On Top isn't offered on Linux.)"""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        exported = os.path.join(folder, "exported.conf")
        other = os.path.join(folder, "other.conf")
        empty = os.path.join(folder, "empty.conf")
        freedvtest.write_config(other, {"/Reporting/Callsign": "K1ABC", "/Reporting/GridSquare": "FN42",
                                        "/Reporting/FreeDV/Hostname": NOWHERE, "/Reporting/FreeDV/UseTls": False})
        open(empty, "w").close()

        start = {"/Reporting/Callsign": CALLSIGN, "/Reporting/GridSquare": "DM12",
                 "/Reporting/FreeDV/Hostname": NOWHERE, "/Reporting/FreeDV/UseTls": False}
        with freedvtest.start(settings=start) as app:
            app.wait_for_window("main")
            title = app.get("main")["label"]
            check(title.startswith("FreeDV ") and "(freedv.conf)" in title, "Window title is %r" % title)

            def callsign():
                return app.state()["config"]["reporting"]["reportingCallsign"]

            # Help > About
            app.menu("Help/About FreeDV")
            boxes = app.message_boxes(clear=True)
            check_equal([box["caption"] for box in boxes], ["About"], "Message displayed")
            check(boxes[0]["message"].startswith("FreeDV GUI ") and "freedv-gui git hash:" in boxes[0]["message"],
                  "About says %r" % boxes[0]["message"])

            # Settings > Export Configuration
            app.menu("Settings/Export Configuration...")
            check_equal(app.path_dialogs(clear=True), ["Export FreeDV Configuration"], "File choosers displayed")
            check(not os.path.exists(exported), "Something was exported after cancelling")
            app.choose_next_path(exported)
            app.menu("Settings/Export Configuration...")
            app.wait_until(lambda: os.path.exists(exported), timeout=5, description="the configuration to be exported")
            check_equal(freedvtest.read_config(exported).get("/Reporting/Callsign"), CALLSIGN, "Exported callsign")

            # Settings > Use Configuration
            app.menu("Settings/Use Configuration...")
            check_equal(app.path_dialogs(clear=True), ["Export FreeDV Configuration", "Import FreeDV Configuration"],
                        "File choosers displayed")
            check_equal(callsign(), CALLSIGN, "Callsign after cancelling")

            for path, message in [(os.path.join(folder, "missing.conf"), "The selected file does not exist."),
                                  (empty, "The selected file could not be parsed as a FreeDV configuration.")]:
                app.choose_next_path(path)
                app.menu("Settings/Use Configuration...")
                check_equal([box["message"] for box in app.message_boxes(clear=True)], [message],
                            "Message displayed for %s" % os.path.basename(path))
                check_equal(callsign(), CALLSIGN, "Callsign after a file that couldn't be used")
                check_equal(app.get("main")["label"], title, "Window title")

            app.choose_next_path(other)
            app.menu("Settings/Use Configuration...")
            try:
                app.wait_until(lambda: callsign() == "K1ABC", timeout=10)
            except freedvtest.FreeDVError:
                pass
            check_equal(app.message_boxes(), [], "Messages displayed")
            check_equal(callsign(), "K1ABC", "Callsign after using another configuration")
            check("(other.conf)" in app.get("main")["label"], "Window title is %r" % app.get("main")["label"])
            # (Restoring defaults below deletes the file that's in use, so
            # this checks here that using one left the original alone.)
            check_equal(freedvtest.read_config(other).get("/Reporting/Callsign"), "K1ABC",
                        "Callsign in the file that was used")

            # Settings > Restore defaults, which also closes the reporter
            # window that startup set up.
            app.answer_next_message_box("yes")
            app.menu("Tools/Restore defaults")
            app.wait_until(lambda: callsign() == "", timeout=10,
                           description="the configuration to be reset")
            app.wait_until(lambda: any("successfully deleted" in b["message"] for b in app.message_boxes()),
                           timeout=10, description="FreeDV to say the restore is done")
            app.message_boxes(clear=True)
            # The PTT key with no reporter window around at all.
            app.key("main.notebook", "Space")
            time.sleep(0.5)

            # Restoring defaults again, with no reporter window to close.
            app.answer_next_message_box("yes")
            app.menu("Tools/Restore defaults")
            app.wait_until(lambda: any("successfully deleted" in b["message"] for b in app.message_boxes()),
                           timeout=10, description="FreeDV to say the second restore is done")
            app.message_boxes(clear=True)
            # Restoring defaults deleted the configuration file that was in
            # use, so getting the reporter window back needs a new one.
            another = os.path.join(folder, "another.conf")
            freedvtest.write_config(another, {"/Reporting/Callsign": "K1ABC", "/Reporting/GridSquare": "FN42",
                                              "/Reporting/FreeDV/Hostname": NOWHERE, "/Reporting/FreeDV/UseTls": False})
            app.choose_next_path(another)
            app.menu("Settings/Use Configuration...")
            app.wait_until(lambda: callsign() == "K1ABC", timeout=10, description="the new configuration to be used")
            app.menu("Tools/FreeDV Reporter")
            app.wait_for_window("reporter")
            check_equal(app.message_boxes(), [], "Messages displayed with the reporter window made afresh")

            # Settings > Load Default Configuration, which asks first.
            app.answer_next_message_box("no")
            app.menu("Settings/Load Default Configuration")
            check_equal([box["message"] for box in app.message_boxes(clear=True)],
                        ["This will load the default FreeDV configuration. Are you sure?"], "Message displayed")
            check_equal(callsign(), "K1ABC", "Callsign after declining")

            app.answer_next_message_box("yes")
            app.menu("Settings/Load Default Configuration")
            try:
                app.wait_until(lambda: callsign() == "", timeout=10)
            except freedvtest.FreeDVError:
                pass
            app.message_boxes(clear=True)
            check_equal(callsign(), "", "Callsign after loading the default configuration")
            check(".conf)" not in app.get("main")["label"], "Window title is %r" % app.get("main")["label"])

            check_equal(app.quit(), 0, "FreeDV's exit code")

        # What was in use before is left as it was.
        check_equal(freedvtest.read_config(another).get("/Reporting/Callsign"), "K1ABC", "Callsign in another.conf")
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_tab_layout():
    """With experimental features on, how the tabs are laid out is kept from
    one run to the next."""
    with freedvtest.start(settings=settings(ExperimentalFeatures=True)) as app:
        app.wait_for_window("main")
        check_equal(app.get("main.notebook")["pages"], TABS, "Tabs")
        check_equal(app.quit(), 0, "FreeDV's exit code")
        config = app.config_path
        saved = freedvtest.read_config(config)
        check(saved.get("/MainFrame/TabLayout"), "No tab layout was saved: %r" % saved.get("/MainFrame/TabLayout"))

        # Along with the layout comes the tab that was being looked at before
        # the last transmission.
        saved["/MainFrame/rxNbookCtrl"] = TABS.index("SNR")
        freedvtest.write_config(config, saved)
        with freedvtest.start(config=config) as again:
            again.wait_for_window("main")
            check_equal(sorted(again.get("main.notebook")["pages"]), sorted(TABS), "Tabs the second time")
            check_equal(again.get("main.notebook")["value"], TABS.index("SNR"), "Tab displayed the second time")
            check_equal(again.message_boxes(), [], "Messages displayed")
            check_equal(again.quit(), 0, "FreeDV's exit code the second time")
        check(freedvtest.read_config(config).get("/MainFrame/TabLayout"), "No tab layout was saved the second time")

    # Without them, it isn't.
    with freedvtest.start(settings=settings()) as app:
        app.wait_for_window("main")
        check_equal(app.quit(), 0, "FreeDV's exit code")
        check(not freedvtest.read_config(app.config_path).get("/MainFrame/TabLayout"),
              "A tab layout was saved without experimental features")


def test_heard_list():
    """The list of stations heard starts out with the most recent ones in
    the log of them."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        csv_log = os.path.join(folder, "heard.csv")
        callsigns = ["K%d%s" % (n % 10, "ABCDEFGHIJKL"[n // 10] * 3) for n in range(120)]
        with open(csv_log, "w") as f:
            f.write("date,time,callsign,mode,frequency_hz,snr_db\n")
            for n, callsign in enumerate(callsigns):
                f.write("2026-03-04,05:%02d:%02d,%s,RADEV1,%d,%d\n"
                        % (n // 60, n % 60, callsign, 14236000 + n * 500, n % 20))
                if n == 110:
                    # Things that aren't stations heard are passed over.
                    f.write("\n")
                    f.write("2026-03-04,06:00:00,NOFREQ\n")
                    f.write("2026-03-04,06:00:01,BADFREQ,RADEV1,somewhere,3\n")
                    f.write("2026-03-04\n")
                    f.write("2026-03-04,06:00:02\n")
                    f.write("2026-03-04,06:00:03,NOFREQ,RADEV1\n")
                    f.write("2026-03-04,06:00:04,NOSNR,RADEV1,14236000\n")
                    f.write("2026-03-04,06:00:05,BADSNR,RADEV1,14236000,loud\n")

        def heard(app):
            return [(item["columns"][0], item["columns"][1].replace(",", ""))
                    for item in app.get("main.listLastReportedCallsigns")["items"]]

        # Only what's in the last hundred lines, the latest first.
        extra = dict(Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare="DM12",
                     Reporting__CSV__LogFilePath=csv_log)
        expected = [(callsign, 14236000 + n * 500) for n, callsign in enumerate(callsigns)][-93:][::-1]
        with freedvtest.start(settings=settings(**extra)) as app:
            app.wait_for_window("main")
            check_equal(heard(app), [(callsign, "%.4f" % (frequency / 1e6)) for callsign, frequency in expected],
                        "Stations heard")
            check_equal(app.quit(), 0, "FreeDV's exit code")

        # In kHz, for those who have frequencies that way.
        with freedvtest.start(settings=settings(Reporting__FrequencyAsKHz=True, **extra)) as app:
            app.wait_for_window("main")
            check_equal(heard(app), [(callsign, "%.1f" % (frequency / 1e3)) for callsign, frequency in expected],
                        "Stations heard, in kHz")
            check_equal(app.quit(), 0, "FreeDV's exit code")

        # A log that isn't there (yet) is nothing to complain about.
        extra["Reporting__CSV__LogFilePath"] = os.path.join(folder, "missing", "heard.csv")
        with freedvtest.start(settings=settings(**extra)) as app:
            app.wait_for_window("main")
            check_equal(heard(app), [], "Stations heard without a log")
            check_equal(app.message_boxes(), [], "Messages displayed")
            check_equal(app.quit(), 0, "FreeDV's exit code")
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_window_geometry():
    """A saved window position or size that makes no sense isn't used."""
    def saved_geometry(left, top, width, height):
        extra = dict(MainFrame__left=left, MainFrame__top=top, MainFrame__width=width, MainFrame__height=height)
        with freedvtest.start(settings=settings(**extra)) as app:
            app.wait_for_window("main")
            check_equal(app.message_boxes(), [], "Messages displayed")
            quit_and_check_log(app)
            saved = freedvtest.read_config(app.config_path)
        return tuple(int(saved["/MainFrame/" + name]) for name in ("left", "top", "width", "height"))

    def check_sensible(geometry, what, smallest=200):
        left, top, width, height = geometry
        # (Where it's put is reported from inside its title bar and border,
        # which can be a little off the edge of where it was asked to go.)
        check(-100 <= left <= 2048 and -100 <= top <= 2048 and
              smallest <= width <= 2048 and smallest <= height <= 2048,
              "With %s, the window ended up at %d,%d and %dx%d" % (what, left, top, width, height))

    check_sensible(saved_geometry(-500, -500, -800, -600), "everything negative")
    check_sensible(saved_geometry(50000, 50000, 50000, 50000), "everything enormous")
    check_sensible(saved_geometry(30, 40, 10, 10), "a size that nothing fits in")

    # One that does make sense is kept. (It has to fit on CI's 1024x768
    # screens: Windows makes no window much taller than the screen.)
    left, top, width, height = saved_geometry(30, 40, 900, 700)
    check((width, height) == (900, 700), "A 900x700 window was saved as %dx%d" % (width, height))


def test_receive_reports():
    """Where a callsign that's been received goes other than FreeDV Reporter:
    the list of stations heard, the log of them and to other programs
    listening for them. As in test_receive(), FreeDV receives itself."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        listener.bind(("127.0.0.1", 0))
        listener.settimeout(20)
        csv_log = os.path.join(folder, "heard.csv")
        # The log already holds the very callsign and frequency about to be
        # heard, so the report takes the place of that entry from the log
        # instead of merely being added to the front of the list.
        with open(csv_log, "w") as f:
            f.write("date,time,callsign,mode,frequency_hz,snr_db\n")
            f.write("2026-10-23,12:00:00,%s,RADEV1,14236000,10\n" % CALLSIGN)
        extra = dict(Rig__HalfDuplex=False, Reporting__Enable=True, Reporting__Callsign=CALLSIGN,
                     Reporting__GridSquare="DM12", Reporting__Frequency=14236000, Reporting__FrequencyAsKHz=True,
                     Reporting__ManualFrequencyReporting=True, Reporting__FreeDV__Enable=False,
                     Reporting__CSV__LogFilePath=csv_log, CallsignList__UseUTCTime=True,
                     Reporting__UDPBroadcast__Enable=True, Reporting__UDPBroadcast__Address="127.0.0.1",
                     Reporting__UDPBroadcast__Port=listener.getsockname()[1])
        with audio_devices(), Loopback(RADIO_TX, RADIO_RX):
            with freedvtest.start(settings=settings(**extra)) as app:
                app.wait_for_window("main")
                start_modem(app)
                app.click("main.btnPTT")
                wait_for_transmit(app)
                app.wait_until(lambda: app.state()["sync"] == 1, timeout=20, description="FreeDV to sync")
                time.sleep(8)
                app.click("main.btnPTT")
                wait_for_transmit(app, False)
                app.wait_until(lambda: app.get("main.cboLastReportedCallsigns")["value"] == CALLSIGN, timeout=15,
                               description="the callsign to be received")

                # The list of stations heard, in kHz and UTC as asked.
                items = app.get("main.listLastReportedCallsigns")["items"]
                check_equal([(item["columns"][0], item["columns"][1].replace(",", "")) for item in items],
                            [(CALLSIGN, "14236.0")] * 2, "Stations heard")
                when = re.match(r"^(\d{4}-\d\d-\d\d)\D(\d\d:\d\d:\d\d)$", items[0]["columns"][2])
                check(when, "Time heard is %r" % items[0]["columns"][2])
                heard_at = datetime.datetime.strptime(" ".join(when.groups()), "%Y-%m-%d %H:%M:%S")
                now = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None)
                check(abs((now - heard_at).total_seconds()) < 120,
                      "Time heard is %s, at %s UTC" % (items[0]["columns"][2], now))

                # Other programs are told...
                try:
                    told = json.loads(listener.recv(65536).decode("utf-8"))
                except socket.timeout:
                    check(False, "Nothing was sent to other programs about the callsign received")
                check_equal((told.get("type"), told.get("callsign"), told.get("mode"), told.get("frequency_hz")),
                            ("fdv_callsign", CALLSIGN, "RADEV1", 14236000), "What other programs were told")

                # ...and it's logged.
                stop_modem(app)
                quit_and_check_log(app)
                with open(csv_log, "r") as f:
                    logged = [line.strip().split(",") for line in f if line.strip()]
                check(len(logged) >= 2 and logged[-1][2:5] == [CALLSIGN, "RADEV1", "14236000"],
                      "The log of stations heard is %r" % logged)

                # Which is where the list comes from the next time.
                with freedvtest.start(config=app.config_path) as again:
                    again.wait_for_window("main")
                    items = again.get("main.listLastReportedCallsigns")["items"]
                    check([(item["columns"][0], item["columns"][1].replace(",", "")) for item in items][:2]
                          == [(CALLSIGN, "14236.0")] * 2, "Stations heard the next time: %r" % items)
                    check_equal(again.quit(), 0, "FreeDV's exit code the next time")
    finally:
        listener.close()
        shutil.rmtree(folder, ignore_errors=True)


def set_debugging(app, **values):
    app.menu("Settings/Edit Settings...")
    app.wait_for_window("settings")
    app.set_value("settings.notebook", "Debugging")
    for name, value in values.items():
        app.set_value("settings." + name, value)
    app.click("settings.sdbSizer5OK")
    app.wait_for_window_closed("settings")


def test_test_tone():
    """The tone that can be added to what's received (Settings > Debugging),
    to see how FreeDV copes with interference, and the one that can be put on
    the left channel of what's transmitted to key a radio by VOX."""
    def heard(device):
        return record(device, 1.0, channels=2)

    with audio_devices():
        with freedvtest.start(settings=settings()) as app:
            app.wait_for_window("main")
            start_modem(app)

            # In analog mode, what comes from the radio goes to the speaker:
            # nothing here, until the tone's added to it.
            app.click("main.btnAnalog")
            check_silent(SPEAKER_MONITOR, "Something is being played with nothing being received")
            set_debugging(app, ckboxTone=True, txtToneFreqHz="1500", txtToneAmplitude="8000")
            left, right = heard(SPEAKER_MONITOR)
            for name, channel in [("left", left), ("right", right)]:
                check(tone_share(channel, 1500) > 0.8, "What's played on the %s is only %.2f the tone (level %d)"
                      % (name, tone_share(channel, 1500), math.sqrt(sum(x * x for x in channel) / len(channel))))

            # Another frequency. (Listened for until it's heard: on Windows CI,
            # what's played sometimes has gaps in it.)
            set_debugging(app, txtToneFreqHz="700")
            shares = []
            def changed():
                left, right = heard(SPEAKER_MONITOR)
                shares.append((tone_share(left, 700), tone_share(left, 1500)))
                return shares[-1][0] > 0.8 and shares[-1][1] < 0.1
            try:
                app.wait_until(changed, timeout=10)
            except freedvtest.FreeDVError:
                check(False, "After changing the tone to 700 Hz, what's played is %.2f that and %.2f 1500 Hz"
                      % shares[-1])

            # And it goes away again.
            set_debugging(app, ckboxTone=False)
            try:
                app.wait_until(lambda: level(SPEAKER_MONITOR, 0.5) < SILENT, timeout=10)
            except freedvtest.FreeDVError:
                check(False, "The tone is still being played after turning it off")
            stop_modem(app)
            quit_and_check_log(app)

        # The VOX tone: 1000 Hz on the left, the signal on the right.
        with freedvtest.start(settings=settings(Rig__leftChannelVoxTone=True)) as app:
            app.wait_for_window("main")
            start_modem(app)
            check_silent(RADIO_TX_MONITOR, "Something is being sent to the radio while receiving")
            app.click("main.btnPTT")
            wait_for_transmit(app)
            left, right = heard(RADIO_TX_MONITOR)
            check(tone_share(left, 1000) > 0.8, "The left channel is only %.2f VOX tone" % tone_share(left, 1000))
            check(tone_share(right, 1000) < 0.2, "The right channel is mostly VOX tone (%.2f)"
                  % tone_share(right, 1000))
            check(math.sqrt(sum(sample * sample for sample in right) / len(right)) > AUDIBLE,
                  "Nothing but the VOX tone is being sent to the radio")
            app.click("main.btnPTT")
            wait_for_transmit(app, False)
            check_silent(RADIO_TX_MONITOR, "Something is still being sent to the radio after transmitting")
            stop_modem(app)
            quit_and_check_log(app)


def test_exit_while_tuning():
    """File > Exit with Tune on stops the tone first."""
    with audio_devices():
        with freedvtest.start(settings=settings()) as app:
            app.wait_for_window("main")
            start_modem(app)
            app.click("main.btnTune")
            check_audible(RADIO_TX_MONITOR, "Nothing is being sent to the radio while tuning")
            app.menu(EXIT_MENU_ITEM)
            check_equal(app.wait_for_exit(), 0, "FreeDV's exit code having been tuning")
            errors = [line for line in app.log().splitlines() if " ERROR " in line or " FATAL " in line]
            check_equal(errors, [], "Errors logged by FreeDV")
        check_silent(RADIO_TX_MONITOR, "Something is still being sent to the radio")


def test_old_settings():
    """Settings from before they were kept the way they are now."""
    # The radio used to be kept as its place in Hamlib's list.
    with freedvtest.start(settings=settings(Hamlib__RigName=1, Debug__verbose=True)) as app:
        app.wait_for_window("main")
        radio = app.state()["config"]["rigControl"]["hamlibRigName"]
        check(radio, "No radio was brought over from the old setting")

        app.menu("Settings/Edit Settings...")
        app.wait_for_window("settings")
        app.set_value("settings.notebook", "Rig Control")
        check_equal(app.get("settings.cbRigName")["value"], radio, "Radio displayed in Settings")
        app.click("settings.sdbSizer5OK")
        app.wait_for_window_closed("settings")
        check_equal(app.quit(), 0, "FreeDV's exit code")
        check_equal(freedvtest.read_config(app.config_path).get("/Hamlib/RigNameStr"), radio, "Saved radio")

        # And is found by its name from then on.
        with freedvtest.start(config=app.config_path) as again:
            again.wait_for_window("main")
            check_equal(again.state()["config"]["rigControl"]["hamlibRigName"], radio, "Radio the next time")
            check_equal(again.quit(), 0, "FreeDV's exit code the next time")


def test_bad_command_line():
    """Started with something it doesn't understand, FreeDV says how it's
    meant to be started rather than carrying on regardless."""
    folder = tempfile.mkdtemp(prefix="freedvtest-home-")
    try:
        env = dict(os.environ)
        env.update({"HOME": folder, "XDG_CONFIG_HOME": os.path.join(folder, ".config"), "LANGUAGE": "en",
                    "LC_ALL": "C.UTF-8", "GDK_BACKEND": "x11"})
        if sys.platform == "darwin":
            # (See freedvtest.start().)
            env["CFFIXED_USER_HOME"] = folder
        if sys.platform == "win32":
            # (Likewise, to keep it out of the registry.)
            env["FREEDV_TEST_CONFIG_DIR"] = folder
        for arguments in [["--no-such-option"], ["--help"]]:
            result = subprocess.run([freedvtest.default_binary()] + arguments, env=env, timeout=60,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            output = result.stdout.decode("utf-8", "replace")
            # (It's "FreeDV" on macOS, where that's the program's name. On
            # Windows, it's written to the console window that the program
            # was started from, if any, rather than to stdout or stderr, and
            # so can't be seen here.)
            if sys.platform != "win32":
                check("usage: freedv " in output.lower(), "Given %s, FreeDV said %r" % (arguments, output))
            check(result.returncode != 0, "Given %s, FreeDV's exit code was 0" % arguments)
            # (Files only: macOS makes some empty folders of its own there.)
            left = [os.path.relpath(os.path.join(path, file), folder)
                    for path, _, files in os.walk(folder) for file in files]
            check_equal(left, [], "Left behind when given %s" % arguments)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


SCENARIOS = {
    "transmit": test_transmit,
    "ptt_key": test_ptt_key,
    "ptt_key_elsewhere": test_ptt_key_elsewhere,
    "tx_level": test_tx_level,
    "receive": test_receive,
    "analog": test_analog,
    "play_file": test_play_file,
    "voice_keyer": test_voice_keyer,
    "voice_keyer_reply": test_voice_keyer_reply,
    "voice_keyer_files": test_voice_keyer_files,
    "recording": test_recording,
    "recording_options": test_recording_options,
    "unwritable_files": test_unwritable_files,
    "monitor_volume": test_monitor_volume,
    "mouse_controls": test_mouse_controls,
    "play_file_loop": test_play_file_loop,
    "help_and_restore": test_help_and_restore,
    "tune_level_per_band": test_tune_level_per_band,
    "exit_menu": test_exit_menu,
    "reporting_without_callsign": test_reporting_without_callsign,
    "band_names": test_band_names,
    "frequency_units": test_frequency_units,
    "config_migration_failure": test_config_migration_failure,
    "default_config": test_default_config,
    "appearance_change": test_appearance_change,
    "time_out_timer": test_time_out_timer,
    "voice_keyer_time_out": test_voice_keyer_time_out,
    "ptt_key_time_out": test_ptt_key_time_out,
    "menus": test_menus,
    "tab_layout": test_tab_layout,
    "heard_list": test_heard_list,
    "window_geometry": test_window_geometry,
    "receive_reports": test_receive_reports,
    "test_tone": test_test_tone,
    "exit_while_tuning": test_exit_while_tuning,
    "old_settings": test_old_settings,
    "bad_command_line": test_bad_command_line,
}


def main():
    names = sys.argv[1:] or list(SCENARIOS)
    for scenario in names:
        if scenario not in SCENARIOS:
            sys.exit("Unknown scenario '%s'. Scenarios: %s" % (scenario, ", ".join(SCENARIOS)))
    for scenario in names:
        print("Running %s..." % scenario, flush=True)
        try:
            SCENARIOS[scenario]()
        except freedvtest.audio.Unavailable as e:
            # What it needs of the audio devices can't be had here (see freedvtest.audio).
            print("SKIPPED: %s" % e, flush=True)
            sys.exit(77)
    print("PASS")


if __name__ == "__main__":
    main()
