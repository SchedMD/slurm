############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
from pathlib import Path

import pytest

import atf

oom_started = "Allocating"
oom_finished = "Done."
sleeper_survived = "SLEEPER SURVIVED OOM_KILL_STEP"
post_oom_success = "POST OOM STEP SUCCEEDED"

step_memory_mib = 32
job_memory_mib = 128
# Large enough to exceed step_memory_mib, but still fit inside job_memory_mib.
oom_allocation_mib = 92


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", ["select/cons_tres", "select/linear"])
    atf.require_config_parameter_includes(
        "SelectTypeParameters", ["CR_Core_Memory", "CR_Memory"]
    )

    atf.require_config_parameter_includes("TaskPlugin", "cgroup")

    atf.require_config_parameter("ConstrainRAMSpace", "yes", source="cgroup")
    atf.require_config_parameter("AllowedRAMSpace", 100, source="cgroup")

    # Make OOM deterministic
    atf.require_config_parameter("ConstrainSwapSpace", "yes", source="cgroup")
    atf.require_config_parameter("AllowedSwapSpace", 0, source="cgroup")

    # Ensure step_memory_mib is not clipped
    atf.require_config_parameter("MinRAMSpace", 30, source="cgroup")
    atf.require_config_parameter("CgroupPlugin", "autodetect", source="cgroup")

    atf.require_nodes(2, [("CPUs", 2), ("RealMemory", job_memory_mib)])
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def step_script(use_memory_program):
    path = Path("step.sh").absolute()
    atf.make_bash_script(
        path,
        f"""
if [ "$SLURM_PROCID" -eq 0 ]; then
    {use_memory_program} {oom_allocation_mib} 1
else
    sleep 5
    echo "{sleeper_survived} $SLURM_PROCID"
fi
""",
    )

    return str(path)


@pytest.mark.parametrize(
    "num_nodes",
    [
        pytest.param(1, id="single-node"),
        pytest.param(2, id="multi-node"),
    ],
)
@pytest.mark.parametrize(
    "oom_kill_step",
    [
        pytest.param(0, id="oom-kill-step-disabled"),
        pytest.param(1, id="oom-kill-step-enabled"),
    ],
)
def test_oom_kill_step(num_nodes, oom_kill_step, step_script):
    """Test that sbatch's --oom-kill-step flag controls OOM step cleanup."""

    ntasks_per_node = 2 if num_nodes == 1 else 1
    output_file = f"oom_kill_{num_nodes}_{oom_kill_step}.out"
    batch_script = f"oom_kill_{num_nodes}_{oom_kill_step}.sh"
    atf.make_bash_script(
        batch_script,
        f"""
#SBATCH --output={output_file}
#SBATCH --nodes={num_nodes}
#SBATCH --ntasks=2
#SBATCH --ntasks-per-node={ntasks_per_node}
#SBATCH --mem={job_memory_mib}M

srun --mem={step_memory_mib}M --kill-on-bad-exit=0 {step_script}
srun --nodes=1 --ntasks=1 --ntasks-per-node=1 echo "{post_oom_success}"
""",
    )

    job_id = atf.submit_job_sbatch(
        f"--oom-kill-step={oom_kill_step} {batch_script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    for t in atf.timer():
        output = atf.run_command_output(f"cat {output_file}")
        if post_oom_success in output:
            break
    else:
        assert False, f"Output file should contain the last line: {post_oom_success}"

    assert oom_started in output, "OOM script task should start"
    assert (
        "step tasks have been OOM Killed" in output
    ), "Step OOM killer should be triggered"
    if oom_kill_step:
        assert (
            sleeper_survived not in output
        ), "--oom-kill-step=1 should kill all step tasks after the OOM event"
    else:
        assert (
            sleeper_survived in output
        ), "--oom-kill-step=0 should NOT kill healthy step tasks after the OOM event"
    assert (
        oom_finished not in output
    ), "OOM script finished allocation instead of being killed"
