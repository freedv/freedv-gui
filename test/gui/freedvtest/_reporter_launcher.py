"""Runs the FreeDV Reporter server for freedvtest.reporter.ReporterServer.

    python3 _reporter_launcher.py <server source folder> <port>

The server's own source is used as is; this only arranges for it to listen
on the given port and to do without its optional link to Discord. Two
environment variables make timestamps the server stamps itself come out
otherwise, for the Reporter list's time comparators (see freedvtest.reporter):

  FREEDVTEST_REPORTER_FIXED_TIME=<iso 8601>     every station shares one time
  FREEDVTEST_REPORTER_BAD_TIME_CALLSIGN=a,b     these report no last_update
"""

import os
import runpy
import sys
import types


def main():
    source_dir, port = sys.argv[1], int(sys.argv[2])

    # The server imports the Discord library even when its Discord link is
    # turned off (which it is here), so give it something to import.
    discord = types.ModuleType("discord")
    discord.Client = object
    discord.Intents = object
    discord.Embed = object
    sys.modules["discord"] = discord

    # The server always listens on aiohttp's default port. And when stopped,
    # it needn't wait (by default, up to a minute) for whoever's connected to
    # go first: stopping is meant to be as if it had gone away.
    from aiohttp import web
    run_app = web.run_app
    web.run_app = lambda app, **kwargs: run_app(app, **dict(kwargs, port=port, shutdown_timeout=0.1))

    sys.path.insert(0, source_dir)

    # The server stamps connect_time and last_update itself, so a test can't
    # line two stations up to the millisecond or make one of them unreadable --
    # both of which the Reporter list's sorting needs to compare. Patch the
    # module the server is about to import for its own use.
    import datetime
    import user_info

    fixed = os.environ.get("FREEDVTEST_REPORTER_FIXED_TIME")
    if fixed:
        instant = datetime.datetime.fromisoformat(fixed)
        user_info.datetime = types.SimpleNamespace(
            datetime=type("DateTime", (datetime.datetime,),
                          {"now": staticmethod(lambda tz=None: instant)}),
            timezone=datetime.timezone)

    bad = os.environ.get("FREEDVTEST_REPORTER_BAD_TIME_CALLSIGN")
    if bad:
        callsigns = {name.strip() for name in bad.split(",") if name.strip()}

        def without_a_time(send):
            def patched(self):
                data = send(self)
                if self.callsign in callsigns:
                    data["last_update"] = ""
                return data
            return patched

        for name in ("new_connection_data", "freq_change_data", "tx_report_data",
                     "message_report_data"):
            setattr(user_info.UserInfo, name, without_a_time(getattr(user_info.UserInfo, name)))

    sys.argv = [source_dir + "/freedv-reporter.py"]
    runpy.run_path(sys.argv[0], run_name="__main__")

    # The server never closes its database, whose thread would otherwise keep
    # Python running after the server has stopped (until it is killed).
    os._exit(0)


if __name__ == "__main__":
    main()
