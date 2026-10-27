############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test SLURM_STEP_ID and SLURM_STEPID of the batch, interactive and srun steps.

Only the steps launched with srun, the interactive one included, have a step
id in the environment. The batch step has none.
"""

import re

import pytest

import atf

# SLURM_INTERACTIVE_STEP, as srun sets it for the interactive step.
interactive_step_id = "4294967290"

# The slurmstepd of 26.05 replaced the batch and interactive step ids with
# negative numbers. Older ones did not, so only 26.05 is expected to fail.
xfail_negative_ids = pytest.mark.xfail(
    (26, 5) <= atf.get_version("sbin/slurmd") < (26, 11),
    reason="Issue 51002: special steps got a negative SLURM_STEP_ID in 26.05",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    # salloc given no command runs its interactive step, and one that needs no
    # terminal is enough to print the step's environment.
    atf.require_config_parameter_includes("LaunchParameters", "use_interactive_step")
    atf.require_config_parameter(
        "InteractiveStepOptions", '"--interactive --preserve-env env"'
    )
    atf.require_slurm_running()


@xfail_negative_ids
def test_batch_step():
    """The batch step has no step id, while its srun steps have their own."""

    file_out = "step_ids.out"
    script = "step_ids.sh"
    ids = "${SLURM_STEP_ID-unset} ${SLURM_STEPID-unset}"
    atf.make_bash_script(
        script,
        f"echo BATCH {ids}\n"
        f"srun -n1 bash -c 'echo STEP {ids}'\n"
        f"srun -n1 bash -c 'echo STEP {ids}'\n",
    )
    job_id = atf.submit_job_sbatch(
        f"-N1 --output={file_out} --error=step_ids.err {script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    atf.assert_file_contents(file_out, "BATCH unset unset\nSTEP 0 0\nSTEP 1 1")


@xfail_negative_ids
def test_interactive_step():
    """The interactive step has the step id srun gave it."""

    result = atf.run_command("salloc -N1")
    assert result["exit_code"] == 0, f"salloc failed: {result}"

    for name in ["SLURM_STEP_ID", "SLURM_STEPID"]:
        match = re.search(rf"^{name}=(.*)$", result["stdout"], re.M)
        assert match, f"The interactive step has no {name}: {result['stdout']}"
        assert (
            match.group(1) == interactive_step_id
        ), f"The interactive step has {name}={match.group(1)}: {result['stdout']}"
