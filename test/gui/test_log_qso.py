#!/usr/bin/env python3
"""Tests logging contacts from FreeDV: the Log QSO button, the window it
opens and what's sent to the logging program as a result.

Each scenario below launches its own copy (or copies) of FreeDV. With no
arguments all of them are run; otherwise, only those named:

    python3 test_log_qso.py [scenario...]

Run from the build folder (or set FREEDV_BINARY) with a window manager
running, on Linux or macOS: see freedvtest/audio.py for the audio devices it
needs. The "reporter" scenario also needs what test_reporter.py does.
"""

import calendar
import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import freedvtest
import freedvtest.audio
from freedvtest.wsjtx import WsjtxListener, utc
from test_main_window import (CALLSIGN, audio_devices, check, check_equal, label, quit_and_check_log, settings,
                              start_modem, stop_modem)

GRID_SQUARE = "DM12"
FREQUENCY = 14236000

CALLSIGN_LIST = "main.listLastReportedCallsigns"
FIELDS = ["dxCall", "dxLocator", "frequency", "txReport", "rxReport", "name", "comments"]


def logging_settings(logger, **extra):
    """Returns settings for FreeDV to send contacts to the given listener."""
    return settings(Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare=GRID_SQUARE,
                    Reporting__Frequency=FREQUENCY, Reporting__ManualFrequencyReporting=True,
                    Reporting__UDP__Enable=True, Reporting__UDP__Hostname="127.0.0.1",
                    Reporting__UDP__Port=logger.port, **extra)


def number(text):
    """Returns a number as FreeDV displays it, without the separators that
    some languages put between thousands."""
    return text.replace(",", "")


def open_log_entry(app):
    app.click("main.btnLogQSO")
    return app.wait_for_window("logEntry")


def log_entry(app):
    """Returns what's in the window's fields."""
    return {field: app.get("logEntry." + field)["value"] for field in FIELDS}


def fill_in(app, **values):
    for field, value in values.items():
        app.set_value("logEntry." + field, value)


def confirm(app):
    app.click("logEntry.btnOK")
    app.wait_for_window_closed("logEntry")


def logged(logger):
    """Waits for a contact to be logged and returns everything that was sent
    for it: the status and (if there was one) decode that go before it, and
    the contact itself. Nothing is left over for the next one."""
    contact = logger.wait_for(WsjtxListener.QSO_LOGGED)
    status = logger.messages(WsjtxListener.STATUS)
    decodes = logger.messages(WsjtxListener.DECODE)
    contacts = logger.messages(WsjtxListener.QSO_LOGGED, clear=True)
    check_equal(len(contacts), 1, "Number of contacts logged")
    check_equal(len(status), 1, "Number of status messages sent with a contact")
    check_equal(logger.errors, [], "Messages from FreeDV that couldn't be read")
    return status[0], decodes, contact


def check_nothing_logged(logger, what):
    time.sleep(0.5)
    check_equal(logger.messages(WsjtxListener.QSO_LOGGED), [], what)
    check_equal(logger.messages(WsjtxListener.STATUS), [], what + " (status)")


def write_csv_log(path, rows):
    """Creates the file that FreeDV fills its list of callsigns from on
    startup. Rows are (date and time, callsign, frequency in Hz, SNR)."""
    with open(path, "w") as f:
        f.write("date,time,callsign,mode,frequency_hz,snr_db\n")
        for moment, callsign, frequency, snr in rows:
            f.write("%s,%s,%s,RADEV1,%d,%d\n" % (moment[:10], moment[11:], callsign, frequency, snr))


def choose_callsign(app, row):
    """Picks a row of the list of callsigns heard, as the mouse would."""
    app.combo_popup("main.cboLastReportedCallsigns", open=True)
    app.wait_until(lambda: app.get(CALLSIGN_LIST)["shown"], description="the list of callsigns to open")
    app.click(CALLSIGN_LIST, item=row)
    app.wait_until(lambda: not app.get(CALLSIGN_LIST)["shown"], description="the list of callsigns to close")


# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------

def test_manual():
    """Logging a contact that's typed in from scratch."""
    with WsjtxListener() as logger, audio_devices():
        with freedvtest.start(settings=logging_settings(logger)) as app:
            app.wait_for_window("main")
            check_equal(app.get("main.btnLogQSO")["enabled"], False, "Log QSO enabled before starting")
            check_equal(logger.messages(), [], "Sent to the logging program before starting")

            # The logging program hears from FreeDV once it's running.
            start_modem(app)
            heartbeat = logger.wait_for(WsjtxListener.HEARTBEAT)
            check_equal(heartbeat["id"], "FreeDV", "Who the heartbeat is from")
            check(heartbeat["version"] != "", "The heartbeat has no version")
            check_equal(app.get("main.btnLogQSO")["enabled"], True, "Log QSO enabled once running")

            # With nobody chosen, only what FreeDV knows itself is filled in.
            dialog = open_log_entry(app)
            check_equal(dialog["label"], "Confirm Log Entry", "Window title")
            check_equal(label(app, "logEntry.myCall"), CALLSIGN, "Your Call")
            check_equal(label(app, "logEntry.myLocator"), GRID_SQUARE, "Your Locator")
            check_equal(label(app, "logEntry.labelFrequency"), "Frequency (MHz):", "Frequency's label")
            check_equal(log_entry(app), dict(dxCall="", dxLocator="", frequency="14.2360", txReport="59",
                                             rxReport="59", name="", comments=""), "Fields to begin with")
            shown = label(app, "logEntry.time")
            now = time.time()
            check(shown in [utc(now - seconds)[11:] for seconds in range(0, 30)],
                  "Time is %r at %s UTC" % (shown, utc(now)[11:]))

            # It needs to know who was worked...
            app.click("logEntry.btnOK")
            check_equal([box["message"] for box in app.message_boxes(clear=True)],
                        ["Frequency and DX Call must be filled in in order to log the contact."],
                        "Message displayed without a DX call")
            check(app.window_exists("logEntry"), "The window closed without a DX call")

            # ...and where.
            fill_in(app, dxCall="K1ABC", frequency="")
            app.click("logEntry.btnOK")
            check_equal([box["caption"] for box in app.message_boxes(clear=True)], ["Error"],
                        "Message displayed without a frequency")
            check(app.window_exists("logEntry"), "The window closed without a frequency")
            check_nothing_logged(logger, "Contacts logged without what's needed")

            fill_in(app, dxLocator="FN42", frequency="7.1770", txReport="57", rxReport="55", name="Alex",
                    comments="First contact, on 40m")
            confirm(app)
            status, decodes, contact = logged(logger)
            check_equal((status["id"], status["frequency"], status["mode"], status["dx_call"], status["dx_grid"],
                         status["my_call"], status["my_grid"], status["submode"]),
                        ("FreeDV", 7177000, "DIGITALVOICE", "K1ABC", "FN42", CALLSIGN, GRID_SQUARE, "FREEDV"),
                        "Status sent with the contact")
            check_equal(decodes, [], "Signal reports sent for a contact without one")
            check_equal({key: contact[key] for key in ["id", "dx_call", "dx_grid", "frequency", "mode",
                                                       "report_sent", "report_received", "name", "comments",
                                                       "my_call", "operator_call", "my_grid"]},
                        dict(id="FreeDV", dx_call="K1ABC", dx_grid="FN42", frequency=7177000, mode="DIGITALVOICE",
                             report_sent="57", report_received="55", name="Alex", comments="First contact, on 40m",
                             my_call=CALLSIGN, operator_call=CALLSIGN, my_grid=GRID_SQUARE), "Contact logged")
            check(abs(contact["time_off"] - time.time()) < 60 and contact["time_on"] == contact["time_off"],
                  "The contact is logged at %s and %s, at %s" % (utc(contact["time_on"]), utc(contact["time_off"]),
                                                              utc(time.time())))
            check_equal(utc(contact["time_on"])[11:], shown, "Time logged")

            # Nothing's kept from one contact to the next.
            open_log_entry(app)
            check_equal(log_entry(app), dict(dxCall="", dxLocator="", frequency="14.2360", txReport="59",
                                             rxReport="59", name="", comments=""), "Fields the next time")

            # Changing one's mind, by either means.
            fill_in(app, dxCall="K1ABC")
            app.click("logEntry.btnCancel")
            app.wait_for_window_closed("logEntry")
            open_log_entry(app)
            fill_in(app, dxCall="K1ABC")
            app.close("logEntry")
            app.wait_for_window_closed("logEntry")
            check_nothing_logged(logger, "Contacts logged after cancelling")
            check_equal([window["name"] for window in app.windows() if window["name"] == "logEntry"], [], "Log entry windows left behind")

            # The frequency follows what FreeDV's reporting.
            app.set_value("main.cboReportFrequency", "7.1770")
            app.wait_until(lambda: app.get("main.cboReportFrequency")["value"] == "7.1770")
            open_log_entry(app)
            check_equal(log_entry(app)["frequency"], "7.1770", "Frequency after changing band")
            fill_in(app, dxCall="vk5dgr")
            confirm(app)
            status, decodes, contact = logged(logger)
            check_equal((contact["dx_call"], contact["dx_grid"], contact["frequency"]), ("vk5dgr", "", 7177000),
                        "Contact logged")

            stop_modem(app)
            check_equal(app.get("main.btnLogQSO")["enabled"], False, "Log QSO enabled after stopping")
            quit_and_check_log(app)


def test_frequency_in_khz():
    """The window when frequencies are shown in kHz."""
    with WsjtxListener() as logger, audio_devices():
        with freedvtest.start(settings=logging_settings(logger, Reporting__FrequencyAsKHz=True)) as app:
            app.wait_for_window("main")
            start_modem(app)

            open_log_entry(app)
            check_equal(label(app, "logEntry.labelFrequency"), "Frequency (kHz):", "Frequency's label")
            check_equal(number(log_entry(app)["frequency"]), "14236.0", "Frequency")

            # Logged as it's displayed, whatever that looks like...
            fill_in(app, dxCall="K1ABC")
            confirm(app)
            status, decodes, contact = logged(logger)
            check_equal((status["frequency"], contact["frequency"]), (FREQUENCY, FREQUENCY), "Frequency logged")

            # ...and as typed.
            open_log_entry(app)
            fill_in(app, dxCall="K1ABC", frequency="7177.5")
            confirm(app)
            status, decodes, contact = logged(logger)
            check_equal((status["frequency"], contact["frequency"]), (7177500, 7177500), "Frequency logged")

            stop_modem(app)
            quit_and_check_log(app)


def test_callsign_list():
    """Logging someone from the list of callsigns heard, which doesn't need
    FreeDV to be running."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        csv_log = os.path.join(folder, "heard.csv")
        heard = [("2026-03-04 05:06:07", "K1ABC", 7177000, 4), ("2026-03-04 21:43:02", "VK5DGR", 14236000, 11)]

        for use_utc in [False, True]:
            # The newest is listed first.
            write_csv_log(csv_log, heard)
            moment = time.strptime(heard[1][0], "%Y-%m-%d %H:%M:%S")
            heard_at = calendar.timegm(moment) if use_utc else time.mktime(moment)

            with WsjtxListener() as logger, audio_devices():
                extra = dict(Reporting__CSV__LogFilePath=csv_log, CallsignList__UseUTCTime=use_utc)
                with freedvtest.start(settings=logging_settings(logger, **extra)) as app:
                    app.wait_for_window("main")
                    check_equal(app.get("main.btnLogQSO")["enabled"], True, "Log QSO enabled with callsigns listed")
                    check_equal(app.get("main.cboLastReportedCallsigns")["value"], "", "Callsign chosen")

                    # Nobody's been chosen yet.
                    open_log_entry(app)
                    check_equal(log_entry(app)["dxCall"], "", "DX call with nobody chosen")
                    app.click("logEntry.btnCancel")
                    app.wait_for_window_closed("logEntry")

                    choose_callsign(app, 0)
                    check_equal(app.get("main.cboLastReportedCallsigns")["value"], "VK5DGR", "Callsign chosen")
                    open_log_entry(app)
                    entry = log_entry(app)
                    check_equal((entry["dxCall"], entry["dxLocator"], entry["frequency"]),
                                ("VK5DGR", "", "14.2360"), "Who's being logged")
                    check_equal(label(app, "logEntry.time"), utc(heard_at)[11:],
                                "Time (heard at %s %s)" % (heard[1][0], "UTC" if use_utc else "local time"))
                    confirm(app)

                    status, decodes, contact = logged(logger)
                    check_equal((status["dx_call"], status["frequency"]), ("VK5DGR", 14236000), "Status sent")
                    check_equal([decode["snr"] for decode in decodes], [11], "Signal reports sent")
                    check_equal((contact["dx_call"], contact["frequency"], utc(contact["time_on"]),
                                 utc(contact["time_off"])),
                                ("VK5DGR", 14236000, utc(heard_at), utc(heard_at)), "Contact logged")

                    # Someone else
                    choose_callsign(app, 1)
                    check_equal(app.get("main.cboLastReportedCallsigns")["value"], "K1ABC", "Callsign chosen")
                    open_log_entry(app)
                    entry = log_entry(app)
                    check_equal((entry["dxCall"], entry["frequency"]), ("K1ABC", "7.1770"), "Who's being logged")
                    confirm(app)
                    status, decodes, contact = logged(logger)
                    check_equal((contact["dx_call"], contact["frequency"], [decode["snr"] for decode in decodes]),
                                ("K1ABC", 7177000, [4]), "Contact logged")

                    # Right-clicking in the list chooses nobody again.
                    app.combo_popup("main.cboLastReportedCallsigns", open=True)
                    app.wait_until(lambda: app.get(CALLSIGN_LIST)["shown"])
                    app.click(CALLSIGN_LIST, item=1, button="right")
                    app.wait_until(lambda: not app.get(CALLSIGN_LIST)["shown"])
                    check_equal(app.get(CALLSIGN_LIST)["selection"], -1, "Row selected after right-clicking")
                    open_log_entry(app)
                    check_equal(log_entry(app)["dxCall"], "", "DX call after right-clicking")
                    app.click("logEntry.btnCancel")
                    app.wait_for_window_closed("logEntry")
                    check_nothing_logged(logger, "Contacts logged after cancelling")

                    # Clearing the list leaves nobody to choose.
                    choose_callsign(app, 0)
                    app.click("main.btnCallSignReset")
                    check_equal(app.get(CALLSIGN_LIST)["items"], [], "Callsigns listed after clearing")
                    check_equal(app.get("main.cboLastReportedCallsigns")["value"], "", "Callsign chosen")
                    # Choosing nobody from the emptied list, the way the mouse
                    # would: opening it, moving over it and clicking in it.
                    app.combo_popup("main.cboLastReportedCallsigns", open=True)
                    app.wait_until(lambda: app.get(CALLSIGN_LIST)["shown"], description="the empty list to open")
                    app.mouse(CALLSIGN_LIST, "motion", x=0.1, y=0.9)
                    app.mouse(CALLSIGN_LIST, "left_up", x=0.1, y=0.9)
                    app.mouse(CALLSIGN_LIST, "right_up", x=0.1, y=0.9)
                    app.wait_until(lambda: not app.get(CALLSIGN_LIST)["shown"], description="the list to close")
                    check_equal(app.get(CALLSIGN_LIST)["selection"], -1, "Nobody chosen from the empty list")
                    open_log_entry(app)
                    check_equal(log_entry(app)["dxCall"], "", "DX call after clearing the list")
                    app.click("logEntry.btnCancel")
                    app.wait_for_window_closed("logEntry")

                    quit_and_check_log(app)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_no_udp():
    """Without UDP reporting there's no logging program to send a contact
    to, so Log QSO stays off, on the air or off it, even with callsigns
    heard."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        csv_log = os.path.join(folder, "heard.csv")
        with open(csv_log, "w") as f:
            f.write("date,time,callsign,mode,frequency_hz,snr_db\n")
            f.write("2026-03-04,05:06:07,K1ABC/P,RADEV1,14236000,7\n")
        with audio_devices():
            with freedvtest.start(settings=settings(
                    Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare=GRID_SQUARE,
                    Reporting__Frequency=FREQUENCY, Reporting__ManualFrequencyReporting=True,
                    Reporting__CSV__LogFilePath=csv_log,
                    Reporting__UDP__Enable=False)) as app:
                app.wait_for_window("main")
                start_modem(app)
                check_equal(app.get("main.btnLogQSO")["enabled"], False,
                            "Log QSO enabled while on the air without a logging program")
                stop_modem(app)
                check_equal(app.get("main.btnLogQSO")["enabled"], False,
                            "Log QSO enabled off the air without a logging program")

                quit_and_check_log(app)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_reporter():
    """Logging someone chosen in the FreeDV Reporter window."""
    from freedvtest.reporter import ReporterServer

    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    with ReporterServer() as server, WsjtxListener() as logger, audio_devices():
        k1abc = server.station("K1ABC", "FN42")
        k1abc.set_frequency(7177000)

        # Two who were heard earlier, only one of whom is on FreeDV Reporter.
        csv_log = os.path.join(folder, "heard.csv")
        write_csv_log(csv_log, [("2026-03-04 05:06:07", "K1ABC", 7177000, 4),
                                ("2026-03-04 21:43:02", "ZL9ZZ", 14236000, 11)])
        extra = dict(Reporting__FreeDV__Enable=True, Reporting__FreeDV__Hostname=server.hostname,
                     Reporting__FreeDV__UseTls=False, Reporting__CSV__LogFilePath=csv_log)
        with freedvtest.start(settings=logging_settings(logger, **extra)) as app:
            app.wait_for_window("main")
            app.menu("Tools/FreeDV Reporter")
            app.wait_for_window("reporter")
            app.activate("reporter")
            start_modem(app)
            app.activate("reporter")

            def callsigns():
                return [row[0] for row in app.rows("reporter.listSpots")]

            app.wait_until(lambda: "K1ABC" in callsigns(), timeout=15, description="K1ABC to be listed")
            app.select_row("reporter.listSpots", (0, "K1ABC"))

            open_log_entry(app)
            check_equal(log_entry(app), dict(dxCall="K1ABC", dxLocator="FN42", frequency="7.1770", txReport="59",
                                             rxReport="59", name="", comments=""), "Fields for K1ABC")
            confirm(app)
            status, decodes, contact = logged(logger)
            check_equal((status["dx_call"], status["dx_grid"], status["frequency"]), ("K1ABC", "FN42", 7177000),
                        "Status sent")
            check_equal(decodes, [], "Signal reports sent")
            check_equal((contact["dx_call"], contact["dx_grid"], contact["frequency"]), ("K1ABC", "FN42", 7177000),
                        "Contact logged")
            check(abs(contact["time_on"] - time.time()) < 60, "The contact is logged at %s" % utc(contact["time_on"]))

            # Someone chosen from the list of callsigns heard instead has
            # their locator looked up there, if that's where they are.
            for row, callsign, locator in [(1, "K1ABC", "FN42"), (0, "ZL9ZZ", "")]:
                app.activate("main")
                choose_callsign(app, row)
                check_equal(app.get("main.cboLastReportedCallsigns")["value"], callsign, "Callsign chosen")
                open_log_entry(app)
                entry = log_entry(app)
                check_equal((entry["dxCall"], entry["dxLocator"]), (callsign, locator), "Who's being logged")
                app.click("logEntry.btnCancel")
                app.wait_for_window_closed("logEntry")
            app.activate("reporter")
            app.select_row("reporter.listSpots", (0, "K1ABC"))

            # With the chosen one filtered out of the list, logging falls
            # back to the frequency being reported.
            app.set_value("reporter.cbBandFilter", "20 m")
            app.wait_until(lambda: all(row[0] != "K1ABC" for row in app.rows("reporter.listSpots")),
                           timeout=5, description="K1ABC to be filtered out")
            open_log_entry(app)
            entry = log_entry(app)
            check_equal((entry["dxCall"], entry["dxLocator"], entry["frequency"]),
                        ("", "", "14.2360"), "Who's being logged with nobody chosen")
            app.click("logEntry.btnCancel")
            app.wait_for_window_closed("logEntry")
            check_nothing_logged(logger, "Contacts logged with nobody chosen")
            app.set_value("reporter.cbBandFilter", "All")
            app.wait_until(lambda: "K1ABC" in [row[0] for row in app.rows("reporter.listSpots")],
                           timeout=5, description="K1ABC to be listed again")
            app.select_row("reporter.listSpots", (0, "K1ABC"))

            # They're also who a recording started now is named after.
            app.click("main.btnRecord")
            app.wait_for_window("beginRecording")
            check_equal(app.get("beginRecording.recordingSuffix")["value"], "K1ABC", "Recording suffix")
            app.click("beginRecording.btnCancel")
            app.wait_for_window_closed("beginRecording")

            stop_modem(app)
            quit_and_check_log(app)
    shutil.rmtree(folder, ignore_errors=True)


def test_selected_khz():
    """Logging someone chosen from the list of callsigns heard when
    frequencies are shown in kHz, with no FreeDV Reporter around to fill in
    their grid."""
    folder = tempfile.mkdtemp(prefix="freedvtest-files-")
    try:
        csv_log = os.path.join(folder, "heard.csv")
        write_csv_log(csv_log, [("2026-03-04 05:06:07", "K1ABC", 7177000, 4),
                                ("2026-03-04 21:43:02", "VK5DGR", 14236000, 11)])
        with WsjtxListener() as logger, audio_devices():
            extra = dict(Reporting__CSV__LogFilePath=csv_log, Reporting__FrequencyAsKHz=True,
                         Reporting__FreeDV__Hostname="")
            with freedvtest.start(settings=logging_settings(logger, **extra)) as app:
                app.wait_for_window("main")
                choose_callsign(app, 0)
                open_log_entry(app)
                entry = log_entry(app)
                check_equal((entry["dxCall"], entry["dxLocator"], number(entry["frequency"])),
                            ("VK5DGR", "", "14236.0"), "Who's being logged")
                confirm(app)
                status, decodes, contact = logged(logger)
                check_equal((contact["dx_call"], contact["dx_grid"], contact["frequency"]),
                            ("VK5DGR", "", 14236000), "Contact logged")
                check_equal(logger.messages(WsjtxListener.QSO_LOGGED, clear=True), [], "Extra contacts logged")
                quit_and_check_log(app)
    finally:
        shutil.rmtree(folder, ignore_errors=True)



SCENARIOS = {
    "manual": test_manual,
    "frequency_in_khz": test_frequency_in_khz,
    "callsign_list": test_callsign_list,
    "reporter": test_reporter,
    "selected_khz": test_selected_khz,
    "no_udp": test_no_udp,
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
