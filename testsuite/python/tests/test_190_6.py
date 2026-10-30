############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Count-only GRES drain tests using bandwidth:lustre.

Tests that the scheduler correctly enforces per-node count-only GRES drain
across a discontinuous 4-node cluster (node99=skip, node100=NODE_A,
node101=NODE_GOOD, node102=NODE_B).  All tests use bandwidth:lustre as the
representative count-only GRES; GPU GRES is always topology-based and cannot
be used with Flags=CountOnly.

Each test writes gres.conf and restarts slurm (same pattern as test_190_7.py).
This avoids MaxNodeCount exhaustion from static nodes left by previous test
modules (several call require_nodes(4,...)) and ensures a clean GRES state for
every test.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

# Node names need to be in the form of nodeNN for the s2n variant. The nodes
# are deliberately discontinuous: DYN_NODE_SKIP is registered but never
# targeted (it absorbs the scheduler's first-choice bias) and DYN_NODE_GOOD
# sits between the two drain nodes, so a job pinned to DYN_NODE/DYN_NODE_B
# exercises the discontinuous-bitmap paths.
DYN_NODE_SKIP = "node99"  # index 0: registered but not used in jobs
DYN_NODE = "node100"  # index 1: primary drain node (NODE_A)
DYN_NODE_GOOD = "node101"  # index 2: healthy node between DYN_NODE and _B
DYN_NODE_B = "node102"  # index 3: secondary drain node (NODE_B)

# Each dynamic slurmd on the same host must listen on a distinct port. Without
# explicit Port=, all four slurmds default to SlurmdPort (e.g. 6818) and only
# the first one can bind; subsequent nodes fail to register.
PORT_SKIP = 61599
PORT_A = 61600
PORT_GOOD = 61601
PORT_B = 61602

# 4 bandwidth:lustre TRES units per node -- enough headroom for drain+job tests.
BW_GRES_CONF = (
    "NodeName=node[99-102]"
    " AutoDetect=off Name=bandwidth Type=lustre Count=4 Flags=CountOnly\n"
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25180: GRES drain added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: node MaxDrainedTres added in 26.11",
    )
    # Use a larger MaxNodeCount than the 4-node minimum so that static nodes
    # from previous test modules (several call require_nodes(4,...)) do not
    # exhaust the limit before dynamic nodes can register.
    atf.require_config_parameter("MaxNodeCount", 16)
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter_includes("SlurmctldParameters", "cloud_reg_addrs")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "bandwidth")
    atf.require_slurm_running()


@pytest.fixture
def bandwidth_dynamic_nodes(dynamic_slurmds):
    """Start the four bandwidth:lustre dynamic nodes on a clean cluster.

    Yields a callable returning the (skip, a, good, b) node names.
    ``node_a_extra_conf`` appends per-node configuration (e.g. a
    MaxDrainedTres limit) to DYN_NODE only.
    """

    def _start(node_a_extra_conf=""):
        atf.require_config_file("gres.conf", BW_GRES_CONF)
        atf.restart_slurm(clean=True, quiet=True)

        names = []
        for node_name, port, extra_conf in (
            (DYN_NODE_SKIP, PORT_SKIP, ""),
            (DYN_NODE, PORT_A, node_a_extra_conf),
            (DYN_NODE_GOOD, PORT_GOOD, ""),
            (DYN_NODE_B, PORT_B, ""),
        ):
            conf = f"Port={port} Gres=bandwidth:lustre:4"
            if extra_conf:
                conf += f" {extra_conf}"
            names.append(dynamic_slurmds(node_name, conf))
        return tuple(names)

    return _start


def test_count_only_gres_multinode_symmetric_drain_blocks_drained_pair(
    bandwidth_dynamic_nodes,
):
    """Symmetric bandwidth drain on the NODE_A/NODE_B pair blocks jobs on that pair.

    Four nodes are registered: DYN_NODE_SKIP (index 0, bias guard), DYN_NODE
    (NODE_A, index 1), DYN_NODE_GOOD (index 2, healthy between), DYN_NODE_B
    (NODE_B, index 3).  Each has 4 bandwidth:lustre TRES units.

    Draining all 4 TRES units on NODE_A and NODE_B leaves 0 available on each.
    A job requiring 1 bandwidth:lustre per task (1 task per node) pinned to
    the discontinuous pair (node100, node102) stays PENDING; the identical
    job pinned to the healthy pair (node99, node101) runs.
    """
    node_skip, node_a, node_good, node_b = bandwidth_dynamic_nodes()

    for node in (node_a, node_b):
        result = atf.update_tres_drain(
            node, "gres/bandwidth:lustre", "Count=4", reason="sym-drain"
        )
        assert result["exit_code"] == 0, f"drain on {node}: {result}"
        assert atf.wait_for_node_state(
            node, "DEGRADED"
        ), f"{node} should be DEGRADED after bandwidth drain"

    job_drained = atf.submit_job_sbatch(
        f"--nodes=2 -w {node_a},{node_b}"
        f" --gres=bandwidth:lustre:1 --wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.job_stays_pending(
        job_drained
    ), "job on drained pair must stay PENDING: both nodes have 0 bandwidth"

    job_healthy = atf.submit_job_sbatch(
        f"--nodes=2 -w {node_skip},{node_good}"
        f" --gres=bandwidth:lustre:1 --wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_healthy, "RUNNING"
    ), "job on healthy pair must run: node99 and node101 have 4 bandwidth each"


def test_count_only_gres_multinode_fully_drained_node_excluded(
    bandwidth_dynamic_nodes,
):
    """A fully-drained node cannot satisfy per-task bandwidth in a spanning job.

    NODE_A's bandwidth is drained to 0; NODE_B is healthy (4 TRES units).  A
    2-node job pinned to the discontinuous pair (node100, node102) stays
    PENDING because node100 cannot supply even 1 TRES unit.  Resuming NODE_A's
    drain makes the pair viable and the job starts.
    """
    _, node_a, _, node_b = bandwidth_dynamic_nodes()

    result = atf.update_tres_drain(
        node_a, "gres/bandwidth:lustre", "Count=4", reason="full-drain"
    )
    assert result["exit_code"] == 0, result
    assert atf.wait_for_node_state(
        node_a, "DEGRADED"
    ), f"{node_a} should be DEGRADED after bandwidth drain"
    assert "DEGRADED" not in (
        atf.get_node_parameter(node_b, "state") or ""
    ), f"{node_b} must not be DEGRADED -- only {node_a} was drained"

    job_id = atf.submit_job_sbatch(
        f"--nodes=2 -w {node_a},{node_b}"
        f" --gres=bandwidth:lustre:1 --wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.job_stays_pending(
        job_id
    ), f"job must stay PENDING: {node_a} has 0 bandwidth"

    result = atf.update_tres_drain(
        node_a, "gres/bandwidth:lustre", "Count=4", action="Resume"
    )
    assert result["exit_code"] == 0, result
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "job should start once NODE_A bandwidth drain is resumed"


def test_count_only_gres_exclusive_spanning_job_blocked_by_drain(
    bandwidth_dynamic_nodes,
):
    """Exclusive spanning job blocked when both nodes have all bandwidth drained.

    Combines --exclusive with per-node count-only GRES drain.  The single-node
    GPU test (test_190_7::test_exclusive_whole_node_excludes_drained_gpu)
    covers a drained device; this test covers a count-only drain with the same
    exclusive-spanning-job structure.
    """
    _, node_a, _, node_b = bandwidth_dynamic_nodes()

    for node in (node_a, node_b):
        result = atf.update_tres_drain(
            node, "gres/bandwidth:lustre", "Count=4", reason="excl-drain"
        )
        assert result["exit_code"] == 0, f"drain on {node}: {result}"
        assert atf.wait_for_node_state(
            node, "DEGRADED"
        ), f"{node} should be DEGRADED after bandwidth drain"

    job_id = atf.submit_job_sbatch(
        f"--exclusive --nodes=2 -w {node_a},{node_b}"
        f" --gres=bandwidth:lustre:1 --wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.job_stays_pending(
        job_id
    ), "exclusive spanning job must stay PENDING: both nodes have 0 bandwidth"

    for node in (node_a, node_b):
        result = atf.update_tres_drain(
            node, "gres/bandwidth:lustre", "Count=4", action="Resume"
        )
        assert result["exit_code"] == 0, f"resume on {node}: {result}"
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "exclusive spanning job should run once bandwidth drain is resumed on both nodes"


def test_count_only_gres_multinode_running_job_not_interrupted(
    bandwidth_dynamic_nodes,
):
    """Draining bandwidth on one node mid-flight does not interrupt a running spanning job.

    Mirror of test_190_7::test_gres_drain_allows_allocated_gpu for count-only GRES.
    A spanning job that holds bandwidth from both nodes continues running after
    an admin drain is applied to NODE_A.
    """
    _, node_a, _, node_b = bandwidth_dynamic_nodes()

    job_id = atf.submit_job_sbatch(
        f"--nodes=2 -w {node_a},{node_b} --gres=bandwidth:lustre:1 --wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "spanning bandwidth job should reach RUNNING before drain"

    # Drain bandwidth on NODE_A while the job is running.
    result = atf.update_tres_drain(
        node_a, "gres/bandwidth:lustre", "Count=2", reason="mid-run"
    )
    assert result["exit_code"] == 0, result
    assert atf.wait_for_node_state(
        node_a, "DEGRADED"
    ), f"{node_a} should be DEGRADED after mid-run drain"

    job_state = atf.get_job_parameter(job_id, "JobState", quiet=True)
    assert (
        job_state == "RUNNING"
    ), f"spanning job must still be RUNNING after draining {node_a}: got {job_state!r}"


def test_count_only_gres_multinode_partial_drain_per_node(bandwidth_dynamic_nodes):
    """Asymmetric drain amounts on NODE_A and NODE_B are tracked independently.

    NODE_A: drain 2 of 4 -> 2 remain available.
    NODE_B: drain 3 of 4 -> 1 remains available.
    A job needing 2 per node stays PENDING (NODE_B has only 1).
    Partially resuming 2 on NODE_B (-> 3 available) lets the job start while
    NODE_B stays DEGRADED (1 still drained).
    """
    _, node_a, _, node_b = bandwidth_dynamic_nodes()

    for node, cnt in ((node_a, 2), (node_b, 3)):
        result = atf.update_tres_drain(
            node, "gres/bandwidth:lustre", f"Count={cnt}", reason="partial"
        )
        assert result["exit_code"] == 0, f"drain on {node}: {result}"
        assert atf.wait_for_node_state(node, "DEGRADED"), f"{node} should be DEGRADED"

    # 2 per node: NODE_A has 2 (OK), NODE_B has 1 (insufficient) -> PENDING.
    job_id = atf.submit_job_sbatch(
        f"--nodes=2 -w {node_a},{node_b} --gres=bandwidth:lustre:2 --wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.job_stays_pending(
        job_id
    ), "job must stay PENDING: NODE_B has only 1 bandwidth available (needs 2)"

    # Resume 2 on NODE_B: 3 now available; NODE_B still DEGRADED (1 drained).
    result = atf.update_tres_drain(
        node_b, "gres/bandwidth:lustre", "Count=2", action="Resume"
    )
    assert result["exit_code"] == 0, result

    assert (
        atf.get_node_tres_drained(node_b) == "bandwidth:lustre:1(partial)"
    ), "NODE_B should still have 1 TRES unit drained"

    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "job should start: NODE_A has 2 available, NODE_B now has 3 available"


def test_count_only_gres_multinode_persists_across_slurmctld_restart(
    bandwidth_dynamic_nodes,
):
    """Bandwidth drain on both nodes survives a non-clean slurmctld restart.

    After restart the dynamic slurmds re-register; the controller restores their
    drain state from its state files, leaving both nodes DEGRADED.
    """
    _, node_a, _, node_b = bandwidth_dynamic_nodes()

    expected = {}
    for node, cnt in ((node_a, 2), (node_b, 3)):
        result = atf.update_tres_drain(
            node, "gres/bandwidth:lustre", f"Count={cnt}", reason="persist"
        )
        assert result["exit_code"] == 0, f"drain on {node}: {result}"
        expected[node] = f"bandwidth:lustre:{cnt}(persist)"

    for node in (node_a, node_b):
        for _ in atf.timer(fatal=True):
            if atf.get_node_tres_drained(node) == expected[node]:
                break

    atf.restart_slurmctld(clean=False, quiet=True)

    for node in (node_a, node_b):
        for _ in atf.timer(fatal=True):
            if atf.get_node_tres_drained(node) == expected[node]:
                break
        assert atf.wait_for_node_state(
            node, "DEGRADED"
        ), f"{node} should remain DEGRADED after controller restart"


def test_count_only_gres_multinode_over_capacity_capped(bandwidth_dynamic_nodes):
    """Draining more than a node's total bandwidth is accepted and capped at the total.

    Requesting CNT:99 (>>4) on both nodes is accepted; the effective drain is
    capped at 4, leaving 0 available.  Resuming with the same over-limit amount
    clears the drain on both nodes.
    """
    _, node_a, _, node_b = bandwidth_dynamic_nodes()

    for node in (node_a, node_b):
        result = atf.update_tres_drain(
            node, "gres/bandwidth:lustre", "Count=99", reason="over-cap"
        )
        assert (
            result["exit_code"] == 0
        ), f"over-capacity drain on {node} should be accepted"
        assert atf.wait_for_node_state(
            node, "DEGRADED"
        ), f"{node} should be DEGRADED after over-capacity drain"

    # Drain is capped at 4 -> 0 available; any bandwidth job on the pair is PENDING.
    job_id = atf.submit_job_sbatch(
        f"--nodes=2 -w {node_a},{node_b} --gres=bandwidth:lustre:1 --wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.job_stays_pending(
        job_id
    ), "job must stay PENDING: over-capacity drain leaves 0 bandwidth on each node"

    # Resuming the same over-limit amount clears the drain on both nodes.
    for node in (node_a, node_b):
        result = atf.update_tres_drain(
            node, "gres/bandwidth:lustre", "Count=99", action="Resume"
        )
        assert result["exit_code"] == 0, f"resume on {node}: {result}"
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "job should start once over-capacity drains are cleared on both nodes"


def test_count_only_gres_multinode_max_drained_tres_triggers_drain(
    bandwidth_dynamic_nodes,
):
    """MaxDrainedTres triggers whole-node DRAIN on NODE_A independently of NODE_B.

    NODE_A is started with MaxDrainedTres=gres/bandwidth:lustre:2.  Draining 3
    TRES units (> 2) exceeds the limit and transitions NODE_A to DRAIN.  NODE_B
    has no limit; draining 3 TRES units leaves it DEGRADED only.
    """
    _, node_a, _, node_b = bandwidth_dynamic_nodes(
        node_a_extra_conf="MaxDrainedTres=gres/bandwidth:lustre:2"
    )

    # Drain 3 on NODE_A (> MaxDrainedTres of 2) -> triggers whole-node DRAIN.
    result = atf.update_tres_drain(
        node_a, "gres/bandwidth:lustre", "Count=3", reason="over-limit"
    )
    assert result["exit_code"] == 0, result
    assert atf.wait_for_node_state(
        node_a, "DRAIN"
    ), f"{node_a} must enter DRAIN when drain (3) exceeds MaxDrainedTres (2)"
    assert (
        atf.get_node_parameter(node_a, "reason")
        == "bandwidth:lustre has 3 drained, exceeding MaxDrainedTres limit of 2"
    ), atf.get_node_parameter(node_a, "reason")

    # NODE_B has no MaxDrainedTres; drain 3 -> DEGRADED only.
    result = atf.update_tres_drain(
        node_b, "gres/bandwidth:lustre", "Count=3", reason="no-limit"
    )
    assert result["exit_code"] == 0, result
    assert atf.wait_for_node_state(
        node_b, "DEGRADED"
    ), f"{node_b} should be DEGRADED (no MaxDrainedTres limit)"
    assert "DRAIN" not in (
        atf.get_node_parameter(node_b, "state") or ""
    ), f"{node_b} must not enter DRAIN -- it has no MaxDrainedTres limit"


def test_count_only_gres_multinode_admin_drain_preserved_across_slurmd_rereg(
    bandwidth_dynamic_nodes,
):
    """Admin bandwidth drain survives killing and restarting a node's slurmd.

    The drain record lives in slurmctld state.  When a dynamic slurmd is killed
    and re-registers, the controller re-applies the stored drain so the node
    returns DEGRADED with the same DrainedTRES as before.  NODE_B (not
    restarted) must remain undisturbed throughout.
    """
    _, node_a, _, node_b = bandwidth_dynamic_nodes()

    expected = {}
    for node, cnt in ((node_a, 2), (node_b, 3)):
        result = atf.update_tres_drain(
            node, "gres/bandwidth:lustre", f"Count={cnt}", reason="rereg-test"
        )
        assert result["exit_code"] == 0, f"drain on {node}: {result}"
        expected[node] = f"bandwidth:lustre:{cnt}(rereg-test)"

    for node in (node_a, node_b):
        for _ in atf.timer(fatal=True):
            if atf.get_node_tres_drained(node) == expected[node]:
                break

    # Stop NODE_A's slurmd, keeping the controller's node record so the restart
    # is a true re-registration against the stored drain state.
    atf.stop_slurmd(node_a, quiet=True)

    # Re-register NODE_A with the same configuration.
    atf.start_dynamic_slurmd(
        node_a,
        extra_conf=f"Port={PORT_A} Gres=bandwidth:lustre:4",
    )

    # Drain must be preserved on NODE_A after re-registration.
    for _ in atf.timer(fatal=True):
        if atf.get_node_tres_drained(node_a) == expected[node_a]:
            break
    assert "DEGRADED" in (
        atf.get_node_parameter(node_a, "state") or ""
    ), f"{node_a} must be DEGRADED after re-registration (drain preserved)"

    # NODE_B was not touched; its drain must also be intact.
    for _ in atf.timer(fatal=True):
        if atf.get_node_tres_drained(node_b) == expected[node_b]:
            break


########################################################################
# Single-node count-only GRES drain/resume mechanics.
#
# These use a per-test gres.conf on DYN_NODE only (distinct type/count
# layouts), not the shared node[99-102] config.  They cover the count-only
# drain state machine (resume, typed drains, MaxDrainedTres, edge cases)
# rather than multinode scheduling.
########################################################################


def _single_node(dynamic_slurmds, gres_conf, extra_conf):
    """Write a single-node gres.conf, restart Slurm and start DYN_NODE."""
    atf.require_config_file("gres.conf", gres_conf)
    atf.restart_slurm(clean=True, quiet=True)
    return dynamic_slurmds(DYN_NODE, extra_conf)


def test_gres_drain_all_count_only_gres(dynamic_slurmds):
    """Drain all count-only GRES after a prior partial drain."""
    node = _single_node(
        dynamic_slurmds,
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=lustre Count=4 Flags=CountOnly\n",
        "Gres=bandwidth:lustre:4",
    )
    result = atf.update_tres_drain(node, "gres/bandwidth", "Count=1", reason="first")
    assert result["exit_code"] == 0, "command should succeed"
    assert re.search(
        r"DrainedTRES=.*bandwidth:lustre:1\(first\)", atf.get_node_detail(node)
    ), "DrainedTRES should show the drain"

    result = atf.update_tres_drain(node, "gres/bandwidth", "Count=ALL", reason="all")
    assert result["exit_code"] == 0, "command should succeed"
    detail = atf.get_node_detail(node)
    assert re.search(r"bandwidth:lustre:4\(all\)", detail), detail
    assert "(first)" not in detail, detail


def test_count_only_gres_resume(dynamic_slurmds):
    """A resume clears a count-only GRES drain (no device indexes)."""
    node = _single_node(
        dynamic_slurmds,
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=lustre Count=4 Flags=CountOnly\n",
        "Gres=bandwidth:lustre:4",
    )
    result = atf.update_tres_drain(node, "gres/bandwidth", "Count=2", reason="co")
    assert result["exit_code"] == 0, result
    assert re.search(
        r"DrainedTRES=.*bandwidth:lustre:2\(co\)", atf.get_node_detail(node)
    ), "DrainedTRES should show the drain"
    assert "DEGRADED" in (
        atf.get_node_parameter(node, "state") or ""
    ), "node should be DEGRADED"
    result = atf.update_tres_drain(node, "gres/bandwidth", "Count=ALL", action="Resume")
    assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(node, "DEGRADED", reverse=True), (
        "node still DEGRADED after count-only resume: "
        f"{atf.get_node_parameter(node, 'state')!r}"
    )
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)


def test_count_only_gres_partial_resume(dynamic_slurmds):
    """A resume subtracts from a count-only GRES drain by count."""
    node = _single_node(
        dynamic_slurmds,
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=lustre Count=4 Flags=CountOnly\n",
        "Gres=bandwidth:lustre:4",
    )
    result = atf.update_tres_drain(node, "gres/bandwidth", "Count=3", reason="co")
    assert result["exit_code"] == 0, result
    assert re.search(
        r"DrainedTRES=.*bandwidth:lustre:3\(co\)", atf.get_node_detail(node)
    ), "DrainedTRES should show the drain"
    assert "DEGRADED" in (
        atf.get_node_parameter(node, "state") or ""
    ), "node should be DEGRADED"

    result = atf.update_tres_drain(node, "gres/bandwidth", "Count=1", action="Resume")
    assert result["exit_code"] == 0, result
    assert re.search(
        r"DrainedTRES=.*bandwidth:lustre:2\(co\)", atf.get_node_detail(node)
    ), "DrainedTRES should show the drain"
    assert "DEGRADED" in (
        atf.get_node_parameter(node, "state") or ""
    ), "node should be DEGRADED"

    result = atf.update_tres_drain(node, "gres/bandwidth", "Count=2", action="Resume")
    assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(node, "DEGRADED", reverse=True), (
        "node still DEGRADED after partial count-only resume: "
        f"{atf.get_node_parameter(node, 'state')!r}"
    )
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)


def test_count_only_gres_set_drain_is_idempotent(dynamic_slurmds):
    """Action=SetDrain names the drained count, so repeating it changes nothing.

    A count is where SetDrain and Drain differ: Drain adds to what is already
    drained, while SetDrain replaces it. That makes SetDrain safe to re-run from
    configuration management, and lets one request lower a drain as well as
    raise it.
    """
    node = _single_node(
        dynamic_slurmds,
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=lustre Count=4 Flags=CountOnly\n",
        "Gres=bandwidth:lustre:4",
    )

    result = atf.update_tres_drain(
        node, "gres/bandwidth", "Count=2", action="SetDrain", reason="set2"
    )
    assert result["exit_code"] == 0, result
    assert re.search(
        r"DrainedTRES=.*bandwidth:lustre:2\(set2\)", atf.get_node_detail(node)
    ), atf.get_node_detail(node)
    assert "DEGRADED" in (
        atf.get_node_parameter(node, "state") or ""
    ), "node should be DEGRADED"

    # Repeating the same request is a no-op.
    before = atf.get_node_tres_drained(node)
    result = atf.update_tres_drain(
        node, "gres/bandwidth", "Count=2", action="SetDrain", reason="set2"
    )
    assert result["exit_code"] == 0, result
    assert atf.get_node_tres_drained(node) == before, atf.get_node_detail(node)

    # Action=Drain is cumulative, which is what makes the above meaningful.
    result = atf.update_tres_drain(node, "gres/bandwidth", "Count=1", reason="add1")
    assert result["exit_code"] == 0, result
    assert re.search(
        r"DrainedTRES=.*bandwidth:lustre:3\(add1\)", atf.get_node_detail(node)
    ), atf.get_node_detail(node)

    # SetDrain can lower the drained count again, which Drain cannot.
    result = atf.update_tres_drain(
        node, "gres/bandwidth", "Count=1", action="SetDrain", reason="set1"
    )
    assert result["exit_code"] == 0, result
    assert re.search(
        r"DrainedTRES=.*bandwidth:lustre:1\(set1\)", atf.get_node_detail(node)
    ), atf.get_node_detail(node)

    # Count=0 names an empty drained set, resuming everything.
    result = atf.update_tres_drain(
        node, "gres/bandwidth", "Count=0", action="SetDrain", reason="none"
    )
    assert result["exit_code"] == 0, result
    assert atf.wait_for_node_state(node, "DEGRADED", reverse=True), (
        "node still DEGRADED after SetDrain Count=0: "
        f"{atf.get_node_parameter(node, 'state')!r}"
    )
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)


def test_count_only_gres_set_drain_is_scoped_to_the_named_type(dynamic_slurmds):
    """SetDrain of one type leaves a different type and a different TRES.

    SetDrain resumes whatever of the named TRES it does not list. That must
    not reach a different type of the same GRES, or a different TRES.
    """
    node = _single_node(
        dynamic_slurmds,
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=alpha Count=2 Flags=CountOnly\n"
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=zeta Count=2 Flags=CountOnly\n",
        "Gres=bandwidth:alpha:2,bandwidth:zeta:2",
    )
    assert (
        atf.update_tres_drain(node, "gres/bandwidth:alpha", "Count=2", reason="alpha")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    assert (
        atf.update_tres_drain(node, "gres/bandwidth:zeta", "Count=2", reason="zeta")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    assert (
        atf.update_tres_drain(node, "cpu", "Index=0", reason="cpu0")["exit_code"] == 0
    ), "drain update should succeed"

    result = atf.update_tres_drain(
        node,
        "gres/bandwidth:alpha",
        "Count=1",
        action="SetDrain",
        reason="alpha_set",
    )
    assert result["exit_code"] == 0, result
    drain = atf.get_node_tres_drained(node) or ""
    assert re.search(r"bandwidth:alpha:1\(alpha_set\)", drain), drain
    assert re.search(r"bandwidth:zeta:2\(zeta\)", drain), drain
    assert re.search(r"cpu\(IDX:0\)\(cpu0\)", drain), drain


def _bandwidth_drained(node):
    """Maps each drained bandwidth type on the node to (count, reason)."""
    drain = atf.get_node_tres_drained(node) or ""
    return {
        gres_type: (int(count), reason)
        for gres_type, count, reason in re.findall(
            r"bandwidth:(\w+):(\d+)\(([^)]*)\)", drain
        )
    }


def test_count_only_gres_type_aware_drain(dynamic_slurmds):
    """Count-only drains retain typed counts and a per-type reason.

    Each type keeps the reason it was drained with, so draining one type later
    does not relabel a type that was drained earlier for something else.
    """
    node = _single_node(
        dynamic_slurmds,
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=zeta Count=2 Flags=CountOnly\n"
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=alpha Count=2 Flags=CountOnly\n",
        "Gres=bandwidth:zeta:2,bandwidth:alpha:2",
    )
    result = atf.update_tres_drain(node, "gres/bandwidth", "Count=3", reason="first")
    assert result["exit_code"] == 0, result
    # Which type an untyped count takes first is not documented. Only that the
    # count is drained, one type fully and the other partly, with the reason.
    detail = atf.get_node_detail(node)
    drained = _bandwidth_drained(node)
    assert sorted(count for count, _ in drained.values()) == [1, 2], detail
    assert all(reason == "first" for _, reason in drained.values()), detail
    full, partial = sorted(drained, key=lambda t: drained[t][0], reverse=True)

    # The fully drained type should keep its job pending, not rejected,
    # because the type is still configured.
    pending_job = atf.submit_job_sbatch(
        f"-w {node} --gres=bandwidth:{full}:1 --wrap true", fatal=True
    )
    assert atf.job_stays_pending(pending_job), "drained type must keep its job pending"
    usable_job = atf.submit_job_sbatch(
        f"-w {node} --gres=bandwidth:{partial}:1 --wrap true", fatal=True
    )
    atf.wait_for_job_state(usable_job, "DONE", fatal=True, timeout=60)
    atf.cancel_jobs([pending_job])

    result = atf.update_tres_drain(
        node, f"gres/bandwidth:{partial}", "Count=1", reason="second"
    )
    assert result["exit_code"] == 0, result
    detail = atf.get_node_detail(node)
    # Only one type was named the second time, so the other keeps its reason.
    assert re.search(rf"bandwidth:{full}:2\(first\)", detail), detail
    assert re.search(rf"bandwidth:{partial}:2\(second\)", detail), detail

    result = atf.update_tres_drain(
        node, f"gres/bandwidth:{partial}", "Count=1", action="Resume"
    )
    assert result["exit_code"] == 0, result
    detail = atf.get_node_detail(node)
    assert re.search(rf"bandwidth:{partial}:1\(second\)", detail), detail

    # Again the type resumed first is not documented, only how many are left
    # and that whatever stays drained keeps the reason it was drained with.
    result = atf.update_tres_drain(node, "gres/bandwidth", "Count=2", action="Resume")
    assert result["exit_code"] == 0, result
    reasons = {full: "first", partial: "second"}
    drain = atf.get_node_tres_drained(node) or ""
    drained = _bandwidth_drained(node)
    assert sum(count for count, _ in drained.values()) == 1, drain
    assert all(reason == reasons[t] for t, (_, reason) in drained.items()), drain


def test_count_only_gres_typed_max_drained_tres(dynamic_slurmds):
    """A typed MaxDrainedTres limit applies to its count-only GRES type."""
    node = _single_node(
        dynamic_slurmds,
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=alpha Count=3 Flags=CountOnly\n"
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=zeta Count=3 Flags=CountOnly\n",
        (
            "Gres=bandwidth:alpha:3,bandwidth:zeta:3 "
            "MaxDrainedTres=gres/bandwidth:alpha:1"
        ),
    )
    result = atf.update_tres_drain(
        node, "gres/bandwidth:alpha", "Count=2", reason="typed_limit"
    )
    assert result["exit_code"] == 0, result
    assert atf.wait_for_node_state(
        node, "DRAIN"
    ), f"node did not drain after typed limit was exceeded: {atf.get_node_detail(node)!r}"
    assert (
        atf.get_node_parameter(node, "reason")
        == "bandwidth:alpha has 2 drained, exceeding MaxDrainedTres limit of 1"
    ), atf.get_node_parameter(node, "reason")


def test_count_only_gres_max_drained_tres_count_suffix(dynamic_slurmds):
    """A gres MaxDrainedTres count with a K suffix is that many times 1024.

    Draining exactly the limit keeps the node DEGRADED. One more drains it,
    and the reason gives the limit as the multiplied count.
    """
    node = _single_node(
        dynamic_slurmds,
        f"NodeName={DYN_NODE} AutoDetect=off Name=bandwidth Type=lustre Count=4096 Flags=CountOnly\n",
        "Gres=bandwidth:lustre:4096 MaxDrainedTres=gres/bandwidth:lustre:1K",
    )
    atf.update_tres_drain(
        node, "gres/bandwidth:lustre", "Count=1024", reason="at_limit", fatal=True
    )
    assert atf.wait_for_node_state(
        node, "DEGRADED"
    ), f"node should be DEGRADED at the limit: {atf.get_node_detail(node)!r}"
    assert "DRAIN" not in (
        atf.get_node_parameter(node, "state") or ""
    ), f"draining 1024 must not exceed a 1K limit: {atf.get_node_detail(node)!r}"

    atf.update_tres_drain(
        node, "gres/bandwidth:lustre", "Count=1", reason="over_limit", fatal=True
    )
    assert atf.wait_for_node_state(
        node, "DRAIN"
    ), f"node did not drain over the 1K limit: {atf.get_node_detail(node)!r}"
    assert (
        atf.get_node_parameter(node, "reason")
        == "bandwidth:lustre has 1025 drained, exceeding MaxDrainedTres limit of 1024"
    ), atf.get_node_parameter(node, "reason")


def test_show_drainedtres_lists_a_count_only_drain(dynamic_slurmds):
    """show drainedtres reports a count-only drain by Count, not Index."""
    node = _single_node(
        dynamic_slurmds,
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=alpha Count=4 Flags=CountOnly\n",
        "Gres=bandwidth:alpha:4",
    )
    atf.update_tres_drain(
        node, "gres/bandwidth:alpha", "Count=2", reason="bw_count", fatal=True
    )
    output = atf.run_command_output(
        f"scontrol show drainedtres NodeName={node}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    lines = [line for line in output.splitlines() if "Reason=bw_count" in line]
    assert len(lines) == 1, output
    assert "TresName=gres/bandwidth:alpha" in lines[0], output
    assert "Count=2" in lines[0], output
    assert "Index=" not in lines[0] and "Uuid=" not in lines[0], output


def test_count_only_gres_drain_edge_cases(dynamic_slurmds):
    """Count-only drain validation, all, and oversized counts are deterministic."""
    node = _single_node(
        dynamic_slurmds,
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=alpha Count=2 Flags=CountOnly\n"
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=zeta Count=2 Flags=CountOnly\n",
        "Gres=bandwidth:alpha:2,bandwidth:zeta:2",
    )
    for tres, selector, error in (
        # unknown type
        (
            "gres/bandwidth:omega",
            "Count=1",
            "Invalid GRES specification (with and without type identification)",
        ),
        # a count-only GRES has no device identity to index
        ("gres/bandwidth", "Index=0", "Invalid generic resource (gres) specification"),
        # no selector at all
        ("gres/bandwidth", "", "TresName requires Index, Uuid or Count"),
    ):
        result = atf.update_tres_drain(node, tres, selector, reason="invalid")
        assert result["exit_code"] != 0, result
        assert error in result["stderr"], result
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)

    result = atf.update_tres_drain(node, "gres/bandwidth", "Count=0", reason="zero")
    assert result["exit_code"] == 0, result
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)

    result = atf.update_tres_drain(
        node, "gres/bandwidth:alpha", "Count=ALL", reason="alpha_all"
    )
    assert result["exit_code"] == 0, result
    result = atf.update_tres_drain(
        node, "gres/bandwidth:zeta", "Count=99", reason="zeta_cap"
    )
    assert result["exit_code"] == 0, result
    detail = atf.get_node_detail(node)
    # Each type reports the reason it was actually drained with.
    assert re.search(r"bandwidth:alpha:2\(alpha_all\)", detail), detail
    assert re.search(r"bandwidth:zeta:2\(zeta_cap\)", detail), detail

    result = atf.update_tres_drain(
        node, "gres/bandwidth:alpha", "Count=ALL", action="Resume"
    )
    assert result["exit_code"] == 0, result
    result = atf.update_tres_drain(
        node, "gres/bandwidth:zeta", "Count=99", action="Resume"
    )
    assert result["exit_code"] == 0, result
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)
