############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify a step create carrying an array task id is served by the stepmgr.

A create request whose array_task_id is set is resolved through
stepmgr_ops->find_job_array_rec(). stepmgr_init() fills in no default for that
op, so each daemon that serves creates installs its own. slurmctld does, and so
must slurmstepd, which serves the create under
SlurmctldParameters=enable_stepmgr. Without the op the stepd would crash rather
than find the task.

srun sets array_task_id from --jobid=<array_job_id>_<task_id>. A plain step
never carries it to the create: existing_allocation() clears the global copy
once it has resolved the allocation. A heterogeneous step does carry it,
because each component's options were snapshotted while the arguments were
parsed, which is before that clear. So the het invocation below is not
incidental -- it is the shape that reaches the array lookup.
"""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    # stepmgr is enabled when both SlurmctldParameters=enable_stepmgr and
    # PrologFlags=Contain are set.
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    # A het step's components do not share a node, so the two below need one
    # node each.
    atf.require_nodes(2, [("CPUs", 1)])
    # Both components are submitted as one het step, which SchedulerParameters
    # can turn off.
    atf.require_config_parameter_excludes("SchedulerParameters", "disable_hetero_steps")
    atf.require_version(
        (26, 5, 6),
        "sbin/slurmd",
        reason="Issue 50940: stepd find_job_array_rec added in 26.05.6",
    )
    atf.require_slurm_running()


def test_het_step_on_array_task_launches_under_stepmgr():
    """A het step attached to an array task runs, and the stepmgr survives it.

    --jobid is named on both components deliberately. heterogeneous_jobs.html
    does not list it among the options propagated to later components, so the
    test does not rely on it carrying over."""

    # Every array task but the last to start runs under its own job id, so a
    # lookup by the array's id alone would not find it. Two nodes fit only one
    # task, so task 0 exits to let task 1 start while task 2 is still pending.
    script = '[ "$SLURM_ARRAY_TASK_ID" = 0 ] || sleep infinity'
    job_id = atf.submit_job_sbatch(
        f"--array=0-2 -N2 -n2 -t5 --wrap '{script}'", fatal=True
    )
    assert job_id != 0, "sbatch should submit the array job"

    # A task only gets its own record once it is scheduled
    task_job_id = 0
    for t in atf.timer(fatal=True):
        task_job_id = atf.get_job_id_from_array_task(job_id, 1)
        if task_job_id != 0:
            break
    atf.wait_for_job_state(task_job_id, "RUNNING", fatal=True)
    assert (
        task_job_id != job_id
    ), f"array task {job_id}_1 should run under its own job id"

    target = f"--jobid={job_id}_1"
    component = f"{target} -N1 -n1 printenv SLURM_JOB_ID"
    result = atf.run_command(f"srun {component} : {component}", quiet=True)
    assert result["exit_code"] == 0, (
        f"the het step on array task {job_id}_1 should run, got exit "
        f"{result['exit_code']}: {result['stderr']!r}"
    )
    assert result["stdout"].split() == [str(task_job_id)] * 2, (
        f"both components should run in job {task_job_id}, got: "
        f"{result['stdout']!r}"
    )

    # The step manager served that create rather than dying on it: it is still
    # there to serve another one.
    result = atf.run_command(f"srun {target} -N1 -n1 true", quiet=True)
    assert result["exit_code"] == 0, (
        f"the stepmgr for array task {job_id}_1 should still serve step "
        f"creates, got exit {result['exit_code']}: {result['stderr']!r}"
    )
