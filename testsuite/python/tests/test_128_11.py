############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Preemption by jobs with a required or excluded node list (preempt/qos).

A pending job only preempts running jobs whose resources it can use. With a
required node list (--nodelist) or an excluded node list (--exclude) that
narrows down to:

- jobs on the nodes the pending job may be allocated;
- jobs holding a license the pending job needs, on any node
  (PreemptParameters=reclaim_licenses).

These tests check that such jobs are still preempted: the job on a required
node, the job on the free node of a list longer than the node count, the
job on the node left by an exclusion, a job spanning that node and an
excluded one, license holders anywhere, and the job on the extra node of a
pending job that needs more nodes than it requires (-N2 -w node, or -N1-2
with a task count that needs two nodes).
"""

import pytest

import atf

pytestmark = pytest.mark.slow

_QOS_LOW = "test_128_11_low"
_QOS_HIGH = "test_128_11_high"
_PARTITION = "test_128_11"
_LICENSE = "test_128_11_lic"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 25865: preemption candidates limited to usable nodes in 26.11",
    )
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter("PreemptType", "preempt/qos")
    atf.require_config_parameter("PreemptMode", "REQUEUE")
    atf.require_config_parameter_includes("PreemptParameters", "reclaim_licenses")
    atf.require_config_parameter("PreemptExemptTime", "0")
    atf.require_config_parameter("JobRequeue", "1")
    atf.require_config_parameter_includes("Licenses", f"{_LICENSE}:1")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_nodes(2, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    atf.require_config_parameter("PartitionName", {_PARTITION: {"Nodes": "ALL"}})
    atf.require_slurm_running()

    cluster = atf.get_config_parameter("ClusterName")
    user = atf.properties["test-user"]
    su = atf.properties["slurm-user"]

    atf.run_command(f"sacctmgr -i add qos {_QOS_LOW}", user=su, fatal=True)
    # "Preempt=" lists the QOSs *this* QOS can preempt, so it belongs on the
    # high-priority QOS, pointing at the low one.
    atf.run_command(
        f"sacctmgr -i add qos {_QOS_HIGH} Preempt={_QOS_LOW}", user=su, fatal=True
    )
    atf.run_command(
        f"sacctmgr -i add user {user} cluster={cluster} account=root",
        user=su,
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i modify user {user} where cluster={cluster} "
        f"set qos+={_QOS_LOW},{_QOS_HIGH}",
        user=su,
        fatal=True,
    )
    for qos in (_QOS_LOW, _QOS_HIGH):
        _wait_for_qos_grant(user, qos, _PARTITION)

    yield

    atf.run_command(
        f"sacctmgr -i modify user {user} where cluster={cluster} "
        f"set qos-={_QOS_LOW},{_QOS_HIGH}",
        user=su,
        quiet=True,
    )
    for qos in (_QOS_LOW, _QOS_HIGH):
        atf.run_command(f"sacctmgr -i remove qos {qos}", user=su, quiet=True)


@pytest.fixture(scope="module")
def nodes():
    """(node_a, node_b, others): two test nodes and the rest of the cluster.

    require_nodes() guarantees at least two nodes. Any further node in the
    partition would let the pending jobs below spread without preempting, so
    they exclude the others.
    """

    pair = atf.run_job_nodes(f"-N2 -p {_PARTITION} true", fatal=True)
    assert len(pair) == 2, f"Expected two allocated nodes, got {pair}"
    others = sorted(set(atf.get_nodes(quiet=True)) - set(pair))
    return pair[0], pair[1], others


def _exclude(*nodes):
    """The -x option excluding the given nodes, empty when there are none."""

    return f"-x {','.join(nodes)}" if nodes else ""


def _wait_for_qos_grant(user, qos, partition):
    """Block until slurmctld has picked up a QOS grant made via sacctmgr.

    The grant reaches slurmctld asynchronously through slurmdbd, so a submit
    issued right after it can be rejected with "Invalid qos specification".
    A trial sbatch --test-only exercises the same acceptance check.
    """

    for _ in atf.timer(fatal=True):
        result = atf.run_command(
            f"sbatch --test-only -p {partition} -q {qos} --wrap true",
            user=user,
            quiet=True,
        )
        if result["exit_code"] == 0:
            break


def _submit_running(nodes, qos, args):
    """Run a job on the given nodes that stays until cancelled or preempted."""

    job_id = atf.submit_job_sbatch(
        f"-N{len(nodes)} -w {','.join(nodes)} {args} -t5 -o /dev/null "
        f"-p {_PARTITION} -q {qos} --requeue --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    return job_id


def _submit_high(args):
    return atf.submit_job_sbatch(
        f"-t1 -o /dev/null -p {_PARTITION} -q {_QOS_HIGH} {args} "
        "--wrap 'sleep infinity'",
        fatal=True,
    )


def _assert_preempted_and_started(low, high, expected_nodes):
    assert atf.wait_for_job_state(
        high, "RUNNING"
    ), f"High-QOS job ({high}) never started; it should have preempted job {low}"
    allocated = atf.node_range_to_list(
        atf.get_job_parameter(high, "NodeList", quiet=True)
    )
    assert sorted(allocated) == sorted(
        expected_nodes
    ), f"High-QOS job ({high}) should run on {expected_nodes}, got {allocated}"
    assert atf.wait_for_job_state(
        low, "PENDING"
    ), f"Low-QOS job ({low}) should have been requeued by preemption"


def test_license_holder_on_other_node_is_preempted(nodes):
    """-N1 -w node_a with a license preempts the license holder on node_b."""

    node_a, node_b, _ = nodes
    low = _submit_running([node_b], _QOS_LOW, f"--exclusive -L {_LICENSE}:1")
    high = _submit_high(f"-N1 -w {node_a} --exclusive -L {_LICENSE}:1")
    _assert_preempted_and_started(low, high, [node_a])


def test_license_holder_on_excluded_node_is_preempted(nodes):
    """-N1 -x node_b with a license preempts the license holder on node_b."""

    node_a, node_b, others = nodes
    low = _submit_running([node_b], _QOS_LOW, f"--exclusive -L {_LICENSE}:1")
    high = _submit_high(f"-N1 {_exclude(node_b, *others)} --exclusive -L {_LICENSE}:1")
    _assert_preempted_and_started(low, high, [node_a])


def test_job_on_required_node_is_preempted(nodes):
    """-N1 -w node_a preempts the job on node_a, its whole allocation."""

    node_a, _, _ = nodes
    low = _submit_running([node_a], _QOS_LOW, "--exclusive")
    high = _submit_high(f"-N1 -w {node_a} --exclusive")
    _assert_preempted_and_started(low, high, [node_a])


def test_job_on_free_node_of_longer_list_is_preempted(nodes):
    """-N1 -w node_a,node_b preempts the job on node_b while node_a is held.

    A list longer than the node count means "allocate from this list". The
    job on node_a can not be preempted, so the one on node_b has to be.
    """

    node_a, node_b, _ = nodes
    _submit_running([node_a], _QOS_HIGH, "--exclusive")
    low = _submit_running([node_b], _QOS_LOW, "--exclusive")
    high = _submit_high(f"-N1 -w {node_a},{node_b} --exclusive")
    _assert_preempted_and_started(low, high, [node_b])


def test_job_on_node_left_by_exclusion_is_preempted(nodes):
    """-N1 -x node_b preempts the job on node_a, the only node left."""

    node_a, node_b, others = nodes
    low = _submit_running([node_a], _QOS_LOW, "--exclusive")
    high = _submit_high(f"-N1 {_exclude(node_b, *others)} --exclusive")
    _assert_preempted_and_started(low, high, [node_a])


def test_job_spanning_excluded_node_is_preempted(nodes):
    """-N1 -x node_b preempts a job running on node_a and the excluded node_b."""

    node_a, node_b, others = nodes
    low = _submit_running([node_a, node_b], _QOS_LOW, "--exclusive")
    high = _submit_high(f"-N1 {_exclude(node_b, *others)} --exclusive")
    _assert_preempted_and_started(low, high, [node_a])


def test_required_node_list_smaller_than_node_count_preempts(nodes):
    """-N2 -w node_a preempts on node_b to get its second node."""

    node_a, node_b, others = nodes
    low = _submit_running([node_b], _QOS_LOW, "--exclusive")
    high = _submit_high(f"-N2 -w {node_a} --exclusive {_exclude(*others)}")
    _assert_preempted_and_started(low, high, [node_a, node_b])


def test_growable_job_preempts_for_its_extra_node(nodes):
    """-N1-2 -w node_a with more tasks than node_a holds preempts on node_b."""

    node_a, node_b, others = nodes
    low = _submit_running([node_b], _QOS_LOW, "--exclusive")
    # One task more than node_a has CPUs needs a second node.
    tasks = int(atf.get_node_parameter(node_a, "cpus")) + 1
    high = _submit_high(f"-N1-2 -n{tasks} -w {node_a} {_exclude(*others)}")
    _assert_preempted_and_started(low, high, [node_a, node_b])
