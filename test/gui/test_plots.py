#!/usr/bin/env python3
"""Tests the plots in FreeDV's main window: tuning by clicking on, scrolling
over and pressing keys in the waterfall and spectrum, and each plot being
drawn while there's something to show.

Each scenario below launches its own copy of FreeDV. With no arguments all of
them are run; otherwise, only those named:

    python3 test_plots.py [scenario...]

Run from the build folder (or set FREEDV_BINARY) with a window manager
running, on Linux or macOS: see freedvtest/audio.py for the audio devices it
needs.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import freedvtest
import freedvtest.audio
from freedvtest.audio import Loopback, Tone
from test_main_window import (CALLSIGN, MICROPHONE, RADIO_RX, RADIO_TX, TABS, audio_devices, check, check_equal,
                              quit_and_check_log, settings, start_modem, stop_modem, wait_for_transmit)

# What's tuned to when nothing's been done, and the range that can be.
CENTRE = 1500
LOWEST = 0
HIGHEST = 3000

PLOTS = {
    "Waterfall": "main.plotWaterfall",
    "Spectrum": "main.plotSpectrum",
    "Frm Radio": "main.plotFromRadio",
    "Frm Mic": "main.plotFromMic",
    "Frm Decoder": "main.plotFromDecoder",
    "SNR": "main.plotSNR",
}


def tuned_to(app):
    """Returns the frequency FreeDV is tuned to within what it hears, having
    checked that transmit follows receive."""
    state = app.state()
    check_equal(state["txFreqOffsetHz"], -state["rxFreqOffsetHz"], "Transmit offset, given the receive offset")
    return CENTRE - state["rxFreqOffsetHz"]


def show(app, tab):
    app.set_value("main.notebook", tab)
    app.wait_until(lambda: app.get(PLOTS[tab])["shown"], description="the %s plot to be displayed" % tab)
    time.sleep(0.3)


# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------

def test_tuning():
    """The waterfall and spectrum's ways of choosing where to tune."""
    with audio_devices():
        with freedvtest.start(settings=settings()) as app:
            app.wait_for_window("main")
            start_modem(app)
            check_equal(tuned_to(app), CENTRE, "Tuned to, to begin with")

            for tab in ["Waterfall", "Spectrum"]:
                plot = PLOTS[tab]
                show(app, tab)

                # Double-clicking, with either button: further right is higher.
                tuned = []
                for event, x in [("left_dclick", 0.25), ("left_dclick", 0.4), ("right_dclick", 0.6),
                                 ("right_dclick", 0.9)]:
                    app.mouse(plot, event, x=x, y=0.5)
                    tuned.append(tuned_to(app))
                check(LOWEST < tuned[0] < tuned[1] < CENTRE < tuned[2] < tuned[3] < HIGHEST,
                      "Double-clicking across the %s tuned to %r" % (tab, tuned))
                check(all(frequency % 10 == 0 for frequency in tuned),
                      "Double-clicking on the %s tuned to %r, which aren't all multiples of 10 Hz" % (tab, tuned))
                check(900 < tuned[3] - tuned[0] < 2800, "The %s spans %r" % (tab, tuned))

                # Beside the plot itself, in any direction, nothing happens.
                for x, y in [(0.001, 0.5), (0.999, 0.5), (0.3, 0.002)]:
                    app.mouse(plot, "left_dclick", x=x, y=y)
                    check_equal(tuned_to(app), tuned[3], "Tuned to after double-clicking beside the %s" % tab)

                # The wheel and the arrow keys move 10 Hz at a time.
                for action, expected in [(lambda: app.mouse(plot, "wheel", rotation=1), 10),
                                         (lambda: app.mouse(plot, "wheel", rotation=1), 20),
                                         (lambda: app.mouse(plot, "wheel", rotation=-1), 10),
                                         (lambda: app.key(plot, "Up"), 20),
                                         (lambda: app.key(plot, "Down"), 10),
                                         (lambda: app.key(plot, "Down"), 0),
                                         (lambda: app.key(plot, "Left"), 0),
                                         (lambda: app.key(plot, "A"), 0)]:
                    action()
                    check_equal(tuned_to(app), tuned[3] + expected, "Tuned to on the %s" % tab)

                # The middle button goes back to the middle.
                app.mouse(plot, "middle_down")
                check_equal(tuned_to(app), CENTRE, "Tuned to after middle-clicking the %s" % tab)

                # Just either side of the middle counts as the middle.
                app.mouse(plot, "wheel", rotation=1)
                check_equal(tuned_to(app), CENTRE + 10, "Tuned to one notch up from the middle")
                app.mouse(plot, "wheel", rotation=-1)
                check_equal(tuned_to(app), CENTRE, "Tuned to back at the middle")

                # There's no going past either end. (Not too near the left
                # edge, which can be the frequency axis's labels instead.)
                for rotation, limit in [(1, HIGHEST), (-1, LOWEST)]:
                    app.mouse(plot, "left_dclick", x=0.95 if rotation > 0 else 0.25, y=0.5)
                    previous = None
                    for notch in range(200):
                        app.mouse(plot, "wheel", rotation=rotation)
                        now = tuned_to(app)
                        if now == previous:
                            break
                        previous = now
                    check_equal(previous, limit, "Furthest that the %s's wheel goes" % tab)

                # The arrow keys stop at either end as well. (Again not too
                # near the left edge: on macOS the Spectrum's labels take up
                # more than its first 12%.)
                for key, x, limit in [("Up", 0.95, HIGHEST), ("Down", 0.25, LOWEST)]:
                    app.mouse(plot, "left_dclick", x=x, y=0.5)
                    previous = None
                    for press in range(200):
                        app.key(plot, key)
                        now = tuned_to(app)
                        if now == previous:
                            break
                        previous = now
                    check_equal(previous, limit, "Furthest that the %s key goes on the %s" % (key, tab))
                app.mouse(plot, "middle_down")

                # Other things the mouse does there are harmless.
                for event in ["motion", "left_down", "left_up", "right_down", "right_up"]:
                    app.mouse(plot, event, x=0.3, y=0.3)
                check_equal(tuned_to(app), CENTRE, "Tuned to after moving and clicking on the %s" % tab)

            # Where it's tuned to is kept when stopped and started again.
            show(app, "Waterfall")
            app.mouse(PLOTS["Waterfall"], "left_dclick", x=0.7, y=0.5)
            tuned = tuned_to(app)
            check(tuned > CENTRE, "Double-clicking the waterfall didn't tune anywhere")
            stop_modem(app)
            start_modem(app)
            check_equal(tuned_to(app), tuned, "Tuned to after starting again")

            stop_modem(app)
            quit_and_check_log(app)


def test_tuned_away():
    """Tuning away from a signal loses it, and tuning back finds it again."""
    with audio_devices(), Loopback(RADIO_TX, RADIO_RX):
        with freedvtest.start(settings=settings(Rig__HalfDuplex=False)) as app:
            app.wait_for_window("main")
            start_modem(app)
            app.click("main.btnPTT")
            wait_for_transmit(app)
            app.wait_until(lambda: app.state()["sync"] == 1, timeout=20, description="FreeDV to sync")
            show(app, "Waterfall")  # (transmitting shows what's coming from the microphone instead)

            # Transmit moves with receive, so FreeDV still hears itself in
            # the same place after tuning both.
            app.mouse(PLOTS["Waterfall"], "left_dclick", x=0.7, y=0.5)
            check(tuned_to(app) > CENTRE + 100, "Double-clicking the waterfall tuned to %d" % tuned_to(app))
            time.sleep(4)
            app.wait_until(lambda: app.state()["sync"] == 1, timeout=20,
                           description="FreeDV to sync having tuned transmit and receive together")

            app.mouse(PLOTS["Waterfall"], "middle_down")
            time.sleep(4)
            app.wait_until(lambda: app.state()["sync"] == 1, timeout=20, description="FreeDV to sync back at the middle")

            app.click("main.btnPTT")
            wait_for_transmit(app, False)
            stop_modem(app)
            quit_and_check_log(app)


def test_drawing():
    """Each plot, while there's something for it to show: FreeDV receiving
    its own transmission of what's said into the microphone."""
    reporting = dict(Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare="DM12")
    with audio_devices(), Loopback(RADIO_TX, RADIO_RX):
        with freedvtest.start(settings=settings(Rig__HalfDuplex=False, **reporting)) as app:
            app.wait_for_window("main")

            # Before starting, there's nothing to show but they're there.
            for tab in TABS:
                show(app, tab)

            start_modem(app)
            with Tone(MICROPHONE):
                app.click("main.btnPTT")
                wait_for_transmit(app)
                app.wait_until(lambda: app.state()["sync"] == 1, timeout=20, description="FreeDV to sync")

                # A good look at each, with the mouse over it.
                for tab in TABS:
                    show(app, tab)
                    plot = PLOTS[tab]
                    for step in range(6):
                        app.mouse(plot, "motion", x=0.2 + 0.1 * step, y=0.5)
                        time.sleep(0.4)
                    app.mouse(plot, "wheel", rotation=1)
                    app.mouse(plot, "wheel", rotation=-1)
                    check_equal(app.state()["running"], True, "Running while showing " + tab)
                check_equal(app.state()["sync"], 1, "Sync after looking at each plot")

                # And over to receive with one of them showing.
                show(app, "Frm Mic")
                app.click("main.btnPTT")
                wait_for_transmit(app, False)
                show(app, "SNR")
                time.sleep(1)

            stop_modem(app)
            # Stopped, they're still there to be looked at.
            for tab in TABS:
                show(app, tab)
            quit_and_check_log(app)


def test_waterfall_colours():
    """The waterfall's other colour schemes, and tuning across it with the
    arrow keys as far as it goes.

    What's drawn can't be seen from here: this is FreeDV drawing a quiet
    and a loud signal in each scheme without anything going wrong."""
    plot = PLOTS["Waterfall"]
    with audio_devices():
        for scheme in (1, 2, 3):
            with freedvtest.start(settings=settings(Waterfall__Color=scheme)) as app:
                app.wait_for_window("main")
                start_modem(app)
                show(app, "Waterfall")
                time.sleep(1)
                with Tone(RADIO_RX):
                    time.sleep(2)
                    check_equal(app.state()["running"], True, "Running while drawing in colour scheme %d" % scheme)

                    # The arrow keys stop at either end.
                    for key, x, limit in [("Up", 0.95, HIGHEST), ("Down", 0.25, LOWEST)]:
                        app.mouse(plot, "left_dclick", x=x, y=0.5)
                        previous = None
                        for press in range(200):
                            app.key(plot, key)
                            now = tuned_to(app)
                            if now == previous:
                                break
                            previous = now
                        check_equal(previous, limit, "Furthest that the %s key goes" % key)

                    # Held at the bottom long enough for the periodic
                    # redraws to catch up with where it's tuned.
                    check_equal(tuned_to(app), LOWEST, "Held at the bottom")
                    time.sleep(1)
                    app.mouse(plot, "middle_down")
                time.sleep(1)
                stop_modem(app)
                quit_and_check_log(app)
                check_equal(int(freedvtest.read_config(app.config_path)["/Waterfall/Color"]), scheme,
                            "Colour scheme saved")

def test_spectrum_averaging():
    """The spectrum's averaging settings other than the default one: what
    it shows over the last few redraws instead of just the latest."""
    with audio_devices():
        for averaging in (1, 2):
            with freedvtest.start(settings=settings(Plot__Spectrum__CurrentAveraging=averaging)) as app:
                app.wait_for_window("main")
                start_modem(app)
                show(app, "Spectrum")
                time.sleep(2)
                check_equal(app.state()["running"], True,
                            "Running with averaging set to %d" % averaging)
                stop_modem(app)
                quit_and_check_log(app)
                check_equal(int(freedvtest.read_config(app.config_path)["/Plot/Spectrum/CurrentAveraging"]),
                            averaging, "Averaging saved")


def test_sized_plots():
    """The plots at window sizes where they have to fit fewer pixels' worth
    into the space they're given: shorter waterfall columns, and a narrow
    spectrum whose bins no longer all get drawn.

    Consecutive heights are launched so that one of them divides the
    waterfall's slices exactly."""
    plot = PLOTS["Waterfall"]
    with audio_devices():
        for height in (600, 601, 602):
            with freedvtest.start(settings=settings(MainFrame__height=height)) as app:
                app.wait_for_window("main")
                start_modem(app)
                show(app, "Waterfall")
                time.sleep(20)
                check_equal(app.state()["running"], True,
                            "Running with a %d-high window" % height)
                app.mouse(plot, "middle_down")  # back to the middle, whatever the plot's size
                check_equal(tuned_to(app), CENTRE, "Tuned to in a %d-high window" % height)
                stop_modem(app)
                quit_and_check_log(app)

        for width in (700, 660, 620):
            with freedvtest.start(settings=settings(MainFrame__width=width)) as app:
                app.wait_for_window("main")
                start_modem(app)
                show(app, "Spectrum")
                time.sleep(2)
                check_equal(app.state()["running"], True,
                            "Running with a %d-wide window" % width)
                app.mouse(PLOTS["Spectrum"], "middle_down")
                check_equal(tuned_to(app), CENTRE, "Tuned to in a %d-wide window" % width)
                stop_modem(app)
                quit_and_check_log(app)

def test_translated_layout():
    """Running in a language FreeDV is translated into. Labels, messages and
    layout all go through the translation machinery, which only switches to
    a translated layout when the language is one FreeDV actually has."""
    binary = os.path.abspath(freedvtest.default_binary())
    build_dir = os.path.dirname(os.path.dirname(binary))
    if not os.path.exists(os.path.join(build_dir, "share", "locale", "pt_BR", "LC_MESSAGES", "freedv.mo")):
        print("SKIPPED: no compiled Brazilian Portuguese translations in %s" % build_dir)
        sys.exit(77)
    # FreeDV looks for its translations beside the folder it's in, which
    # isn't where the build tree keeps them.
    share = os.path.join(os.path.dirname(binary), "share")
    if not os.path.exists(share) and os.path.exists(os.path.join(build_dir, "share")):
        os.symlink(os.path.join(build_dir, "share"), share)

    def english(label):
        """The Portuguese wording of an English menu label, as the translated
        menu bar presents it (po/pt_BR.po is what the catalog was built
        from). Anything FreeDV doesn't translate, it says in English."""
        po = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "po", "pt_BR.po")
        entry = None
        try:
            with open(po, encoding="utf-8") as messages:
                for line in messages:
                    if line.startswith('msgid "'):
                        entry = line[len('msgid "'):].rstrip('"\n')
                    elif line.startswith('msgstr "') and entry is not None:
                        translation = line[len('msgstr "'):].rstrip('"\n')
                        if entry.replace("&", "") == label and translation:
                            return translation.replace("&", "")
                        entry = None
        except OSError:
            pass
        return label

    with audio_devices():
        with freedvtest.start(settings=settings(
                Rig__TotTimerSecs=18,
                Reporting__Enable=True, Reporting__Callsign=CALLSIGN, Reporting__GridSquare="DM12",
                Reporting__UDP__Enable=True), env={"LANGUAGE": "pt_BR"}) as app:
            app.wait_for_window("main")
            start_modem(app)

            for tab, plot in PLOTS.items():  # the notebook's pages are named in Portuguese
                app.set_value("main.notebook", english(tab))
                app.wait_until(lambda plot=plot: app.get(plot)["shown"],
                               description="the plot to be displayed")
            app.wait_until(lambda: app.get("main.btnPTT")["enabled"],
                           description="transmitting to be possible, with the monitor up")

            # A volume control, which pops up over the button it adjusts.
            app.context_menu("main.btnPTT", choose=english("Monitor transmitted audio"))
            app.context_menu("main.btnPTT", choose=english("Adjust Monitor Volume..."))
            check_equal(app.get("monitorVolume")["shown"], True, "Volume control shown")
            app.close("monitorVolume")

            # The dialogs whose menu items FreeDV translates.
            app.menu("%s/%s" % (english("Settings"), english("Edit Settings...")))
            app.wait_for_window("settings")
            app.click("settings.sdbSizer5OK")
            app.wait_for_window_closed("settings")
            app.menu("%s/%s" % (english("Tools"), english("FreeDV Reporter")))
            app.wait_for_window("reporter")
            app.menu("%s/%s" % (english("Tools"), english("Filter Audio...")))
            app.wait_for_window("filter")
            app.click("filter.btnClose")
            app.wait_for_window_closed("filter")

            # And the ones it reaches from buttons, whatever language they're in.
            app.click("main.btnLogQSO")
            app.wait_for_window("logEntry")
            app.click("logEntry.btnCancel")
            app.wait_for_window_closed("logEntry")
            app.click("main.btnRecord")
            app.wait_for_window("beginRecording")
            app.click("beginRecording.btnCancel")
            app.wait_for_window_closed("beginRecording")
            app.wait_until(lambda: app.get("main.btnRecord")["enabled"],
                           description="Recording to be offered again")

            # The time-out warning, which appears near the end of a transmission
            # that has gone on too long.
            app.click("main.btnPTT")
            wait_for_transmit(app)
            app.wait_for_window("totWarning", timeout=30)
            app.click("totWarning.btnExtend")
            app.wait_for_window_closed("totWarning")
            app.click("main.btnPTT")  # end the transmission, before the timer runs out
            app.close("reporter")
            app.wait_for_window_closed("reporter")
            app.wait_until(lambda: app.get("main.btnOnOff")["enabled"],
                           description="the modem to be stoppable again")
            stop_modem(app)
            quit_and_check_log(app)


SCENARIOS = {
    "tuning": test_tuning,
    "waterfall_colours": test_waterfall_colours,
    "tuned_away": test_tuned_away,
    "spectrum_averaging": test_spectrum_averaging,
    "translated_layout": test_translated_layout,
    "sized_plots": test_sized_plots,
    "drawing": test_drawing,
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
