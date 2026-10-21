############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sacct --state=SUSPENDED selection against the suspend history.

sacct.1 says --state "selects jobs based on their state during the time
period given".  For SUSPENDED that period is matched against each job's
suspend records, not against the job's current state, so a job that was
suspended inside the window and has since resumed or finished is still
selected, and one that is suspended now but was not during the window is
not.

This pins down which jobs are selected for every placement of a suspend
period relative to the window, including the exact-second boundaries.  Both
window ends are matched inclusively against the suspend period: one that
ends exactly at --starttime, or begins exactly at --endtime, is selected.

The job itself must also have been around during the window, as for the
default time window: ended no earlier than --starttime, and eligible before
--endtime.  So a suspend record still open on a job that has since ended
does not match windows after the job's end, and a job that became eligible
in the window's last second does not match it.  Releases before 26.11 did
not apply that bound (ticket 26019); those cases have their own test.

Rows are seeded directly in MySQL because suspend periods landing on exact
seconds relative to a window cannot be produced reliably with real jobs.
Seeding proves the selection; it does not prove slurmdbd writes these
shapes.

Run this only against a throwaway accounting DB.
"""

import collections
from datetime import datetime, timedelta

import pytest

import atf

_CLUSTER = "test10111susp"
_NODES = "t10111n[1-2]"
_NODE1 = "t10111n1"
_NODE1_INX = "0"
_NODE2 = "t10111n2"
_NODE2_INX = "1"

_NO_VAL = 0xFFFFFFFE

# Slurm job states (enum job_states and slurm.h flags)
_JOB_RUNNING = 1
_JOB_SUSPENDED = 2
_JOB_COMPLETE = 3
_JOB_REQUEUE = 0x400  # SLURM_BIT(10)

# Every offset below is in seconds from the base time chosen at seed time.
_RUN_END = 10000

# Windows (start, end).  None means the option is not given.
_W = (1000, 2000)
_LATE = (5000, 6000)
_START_ONLY = (1500, None)
_NOW = (None, None)
# Around _RUN_END, where most seeded jobs complete.
_END = (9500, 10500)

# job id -> (state, start, end, node index, [(suspend start, suspend end)]).
# A suspend end of 0 is a suspend record that was never closed.  An end of
# 0 for the job itself is a job that has not finished.
# The job became eligible at "eligible", 10 seconds before the base time
# unless given.
_Job = collections.namedtuple(
    "_Job", "state start end node suspends eligible", defaults=[-10]
)
_JOBS = {
    1: _Job(_JOB_COMPLETE, 0, _RUN_END, _NODE1_INX, [(100, 500)]),
    2: _Job(_JOB_COMPLETE, 0, _RUN_END, _NODE1_INX, [(800, 1200)]),
    3: _Job(_JOB_COMPLETE, 0, _RUN_END, _NODE2_INX, [(1200, 1500)]),
    4: _Job(_JOB_COMPLETE, 0, _RUN_END, _NODE1_INX, [(1800, 2500)]),
    5: _Job(_JOB_COMPLETE, 0, _RUN_END, _NODE1_INX, [(2500, 3000)]),
    6: _Job(_JOB_COMPLETE, 0, _RUN_END, _NODE1_INX, [(500, 3000)]),
    # Ends exactly at the window start.
    7: _Job(_JOB_COMPLETE, 0, _RUN_END, _NODE1_INX, [(500, 1000)]),
    # Begins exactly at the window end.
    8: _Job(_JOB_COMPLETE, 0, _RUN_END, _NODE2_INX, [(2000, 2500)]),
    # One second clear of the window on either side.
    9: _Job(_JOB_COMPLETE, 0, _RUN_END, _NODE1_INX, [(500, 999)]),
    10: _Job(_JOB_COMPLETE, 0, _RUN_END, _NODE1_INX, [(2001, 2500)]),
    # Suspended twice inside the window; selected once.
    11: _Job(_JOB_COMPLETE, 0, _RUN_END, _NODE1_INX, [(1100, 1200), (1300, 1400)]),
    # Still suspended.
    12: _Job(_JOB_SUSPENDED, 0, 0, _NODE1_INX, [(1500, 0)]),
    # Finished at 3000 with its suspend record left open, as a runaway fix
    # before 26.11 left a job that was suspended when it was fixed.
    13: _Job(_JOB_COMPLETE, 0, 3000, _NODE1_INX, [(1500, 0)]),
    # Never suspended.
    14: _Job(_JOB_COMPLETE, 0, _RUN_END, _NODE2_INX, []),
    # Suspended now, but only since after the window.
    15: _Job(_JOB_SUSPENDED, 0, 0, _NODE1_INX, [(2500, 0)]),
    # Suspended inside the window, running again now.
    16: _Job(_JOB_RUNNING, 0, 0, _NODE2_INX, [(1200, 1300)]),
    # First run of a requeued job, suspended inside the window.  Its second
    # run (_REQUEUED_SECOND_RUN) shares the job id and was never suspended.
    17: _Job(_JOB_REQUEUE, 0, 1600, _NODE1_INX, [(1100, 1200)]),
    # Eligible, started and suspended in the window's last second.
    18: _Job(_JOB_COMPLETE, 2000, _RUN_END, _NODE1_INX, [(2000, 2500)], 2000),
    # Ended exactly at the window start, while suspended.
    19: _Job(_JOB_COMPLETE, 0, 1000, _NODE1_INX, [(500, 1000)]),
    # Ended a second before the window start, with its suspend record open.
    20: _Job(_JOB_COMPLETE, 0, 999, _NODE1_INX, [(500, 0)]),
}
_REQUEUED_JOB = 17
_REQUEUED_SECOND_RUN = 117  # job_db_inx of the second run

# Jobs selected or not depending on the job's own lifetime; checked only by
# test_selection_bounded_by_job_lifetime and ignored everywhere else.
_LIFETIME_JOBS = {13, 18, 19, 20}

_EXPECTED = {
    _W: {2, 3, 4, 6, 7, 8, 11, 12, 16, 17},
    _LATE: {12, 15},
    # With --starttime alone the window is that single second.
    _START_ONLY: {3, 6, 12},
    # With neither, it is the current second.
    _NOW: {12, 15},
}

xfail_t26019 = pytest.mark.xfail(
    atf.get_version("sbin/slurmdbd") < (26, 11),
    reason="Ticket 26019: SUSPENDED selection was not bounded by the job's "
    "own lifetime before 26.11",
)

_Row = collections.namedtuple("_Row", "db_index jobid")


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    # Seeded rows sit in the past; a purge pass mid-test would delete them.
    atf.require_config_parameter("PurgeJobAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeSuspendAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeEventAfter", None, source="slurmdbd")
    atf.require_slurm_running()


def _sql(mysql_cmd, stmt, fatal=True, quiet=False):
    atf.run_command(
        f'{mysql_cmd} -e "{stmt.replace("`", r"\`")}"',
        user=atf.properties["slurm-user"],
        fatal=fatal,
        quiet=quiet,
    )


def _base_time():
    """Local noon three days ago.

    Noon keeps every window clear of a DST transition, so the local time
    strings passed to sacct map back to exactly these epochs.
    """
    day = datetime.now() - timedelta(days=3)
    return int(day.replace(hour=12, minute=0, second=0, microsecond=0).timestamp())


@pytest.fixture(scope="module")
def base(request, setup, sql_statement_repeat):
    """Create the test cluster, seed every case once, return the base time."""
    slurm_user = atf.properties["slurm-user"]

    # Best effort: the per-cluster tables do not exist on a fresh database,
    # and a previous interrupted run may have left rows at these keys.
    for table in ("job_table", "suspend_table", "event_table"):
        _sql(
            sql_statement_repeat,
            f"TRUNCATE TABLE {_CLUSTER}_{table}",
            fatal=False,
            quiet=True,
        )
    # Not fatal: on a fresh database there is no cluster to remove yet.
    atf.run_command(
        f"sacctmgr -i remove cluster {_CLUSTER}", user=slurm_user, quiet=True
    )
    atf.run_command(f"sacctmgr -i add cluster {_CLUSTER}", user=slurm_user, fatal=True)

    def _teardown():
        # Records with time_end=0 count as active jobs and make the cluster
        # removal refuse, so drop the jobs first.
        _sql(
            sql_statement_repeat,
            f"TRUNCATE TABLE {_CLUSTER}_job_table",
            fatal=False,
        )
        atf.run_command(
            f"sacctmgr -i remove cluster {_CLUSTER}", user=slurm_user, fatal=True
        )

    request.addfinalizer(_teardown)

    b = _base_time()

    # Node layout for --nodelist, valid from a day before any job started.
    _sql(
        sql_statement_repeat,
        f"INSERT INTO `{_CLUSTER}_event_table` "
        f"(time_start, time_end, node_name, cluster_nodes, reason, state, tres) "
        f"VALUES ({b - 86400}, 0, '', '{_NODES}', '', 0, '1=8')",
    )

    runs = [(job_id, job_id, b + job.eligible, job) for job_id, job in _JOBS.items()]
    runs.append(
        (
            _REQUEUED_SECOND_RUN,
            _REQUEUED_JOB,
            b + 1600,
            _Job(_JOB_COMPLETE, 1700, _RUN_END, _NODE1_INX, []),
        )
    )
    for db_inx, job_id, submit, job in runs:
        nodename = _NODE2 if job.node == _NODE2_INX else _NODE1
        end = b + job.end if job.end else 0
        # A suspended job's time_suspended is when it was suspended; for any
        # other job it is the total seconds it spent suspended.
        if job.state == _JOB_SUSPENDED:
            suspended = b + job.suspends[-1][0]
        else:
            suspended = sum(e - s for s, e in job.suspends if e)
        _sql(
            sql_statement_repeat,
            f"INSERT INTO `{_CLUSTER}_job_table` "
            f"(job_db_inx, id_assoc, id_wckey, id_resv, id_job, "
            f"id_user, id_group, het_job_id, het_job_offset, "
            f"state_reason_prev, nodes_alloc, `partition`, priority, state, "
            f"time_submit, time_eligible, time_start, time_end, time_suspended, "
            f"cpus_req, job_name, account, nodelist, node_inx) "
            f"VALUES ({db_inx}, 0, 0, 0, {job_id}, "
            f"0, 0, 0, {_NO_VAL}, "
            f"0, 1, 'part1', 0, {job.state}, "
            f"{submit}, {submit}, {b + job.start}, {end}, {suspended}, "
            f"1, 'test10111', 'root', '{nodename}', '{job.node}')",
        )
        for s, e in job.suspends:
            _sql(
                sql_statement_repeat,
                f"INSERT INTO `{_CLUSTER}_suspend_table` "
                f"(job_db_inx, id_assoc, time_start, time_end) "
                f"VALUES ({db_inx}, 0, {b + s}, {b + e if e else 0})",
            )

    return b


def _sacct(b, window, *opts, duplicates=True):
    """Return the sacct records selected for window as a list of _Row.

    --duplicates keeps every matching job record (sacct.1, -D), so a query
    that matched one record more than once, or matched a requeued job's other
    record, shows up here instead of being collapsed into one row.
    """
    start, end = window
    cmd = (
        f"sacct -M {_CLUSTER} --allusers -X -n -P {'-D ' if duplicates else ''}"
        f"--format=DBIndex,JobIDRaw {' '.join(opts)}"
    )
    if start is not None:
        cmd += f" -S {datetime.fromtimestamp(b + start):%Y-%m-%dT%H:%M:%S}"
    if end is not None:
        cmd += f" -E {datetime.fromtimestamp(b + end):%Y-%m-%dT%H:%M:%S}"
    out = atf.run_command_output(cmd, user=atf.properties["slurm-user"], fatal=True)
    rows = []
    for line in out.splitlines():
        fields = line.split("|")
        assert len(fields) == len(_Row._fields), f"unparsable sacct row: {line!r}"
        rows.append(_Row(int(fields[0]), int(fields[1])))
    return rows


def _suspended_jobs(b, window, *opts):
    """Return the set of job ids sacct selects as SUSPENDED in window.

    Every job here has a single record matching any SUSPENDED query (the
    requeued job's second run was never suspended), so each job id must
    appear exactly once even with --duplicates.  _LIFETIME_JOBS are left
    out; test_selection_bounded_by_job_lifetime covers them.
    """
    rows = _sacct(b, window, "--state=SUSPENDED", *opts)
    ids = [r.jobid for r in rows]
    assert len(ids) == len(set(ids)), f"a job was selected more than once: {rows}"
    assert _REQUEUED_SECOND_RUN not in {
        r.db_index for r in rows
    }, "the never-suspended run of the requeued job was selected"
    return set(ids) - _LIFETIME_JOBS


@pytest.mark.parametrize(
    "window",
    [_W, _LATE, _START_ONLY, _NOW],
    ids=["window", "late_window", "starttime_only", "now"],
)
def test_suspended_selection(base, window):
    """Exactly the jobs with a suspend period touching the window."""
    got = _suspended_jobs(base, window)
    expected = _EXPECTED[window]
    assert (
        got == expected
    ), f"missing {sorted(expected - got)}, unexpected {sorted(got - expected)}"


def test_short_state_name(base):
    """The short form S selects the same jobs as SUSPENDED."""
    rows = _sacct(base, _W, "--state=S")
    got = {r.jobid for r in rows} - _LIFETIME_JOBS
    assert got == _EXPECTED[_W], f"--state=S selected {sorted(got)}"


_RUNNING_LIST = "SUSPENDED,RUNNING"
_COMPLETED_LIST = "SUSPENDED,COMPLETED"
_TERMINAL_LIST = "BF,CA,CD,DL,F,NF,OOM,PR,S,TO"


@pytest.mark.parametrize(
    "window, states, others_match",
    [
        (_W, _RUNNING_LIST, True),
        (_W, _COMPLETED_LIST, False),
        (_W, _TERMINAL_LIST, False),
        (_LATE, _RUNNING_LIST, True),
        (_LATE, _COMPLETED_LIST, False),
        (_LATE, _TERMINAL_LIST, False),
        (_END, _RUNNING_LIST, True),
        (_END, _COMPLETED_LIST, True),
        (_END, _TERMINAL_LIST, True),
    ],
    ids=[
        f"{w}-{s}"
        for w in ("window", "late", "end")
        for s in ("running", "completed", "terminal")
    ],
)
def test_state_list_is_union(base, window, states, others_match):
    """A state list selects exactly the records its states select one by one.

    others_match says whether the states other than SUSPENDED select records
    of their own in the window, so that the list has to return more than
    SUSPENDED alone.  Jobs complete mostly at _RUN_END, and the only job
    completing in _W (19) was also suspended there.
    """
    both = {r.db_index for r in _sacct(base, window, f"--state={states}")}
    each = set()
    for state in states.split(","):
        each |= {r.db_index for r in _sacct(base, window, f"--state={state}")}
    assert (
        both == each
    ), f"--state={states} selected {sorted(both)}, its states one by one {sorted(each)}"
    suspended = {r.db_index for r in _sacct(base, window, "--state=SUSPENDED")}
    assert (
        bool(both - suspended) == others_match
    ), f"--state={states} added {sorted(both - suspended)} to SUSPENDED's records"


@pytest.mark.parametrize(
    "window, jobs, expected",
    [(_W, "3,5,14", {3}), (_NOW, "1,5,14", {1, 5})],
    ids=["window", "default_window"],
)
def test_jobs_filter(base, window, jobs, expected):
    """--jobs narrows the SUSPENDED selection to the listed jobs.

    With no --starttime or --endtime, --jobs makes the window run from Epoch
    to now (sacct.1, DEFAULT TIME WINDOW).
    """
    got = _suspended_jobs(base, window, f"--jobs={jobs}")
    assert got == expected, f"--jobs={jobs} selected {sorted(got)}"


@pytest.mark.parametrize(
    "window, expected",
    [(_W, {3, 8, 16}), (_NOW, set())],
    ids=["window", "now"],
)
def test_nodelist_filter(base, window, expected):
    """--nodelist narrows the SUSPENDED selection to jobs on that node.

    The "now" case: a node with a running job and nothing suspended returns
    no jobs, and returns successfully.
    """
    got = _suspended_jobs(base, window, f"--nodelist={_NODE2}")
    assert got == expected, f"--nodelist={_NODE2} selected {sorted(got)}"


def test_truncate_keeps_selection(base):
    """--truncate changes reported times, not which jobs are selected."""
    got = _suspended_jobs(base, _W, "--truncate")
    assert got == _EXPECTED[_W], f"--truncate selected {sorted(got)}"


@pytest.mark.parametrize(
    "states, db_index",
    [("SUSPENDED", _REQUEUED_JOB), ("SUSPENDED,RUNNING", _REQUEUED_SECOND_RUN)],
    ids=["suspended", "suspended_running"],
)
def test_requeued_job_without_duplicates(base, states, db_index):
    """Without --duplicates, a requeued job shows one record.

    It is the most recent of its records the query matched (sacct.1, -D):
    the suspended first run for SUSPENDED alone, and the later second run
    once RUNNING also matches that one.
    """
    rows = _sacct(base, _W, f"--state={states}", duplicates=False)
    got = [r for r in rows if r.jobid == _REQUEUED_JOB]
    assert got == [
        _Row(db_index, _REQUEUED_JOB)
    ], f"--state={states} without --duplicates showed {got}"


@pytest.mark.parametrize(
    "window, expected",
    [
        pytest.param(_W, {13, 19}, marks=xfail_t26019),
        pytest.param(_LATE, set(), marks=xfail_t26019),
        pytest.param(_START_ONLY, {13}, marks=xfail_t26019),
        pytest.param(_NOW, set(), marks=xfail_t26019),
    ],
    ids=["window", "late_window", "starttime_only", "now"],
)
def test_selection_bounded_by_job_lifetime(base, window, expected):
    """A job is selected only for windows it was around in.

    Job 13 ended at 3000 with its suspend record left open, so it matches
    windows up to its end and none after.  Job 18 became eligible in the
    window's last second, so it does not match that window.  Job 19 ended
    exactly at the window start and matches it; job 20 ended a second
    earlier with its record open and does not.
    """
    rows = _sacct(base, window, "--state=SUSPENDED")
    got = {r.jobid for r in rows} & _LIFETIME_JOBS
    assert (
        got == expected
    ), f"of jobs {sorted(_LIFETIME_JOBS)}, --state=SUSPENDED selected {got}"
