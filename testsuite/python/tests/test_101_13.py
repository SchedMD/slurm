############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test a job's suspend records are closed when the job ends.

slurmdbd records each suspension of a job as a suspend record that stays
open until the job resumes.  A job can also end while suspended, and its
suspension ends with it, so the record must be closed too, or the job looks
suspended forever to anything reading the suspend history.

When slurmctld ends a suspended job, for example on scancel, it records the
end of the suspension first.  When sacctmgr fixes a runaway job, slurmctld
is not involved, and slurmdbd has to close the record itself; releases
before 26.11 did not (ticket 26019).

The records are read directly from MySQL, since sacct reports no field that
exposes an open suspend record of a finished job.

Run this only against a throwaway accounting DB.
"""

import time

import pytest

import atf

# A job id slurmctld does not know, so sacctmgr reports it as a runaway.
_RUNAWAY_JOB = 9026019

_JOB_RUNNING = 1

xfail_t26019 = pytest.mark.xfail(
    atf.get_version("sbin/slurmdbd") < (26, 11),
    reason="Ticket 26019: fixing a runaway job left its open suspend record "
    "open before 26.11",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


def _sql(mysql_cmd, stmt, fatal=True, quiet=False):
    return atf.run_command_output(
        f'{mysql_cmd} -N -B -e "{stmt.replace("`", r"\`")}"',
        user=atf.properties["slurm-user"],
        fatal=fatal,
        quiet=quiet,
    )


def _suspensions(mysql_cmd, cluster, job_id):
    """Return [(suspend start, suspend end, job end)] for every record of job_id."""
    out = _sql(
        mysql_cmd,
        f"SELECT s.time_start, s.time_end, j.time_end "
        f"FROM `{cluster}_suspend_table` s JOIN `{cluster}_job_table` j "
        f"ON s.job_db_inx=j.job_db_inx WHERE j.id_job={job_id}",
    )
    return [tuple(int(f) for f in line.split()) for line in out.splitlines() if line]


def _assert_closed(rows, label):
    assert rows, f"{label}: no suspend record found"
    for start, end, job_end in rows:
        assert end, f"{label}: suspend record starting {start} is still open"
        assert (
            start <= end <= max(start, job_end)
        ), f"{label}: suspend record {start}-{end} does not end with the job at {job_end}"


def test_cancelled_while_suspended(sql_statement_repeat):
    """A job cancelled while suspended has its suspend record closed."""
    slurm_user = atf.properties["slurm-user"]
    cluster = atf.get_config_parameter("ClusterName")
    job_id = atf.submit_job_sbatch(
        '-N1 -o /dev/null --wrap "sleep infinity"', fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.run_command(f"scontrol suspend {job_id}", user=slurm_user, fatal=True)
    atf.wait_for_job_state(job_id, "SUSPENDED", fatal=True)
    atf.wait_for_job_accounted(job_id, "State", "SUSPENDED", fatal=True)

    atf.cancel_jobs([job_id], fatal=True)
    atf.wait_for_job_accounted(job_id, "State", "CANCELLED", fatal=True)

    _assert_closed(_suspensions(sql_statement_repeat, cluster, job_id), "cancelled job")


@pytest.fixture
def runaway_job(sql_statement_repeat):
    """Seed a running job slurmctld does not know, suspended since an hour ago."""
    cluster = atf.get_config_parameter("ClusterName")
    mysql_cmd = sql_statement_repeat
    now = int(time.time())

    def _clean():
        _sql(
            mysql_cmd,
            f"DELETE s FROM `{cluster}_suspend_table` s "
            f"JOIN `{cluster}_job_table` j ON s.job_db_inx=j.job_db_inx "
            f"WHERE j.id_job={_RUNAWAY_JOB}; "
            f"DELETE FROM `{cluster}_job_table` WHERE id_job={_RUNAWAY_JOB}",
            fatal=False,
        )

    _clean()
    _sql(
        mysql_cmd,
        f"INSERT INTO `{cluster}_job_table` "
        f"(id_job, id_assoc, id_wckey, id_resv, id_user, id_group, "
        f"het_job_id, het_job_offset, state_reason_prev, nodes_alloc, "
        f"`partition`, priority, state, time_submit, time_eligible, "
        f"time_start, time_end, time_suspended, cpus_req, job_name, account) "
        f"VALUES ({_RUNAWAY_JOB}, 0, 0, 0, 0, 0, 0, 4294967294, 0, 1, "
        f"'part1', 0, {_JOB_RUNNING}, {now - 7200}, {now - 7200}, "
        f"{now - 7000}, 0, {now - 3600}, 1, 'test_101_13', 'root'); "
        f"INSERT INTO `{cluster}_suspend_table` "
        f"(job_db_inx, id_assoc, time_start, time_end) "
        f"SELECT job_db_inx, 0, {now - 3600}, 0 FROM `{cluster}_job_table` "
        f"WHERE id_job={_RUNAWAY_JOB}",
    )
    yield mysql_cmd, cluster
    _clean()


@xfail_t26019
def test_fixed_runaway_while_suspended(runaway_job):
    """Fixing a runaway job closes its open suspend record."""
    mysql_cmd, cluster = runaway_job
    # This fixes every runaway job on the cluster, not only the seeded one.
    output = atf.run_command_output(
        "sacctmgr -i show runawayjobs",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert str(_RUNAWAY_JOB) in output, f"job not reported as a runaway:\n{output}"

    _assert_closed(_suspensions(mysql_cmd, cluster, _RUNAWAY_JOB), "runaway job")
