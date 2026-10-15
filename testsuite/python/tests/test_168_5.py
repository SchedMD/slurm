############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify that a job array of a heterogeneous job is rejected.

Heterogeneous jobs and job arrays cannot be combined, so asking for both
must be refused rather than silently dropping one of them.
"""

import pytest

import atf

HETJOB_ARRAY_ERROR = "Heterogeneous jobs and job arrays cannot be combined"

# slurmctld picks the error but sbatch renders it from its own copy of the
# error table, so sbatch has to be new enough to expect the specific message.
# slurmctld is never older than its client commands, so this covers it too.
# Otherwise only the generic submission failure can be matched.
if atf.get_version("bin/sbatch") >= (26, 11):
    expected_error = HETJOB_ARRAY_ERROR
else:
    expected_error = "Batch job submission failed"

# Every submission below differs only in --array and --test-only, so a
# rejection cannot come from the components themselves.
COMPONENTS = "-t1 -N1 -o /dev/null : -t1 -N1 -o /dev/null --wrap 'date'"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_config_parameter("MaxArraySize", 2, ">=")
    atf.require_slurm_running()


def test_hetjob_without_array():
    """Test that the same hetjob without --array is accepted."""
    assert atf.submit_job_sbatch(
        COMPONENTS
    ), "A hetjob without --array should be accepted"


def test_hetjob_array():
    """Test that a hetjob submitted with --array is rejected."""
    result = atf.run_command(f"sbatch -a 0-1 {COMPONENTS}", xfail=True)
    assert (
        result["exit_code"] != 0
    ), f"A hetjob with --array should be rejected, but printed {result['stdout']!r}"
    assert (
        expected_error in result["stderr"]
    ), f"sbatch should report {expected_error!r}, but printed {result['stderr']!r}"


@pytest.mark.skipif(
    atf.get_version("bin/sbatch") < (26, 11),
    reason="Ticket 25423: sbatch --test-only did not reject a hetjob with --array before 26.11",
)
def test_hetjob_array_test_only():
    """Test that --test-only rejects a hetjob with --array."""
    result = atf.run_command(f"sbatch --test-only -a 0-1 {COMPONENTS}", xfail=True)
    assert (
        result["exit_code"] != 0
    ), f"--test-only should reject a hetjob with --array, but printed {result['stdout']!r}"
    assert (
        HETJOB_ARRAY_ERROR in result["stderr"]
    ), f"sbatch should report {HETJOB_ARRAY_ERROR!r}, but printed {result['stderr']!r}"
