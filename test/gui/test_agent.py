#!/usr/bin/env python3
"""Tests how FreeDV's test agent answers what it can't carry out.

Every other GUI test drives FreeDV through the agent in src/test_agent.cpp,
so the errors it gives back matter: a script that's told a control "is not
visible" when it meant "is disabled" ends up waiting for something that will
never happen. This scenario sends the malformed requests, the unknown names
and the wrong types it would be easy to send by accident, and checks what
FreeDV says about each.

Each scenario below launches its own copy of FreeDV. With no arguments all
of them are run; otherwise, only those named:

    python3 test_agent.py [scenario...]

Run from the build folder (or set FREEDV_BINARY), on Linux or macOS: see
freedvtest/audio.py for the audio devices it needs.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import freedvtest
import freedvtest.audio
from test_main_window import CALLSIGN, audio_devices, check, check_equal, settings as main_window_settings

SETTINGS_TABS = ["RX Audio", "TX Audio", "Radio", "Rig Control", "Reporting", "Voice Keyer", "Display", "Modem",
                 "Debugging"]

# The FreeDV Reporter window's list of spots, which stays empty here.
SPOTS = "reporter.listSpots"


def settings():
    """The main window tests' settings (which point FreeDV Reporter at an
    address nothing answers on), with reporting turned on so that the
    FreeDV Reporter window can be opened."""
    return main_window_settings(Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare="DM12")


def expect_error(app, expected, cmd, **params):
    """Sends a request FreeDV should refuse and checks the message it gives
    back. Every message expected here is one of the replyError_()s in
    src/test_agent.cpp, with the names and numbers filled in."""
    try:
        result = app.command(cmd, **params)
    except freedvtest.CommandError as e:
        check_equal(str(e), expected, "'%s' error" % cmd)
        return
    raise AssertionError("'%s' was accepted (%r), expected %r" % (cmd, result, expected))


def go_to_tab(app, tab):
    app.set_value("settings.notebook", tab)
    check_equal(app.get("settings.notebook")["value"], SETTINGS_TABS.index(tab), "Tab displayed")


def test_errors():
    """The agent's own error paths: requests it doesn't know, windows and
    controls it can't find, controls that can't be used as asked, and values
    that don't fit them."""
    with audio_devices():
        with freedvtest.start(settings=settings()) as app:
            app.wait_for_window("main")

            # A command that doesn't exist, and lines that aren't JSON
            # objects at all: half-broken JSON, and valid JSON that isn't an
            # object. Neither has an id to reply to.
            check_equal(app.command("ping"), None, "ping's result")
            expect_error(app, "unknown command 'not_a_real_command'", "not_a_real_command")
            expect_error(app, "'hold_on_off' needs a boolean 'hold'", "hold_on_off")
            expect_error(app, "'hold_on_off' needs a boolean 'hold'", "hold_on_off", hold="yes")
            for request in ['{"id": 1, "cmd": "ping"', '["ping"]']:
                reply = app.raw_request(request)
                check_equal(reply["ok"], False, "%r was accepted" % request)
                check_equal(reply["error"], "request is not a JSON object", "%r error" % request)
                check_equal(reply["id"], None, "%r reply's id" % request)

            # Commands that name a window: none at all, and one that isn't
            # there.
            expect_error(app, "no window name given", "get")
            expect_error(app, "no window named 'main.noSuchControl'", "get", name="main.noSuchControl")

            # The main window's controls. The tune button is disabled until
            # FreeDV is started, and the SNR readout is a label rather than
            # something to click, to close or to bring to the front.
            expect_error(app, "'main.btnTune' is disabled", "click", name="main.btnTune")
            expect_error(app, "'main.textSNR' cannot be clicked", "click", name="main.textSNR")
            expect_error(app, "'main.textSNR' is not a top-level window", "close", name="main.textSNR")
            expect_error(app, "'main.textSNR' is not a top-level window", "activate", name="main.textSNR")
            expect_error(app, "'main.textSNR' is not a list with columns", "data_view",
                         name="main.textSNR", action="hover", row=0)
            expect_error(app, "'main.btnOnOff' has no list to open", "combo_popup", name="main.btnOnOff", open=True)

            # Values of the wrong kind, and requests that name something the
            # control has no idea what to do with.
            expect_error(app, "the value of 'main.gaugeSNR' cannot be set", "set_value",
                         name="main.gaugeSNR", value=5)
            expect_error(app, "'main.cboReportFrequency' needs a string value", "set_value",
                         name="main.cboReportFrequency", value=12)
            expect_error(app, "'main.ckboxSNR' needs a boolean value", "set_value",
                         name="main.ckboxSNR", value="yes")
            expect_error(app, "unknown key 'NoSuchKey'", "key", name="main.notebook", key="NoSuchKey")
            expect_error(app, "unknown mouse event 'wiggle'", "mouse", name="main.btnOnOff", event="wiggle")
            expect_error(app, "no menu item 'Settings/No Such Item'", "menu", path="Settings/No Such Item")

            # The same kind of request when it can be carried out.
            app.key("main.notebook", "Escape")
            app.mouse("main.gaugeSNR", "motion")
            app.set_value("main.ckboxSNR", True)
            check_equal(app.get("main.ckboxSNR")["value"], True, "Slow SNR checkbox")
            app.set_value("main.ckboxSNR", False)

            # The FreeDV Reporter window's list, which has no rows here: no
            # action, no column and no row can be found in it.
            app.menu("Tools/FreeDV Reporter")
            app.wait_for_window("reporter")
            app.activate("reporter")
            expect_error(app, "unknown list action 'wiggle'", "data_view", name=SPOTS, action="wiggle", row=0)
            expect_error(app, "'%s' is not displaying the requested column" % SPOTS, "data_view",
                         name=SPOTS, action="hover", row=0, column=999)
            expect_error(app, "'%s' has no such row" % SPOTS, "data_view", name=SPOTS, action="hover", row=0)

            # The status message combo does have a list to open.
            app.combo_popup("reporter.cbStatusMessage", True)
            app.combo_popup("reporter.cbStatusMessage", False)

            # With the Settings window open, nothing else on screen can be
            # used, however sensible the request for it is: the user would
            # have to close Settings first. A dialog also has no menu bar of
            # its own to take a path from.
            app.menu("Settings/Edit Settings...")
            app.wait_for_window("settings")
            for cmd, window, params in [
                ("click", "main.btnOnOff", {}),
                ("set_value", "main.ckboxSNR", {"value": True}),
                ("close", "reporter", {}),
                ("activate", "reporter", {}),
                ("data_view", SPOTS, {"action": "hover", "row": 0}),
                ("combo_popup", "reporter.cbStatusMessage", {"open": True}),
                ("context_menu", "reporter.cbBandFilter", {}),
                ("focus", "reporter.cbBandFilter", {}),
                ("key", SPOTS, {"key": "Escape"}),
                ("mouse", SPOTS, {"event": "motion"}),
            ]:
                expect_error(app, "'%s' cannot be used while 'settings' is open" % window, cmd,
                             name=window, **params)
            expect_error(app, "that window has no menu bar", "menu", path="File/Exit", name="settings")

            # The notebook's pages, by title.
            expect_error(app, "'settings.notebook' has no such page", "set_value",
                         name="settings.notebook", value="No Such Tab")

            # The audio device lists: one is a list of rows that can only be
            # selected, the other shows what's selected.
            go_to_tab(app, "RX Audio")
            expect_error(app, "'settings.tcSoundCard1InDevice' is read-only", "set_value",
                         name="settings.tcSoundCard1InDevice", value="none")
            expect_error(app, "'settings.lcSoundCard1InDevice' has no such item", "set_value",
                         name="settings.lcSoundCard1InDevice", index=999)
            check(app.get("settings.lcSoundCard1InDevice")["items"], "No radio input devices listed")
            app.set_value("settings.lcSoundCard1InDevice", index=0)
            check_equal(app.get("settings.lcSoundCard1InDevice")["selection"], 0, "Radio input device chosen")

            # A choice with three items, asked for in every way that doesn't
            # match one of them, and then in one that does.
            go_to_tab(app, "Display")
            averaging = "settings.cbxNumSpectrumAveraging"
            expect_error(app, "'%s' has no item 999" % averaging, "set_value", name=averaging, index=999)
            expect_error(app, "'%s' has no item with data 'no such data'" % averaging, "set_value",
                         name=averaging, data="no such data")
            expect_error(app, "'%s' has no item labeled '9'" % averaging, "set_value", name=averaging, value="9")
            expect_error(app, "'%s' needs a value, data or index" % averaging, "set_value", name=averaging)
            app.set_value(averaging, index=1)
            check_equal(app.get(averaging)["value"], "2", "Spectrum averaging chosen by position")

            # A colour that isn't one, and one that is.
            colour = "settings.freedvReporterTxBackgroundColor"
            expect_error(app, "'%s' needs a color such as \"#FF0000\"" % colour, "set_value",
                         name=colour, value="not a colour")
            app.set_value(colour, value="#FF0000")
            check_equal(app.get(colour)["value"], "#FF0000", "Reporter TX background colour")

            # A text field given a number instead of text.
            go_to_tab(app, "Reporting")
            expect_error(app, "'settings.txtCtrlCsvLogFilePath' needs a string value", "set_value",
                         name="settings.txtCtrlCsvLogFilePath", value=12)

            app.click("settings.sdbSizer5Cancel")
            app.wait_for_window_closed("settings")

            app.click("reporter.btnClose")
            app.wait_for_window_closed("reporter")

            # The speaker level slider, usable once the modem is running,
            # and only within the range it was made with.
            app.click("main.btnOnOff")
            app.wait_until(lambda: app.state()["running"], description="FreeDV to start")
            app.wait_until(lambda: app.get("main.sliderMicSpkrLevel")["enabled"],
                           timeout=15, description="the speaker level slider to be enabled")
            expect_error(app, "'main.sliderMicSpkrLevel' needs a value from -200 to 200", "set_value",
                         name="main.sliderMicSpkrLevel", value=999)
            app.set_value("main.sliderMicSpkrLevel", -60)
            check_equal(app.get("main.sliderMicSpkrLevel")["value"], -60, "Speaker level")

            app.click("main.btnOnOff")
            app.wait_until(lambda: not app.state()["running"], description="FreeDV to stop")
            check_equal(app.quit(), 0, "FreeDV's exit code")


SCENARIOS = {
    "errors": test_errors,
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
