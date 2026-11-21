#!/usr/bin/env python3
"""Tests what FreeDV does when its audio devices aren't as it expects:
missing, unusable, not set up at all, or changed while it's running.

Each scenario below launches its own copy (or copies) of FreeDV. With no
arguments all of them are run; otherwise, only those named:

    python3 test_audio_errors.py [scenario...]

Run from the build folder (or set FREEDV_BINARY) with a window manager
running, on Linux or macOS: see freedvtest/audio.py for the audio devices it
needs. Those to do with the sound server itself are skipped on macOS.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import freedvtest
import freedvtest.audio
from freedvtest.audio import (AudioServerGateway, PrivateAudioServer, Tone, VirtualAudioDevices, device, move_stream,
                              streams)
from test_main_window import (AUDIBLE, MICROPHONE, RADIO_RX, RADIO_TX, RADIO_TX_MONITOR, SPEAKER, SPEAKER_MONITOR,
                              audio_devices, check, check_equal, check_silent, level, quit_and_check_log, settings,
                              start_modem, stop_modem, wait_for_transmit)

# FreeDV's four devices, as its settings name them, with what each is set to.
CARDS = {
    "soundCard1In": VirtualAudioDevices.input_name(RADIO_RX),
    "soundCard1Out": RADIO_TX,
    "soundCard2In": VirtualAudioDevices.input_name(MICROPHONE),
    "soundCard2Out": SPEAKER,
}

SKIPPED = 77

OTHER_OUTPUT = device("FreeDVGuiTest_Other_Output")
OTHER_INPUT_DEVICE = device("FreeDVGuiTest_Other_Input")
OTHER_INPUT = VirtualAudioDevices.input_name(OTHER_INPUT_DEVICE)


def card_settings(**changes):
    """Returns settings() with something different for some of the cards:
    each of changes is a card's name and what to set its device to, or
    (as <card>_rate) its sample rate."""
    extra = {}
    for key, value in changes.items():
        if key.endswith("_rate"):
            extra["Audio__%sSampleRate" % key[:-5]] = value
        else:
            extra["Audio__%sDeviceName" % key] = value
    return settings(**extra)


def try_to_start(app):
    """Presses Start and waits for FreeDV to have made its mind up."""
    app.click("main.btnOnOff")
    app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to finish starting")
    time.sleep(0.5)


def check_not_started(app, what):
    check_equal(app.state()["running"], False, "Running " + what)
    check_equal(app.get("main.btnOnOff")["value"], False, "Start button pressed " + what)
    for control in ["main.btnPTT", "main.btnTune", "main.btnAnalog", "main.btnVoiceKeyer"]:
        check_equal(app.get(control)["enabled"], False, "%s enabled %s" % (control, what))


def configured_devices(app):
    audio = app.state()["config"]["audio"]
    return {card: audio[card]["deviceName"] for card in CARDS}


def stream_on(app, device):
    """FreeDV's one stream on the given device."""
    found = [stream for stream in streams(app.pid) if stream["device"] == device]
    check_equal(len(found), 1, "Number of FreeDV's streams on %s (it has %r)" % (device, streams(app.pid)))
    return found[0]


def move_to(app, device, to):
    """Moves FreeDV's stream on one device to another, and waits for the
    sound server to have done it."""
    # The sound server doesn't always do as it's asked the first time, so
    # make sure of it before expecting anything of FreeDV.
    for attempt in range(5):
        time.sleep(1)
        move_stream(stream_on(app, device), to)
        deadline = time.time() + 3
        while time.time() < deadline:
            if any(stream["device"] == to for stream in streams(app.pid)):
                return
            time.sleep(0.25)
    check(False, "FreeDV's stream on %s couldn't be moved to %s" % (device, to))


def wait_for_devices(app, expected, what):
    """Waits for FreeDV's settings to say it's using the given devices."""
    try:
        app.wait_until(lambda: configured_devices(app) == expected, timeout=10)
    except freedvtest.FreeDVError:
        told = [line.split("PulseAudioDevice.cpp")[-1] for line in app.log().splitlines()
                if "has been moved" in line]
        check_equal(configured_devices(app), expected,
                    "%s (streams: %r; FreeDV was told: %r)" % (what, streams(app.pid), told))



# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------

def test_missing_device():
    """Starting with a device that isn't there."""
    with audio_devices():
        for card in CARDS:
            missing = "FreeDVGuiTest_Missing" + (".monitor" if card.endswith("In") else "")
            with freedvtest.start(settings=card_settings(**{card: missing})) as app:
                app.wait_for_window("main")
                for attempt in range(2):
                    try_to_start(app)
                    boxes = app.message_boxes(clear=True)
                    check_equal([box["caption"] for box in boxes], ["Sound Device Not Found"],
                                "Messages displayed with %s missing" % card)
                    check(boxes[0]["message"].startswith("Your %s device cannot be found" % missing),
                          "Message displayed with %s missing is %r" % (card, boxes[0]["message"]))
                    check_not_started(app, "with %s missing" % card)
                check(not app.window_exists("settings"), "The Settings window opened with %s missing" % card)
                check_equal(configured_devices(app)[card], missing, "What %s is set to afterwards" % card)
                check_equal(app.quit(), 0, "FreeDV's exit code")

    # A device that goes away between one start and the next, and comes back.
    # (Not on macOS or Windows, where one can't be taken away here: see
    # freedvtest.audio.)
    if freedvtest.audio.FIXED_DEVICES:
        return
    with VirtualAudioDevices(RADIO_RX, RADIO_TX, MICROPHONE):
        speaker = VirtualAudioDevices(SPEAKER)
        speaker.__enter__()
        try:
            with freedvtest.start(settings=settings()) as app:
                app.wait_for_window("main")
                start_modem(app)
                stop_modem(app)

                speaker.__exit__(None, None, None)
                try_to_start(app)
                boxes = app.message_boxes(clear=True)
                check(len(boxes) == 1 and boxes[0]["message"].startswith("Your %s device cannot be found" % SPEAKER),
                      "Messages displayed with the speaker unplugged are %r" % boxes)
                check_not_started(app, "with the speaker unplugged")

                speaker.__enter__()
                start_modem(app)
                check_equal(configured_devices(app), CARDS, "Devices in use once the speaker is back")
                stop_modem(app)
                quit_and_check_log(app)
        finally:
            speaker.__exit__(None, None, None)


def test_low_sample_rate():
    """Starting with a device set to a sample rate that's too low to use, or
    a microphone or speaker set below 16 kHz, which is allowed with a
    warning."""
    with audio_devices():
        for card in CARDS:
            minimum = 8000
            rate = minimum // 2
            with freedvtest.start(settings=card_settings(**{card + "_rate": rate})) as app:
                app.wait_for_window("main")
                try_to_start(app)
                check_equal([(box["caption"], box["message"]) for box in app.message_boxes(clear=True)],
                            [("Sample Rate Too Low",
                              "Your %s device is set to use a sample rate of %d, which is less than the minimum of "
                              "%d. Please go to Tools->Settings to check your settings." % (CARDS[card], rate, minimum))],
                            "Messages displayed with %s at %d" % (card, rate))
                check_not_started(app, "with %s at %d" % (card, rate))
                check_equal(app.quit(), 0, "FreeDV's exit code")

        # The lowest rates that are allowed. Only the microphone and speaker
        # are warned about, and only the first time FreeDV starts.
        lowest = {card + "_rate": 8000 for card in CARDS}
        with freedvtest.start(settings=card_settings(**lowest)) as app:
            app.wait_for_window("main")
            try_to_start(app)
            check_low_rate_warning(app, ["soundCard2In", "soundCard2Out"], "with every device at 8000")
            check_equal(app.state()["running"], True, "Running with every device at 8000")
            stop_modem(app)
            start_modem(app)
            stop_modem(app)
            quit_and_check_log(app)

        # Receiving only, what goes to the radio otherwise goes to the
        # speaker, so it's warned about too.
        receive_only = dict(soundCard1Out=SPEAKER, soundCard2In="none", soundCard2Out="none")
        with freedvtest.start(settings=card_settings(soundCard1Out_rate=8000, **receive_only)) as app:
            app.wait_for_window("main")
            try_to_start(app)
            boxes = app.message_boxes(clear=True)
            check(len(boxes) == 1 and boxes[0]["caption"] == "Low Sample Rate" and
                  "%s (8000 Hz)" % SPEAKER in boxes[0]["message"], "Messages displayed are %r" % boxes)
            check_equal(app.state()["running"], True, "Running with the speaker at 8000")
            stop_modem(app)
            quit_and_check_log(app)


def check_low_rate_warning(app, cards, what):
    """Checks that the one message displayed is the warning about the given
    cards' low sample rates."""
    boxes = app.message_boxes(clear=True)
    check_equal([box["caption"] for box in boxes], ["Low Sample Rate"], "Messages displayed " + what)
    for card in CARDS:
        listed = "%s (8000 Hz)" % CARDS[card] in boxes[0]["message"]
        check_equal(listed, card in cards, "%s listed in the warning %s" % (card, what))


def test_no_devices():
    """Starting with no devices chosen, which gets the Settings window
    rather than a complaint."""
    nothing = {card: "none" for card in CARDS}
    with audio_devices():
        with freedvtest.start(settings=card_settings(**nothing)) as app:
            app.wait_for_window("main")
            app.click("main.btnOnOff")
            app.wait_for_window("settings")
            check_equal(app.message_boxes(), [], "Messages displayed")
            app.click("settings.sdbSizer5Cancel")
            app.wait_for_window_closed("settings")
            app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to give up starting")
            check_not_started(app, "with no devices chosen")

            # Only half of the first pair is as good as none.
            check_equal(app.quit(), 0, "FreeDV's exit code")

        with freedvtest.start(settings=card_settings(soundCard1Out="none", soundCard2In="none",
                                                     soundCard2Out="none")) as app:
            app.wait_for_window("main")
            app.click("main.btnOnOff")
            app.wait_for_window("settings")
            app.click("settings.sdbSizer5Cancel")
            app.wait_for_window_closed("settings")
            app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to give up starting")
            check_not_started(app, "with only the radio's input chosen")
            check_equal(app.quit(), 0, "FreeDV's exit code")


def test_partly_configured():
    """Starting with some devices chosen and others not, where what is
    chosen is and isn't usable."""
    missing = "FreeDVGuiTest_Missing"
    with audio_devices():
        for changes, outcome in [
                # Without both of the radio's devices there's nothing to run with.
                (dict(soundCard1In="none"), "settings"),
                (dict(soundCard1Out="none"), "settings"),
                # Only one of the other pair is the same as neither: receive only.
                (dict(soundCard2In="none"), "started"),
                (dict(soundCard2Out="none"), "started"),
                # What is chosen is still checked, whatever else isn't.
                (dict(soundCard1In="none", soundCard1Out=missing), "Sound Device Not Found"),
                (dict(soundCard2In="none", soundCard2Out=missing), "Sound Device Not Found"),
                (dict(soundCard2Out="none", soundCard2In=missing + ".monitor"), "Sound Device Not Found"),
                # A microphone or speaker below 16 kHz starts with a warning.
                (dict(soundCard2In="none", soundCard2Out_rate=8000), "warned"),
                (dict(soundCard2Out="none", soundCard2In_rate=8000), "warned"),
                (dict(soundCard2In="none", soundCard2Out="none", soundCard1Out_rate=8000), "warned"),
                (dict(soundCard2In="none", soundCard2Out="none", soundCard1Out_rate=16000), "started")]:
            what = "with %s" % ", ".join("%s as %s" % item for item in sorted(changes.items()))
            with freedvtest.start(settings=card_settings(**changes)) as app:
                app.wait_for_window("main")
                app.click("main.btnOnOff")
                if outcome == "settings":
                    app.wait_for_window("settings")
                    check_equal(app.message_boxes(), [], "Messages displayed " + what)
                    app.click("settings.sdbSizer5Cancel")
                    app.wait_for_window_closed("settings")
                app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to finish starting")
                time.sleep(0.5)
                if outcome in ("started", "warned"):
                    expected = ["Low Sample Rate"] if outcome == "warned" else []
                    check_equal([box["caption"] for box in app.message_boxes(clear=True)], expected,
                                "Messages displayed " + what)
                    check_equal(app.state()["running"], True, "Running " + what)
                    check_equal(app.get("main.btnPTT")["enabled"], False, "PTT button enabled " + what)
                    stop_modem(app)
                else:
                    if outcome != "settings":
                        check_equal([box["caption"] for box in app.message_boxes(clear=True)], [outcome],
                                    "Messages displayed " + what)
                    check(not app.window_exists("settings"), "The Settings window is open " + what)
                    check_not_started(app, what)
                check_equal(app.quit(), 0, "FreeDV's exit code")


def test_receive_only():
    """Starting with only the first pair of devices, which is enough to
    receive with."""
    with audio_devices():
        for second_pair in [dict(soundCard2In="none", soundCard2Out="none"),
                            # Half a second pair is no pair at all.
                            dict(soundCard2Out="none")]:
            what = "with %r" % second_pair
            with freedvtest.start(settings=card_settings(soundCard1Out=SPEAKER, **second_pair)) as app:
                app.wait_for_window("main")
                start_modem(app)
                check_equal(app.state()["numSoundCards"], 1, "Pairs of devices in use " + what)
                for control in ["main.btnPTT", "main.btnTune", "main.btnVoiceKeyer"]:
                    check_equal(app.get(control)["enabled"], False, "%s enabled %s" % (control, what))
                check_equal(app.get("main.btnAnalog")["enabled"], True, "Analog enabled " + what)

                # What's received is heard through the one output there is.
                app.click("main.btnAnalog")
                with Tone(RADIO_RX):
                    time.sleep(0.5)
                    heard = level(SPEAKER_MONITOR)
                    check(heard > AUDIBLE, "The radio isn't being played through the speaker %s (level %d)"
                          % (what, heard))
                    # (Which can't be told apart from the tone when the radio's
                    # input and output are the same device: see
                    # freedvtest.audio.shared().)
                    if not freedvtest.audio.shared(RADIO_RX, RADIO_TX):
                        check_silent(RADIO_TX_MONITOR, "Something is being sent to the radio " + what)

                stop_modem(app)
                quit_and_check_log(app)


def test_device_moved():
    """Streams being moved to other devices while FreeDV is running, as the
    system's sound settings can do. FreeDV carries on with, and remembers,
    where each ended up."""
    with audio_devices(), VirtualAudioDevices(OTHER_OUTPUT, OTHER_INPUT_DEVICE):
        with freedvtest.start(settings=settings()) as app:
            app.wait_for_window("main")
            start_modem(app)
            check_equal(configured_devices(app), CARDS, "Devices to begin with")

            # Each of the four in turn, and back again.
            for card, device in CARDS.items():
                other = OTHER_INPUT if card.endswith("In") else OTHER_OUTPUT
                move_to(app, device, other)
                wait_for_devices(app, dict(CARDS, **{card: other}), "Devices after moving " + card)
                check_equal(freedvtest.read_config(app.config_path).get("/Audio/%sDeviceName" % card), other,
                            "Saved device for " + card)
                check_equal(app.message_boxes(), [], "Messages displayed after moving " + card)
                check_equal(app.state()["running"], True, "Running after moving " + card)

                move_to(app, other, device)
                wait_for_devices(app, CARDS, "Devices after moving %s back" % card)

            # It's the new device that's then used: the speaker's stream,
            # with what's coming from the radio played through it.
            app.click("main.btnAnalog")
            move_to(app, SPEAKER, OTHER_OUTPUT)
            wait_for_devices(app, dict(CARDS, soundCard2Out=OTHER_OUTPUT), "Devices after moving the speaker")
            with Tone(RADIO_RX):
                time.sleep(0.75)
                heard = level(VirtualAudioDevices.input_name(OTHER_OUTPUT))
                check(heard > AUDIBLE, "The radio isn't being played through the other output (level %d)" % heard)
                check_silent(SPEAKER_MONITOR, "Something is still being played through the old speaker")

            # Likewise for what's sent to the radio.
            move_to(app, RADIO_TX, SPEAKER)
            wait_for_devices(app, dict(CARDS, soundCard1Out=SPEAKER, soundCard2Out=OTHER_OUTPUT),
                             "Devices after moving the radio's output")
            app.click("main.btnPTT")
            wait_for_transmit(app)
            with Tone(MICROPHONE):
                time.sleep(0.75)
                sent = level(SPEAKER_MONITOR)
                check(sent > AUDIBLE, "The microphone isn't being sent to where the radio's output went (level %d)"
                      % sent)
            app.click("main.btnPTT")
            wait_for_transmit(app, False)

            # And where it'll start from the next time.
            stop_modem(app)
            start_modem(app)
            check_equal(configured_devices(app), dict(CARDS, soundCard1Out=SPEAKER, soundCard2Out=OTHER_OUTPUT),
                        "Devices after starting again")
            check_equal(sorted(stream["device"] for stream in streams(app.pid)),
                        sorted([CARDS["soundCard1In"], SPEAKER, CARDS["soundCard2In"], OTHER_OUTPUT]),
                        "What FreeDV's streams are connected to after starting again")
            stop_modem(app)
            quit_and_check_log(app)


def test_receive_only_device_moved():
    """Streams being moved to other devices while FreeDV is receiving with
    only one pair of devices: it carries on with, and remembers, where each
    ended up."""
    receive_only = card_settings(soundCard1Out=SPEAKER, soundCard2In="none", soundCard2Out="none")
    with audio_devices(), VirtualAudioDevices(OTHER_OUTPUT, OTHER_INPUT_DEVICE):
        with freedvtest.start(settings=receive_only) as app:
            app.wait_for_window("main")
            start_modem(app)
            check_equal(app.state()["numSoundCards"], 1, "Pairs of devices in use")
            before = configured_devices(app)
            check_equal([before["soundCard1In"], before["soundCard1Out"]], [CARDS["soundCard1In"], SPEAKER],
                        "Devices to begin with")

            # The one input and the one output, in turn.
            move_to(app, CARDS["soundCard1In"], OTHER_INPUT)
            wait_for_devices(app, dict(before, soundCard1In=OTHER_INPUT), "Devices after moving the radio's input")
            check_equal(freedvtest.read_config(app.config_path).get("/Audio/soundCard1InDeviceName"), OTHER_INPUT,
                        "Saved device for the radio's input")

            move_to(app, SPEAKER, OTHER_OUTPUT)
            wait_for_devices(app, dict(before, soundCard1In=OTHER_INPUT, soundCard1Out=OTHER_OUTPUT),
                             "Devices after moving the speaker")
            check_equal(freedvtest.read_config(app.config_path).get("/Audio/soundCard1OutDeviceName"), OTHER_OUTPUT,
                        "Saved device for the speaker")
            check_equal(app.message_boxes(), [], "Messages displayed after moving the devices")
            check_equal(app.state()["running"], True, "Running after moving the devices")

            # It's the new devices that are then used: what comes in through
            # the one that the radio's stream moved to is heard out of the
            # one that the speaker's stream moved to.
            app.click("main.btnAnalog")
            with Tone(OTHER_INPUT_DEVICE):
                time.sleep(0.75)
                heard = level(VirtualAudioDevices.input_name(OTHER_OUTPUT))
                check(heard > AUDIBLE, "The radio isn't being played through the other output (level %d)" % heard)
                check_silent(SPEAKER_MONITOR, "Something is still being played through the old speaker")

            stop_modem(app)
            quit_and_check_log(app)



def test_device_removed():
    """A device going away while FreeDV is using it, and then all of them
    at once. This is done with a sound server of the test's own: what was
    using a device that's removed is moved to the system's default device,
    which mustn't be someone's real one."""
    if not PrivateAudioServer.available():
        print("SKIPPED: PipeWire and what's needed to run it separately were not all found")
        sys.exit(SKIPPED)

    with PrivateAudioServer() as server:
        with VirtualAudioDevices(RADIO_RX, RADIO_TX, MICROPHONE):
            speaker = VirtualAudioDevices(SPEAKER)
            speaker.__enter__()
            try:
                with freedvtest.start(settings=settings()) as app:
                    app.wait_for_window("main")
                    start_modem(app)
                    app.click("main.btnAnalog")
                    time.sleep(2)

                    with Tone(RADIO_RX):
                        time.sleep(0.75)
                        heard = level(SPEAKER_MONITOR)
                        check(heard > AUDIBLE, "The radio isn't being played through the speaker (level %d)" % heard)

                        # The speaker's unplugged. FreeDV carries on with
                        # where the system sends its sound instead.
                        speaker.__exit__(None, None, None)
                        expected = dict(CARDS, soundCard2Out=PrivateAudioServer.DEFAULT_OUTPUT)
                        try:
                            app.wait_until(lambda: configured_devices(app) == expected, timeout=15)
                        except freedvtest.FreeDVError:
                            check_equal(configured_devices(app), expected, "Devices after the speaker was unplugged")
                        check_equal(app.state()["running"], True, "Running after the speaker was unplugged")
                        check_equal(app.message_boxes(), [], "Messages displayed after the speaker was unplugged")
                        time.sleep(1)
                        heard = level(VirtualAudioDevices.input_name(PrivateAudioServer.DEFAULT_OUTPUT))
                        check(heard > AUDIBLE,
                              "The radio isn't being played through the default device instead (level %d)" % heard)

                    # The sound server itself goes away, and everything with
                    # it. FreeDV says so, rather than carrying on in silence
                    # or falling over, and can still be stopped and closed.
                    server.stop()
                    app.wait_until(lambda: app.message_boxes(), timeout=30,
                                   description="FreeDV to say that something's wrong with its audio")
                    time.sleep(2)
                    boxes = app.message_boxes(clear=True)
                    print("Messages after losing the sound server: %r" % [box["message"] for box in boxes], flush=True)
                    check(all(box["message"].startswith("Error encountered while") for box in boxes),
                          "Messages displayed are %r" % boxes)

                    app.click("main.btnOnOff")
                    app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], timeout=60,
                                   description="FreeDV to stop")
                    check_equal(app.state()["running"], False, "Running after pressing Stop")
                    app.message_boxes(clear=True)

                    # Starting again without one gets an explanation as well.
                    app.click("main.btnOnOff")
                    app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], timeout=60,
                                   description="FreeDV to give up starting")
                    time.sleep(1)
                    boxes = app.message_boxes(clear=True)
                    print("Messages on starting without a sound server: %r" % [box["message"] for box in boxes],
                          flush=True)
                    check(len(boxes) > 0, "Nothing was said about there being no sound server to start with")
                    check_equal(app.state()["running"], False, "Running without a sound server")
                    check_equal(app.quit(), 0, "FreeDV's exit code")
            finally:
                try:
                    speaker.__exit__(None, None, None)
                except Exception:
                    pass


def test_sound_server_restarted():
    """The sound server being restarted under FreeDV, as happens when it's
    updated or has crashed: FreeDV says that it's lost its devices, and
    carries on once it's been stopped and started again."""
    if not PrivateAudioServer.available():
        print("SKIPPED: PipeWire and what's needed to run it separately were not all found")
        sys.exit(SKIPPED)

    with PrivateAudioServer() as server:
        devices = audio_devices()
        devices.__enter__()
        try:
            with freedvtest.start(settings=settings()) as app:
                app.wait_for_window("main")
                start_modem(app)
                app.click("main.btnAnalog")
                time.sleep(2)

                server.stop()
                devices.forget()
                app.wait_until(lambda: len(app.message_boxes()) >= 4, timeout=30,
                               description="FreeDV to say that it's lost each of its devices")
                lost = sorted(box["message"] for box in app.message_boxes(clear=True))
                expected = sorted("Error encountered while processing audio: Lost connection to %s (Connection "
                                  "terminated)" % device for device in CARDS.values())
                check_equal(lost, expected, "Messages displayed")
                check_equal(app.state()["running"], True, "Running, as far as FreeDV's concerned")

                # Nothing works again by itself...
                server.start()
                devices = audio_devices()
                devices.__enter__()
                time.sleep(2)
                check_equal(streams(app.pid), [], "FreeDV's streams on the new sound server")

                # ...but stopping and starting does it.
                stop_modem(app)
                app.message_boxes(clear=True)
                start_modem(app)
                check_equal(configured_devices(app), CARDS, "Devices in use")
                check_equal(len(streams(app.pid)), 4, "Number of FreeDV's streams")
                check_equal(app.state()["analog"], True, "Still in analog mode")
                with Tone(RADIO_RX):
                    time.sleep(0.75)
                    heard = level(SPEAKER_MONITOR)
                    check(heard > AUDIBLE, "The radio isn't being played through the speaker (level %d)" % heard)
                stop_modem(app)
                quit_and_check_log_ignoring(app, "Lost connection to")
        finally:
            try:
                devices.__exit__(None, None, None)
            except Exception:
                pass


def test_sound_server_gone_while_starting():
    """The sound server going away part way through FreeDV starting: the
    sound cards check out, and then there's nothing to connect to when it
    comes to opening the streams."""
    with audio_devices(), AudioServerGateway() as gateway:
        with freedvtest.start(settings=settings(), env=gateway.env()) as app:
            app.wait_for_window("main")

            # FreeDV connects to the sound server to check its sound cards,
            # and closes that connection before opening any streams. From
            # then on, there's nothing on the other end of the gateway.
            # (Counted rather than waiting to see the first one close, which
            # the gateway can find out about after FreeDV has made the next.)
            gateway.refuse_connections_after(1)
            try_to_start(app)

            boxes = sorted((box["caption"], box["message"]) for box in app.message_boxes(clear=True))
            expected = sorted([
                ("Error", "Error encountered while initializing the audio engine: Could not connect PulseAudio "
                          "context.."),
                ("Error", "Could not find TX input sound device '%s'. Please check settings and try again."
                          % CARDS["soundCard2In"]),
            ])
            check_equal(boxes, expected, "Messages displayed when the sound server went while starting")
            check_not_started(app, "with the sound server gone while starting")
            quit_and_check_log(app)


def test_sound_card_gone_while_starting():
    """One sound card going away part way through FreeDV starting: the sound
    cards check out, and then one of them isn't there when it comes to
    opening the streams. FreeDV names the one that's gone, and starts none
    of them. Which card plays which part depends on whether the second pair
    is set up at all, so both ways round are covered here."""
    receive_only = dict(soundCard1Out=SPEAKER, soundCard2In="none", soundCard2Out="none")
    cases = [
        # (settings changes, the part the card plays, the device that goes,
        # how FreeDV's settings name it, the devices that stay)
        ({}, "TX output", RADIO_TX, RADIO_TX, (RADIO_RX, MICROPHONE, SPEAKER)),
        ({}, "RX input", RADIO_RX, CARDS["soundCard1In"], (RADIO_TX, MICROPHONE, SPEAKER)),
        ({}, "RX output", SPEAKER, SPEAKER, (RADIO_RX, RADIO_TX, MICROPHONE)),
        (receive_only, "RX input", RADIO_RX, CARDS["soundCard1In"], (SPEAKER,)),
        (receive_only, "RX output", SPEAKER, SPEAKER, (RADIO_RX,)),
    ]
    for changes, role, gone, named, kept in cases:
        with AudioServerGateway() as gateway:
            with VirtualAudioDevices(*kept), VirtualAudioDevices(gone) as removable:
                with freedvtest.start(settings=card_settings(**changes), env=gateway.env()) as app:
                    app.wait_for_window("main")

                    # FreeDV connects to the sound server to check its sound
                    # cards, and closes that connection before opening any
                    # streams. That's when the card is unplugged.
                    gateway.after_next_connection_close(lambda: removable.remove(gone))
                    try_to_start(app)

                    boxes = sorted((box["caption"], box["message"]) for box in app.message_boxes(clear=True))
                    check_equal(boxes, [("Error", "Could not find %s sound device '%s'. Please check settings and "
                                                  "try again." % (role, named))],
                                "Messages displayed when %s went while starting" % named)
                    check_not_started(app, "with %s gone while starting" % named)
                    quit_and_check_log(app)


def test_sound_server_gone_while_opening_streams():
    """The sound server going away as FreeDV opens its streams: each of the
    four in turn finds nothing on the other end of the connection. FreeDV
    names the ones it couldn't reach, closes the ones it had opened, and
    starts none of them."""
    # FreeDV makes one connection to check its sound cards, one to list the
    # devices it's about to use, and then one for each stream: the TX pair
    # first (both of them, before it looks at whether either worked), then
    # the RX pair. Refusing every connection from the (2 + n)th onwards is
    # what the server going away as FreeDV opens its (n + 1)th stream looks
    # like to it.
    cases = [
        # (the stream that first fails, the devices FreeDV is told it
        # couldn't reach)
        ("TX input", [CARDS["soundCard2In"], CARDS["soundCard1Out"]]),
        ("TX output", [CARDS["soundCard1Out"]]),
        ("RX input", [CARDS["soundCard1In"]]),
        ("RX output", [CARDS["soundCard2Out"]]),
    ]
    for n, (role, unreachable) in enumerate(cases):
        why = "while opening the %s stream" % role
        expected = sorted(("Error", "Error encountered while processing audio: Could not connect PulseAudio "
                                    "context. (%s)" % device) for device in unreachable)
        with audio_devices(), AudioServerGateway() as gateway:
            with freedvtest.start(settings=settings(), env=gateway.env()) as app:
                app.wait_for_window("main")

                gateway.refuse_connections_after(2 + n)
                try_to_start(app)

                boxes = sorted((box["caption"], box["message"]) for box in app.message_boxes(clear=True))
                check_equal(boxes, expected, "Messages displayed " + why)
                check_not_started(app, "with the sound server gone " + why)

                # None of the streams it had opened is left connected.
                try:
                    app.wait_until(lambda: not streams(app.pid), timeout=5)
                except freedvtest.FreeDVError:
                    pass
                check_equal(streams(app.pid), [], "FreeDV's streams left " + why)

                quit_and_check_log_ignoring(app, "Could not connect PulseAudio context")


def quit_and_check_log_ignoring(app, *expected):
    """Quits FreeDV and checks what it logged, allowing through the errors
    that the scenario meant it to report."""
    check_equal(app.message_boxes(), [], "Errors displayed")
    check_equal(app.quit(), 0, "FreeDV's exit code")
    errors = [line for line in app.log().splitlines()
              if (" ERROR " in line or " FATAL " in line) and not any(text in line for text in expected)]
    check_equal(errors, [], "Errors logged by FreeDV, other than the expected ones")


SCENARIOS = {
    "missing_device": test_missing_device,
    "low_sample_rate": test_low_sample_rate,
    "no_devices": test_no_devices,
    "partly_configured": test_partly_configured,
    "receive_only": test_receive_only,
    "device_moved": test_device_moved,
    "receive_only_device_moved": test_receive_only_device_moved,
    "device_removed": test_device_removed,
    "sound_server_restarted": test_sound_server_restarted,
    "sound_server_gone_while_starting": test_sound_server_gone_while_starting,
    "sound_card_gone_while_starting": test_sound_card_gone_while_starting,
    "sound_server_gone_while_opening_streams": test_sound_server_gone_while_opening_streams,
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
