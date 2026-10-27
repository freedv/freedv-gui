# FreeDV GUI tests

These tests drive FreeDV the way a user would: they start it, click buttons,
fill in settings, play audio into it and listen to what comes out, and check
what happens. They run on Linux, macOS and Windows, locally and in CI.

Each `test_<area>.py` script holds the scenarios for one part of FreeDV (the
main window, Settings, FreeDV Reporter, rig control and so on). The `freedvtest`
package does the work shared between them: starting FreeDV, talking to it, and
providing the audio devices, serial ports, radio and FreeDV Reporter server that
the scenarios need.

## Running them

FreeDV has to be built with `-DUNITTEST=1`, which adds the test agent (see
below). Then, from the build folder:

```sh
ctest -L gui                                 # all of them (Linux and macOS)
ctest -L gui -R gui_reporter_                # just FreeDV Reporter's
python3 ../test/gui/test_settings.py audio   # one scenario
python3 ../test/gui/test_settings.py         # all of one script's
```

A scenario passes if its script exits with 0, and is skipped if it exits with
77: something it needs can't be had on this machine (see [Skipping](#skipping)).
Anything else is a failure, and the script prints what went wrong along with
everything FreeDV logged.

`run_tests.py` runs scenarios the way CTest does, for where there's no build
folder to run CTest in (Windows, where FreeDV is cross-compiled elsewhere):

```sh
python run_tests.py --retries 1 --exclude reporter:live_view --logs logs
python run_tests.py settings main_window:transmit
```

It runs each scenario in a process of its own, says which passed, were
skipped and failed, and prints the output of every try that failed,
including those of a scenario that then passed on a retry.

`FREEDV_BINARY` says which FreeDV to test. It's otherwise the one in the
build folder that the script was run from. Likewise `FREEDVTEST_SHIMS` says
where the small libraries some tests load into FreeDV are (`serial_shim.so`,
`pulse_shim.so`); they're otherwise in `test/gui` in the build folder, built
along with FreeDV's tests (`-DUNITTEST=1`).

`reporter:live_view` connects to the public FreeDV Reporter server, so CI
leaves it out. Everything else stays on the machine it runs on.

**The tests take over the screen.** They activate windows, move the mouse
pointer and take keyboard focus, so a machine that's running them isn't much
use for anything else meanwhile.

### What each machine needs

| | Linux | macOS | Windows |
| --- | --- | --- | --- |
| Display | an X display (Xvfb works; Wayland sessions are told to use X) and a window manager | the desktop | the logged-in desktop, not an SSH session's |
| Audio | PulseAudio or PipeWire (`pactl`) | four separate loopback devices: BlackHole (`build_macos_sound_drivers.sh` builds extra copies) and/or VB-Cable | Virtual Audio Cable and/or VB-Cable |
| Playing and recording | `paplay`, `parecord` | `sox` | `pip install sounddevice` |
| Serial ports | nothing more (the build makes the library they need) | the same | not available yet |
| Rig control | `rigctld` (Hamlib's, built with FreeDV, is found by itself) | the same | `rigctld.exe` (built with FreeDV), named by `RIGCTLD` |
| FreeDV Reporter | `pip install aiohttp python-socketio aiosqlite`, and git | the same | the same |

Python 3.9 or later. On macOS, the system Python's python-socketio is too old
for the FreeDV Reporter server, so use a venv.

## How it works

```
 test_<area>.py ──── freedvtest ──── JSON over TCP (localhost) ──── TestAgent (in FreeDV)
                      │                                                │
                      ├─ audio: virtual devices, play/record           ├─ finds widgets by name
                      ├─ reporter: local FreeDV Reporter server        ├─ clicks, sets values, sends
                      ├─ rigctld: a stand-in radio                     │  mouse and key events
                      ├─ serial: pseudo-terminal serial ports          └─ catches message boxes, file
                      └─ wsjtx: a stand-in logging program                choosers, popup menus, browser
```

### The test agent

`src/test_agent.cpp` is compiled into FreeDV when it's built with
`UNITTEST` (as `ENABLE_TEST_AGENT`). Started with `-testport <port>`, FreeDV
connects to the test script on that port and carries out its commands, one
line of JSON each:

```
{"id": 1, "cmd": "click", "name": "main.btnOnOff"}
{"id": 1, "ok": true, "result": null}
```

Widgets are found by the names given to them in FreeDV's source with
`SetName()` (for example `main.btnOnOff` or `settings.txt_callsign`). Windows
without names of their own, such as wxWidgets' standard dialogs, are
`@<title>`, and the controls in them `@<title>/<label>`.

Commands run on the UI thread. One that does something replies before doing
it, as that may not return for a while (a button that opens a modal dialog,
say). Commands sent meanwhile are carried out by that dialog's event loop.
So after an action, a script waits for its effect (`wait_until()`,
`wait_for_window()`) rather than assuming it has already happened.

FreeDV routes a few things through hooks that the agent takes over, so that
nothing waits for a person:

- **Message boxes** aren't shown. FreeDV carries on as if the default button
  had been pressed, or the one the script chose with
  `answer_next_message_box()`. `message_boxes()` returns what would have been
  shown.
- **File and folder choosers** count as cancelled, unless the script said
  what to choose with `choose_next_path()`.
- **Popup menus** aren't shown. The script gets their items and can choose one.
- **Web pages** aren't opened. `browser_urls()` says which would have been.

`freedvtest.FreeDV` (from `freedvtest.start()`) wraps all of this: `click()`,
`set_value()`, `get()`, `menu()`, `context_menu()`, `mouse()`, `key()`,
`rows()` for lists, `state()` for FreeDV's own state and configuration,
`quit()` and so on. Its docstrings describe each.

### Keeping FreeDV to itself

Each FreeDV started by a test gets a configuration file of its own (written
from the `settings` given to `start()`) and a stand-in home folder, so that it
never sees or changes the settings of whoever runs the tests:

- **Linux:** `HOME` and `XDG_CONFIG_HOME`.
- **macOS:** `HOME` and `CFFIXED_USER_HOME`. macOS finds `~/Library/Preferences`
  without looking at `HOME`.
- **Windows:** FreeDV keeps its default configuration, and which configuration
  was last used, in the registry. Test builds keep them as files in the folder
  named by `FREEDV_TEST_CONFIG_DIR` instead (see `main.cpp`), which the tests
  set to the stand-in home folder. `LOCALAPPDATA` stands in for where other
  programs' settings are imported from.

Settings files are read and written escaped as wxFileConfig does it. Windows
paths are full of backslashes, which start escape sequences there.

The tests also keep FreeDV off the public FreeDV Reporter server: unless a
test says otherwise, it's pointed at `127.0.0.1:9`, where there's nothing.

### Audio devices

`freedvtest.audio` provides the devices FreeDV uses. Tests ask for them by
name: `FreeDVGuiTest_Radio_To_Computer`, `..._Computer_To_Radio`,
`..._Microphone_To_Computer` and `..._Computer_To_Speaker`. They then:

- play into one (`Tone`, `play()`) to stand in for the radio or microphone;
- record from one (`record()`, `record_level()`, `tone_share()`) to hear what
  FreeDV sends to the radio or speaker.

How the devices are provided depends on the platform:

- **Linux:** `VirtualAudioDevices` creates null sinks for the duration of a
  test. What's played into one is heard from its monitor (`input_name()`).
  `PrivateAudioServer` runs a PipeWire of the test's own for tests that take
  devices away or stop the sound server. `AudioServerGateway` stands between
  FreeDV and the sound server to make it fail at chosen moments.
- **macOS and Windows:** devices can't be created, so the tests are given
  loopback devices that are already there in place of the ones they ask for
  (`device()`). They're found automatically or named in
  `FREEDVTEST_AUDIO_DEVICES` (comma-separated). On macOS, a driver's extra
  devices ("BlackHole 2ch 2", "VB-Cable A") share their first device's audio,
  so they're left out.
- **Windows:** Virtual Audio Cable's lines come first, then VB-Cable, which
  now and then drops a few milliseconds of audio. A device is named after its
  playback side, and recorded from under the same name or that of the input
  with the same driver in brackets ("Speakers (VB-Audio Virtual Cable)" is
  heard from "CABLE Output (VB-Audio Virtual Cable)").

**Shared devices.** With fewer than four separate devices, as on Windows CI
(VAC Lite's one line and VB-Cable), they're shared: one for the radio's input
and output, the other for the microphone and speaker. FreeDV then hears its
own transmission, as it does in the other Windows audio tests. Naming a
device more than once in `FREEDVTEST_AUDIO_DEVICES` does the same. A few
checks can't be made on a shared device, for example that nothing is sent to
the radio while a tone plays into its input. Those check `audio.shared()` and
are left out.

**Hearing its own transmission.** `Loopback` makes FreeDV hear what it sends
to the radio. On Linux it feeds one device into the other. On macOS and
Windows it points FreeDV's radio input at the device it sends to.

### Other stand-ins

- **`reporter.py`** runs the real FreeDV Reporter server locally (from
  `FREEDV_REPORTER_SERVER_DIR`, or cloned into `freedv_reporter_src`). It also
  runs stations that connect to it as other copies of FreeDV would, and can
  cut FreeDV's connection.
- **`rigctld.py`** runs Hamlib's `rigctld` with its dummy radio for FreeDV to
  control over the network, and reads and sets its frequency and mode.
- **`serial.py`** provides serial ports (Linux and macOS): pseudo-terminals,
  plus a small library (`serial_shim.c`) loaded into FreeDV with
  `LD_PRELOAD` or `DYLD_INSERT_LIBRARIES`. It records the control lines
  FreeDV sets and lets the test set the one FreeDV reads.
- **`pulse.py`** makes PulseAudio report a given latency for FreeDV's input
  (Linux), with a small library (`pulse_shim.c`) loaded into FreeDV with
  `LD_PRELOAD`.
- **`wsjtx.py`** stands in for a logging program that takes contacts in WSJT-X's
  format, which is how FreeDV sends logged contacts.

### Skipping

When something a scenario needs can't be had (a device created or taken away
on macOS or Windows, a serial port on Windows, rigctld that isn't installed),
`audio.Unavailable` is raised. The script then exits with 77, which CTest
(`SKIP_RETURN_CODE 77`) and `run_tests.py` count as skipped. Where only part
of a scenario can't be done, that part is left out and the rest still runs.

## In CI

- **Linux** (`cmake-linux.yml`): CTest, with `-L gui`, on Xvfb with metacity
  (`.github/actions/run-linux-gui-test`), under each sanitizer, retrying
  failures. The coverage job runs them too.
- **macOS** (`cmake-macos.yml`): CTest, with four BlackHole drivers built and
  installed by `.github/actions/setup-macos-virtual-audio`, under each
  sanitizer. As on Linux, a GUI test that fails is run again.
- **Windows** (`cmake-windows.yml`): `build-unit-tests` also builds FreeDV for
  testing (x86_64) and stashes it with `rigctld.exe`. The `gui-tests` job
  installs VAC Lite and VB-Cable (`setup-windows-audio-drivers`) and runs
  `run_tests.py` with one retry.

Pull requests get one configuration per platform: on Linux, ubuntu-26.04
without sanitizers; on macOS, without sanitizers; on Windows, x86_64. The rest
(each sanitizer, the coverage job) run on pushes to master and the `v*-dev`
branches, on manual runs, and on pull requests labelled `full-ci`.

## Writing a scenario

- Add a `test_<name>()` function to the script for that area, and add it to
  the script's `SCENARIOS`. Then add its name to the script's `foreach` list
  in `CMakeLists.txt`, so that CTest runs it.
- Start FreeDV with `with freedvtest.start(settings=...) as app:` and end with
  `app.quit()`. The `with` block cleans up whatever happens.
- Give any control the test needs a name with `SetName()` in FreeDV's source.
- Wait for effects rather than sleeping: `app.wait_until(...)`,
  `app.wait_for_window(...)`, `app.wait_for_window_closed(...)`.
- For audio, ask for devices with `audio_devices()` (or
  `VirtualAudioDevices`), and check levels and tones rather than exact samples.
- Raise `audio.Unavailable` for what can't be done on a platform, and use
  `audio.shared()` around checks that need separate devices.
