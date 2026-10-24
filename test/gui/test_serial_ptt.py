#!/usr/bin/env python3
"""Tests FreeDV keying a radio with a serial port's RTS and DTR lines, and
being told to transmit by its CTS line.

Each scenario below launches its own copy (or copies) of FreeDV. With no
arguments all of them are run; otherwise, only those named:

    python3 test_serial_ptt.py [scenario...]

Run from the build folder (or set FREEDV_BINARY) with a window manager
running, on Linux or macOS, and needs a C compiler: see freedvtest/serial.py
for how serial ports are stood in for.
"""

import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import freedvtest
import freedvtest.audio
from freedvtest.serial import SerialPorts
from test_main_window import (audio_devices, check, check_equal, quit_and_check_log, settings, start_modem,
                              stop_modem, transmitting, wait_for_transmit)

OFF = {"rts": False, "dtr": False}


def serial_ptt(port, use_rts=True, rts_polarity=True, use_dtr=False, dtr_polarity=False, **extra):
    """Returns settings for FreeDV to key the radio with the given port."""
    return settings(Rig__UseSerialPTT=True, Rig__Port=port.name, Rig__UseRTS=use_rts, Rig__RTSPolarity=rts_polarity,
                    Rig__UseDTR=use_dtr, Rig__DTRPolarity=dtr_polarity, **extra)


def wait_for_lines(app, port, expected, what, timeout=5):
    try:
        app.wait_until(lambda: port.lines == expected, timeout)
    except freedvtest.FreeDVError:
        check_equal(port.lines, expected, what)


def key(app):
    app.click("main.btnPTT")
    wait_for_transmit(app)


def unkey(app):
    app.click("main.btnPTT")
    wait_for_transmit(app, False)


# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------

def test_rts():
    """Keying with RTS, which is what FreeDV uses unless told otherwise."""
    with SerialPorts() as ports, audio_devices():
        radio = ports.add()
        with freedvtest.start(settings=serial_ptt(radio), env=ports.env) as app:
            app.wait_for_window("main")
            check_equal(radio.history(), [], "Lines set before starting")

            start_modem(app)
            wait_for_lines(app, radio, OFF, "Lines once started")

            key(app)
            wait_for_lines(app, radio, {"rts": True, "dtr": False}, "Lines while transmitting")
            unkey(app)
            wait_for_lines(app, radio, OFF, "Lines back on receive")
            check(all(not state["dtr"] for state in radio.history()), "DTR was used: %r" % radio.history())

            # Tune and the voice keyer key the radio like PTT does.
            app.click("main.btnTune")
            wait_for_lines(app, radio, {"rts": True, "dtr": False}, "Lines while tuning")
            app.click("main.btnTune")
            wait_for_lines(app, radio, OFF, "Lines after tuning")

            # FreeDV can't be stopped while it's transmitting...
            key(app)
            wait_for_lines(app, radio, {"rts": True, "dtr": False}, "Lines while transmitting")
            check_equal(app.get("main.btnOnOff")["enabled"], False, "Stop enabled while transmitting")
            unkey(app)
            wait_for_lines(app, radio, OFF, "Lines back on receive")

            # ...and can be started again once it has been.
            stop_modem(app)
            wait_for_lines(app, radio, OFF, "Lines after stopping")
            start_modem(app)
            key(app)
            wait_for_lines(app, radio, {"rts": True, "dtr": False}, "Lines while transmitting the second time")

            # Closing FreeDV while it's transmitting lets go of the radio.
            check_equal(app.quit(), 0, "FreeDV's exit code")
            check_equal(radio.lines, OFF, "Lines after closing FreeDV while transmitting")


def test_polarity():
    """The other ways of keying: DTR, both lines, and either the other way up."""
    cases = [
        # use RTS, RTS polarity, use DTR, DTR polarity, lines on receive, lines on transmit
        (False, True, True, True, OFF, {"rts": False, "dtr": True}),
        (True, True, True, True, OFF, {"rts": True, "dtr": True}),
        (True, False, False, False, {"rts": True, "dtr": False}, OFF),
        (False, False, True, False, {"rts": False, "dtr": True}, OFF),
        (True, True, True, False, {"rts": False, "dtr": True}, {"rts": True, "dtr": False}),
    ]
    with SerialPorts() as ports, audio_devices():
        for use_rts, rts_polarity, use_dtr, dtr_polarity, receiving, sending in cases:
            what = "RTS %s (%s), DTR %s (%s)" % ("used" if use_rts else "unused", "+" if rts_polarity else "-",
                                                 "used" if use_dtr else "unused", "+" if dtr_polarity else "-")
            radio = ports.add()
            with freedvtest.start(settings=serial_ptt(radio, use_rts, rts_polarity, use_dtr, dtr_polarity),
                                  env=ports.env) as app:
                app.wait_for_window("main")
                start_modem(app)
                wait_for_lines(app, radio, receiving, "Lines once started with " + what)
                key(app)
                wait_for_lines(app, radio, sending, "Lines while transmitting with " + what)
                unkey(app)
                wait_for_lines(app, radio, receiving, "Lines back on receive with " + what)
                stop_modem(app)
                wait_for_lines(app, radio, receiving, "Lines after stopping with " + what)
                quit_and_check_log(app)


def test_ptt_input():
    """Transmitting when a serial port's CTS line says to, e.g. from a foot
    switch."""
    with SerialPorts() as ports, audio_devices():
        # CTS raised means transmit.
        radio = ports.add()
        switch = ports.add()
        switch.set_cts(False)
        extra = dict(Rig__UseSerialPTTInput=True, Rig__PttInPort=switch.name, Rig__CTSPolarity=True)
        with freedvtest.start(settings=serial_ptt(radio, **extra), env=ports.env) as app:
            app.wait_for_window("main")
            switch.set_cts(True)
            time.sleep(1)
            check_equal(app.state()["running"], False, "Started by the PTT input")
            switch.set_cts(False)

            start_modem(app)
            time.sleep(1)
            check_equal(transmitting(app), False, "Transmitting with the switch up")
            # Its own RTS and DTR are raised, for switches powered from them.
            check_equal(switch.lines, {"rts": True, "dtr": True}, "PTT input port's lines")

            switch.set_cts(True)
            wait_for_transmit(app)
            check_equal(app.get("main.btnPTT")["value"], True, "PTT button pressed")
            wait_for_lines(app, radio, {"rts": True, "dtr": False}, "Radio's lines with the switch down")
            switch.set_cts(False)
            wait_for_transmit(app, False)
            check_equal(app.get("main.btnPTT")["value"], False, "PTT button pressed")
            wait_for_lines(app, radio, OFF, "Radio's lines with the switch up")

            # The button on screen still works, and the switch takes over
            # again when it next changes.
            key(app)
            switch.set_cts(True)
            time.sleep(1)
            check_equal(transmitting(app), True, "Transmitting with both the button and the switch")
            switch.set_cts(False)
            wait_for_transmit(app, False)
            check_equal(app.get("main.btnPTT")["value"], False, "PTT button pressed")

            # Stopped, the switch does nothing.
            stop_modem(app)
            switch.set_cts(True)
            time.sleep(1)
            check_equal((app.state()["running"], transmitting(app)), (False, False), "Running and transmitting")
            switch.set_cts(False)
            quit_and_check_log(app)

        # CTS lowered means transmit: the switch is down to begin with.
        radio = ports.add()
        switch = ports.add()
        switch.set_cts(True)
        extra = dict(Rig__UseSerialPTTInput=True, Rig__PttInPort=switch.name, Rig__CTSPolarity=False)
        with freedvtest.start(settings=serial_ptt(radio, **extra), env=ports.env) as app:
            app.wait_for_window("main")
            start_modem(app)
            time.sleep(1)
            check_equal(transmitting(app), False, "Transmitting with CTS raised")
            switch.set_cts(False)
            wait_for_transmit(app)
            wait_for_lines(app, radio, {"rts": True, "dtr": False}, "Radio's lines with CTS lowered")
            switch.set_cts(True)
            wait_for_transmit(app, False)
            stop_modem(app)
            quit_and_check_log(app)


def test_errors():
    """Ports that can't be used."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        with SerialPorts() as ports, audio_devices():
            # A port that isn't there (any more). FreeDV says so and carries
            # on without it.
            missing = "/dev/ttyFreeDVTestMissing"
            config = settings(Rig__UseSerialPTT=True, Rig__Port=missing)
            with freedvtest.start(settings=config, env=ports.env) as app:
                app.wait_for_window("main")
                app.click("main.btnOnOff")
                app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to start")
                app.wait_until(lambda: app.message_boxes(), timeout=10, description="a message to be displayed")
                boxes = app.message_boxes(clear=True)
                check_equal([box["caption"] for box in boxes], ["Error"], "Messages displayed")
                check(boxes[0]["message"].startswith("Could not open serial port %s." % missing),
                      "Message displayed is %r" % boxes[0]["message"])
                check_equal(app.state()["running"], True, "Running without the radio's port")

                # There's nothing to key, but FreeDV still transmits.
                key(app)
                unkey(app)
                stop_modem(app)
                check_equal(app.quit(), 0, "FreeDV's exit code")

            # The same goes for where it's told to transmit from.
            radio = ports.add()
            extra = dict(Rig__UseSerialPTTInput=True, Rig__PttInPort=missing, Rig__CTSPolarity=True)
            with freedvtest.start(settings=serial_ptt(radio, **extra), env=ports.env) as app:
                app.wait_for_window("main")
                app.click("main.btnOnOff")
                app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to start")
                app.wait_until(lambda: app.message_boxes(), timeout=10, description="a message to be displayed")
                boxes = app.message_boxes(clear=True)
                check(len(boxes) == 1 and boxes[0]["message"].startswith("Could not open serial port %s." % missing),
                      "Messages displayed are %r" % boxes)
                check_equal(app.state()["running"], True, "Running without the PTT input port")
                key(app)
                wait_for_lines(app, radio, {"rts": True, "dtr": False}, "Radio's lines while transmitting")
                unkey(app)
                stop_modem(app)
                check_equal(app.quit(), 0, "FreeDV's exit code")

            # Something that can be opened but isn't a serial port: FreeDV
            # finds that out when it comes to set it up, and carries on
            # without.
            not_serial = "/dev/null"
            config = settings(Rig__UseSerialPTT=True, Rig__Port=not_serial)
            with freedvtest.start(settings=config, env=ports.env) as app:
                app.wait_for_window("main")
                app.click("main.btnOnOff")
                app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to start")
                app.wait_until(lambda: app.message_boxes(), timeout=10, description="a message to be displayed")
                check_equal([(box["caption"], box["message"]) for box in app.message_boxes(clear=True)],
                            [("Error", "Couldn't open serial port for PTT output: Could not open serial port %s"
                              % not_serial)], "Messages displayed")
                check_equal(app.state()["running"], True, "Running without %s" % not_serial)
                key(app)
                unkey(app)
                stop_modem(app)
                check_equal(app.quit(), 0, "FreeDV's exit code")

            # No port chosen at all is the same as not using one.
            with freedvtest.start(settings=settings(Rig__UseSerialPTT=True, Rig__Port=""), env=ports.env) as app:
                app.wait_for_window("main")
                start_modem(app)
                key(app)
                unkey(app)
                stop_modem(app)
                quit_and_check_log(app)

            # No input port chosen is the same as not using one: FreeDV
            # doesn't complain and its own port keys the radio as usual.
            radio = ports.add()
            extra = dict(Rig__UseSerialPTTInput=True, Rig__PttInPort="", Rig__CTSPolarity=True)
            with freedvtest.start(settings=serial_ptt(radio, **extra), env=ports.env) as app:
                app.wait_for_window("main")
                start_modem(app)
                check_equal(app.message_boxes(), [], "Messages displayed")
                key(app)
                wait_for_lines(app, radio, {"rts": True, "dtr": False}, "Radio's lines while transmitting")
                unkey(app)
                stop_modem(app)
                quit_and_check_log(app)

            # An input port that looks usable (not under /dev/, so FreeDV
            # doesn't check it up front) but can't actually be opened:
            # FreeDV says so once and carries on keying its own port.
            radio = ports.add()
            phantom = "freedv-test-no-such-port"
            extra = dict(Rig__UseSerialPTTInput=True, Rig__PttInPort=phantom, Rig__CTSPolarity=True)
            with freedvtest.start(settings=serial_ptt(radio, **extra), env=ports.env) as app:
                app.wait_for_window("main")
                app.click("main.btnOnOff")
                app.wait_until(lambda: app.get("main.btnOnOff")["enabled"], description="FreeDV to start")
                app.wait_until(lambda: app.message_boxes(), timeout=10, description="a message to be displayed")
                boxes = app.message_boxes(clear=True)
                check(len(boxes) == 1 and boxes[0]["message"].startswith(
                    "Couldn't open PTT input port: Could not open serial port %s" % phantom),
                    "Messages displayed are %r" % boxes)
                check_equal(app.state()["running"], True, "Running without the PTT input port")
                key(app)
                wait_for_lines(app, radio, {"rts": True, "dtr": False}, "Radio's lines while transmitting")
                unkey(app)
                stop_modem(app)
                check_equal(app.quit(), 0, "FreeDV's exit code")
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_settings_ptt_test():
    """The Settings window's Test PTT button with a serial port."""
    with SerialPorts() as ports:
        radio = ports.add()
        with freedvtest.start(settings=serial_ptt(radio), env=ports.env) as app:
            app.wait_for_window("main")
            app.menu("Settings/Edit Settings...")
            app.wait_for_window("settings")
            app.set_value("settings.notebook", "Radio")
            check_equal(app.get("settings.ckUseSerialPTT")["value"], True, "Serial PTT chosen")
            check_equal(app.get("settings.cbCtlDevicePath")["value"], radio.name, "Serial port")

            # The radio's keyed for a second.
            for attempt in range(3):
                before = len(radio.history())
                app.click("settings.buttonTestPTT")
                app.wait_until(lambda: radio.rts, timeout=5, description="the radio to be keyed")
                keyed = time.time()
                check_equal(app.get("settings.buttonTestPTT")["enabled"], False, "Test PTT enabled during the test")
                app.wait_until(lambda: not radio.rts, timeout=5, description="the radio to be unkeyed")
                check(0.7 < time.time() - keyed < 2.5, "The radio was keyed for %.1f s" % (time.time() - keyed))
                app.wait_until(lambda: app.get("settings.buttonTestPTT")["enabled"], timeout=10,
                               description="the PTT test to finish")
                check_equal([state["rts"] for state in radio.history()[before:]], [False, True, False],
                            "RTS during the test")
                check_equal(app.message_boxes(), [], "Messages displayed")

            # With DTR instead.
            app.set_value("settings.rbUseDTR", True)
            app.set_value("settings.ckDTRPos", True)
            before = len(radio.history())
            app.click("settings.buttonTestPTT")
            app.wait_until(lambda: radio.dtr, timeout=5, description="the radio to be keyed by DTR")
            app.wait_until(lambda: app.get("settings.buttonTestPTT")["enabled"], timeout=10,
                           description="the PTT test to finish")
            check_equal(radio.lines, OFF, "Lines after the test")
            check(all(not state["rts"] for state in radio.history()[before:]),
                  "RTS was used: %r" % radio.history()[before:])

            # A port that isn't there.
            app.set_value("settings.cbCtlDevicePath", "/dev/ttyFreeDVTestMissing")
            app.click("settings.buttonTestPTT")
            app.wait_until(lambda: app.message_boxes(), timeout=10, description="a message to be displayed")
            boxes = app.message_boxes(clear=True)
            check(len(boxes) == 1 and boxes[0]["message"].startswith("Could not open serial port"),
                  "Messages displayed are %r" % boxes)
            app.wait_until(lambda: app.get("settings.buttonTestPTT")["enabled"], timeout=10,
                           description="Test PTT to be usable again")

            app.click("settings.sdbSizer5Cancel")
            app.wait_for_window_closed("settings")
            check_equal(app.quit(), 0, "FreeDV's exit code")


SCENARIOS = {
    "rts": test_rts,
    "polarity": test_polarity,
    "ptt_input": test_ptt_input,
    "errors": test_errors,
    "settings_ptt_test": test_settings_ptt_test,
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
