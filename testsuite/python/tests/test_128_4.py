############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test hetjob REQUEUE and CANCEL preemption under a non-SUSPEND PreemptMode."""

import pytest

import atf

pytestmark = pytest.mark.slow

# The bare REQUEUE and CANCEL rows test longstanding behavior and must pass on
# every supported version. Everything that depends on gang scheduling carries
# this gate instead, on the row or on the test, so the skip is visible at
# collection time.
gang_gate = pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Ticket 23654: hetjob preemption under gang fixed in 26.11",
)

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


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU_Memory")
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_config_parameter("PreemptType", "preempt/partition_prio")
    # partition_prio requires a non-OFF PreemptMode, or slurmctld refuses the
    # config. Set a valid default so Slurm starts; each test then overrides it
    # with the mode under test via set_config_parameter().
    atf.require_config_parameter("PreemptMode", "REQUEUE")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    # A hetjob needs the backfill scheduler to place each component on a
    # different node to start, so require two nodes
    atf.require_nodes(2, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    # A REQUEUE-mode preemption can only requeue a requeue-able job; otherwise
    # it falls back to cancel. Force requeue-ability so the resolved mode, not
    # the cluster's JobRequeue default, decides the outcome.
    atf.require_config_parameter("JobRequeue", "1")
    atf.require_config_parameter(
        "PartitionName",
        {
            "lowprio": {"Nodes": "ALL", "PriorityTier": "1"},
            "highprio": {"Nodes": "ALL", "PriorityTier": "2"},
            # OverSubscribe=FORCE lets a job in this partition share cores, so
            # a hetjob preemptor submitted here is not turned away simply for
            # being unable to share. It must still take the victim's cores by
            # preempting rather than by sharing them. FORCE:1 allows a single
            # row, so exercising that does not also introduce real
            # oversubscription; test_128_7 covers FORCE:2.
            "highprio_os": {
                "Nodes": "ALL",
                "PriorityTier": "2",
                "OverSubscribe": "FORCE:1",
            },
            # A low-priority partition whose own PreemptMode is SUSPEND, so a
            # victim here resolves to SUSPEND even when the cluster mode is
            # not. Used to check the per-victim rule rather than the
            # cluster-wide setting.
            "low_suspend": {
                "Nodes": "ALL",
                "PriorityTier": "1",
                "PreemptMode": "SUSPEND",
            },
        },
    )
    atf.require_slurm_running()


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


def _submit_hetjob(name, partition, time_limit=5):
    """Submit a two component hetjob, one component per node.

    IN name       - file name for the generated batch script
    IN partition  - partition for both components
    IN time_limit - job time limit in minutes
    RET (leader job id, list of component job ids)
    """
    atf.make_bash_script(
        name,
        f"""
#SBATCH -N1 -c2 -p {partition} -t{time_limit} -o /dev/null
#SBATCH hetjob
#SBATCH -N1 -c2 -p {partition} -t{time_limit}
sleep infinity
""",
    )
    het_id = atf.submit_job_sbatch(name, fatal=True)
    components = atf.range_to_list(
        atf.get_job_parameter(het_id, "HetJobIdSet", fatal=True)
    )
    return het_id, components


@pytest.mark.parametrize(
    "preempt_mode,preempted_state",
    [
        ("REQUEUE", "PENDING"),
        ("CANCEL", "PREEMPTED"),
        pytest.param("REQUEUE,GANG", "PENDING", marks=gang_gate),
        pytest.param("CANCEL,GANG", "PREEMPTED", marks=gang_gate),
    ],
)
def test_hetjob_victim_is_preempted_by_higher_priority_partition(
    preempt_mode, preempted_state
):
    """A running hetjob IS preempted under REQUEUE and CANCEL.

    The exemption for heterogeneous jobs is specific to SUSPEND (gang)
    preemption. Under REQUEUE or CANCEL a hetjob has no special protection: a
    higher-priority-partition job that needs its resources must preempt it,
    requeuing (PENDING) or cancelling (PREEMPTED) every component. The GANG
    variants confirm the hetjob is not wrongly exempted just because gang
    scheduling is active, only because the resolved mode is SUSPEND.
    """

    atf.set_config_parameter("PreemptMode", preempt_mode, restart=True)

    # Heterogeneous job fills the low-priority partition (one component per node).
    hetjob_id, hetjob_components = _submit_hetjob("hetjob_victim.in", "lowprio")
    for component_id in hetjob_components:
        assert atf.wait_for_job_state(
            component_id, "RUNNING"
        ), f"Heterogeneous job component ({component_id}) never started running"

    # A higher-priority-partition job requesting both whole nodes -- the
    # resources currently held by the hetjob.
    preemptor_id = atf.submit_job_sbatch(
        '-N2 --ntasks-per-node=1 -c2 -p highprio -t5 -o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )

    # The preemptor MUST start by preempting the hetjob.
    assert atf.wait_for_job_state(
        preemptor_id, "RUNNING"
    ), f"Preemptor ({preemptor_id}) never started -- the hetjob was not preempted"

    # Every component of the hetjob must be preempted (requeued or cancelled).
    for component_id in hetjob_components:
        assert atf.wait_for_job_state(component_id, preempted_state), (
            f"Component ({component_id}) is "
            f"{atf.get_job_parameter(component_id, 'JobState')}, expected "
            f"{preempted_state}"
        )


@pytest.mark.parametrize(
    "preempt_mode,preempted_state",
    [
        ("REQUEUE", "PENDING"),
        ("CANCEL", "PREEMPTED"),
        pytest.param("REQUEUE,GANG", "PENDING", marks=gang_gate),
        pytest.param("CANCEL,GANG", "PREEMPTED", marks=gang_gate),
    ],
)
def test_hetjob_preemptor_preempts_lower_priority_job(preempt_mode, preempted_state):
    """A hetjob CAN preempt a lower-priority job under REQUEUE and CANCEL.

    The exemption is only for SUSPEND preemption. A hetjob submitted to a
    higher-priority partition must be able to requeue (PENDING) or cancel
    (PREEMPTED) a lower-priority job holding the resources it needs, and then
    start every component.

    The GANG rows are the ones that matter here: they show the SUSPEND
    exemption does not leak into the other modes merely because gang
    scheduling is enabled. Neither partition oversubscribes, so the hetjob has
    to preempt to start at all;
    test_hetjob_preemptor_preempts_from_oversubscribed_partition covers a
    partition that could otherwise let it share.
    """

    atf.set_config_parameter("PreemptMode", preempt_mode, restart=True)

    # A normal low-priority job fills both nodes (one task per node).
    victim_id = atf.submit_job_sbatch(
        '-N2 --ntasks-per-node=1 -c2 -p lowprio -t5 -o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        victim_id, "RUNNING"
    ), f"Low-priority victim ({victim_id}) never started running"

    # A higher-priority-partition hetjob requesting both whole nodes -- the
    # resources the victim is currently holding.
    hetjob_id, hetjob_components = _submit_hetjob("hetjob_preemptor.in", "highprio")

    # The victim must be preempted (requeued or cancelled) to free its nodes.
    assert atf.wait_for_job_state(victim_id, preempted_state), (
        f"Low-priority victim ({victim_id}) is "
        f"{atf.get_job_parameter(victim_id, 'JobState')}, expected "
        f"{preempted_state} by the hetjob"
    )

    # Every component of the hetjob preemptor must then start running.
    for component_id in hetjob_components:
        assert atf.wait_for_job_state(
            component_id, "RUNNING"
        ), f"Heterogeneous preemptor component ({component_id}) never started running"


@gang_gate
@pytest.mark.parametrize(
    "preempt_mode,preempted_state",
    [
        ("REQUEUE,GANG", "PENDING"),
        ("CANCEL,GANG", "PREEMPTED"),
    ],
)
def test_hetjob_preemptor_preempts_from_oversubscribed_partition(
    preempt_mode, preempted_state
):
    """A hetjob preemptor on an oversubscribed partition preempts, not shares.

    A heterogeneous job is excluded from oversubscription while GANG is
    enabled, so even from an OverSubscribe=FORCE partition it must take the
    victim's cores by preempting it rather than by quietly sharing them
    (slurm.conf(5), PreemptMode).

    Only GANG rows appear here. Without gang the partition oversubscribes
    exactly as configured and a hetjob may share, which test_128_7 covers.
    """

    atf.set_config_parameter("PreemptMode", preempt_mode, restart=True)

    # A normal low-priority job fills both nodes (one task per node).
    victim_id = atf.submit_job_sbatch(
        '-N2 --ntasks-per-node=1 -c2 -p lowprio -t5 -o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        victim_id, "RUNNING"
    ), f"Low-priority victim ({victim_id}) never started running"

    # A hetjob in the oversubscribed higher-priority partition requesting both
    # whole nodes -- the resources the victim is currently holding.
    hetjob_id, hetjob_components = _submit_hetjob(
        "hetjob_oversub_preemptor.in", "highprio_os"
    )

    # The victim must be preempted, not shared with.
    assert atf.wait_for_job_state(victim_id, preempted_state), (
        f"Low-priority victim ({victim_id}) is "
        f"{atf.get_job_parameter(victim_id, 'JobState')}, expected "
        f"{preempted_state}; the hetjob shared its cores instead of preempting"
    )

    # Every component of the hetjob preemptor must then start running.
    for component_id in hetjob_components:
        assert atf.wait_for_job_state(
            component_id, "RUNNING"
        ), f"Heterogeneous preemptor component ({component_id}) never started running"


@gang_gate
def test_hetjob_does_not_preempt_victim_resolving_to_suspend():
    """A hetjob never preempts a victim whose resolved PreemptMode is SUSPEND.

    The rule is per victim, not per cluster: "A heterogeneous job is never
    suspended as a preemptee, and it will never preempt a job whose resolved
    PreemptMode is SUSPEND" (slurm.conf(5), PreemptMode). The cluster mode
    here is REQUEUE,GANG, and only the victim's partition resolves to SUSPEND,
    so the hetjob must leave it alone and stay pending.

    test_hetjob_preemptor_preempts_lower_priority_job runs the same hetjob at
    the same cluster mode against a REQUEUE victim and shows it does preempt,
    which is what makes this a per-victim result rather than "a hetjob cannot
    preempt anything under gang".
    """

    atf.set_config_parameter("PreemptMode", "REQUEUE,GANG", restart=True)

    # A normal job fills both nodes from the partition whose PreemptMode is
    # SUSPEND, so the victim resolves to SUSPEND while the cluster is REQUEUE.
    victim_id = atf.submit_job_sbatch(
        "-N2 --ntasks-per-node=1 -c2 -p low_suspend -t5 -o /dev/null "
        '--wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        victim_id, "RUNNING"
    ), f"SUSPEND-mode victim ({victim_id}) never started running"

    # A higher-priority-partition hetjob requesting both whole nodes.
    hetjob_id, hetjob_components = _submit_hetjob(
        "hetjob_suspend_victim.in", "highprio"
    )

    # The hetjob must NOT start: it may not preempt a SUSPEND-resolved victim.
    # With bf_interval=1 the scheduler re-evaluates every second, so a broken
    # exemption would preempt well inside NOT_RUNNING_TIMEOUT.
    assert not atf.wait_for_job_state(
        hetjob_id, "RUNNING", timeout=NOT_RUNNING_TIMEOUT, xfail=True
    ), (
        f"Heterogeneous preemptor ({hetjob_id}) started; it must not preempt a "
        "victim whose resolved PreemptMode is SUSPEND"
    )
    _assert_blocked(hetjob_id, "Heterogeneous preemptor")
    for component_id in hetjob_components:
        _assert_blocked(component_id, "Heterogeneous preemptor component")

    # The victim must be untouched: never suspended, never requeued.
    assert (
        atf.get_job_parameter(victim_id, "JobState") == "RUNNING"
    ), f"SUSPEND-mode victim ({victim_id}) should still be running"
