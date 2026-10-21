############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that stepmgr releases a step that fails to launch.

Under enable_stepmgr a step that fails to launch must not leave its
resources held, so a following step in the same allocation can run.
"""

import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/srun",
        reason="Issue 51229: srun completes unlaunched steps on the step manager",
    )
    atf.require_nodes(1, [("RealMemory", 100)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_config_parameter_includes("TaskPlugin", "task/affinity")
    atf.require_config_parameter("JobAcctGatherFrequency", ["5", "task=5"])
    atf.require_slurm_running()


def run_failing_then_good_step(name, bad_srun_args, extra_sbatch_args=""):
    """Run a step that fails to launch, then a good one, in one allocation.

    Without --exact the first step takes all of the job's resources on the
    node, so the second step can only start once the failed one is released.

    Args:
        name (string): Unique basename for the script and output files.
        bad_srun_args (string): srun arguments that make the first step fail
            to launch.
        extra_sbatch_args (string): Additional sbatch arguments, such as a
            memory limit.

    Returns:
        Tuple (rc1, rc2, stderr1) with the exit codes of the failing and the
        following step, and the stderr of the failing step.
    """
    script = f"{name}.sh"
    output = f"{name}.out"

    atf.make_bash_script(
        script,
        f"""\
srun -n1 {bad_srun_args} true 2> {name}.err
echo "rc1=$?"
timeout 20 srun -n1 true
echo "rc2=$?"
""",
    )

    job_id = atf.submit_job_sbatch(
        f"-n1 {extra_sbatch_args} --output={output} {script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(output, fatal=True)

    result = atf.run_command_output(f"cat {output}", fatal=True)
    rc1 = re.search(r"rc1=(\d+)", result)
    rc2 = re.search(r"rc2=(\d+)", result)
    assert rc1 and rc2, f"Did not find both step results in output: {result!r}"
    stderr1 = atf.run_command_output(f"cat {name}.err", fatal=True)
    return int(rc1.group(1)), int(rc2.group(1)), stderr1


def test_failed_setup_step_releases_resources():
    """A step rejected by slurmstepd at setup does not strand the allocation.

    srun reports the completion to the stepmgr, so the following step
    runs. An over-range accounting frequency fails the setup under a memory
    limit."""

    rc1, rc2, stderr1 = run_failing_then_good_step(
        "setup", "--acctg-freq=task=30", extra_sbatch_args="--mem=100"
    )
    assert rc1 != 0, "The step with an invalid accounting frequency should fail"
    assert (
        "Invalid accounting frequency requested" in stderr1
    ), f"The step should have been rejected at slurmstepd setup, got: {stderr1}"
    assert rc2 == 0, "The following step should run once the failed step is released"


def test_prefork_failed_step_releases_resources():
    """A step rejected by slurmd before the fork does not strand the allocation.

    srun reports the completion to the stepmgr, so the following step runs.
    Binding to a CPU outside the step allocation fails the launch before the
    slurmstepd is forked."""

    # One past the largest node's CPUs is guaranteed outside any single-core
    # step allocation, regardless of which core the scheduler picked.
    outside_cpu = max(
        int(node.get("cpus", 1)) for node in atf.get_nodes(quiet=True).values()
    )

    rc1, rc2, stderr1 = run_failing_then_good_step(
        "prefork", f"--cpu-bind=map_cpu:{outside_cpu}"
    )
    assert rc1 != 0, "The step bound outside its allocation should fail"
    assert (
        "Unable to satisfy cpu bind request" in stderr1
    ), f"The step should have been rejected by slurmd before the fork, got: {stderr1}"
    assert rc2 == 0, "The following step should run once the failed step is released"
