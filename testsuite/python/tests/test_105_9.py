############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
"""
Expedited requeue (--requeue=expedite) — expected behavior per SOW.

Requirements (SOW):
- Job success (exit 0): job completes; no requeue or epilog check.
- Job failure (non-zero exit): allocated resources remain reserved until
  Epilog has completed on all nodes (SOW 2a).
- Epilog indicates one or more nodes have a hardware failure (epilog
  non-zero or node down): job presumed failed due to hardware and gets
  expedited requeue scheduling (SOW 2b).
- All Epilogs indicate healthy nodes: job presumed failed due to
  job-specific problems; no expedited requeue; all resources released
  (SOW 2c).
- Expedited requeue jobs: effective infinite priority; time limit
  recalculated (original minus prior run plus slack); not blocked by
  accounting limits (SOW 3).
- No cred_expire delay before relaunch; SLUID tracks invocations (SOW 1).

Tested in this file:
- Job success (exit 0): job completes, no requeue.
- Job failure, all epilogs succeed: job requeued in REQUEUE_HOLD, resources released.
- Job failure, at least one epilog fails: expedited requeue, node drained.
- Job success, epilog fails: node drained; job completes (no requeue criteria set, so no requeue).
- Node failure during job: expedited requeue, allocation cleared.

Untested (not covered by this file):
- Resources kept idle until epilog completes (SOW 2a); no assertion.
- Expedited requeue disabled (enable_expedited_requeue off); submit rejected.
- SOW item 3: infinite priority, time limit recalculation, accounting bypass.
- Multi-node job with partial epilog failure (some nodes fail, some succeed).
- Node down or timeout while waiting for epilog complete.
- Requeue delay removed (no cred_expire wait before relaunch).
"""
import atf
import pytest


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to set and unset Epilog")
    atf.require_config_parameter_includes(
        "SlurmctldParameters", "enable_expedited_requeue"
    )
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def node(setup):
    yield next(iter(atf.nodes))


@pytest.fixture(scope="function", autouse=True)
def cancel_jobs(setup):
    yield
    atf.cancel_jobs(atf.properties["submitted-jobs"])


@pytest.fixture(scope="function", autouse=True)
def resume_node(setup, cancel_jobs, node):
    yield
    atf.run_command(
        f"scontrol update nodename={node} state=RESUME",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.wait_for_node_state(node, "IDLE", fatal=True)


def test_expedited_requeue_success():
    """Test that --requeue=expedite does NOT requeue on successful completion."""

    # Submit a job that will succeed
    job_id = atf.submit_job_sbatch(
        '--requeue=expedite --wrap "true"',
        fatal=True,
    )

    # Wait for job to complete
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    # Verify job completed (not requeued)
    job_state = atf.get_job_parameter(job_id, "JobState")
    assert job_state == "COMPLETED", f"Job should have COMPLETED state, got {job_state}"


def test_expedited_requeue_failure():
    """Test that --requeue=expedite requeues with hold when job fails and all epilogs succeed (job-specific failure)."""

    # Submit a job that will fail (epilogs succeed by default)
    job_id = atf.submit_job_sbatch(
        '--requeue=expedite --wrap "false"',
        fatal=True,
    )

    # Wait for the job to reach REQUEUE_HOLD (job failed, epilogs OK -> not expedited)
    atf.repeat_until(
        lambda: atf.get_job_parameter(job_id, "JobState"),
        lambda state: state == "REQUEUE_HOLD",
    )
    job_state = atf.get_job_parameter(job_id, "JobState")
    assert (
        job_state == "REQUEUE_HOLD"
    ), f"Job should be REQUEUE_HOLD when job fails and all epilogs succeed (no expedited requeue), got {job_state}"

    # Verify ExpeditedRequeue flag is properly set
    expedited_requeue = atf.get_job_parameter(job_id, "ExpeditedRequeue")
    assert (
        expedited_requeue == "Yes"
    ), f"ExpeditedRequeue flag should be set for job {job_id}, got {expedited_requeue}"

    # Verify job was requeued (Restarts > 0 and ExitCode shows failure)
    restart_cnt = atf.get_job_parameter(job_id, "Restarts")
    assert (
        int(restart_cnt) > 0
    ), f"Job should have Restarts > 0 after requeue, got {restart_cnt}"

    exit_code = atf.get_job_parameter(job_id, "ExitCode")
    assert exit_code.startswith(
        "1:"
    ), f"Job should have failed with exit code 1, got {exit_code}"


@pytest.fixture(scope="function")
def epilog_failure(tmp_path):
    """
    Set Epilog to a failing script
    """
    prev_epilog = atf.get_config_parameter("Epilog")

    epilog = str(tmp_path / "epilog.sh")
    atf.make_bash_script(
        epilog,
        """#!/bin/bash
# Epilog that always fails to simulate failure detection
exit 1
        """,
    )
    atf.set_config_parameter("Epilog", epilog)

    yield

    atf.set_config_parameter("Epilog", prev_epilog)
    atf.run_command(f"rm -f {epilog}", fatal=True)


def test_expedited_requeue_epilog_failure(epilog_failure, node):
    """When epilog fails with job exit 0, node is drained; job completes (no requeue criteria, so no requeue)."""

    # Submit a job that succeeds but epilog will fail (run long enough to see RUNNING)
    job_id = atf.submit_job_sbatch(
        f'--requeue=expedite -w {node} --wrap "true"',
        fatal=True,
    )

    # Wait for job to complete (epilog fails, node drains; job completes)
    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), "Job should be COMPLETED even if epilog failed"

    # Epilog failure must drain the node
    assert atf.wait_for_node_state(
        node, "DRAIN"
    ), f"Node {node} should be drained when epilog failed"


def test_expedited_requeue_job_and_epilog_failure(epilog_failure, node):
    """Test that --requeue=expedite does expedited requeue when both job and epilog fail."""

    # Submit a job that fails (non-zero exit) AND epilog will fail (run long enough to see RUNNING)
    job_id = atf.submit_job_sbatch(
        f'--requeue=expedite -w {node} --wrap "false"',
        fatal=True,
    )

    # Expedited requeue: job must NOT be in REQUEUE_HOLD (that would mean epilogs all succeeded)
    assert atf.wait_for_job_state(
        job_id, "EXPEDITING"
    ), "Job should not be REQUEUE_HOLD when epilog failed (expedited requeue expected)"

    # Verify the node is unavailable (epilog failure drains the node)
    reason = (atf.get_job_parameter(job_id, "Reason") or "").upper()
    assert (
        "DRAINED" in reason
    ), f"Job reason should indicate node is drained, got {reason}"

    # Verify ExpeditedRequeue flag is set
    expedited_requeue = atf.get_job_parameter(job_id, "ExpeditedRequeue")
    assert (
        expedited_requeue == "Yes"
    ), f"ExpeditedRequeue flag should be set for job {job_id}, got {expedited_requeue}"


def test_expedited_requeue_node_failure(node):
    """Test that --requeue=expedite does expedited requeue when node fails during job."""

    # Submit a long-running job with expedited requeue on the specific node
    job_id = atf.submit_job_sbatch(
        f'--requeue=expedite -N1 -w {node} --wrap "sleep 60"',
        fatal=True,
    )

    # Wait for job to start running
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # Simulate node failure by setting it DOWN (this test brings the node back up in finally)
    atf.run_command(
        f"scontrol update nodename={node} state=DOWN reason=test_node_failure",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Wait for job to be requeued (EXPEDITING) after node DOWN
    assert atf.wait_for_job_state(
        job_id, "EXPEDITING"
    ), "Job should be EXPEDITING when node goes down"

    # Requeued job should lose its node that now is down
    atf.repeat_until(
        lambda: atf.get_job_parameter(job_id, "NodeList"),
        lambda new_node: new_node != node,
    )
    new_node = atf.get_job_parameter(job_id, "NodeList")
    assert new_node != node, f"{node} should be removed from job's NodeList"

    # Verify ExpeditedRequeue flag is set
    expedited_requeue = atf.get_job_parameter(job_id, "ExpeditedRequeue")
    assert (
        expedited_requeue == "Yes"
    ), f"ExpeditedRequeue flag should be set for job {job_id}, got {expedited_requeue}"

    # Verify job was requeued (Restarts > 0)
    restart_cnt = atf.get_job_parameter(job_id, "Restarts")
    assert (
        int(restart_cnt) > 0
    ), f"Job should have Restarts > 0 after node failure, got {restart_cnt}"
