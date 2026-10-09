############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that a job update during a requeue does not rewrite the previous run.

Ticket 25413: while a requeued job is still COMPLETING, its accounting record is
the one of the run that just ended. An update sent in that window used to
overwrite that record, e.g. its Eligible time.

A marker file holds the Epilog, so the COMPLETING window lasts as long as the
test needs it.
"""

from pathlib import Path

import pytest

import atf

# The Epilog releases itself after this many seconds even if the marker remains.
EPILOG_MAX_WAIT = 120
EPILOG_TIMEOUT = 300

_REQUEUE_ACCT_UNFIXED = atf.get_version("sbin/slurmctld") < (26, 5, 5)
_REQUEUE_ACCT_REASON = (
    "Ticket 25413: requeue window no longer rewrites the previous run's record "
    "in 26.05.5"
)

# The Epilog blocks while this file exists. Set in setup().
epilog_marker = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()

    global epilog_marker
    epilog_marker = str(Path.cwd() / "epilog_block")
    epilog = str(Path.cwd() / "epilog.sh")
    atf.make_bash_script(
        epilog,
        f"for i in $(seq 1 {EPILOG_MAX_WAIT}); do\n"
        f'    [ -f "{epilog_marker}" ] || exit 0\n'
        f"    sleep 1\n"
        f"done\n"
        f"exit 0\n",
    )
    atf.require_config_parameter("Epilog", epilog)
    atf.require_config_parameter("PrologEpilogTimeout", EPILOG_TIMEOUT)

    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def block_epilog(setup):
    Path(epilog_marker).touch()
    yield
    # Release the Epilog in case the test failed before doing it.
    Path(epilog_marker).unlink(missing_ok=True)


def _sacct_records(job_id, fields):
    """Return the given fields for every run of the job, oldest first.

    No --state filter: it can hide records with an Unknown Eligible time.
    """
    return [
        line.split("|")
        for line in atf.run_command_output(
            f"sacct -XPnj {job_id} --duplicates -o {fields}",
            quiet=True,
            fatal=True,
        )
        .strip()
        .splitlines()
        if line
    ]


def _submit_and_requeue(sbatch_args=""):
    """Submit a job, requeue it once it is accounted, and leave it COMPLETING.

    Returns the job id and the first run's [Submit, Eligible] record.
    """
    job_id = atf.submit_job_sbatch(
        f'{sbatch_args} --requeue -o /dev/null --wrap "sleep infinity"', fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # Wait for a real Eligible time, not "Unknown".
    atf.wait_for_job_accounted(job_id, "Eligible", value=r"^(?!Unknown$).+", fatal=True)
    # The Submit time tells this run's record apart from the requeued one.
    records = _sacct_records(job_id, "Submit,Eligible")
    if len(records) != 1 or records[0][1] in ("", "Unknown"):
        pytest.fail(
            f"Job {job_id} should have one record with a real eligible time "
            f"before the requeue, got {records}"
        )

    atf.run_command(
        f"scontrol requeue {job_id}", user=atf.properties["slurm-user"], fatal=True
    )
    # A requeued job that is still running its Epilog shows as COMPLETING.
    atf.wait_for_job_state(job_id, "COMPLETING", fatal=True)

    return job_id, records[0]


def _check_still_completing(job_id, action):
    """Fail unless the update landed while the Epilog still runs."""
    if atf.get_job_parameter(job_id, "JobState", fatal=True) != "COMPLETING":
        pytest.fail(f"Job {job_id} left COMPLETING before the {action} was processed")


def _release_and_get_records(request, job_id, fields):
    """Let the requeued run start its own record, and return both records.

    Arms the xfail for unfixed versions once both records exist, so a setup
    failure is still reported as a failure.
    """
    # Updates reach the database in order, so once the second record shows up,
    # any bad write before it is visible too.
    Path(epilog_marker).unlink(missing_ok=True)
    # Not fatal, so a timeout still reaches the assert below with its dump.
    for _ in atf.timer():
        if len(_sacct_records(job_id, fields)) >= 2:
            break

    records = _sacct_records(job_id, fields)
    detail = atf.run_command_output(
        f"sacct -Xj {job_id} --duplicates -o JobID,{fields},Start,End,State",
        fatal=True,
    )
    assert len(records) == 2, (
        f"Expected exactly 2 accounting records for job {job_id} (the "
        f"original run plus the requeued one); got {len(records)}:\n{detail}"
    )
    if _REQUEUE_ACCT_UNFIXED:
        request.node.add_marker(pytest.mark.xfail(reason=_REQUEUE_ACCT_REASON))

    return records, detail


def test_hold_during_requeue(request):
    """Hold a requeued job while it is COMPLETING.

    The first run's record must keep the Eligible time it had before the
    requeue.
    """

    job_id, (submit_before, eligible_before) = _submit_and_requeue()

    # A hold on a job whose priority is already 0 changes nothing.
    priority_before = atf.get_job_parameter(job_id, "Priority", fatal=True)
    if int(priority_before) <= 0:
        pytest.fail(
            f"Job {job_id} should have a non-zero priority before the hold, "
            f"got {priority_before}"
        )

    atf.run_command(
        f"scontrol hold {job_id}", user=atf.properties["slurm-user"], fatal=True
    )
    _check_still_completing(job_id, "hold")

    # A hold updates the accounting record. Priority=0 shows it went through.
    priority = atf.get_job_parameter(job_id, "Priority", fatal=True)
    if int(priority) != 0:
        pytest.fail(
            f"Job {job_id} should be held after 'scontrol hold' (Priority=0), "
            f"got {priority}"
        )

    records, detail = _release_and_get_records(request, job_id, "Submit,Eligible")
    original = [eligible for submit, eligible in records if submit == submit_before]

    assert len(original) == 1, (
        f"Exactly one record of job {job_id} should carry the pre-requeue submit "
        f"time ({submit_before}); the runs are no longer distinguishable:\n{detail}"
    )
    assert original[0] == eligible_before, (
        f"The original run's eligible time changed from {eligible_before} to "
        f"{original[0]} while job {job_id} was requeuing:\n{detail}"
    )


def test_update_during_requeue(request):
    """Rename a requeued job while it is COMPLETING.

    The first run's record must keep the original name, and the requeued run's
    record must get the new one.
    """

    job_id, (submit_before, _) = _submit_and_requeue("--job-name=original")

    atf.run_command(
        f"scontrol update JobId={job_id} JobName=renamed",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    _check_still_completing(job_id, "update")

    records, detail = _release_and_get_records(request, job_id, "Submit,JobName")
    names = {submit: name for submit, name in records}

    assert names.get(submit_before) == "original", (
        f"The original run's name should still be 'original' after job {job_id} "
        f"was renamed while requeuing:\n{detail}"
    )
    assert [n for s, n in records if s != submit_before] == [
        "renamed"
    ], f"The requeued run of job {job_id} should be named 'renamed':\n{detail}"
