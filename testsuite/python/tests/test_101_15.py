############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A job killed by SIGHUP is recorded as signalled (0:1).

Its wait(2) status is exactly 1: signal 1, no core dump. That has to be
stored as-is, not read as an exit status of 1.
"""

import pytest

import atf

# The batch script echoes this before sleeping, and the test waits for it to
# reach the job's output file before signalling. RUNNING is not a launch
# barrier: slurmctld sets it in select_nodes() and only then queues the
# batch-launch agent, and job_signal() returns success for a RUNNING job
# whether or not a stepd exists yet, so a signal sent too early is dropped
# silently and the job sleeps until the test times out.
LAUNCH_SENTINEL = "READY"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_slurm_running()


@pytest.mark.skipif(
    atf.get_version("bin/sacct") < (26, 11),
    reason="Ticket 24195: accounting rewrote an exit_code of 1 into 1:0 "
    "before 26.11",
)
def test_sighup_is_recorded_as_a_signal():
    """Verify a SIGHUP-killed job is recorded as 0:1, not 1:0"""

    # sleep infinity rather than a fixed duration: the job only has to
    # outlive the signal below, and it is ended explicitly.
    job_id = atf.submit_job_sbatch(
        f"--no-requeue --wrap 'echo {LAUNCH_SENTINEL}; sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.assert_file_contents(
        f"slurm-{job_id}.out",
        LAUNCH_SENTINEL,
        contains=True,
        message=f"Job {job_id} never reached its script; see LAUNCH_SENTINEL",
    )

    # --signal signals the job rather than cancelling it, so the batch
    # script's death is what ends the job and the signal reaches the job's
    # own record. A plain scancel would mark the job CANCELLED first and
    # the signal would land on the .batch step instead. --full is required
    # alongside it: scancel sends no signal but SIGKILL to the batch step
    # without it, so a bare --signal=HUP would never reach the script.
    atf.run_command(f"scancel --full --signal=HUP {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)

    exit_code, rc = (
        atf.run_command_output(
            f"sacct -j {job_id} -X --noheader -P -o ExitCode,SlurmRC", fatal=True
        )
        .strip()
        .split("|")
    )

    assert exit_code == "0:1", (
        f"A job killed by SIGHUP should be recorded as signalled (0:1), got "
        f"{exit_code}. A value of 1:0 means the wait(2) status was stored as "
        "an exit status."
    )
    assert (
        rc == "0"
    ), f"A signalled job was not failed by Slurm, so SlurmRC must be 0; got {rc}"
