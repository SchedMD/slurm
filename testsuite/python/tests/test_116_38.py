############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

node_count = 9


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(node_count)
    atf.require_slurm_running()


def test_test_only():
    """Test of slurm_job_will_run API, (srun --test-only option)."""

    error_output = atf.run_command_error(
        f"srun --test-only -O -N{node_count} printenv SLURMD_NODENAME"
    )
    assert (
        re.search(r"Job \d+ to start at \d+", error_output) is not None
    ), "Failed out output job number or start time"


@pytest.mark.xfail(
    atf.get_version("bin/srun") < (26, 5),
    reason="MR !3126: srun --test-only --jobid fixed in 26.05",
)
def test_test_only_with_jobid():
    """Test --test-only with --jobid for a pending (held) job"""

    job_id = atf.submit_job_sbatch("-N1 -H --wrap 'sleep infinity'", fatal=True)
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)

    results = atf.run_command(
        f"srun --test-only --jobid={job_id}",
        fatal=True,
    )
    combined = results["stderr"] + results["stdout"]
    assert (
        re.search(rf"Job {job_id} to start at ", combined) is not None
    ), f"Expected --test-only to report will-run for pending job id {job_id}"

    atf.cancel_jobs([job_id])
