############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test 115.2: Accounting storage rollup arithmetic correctness.

Seeds a synthetic cluster with deterministic rows directly in MySQL, triggers
sacctmgr rollup for an explicit time window, then verifies rollup output
through the documented sreport interface.

Data is pulled from the rollup table with the longest interval that can
satisfy the requested report period (sreport.1, DESCRIPTION), so the hour,
day and month rows are each checked by asking for the matching window.
Per-TRES values come from a single -T per report, which keeps one TRES per
row and needs no TresName column.

Skipped outside auto-config mode; the fixtures seed rows through the mysql
helper that conftest only provides there.

Run this only against a throwaway accounting DB.  sacctmgr rollup takes no
cluster argument, so every rollup here recomputes and overwrites usage rows
for every cluster in the DB over the window it covers -- and a window whose
source jobs were already purged recomputes to empty.
"""

import re
from datetime import datetime, timedelta

import pytest

import atf

pytestmark = pytest.mark.slow

_CLUSTER = "test1152rollup"
_NO_VAL = 0xFFFFFFFE
_ACCOUNT = "test1152acct"
_ACCOUNT2 = "test1152acct2"
_USER = "root"
_WCKEY = "test1152wck"

_QOS = "test1152qos"

_RESV_ID = 1

# Reservation flags (slurm.h)
_RESV_FLAG_MAINT = 1  # SLURM_BIT(0)
_RESV_FLAG_IGN_JOBS = 64  # SLURM_BIT(6)

_RESV_IDLE_REASON = (
    "Ticket 24919: reservation idle was charged the reservation's TRES count "
    "instead of the job's before 26.11"
)
_ENERGY_REASON = (
    "Ticket 24919: energy from a zero-elapsed row was charged to the previous "
    "row's assoc/QOS/wckey before 26.11"
)

# Slurm job states (enum job_states)
_JOB_RUNNING = 1
_JOB_COMPLETE = 3

# Slurm node states (slurm.h).  FUTURE is a NODE_STATE_BASE value, POWERED_DOWN
# a NODE_STATE_FLAGS bit; the rollup credits pdown_secs on either.
_NODE_STATE_FUTURE = 6
_NODE_STATE_POWERED_DOWN = 0x1000  # SLURM_BIT(12)

# Standard Slurm TRES IDs
_CPU = 1
_MEM = 2
_ENERGY = 3
_NODE = 4
_BILLING = 5

# Job allocation: 4 CPUs, 256 MB, 1 node, billing=4
_TRES_ALLOC = "1=4,2=256,4=1,5=4"
_CPU_COUNT = 4
_MEM_COUNT = 256
_NODE_COUNT = 1
_BILLING_COUNT = 4

# Cluster registration: 8 CPUs, 512 MB, 2 nodes, billing=8
_CLUSTER_TRES = "1=8,2=512,4=2,5=8"
_CLUSTER_CPU = 8
_CLUSTER_MEM = 512

# (tres_id, per-job count, label) for the standard job TRES.
_STD_TRES = [
    (_CPU, _CPU_COUNT, "cpu"),
    (_MEM, _MEM_COUNT, "mem"),
    (_NODE, _NODE_COUNT, "node"),
    (_BILLING, _BILLING_COUNT, "billing"),
]
_NONCPU_TRES = _STD_TRES[1:]

# Per-cluster tables the tests seed directly, plus every usage table the rollup
# writes.  All of these have to be cleared between tests -- see the db fixture.
_RESET_TABLES = ["event_table", "job_table", "resv_table", "suspend_table"] + [
    f"{obj}usage_{period}_table"
    for period in ("hour", "day", "month")
    for obj in ("", "assoc_", "qos_", "wckey_")
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_config_parameter("AllowNoDefAcct", None, source="slurmdbd")
    atf.require_config_parameter("TrackWCKey", "yes", source="slurmdbd")
    atf.require_config_parameter("PurgeJobAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeEventAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeUsageAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeResvAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeStepAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeSuspendAfter", None, source="slurmdbd")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def qos_id(setup):
    """Create the test QOS and return its id.

    QOS objects are not cluster-scoped, so this outlives the per-test cluster
    the db fixture adds and removes.
    """
    slurm_user = atf.properties["slurm-user"]
    atf.run_command(f"sacctmgr -i add qos {_QOS}", user=slurm_user, fatal=True)
    out = atf.run_command_output(
        f"sacctmgr -nP list qos names={_QOS} format=id",
        user=slurm_user,
        fatal=True,
    )
    if not out.strip():
        pytest.fail(f"no qos {_QOS} after adding it")
    yield int(out.strip().splitlines()[0].rstrip("|"))

    atf.run_command(f"sacctmgr -i remove qos {_QOS}", user=slurm_user, fatal=True)


@pytest.fixture(scope="function")
def db(sql_statement_repeat):
    """Create cluster with test account/user/wckey; yield context; teardown."""
    if not sql_statement_repeat:
        pytest.skip(
            "Ticket 24919: needs the conftest mysql helper, which requires "
            "auto-config"
        )

    slurm_user = atf.properties["slurm-user"]

    # Truncate before the remove.  sacctmgr remove cluster marks the cluster
    # deleted but leaves the per-cluster tables, and re-adding reuses the same
    # assoc and wckey ids, so stale usage rows would sit at exactly the
    # (id, id_tres, time_start) keys the next test reads.  Running rows
    # (time_end=0, state=JOB_RUNNING) left by an interrupted run also make the
    # remove refuse.  The tables do not exist on a fresh database, so this is
    # best effort.
    for _t in _RESET_TABLES:
        _sql(
            sql_statement_repeat,
            f"TRUNCATE TABLE {_CLUSTER}_{_t}",
            fatal=False,
            quiet=True,
        )

    atf.run_command(
        f"sacctmgr -i remove cluster {_CLUSTER}",
        user=slurm_user,
    )
    atf.run_command(
        f"sacctmgr -i add cluster {_CLUSTER}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {_ACCOUNT} cluster={_CLUSTER}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {_USER} account={_ACCOUNT} "
        f"cluster={_CLUSTER} wckey={_WCKEY}",
        user=slurm_user,
        fatal=True,
    )
    assoc_id = _assoc_id_of(slurm_user, _ACCOUNT)
    out = atf.run_command_output(
        f"sacctmgr -nP list wckeys users={_USER} wckeys={_WCKEY} "
        f"cluster={_CLUSTER} format=id",
        user=slurm_user,
        fatal=True,
    )
    if not out.strip():
        pytest.fail(f"no wckey {_WCKEY} for {_USER} on {_CLUSTER}; is TrackWCKey on?")
    wckey_id = int(out.strip().splitlines()[0].rstrip("|"))

    # Use 2 days ago at midnight so synthetic data doesn't collide with
    # any real rollup pass currently in progress.
    ws = None
    for _days in (2, 3, 4):
        ws = _local_midnight(datetime.now() - timedelta(days=_days))
        if ws:
            break
    if not ws:
        pytest.fail("no usable local midnight in the last 4 days")
    yield sql_statement_repeat, ws, assoc_id, wckey_id

    # Running rows (time_end=0, state=JOB_RUNNING) count as active jobs and
    # make sacctmgr refuse to remove the cluster, so drop them first.
    _sql(sql_statement_repeat, f"TRUNCATE TABLE {_CLUSTER}_job_table")

    # Removing a cluster cascades a delete across every per-cluster usage
    # table, which can exceed the default command timeout on a loaded DB.
    atf.run_command(
        f"sacctmgr -i remove cluster {_CLUSTER}",
        user=slurm_user,
        timeout=120,
        fatal=True,
    )

    # Accounts are not cluster-scoped, so removing the cluster leaves them
    # behind in acct_table.
    atf.run_command(
        f"sacctmgr -i remove account {_ACCOUNT}",
        user=slurm_user,
        fatal=True,
    )
    # Only the second_assoc fixture adds this one, so it is absent for most
    # tests and the remove is expected to fail there.
    atf.run_command(
        f"sacctmgr -i remove account {_ACCOUNT2}",
        user=slurm_user,
    )


@pytest.fixture(scope="function")
def second_assoc(db):
    """Add a second account/association on the test cluster; return its id."""
    slurm_user = atf.properties["slurm-user"]
    atf.run_command(
        f"sacctmgr -i add account {_ACCOUNT2} cluster={_CLUSTER}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {_USER} account={_ACCOUNT2} cluster={_CLUSTER}",
        user=slurm_user,
        fatal=True,
    )
    return _assoc_id_of(slurm_user, _ACCOUNT2)


def _assoc_id_of(slurm_user, account):
    """Return the association id for account/{_USER} on the test cluster."""
    out = atf.run_command_output(
        f"sacctmgr -nP list assoc cluster={_CLUSTER} account={account} "
        f"user={_USER} format=id",
        user=slurm_user,
        fatal=True,
    )
    return int(out.strip().splitlines()[0].rstrip("|"))


def _account_assoc_id_of(slurm_user, account):
    """Return the account-level (no user) association id on the test cluster."""
    out = atf.run_command_output(
        f"sacctmgr -nP list assoc cluster={_CLUSTER} account={account} "
        f"format=id,user",
        user=slurm_user,
        fatal=True,
    )
    for line in out.strip().splitlines():
        assoc_id, _, user = line.partition("|")
        if not user.strip("|"):
            return int(assoc_id)
    pytest.fail(f"no account-level assoc for {account} on {_CLUSTER}:\n{out}")


# ---------------------------------------------------------------------------
# SQL helpers
# ---------------------------------------------------------------------------


def _sql(mysql_cmd, stmt, fatal=True, quiet=False):
    atf.run_command(
        f'{mysql_cmd} -e "{stmt.replace("`", r"\`")}"',
        user=atf.properties["slurm-user"],
        fatal=fatal,
        quiet=quiet,
    )


def _local_midnight(dt):
    """Epoch of local midnight on dt's date, or None if it does not exist.

    A few zones shift 00:00 -> 01:00.  Python's fold rule and the plugin's
    slurm_mktime(tm_isdst=-1) can resolve that missing hour to different
    instants, so callers pick another date rather than key off it.
    """
    ws = int(dt.replace(hour=0, minute=0, second=0, microsecond=0).timestamp())
    return ws if datetime.fromtimestamp(ws).hour == 0 else None


def _day_start(ws, days=0):
    """Epoch of the local midnight `days` days after the local midnight at ws.

    The day and month rollups walk the calendar rather than adding 86400
    (as_mysql_rollup.c as_mysql_nonhour_rollup), so day rows are keyed on
    local midnights.  Adding 86400 drifts by an hour across a daylight
    saving transition, which both misses those keys and can leave a
    "one day" window inside a single 25 hour local day.
    """
    return int((datetime.fromtimestamp(ws) + timedelta(days=days)).timestamp())


def _rollup(ws, we):
    start = datetime.fromtimestamp(ws).strftime("%Y-%m-%dT%H:%M:%S")
    end = datetime.fromtimestamp(we).strftime("%Y-%m-%dT%H:%M:%S")
    # A rollup over a multi-day/month window can run long on a loaded DB, so
    # allow more than the default command timeout.
    atf.run_command(
        f"sacctmgr -i rollup {start} {end}",
        user=atf.properties["slurm-user"],
        fatal=True,
        timeout=120,
    )


def _add_cluster_event(mysql_cmd, ws, tres=_CLUSTER_TRES):
    """Insert a cluster registration (empty node_name) event."""
    _sql(
        mysql_cmd,
        f"INSERT INTO `{_CLUSTER}_event_table` "
        f"(node_name, state, tres, time_start, time_end, reason, reason_uid) "
        f"VALUES ('', 0, '{tres}', {ws}, 0, 'Cluster registration', 0)",
    )


def _add_node_event(mysql_cmd, node_name, state, ws, we, tres):
    """Insert a node-state event (down, powered-down, etc.)."""
    _sql(
        mysql_cmd,
        f"INSERT INTO `{_CLUSTER}_event_table` "
        f"(node_name, state, tres, time_start, time_end, reason, reason_uid) "
        f"VALUES ('{node_name}', {state}, '{tres}', {ws}, {we}, 'down', 0)",
    )


def _add_job(
    mysql_cmd,
    *,
    assoc_id,
    time_start,
    time_end,
    time_eligible,
    db_inx=1,
    qos_id=0,
    wckey_id=0,
    resv_id=0,
    tres_alloc=_TRES_ALLOC,
    cpus_req=_CPU_COUNT,
    state=_JOB_COMPLETE,
    time_suspended=0,
    array_task_pending=0,
):
    _sql(
        mysql_cmd,
        f"INSERT INTO `{_CLUSTER}_job_table` "
        f"(job_db_inx, id_assoc, id_qos, id_wckey, id_resv, id_job, "
        f"id_user, id_group, het_job_id, het_job_offset, state_reason_prev, "
        f"nodes_alloc, `partition`, priority, state, "
        f"time_start, time_end, time_eligible, time_submit, time_suspended, "
        f"cpus_req, array_task_pending, tres_alloc, tres_req, "
        f"job_name, nodelist, node_inx, account, env_hash_inx, script_hash_inx) "
        f"VALUES ({db_inx}, {assoc_id}, {qos_id}, {wckey_id}, {resv_id}, "
        f"{db_inx}, 0, 0, 0, {_NO_VAL}, 0, "
        f"1, 'part1', 0, {state}, "
        f"{time_start}, {time_end}, {time_eligible}, {time_eligible - 1}, "
        f"{time_suspended}, {cpus_req}, {array_task_pending}, "
        f"'{tres_alloc}', '{tres_alloc}', "
        f"'test1152', 'n1', 0, 'root', 0, 0)",
    )


# ---------------------------------------------------------------------------
# sreport helpers - each runs one documented report and returns the value it
# published for the requested TRES.
# ---------------------------------------------------------------------------


def _sreport(ws, report, fmt, we=None, tres=None):
    """Run one sreport cluster report over [ws, we), defaulting to one hour.

    A single -T keeps one TRES per report, so every row the caller matches
    belongs to that TRES and no TresName column is needed.  The window also
    picks the usage table slurmdbd reads (sreport.1, DESCRIPTION): hour rows
    off a day boundary, day rows for a day-aligned window, month rows for a
    window that runs 1st-of-month to 1st-of-month.
    """
    start = datetime.fromtimestamp(ws).strftime("%Y-%m-%dT%H:%M:%S")
    end = datetime.fromtimestamp(we if we else ws + 3600).strftime("%Y-%m-%dT%H:%M:%S")
    cmd = (
        f"sreport --local cluster {report} cluster={_CLUSTER} "
        f"start={start} end={end} -tSeconds -P -n format={fmt}"
    )
    if tres:
        cmd += f" -T {tres}"
    return atf.run_command_output(
        cmd,
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def _sreport_value(ws, report, fmt, row_regex, we=None, tres=None, default=None):
    """Run an sreport cluster report and return the int captured by row_regex.

    A missing row fails the test unless default is given.  sreport omits an
    object entirely when it has no usage, which is the expected result where a
    test asserts that nothing was credited.
    """
    out = _sreport(ws, report, fmt, we=we, tres=tres)
    m = re.search(row_regex, out)
    if not m:
        if default is not None:
            return default
        pytest.fail(f"no row matching {row_regex} in {report} output:\n{out}")
    return int(m.group(1))


def _sreport_account_used(
    ws, account=_ACCOUNT, user=_USER, tres=None, we=None, default=None
):
    """alloc_secs for account/user via AccountUtilizationByUser."""
    return _sreport_value(
        ws,
        "AccountUtilizationByUser",
        "cluster,account,login,used",
        rf"{_CLUSTER}\|{account}\|{user}\|(\d+)",
        we=we,
        tres=tres,
        default=default,
    )


def _sreport_qos_used(ws, qos, account=_ACCOUNT, tres=None, we=None, default=None):
    """alloc_secs for account/qos via AccountUtilizationByQOS."""
    return _sreport_value(
        ws,
        "AccountUtilizationByQOS",
        "cluster,account,qos,used",
        rf"{_CLUSTER}\|{account}\|{qos}\|(\d+)",
        we=we,
        tres=tres,
        default=default,
    )


def _sreport_wckey_used(ws, user=_USER, wckey=_WCKEY, tres=None, we=None, default=None):
    """alloc_secs for user/wckey via UserUtilizationByWckey."""
    return _sreport_value(
        ws,
        "UserUtilizationByWckey",
        "cluster,login,wckey,used",
        rf"{_CLUSTER}\|{user}\|{wckey}\|(\d+)",
        we=we,
        tres=tres,
        default=default,
    )


def _sreport_cluster_util(ws, field, tres=None, we=None):
    """Value of a cluster-utilization field (alloc, down, PlannedDown...).

    NOTE: 'reserved' is an undocumented alias for 'planned' in sreport, not a
    reservation figure; both print plan_secs.
    """
    return _sreport_value(
        ws,
        "utilization",
        f"cluster,{field}",
        rf"{_CLUSTER}\|(\d+)",
        we=we,
        tres=tres,
    )


def _sreport_resv_util(ws, tres="cpu", we=None, resv_name=None):
    """(Allocated, Idle) TRES-seconds from the reservation's own report.

    This is not a cluster report, so it takes the cluster as a keyword of its
    own; --local resolves to the local cluster and returns nothing here.
    """
    start = datetime.fromtimestamp(ws).strftime("%Y-%m-%dT%H:%M:%S")
    end = datetime.fromtimestamp(we if we else ws + 3600).strftime("%Y-%m-%dT%H:%M:%S")
    out = atf.run_command_output(
        f"sreport reservation utilization cluster={_CLUSTER} "
        f"start={start} end={end} -tSeconds -P -n "
        f"format=Name,Allocated,Idle -T {tres}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    name = resv_name if resv_name else f"resv{_RESV_ID}"
    m = re.search(rf"{name}\|(\d+)\|(\d+)", out)
    if not m:
        pytest.fail(f"no {name} row in reservation utilization output:\n{out}")
    return int(m.group(1)), int(m.group(2))


# CPU alloc_secs readers for the standard test objects, so a test that seeds
# one window can check every per-object usage table the rollup writes.
_OBJECT_READERS = {
    "assoc": lambda ws, we: _sreport_account_used(ws, we=we),
    "qos": lambda ws, we: _sreport_qos_used(ws, _QOS, we=we),
    "wckey": lambda ws, we: _sreport_wckey_used(ws, we=we),
}


def _assert_cluster_reconciles(ws, count, label="cpu", window=3600):
    """count * window must equal alloc + idle + planned + down + pdown.

    OverCommitted is not a share of the window.  sreport.1 defines it as the
    demand past the Reported time, which clamping has already taken back out
    of planned, so summing it here would count that excess twice.
    """
    parts = {
        f: _sreport_cluster_util(ws, f, tres=label)
        for f in ("alloc", "idle", "planned", "down", "PlannedDown")
    }
    total = sum(parts.values())
    assert total == count * window, (
        f"cluster {label} utilization does not reconcile: {parts} sums to "
        f"{total}, expected {count * window}"
    )


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


def test_assoc_alloc_secs(db):
    """alloc_secs must accumulate across multiple jobs for all standard TRES."""
    mysql_cmd, ws, assoc_id, _ = db
    elapsed1, elapsed2 = 1200, 1500
    _add_cluster_event(mysql_cmd, ws)
    _add_job(
        mysql_cmd,
        db_inx=1,
        assoc_id=assoc_id,
        time_start=ws + 100,
        time_end=ws + 100 + elapsed1,
        time_eligible=ws,
    )
    _add_job(
        mysql_cmd,
        db_inx=2,
        assoc_id=assoc_id,
        time_start=ws + 200,
        time_end=ws + 200 + elapsed2,
        time_eligible=ws,
    )
    _rollup(ws, ws + 3600)

    total = elapsed1 + elapsed2
    for _tres_id, count, label in _STD_TRES:
        used = _sreport_account_used(ws, tres=label)
        assert (
            used == count * total
        ), f"assoc {label} alloc={used}, expected {count * total}"


def test_qos_alloc_secs(db, qos_id):
    """QOS rollup must accumulate alloc_secs across multiple jobs."""
    mysql_cmd, ws, assoc_id, _ = db
    elapsed1, elapsed2 = 1200, 1500
    _add_cluster_event(mysql_cmd, ws)
    _add_job(
        mysql_cmd,
        db_inx=1,
        assoc_id=assoc_id,
        qos_id=qos_id,
        time_start=ws + 100,
        time_end=ws + 100 + elapsed1,
        time_eligible=ws,
    )
    _add_job(
        mysql_cmd,
        db_inx=2,
        assoc_id=assoc_id,
        qos_id=qos_id,
        time_start=ws + 200,
        time_end=ws + 200 + elapsed2,
        time_eligible=ws,
    )
    _rollup(ws, ws + 3600)

    total = elapsed1 + elapsed2
    for _tres_id, count, label in _STD_TRES:
        used = _sreport_qos_used(ws, _QOS, tres=label)
        assert (
            used == count * total
        ), f"qos {label} alloc={used}, expected {count * total}"


def test_wckey_alloc_secs(db):
    """WCKey rollup must accumulate alloc_secs across multiple jobs."""
    mysql_cmd, ws, assoc_id, wckey_id = db
    elapsed1, elapsed2 = 1200, 1500
    _add_cluster_event(mysql_cmd, ws)
    _add_job(
        mysql_cmd,
        db_inx=1,
        assoc_id=assoc_id,
        wckey_id=wckey_id,
        time_start=ws + 100,
        time_end=ws + 100 + elapsed1,
        time_eligible=ws,
    )
    _add_job(
        mysql_cmd,
        db_inx=2,
        assoc_id=assoc_id,
        wckey_id=wckey_id,
        time_start=ws + 200,
        time_end=ws + 200 + elapsed2,
        time_eligible=ws,
    )
    _rollup(ws, ws + 3600)

    total = elapsed1 + elapsed2
    for _tres_id, count, label in _STD_TRES:
        used = _sreport_wckey_used(ws, tres=label)
        assert (
            used == count * total
        ), f"wckey {label} alloc={used}, expected {count * total}"


def test_suspend_cpu_deduction(db):
    """CPU alloc_secs must deduct suspend time; memory must not.
    One suspended job and one unsuspended job share the same assoc to
    verify accumulation is correct for both."""
    mysql_cmd, ws, assoc_id, _ = db
    elapsed = 1800
    suspend_start = ws + 300
    suspend_secs = 300
    _add_cluster_event(mysql_cmd, ws)
    # Job 1: suspended.
    _add_job(
        mysql_cmd,
        db_inx=1,
        assoc_id=assoc_id,
        time_start=ws + 100,
        time_end=ws + 100 + elapsed,
        time_eligible=ws,
        time_suspended=1,
    )
    _sql(
        mysql_cmd,
        f"INSERT INTO `{_CLUSTER}_suspend_table` "
        f"(job_db_inx, id_assoc, time_start, time_end) "
        f"VALUES (1, {assoc_id}, {suspend_start}, {suspend_start + suspend_secs})",
    )
    # Job 2: not suspended.
    _add_job(
        mysql_cmd,
        db_inx=2,
        assoc_id=assoc_id,
        time_start=ws + 200,
        time_end=ws + 200 + elapsed,
        time_eligible=ws,
    )
    _rollup(ws, ws + 3600)

    cpu = _sreport_account_used(ws, tres="cpu")
    mem = _sreport_account_used(ws, tres="mem")
    expected_cpu = _CPU_COUNT * (elapsed - suspend_secs) + _CPU_COUNT * elapsed
    expected_mem = _MEM_COUNT * elapsed * 2
    assert cpu == expected_cpu, (
        f"CPU alloc_secs={cpu}, expected {expected_cpu} "
        f"(suspended job deducts {suspend_secs}s, unsuspended job does not)"
    )
    assert (
        mem == expected_mem
    ), f"Mem alloc_secs={mem}, expected {expected_mem} (suspend must not affect mem)"


def test_cluster_alloc_secs(db):
    """Cluster time_alloc must equal the sum of all job TRES-seconds."""
    mysql_cmd, ws, assoc_id, _ = db
    elapsed1, elapsed2 = 1200, 1500
    _add_cluster_event(mysql_cmd, ws)
    _add_job(
        mysql_cmd,
        db_inx=1,
        assoc_id=assoc_id,
        time_start=ws + 100,
        time_end=ws + 100 + elapsed1,
        time_eligible=ws,
    )
    _add_job(
        mysql_cmd,
        db_inx=2,
        assoc_id=assoc_id,
        time_start=ws + 200,
        time_end=ws + 200 + elapsed2,
        time_eligible=ws,
    )
    _rollup(ws, ws + 3600)

    total = elapsed1 + elapsed2
    # TRES node usage is not reported in Cluster Utilization (sreport.1), so
    # mem is the only non-CPU TRES tracked here.
    for count, label in ((_CPU_COUNT, "cpu"), (_MEM_COUNT, "mem")):
        used = _sreport_cluster_util(ws, "alloc", tres=label)
        assert (
            used == count * total
        ), f"cluster {label} alloc={used}, expected {count * total}"


def test_cluster_down_secs(db):
    """Node-down events must accumulate into cluster down_secs per TRES."""
    mysql_cmd, ws, _, _ = db
    down_start = ws + 2000
    down_secs = 1000
    node_tres = "1=4,2=256,4=1"
    _add_cluster_event(mysql_cmd, ws)
    # NODE_STATE_DOWN = 1
    _add_node_event(mysql_cmd, "n1", 1, down_start, down_start + down_secs, node_tres)
    _rollup(ws, ws + 3600)

    for count, label in ((4, "cpu"), (256, "mem")):
        down = _sreport_cluster_util(ws, "down", tres=label)
        assert (
            down == count * down_secs
        ), f"cluster {label} down={down}, expected {count * down_secs}"


@pytest.mark.parametrize(
    "state",
    [
        pytest.param(_NODE_STATE_POWERED_DOWN, id="powered_down"),
        pytest.param(_NODE_STATE_FUTURE, id="future"),
    ],
)
def test_cluster_pdown_secs(db, state):
    """Powered-down and future node events must accumulate into cluster
    pdown_secs per TRES."""
    mysql_cmd, ws, _, _ = db
    pdown_start = ws + 2000
    pdown_secs = 1000
    node_tres = "1=4,2=256,4=1"
    _add_cluster_event(mysql_cmd, ws)
    # The rollup credits pdown_secs on both of these; NODE_STATE_POWER_DOWN
    # (bit 23) is a separate transition-request flag and rolls up as down_secs
    # instead.
    _add_node_event(
        mysql_cmd, "n1", state, pdown_start, pdown_start + pdown_secs, node_tres
    )
    _rollup(ws, ws + 3600)

    for count, label in ((4, "cpu"), (256, "mem")):
        pdown = _sreport_cluster_util(ws, "PlannedDown", tres=label)
        assert (
            pdown == count * pdown_secs
        ), f"cluster {label} pdown={pdown}, expected {count * pdown_secs}"


def test_cluster_down_secs_hour_boundary(db):
    """A node-down event spanning an hour boundary must be split across both
    hour rows, each clamped to its own window."""
    mysql_cmd, ws, _, _ = db
    node_tres = "1=4,2=256,4=1"
    # Down from 45 min into hour 1 to 15 min into hour 2: 900s in each hour.
    _add_cluster_event(mysql_cmd, ws)
    # NODE_STATE_DOWN = 1
    _add_node_event(mysql_cmd, "n1", 1, ws + 2700, ws + 4500, node_tres)
    _rollup(ws, ws + 7200)

    for hour_ws in (ws, ws + 3600):
        for count, label in ((4, "cpu"), (256, "mem")):
            down = _sreport_cluster_util(hour_ws, "down", tres=label)
            assert (
                down == count * 900
            ), f"hour {hour_ws} {label} down={down}, expected {count * 900}"


def test_cluster_pdown_secs_hour_boundary(db):
    """A powered-down event spanning an hour boundary must be split across both
    hour rows, each clamped to its own window."""
    mysql_cmd, ws, _, _ = db
    node_tres = "1=4,2=256,4=1"
    # Powered down from 45 min into hour 1 to 15 min into hour 2: 900s each.
    _add_cluster_event(mysql_cmd, ws)
    _add_node_event(
        mysql_cmd, "n1", _NODE_STATE_POWERED_DOWN, ws + 2700, ws + 4500, node_tres
    )
    _rollup(ws, ws + 7200)

    for hour_ws in (ws, ws + 3600):
        for count, label in ((4, "cpu"), (256, "mem")):
            pdown = _sreport_cluster_util(hour_ws, "PlannedDown", tres=label)
            assert (
                pdown == count * 900
            ), f"hour {hour_ws} {label} pdown={pdown}, expected {count * 900}"


def test_cluster_plan_secs(db):
    """plan_secs must accumulate cpus_req * (time_start - time_eligible) across jobs."""
    mysql_cmd, ws, assoc_id, _ = db
    _add_cluster_event(mysql_cmd, ws)
    # Job 1: waited 100s before starting.
    _add_job(
        mysql_cmd,
        db_inx=1,
        assoc_id=assoc_id,
        time_start=ws + 150,
        time_end=ws + 150 + 1800,
        time_eligible=ws + 50,
    )
    # Job 2: waited 300s before starting.
    _add_job(
        mysql_cmd,
        db_inx=2,
        assoc_id=assoc_id,
        time_start=ws + 400,
        time_end=ws + 400 + 1800,
        time_eligible=ws + 100,
    )
    _rollup(ws, ws + 3600)

    expected = _CPU_COUNT * 100 + _CPU_COUNT * 300
    actual = _sreport_cluster_util(ws, "planned", tres="cpu")
    assert actual == expected, f"cluster planned={actual}, expected {expected}"


def test_cluster_overcommitted(db):
    """Planned demand past the window must clamp, leaving the excess in over.

    sreport.1: if Allocated plus Planned time exceeds the Reported time, the
    excess is reported as OverCommitted.
    """
    mysql_cmd, ws, assoc_id, _ = db
    wait, elapsed = 3500, 100
    _add_cluster_event(mysql_cmd, ws)
    # Each job asks for the whole cluster after waiting nearly the full hour,
    # so plan demand alone is several times the window.
    for db_inx in (1, 2):
        _add_job(
            mysql_cmd,
            db_inx=db_inx,
            assoc_id=assoc_id,
            time_start=ws + wait,
            time_end=ws + wait + elapsed,
            time_eligible=ws,
            cpus_req=_CLUSTER_CPU,
        )
    _rollup(ws, ws + 3600)

    reported = _CLUSTER_CPU * 3600
    demand = 2 * _CLUSTER_CPU * wait
    expected_alloc = 2 * _CPU_COUNT * elapsed

    alloc = _sreport_cluster_util(ws, "alloc", tres="cpu")
    assert (
        alloc == expected_alloc
    ), f"cluster cpu alloc={alloc}, expected {expected_alloc}"

    planned = _sreport_cluster_util(ws, "planned", tres="cpu")
    assert planned == reported - alloc, (
        f"cluster cpu planned={planned}, expected {reported - alloc}: "
        f"{demand} of demand must clamp to what the window has left"
    )

    over = _sreport_cluster_util(ws, "OverCommitted", tres="cpu")
    assert (
        over == demand - planned
    ), f"cluster cpu over={over}, expected {demand - planned}"

    idle = _sreport_cluster_util(ws, "idle", tres="cpu")
    assert idle == 0, f"cluster cpu idle={idle}, expected 0"

    _assert_cluster_reconciles(ws, _CLUSTER_CPU)


def test_cluster_utilization_invariant(db):
    """Cluster utilization fields must satisfy the identity across multiple jobs:
    count * window == alloc + idle + plan (when down/pdown/over are 0)."""
    mysql_cmd, ws, assoc_id, _ = db
    elapsed1, elapsed2 = 1200, 1500
    _add_cluster_event(mysql_cmd, ws)
    _add_job(
        mysql_cmd,
        db_inx=1,
        assoc_id=assoc_id,
        time_start=ws + 100,
        time_end=ws + 100 + elapsed1,
        time_eligible=ws,
    )
    _add_job(
        mysql_cmd,
        db_inx=2,
        assoc_id=assoc_id,
        time_start=ws + 200,
        time_end=ws + 200 + elapsed2,
        time_eligible=ws,
    )
    _rollup(ws, ws + 3600)

    total = elapsed1 + elapsed2
    # plan_secs = cpu_count * (time_start - time_eligible) per job; CPU only.
    expected_plan_by_tres = {
        _CPU: _CPU_COUNT * 100 + _CPU_COUNT * 200,
        _MEM: 0,
    }
    for tres_id, cluster_count, job_count, label in [
        (_CPU, _CLUSTER_CPU, _CPU_COUNT, "cpu"),
        (_MEM, _CLUSTER_MEM, _MEM_COUNT, "mem"),
    ]:
        count = _sreport_cluster_util(ws, "TresCount", tres=label)
        alloc = _sreport_cluster_util(ws, "alloc", tres=label)
        idle = _sreport_cluster_util(ws, "idle", tres=label)
        over = _sreport_cluster_util(ws, "OverCommitted", tres=label)
        down = _sreport_cluster_util(ws, "down", tres=label)
        pdown = _sreport_cluster_util(ws, "PlannedDown", tres=label)
        plan = _sreport_cluster_util(ws, "planned", tres=label)
        expected_alloc = job_count * total
        expected_plan = expected_plan_by_tres[tres_id]
        expected_idle = cluster_count * 3600 - expected_alloc - expected_plan
        assert (
            count == cluster_count
        ), f"cluster {label} count={count}, expected {cluster_count}"
        assert over == 0, f"cluster {label} over_secs={over}, expected 0"
        assert down == 0, f"cluster {label} down_secs={down}, expected 0"
        assert pdown == 0, f"cluster {label} pdown_secs={pdown}, expected 0"
        assert (
            plan == expected_plan
        ), f"cluster {label} plan_secs={plan}, expected {expected_plan}"
        assert (
            alloc == expected_alloc
        ), f"cluster {label} alloc_secs={alloc}, expected {expected_alloc}"
        assert (
            idle == expected_idle
        ), f"cluster {label} idle_secs={idle}, expected {expected_idle}"


@pytest.mark.skipif(
    atf.get_version("sbin/slurmdbd") < (26, 11), reason=_RESV_IDLE_REASON
)
def test_reservation_idle_redistribution(db):
    """Idle reservation TRES-seconds must be credited to eligible assocs.

    The reservation holds two TRES and the job takes a different fraction of
    each, so an implementation that credits the first and stops is
    distinguishable from one that credits every TRES the reservation holds.
    """
    mysql_cmd, ws, assoc_id, _ = db
    job_elapsed = 1800
    resv_cpu, resv_mem = 8, 256
    _add_cluster_event(mysql_cmd, ws)
    _sql(
        mysql_cmd,
        f"INSERT INTO `{_CLUSTER}_resv_table` "
        f"(id_resv, assoclist, nodelist, resv_name, tres, "
        f"time_start, time_end, flags, unused_wall) "
        f"VALUES ({_RESV_ID}, '{assoc_id}', 'n1', 'resv{_RESV_ID}', "
        f"'1={resv_cpu},2={resv_mem}', {ws}, {ws + 3600}, 0, 0.0)",
    )
    # Use fewer TRES than the reservation holds, a quarter of the CPUs and an
    # eighth of the memory.  At exactly half, crediting the job's own TRES
    # seconds and crediting the reservation's full width give the same total,
    # so the assertion would not tell them apart; differing fractions also
    # catch a fix that reuses the first TRES's proportion for the rest.
    job_cpu, job_mem = 2, 32
    _add_job(
        mysql_cmd,
        assoc_id=assoc_id,
        resv_id=_RESV_ID,
        time_start=ws + 100,
        time_end=ws + 100 + job_elapsed,
        time_eligible=ws,
        tres_alloc=f"1={job_cpu},2={job_mem}",
        cpus_req=job_cpu,
    )
    _rollup(ws, ws + 3600)

    # The reservation is charged what the job actually used, so the idle it
    # redistributes is everything else.  With one eligible assoc that assoc
    # ends up with the whole reservation: its own job usage plus that idle.
    for resv_count, job_count, label in (
        (resv_cpu, job_cpu, "cpu"),
        (resv_mem, job_mem, "mem"),
    ):
        job_secs = job_count * job_elapsed
        resv_idle_secs = resv_count * 3600 - job_secs
        expected = job_secs + resv_idle_secs
        used = _sreport_account_used(ws, tres=label)
        assert used == expected, (
            f"assoc {label} alloc={used}, expected {expected} "
            f"(job={job_secs} + resv_idle={resv_idle_secs})"
        )

    # The reservation's own report has to agree with what it handed out: it is
    # charged the job's TRES seconds, and the rest is the idle it redistributed.
    for resv_count, job_count, label in (
        (resv_cpu, job_cpu, "cpu"),
        (resv_mem, job_mem, "mem"),
    ):
        job_secs = job_count * job_elapsed
        alloc, idle = _sreport_resv_util(ws, tres=label)
        assert alloc == job_secs, f"resv {label} Allocated={alloc}, expected {job_secs}"
        assert idle == resv_count * 3600 - job_secs, (
            f"resv {label} Idle={idle}, expected " f"{resv_count * 3600 - job_secs}"
        )

    # Reservation time still has to land somewhere in the cluster row.
    _assert_cluster_reconciles(ws, _CLUSTER_CPU)
    _assert_cluster_reconciles(ws, _CLUSTER_MEM, "mem")


@pytest.mark.parametrize(
    "flags,in_alloc,in_pdown",
    [
        pytest.param(0, True, False, id="normal"),
        pytest.param(_RESV_FLAG_MAINT, False, True, id="maint"),
        pytest.param(_RESV_FLAG_IGN_JOBS, False, False, id="ignore_jobs"),
        pytest.param(
            _RESV_FLAG_MAINT | _RESV_FLAG_IGN_JOBS, False, False, id="maint_ignore_jobs"
        ),
    ],
)
def test_reservation_time_in_cluster_usage(db, flags, in_alloc, in_pdown):
    """A reservation's TRES time must reach the cluster row even with no jobs.

    Nodes held by a reservation are unavailable to everyone else, so the
    reservation is charged to the cluster as allocated time, or as planned
    down time when it is a maintenance reservation.  A reservation carrying
    IGNORE_JOBS is not tracked in the cluster utilization report at all, so
    its time stays idle and must reach neither column.
    """
    mysql_cmd, ws, assoc_id, _ = db
    resv_cpu = 4
    _add_cluster_event(mysql_cmd, ws)
    _sql(
        mysql_cmd,
        f"INSERT INTO `{_CLUSTER}_resv_table` "
        f"(id_resv, assoclist, nodelist, resv_name, tres, "
        f"time_start, time_end, flags, unused_wall) "
        f"VALUES ({_RESV_ID}, '{assoc_id}', 'n1', 'resv{_RESV_ID}', "
        f"'1={resv_cpu}', {ws}, {ws + 3600}, {flags}, 0.0)",
    )
    _rollup(ws, ws + 3600)

    # No job ran, so the whole reservation is the only thing that can put time
    # in either column.
    resv_secs = resv_cpu * 3600
    for field, expected in (
        ("alloc", resv_secs if in_alloc else 0),
        ("PlannedDown", resv_secs if in_pdown else 0),
    ):
        used = _sreport_cluster_util(ws, field, tres="cpu")
        assert used == expected, f"cluster {field}={used}, expected {expected}"

    _assert_cluster_reconciles(ws, _CLUSTER_CPU)


@pytest.mark.skipif(
    atf.get_version("sbin/slurmdbd") < (26, 11), reason=_RESV_IDLE_REASON
)
def test_reservation_idle_whole_account(db):
    """A reservation held by a whole account credits the account association.

    Idle time from a reservation assigned to accounts rather than users is
    counted in the account's own association, so a parent account's usage can
    exceed the sum of its children.
    """
    mysql_cmd, ws, _, _ = db
    slurm_user = atf.properties["slurm-user"]
    acct_assoc_id = _account_assoc_id_of(slurm_user, _ACCOUNT)

    resv_cpu = 8
    _add_cluster_event(mysql_cmd, ws)
    _sql(
        mysql_cmd,
        f"INSERT INTO `{_CLUSTER}_resv_table` "
        f"(id_resv, assoclist, nodelist, resv_name, tres, "
        f"time_start, time_end, flags, unused_wall) "
        f"VALUES ({_RESV_ID}, '{acct_assoc_id}', 'n1', 'resv{_RESV_ID}', "
        f"'1={resv_cpu}', {ws}, {ws + 3600}, 0, 0.0)",
    )
    _rollup(ws, ws + 3600)

    # No job ran, so the whole reservation is idle and the single eligible
    # association takes all of it.
    expected = resv_cpu * 3600
    account_used = _sreport_account_used(ws, account=_ACCOUNT, user="")
    assert (
        account_used == expected
    ), f"account assoc cpu alloc={account_used}, expected {expected}"

    user_used = _sreport_account_used(ws, account=_ACCOUNT, default=0)
    assert user_used == 0, (
        f"user assoc cpu alloc={user_used}, expected 0; whole-account "
        f"reservation idle must not be credited to the user associations"
    )

    _assert_cluster_reconciles(ws, _CLUSTER_CPU)


def test_daily_rollup_aggregation(db, qos_id):
    """Day rows must equal the sum of the contributing hour rows.

    Checked for every object type the rollup writes, so a mistake that only
    affects one of them does not escape.
    """
    mysql_cmd, ws, assoc_id, wckey_id = db
    elapsed = 1800
    _add_cluster_event(mysql_cmd, ws)
    for h in range(3):
        hour_ws = ws + h * 3600
        _add_job(
            mysql_cmd,
            db_inx=h + 1,
            assoc_id=assoc_id,
            qos_id=qos_id,
            wckey_id=wckey_id,
            time_start=hour_ws + 100,
            time_end=hour_ws + 100 + elapsed,
            time_eligible=hour_ws,
        )
    # Window must cross midnight to trigger the daily rollup pass.
    day_end = _day_start(ws, 1)
    _rollup(ws, day_end)

    # An hour-aligned window under a day reads the hour rows; the day-aligned
    # window reads the day row.
    expected = _CPU_COUNT * elapsed * 3
    for obj, read in _OBJECT_READERS.items():
        hour_total = sum(read(ws + h * 3600, ws + (h + 1) * 3600) for h in range(3))
        day_total = read(ws, day_end)
        assert (
            hour_total == expected
        ), f"{obj} hourly sum={hour_total}, expected {expected}"
        assert (
            day_total == expected
        ), f"{obj} day alloc={day_total}, expected {expected}"


def test_monthly_rollup_aggregation(db, qos_id):
    """Month rows must equal the sum of the contributing day rows.

    Checked for every object type the rollup writes, so a mistake that only
    affects one of them does not escape.
    """
    mysql_cmd, _, assoc_id, wckey_id = db
    elapsed = 1800
    # First of the month two months ago gives a complete, past month boundary.
    month_ws = None
    for _days in (60, 90, 120):
        month_ws = _local_midnight(
            (datetime.now() - timedelta(days=_days)).replace(day=1)
        )
        if month_ws:
            break
    if not month_ws:
        pytest.fail("no usable month start in the last 4 months")
    _add_cluster_event(mysql_cmd, month_ws)
    for d in range(3):
        day_ws = _day_start(month_ws, d)
        _add_job(
            mysql_cmd,
            db_inx=d + 1,
            assoc_id=assoc_id,
            qos_id=qos_id,
            wckey_id=wckey_id,
            time_start=day_ws + 100,
            time_end=day_ws + 100 + elapsed,
            time_eligible=day_ws,
        )
    # Window must cross into the next month to trigger the monthly rollup pass.
    month_dt = datetime.fromtimestamp(month_ws)
    next_month = (
        month_dt.replace(day=1, month=month_dt.month % 12 + 1)
        if month_dt.month < 12
        else month_dt.replace(day=1, month=1, year=month_dt.year + 1)
    )
    # The monthly pass aggregates the whole month out of the day table, so it
    # does not need a month-wide hourly pass.  Roll up the seeded days, then a
    # one-hour window at the end of the month to trigger the monthly pass:
    # ~73 hourly iterations per cluster instead of ~720.
    _rollup(month_ws, _day_start(month_ws, 3))
    month_end_ws = int(next_month.timestamp())
    _rollup(month_end_ws - 3600, month_end_ws)

    # A day-aligned window reads the day rows; the whole-month window reads
    # the month row.
    expected = _CPU_COUNT * elapsed * 3
    for obj, read in _OBJECT_READERS.items():
        day_total = sum(
            read(_day_start(month_ws, d), _day_start(month_ws, d + 1)) for d in range(3)
        )
        month_total = read(month_ws, month_end_ws)
        assert (
            day_total == expected
        ), f"{obj} daily sum={day_total}, expected {expected}"
        assert (
            month_total == expected
        ), f"{obj} month alloc={month_total}, expected {expected}"


def test_boundary_spanning_job(db):
    """Jobs spanning the whole window are each clamped to 3600s; totals accumulate."""
    mysql_cmd, ws, assoc_id, _ = db
    _add_cluster_event(mysql_cmd, ws)
    # Both jobs start before the window and end after it: each must clamp to
    # the full 3600s window, not its true elapsed time.
    _add_job(
        mysql_cmd,
        db_inx=1,
        assoc_id=assoc_id,
        time_start=ws - 500,
        time_end=ws + 3600 + 500,
        time_eligible=ws - 500,
    )
    _add_job(
        mysql_cmd,
        db_inx=2,
        assoc_id=assoc_id,
        time_start=ws - 100,
        time_end=ws + 3600 + 100,
        time_eligible=ws - 100,
    )
    _rollup(ws, ws + 3600)

    used = _sreport_account_used(ws)
    assert (
        used == _CPU_COUNT * 3600 * 2
    ), f"spanning-job cpu alloc={used}, expected {_CPU_COUNT * 3600 * 2}"


def test_running_job_alloc(db):
    """Still-running jobs (time_end=0) are each credited from their start to the
    window end; contributions accumulate across jobs."""
    mysql_cmd, ws, assoc_id, _ = db
    _add_cluster_event(mysql_cmd, ws)
    # time_end=0 marks each job still running; the rollup clamps its end to the
    # window end, so each contributes cpus * (3600 - start_offset).
    _add_job(
        mysql_cmd,
        db_inx=1,
        assoc_id=assoc_id,
        time_start=ws + 100,
        time_end=0,
        time_eligible=ws + 100,
        state=_JOB_RUNNING,
    )
    _add_job(
        mysql_cmd,
        db_inx=2,
        assoc_id=assoc_id,
        time_start=ws + 200,
        time_end=0,
        time_eligible=ws + 200,
        state=_JOB_RUNNING,
    )
    _rollup(ws, ws + 3600)

    expected = _CPU_COUNT * (3600 - 100) + _CPU_COUNT * (3600 - 200)
    used = _sreport_account_used(ws)
    assert used == expected, f"running-job cpu alloc={used}, expected {expected}"


@pytest.mark.skipif(
    atf.get_version("sbin/slurmdbd") < (26, 11), reason=_RESV_IDLE_REASON
)
def test_reservation_idle_multi_assoc(db, second_assoc):
    """Reservation idle TRES-seconds must be split evenly across every
    eligible association, on top of each assoc's own in-reservation usage."""
    mysql_cmd, ws, assoc_id, _ = db
    assoc_id2 = second_assoc

    job_elapsed = 1800
    resv_cpu = 8
    _add_cluster_event(mysql_cmd, ws)
    _sql(
        mysql_cmd,
        f"INSERT INTO `{_CLUSTER}_resv_table` "
        f"(id_resv, assoclist, nodelist, resv_name, tres, "
        f"time_start, time_end, flags, unused_wall) "
        f"VALUES ({_RESV_ID}, '{assoc_id},{assoc_id2}', 'n1', 'resv{_RESV_ID}', "
        f"'1={resv_cpu}', {ws}, {ws + 3600}, 0, 0.0)",
    )
    # Fewer CPUs than the reservation holds; see the comment in
    # test_reservation_idle_redistribution about why half does not work.
    job_cpu = 2
    _add_job(
        mysql_cmd,
        assoc_id=assoc_id,
        resv_id=_RESV_ID,
        time_start=ws + 100,
        time_end=ws + 100 + job_elapsed,
        time_eligible=ws,
        tres_alloc=f"1={job_cpu}",
        cpus_req=job_cpu,
    )
    _rollup(ws, ws + 3600)

    # The reservation is charged what the job actually used, and the rest is
    # split evenly across the two eligible assocs.
    job_cpu_secs = job_cpu * job_elapsed
    idle_share = (resv_cpu * 3600 - job_cpu_secs) // 2
    running_assoc = _sreport_account_used(ws, account=_ACCOUNT)
    idle_assoc = _sreport_account_used(ws, account=_ACCOUNT2)
    assert running_assoc == job_cpu_secs + idle_share, (
        f"job-running assoc cpu alloc={running_assoc}, "
        f"expected {job_cpu_secs + idle_share} (job {job_cpu_secs} + "
        f"idle share {idle_share})"
    )
    assert (
        idle_assoc == idle_share
    ), f"idle-only assoc cpu alloc={idle_assoc}, expected {idle_share}"

    # No QOS is connected with idle time, so it is counted against the
    # account's default QOS, or 'normal' where there is none.  Neither test
    # account has a default.
    idle_qos = _sreport_qos_used(ws, "normal", account=_ACCOUNT2)
    assert (
        idle_qos == idle_share
    ), f"idle-only assoc normal-QOS alloc={idle_qos}, expected {idle_share}"

    # Reservation time still has to land somewhere in the cluster row.
    _assert_cluster_reconciles(ws, _CLUSTER_CPU)


@pytest.mark.skipif(atf.get_version("sbin/slurmdbd") < (26, 11), reason=_ENERGY_REASON)
def test_zero_duration_row_energy_attribution(db, second_assoc, qos_id):
    """A row with no elapsed time must not charge its energy to another id.

    ENERGY is the one TRES the rollup does not scale by elapsed time, so a job
    row that contributes no time to the window still carries a non-zero energy
    value into the accumulators.  Such a row reaches calc_cluster without
    setting them, so its energy lands on whichever assoc/QOS/wckey the
    preceding row left there.

    Both rows sit in the same hour and the assertion is on the victim: the
    running job's assoc, QOS and wckey must be charged its own energy and
    nothing more.  The rollup selects running rows (time_end=0) and completed
    rows in separate UNION ALL branches in that order, so the running job
    below reaches the accumulators before the zero-elapsed row does.
    """
    mysql_cmd, ws, assoc_id, wckey_id = db
    assoc_id2 = second_assoc
    hour2 = ws + 3600

    energy1 = 1000
    energy2 = 5000
    _add_cluster_event(mysql_cmd, ws)

    # Still running, and eligible only from hour 2, so it is the first row that
    # hour selects and it leaves the accumulators pointing at its own ids.
    _add_job(
        mysql_cmd,
        db_inx=1,
        assoc_id=assoc_id,
        qos_id=qos_id,
        wckey_id=wckey_id,
        time_start=ws + 3700,
        time_end=0,
        time_eligible=ws + 3700,
        state=_JOB_RUNNING,
        tres_alloc=f"1={_CPU_COUNT},{_ENERGY}={energy1}",
    )
    # Ends exactly on the hour boundary: real elapsed time in hour 1, none in
    # hour 2, where it is still selected and carries its energy in.
    _add_job(
        mysql_cmd,
        db_inx=2,
        assoc_id=assoc_id2,
        time_start=ws + 200,
        time_end=hour2,
        time_eligible=ws,
        tres_alloc=f"1={_CPU_COUNT},{_ENERGY}={energy2}",
    )
    _rollup(ws, ws + 7200)

    # Hour 1 is ordinary accounting: the completed job's energy is its own.
    assert (
        _sreport_account_used(ws, account=_ACCOUNT2, tres="energy") == energy2
    ), f"hour 1 assoc2 energy != {energy2}"

    for label, got in (
        ("assoc", _sreport_account_used(hour2, tres="energy")),
        ("qos", _sreport_qos_used(hour2, _QOS, tres="energy")),
        ("wckey", _sreport_wckey_used(hour2, tres="energy")),
    ):
        assert got == energy1, (
            f"hour 2 {label} energy={got}, expected {energy1}; a zero-elapsed "
            f"row charged its energy to the running job's {label}"
        )

    assert (
        _sreport_account_used(hour2, account=_ACCOUNT2, tres="energy", default=0) == 0
    ), "hour 2 assoc2 energy != 0; a zero-elapsed row was credited its own energy"
