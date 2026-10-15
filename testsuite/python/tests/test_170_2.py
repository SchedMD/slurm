############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify backfill reserves the nodes it preempted and keeps others off them.

preempt.html, "Limitations of Preemption During Backfill Scheduling", promises
that "If during backfill scheduling a job preempts one or more other jobs, the
whole nodes for those preempted jobs are reserved for the preemptor job".
sched_config.html promises that "Backfill scheduling will start lower priority
jobs if doing so does not delay the expected start time of any higher priority
jobs." This test covers both halves: which nodes the preemptor reserves, and
that a job queued behind it is not started on them.

Backfill plans a preemptor's placement by simulating every preemptable job
going away at once, then starts it by preempting as few jobs as it can. The
two can choose different nodes. The start then fails while the victims drain,
and the reservation backfill falls back on must name the nodes it really
preempted.

The layout forces the two choices apart. Two one-node jobs fill the first
two of three nodes, and the last node is left idle. A two-node preemptor
plans against a cluster where every preemptable job is already gone, so it
takes the leading pair. To actually start it only needs one more node beside
the idle one, so it preempts a single one-node job. The planned pair
therefore never holds the idle node.

That idle node is also the only node another job could run on while the
victim drains, so it carries the second half of the contract: a job submitted
after the reservation exists must not be given it.

Grace time holds the victim in RUNNING while it drains, which is the window
where the reservation is readable. Each scenario arranges that differently.
preempt/partition_prio takes it from the low priority partition. preempt/qos
ignores the partition and takes it from the preemptee's QOS.
"""

import collections
import re

import pytest

import atf

# GraceTime, in seconds, given to a preempted job. The reservation only exists
# while the victim drains, so this bounds the whole observation: it must cover
# backfill picking up the preemptor and the filler check below.
GRACE_TIME = 60

# Backfill must run inside GRACE_TIME for the reservation to appear, and the
# filler check below counts backfill cycles rather than seconds.
BF_INTERVAL = 1

# How many backfill cycles must really have run in that window. bf_interval
# is a minimum rather than a promise, so without this the filler staying
# pending could just mean backfill never looked at it.
MIN_BACKFILL_CYCLES = 2

# sdiag prints "Total cycles" twice, once for the main scheduler and once for
# backfill. Only the backfill one counts here.
_BACKFILL_CYCLES_RE = re.compile(
    r"Backfilling stats.*?Total cycles:\s*(\d+)", re.DOTALL
)

PARTITION_LOW = "lowprio_bf"
PARTITION_HIGH = "highprio_bf"
PARTITION_QOS = "qos_bf"

QOS_LOW = "test_170_2_low"
QOS_HIGH = "test_170_2_high"
QOS_TAKER = "test_170_2_taker"

# How one scenario submits each of the three roles. Every callback takes no
# arguments except submit_victim(), which takes the node to pin the victim to.
Scenario = collections.namedtuple(
    "Scenario", ["submit_victim", "submit_preemptor", "submit_filler"]
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5, 6),
        component="sbin/slurmctld",
        reason="Ticket 25980: backfill reserves the preempted nodes in 26.05.6",
    )

    # SchedulerType is only defaulted to sched/backfill, and the reservation
    # this test reads is written by that plugin alone.
    atf.require_config_parameter("SchedulerType", "sched/backfill")

    # The layout rests on planning taking the leading pair, which is a
    # property of how the select plugin orders candidate nodes rather than
    # something the scheduler promises, so the plugin is pinned too.
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")

    # A valid starting configuration so slurmctld comes up. Each scenario
    # fixture then sets the PreemptType it needs.
    atf.require_config_parameter("PreemptType", "preempt/partition_prio")
    atf.require_config_parameter("PreemptMode", "CANCEL")

    # Every victim is preempted seconds after it starts, so nothing may be
    # exempt from preemption for an initial period.
    atf.require_config_parameter("PreemptExemptTime", "0")

    # The qos scenario resolves both the preempt list and the preemptee's
    # GraceTime out of the database, so the module needs accounting even
    # though the partition scenario does not.
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")

    # Tuple form, so an existing token is replaced rather than leaving two of
    # them for slurmctld to choose between.
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("bf_interval", BF_INTERVAL)
    )

    # Backfill is the scheduler under test. The main loop would otherwise start
    # the preemptor from a path this behavior does not live in, and would race
    # every observation below.
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", -1))

    # Every job is exclusive, so one CPU per node is enough.
    nodes = atf.require_nodes(3, [("CPUs", 1)])[:3]
    node_range = ",".join(nodes)

    # The partitions name these three nodes rather than ALL. A fourth free node
    # anywhere in the partition would let the preemptor start without
    # preempting anything.
    atf.require_config_parameter(
        "PartitionName",
        {
            PARTITION_LOW: {
                "Nodes": node_range,
                "PriorityTier": "1",
                "PreemptMode": "CANCEL",
                "GraceTime": str(GRACE_TIME),
            },
            PARTITION_HIGH: {
                "Nodes": node_range,
                "PriorityTier": "2",
                "PreemptMode": "OFF",
            },
            PARTITION_QOS: {
                "Nodes": node_range,
                "PriorityTier": "1",
                "Default": "YES",
            },
        },
    )

    atf.require_slurm_running()

    # AccountingStorageEnforce=qos which implies associations is on for the
    # whole module, so every scenario needs the submitting user to have an
    # association.
    atf.run_command(
        f"sacctmgr -i add user {atf.properties['test-user']} "
        f"cluster={atf.get_config_parameter('ClusterName')} account=root",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="module")
def nodes():
    """Return the three test nodes in Slurm's own order.

    Which nodes backfill plans on and which one it leaves idle both follow the
    node order slurmctld uses, so the order must come from slurmctld rather
    than from a Python sort of the names.
    """

    names = atf.node_range_to_list(atf.get_partition_parameter(PARTITION_HIGH, "Nodes"))
    assert len(names) == 3, f"The test partitions should hold three nodes, got {names}"
    return names


@pytest.fixture(scope="module")
def qos_setup():
    """Create the three QOS the qos scenario uses and grant them to the user.

    Nothing is yielded; the QOS names are module constants. Teardown drops the
    grants and the QOS so a later module does not inherit them.
    """

    cluster = atf.get_config_parameter("ClusterName")
    user = atf.properties["test-user"]
    slurm_user = atf.properties["slurm-user"]

    # Partition GraceTime is ignored under preempt/qos. The preemptee's grace
    # is resolved from its own QOS, so it is set here instead.
    atf.run_command(
        f"sacctmgr -i add qos {QOS_LOW} PreemptMode=CANCEL GraceTime={GRACE_TIME}",
        user=slurm_user,
        fatal=True,
    )

    # "Preempt=" lists the QOSs this QOS may preempt, so it belongs on the high
    # QOS pointing at the low one. Nothing lists high or taker, which is what
    # makes those two unpreemptable.
    atf.run_command(
        f"sacctmgr -i add qos {QOS_HIGH} Preempt={QOS_LOW}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(f"sacctmgr -i add qos {QOS_TAKER}", user=slurm_user, fatal=True)

    atf.run_command(
        f"sacctmgr -i modify user {user} where cluster={cluster} "
        f"set qos+={QOS_LOW},{QOS_HIGH},{QOS_TAKER}",
        user=slurm_user,
        fatal=True,
    )
    for qos in (QOS_LOW, QOS_HIGH, QOS_TAKER):
        _wait_for_qos_grant(user, qos)

    yield

    atf.run_command(
        f"sacctmgr -i modify user {user} where cluster={cluster} "
        f"set qos-={QOS_LOW},{QOS_HIGH},{QOS_TAKER}",
        user=slurm_user,
        quiet=True,
    )
    for qos in (QOS_LOW, QOS_HIGH, QOS_TAKER):
        atf.run_command(f"sacctmgr -i remove qos {qos}", user=slurm_user, quiet=True)


@pytest.fixture
def partition_prio_scenario():
    """Return the scenario where partition PriorityTier drives preemption.

    The filler shares the preemptor's partition, so it can preempt as well.
    Kept off the reserved idle node, it preempts the remaining victim instead
    and stays pending through that victim's grace time.
    """

    atf.set_config_parameter("PreemptType", "preempt/partition_prio", restart=True)

    return Scenario(
        submit_victim=lambda node: _submit(f"-N1 -w {node} -p {PARTITION_LOW}"),
        submit_preemptor=lambda: _submit(f"-N2 -p {PARTITION_HIGH}"),
        submit_filler=lambda: _submit(f"-N1 -p {PARTITION_HIGH}"),
    )


@pytest.fixture
def qos_scenario(qos_setup):
    """Return the scenario where QOS drives preemption.

    All three jobs share one partition, so the only thing that can preempt
    here is the QOS. The filler uses a QOS the preemptor cannot preempt, which
    is what would make a node it wrongly takes unrecoverable.
    """

    atf.set_config_parameter("PreemptType", "preempt/qos", restart=True)

    return Scenario(
        submit_victim=lambda node: _submit(
            f"-N1 -w {node} -p {PARTITION_QOS} -q {QOS_LOW}"
        ),
        submit_preemptor=lambda: _submit(f"-N2 -p {PARTITION_QOS} -q {QOS_HIGH}"),
        submit_filler=lambda: _submit(f"-N1 -p {PARTITION_QOS} -q {QOS_TAKER}"),
    )


def _submit(sbatch_args):
    """Submit one exclusive placeholder job and return its id.

    IN sbatch_args - the arguments that tell this job apart from the others
    RET the job id
    """

    return atf.submit_job_sbatch(
        f"{sbatch_args} -t5 -o /dev/null --exclusive --wrap 'sleep infinity'",
        fatal=True,
    )


def _wait_for_qos_grant(user, qos):
    """Block until slurmctld has picked up a QOS grant made via sacctmgr.

    sacctmgr provisioning reaches slurmctld asynchronously through the
    assoc_mgr update pushed from slurmdbd, so a submit issued right after
    granting a QOS can race that update and be rejected with "Invalid qos
    specification". A trial sbatch --test-only exercises the exact acceptance
    check a real submit would hit.
    IN user - the user the QOS was granted to
    IN qos  - the QOS that must be accepted
    """

    for _ in atf.timer():
        result = atf.run_command(
            f"sbatch --test-only -p {PARTITION_QOS} -q {qos} --wrap true",
            user=user,
            quiet=True,
        )
        if result["exit_code"] == 0:
            return
    pytest.fail(
        f"QOS '{qos}' was never accepted for user '{user}'; the sacctmgr grant "
        f"never propagated to slurmctld"
    )


def _backfill_cycles():
    """Return how many backfill cycles slurmctld has run since its last reset.

    RET the cycle count
    """

    output = atf.run_command_output("sdiag", fatal=True, quiet=True)
    match = _BACKFILL_CYCLES_RE.search(output)
    assert match, f"sdiag reported no backfill cycle count:\n{output}"
    return int(match.group(1))


def _node_set(node_expression):
    """Convert a node range expression into a set, treating empty as empty.

    An empty expression must not reach scontrol: `scontrol show hostnames` with
    no value falls back to $SLURM_NODELIST, which would turn "no reservation
    yet" into a bogus node set.
    IN node_expression - a node range expression, possibly empty
    RET a set of node names
    """

    if not node_expression:
        return set()
    return set(atf.node_range_to_list(node_expression))


@pytest.mark.parametrize(
    "scenario",
    [
        pytest.param("partition_prio_scenario", id="partition_prio"),
        pytest.param("qos_scenario", id="qos"),
    ],
)
def test_backfill_reserves_the_preempted_nodes(request, nodes, scenario):
    """Verify the preemptor reserves the preempted nodes and keeps others off."""

    scenario = request.getfixturevalue(scenario)

    # The last node stays idle. Pinning each victim with -w makes the layout a
    # fact of the submission rather than something to read back and check.
    *busy_nodes, idle_node = nodes

    victim_nodes = {scenario.submit_victim(node): node for node in busy_nodes}
    for job_id in victim_nodes:
        atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    preemptor = scenario.submit_preemptor()

    # Both observables come from one snapshot, so the victim set and the
    # reservation describe the same instant. fatal=True covers unparsable
    # scontrol output, which is an environment failure. An empty read is
    # normal until preemption starts, and the loop handles it.
    preempted = set()
    sched_nodes = set()
    for _ in atf.timer(timeout=GRACE_TIME, poll_interval=1):
        jobs = atf.get_jobs(use_json=True, fatal=True, quiet=True)
        preempted = {
            job_id
            for job_id in victim_nodes
            if jobs.get(job_id, {}).get("preempt_time", {}).get("number")
        }
        sched_nodes = _node_set(jobs.get(preemptor, {}).get("scheduled_nodes", ""))
        if preempted and sched_nodes:
            break

    assert (
        preempted
    ), f"No victim was preempted for the preemptor ({preemptor}) within {GRACE_TIME}s"
    assert sched_nodes, (
        f"Preemptor ({preemptor}) got no backfill reservation; it was "
        f"{atf.get_job_parameter(preemptor, 'JobState', default='NOT_FOUND')}"
    )

    # One free node plus one preempted node is all a two-node preemptor needs.
    # Preempting more than that means the scenario did not reproduce, and the
    # comparison below would not mean what it says.
    assert len(preempted) == 1, (
        f"Expected one victim to be preempted, got {sorted(preempted)}; the "
        f"preemptor ({preemptor}) only needed one node beside {idle_node}"
    )

    reclaimed = {victim_nodes[job_id] for job_id in preempted}

    # The contract itself. preempt.html promises the whole nodes of the
    # preempted jobs are reserved for the preemptor, and permits reserving
    # more than that, so this half is containment rather than equality.
    assert reclaimed <= sched_nodes, (
        f"Preemptor ({preemptor}) preempted {sorted(preempted)} on "
        f"{sorted(reclaimed)} but reserved {sorted(sched_nodes)}; the whole "
        f"nodes of a preempted job must be reserved for the preemptor"
    )

    # scheduled_nodes is documented as the nodes the job is expected to start
    # on. Containment alone does not catch the defect: the planned pair can
    # happen to hold the preempted node. In this layout the preemptor can only
    # start on the preempted node and the idle one, so anything else means
    # backfill kept the nodes it planned on instead of the ones it preempted.
    assert sched_nodes == reclaimed | {idle_node}, (
        f"Preemptor ({preemptor}) reserved {sorted(sched_nodes)}, but it "
        f"preempted {sorted(preempted)} on {sorted(reclaimed)} and the only "
        f"free node was {idle_node}"
    )

    # Backfill must not start a job queued behind the preemptor on its
    # reserved nodes. The victims hold every other node through their grace
    # time, so the only node the filler could start on is the idle one.
    filler = scenario.submit_filler()
    cycles_before = _backfill_cycles()
    for _ in atf.timer(fatal=True):
        # Count cycles before reading the state, so the state read covers
        # every cycle counted.
        cycles_run = _backfill_cycles() - cycles_before
        state = atf.get_job_parameter(filler, "JobState", quiet=True)
        assert state != "RUNNING", (
            f"Filler ({filler}) started on "
            f"{atf.get_job_parameter(filler, 'NodeList', default='')}, taking "
            f"a node reserved for the preemptor ({preemptor}): "
            f"{sorted(sched_nodes)}"
        )
        if cycles_run >= MIN_BACKFILL_CYCLES:
            break
