############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Performance test: sacct --state=SUSPENDED must not scan the job table.

Ticket 26019: "sacct --state=SUSPENDED" read every record in the cluster's
job table to answer a question about a short window, so on a site with years
of job history it made sacct time out waiting for slurmdbd while
"--state=RUNNING" for the same window returned at once.

Wall-clock limits need a table large enough to make a scan slow on any
machine, which takes far too long to seed.  Instead this seeds a modest
table of old, finished jobs and measures how many rows the database reads
to answer each query, from the sum of the server's Handler_read_* counters,
which every index lookup and table scan advances (MySQL and MariaDB both
keep them).  A query
that uses an index to find the window reads a handful of rows; one that
scans the table reads all of them.  That difference does not depend on
hardware or load, so the threshold can sit far from both outcomes.

The counter is server-wide, so other clients' reads land in it too; each
query is measured twice and the smaller reading is used.

SUSPENDED is also measured inside state lists, where every state's
condition has to be bounded by time for the database to use an index: with
RUNNING, and with the terminal states of a typical accounting query.

RUNNING and the default (no --state) query are measured the same way, so a
change that fixes SUSPENDED by slowing another state is caught here too.

Run this only against a throwaway accounting DB.
"""

import logging
from datetime import datetime, timedelta

import pytest

import atf

_CLUSTER = "test10112perf"
_NO_VAL = 0xFFFFFFFE

_JOB_RUNNING = 1
_JOB_SUSPENDED = 2
_JOB_COMPLETE = 3

_NODE = "n1"

# Old finished jobs that no query here should need to look at.
_OLD_JOBS = 100000
# A query that reads more than this has scanned the table.
_MAX_ROWS_READ = _OLD_JOBS // 10

_SUSPENDED_JOB = _OLD_JOBS + 1
_RUNNING_JOB = _OLD_JOBS + 2
# Suspended now, and eligible only after _W, so no query over _W selects it.
_SUSPENDED_NOW_JOB = _OLD_JOBS + 3

# Window, in seconds from the base time.
_W = (1000, 2000)

xfail_t26019 = pytest.mark.xfail(
    atf.get_version("sbin/slurmdbd") < (26, 11),
    reason="Ticket 26019: sacct --state=SUSPENDED scanned the whole job "
    "table before 26.11",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter("PurgeJobAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeSuspendAfter", None, source="slurmdbd")
    atf.require_slurm_running()


def _sql(mysql_cmd, stmt, fatal=True, quiet=False):
    return atf.run_command_output(
        f'{mysql_cmd} -N -B -e "{stmt.replace("`", r"\`")}"',
        user=atf.properties["slurm-user"],
        fatal=fatal,
        quiet=quiet,
    )


def _rows_read(mysql_cmd):
    """Return the total rows the server has read through any access path."""
    out = _sql(mysql_cmd, "SHOW GLOBAL STATUS LIKE 'Handler_read_%'", quiet=True)
    counters = [line.split() for line in out.splitlines() if line.strip()]
    if not counters:
        pytest.fail(f"no Handler_read_* status counters:\n{out}")
    return sum(int(value) for _name, value in counters)


def _base_time():
    """Local noon three days ago, clear of any DST transition."""
    day = datetime.now() - timedelta(days=3)
    return int(day.replace(hour=12, minute=0, second=0, microsecond=0).timestamp())


def _digits(alias):
    union = " union all ".join(f"select {d} d" for d in range(10))
    return f"({union}) {alias}"


@pytest.fixture(scope="module")
def seeded(request, setup, sql_statement_repeat):
    """Create the test cluster and seed it; return (mysql command, base time)."""
    slurm_user = atf.properties["slurm-user"]
    mysql_cmd = sql_statement_repeat

    for table in ("job_table", "suspend_table", "event_table"):
        _sql(mysql_cmd, f"TRUNCATE TABLE {_CLUSTER}_{table}", fatal=False, quiet=True)
    # Not fatal: on a fresh database there is no cluster to remove yet.
    atf.run_command(
        f"sacctmgr -i remove cluster {_CLUSTER}", user=slurm_user, quiet=True
    )
    atf.run_command(f"sacctmgr -i add cluster {_CLUSTER}", user=slurm_user, fatal=True)

    def _teardown():
        # Records with time_end=0 count as active jobs and make the cluster
        # removal refuse, so drop the jobs first.
        _sql(mysql_cmd, f"TRUNCATE TABLE {_CLUSTER}_job_table", fatal=False)
        atf.run_command(
            f"sacctmgr -i remove cluster {_CLUSTER}", user=slurm_user, fatal=True
        )

    request.addfinalizer(_teardown)

    # Mark usage as already rolled up to now.  Otherwise slurmdbd's next
    # rollup pass would walk every hour since the oldest seeded job, reading
    # the whole job table and blocking slurmdbd for the duration.
    hour = datetime.now().replace(minute=0, second=0, microsecond=0)
    day = hour.replace(hour=0)
    month = day.replace(day=1)
    _sql(mysql_cmd, f"DELETE FROM {_CLUSTER}_last_ran_table")
    _sql(
        mysql_cmd,
        f"INSERT INTO {_CLUSTER}_last_ran_table "
        f"(hourly_rollup, daily_rollup, monthly_rollup) VALUES "
        f"({int(hour.timestamp())}, {int(day.timestamp())}, "
        f"{int(month.timestamp())})",
    )

    b = _base_time()
    # Old jobs: one a second, each running ten minutes, all over well before
    # the window.
    old = b - 10 * 86400
    seq = "a.d + 10 * b.d + 100 * c.d + 1000 * d.d + 10000 * e.d + 1"
    assert _OLD_JOBS == 100000, "the digit cross join below makes 10^5 rows"
    columns = (
        "job_db_inx, id_assoc, id_wckey, id_resv, id_job, id_user, id_group, "
        "het_job_id, het_job_offset, state_reason_prev, nodes_alloc, "
        "`partition`, priority, state, time_submit, time_eligible, "
        "time_start, time_end, time_suspended, cpus_req, job_name, account, "
        "nodelist, node_inx"
    )
    _sql(
        mysql_cmd,
        f"INSERT INTO `{_CLUSTER}_job_table` ({columns}) "
        f"SELECT n, 0, 0, 0, n, 0, 0, 0, {_NO_VAL}, 0, 1, 'part1', 0, "
        f"{_JOB_COMPLETE}, {old} + n, {old} + n, {old} + n + 60, "
        f"{old} + n + 660, 0, 1, 'test10112', 'root', 'n1', '0' "
        f"FROM (SELECT {seq} AS n FROM {_digits('a')}, {_digits('b')}, "
        f"{_digits('c')}, {_digits('d')}, {_digits('e')}) s",
    )

    # One job suspended inside the window and one running through it.
    for job_id, state, end, suspended in [
        (_SUSPENDED_JOB, _JOB_COMPLETE, b + 10000, 300),
        (_RUNNING_JOB, _JOB_RUNNING, 0, 0),
    ]:
        _sql(
            mysql_cmd,
            f"INSERT INTO `{_CLUSTER}_job_table` ({columns}) VALUES "
            f"({job_id}, 0, 0, 0, {job_id}, 0, 0, 0, {_NO_VAL}, 0, 1, "
            f"'part1', 0, {state}, {b - 10}, {b - 10}, {b}, {end}, "
            f"{suspended}, 1, 'test10112', 'root', '{_NODE}', '0')",
        )
    _sql(
        mysql_cmd,
        f"INSERT INTO `{_CLUSTER}_job_table` ({columns}) VALUES "
        f"({_SUSPENDED_NOW_JOB}, 0, 0, 0, {_SUSPENDED_NOW_JOB}, 0, 0, 0, "
        f"{_NO_VAL}, 0, 1, 'part1', 0, {_JOB_SUSPENDED}, {b + 2500}, "
        f"{b + 2500}, {b + 2600}, 0, {b + 3000}, 1, 'test10112', 'root', "
        f"'{_NODE}', '0')",
    )
    _sql(
        mysql_cmd,
        f"INSERT INTO `{_CLUSTER}_suspend_table` "
        f"(job_db_inx, id_assoc, time_start, time_end) "
        f"VALUES ({_SUSPENDED_JOB}, 0, {b + 1200}, {b + 1500}), "
        f"({_SUSPENDED_NOW_JOB}, 0, {b + 3000}, 0)",
    )
    # Node layout for --nodelist, valid from before the oldest job.
    _sql(
        mysql_cmd,
        f"INSERT INTO `{_CLUSTER}_event_table` "
        f"(time_start, time_end, node_name, cluster_nodes, reason, state, tres) "
        f"VALUES ({old - 86400}, 0, '', '{_NODE}', '', 0, '1=8')",
    )
    _sql(mysql_cmd, f"ANALYZE TABLE {_CLUSTER}_job_table", quiet=True)

    # A table scan must register in the counter, or every assertion below
    # would pass without measuring anything.
    before = _rows_read(mysql_cmd)
    _sql(mysql_cmd, f"SELECT SUM(cpus_req) FROM {_CLUSTER}_job_table", quiet=True)
    if _rows_read(mysql_cmd) - before < _OLD_JOBS:
        pytest.fail(
            "Ticket 26019: Handler_read_* did not count a table scan, so "
            "rows read cannot be measured on this server"
        )

    return mysql_cmd, b


def _measure(seeded, window, *opts):
    """Run sacct over window, or its default window if None.

    Return (job ids, fewest rows read).
    """
    mysql_cmd, b = seeded
    cmd = f"sacct -M {_CLUSTER} --allusers -X -n -P --format=JobIDRaw"
    if window:
        cmd += (
            f" -S {datetime.fromtimestamp(b + window[0]):%Y-%m-%dT%H:%M:%S}"
            f" -E {datetime.fromtimestamp(b + window[1]):%Y-%m-%dT%H:%M:%S}"
        )
    cmd += f" {' '.join(opts)}"
    jobs, reads = None, None
    for _ in range(2):
        before = _rows_read(mysql_cmd)
        out = atf.run_command_output(cmd, user=atf.properties["slurm-user"], fatal=True)
        read = _rows_read(mysql_cmd) - before
        reads = read if reads is None else min(reads, read)
        jobs = {int(line) for line in out.splitlines() if line}
    logging.info(f"sacct {' '.join(opts) or '(no --state)'} read {reads} rows")
    return jobs, reads


@xfail_t26019
@pytest.mark.parametrize(
    "window, states, opts, expected",
    [
        (_W, "SUSPENDED", [], {_SUSPENDED_JOB}),
        (None, "SUSPENDED", [f"--nodelist={_NODE}"], {_SUSPENDED_NOW_JOB}),
        (_W, "SUSPENDED,RUNNING", [], {_SUSPENDED_JOB, _RUNNING_JOB}),
        (_W, "BF,CA,CD,DL,F,NF,OOM,PR,S,TO", [], {_SUSPENDED_JOB}),
    ],
    ids=["window", "now_nodelist", "with_running", "with_terminal"],
)
def test_suspended_reads_only_window(seeded, window, states, opts, expected):
    """--state=SUSPENDED finds its job without reading the old jobs.

    The "now_nodelist" case is the default window, as in
    "sacct -N <node> --state=S -X".
    """
    jobs, reads = _measure(seeded, window, f"--state={states}", *opts)
    assert jobs == expected, f"--state={states} selected {sorted(jobs)}"
    assert reads < _MAX_ROWS_READ, (
        f"--state={states} read {reads} rows to find {len(expected)} jobs "
        f"among {_OLD_JOBS} old ones"
    )


@pytest.mark.parametrize(
    "opts, expected",
    [
        (["--state=RUNNING"], {_SUSPENDED_JOB, _RUNNING_JOB}),
        ([], {_SUSPENDED_JOB, _RUNNING_JOB}),
    ],
    ids=["running", "no_state"],
)
def test_other_queries_read_only_window(seeded, opts, expected):
    """RUNNING and the default query stay index lookups too."""
    jobs, reads = _measure(seeded, _W, *opts)
    assert jobs == expected, f"sacct {' '.join(opts)} selected {sorted(jobs)}"
    assert reads < _MAX_ROWS_READ, (
        f"sacct {' '.join(opts) or '(no --state)'} read {reads} rows "
        f"to find {len(expected)} jobs among {_OLD_JOBS} old ones"
    )
