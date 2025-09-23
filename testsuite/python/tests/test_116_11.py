############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
from datetime import datetime


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def idle_node():
    """Wait until at least one node is idle"""
    atf.repeat_until(
        lambda: atf.get_nodes(quiet=True),
        lambda nodes: any(node["state"] == ["IDLE"] for node in nodes.values()),
        fatal=True,
    )


@pytest.mark.parametrize("itime", ["", "=1", "=5", "=10"])
def test_immediate_run(itime, idle_node):
    """
    Verify that a job submitted with --immediate runs if the system has
    available resources.
    """
    assert (
        atf.run_command_exit(f"srun --immediate{itime} true", timeout=5) == 0
    ), "srun --immediate should end correctly and quickly"


def assert_fail(results):
    assert results["exit_code"] == 1, "srun should fail"
    assert (
        results["duration"] < 2
    ), "srun should fail as soon as the controller responds"
    assert (
        "Unable to allocate resources" in results["stderr"]
    ), "srun message should be correct"


def assert_cancel(job_id, itime):
    job = atf.get_jobs()[job_id]

    submit_time = datetime.fromisoformat(job["SubmitTime"])
    end_time = datetime.fromisoformat(job["EndTime"])
    elapsed = (end_time - submit_time).total_seconds()

    assert elapsed >= itime, f"Job should wait at least {itime} seconds"
    assert elapsed < itime + 5, f"Job should be cancelled soon after {itime} seconds"
    assert job["JobState"] == "CANCELLED", "Job should be cancelled"


def test_immediate_hold():
    """
    Spawn a srun with --immediate and --hold (priority==0) option.
    The job can't run immediately with a priority of zero.
    """
    results = atf.run_command("srun --immediate --hold true", xfail=True)
    assert_fail(results)


@pytest.fixture(scope="function")
def block_job_node():
    # submit a job to block the cluster
    job_id = atf.submit_job_sbatch("--exclusive --wrap 'sleep infinity'", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    yield atf.get_jobs()[job_id]["NodeList"]

    atf.cancel_jobs([job_id], fatal=True)


@pytest.mark.parametrize("itime", ["", "=1"])
def test_immediate_fail(itime, block_job_node):
    """
    Spawn a srun with --immediate with default 1s while the cluster is busy.
    The job can't run immediately, so submission should fail immediately.
    """
    results = atf.run_command(
        f"srun -w {block_job_node} --immediate{itime} true", xfail=True
    )
    assert_fail(results)


@pytest.mark.parametrize("itime", [2, 5, 10])
def test_immediate_cancel(itime, block_job_node):
    """
    Spawn a srun with --immediate with some seconds while the cluster is busy.
    The job can't run on those seconds, so job should exists but should be
    cancelled once those seconds pass.
    """
    job_id = atf.submit_job_srun(
        f"-w {block_job_node} --immediate={itime} true", xfail=True
    )
    assert_cancel(job_id, itime)
