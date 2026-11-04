############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sacctmgr show reservation WithKV reports the reservation's KV."""

import glob
import json
import pathlib
import re
import shlex
import time

import pytest

import atf

RESV_NAME = "resv_kv_acct"
ARCHIVE_RESV_NAME = "resv_kv_archive"

# How far back the archive test moves the reservation. sacctmgr purges with
# hour granularity and truncates the cutoff to the top of the current hour, so
# a reservation that just ended is never in range and cannot be archived as-is.
ARCHIVE_BACKDATE_HOURS = 3


@pytest.fixture(scope="module", autouse=True)
def setup():
    # The archive test purges and reloads reservation records
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStoreFlags", "resv_kv")
    atf.require_version(
        (26, 11), "bin/scontrol", reason="Issue 51034: KV was added in 26.11"
    )
    atf.require_version(
        (26, 11), "bin/sacctmgr", reason="Issue 51034: KV was added in 26.11"
    )
    atf.require_slurm_running()


@pytest.fixture
def reservation():
    """A reservation starting in an hour, created with KV, deleted after."""

    admin = atf.properties["slurm-user"]
    # The quotes and backslash check the document is escaped on its way
    # into the database
    kv = shlex.quote('{"a":"x\\"y\'z\\\\w"}')
    atf.run_command(
        f"scontrol create reservation reservationname={RESV_NAME} "
        f"starttime=now+1hour duration=10 nodecnt=1 users={admin} KV={kv}",
        user=admin,
        fatal=True,
    )
    yield RESV_NAME
    atf.run_command(f"scontrol delete reservation={RESV_NAME}", user=admin, fatal=True)


@pytest.fixture
def make_reservation():
    """Create reservations with scontrol, and delete any left at the end."""

    admin = atf.properties["slurm-user"]
    names = []

    def create(name, options):
        atf.run_command(
            f"scontrol create reservation reservationname={name} {options}",
            user=admin,
            fatal=True,
        )
        names.append(name)

    yield create
    # A test may already have deleted its reservation
    for name in names:
        atf.run_command(f"scontrol delete reservation={name}", user=admin)


# Returned by sacctmgr_kv() when sacctmgr has no row for the reservation yet
NO_ROW = "no sacctmgr row"


def sacctmgr_kv_periods(name, window):
    """Return (start, KV) for each period WithKV reports, None for NONE."""
    output = atf.run_command_output(
        f"sacctmgr show reservation names={name} {window} WithKV", fatal=True
    )
    lines = output.splitlines()
    # Each period is a header naming it, a rule, then the document
    assert len(lines) % 3 == 0, f"sacctmgr should print whole periods: {output!r}"
    periods = []
    for header, _, kv in zip(lines[::3], lines[1::3], lines[2::3]):
        match = re.fullmatch(rf"KV for {re.escape(name)} \((.+)\)", header)
        assert match, f"sacctmgr should report reservation {name}: {output!r}"
        periods.append((match.group(1), None if kv == "NONE" else json.loads(kv)))
    return periods


def sacctmgr_kv(name, window="start=now end=now+2hours"):
    """Return the reservation's KV as sacctmgr reports it, None when blank."""
    periods = sacctmgr_kv_periods(name, window)
    if not periods:
        return NO_ROW
    assert len(periods) == 1, f"sacctmgr should report one period of {name}"
    return periods[0][1]


def wait_for_kv(name, expected, window="start=now end=now+2hours"):
    """Wait for sacctmgr to report the expected KV, and return the last one seen."""
    for _ in atf.timer():
        kv = sacctmgr_kv(name, window)
        if kv == expected:
            break
    return kv


def test_sacctmgr_reservation_kv(reservation):
    """Verify sacctmgr reports KV as it is set, changed and cleared."""

    expected = {"a": "x\"y'z\\w"}
    assert (
        wait_for_kv(reservation, expected) == expected
    ), "sacctmgr should report the KV the reservation was created with"

    admin = atf.properties["slurm-user"]
    target = f"reservationname={reservation}"

    patch = shlex.quote('[{"op":"add","path":"/b","value":2}]')

    atf.run_command(f"scontrol update {target} KVPatch={patch}", user=admin, fatal=True)
    expected = {"a": "x\"y'z\\w", "b": 2}
    assert (
        wait_for_kv(reservation, expected) == expected
    ), "sacctmgr should report the KV again after it changes"

    atf.run_command(f"scontrol update {target} KV=", user=admin, fatal=True)
    assert (
        wait_for_kv(reservation, None) is None
    ), "sacctmgr should report a cleared KV as blank"


@pytest.fixture(scope="function")
def archive_dir():
    """A slurmdbd-writable archive directory."""
    path = pathlib.Path("archive").resolve()
    path.mkdir(exist_ok=True)
    # slurmdbd writes the archive file itself, as SlurmUser.
    path.chmod(0o777)
    return path


def run_sql(statement):
    """Run one statement against the slurmdbd database.

    ATF has no SQL helper, so the connection parameters are read out of
    slurmdbd.conf the same way atf.dump_accounting_database() does.
    """
    conf = atf.get_config(live=False, source="slurmdbd", quiet=True)

    options = []
    if conf.get("StorageHost"):
        options.append(f"-h {conf['StorageHost']}")
    if conf.get("StoragePort"):
        options.append(f"-P {conf['StoragePort']}")
    options.append(f"-u {conf.get('StorageUser') or atf.properties['slurm-user']}")
    if conf.get("StoragePass"):
        options.append(f"-p{conf['StoragePass']}")
    database = conf.get("StorageLoc") or "slurm_acct_db"

    return atf.run_command_output(
        f"mysql {' '.join(options)} {database} -N -B -e {shlex.quote(statement)}",
        fatal=True,
        quiet=True,
    )


def test_kv_survives_archive(archive_dir, make_reservation):
    """Verify reservation KV survives an archive dump and load.

    The purge also removes the document once nothing refers to it, and
    archives it to its own file.
    """

    admin = atf.properties["slurm-user"]
    # The quotes and backslash check the document is escaped on its way
    # back into the database. The test key keeps it apart from the other
    # reservations' documents, which would keep it in use.
    expected = {"a": "x\"y'z\\w", "test": "archive"}
    make_reservation(
        ARCHIVE_RESV_NAME,
        f"starttime=now duration=10 nodecnt=1 users={admin} "
        f"KV={shlex.quote(json.dumps(expected))}",
    )
    window = "start=now-1hours end=now+1hours"
    assert (
        wait_for_kv(ARCHIVE_RESV_NAME, expected, window) == expected
    ), "sacctmgr should report the KV of the new reservation"

    # Only a reservation that has ended can be archived. Deleting one that
    # has started ends it now and marks its record deleted.
    atf.run_command(
        f"scontrol delete reservation={ARCHIVE_RESV_NAME}", user=admin, fatal=True
    )
    cluster = atf.get_config_parameter("ClusterName")
    table = f"`{cluster}_resv_table`"
    where = f"WHERE resv_name = '{ARCHIVE_RESV_NAME}'"
    for _ in atf.timer(fatal=True):
        if run_sql(f"SELECT deleted FROM {table} {where}").strip() == "1":
            break

    # Move the reservation out of the current hour. The purge cutoff is the
    # top of the current hour minus the requested hours, so a reservation
    # that just ended is never in range.
    shift = ARCHIVE_BACKDATE_HOURS * 3600
    run_sql(
        f"UPDATE {table} SET time_start = time_start - {shift}, "
        f"time_end = time_end - {shift} {where}"
    )
    window = f"start=now-{ARCHIVE_BACKDATE_HOURS + 2}hours end=now"
    assert (
        wait_for_kv(ARCHIVE_RESV_NAME, expected, window) == expected
    ), "sacctmgr should report the KV of the ended reservation before archiving"
    columns = f"SELECT deleted, assoclist FROM {table} {where} ORDER BY time_start"
    archived_columns = run_sql(columns)
    kv_inx = run_sql(f"SELECT DISTINCT kv_hash_inx FROM {table} {where}").split()
    assert len(kv_inx) == 1, f"The reservation should refer to one document: {kv_inx}"
    kv_where = f"WHERE hash_inx = {kv_inx[0]}"

    atf.run_command(
        f"sacctmgr -i archive dump Directory={archive_dir} Reservations "
        "PurgeResvAfter=1hours",
        user=admin,
        fatal=True,
    )
    assert (
        sacctmgr_kv(ARCHIVE_RESV_NAME, window) == NO_ROW
    ), "The archive dump should have purged the reservation"
    assert (
        run_sql(f"SELECT COUNT(*) FROM `{cluster}_kv_table` {kv_where}").strip() == "0"
    ), "The archive dump should have purged the document nothing refers to"

    archive_files = []
    for table_name in ("resv_table", "kv_table"):
        files = glob.glob(f"{archive_dir}/*_{table_name}_archive_*")
        assert (
            len(files) == 1
        ), f"Expected exactly one {table_name} archive in {archive_dir}; got {files}"
        archive_files += files

    for archive_file in archive_files:
        atf.run_command(
            f"sacctmgr -i archive load File={archive_file}", user=admin, fatal=True
        )
    assert (
        wait_for_kv(ARCHIVE_RESV_NAME, expected, window) == expected
    ), "The reloaded reservation should report the KV it was archived with"
    assert (
        run_sql(columns) == archived_columns
    ), "The reloaded reservation should keep its deleted flag and associations"


def sacctmgr_periods(name, window="start=now-1hours end=now+1hours"):
    """Return the KV of each accounting period of a reservation, oldest first."""
    return [kv for _, kv in sorted(sacctmgr_kv_periods(name, window))]


def test_kv_change_on_started_reservation(make_reservation):
    """Verify a KV change on a started reservation keeps one document per period."""

    admin = atf.properties["slurm-user"]
    name = "resv_kv_started"
    make_reservation(
        name, f"starttime=now duration=10 nodecnt=1 users={admin} KV='{{\"a\":1}}'"
    )
    for _ in atf.timer(fatal=True):
        if sacctmgr_periods(name) == [{"a": 1}]:
            break

    # Accounting periods are keyed on whole seconds, so let the start second
    # pass before the change opens the next period
    time.sleep(2)
    atf.run_command(
        f"scontrol update reservationname={name} KV='{{\"a\":2}}'",
        user=admin,
        fatal=True,
    )

    expected = [{"a": 1}, {"a": 2}]
    for _ in atf.timer():
        periods = sacctmgr_periods(name)
        if periods == expected:
            break
    assert (
        periods == expected
    ), "sacctmgr should list the reservation once per period, each with its KV"


def test_kv_kept_across_restart(make_reservation):
    """Verify a restart leaves a started Nodes=ALL reservation's record alone."""

    admin = atf.properties["slurm-user"]
    name = "resv_kv_all_nodes"
    expected = [{"a": 1}]
    make_reservation(
        name, f"starttime=now duration=10 nodes=ALL users={admin} KV='{{\"a\":1}}'"
    )
    for _ in atf.timer(fatal=True):
        if sacctmgr_periods(name) == expected:
            break

    # Accounting periods are keyed on whole seconds, so let the start second
    # pass, or a wrongly opened period would replace the first one unseen
    time.sleep(2)

    # Recovering state checks every reservation's nodes, which must not count
    # as a change to an unchanged reservation
    atf.restart_slurmctld()

    # slurmctld sends records in order, so once a later job is recorded any
    # update to the reservation from the restart has been handled
    job_id = atf.submit_job_sbatch("--hold --wrap true", fatal=True)
    atf.wait_for_job_accounted(job_id, field="Submit", value=r"\d", fatal=True)
    assert (
        sacctmgr_periods(name) == expected
    ), "A restart should not open a new accounting period for the reservation"
