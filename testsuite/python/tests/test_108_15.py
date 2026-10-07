############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the `scontrol completing` output, including the SLUID on requeue."""

import os
import re

import pytest

import atf

# Long enough to observe the job in COMPLETING, short enough to keep the test
# cheap. The job body sleeps for the same duration.
completing_delay = 5


@pytest.fixture(scope="module", autouse=True)
def setup(epilog_script):
    atf.require_config_parameter("Epilog", epilog_script)
    # A requeued job is not eligible to run again until requeue_delay elapses
    # (default: AuthInfo cred_expire, 120 seconds), and the schedulers need to
    # run often enough to pick it up once it is eligible
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("requeue_delay", 0), source="slurm"
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("sched_interval", 1), source="slurm"
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("bf_interval", 1), source="slurm"
    )
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def epilog_script():
    """Epilog that holds the job in COMPLETING long enough to sample it."""
    script = "epilog.sh"
    atf.make_bash_script(script, f"sleep {completing_delay}\n")
    return os.path.abspath(script)


def _get_completing_sluids():
    """Return the `scontrol completing` output and its {JobId: SLUID} dict.

    The raw output is returned alongside the dict so that a job listed without
    a SLUID field is distinguishable from a job that is not listed at all.
    """

    output = atf.run_command_output("scontrol completing", fatal=True)
    return output, {
        int(match.group(1)): match.group(2)
        for match in re.finditer(r"JobId=(\d+) SLUID=(\S+) ", output)
    }


def test_completing_command_succeeds():
    """Test that `scontrol completing` is recognized and reaches the controller"""

    assert (
        atf.run_command_exit("scontrol completing") == 0
    ), "`scontrol completing` should be a recognized keyword and reach the controller"


def test_completing_lists_completing_job():
    """Test that `scontrol completing` lists a job that is completing"""

    job_id = atf.submit_job_sbatch(f'-t1 --wrap "sleep {completing_delay}"', fatal=True)

    atf.wait_for_job_state(job_id, "COMPLETING", fatal=True)
    output, _ = _get_completing_sluids()
    assert (
        f"JobId={job_id} " in output
    ), f"JobId={job_id} not listed by `scontrol completing`: {output}"


@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="Issue 51098: `scontrol completing` did not report the SLUID "
    "before 26.11.",
)
def test_completing_sluid_after_requeue():
    """Test that `scontrol completing` reports the SLUID of the completing instance"""

    job_id = atf.submit_job_sbatch(
        f'--requeue -t1 --wrap "sleep {completing_delay}"', fatal=True
    )

    # Capture the SLUID of the first run of the job while it is completing
    atf.wait_for_job_state(job_id, "COMPLETING", fatal=True)
    output, completing_sluids = _get_completing_sluids()
    assert (
        f"JobId={job_id} " in output
    ), f"JobId={job_id} not listed by `scontrol completing`: {output}"
    assert (
        job_id in completing_sluids
    ), f"JobId={job_id} is listed by `scontrol completing` with no SLUID: {output}"
    first_sluid = completing_sluids[job_id]
    assert first_sluid == atf.get_job_parameter(job_id, "SLUID", fatal=True), (
        f"The SLUID reported for JobId={job_id} by `scontrol completing` "
        f"(SLUID={first_sluid}) should be the SLUID of the job"
    )

    atf.run_command(
        f"scontrol requeue {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # A requeued job keeps its SLUID until it is done completing, so capture the
    # second SLUID once the job is completing again after having been requeued
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETING", fatal=True)
    output, completing_sluids = _get_completing_sluids()
    assert (
        f"JobId={job_id} " in output
    ), f"Requeued JobId={job_id} not listed by `scontrol completing`: {output}"
    assert (
        job_id in completing_sluids
    ), f"Requeued JobId={job_id} is listed with no SLUID: {output}"
    second_sluid = completing_sluids[job_id]
    assert second_sluid == atf.get_job_parameter(job_id, "SLUID", fatal=True), (
        f"The SLUID reported for the requeued JobId={job_id} by `scontrol "
        f"completing` (SLUID={second_sluid}) should be the SLUID of the job"
    )

    # Guard that the two samples really came from different instances of the
    # job, without which the assertions above could both hold vacuously. That a
    # requeue rotates the SLUID is covered by test_101_2.
    assert second_sluid != first_sluid, (
        f"The SLUID of JobId={job_id} in the `scontrol completing` output should "
        f"have changed after the job was requeued (SLUID={second_sluid})"
    )
