############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that srun completes a step it never launched when using stepmgr."""

import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Backfill places each het component on its own node
    atf.require_nodes(3, [("CPUs", 2)])
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    # Hetjobs are only started by backfill; don't wait for its full cycle
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    # Keep the node with the killed slurmd from going DOWN during the test
    atf.require_config_parameter("SlurmdTimeout", 300)
    atf.require_slurm_running()


@pytest.fixture
def stopped_slurmd():
    """Yield a function that kills a node's slurmd, restarting it afterwards."""

    killed = []

    def kill(node):
        assert atf.stop_slurmd(node, signal="KILL"), f"slurmd for {node} should stop"
        killed.append(node)

    yield kill

    for node in killed:
        atf.start_slurmd(node)


def assert_step_completed_early(job_id, step_id):
    """Assert a step is removed while its job is still running.

    The step would otherwise stay until the job ended, so the job must still be
    running for the step's removal to show that srun completed it.
    """

    assert wait_for_step_gone(
        job_id, step_id
    ), f"Step {job_id}.{step_id} should be completed after srun gave up on it"
    assert (
        atf.get_job_parameter(job_id, "JobState", fatal=True) == "RUNNING"
    ), f"Job {job_id} should still be running when its step is completed"


def wait_for_step_gone(job_id, step_id):
    """Wait for a step srun gave up on to be completed and removed."""

    for t in atf.timer():
        output = atf.run_command_output(f"scontrol -o show step {job_id}.{step_id}")
        if not re.search(rf"StepId={job_id}\.{step_id}\b", output):
            return True
    return False


# The fix needs both a 26.11 srun, to send the completion to the stepmgr, and a
# 26.11 slurmd on the batch host, to accept it from the job owner
@pytest.mark.xfail(
    atf.get_version("bin/srun") < (26, 11) or atf.get_version("sbin/slurmd") < (26, 11),
    reason="Issue 51229: srun did not complete a step whose launch failed with stepmgr",
)
def test_launch_failed_on_node(stopped_slurmd):
    """Verify a step whose launch fails on a node does not stay RUNNING."""

    flag = "flag"
    atf.make_bash_script(
        "job.sh",
        f"""while [ ! -e {flag} ]; do sleep 1; done
srun -N2 true || echo srun_failed
sleep infinity""",
    )
    job_id = atf.submit_job_sbatch("-N2 --output=out.%j job.sh", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    batch_host = atf.get_job_parameter(job_id, "BatchHost", fatal=True)
    nodes = atf.node_range_to_list(
        atf.get_job_parameter(job_id, "NodeList", fatal=True)
    )
    other = next(n for n in nodes if n != batch_host)

    # Fail the launch on the node that is not the step manager
    stopped_slurmd(other)
    atf.run_command(f"touch {flag}", fatal=True)

    atf.assert_file_contents(
        f"out.{job_id}",
        "srun_failed",
        contains=True,
        message=f"srun in job {job_id} should fail when its launch fails on {other}",
    )
    # The step was created, so it can only go away by srun completing it
    atf.assert_file_contents(
        f"out.{job_id}",
        f"Task launch for StepId={job_id}.0 failed on node {other}",
        contains=True,
    )
    assert_step_completed_early(job_id, 0)


# Before 26.11 slurmctld gave het jobs no stepmgr, so their steps were completed
# by slurmctld and the issue didn't apply
@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Issue 50976: Het jobs only use stepmgr since 26.11",
)
# The fix needs both a 26.11 srun, to send the completion to the stepmgr, and a
# 26.11 slurmd on the batch host, to accept it from the job owner
@pytest.mark.xfail(
    atf.get_version("bin/srun") < (26, 11) or atf.get_version("sbin/slurmd") < (26, 11),
    reason="Issue 51229: srun did not complete created het components with stepmgr",
)
def test_het_component_not_created():
    """Verify created het components are completed when a later one fails."""

    atf.make_bash_script(
        "het.sh",
        """#SBATCH -N1 --output=het.%j
#SBATCH hetjob
#SBATCH -N1
#SBATCH hetjob
#SBATCH -N1
srun -N1 -n1 true : -N1 -n1 true : -N1 -n9999 true || echo srun_failed
sleep infinity""",
    )
    job_id = atf.submit_job_sbatch("het.sh", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    atf.assert_file_contents(
        f"het.{job_id}",
        "srun_failed",
        contains=True,
        message=f"srun in het job {job_id} should fail to create its last component",
    )
    jobs = atf.get_jobs(job_id, fatal=True)
    components = {
        jobs[c]["HetJobOffset"]: c
        for c in atf.range_to_list(jobs[job_id]["HetJobIdSet"])
    }

    # srun creates the components in order, so a failure creating the last
    # component's step means the steps of the first two were created first
    output = atf.run_command_output(f"cat het.{job_id}", fatal=True)
    match = re.search(r"Unable to create step for job (\d+)", output)
    assert match, f"srun in het job {job_id} should fail to create a step"
    assert (
        int(match.group(1)) == components[2]
    ), f"srun should fail on the last component {components[2]}"

    # Each component's step is completed on that component's own stepmgr
    for offset in (0, 1):
        assert_step_completed_early(components[offset], 0)
