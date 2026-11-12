############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that --signal=R: jobs are rejected when PreemptMode=OFF."""

import pytest

import atf

PREEMPTION_REQUIRED = "Preemption is required but is disabled on this system"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("PreemptType", None)
    atf.require_config_parameter("PreemptMode", "OFF")
    atf.require_slurm_running()


@pytest.mark.parametrize(
    "client,args",
    [
        ("sbatch", "-o /dev/null --wrap true"),
        ("salloc", "true"),
        ("srun", "true"),
    ],
    ids=["sbatch", "salloc", "srun"],
)
def test_signal_r_rejected(client, args):
    """Test that a job with R: is rejected and creates no job."""
    # The same job without R: is accepted
    atf.run_command(f"{client} --signal=USR1@0 {args}", fatal=True)
    jobs = atf.get_jobs(quiet=True, fatal=True)

    result = atf.run_command(f"{client} --signal=R:USR1@0 {args}", xfail=True)
    assert result["exit_code"] != 0, f"{client} with R: should be rejected"
    assert (
        PREEMPTION_REQUIRED in result["stderr"]
    ), f"{client} should report {PREEMPTION_REQUIRED!r}, got {result['stderr']!r}"
    new_jobs = atf.get_jobs(quiet=True, fatal=True).keys() - jobs.keys()
    assert not new_jobs, f"{client} with R: should create no job, got {new_jobs}"
