"""Drives FreeDV's GUI from test scripts.

start() launches FreeDV and returns an object that performs UI actions (e.g.
clicking a button) and retrieves the state of the UI and of FreeDV itself:

    import freedvtest

    with freedvtest.start() as app:
        app.wait_for_window("wizard")
        app.click("wizard.btnNext")
        ...
        app.click("main.btnOnOff")
        app.wait_until(lambda: app.state()["running"])
        app.quit()

Controls are referred to by the names given to them in FreeDV's source code
with SetName(); widgets() lists those in a window. Windows that have no name
of their own (e.g. dialogs provided by wxWidgets) can be referred to by
"@<window title>", and the controls in them by "@<window title>/<label>" or
"@<window title>/<class>", e.g. "@Add Message/OK".

A failed check or action raises an exception, so a script that runs to
completion has passed.

FreeDV must have been built with -DUNITTEST=1.
"""

import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time


# What a sanitizer's report starts with (the same as ctest goes by: see
# freedv_set_test_pass_criteria in CMakeLists.txt).
_SANITIZER_REPORT = re.compile(
    r"WARNING: ThreadSanitizer|ERROR: ThreadSanitizer|FATAL: ThreadSanitizer|ERROR: AddressSanitizer|"
    r"ERROR: LeakSanitizer|ERROR: RealtimeSanitizer|UndefinedBehaviorSanitizer|runtime error:")


class FreeDVError(Exception):
    """FreeDV could not be started, stopped responding or exited."""


class CommandError(FreeDVError):
    """FreeDV could not carry out a command, e.g. clicking a disabled button."""


def default_binary():
    """Returns the FreeDV to test: $FREEDV_BINARY, or the one in the build
    folder that the script was run from."""
    binary = os.environ.get("FREEDV_BINARY")
    if binary:
        return binary
    if sys.platform == "darwin":
        return "src/FreeDV.app/Contents/MacOS/FreeDV"
    if sys.platform == "win32":
        return os.path.join("src", "freedv.exe")
    return "src/freedv"


def shim_path(name):
    """Returns the library the build made from freedvtest/<name>.c, for
    loading into FreeDV: in $FREEDVTEST_SHIMS, or test/gui in the build folder
    that the script was run from. These are built with FreeDV's tests
    (-DUNITTEST=1)."""
    folder = os.environ.get("FREEDVTEST_SHIMS") or os.path.join("test", "gui")
    return os.path.abspath(os.path.join(folder, name + ".so"))


# Somewhere for FreeDV to look for a FreeDV Reporter server and not find one.
NO_REPORTER_SERVER = "127.0.0.1:9"


def default_config_path(home):
    """Returns where FreeDV looks for its configuration when not told which
    to use, for the given stand-in home folder (see start()): where it has
    always kept it on Linux (newer builds move it to where such things now
    go), where macOS keeps programs' settings, and on Windows, where test
    builds keep what would otherwise be in the registry (see
    $FREEDV_TEST_CONFIG_DIR in FreeDV's main.cpp)."""
    if sys.platform == "darwin":
        return os.path.join(home, "Library", "Preferences", "FreeDV Preferences")
    if sys.platform == "win32":
        return os.path.join(home, "FreeDV.ini")
    return os.path.join(home, ".freedv")


def _escape(value):
    """Writes a value as wxFileConfig reads it (see FilterOutValue() in
    wxWidgets' fileconf.cpp), in which a backslash starts an escape sequence:
    Windows' paths are full of them."""
    value = str(value)
    quote = bool(value) and (value[0].isspace() or value[0] == '"')
    escaped = ""
    for character in value:
        if character in "\n\r\t\\" or (character == '"' and quote):
            escaped += "\\" + {"\n": "n", "\r": "r", "\t": "t"}.get(character, character)
        else:
            escaped += character
    return '"%s"' % escaped if quote else escaped


def _unescape(value):
    """Reads a value as wxFileConfig does (see FilterInValue())."""
    quoted = value.startswith('"')
    result = ""
    characters = iter(value[1:] if quoted else value)
    for character in characters:
        if character == "\\":
            escaped = next(characters, "")
            result += {"n": "\n", "r": "\r", "t": "\t", "\\": "\\", '"': '"'}.get(escaped, "")
        elif character != '"' or not quoted:
            result += character
    return result


def write_config(path, settings):
    """Creates a FreeDV configuration file. settings maps the names of
    settings as they appear in FreeDV's source code (e.g.
    "/Reporting/Callsign") to their values. Anything not given has its
    default value, other than:

    - the Setup Wizard isn't shown on startup ("/FirstTimeUse": True for it
      to be);
    - FreeDV doesn't connect to the public FreeDV Reporter server, which it
      otherwise does as soon as it's launched. It looks for one at
      NO_REPORTER_SERVER instead (give "/Reporting/FreeDV/Hostname" and
      "/Reporting/FreeDV/UseTls" for somewhere else)."""
    settings = dict(settings)
    if "/Reporting/FreeDV/Hostname" not in settings:
        settings["/Reporting/FreeDV/Hostname"] = NO_REPORTER_SERVER
        settings.setdefault("/Reporting/FreeDV/UseTls", False)
    groups = {"": {"FirstTimeUse": 0}}
    for name, value in settings.items():
        group, _, key = name.strip("/").rpartition("/")
        if isinstance(value, bool):
            value = int(value)
        groups.setdefault(group, {})[key] = value

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w") as f:
        for group in sorted(groups):
            if group:
                f.write("[%s]\n" % group)
            for key, value in groups[group].items():
                f.write("%s=%s\n" % (key, _escape(value)))


def read_config(path):
    """Returns the settings in a FreeDV configuration file, by the names
    they have in FreeDV's source code (e.g. "/Reporting/Callsign"). Values
    are returned as text."""
    settings = {}
    group = ""
    with open(path, "r") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("[") and line.endswith("]"):
                group = "/" + line[1:-1]
            elif "=" in line:
                key, _, value = line.partition("=")
                settings["%s/%s" % (group, key)] = _unescape(value)
    return settings


def start(binary=None, config=None, settings=None, args=(), env=None, user_config_files=None, timeout=60,
          home=None, default_config=False):
    """Launches FreeDV and returns once it's ready to take commands.

    config is the configuration file to use. If not given, FreeDV starts
    without one, as it does when run for the first time, unless settings
    (see write_config()) are given to create one from.

    user_config_files maps names to contents of files to place where FreeDV
    looks for other programs' settings (see FreeDV.user_config_dir). FreeDV
    never sees the real ones.

    home is a folder to stand in for the user's home folder, for when what FreeDV keeps there needs to last from one launch to the
    next; otherwise each launch gets an empty one of its own. With
    default_config, FreeDV isn't told which configuration file to use and so
    finds its own, as it does for a user: in that home folder (see
    default_config_path()).
    """
    return FreeDV(binary or default_binary(), config, settings, args, env, user_config_files, timeout, home,
                  default_config)


class FreeDV:
    """A running copy of FreeDV. Create with start()."""

    def __init__(self, binary, config, settings, args, env, user_config_files, timeout, home=None,
                 default_config=False):
        self._temp_dir = tempfile.mkdtemp(prefix="freedvtest-")
        self._next_id = 1
        self._received = b""
        self._connection = None
        self._process = None
        self._log_file = None
        self._log_shown = False

        #: The configuration file that FreeDV is using.
        self.config_path = config or os.path.join(self._temp_dir, "freedv.conf")
        #: Everything that FreeDV has printed.
        self.log_path = os.path.join(self._temp_dir, "freedv.log")
        #: Stands in for where FreeDV looks for other programs' settings:
        #: the user's ~/.config on Linux, ~/Library/Preferences on macOS,
        #: %LOCALAPPDATA% on Windows. See user_config_path().
        self.user_config_dir = os.path.join(home, ".config") if home else os.path.join(self._temp_dir, "config")
        #: Stands in for the user's home folder.
        self.home_dir = None

        if settings is not None:
            write_config(self.config_path, settings)
        elif config is None:
            # As when run for the first time, other than keeping to itself.
            write_config(self.config_path, {"/FirstTimeUse": True})

        process_env = dict(os.environ)
        # Labels checked by test scripts are the English ones.
        process_env.update({"LANGUAGE": "en", "LC_ALL": "C.UTF-8", "LANG": "C.UTF-8"})
        # Use the X display that the test was run on (e.g. Xvfb) even if a
        # Wayland session also exists, which GTK would otherwise prefer.
        # Moving the mouse pointer and activating windows also need X.
        if sys.platform.startswith("linux"):
            process_env["GDK_BACKEND"] = "x11"
        # Keep FreeDV away from settings belonging to whoever is running the
        # test, e.g. those of other programs that the Setup Wizard can import.
        # The same goes for their home folder, which is where FreeDV keeps
        # (among other things) its default configuration. The display may
        # need what's in the real one to be connected to, though.
        if sys.platform.startswith("linux"):
            xauthority = os.path.expanduser("~/.Xauthority")
            if "XAUTHORITY" not in process_env and os.path.exists(xauthority):
                process_env["XAUTHORITY"] = xauthority
        self.home_dir = home or os.path.join(self._temp_dir, "home")
        os.makedirs(self.home_dir, exist_ok=True)
        if sys.platform.startswith("linux") or sys.platform == "darwin":
            process_env["HOME"] = self.home_dir
        if sys.platform == "win32":
            # What would be in the registry is kept in the stand-in home
            # folder instead, and other programs' settings in a stand-in
            # %LOCALAPPDATA% there.
            process_env["FREEDV_TEST_CONFIG_DIR"] = self.home_dir
            self.user_config_dir = os.path.join(self.home_dir, "AppData", "Local")
            process_env["LOCALAPPDATA"] = self.user_config_dir
        if sys.platform == "darwin":
            # macOS finds the home folder (and so ~/Library/Preferences, where
            # FreeDV keeps its settings) without looking at $HOME.
            process_env["CFFIXED_USER_HOME"] = self.home_dir
            self.user_config_dir = os.path.join(self.home_dir, "Library", "Preferences")
        os.makedirs(self.user_config_dir, exist_ok=True)
        for name, contents in (user_config_files or {}).items():
            path = self.user_config_path(name)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write(contents)
        process_env["XDG_CONFIG_HOME"] = self.user_config_dir
        process_env.update(env or {})

        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(0.2)
            port = listener.getsockname()[1]

            self._log_file = open(self.log_path, "wb")
            self._process = subprocess.Popen(
                [binary] + ([] if default_config else ["-f", self.config_path]) +
                ["-testport", str(port)] + list(args),
                stdin=subprocess.DEVNULL, stdout=self._log_file, stderr=subprocess.STDOUT,
                env=process_env)

            deadline = time.monotonic() + timeout
            while self._connection is None:
                try:
                    self._connection, _ = listener.accept()
                except socket.timeout:
                    if self._process.poll() is not None:
                        raise FreeDVError(
                            "FreeDV exited with code %d before connecting:\n%s"
                            % (self._process.returncode, self.log()))
                    if time.monotonic() > deadline:
                        raise FreeDVError(
                            "FreeDV did not connect within %d seconds:\n%s" % (timeout, self.log()))
            self._connection.settimeout(timeout)
            self.command("ping")
        except BaseException:
            self.stop()
            raise
        finally:
            listener.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        if exc_type is not None and not issubclass(exc_type, (KeyboardInterrupt, SystemExit)):
            self._show_log()
        self.stop()
        return False

    def user_config_path(self, name):
        """Returns where FreeDV looks for another program's settings file of
        the given name (e.g. "WSJT-X.ini"): in user_config_dir or, on
        Windows, a folder there named after the program."""
        if sys.platform == "win32":
            return os.path.join(self.user_config_dir, os.path.splitext(name)[0], name)
        return os.path.join(self.user_config_dir, name)

    # -- Commands ---------------------------------------------------------

    def command(self, cmd, **params):
        """Sends a command to FreeDV and returns its result."""
        if self._connection is None:
            raise FreeDVError("FreeDV is not running")

        request = {"id": self._next_id, "cmd": cmd}
        request.update({key: value for key, value in params.items() if value is not None})
        self._next_id += 1

        try:
            self._connection.sendall(json.dumps(request).encode("utf-8") + b"\n")
            while b"\n" not in self._received:
                data = self._connection.recv(65536)
                if not data:
                    raise FreeDVError("FreeDV closed the connection during '%s'" % cmd)
                self._received += data
        except OSError as e:
            raise FreeDVError("No response from FreeDV to '%s': %s" % (cmd, e))

        line, self._received = self._received.split(b"\n", 1)
        response = json.loads(line.decode("utf-8"))
        if response.get("id") != request["id"]:
            raise FreeDVError("Unexpected response to '%s': %r" % (cmd, response))
        if not response.get("ok"):
            raise CommandError(response.get("error", "unknown error"))
        return response.get("result")

    def windows(self):
        """Returns FreeDV's top-level windows (the main window and dialogs)."""
        return self.command("list_windows")

    def window_exists(self, name):
        """Returns whether the given top-level window is currently displayed."""
        return any(
            window["shown"] and name in (window["name"], "@" + window["label"])
            for window in self.windows())

    def widgets(self, window, named_only=True):
        """Returns the controls inside the given window. Useful for finding
        out what a test script can refer to."""
        widgets = self.command("list_widgets", name=window)
        if named_only:
            # Controls that weren't given a name have wxWidgets' default one.
            widgets = [widget for widget in widgets if "." in widget["name"]]
        return widgets

    def get(self, name):
        """Returns the state of a window or control: its class, label,
        whether it's enabled and shown and, depending on what it is, its
        value, selection and items."""
        return self.command("get", name=name)

    def click(self, name, item=None, button=None):
        """Clicks a button, toggle button, checkbox or radio button, or the
        given item (counting from 0) of a list. Lists with columns can also
        be clicked with the "right" button, and below their items (by not
        giving one)."""
        self.command("click", name=name, item=item, button=button)

    def set_value(self, name, value=None, data=None, index=None):
        """Sets a control's value: True/False for checkboxes and toggle
        buttons, or text for text fields and combo boxes. Items in lists can
        also be chosen by position (index) or by the string attached to them
        (data), e.g. an audio device's internal name."""
        self.command("set_value", name=name, value=value, data=data, index=index)

    def activate(self, name, timeout=10):
        """Brings a top-level window to the front, as clicking on it would,
        and waits for that to have happened."""
        # Window managers are wary of windows asking for attention (e.g.
        # just after another window has been given it), so keep asking.
        deadline = time.monotonic() + timeout
        while not self.get(name)["active"]:
            if time.monotonic() > deadline:
                raise FreeDVError("Timed out waiting for window '%s' to become active" % name)
            self.command("activate", name=name)
            try:
                self.wait_until(lambda: self.get(name)["active"], 1)
            except FreeDVError:
                pass

    def close(self, name):
        """Closes a top-level window as its title bar's close button would,
        or something that's popped up as clicking elsewhere would."""
        self.command("close", name=name)

    def menu(self, path, window=None):
        """Selects a menu item, e.g. "Settings/Setup Wizard...", from the
        main window's menu bar or that of the named window."""
        self.command("menu", path=path, name=window)

    def menu_checked(self, path, window=None):
        """Returns whether a menu item has a check mark."""
        return self.command("menu", path=path, name=window, query=True)["checked"]

    def context_menu(self, name, choose=None, item=None):
        """Right-clicks a control (or the given item of a list box) and
        returns the items of the popup menu that results, each with a
        "label" and whether it's "enabled" and "checked". The menu itself
        isn't displayed; if choose is the label of one of its items, that
        item is selected from it."""
        self.command("context_menu", name=name, choose=choose, item=item)
        return self.command("get_popup_menu")["items"]

    def combo_popup(self, name, open=True):
        """Opens or closes the list of a control that has its own kind of
        drop-down list, e.g. FreeDV Reporter's status message, and waits
        for that to have happened. (On Windows, the list slides open, and
        ignores the mouse until it's done.)"""
        self.command("combo_popup", name=name, open=open)
        self.wait_until(lambda: self.get(name).get("popupShown", open) == open, 10,
                        "the list of '%s' to %s" % (name, "open" if open else "close"))

    # -- Lists with columns -------------------------------------------------

    def columns(self, name):
        """Returns a list's columns from left to right, each with its "id",
        "title", whether it's "shown" and how it's sorted ("sort")."""
        return sorted(self.get(name)["columns"], key=lambda column: column["position"])

    def rows(self, name):
        """Returns the rows of a list from top to bottom. Each is a dict of
        the text in each column, keyed by the column's ID, plus the row's
        "background" and "foreground" colors if it has its own and whether
        it's "selected"."""
        state = self.get(name)
        rows = []
        for position, row in enumerate(state["rows"]):
            cells = {int(column): text for column, text in row["cells"].items()}
            cells["background"] = row.get("background")
            cells["foreground"] = row.get("foreground")
            cells["selected"] = (position == state["selection"])
            rows.append(cells)
        return rows

    def _row_action(self, name, action, row, column, choose=None):
        # A row is its position (counting from 0) or, as that can change at
        # any moment in a list that's kept sorted, a (column ID, text) pair
        # identifying it by what's in one of its columns.
        params = {"row": row}
        if isinstance(row, tuple):
            params = {"match_column": row[0], "match_text": row[1]}

        # A row that had to be scrolled into view isn't there straight away.
        deadline = time.monotonic() + 5
        while True:
            try:
                self.command("data_view", name=name, action=action, column=column, choose=choose, **params)
                return
            except CommandError as e:
                scrolling = "is not being displayed" in str(e) or "could not move the mouse pointer" in str(e)
                if not scrolling or time.monotonic() > deadline:
                    raise
                time.sleep(0.1)

    def hover_row(self, name, row, column=None):
        """Moves the mouse pointer over a row of a list: either its position
        (counting from 0) or a (column ID, text) pair to find it by. The
        window that the list is in needs to be active (see activate())."""
        self._row_action(name, "hover", row, column)

    def select_row(self, name, row, column=None):
        """Clicks on a row of a list."""
        self._row_action(name, "select", row, column)

    def activate_row(self, name, row, column=None):
        """Double-clicks on a row of a list."""
        self._row_action(name, "activate", row, column)

    def row_context_menu(self, name, row, column=None, choose=None):
        """Right-clicks on a row of a list. See context_menu()."""
        self._row_action(name, "context_menu", row, column, choose)
        return self.command("get_popup_menu")["items"]

    def click_column(self, name, column):
        """Clicks on a column's heading, which sorts the list by it."""
        self.command("data_view", name=name, action="header_click", column=column)

    def column_context_menu(self, name, column, choose=None):
        """Right-clicks on a column's heading. See context_menu()."""
        self.command("data_view", name=name, action="header_context_menu", column=column, choose=choose)
        return self.command("get_popup_menu")["items"]

    def state(self):
        """Returns FreeDV's own state (e.g. whether the modem is running)
        and its current configuration."""
        return self.command("get_state")

    def message_boxes(self, clear=False):
        """Returns the message boxes FreeDV has tried to display so far.
        They aren't actually displayed; FreeDV carries on as if their default
        button had been pressed (but see answer_next_message_box())."""
        return self.command("message_boxes", clear=clear)

    def path_dialogs(self, clear=False):
        """Returns the titles of the file and folder choosers that FreeDV has
        tried to display so far. They aren't actually displayed; FreeDV
        carries on as if they had been cancelled, unless choose_next_path()
        said otherwise."""
        return self.command("path_dialogs", clear=clear)

    def choose_next_path(self, path, **options):
        """Has FreeDV carry on from its next file or folder chooser as if the
        given path had been chosen (or, if None, as if it had been
        cancelled). options are for whatever else the chooser offers, e.g.
        loop=True when choosing a file to play."""
        if path is None:
            self.command("path_dialogs", cancel=True)
        else:
            self.command("path_dialogs", choose=path, options=options or None)

    def browser_urls(self, clear=False):
        """Returns the addresses of the web pages that FreeDV has tried to
        open so far. They aren't actually opened."""
        return self.command("browser_urls", clear=clear)

    def focus(self, name, lost=False):
        """Tells a control that it's been given the keyboard focus (or, with
        lost, that it's been taken away again), as clicking in it or tabbing
        to it would."""
        self.command("focus", name=name, lost=lost or None)

    def clipboard(self):
        """Returns the text that's on the clipboard, as FreeDV sees it."""
        return self.command("clipboard")["text"]

    def mouse(self, name, event, x=None, y=None, rotation=None):
        """Sends a mouse event to a control: "motion", "enter", "leave",
        "left_down", "left_up", "left_dclick", "right_down", "right_up",
        "right_dclick", "middle_down", "middle_up" or "wheel". x and y say
        where, as fractions of the control's width and height (its middle if
        not given); rotation is how many notches the wheel is turned, with
        positive being away from the user."""
        self.command("mouse", name=name, event=event, x=x, y=y, rotation=rotation)

    def key(self, name, key, hold=False, release=False):
        """Presses and releases a key while the given control has the
        keyboard focus. With hold, the key is only pressed; with release, a
        key that's being held is released. key is a single character, one of
        Escape, Space, Tab, Enter, Backspace, Delete, Insert, Home, End,
        PageUp, PageDown, Shift, Control, Up, Down, Left, Right and F1 to F12,
        or (as a number) wxWidgets' code for any other key."""
        self.command("key", name=name, key=key, hold=hold or None, release=release or None)

    def answer_next_message_box(self, answer):
        """Has FreeDV carry on from its next message box as if the given
        button ("yes", "no", "ok" or "cancel") had been pressed."""
        self.command("message_boxes", answer=answer)

    # -- Waiting ----------------------------------------------------------

    def wait_until(self, condition, timeout=30, description=None):
        """Waits for condition() to return something true, and returns it."""
        deadline = time.monotonic() + timeout
        while True:
            result = condition()
            if result:
                return result
            if time.monotonic() > deadline:
                raise FreeDVError("Timed out waiting for %s" % (description or "condition"))
            time.sleep(0.1)

    def wait_for_window(self, name, timeout=30):
        """Waits for a top-level window to be displayed and returns its state."""
        self.wait_until(lambda: self.window_exists(name), timeout, "window '%s' to open" % name)
        return self.get(name)

    def wait_for_window_closed(self, name, timeout=30):
        """Waits for a top-level window to go away and, if it was a dialog
        that FreeDV was waiting on, for FreeDV to have carried on from it
        (e.g. to have acted on what was chosen)."""
        self.wait_until(lambda: not self.window_exists(name), timeout, "window '%s' to close" % name)

        def carried_on():
            if not self.state().get("inNestedEventLoop"):
                return True
            # Unless another such dialog is still open, which is fine.
            return any(window["shown"] and window.get("modal") for window in self.windows())

        self.wait_until(carried_on, timeout, "FreeDV to carry on after window '%s' closed" % name)

    # -- Lifetime ---------------------------------------------------------

    @property
    def pid(self):
        """FreeDV's process ID."""
        return self._process.pid

    def log(self):
        """Returns everything that FreeDV has printed so far."""
        if self._log_file is not None:
            self._log_file.flush()
        try:
            with open(self.log_path, "r", errors="replace") as f:
                return f.read()
        except OSError:
            return ""

    def wait_for_exit(self, timeout=60):
        """Waits for FreeDV to exit of its own accord (e.g. after File > Exit
        was chosen) and returns its exit code."""
        try:
            return self._process.wait(timeout)
        except subprocess.TimeoutExpired:
            raise FreeDVError("FreeDV did not exit within %d seconds%s" % (timeout, self._stacks()))

    def system_colours_changed(self):
        """Tells FreeDV's windows that the system's colours have changed, as
        happens when the desktop switches between light and dark themes."""
        self.command("system_colours_changed")

    def quit(self, timeout=60):
        """Closes FreeDV's main window, waits for FreeDV to exit and returns
        its exit code."""
        self.command("quit")
        try:
            return self._process.wait(timeout)
        except subprocess.TimeoutExpired:
            raise FreeDVError("FreeDV did not exit within %d seconds%s" % (timeout, self._stacks()))

    def _stacks(self):
        """Returns what each of FreeDV's threads is doing, for working out
        why it's stuck, if there's something installed that can tell."""
        for tool in (["eu-stack", "-p"], ["pstack"]):
            try:
                result = subprocess.run(tool + [str(self._process.pid)], stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, timeout=60)
            except (OSError, subprocess.TimeoutExpired):
                continue
            output = result.stdout.decode("utf-8", errors="replace")
            if result.returncode == 0 and output.strip():
                return "\n\nThreads:\n" + output
        return ""

    def _show_log(self):
        """Writes everything that FreeDV printed to stderr, once."""
        if not self._log_shown:
            self._log_shown = True
            sys.stderr.write("----- FreeDV output -----\n%s\n-------------------------\n" % self.log())

    def stop(self):
        """Makes sure FreeDV is no longer running and cleans up after it.
        Called automatically at the end of a "with" block."""
        if self._process is not None and self._process.poll() is None:
            self._process.kill()
            self._process.wait()

        # FreeDV's output is only shown when something goes wrong, but in a
        # sanitizer build a sanitizer's report is what decides whether the
        # test passes, so it's shown whenever there's one of those in it.
        if self._process is not None and _SANITIZER_REPORT.search(self.log()):
            self._show_log()
        if self._connection is not None:
            self._connection.close()
            self._connection = None
        if self._log_file is not None:
            self._log_file.close()
            self._log_file = None
        shutil.rmtree(self._temp_dir, ignore_errors=True)
