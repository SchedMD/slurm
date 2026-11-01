############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Draining a TRES must not shrink the node or cluster TRES totals.

A drained TRES is reported to accounting as a node event whose TRES column
names what was taken away (see test_190_9), and the slurmdbd rollup charges
that event as down time. The capacity it is charged against comes from a
separate cluster-registration event built by set_cluster_tres(), so the totals
must stay at *configured* capacity for every TRES. Subtracting a drain from the
totals as well would count the same TRES twice, leaving sreport under-reporting
both the cluster's capacity and its idle time.

set_cluster_tres() only republishes on a reconfigure (or a node add/delete), so
each test drains, asserts, reconfigures and asserts again -- a total that is
only wrong after the reconfigure is exactly the regression being guarded.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node1"  # require_nodes(1, ...) yields node1
NODE_CPUS = 4
NODE_MEM = 2048
NODE_GPUS = 2
# Fake GPU device UUIDs, in fake_gpus.conf device order.
GPU_A = "GPU-aaaaaaaa-0000-0000-0000-000000000000"
GPU_B = "GPU-bbbbbbbb-1111-1111-1111-111111111111"

DRAIN_MEM = 512


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25180: TRES drain and its accounting events added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="bin/sacctmgr",
        reason="Ticket 25180: TresReference event format field added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: GRES device slots keyed by UUID added in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU_Memory")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_accounting()
    atf.require_config_parameter_includes("AccountingStorageTRES", "gres/gpu")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_config_file("gres.conf", "Autodetect=full\n")
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (1, GPU_B))
    )
    atf.require_nodes(
        1,
        [
            ("Gres", f"gpu:a100:{NODE_GPUS}"),
            ("CPUs", NODE_CPUS),
            ("RealMemory", NODE_MEM),
        ],
    )
    atf.require_slurm_running()


@pytest.fixture
def resume_all():
    """Resume every TRES the test drained, so totals start clean each time."""

    yield

    for tres, selector in (
        ("cpu", "Index=ALL"),
        ("mem", f"Count={DRAIN_MEM}"),
        ("gres/gpu", "Index=ALL"),
    ):
        # Not fatal: a test may have left only some of these drained.
        atf.update_tres_drain(NODE, tres, selector, action="Resume", quiet=True)
    for _ in atf.timer(fatal=True):
        drained = atf.get_node_tres_drained(NODE)
        if not drained:
            break


def _tres_dict(tres_str):
    """Parse a 'cpu=4,mem=2G,gres/gpu=2' TRES string into a dict."""

    parsed = {}
    for item in (tres_str or "").split(","):
        name, sep, count = item.partition("=")
        if sep:
            parsed[name.strip()] = count.strip()
    return parsed


def _node_tres():
    """Return the node's configured TRES (CfgTRES) as a dict."""

    return _tres_dict(atf.get_node_parameter(NODE, "tres"))


def _cluster_tres():
    """Return the cluster's registered TRES as a dict."""

    cluster = atf.get_config_parameter("ClusterName", quiet=True)
    output = atf.run_command_output(
        "sacctmgr -n -P show cluster format=cluster,tres",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    for line in output.splitlines():
        name, sep, tres = line.partition("|")
        if sep and name.strip().lower() == (cluster or "").lower():
            return _tres_dict(tres)
    return {}


def _drain_one_of_each():
    """Drain a CPU, some memory and a GPU, and wait for the node to go DEGRADED."""

    for tres, selector in (
        ("cpu", "Index=0"),
        ("mem", f"Count={DRAIN_MEM}"),
        ("gres/gpu", "Index=0"),
    ):
        result = atf.update_tres_drain(NODE, tres, selector, reason="tres_totals")
        assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(
        NODE, "DEGRADED"
    ), f"node should be DEGRADED after the drains: {atf.get_node_tres_drained(NODE)!r}"


def _reconfigure():
    """Republish the cluster TRES, which set_cluster_tres() only does here.

    The reconfigured slurmctld sends the new totals to slurmdbd and waits for
    the reply before it notifies its parent, so they are stored by the time
    scontrol returns. Node events still go through the agent queue, so wait
    for it to drain before anything reads the database.
    """

    atf.run_command(
        "scontrol reconfigure",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    for _ in atf.timer(fatal=True):
        state = atf.get_node_parameter(NODE, "state") or ""
        if "DEGRADED" in state:
            break

    atf.wait_for_dbd_agent_drained(fatal=True)


def test_node_cfgtres_is_configured_capacity_while_drained(resume_all):
    """CfgTRES keeps reporting configured capacity for cpu, mem and gres.

    CfgTRES is built straight from the tres_cnt[] array that set_cluster_tres()
    fills and that the cluster registration is summed from, so this is the
    per-node half of the same contract.
    """
    before = _node_tres()
    assert before, "could not read the node's CfgTRES"

    _drain_one_of_each()

    # The drain is visible as drained TRES...
    drained = atf.get_node_tres_drained(NODE) or ""
    assert re.search(r"\bcpu\b", drained), f"CPU drain not reported: {drained!r}"
    assert re.search(r"\bmem\b", drained), f"memory drain not reported: {drained!r}"
    assert "gpu" in drained, f"GPU drain not reported: {drained!r}"

    # ...but must not have shrunk the configured totals, before or after the
    # reconfigure that rebuilds them.
    assert _node_tres() == before, "CfgTRES changed when the TRES were drained"
    _reconfigure()
    assert _node_tres() == before, "CfgTRES changed after reconfigure while drained"

    # Spelled out per TRES so a failure names the offender rather than diffing
    # two long strings.
    after = _node_tres()
    assert after.get("cpu") == str(NODE_CPUS), f"cpu total changed: {after!r}"
    assert after.get("gres/gpu") == str(NODE_GPUS), f"gres/gpu total changed: {after!r}"
    assert "mem" in after and after["mem"] == before["mem"], f"mem changed: {after!r}"
    assert after.get("billing") == before.get("billing"), f"billing changed: {after!r}"


def test_cluster_tres_is_configured_capacity_while_drained(resume_all):
    """The cluster registration TRES stays at configured capacity.

    This is the value the rollup uses as total capacity. If a drain were
    subtracted here as well as charged as event down time, the drained TRES
    would be counted twice.

    Node CfgTRES and the cluster registration are filled separately, so keep
    this even though test_node_cfgtres_is_configured_capacity_while_drained
    already covers the per-node totals.
    """
    before = _cluster_tres()
    assert before, "could not read the cluster's registered TRES"

    _drain_one_of_each()
    _reconfigure()

    after = _cluster_tres()
    for tres in ("cpu", "mem", "gres/gpu", "billing"):
        if tres in before:
            assert after.get(tres) == before[tres], (
                f"cluster {tres} changed from {before[tres]!r} to "
                f"{after.get(tres)!r} while drained"
            )


def test_drain_is_charged_as_an_event_not_as_lost_capacity(resume_all):
    """The drain is recorded in the event's TRES while the totals stay whole.

    Both halves of the contract in one place: the event names what was drained,
    and the totals it will be charged against are untouched. Resuming closes the
    event and still leaves the totals alone.
    """
    node_before = _node_tres()
    assert node_before, "could not read the node's CfgTRES"
    cluster_before = _cluster_tres()
    assert cluster_before, "could not read the cluster's registered TRES"

    _drain_one_of_each()

    for _ in atf.timer(fatal=True):
        tres = atf.get_open_node_event_tres(NODE)
        if (
            tres
            and re.search("(?:^|,)cpu=1(?:,|$)", tres)
            and re.search(f"(?:^|,)mem={DRAIN_MEM}M?(?:,|$)", tres)
            and re.search("(?:^|,)gres/gpu=1(?:,|$)", tres)
        ):
            break

    _reconfigure()
    assert _node_tres() == node_before, "CfgTRES shrank while the event was open"
    assert (
        _cluster_tres() == cluster_before
    ), "cluster TRES shrank while the event was open"

    for tres, selector in (
        ("cpu", "Index=0"),
        ("mem", f"Count={DRAIN_MEM}"),
        ("gres/gpu", "Index=0"),
    ):
        result = atf.update_tres_drain(NODE, tres, selector, action="Resume")
        assert result["exit_code"] == 0, result

    for _ in atf.timer(fatal=True):
        event = atf.get_open_node_event(NODE)
        if event is None:
            break

    assert (
        _node_tres() == node_before
    ), "CfgTRES did not return to configured capacity after resume"
