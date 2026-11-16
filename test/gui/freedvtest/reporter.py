"""A FreeDV Reporter server, and stations to populate it with, for tests.

ReporterServer runs the real FreeDV Reporter server on this machine, so that
tests can control exactly who's on it without anything appearing on the
public one. Station connects to it as another copy of FreeDV would:

    with ReporterServer() as server:
        with server.station("K1ABC", "FN42") as station:
            station.set_frequency(14236000)
            station.transmit("RADEV1", True)
            ...

Needs the Python packages the server itself does (other than discord):

    pip3 install aiohttp python-socketio aiosqlite

The server's source is taken from the folder named by
$FREEDV_REPORTER_SERVER_DIR or, failing that, downloaded into
freedv_reporter_src in the current folder.
"""

import asyncio
import os
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time

SERVER_REPOSITORY = "https://bitbucket.org/tmiw/freedv-reporter.git"
PROTOCOL_VERSION = 2


class ReporterError(Exception):
    """The FreeDV Reporter server or a station connected to it failed."""


def _require_packages():
    try:
        import aiohttp, aiosqlite, socketio  # noqa: F401,E401
    except ImportError as e:
        raise ReporterError(
            "%s. The FreeDV Reporter server needs: pip3 install aiohttp python-socketio aiosqlite" % e)


def server_source_dir():
    """Returns the folder containing the server's source, downloading it if
    need be."""
    source_dir = os.environ.get("FREEDV_REPORTER_SERVER_DIR")
    if not source_dir:
        source_dir = os.path.abspath("freedv_reporter_src")
        if not os.path.isdir(source_dir):
            subprocess.check_call(["git", "clone", "--depth", "1", SERVER_REPOSITORY, source_dir])
    if not os.path.isfile(os.path.join(source_dir, "freedv-reporter.py")):
        raise ReporterError("%s does not contain the FreeDV Reporter server" % source_dir)
    return source_dir


class _Relay:
    """Passes connections on to the server, so that they can be cut off
    without the server (or anyone connected to it directly) going away."""

    def __init__(self, server_port):
        self._server_port = server_port
        self._lock = threading.Lock()
        self._sockets = []
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.bind(("127.0.0.1", 0))
        self._listener.listen(8)
        self.port = self._listener.getsockname()[1]
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self):
        while True:
            try:
                client, _ = self._listener.accept()
            except OSError:
                return
            try:
                upstream = socket.create_connection(("127.0.0.1", self._server_port), timeout=5)
                upstream.settimeout(None)
            except OSError:
                client.close()
                continue
            with self._lock:
                self._sockets += [client, upstream]
            for source, destination in ((client, upstream), (upstream, client)):
                threading.Thread(target=self._forward, args=(source, destination), daemon=True).start()

    def _forward(self, source, destination):
        try:
            while True:
                data = source.recv(65536)
                if not data:
                    break
                destination.sendall(data)
        except OSError:
            pass
        for connection in (source, destination):
            self._close(connection)

    def _close(self, connection):
        with self._lock:
            if connection in self._sockets:
                self._sockets.remove(connection)
        try:
            connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        connection.close()

    def cut(self):
        with self._lock:
            connections = list(self._sockets)
        for connection in connections:
            self._close(connection)

    def stop(self):
        self._listener.close()
        self.cut()


class ReporterServer:
    """Runs a FreeDV Reporter server for the duration of a "with" block."""

    def __init__(self):
        _require_packages()
        self._source_dir = server_source_dir()
        self._work_dir = None
        self._process = None
        self._log_file = None
        self._stations = []
        self._relay = None

        #: The port that the server is listening on.
        self.port = None

    @property
    def hostname(self):
        """What to set FreeDV's FreeDV Reporter hostname to."""
        return "127.0.0.1:%d" % self._relay.port

    def interrupt_freedv(self):
        """Cuts FreeDV's connection to the server, as a network problem
        would. The server carries on, as does everyone else connected to it;
        FreeDV reconnects by itself a few seconds later."""
        self._relay.cut()

    def __enter__(self):
        try:
            # The server keeps its files in the folder it's run from, so give
            # it one of its own rather than the one containing its source.
            self._work_dir = tempfile.mkdtemp(prefix="freedvtest-reporter-")
            for name in ("index.html", "static"):
                source, link = os.path.join(self._source_dir, name), os.path.join(self._work_dir, name)
                try:
                    os.symlink(source, link)
                except OSError:
                    # Windows only lets some users make links.
                    if os.path.isdir(source):
                        shutil.copytree(source, link)
                    else:
                        shutil.copy2(source, link)
            with open(os.path.join(self._source_dir, "db-schema.sql"), "r") as f:
                database = sqlite3.connect(os.path.join(self._work_dir, "freedv-reporter.db"))
                database.executescript(f.read())
                database.close()
            with open(os.path.join(self._work_dir, "freedv-reporter.conf"), "w") as f:
                f.write("[discord]\nenabled = no\n")

            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
                probe.bind(("127.0.0.1", 0))
                self.port = probe.getsockname()[1]

            self._log_file = open(os.path.join(self._work_dir, "server.log"), "wb")
            self._start()

            # FreeDV connects by way of this (see interrupt_freedv()).
            self._relay = _Relay(self.port)
        except BaseException:
            self.__exit__(None, None, None)
            raise
        return self

    def _start(self):
        launcher = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_reporter_launcher.py")
        self._process = subprocess.Popen(
            [sys.executable, launcher, self._source_dir, str(self.port)],
            cwd=self._work_dir, stdin=subprocess.DEVNULL, stdout=self._log_file, stderr=subprocess.STDOUT)

        deadline = time.monotonic() + 30
        while True:
            if self._process.poll() is not None:
                raise ReporterError("The FreeDV Reporter server did not start:\n%s" % self.log())
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=1).close()
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise ReporterError("The FreeDV Reporter server did not start:\n%s" % self.log())
                time.sleep(0.1)

    def _stop(self):
        for station in list(self._stations):
            station.disconnect()
        if self._process is not None:
            if self._process.poll() is None:
                self._process.terminate()
                try:
                    self._process.wait(10)
                except subprocess.TimeoutExpired:
                    self._process.kill()
                    self._process.wait()
            self._process = None

    def restart(self):
        """Stops the server and starts it again on the same port, as if it
        (or the connection to it) had gone away for a moment. Everyone is
        disconnected; FreeDV reconnects by itself, but stations have to be
        created again."""
        self._stop()
        self._start()

    def __exit__(self, exc_type, exc_value, traceback):
        if self._relay is not None:
            self._relay.stop()
            self._relay = None
        self._stop()
        if self._log_file is not None:
            self._log_file.close()
            self._log_file = None
        if self._work_dir is not None:
            shutil.rmtree(self._work_dir, ignore_errors=True)
            self._work_dir = None
        return False

    def log(self):
        """Returns everything that the server has printed so far."""
        try:
            self._log_file.flush()
            with open(os.path.join(self._work_dir, "server.log"), "r", errors="replace") as f:
                return f.read()
        except (OSError, AttributeError, TypeError):
            return ""

    def station(self, callsign, grid_square, version="freedvtest", rx_only=False):
        """Connects a station to the server, as if someone else were running
        FreeDV. Disconnects at the end of a "with" block, or when the server
        is stopped."""
        station = Station(self, callsign, grid_square, version, rx_only)
        self._stations.append(station)
        return station

    def viewer(self):
        """Connects to the server without reporting anything, as the FreeDV
        Reporter website does. Useful for seeing what FreeDV itself reports."""
        station = Station(self, None, None, None, True)
        self._stations.append(station)
        return station


class Station:
    """Someone else connected to the FreeDV Reporter server. Create with
    ReporterServer.station() or ReporterServer.viewer()."""

    def __init__(self, server, callsign, grid_square, version, rx_only):
        import socketio

        self._server = server
        self._lock = threading.Lock()
        self._arrived = threading.Condition(self._lock)
        self._events = []
        self._users = {}

        #: This station's callsign (None for viewers).
        self.callsign = callsign

        # The client is asynchronous, so it gets a thread of its own to run in.
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._loop.run_forever, daemon=True)
        self._thread.start()

        # Created in that thread: with Python 3.9 (macOS's own), what it
        # creates belongs to the event loop of whichever thread it's created
        # in, and messages it's given to send would otherwise never go.
        async def create_client():
            return socketio.AsyncClient(reconnection=False)

        self._client = self._run(create_client())
        self._client.on("*", self._on_event)

        if callsign is None:
            auth = {"role": "view", "protocol_version": PROTOCOL_VERSION}
        else:
            auth = {
                "role": "report",
                "callsign": callsign,
                "grid_square": grid_square,
                "version": version,
                "rx_only": rx_only,
                "os": "linux",
                "protocol_version": PROTOCOL_VERSION,
            }
        self._run(self._client.connect(
            "http://127.0.0.1:%d" % server.port, auth=auth, transports=["websocket"], wait_timeout=10))
        self.wait_for_event("connection_successful")

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.disconnect()
        return False

    def _run(self, coroutine, timeout=15):
        return asyncio.run_coroutine_threadsafe(coroutine, self._loop).result(timeout)

    async def _on_event(self, event, *args):
        data = args[0] if args else None
        with self._lock:
            # Several updates can arrive bundled together.
            updates = data if event == "bulk_update" else [[event, data]]
            for name, update in updates:
                self._events.append((name, update))
                if name == "new_connection":
                    self._users[update["sid"]] = update["callsign"]
                elif name == "remove_connection":
                    self._users.pop(update["sid"], None)
            self._arrived.notify_all()

    # -- What this station reports ----------------------------------------

    def set_frequency(self, frequency_hz, sideband=None):
        """Reports a frequency and, if given, the sideband ("USB", "LSB",
        or "" for not known) the station's radio is on."""
        data = {"freq": frequency_hz}
        if sideband is not None:
            data["sideband"] = sideband
        self._run(self._client.emit("freq_change", data))

    def transmit(self, mode, transmitting):
        self._run(self._client.emit("tx_report", {"mode": mode, "transmitting": transmitting}))

    def receive(self, callsign, mode, snr):
        """Reports having received the given callsign."""
        self._run(self._client.emit("rx_report", {"callsign": callsign, "mode": mode, "snr": snr}))

    def set_message(self, message):
        self._run(self._client.emit("message_update", {"message": message}))

    def request_qsy(self, callsign, frequency_hz, message=""):
        """Asks the given station to change to a frequency."""
        self._run(self._client.emit(
            "qsy_request", {"dest_sid": self.sid_of(callsign), "frequency": frequency_hz, "message": message}))

    def hide(self):
        self._run(self._client.emit("hide_self"))

    def show(self):
        self._run(self._client.emit("show_self"))

    def disconnect(self):
        if self._client is None:
            return
        try:
            self._run(self._client.disconnect())
        except Exception:
            pass
        self._client = None
        self._loop.call_soon_threadsafe(self._loop.stop)
        self._thread.join(5)
        if not self._thread.is_alive():
            self._loop.close()
        if self in self._server._stations:
            self._server._stations.remove(self)

    # -- What this station hears from the server ---------------------------

    def events(self, name=None, callsign=None):
        """Returns the data of the updates received so far, oldest first,
        optionally only those of one kind and/or about one station."""
        with self._lock:
            return self._events_of(name, callsign)

    def _events_of(self, name, callsign):
        return [
            data for event, data in self._events
            if (name is None or event == name) and
               (callsign is None or (isinstance(data, dict) and self._callsign_of(data) == callsign))]

    def _callsign_of(self, data):
        return data.get("callsign") or self._users.get(data.get("sid"))

    def callsigns(self):
        """Returns the callsigns of the stations that the server currently
        lists (which doesn't include any that are hiding)."""
        with self._lock:
            return sorted(self._users.values())

    def sid_of(self, callsign):
        """Returns the server's identifier for a station that's connected."""
        with self._lock:
            for sid, user in self._users.items():
                if user == callsign:
                    return sid
        raise ReporterError("%s is not connected to the FreeDV Reporter server" % callsign)

    def wait_for_event(self, name, callsign=None, where=None, timeout=15):
        """Waits for an update of the given kind (optionally about one
        station and/or satisfying where(data)) and returns the latest one's
        data."""
        deadline = time.monotonic() + timeout
        with self._arrived:
            while True:
                matches = [data for data in self._events_of(name, callsign) if where is None or where(data)]
                if matches:
                    return matches[-1]
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise ReporterError(
                        "Timed out waiting for '%s'%s from the FreeDV Reporter server"
                        % (name, " about %s" % callsign if callsign else ""))
                self._arrived.wait(remaining)
