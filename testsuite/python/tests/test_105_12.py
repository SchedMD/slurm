############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test job_mem_limit_enforce() cancels an over-memory job via OverMemoryKill.

The combined memory usage of two overlapping steps exceeds the job memory
limit while neither step alone exceeds it, with
JobAcctGatherParams=OverMemoryKill configured.
"""

import pytest

import atf

job_mem_mib = 100
# Each step uses step_mib MiB; two steps combined (2 * step_mib) exceed
# job_mem_mib so the job is killed, but neither step alone exceeds it.
# This exercises the slurmd-level job_mem_limit_enforce() path rather than
# the per-step slurmstepd enforcement path.
step_mib = 55
node_real_mem_mib = 256
health_check_interval = 10
gather_freq_secs = 4
# Hold long enough for at least two HealthCheckInterval cycles plus one
# JobAcctGatherFrequency sample and step startup overhead.
hold_secs = 2 * health_check_interval + gather_freq_secs + 6


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "sbin/slurmd",
        reason="Issue 51067: REQUEST_CANCEL_JOB_STEP memcpy fix available in 26.05+",
    )
    atf.require_config_parameter("SelectType", ["select/cons_tres", "select/linear"])
    atf.require_config_parameter_includes(
        "SelectTypeParameters", ["CR_Core_Memory", "CR_Memory"]
    )
    atf.require_config_parameter("JobAcctGatherType", "jobacct_gather/cgroup")
    atf.require_config_parameter_includes("JobAcctGatherParams", "OverMemoryKill")
    atf.require_config_parameter_includes(
        "JobAcctGatherFrequency", ("task", gather_freq_secs)
    )
    # OverMemoryKill enforcement is documented to run on JobAcctGather sampling,
    # but job-level detection also fires from job_mem_limit_enforce() on each
    # health-check firing. This scaffolding is an implementation-timing crutch to
    # detect the over-limit condition promptly; it is not documented behavior.
    atf.require_config_parameter("HealthCheckInterval", health_check_interval)
    atf.require_config_parameter("HealthCheckNodeState", "ANY")
    atf.require_config_parameter("HealthCheckProgram", "/bin/true")
    atf.require_config_parameter("HealthCheckTimeout", 0)
    # Disable cgroup RAM and swap constraining so the kernel does not OOM-kill
    # a step before job_mem_limit_enforce() can detect the over-limit condition
    # and send REQUEST_CANCEL_JOB_STEP to the controller.
    atf.require_config_parameter("ConstrainRAMSpace", "no", source="cgroup")
    atf.require_config_parameter("ConstrainSwapSpace", "no", source="cgroup")
    atf.require_nodes(1, [("RealMemory", node_real_mem_mib)])
    atf.require_accounting()
    atf.require_slurm_running()


def test_over_memory_kill(use_memory_program):
    """job_mem_limit_enforce() cancels a job when two overlapping steps
    collectively exceed the job memory limit."""

    job_id = atf.submit_job_sbatch(
        f"--mem={job_mem_mib}M --wrap='"
        f"srun --overlap {use_memory_program} {step_mib} {hold_secs} & "
        f"srun --overlap {use_memory_program} {step_mib} {hold_secs} & "
        f"wait'",
        fatal=True,
    )

    for _ in atf.timer(fatal=True):
        if (
            atf.get_step_parameter(f"{job_id}.0", "State") == "RUNNING"
            and atf.get_step_parameter(f"{job_id}.1", "State") == "RUNNING"
        ):
            break

    atf.wait_for_job_state(job_id, "CANCELLED", fatal=True)

    # sacct reads slurmdbd, which is updated asynchronously from the slurmctld
    # state that wait_for_job_state() polled, so retry until the record settles.
    state = None
    for _ in atf.timer(fatal=True):
        state = atf.run_command_output(f"sacct -nP -X -j {job_id} -o state%30").strip()
        if state and state != "RUNNING":
            break
    assert state == "CANCELLED by 0", (
        f"Expected job to be cancelled by the daemon (uid 0) via OverMemoryKill, "
        f"got: {state!r}"
    )
