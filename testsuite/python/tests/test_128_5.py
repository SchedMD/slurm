############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test hetjob preemption when PreemptMode resolves from mixed sources."""

from datetime import datetime

import pytest

import atf

pytestmark = pytest.mark.slow

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

# GraceTime, in seconds, for the two partitions used by the GraceTime test. The
# gap between them is what the test measures, so keep them far enough apart to
# tell apart with a comfortable tolerance.
GRACE_SHORT = 10
GRACE_LONG = 30


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Ticket 23654: hetjob PreemptMode resolution changed to "
        "REQUEUE then CANCEL, with SUSPEND treated as OFF, in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU_Memory")
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_config_parameter("PreemptType", "preempt/partition_prio")
    # GANG is required so a partition may override PreemptMode to SUSPEND.
    atf.require_config_parameter("PreemptMode", "SUSPEND,GANG")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    # A hetjob needs the backfill scheduler to place each component on a
    # different node to start, so require one node per component. Three, since
    # the three-component case below is what makes "the first component found"
    # and "applied to every component" distinguishable.
    atf.require_nodes(3, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    # A REQUEUE-mode preemption can only requeue a requeue-able job; otherwise
    # it falls back to cancel. Force requeue-ability so the resolved mode, not
    # the cluster's JobRequeue default, decides the outcome.
    atf.require_config_parameter("JobRequeue", "1")
    # Low-priority partitions, one per PreemptMode, so a hetjob can put each
    # component in a partition with a different mode. A high-priority partition
    # provides the preemptor.
    atf.require_config_parameter(
        "PartitionName",
        {
            "low_suspend": {
                "Nodes": "ALL",
                "PriorityTier": "1",
                "PreemptMode": "SUSPEND",
            },
            "low_requeue": {
                "Nodes": "ALL",
                "PriorityTier": "1",
                "PreemptMode": "REQUEUE",
            },
            "low_cancel": {
                "Nodes": "ALL",
                "PriorityTier": "1",
                "PreemptMode": "CANCEL",
            },
            # Two more REQUEUE partitions that differ only in GraceTime, so a
            # hetjob can span both and each component's own GraceTime can be
            # told apart. GraceTime is not used under SUSPEND, so these
            # resolve to REQUEUE.
            "low_grace_short": {
                "Nodes": "ALL",
                "PriorityTier": "1",
                "PreemptMode": "REQUEUE",
                "GraceTime": str(GRACE_SHORT),
            },
            "low_grace_long": {
                "Nodes": "ALL",
                "PriorityTier": "1",
                "PreemptMode": "REQUEUE",
                "GraceTime": str(GRACE_LONG),
            },
            "highprio": {"Nodes": "ALL", "PriorityTier": "2"},
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


def _submit_hetjob(name, partitions):
    """Submit a hetjob with one component per named partition, one per node.

    IN name       - file name for the generated batch script
    IN partitions - partition for each component, in order
    RET (leader job id, list of component job ids)
    """
    components = [
        f"#SBATCH -N1 -c2 -p {partition} -t5 -o /dev/null" for partition in partitions
    ]
    atf.make_bash_script(
        name, "\n#SBATCH hetjob\n".join(components) + "\nsleep infinity\n"
    )

    het_id = atf.submit_job_sbatch(name, fatal=True)
    return het_id, atf.range_to_list(
        atf.get_job_parameter(het_id, "HetJobIdSet", fatal=True)
    )


def _end_time(job_id):
    """Return a job's EndTime as a datetime, or None if it is not a timestamp.

    scontrol renders EndTime as "Unknown" or "None" for a job whose end time
    has been reset, which a component that has left its grace period and been
    requeued will show.
    """
    end_time = atf.get_job_parameter(job_id, "EndTime")
    if end_time in ("Unknown", "None"):
        return None
    return datetime.fromisoformat(end_time)


@pytest.mark.parametrize(
    "partitions,resolved_mode,preempted_state",
    [
        # A component resolving to SUSPEND is treated as OFF, so REQUEUE and
        # CANCEL both win over it whichever component carries them.
        (["low_suspend", "low_requeue"], "REQUEUE", "PENDING"),
        (["low_requeue", "low_suspend"], "REQUEUE", "PENDING"),
        (["low_suspend", "low_cancel"], "CANCEL", "PREEMPTED"),
        (["low_cancel", "low_suspend"], "CANCEL", "PREEMPTED"),
        # REQUEUE is searched for before CANCEL, so it wins from either
        # position. The reversed row is the one that tells this apart from
        # simply taking the first component that is not SUSPEND.
        (["low_requeue", "low_cancel"], "REQUEUE", "PENDING"),
        (["low_cancel", "low_requeue"], "REQUEUE", "PENDING"),
        # Every component resolving to SUSPEND means the whole job resolves to
        # OFF: it is exempt, so it keeps RUNNING and the preemptor waits.
        (["low_suspend", "low_suspend"], "SUSPEND", "RUNNING"),
        # Three components put the winning mode in the middle, where neither
        # "the first component found" nor "applied to every component" is
        # degenerate.
        (["low_cancel", "low_requeue", "low_suspend"], "REQUEUE", "PENDING"),
    ],
)
def test_hetjob_mixed_component_modes_resolve_requeue_then_cancel(
    partitions, resolved_mode, preempted_state
):
    """A hetjob with mixed-mode components resolves to one mode for all of them.

    The first component found with PreemptMode=REQUEUE sets the mode for every
    component; if there is none, the first found with CANCEL does. A component
    resolving to SUSPEND is treated as PreemptMode=OFF, and a job with no
    REQUEUE or CANCEL component is not preempted at all (slurm.conf(5),
    PreemptMode). Each pair of partitions is submitted in both orders, so the
    search order is what decides the outcome rather than component position.
    """

    hetjob_id, hetjob_components = _submit_hetjob("hetjob_mixed.in", partitions)
    for component_id in hetjob_components:
        assert atf.wait_for_job_state(
            component_id, "RUNNING"
        ), f"Heterogeneous job component ({component_id}) never started running"

    # A higher-priority job needing every node the hetjob holds forces it to be
    # preempted as a unit, using the single resolved mode.
    preemptor_id = atf.submit_job_sbatch(
        f"-N{len(partitions)} --ntasks-per-node=1 -c2 -p highprio -t5 "
        f'-o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )
    if resolved_mode == "SUSPEND":
        # SUSPEND resolves to OFF for a hetjob, so it is exempt: the preemptor
        # must stay pending. With bf_interval=1 a broken exemption would
        # preempt well inside NOT_RUNNING_TIMEOUT.
        assert not atf.wait_for_job_state(
            preemptor_id, "RUNNING", timeout=NOT_RUNNING_TIMEOUT, xfail=True
        ), f"Preemptor ({preemptor_id}) started; a SUSPEND-resolved hetjob must not be preempted"
        _assert_blocked(preemptor_id, "Preemptor")
    else:
        assert atf.wait_for_job_state(preemptor_id, "RUNNING"), (
            f"Preemptor ({preemptor_id}) never started, the hetjob should have "
            f"been preempted as {resolved_mode}"
        )

    # Every component must reach the state implied by the resolved mode
    # (RUNNING when SUSPEND-exempt), including components whose own partition
    # uses a different mode.
    for component_id in hetjob_components:
        assert atf.wait_for_job_state(component_id, preempted_state), (
            f"Component ({component_id}) is "
            f"{atf.get_job_parameter(component_id, 'JobState')}, expected "
            f"{preempted_state}; the hetjob should resolve to {resolved_mode} "
            f"for all components"
        )


def test_hetjob_components_keep_their_own_grace_time():
    """Each hetjob component keeps its own partition's GraceTime.

    One resolved PreemptMode is applied to every component, but "the GraceTime
    and user warning signal for each component of the heterogeneous job remain
    unique" (slurm.conf(5), PreemptMode). Span a hetjob across two partitions
    that differ only in GraceTime and check each component's end time moves in
    by its own partition's value, not by a single shared one.

    Both components enter their grace period in the same pass, so the gap
    between their end times is the gap between the two GraceTime settings.
    """

    hetjob_id, hetjob_components = _submit_hetjob(
        "hetjob_grace.in", ["low_grace_short", "low_grace_long"]
    )
    for component_id in hetjob_components:
        assert atf.wait_for_job_state(
            component_id, "RUNNING"
        ), f"Heterogeneous job component ({component_id}) never started running"

    short_id, long_id = hetjob_components
    original_end_times = {job_id: _end_time(job_id) for job_id in hetjob_components}

    # A higher-priority job needing both nodes selects the hetjob for
    # preemption, which starts each component's grace period.
    preemptor_id = atf.submit_job_sbatch(
        '-N2 --ntasks-per-node=1 -c2 -p highprio -t5 -o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )

    # The hetjob is not actually preempted until the longest grace expires, so
    # there is a full GRACE_LONG window in which to read both end times. Bound
    # the wait by that window rather than by the atf default: past it the end
    # times are gone and a longer wait could only report a later failure.
    grace_end_times = {}
    for _ in atf.timer(timeout=GRACE_LONG, poll_interval=1):
        end_times = {job_id: _end_time(job_id) for job_id in hetjob_components}
        if all(
            end_times[job_id] is not None
            and end_times[job_id] < original_end_times[job_id]
            for job_id in hetjob_components
        ):
            grace_end_times = end_times
            break
    assert grace_end_times, (
        f"Both components should have entered a grace period after preemptor "
        f"({preemptor_id}) was submitted"
    )

    gap = (grace_end_times[long_id] - grace_end_times[short_id]).total_seconds()
    expected_gap = GRACE_LONG - GRACE_SHORT
    assert abs(gap - expected_gap) <= 5, (
        f"Component ({long_id}) in low_grace_long should end {expected_gap}s "
        f"after component ({short_id}) in low_grace_short, but the gap is "
        f"{gap}s; the components did not keep their own GraceTime"
    )


def test_normal_job_in_low_suspend_is_suspend_preempted():
    """Positive control: a non-hetjob in low_suspend IS SUSPEND-preempted.

    Anchors the ["low_suspend", "low_suspend"] row above, which asserts that an
    all-SUSPEND hetjob resolves to OFF and is left alone. That row would read
    the same way if SUSPEND preemption were simply inoperative in this module's
    partition layout, which differs from test_128_3's. Proving a normal job in
    the same partition does get suspended is what makes the hetjob's survival a
    real exemption (slurm.conf(5), PreemptMode).
    """

    # Every node, so the preemptor has nowhere to start except by preempting.
    victim_id = atf.submit_job_sbatch(
        "-N3 --ntasks-per-node=1 -c2 -p low_suspend -t2 -o /dev/null "
        '--wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        victim_id, "RUNNING", fatal=True
    ), f"Low-priority victim ({victim_id}) never started running"

    preemptor_id = atf.submit_job_sbatch(
        '-N1 -c2 -p highprio -t1 -o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        preemptor_id, "RUNNING"
    ), f"Preemptor ({preemptor_id}) never started; SUSPEND preemption is not live"

    assert atf.wait_for_job_state(victim_id, "SUSPENDED"), (
        f"Victim ({victim_id}) in low_suspend was not suspended; SUSPEND "
        f"preemption is not live in this configuration"
    )
