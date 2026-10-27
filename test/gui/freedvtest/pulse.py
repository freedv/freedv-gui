"""Makes PulseAudio (or PipeWire's stand-in for it) report a given latency
for FreeDV's record streams, as some PipeWire versions do (#1601). Linux only;
FreeDV is run with a small library (pulse_shim.c, built along with FreeDV's
tests; see freedvtest.shim_path()) in front of libpulse.

    with RecordLatency(2055085278) as latency:
        with freedvtest.start(settings=..., env=latency.env) as app:
            ...
            assert latency.reported()

Where FreeDV doesn't get its input's latency from PulseAudio (other systems,
or a build using PortAudio), nothing changes and reported() stays False.
"""

import os
import shutil
import sys
import tempfile

from . import shim_path


class PulseShimError(Exception):
    pass


class RecordLatency:
    """For the duration of a "with" block. FreeDV needs starting with env for
    it to take effect."""

    def __init__(self, microseconds):
        self._microseconds = microseconds
        self._directory = None
        self._log = None
        self.env = {}

    def __enter__(self):
        if not sys.platform.startswith("linux"):
            return self
        shim = shim_path("pulse_shim")
        if not os.path.exists(shim):
            raise PulseShimError("%s isn't there; it's built with FreeDV's tests (-DUNITTEST=1)" % shim)
        self._directory = tempfile.mkdtemp(prefix="freedvtest-pulse-")
        self._log = os.path.join(self._directory, "reported.log")
        self.env = {"LD_PRELOAD": shim, "FREEDVTEST_PULSE_RECORD_LATENCY_US": str(self._microseconds),
                    "FREEDVTEST_PULSE_SHIM_LOG": self._log}
        # A FreeDV built with the address sanitizer expects that to be what's
        # loaded first.
        options = os.environ.get("ASAN_OPTIONS", "")
        self.env["ASAN_OPTIONS"] = (options + ":" if options else "") + "verify_asan_link_order=0"
        return self

    def reported(self):
        """Whether FreeDV has asked PulseAudio for a record stream's latency
        and been given this one."""
        return self._log is not None and os.path.exists(self._log)

    def __exit__(self, exc_type, exc_value, traceback):
        if self._directory is not None:
            shutil.rmtree(self._directory, ignore_errors=True)
        return False
