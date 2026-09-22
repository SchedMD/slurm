############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that heterogeneous jobs are not SUSPEND-preempted."""

import pytest

import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Ticket 23654: HetJob SUSPEND exemption added in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU_Memory")
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_config_parameter("PreemptType", "preempt/partition_prio")
    atf.require_config_parameter("PreemptMode", "SUSPEND,GANG")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    # A hetjob needs the backfill scheduler to place each component on a
    # different node to start, so require two nodes
    atf.require_nodes(2, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    atf.require_config_parameter(
        "PartitionName",
        {
            "lowprio": {"Nodes": "ALL", "PriorityTier": "1"},
            "highprio": {"Nodes": "ALL", "PriorityTier": "2"},
        },
    )
    atf.require_slurm_running()


# Reasons that mean "another job holds the resources this job asked for". A job
# pending for anything else -- a limit, a hold, a drained node -- is pending for
# a reason unrelated to what these tests check.
BLOCKED_REASONS = ("Resources", "Priority")

# A negative wait has to burn its whole timeout, so bound it rather than taking
# the atf default of 45s. With bf_interval=1 and sched_interval=1 the scheduler
# re-evaluates every second, so a broken exemption shows up within a cycle or
# two; this leaves a wide margin and is the difference between a ~10s test and
# a ~50s one. Shortening it can only lower sensitivity, never cause a false
# failure.
NOT_RUNNING_TIMEOUT = 15


def _assert_blocked(job_id, what):
    """Assert job_id is pending because another job holds its resources.

    wait_for_job_state() also returns False for a terminal state, so the state
    is checked explicitly rather than inferred from a failed wait.
    IN job_id - the job that must be pending
    IN what   - how to name the job in the failure message
    """
    state = atf.get_job_parameter(job_id, "JobState")
    assert state == "PENDING", f"{what} ({job_id}) should be pending, it is {state}"

    reason = atf.get_job_parameter(job_id, "Reason")
    assert reason in BLOCKED_REASONS, (
        f"{what} ({job_id}) is pending for {reason}, not because another job "
        f"holds its resources"
    )


def test_hetjob_not_preempted_by_higher_priority_partition():
    """A running hetjob must not be preempted under PreemptMode=SUSPEND,GANG.

    Gang scheduling excludes heterogeneous jobs, so a running hetjob cannot be
    suspended to reclaim its resources. A higher-priority-partition job that
    requests those resources must stay pending, and the hetjob must keep running.
    """

    # Heterogeneous job fills the low-priority partition (one component per node).
    atf.make_bash_script(
        "hetjob.in",
        """
#SBATCH -N1 -c2 -p lowprio -t5 -o /dev/null
#SBATCH hetjob
#SBATCH -N1 -c2 -p lowprio -t5
sleep infinity
""",
    )
    hetjob_id = atf.submit_job_sbatch("hetjob.in", fatal=True)
    assert atf.wait_for_job_state(
        hetjob_id, "RUNNING"
    ), f"Heterogeneous job leader ({hetjob_id}) never started running"

    # Wait for every component so both nodes are provably held by the
    # hetjob before submitting the preemptor
    hetjob_components = atf.range_to_list(
        atf.get_job_parameter(hetjob_id, "HetJobIdSet", fatal=True)
    )
    for component_id in hetjob_components:
        assert atf.wait_for_job_state(
            component_id, "RUNNING"
        ), f"Heterogeneous job component ({component_id}) never started running"
    # A higher-priority-partition job requesting a whole node -- resources the
    # hetjob is currently holding.
    preemptor_id = atf.submit_job_sbatch(
        '-N1 -c2 -p highprio -t1 -o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )

    # The preemptor must NOT start: the hetjob cannot be gang-suspended, so the
    # preemptor stays pending instead of oversubscribing the node. With
    # bf_interval=1 the scheduler re-evaluates every second, so a broken
    # exemption would preempt well inside NOT_RUNNING_TIMEOUT.
    assert not atf.wait_for_job_state(
        preemptor_id, "RUNNING", timeout=NOT_RUNNING_TIMEOUT, xfail=True
    ), f"Preemptor ({preemptor_id}) started, the hetjob was oversubscribed instead of protected"

    _assert_blocked(preemptor_id, "Preemptor")

    # The hetjob must still be running (never suspended or preempted) -- check
    # every component, not just the leader.
    for component_id in hetjob_components:
        assert (
            atf.get_job_parameter(component_id, "JobState") == "RUNNING"
        ), f"Heterogeneous job component ({component_id}) should still be running"


def test_hetjob_does_not_preempt_lower_priority_partition():
    """A hetjob must not SUSPEND-preempt a lower-priority job under SUSPEND,GANG.

    Gang scheduling excludes heterogeneous jobs, so a hetjob cannot drive
    gang-suspend preemption. A hetjob submitted to a higher-priority partition
    must therefore stay pending behind the lower-priority job holding its
    resources, and that job must keep running (never suspended).
    """

    # A normal low-priority job fills both nodes (one task per node).
    victim_id = atf.submit_job_sbatch(
        '-N2 --ntasks-per-node=1 -c2 -p lowprio -t2 -o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        victim_id, "RUNNING"
    ), f"Low-priority victim ({victim_id}) never started running"

    # A higher-priority-partition hetjob requesting both whole nodes -- the
    # resources the victim is currently holding.
    atf.make_bash_script(
        "hetjob_preemptor.in",
        """
#SBATCH -N1 -c2 -p highprio -t1 -o /dev/null
#SBATCH hetjob
#SBATCH -N1 -c2 -p highprio -t1
sleep infinity
""",
    )
    hetjob_id = atf.submit_job_sbatch("hetjob_preemptor.in", fatal=True)

    # The hetjob must NOT start: it cannot gang-suspend the victim, so it stays
    # pending instead of preempting a job it is not allowed to preempt. With
    # bf_interval=1 the scheduler re-evaluates every second, so a broken
    # exemption would preempt well inside NOT_RUNNING_TIMEOUT.
    assert not atf.wait_for_job_state(
        hetjob_id, "RUNNING", timeout=NOT_RUNNING_TIMEOUT, xfail=True
    ), f"Heterogeneous preemptor ({hetjob_id}) started, it must not preempt the victim"

    _assert_blocked(hetjob_id, "Heterogeneous preemptor")

    # The victim must still be running (never suspended by the hetjob).
    assert (
        atf.get_job_parameter(victim_id, "JobState") == "RUNNING"
    ), f"Low-priority victim ({victim_id}) should still be running"


def test_normal_job_is_suspend_preempted():
    """Positive control: a non-hetjob IS SUSPEND-preempted under SUSPEND,GANG.

    This anchors the hetjob negative assertions above. It proves the cluster's
    SUSPEND,GANG preemption is actually live in this config, so a hetjob that
    stays RUNNING is a real exemption and not a dormant preemption path.
    """

    # A normal low-priority job fills both nodes (one task per node).
    victim_id = atf.submit_job_sbatch(
        '-N2 --ntasks-per-node=1 -c2 -p lowprio -t2 -o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        victim_id, "RUNNING"
    ), f"Low-priority victim ({victim_id}) never started running"

    # A higher-priority-partition job requesting a whole node forces the victim
    # to be gang-suspended to free it.
    preemptor_id = atf.submit_job_sbatch(
        '-N1 -c2 -p highprio -t1 -o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        preemptor_id, "RUNNING"
    ), f"Preemptor ({preemptor_id}) never started; SUSPEND preemption is not live"

    # The victim must be suspended, proving SUSPEND,GANG preemption is operative.
    assert atf.wait_for_job_state(
        victim_id, "SUSPENDED"
    ), f"Victim ({victim_id}) was not suspended; SUSPEND preemption is not live"


def test_hetjob_cores_stay_exempt_across_slurmctld_restart():
    """A hetjob's cores must stay exempt after slurmctld restarts.

    The exempt cores live only in slurmctld memory. On restart they are
    rebuilt from the recovered jobs rather than from the job start path, so a
    hetjob that was already running must still be protected afterwards. If the
    rebuild were skipped the preemptor below would be placed on the hetjob's
    cores, which gang scheduling could never then free.
    """

    atf.make_bash_script(
        "hetjob_restart.in",
        """
#SBATCH -N1 -c2 -p lowprio -t10 -o /dev/null
#SBATCH hetjob
#SBATCH -N1 -c2 -p lowprio -t10
sleep infinity
""",
    )
    hetjob_id = atf.submit_job_sbatch("hetjob_restart.in", fatal=True)
    hetjob_components = atf.range_to_list(
        atf.get_job_parameter(hetjob_id, "HetJobIdSet", fatal=True)
    )
    for component_id in hetjob_components:
        assert atf.wait_for_job_state(
            component_id, "RUNNING"
        ), f"Heterogeneous job component ({component_id}) never started running"

    # Restart without clearing state, so the hetjob is recovered and its cores
    # have to be re-exempted from the recovered job rather than at job start.
    atf.restart_slurmctld()

    for component_id in hetjob_components:
        assert atf.wait_for_job_state(
            component_id, "RUNNING"
        ), f"Heterogeneous job component ({component_id}) did not survive the restart"

    # A higher-priority-partition job requesting a whole node the hetjob holds.
    preemptor_id = atf.submit_job_sbatch(
        '-N1 -c2 -p highprio -t1 -o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )

    assert not atf.wait_for_job_state(
        preemptor_id, "RUNNING", timeout=NOT_RUNNING_TIMEOUT, xfail=True
    ), (
        f"Preemptor ({preemptor_id}) started after the restart; the hetjob's "
        "cores were not re-exempted on recovery"
    )
    _assert_blocked(preemptor_id, "Preemptor")
