############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Regression test for bug 25240: per-run accounting after a requeue.

Each run of a requeued batch job must have its own accounting row with a
distinct restart count (0 for the first run, 1 for the requeued run) and a
valid start time.  scontrol requeue is documented for running, suspended or
finished jobs; the requeueable states reachable without a suspend are covered:

- COMPLETING: requeuing while the Epilog is still running.
- RUNNING: an ordinary requeue of a running job.
- COMPLETED: requeuing a job that has already finished.
"""

import collections
import os

import pytest

import atf

# The Epilog blocks while a flag file exists, so the COMPLETING window stays
# open exactly until the test has issued the requeue and removed the flag.
# Hard cap (seconds) so a dead test cannot wedge the node in COMPLETING.
EPILOG_MAX_WAIT = 30
# Short job body so the job reaches COMPLETING quickly.
JOB_SLEEP = 1
# Long enough that the job is reliably observed RUNNING before it is requeued,
# short enough that waiting for the requeued run to finish stays cheap.
RUNNING_JOB_SLEEP = 10

ACCOUNT = "acct25240"
USER = atf.properties["test-user"]

# sacct renders an unset timestamp as one of these; a zero start_time -- the
# symptom reported alongside the duplicated restart count -- prints as Unknown.
EMPTY_TIMES = ("", "Unknown", "None")

Row = collections.namedtuple("Row", "restarts start submit state end dbindex")

# A requeue path to drive a job through, and what its accounting must show.
# start_precedes_requeue records whether the job body ran to completion before
# the requeue, which is what lets the first run's Start be ordered against the
# requeue's Submit.
Scenario = collections.namedtuple(
    "Scenario",
    "wait_state job_sleep hold_epilog first_state start_precedes_requeue",
)

SCENARIOS = {
    "completing": Scenario("COMPLETING", JOB_SLEEP, True, "COMPLETED", True),
    "running": Scenario("RUNNING", RUNNING_JOB_SLEEP, False, "REQUEUED", False),
    "finished": Scenario("COMPLETED", JOB_SLEEP, False, "COMPLETED", True),
}


@pytest.fixture(scope="module", autouse=True)
def setup(epilog_script):
    # Both tests need the requeued instance to start promptly, which relies on
    # SchedulerParameters=requeue_delay.  Without it the requeue is deferred by
    # AuthInfo cred_expire (~120s) and the tests only ever time out.
    atf.require_version(
        (25, 11),
        reason="Ticket 25240: SchedulerParameters=requeue_delay was added in 25.11",
    )

    atf.require_accounting(modify=True)
    # The corrupting accounting update is only emitted from the requeue limit
    # re-validation path when limit enforcement is on; without this the bug
    # cannot be reproduced.
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")

    atf.require_config_parameter("Epilog", epilog_script)
    # EpilogSlurmctld would add an extra COMPLETING barrier that masks the race.
    atf.require_config_parameter("EpilogSlurmctld", None)

    # A requeued batch job is not eligible to run again until requeue_delay
    # elapses (default: AuthInfo cred_expire, ~120s).  Set it to 0 and schedule
    # tightly so the requeued instance starts promptly once the node frees.
    _require_scheduler_parameters(requeue_delay=0, bf_interval=1, sched_interval=1)

    atf.require_slurm_running()

    # With limit enforcement on, the submitting user needs a valid association.
    atf.run_command(
        f"sacctmgr -i add account {ACCOUNT}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {USER} account={ACCOUNT}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    # Delete the association before the parent account.
    atf.run_command(
        f"sacctmgr -i delete user {USER} account={ACCOUNT}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i delete account {ACCOUNT}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.fixture(scope="module")
def epilog_flag():
    """Flag file that keeps the Epilog (and thus COMPLETING) alive."""
    return os.path.abspath("epilog_flag")


@pytest.fixture
def epilog_gate(epilog_flag):
    """Yield the Epilog flag path, lowered before and after the test.

    Raising the flag is left to the test body because only some requeue paths
    need the COMPLETING window held open.  The teardown is the safety net that
    keeps a failed test from wedging the node in COMPLETING.
    """
    atf.run_command(f"rm -f {epilog_flag}", fatal=True)
    yield epilog_flag
    atf.run_command(f"rm -f {epilog_flag}", fatal=True)


@pytest.fixture(scope="module")
def epilog_script(epilog_flag):
    """Epilog that blocks on the flag file to hold the COMPLETING window open."""
    script = "epilog.sh"
    atf.make_bash_script(
        script,
        f"""for i in $(seq 1 {EPILOG_MAX_WAIT}); do
    [ -f "{epilog_flag}" ] || exit 0
    sleep 1
done
""",
    )
    return os.path.abspath(script)


def _restart_rows(job_id):
    """Return a Row per sacct accounting record for the job.

    --duplicates makes sacct emit every accounting row for the job, including
    the row for the original (first) run, which sacct otherwise omits.
    DBIndex identifies which record each row is, so it is collected alongside
    Submit.  SLUID would be redundant (it renders the same value as DBIndex)
    and OriginalSLUID does not exist on every supported version.
    """
    output = atf.run_command_output(
        f"sacct -X -j {job_id} --duplicates --noheader --parsable2 "
        "--format=Restarts,Start,Submit,State,End,DBIndex",
        fatal=True,
    )
    rows = []
    for line in output.splitlines():
        fields = [field.strip() for field in line.split("|")]
        if len(fields) == len(Row._fields):
            rows.append(Row(*fields))
    return rows


def _by_submit(rows):
    """Return rows oldest-Submit first; a requeue resets the Submit time.

    Submit has one-second resolution, so DBIndex breaks a tie: the requeued
    run is inserted after the original and gets the higher index.
    """
    return sorted(rows, key=lambda row: (row.submit, int(row.dbindex)))


def _assert_distinct_records(rows, job_id):
    """Assert each run got its own database record.

    The bug is a job_start landing on the previous run's record, so the
    restart counts alone are only a symptom; pin the record identity too.
    """
    assert len({row.dbindex for row in rows}) == len(rows), (
        f"Ticket 25240: expected a distinct DBIndex per run of job {job_id}; "
        f"got {rows}"
    )


def _require_scheduler_parameters(**params):
    """Set SchedulerParameters subparameters, replacing any existing values.

    Appending would leave an earlier token for the same key in place, and
    slurmctld reads the first occurrence -- so a config that already sets one
    of these (the perf variants set sched_interval=30) would win over us.
    """
    current = atf.get_config_parameter("SchedulerParameters", live=False, default="")
    kept = [
        token
        for token in (current or "").split(",")
        if token and token.split("=")[0] not in params
    ]
    kept += [f"{key}={value}" for key, value in params.items()]
    atf.require_config_parameter("SchedulerParameters", ",".join(kept))


@pytest.mark.parametrize(
    "scenario",
    [
        pytest.param(
            SCENARIOS["completing"],
            id="completing",
            marks=pytest.mark.xfail(
                atf.get_version("sbin/slurmctld") < (26, 5, 4),
                reason="Ticket 25240: restart count fix on requeue landed in 26.05.4",
            ),
        ),
        pytest.param(SCENARIOS["running"], id="running"),
        pytest.param(SCENARIOS["finished"], id="finished"),
    ],
)
def test_requeue_keeps_distinct_restart_cnt(scenario, epilog_gate):
    """Each run of a requeued job must keep its own accounting record.

    Drives a job to the scenario's state, requeues it, and pins the per-run
    accounting contract: two sacct --duplicates rows with distinct DBIndex,
    restart counts [0, 1] in Submit order, each row keeping its own Start and
    final State, and the non-duplicate view resolving to the requeued run.

    Only the COMPLETING scenario is corrupted by ticket 25240, so only it
    carries the xfail marker; the other scenarios pass on an unfixed tree.
    """

    if scenario.hold_epilog:
        atf.run_command(f"touch {epilog_gate}", fatal=True)

    # Each run appends the restart count it was handed, to be checked against
    # the Restarts recorded for it.  The first run is not given the variable.
    restart_count_file = os.path.abspath("restart_count.out")
    job_id = atf.submit_job_sbatch(
        f"--account={ACCOUNT} --requeue -N1 "
        f"--wrap 'echo ${{SLURM_RESTART_COUNT:-0}} >> {restart_count_file}; "
        f"sleep {scenario.job_sleep}'",
        fatal=True,
    )

    assert atf.wait_for_job_state(job_id, scenario.wait_state), (
        f"Job {job_id} never reached {scenario.wait_state}; cannot exercise this "
        f"requeue path"
    )

    atf.run_command(
        f"scontrol requeue {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Release the Epilog so the requeued instance can start immediately; the
    # fixture teardown lowers the flag too if we failed before reaching here.
    atf.run_command(f"rm -f {epilog_gate}", fatal=True)

    # Wait until the requeued instance has its own row and has recorded a Start.
    # Key on Submit order, not on the restart count: the bug rewrites the old
    # row's count to 1, so a count-keyed wait is satisfied by the corruption
    # itself and would break before the second row ever appears.  We don't wait
    # for the transient PENDING state because requeue_delay=0 and tight
    # scheduling can move the job PENDING->RUNNING faster than a poll interval.
    rows = []
    for _ in atf.timer(fatal=True):
        rows = _restart_rows(job_id)
        if len(rows) >= 2 and _by_submit(rows)[-1].start not in EMPTY_TIMES:
            break

    _assert_distinct_records(rows, job_id)

    # Core assertion: bind each restart_cnt to its row by Submit order.  A
    # requeue resets the Submit time, so the original run (earlier Submit) must
    # carry restart_cnt 0 and the requeued run (later Submit) restart_cnt 1.
    # The bug writes the new count into the old row instead.
    assert [row.restarts for row in _by_submit(rows)] == ["0", "1"], (
        f"Ticket 25240: expected restart_cnt [0, 1] in Submit order for job "
        f"{job_id}; got {rows}"
    )

    # Both runs must keep their own real Start.  Merely checking for a non-empty
    # value would still pass if the requeued run's Start were written into the
    # old row, so bind the timestamps to their rows.
    first_row, second_row = _by_submit(rows)
    assert all(
        row.start not in EMPTY_TIMES for row in (first_row, second_row)
    ), f"Ticket 25240: accounting row with missing Start for job {job_id}: {rows}"

    if scenario.start_precedes_requeue:
        assert first_row.start < second_row.submit < second_row.start, (
            f"Ticket 25240: expected the first run's Start to precede the requeue "
            f"and the requeued run's Start to follow it for job {job_id}; got {rows}"
        )
    else:
        # The requeue lands in the same second the job started, so its Submit
        # cannot be ordered against the first Start here.  Comparing the two
        # Starts still catches the clobber: a rewritten old row would carry the
        # requeued run's Start.
        assert (
            first_row.start < second_row.start
        ), f"Expected each run to keep its own Start for job {job_id}; got {rows}"

    # The requeued instance must run to completion with the same job id, and the
    # counts must survive the job-complete write -- the field reports show the
    # corruption on completed rows.
    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), f"Requeued job {job_id} never completed"

    rows = []
    for _ in atf.timer(fatal=True):
        rows = _restart_rows(job_id)
        if len(rows) >= 2 and all(row.end not in EMPTY_TIMES for row in rows):
            break

    _assert_distinct_records(rows, job_id)

    assert [row.restarts for row in _by_submit(rows)] == ["0", "1"], (
        f"Ticket 25240: expected restart counts [0, 1] in Submit order to survive "
        f"completion of job {job_id}; got {rows}"
    )

    # Bind State per row rather than asserting it over the set: the requeue
    # leaves each run with its own final state, and a clobbered record would
    # otherwise be hidden by an all() that both rows happen to satisfy.
    first_row, second_row = _by_submit(rows)
    assert (first_row.state, second_row.state) == (scenario.first_state, "COMPLETED"), (
        f"Expected the original run of job {job_id} to end {scenario.first_state} "
        f"and the requeued run to end COMPLETED; got {rows}"
    )

    # Without --duplicates sacct promises exactly the most recent record, so
    # the requeued run must be the one it picks.
    output = atf.run_command_output(
        f"sacct -X -j {job_id} --noheader --parsable2 --format=Restarts,Submit",
        fatal=True,
    )
    default_rows = [line.strip() for line in output.splitlines() if line.strip()]
    assert (
        len(default_rows) == 1
    ), f"Expected one non-duplicate row for job {job_id}; got {default_rows}"

    restarts, submit = (field.strip() for field in default_rows[0].split("|"))
    latest_row = _by_submit(rows)[-1]
    assert (restarts, submit) == (latest_row.restarts, latest_row.submit), (
        f"Expected the non-duplicate row for job {job_id} to be the requeued "
        f"run {latest_row}; got Restarts={restarts} Submit={submit}"
    )

    # Tie the records back to what the runs themselves saw: SLURM_RESTART_COUNT
    # is handed to the requeued instance, so it must agree with the Restarts
    # stored for that run.  This is the invariant the bug broke.
    seen_counts = atf.run_command_output(
        f"cat {restart_count_file}", fatal=True
    ).split()

    assert seen_counts == [row.restarts for row in _by_submit(rows)], (
        f"Expected the restart count seen by each run of job {job_id} to match "
        f"the Restarts recorded for it; got {seen_counts} against {rows}"
    )
