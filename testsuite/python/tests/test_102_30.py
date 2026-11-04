############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sacctmgr archive dump and load of each record type."""

import glob
import json
import pathlib
import re
import shlex
import time

import pytest

import atf

# How far back records are moved. With Purge*After=1hours, a dump only takes
# records older than one hour before the start of the current hour.
BACKDATE = 3 * 3600

# How sacct and sacctmgr print a time
TIME_RE = r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d"

# Reason comes first, to find our event
EVENT_FORMAT = (
    "Reason,Start,End,Duration,Cluster,ClusterNodes,NodeName,Event,EventRaw,"
    "State,StateRaw,User,TRES"
)
RESV_FORMAT = "Name,Cluster,ID,Start,End,Flags,NodeNames,TRES,Associations,UnusedWall"
TXN_FORMAT = "TimeStamp,Action,Actor,Where,Info"

# archive dump takes these from slurmdbd.conf when they are not given
DBD_ARCHIVE_PARAMS = [
    "ArchiveEvents",
    "ArchiveJobs",
    "ArchiveJobEnv",
    "ArchiveJobScript",
    "ArchiveResvs",
    "ArchiveScript",
    "ArchiveSteps",
    "ArchiveSuspend",
    "ArchiveTXN",
    "ArchiveUsage",
    "PurgeEventAfter",
    "PurgeJobAfter",
    "PurgeJobEnvAfter",
    "PurgeJobScriptAfter",
    "PurgeResvAfter",
    "PurgeStepAfter",
    "PurgeSuspendAfter",
    "PurgeTXNAfter",
    "PurgeUsageAfter",
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Only auto-config restores the purged records
    atf.require_auto_config("purges accounting records permanently")
    atf.require_accounting(modify=True)
    atf.require_tool("mysql")
    for param in DBD_ARCHIVE_PARAMS:
        atf.require_config_parameter(param, None, source="slurmdbd")
    # Jobs are archived with their scripts and environments
    atf.require_config_parameter_includes("AccountingStoreFlags", "job_script")
    atf.require_config_parameter_includes("AccountingStoreFlags", "job_env")
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "nojobs")
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "nosteps")
    atf.require_slurm_running()


def iso(epoch):
    """Return a time in the format the commands take."""
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(epoch))


def run_sql(statement):
    """Run SQL against the slurmdbd database.

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


@pytest.fixture
def archive_dir():
    """A directory slurmdbd can write archive files to."""
    path = pathlib.Path("archive").resolve()
    path.mkdir()
    # slurmdbd writes the files as SlurmUser
    path.chmod(0o777)
    return path


def archive_file(archive_dir, table):
    """Return the archive file of a table, and check its name."""
    cluster = atf.get_config_parameter("ClusterName")
    prefix = f"{cluster}_{table}_archive_"
    files = glob.glob(f"{archive_dir}/{prefix}*")
    assert len(files) == 1, f"Expected one {table} archive file, got {files}"
    path = pathlib.Path(files[0])
    # <ClusterName>_<ArchiveObject>_archive_<BeginTimeStamp>_<EndTimeStamp>
    assert re.fullmatch(
        rf"{re.escape(prefix)}{TIME_RE}_{TIME_RE}", path.name
    ), f"Archive file name should be {prefix}<begin>_<end>, got {path.name}"
    return path


def check_round_trip(archive_dir, options, tables, view):
    """Dump old records, check they are gone, then load them back.

    view() returns the records as a client shows them, and an empty list
    once they are purged. The first table holds the records themselves.
    """
    admin = atf.properties["slurm-user"]
    before = view()
    assert before, "The old records should be in the database before the dump"

    result = atf.run_command(
        f"sacctmgr -i archive dump Directory={archive_dir} {options}", user=admin
    )
    assert result["exit_code"] == 0, f"archive dump failed: {result['stderr']}"
    assert view() == [], f"archive dump {options} should purge the old records"

    files = [archive_file(archive_dir, table) for table in tables]
    for path in files:
        result = atf.run_command(f"sacctmgr -i archive load File={path}", user=admin)
        assert (
            result["exit_code"] == 0
        ), f"archive load of {path.name} failed: {result['stderr']}"
    assert view() == before, "The loaded records should match the ones dumped"

    result = atf.run_command(f"sacctmgr -i archive load File={files[0]}", user=admin)
    assert (
        result["exit_code"] != 0
    ), f"A second archive load of {files[0].name} should fail"
    assert view() == before, "A failed second archive load should change nothing"


def sacct_jobs(job_id):
    output = atf.run_command_output(f"sacct -j {job_id} --json", fatal=True)
    return json.loads(output)["jobs"]


def job_view(job_id):
    """Return the job as sacct shows it, with its script and environment."""
    jobs = sacct_jobs(job_id)
    if not jobs:
        return []
    return jobs + [
        atf.run_command_output(
            f"sacct -j {job_id} {option}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        for option in ("--batch-script", "--env-vars")
    ]


def step_view(job_id):
    jobs = sacct_jobs(job_id)
    assert len(jobs) == 1, f"sacct should show job {job_id}, got {jobs}"
    return jobs[0]["steps"]


def suspend_view(job_id, start):
    """Return the job's line when sacct lists jobs suspended since start."""
    output = atf.run_command_output(
        f"sacct -X -n -P -s SUSPENDED -S {iso(start)} -E now -o JobID,Suspended",
        fatal=True,
    )
    return [line for line in output.splitlines() if line.split("|")[0] == str(job_id)]


def event_view(node, reason, start):
    output = atf.run_command_output(
        f"sacctmgr -n -P show event nodes={node} start={iso(start)} "
        f"format={EVENT_FORMAT}",
        fatal=True,
    )
    return [line for line in output.splitlines() if line.split("|")[0] == reason]


def resv_view(name, start):
    return atf.run_command_output(
        f"sacctmgr -n -P show reservation names={name} start={iso(start)} "
        f"format={RESV_FORMAT}",
        fatal=True,
    ).splitlines()


@pytest.fixture
def old_event():
    """An ended node event, moved into the past, as (node, reason, start)."""
    admin = atf.properties["slurm-user"]
    node = next(iter(atf.get_nodes(fatal=True)))
    reason = f"test_102_30_{time.time_ns()}"
    start = int(time.time()) - BACKDATE - 3600

    # Before 26.11, an event in the same second as the node's previous
    # one is lost, so drain after the second its registration ended in
    atf.wait_for_node_state(node, "IDLE", fatal=True)
    registered = int(time.time())
    for _ in atf.timer(fatal=True):
        if int(time.time()) > registered:
            break

    atf.run_command(
        f"scontrol update nodename={node} state=drain reason={reason}",
        user=admin,
        fatal=True,
    )
    atf.run_command(
        f"scontrol update nodename={node} state=resume", user=admin, fatal=True
    )
    # slurmdbd records the event and its end time asynchronously
    for _ in atf.timer(fatal=True):
        events = event_view(node, reason, start)
        if events and re.fullmatch(TIME_RE, events[0].split("|")[2]):
            break

    cluster = atf.get_config_parameter("ClusterName")
    run_sql(
        f"UPDATE `{cluster}_event_table` SET time_start = time_start - {BACKDATE}, "
        f"time_end = time_end - {BACKDATE} "
        f"WHERE node_name = '{node}' AND reason = '{reason}'"
    )
    return node, reason, start


@pytest.fixture
def old_job(request):
    """An ended job with steps and a suspension, moved into the past.

    Returns the job id and a start time for queries that cover it.
    """
    admin = atf.properties["slurm-user"]
    start = int(time.time()) - BACKDATE - 3600

    # Its own script, so no other job shares the stored copy
    job_id = atf.submit_job_sbatch(
        f"--wrap 'echo {request.node.name}; srun true; sleep infinity'", fatal=True
    )
    atf.wait_for_step_accounted(job_id, 0, fatal=True)
    atf.run_command(f"scontrol suspend {job_id}", user=admin, fatal=True)
    atf.wait_for_job_state(job_id, "SUSPENDED", fatal=True)
    atf.run_command(f"scontrol resume {job_id}", user=admin, fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.cancel_jobs([job_id], fatal=True)

    # Steps get their end time after the job ends
    for _ in atf.timer(fatal=True):
        ends = atf.run_command_output(
            f"sacct -j {job_id} -n -P -o End", fatal=True
        ).split()
        if ends and all(re.fullmatch(TIME_RE, end) for end in ends):
            break

    cluster = atf.get_config_parameter("ClusterName")
    job_inx = f"(SELECT job_db_inx FROM `{cluster}_job_table` WHERE id_job = {job_id})"
    statements = [
        f"UPDATE `{cluster}_job_table` SET "
        f"time_submit = time_submit - {BACKDATE}, "
        f"time_eligible = time_eligible - {BACKDATE}, "
        f"time_start = time_start - {BACKDATE}, "
        f"time_end = time_end - {BACKDATE} WHERE id_job = {job_id}"
    ]
    for table in ("step_table", "suspend_table"):
        statements.append(
            f"UPDATE `{cluster}_{table}` SET time_start = time_start - {BACKDATE}, "
            f"time_end = time_end - {BACKDATE} WHERE job_db_inx = {job_inx}"
        )
    run_sql("; ".join(statements))
    return job_id, start


@pytest.fixture
def old_reservation():
    """An ended reservation, moved into the past, as (name, start)."""
    admin = atf.properties["slurm-user"]
    name = f"resv_{time.time_ns()}"
    start = int(time.time()) - BACKDATE - 3600

    atf.run_command(
        f"scontrol create reservation reservationname={name} starttime=now "
        f"duration=10 nodecnt=1 users={admin}",
        user=admin,
        fatal=True,
    )
    for _ in atf.timer(fatal=True):
        planned = resv_view(name, start)
        if planned:
            break

    # Deleting a started reservation ends it now
    atf.run_command(f"scontrol delete reservation={name}", user=admin, fatal=True)
    for _ in atf.timer(fatal=True):
        rows = resv_view(name, start)
        if rows and rows[0].split("|")[4] != planned[0].split("|")[4]:
            break

    cluster = atf.get_config_parameter("ClusterName")
    run_sql(
        f"UPDATE `{cluster}_resv_table` SET time_start = time_start - {BACKDATE}, "
        f"time_end = time_end - {BACKDATE} WHERE resv_name = '{name}'"
    )
    return name, start


@pytest.fixture
def old_transactions():
    """Transactions of a new account, moved into the past.

    Returns the account, and a start and end time that only cover the
    moved transactions.
    """
    admin = atf.properties["slurm-user"]
    cluster = atf.get_config_parameter("ClusterName")
    account = f"acct_{time.time_ns()}"
    created = int(time.time())

    atf.run_command(
        f"sacctmgr -i add account {account} cluster={cluster}", user=admin, fatal=True
    )
    # Only move the transactions with a cluster, as every supported
    # version purges those
    run_sql(
        f"UPDATE txn_table SET timestamp = timestamp - {BACKDATE} "
        f"WHERE cluster = '{cluster}' AND timestamp >= {created}"
    )
    return account, created - BACKDATE - 60, created - BACKDATE + 600


def test_events(archive_dir, old_event):
    """Verify old events are purged to an archive file and load back."""
    check_round_trip(
        archive_dir,
        "Events PurgeEventAfter=1hours",
        ["event_table"],
        lambda: event_view(*old_event),
    )


def test_events_purge_only(archive_dir, old_event):
    """Verify a dump without Events purges old events and archives nothing."""
    assert event_view(*old_event), "The old event should be there before the dump"

    result = atf.run_command(
        f"sacctmgr -i archive dump Directory={archive_dir} PurgeEventAfter=1hours",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] == 0, f"archive dump failed: {result['stderr']}"
    assert event_view(*old_event) == [], "archive dump should purge the old event"
    files = glob.glob(f"{archive_dir}/*")
    assert files == [], f"A dump without Events should write no file, got {files}"


def test_jobs(archive_dir, old_job):
    """Verify old jobs, with scripts and environments, survive dump and load."""
    job_id, _ = old_job
    script, env = job_view(job_id)[-2:]
    assert "srun true" in script, f"sacct should show job {job_id}'s script"
    assert "PATH=" in env, f"sacct should show job {job_id}'s environment"
    check_round_trip(
        archive_dir,
        "Jobs PurgeJobAfter=1hours",
        ["job_table", "job_script_table", "job_env_table"],
        lambda: job_view(job_id),
    )


def test_steps(archive_dir, old_job):
    """Verify old steps are purged to an archive file and load back."""
    job_id, _ = old_job
    check_round_trip(
        archive_dir,
        "Steps PurgeStepAfter=1hours",
        ["step_table"],
        lambda: step_view(job_id),
    )


def test_suspend(archive_dir, old_job):
    """Verify old suspend records are purged to an archive file and load back."""
    check_round_trip(
        archive_dir,
        "Suspend PurgeSuspendAfter=1hours",
        ["suspend_table"],
        lambda: suspend_view(*old_job),
    )


@pytest.mark.xfail(
    atf.get_version("sbin/slurmdbd") < (26, 11),
    reason="Issue 51034: before c72afa288cb (26.11) archive load swapped a "
    "reservation's deleted flag and associations",
)
def test_reservations(archive_dir, old_reservation):
    """Verify old reservations are purged to an archive file and load back."""
    check_round_trip(
        archive_dir,
        "Reservations PurgeResvAfter=1hours",
        ["resv_table"],
        lambda: resv_view(*old_reservation),
    )


def test_transactions(archive_dir, old_transactions):
    """Verify old transactions are purged to an archive file and load back."""
    account, start, end = old_transactions
    check_round_trip(
        archive_dir,
        "TXN PurgeTXNAfter=1hours",
        ["txn_table"],
        lambda: atf.run_command_output(
            f"sacctmgr -n -P show transaction accounts={account} "
            f"start={iso(start)} end={iso(end)} format={TXN_FORMAT}",
            fatal=True,
        ).splitlines(),
    )
