############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import re

import pytest

import atf

NODE_COUNT = 12


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create custom topology.conf and set weights")
    atf.require_nodes(NODE_COUNT, [("CPUs", 4)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TopologyPlugin", "topology/flat")

    # Mark topology.conf for teardown. The tree test writes the real content.
    atf.require_config_parameter("", "", source="topology")

    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def cleanup():
    """Undo per-test node state so tests do not depend on each other."""
    yield
    atf.cancel_all_jobs(quiet=True)
    atf.run_command(
        f"scontrol update NodeName=node[1-{NODE_COUNT}] State=RESUME Weight=1",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def node_indices(nodelist):
    """Return the numeric suffixes of a node list, in order.

    Auto-config names nodes node1..nodeN in configuration order, so the
    suffixes track the node record indices that contiguity is defined over.
    """
    return [int(re.sub(r"\D", "", name)) for name in atf.node_range_to_list(nodelist)]


def assert_contiguous(job_id):
    indices = node_indices(atf.get_job_parameter(job_id, "NodeList"))
    assert indices == list(
        range(indices[0], indices[0] + len(indices))
    ), f"{atf.get_job_parameter(job_id, 'NodeList')} is not a contiguous set"


def use_topology(plugin, content=None):
    """Switch the topology plugin, writing topology.conf first if needed."""
    if content is not None:
        conf = atf.properties["slurm-config-dir"] + "/topology.conf"
        atf.run_command(
            f"cat > {conf}",
            input=content,
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    atf.set_config_parameter("TopologyPlugin", plugin, restart=True)


def drain(nodes):
    atf.run_command(
        f"scontrol update NodeName={nodes} State=DRAIN Reason=test_150_10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def test_contiguous_honored_flat():
    """A --contiguous job under topology/flat gets consecutive nodes.

    node4 is drained, so the available nodes fall into two runs, node[1-3]
    and node[5-12]. Only the second is long enough for a 5 node job.
    """
    use_topology("topology/flat")
    drain("node4")

    job_id = atf.submit_job_sbatch(
        '--contiguous -N5 -n5 --mem=1 --wrap="sleep 60"', fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", timeout=60
    ), "Verify that the contiguous job started"

    assert_contiguous(job_id)
    assert min(node_indices(atf.get_job_parameter(job_id, "NodeList"))) > 4, (
        "Verify the job used the run above the drained node, "
        "which is the only one long enough"
    )


def test_contiguous_honored_tree():
    """topology/tree also honors --contiguous.

    The tree evaluator declines contiguous jobs, so eval_nodes() falls
    through to the consecutive-node evaluator, which enforces it. The man
    pages document this, so it is worth pinning down.
    """
    use_topology(
        "topology/tree",
        """
        SwitchName=s1 Nodes=node[1-6]
        SwitchName=s2 Nodes=node[7-12]
        SwitchName=root Switches=s1,s2
        """,
    )
    drain("node4")

    job_id = atf.submit_job_sbatch(
        '--contiguous -N5 -n5 --mem=1 --wrap="sleep 60"', fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", timeout=60
    ), "Verify that the contiguous job started under topology/tree"

    assert_contiguous(job_id)


def test_contiguous_ignored_block():
    """topology/block does not honor --contiguous.

    block sets trump_others, so eval_nodes() dispatches straight to the
    block evaluator and never reaches the consecutive-node evaluator. That
    evaluator never reads details_ptr->contiguous, so the request is
    dropped silently: no error, no warning, and the job lands exactly where
    an ordinary job would.

    This asserts the current behavior on purpose, so that the code and the
    --contiguous NOTE in sbatch(1) cannot drift apart. If block ever learns
    to honor contiguity this test is expected to fail; update it together
    with the man pages rather than deleting it.
    """
    use_topology(
        "topology/block",
        """
        BlockName=b1 Nodes=node[1-4]
        BlockName=b2 Nodes=node[5-8]
        BlockName=b3 Nodes=node[9-12]
        BlockSizes=4,8
        """,
    )
    drain("node2")

    # With node2 drained, b1 offers exactly three free nodes with a gap in
    # the middle, while b2 and b3 each offer four consecutive ones. A job
    # that honored contiguity would have to use b2 or b3.
    plain_id = atf.submit_job_sbatch('-N3 -n3 --mem=1 --wrap="sleep 60"', fatal=True)
    assert atf.wait_for_job_state(
        plain_id, "RUNNING", timeout=60
    ), "Verify that the ordinary job started"
    plain_nodes = atf.get_job_parameter(plain_id, "NodeList")
    atf.cancel_all_jobs(quiet=True)

    contiguous_id = atf.submit_job_sbatch(
        '--contiguous -N3 -n3 --mem=1 --wrap="sleep 60"', fatal=True
    )
    assert atf.wait_for_job_state(
        contiguous_id, "RUNNING", timeout=60
    ), "Verify that the contiguous job started"
    contiguous_nodes = atf.get_job_parameter(contiguous_id, "NodeList")

    assert contiguous_nodes == plain_nodes, (
        f"Verify --contiguous changed nothing under topology/block "
        f"(ordinary job got {plain_nodes}, contiguous job got {contiguous_nodes})"
    )

    indices = node_indices(contiguous_nodes)
    assert indices != list(range(indices[0], indices[0] + len(indices))), (
        f"Verify the allocation {contiguous_nodes} is not contiguous, which "
        "is the point of this test: the request was ignored"
    )


def test_contiguous_unsatisfiable_pends():
    """A --contiguous job with no run long enough must pend, not be placed.

    Draining node4 and node8 leaves runs of 3, 3 and 4 nodes, so a 6 node
    contiguous job cannot be satisfied. The job must stay pending rather
    than receive a non-contiguous allocation.
    """
    use_topology("topology/flat")
    drain("node4,node8")

    job_id = atf.submit_job_sbatch(
        '--contiguous -N6 -n6 --mem=1 --wrap="sleep 60"', fatal=True
    )
    assert not atf.wait_for_job_state(
        job_id, "RUNNING", timeout=15, xfail=True
    ), "Verify that the unsatisfiable contiguous job does not start"
    assert atf.wait_for_job_state(
        job_id, "PENDING"
    ), "Verify that the unsatisfiable contiguous job is pending"


def test_contiguous_sufficient_set_not_best_fit():
    """A sufficient set must still be found when it is not the best fit.

    The scan in _eval_nodes_consec() makes a set the new best fit on lower
    node weight regardless of whether it is large enough, so a set that is
    big enough can be displaced by a smaller one. The retry loop resolves
    this by removing the low weight set's nodes, after which the sufficient
    set wins. A scheduler shortcut that gives up as soon as the best fit is
    too small breaks this job.

    Layout: node7 drained, so node[1-6] (weight 10, big enough for 6 nodes)
    and node[8-12] (weight 5, only 5 nodes). A filler job leaves one CPU
    free on each of node[8-12] so that they sort lowest on available
    resources and are the ones the retry loop removes first, which makes the
    outcome deterministic rather than dependent on qsort tie ordering.
    """
    use_topology("topology/flat")
    drain("node7")
    atf.run_command(
        "scontrol update NodeName=node[1-6] Weight=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        "scontrol update NodeName=node[8-12] Weight=5",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    filler_id = atf.submit_job_sbatch(
        '-w node[8-12] -N5 --ntasks-per-node=3 --mem=1 --wrap="sleep 120"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        filler_id, "RUNNING", timeout=60
    ), "Verify that the filler job started"

    job_id = atf.submit_job_sbatch(
        '--contiguous -N6 -n6 --mem=1 --wrap="sleep 60"', fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", timeout=60
    ), "Verify that the job ran on the sufficient set that lost the best fit"

    assert_contiguous(job_id)
    assert node_indices(atf.get_job_parameter(job_id, "NodeList")) == list(
        range(1, 7)
    ), "Verify that the job used the higher weight set that was large enough"
