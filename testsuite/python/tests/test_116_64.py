############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test srun forced termination of a hetjob."""

import os
import re
import signal

import pexpect
import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Hetjobs are only started by backfill; don't wait for its full cycle
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_nodes(2, [("CPUs", 2)])
    atf.require_slurm_running()


@pytest.fixture
def hetjob_with_waiting_tasks():
    """Spawn a 2 component hetjob whose tasks ignore SIGINT and wait forever.

    Yields the pexpect child, once the task of every component has started,
    and reaps it afterwards even if the test fails midway.
    """
    file_in = "hetjob_int_prog"
    atf.make_bash_script(
        file_in,
        """trap "" INT
echo WAITING
sleep infinity""",
    )

    child = pexpect.spawn(
        f"srun -N1 -n1 --unbuffered ./{file_in} : -N1 -n1 --unbuffered ./{file_in}",
        encoding="utf-8",
    )
    for _ in range(2):
        assert (
            child.expect(
                ["WAITING", pexpect.EOF, pexpect.TIMEOUT],
                timeout=atf.default_command_timeout,
            )
            == 0
        ), (
            "the task of every hetjob component must start before the"
            f" interrupts. Output:\n{child.before}"
        )

    yield child

    child.close()


def test_hetjob_force_terminate(hetjob_with_waiting_tasks):
    """Test two interrupts force terminate the tasks of every hetjob component.

    srun.1 documents that a second interrupt within one second sends SIGINT
    to all tasks and enters a termination sequence for all spawned tasks.
    For a hetjob that has to reach the step of every component, otherwise
    srun waits forever on the components it never signaled.

    This covers the first stage of job_force_termination(), which forwards
    the signal to the tasks directly.
    """
    child = hetjob_with_waiting_tasks

    # Only an interrupt arriving before the deadline that the previous one
    # armed escalates, so wait for srun to announce it rather than racing it
    os.kill(child.pid, signal.SIGINT)
    assert (
        child.expect(
            ["one more within 1 sec to abort", pexpect.EOF, pexpect.TIMEOUT],
            timeout=atf.default_command_timeout,
        )
        == 0
    ), f"srun did not arm the interrupt deadline. Output:\n{child.before}"
    os.kill(child.pid, signal.SIGINT)

    assert (
        child.expect(
            [pexpect.EOF, pexpect.TIMEOUT], timeout=atf.default_command_timeout
        )
        == 0
    ), (
        "srun did not exit after two interrupts, so the step of some hetjob"
        f" component was never signaled. Output:\n{child.before}"
    )

    cancelled_job_ids = set(
        re.findall(
            r"STEP (\d+)\.\d+(?:\+\d+)? ON \S+ CANCELLED AT \S+ DUE to SIGNAL",
            child.before,
        )
    )
    assert len(cancelled_job_ids) == 2, (
        "the termination sequence must reach the step of every hetjob"
        f" component, but only {sorted(cancelled_job_ids)} got it."
        f" Output:\n{child.before}"
    )


@pytest.mark.xfail(
    atf.get_version("bin/srun") < (26, 5, 4),
    reason="Issue 51060: job_force_termination() only asked slurmctld to"
    " terminate the step of the first hetjob component",
)
def test_hetjob_force_terminate_through_slurmctld(hetjob_with_waiting_tasks):
    """Test the slurmctld stage of a forced termination reaches every component.

    job_force_termination() forwards SIGKILL to the tasks first, and only
    asks slurmctld to terminate the steps on a later call. A third interrupt
    reaches that stage, because the escalating interrupt does not re-arm the
    one second deadline that the first one armed.
    """
    child = hetjob_with_waiting_tasks

    os.kill(child.pid, signal.SIGINT)
    assert (
        child.expect(
            ["one more within 1 sec to abort", pexpect.EOF, pexpect.TIMEOUT],
            timeout=atf.default_command_timeout,
        )
        == 0
    ), f"srun did not arm the interrupt deadline. Output:\n{child.before}"

    # Identical signals are not queued, so the escalating interrupt has to be
    # seen handled before the next one, which still has to arrive within the
    # window the first one armed. Signal in process to stay inside it.
    os.kill(child.pid, signal.SIGINT)
    assert (
        child.expect(
            ["forcing job termination", pexpect.EOF, pexpect.TIMEOUT],
            timeout=atf.default_command_timeout,
        )
        == 0
    ), f"srun did not force the termination. Output:\n{child.before}"
    os.kill(child.pid, signal.SIGINT)

    assert (
        child.expect(
            [pexpect.EOF, pexpect.TIMEOUT], timeout=atf.default_command_timeout
        )
        == 0
    ), f"srun did not exit after the interrupts. Output:\n{child.before}"

    terminated_job_ids = set(
        re.findall(r"Terminating StepId=(\d+)\.\d+(?:\+\d+)?", child.before)
    )
    assert len(terminated_job_ids) == 2, (
        "the termination requested through slurmctld must cover the step of"
        f" every hetjob component, but only {sorted(terminated_job_ids)} got"
        f" it. Output:\n{child.before}"
    )
