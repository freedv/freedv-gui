"""Virtual audio devices for FreeDV to use during tests.

On Linux, they're created for each test with PulseAudio/PipeWire, as null
sinks: what's played into one can be heard from its monitor.

On macOS, devices can't be created like that, so tests are given loopback
devices that are already there (BlackHole and VB-Cable: what's played into
one comes back from its input) in place of the ones they ask for, by
device(). Most tests need four, separate from each other: a driver's extra
devices ("BlackHole 2ch 2", "VB-Cable A") carry what's played into its first,
so they're left out. (build_macos_sound_drivers.sh builds four BlackHole
drivers, as CI uses.) Which there are to use is found out, or can be given as
a comma-separated list in $FREEDVTEST_AUDIO_DEVICES. Tests that need something
that can't be done there (more devices than there are, or anything to do with
the sound server itself) raise Unavailable, for which a test script exits
with 77 (skipped). sox plays and records.

Windows is like macOS, with Virtual Audio Cable's lines and VB-Cable as the
loopback devices (named, as FreeDV names them, after where they're played
into, e.g. "Line 1 (Virtual Audio Cable)" or "Speakers (VB-Audio Virtual
Cable)", and recorded from under the same name or that of the input with the
same driver's name in brackets), and the sounddevice module playing and
recording (see wasapi.py). Virtual Audio Cable comes first: VB-Cable now and
then leaves a few milliseconds out of what's played through it.
With fewer than four, they're shared: with two, as Windows CI has, one for
the radio's input and output and the other for the microphone and speaker,
as CI's other tests have them. (With three, the radio's share one.)
$FREEDVTEST_AUDIO_DEVICES can name the same device more than once to share
it. Checks that can't be made on a shared device (that nothing's played into
one while something else is) are left out where shared() says so.
"""

import array
import math
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import wave

MACOS = sys.platform == "darwin"
WINDOWS = sys.platform == "win32"
# Whether tests are given loopback devices that are already there, rather
# than ones made for them (see above).
FIXED_DEVICES = MACOS or WINDOWS


# How sox is run on macOS, to play into a device and to record from one.
# BlackHole loses much of what's played into it unless whatever plays into it
# uses smaller blocks than FreeDV does, and whatever records from it larger
# ones. (Found by trying: these are the buffer sizes that came through
# cleanly.)
_SOX_PLAY = ["sox", "-q", "--buffer", "4096"]
_SOX_RECORD = ["sox", "-q", "--buffer", "16384"]


class Unavailable(Exception):
    """What a test needs of the audio devices can't be done here."""


def _pactl(*args):
    return subprocess.check_output(("pactl",) + args).decode()


# The device names that tests use, in the order they're given macOS's
# loopback devices (so that each always gets the same one, whatever order a
# test asks for them in). Any others get those that are left.
_STANDARD_NAMES = ["FreeDVGuiTest_Radio_To_Computer", "FreeDVGuiTest_Computer_To_Radio",
                   "FreeDVGuiTest_Microphone_To_Computer", "FreeDVGuiTest_Computer_To_Speaker"]
_assigned = {}
_loopback_devices = None


def _windows_loopback_devices():
    from . import wasapi

    outputs = set(device["name"] for device in wasapi.devices() if device["output"])
    # (VB-Cable's "CABLE In 16ch" carries what's played into it to the same
    # place as its other output, so it's left out.)
    found = sorted(name for name in outputs if name.endswith("(Virtual Audio Cable)"))
    found += sorted(name for name in outputs if name.endswith("(VB-Audio Virtual Cable)") and "16ch" not in name)
    # Shared if there are fewer than four (see above).
    if len(found) == 2:
        found = [found[0], found[0], found[1], found[1]]
    elif len(found) == 3:
        found = [found[0], found[0], found[1], found[2]]
    return found


_windows_inputs = None


def _windows_input_name(name):
    """The input that what's played into an output device on Windows is
    heard from: the one of the same name or, failing that (as for VB-Cable),
    with the same driver's name in brackets."""
    global _windows_inputs
    if _windows_inputs is None:
        from . import wasapi
        _windows_inputs = [device["name"] for device in wasapi.devices() if device["input"]]
    if name in _windows_inputs:
        return name
    driver = name[name.rfind(" ("):] if " (" in name else None
    matching = [input_name for input_name in _windows_inputs if driver and input_name.endswith(driver)]
    return matching[0] if len(matching) == 1 else name


def _loopback_devices_given():
    global _loopback_devices
    if _loopback_devices is None:
        given = os.environ.get("FREEDVTEST_AUDIO_DEVICES")
        if given:
            _loopback_devices = [name.strip() for name in given.split(",") if name.strip()]
        elif WINDOWS:
            _loopback_devices = _windows_loopback_devices()
        else:
            from . import coreaudio

            both = [device["name"] for device in coreaudio.devices() if device["input"] and device["output"]]
            # Devices named after another with something added (BlackHole's
            # "<name> 2", VB-Cable's "VB-Cable A" and "VB-Cable B") share
            # what's played into them with it, so only the first will do.
            _loopback_devices = sorted(
                name for name in both
                if (name.startswith("BlackHole") or name.startswith("VB-Cable"))
                and not any(name.startswith(other + " ") for other in both))
    return _loopback_devices


def device(name):
    """Returns the name of the device that stands for the given one: on
    Linux, itself (VirtualAudioDevices creates it); on macOS and Windows, the
    loopback device it's been given."""
    if not FIXED_DEVICES:
        return name
    if name not in _assigned:
        available = _loopback_devices_given()
        order = [n for n in _STANDARD_NAMES if n in _STANDARD_NAMES[:len(available)]]
        if name in order:
            _assigned[name] = available[order.index(name)]
        else:
            taken = set(available[:len(order)]) | set(_assigned.values())
            free = [n for n in available if n not in taken]
            if not free:
                # Using it as is: VirtualAudioDevices says it's unavailable.
                return name
            _assigned[name] = free[0]
    return _assigned[name]


def shared(*names):
    """Returns whether any of the given devices (from device()) are the same
    one, so that what's played into one is heard from the others too."""
    return len(set(names)) < len(names)


def default_output():
    """Returns the name of the system's default output device."""
    if WINDOWS:
        from . import wasapi
        return wasapi.default_output()
    if MACOS:
        from . import coreaudio
        return coreaudio.default_output()
    return _pactl("get-default-sink").strip()


def record(device, seconds=1.0, channels=1, rate=8000):
    """Listens to an input device (or input_name() of an output device) and
    returns what it hears: a list of 16 bit samples for each channel."""
    if WINDOWS:
        from . import wasapi
        data = wasapi.record(device, seconds, rate, channels)
        frame = 2 * channels
        samples = array.array("h", data[:len(data) // frame * frame])
        return [list(samples[channel::channels]) for channel in range(channels)]
    with tempfile.TemporaryDirectory(prefix="freedvtest-audio-") as folder:
        path = os.path.join(folder, "recording.wav")
        if MACOS:
            subprocess.check_call(_SOX_RECORD + [
                "-t", "coreaudio", device, "-c", str(channels), "-r", str(rate), "-b", "16",
                "-e", "signed-integer", path, "trim", "0", str(seconds)])
        else:
            recorder = subprocess.Popen([
                "parecord", "--device=" + device, "--file-format=wav", "--channels=%d" % channels,
                "--rate=%d" % rate, "--format=s16le", "--latency-msec=20", path])
            time.sleep(seconds)
            recorder.terminate()
            recorder.wait()

        # The recorder doesn't get to fill in the file's length, so don't rely on it.
        with open(path, "rb") as f:
            data = f.read()[44:]
    frame = 2 * channels
    samples = array.array("h", data[:len(data) // frame * frame])
    return [list(samples[channel::channels]) for channel in range(channels)]


def _rms(samples):
    if not samples:
        return 0.0
    return math.sqrt(sum(sample * sample for sample in samples) / len(samples))


def record_level(device, seconds=1.0):
    """Listens to an input device (or input_name() of an output device) and
    returns how loud what it hears is, as the RMS of 16 bit samples: 0 for
    silence, around 23000 for a full-scale tone."""
    return _rms(record(device, seconds)[0])


def tone_share(samples, frequency, rate=8000):
    """Returns how much of what's in some samples (see record()) is a tone
    of the given frequency: close to 1 if that's all there is, close to 0 if
    it isn't there or is lost among everything else."""
    total = sum(sample * sample for sample in samples)
    if not total:
        return 0.0
    step = 2 * math.pi * frequency / rate
    real = sum(sample * math.cos(step * n) for n, sample in enumerate(samples))
    imaginary = sum(sample * math.sin(step * n) for n, sample in enumerate(samples))
    return 2 * (real * real + imaginary * imaginary) / (len(samples) * total)


def write_tone(path, seconds=2.0, rate=16000, frequency=1000.0, channels=1, amplitude=16000):
    """Creates a WAV file containing a tone."""
    samples = array.array("h")
    for n in range(int(rate * seconds)):
        samples.extend([int(amplitude * math.sin(2 * math.pi * frequency * n / rate))] * channels)
    with wave.open(path, "wb") as f:
        f.setnchannels(channels)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(samples.tobytes())


def file_level(path):
    """Returns how loud a 16 bit WAV file is (see record_level()) and how
    long it is in seconds."""
    with wave.open(path, "rb") as f:
        rate = f.getframerate()
        samples = array.array("h", f.readframes(f.getnframes()))
    if not samples:
        return (0.0, 0.0)
    return (_rms(samples), len(samples) / float(rate))


# macOS: devices whose input_name() is, for now, another's (see Loopback).
_heard_from = {}


class Loopback:
    """Feeds what's played into one output device to another for the
    duration of a "with" block, e.g. so that FreeDV hears what it transmits.

    On macOS and Windows, nothing is fed anywhere: instead, input_name() of the second
    device is that of the first while the block lasts, so what's set up
    inside it (FreeDV's settings, say) listens to the first directly.
    (Feeding one of BlackHole's devices to another loses most of what's
    played into the first.) Nothing else should be played into the second
    meanwhile."""

    def __init__(self, from_device, to_device):
        self._from_device = from_device
        self._from = VirtualAudioDevices.input_name(from_device)
        self._to = to_device
        self._module = None

    def __enter__(self):
        if FIXED_DEVICES:
            _heard_from[self._to] = self._from_device
        else:
            self._module = _pactl(
                "load-module", "module-loopback", "source=" + self._from, "sink=" + self._to,
                "latency_msec=20").strip()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        if self._module is not None:
            subprocess.call(["pactl", "unload-module", self._module])
        _heard_from.pop(self._to, None)
        return False


def play(device, path):
    """Starts playing a sound file into an output device, and returns the
    process doing it (to be killed when it's no longer wanted)."""
    if WINDOWS:
        from . import wasapi
        return wasapi.Player(device, path)
    if MACOS:
        return subprocess.Popen(_SOX_PLAY + [path, "-t", "coreaudio", device])
    return subprocess.Popen(["paplay", "--device=" + device, path])


class Tone:
    """Plays a tone into an output device for the duration of a "with" block
    (at most max_seconds). amplitude is its peak, in 16 bit samples."""

    def __init__(self, device, frequency=1000.0, max_seconds=30, amplitude=16000):
        self._device = device
        self._frequency = frequency
        self._amplitude = amplitude
        self._seconds = max_seconds
        self._folder = None
        self._player = None

    def __enter__(self):
        self._folder = tempfile.TemporaryDirectory(prefix="freedvtest-audio-")
        path = os.path.join(self._folder.name, "tone.wav")
        rate = 8000
        samples = array.array("h", (
            int(self._amplitude * math.sin(2 * math.pi * self._frequency * n / rate)) for n in range(rate * self._seconds)))
        with wave.open(path, "wb") as f:
            f.setnchannels(1)
            f.setsampwidth(2)
            f.setframerate(rate)
            f.writeframes(samples.tobytes())
        self._player = play(self._device, path)
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        if self._player is not None:
            # (See VirtualAudioDevices.__exit__().)
            self._player.kill() if FIXED_DEVICES else self._player.terminate()
            self._player.wait()
        self._folder.cleanup()
        return False


class VirtualAudioDevices:
    """Creates PulseAudio/PipeWire devices for the duration of a "with" block.

    Each positional argument is the name of an output device that discards
    what's played into it. It can also be used as an input device (which
    hears whatever was played into it) under input_name().

    inputs maps names to descriptions of devices that are only inputs, as a
    radio's or microphone's would be, and outputs does the same for further
    output devices. The description is what a device is displayed as, which
    is otherwise its name.

    rates maps the names of output devices to the sample rate each is to run
    at, for those that aren't to run at the sound server's usual one.

    On macOS, the names are those from device(), and nothing is created:
    the loopback devices are set to run at 48 kHz (or the rate given) while
    in use, and silence is played into them throughout. (Some, VB-Cable
    among them, otherwise repeat whatever was last played into them for as
    long as nothing else is, where a null sink's monitor is silent.)
    Devices that are only inputs, and descriptions, can't be had there.
    """

    def __init__(self, *names, outputs=None, inputs=None, rates=None):
        self._outputs = {name: name for name in names}
        self._outputs.update(outputs or {})
        self._inputs = dict(inputs or {})
        self._rates = dict(rates or {})
        self._modules = []  # (device name, module index), in the order loaded
        self._previous_rates = {}  # macOS: what each device was running at
        self._silence = []  # macOS: what's playing silence into them

    @staticmethod
    def input_name(name):
        if WINDOWS:
            return _windows_input_name(_heard_from.get(name, name))
        return _heard_from.get(name, name) if MACOS else name + ".monitor"

    @staticmethod
    def input_label(name):
        """What FreeDV lists the input_name() of a device as."""
        if WINDOWS or MACOS:
            # (Inside a Loopback block, the device listened to instead.)
            return VirtualAudioDevices.input_name(name)
        return "Monitor of " + name

    def __enter__(self):
        if WINDOWS:
            return self._enter_windows()
        if MACOS:
            return self._enter_macos()
        try:
            for name, description in self._outputs.items():
                rate = ["rate=%d" % self._rates[name]] if name in self._rates else []
                self._load(name, "module-null-sink", "sink_name=" + name, self._properties("sink", description),
                           *rate)
            if self._inputs:
                # PipeWire has no module-null-source, but can make a null
                # sink behave as a source instead.
                pipewire = "PipeWire" in _pactl("info")
                for name, description in self._inputs.items():
                    if pipewire:
                        self._load(
                            name, "module-null-sink", "media.class=Audio/Source/Virtual",
                            "sink_name=" + name, self._properties("sink", description))
                    else:
                        self._load(
                            name, "module-null-source", "source_name=" + name,
                            self._properties("source", description))
            for name in self._outputs:
                self._wait_for("sinks", name)
            for name in self._inputs:
                self._wait_for("sources", name)
        except BaseException:
            self.__exit__(None, None, None)
            raise
        return self

    def _enter_windows(self):
        from . import wasapi

        if self._inputs or any(description != name for name, description in self._outputs.items()):
            raise Unavailable("Devices that are only inputs, or that have descriptions of their own, can't be had "
                              "on Windows")
        available = _loopback_devices_given()
        for name in self._outputs:
            if name not in available:
                raise Unavailable("%s isn't one of the loopback devices (%s); see freedvtest.audio.device()"
                                  % (name, ", ".join(available) or "none"))
        for name, rate in self._rates.items():
            if wasapi.sample_rate(name) != rate:
                raise Unavailable("%s can't be set to run at %d Hz on Windows" % (name, rate))
        return self

    def _enter_macos(self):
        from . import coreaudio

        if self._inputs or any(description != name for name, description in self._outputs.items()):
            raise Unavailable("Devices that are only inputs, or that have descriptions of their own, can't be had "
                              "on macOS")
        available = _loopback_devices_given()
        for name in self._outputs:
            if name not in available:
                raise Unavailable("%s isn't one of the loopback devices (%s); see freedvtest.audio.device()"
                                  % (name, ", ".join(available) or "none"))
        try:
            for name in self._outputs:
                rate = self._rates.get(name, 48000)
                previous = coreaudio.sample_rate(name)
                if previous != rate:
                    try:
                        coreaudio.set_sample_rate(name, rate)
                    except OSError as e:
                        raise Unavailable(str(e))
                    self._previous_rates[name] = previous
                if not name.startswith("BlackHole"):
                    self._silence.append(subprocess.Popen(
                        _SOX_PLAY + ["-n", "-r", str(rate), "-c", "2", "-t", "coreaudio", name,
                         "synth", "86400", "sine", "0", "vol", "0"]))
        except BaseException:
            self.__exit__(None, None, None)
            raise
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        for _, module in reversed(self._modules):
            subprocess.call(["pactl", "unload-module", module])
        self._modules = []
        for player in self._silence:
            # (Not terminate(): sox then tries to finish what it's playing,
            # which it can get stuck doing.)
            player.kill()
            player.wait()
        self._silence = []
        if self._previous_rates:
            from . import coreaudio
            for name, rate in self._previous_rates.items():
                try:
                    coreaudio.set_sample_rate(name, rate)
                except OSError:
                    pass
            self._previous_rates = {}
        return False

    def forget(self):
        """For when the sound server itself has gone, and the devices with
        it: there's nothing left to remove at the end of the "with" block."""
        self._modules = []

    def remove(self, *names):
        """Unloads the named devices now, and waits until the sound server
        stops listing them (and, for outputs, their monitors)."""
        if FIXED_DEVICES:
            raise Unavailable("Devices can't be taken away on macOS or Windows")
        for name in names:
            for entry in [entry for entry in self._modules if entry[0] == name]:
                subprocess.call(["pactl", "unload-module", entry[1]])
                self._modules.remove(entry)
        gone = [name for name in names for name in (name, self.input_name(name))]
        deadline = time.monotonic() + 10
        while True:
            listed = set()
            for kind in ("sinks", "sources"):
                for line in _pactl("list", "short", kind).splitlines():
                    fields = line.split("\t")
                    if len(fields) > 1:
                        listed.add(fields[1])
            if not any(name in listed for name in gone):
                return
            if time.monotonic() > deadline:
                raise RuntimeError("Audio device %s was still listed after being removed" % ", ".join(gone))
            time.sleep(0.1)

    def _load(self, name, *args):
        self._modules.append((name, _pactl("load-module", *args).strip()))

    @staticmethod
    def _properties(kind, description):
        return "%s_properties=\"device.description='%s'\"" % (kind, description)

    @staticmethod
    def _wait_for(kind, name, timeout=30):
        deadline = time.monotonic() + timeout
        while True:
            devices = _pactl("list", "short", kind)
            if any(line.split("\t")[1:2] == [name] for line in devices.splitlines()):
                return
            if time.monotonic() > deadline:
                raise RuntimeError("Audio device %s did not appear" % name)
            time.sleep(0.5)


def streams(pid):
    """Returns the audio streams that a program has open: for each, whether
    it's a "playback" or "record" stream, its "index" and the name of the
    "device" it's connected to."""
    import json

    if FIXED_DEVICES:
        raise Unavailable("Which device a program's streams are on can't be found out on macOS or Windows")
    found = []
    for kind, listing, devices, key in [("playback", "sink-inputs", "sinks", "sink"),
                                        ("record", "source-outputs", "sources", "source")]:
        names = {device["index"]: device["name"] for device in json.loads(_pactl("--format=json", "list", devices))}
        for stream in json.loads(_pactl("--format=json", "list", listing)):
            if str(stream.get("properties", {}).get("application.process.id")) == str(pid):
                found.append({"kind": kind, "index": stream["index"], "device": names.get(stream[key])})
    return found


def move_stream(stream, device):
    """Connects one of streams() to a different device, as the system's
    sound settings (or the device it was using going away) would."""
    if FIXED_DEVICES:
        raise Unavailable("Streams can't be moved between devices on macOS or Windows")
    command = "move-sink-input" if stream["kind"] == "playback" else "move-source-output"
    _pactl(command, str(stream["index"]), device)


class PrivateAudioServer:
    """Runs a sound server (PipeWire, with its PulseAudio interface) of the
    test's own for the duration of a "with" block, for tests that would
    otherwise disturb whatever else is using the system's: anything that
    removes devices that are in use, as what was using them then moves to
    the default device.

    Everything in this module, and any FreeDV started, uses it while it's
    running. Its default devices are ones that go nowhere.
    """

    DEFAULT_OUTPUT = "FreeDVGuiTest_Default_Output"
    DEFAULT_INPUT = "FreeDVGuiTest_Default_Input"

    PROGRAMS = ["dbus-run-session", "pipewire", "wireplumber", "pipewire-pulse"]

    def __init__(self):
        self._folder = None
        self._server = None
        self._previous = None

    @classmethod
    def available(cls):
        import shutil
        if FIXED_DEVICES:
            return False
        return all(shutil.which(program) for program in cls.PROGRAMS + ["pactl"])

    def __enter__(self):
        self._folder = tempfile.mkdtemp(prefix="freedvtest-sound-")
        self._previous = os.environ.get("PULSE_SERVER")
        os.environ["PULSE_SERVER"] = "unix:" + os.path.join(self._folder, "pulse", "native")
        try:
            self.start()
        except BaseException:
            self.__exit__(None, None, None)
            raise
        return self

    def start(self):
        """Starts the sound server (again, after stop()). It comes back
        without any of the devices that tests had created."""
        # (Without the last two, the desktop's file services get started as
        # well, and mount something in the folder.)
        env = dict(os.environ, XDG_RUNTIME_DIR=self._folder, GIO_USE_VFS="local", GVFS_DISABLE_FUSE="1")
        for name in ("PULSE_SERVER", "PIPEWIRE_REMOTE", "PIPEWIRE_RUNTIME_DIR", "PULSE_RUNTIME_PATH",
                     "DBUS_SESSION_BUS_ADDRESS"):
            env.pop(name, None)
        with open(os.path.join(self._folder, "server.log"), "ab") as log:
            self._server = subprocess.Popen(
                ["dbus-run-session", "--", "sh", "-c",
                 "pipewire & sleep 1; wireplumber & sleep 1; pipewire-pulse & wait"],
                env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)

        deadline = time.monotonic() + 30
        while True:
            if subprocess.call(["pactl", "info"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0:
                break
            if self._server.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("Could not start a sound server for the test")
            time.sleep(0.5)

        # Whatever it's found of the real world, nothing goes there
        # unless it's asked to.
        _pactl("load-module", "module-null-sink", "sink_name=" + self.DEFAULT_OUTPUT)
        _pactl("load-module", "module-null-sink", "sink_name=FreeDVGuiTest_Default_Input_Source")
        VirtualAudioDevices._wait_for("sinks", self.DEFAULT_OUTPUT)
        VirtualAudioDevices._wait_for("sinks", "FreeDVGuiTest_Default_Input_Source")
        _pactl("set-default-sink", self.DEFAULT_OUTPUT)
        _pactl("set-default-source", "FreeDVGuiTest_Default_Input_Source.monitor")
        deadline = time.monotonic() + 15
        while default_output() != self.DEFAULT_OUTPUT:
            if time.monotonic() > deadline:
                raise RuntimeError("Could not set the test sound server's default device")
            time.sleep(0.25)

    def stop(self):
        """Stops the sound server, as if it had crashed."""
        import signal

        if self._server is not None:
            try:
                os.killpg(self._server.pid, signal.SIGKILL)
            except OSError:
                pass
            self._server.wait()
            self._server = None

    def __exit__(self, exc_type, exc_value, traceback):
        import shutil

        self.stop()
        if self._previous is None:
            os.environ.pop("PULSE_SERVER", None)
        else:
            os.environ["PULSE_SERVER"] = self._previous
        shutil.rmtree(self._folder, ignore_errors=True)
        return False


def _sound_server_socket():
    """The address of the sound server that this process is using."""
    configured = os.environ.get("PULSE_SERVER", "")
    if configured.startswith("unix:"):
        return configured[len("unix:"):]
    if configured.startswith("tcp:"):
        return configured[len("tcp:"):]
    if configured.startswith("/") or ":" in configured:
        return configured
    runtime = os.environ.get("XDG_RUNTIME_DIR") or "/run/user/%d" % os.getuid()
    return os.path.join(runtime, "pulse", "native")


class AudioServerGateway:
    """Stands between FreeDV and the sound server, so that a test can choose
    when FreeDV stops being able to reach it: as when the sound server goes
    away part way through FreeDV starting up.

    Every connection is relayed to the sound server, until
    refuse_next_connections() is called: the next connection that FreeDV
    closes is then the last one that gets through, and every connection after
    that is closed as soon as it's made, which is what a server that has just
    gone does. refuse_connections_after(count) instead lets a test say how
    far FreeDV gets before the server stops answering.

    after_next_connection_close(action) instead lets a test do something of
    its own (take a sound card away, say) when the next connection closes,
    holding back the connections after it until that's done.

    FreeDV is pointed at the gateway with env(); the test's own audio calls
    (see _pactl) keep going to the sound server directly.
    """

    def __init__(self):
        self._folder = None
        self._socket = None
        self._refusing = False
        self._refuse_after_close = False
        self._refuse_after = None    # connections still allowed, then None
        self._trigger = None    # (action, barrier) for the next connection
        self._barrier = None    # set once that action has run

    def __enter__(self):
        if FIXED_DEVICES:
            raise Unavailable("There's no sound server to stand in front of on macOS or Windows")
        self._folder = tempfile.mkdtemp(prefix="freedvtest-gateway-")
        self._socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._socket.bind(os.path.join(self._folder, "native"))
        self._socket.listen(8)
        threading.Thread(target=self._accept, daemon=True).start()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        import shutil

        if self._socket is not None:
            self._socket.close()
            self._socket = None
        shutil.rmtree(self._folder, ignore_errors=True)
        return False

    def env(self):
        """What to start FreeDV with (see freedvtest.start(env=...)) so that
        it reaches the sound server through the gateway."""
        return {"PULSE_SERVER": "unix:" + self._socket.getsockname()}

    def refuse_next_connections(self):
        """Refuses every connection from the next one that FreeDV closes
        onwards. FreeDV closes the connection it made while checking its
        sound cards before it opens any streams, so this is what going away
        in between looks like to it."""
        self._refuse_after_close = True

    def refuse_connections_after(self, count):
        """Refuses every connection after the next count of them. FreeDV
        makes one connection to check its sound cards, one to list the
        devices it's about to use, and one for each stream it opens, so a
        test can say which stream the sound server stops being reachable
        for."""
        self._refuse_after = count

    def after_next_connection_close(self, action):
        """Runs action() when the next connection that FreeDV makes has
        closed, and before anything is relayed on the connections after it.
        FreeDV closes the connection it made while checking its sound cards
        before it opens any streams, so this is what a sound card going away
        in between looks like to it."""
        self._barrier = threading.Event()
        self._trigger = (action, self._barrier)

    def _accept(self):
        while True:
            try:
                client, _ = self._socket.accept()
            except OSError:
                return
            if self._refuse_after is not None:
                if self._refuse_after > 0:
                    self._refuse_after -= 1
                else:
                    self._refusing = True
            # Only the first connection after after_next_connection_close()
            # is the one that triggers it.
            trigger, self._trigger = self._trigger, None
            threading.Thread(target=self._serve, args=(client, trigger), daemon=True).start()

    def _serve(self, client, trigger=None):
        if trigger is None and self._barrier is not None:
            # What the connection before this one was asked to do (taking a
            # sound card away, say) is finished before this one carries
            # anything.
            self._barrier.wait()
        try:
            if self._refusing:
                client.close()
                return
            try:
                server = _connect_to_sound_server()
            except OSError:
                client.close()
                return
            pairs = []
            for source, sink in ((client, server), (server, client)):
                pair = threading.Thread(target=_relay, args=(source, sink), daemon=True)
                pair.start()
                pairs.append(pair)
            for pair in pairs:
                pair.join()
            client.close()
            server.close()
        finally:
            if trigger is not None:
                action, barrier = trigger
                action()
                barrier.set()
        if self._refuse_after_close:
            self._refuse_after_close = False
            self._refusing = True


def _connect_to_sound_server():
    target = _sound_server_socket()
    if ":" in target and not target.startswith("/"):
        host, port = target.rsplit(":", 1)
        return socket.create_connection((host, int(port)), timeout=10)
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connection.connect(target)
    return connection


def _relay(source, sink):
    try:
        while True:
            data = source.recv(65536)
            if not data:
                break
            sink.sendall(data)
    except OSError:
        pass
    finally:
        try:
            sink.shutdown(socket.SHUT_WR)
        except OSError:
            pass
