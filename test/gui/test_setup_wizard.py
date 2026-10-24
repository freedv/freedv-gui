#!/usr/bin/env python3
"""Tests FreeDV's Setup Wizard.

Each scenario below launches its own copy of FreeDV. With no arguments all
of them are run; otherwise, only those named:

    python3 test_setup_wizard.py [scenario...]

Run from the build folder (or set FREEDV_BINARY), on Linux or macOS: see
freedvtest/audio.py for the audio devices it needs.
"""

import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import freedvtest
import freedvtest.audio
from freedvtest.audio import VirtualAudioDevices, default_output, device

RADIO_RX = device("FreeDVGuiTest_Radio_To_Computer")
RADIO_TX = device("FreeDVGuiTest_Computer_To_Radio")
MICROPHONE = device("FreeDVGuiTest_Microphone_To_Computer")
SPEAKER = device("FreeDVGuiTest_Computer_To_Speaker")

RADIO_RX_IN = VirtualAudioDevices.input_name(RADIO_RX)
MICROPHONE_IN = VirtualAudioDevices.input_name(MICROPHONE)

# The controls on the Radio Control page that belong to each way of keying
# the radio.
HAMLIB_CONTROLS = ["cbRigName", "cbSerialPort", "cbSerialRate", "cbPttMethod"]
SERIAL_PTT_CONTROLS = ["cbCtlDevicePath", "rbUseRTS", "ckRTSPos", "rbUseDTR", "ckDTRPos"]

# Values of the Hamlib "PTT uses" setting.
PTT_VIA_CAT, PTT_VIA_RTS, PTT_VIA_DTR, PTT_VIA_NONE, PTT_VIA_CAT_DATA = range(5)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def check_equal(actual, expected, what):
    check(actual == expected, "%s is %r, expected %r" % (what, actual, expected))


def check_values(app, expected):
    """Checks the "value" of each of the given Setup Wizard controls."""
    for name, value in expected.items():
        check_equal(app.get("wizard." + name)["value"], value, name)


def check_enabled(app, names, enabled):
    for name in names:
        check_equal(app.get("wizard." + name)["enabled"], enabled, name + " enabled")


def check_config(app, section, expected):
    config = app.state()["config"][section]
    for key, value in expected.items():
        check_equal(config[key], value, "%s setting %s" % (section, key))


def check_audio_config(app, expected):
    """Checks the audio devices that FreeDV is configured to use, and that
    they've been saved rather than just applied."""
    audio = app.state()["config"]["audio"]
    with open(app.config_path, "r") as f:
        saved = f.read()
    for key, device in expected.items():
        check_equal(audio[key]["deviceName"], device, key + " device")
        check("%sDeviceName=%s\n" % (key, device) in saved, "%s=%s is not in %s" % (key, device, app.config_path))
        if device != "none":
            check(audio[key]["sampleRate"] > 0, "%s has no sample rate" % key)


def label(app, name):
    """Returns a control's label without the "&" marking its shortcut key."""
    return app.get(name)["label"].replace("&", "")


def check_wizard_page(app, page):
    check_equal(app.get("wizard.book")["value"], page, "Setup Wizard page")
    check_equal(app.get("wizard.btnPrev")["enabled"], page > 0, "Previous button enabled")
    check_equal(app.get("wizard.btnNext")["shown"], page < 3, "Next button shown")
    check_equal(app.get("wizard.btnFinish")["shown"], page == 3, "Finish button shown")


def go_to_page(app, page):
    """Moves to the given page (0-3) of the Setup Wizard."""
    current = app.get("wizard.book")["value"]
    while current != page:
        app.click("wizard.btnNext" if current < page else "wizard.btnPrev")
        current += 1 if current < page else -1
    check_wizard_page(app, page)


def finish_wizard(app):
    go_to_page(app, 3)
    app.click("wizard.btnFinish")
    app.wait_for_window_closed("wizard")


def reopen_wizard(app):
    app.menu("Settings/Setup Wizard...")
    app.wait_for_window("wizard")
    check_wizard_page(app, 0)


def quit_and_check_log(app):
    check_equal(app.message_boxes(), [], "Errors displayed")
    check_equal(app.quit(), 0, "FreeDV's exit code")
    errors = [line for line in app.log().splitlines() if " ERROR " in line or " FATAL " in line]
    check_equal(errors, [], "Errors logged by FreeDV")


def find_rig(app, name):
    """Returns the rig in the wizard's list with the given name, which is a
    Hamlib manufacturer followed by a model."""
    rigs = [item["label"].strip() for item in app.get("wizard.cbRigName")["items"]]
    check(name in rigs, "Hamlib does not have a rig named '%s'" % name)
    return name


def run_setup_wizard(app):
    """Goes through the wizard as shown on FreeDV's first run, setting up
    all four audio devices and nothing else."""
    wizard = app.wait_for_window("wizard")
    check_equal(wizard["label"], "FreeDV Setup Wizard", "Setup Wizard title")
    check(wizard["modal"], "Setup Wizard is not modal")

    # Step 1: Receive Audio
    check_wizard_page(app, 0)
    app.set_value("wizard.cbRadioIn", data=RADIO_RX_IN)
    app.set_value("wizard.cbSpeakerOut", data=SPEAKER)
    app.click("wizard.btnNext")

    # Step 2: Transmit Audio. Nothing's configured yet, so the wizard should
    # assume receive only until told otherwise.
    check_wizard_page(app, 1)
    check_values(app, {"ckReceiveOnly": True})
    check_enabled(app, ["cbMicIn", "cbRadioOut"], False)
    app.set_value("wizard.ckReceiveOnly", False)
    check_enabled(app, ["cbMicIn", "cbRadioOut"], True)
    app.set_value("wizard.cbMicIn", data=MICROPHONE_IN)
    app.set_value("wizard.cbRadioOut", data=RADIO_TX)
    app.click("wizard.btnNext")

    # Step 3: Radio Control. No radio is attached, so leave it all off.
    check_wizard_page(app, 2)
    check_values(app, {"ckHamlib": False, "ckSerialPTT": False})
    check_enabled(app, HAMLIB_CONTROLS + SERIAL_PTT_CONTROLS, False)
    app.click("wizard.btnNext")

    # Step 4: Reporting. Left off so that the test doesn't appear on
    # FreeDV Reporter or PSK Reporter.
    check_wizard_page(app, 3)
    check_values(app, {"ckReportingEnable": False})
    check_enabled(app, ["txtCallsign", "txtGridSquare"], False)

    # Going back shouldn't lose anything.
    app.click("wizard.btnPrev")
    check_wizard_page(app, 2)
    app.click("wizard.btnPrev")
    check_wizard_page(app, 1)
    check_equal(app.get("wizard.cbMicIn")["data"], MICROPHONE_IN, "Microphone")
    app.click("wizard.btnNext")
    app.click("wizard.btnNext")
    check_wizard_page(app, 3)

    app.click("wizard.btnFinish")
    app.wait_for_window_closed("wizard")


TWO_SOUND_CARDS = {
    "soundCard1In": RADIO_RX_IN,
    "soundCard1Out": RADIO_TX,
    "soundCard2In": MICROPHONE_IN,
    "soundCard2Out": SPEAKER,
}


def start_and_stop_modem(app, sound_cards=2):
    """Presses Start, makes sure FreeDV runs without complaint, and presses
    Stop."""
    can_transmit = (sound_cards == 2)

    start_button = app.get("main.btnOnOff")
    check(start_button["enabled"], "Start button is disabled")
    check_equal(start_button["value"], False, "Start button pressed")
    check_equal(label(app, "main.btnOnOff"), "Start Modem", "Start button label")
    check_equal(app.get("main.btnPTT")["enabled"], False, "PTT button enabled before start")

    app.click("main.btnOnOff")

    # The button is disabled until FreeDV has finished starting (or failed to).
    app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to start")
    check_equal(app.message_boxes(), [], "Errors displayed on start")
    state = app.state()
    check(state["running"], "FreeDV is not running after pressing Start")
    check(state["modemRunning"], "The modem is not running after pressing Start")
    check_equal(state["numSoundCards"], sound_cards, "Number of sound cards in use")
    check_equal(state["transmitting"], False, "Transmitting")
    check_equal(app.get("main.btnOnOff")["value"], True, "Start button pressed")
    check_equal(label(app, "main.btnOnOff"), "Stop Modem", "Start button label")
    check_equal(app.get("main.btnPTT")["enabled"], can_transmit, "PTT button enabled while running")
    check_equal(app.get("main.btnVoiceKeyer")["enabled"], can_transmit, "Voice keyer button enabled while running")

    # The wizard can't be used while running.
    try:
        app.menu("Settings/Setup Wizard...")
        check(False, "The Setup Wizard can be opened while FreeDV is running")
    except freedvtest.CommandError:
        pass

    # Audio problems are reported as they happen, so give them a chance to.
    time.sleep(5)
    check_equal(app.message_boxes(), [], "Errors displayed while running")
    check(app.state()["running"], "FreeDV stopped running by itself")

    app.click("main.btnOnOff")
    app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to stop")
    check_equal(app.message_boxes(), [], "Errors displayed on stop")
    check_equal(app.state()["running"], False, "Running after pressing Stop")
    check_equal(app.get("main.btnOnOff")["value"], False, "Start button pressed")
    check_equal(label(app, "main.btnOnOff"), "Start Modem", "Start button label")
    check_equal(app.get("main.btnPTT")["enabled"], False, "PTT button enabled after stop")


# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------

def test_first_run():
    """The wizard appears by itself the first time FreeDV is run, and
    FreeDV starts with what's set up in it."""
    with VirtualAudioDevices(RADIO_RX, RADIO_TX, MICROPHONE, SPEAKER):
        # No configuration file is given, so this is FreeDV's first run.
        with freedvtest.start() as app:
            run_setup_wizard(app)
            check_audio_config(app, TWO_SOUND_CARDS)
            start_and_stop_modem(app)

            # The wizard should show what was chosen when it's run again.
            reopen_wizard(app)
            check_equal(app.get("wizard.cbRadioIn")["data"], RADIO_RX_IN, "Radio input")
            check_equal(app.get("wizard.cbSpeakerOut")["data"], SPEAKER, "Speaker")
            check_values(app, {"ckReceiveOnly": False})
            check_equal(app.get("wizard.cbMicIn")["data"], MICROPHONE_IN, "Microphone")
            check_equal(app.get("wizard.cbRadioOut")["data"], RADIO_TX, "Radio output")
            app.click("wizard.btnCancel")
            app.wait_for_window_closed("wizard")

            quit_and_check_log(app)


def test_second_run():
    """The wizard only appears by itself once, and what was set up in it is
    still there the next time FreeDV is run."""
    config_dir = tempfile.mkdtemp(prefix="freedvtest-config-")
    try:
        config = os.path.join(config_dir, "freedv.conf")
        with VirtualAudioDevices(RADIO_RX, RADIO_TX, MICROPHONE, SPEAKER):
            with freedvtest.start(config=config) as app:
                run_setup_wizard(app)
                quit_and_check_log(app)

            with freedvtest.start(config=config) as app:
                app.wait_for_window("main")
                time.sleep(3)
                check(not app.window_exists("wizard"), "The Setup Wizard appeared on FreeDV's second run")
                check_audio_config(app, TWO_SOUND_CARDS)
                start_and_stop_modem(app)
                quit_and_check_log(app)
    finally:
        shutil.rmtree(config_dir, ignore_errors=True)


def test_receive_only():
    """A station that only receives needs just a radio input and a speaker."""
    with VirtualAudioDevices(RADIO_RX, SPEAKER):
        with freedvtest.start() as app:
            app.wait_for_window("wizard")

            # The system's default output is suggested for the speaker.
            speaker = app.get("wizard.cbSpeakerOut")
            if any(item.get("data") == default_output() for item in speaker["items"]):
                check_equal(speaker["data"], default_output(), "Suggested speaker")

            app.set_value("wizard.cbRadioIn", data=RADIO_RX_IN)
            app.set_value("wizard.cbSpeakerOut", data=SPEAKER)
            go_to_page(app, 1)
            check_values(app, {"ckReceiveOnly": True})
            check_enabled(app, ["cbMicIn", "cbRadioOut"], False)
            finish_wizard(app)

            # With one sound card, the speaker is that card's output.
            receive_only = {
                "soundCard1In": RADIO_RX_IN,
                "soundCard1Out": SPEAKER,
                "soundCard2In": "none",
                "soundCard2Out": "none",
            }
            check_audio_config(app, receive_only)
            start_and_stop_modem(app, sound_cards=1)

            reopen_wizard(app)
            check_equal(app.get("wizard.cbRadioIn")["data"], RADIO_RX_IN, "Radio input")
            check_equal(app.get("wizard.cbSpeakerOut")["data"], SPEAKER, "Speaker")
            check_values(app, {"ckReceiveOnly": True})

            # Unchecking and rechecking Receive Only shouldn't leave transmit
            # devices behind.
            go_to_page(app, 1)
            app.set_value("wizard.ckReceiveOnly", False)
            check_enabled(app, ["cbMicIn", "cbRadioOut"], True)
            app.set_value("wizard.cbRadioOut", data=RADIO_RX)
            app.set_value("wizard.ckReceiveOnly", True)
            check_enabled(app, ["cbMicIn", "cbRadioOut"], False)
            finish_wizard(app)
            check_audio_config(app, receive_only)

            # Finishing with "none" chosen for the devices saves no rates.
            reopen_wizard(app)
            app.set_value("wizard.cbRadioIn", data="none")
            app.set_value("wizard.cbSpeakerOut", data="none")
            finish_wizard(app)
            audio = app.state()["config"]["audio"]
            for key in ["soundCard1In", "soundCard1Out", "soundCard2In", "soundCard2Out"]:
                check_equal(audio[key]["deviceName"], "none", key + " device")
            check_equal(audio["soundCard1In"]["sampleRate"], 48000, "soundCard1In sample rate")

            quit_and_check_log(app)


def test_odd_starting_points():
    """Opened on settings that it wouldn't have made itself: devices that
    have since gone, and only some of what transmitting needs."""
    def audio(radio_in, radio_out, microphone, speaker):
        settings = {}
        for card, device in [("soundCard1In", radio_in), ("soundCard1Out", radio_out),
                             ("soundCard2In", microphone), ("soundCard2Out", speaker)]:
            settings["/Audio/%sDeviceName" % card] = device
            settings["/Audio/%sSampleRate" % card] = 48000
        return settings

    def choose_everything_and_finish(app):
        go_to_page(app, 0)
        app.set_value("wizard.cbRadioIn", data=RADIO_RX_IN)
        app.set_value("wizard.cbSpeakerOut", data=SPEAKER)
        go_to_page(app, 1)
        app.set_value("wizard.ckReceiveOnly", False)
        app.set_value("wizard.cbMicIn", data=MICROPHONE_IN)
        app.set_value("wizard.cbRadioOut", data=RADIO_TX)
        finish_wizard(app)
        check_audio_config(app, TWO_SOUND_CARDS)

    gone = "FreeDVGuiTest_Gone"
    with VirtualAudioDevices(RADIO_RX, RADIO_TX, MICROPHONE, SPEAKER) as devices:
        # Every device has gone. Finishing without choosing others leaves
        # nothing that's there to be chosen in their place.
        with freedvtest.start(settings=audio(gone + ".monitor", gone, gone + "2.monitor", gone + "2")) as app:
            app.wait_for_window("main")
            reopen_wizard(app)
            go_to_page(app, 1)
            check_values(app, {"ckReceiveOnly": False})
            finish_wizard(app)
            check_equal(app.message_boxes(), [], "Messages displayed")
            reopen_wizard(app)
            choose_everything_and_finish(app)
            start_and_stop_modem(app)
            quit_and_check_log(app)

        # Receive only, without a speaker having been chosen: one's suggested.
        with freedvtest.start(settings=audio(RADIO_RX_IN, "none", "none", "none")) as app:
            app.wait_for_window("main")
            reopen_wizard(app)
            speaker = app.get("wizard.cbSpeakerOut")
            if any(item.get("data") == default_output() for item in speaker["items"]):
                check_equal(speaker["data"], default_output(), "Suggested speaker")
            check_equal(app.get("wizard.cbRadioIn")["data"], RADIO_RX_IN, "Radio input")
            go_to_page(app, 1)
            check_values(app, {"ckReceiveOnly": True})
            choose_everything_and_finish(app)
            quit_and_check_log(app)

        # A speaker but no microphone isn't receive only.
        with freedvtest.start(settings=audio(RADIO_RX_IN, RADIO_TX, "none", SPEAKER)) as app:
            app.wait_for_window("main")
            reopen_wizard(app)
            check_equal(app.get("wizard.cbSpeakerOut")["data"], SPEAKER, "Speaker")
            go_to_page(app, 1)
            check_values(app, {"ckReceiveOnly": False})
            check_equal(app.get("wizard.cbRadioOut")["data"], RADIO_TX, "Radio output")
            choose_everything_and_finish(app)
            start_and_stop_modem(app)
            quit_and_check_log(app)

        # The radio output is "none" while the rest of transmitting is set up.
        with freedvtest.start(settings=audio(RADIO_RX_IN, "none", MICROPHONE_IN, SPEAKER)) as app:
            app.wait_for_window("main")
            reopen_wizard(app)
            check_equal(app.get("wizard.cbRadioOut")["data"], "none", "Radio output")
            go_to_page(app, 1)
            check_values(app, {"ckReceiveOnly": False})
            choose_everything_and_finish(app)
            start_and_stop_modem(app)
            quit_and_check_log(app)

        # ...or saved as nothing at all.
        with freedvtest.start(settings=audio(RADIO_RX_IN, "", MICROPHONE_IN, SPEAKER)) as app:
            app.wait_for_window("main")
            reopen_wizard(app)
            go_to_page(app, 1)
            check_values(app, {"ckReceiveOnly": False})
            choose_everything_and_finish(app)
            quit_and_check_log(app)

        # A radio output unplugged while the wizard is open keeps its selection.
        with freedvtest.start(settings=audio(RADIO_RX_IN, RADIO_TX, MICROPHONE_IN, SPEAKER)) as app:
            app.wait_for_window("main")
            reopen_wizard(app)
            go_to_page(app, 1)
            app.set_value("wizard.cbRadioOut", data=RADIO_TX)
            devices.remove(RADIO_TX)
            finish_wizard(app)
            check_equal(app.state()["config"]["audio"]["soundCard1Out"]["deviceName"], RADIO_TX,
                        "soundCard1Out device")
            check_equal(app.state()["config"]["audio"]["soundCard1Out"]["sampleRate"], 48000,
                        "soundCard1Out sample rate")
            quit_and_check_log(app)


def test_cancel():
    """Leaving the wizard without finishing it changes nothing."""
    unconfigured = {
        "soundCard1In": "none",
        "soundCard1Out": "none",
        "soundCard2In": "none",
        "soundCard2Out": "none",
    }

    def change_everything(app):
        app.set_value("wizard.cbRadioIn", data=RADIO_RX_IN)
        app.set_value("wizard.cbSpeakerOut", data=SPEAKER)
        go_to_page(app, 1)
        app.set_value("wizard.ckReceiveOnly", False)
        app.set_value("wizard.cbMicIn", data=MICROPHONE_IN)
        app.set_value("wizard.cbRadioOut", data=RADIO_TX)
        go_to_page(app, 2)
        app.set_value("wizard.ckSerialPTT", True)
        app.set_value("wizard.cbCtlDevicePath", "/dev/ttyFreeDVTest0")
        go_to_page(app, 3)
        app.set_value("wizard.ckReportingEnable", True)
        app.set_value("wizard.txtCallsign", "ZZ0ZZZ")
        app.set_value("wizard.txtGridSquare", "AA00")

    def check_unchanged(app):
        audio = app.state()["config"]["audio"]
        for key, device in unconfigured.items():
            check_equal(audio[key]["deviceName"], device, key + " device")
        check_config(app, "rigControl", {"useSerialPTT": False, "serialPTTPort": ""})
        check_config(app, "reporting", {
            "reportingEnabled": False, "reportingCallsign": "", "reportingGridSquare": ""})
        if os.path.exists(app.config_path):
            with open(app.config_path, "r") as f:
                saved = f.read()
            check("FreeDVGuiTest" not in saved, "Cancelled choices were saved to %s" % app.config_path)

    with VirtualAudioDevices(RADIO_RX, RADIO_TX, MICROPHONE, SPEAKER):
        # The settings home is set but empty, so the default one is used instead.
        with freedvtest.start(env={"XDG_CONFIG_HOME": ""}) as app:
            app.wait_for_window("wizard")

            # No other programs' settings exist, so there's nothing to import.
            names = [widget["name"] for widget in app.widgets("wizard")]
            check("wizard.btnImport" not in names, "Import is offered with nothing to import from")

            # With the Cancel button...
            change_everything(app)
            app.click("wizard.btnCancel")
            app.wait_for_window_closed("wizard")
            check_unchanged(app)

            # ...and with the window's close button.
            reopen_wizard(app)
            check(app.get("wizard.cbRadioIn")["data"] != RADIO_RX_IN, "Cancelled radio input is still selected")
            check_values(app, {"ckReceiveOnly": True, "ckSerialPTT": False, "ckReportingEnable": False,
                               "txtCallsign": "", "txtGridSquare": ""})
            change_everything(app)
            app.close("wizard")
            app.wait_for_window_closed("wizard")
            check_unchanged(app)

            quit_and_check_log(app)


def test_auto_select():
    """Sound devices belonging to a radio are picked out automatically."""
    codec_in = "FreeDVGuiTest_Codec_In"
    codec_out = "FreeDVGuiTest_Codec_Out"
    with VirtualAudioDevices(
            SPEAKER,
            inputs={codec_in: "USB Audio CODEC"},
            outputs={codec_out: "USB Audio CODEC"}):
        with freedvtest.start() as app:
            app.wait_for_window("wizard")

            # The output's monitor has the same name, but isn't the radio.
            check_equal(app.get("wizard.cbRadioIn")["data"], codec_in, "Radio input")
            check_equal(app.get("wizard.cbRadioOut")["data"], codec_out, "Radio output")
            check_values(app, {"ckReceiveOnly": False})

            app.set_value("wizard.cbSpeakerOut", data=SPEAKER)
            go_to_page(app, 1)
            app.set_value("wizard.cbMicIn", data=VirtualAudioDevices.input_name(SPEAKER))
            finish_wizard(app)
            check_audio_config(app, {
                "soundCard1In": codec_in,
                "soundCard1Out": codec_out,
                "soundCard2In": VirtualAudioDevices.input_name(SPEAKER),
                "soundCard2Out": SPEAKER,
            })

            # Once a radio's been chosen, that choice is left alone.
            reopen_wizard(app)
            app.set_value("wizard.cbRadioIn", data=VirtualAudioDevices.input_name(SPEAKER))
            finish_wizard(app)
            reopen_wizard(app)
            check_equal(
                app.get("wizard.cbRadioIn")["data"], VirtualAudioDevices.input_name(SPEAKER), "Radio input")
            app.click("wizard.btnCancel")
            app.wait_for_window_closed("wizard")

            quit_and_check_log(app)

    # FlexRadio's DAX has several receive channels and IQ streams, which
    # aren't necessarily listed in order.
    dax_iq = "FreeDVGuiTest_Dax_Iq"
    dax_rx2 = "FreeDVGuiTest_Dax_Second"
    dax_rx1 = "FreeDVGuiTest_Dax_First"
    # A receive channel without a number, and a device that isn't a receiver.
    dax_left = "FreeDVGuiTest_Dax_Left"
    dax_side = "FreeDVGuiTest_Dax_Side"
    dax_tx = "FreeDVGuiTest_Dax_Transmit"
    with VirtualAudioDevices(
            inputs={dax_iq: "DAX IQ RX 1", dax_rx2: "DAX Audio RX 2", dax_rx1: "DAX Audio RX 1",
                    dax_left: "DAX Audio RX Left", dax_side: "DAX Side"},
            outputs={dax_tx: "DAX Audio TX (FlexRadio Systems DAX TX)"}):
        with freedvtest.start() as app:
            app.wait_for_window("wizard")
            check_equal(app.get("wizard.cbRadioIn")["data"], dax_rx1, "Radio input")
            check_equal(app.get("wizard.cbRadioOut")["data"], dax_tx, "Radio output")
            check_values(app, {"ckReceiveOnly": False})
            app.click("wizard.btnCancel")
            app.wait_for_window_closed("wizard")
            quit_and_check_log(app)

    # A known radio input with no matching output leaves nothing chosen.
    codec_alone = "FreeDVGuiTest_Codec_Alone"
    with VirtualAudioDevices(inputs={codec_alone: "USB Audio CODEC"}):
        with freedvtest.start() as app:
            app.wait_for_window("wizard")
            check_equal(app.get("wizard.cbRadioIn")["data"], "none", "Radio input")
            app.click("wizard.btnCancel")
            app.wait_for_window_closed("wizard")
            quit_and_check_log(app)


def test_radio_control():
    """The Radio Control page: CAT control via Hamlib and serial port PTT."""
    with freedvtest.start() as app:
        app.wait_for_window("wizard")
        go_to_page(app, 2)
        rig = find_rig(app, "Icom IC-7300")
        other_rig = find_rig(app, "Yaesu FT-991")

        # Nothing's usable until the way of keying the radio is chosen.
        check_values(app, {"ckHamlib": False, "ckSerialPTT": False})
        check_enabled(app, HAMLIB_CONTROLS + ["cbPttSerialPort"] + SERIAL_PTT_CONTROLS, False)
        check_enabled(app, ["ckUseAnalogModes"], False)

        app.set_value("wizard.ckHamlib", True)
        check_enabled(app, HAMLIB_CONTROLS + ["ckUseAnalogModes"], True)
        check_enabled(app, SERIAL_PTT_CONTROLS, False)

        # A separate serial device for PTT only applies to RTS and DTR.
        for method, needs_port in [("CAT", False), ("RTS", True), ("None", False), ("DTR", True),
                                   ("CAT via Data port", False), ("RTS", True)]:
            app.set_value("wizard.cbPttMethod", method)
            check_equal(app.get("wizard.cbPttSerialPort")["enabled"], needs_port,
                        "PTT serial device enabled for %s" % method)

        # Only the serial rates that the chosen rig supports are offered.
        def rates():
            return [item["label"] for item in app.get("wizard.cbSerialRate")["items"]]

        all_rates = rates()
        check_values(app, {"cbSerialRate": "default"})
        app.set_value("wizard.cbRigName", rig)
        rig_rates = rates()
        check_equal(rig_rates[0], "default", "First serial rate")
        check("19200" in rig_rates, "%s cannot use 19200 baud" % rig)
        check(len(rig_rates) < len(all_rates), "Serial rates weren't narrowed down for %s" % rig)
        check_equal([int(rate) for rate in rig_rates[1:]], sorted(int(rate) for rate in rig_rates[1:]),
                    "Serial rates")
        check_values(app, {"cbSerialRate": "default"})

        # The chosen rate survives a change of rig if the new rig has it too.
        app.set_value("wizard.cbSerialRate", "19200")
        app.set_value("wizard.cbRigName", other_rig)
        check("19200" in rates(), "%s cannot use 19200 baud" % other_rig)
        check_values(app, {"cbSerialRate": "19200"})
        app.set_value("wizard.cbRigName", rig)
        check_values(app, {"cbSerialRate": "19200"})

        # A rate the new rig can't use falls back to the default.
        app.set_value("wizard.cbSerialRate", "12345")
        app.set_value("wizard.cbRigName", other_rig)
        check_values(app, {"cbSerialRate": "default"})
        app.set_value("wizard.cbSerialRate", "19200")
        app.set_value("wizard.cbRigName", rig)
        check_values(app, {"cbSerialRate": "19200"})

        # Rates outside the rig's range, slow or fast, aren't offered either.
        low_rig = find_rig(app, "Yaesu FT-817")
        app.set_value("wizard.cbRigName", low_rig)
        low_rates = rates()
        check("19200" in low_rates, "19200 not offered for %s" % low_rig)
        check("38400" in low_rates, "38400 not offered for %s" % low_rig)
        check("115200" not in low_rates, "Too fast rate offered for %s" % low_rig)
        check("2400" not in low_rates, "Too slow rate offered for %s" % low_rig)
        app.set_value("wizard.cbRigName", rig)
        check_values(app, {"cbSerialRate": "19200"})

        # Serial devices can be typed in as well as chosen.
        app.set_value("wizard.cbSerialPort", "/dev/ttyFreeDVTest0")
        app.set_value("wizard.cbPttSerialPort", "/dev/ttyFreeDVTest1")
        app.set_value("wizard.ckUseAnalogModes", True)

        # Hamlib and serial port PTT can't both be used.
        app.set_value("wizard.ckSerialPTT", True)
        check_values(app, {"ckHamlib": False})
        check_enabled(app, HAMLIB_CONTROLS + ["cbPttSerialPort", "ckUseAnalogModes"], False)
        check_enabled(app, SERIAL_PTT_CONTROLS, True)
        app.click("wizard.rbUseDTR")
        check_values(app, {"rbUseDTR": True, "rbUseRTS": False})
        app.click("wizard.rbUseRTS")
        check_values(app, {"rbUseDTR": False, "rbUseRTS": True})
        app.set_value("wizard.ckHamlib", True)
        check_values(app, {"ckSerialPTT": False})
        check_enabled(app, HAMLIB_CONTROLS + ["cbPttSerialPort", "ckUseAnalogModes"], True)
        check_enabled(app, SERIAL_PTT_CONTROLS, False)

        # What was entered for Hamlib is still there.
        hamlib_values = {
            "ckHamlib": True,
            "cbRigName": rig,
            "cbSerialPort": "/dev/ttyFreeDVTest0",
            "cbSerialRate": "19200",
            "cbPttMethod": "RTS",
            "cbPttSerialPort": "/dev/ttyFreeDVTest1",
            "ckUseAnalogModes": True,
            "ckSerialPTT": False,
        }
        check_values(app, hamlib_values)
        finish_wizard(app)
        check_config(app, "rigControl", {
            "hamlibUseForPTT": True,
            "hamlibRigName": rig,
            "hamlibSerialPort": "/dev/ttyFreeDVTest0",
            "hamlibSerialRate": 19200,
            "hamlibPTTType": PTT_VIA_RTS,
            "hamlibPttSerialPort": "/dev/ttyFreeDVTest1",
            "hamlibUseAnalogModes": True,
            "useSerialPTT": False,
        })

        reopen_wizard(app)
        check_values(app, hamlib_values)
        go_to_page(app, 2)
        check_enabled(app, HAMLIB_CONTROLS + ["cbPttSerialPort", "ckUseAnalogModes"], True)

        # The default serial rate is saved as no rate at all.
        app.set_value("wizard.cbSerialRate", "default")
        app.set_value("wizard.cbPttMethod", "CAT")
        finish_wizard(app)
        check_config(app, "rigControl", {"hamlibSerialRate": 0, "hamlibPTTType": PTT_VIA_CAT})

        # Now serial port PTT instead.
        reopen_wizard(app)
        go_to_page(app, 2)
        check_values(app, {"cbSerialRate": "default", "cbPttMethod": "CAT"})
        app.set_value("wizard.ckSerialPTT", True)
        app.set_value("wizard.cbCtlDevicePath", "/dev/ttyFreeDVTest2")
        app.click("wizard.rbUseDTR")
        app.set_value("wizard.ckRTSPos", False)
        app.set_value("wizard.ckDTRPos", True)
        serial_values = {
            "ckHamlib": False,
            "ckSerialPTT": True,
            "cbCtlDevicePath": "/dev/ttyFreeDVTest2",
            "rbUseRTS": False,
            "ckRTSPos": False,
            "rbUseDTR": True,
            "ckDTRPos": True,
        }
        check_values(app, serial_values)
        finish_wizard(app)
        check_config(app, "rigControl", {
            "hamlibUseForPTT": False,
            "useSerialPTT": True,
            "serialPTTPort": "/dev/ttyFreeDVTest2",
            "serialPTTUseRTS": False,
            "serialPTTPolarityRTS": False,
            "serialPTTUseDTR": True,
            "serialPTTPolarityDTR": True,
        })

        reopen_wizard(app)
        check_values(app, serial_values)

        go_to_page(app, 2)
        # Turning either way of keying the radio off disables its controls again.
        app.set_value("wizard.ckHamlib", True)
        check_values(app, {"ckHamlib": True, "ckSerialPTT": False})
        app.set_value("wizard.ckHamlib", False)
        check_enabled(app, HAMLIB_CONTROLS + ["cbPttSerialPort", "ckUseAnalogModes"], False)
        app.set_value("wizard.ckSerialPTT", True)
        check_enabled(app, SERIAL_PTT_CONTROLS, True)
        app.set_value("wizard.ckSerialPTT", False)
        check_enabled(app, SERIAL_PTT_CONTROLS, False)
        app.click("wizard.btnCancel")
        app.wait_for_window_closed("wizard")

        quit_and_check_log(app)


def test_hamlib_start():
    """FreeDV can control a radio that was set up in the wizard. Hamlib's
    "Dummy" rig stands in for the radio."""
    with VirtualAudioDevices(RADIO_RX, RADIO_TX, MICROPHONE, SPEAKER):
        with freedvtest.start() as app:
            app.wait_for_window("wizard")
            app.set_value("wizard.cbRadioIn", data=RADIO_RX_IN)
            app.set_value("wizard.cbSpeakerOut", data=SPEAKER)
            go_to_page(app, 1)
            app.set_value("wizard.ckReceiveOnly", False)
            app.set_value("wizard.cbMicIn", data=MICROPHONE_IN)
            app.set_value("wizard.cbRadioOut", data=RADIO_TX)
            go_to_page(app, 2)
            app.set_value("wizard.ckHamlib", True)
            app.set_value("wizard.cbRigName", find_rig(app, "Hamlib Dummy"))
            app.set_value("wizard.cbPttMethod", "CAT")
            finish_wizard(app)
            check_config(app, "rigControl", {"hamlibUseForPTT": True, "hamlibRigName": "Hamlib Dummy"})

            start_and_stop_modem(app)
            quit_and_check_log(app)


def test_reporting():
    """The Reporting page. FreeDV isn't started, so nothing is reported."""
    with freedvtest.start() as app:
        app.wait_for_window("wizard")
        go_to_page(app, 3)
        check_values(app, {"ckReportingEnable": False, "txtCallsign": "", "txtGridSquare": ""})
        check_enabled(app, ["txtCallsign", "txtGridSquare"], False)

        app.set_value("wizard.ckReportingEnable", True)
        check_enabled(app, ["txtCallsign", "txtGridSquare"], True)
        app.set_value("wizard.txtCallsign", "ZZ0ZZZ")
        app.set_value("wizard.txtGridSquare", "AA00")
        app.set_value("wizard.ckReportingEnable", False)
        check_enabled(app, ["txtCallsign", "txtGridSquare"], False)
        app.set_value("wizard.ckReportingEnable", True)
        check_values(app, {"txtCallsign": "ZZ0ZZZ", "txtGridSquare": "AA00"})
        finish_wizard(app)

        enabled = {
            "reportingEnabled": True,
            "reportingCallsign": "ZZ0ZZZ",
            "reportingGridSquare": "AA00",
            "freedvReporterEnabled": True,
            "pskReporterEnabled": True,
        }
        check_config(app, "reporting", enabled)

        # Turning reporting off keeps the callsign and grid square.
        reopen_wizard(app)
        check_values(app, {"ckReportingEnable": True, "txtCallsign": "ZZ0ZZZ", "txtGridSquare": "AA00"})
        go_to_page(app, 3)
        app.set_value("wizard.ckReportingEnable", False)
        finish_wizard(app)
        check_config(app, "reporting", dict(
            enabled, reportingEnabled=False, freedvReporterEnabled=False, pskReporterEnabled=False))

        reopen_wizard(app)
        check_values(app, {"ckReportingEnable": False, "txtCallsign": "ZZ0ZZZ", "txtGridSquare": "AA00"})
        app.click("wizard.btnCancel")
        app.wait_for_window_closed("wizard")

        quit_and_check_log(app)


def wsjtx_settings(**settings):
    """Returns the contents of a WSJT-X (or similar) settings file."""
    lines = ["[Configuration]"] + ["%s=%s" % item for item in settings.items()]
    return "\n".join(lines) + "\n"


def import_status(app):
    """Returns the result of an import as displayed, less any line breaks
    added to fit it in."""
    return " ".join(app.get("wizard.stImportStatus")["label"].split())


def test_import():
    """Settings can be brought in from WSJT-X and programs derived from it."""
    with VirtualAudioDevices(RADIO_RX, RADIO_TX, SPEAKER):
        user_config_files = {
            # Nothing in here can be used.
            "JTDX.ini": wsjtx_settings(
                Rig="Ham Radio Deluxe",
                SoundInName="FreeDVGuiTest Nonexistent Device"),
            "WSJT-X.ini": "",
        }
        with freedvtest.start(user_config_files=user_config_files) as app:
            def write_wsjtx_settings(**settings):
                with open(app.user_config_path("WSJT-X.ini"), "w") as f:
                    f.write(wsjtx_settings(**settings))

            app.wait_for_window("wizard")
            rig = find_rig(app, "Icom IC-7300")
            network_rig = find_rig(app, "Hamlib NET rigctl")

            sources = app.get("wizard.chImportSource")
            check_equal([item["label"] for item in sources["items"]], ["WSJT-X", "JTDX"], "Programs to import from")
            check_equal(sources["value"], "WSJT-X", "Program to import from")
            check_equal(import_status(app), "", "Import result before importing")

            # A program whose settings are of no use.
            app.set_value("wizard.chImportSource", "JTDX")
            app.click("wizard.btnImport")
            check_equal(
                import_status(app),
                "No usable settings were found in JTDX. "
                "Not imported: radio audio devices (not currently connected), "
                "rig control (Ham Radio Deluxe is not supported by FreeDV). "
                "Review each page before clicking Finish.",
                "Import result")
            check_values(app, {"ckReceiveOnly": True, "ckHamlib": False, "ckSerialPTT": False,
                               "ckReportingEnable": False, "txtCallsign": ""})

            # A radio controlled over a serial port, with PTT on another.
            # Devices are matched by either name, whatever their case, and
            # values are written by Qt, which quotes some and wraps others.
            write_wsjtx_settings(
                MyCall='"ZZ0ZZZ"',
                MyGrid="AA00",
                PSKReporter="true",
                SoundInName=VirtualAudioDevices.input_label(RADIO_RX),
                SoundOutName=RADIO_TX.upper(),
                Rig=rig,
                CATSerialPort="/dev/ttyFreeDVTest0",
                CATSerialRate="19200",
                PTTMethod=r"@Variant(\0\0\0\x7f\0\0\0\x1eTransceiverFactory::PTTMethod\0\0\0\0\x11PTT_method_RTS\0)",
                PTTport="/dev/ttyFreeDVTest1",
                DataMode=r"@Variant(\0\0\0\x7f\0\0\0\x1dTransceiverFactory::DataMode\0\0\0\0\x0edata_mode_USB\0)")
            app.set_value("wizard.chImportSource", "WSJT-X")
            app.click("wizard.btnImport")
            check_equal(
                import_status(app),
                "Imported: radio audio devices, Hamlib rig control (%s), callsign, grid square, "
                "reporting enabled. Review each page before clicking Finish." % rig,
                "Import result")
            check_equal(app.get("wizard.cbRadioIn")["data"], RADIO_RX_IN, "Radio input")
            check_equal(app.get("wizard.cbRadioOut")["data"], RADIO_TX, "Radio output")
            check_values(app, {
                "ckReceiveOnly": False,
                "ckHamlib": True,
                "cbRigName": rig,
                "cbSerialPort": "/dev/ttyFreeDVTest0",
                "cbSerialRate": "19200",
                "cbPttMethod": "RTS",
                "cbPttSerialPort": "/dev/ttyFreeDVTest1",
                "ckUseAnalogModes": True,
                "ckSerialPTT": False,
                "ckReportingEnable": True,
                "txtCallsign": "ZZ0ZZZ",
                "txtGridSquare": "AA00",
            })
            check_enabled(app, HAMLIB_CONTROLS + ["cbPttSerialPort", "ckUseAnalogModes", "cbMicIn", "cbRadioOut",
                                                  "txtCallsign", "txtGridSquare"], True)
            check_enabled(app, SERIAL_PTT_CONTROLS, False)

            # A radio controlled over the network, keyed by CAT with its
            # audio going in through the data port. WSJT-X leaves the address
            # blank to mean Hamlib's default.
            write_wsjtx_settings(
                Rig=network_rig,
                CATNetworkPort="",
                CATSerialPort="/dev/ttyFreeDVTest0",
                CATSerialRate="19200",
                PTTMethod="PTT_method_CAT",
                PTTport="CAT",
                TXAudioSource="TX_audio_source_rear",
                DataMode="data_mode_data")
            app.click("wizard.btnImport")
            check_equal(
                import_status(app),
                "Imported: Hamlib rig control (%s). Review each page before clicking Finish." % network_rig,
                "Import result")
            check_values(app, {
                "ckHamlib": True,
                "cbRigName": network_rig,
                "cbSerialRate": "default",
                "cbPttMethod": "CAT via Data port",
                "cbPttSerialPort": "",
                "ckUseAnalogModes": False,
            })
            network_address = app.get("wizard.cbSerialPort")["value"]
            check(network_address not in ("", "/dev/ttyFreeDVTest0"),
                  "Network rig's address is '%s'" % network_address)
            check_enabled(app, ["cbPttSerialPort"], False)

            # No CAT control, just PTT on a serial port.
            write_wsjtx_settings(
                Rig="None",
                PTTMethod="PTT_method_DTR",
                PTTport="/dev/ttyFreeDVTest2")
            app.click("wizard.btnImport")
            check_equal(
                import_status(app),
                "Imported: serial port PTT (/dev/ttyFreeDVTest2). Review each page before clicking Finish.",
                "Import result")
            check_values(app, {
                "ckHamlib": False,
                "ckSerialPTT": True,
                "cbCtlDevicePath": "/dev/ttyFreeDVTest2",
                "rbUseRTS": False,
                "rbUseDTR": True,
                "ckRTSPos": False,
                "ckDTRPos": False,
            })
            check_enabled(app, SERIAL_PTT_CONTROLS, True)
            check_enabled(app, HAMLIB_CONTROLS + ["cbPttSerialPort", "ckUseAnalogModes"], False)

            # Importing only fills in the wizard; nothing's applied until
            # it's finished.
            check_config(app, "rigControl", {"useSerialPTT": False, "hamlibUseForPTT": False})
            check_config(app, "reporting", {"reportingEnabled": False, "reportingCallsign": ""})

            go_to_page(app, 1)
            app.set_value("wizard.cbMicIn", data=VirtualAudioDevices.input_name(SPEAKER))
            go_to_page(app, 0)
            app.set_value("wizard.cbSpeakerOut", data=SPEAKER)
            finish_wizard(app)
            check_audio_config(app, {
                "soundCard1In": RADIO_RX_IN,
                "soundCard1Out": RADIO_TX,
                "soundCard2In": VirtualAudioDevices.input_name(SPEAKER),
                "soundCard2Out": SPEAKER,
            })
            check_config(app, "rigControl", {
                "hamlibUseForPTT": False,
                "useSerialPTT": True,
                "serialPTTPort": "/dev/ttyFreeDVTest2",
                "serialPTTUseRTS": False,
                "serialPTTUseDTR": True,
            })
            check_config(app, "reporting", {
                "reportingEnabled": True, "reportingCallsign": "ZZ0ZZZ", "reportingGridSquare": "AA00"})

            quit_and_check_log(app)


def test_import_more():
    """The less usual things that WSJT-X's settings can say."""
    with VirtualAudioDevices(RADIO_RX, RADIO_TX, SPEAKER):
        with freedvtest.start(user_config_files={"WSJT-X.ini": ""}) as app:
            def import_wsjtx(**settings):
                with open(app.user_config_path("WSJT-X.ini"), "w") as f:
                    f.write(wsjtx_settings(**settings))
                app.click("wizard.btnImport")
                return import_status(app)

            app.wait_for_window("wizard")
            rig = find_rig(app, "Icom IC-7300")
            rigs = [item["label"].strip() for item in app.get("wizard.cbRigName")["items"]]

            # Device names cut short (as Qt once did on Windows) still find
            # their devices...
            radio_in_shown = [item["label"] for item in app.get("wizard.cbRadioIn")["items"]
                              if item.get("data") == RADIO_RX_IN][0]
            # (Not with the names of macOS's loopback devices, which are too
            # short for this: see freedvtest.audio.device().)
            long_names = len(radio_in_shown) > 24 and len(RADIO_TX) > 24
            check(long_names or freedvtest.audio.MACOS,
                  "Device names are too short for this: %r, %r" % (radio_in_shown, RADIO_TX))
            if long_names:
                check_equal(import_wsjtx(SoundInName=radio_in_shown[:24], SoundOutName=RADIO_TX[:24]),
                            "Imported: radio audio devices. Review each page before clicking Finish.",
                            "Import result")
                check_equal(app.get("wizard.cbRadioIn")["data"], RADIO_RX_IN, "Radio input")
                check_equal(app.get("wizard.cbRadioOut")["data"], RADIO_TX, "Radio output")

                # ...unless there's too little left to go by.
                check_equal(import_wsjtx(SoundInName=radio_in_shown[:7], SoundOutName=RADIO_TX[:7]),
                            "No usable settings were found in WSJT-X. Not imported: radio audio devices (not "
                            "currently connected). Review each page before clicking Finish.", "Import result")

            # Only one of the two found: that much is imported, and the rest
            # is said to be missing.
            check_equal(import_wsjtx(SoundInName=radio_in_shown, SoundOutName="FreeDVGuiTest Nonexistent Device"),
                        "Imported: radio audio devices. Not imported: radio audio devices (not currently "
                        "connected). Review each page before clicking Finish.", "Import result")

            # A radio keyed by DTR on its own port, with no word on modes.
            check_equal(import_wsjtx(Rig=rig, CATSerialPort="/dev/ttyFreeDVTest0", PTTMethod="PTT_method_DTR",
                                     PTTport="/dev/ttyFreeDVTest1"),
                        "Imported: Hamlib rig control (%s). Review each page before clicking Finish." % rig,
                        "Import result")
            check_values(app, {"ckHamlib": True, "cbRigName": rig, "cbSerialPort": "/dev/ttyFreeDVTest0",
                               "cbSerialRate": "default", "cbPttMethod": "DTR",
                               "cbPttSerialPort": "/dev/ttyFreeDVTest1"})

            # Keyed by DTR on the port it's controlled through.
            import_wsjtx(Rig=rig, CATSerialPort="/dev/ttyFreeDVTest0", CATSerialRate="9600",
                         PTTMethod="PTT_method_DTR", PTTport="/dev/ttyFreeDVTest0")
            check_values(app, {"cbSerialRate": "9600", "cbPttMethod": "DTR", "cbPttSerialPort": ""})

            # Keyed by its own VOX: FreeDV has nothing to do.
            import_wsjtx(Rig=rig, CATSerialPort="/dev/ttyFreeDVTest0", PTTMethod="PTT_method_VOX", PTTport="CAT")
            check_values(app, {"ckHamlib": True, "cbPttMethod": "None", "cbPttSerialPort": ""})

            # A network rig with an address of its own.
            network_rig = find_rig(app, "Hamlib NET rigctl")
            check_equal(import_wsjtx(Rig=network_rig, CATNetworkPort="192.168.1.5:4532"),
                        "Imported: Hamlib rig control (%s). Review each page before clicking Finish." % network_rig,
                        "Import result")
            check_values(app, {"cbRigName": network_rig, "cbSerialPort": "192.168.1.5:4532",
                               "cbSerialRate": "default"})

            # A radio that plugs straight into USB has a setting of its own
            # for where it is.
            usb_rigs = [name for name in rigs if "FUNcube Dongle" in name or "Si570 AVR-USB" in name]
            if usb_rigs:
                check_equal(import_wsjtx(Rig=usb_rigs[0], CATUSBPort="1-2.3", CATSerialPort="/dev/ttyFreeDVTest0",
                                         CATSerialRate="19200"),
                            "Imported: Hamlib rig control (%s). Review each page before clicking Finish."
                            % usb_rigs[0], "Import result")
                check_values(app, {"ckHamlib": True, "cbRigName": usb_rigs[0], "cbSerialPort": "1-2.3",
                                   "cbSerialRate": "default"})

            # No radio to control, keyed by RTS.
            check_equal(import_wsjtx(PTTMethod="PTT_method_RTS", PTTport="/dev/ttyFreeDVTest2"),
                        "Imported: serial port PTT (/dev/ttyFreeDVTest2). Review each page before clicking Finish.",
                        "Import result")
            check_values(app, {"ckHamlib": False, "ckSerialPTT": True, "cbCtlDevicePath": "/dev/ttyFreeDVTest2",
                               "rbUseRTS": True, "rbUseDTR": False})

            # No radio to control and nothing to key it with either.
            for nothing in [dict(Rig="None", PTTMethod="PTT_method_CAT", PTTport="CAT"),
                            dict(Rig="None", PTTMethod="PTT_method_DTR", PTTport=""),
                            dict(MyCall="", MyGrid="", PSKReporter="false")]:
                check_equal(import_wsjtx(**nothing),
                            "No usable settings were found in WSJT-X. Review each page before clicking Finish.",
                            "Import result for %r" % nothing)

            # Who and where, without the rest.
            check_equal(import_wsjtx(MyCall="ZZ0ZZZ", PSKReporter="TRUE"),
                        "Imported: callsign, reporting enabled. Review each page before clicking Finish.",
                        "Import result")
            check_equal(import_wsjtx(MyGrid="AA00"),
                        "Imported: grid square. Review each page before clicking Finish.", "Import result")

            # A name quoted at one end only isn't taken as quoted.
            check_equal(import_wsjtx(SoundInName='"FreeDVGuiTest Radio'),
                        "No usable settings were found in WSJT-X. Not imported: radio audio devices (not "
                        "currently connected). Review each page before clicking Finish.", "Import result")

            # The transmit device on its own is worth importing.
            check_equal(import_wsjtx(SoundOutName=RADIO_TX),
                        "Imported: radio audio devices. Review each page before clicking Finish.",
                        "Import result")
            check_equal(app.get("wizard.cbRadioOut")["data"], RADIO_TX, "Radio output")

            app.click("wizard.btnCancel")
            app.wait_for_window_closed("wizard")
            quit_and_check_log(app)


SCENARIOS = {
    "first_run": test_first_run,
    "second_run": test_second_run,
    "receive_only": test_receive_only,
    "odd_starting_points": test_odd_starting_points,
    "cancel": test_cancel,
    "auto_select": test_auto_select,
    "radio_control": test_radio_control,
    "hamlib_start": test_hamlib_start,
    "reporting": test_reporting,
    "import": test_import,
    "import_more": test_import_more,
}


def main():
    names = sys.argv[1:] or list(SCENARIOS)
    for name in names:
        if name not in SCENARIOS:
            sys.exit("Unknown scenario '%s'. Scenarios: %s" % (name, ", ".join(SCENARIOS)))
    for name in names:
        print("Running %s..." % name, flush=True)
        try:
            SCENARIOS[name]()
        except freedvtest.audio.Unavailable as e:
            # What it needs of the audio devices can't be had here (see freedvtest.audio).
            print("SKIPPED: %s" % e, flush=True)
            sys.exit(77)
    print("PASS")


if __name__ == "__main__":
    main()
