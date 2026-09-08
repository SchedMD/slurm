############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A job step's exit code is derived from the statuses of all its tasks.

_get_exit_code() in slurmstepd reduces a node's task statuses to the one
value the step reports. A task killed by a signal, or one that called
PMI_Abort(), sets the step's status outright; otherwise the step reports
the highest exit status.

The failing task is local task 0 and the succeeding one local task 1, so a
reduction that kept the last task's status would fail. -N1 -n2 puts both
tasks on one node, where the reduction happens. The last case spreads one
step over two nodes.

The step's accounting row is asserted rather than srun's exit status, which
srun computes on its own (src/srun/launch.c).
"""

import os
import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    # The step and job rows read below do not exist under nosteps or nojobs.
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "nosteps")
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "nojobs")
    # Two nodes for the cross-node case; the rest use one.
    atf.require_nodes(2, [("CPUs", 2)])
    atf.require_slurm_running()


def _submit_two_task_step(name, task0_exit, task1_exit, task1_cmd=None):
    """Run one step of two tasks on one node, and return the job id.

    Local task 0 exits task0_exit. Local task 1 exits task1_exit, unless
    task1_cmd is given, in which case it runs that instead - used to make a
    task die from a signal rather than exit.
    """
    task_script = os.path.abspath(f"task_{name}.sh")
    task1_body = task1_cmd if task1_cmd else f"exit {task1_exit}"
    atf.make_bash_script(
        task_script,
        f"""if [ "$SLURM_LOCALID" -eq 0 ]; then
    exit {task0_exit}
fi
{task1_body}
""",
    )

    batch_script = os.path.abspath(f"batch_{name}.sh")
    atf.make_bash_script(batch_script, f"srun -N1 -n2 {task_script}\n")

    job_id = atf.submit_job_sbatch(f"-N1 -n2 --no-requeue {batch_script}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    return job_id


def _step_exit_code(job_id):
    """Return the ExitCode accounting recorded for the job's first step.

    The step row exists from step START, carrying an ExitCode of 0:0 until
    the completion record lands, so the read has to wait for that record.
    Waiting on End does not do it: sacct substitutes the JOB's End into any
    step row whose own End is unset (src/sacct/options.c), so End reads as a
    real timestamp while the row is still the start row. Poll the step's
    State instead, and take ExitCode from the same read so the two cannot
    come from different rows.
    """
    exit_code = ""
    for _ in atf.timer(fatal=True):
        row = atf.run_command_output(
            f"sacct -j {job_id}.0 --noheader -P -o State,ExitCode", quiet=True
        ).strip()
        state, _, code = row.partition("|")
        words = state.split()
        if words and words[0] not in ("PENDING", "RUNNING"):
            exit_code = code
            break
    return exit_code


# Slurmd owns the reduction, so gate on its version rather than the client's.
requires_max_reduction = pytest.mark.skipif(
    atf.get_version("sbin/slurmd") < (26, 11),
    reason="Ticket 24195: a step reported the LAST task's status rather "
    "than the highest before 26.11",
)


@requires_max_reduction
def test_later_success_does_not_erase_earlier_failure():
    """Verify a task exiting 0 after a failed task does not hide the failure"""

    job_id = _submit_two_task_step("fail_then_pass", 3, 0)

    exit_code = _step_exit_code(job_id)
    assert exit_code == "3:0", (
        f"Step {job_id}.0 ran two tasks exiting 3 and 0, so it must report the "
        f"higher status 3:0; got {exit_code}. A value of 0:0 means the task "
        "reduction dropped the failing task."
    )


@requires_max_reduction
def test_highest_of_several_failures_wins():
    """Verify the highest status is reported when more than one task fails"""

    job_id = _submit_two_task_step("fail_then_fail_lower", 9, 3)

    exit_code = _step_exit_code(job_id)
    assert exit_code == "9:0", (
        f"Step {job_id}.0 ran tasks exiting 3 and 9, so it must report the "
        f"higher status 9:0; got {exit_code}."
    )


# Ungated: holds on every supported version, so it guards the rule everywhere.
def test_a_signalled_task_wins_over_a_higher_exit_status():
    """Verify a signalled task's status is reported over a higher exit status

    job_exit_code.shtml states that a task killed by a signal, or one that
    called PMI_Abort(), has its status reported for the whole step, and that
    the highest-exit-status rule applies only otherwise. Task 0 exits 3,
    which is numerically higher than the signalled task's raw status of 9, so
    a plain maximum would report 3:0 instead.
    """

    job_id = _submit_two_task_step(
        "exit_then_signal", 3, None, task1_cmd="kill -KILL $$"
    )

    exit_code = _step_exit_code(job_id)
    assert exit_code == "0:9", (
        f"Step {job_id}.0 ran a task exiting 3 and a task killed by SIGKILL, "
        f"so it must report the signal as 0:9; got {exit_code}. A value of "
        "3:0 means the signalled task lost a numeric comparison it should "
        "never have entered."
    )


@requires_max_reduction
def test_derived_exit_code_follows_the_step():
    """Verify the job's DerivedExitCode picks up the step's highest status

    DerivedExitCode is the maximum across a job's srun steps, so it reports
    the highest task status of any step. It is the surface an admin is most
    likely to notice, and EpilogSlurmctld receives it as
    SLURM_JOB_DERIVED_EC.
    """

    job_id = _submit_two_task_step("derived", 3, 0)

    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)

    derived = atf.run_command_output(
        f"sacct -j {job_id} -X --noheader -P -o DerivedExitCode", fatal=True
    ).strip()
    assert re.match(r"^3:", derived), (
        "DerivedExitCode should follow the step's highest task status (3), "
        f"got {derived}"
    )


# Ungated: holds on every supported version, so it guards the rule everywhere.
def test_a_cancelled_step_reports_the_signal():
    """Verify a step cancelled with scancel reports the signal that killed it

    job_exit_code.shtml states a task killed by a signal sets the step's
    status outright, and scancel of a step sends SIGKILL.
    """

    batch_script = os.path.abspath("batch_cancelled.sh")
    atf.make_bash_script(batch_script, "srun -N1 -n1 sleep infinity\nexit 0\n")
    job_id = atf.submit_job_sbatch(f"-N1 --no-requeue {batch_script}", fatal=True)
    atf.wait_for_step(job_id, 0, fatal=True)

    atf.run_command(f"scancel {job_id}.0", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    exit_code = _step_exit_code(job_id)
    assert exit_code == "0:9", (
        f"Step {job_id}.0 was cancelled with scancel, so it must report the "
        f"SIGKILL as 0:9; got {exit_code}."
    )


@requires_max_reduction
def test_highest_status_across_nodes():
    """Verify a step reports the highest task status across every node

    Each node reduces its own tasks, and the step then takes the highest of
    the nodes' results. The failing task is on the first node, so keeping
    the last node's result would report 0:0.
    """

    task_script = os.path.abspath("task_across_nodes.sh")
    atf.make_bash_script(
        task_script,
        """if [ "$SLURM_NODEID" -eq 0 ]; then
    exit 3
fi
exit 0
""",
    )
    batch_script = os.path.abspath("batch_across_nodes.sh")
    atf.make_bash_script(
        batch_script, f"srun -N2 -n2 --ntasks-per-node=1 {task_script}\n"
    )

    job_id = atf.submit_job_sbatch(f"-N2 -n2 --no-requeue {batch_script}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    exit_code = _step_exit_code(job_id)
    assert exit_code == "3:0", (
        f"Step {job_id}.0 ran a task exiting 3 on one node and 0 on the other, "
        f"so it must report 3:0; got {exit_code}."
    )
