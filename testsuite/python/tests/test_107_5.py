############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scancel of a het job step."""

import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Het jobs are only started by backfill; don't wait for its full cycle
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    # --stepmgr is silently dropped without it, see PrologFlags in slurm.conf(5)
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_nodes(2, [("CPUs", 2)])
    atf.require_slurm_running()


def submit_hetjob_with_step(het_group, file_out, msg_done, stepmgr=False):
    """Submit a 2 component het job running a step on the given het groups.

    The batch script echoes msg_done once the step returns, so that the file
    contents tell the step being canceled apart from the whole job dying.

    Returns a (leader_job_id, component_job_ids) tuple.
    """
    suffix = "_stepmgr" if stepmgr else ""
    script = f"hetjob_step_{het_group.replace(',', '_')}{suffix}.sh"
    leader_opts = "-n1" + (" --stepmgr" if stepmgr else "")
    atf.make_bash_script(
        script,
        f"""
#SBATCH {leader_opts}
#SBATCH hetjob
#SBATCH -n1

srun --het-group={het_group} sleep infinity
echo "{msg_done}"
sleep infinity
        """,
    )
    leader_job_id = atf.submit_job_sbatch(
        f"-t3 --output={file_out} {script}", fatal=True
    )
    atf.wait_for_job_state(leader_job_id, "RUNNING", fatal=True)

    component_job_ids = atf.range_to_list(
        atf.get_job_parameter(leader_job_id, "HetJobIdSet", fatal=True)
    )
    assert (
        len(component_job_ids) == 2
    ), f"the het job must have 2 components, got {component_job_ids}"

    return leader_job_id, component_job_ids


@pytest.mark.xfail(
    (25, 11) <= atf.get_version("sbin/slurmctld") < (26, 5, 4),
    reason="Issue 51060: since 4d70133431 (25.11) kill_job_step() propagated a"
    " whole-job cancel to het components instead of a step kill",
)
def test_cancel_hetjob_step_keeps_allocation():
    """Issue 51060: scancel of a het job step must not cancel the entire job.

    Canceling <leader_job_id>.<step_id> propagates the SIGKILL to the
    matching step of every het job component, but must only kill the steps:
    the component jobs (the allocations) have to keep running. A regression
    turned the propagation into a whole-job cancel of every component.
    """
    file_out = "hetjob_step.out"
    msg_done = "step_terminated"

    leader_job_id, component_job_ids = submit_hetjob_with_step(
        "0,1", file_out, msg_done
    )

    for job_id in component_job_ids:
        atf.wait_for_step(job_id, 0, fatal=True)

    atf.run_command(f"scancel {leader_job_id}.0", fatal=True)

    # srun returning (and the script moving on) proves both steps died
    atf.assert_file_contents(
        file_out,
        msg_done,
        contains=True,
        message=f"srun did not return after scancel {leader_job_id}.0: either the"
        " step of some het component was not canceled, or the whole component"
        " jobs were killed",
    )

    # Het step ids may be displayed with a +<comp> suffix on some versions
    for _ in atf.timer(fatal=True):
        if all(
            not any(
                re.fullmatch(rf"{job_id}\.0(\+\d+)?", step_id)
                for step_id in atf.get_steps(job_id, quiet=True)
            )
            for job_id in component_job_ids
        ):
            break

    for job_id in component_job_ids:
        assert (
            atf.get_job_parameter(job_id, "JobState") == "RUNNING"
        ), f"component job {job_id} must survive the cancellation of its step"


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (26, 5, 4),
    reason="Issue 51060: before 25.11 a component without the step made the"
    " whole request report an invalid job id, and since 4d70133431 the"
    " propagation canceled the component jobs instead of their steps",
)
@pytest.mark.parametrize("het_group", ["0", "1"])
def test_cancel_hetjob_step_of_one_component(het_group):
    """Issue 51060: canceling a step only some het job components have.

    A step does not have to exist in every component, so canceling it
    through the leader must cancel it where it exists and report no error
    for the components that do not have it. The leader is one of the
    components that may not have it.
    """
    file_out = f"hetjob_step_one_{het_group}.out"
    msg_done = "step_terminated"

    leader_job_id, component_job_ids = submit_hetjob_with_step(
        het_group, file_out, msg_done
    )

    atf.wait_for_step(component_job_ids[int(het_group)], 0, fatal=True)

    result = atf.run_command(f"scancel {leader_job_id}.0", fatal=True)

    assert "Invalid job id" not in result["stderr"], (
        "canceling a step that only some het job components have must not be"
        f" an error. stderr:\n{result['stderr']}"
    )

    atf.assert_file_contents(
        file_out,
        msg_done,
        contains=True,
        message=f"srun did not return after scancel {leader_job_id}.0, so the"
        " step of the component owning it was not canceled",
    )

    for job_id in component_job_ids:
        assert (
            atf.get_job_parameter(job_id, "JobState") == "RUNNING"
        ), f"component job {job_id} must survive the cancellation of the step"


@pytest.mark.xfail(
    (25, 11) <= atf.get_version("sbin/slurmctld") < (26, 5, 4),
    reason="Issue 51060: since 4d70133431 (25.11) the propagation to the het"
    " components canceled the component jobs for a step id that none of them"
    " has",
)
def test_cancel_hetjob_step_of_no_component():
    """Issue 51060: canceling a step id that no het job component has.

    Canceling a step no component has must leave the existing steps running.
    Without stepmgr that request is reported as an invalid job id; with
    stepmgr the controller forwards the cancel to the components' stepmgr and
    cannot tell synchronously that no component has the step.
    """
    file_out = "hetjob_step_none.out"
    msg_done = "step_terminated"

    leader_job_id, component_job_ids = submit_hetjob_with_step(
        "0,1", file_out, msg_done
    )

    # Keyed off the job, not SlurmctldParameters: before 26.11 a het job does
    # not get a stepmgr even where enable_stepmgr is configured.
    jobs = atf.get_jobs(leader_job_id, fatal=True)
    stepmgr_enabled = jobs[leader_job_id].get("StepMgrEnabled") == "Yes"

    for job_id in component_job_ids:
        atf.wait_for_step(job_id, 0, fatal=True)

    result = atf.run_command(
        f"scancel {leader_job_id}.7", fatal=True, xfail=not stepmgr_enabled
    )

    if stepmgr_enabled:
        assert result["exit_code"] == 0, (
            "with stepmgr the cancel is forwarded to the components' stepmgr"
            " and must be accepted, got exit code"
            f" {result['exit_code']}. stderr:\n{result['stderr']}"
        )
    else:
        assert "Invalid job id" in result["stderr"], (
            "canceling a step id that no het job component has must report an"
            f" invalid job id. stderr:\n{result['stderr']}"
        )

    for job_id in component_job_ids:
        assert any(
            re.fullmatch(rf"{job_id}\.0(\+\d+)?", step_id)
            for step_id in atf.get_steps(job_id)
        ), (
            f"step {job_id}.0 must keep running after canceling a step id that"
            " no het job component has"
        )


# The client tools upgrade last, so gating on srun >= 26.11 already implies
# slurmd and slurmctld are >= 26.11.
@pytest.mark.skipif(
    atf.get_version("bin/srun") < (26, 11),
    reason="Issue 50976: het jobs only run with stepmgr since 26.11",
)
def test_cancel_hetjob_step_with_stepmgr():
    """Canceling a het job step of a --stepmgr het job keeps the allocations.

    With stepmgr the controller forwards the cancel to the components' step
    managers rather than killing the steps itself, so the propagation is a
    different path from the controller-managed one.
    """
    file_out = "hetjob_step_stepmgr.out"
    msg_done = "step_terminated"

    leader_job_id, component_job_ids = submit_hetjob_with_step(
        "0,1", file_out, msg_done, stepmgr=True
    )

    jobs = atf.get_jobs(leader_job_id, fatal=True)
    assert jobs[leader_job_id].get("StepMgrEnabled") == "Yes", (
        "the het job must have a stepmgr for this test to exercise the stepmgr"
        " cancel path"
    )

    for job_id in component_job_ids:
        atf.wait_for_step(job_id, 0, fatal=True)

    atf.run_command(f"scancel {leader_job_id}.0", fatal=True)

    atf.assert_file_contents(
        file_out,
        msg_done,
        contains=True,
        message=f"srun did not return after scancel {leader_job_id}.0 on a"
        " stepmgr het job, so the step of some component was not canceled",
    )

    for job_id in component_job_ids:
        assert (
            atf.get_job_parameter(job_id, "JobState", fatal=True) == "RUNNING"
        ), f"component job {job_id} must survive the cancellation of its step"
