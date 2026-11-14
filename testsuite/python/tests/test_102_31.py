############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sacctmgr archive dump and load of usage records."""

import datetime
import glob
import pathlib
import re
import shlex

import pytest

import atf

# How far back usage records are moved. With PurgeUsageAfter=1hours, a dump
# only takes records changed more than one hour before the current hour.
BACKDATE = 3 * 3600

# How archive file names print their begin and end times
TIME_RE = r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d"

ACCOUNT = "test_102_31_acct"
WCKEY = "test_102_31_wckey"

# The sreport cluster report that reads each kind of usage table
REPORTS = {
    "usage": "Utilization format=Cluster,Allocated,Down,PlannedDown,Idle,Reported",
    "assoc_usage": "AccountUtilizationByUser format=Accounts,Login,Used",
    "wckey_usage": "WCKeyUtilizationByUser format=Wckey,Login,Used",
    "qos_usage": "AccountUtilizationByQOS format=Accounts,QOS,Used",
}
PERIODS = ["hour", "day", "month"]

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
    atf.require_accounting(modify=True)
    atf.require_tool("mysql")
    for param in DBD_ARCHIVE_PARAMS:
        atf.require_config_parameter(param, None, source="slurmdbd")
    # WCKey usage is only rolled up with TrackWCKey on both sides
    atf.require_config_parameter("TrackWCKey", "yes")
    atf.require_config_parameter("TrackWCKey", "yes", source="slurmdbd")
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "nojobs")
    atf.require_slurm_running()


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


def sreport(kind, window):
    """Return the lines of the report that reads this kind of usage."""
    start, end = window
    return atf.run_command_output(
        f"sreport -n -P -t seconds cluster {REPORTS[kind]} "
        f"start={start:%Y-%m-%dT%H:%M:%S} end={end:%Y-%m-%dT%H:%M:%S}",
        user=atf.properties["slurm-user"],
        fatal=True,
    ).splitlines()


def used(lines, key):
    """Return the usage a report gives to key, or 0 if it has none."""
    for line in lines:
        fields = line.split("|")
        if tuple(fields[: len(key)]) == key:
            return int(fields[len(key)])
    return 0


@pytest.fixture(scope="module")
def usage():
    """Roll up one job in the previous month, then dump all usage.

    Returns the report windows, the keys of the job's usage in each report,
    the reports before the dump, the dump result, the archive directory and
    the cluster usage loads done so far.
    """
    admin = atf.properties["slurm-user"]
    user = atf.properties["test-user"]
    cluster = atf.get_config_parameter("ClusterName")
    atf.run_command(
        f"sacctmgr -i add account {ACCOUNT} cluster={cluster}", user=admin, fatal=True
    )
    atf.run_command(
        f"sacctmgr -i add user {user} account={ACCOUNT} wckeys={WCKEY} "
        f"cluster={cluster}",
        user=admin,
        fatal=True,
    )
    # slurmctld gets new associations asynchronously
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(
            f"scontrol show assoc_mgr users={user} accounts={ACCOUNT} flags=assoc",
            user=admin,
            fatal=True,
        )
        if f"UserName={user}(" in output:
            break
    job_id = atf.submit_job_sbatch(
        f"-A {ACCOUNT} --wckey={WCKEY} --wrap 'sleep 2'", fatal=True
    )
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)
    started, qos = (
        atf.run_command_output(f"sacct -j {job_id} -X -n -P -o Start,QOS", fatal=True)
        .strip()
        .split("|")
    )

    # The job moves to 22:01 on the last day of the previous month, so the
    # hour, day and month tables all get its usage
    month = datetime.datetime.now().replace(
        day=1, hour=0, minute=0, second=0, microsecond=0
    )
    day = month - datetime.timedelta(days=1)
    hour = month - datetime.timedelta(hours=2)
    windows = {
        "hour": (hour, hour + datetime.timedelta(hours=1)),
        "day": (day, month),
        "month": (day.replace(day=1), month),
    }
    shift = int(datetime.datetime.fromisoformat(started).timestamp()) - int(
        hour.timestamp() + 60
    )
    rewind = int(hour.timestamp()) - 3600

    # Move the job and the cluster's registration back, and rewind the
    # rollup to before them. slurmdbd rolls up usage when it starts.
    atf.stop_slurmdbd(quiet=True)
    run_sql(
        "; ".join(
            [
                f"UPDATE `{cluster}_job_table` SET "
                f"time_submit = time_submit - {shift}, "
                f"time_eligible = time_eligible - {shift}, "
                f"time_start = time_start - {shift}, "
                f"time_end = time_end - {shift} WHERE id_job = {job_id}",
                f"UPDATE `{cluster}_event_table` SET "
                f"time_start = time_start - {shift}, "
                f"time_end = IF(time_end = 0, 0, time_end - {shift}) "
                "WHERE node_name = ''",
                f"UPDATE `{cluster}_last_ran_table` SET hourly_rollup = {rewind}, "
                f"daily_rollup = {rewind}, monthly_rollup = {rewind}",
            ]
        )
    )
    atf.start_slurmdbd(quiet=True)

    keys = {
        "usage": (cluster,),
        "assoc_usage": (ACCOUNT, user),
        "wckey_usage": (WCKEY, user),
        "qos_usage": (ACCOUNT, qos),
    }
    # The rollup covers every hour since the rewind, up to a month of them.
    # The month tables are committed last, so once they show the job the
    # hour and day tables do too.
    for _ in atf.timer(timeout=300, fatal=True):
        if used(sreport("usage", windows["month"]), keys["usage"]):
            break
    before = {
        (kind, period): sreport(kind, windows[period])
        for kind in REPORTS
        for period in PERIODS
    }

    run_sql(
        "; ".join(
            f"UPDATE `{cluster}_{kind}_{period}_table` SET "
            f"creation_time = creation_time - {BACKDATE}, "
            f"mod_time = mod_time - {BACKDATE}"
            for kind in REPORTS
            for period in PERIODS
        )
    )
    archive_dir = pathlib.Path("archive").resolve()
    archive_dir.mkdir()
    # slurmdbd writes the files as SlurmUser
    archive_dir.chmod(0o777)
    dump = atf.run_command(
        f"sacctmgr -i archive dump Directory={archive_dir} Usage "
        "PurgeUsageAfter=1hours",
        user=admin,
    )
    return {
        "windows": windows,
        "keys": keys,
        "before": before,
        "dump": dump,
        "dir": archive_dir,
        "cluster_loads": {},
    }


def archive_file(usage, kind, period):
    """Return the archive file the dump wrote for a usage table."""
    cluster = atf.get_config_parameter("ClusterName")
    prefix = f"{cluster}_{kind}_{period}_table_archive_"
    files = glob.glob(f"{usage['dir']}/{prefix}*")
    assert len(files) == 1, f"Expected one {kind}_{period} archive file, got {files}"
    path = pathlib.Path(files[0])
    # <ClusterName>_<ArchiveObject>_archive_<BeginTimeStamp>_<EndTimeStamp>
    assert re.fullmatch(
        rf"{re.escape(prefix)}{TIME_RE}_{TIME_RE}", path.name
    ), f"Archive file name should be {prefix}<begin>_<end>, got {path.name}"
    return path


def archive_load(path):
    """Load an archive file as SlurmUser."""
    return atf.run_command(
        f"sacctmgr -i archive load File={path}", user=atf.properties["slurm-user"]
    )


def load_cluster_usage(usage, period):
    """Load the archive file of a period's cluster usage, once per module.

    sreport leaves a cluster out of its association, WCKey and QOS reports
    while the cluster has no usage in the window. Returns whether the dump
    purged the cluster usage, the file, and the result of its load.
    """
    loads = usage["cluster_loads"]
    if period not in loads:
        window = usage["windows"][period]
        purged = used(sreport("usage", window), usage["keys"]["usage"]) == 0
        path = archive_file(usage, "usage", period)
        loads[period] = {"purged": purged, "path": path, "result": archive_load(path)}
    return loads[period]


@pytest.mark.parametrize("period", PERIODS)
@pytest.mark.parametrize(
    "kind",
    [
        "usage",
        "assoc_usage",
        "wckey_usage",
        pytest.param(
            "qos_usage",
            marks=pytest.mark.xfail(
                atf.get_version("sbin/slurmdbd") < (25, 11, 5),
                reason="Ticket 24943: QOS usage was not archived before 25.11 "
                "(Ticket 23383), and did not load back before 25.11.5",
            ),
        ),
    ],
)
def test_usage(usage, kind, period):
    """Verify a usage table is purged to its archive file and loads back."""
    window = usage["windows"][period]
    key = usage["keys"][kind]
    before = usage["before"][kind, period]
    assert used(before, key) > 0, f"sreport should show the job's {kind} first"

    dump = usage["dump"]
    assert dump["exit_code"] == 0, f"archive dump failed: {dump['stderr']}"

    cluster = load_cluster_usage(usage, period)
    if kind == "usage":
        assert cluster["purged"], f"archive dump should purge the {period} {kind}"
        path, result = cluster["path"], cluster["result"]
    else:
        assert (
            cluster["result"]["exit_code"] == 0
        ), f"archive load of {cluster['path'].name} failed: {cluster['result']['stderr']}"
        assert (
            used(sreport(kind, window), key) == 0
        ), f"archive dump should purge the {period} {kind}"
        path = archive_file(usage, kind, period)
        result = archive_load(path)

    assert (
        result["exit_code"] == 0
    ), f"archive load of {path.name} failed: {result['stderr']}"
    assert (
        sreport(kind, window) == before
    ), f"The loaded {period} {kind} should match sreport before the dump"

    result = archive_load(path)
    assert result["exit_code"] != 0, f"A second archive load of {path.name} should fail"
    assert (
        sreport(kind, window) == before
    ), f"A failed second archive load of {path.name} should change nothing"
