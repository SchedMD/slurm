############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test hetjob preemption under PreemptType=preempt/qos."""

import pytest

import atf

pytestmark = pytest.mark.slow

QOS_LOW = "qos_128_6_low"
# One low QOS per PreemptMode, so a hetjob can put each component in a QOS with
# a different mode. QOS_LOW deliberately carries no PreemptMode, covering the
# fallback to the cluster PreemptMode.
QOS_SUSPEND = "qos_128_6_suspend"
QOS_REQUEUE = "qos_128_6_requeue"
QOS_CANCEL = "qos_128_6_cancel"
# A QOS the preemptor is NOT allowed to preempt, used to make one component of
# a hetjob ineligible while the others stay eligible.
QOS_PROTECTED = "qos_128_6_protected"
QOS_HIGH = "qos_128_6_high"

# Every QOS the preemptor is allowed to preempt. QOS_PROTECTED is deliberately
# absent.
QOS_VICTIMS = [QOS_LOW, QOS_SUSPEND, QOS_REQUEUE, QOS_CANCEL]

# Every QOS the test user must be able to submit against.
QOS_ALL = QOS_VICTIMS + [QOS_PROTECTED, QOS_HIGH]

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
def setup(request):
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Ticket 23654: HetJob SUSPEND exemption added in 26.11",
    )
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU_Memory")
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_config_parameter("PreemptType", "preempt/qos")
    atf.require_config_parameter("PreemptMode", "SUSPEND,GANG")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    # A hetjob needs the backfill scheduler to place each component on a
    # different node to start, so require two nodes.
    atf.require_nodes(2, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    # To specifically test SUSPEND preemption rather than time-sliced gang
    # scheduling, we need to place preemptor and preemptee jobs into high and
    # low priority partitions, respectively. See this paragraph of the preempt
    # docs for more info:
    # If PreemptType=preempt/qos is configured and if the preempted job(s) and
    # the preemptor job from are on the same partition, then they will share
    # resources with the Gang scheduler (time-slicing). If not (i.e. if the
    # preemptees and preemptor are on different partitions) then the preempted
    # jobs will remain suspended until the preemptor ends.
    atf.require_config_parameter(
        "PartitionName",
        {
            "lowprio": {"Nodes": "ALL", "PriorityTier": "1", "Default": "YES"},
            "highprio": {"Nodes": "ALL", "PriorityTier": "2"},
        },
    )
    # A REQUEUE-mode preemption can only requeue a requeue-able job; otherwise
    # it falls back to cancel. Force requeue-ability so the resolved mode, not
    # the cluster's JobRequeue default, decides the outcome.
    atf.require_config_parameter("JobRequeue", "1")
    atf.require_slurm_running()

    cluster = atf.get_config_parameter("ClusterName")
    user = atf.properties["test-user"]
    su = atf.properties["slurm-user"]

    def cleanup():
        atf.run_command(
            f"sacctmgr -i modify user {user} where cluster={cluster} set qos=normal",
            user=su,
            quiet=True,
        )
        # Remove QOS_HIGH first: it references the others through Preempt.
        atf.run_command(
            f"sacctmgr -i remove qos {QOS_HIGH}",
            user=su,
            quiet=True,
        )
        atf.run_command(
            f"sacctmgr -i remove qos {','.join(QOS_VICTIMS + [QOS_PROTECTED])}",
            user=su,
            quiet=True,
        )

    # Register teardown before creating anything so a failure partway through
    # setup still tears down, and pre-clean any QOS leaked by an aborted prior
    # run so the fatal adds below do not fail on a name that already exists.
    request.addfinalizer(cleanup)
    cleanup()

    # QOS_LOW carries no PreemptMode, so it inherits the cluster's SUSPEND.
    atf.run_command(
        f"sacctmgr -i add qos {QOS_LOW}",
        user=su,
        fatal=True,
    )
    # One QOS per PreemptMode drives mode resolution from the QOS rather than
    # the partition. QOS_PROTECTED gets a preemptable mode too: what makes it
    # protected is its absence from QOS_HIGH's Preempt list, not its mode.
    for qos, mode in (
        (QOS_SUSPEND, "suspend"),
        (QOS_REQUEUE, "requeue"),
        (QOS_CANCEL, "cancel"),
        (QOS_PROTECTED, "requeue"),
    ):
        atf.run_command(
            f"sacctmgr -i add qos {qos} PreemptMode={mode}",
            user=su,
            fatal=True,
        )
    # QOS_HIGH preempts every low QOS. A higher Priority makes the scheduler
    # consider the preemptor's jobs first.
    atf.run_command(
        f"sacctmgr -i add qos {QOS_HIGH} Preempt={','.join(QOS_VICTIMS)} "
        f"Priority=100",
        user=su,
        fatal=True,
    )
    # Create the association if it is missing, then set the QOS list
    # unconditionally. "sacctmgr add user" exits 0 with "Nothing added." when
    # the association already exists, so the add alone cannot be relied on to
    # apply qos=.
    qos_list = ",".join(["normal"] + QOS_ALL)
    atf.run_command(
        f"sacctmgr -i add user {user} cluster={cluster} account=root "
        f"qos={qos_list}",
        user=su,
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i modify user {user} where cluster={cluster} "
        f"set qos={qos_list}",
        user=su,
        fatal=True,
    )


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


def _submit_hetjob(name, qos_list, partition="lowprio"):
    """Submit a hetjob with one component per named QOS, one per node.

    IN name      - file name for the generated batch script
    IN qos_list  - QOS for each component, in order
    IN partition - partition for every component
    RET (leader job id, list of component job ids)
    """
    components = [
        f"#SBATCH -N1 -c2 -p {partition} -q {qos} -t5 -o /dev/null" for qos in qos_list
    ]
    atf.make_bash_script(
        name, "\n#SBATCH hetjob\n".join(components) + "\nsleep infinity\n"
    )

    het_id = atf.submit_job_sbatch(name, fatal=True)
    return het_id, atf.range_to_list(
        atf.get_job_parameter(het_id, "HetJobIdSet", fatal=True)
    )


def _submit_preemptor(nodes=2, time_limit=5):
    """Submit a QOS_HIGH job in the high-priority partition.

    IN nodes      - number of whole nodes to request
    IN time_limit - job time limit in minutes
    RET the job id
    """
    return atf.submit_job_sbatch(
        f"-N{nodes} --ntasks-per-node=1 -c2 -p highprio -q {QOS_HIGH} "
        f'-t{time_limit} -o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )


def test_hetjob_not_suspend_preempted_by_higher_qos():
    """A running hetjob must not be SUSPEND-preempted by a higher QOS.

    Under preempt/qos with PreemptMode=SUSPEND,GANG a higher-QOS job normally
    suspends a lower-QOS job to reclaim its resources. Gang scheduling excludes
    heterogeneous jobs, so the hetjob cannot be suspended: the higher-QOS job
    must stay pending, and the hetjob must keep running.
    """

    # Heterogeneous job fills the low-QOS resources (one component per node).
    hetjob_id, hetjob_components = _submit_hetjob("hetjob_qos.in", [QOS_LOW, QOS_LOW])
    # Wait for every component so both nodes are provably held by the hetjob
    # before submitting the preemptor.
    for component_id in hetjob_components:
        assert atf.wait_for_job_state(
            component_id, "RUNNING"
        ), f"Heterogeneous job component ({component_id}) never started running"

    # A higher-QOS job requesting a whole node, the resources the hetjob holds.
    preemptor_id = _submit_preemptor(nodes=1, time_limit=1)

    # The preemptor must NOT start: the hetjob cannot be gang-suspended, so the
    # preemptor stays pending instead of oversubscribing the node. With
    # bf_interval=1 the scheduler re-evaluates every second, so a broken
    # exemption would preempt well inside NOT_RUNNING_TIMEOUT.
    assert not atf.wait_for_job_state(
        preemptor_id, "RUNNING", timeout=NOT_RUNNING_TIMEOUT, xfail=True
    ), f"Preemptor ({preemptor_id}) started, the hetjob was oversubscribed instead of protected"

    _assert_blocked(preemptor_id, "Preemptor")

    # The hetjob must still be running (never suspended), every component.
    for component_id in hetjob_components:
        assert (
            atf.get_job_parameter(component_id, "JobState") == "RUNNING"
        ), f"Heterogeneous job component ({component_id}) should still be running"


def test_hetjob_does_not_suspend_preempt_lower_qos():
    """A hetjob must not SUSPEND-preempt a lower-QOS job.

    Gang scheduling excludes heterogeneous jobs, so a hetjob cannot drive
    gang-suspend preemption. A hetjob in the higher QOS must stay pending behind
    the lower-QOS job holding its resources, and that job must keep running.
    """

    # A normal low-QOS job fills both nodes (one task per node).
    victim_id = atf.submit_job_sbatch(
        f"-N2 --ntasks-per-node=1 -c2 -p lowprio -q {QOS_LOW} -t2 -o /dev/null "
        f'--wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        victim_id, "RUNNING"
    ), f"Low-QOS victim ({victim_id}) never started running"

    # A higher-QOS hetjob requesting both whole nodes, the resources the victim
    # is currently holding.
    hetjob_id, hetjob_components = _submit_hetjob(
        "hetjob_qos_preemptor.in", [QOS_HIGH, QOS_HIGH], partition="highprio"
    )

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
    ), f"Low-QOS victim ({victim_id}) should still be running"


def test_normal_job_is_suspend_preempted_by_higher_qos():
    """Positive control: a non-hetjob IS SUSPEND-preempted under preempt/qos.

    This anchors the hetjob negative assertions above. It proves preempt/qos
    SUSPEND,GANG preemption is actually live in this config, so a hetjob that
    stays RUNNING is a real exemption and not a dormant preemption path.
    """

    # A normal low-QOS job fills both nodes (one task per node).
    victim_id = atf.submit_job_sbatch(
        f"-N2 --ntasks-per-node=1 -c2 -p lowprio -q {QOS_LOW} -t2 -o /dev/null "
        f'--wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        victim_id, "RUNNING"
    ), f"Low-QOS victim ({victim_id}) never started running"

    # A higher-QOS job requesting a whole node forces the victim to be
    # gang-suspended to free it.
    preemptor_id = _submit_preemptor(nodes=1, time_limit=1)
    assert atf.wait_for_job_state(
        preemptor_id, "RUNNING"
    ), f"Preemptor ({preemptor_id}) never started; QOS SUSPEND preemption is not live"

    # The victim must be suspended, proving preempt/qos SUSPEND,GANG is live.
    assert atf.wait_for_job_state(
        victim_id, "SUSPENDED"
    ), f"Victim ({victim_id}) was not suspended; QOS SUSPEND preemption is not live"


@pytest.mark.parametrize(
    "qos_list,resolved_mode,preempted_state",
    [
        # A component resolving to SUSPEND is treated as OFF, so REQUEUE and
        # CANCEL both win over it whichever component carries them.
        ([QOS_SUSPEND, QOS_REQUEUE], "REQUEUE", "PENDING"),
        ([QOS_REQUEUE, QOS_SUSPEND], "REQUEUE", "PENDING"),
        ([QOS_SUSPEND, QOS_CANCEL], "CANCEL", "PREEMPTED"),
        ([QOS_CANCEL, QOS_SUSPEND], "CANCEL", "PREEMPTED"),
        # REQUEUE is searched for before CANCEL, so it wins from either
        # position. The reversed row is the one that tells this apart from
        # simply taking the first component that is not SUSPEND.
        ([QOS_REQUEUE, QOS_CANCEL], "REQUEUE", "PENDING"),
        ([QOS_CANCEL, QOS_REQUEUE], "REQUEUE", "PENDING"),
        # Every component resolving to SUSPEND means the whole job resolves to
        # OFF: it is exempt, so it keeps RUNNING and the preemptor waits.
        ([QOS_SUSPEND, QOS_SUSPEND], "SUSPEND", "RUNNING"),
    ],
)
def test_hetjob_mixed_component_qos_modes_resolve_requeue_then_cancel(
    qos_list, resolved_mode, preempted_state
):
    """A hetjob resolves one PreemptMode for all components, driven by the QOS.

    Mirrors test_128_5 but takes each component's mode from its QOS rather than
    its partition. The first component found with PreemptMode=REQUEUE sets the
    mode for every component; if there is none, the first found with CANCEL
    does. A component resolving to SUSPEND is treated as PreemptMode=OFF, and a
    job with no REQUEUE or CANCEL component is not preempted at all
    (slurm.conf(5), PreemptMode). Each pair is submitted in both orders, so the
    search order decides the outcome rather than component position.
    """

    hetjob_id, hetjob_components = _submit_hetjob("hetjob_qos_mixed.in", qos_list)
    for component_id in hetjob_components:
        assert atf.wait_for_job_state(
            component_id, "RUNNING"
        ), f"Heterogeneous job component ({component_id}) never started running"

    # A higher-QOS job needing both nodes forces the hetjob to be preempted as
    # a unit, using the single resolved mode.
    preemptor_id = _submit_preemptor()
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
    # (RUNNING when SUSPEND-exempt), including components whose own QOS uses a
    # different mode.
    for component_id in hetjob_components:
        assert atf.wait_for_job_state(component_id, preempted_state), (
            f"Component ({component_id}) is "
            f"{atf.get_job_parameter(component_id, 'JobState')}, expected "
            f"{preempted_state}; the hetjob should resolve to {resolved_mode} "
            f"for all components"
        )


def test_hetjob_with_one_ineligible_component_is_not_preempted():
    """A hetjob is preempted only if every component is eligible.

    "For a heterogeneous job to be considered for preemption, all components
    must be eligible for preemption" (sacctmgr(1), PreemptMode). Under
    preempt/qos eligibility is the preemptor QOS's Preempt list. One component
    sits in a QOS on that list and the other does not, so the hetjob as a whole
    must be left alone even though its resolved mode is REQUEUE.

    test_hetjob_mixed_component_qos_modes_resolve_requeue_then_cancel runs the
    same QOS_REQUEUE component against the same preemptor and shows it is
    preempted when every component is eligible.
    """

    hetjob_id, hetjob_components = _submit_hetjob(
        "hetjob_qos_ineligible.in", [QOS_REQUEUE, QOS_PROTECTED]
    )
    for component_id in hetjob_components:
        assert atf.wait_for_job_state(
            component_id, "RUNNING"
        ), f"Heterogeneous job component ({component_id}) never started running"

    preemptor_id = _submit_preemptor()

    # The preemptor must NOT start: one component is outside its Preempt list,
    # so the whole hetjob is ineligible. The default polling timeout is amply
    # generous; with bf_interval=1 the scheduler re-evaluates every second.
    assert not atf.wait_for_job_state(
        preemptor_id, "RUNNING", timeout=NOT_RUNNING_TIMEOUT, xfail=True
    ), (
        f"Preemptor ({preemptor_id}) started; a hetjob with an ineligible "
        "component must not be preempted"
    )
    _assert_blocked(preemptor_id, "Preemptor")

    # Every component must be untouched, including the eligible one.
    for component_id in hetjob_components:
        assert (
            atf.get_job_parameter(component_id, "JobState") == "RUNNING"
        ), f"Heterogeneous job component ({component_id}) should still be running"


def test_hetjob_does_not_preempt_victim_resolving_to_suspend_by_qos():
    """A hetjob never preempts a victim whose QOS resolves to SUSPEND.

    The rule is per victim, not per cluster: "A heterogeneous job is never
    suspended as a preemptee, and it will never preempt a job whose resolved
    PreemptMode is SUSPEND" (slurm.conf(5), PreemptMode). Under preempt/qos the
    mode comes from the victim's QOS, a different lookup from the partition
    path test_128_4 covers.

    The cluster mode here is REQUEUE,GANG and only the victim's QOS resolves to
    SUSPEND, so a result of "the hetjob stays pending" cannot be explained by
    the cluster being SUSPEND. The second half runs the same hetjob at the same
    cluster mode against a QOS_REQUEUE victim and shows it does preempt, which
    is what makes this per-victim rather than "a hetjob cannot preempt anything
    under gang".
    """

    atf.set_config_parameter("PreemptMode", "REQUEUE,GANG", restart=True)

    # A normal job fills both nodes under the QOS whose PreemptMode is SUSPEND.
    victim_id = atf.submit_job_sbatch(
        f"-N2 --ntasks-per-node=1 -c2 -p lowprio -q {QOS_SUSPEND} -t5 "
        '-o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        victim_id, "RUNNING", fatal=True
    ), f"SUSPEND-QOS victim ({victim_id}) never started running"

    hetjob_id, hetjob_components = _submit_hetjob(
        "hetjob_qos_suspend_victim.in", [QOS_HIGH, QOS_HIGH], partition="highprio"
    )

    assert not atf.wait_for_job_state(
        hetjob_id, "RUNNING", timeout=NOT_RUNNING_TIMEOUT, xfail=True
    ), (
        f"Heterogeneous preemptor ({hetjob_id}) started; it must not preempt a "
        f"victim whose QOS resolves to PreemptMode SUSPEND"
    )
    _assert_blocked(hetjob_id, "Heterogeneous preemptor")

    assert (
        atf.get_job_parameter(victim_id, "JobState") == "RUNNING"
    ), f"SUSPEND-QOS victim ({victim_id}) should still be running"

    atf.cancel_jobs([hetjob_id, victim_id], fatal=True)

    # Control: the same hetjob at the same cluster mode DOES preempt a victim
    # whose QOS resolves to REQUEUE.
    requeue_victim_id = atf.submit_job_sbatch(
        f"-N2 --ntasks-per-node=1 -c2 -p lowprio -q {QOS_REQUEUE} -t5 "
        '-o /dev/null --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        requeue_victim_id, "RUNNING", fatal=True
    ), f"REQUEUE-QOS victim ({requeue_victim_id}) never started running"

    control_id, control_components = _submit_hetjob(
        "hetjob_qos_requeue_victim.in", [QOS_HIGH, QOS_HIGH], partition="highprio"
    )
    for component_id in control_components:
        assert atf.wait_for_job_state(component_id, "RUNNING"), (
            f"Heterogeneous preemptor ({control_id}) component ({component_id}) "
            f"never started; it must preempt a REQUEUE-resolved victim"
        )
