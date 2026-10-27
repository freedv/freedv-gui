"""Serial ports for FreeDV to use during tests (Linux and macOS).

These are pseudo-terminals, which FreeDV can open as it would a real port.
What they lack is a real port's control lines, so FreeDV is run with a
small library (serial_shim.c, built along with FreeDV's tests; see
freedvtest.shim_path()) that keeps track of the ones it sets and lets the
test set the one it reads.

    with SerialPorts() as ports:
        radio = ports.add()
        with freedvtest.start(settings={"/Rig/Port": radio.name, ...}, env=ports.env) as app:
            ...
            assert radio.rts
"""

import os
import re
import shutil
import sys
import tempfile

from . import shim_path


class SerialError(Exception):
    pass


class SerialPort:
    def __init__(self, directory):
        self._control, self._port = os.openpty()
        self.name = os.ttyname(self._port)
        # (As serial_shim.c numbers them: /dev/pts/<n>, or /dev/ttys<n>.)
        self._prefix = os.path.join(directory, str(int(re.search(r"(\d+)$", self.name).group(1))))
        # Whatever had this name before is nothing to do with this port.
        for suffix in (".lines", ".history", ".cts"):
            if os.path.exists(self._prefix + suffix):
                os.remove(self._prefix + suffix)

    def close(self):
        os.close(self._port)
        os.close(self._control)

    @staticmethod
    def _parse(line):
        return {key.lower(): value == "1" for key, value in (item.split("=") for item in line.split())}

    def history(self):
        """Returns every state FreeDV has put RTS and DTR in, oldest first,
        each as a dict with "rts" and "dtr"."""
        try:
            with open(self._prefix + ".history") as f:
                return [self._parse(line) for line in f.read().splitlines() if line]
        except OSError:
            return []

    @property
    def lines(self):
        """RTS and DTR as FreeDV last set them (both off if it never has)."""
        history = self.history()
        return history[-1] if history else {"rts": False, "dtr": False}

    @property
    def rts(self):
        return self.lines["rts"]

    @property
    def dtr(self):
        return self.lines["dtr"]

    def set_cts(self, raised):
        """Sets what FreeDV reads CTS as."""
        with open(self._prefix + ".cts.new", "w") as f:
            f.write("1" if raised else "0")
        os.replace(self._prefix + ".cts.new", self._prefix + ".cts")


class SerialPorts:
    """Provides serial ports for the duration of a "with" block. FreeDV needs
    starting with env for their control lines to work."""

    def __init__(self):
        self._directory = None
        self._ports = []
        self.env = {}

    def __enter__(self):
        if sys.platform == "win32":
            # (It would take pairs of virtual ports joined to each other,
            # such as com0com's.)
            from .audio import Unavailable
            raise Unavailable("Serial ports for tests can't be had on Windows")
        shim = shim_path("serial_shim")
        if not os.path.exists(shim):
            raise SerialError("%s isn't there; it's built with FreeDV's tests (-DUNITTEST=1)" % shim)
        self._directory = tempfile.mkdtemp(prefix="freedvtest-serial-")

        preload = "DYLD_INSERT_LIBRARIES" if sys.platform == "darwin" else "LD_PRELOAD"
        self.env = {preload: shim, "FREEDVTEST_SERIAL_DIR": self._directory}
        # A FreeDV built with the address sanitizer expects that to be what's
        # loaded first.
        options = os.environ.get("ASAN_OPTIONS", "")
        self.env["ASAN_OPTIONS"] = (options + ":" if options else "") + "verify_asan_link_order=0"
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        for port in self._ports:
            port.close()
        self._ports = []
        shutil.rmtree(self._directory, ignore_errors=True)
        return False

    def add(self):
        """Creates a port and returns it; its name is what FreeDV is given."""
        port = SerialPort(self._directory)
        self._ports.append(port)
        return port
