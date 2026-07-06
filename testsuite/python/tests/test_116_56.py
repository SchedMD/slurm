############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Regression test for Ticket 25434: srun --relative with -c but without -n."""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5, 2),
        "sbin/slurmd",
        reason="Ticket 25434: srun --relative with -c and no -n was deferred indefinitely",
    )
    atf.require_slurm_running()


def test_relative_cpus_per_task_no_ntasks():
    job_id = atf.submit_job_sbatch(
        "-N1 -t1 -o /dev/null --wrap 'srun --relative=0 -c1 -I true'",
        fatal=True,
    )

    # With the bug: -I makes srun exit immediately -> job reaches FAILED.
    # With the fix: step runs immediately -> job reaches COMPLETED.
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    state = atf.get_job_parameter(job_id, "JobState")
    assert state == "COMPLETED", f"Expected COMPLETED but got {state!r}"
