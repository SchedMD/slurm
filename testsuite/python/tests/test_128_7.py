############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test hetjob exclusion from oversubscription while gang scheduling is on."""

import pytest

import atf

pytestmark = pytest.mark.slow

# Everything here except the no-gang control tests behavior added in 26.11.
gang_gate = pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Ticket 23654: hetjob oversubscription under gang fixed in 26.11",
)

# GANG "is used to enable gang scheduling independent of whether preemption is
# enabled (i.e. independent of the PreemptType setting)" (slurm.conf(5),
# PreemptMode), and the exclusion is scoped to GANG, so it holds for every
# PreemptType including preempt/none. Vary both so neither the SUSPEND mode nor
# the presence of preemption can be what makes these tests pass.
GANG_CONFIGS = [
    ("preempt/partition_prio", "SUSPEND,GANG"),
    ("preempt/partition_prio", "REQUEUE,GANG"),
    ("preempt/none", "GANG"),
]

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

# require_nodes() treats the CPU count as a minimum, so a filler asking for a
# hard-coded 2 CPUs leaves cores free on any wider node and the jobs that are
# supposed to be sharing a row never share one. Every job sizes its -c request
# from the narrowest node instead, so a filler provably takes every CPU.
CPUS_PER_NODE = 0


@pytest.fixture(scope="module", autouse=True)
def setup():
    global CPUS_PER_NODE

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU_Memory")
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    # A valid starting configuration so slurmctld comes up; each test then sets
    # the PreemptType and PreemptMode it needs.
    atf.require_config_parameter("PreemptType", "preempt/partition_prio")
    atf.require_config_parameter("PreemptMode", "SUSPEND,GANG")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    # A hetjob needs the backfill scheduler to place each component on a
    # different node to start, so require two nodes.
    atf.require_nodes(2, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    # Every job in these partitions shares one PriorityTier, so nothing here is
    # preemption: the partitions isolate oversubscription on its own. FORCE
    # oversubscribes without the job asking; YES only oversubscribes jobs
    # submitted with --oversubscribe. Both give two rows, so sharing is really
    # possible.
    atf.require_config_parameter(
        "PartitionName",
        {
            "shared": {
                "Nodes": "ALL",
                "PriorityTier": "1",
                "OverSubscribe": "FORCE:2",
            },
            "shared_yes": {
                "Nodes": "ALL",
                "PriorityTier": "1",
                "OverSubscribe": "YES:2",
            },
        },
    )
    atf.require_slurm_running()
    CPUS_PER_NODE = min(
        int(atf.get_node_parameter(name, "CPUTot", default=2))
        for name in atf.get_nodes(quiet=True)
    )


def _apply_preempt(preempt_type, preempt_mode):
    """Configure preemption and restart once for both parameters.

    IN preempt_type - PreemptType to configure
    IN preempt_mode - PreemptMode to configure
    """
    atf.set_config_parameter("PreemptType", preempt_type)
    atf.set_config_parameter("PreemptMode", preempt_mode, restart=True)


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


def _submit_filler(partition="shared", share_arg=""):
    """Submit a job taking every CPU on both nodes of a shared partition.

    IN partition  - partition to submit to
    IN share_arg  - extra sbatch arguments, for an OverSubscribe=YES partition
    RET the job id
    """
    return atf.submit_job_sbatch(
        f"-N2 --ntasks-per-node=1 -c{CPUS_PER_NODE} -p {partition} {share_arg} "
        f'-t5 -o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )


def _submit_shared_hetjob(name, partition="shared", share_arg=""):
    """Submit a two component hetjob filling both nodes of a shared partition.

    IN name       - file name for the generated batch script
    IN partition  - partition for both components
    IN share_arg  - extra sbatch arguments, for an OverSubscribe=YES partition
    RET (leader job id, list of component job ids)
    """
    atf.make_bash_script(
        name,
        f"""
#SBATCH -N1 -c{CPUS_PER_NODE} -p {partition} {share_arg} -t5 -o /dev/null
#SBATCH hetjob
#SBATCH -N1 -c{CPUS_PER_NODE} -p {partition} {share_arg} -t5
sleep infinity
""",
    )
    het_id = atf.submit_job_sbatch(name, fatal=True)
    return het_id, atf.range_to_list(
        atf.get_job_parameter(het_id, "HetJobIdSet", fatal=True)
    )


def _assert_sharing_is_live(partition="shared", share_arg=""):
    """Fill a shared partition twice over and assert the second job is placed.

    Every CPU is taken by the first job, so the second can only be allocated by
    taking the partition's second row. Gang parks a job in the inactive row, so
    SUSPENDED is the proof it was allocated at all: a job that cannot share
    stays PENDING. Waiting for RUNNING instead would wait on a
    SchedulerTimeSlice rotation, which these tests do not configure.
    IN partition - partition to fill
    IN share_arg - extra sbatch arguments, for an OverSubscribe=YES partition
    RET (first job id, second job id)
    """
    first_id = _submit_filler(partition, share_arg)
    assert atf.wait_for_job_state(
        first_id, "RUNNING", fatal=True
    ), f"First job ({first_id}) never started running"

    second_id = _submit_filler(partition, share_arg)
    assert atf.wait_for_job_state(second_id, "SUSPENDED"), (
        f"Second job ({second_id}) did not oversubscribe; sharing is not live "
        f"in partition {partition}"
    )
    return first_id, second_id


@gang_gate
@pytest.mark.parametrize("preempt_type,preempt_mode", GANG_CONFIGS)
def test_normal_job_oversubscribes_under_gang(preempt_type, preempt_mode):
    """Positive control: two normal jobs DO share a partition under gang.

    Establishes that oversubscription is live in each configuration, so the
    hetjob tests below cannot pass merely because nothing can ever share here.
    """

    _apply_preempt(preempt_type, preempt_mode)
    _assert_sharing_is_live()


@gang_gate
@pytest.mark.parametrize("preempt_type,preempt_mode", GANG_CONFIGS)
def test_hetjob_does_not_oversubscribe_onto_running_job(preempt_type, preempt_mode):
    """A hetjob must not take a shared row over a running job under gang.

    A heterogeneous job "is never placed on cores already allocated to another
    job, not even in its own partition's oversubscription rows" while GANG is
    enabled (slurm.conf(5), PreemptMode). Every job here is in one partition at
    one PriorityTier, so no preemption is involved and the hetjob must simply
    stay pending.
    """

    _apply_preempt(preempt_type, preempt_mode)

    filler_id = _submit_filler()
    assert atf.wait_for_job_state(
        filler_id, "RUNNING", fatal=True
    ), f"Filler job ({filler_id}) never started running"

    het_id, components = _submit_shared_hetjob("hetjob_shared.in")

    assert not atf.wait_for_job_state(
        het_id, "RUNNING", timeout=NOT_RUNNING_TIMEOUT, xfail=True
    ), f"Heterogeneous job ({het_id}) started, oversubscribing the running job"
    for component_id in components:
        _assert_blocked(component_id, "Heterogeneous job component")


@gang_gate
@pytest.mark.parametrize("preempt_type,preempt_mode", GANG_CONFIGS)
def test_normal_job_does_not_oversubscribe_onto_running_hetjob(
    preempt_type, preempt_mode
):
    """A normal job must not take a shared row over a running hetjob under gang.

    The reverse direction of the test above: "no other job is placed on its
    cores while it runs" (slurm.conf(5), PreemptMode).
    """

    _apply_preempt(preempt_type, preempt_mode)

    het_id, components = _submit_shared_hetjob("hetjob_shared_first.in")
    for component_id in components:
        assert atf.wait_for_job_state(
            component_id, "RUNNING", fatal=True
        ), f"Heterogeneous job component ({component_id}) never started running"

    normal_id = _submit_filler()

    assert not atf.wait_for_job_state(
        normal_id, "RUNNING", timeout=NOT_RUNNING_TIMEOUT, xfail=True
    ), f"Job ({normal_id}) started, oversubscribing the running heterogeneous job"
    _assert_blocked(normal_id, "Job")


@gang_gate
def test_hetjob_does_not_oversubscribe_in_oversubscribe_yes_partition():
    """The exclusion also covers OverSubscribe=YES, not just FORCE.

    A heterogeneous job is excluded from oversubscription "in every partition"
    while GANG is enabled (slurm.conf(5), OverSubscribe). YES takes a different
    path from FORCE, since it only shares between jobs that asked to, so it
    gets its own coverage. The normal-job pair first proves YES sharing is live
    here.
    """

    _apply_preempt("preempt/partition_prio", "SUSPEND,GANG")

    first_id, second_id = _assert_sharing_is_live("shared_yes", "--oversubscribe")
    atf.cancel_jobs([first_id, second_id], fatal=True)

    filler_id = _submit_filler("shared_yes", "--oversubscribe")
    assert atf.wait_for_job_state(
        filler_id, "RUNNING", fatal=True
    ), f"Filler job ({filler_id}) never started running"

    het_id, components = _submit_shared_hetjob(
        "hetjob_shared_yes.in", "shared_yes", "--oversubscribe"
    )

    assert not atf.wait_for_job_state(
        het_id, "RUNNING", timeout=NOT_RUNNING_TIMEOUT, xfail=True
    ), (
        f"Heterogeneous job ({het_id}) started, oversubscribing the running "
        "job in an OverSubscribe=YES partition"
    )
    for component_id in components:
        _assert_blocked(component_id, "Heterogeneous job component")


def test_hetjob_oversubscribes_without_gang():
    """A hetjob DOES take a shared row when gang scheduling is off.

    The counterpart to test_hetjob_does_not_oversubscribe_onto_running_job.
    The restriction applies only while GANG is enabled, so without it the
    partition oversubscribes exactly as OverSubscribe=FORCE:2 configures it.

    No version gate: this is longstanding behavior that must keep passing on
    every supported version, and it is the guard that the hetjob exclusion did
    not leak out of gang scheduling.
    """

    _apply_preempt("preempt/partition_prio", "REQUEUE")

    filler_id = _submit_filler()
    assert atf.wait_for_job_state(
        filler_id, "RUNNING", fatal=True
    ), f"Filler job ({filler_id}) never started running"

    # Every CPU is taken, so the hetjob can only start by sharing row 1.
    het_id, components = _submit_shared_hetjob("hetjob_shared_nogang.in")

    for component_id in components:
        assert atf.wait_for_job_state(component_id, "RUNNING"), (
            f"Heterogeneous job ({het_id}) component ({component_id}) never "
            "started; without gang it must oversubscribe the filler"
        )
