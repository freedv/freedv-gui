#!/usr/bin/env python3
"""Tests FreeDV's Settings window.

Each scenario below launches its own copy of FreeDV. With no arguments all
of them are run; otherwise, only those named:

    python3 test_settings.py [scenario...]

Run from the build folder (or set FREEDV_BINARY), on Linux or macOS: see
freedvtest/audio.py for the audio devices it needs.
"""

import contextlib
import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import freedvtest
import freedvtest.audio
from freedvtest.audio import Tone, VirtualAudioDevices, device, record, record_level, tone_share

RADIO_RX = device("FreeDVGuiTest_Radio_To_Computer")
RADIO_TX = device("FreeDVGuiTest_Computer_To_Radio")
MICROPHONE = device("FreeDVGuiTest_Microphone_To_Computer")
SPEAKER = device("FreeDVGuiTest_Computer_To_Speaker")

RADIO_RX_IN = VirtualAudioDevices.input_name(RADIO_RX)
MICROPHONE_IN = VirtualAudioDevices.input_name(MICROPHONE)

TABS = ["RX Audio", "TX Audio", "Radio", "Rig Control", "Reporting", "Voice Keyer", "Display", "Modem", "Debugging"]

# Somewhere for FreeDV to report to that isn't the public FreeDV Reporter
# server (nothing's listening there).
NOWHERE = "127.0.0.1:9"

# Values of the Hamlib "PTT uses" setting.
PTT_VIA_CAT, PTT_VIA_RTS, PTT_VIA_DTR, PTT_VIA_NONE, PTT_VIA_CAT_DATA = range(5)

WXK_F5 = 344
# wxWidgets' code for the keypad's Enter. The plain one never reaches the
# list: the combo's text box acts on it first.
NUMPAD_ENTER = 370


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def check_equal(actual, expected, what):
    check(actual == expected, "%s is %r, expected %r" % (what, actual, expected))


def name(control):
    return "settings." + control


def value(app, control):
    return app.get(name(control))["value"]


def label(app, control):
    return app.get(name(control))["label"].replace("&", "")


def set_value(app, control, new_value):
    app.set_value(name(control), new_value)


def check_values(app, expected):
    for control, expected_value in expected.items():
        check_equal(value(app, control), expected_value, control)


def check_enabled(app, controls, enabled):
    for control in controls:
        check_equal(app.get(name(control))["enabled"], enabled, control + " enabled")


def open_settings(app, tab=None):
    """Opens the Settings window, optionally at the given tab."""
    app.wait_for_window("main")
    app.menu("Settings/Edit Settings...")
    app.wait_for_window("settings")
    if tab is not None:
        go_to_tab(app, tab)


def go_to_tab(app, tab):
    set_value(app, "notebook", tab)
    check_equal(value(app, "notebook"), TABS.index(tab), "Tab displayed")


def press_ok(app):
    app.click(name("sdbSizer5OK"))
    app.wait_for_window_closed("settings")


def press_cancel(app):
    app.click(name("sdbSizer5Cancel"))
    app.wait_for_window_closed("settings")


def press_apply(app):
    app.click(name("sdbSizer5Apply"))
    check(app.window_exists("settings"), "Apply closed the Settings window")


def saved(app):
    """Returns the settings in FreeDV's configuration file."""
    return freedvtest.read_config(app.config_path)


def check_saved(app, expected):
    settings = saved(app)
    for key, expected_value in expected.items():
        check_equal(settings.get(key), str(expected_value), "Saved setting " + key)


def quit_and_check_log(app):
    check_equal(app.message_boxes(), [], "Errors displayed")
    check_equal(app.quit(), 0, "FreeDV's exit code")
    errors = [line for line in app.log().splitlines() if " ERROR " in line or " FATAL " in line]
    check_equal(errors, [], "Errors logged by FreeDV")


def audio_devices():
    return VirtualAudioDevices(RADIO_RX, RADIO_TX, MICROPHONE, SPEAKER)


def audio_settings():
    settings = {}
    for card, device in [("soundCard1In", RADIO_RX_IN), ("soundCard1Out", RADIO_TX),
                         ("soundCard2In", MICROPHONE_IN), ("soundCard2Out", SPEAKER)]:
        settings["/Audio/%sDeviceName" % card] = device
        settings["/Audio/%sSampleRate" % card] = 48000
    return settings


def device_labels(app, control):
    return [item["label"] for item in app.get(name(control))["items"]]


def start_modem(app):
    app.click("main.btnOnOff")
    app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to start")
    check_equal(app.message_boxes(), [], "Errors displayed on start")
    check(app.state()["running"], "FreeDV is not running after pressing Start")


def stop_modem(app):
    app.click("main.btnOnOff")
    app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to stop")
    check_equal(app.state()["running"], False, "Running after pressing Stop")


# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------

def test_buttons():
    """OK, Cancel and Apply, and the other ways of leaving the window."""
    with freedvtest.start(settings={}) as app:
        open_settings(app)
        settings = app.get("settings")
        check_equal(settings["label"], "Settings (freedv.conf)", "Window title")
        check(settings["modal"], "The Settings window is not modal")
        check_equal(app.get(name("notebook"))["pages"], TABS, "Tabs")
        check_equal(value(app, "notebook"), 0, "Tab displayed")

        # What's on other tabs can't be reached without going to them.
        try:
            app.click(name("ckHalfDuplex"))
            check(False, "A control on a tab that isn't displayed could be clicked")
        except freedvtest.CommandError:
            pass

        def change_something(stats_reset_time):
            go_to_tab(app, "Modem")
            set_value(app, "statsResetTime", stats_reset_time)

        # Cancel, Escape and the title bar's close button all discard changes.
        for leave in [press_cancel,
                      lambda app: app.key(name("sdbSizer5OK"), "Escape"),
                      lambda app: app.close("settings")]:
            change_something("42")
            leave(app)
            app.wait_for_window_closed("settings")
            check(saved(app).get("/Stats/ResetTime") != "42", "A cancelled change was saved")
            open_settings(app, "Modem")
            check_values(app, {"statsResetTime": "10"})

        # Apply saves and keeps the window open...
        change_something("42")
        press_apply(app)
        check_saved(app, {"/Stats/ResetTime": 42})
        check_values(app, {"statsResetTime": "42"})

        # ...so cancelling afterwards doesn't undo it.
        change_something("43")
        press_cancel(app)
        check_saved(app, {"/Stats/ResetTime": 42})

        # OK saves and closes.
        open_settings(app, "Modem")
        check_values(app, {"statsResetTime": "42"})
        change_something("44")
        press_ok(app)
        check_saved(app, {"/Stats/ResetTime": 44})

        open_settings(app, "Modem")
        check_values(app, {"statsResetTime": "44"})
        press_cancel(app)

        quit_and_check_log(app)


def test_audio():
    """The RX Audio and TX Audio tabs."""
    transmit_controls = ["lcSoundCard2InDevice", "tcSoundCard2InDevice", "btnSoundCard2InTest",
                         "lcSoundCard2OutDevice", "tcSoundCard2OutDevice", "btnSoundCard2OutTest"]

    def first_settings(app):
        """Choosing receive devices only, from where FreeDV starts out."""
        open_settings(app, "RX Audio")

        # Every device is offered, along with "none", which is what's
        # selected to begin with.
        inputs = device_labels(app, "lcSoundCard1InDevice")
        outputs = device_labels(app, "lcSoundCard1OutDevice")
        check(VirtualAudioDevices.input_label(RADIO_RX) in inputs, "The radio's input isn't listed: %r" % inputs)
        check(SPEAKER in outputs, "The speaker isn't listed: %r" % outputs)
        check_equal((inputs[-1], outputs[-1]), ("none", "none"), "Last devices listed")
        listed = [item for item in app.get(name("lcSoundCard1OutDevice"))["items"] if item["label"] == SPEAKER]
        check_equal(listed[0]["columns"][2], "48000 Hz", "Speaker's sample rate as listed")
        check_values(app, {"tcSoundCard1InDevice": "none", "tcSoundCard1OutDevice": "none"})

        set_value(app, "lcSoundCard1InDevice", VirtualAudioDevices.input_label(RADIO_RX))
        set_value(app, "lcSoundCard1OutDevice", SPEAKER)
        check_values(app, {"tcSoundCard1InDevice": VirtualAudioDevices.input_label(RADIO_RX),
                           "tcSoundCard1OutDevice": SPEAKER})

        # Receive only until told otherwise.
        go_to_tab(app, "TX Audio")
        check_values(app, {"ckTxReceiveOnly": True})
        check_enabled(app, transmit_controls, False)
        press_ok(app)
        check_saved(app, {
            "/Audio/soundCard1InDeviceName": RADIO_RX_IN, "/Audio/soundCard1InSampleRate": 48000,
            "/Audio/soundCard1OutDeviceName": SPEAKER, "/Audio/soundCard1OutSampleRate": 48000,
            "/Audio/soundCard2InDeviceName": "none", "/Audio/soundCard2OutDeviceName": "none",
        })

    with audio_devices():
        # Transmit devices that aren't set at all start out the same as
        # ones saved as "none".
        with freedvtest.start(settings={}) as app:
            first_settings(app)
            quit_and_check_log(app)

        with freedvtest.start(settings={"/Audio/soundCard2InDeviceName": "none",
                                        "/Audio/soundCard2OutDeviceName": "none"}) as app:
            first_settings(app)

            # With transmit devices as well, the speaker becomes the second
            # sound card's output and the radio the first's.
            open_settings(app, "RX Audio")
            check_values(app, {"tcSoundCard1InDevice": VirtualAudioDevices.input_label(RADIO_RX),
                               "tcSoundCard1OutDevice": SPEAKER})
            go_to_tab(app, "TX Audio")
            set_value(app, "ckTxReceiveOnly", False)
            check_enabled(app, transmit_controls, True)
            set_value(app, "lcSoundCard2InDevice", VirtualAudioDevices.input_label(MICROPHONE))
            set_value(app, "lcSoundCard2OutDevice", RADIO_TX)
            press_ok(app)
            two_cards = {
                "/Audio/soundCard1InDeviceName": RADIO_RX_IN, "/Audio/soundCard1OutDeviceName": RADIO_TX,
                "/Audio/soundCard2InDeviceName": MICROPHONE_IN, "/Audio/soundCard2OutDeviceName": SPEAKER,
                "/Audio/soundCard1OutSampleRate": 48000, "/Audio/soundCard2InSampleRate": 48000,
                "/Audio/soundCard2OutSampleRate": 48000,
            }
            check_saved(app, two_cards)

            # FreeDV can be started with what was chosen.
            start_modem(app)
            check_equal(app.state()["numSoundCards"], 2, "Number of sound cards in use")
            stop_modem(app)

            # It's all still there next time...
            open_settings(app, "RX Audio")
            check_values(app, {"tcSoundCard1InDevice": VirtualAudioDevices.input_label(RADIO_RX),
                               "tcSoundCard1OutDevice": SPEAKER})
            go_to_tab(app, "TX Audio")
            check_values(app, {"ckTxReceiveOnly": False,
                               "tcSoundCard2InDevice": VirtualAudioDevices.input_label(MICROPHONE),
                               "tcSoundCard2OutDevice": RADIO_TX})

            # ...including after the lists are refreshed, which is how a
            # device that's just been plugged in gets to be listed, and how a
            # removal of a device gets noticed too. (Only the refreshing on
            # macOS and Windows, where a device can't be plugged in here.)
            def new_device_unlisted():
                return "FreeDVGuiTest_New_Device" not in device_labels(app, "lcSoundCard2OutDevice")
            with contextlib.ExitStack() as new_device:
                if not freedvtest.audio.FIXED_DEVICES:
                    new_device.enter_context(VirtualAudioDevices("FreeDVGuiTest_New_Device"))
                    check(new_device_unlisted(), "A new device was listed without refreshing")
                app.click(name("btnRefreshTxAudio"))
                if not freedvtest.audio.FIXED_DEVICES:
                    check(not new_device_unlisted(), "A new device wasn't listed after refreshing")
                    # Refreshing one tab's lists refreshes the other's too.
                    go_to_tab(app, "RX Audio")
                    check("FreeDVGuiTest_New_Device" in device_labels(app, "lcSoundCard1OutDevice"),
                          "A new device wasn't listed on the other tab after refreshing")
                    go_to_tab(app, "TX Audio")
                    new_device.close()
                    app.click(name("btnRefreshTxAudio"))
                    check(new_device_unlisted(), "An unplugged device was still listed after refreshing")
                check_values(app, {"tcSoundCard2InDevice": VirtualAudioDevices.input_label(MICROPHONE),
                                   "tcSoundCard2OutDevice": RADIO_TX})
                go_to_tab(app, "RX Audio")
                if not freedvtest.audio.FIXED_DEVICES:
                    check("FreeDVGuiTest_New_Device" not in device_labels(app, "lcSoundCard1OutDevice"),
                          "An unplugged device was listed on the other tab after refreshing")
                check_values(app, {"tcSoundCard1InDevice": VirtualAudioDevices.input_label(RADIO_RX),
                                   "tcSoundCard1OutDevice": SPEAKER})
                app.click(name("btnRefreshRxAudio"))
                check_values(app, {"tcSoundCard1InDevice": VirtualAudioDevices.input_label(RADIO_RX),
                                   "tcSoundCard1OutDevice": SPEAKER})
            go_to_tab(app, "TX Audio")
            set_value(app, "ckTxReceiveOnly", True)
            check_enabled(app, transmit_controls, False)
            press_ok(app)
            check_saved(app, {
                "/Audio/soundCard1InDeviceName": RADIO_RX_IN, "/Audio/soundCard1OutDeviceName": SPEAKER,
                "/Audio/soundCard2InDeviceName": "none", "/Audio/soundCard2OutDeviceName": "none",
            })
            start_modem(app)
            check_equal(app.state()["numSoundCards"], 1, "Number of sound cards in use")
            stop_modem(app)

            quit_and_check_log(app)


def test_audio_tests():
    """The Test buttons next to each audio device."""
    test_buttons_ = ["btnSoundCard1InTest", "btnSoundCard1OutTest", "btnSoundCard2InTest", "btnSoundCard2OutTest"]

    def wait_for_test_to_finish(app, button):
        app.wait_until(lambda: label(app, button) == "Test", timeout=30, description="the audio test to finish")
        check_enabled(app, ["btnSoundCard1InTest", "btnSoundCard1OutTest"], True)

    with audio_devices():
        with freedvtest.start(settings=audio_settings()) as app:
            open_settings(app, "RX Audio")
            check_enabled(app, ["btnSoundCard1InTest", "btnSoundCard1OutTest"], True)

            # An output: a tone is played through it for a couple of seconds.
            check(record_level(VirtualAudioDevices.input_name(SPEAKER), 0.5) < 50, "The speaker isn't silent")
            app.click(name("btnSoundCard1OutTest"))
            check_equal(label(app, "btnSoundCard1OutTest"), "Playing", "Test button's label while testing")
            check_enabled(app, ["btnSoundCard1InTest", "btnSoundCard1OutTest"], False)
            level = record_level(VirtualAudioDevices.input_name(SPEAKER), 1.0)
            check(level > 500, "Nothing was played through the speaker (level %d)" % level)
            wait_for_test_to_finish(app, "btnSoundCard1OutTest")
            check(record_level(VirtualAudioDevices.input_name(SPEAKER), 0.5) < 50,
                  "The speaker is still being played through")

            # An input: what it hears for five seconds is played back
            # through the speaker.
            with Tone(RADIO_RX):
                app.click(name("btnSoundCard1InTest"))
                check_equal(label(app, "btnSoundCard1InTest"), "Recording", "Test button's label while testing")
                check_enabled(app, ["btnSoundCard1InTest", "btnSoundCard1OutTest"], False)
                app.wait_until(lambda: label(app, "btnSoundCard1InTest") == "Playing", timeout=15,
                               description="the recording to be played back")
            level = record_level(VirtualAudioDevices.input_name(SPEAKER), 1.5)
            check(level > 500, "The recording wasn't played back through the speaker (level %d)" % level)
            wait_for_test_to_finish(app, "btnSoundCard1InTest")

            # The transmit side's devices likewise: the tone goes to the
            # radio, but what the microphone hears comes back through the
            # speaker.
            go_to_tab(app, "TX Audio")
            app.click(name("btnSoundCard2OutTest"))
            check_equal(label(app, "btnSoundCard2OutTest"), "Playing", "Test button's label while testing")
            level = record_level(VirtualAudioDevices.input_name(RADIO_TX), 1.0)
            check(level > 500, "Nothing was played to the radio (level %d)" % level)
            app.wait_until(lambda: label(app, "btnSoundCard2OutTest") == "Test", timeout=30,
                           description="the audio test to finish")

            with Tone(MICROPHONE):
                app.click(name("btnSoundCard2InTest"))
                check_equal(label(app, "btnSoundCard2InTest"), "Recording", "Test button's label while testing")
                app.wait_until(lambda: label(app, "btnSoundCard2InTest") == "Playing", timeout=15,
                               description="the recording to be played back")
            level = record_level(VirtualAudioDevices.input_name(SPEAKER), 1.5)
            radio_level = record_level(VirtualAudioDevices.input_name(RADIO_TX), 0.5)
            check(level > 500, "The microphone recording wasn't played back through the speaker (level %d)" % level)
            check(radio_level < 50, "The microphone recording was played to the radio (level %d)" % radio_level)
            app.wait_until(lambda: label(app, "btnSoundCard2InTest") == "Test", timeout=30,
                           description="the audio test to finish")
            # An input whose matching output is set to none records, but
            # plays its recording to nothing.
            go_to_tab(app, "RX Audio")
            set_value(app, "lcSoundCard1OutDevice", "none")
            with Tone(RADIO_RX):
                app.click(name("btnSoundCard1InTest"))
                check_equal(label(app, "btnSoundCard1InTest"), "Recording", "Test button's label while testing")
                level = record_level(VirtualAudioDevices.input_name(SPEAKER), 4.0)
            check(level < 50, "A recording was played through the speaker although its device is none (level %d)" % level)
            app.wait_until(lambda: label(app, "btnSoundCard1InTest") == "Test", timeout=15,
                           description="the test to finish without playback")


            # There has to be a device to test.
            go_to_tab(app, "RX Audio")
            set_value(app, "lcSoundCard1OutDevice", "none")
            app.click(name("btnSoundCard1OutTest"))
            set_value(app, "lcSoundCard1InDevice", "none")
            app.click(name("btnSoundCard1InTest"))
            check_equal([box["message"] for box in app.message_boxes(clear=True)],
                        ["Please select an audio output device first.", "Please select an audio input device first."],
                        "Messages displayed")
            check_equal(label(app, "btnSoundCard1OutTest"), "Test", "Test button's label")
            press_cancel(app)

            quit_and_check_log(app)

    # A speaker that runs at a different sample rate from what's recorded:
    # the recording still comes back through it, at the pitch it went in at.
    # (Not where devices' rates can't be changed, as on Windows.)
    devices = VirtualAudioDevices(RADIO_RX, RADIO_TX, MICROPHONE, SPEAKER, rates={SPEAKER: 44100})
    try:
        devices.__enter__()
    except freedvtest.audio.Unavailable as e:
        print("Not tried with a speaker at another sample rate: %s" % e)
        return
    try:
        with freedvtest.start(settings=audio_settings()) as app:
            open_settings(app, "RX Audio")
            with Tone(RADIO_RX):
                app.click(name("btnSoundCard1InTest"))
                app.wait_until(lambda: label(app, "btnSoundCard1InTest") == "Playing", timeout=15,
                               description="the recording to be played back")
            share = tone_share(record(VirtualAudioDevices.input_name(SPEAKER), 1.5)[0], 1000)
            check(share > 0.5, "The recording played back isn't the 1000 Hz tone that was recorded (%d%% of it is)"
                  % (100 * share))
            wait_for_test_to_finish(app, "btnSoundCard1InTest")
            check_equal(app.message_boxes(), [], "Messages displayed")
            press_cancel(app)
            quit_and_check_log(app)
    finally:
        devices.__exit__(None, None, None)


def find_rig(app, rig):
    rigs = [item["label"].strip() for item in app.get(name("cbRigName"))["items"]]
    check(rig in rigs, "Hamlib does not have a rig named '%s'" % rig)
    return rig


HAMLIB_CONTROLS = ["cbRigName", "cbSerialPort", "cbSerialRate", "cbPttMethod"]
SERIAL_PTT_CONTROLS = ["cbCtlDevicePath", "rbUseRTS", "ckRTSPos", "rbUseDTR", "ckDTRPos"]
PTT_INPUT_CONTROLS = ["cbCtlDevicePathPttIn", "ckCTSPos"]


def test_radio():
    """The Radio tab: CAT control via Hamlib, serial port PTT and PTT input."""
    with freedvtest.start(settings={}) as app:
        open_settings(app, "Radio")
        rig = find_rig(app, "Yaesu FT-991")
        icom = find_rig(app, "Icom IC-7300")

        # Nothing's usable until the way of keying the radio is chosen.
        check_values(app, {"ckUseHamlibPTT": False, "ckUseSerialPTT": False, "ckUsePTTInput": False})
        check_enabled(app, HAMLIB_CONTROLS + ["cbPttSerialPort", "buttonTestPTT"], False)
        check_enabled(app, SERIAL_PTT_CONTROLS + PTT_INPUT_CONTROLS, False)

        set_value(app, "ckUseHamlibPTT", True)
        check_enabled(app, HAMLIB_CONTROLS + ["buttonTestPTT"], True)
        check_enabled(app, SERIAL_PTT_CONTROLS, False)

        # A separate serial device for PTT only applies to RTS and DTR.
        for method, needs_port in [("CAT", False), ("RTS", True), ("None", False), ("DTR", True),
                                   ("CAT via Data port", False), ("CAT", False)]:
            set_value(app, "cbPttMethod", method)
            check_equal(app.get(name("cbPttSerialPort"))["enabled"], needs_port,
                        "PTT serial device enabled for %s" % method)

        # Only the serial rates that the chosen rig supports are offered, and
        # only Icom radios have an address.
        def rates():
            return [item["label"] for item in app.get(name("cbSerialRate"))["items"]]

        all_rates = rates()
        check_equal(app.get(name("tcIcomCIVHex"))["shown"], False, "Radio address shown with no rig chosen")
        set_value(app, "cbRigName", rig)
        check_equal(app.get(name("tcIcomCIVHex"))["shown"], False, "Radio address shown for %s" % rig)
        check_equal(rates()[0], "default", "First serial rate")
        check(len(rates()) < len(all_rates), "Serial rates weren't narrowed down for %s" % rig)
        set_value(app, "cbRigName", icom)
        check_equal(app.get(name("tcIcomCIVHex"))["shown"], True, "Radio address shown for %s" % icom)
        check("19200" in rates(), "%s cannot use 19200 baud" % icom)
        set_value(app, "cbSerialRate", "19200")
        set_value(app, "tcIcomCIVHex", "94")

        # While PTT is by CAT, it uses the same serial device as everything else.
        set_value(app, "cbSerialPort", "/dev/ttyFreeDVTest0")
        check_values(app, {"cbPttSerialPort": "/dev/ttyFreeDVTest0"})
        set_value(app, "cbPttMethod", "RTS")
        set_value(app, "cbPttSerialPort", "/dev/ttyFreeDVTest1")
        set_value(app, "cbSerialPort", "/dev/ttyFreeDVTest2")
        check_values(app, {"cbPttSerialPort": "/dev/ttyFreeDVTest1"})
        set_value(app, "ckForceRTSOn", True)

        # Hamlib and serial port PTT can't both be used...
        set_value(app, "ckUseSerialPTT", True)
        check_values(app, {"ckUseHamlibPTT": False})
        check_enabled(app, HAMLIB_CONTROLS + ["cbPttSerialPort"], False)
        check_enabled(app, SERIAL_PTT_CONTROLS + ["buttonTestPTT"], True)
        set_value(app, "ckUseHamlibPTT", True)
        check_values(app, {"ckUseSerialPTT": False})
        check_enabled(app, HAMLIB_CONTROLS + ["cbPttSerialPort"], True)
        check_enabled(app, SERIAL_PTT_CONTROLS, False)

        # ...but PTT input goes with either, or neither.
        set_value(app, "ckUsePTTInput", True)
        check_values(app, {"ckUseHamlibPTT": True})
        check_enabled(app, PTT_INPUT_CONTROLS, True)
        set_value(app, "cbCtlDevicePathPttIn", "/dev/ttyFreeDVTest3")
        set_value(app, "ckCTSPos", True)

        hamlib_values = {
            "ckUseHamlibPTT": True, "cbRigName": icom, "cbSerialPort": "/dev/ttyFreeDVTest2", "cbSerialRate": "19200",
            "tcIcomCIVHex": "94", "cbPttMethod": "RTS", "cbPttSerialPort": "/dev/ttyFreeDVTest1",
            "ckForceRTSOn": True, "ckForceDTROn": False, "ckUseSerialPTT": False,
            "ckUsePTTInput": True, "cbCtlDevicePathPttIn": "/dev/ttyFreeDVTest3", "ckCTSPos": True,
        }
        check_values(app, hamlib_values)
        press_apply(app)
        check_saved(app, {
            "/Hamlib/UseForPTT": 1, "/Hamlib/RigNameStr": icom, "/Hamlib/SerialPort": "/dev/ttyFreeDVTest2",
            "/Hamlib/SerialRate": 19200, "/Hamlib/IcomCIVHex": 0x94, "/Hamlib/PttType": PTT_VIA_RTS,
            "/Hamlib/PttSerialPort": "/dev/ttyFreeDVTest1", "/Hamlib/ForceRTSOn": 1, "/Hamlib/ForceDTROn": 0,
            "/Rig/UseSerialPTT": 0, "/Rig/UseSerialPTTInput": 1, "/Rig/PttInPort": "/dev/ttyFreeDVTest3",
            "/Rig/CTSPolarity": 1,
        })
        check_values(app, hamlib_values)

        # Serial port PTT instead, and the default serial rate.
        set_value(app, "cbSerialRate", "default")
        set_value(app, "ckUseSerialPTT", True)
        set_value(app, "cbCtlDevicePath", "/dev/ttyFreeDVTest4")
        app.click(name("rbUseDTR"))
        check_values(app, {"rbUseDTR": True, "rbUseRTS": False})
        set_value(app, "ckDTRPos", True)
        set_value(app, "ckRTSPos", False)
        set_value(app, "ckUsePTTInput", False)
        check_enabled(app, PTT_INPUT_CONTROLS, False)
        set_value(app, "ckLeftChannelVoxTone", True)
        press_ok(app)
        check_saved(app, {
            "/Hamlib/UseForPTT": 0, "/Hamlib/SerialRate": 0, "/Rig/UseSerialPTT": 1,
            "/Rig/Port": "/dev/ttyFreeDVTest4", "/Rig/UseDTR": 1, "/Rig/UseRTS": 0, "/Rig/DTRPolarity": 1,
            "/Rig/RTSPolarity": 0, "/Rig/UseSerialPTTInput": 0, "/Rig/leftChannelVoxTone": 1,
        })

        open_settings(app, "Radio")
        check_values(app, {
            "ckUseHamlibPTT": False, "ckUseSerialPTT": True, "cbCtlDevicePath": "/dev/ttyFreeDVTest4",
            "rbUseDTR": True, "rbUseRTS": False, "ckDTRPos": True, "ckRTSPos": False, "ckUsePTTInput": False,
            "ckLeftChannelVoxTone": True, "cbRigName": icom, "cbSerialRate": "default",
        })
        check_enabled(app, SERIAL_PTT_CONTROLS, True)
        check_enabled(app, HAMLIB_CONTROLS, False)
        press_cancel(app)

        quit_and_check_log(app)


def test_ptt_test():
    """The Radio tab's Test PTT button."""
    everything_else = ["ckUseHamlibPTT", "ckUseSerialPTT", "ckUsePTTInput", "ckLeftChannelVoxTone",
                       "sdbSizer5OK", "sdbSizer5Cancel", "sdbSizer5Apply"]

    def wait_for_test_to_finish(app):
        app.wait_until(lambda: app.get(name("buttonTestPTT"))["enabled"], timeout=30,
                       description="the PTT test to finish")
        check_enabled(app, everything_else, True)

    with freedvtest.start(settings={}) as app:
        open_settings(app, "Radio")

        # A radio that works: Hamlib's "Dummy" rig. It's keyed for a second,
        # during which nothing else can be done.
        set_value(app, "ckUseHamlibPTT", True)
        set_value(app, "cbRigName", find_rig(app, "Hamlib Dummy"))
        app.click(name("buttonTestPTT"))
        check_enabled(app, ["buttonTestPTT"] + everything_else, False)
        wait_for_test_to_finish(app)
        check_equal(app.message_boxes(), [], "Messages displayed by a successful test")

        # Keying by RTS through Hamlib works just as CAT does.
        set_value(app, "cbPttMethod", "RTS")
        app.click(name("buttonTestPTT"))
        wait_for_test_to_finish(app)
        check_equal(app.message_boxes(), [], "Messages displayed by a successful RTS test")
        set_value(app, "cbPttMethod", "CAT")

        # Keying by tone can't be tested from here, which gets said.
        set_value(app, "ckLeftChannelVoxTone", True)
        app.click(name("buttonTestPTT"))
        wait_for_test_to_finish(app)
        check_equal([box["message"] for box in app.message_boxes(clear=True)],
                    ["Testing of tone based PTT not supported; try PTT after pressing Start on main window"],
                    "Messages displayed")
        set_value(app, "ckLeftChannelVoxTone", False)

        # A radio that isn't there.
        set_value(app, "cbRigName", find_rig(app, "Yaesu FT-991"))
        set_value(app, "cbSerialPort", "/dev/ttyFreeDVTestMissing")
        app.click(name("buttonTestPTT"))
        wait_for_test_to_finish(app)
        messages = [box["message"] for box in app.message_boxes(clear=True)]
        check_equal(len(messages), 1, "Number of messages displayed for a radio that isn't there (%r)" % messages)
        check("/dev/ttyFreeDVTestMissing" in messages[0] or "Couldn't connect to Radio" in messages[0],
              "Unexpected message: %r" % messages[0])

        # A serial port that isn't there.
        set_value(app, "ckUseSerialPTT", True)
        set_value(app, "cbCtlDevicePath", "/dev/ttyFreeDVTestMissing")
        app.click(name("buttonTestPTT"))
        wait_for_test_to_finish(app)
        messages = [box["message"] for box in app.message_boxes(clear=True)]
        check_equal(len(messages), 1, "Number of messages displayed for a port that isn't there (%r)" % messages)
        check("/dev/ttyFreeDVTestMissing" in messages[0], "Unexpected message: %r" % messages[0])

        # Somewhere that FreeDV can't check for itself beforehand: what goes
        # wrong is whatever Hamlib or the system says did.
        set_value(app, "ckUseHamlibPTT", True)
        set_value(app, "cbRigName", find_rig(app, "Hamlib NET rigctl"))
        set_value(app, "cbSerialPort", "127.0.0.1:9")
        set_value(app, "cbSerialRate", "9600")
        app.click(name("buttonTestPTT"))
        wait_for_test_to_finish(app)
        messages = [box["message"] for box in app.message_boxes(clear=True)]
        check_equal(len(messages), 1, "Number of messages displayed for a radio that can't be reached (%r)" % messages)
        check(messages[0].startswith("Couldn't connect to Radio with Hamlib (")
              and messages[0].endswith("Make sure the Hamlib serial Device, Rate, and Params match your radio"),
              "Unexpected message: %r" % messages[0])

        set_value(app, "ckUseSerialPTT", True)
        set_value(app, "cbCtlDevicePath", "freedv-test-no-such-port")
        app.click(name("buttonTestPTT"))
        wait_for_test_to_finish(app)
        messages = [box["message"] for box in app.message_boxes(clear=True)]
        check_equal(len(messages), 1, "Number of messages displayed for a port that can't be opened (%r)" % messages)
        check(messages[0].startswith("Couldn't open serial port freedv-test-no-such-port ("),
              "Unexpected message: %r" % messages[0])

        press_cancel(app)
        check_equal(app.message_boxes(), [], "Messages displayed")
        check_equal(app.quit(), 0, "FreeDV's exit code")
        # Hamlib has its say about the radio that couldn't be reached. On
        # Windows, Hamlib and FreeDV also log what Windows said about the
        # ports that couldn't be opened.
        expected = ["127.0.0.1:9"]
        if sys.platform == "win32":
            expected += ["ttyFreeDVTestMissing", "WinErrorShow: serial error on CreateFileA",
                         "SerialPortRigController.cpp", "freedv-test-no-such-port"]
        errors = [line for line in app.log().splitlines()
                  if (" ERROR " in line or " FATAL " in line) and not any(text in line for text in expected)]
        check_equal(errors, [], "Errors logged by FreeDV")


def test_rig_control():
    """The Rig Control tab, other than its list of frequencies."""
    with freedvtest.start(settings={}) as app:
        open_settings(app, "Rig Control")

        # Defaults
        check_values(app, {
            "ckboxEnableSpacebarForPTT": True, "txtPTTKeyName": "Space", "ckboxPTTMomentaryMode": False,
            "txtTxRxDelayMilliseconds": "0", "ckboxTOTTimerEnabled": True, "txtTOTTimerSecs": "180",
            "ckboxEnableFreqModeChanges": True, "ckboxEnableFreqChangesOnly": False,
            "ckboxNoFreqModeChanges": False, "ckboxUseAnalogModes": False, "ckboxFrequencyEntryAsKHz": False,
        })
        check_equal(label(app, "btnSetPTTKey"), "Change...", "PTT key button")

        # Choosing a different key for PTT.
        app.click(name("btnSetPTTKey"))
        check_values(app, {"txtPTTKeyName": "Press any key..."})
        check_equal(label(app, "btnSetPTTKey"), "Cancel", "PTT key button while choosing")
        app.key(name("txtPTTKeyName"), "F5")
        check_values(app, {"txtPTTKeyName": "F5"})
        check_equal(label(app, "btnSetPTTKey"), "Change...", "PTT key button after choosing")

        # Changing one's mind: the button again, Escape (which mustn't close
        # the window) or moving on with Tab. Shift and the like aren't keys
        # that can be chosen.
        for back_out in [lambda: app.click(name("btnSetPTTKey")),
                         lambda: app.key(name("txtPTTKeyName"), "Escape"),
                         lambda: app.key(name("txtPTTKeyName"), "Tab")]:
            app.click(name("btnSetPTTKey"))
            check_values(app, {"txtPTTKeyName": "Press any key..."})
            app.key(name("txtPTTKeyName"), "Shift")
            check_values(app, {"txtPTTKeyName": "Press any key..."})
            back_out()
            check(app.window_exists("settings"), "Backing out of choosing a key closed the Settings window")
            check_values(app, {"txtPTTKeyName": "F5"})
            check_equal(label(app, "btnSetPTTKey"), "Change...", "PTT key button after backing out")

        # A letter
        app.click(name("btnSetPTTKey"))
        app.key(name("txtPTTKeyName"), "t")
        check_values(app, {"txtPTTKeyName": "T"})
        app.click(name("btnSetPTTKey"))
        app.key(name("txtPTTKeyName"), "F5")

        # No key at all
        set_value(app, "ckboxEnableSpacebarForPTT", False)
        check_enabled(app, ["txtPTTKeyName", "btnSetPTTKey"], False)
        set_value(app, "ckboxEnableSpacebarForPTT", True)
        check_enabled(app, ["txtPTTKeyName", "btnSetPTTKey"], True)
        check_values(app, {"txtPTTKeyName": "F5"})

        set_value(app, "ckboxPTTMomentaryMode", True)
        set_value(app, "txtTxRxDelayMilliseconds", "250")

        # Time-out timer
        set_value(app, "ckboxTOTTimerEnabled", False)
        check_enabled(app, ["txtTOTTimerSecs"], False)
        set_value(app, "ckboxTOTTimerEnabled", True)
        check_enabled(app, ["txtTOTTimerSecs"], True)
        set_value(app, "txtTOTTimerSecs", "90")

        # Using USB instead of DIGU only matters if FreeDV sets the mode.
        check_enabled(app, ["ckboxUseAnalogModes"], True)
        set_value(app, "ckboxUseAnalogModes", True)
        app.click(name("ckboxEnableFreqChangesOnly"))
        check_values(app, {"ckboxEnableFreqModeChanges": False, "ckboxEnableFreqChangesOnly": True,
                           "ckboxNoFreqModeChanges": False})
        check_enabled(app, ["ckboxUseAnalogModes"], False)
        app.click(name("ckboxNoFreqModeChanges"))
        check_enabled(app, ["ckboxUseAnalogModes"], False)
        app.click(name("ckboxEnableFreqModeChanges"))
        check_enabled(app, ["ckboxUseAnalogModes"], True)
        app.click(name("ckboxEnableFreqChangesOnly"))

        press_ok(app)
        check_saved(app, {
            "/Rig/EnableSpacebarForPTT": 1, "/Rig/PttKeyCode": WXK_F5, "/Rig/PttMomentaryMode": 1,
            "/Audio/TxRxDelayMilliseconds": 250, "/Rig/TotTimerEnabled": 1, "/Rig/TotTimerSecs": 90,
            "/Hamlib/EnableFreqModeChanges": 0, "/Hamlib/EnableFreqChangesOnly": 1, "/Hamlib/UseAnalogModes": 1,
        })

        open_settings(app, "Rig Control")
        check_values(app, {
            "ckboxEnableSpacebarForPTT": True, "txtPTTKeyName": "F5", "ckboxPTTMomentaryMode": True,
            "txtTxRxDelayMilliseconds": "250", "ckboxTOTTimerEnabled": True, "txtTOTTimerSecs": "90",
            "ckboxEnableFreqModeChanges": False, "ckboxEnableFreqChangesOnly": True,
            "ckboxNoFreqModeChanges": False,
        })

        # Neither kind of change; no key; and a time-out that makes no sense
        # is brought up to the shortest that does.
        app.click(name("ckboxNoFreqModeChanges"))
        set_value(app, "ckboxEnableSpacebarForPTT", False)
        set_value(app, "txtTOTTimerSecs", "0")
        press_ok(app)
        check_saved(app, {
            "/Hamlib/EnableFreqModeChanges": 0, "/Hamlib/EnableFreqChangesOnly": 0, "/Rig/EnableSpacebarForPTT": 0,
            "/Rig/TotTimerSecs": 1,
        })
        open_settings(app, "Rig Control")
        check_values(app, {"ckboxNoFreqModeChanges": True, "ckboxEnableSpacebarForPTT": False,
                           "txtTOTTimerSecs": "1"})
        press_cancel(app)

        quit_and_check_log(app)


def test_frequency_list():
    """The Rig Control tab's list of predefined frequencies."""
    buttons = ["freqListAdd", "freqListRemove"]

    # Thousands are grouped as the user's region has them on macOS (where
    # LC_ALL doesn't apply), e.g. "3,625.0" in the US: left out here.
    def ungrouped(text):
        return text.replace(",", "")

    def frequencies(app):
        return [ungrouped(item["label"]) for item in app.get(name("freqList"))["items"]]

    def offered(app):
        return [ungrouped(item["label"]) for item in app.get("main.lbReportFrequencies")["items"]]

    def check_buttons(app, add, remove):
        for button, expected in zip(buttons, [add, remove]):
            check_equal(app.get(name(button))["enabled"], expected, button + " enabled")

    settings = {"/Reporting/FrequencyList": "3.625000,7.177000,14.236000"}
    with freedvtest.start(settings=settings) as app:
        open_settings(app, "Rig Control")
        check_equal(frequencies(app), ["3.6250", "7.1770", "14.2360"], "Frequencies listed")
        check_equal(label(app, "labelEnterFreq"), "Enter frequency (MHz):", "Frequency entry label")
        check_values(app, {"txtCtrlNewFrequency": ""})
        check_buttons(app, False, False)
        # Switching to kHz and back with nothing typed in changes the units
        # on offer but nothing else.
        set_value(app, "ckboxFrequencyEntryAsKHz", True)
        press_apply(app)
        check_equal(frequencies(app), ["3625.0", "7177.0", "14236.0"], "Frequencies listed in kHz")
        set_value(app, "ckboxFrequencyEntryAsKHz", False)
        press_apply(app)
        check_equal(frequencies(app), ["3.6250", "7.1770", "14.2360"], "Frequencies listed again in MHz")
        check_values(app, {"txtCtrlNewFrequency": ""})

        # Applying while something is typed in but the units haven't
        # changed adds nothing.
        set_value(app, "txtCtrlNewFrequency", "10.136")
        press_apply(app)
        check_equal(frequencies(app), ["3.6250", "7.1770", "14.2360"], "Frequencies listed")
        # Something that isn't a frequency can't be added.
        for text in ["abc", "0", "-5"]:
            set_value(app, "txtCtrlNewFrequency", text)
            check_buttons(app, False, False)

        # A new frequency goes into its place in ascending order...
        set_value(app, "txtCtrlNewFrequency", "10.136")
        check_buttons(app, True, False)
        app.click(name("freqListAdd"))
        check_equal(frequencies(app), ["3.6250", "7.1770", "10.1360", "14.2360"], "Frequencies listed")
        check_values(app, {"txtCtrlNewFrequency": ""})

        # ...including at the end.
        set_value(app, "txtCtrlNewFrequency", "28.33")
        app.click(name("freqListAdd"))
        check_equal(frequencies(app), ["3.6250", "7.1770", "10.1360", "14.2360", "28.3300"],
                    "Frequencies listed")

        # One that's already listed can be removed, whether it's typed in...
        set_value(app, "txtCtrlNewFrequency", "3.6250")
        check_buttons(app, False, True)

        # ...or picked from the list.
        set_value(app, "freqList", "7.1770")
        check_values(app, {"txtCtrlNewFrequency": "7.1770"})
        check_buttons(app, False, True)


        set_value(app, "freqList", "14.2360")
        app.click(name("freqListRemove"))
        check_equal(frequencies(app), ["3.6250", "7.1770", "10.1360", "28.3300"], "Frequencies listed")
        check_values(app, {"txtCtrlNewFrequency": ""})
        check_buttons(app, False, False)

        # They're what's offered in the main window once saved.
        press_ok(app)
        check_equal([item["label"] for item in app.get("main.lbReportFrequencies")["items"]],
                    ["3.6250", "7.1770", "10.1360", "28.3300"], "Frequencies offered in the main window")

        # Frequencies can be worked with in kHz instead.
        open_settings(app, "Rig Control")
        check_equal(frequencies(app), ["3.6250", "7.1770", "10.1360", "28.3300"], "Frequencies listed")
        set_value(app, "txtCtrlNewFrequency", "14.236")
        set_value(app, "ckboxFrequencyEntryAsKHz", True)
        press_apply(app)
        check_equal(frequencies(app), ["3625.0", "7177.0", "10136.0", "28330.0"], "Frequencies listed in kHz")
        check_equal(label(app, "labelEnterFreq"), "Enter frequency (kHz):", "Frequency entry label")
        check_equal(ungrouped(value(app, "txtCtrlNewFrequency")), "14236.0", "txtCtrlNewFrequency")
        check_buttons(app, True, False)
        app.click(name("freqListAdd"))
        check_equal(frequencies(app), ["3625.0", "7177.0", "10136.0", "14236.0", "28330.0"],
                    "Frequencies listed in kHz")
        press_ok(app)
        check_saved(app, {"/Reporting/FrequencyAsKHz": 1})
        check_equal(offered(app), ["3625.0", "7177.0", "10136.0", "14236.0", "28330.0"],
                    "Frequencies offered in the main window")

        # And back again, along with what's been typed in so far.
        open_settings(app, "Rig Control")
        check_values(app, {"ckboxFrequencyEntryAsKHz": True})
        set_value(app, "txtCtrlNewFrequency", "21313.5")
        set_value(app, "ckboxFrequencyEntryAsKHz", False)
        press_apply(app)
        check_equal(label(app, "labelEnterFreq"), "Enter frequency (MHz):", "Frequency entry label")
        check_values(app, {"txtCtrlNewFrequency": "21.3135"})
        set_value(app, "txtCtrlNewFrequency", "")
        press_ok(app)
        check_equal([item["label"] for item in app.get("main.lbReportFrequencies")["items"]],
                    ["3.6250", "7.1770", "10.1360", "14.2360", "28.3300"],
                    "Frequencies offered in the main window")

        quit_and_check_log(app)


def test_frequency_list_from_main_window():
    """The predefined frequencies can be added to and deleted from the main
    window's frequency box."""
    # Thousands are grouped as the user's region has them on macOS and
    # Windows, e.g. "3,600.0" in the US: left out here.
    def ungrouped(text):
        return text.replace(",", "")

    def offered(app):
        return [ungrouped(item["label"]) for item in app.get("main.lbReportFrequencies")["items"]]

    def choose(app, target, label):
        """Chooses a context menu item, going by its label without grouping."""
        labels = [item["label"] for item in app.context_menu(target) if ungrouped(item["label"]) == label]
        check(labels, "No %r in the menu" % label)
        return app.context_menu(target, choose=labels[0])

    def list_shown(app):
        return app.get("main.lbReportFrequencies")["shown"]

    settings = {"/Reporting/FrequencyList": "3.625000,7.177000,14.236000",
                "/Reporting/Frequency": "14236000"}
    with freedvtest.start(settings=settings) as app:
        check_equal(offered(app), ["3.6250", "7.1770", "14.2360"], "Frequencies offered")

        # The frequency in the box can be added, into its place in the list.
        app.set_value("main.cboReportFrequency", "10.136")
        items = app.context_menu("main.cboReportFrequency", choose="Add 10.1360MHz to list")
        check_equal([(item["label"], item["enabled"]) for item in items][0],
                    ("Add 10.1360MHz to list", True), "Frequency box's menu")
        check_equal(offered(app), ["3.6250", "7.1770", "10.1360", "14.2360"], "Frequencies offered")

        # ...but only once.
        items = app.context_menu("main.cboReportFrequency")
        check_equal([(item["label"], item["enabled"]) for item in items][0],
                    ("Add 10.1360MHz to list", False), "Frequency box's menu when it's listed")
        # Text that isn't a frequency, or too small to round to one, can't
        # be added: the menu says so and disables the item.
        app.set_value("main.cboReportFrequency", "not a frequency")
        items = app.context_menu("main.cboReportFrequency")
        check_equal([(item["label"], item["enabled"]) for item in items][0],
                    ("Add frequency to list", False), "Menu for text that isn't a frequency")
        app.set_value("main.cboReportFrequency", "0.00004")
        items = app.context_menu("main.cboReportFrequency")
        check_equal([(item["label"], item["enabled"]) for item in items][0],
                    ("Add frequency to list", False), "Menu for too small a frequency")
        app.set_value("main.cboReportFrequency", "10.1360")

        # One in the open list can be deleted from its menu, and the list
        # opens again for the next.
        app.combo_popup("main.cboReportFrequency", True)
        app.wait_until(lambda: list_shown(app), description="the frequencies to be listed")
        app.context_menu("main.lbReportFrequencies", item=0, choose="Delete 3.6250MHz from list")
        check_equal(offered(app), ["7.1770", "10.1360", "14.2360"], "Frequencies offered")
        app.wait_until(lambda: list_shown(app), description="the list to open again")

        # Or with Del on the highlighted one, which leaves the list open.
        app.set_value("main.lbReportFrequencies", "14.2360")
        app.key("main.lbReportFrequencies", "Delete")
        check_equal(offered(app), ["7.1770", "10.1360"], "Frequencies offered")
        check(list_shown(app), "The list closed")
        # Keys in the open list move the highlight about, clamping at the
        # ends, and Enter takes the highlighted frequency.
        app.set_value("main.lbReportFrequencies", "7.1770")
        app.key("main.lbReportFrequencies", "Down")
        check_equal(app.get("main.lbReportFrequencies")["selection"], 1, "Highlight moved down")
        app.key("main.lbReportFrequencies", "Down")
        check_equal(app.get("main.lbReportFrequencies")["selection"], 1, "Highlight clamped at the end")
        app.key("main.lbReportFrequencies", "Up")
        check_equal(app.get("main.lbReportFrequencies")["selection"], 0, "Highlight moved up")
        app.key("main.lbReportFrequencies", "PageDown")
        check_equal(app.get("main.lbReportFrequencies")["selection"], 1, "Highlight paged down")
        app.key("main.lbReportFrequencies", "PageUp")
        check_equal(app.get("main.lbReportFrequencies")["selection"], 0, "Highlight clamped by paging up")
        app.key("main.lbReportFrequencies", "End")
        check_equal(app.get("main.lbReportFrequencies")["selection"], 1, "Highlight at the end")
        app.key("main.lbReportFrequencies", "Home")
        check_equal(app.get("main.lbReportFrequencies")["selection"], 0, "Highlight at the top")
        app.key("main.lbReportFrequencies", NUMPAD_ENTER)
        check_equal(app.get("main.cboReportFrequency")["value"], "7.1770", "Frequency taken with Enter")
        check(not list_shown(app), "The list closed on Enter")

        # Reopening the list: it highlights the frequency the box already
        # has, and Up clamps at the top. A box that matches no item opens
        # with nothing highlighted, and then Up starts at the end. Closing
        # without a pick leaves the box as it was.
        app.combo_popup("main.cboReportFrequency", True)
        app.wait_until(lambda: list_shown(app), description="the frequencies to be listed")
        check_equal(app.get("main.lbReportFrequencies")["selection"], 0, "The open list highlights the box's frequency")
        app.key("main.lbReportFrequencies", "Up")
        check_equal(app.get("main.lbReportFrequencies")["selection"], 0, "Up clamps at the top")
        app.combo_popup("main.cboReportFrequency", False)
        app.set_value("main.cboReportFrequency", "10.136")
        app.combo_popup("main.cboReportFrequency", True)
        app.wait_until(lambda: list_shown(app), description="the frequencies to be listed again")
        check_equal(app.get("main.lbReportFrequencies")["selection"], -1, "A box matching nothing opens with no highlight")
        app.key("main.lbReportFrequencies", "Up")
        check_equal(app.get("main.lbReportFrequencies")["selection"], 1, "Up starts at the end of the list")
        app.combo_popup("main.cboReportFrequency", False)
        check_equal(app.get("main.cboReportFrequency")["value"], "10.136", "Frequency after closing the list")

        # The changes are kept, as Tools > Options shows.
        open_settings(app, "Rig Control")
        check_equal([item["label"] for item in app.get(name("freqList"))["items"]],
                    ["7.1770", "10.1360"], "Frequencies listed in Options")
        press_cancel(app)
        # Deleting down to nothing leaves an empty list that isn't reopened.
        app.combo_popup("main.cboReportFrequency", True)
        app.wait_until(lambda: list_shown(app), description="the frequencies to be listed")
        app.context_menu("main.lbReportFrequencies", item=0, choose="Delete 7.1770MHz from list")
        check_equal(offered(app), ["10.1360"], "The last frequency")
        app.wait_until(lambda: list_shown(app), description="the list to open again")
        app.context_menu("main.lbReportFrequencies", item=0, choose="Delete 10.1360MHz from list")
        check_equal(offered(app), [], "No frequencies left")
        time.sleep(0.5)
        check(not list_shown(app), "An empty list isn't reopened")

        # With an empty list open, nothing is highlighted and keys do
        # nothing, including Delete and the numpad one. Enter there leaves
        # the box alone. On macOS, a key the list doesn't use closes it, so
        # it's opened again for each.
        def open_empty_list():
            if not list_shown(app):
                app.combo_popup("main.cboReportFrequency", True)
                app.wait_until(lambda: list_shown(app), description="the empty list to open")

        open_empty_list()
        check_equal(app.get("main.lbReportFrequencies")["selection"], -1, "Highlight in the empty list")
        for key in ("Down", "PageDown", "PageUp", "x", "Delete", 385):  # 385 is numpad Delete
            open_empty_list()
            app.key("main.lbReportFrequencies", key)
        check_equal(offered(app), [], "Still no frequencies")
        open_empty_list()
        app.key("main.lbReportFrequencies", "Enter")
        check_equal(app.get("main.cboReportFrequency")["value"], "10.136", "Frequency box after Enter on the empty list")
        app.combo_popup("main.cboReportFrequency", False)
        check_equal(app.get("main.cboReportFrequency")["value"], "10.136", "Frequency box after closing the empty list")

        quit_and_check_log(app)

    # In kHz mode, the box and the list show the list's frequencies in kHz,
    # to one decimal place, and the box is read as kHz too.
    settings = {"/Reporting/FrequencyList": "3.6,14.2", "/Reporting/FrequencyAsKHz": True,
                "/Reporting/Frequency": "14200000"}
    with freedvtest.start(settings=settings) as app:
        check_equal(offered(app), ["3600.0", "14200.0"], "Frequencies offered in kHz")
        app.set_value("main.cboReportFrequency", "10136")
        items = choose(app, "main.cboReportFrequency", "Add 10136.0kHz to list")
        check_equal([(ungrouped(item["label"]), item["enabled"]) for item in items][0],
                    ("Add 10136.0kHz to list", True), "Frequency box's menu in kHz")
        check_equal(offered(app), ["3600.0", "10136.0", "14200.0"], "Frequencies offered")
        app.combo_popup("main.cboReportFrequency", True)
        app.wait_until(lambda: list_shown(app), description="the frequencies to be listed")
        listed = app.get("main.lbReportFrequencies")["items"][1]["label"]
        check_equal(ungrouped(listed), "10136.0", "Second frequency listed")
        app.context_menu("main.lbReportFrequencies", item=1, choose="Delete %skHz from list" % listed)
        check_equal(offered(app), ["3600.0", "14200.0"], "Frequencies after a kHz delete")
        # The delete reopened the list; quitting with it still open has to
        # work too.
        app.wait_until(lambda: list_shown(app), description="the list to open again")
        quit_and_check_log(app)


def test_reporting():
    """The Reporting tab."""
    needs_reporting = ["txt_callsign", "txt_grid_square", "ckboxManualFrequencyReporting", "ckboxPskReporterEnable",
                       "ckboxFreeDVReporterEnable", "ckboxUDPReportingEnable", "ckboxUDPBroadcastEnable"]

    with freedvtest.start(settings={"/Reporting/FreeDV/Hostname": NOWHERE, "/Reporting/FreeDV/UseTls": False}) as app:
        app.wait_for_window("main")

        # Without reporting, the main window just has somewhere to show the
        # last callsign received.
        check_equal(app.get("main.textCallSign")["shown"], True, "Callsign box shown")
        check_equal(app.get("main.cboLastReportedCallsigns")["shown"], False, "Callsign list shown")
        check_equal(app.get("main.btnReporterHidden")["enabled"], False, "FreeDV Reporter button enabled")

        open_settings(app, "Reporting")
        check_values(app, {"ckboxReportingEnable": False, "txt_callsign": "", "txt_grid_square": "",
                           "freedvReporterHostname": NOWHERE, "ckboxFreeDVReporterUseTls": False,
                           "udpHostname": "127.0.0.1", "udpPort": "2237",
                           "udpBroadcastAddress": "224.0.0.1", "udpBroadcastPort": "7177"})
        check_enabled(app, needs_reporting + ["udpHostname", "udpPort", "udpBroadcastAddress", "udpBroadcastPort"],
                      False)
        # These don't depend on reporting being enabled.
        check_enabled(app, ["freedvReporterHostname", "ckboxFreeDVReporterUseTls", "useMetricDistances",
                            "useCardinalDirections", "ckbox_use_utc_time"], True)

        set_value(app, "ckboxReportingEnable", True)
        check_enabled(app, needs_reporting, True)
        check_enabled(app, ["udpHostname", "udpPort", "udpBroadcastAddress", "udpBroadcastPort"], False)
        set_value(app, "ckboxUDPReportingEnable", True)
        check_enabled(app, ["udpHostname", "udpPort"], True)
        check_enabled(app, ["udpBroadcastAddress", "udpBroadcastPort"], False)
        set_value(app, "ckboxUDPBroadcastEnable", True)
        check_enabled(app, ["udpBroadcastAddress", "udpBroadcastPort"], True)
        set_value(app, "ckboxUDPReportingEnable", False)
        check_enabled(app, ["udpHostname", "udpPort"], False)
        set_value(app, "ckboxUDPReportingEnable", True)

        set_value(app, "txt_callsign", "AA1ZZZ")
        set_value(app, "txt_grid_square", "DM12")
        set_value(app, "ckboxManualFrequencyReporting", True)
        set_value(app, "ckboxPskReporterEnable", False)
        set_value(app, "udpHostname", "192.0.2.1")
        set_value(app, "udpPort", "2238")
        set_value(app, "udpBroadcastAddress", "224.0.0.2")
        set_value(app, "udpBroadcastPort", "7178")
        set_value(app, "useMetricDistances", False)
        set_value(app, "useCardinalDirections", True)
        set_value(app, "ckboxFreeDVReporterForceReceiveOnly", True)
        set_value(app, "ckbox_use_utc_time", True)

        # Where received callsigns are logged: nothing changes if the file
        # chooser is cancelled.
        original_path = value(app, "txtCtrlCsvLogFilePath")
        app.click(name("buttonChooseCsvLogFilePath"))
        check_values(app, {"txtCtrlCsvLogFilePath": original_path})
        app.choose_next_path("/tmp/freedvtest-log.csv")
        app.click(name("buttonChooseCsvLogFilePath"))
        check_values(app, {"txtCtrlCsvLogFilePath": "/tmp/freedvtest-log.csv"})
        check_equal(app.path_dialogs(clear=True), ["Choose CSV log file location"] * 2, "File choosers displayed")

        press_ok(app)
        check_saved(app, {
            "/Reporting/Enable": 1, "/Reporting/Callsign": "AA1ZZZ", "/Reporting/GridSquare": "DM12",
            "/Reporting/ManualFrequencyReporting": 1, "/Reporting/PSKReporter/Enable": 0,
            "/Reporting/FreeDV/Enable": 1, "/Reporting/FreeDV/Hostname": NOWHERE, "/Reporting/FreeDV/UseTls": 0,
            "/Reporting/FreeDV/UseMetricDistances": 0, "/Reporting/DirectionAsCardinal": 1,
            "/Reporting/FreeDV/ForceReceiveOnly": 1, "/Reporting/UDP/Enable": 1,
            "/Reporting/UDP/Hostname": "192.0.2.1", "/Reporting/UDP/Port": 2238,
            "/Reporting/UDPBroadcast/Enable": 1, "/Reporting/UDPBroadcast/Address": "224.0.0.2",
            "/Reporting/UDPBroadcast/Port": 7178, "/Reporting/CSV/LogFilePath": "/tmp/freedvtest-log.csv",
            "/CallsignList/UseUTCTime": 1,
        })

        # With reporting, the main window keeps a list of callsigns and can
        # hide FreeDV from FreeDV Reporter.
        check_equal(app.get("main.textCallSign")["shown"], False, "Callsign box shown")
        check_equal(app.get("main.cboLastReportedCallsigns")["shown"], True, "Callsign list shown")
        check_equal(app.get("main.btnReporterHidden")["enabled"], True, "FreeDV Reporter button enabled")

        # The FreeDV Reporter window follows the units chosen.
        app.menu("Tools/FreeDV Reporter")
        app.wait_for_window("reporter")
        app.activate("reporter")
        titles = [column["title"] for column in app.columns("reporter.listSpots")]
        check("Miles" in titles and "Dir" in titles, "FreeDV Reporter's columns are %r" % titles)
        app.click("reporter.btnClose")
        app.wait_for_window_closed("reporter")

        open_settings(app, "Reporting")
        check_values(app, {
            "ckboxReportingEnable": True, "txt_callsign": "AA1ZZZ", "txt_grid_square": "DM12",
            "ckboxManualFrequencyReporting": True, "ckboxPskReporterEnable": False,
            "ckboxFreeDVReporterEnable": True, "ckboxUDPReportingEnable": True, "udpHostname": "192.0.2.1",
            "udpPort": "2238", "ckboxUDPBroadcastEnable": True, "udpBroadcastAddress": "224.0.0.2",
            "udpBroadcastPort": "7178", "useMetricDistances": False, "useCardinalDirections": True,
            "ckboxFreeDVReporterForceReceiveOnly": True, "ckbox_use_utc_time": True,
            "txtCtrlCsvLogFilePath": "/tmp/freedvtest-log.csv",
        })

        # Not reporting to FreeDV Reporter in particular.
        set_value(app, "ckboxFreeDVReporterEnable", False)
        press_ok(app)
        check_saved(app, {"/Reporting/Enable": 1, "/Reporting/FreeDV/Enable": 0})
        check_equal(app.get("main.btnReporterHidden")["enabled"], False, "FreeDV Reporter button enabled")

        # Turning reporting off keeps who it was reporting as.
        open_settings(app, "Reporting")
        set_value(app, "ckboxReportingEnable", False)
        check_enabled(app, needs_reporting + ["udpHostname", "udpPort", "udpBroadcastAddress", "udpBroadcastPort"],
                      False)
        press_ok(app)
        check_saved(app, {"/Reporting/Enable": 0, "/Reporting/Callsign": "AA1ZZZ", "/Reporting/GridSquare": "DM12"})
        check_equal(app.get("main.textCallSign")["shown"], True, "Callsign box shown")
        check_equal(app.get("main.cboLastReportedCallsigns")["shown"], False, "Callsign list shown")

        quit_and_check_log(app)


def test_voice_keyer():
    """The Voice Keyer tab."""
    with freedvtest.start(settings={}) as app:
        open_settings(app, "Voice Keyer")
        check_values(app, {"txtCtrlVoiceKeyerRxPause": "10", "txtCtrlVoiceKeyerRepeats": "5"})

        # Each folder has its own chooser, which changes nothing if cancelled.
        choosers = [
            ("buttonChooseVoiceKeyerWaveFilePath", "txtCtrlVoiceKeyerWaveFilePath", "Voice Keyer file location",
             "/tmp/freedvtest-voice-keyer"),
            ("buttonChooseQuickRecordRawPath", "txtCtrlQuickRecordRawPath", "Choose Quick Record save location",
             "/tmp/freedvtest-raw"),
            ("buttonChooseQuickRecordDecodedPath", "txtCtrlQuickRecordDecodedPath",
             "Choose Quick Record save location", "/tmp/freedvtest-decoded"),
        ]
        for button, text_box, title, folder in choosers:
            original = value(app, text_box)
            app.click(name(button))
            check_values(app, {text_box: original})
            app.choose_next_path(folder)
            app.click(name(button))
            check_values(app, {text_box: folder})
            check_equal(app.path_dialogs(clear=True), [title, title], "Folder choosers displayed")
        check_values(app, {"txtCtrlVoiceKeyerWaveFilePath": "/tmp/freedvtest-voice-keyer",
                           "txtCtrlQuickRecordRawPath": "/tmp/freedvtest-raw",
                           "txtCtrlQuickRecordDecodedPath": "/tmp/freedvtest-decoded"})

        set_value(app, "txtCtrlVoiceKeyerRxPause", "15")
        set_value(app, "txtCtrlVoiceKeyerRepeats", "3")
        press_ok(app)
        check_saved(app, {
            "/VoiceKeyer/WaveFilePath": "/tmp/freedvtest-voice-keyer", "/VoiceKeyer/RxPause": 15,
            "/VoiceKeyer/Repeats": 3, "/QuickRecord/SavePath": "/tmp/freedvtest-raw",
            "/QuickRecord/SaveDecodedPath": "/tmp/freedvtest-decoded",
        })

        # Numbers that make no sense are brought within range.
        for pause, repeats, saved_pause, saved_repeats in [("-5", "-1", 0, 0), ("20", "500", 20, 100)]:
            open_settings(app, "Voice Keyer")
            set_value(app, "txtCtrlVoiceKeyerRxPause", pause)
            set_value(app, "txtCtrlVoiceKeyerRepeats", repeats)
            press_ok(app)
            check_saved(app, {"/VoiceKeyer/RxPause": saved_pause, "/VoiceKeyer/Repeats": saved_repeats})

        open_settings(app, "Voice Keyer")
        check_values(app, {"txtCtrlVoiceKeyerRxPause": "20", "txtCtrlVoiceKeyerRepeats": "100",
                           "txtCtrlVoiceKeyerWaveFilePath": "/tmp/freedvtest-voice-keyer"})
        press_cancel(app)

        quit_and_check_log(app)


def test_display():
    """The Display tab."""
    colors = {
        "freedvReporterTxBackgroundColor": ("/Reporting/FreeDV/TxRowBackgroundColor", "#FC4500", "#102030"),
        "freedvReporterTxForegroundColor": ("/Reporting/FreeDV/TxRowForegroundColor", "#000000", "#FFFFFF"),
        "freedvReporterRxBackgroundColor": ("/Reporting/FreeDV/RxRowBackgroundColor", "#379BAF", "#405060"),
        "freedvReporterRxForegroundColor": ("/Reporting/FreeDV/RxRowForegroundColor", "#000000", "#FFFF00"),
        "freedvReporterMsgBackgroundColor": ("/Reporting/FreeDV/MsgRowBackgroundColor", "#E58BE5", "#708090"),
        "freedvReporterMsgForegroundColor": ("/Reporting/FreeDV/MsgRowForegroundColor", "#000000", "#00FFFF"),
    }
    waterfall = ["waterfallColorScheme1", "waterfallColorScheme2", "waterfallColorScheme3"]

    with freedvtest.start(settings={}) as app:
        open_settings(app, "Display")
        check_values(app, {"waterfallColorScheme1": True, "waterfallColorScheme2": False,
                           "waterfallColorScheme3": False})
        check_values(app, {control: default for control, (_, default, _) in colors.items()})

        # Waterfall colors
        for scheme, control in enumerate(waterfall):
            app.click(name(control))
            check_values(app, {other: other == control for other in waterfall})
            press_apply(app)
            check_saved(app, {"/Waterfall/Color": scheme})

        # Spectrum averaging
        averaging = app.get(name("cbxNumSpectrumAveraging"))
        check_equal(averaging["selection"], 0, "Spectrum averaging")
        check(len(averaging["items"]) > 1, "There's only one choice of spectrum averaging")
        app.set_value(name("cbxNumSpectrumAveraging"), index=len(averaging["items"]) - 1)
        chosen_averaging = value(app, "cbxNumSpectrumAveraging")

        # FreeDV Reporter's colors
        for control, (_, _, new_color) in colors.items():
            set_value(app, control, new_color)

        press_ok(app)
        expected = {key: new_color for key, _, new_color in colors.values()}
        expected["/Waterfall/Color"] = 2
        expected["/Plot/Spectrum/CurrentAveraging"] = len(averaging["items"]) - 1
        check_saved(app, expected)

        open_settings(app, "Display")
        check_values(app, {"waterfallColorScheme1": False, "waterfallColorScheme2": False,
                           "waterfallColorScheme3": True, "cbxNumSpectrumAveraging": chosen_averaging})
        check_values(app, {control: new_color for control, (_, _, new_color) in colors.items()})
        press_cancel(app)

        quit_and_check_log(app)


def test_modem_and_debugging():
    """The Modem and Debugging tabs."""
    with freedvtest.start(settings={}) as app:
        app.wait_for_window("main")
        check_equal(app.get("main.textBits")["shown"], False, "Decode stats shown in the main window")

        open_settings(app, "Modem")
        check_values(app, {"ckboxAutoStartOnLaunch": False, "ckHalfDuplex": True, "showDecodeStats": False,
                           "statsResetTime": "10"})
        set_value(app, "ckHalfDuplex", False)
        set_value(app, "showDecodeStats", True)
        set_value(app, "statsResetTime", "25")

        go_to_tab(app, "Debugging")
        check_values(app, {"txtCtrlFifoSize": "640", "ckboxVerbose": False, "ckboxFreeDVAPIVerbose": False,
                           "experimentalFeatures": False, "ckboxTone": False})

        # The interfering tone's settings only apply when there is one.
        check_enabled(app, ["txtToneFreqHz", "txtToneAmplitude"], False)
        set_value(app, "ckboxTone", True)
        check_enabled(app, ["txtToneFreqHz", "txtToneAmplitude"], True)
        set_value(app, "txtToneFreqHz", "1500")
        set_value(app, "txtToneAmplitude", "500")

        set_value(app, "txtCtrlFifoSize", "480")
        set_value(app, "ckboxVerbose", True)
        set_value(app, "ckboxFreeDVAPIVerbose", True)
        set_value(app, "experimentalFeatures", True)
        for checkbox in ["ckboxTxRxThreadPriority", "ckboxTxRxDumpTiming", "ckboxTxRxDumpFifoState"]:
            app.click(name(checkbox))
        app.click(name("BtnFifoReset"))

        press_ok(app)
        check_saved(app, {
            "/Rig/HalfDuplex": 0, "/Debug/showDecodeStats": 1, "/Stats/ResetTime": 25, "/Audio/fifoSize_ms": 480,
            "/Debug/verbose": 1, "/Debug/APIverbose": 1, "/ExperimentalFeatures": 1,
        })
        check_equal(app.get("main.textBits")["shown"], True, "Decode stats shown in the main window")

        # The tone isn't a saved setting, but lasts as long as FreeDV runs.
        open_settings(app, "Modem")
        check_values(app, {"ckHalfDuplex": False, "showDecodeStats": True, "statsResetTime": "25"})
        go_to_tab(app, "Debugging")
        check_values(app, {"txtCtrlFifoSize": "480", "ckboxVerbose": True, "ckboxFreeDVAPIVerbose": True,
                           "experimentalFeatures": True, "ckboxTone": True, "txtToneFreqHz": "1500",
                           "txtToneAmplitude": "500"})
        set_value(app, "ckboxTone", False)
        set_value(app, "ckboxVerbose", False)
        go_to_tab(app, "Modem")
        set_value(app, "showDecodeStats", False)
        press_ok(app)
        check_saved(app, {"/Debug/showDecodeStats": 0, "/Debug/verbose": 0})
        check_equal(app.get("main.textBits")["shown"], False, "Decode stats shown in the main window")

        quit_and_check_log(app)


def test_ptt_key_names():
    """Choosing the key used for PTT: what each kind of key is called, and
    the keys that can't be chosen."""
    with freedvtest.start(settings={}) as app:
        open_settings(app, "Rig Control")
        check_values(app, {"txtPTTKeyName": "Space"})

        def choose(key):
            app.click(name("btnSetPTTKey"))
            check_values(app, {"txtPTTKeyName": "Press any key..."})
            app.key(name("txtPTTKeyName"), key)

        for key, called in [("5", "5"), ("0", "0"), ("q", "Q"), ("Z", "Z"), ("F1", "F1"), ("F12", "F12"),
                            ("Enter", "Enter"), ("Backspace", "Backspace"),
                            ("Delete", "Delete"), ("Insert", "Insert"), ("Home", "Home"), ("End", "End"),
                            ("PageUp", "Page Up"), ("PageDown", "Page Down"), ("Up", "Up"), ("Down", "Down"),
                            ("Left", "Left"), ("Right", "Right"), (";", ";"), ("[", "["),
                            # A letter as some keyboards give it, in lower case
                            (ord("q"), "Q"),
                            # F13 and F24 as they are everywhere but Linux, and as they are there
                            (340 + 12, "F13"), (340 + 23, "F24"), (436, "F13"), (447, "F24"),
                            # Something FreeDV has no name for
                            (390, "Key(390)"), ("Space", "Space")]:
            choose(key)
            check_values(app, {"txtPTTKeyName": called})
            check_equal(label(app, "btnSetPTTKey"), "Change...", "PTT key button after choosing %s" % key)

        # Keys that only modify others aren't a choice; FreeDV keeps waiting.
        app.click(name("btnSetPTTKey"))
        # (By number: Alt, Caps Lock, Num Lock, Scroll Lock, both Windows keys
        # and the menu key.)
        for modifier in ["Shift", "Control", 307, 311, 364, 365, 393, 394, 395]:
            app.key(name("txtPTTKeyName"), modifier)
            check_values(app, {"txtPTTKeyName": "Press any key..."})
            check_equal(label(app, "btnSetPTTKey"), "Cancel", "PTT key button after pressing %s" % modifier)
        app.key(name("txtPTTKeyName"), "F7")
        check_values(app, {"txtPTTKeyName": "F7"})

        # Tab moves on, leaving the key as it was...
        choose("Tab")
        check_values(app, {"txtPTTKeyName": "F7"})
        check_equal(label(app, "btnSetPTTKey"), "Change...", "PTT key button after pressing Tab")

        # ...as does thinking better of it.
        app.click(name("btnSetPTTKey"))
        check_equal(label(app, "btnSetPTTKey"), "Cancel", "PTT key button while choosing")
        app.click(name("btnSetPTTKey"))
        check_values(app, {"txtPTTKeyName": "F7"})

        # Keys pressed when FreeDV isn't waiting for one are just keys.
        app.key(name("txtPTTKeyName"), "F9")
        check_values(app, {"txtPTTKeyName": "F7"})

        press_ok(app)
        check_saved(app, {"/Rig/PttKeyCode": 340 + 6})  # F7
        check_equal(app.quit(), 0, "FreeDV's exit code")


def test_auto_start():
    """The Modem tab's Start Automatically on Launch."""
    config_dir = tempfile.mkdtemp(prefix="freedvtest-config-")
    try:
        config = os.path.join(config_dir, "freedv.conf")
        # (Once running, FreeDV looks for its FreeDV Reporter server; keep it
        # away from the public one.)
        freedvtest.write_config(config, dict(audio_settings(), **{"/Reporting/FreeDV/Hostname": NOWHERE,
                                                                  "/Reporting/FreeDV/UseTls": False}))
        with audio_devices():
            with freedvtest.start(config=config) as app:
                app.wait_for_window("main")
                time.sleep(2)
                check_equal(app.state()["running"], False, "Running without having been started")
                open_settings(app, "Modem")
                set_value(app, "ckboxAutoStartOnLaunch", True)
                press_ok(app)
                check_saved(app, {"/Modem/autoStartOnLaunch": 1})
                check_equal(app.state()["running"], False, "Running just for having changed the setting")
                quit_and_check_log(app)

            with freedvtest.start(config=config) as app:
                app.wait_until(lambda: app.state()["running"], description="FreeDV to start by itself")
                app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to finish starting")
                check_equal(app.get("main.btnOnOff")["value"], True, "Start button pressed")
                check_equal(app.message_boxes(), [], "Errors displayed on start")
                stop_modem(app)
                open_settings(app, "Modem")
                check_values(app, {"ckboxAutoStartOnLaunch": True})
                set_value(app, "ckboxAutoStartOnLaunch", False)
                press_ok(app)
                quit_and_check_log(app)

            # With no usable audio devices, it quietly doesn't.
            freedvtest.write_config(config, {"/Modem/autoStartOnLaunch": True})
            with freedvtest.start(config=config) as app:
                app.wait_for_window("main")
                time.sleep(2)
                check_equal(app.state()["running"], False, "Running with no audio devices")
                check(not app.window_exists("settings"), "The Settings window opened by itself")
                quit_and_check_log(app)
    finally:
        shutil.rmtree(config_dir, ignore_errors=True)


def test_while_running():
    """Some settings can't be changed while FreeDV is running."""
    reporting = ["ckboxReportingEnable", "txt_callsign", "txt_grid_square", "ckboxManualFrequencyReporting",
                 "ckboxPskReporterEnable", "ckboxFreeDVReporterEnable", "useMetricDistances",
                 "freedvReporterHostname", "ckboxFreeDVReporterUseTls", "ckboxFreeDVReporterForceReceiveOnly",
                 "useCardinalDirections", "ckboxUDPReportingEnable", "ckboxUDPBroadcastEnable",
                 "ckbox_use_utc_time"]
    rig_control = ["ckboxEnableFreqModeChanges", "ckboxEnableFreqChangesOnly", "ckboxNoFreqModeChanges",
                   "ckboxUseAnalogModes", "ckboxEnableSpacebarForPTT", "txtTxRxDelayMilliseconds"]

    settings = audio_settings()
    settings.update({
        "/Reporting/Enable": True, "/Reporting/Callsign": "AA1ZZZ", "/Reporting/GridSquare": "DM12",
        "/Reporting/PSKReporter/Enable": False, "/Reporting/FreeDV/Hostname": NOWHERE,
        "/Reporting/FreeDV/UseTls": False,
    })
    with audio_devices():
        with freedvtest.start(settings=settings) as app:
            open_settings(app, "Reporting")
            check_enabled(app, reporting, True)
            go_to_tab(app, "Rig Control")
            check_enabled(app, rig_control, True)
            press_cancel(app)

            start_modem(app)
            open_settings(app, "Reporting")
            check_enabled(app, reporting, False)
            go_to_tab(app, "Rig Control")
            check_enabled(app, rig_control, False)

            # Everything else still can be, and is saved.
            go_to_tab(app, "Modem")
            set_value(app, "statsResetTime", "30")
            press_ok(app)
            check_saved(app, {"/Stats/ResetTime": 30, "/Reporting/Enable": 1, "/Reporting/Callsign": "AA1ZZZ"})
            check(app.state()["running"], "Changing settings stopped FreeDV")

            stop_modem(app)
            open_settings(app, "Reporting")
            check_enabled(app, reporting, True)
            go_to_tab(app, "Rig Control")
            check_enabled(app, rig_control, True)
            press_cancel(app)

            quit_and_check_log(app)


SCENARIOS = {
    "buttons": test_buttons,
    "audio": test_audio,
    "audio_tests": test_audio_tests,
    "radio": test_radio,
    "ptt_test": test_ptt_test,
    "rig_control": test_rig_control,
    "frequency_list": test_frequency_list,
    "frequency_list_from_main_window": test_frequency_list_from_main_window,
    "reporting": test_reporting,
    "voice_keyer": test_voice_keyer,
    "display": test_display,
    "modem_and_debugging": test_modem_and_debugging,
    "ptt_key_names": test_ptt_key_names,
    "auto_start": test_auto_start,
    "while_running": test_while_running,
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
