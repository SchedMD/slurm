############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that a rollup manages Galera streaming replication on its connection.

Only the non-Galera case can run in SQA CI. The Galera cases need a Galera
server, so a passing run there does not mean they were exercised.

Run this only against a throwaway accounting DB.  sacctmgr rollup takes no
cluster argument, so every rollup here recomputes and overwrites usage rows
for every cluster in the DB over the window it covers.
"""

import re
from datetime import datetime, timedelta

import pytest

import atf

pytestmark = pytest.mark.slow

# Logged at debug2 by mysql_db_enable_streaming_replication() and
# mysql_db_restore_streaming_replication() in src/database/mysql_common.c.
# The wsrep_on probe is logged before the non-Galera early return, so it is
# emitted against any database.
WSREP_ON = "wsrep_on="
NOTHING_TO_RESTORE = "no streaming replication settings to restore"
FRAGMENT_SET = "set wsrep_trx_fragment_size="
UNIT_RESTORED = "Restored wsrep_trx_fragment_unit="
FRAGMENT_RESTORED = "Restored wsrep_trx_fragment_size="

# Logged at debug2 when wsrep_on is set but the wsrep_trx_fragment_* variables
# are absent, which is every Galera before 4.
NO_STREAMING_SUPPORT = "does not support streaming replication"

# A server without an optional variable answers the probe with MySQL error 1193
# ("Unknown system variable"). That is the expected answer rather than a
# failure, so a rollup must not report it at error(). These words catch
# anything it does.
ERROR_MARKERS = ("wsrep", "galera", "unknown system variable")

# How the server behind slurmdbd supports streaming replication.
NOT_GALERA = "not galera"
GALERA_NO_STREAMING = "galera without streaming replication"
GALERA_STREAMING = "galera with streaming replication"

# Logged once every per-cluster rollup thread has finished, which includes the
# restore call, so its arrival means everything above is already on disk.
ROLLUP_DONE = "Everything rolled up"

# A rollup can block behind one already holding usage_rollup_lock.
ROLLUP_TIMEOUT = 300


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Test setup with required configurations."""

    atf.require_version(
        (26, 11),
        component="sbin/slurmdbd",
        reason="Ticket 25746 - rollups manage streaming replication from 26.11",
    )
    # modify=True dumps the accounting database and restores it at teardown,
    # but only in auto-config mode. In local-config it makes the module skip
    # unless --allow-slurmdbd-modify was given, and with that flag the rollups
    # below run with nothing to restore them -- see the module docstring.
    atf.require_accounting(modify=True)

    # The messages under test are all logged at debug2.
    atf.require_config_parameter("DebugLevel", "debug2", source="slurmdbd")

    atf.require_slurm_running()


def slurmdbd_log():
    """Returns the path of the slurmdbd log file, or skips if it is unusable."""

    log = atf.get_config_parameter("LogFile", live=False, quiet=True, source="slurmdbd")

    # get_config_parameter casefolds the value it returns, so a path holding
    # any uppercase comes back wrong. test -f catches that as well as a
    # slurmdbd that logs to stdout or syslog instead of a file.
    present = log and atf.run_command_exit(
        f"test -f {log}", user=atf.properties["slurm-user"], quiet=True
    )
    if not log or present != 0:
        pytest.skip(
            "Ticket 25746: LogFile not configured or not present in slurmdbd.conf; "
            "cannot read the rollup's streaming replication messages"
        )

    return log


def server_streaming_support(mysql):
    """Returns how the server behind slurmdbd supports streaming replication.

    Asks the server directly, so a rollup that misdetects it cannot pick the
    assertions it is checked against.
    """

    # Session scope, as slurmdbd probes it: on Percona XtraDB Cluster wsrep_on
    # is a session-only variable and @@global.wsrep_on is an error.
    result = atf.run_command(
        f"{mysql} -Ns -e 'SELECT @@wsrep_on'",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    if result["exit_code"] != 0 or result["stdout"].strip() in ("", "0", "OFF"):
        return NOT_GALERA

    # Check wsrep_on first: MariaDB without Galera has these variables too.
    output = atf.run_command_output(
        f"{mysql} -Ns -e \"SHOW GLOBAL VARIABLES LIKE 'wsrep_trx_fragment_size'\"",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    if output.strip():
        return GALERA_STREAMING

    return GALERA_NO_STREAMING


def wsrep_errors(output):
    """Returns the lines a rollup logged at error() about wsrep or galera."""

    return [
        line
        for line in output.splitlines()
        if "error:" in line and any(m in line.lower() for m in ERROR_MARKERS)
    ]


def rollup_window():
    """Returns the last whole hour as (start, end) sacctmgr timestamps."""

    end = datetime.now().replace(minute=0, second=0, microsecond=0)
    start = end - timedelta(hours=1)
    fmt = "%Y-%m-%dT%H:%M:%S"
    return start.strftime(fmt), end.strftime(fmt)


def rollup(log, start, end):
    """Triggers one rollup and returns the slurmdbd log written while it ran."""

    offset = int(
        atf.run_command_output(
            f"wc -l < {log}",
            user=atf.properties["slurm-user"],
            fatal=True,
            quiet=True,
        ).strip()
    )

    # Longer than atf's default because a rollup can block behind one already
    # holding usage_rollup_lock, including the one slurmdbd runs at startup.
    atf.run_command(
        f"sacctmgr -i rollup {start} {end}",
        user=atf.properties["slurm-user"],
        timeout=ROLLUP_TIMEOUT,
        fatal=True,
    )

    # slurmdbd logs the completion line before it replies, so sacctmgr has
    # already returned by then and this normally matches on the first pass.
    # The loop only guards against the write not being visible to us yet.
    output = ""
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(
            f"tail -n +{offset + 1} {log}",
            user=atf.properties["slurm-user"],
            fatal=True,
            quiet=True,
        )
        if ROLLUP_DONE in output:
            break

    return output


def test_rollup_manages_streaming_replication(sql_statement_repeat):
    """Verify a rollup probes wsrep and leaves the session settings as it found them."""

    if not sql_statement_repeat:
        pytest.skip(
            "Ticket 25746: needs the conftest mysql helper, which requires auto-config"
        )

    log = slurmdbd_log()
    support = server_streaming_support(sql_statement_repeat)
    start, end = rollup_window()

    # Drain the rollup slurmdbd runs at startup so its messages cannot land in
    # the window we measure.
    rollup(log, start, end)

    output = rollup(log, start, end)

    assert (
        WSREP_ON in output
    ), f"slurmdbd should probe wsrep_on while rolling up, see {log}"

    match = re.search(rf"{WSREP_ON}(\d+)", output)
    assert match, f"slurmdbd should log the wsrep_on value, see {log}"

    # A rollup runs hourly, so probing for a variable this server may not have
    # must stay out of the log at error() on every branch below.
    errors = wsrep_errors(output)
    assert not errors, (
        f"slurmdbd should not report an absent server variable as an error, "
        f"got {errors}, see {log}"
    )

    assert bool(int(match.group(1))) == (support != NOT_GALERA), (
        f"slurmdbd should log a wsrep_on that matches the server ({support}), "
        f"got wsrep_on={match.group(1)}, see {log}"
    )

    if support == NOT_GALERA:
        # Not Galera. The probe is all slurmdbd does, and the restore must take
        # its early-out rather than writing a fragment size it never saved.
        assert (
            NOTHING_TO_RESTORE in output
        ), f"slurmdbd should have no settings to restore off Galera, see {log}"
        assert (
            FRAGMENT_SET not in output
        ), f"slurmdbd should not set a fragment size off Galera, see {log}"
        return

    if support == GALERA_NO_STREAMING:
        # Galera, but older than 4, so the wsrep_trx_fragment_* variables the
        # rollup wants are absent. It should give up before setting anything,
        # which leaves nothing for the restore to put back.
        assert (
            NO_STREAMING_SUPPORT in output
        ), f"slurmdbd should report no streaming replication support, see {log}"
        assert (
            FRAGMENT_SET not in output
        ), f"slurmdbd should not set a fragment size without support, see {log}"
        assert (
            NOTHING_TO_RESTORE in output
        ), f"slurmdbd should have no settings to restore without support, see {log}"
        return

    # Galera 4. Streaming replication should be enabled for the rollup and the
    # original session settings put back before the connection is closed.
    assert (
        NO_STREAMING_SUPPORT not in output
    ), f"slurmdbd should detect streaming replication support, see {log}"
    assert (
        FRAGMENT_SET in output
    ), f"slurmdbd should enable streaming replication for the rollup, see {log}"
    assert (
        UNIT_RESTORED in output
    ), f"slurmdbd should restore wsrep_trx_fragment_unit, see {log}"
    assert (
        FRAGMENT_RESTORED in output
    ), f"slurmdbd should restore wsrep_trx_fragment_size, see {log}"
