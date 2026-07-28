############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test PMIx abort propagation across hetjob components."""

import errno
import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_lmod()
    atf.module_load("openmpi")
    atf.require_accounting()
    atf.require_config_parameter("MpiDefault", "pmix")
    atf.require_config_parameter("KillWait", "5")
    # Hetjobs are only started by backfill; don't wait for its full cycle
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_nodes(2, [("CPUs", 2)])
    atf.require_slurm_running()


@pytest.mark.skipif(
    atf.get_version("sbin/slurmd") < (26, 5, 4),
    reason="Issue 51060: PMIx hetjob abort propagation fixed in 26.05.4",
)
@pytest.mark.parametrize("mpi_program", ["mpi_signal_test"], indirect=True)
@pytest.mark.parametrize(
    "abort_arg,expected_status,expected_state",
    [
        ("abort", 42, "CANCELLED"),
        ("abort0", 0, "COMPLETED"),
    ],
)
def test_hetjob_abort_propagation(
    mpi_program, abort_arg, expected_status, expected_state
):
    """Issue 51060: MPI_Abort() in one het component terminates all of them.

    Launches a two-component hetjob where global rank 0 (in component 0)
    calls MPI_Abort() while every other rank (including all ranks of
    component 1) blocks forever in pause(). The abort must terminate the
    steps of *all* components, otherwise srun never returns:
      - abort  (status 42): srun must exit 42.
      - abort0 (status 0):  KILL_NO_SIG_FAIL must survive the propagation
        to the other components, so the whole run must report success
        (exit code 0) exactly as it does for a non-hetjob.
    """
    # OpenMPI's sm BTL names its /dev/shm segments after the hostname, which
    # collides under multi-slurmd, so force TCP for the MPI transport
    result = atf.run_command(
        f"srun -t2 --mpi=pmix -n2 {mpi_program} {abort_arg}"
        f" : -t2 -n2 {mpi_program} {abort_arg}",
        env_vars="OMPI_MCA_btl=self,tcp",
        xfail=expected_status != 0,
        # Bound a possible hang: not propagating the abort blocks srun forever
        timeout=90,
    )

    # Checked before the timeout so that a job that never ran is not reported
    # as a propagation failure
    assert (
        f"rank0_calling_abort_{expected_status}" in result["stdout"]
    ), f"rank 0 never reached MPI_Abort(). stdout:\n{result['stdout']}"

    assert result["exit_code"] != errno.ETIMEDOUT, (
        "srun hung: the abort in component 0 did not terminate the tasks"
        f" of the other het components. stderr:\n{result['stderr']}"
    )

    # Stepd emits the message once per node, so count it per owning job id:
    # two distinct ones prove the SIG_TERM_KILL reached the step of every
    # component, rather than srun returning for some other reason
    killed_job_ids = set(
        re.findall(
            r"STEP (\d+)\.\d+(?:\+\d+)? ON \S+ CANCELLED AT \S+ DUE TO TASK FAILURE",
            result["stderr"],
        )
    )
    assert len(killed_job_ids) == 2, (
        "the abort must terminate the step of every het component, but only"
        f" {sorted(killed_job_ids)} got it. stderr:\n{result['stderr']}"
    )

    assert result["exit_code"] == expected_status, (
        f"MPI_Abort({expected_status}) in a hetjob must report the abort"
        " status as the srun exit code, exactly as it does for a non-hetjob"
        f" (KILL_NO_SIG_FAIL lost?). stderr:\n{result['stderr']}"
    )

    # srun's exit code does not prove KILL_NO_SIG_FAIL survived all the way
    # into what the controller hands slurmdbd, which is what users see later.
    # sacct reports het steps as <leader>+<component>.<step>, so collect them
    # by job id rather than looking each one up as <job_id>.0
    for _ in atf.timer(fatal=True):
        step_states = {}
        for job_id in killed_job_ids:
            for row in atf.run_command_output(
                f"sacct -j {job_id} --noheader -P -o JobID,State",
                fatal=True,
                quiet=True,
            ).splitlines():
                step_id, _, state = row.partition("|")
                if step_id.endswith(".0"):
                    step_states[step_id] = state
        # slurmdbd holds the step from launch time, so waiting for the rows to
        # exist would assert against a state that is not the final one yet
        if len(step_states) == 2 and not any(
            state.startswith(("PENDING", "RUNNING")) for state in step_states.values()
        ):
            break

    for step_id, state in sorted(step_states.items()):
        assert state.startswith(expected_state), (
            f"step {step_id} of the hetjob must be accounted as"
            f" {expected_state} after MPI_Abort({expected_status}),"
            f" got State={state!r}"
        )


def test_hetjob_kill_on_bad_exit():
    """Test --kill-on-bad-exit kills the tasks of every hetjob component.

    --kill-on-bad-exit is propagated to the subsequent components of a
    hetjob, so a task exiting non-zero in one component must terminate the
    tasks of all of them instead of leaving them running.

    srun signals the step of each component itself, so this is a contract
    test rather than a regression test of the hetjob signal propagation
    that slurmctld does for a step kill aimed at the job leader.
    """
    marker = "SHOULD_NOT_BE_HERE"
    failing_script = "hetjob_bad_exit.sh"
    surviving_script = "hetjob_survivor.sh"
    # The sleep is the detection window, not a keep-alive: a task that escapes
    # the kill has to outlive it to print the marker, and has to print it well
    # before the command timeout below
    atf.make_bash_script(
        failing_script,
        f"""
if [ "$SLURM_PROCID" = "0" ]; then
    exit 2
fi
sleep 60
echo "{marker}"
        """,
    )
    atf.make_bash_script(
        surviving_script,
        f"""
sleep 60
echo "{marker}"
        """,
    )

    result = atf.run_command(
        f"srun -t2 --kill-on-bad-exit -n2 ./{failing_script}"
        f" : -t2 -n2 ./{surviving_script}",
        xfail=True,
        # Bound a possible hang: surviving tasks sleep until the time limit
        timeout=90,
    )

    assert result["exit_code"] != errno.ETIMEDOUT, (
        "srun hung: the task failure in component 0 did not terminate the"
        f" tasks of the other het components. stderr:\n{result['stderr']}"
    )

    # Absent this, a step that never launched also leaves the marker unprinted
    assert "task 0: Exited with exit code 2" in result["stderr"], (
        "rank 0 of component 0 did not exit non-zero, so --kill-on-bad-exit"
        f" was never exercised. stderr:\n{result['stderr']}"
    )

    assert marker not in result["stdout"], (
        "--kill-on-bad-exit must terminate the tasks of every het component."
        f" stdout:\n{result['stdout']}"
    )
