############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Reject step dependencies in an allocation whose stepmgr predates 26.11."""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_upgrades()
    atf.require_version(
        (25, 5),
        "sbin/slurmd",
        slurm_prefix=atf.properties["old-slurm-prefix"],
        max_version=(26, 11),
        reason="Issue 51096: requires a pre-26.11 stepmgr",
    )
    atf.require_version(
        (26, 11),
        "bin/srun",
        slurm_prefix=atf.properties["new-slurm-prefix"],
        reason="Issue 51096: step dependencies require a 26.11 or newer srun",
    )
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_slurm_running()


@pytest.mark.xfail(
    reason="Issue 51096: allocation lookup leaves start_protocol_ver at 0, bypassing srun's 26.11 guard",
)
def test_step_dependency_rejected_in_old_allocation():
    """An upgraded srun must reject the option before creating a step."""
    atf.make_bash_script("job.sh", "touch job_started\nsleep infinity\n")
    job_id = atf.submit_job_sbatch("-N1 --exclusive -t10 job.sh", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.wait_for_file("job_started", fatal=True)
    job_info = atf.run_command_output(f"scontrol show job {job_id}", fatal=True)
    assert "StepMgrEnabled=Yes" in job_info, f"Job {job_id} must use stepmgr"

    if (
        atf.get_config_parameter("AccountingStorageType", live=False, quiet=True)
        == "accounting_storage/slurmdbd"
    ):
        atf.upgrade_component("sbin/slurmdbd")
    atf.upgrade_component("sbin/slurmctld")
    atf.upgrade_component("sbin/slurmd")
    atf.upgrade_component("sbin/slurmstepd")
    for node_name in atf.get_nodes():
        atf.start_slurmd(node_name)
    atf.upgrade_component("bin/srun")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    result = atf.run_command(
        f"srun --jobid={job_id} --mpi=none -N1 -n1 "
        f"--step-dependency=afterany:{job_id}.0 true",
        xfail=True,
    )
    assert result["exit_code"] != 0, "Step dependencies must reject old allocations"
    assert (
        "--step-dependency requires a job allocation started by Slurm 26.11 or later."
        in result["stderr"]
    ), f"Expected allocation-version rejection, got: {result}"
