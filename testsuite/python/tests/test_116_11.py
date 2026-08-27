############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re
from datetime import datetime
from pathlib import Path

import pytest

import atf


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


def test_immediate_does_not_wait_on_a_queued_step():
    """
    Spawn a srun with --immediate for a step while the job's own resources
    are busy. --immediate applies to step allocations as well as job
    allocations, so it must fail promptly instead of inheriting the
    MAX(60, SlurmctldTimeout) wait a queued step otherwise gets.
    """
    # With defer, job start waits for the periodic scheduling loop, which can
    # consume most of this test's budget before the step srun even runs.
    atf.require_config_parameter_excludes("SchedulerParameters", "defer")

    out = Path("immediate_step.out")
    ready = Path("immediate_step_ready")
    script = Path("immediate_step.sh")
    # The first step takes the whole allocation, so the second cannot start
    # until it ends. Aborting when the hog never took it keeps a slow runner
    # from reading as a product failure.
    atf.make_bash_script(
        script,
        f"""srun -N1 --exclusive sh -c 'touch "{ready}"; exec sleep infinity' &
for _ in $(seq 1 60); do [ -f '{ready}' ] && break; sleep 0.5; done
[ -f '{ready}' ] || {{ echo HOG_NOT_READY; exit 1; }}
start=$SECONDS
srun --immediate=1 -N1 --exclusive true
echo "IMMEDIATE_RC=$?"
echo "IMMEDIATE_ELAPSED=$((SECONDS - start))"
""",
    )
    job_id = atf.submit_job(
        "sbatch",
        f"-N1 --exclusive -t5 --output={out} --error={out}",
        str(script),
        wrap_job=False,
        fatal=True,
    )
    assert job_id != 0, "sbatch should submit the job"

    # Longer than the atf default: the script first waits up to 30s for the
    # hog to take the allocation before srun --immediate even runs.
    atf.assert_file_contents(out, "IMMEDIATE_ELAPSED=", contains=True, timeout=90)
    text = atf.run_command_output(f"cat {out}", quiet=True, fatal=True)
    assert (
        "IMMEDIATE_RC=0" not in text
    ), f"srun --immediate=1 should fail while the allocation is busy, got: {text}"
    # Pin the failure to the --immediate path rather than any other srun error.
    assert (
        "Unable to create step for job" in text
    ), f"srun should report it could not create the step, got: {text}"
    elapsed = int(re.search(r"IMMEDIATE_ELAPSED=(\d+)", text).group(1))
    assert elapsed < 5, (
        f"srun --immediate=1 should give up after its 1s step wait, not wait "
        f"out the pending-step timeout; took {elapsed}s"
    )
