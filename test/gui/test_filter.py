#!/usr/bin/env python3
"""Tests FreeDV's Filter window (Tools > Filter Audio): the microphone and
speaker equalisers and the processing that can be turned on and off there.

Each scenario below launches its own copy (or copies) of FreeDV. With no
arguments all of them are run; otherwise, only those named:

    python3 test_filter.py [scenario...]

Run from the build folder (or set FREEDV_BINARY) with a window manager
running, on Linux or macOS: see freedvtest/audio.py for the audio devices it
needs.
"""

import math
import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import freedvtest
import freedvtest.audio
from freedvtest.audio import Tone
from test_main_window import RADIO_TX, SPEAKER as SPEAKER_DEVICE
from test_main_window import (AUDIBLE, MICROPHONE, RADIO_RX, RADIO_TX_MONITOR, SPEAKER_MONITOR, audio_devices, check,
                              check_equal, label, level, quit_and_check_log, settings, start_modem, stop_modem,
                              wait_for_transmit)

MIC = "filter.micIn."
SPEAKER = "filter.spkOut."

# The sliders of each equaliser, other than its volume.
EQ_SLIDERS = ["bassFreq", "bassGain", "midFreq", "midGain", "midQ", "trebleFreq", "trebleGain"]

# What Default sets those to (as displayed) and where that puts the sliders.
EQ_DEFAULTS = dict(bassFreq="100", bassGain="0.0", midFreq="1500", midGain="0.0", midQ="1.0", trebleFreq="3000",
                   trebleGain="0.0")
EQ_DEFAULT_SLIDERS = dict(bassFreq=100, bassGain=200, midFreq=1950, midGain=200, midQ=50, trebleFreq=3000,
                          trebleGain=200)


def gain_slider(gain_db):
    """Returns where a gain slider needs to be for the given gain."""
    return int(round((gain_db + 20) * 10))


def open_filter(app):
    app.menu("Tools/Filter Audio...")
    return app.wait_for_window("filter")


def close_filter(app):
    app.click("filter.btnClose")
    app.wait_for_window_closed("filter")


def value(app, name):
    """Returns the number displayed under a slider, without the separators
    that some languages put between thousands."""
    return label(app, name + "Value").replace(",", "")


def values(app, prefix):
    return {slider: value(app, prefix + slider) for slider in EQ_SLIDERS}


def filter_config(app):
    return app.state()["config"]["filter"]


def check_close(actual, expected, what, tolerance=0.011):
    check(abs(actual - expected) <= tolerance, "%s is %r, expected %r" % (what, actual, expected))


def check_ratio(before, after, decibels, what, tolerance_db=2.0):
    """Checks that a level changed by about the given number of dB."""
    check(before > 0 and after > 0, "%s: levels are %d and %d" % (what, before, after))
    change = 20 * math.log10(after / before)
    check(abs(change - decibels) <= tolerance_db,
          "%s: the level went from %d to %d (%+.1f dB), expected %+.1f dB" % (what, before, after, change, decibels))


def settled_level(device):
    time.sleep(0.75)  # for a change to work its way through
    return level(device)


# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------

def test_controls():
    """What's in the window and how its controls depend on each other, with
    FreeDV not running."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        config = os.path.join(folder, "mine.conf")
        freedvtest.write_config(config, {"/Reporting/FreeDV/Hostname": "127.0.0.1:9", "/Reporting/FreeDV/UseTls": False})

        with freedvtest.start(config=config) as app:
            app.wait_for_window("main")
            dialog = open_filter(app)
            check_equal(dialog["label"], "Filter (mine.conf)", "Window title")
            check_equal(dialog["modal"], False, "Window is modal")

            # As FreeDV comes.
            check_equal([app.get("filter.ckbox" + name)["value"] for name in ["NoiseReduction", "Agc", "BwExpand"]],
                        [True, True, True], "Noise suppression, AGC and BW expander")
            check_equal(app.get("filter.notebook")["pages"], ["Microphone In Equaliser", "Speaker Out Equaliser"],
                        "Tabs")
            for prefix, name in [(MIC, "Microphone"), (SPEAKER, "Speaker")]:
                check_equal(app.get(prefix + "ckboxEnable")["value"], False, name + " equaliser enabled")
                check_equal(values(app, prefix), EQ_DEFAULTS, name + " equaliser")
                check_equal({slider: app.get(prefix + slider)["value"] for slider in EQ_SLIDERS}, EQ_DEFAULT_SLIDERS,
                            name + " equaliser's sliders")
                check_equal(value(app, prefix + "volGain"), "0.0", name + " volume")
                check_equal([app.get(prefix + slider)["enabled"] for slider in EQ_SLIDERS], [False] * 7,
                            name + " equaliser's sliders enabled while it's off")
                check_equal(app.get(prefix + "btnDefault")["enabled"], False, name + " Default enabled while off")

            # AGC looks after the microphone's volume, so it's one or the other.
            check_equal(app.get(MIC + "volGain")["enabled"], False, "Microphone volume enabled with AGC")
            check_equal(app.get(SPEAKER + "volGain")["enabled"], True, "Speaker volume enabled")
            app.set_value("filter.ckboxAgc", False)
            check_equal(filter_config(app)["agcEnabled"], False, "AGC")
            check_equal(app.get(MIC + "volGain")["enabled"], True, "Microphone volume enabled without AGC")
            app.set_value(MIC + "volGain", gain_slider(8))
            check_equal(value(app, MIC + "volGain"), "8.0", "Microphone volume")
            check_close(filter_config(app)["micIn"]["volInDB"], 8.0, "Microphone volume in use")
            app.set_value("filter.ckboxAgc", True)
            check_equal(filter_config(app)["agcEnabled"], True, "AGC")
            check_equal(app.get(MIC + "volGain")["enabled"], False, "Microphone volume enabled with AGC back on")
            check_equal((value(app, MIC + "volGain"), app.get(MIC + "volGain")["value"]), ("0.0", 200),
                        "Microphone volume with AGC back on")
            check_close(filter_config(app)["micIn"]["volInDB"], 0.0, "Microphone volume in use")

            # The other two only turn something on and off.
            app.set_value("filter.ckboxNoiseReduction", False)
            check_equal(filter_config(app)["noiseReductionEnable"], False, "Noise suppression")
            app.set_value("filter.ckboxBwExpand", False)
            check_equal(filter_config(app)["bwExpandEnabled"], False, "BW expander")
            app.set_value("filter.ckboxBwExpand", True)
            check_equal(filter_config(app)["bwExpandEnabled"], True, "BW expander")

            # Each equaliser: its sliders, from one end to the other.
            for page, (prefix, channel, name) in enumerate([(MIC, "micIn", "Microphone"),
                                                              (SPEAKER, "spkOut", "Speaker")]):
                app.set_value("filter.notebook", page)
                app.set_value(prefix + "ckboxEnable", True)
                check_equal(filter_config(app)[channel]["eqEnable"], True, name + " equaliser enabled")
                check_equal([app.get(prefix + slider)["enabled"] for slider in EQ_SLIDERS], [True] * 7,
                            name + " equaliser's sliders enabled")
                check_equal(app.get(prefix + "btnDefault")["enabled"], True, name + " Default enabled")

                for slider, position, displayed in [
                        ("bassFreq", 250, "250"), ("bassFreq", 600, "600"), ("bassFreq", 0, "1"),
                        ("bassGain", 400, "20.0"), ("bassGain", 0, "-20.0"), ("bassGain", 265, "6.5"),
                        ("midFreq", 3900, "3000"), ("midFreq", 0, "1"), ("midFreq", 1300, "1000"),
                        ("midGain", 80, "-12.0"),
                        ("midQ", 100, "10.0"), ("midQ", 0, "0.1"), ("midQ", 65, "2.0"),
                        ("trebleFreq", 3900, "3900"), ("trebleFreq", 2500, "2500"),
                        ("trebleGain", 230, "3.0")]:
                    app.set_value(prefix + slider, position)
                    check_equal(value(app, prefix + slider), displayed, "%s %s at %d" % (name, slider, position))
                    # (The lowest frequency there is is 1 Hz, which isn't quite the end.)
                    check_equal(app.get(prefix + slider)["value"], max(position, 1) if "Freq" in slider else position,
                                "%s %s slider" % (name, slider))

                in_use = filter_config(app)[channel]
                check_equal((in_use["bassFreqHz"], in_use["midFreqHz"], in_use["trebleFreqHz"]), (1, 1000, 2500),
                            name + " equaliser's frequencies in use")
                for setting, expected in [("bassGaindB", 6.5), ("midGainDB", -12.0), ("trebleGaindB", 3.0)]:
                    check_close(in_use[setting], expected, "%s %s in use" % (name, setting))
                check_close(in_use["midQ"], 2.0, name + " mid Q in use", 0.05)

            # Default puts one equaliser back, along with its volume.
            app.set_value(SPEAKER + "volGain", gain_slider(-5))
            check_equal(value(app, SPEAKER + "volGain"), "-5.0", "Speaker volume")
            app.click(SPEAKER + "btnDefault")
            check_equal(values(app, SPEAKER), EQ_DEFAULTS, "Speaker equaliser after Default")
            check_equal(value(app, SPEAKER + "volGain"), "0.0", "Speaker volume after Default")
            check_equal(app.get(SPEAKER + "ckboxEnable")["value"], True, "Speaker equaliser enabled after Default")
            check_equal(value(app, MIC + "midFreq"), "1000", "Microphone mid frequency after the speaker's Default")
            in_use = filter_config(app)["spkOut"]
            check_equal((in_use["bassFreqHz"], in_use["midFreqHz"], in_use["trebleFreqHz"]), (100, 1500, 3000),
                        "Speaker equaliser's frequencies in use after Default")

            # Turned off, an equaliser keeps its settings.
            app.set_value(SPEAKER + "ckboxEnable", False)
            check_equal(filter_config(app)["spkOut"]["eqEnable"], False, "Speaker equaliser enabled")
            check_equal([app.get(SPEAKER + slider)["enabled"] for slider in EQ_SLIDERS], [False] * 7,
                        "Speaker equaliser's sliders enabled after turning it off")
            app.set_value(SPEAKER + "volGain", gain_slider(-3))

            # Asking for the window again brings back the one that's open.
            app.menu("Tools/Filter Audio...")
            check_equal(len([window for window in app.windows() if window["name"] == "filter"]), 1,
                        "Number of Filter windows")
            check_equal(value(app, MIC + "midFreq"), "1000", "Microphone mid frequency")

            # Closing it by either means keeps what was set.
            close_filter(app)
            check_equal(label(app, "main.textMicSpkrLevel"), "-3.0dB", "Speaker level in the main window")
            open_filter(app)
            check_equal(value(app, MIC + "bassGain"), "6.5", "Microphone bass gain on reopening")
            app.set_value(MIC + "trebleGain", gain_slider(-4))
            app.close("filter")
            app.wait_for_window_closed("filter")
            check_equal(app.quit(), 0, "FreeDV's exit code")

        saved = freedvtest.read_config(config)
        check_equal({key: saved.get("/Filter/" + key) for key in ["speexpp_enable", "agcEnable", "bwExpandEnable",
                                                                  "MicIn/EQEnable", "SpkOut/EQEnable"]},
                    {"speexpp_enable": "0", "agcEnable": "1", "bwExpandEnable": "1", "MicIn/EQEnable": "1",
                     "SpkOut/EQEnable": "0"}, "Saved settings")

        # And it's all still there the next time.
        with freedvtest.start(config=config) as app:
            app.wait_for_window("main")
            check_equal(label(app, "main.textMicSpkrLevel"), "-3.0dB", "Speaker level in the main window")
            open_filter(app)
            check_equal([app.get("filter.ckbox" + name)["value"] for name in ["NoiseReduction", "Agc", "BwExpand"]],
                        [False, True, True], "Noise suppression, AGC and BW expander")
            check_equal(values(app, MIC), dict(bassFreq="1", bassGain="6.5", midFreq="1000", midGain="-12.0",
                                               midQ="2.0", trebleFreq="2500", trebleGain="-4.0"),
                        "Microphone equaliser")
            check_equal(app.get(MIC + "ckboxEnable")["value"], True, "Microphone equaliser enabled")
            check_equal(values(app, SPEAKER), EQ_DEFAULTS, "Speaker equaliser")
            check_equal(app.get(SPEAKER + "ckboxEnable")["value"], False, "Speaker equaliser enabled")
            check_equal(value(app, SPEAKER + "volGain"), "-3.0", "Speaker volume")

            # FreeDV closing takes the window with it.
            check_equal(app.quit(), 0, "FreeDV's exit code")
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_out_of_range():
    """Saved settings beyond what the sliders allow are brought back within
    it when the window's opened."""
    extra = {"/Filter/MicIn/BassGaindB": 99, "/Filter/MicIn/TrebleGaindB": -99, "/Filter/MicIn/BassFreqHz": 5000,
             "/Filter/MicIn/MidFreqHz": 0.1, "/Filter/MicIn/EQEnable": True,
             "/Filter/SpkOut/BassGaindB": -99, "/Filter/SpkOut/TrebleGaindB": 99, "/Filter/SpkOut/EQEnable": True,
             "/Reporting/FreeDV/Hostname": "127.0.0.1:9", "/Reporting/FreeDV/UseTls": False}
    with freedvtest.start(settings=extra) as app:
        app.wait_for_window("main")
        open_filter(app)
        microphone = values(app, MIC)
        check_equal((microphone["bassGain"], microphone["trebleGain"], microphone["bassFreq"], microphone["midFreq"]),
                    ("20.0", "-20.0", "600", "1"), "Microphone bass gain, treble gain, bass and mid frequencies")
        speaker = values(app, SPEAKER)
        check_equal((speaker["bassGain"], speaker["trebleGain"]), ("-20.0", "20.0"), "Speaker bass and treble gains")
        close_filter(app)
        check_equal(app.message_boxes(), [], "Messages displayed")
        check_equal(app.quit(), 0, "FreeDV's exit code")


def test_volume():
    """The two volume sliders, and the speaker's twin in the main window."""
    with audio_devices():
        with freedvtest.start(settings=settings()) as app:
            app.wait_for_window("main")
            start_modem(app)
            app.click("main.btnAnalog")
            open_filter(app)

            # What comes from the radio, as played through the speaker.
            with Tone(RADIO_RX):
                normal = settled_level(SPEAKER_MONITOR)
                check(normal > AUDIBLE, "The radio isn't being played through the speaker (level %d)" % normal)

                app.set_value("filter.notebook", 1)
                app.set_value(SPEAKER + "volGain", gain_slider(-12))
                check_equal(value(app, SPEAKER + "volGain"), "-12.0", "Speaker volume")
                check_ratio(normal, settled_level(SPEAKER_MONITOR), -12, "Speaker volume at -12 dB")
                app.wait_until(lambda: label(app, "main.textMicSpkrLevel") == "-12.0dB", timeout=5,
                               description="the main window to show the speaker's volume")
                check_equal(app.get("main.sliderMicSpkrLevel")["value"], -120, "Main window's speaker level")

                app.set_value(SPEAKER + "volGain", gain_slider(5))
                check_ratio(normal, settled_level(SPEAKER_MONITOR), 5, "Speaker volume at +5 dB")

                # The main window's slider moves the one here.
                app.set_value("main.sliderMicSpkrLevel", -60)
                app.wait_until(lambda: value(app, SPEAKER + "volGain") == "-6.0", timeout=5,
                               description="the Filter window to show the speaker's volume")
                check_equal(app.get(SPEAKER + "volGain")["value"], gain_slider(-6), "Speaker volume slider")
                check_ratio(normal, settled_level(SPEAKER_MONITOR), -6, "Speaker volume at -6 dB")

                app.set_value(SPEAKER + "volGain", gain_slider(0))
                check_ratio(normal, settled_level(SPEAKER_MONITOR), 0, "Speaker volume back at 0 dB")

            # What's said into the microphone, as sent to the radio.
            app.set_value("filter.notebook", 0)
            check_equal(app.get(MIC + "volGain")["enabled"], True, "Microphone volume enabled")
            app.click("main.btnPTT")
            wait_for_transmit(app)
            with Tone(MICROPHONE):
                normal = settled_level(RADIO_TX_MONITOR)
                check(normal > AUDIBLE, "The microphone isn't being sent to the radio (level %d)" % normal)
                app.set_value(MIC + "volGain", gain_slider(-12))
                check_ratio(normal, settled_level(RADIO_TX_MONITOR), -12, "Microphone volume at -12 dB")
                app.set_value(MIC + "volGain", gain_slider(-20))
                check_ratio(normal, settled_level(RADIO_TX_MONITOR), -20, "Microphone volume at -20 dB")
                app.set_value(MIC + "volGain", gain_slider(0))
                check_ratio(normal, settled_level(RADIO_TX_MONITOR), 0, "Microphone volume back at 0 dB")
            app.click("main.btnPTT")
            wait_for_transmit(app, False)

            close_filter(app)
            stop_modem(app)
            quit_and_check_log(app)


def test_equalizer():
    """The equalisers' effect on what's heard, with the window opened both
    before and after FreeDV is started."""
    for open_first in [True, False]:
        when = "with the window opened %s starting" % ("before" if open_first else "after")
        with audio_devices():
            with freedvtest.start(settings=settings()) as app:
                app.wait_for_window("main")
                if open_first:
                    open_filter(app)
                start_modem(app)
                app.click("main.btnAnalog")
                if not open_first:
                    open_filter(app)

                # A 1 kHz tone from the radio, with the speaker's equaliser
                # set to take that frequency out.
                app.set_value("filter.notebook", 1)
                with Tone(RADIO_RX):
                    normal = settled_level(SPEAKER_MONITOR)
                    check(normal > AUDIBLE, "The radio isn't being played through the speaker (level %d)" % normal)

                    app.set_value(SPEAKER + "ckboxEnable", True)
                    check_ratio(normal, settled_level(SPEAKER_MONITOR), 0, "Speaker equaliser as it comes, " + when)
                    app.set_value(SPEAKER + "midFreq", 1300)
                    app.set_value(SPEAKER + "midGain", gain_slider(-12))
                    check_equal((value(app, SPEAKER + "midFreq"), value(app, SPEAKER + "midGain")),
                                ("1000", "-12.0"), "Speaker mid frequency and gain")
                    check_ratio(normal, settled_level(SPEAKER_MONITOR), -12, "Speaker mid at -12 dB, " + when)

                    # Elsewhere, it leaves the tone alone.
                    app.set_value(SPEAKER + "midFreq", 3900)
                    app.set_value(SPEAKER + "midQ", 100)
                    check_ratio(normal, settled_level(SPEAKER_MONITOR), 0, "Speaker mid at 3 kHz, " + when, 3.0)
                    app.set_value(SPEAKER + "midFreq", 1300)
                    app.set_value(SPEAKER + "midQ", 50)
                    check_ratio(normal, settled_level(SPEAKER_MONITOR), -12, "Speaker mid back at 1 kHz, " + when)

                    # Turning the equaliser off does the same...
                    app.set_value(SPEAKER + "ckboxEnable", False)
                    check_ratio(normal, settled_level(SPEAKER_MONITOR), 0, "Speaker equaliser off, " + when)
                    app.set_value(SPEAKER + "ckboxEnable", True)
                    check_ratio(normal, settled_level(SPEAKER_MONITOR), -12, "Speaker equaliser back on, " + when)

                    # ...as does Default.
                    app.click(SPEAKER + "btnDefault")
                    check_ratio(normal, settled_level(SPEAKER_MONITOR), 0, "Speaker equaliser after Default, " + when)

                    # Bass and treble: the tone's below one and above the other.
                    app.set_value(SPEAKER + "trebleFreq", 500)
                    app.set_value(SPEAKER + "trebleGain", gain_slider(5))
                    check_ratio(normal, settled_level(SPEAKER_MONITOR), 5, "Speaker treble at +5 dB, " + when, 3.0)
                    app.click(SPEAKER + "btnDefault")
                    app.set_value(SPEAKER + "bassFreq", 600)
                    app.set_value(SPEAKER + "bassGain", gain_slider(-20))
                    quieter = settled_level(SPEAKER_MONITOR)
                    check(normal * 0.15 < quieter < normal * 0.95,
                          "Speaker bass at -20 dB below 600 Hz took a 1 kHz tone from %d to %d, %s"
                          % (normal, quieter, when))
                    app.click(SPEAKER + "btnDefault")

                # The microphone's equaliser, likewise.
                app.set_value("filter.notebook", 0)
                app.click("main.btnPTT")
                wait_for_transmit(app)
                with Tone(MICROPHONE):
                    normal = settled_level(RADIO_TX_MONITOR)
                    check(normal > AUDIBLE, "The microphone isn't being sent to the radio (level %d)" % normal)
                    app.set_value(MIC + "ckboxEnable", True)
                    app.set_value(MIC + "midFreq", 1300)
                    app.set_value(MIC + "midGain", gain_slider(-12))
                    check_ratio(normal, settled_level(RADIO_TX_MONITOR), -12, "Microphone mid at -12 dB, " + when)
                    app.set_value(MIC + "ckboxEnable", False)
                    check_ratio(normal, settled_level(RADIO_TX_MONITOR), 0, "Microphone equaliser off, " + when)
                    app.set_value(MIC + "ckboxEnable", True)
                    check_ratio(normal, settled_level(RADIO_TX_MONITOR), -12, "Microphone equaliser back on, " + when)
                    app.click(MIC + "btnDefault")
                    check_ratio(normal, settled_level(RADIO_TX_MONITOR), 0, "Microphone equaliser after Default, " + when)
                app.click("main.btnPTT")
                wait_for_transmit(app, False)

                # The window can outlive FreeDV being stopped.
                stop_modem(app)
                app.set_value(MIC + "midGain", gain_slider(3))
                check_equal(value(app, MIC + "midGain"), "3.0", "Microphone mid gain after stopping")
                close_filter(app)
                quit_and_check_log(app)


def test_equalizer_receive_only():
    """The speaker's equaliser when FreeDV only has the one pair of devices,
    and so is only receiving: what it plays goes to the first pair's output."""
    # (Unless that's the same device as the first pair's input, as on
    # Windows CI (see freedvtest.audio.shared()), when it would be heard
    # along with the tone played into that: the speaker stands in for it.)
    output, output_monitor = RADIO_TX, RADIO_TX_MONITOR
    if freedvtest.audio.shared(RADIO_RX, RADIO_TX):
        output, output_monitor = SPEAKER_DEVICE, SPEAKER_MONITOR
    receive_only = settings(Audio__soundCard1OutDeviceName=output, Audio__soundCard2InDeviceName="none",
                            Audio__soundCard2OutDeviceName="none")
    with audio_devices():
        with freedvtest.start(settings=receive_only) as app:
            app.wait_for_window("main")
            start_modem(app)
            app.click("main.btnAnalog")
            open_filter(app)
            app.set_value("filter.notebook", 1)
            with Tone(RADIO_RX):
                normal = settled_level(output_monitor)
                check(normal > AUDIBLE, "The radio isn't being played (level %d)" % normal)

                app.set_value(SPEAKER + "ckboxEnable", True)
                app.set_value(SPEAKER + "midFreq", 1300)
                app.set_value(SPEAKER + "midGain", gain_slider(-12))
                check_ratio(normal, settled_level(output_monitor), -12, "Speaker mid at -12 dB")
                app.set_value(SPEAKER + "ckboxEnable", False)
                check_ratio(normal, settled_level(output_monitor), 0, "Speaker equaliser off")
                app.set_value(SPEAKER + "ckboxEnable", True)
                check_ratio(normal, settled_level(output_monitor), -12, "Speaker equaliser back on")
                app.click(SPEAKER + "btnDefault")
                check_ratio(normal, settled_level(output_monitor), 0, "Speaker equaliser after Default")

            stop_modem(app)
            close_filter(app)
            quit_and_check_log(app)


def test_noise_suppression():
    """Noise suppression, which takes a steady tone to be noise."""
    with audio_devices():
        with freedvtest.start(settings=settings()) as app:
            app.wait_for_window("main")
            start_modem(app)
            app.click("main.btnAnalog")
            open_filter(app)
            check_equal(app.get("filter.ckboxNoiseReduction")["value"], False, "Noise suppression to begin with")

            app.click("main.btnPTT")
            wait_for_transmit(app)
            with Tone(MICROPHONE):
                normal = settled_level(RADIO_TX_MONITOR)
                check(normal > AUDIBLE, "The microphone isn't being sent to the radio (level %d)" % normal)

                app.set_value("filter.ckboxNoiseReduction", True)
                time.sleep(3)  # for it to make its mind up
                suppressed = level(RADIO_TX_MONITOR)
                check(suppressed < normal / 4, "Noise suppression took a tone from %d to %d" % (normal, suppressed))

                app.set_value("filter.ckboxNoiseReduction", False)
                check_ratio(normal, settled_level(RADIO_TX_MONITOR), 0, "Noise suppression off again")
            app.click("main.btnPTT")
            wait_for_transmit(app, False)

            close_filter(app)
            stop_modem(app)
            quit_and_check_log(app)


def test_default_config():
    """The Filter window when FreeDV is using its own default configuration
    rather than a named file: the window title says so, by not saying it."""
    home = tempfile.mkdtemp(prefix="freedvtest-home-")
    try:
        freedvtest.write_config(freedvtest.default_config_path(home),
                                {"/Reporting/FreeDV/Hostname": "127.0.0.1:9", "/Reporting/FreeDV/UseTls": False})
        with freedvtest.start(home=home, default_config=True) as app:
            app.wait_for_window("main")
            dialog = open_filter(app)
            check_equal(dialog["label"], "Filter", "Window title")
            close_filter(app)
            check_equal(app.quit(), 0, "FreeDV's exit code")
    finally:
        shutil.rmtree(home, ignore_errors=True)

def test_microphone_filters_are_not_built_with_one_sound_card():
    """Microphone volume and equaliser are left unbuilt when there is no
    second sound card: FreeDV only receives, so the mic chain is skipped
    even though its settings are set and kept."""
    one_pair = settings(Audio__soundCard2InDeviceName="none", Audio__soundCard2OutDeviceName="none")
    with audio_devices():
        with freedvtest.start(settings=one_pair) as app:
            app.wait_for_window("main")
            check_equal(app.state()["numSoundCards"], 1, "Number of sound cards")
            start_modem(app)
            open_filter(app)
            app.set_value(MIC + "volGain", gain_slider(-6))
            app.set_value(MIC + "ckboxEnable", True)
            close_filter(app)
            stop_modem(app)
            check_close(filter_config(app)["micIn"]["volInDB"], -6.0,
                        "Microphone volume kept without a second sound card")
            check_equal(filter_config(app)["micIn"]["eqEnable"], True, "Microphone equaliser setting kept")
            quit_and_check_log(app)

SCENARIOS = {
    "out_of_range": test_out_of_range,
    "controls": test_controls,
    "volume": test_volume,
    "equalizer": test_equalizer,
    "equalizer_receive_only": test_equalizer_receive_only,
    "noise_suppression": test_noise_suppression,
    "default_config": test_default_config,
    "one_sound_card": test_microphone_filters_are_not_built_with_one_sound_card,
}


def main():
    names = sys.argv[1:] or list(SCENARIOS)
    for scenario in names:
        if scenario not in SCENARIOS:
            sys.exit("Unknown scenario '%s' (available: %s)" % (scenario, ", ".join(SCENARIOS)))
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
