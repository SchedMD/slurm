############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that renaming a block drains the nodes that referenced it.

slurmctld used to die when a node's saved topology string named a block that
no longer existed. It now drains those nodes with a reason of InvalidTopology
and keeps running. A module fixture renames block1 once, before the first
test, so node1 and node2 start out drained. Later tests recover those nodes
and rewrite topology.conf without restoring it, so no test is insulated
against what ran before it, although each is written to pass on its own.
"""

import re

import pytest

import atf

# test_150_5 owns 61500-61519, so start this module above that range.
PORT_BASE = 61520

NODES = [
    {"name": "node1", "topo": "default:block1", "port": PORT_BASE},
    {"name": "node2", "topo": "default:block1", "port": PORT_BASE + 1},
    {"name": "node3", "topo": "default:block2", "port": PORT_BASE + 2},
    {"name": "node4", "topo": "default:block2", "port": PORT_BASE + 3},
]

# block1 is renamed by the tests, block2 is left alone as a control.
RENAMED = ["node1", "node2"]
UNTOUCHED = ["node3", "node4"]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        reason="Issue 50932: draining a node whose topology string names a missing block was added in 26.11",
    )
    # The tests run the client commands, so an older release's binaries (as in
    # a mixed-version upgrade) lack the sacctmgr TresReference event field and
    # the scontrol State=RESUME_KEEP_TRES, and 25.05 cannot print the node state
    # transition error.
    atf.require_version(
        (26, 11),
        component="bin/sacctmgr",
        reason="Ticket 25180: TresReference event format field added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="State=RESUME_KEEP_TRES was added in 26.11",
    )

    atf.require_accounting()
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter("MaxNodeCount", 8)
    atf.require_config_parameter("TopologyPlugin", "topology/block")
    # The reboot and cancel_reboot cases need a RebootProgram.
    atf.require_config_parameter("RebootProgram", "/bin/true")
    # test_power_down_keeps_the_drain powers a node down.
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendTime", "INFINITE")
    atf.require_config_parameter("SuspendTimeout", 5)
    atf.require_config_parameter_includes("SlurmctldParameters", "idle_on_node_suspend")
    atf.require_config_file("topology.conf", topology_conf("block1", "block2"))

    atf.require_slurm_running()


@pytest.fixture(scope="module", autouse=True)
def dynamic_nodes(setup):
    for node in NODES:
        start_dynamic_slurmd(node)

    yield

    for node in NODES:
        # A live slurmd would re-register and resurrect the deleted record.
        atf.stop_slurmd(node["name"])
        atf.delete_node(node["name"])


@pytest.fixture(scope="module", autouse=True)
def renamed_block(dynamic_nodes):
    """Rename block1 once, so node1 and node2 start out drained."""
    atf.require_config_file("topology.conf", topology_conf("block4", "block2"))
    reconfigure()
    assert atf.wait_for_node_state(
        RENAMED, "DRAIN"
    ), "Nodes under the renamed block should be drained"


def topology_conf(*block_names):
    """Return a topology.conf defining the given blocks, in order."""
    lines = [f"BlockName={name}" for name in block_names]
    lines.append("BlockSizes=2")
    return "\n".join(lines) + "\n"


def start_dynamic_slurmd(node, args="", wait="idle"):
    """Start a dynamic-normal slurmd with the node's --conf topology."""
    atf.start_slurmd(
        node["name"],
        f"-Z {args} --conf 'Port={node['port']} topology={node['topo']}'",
        wait=wait,
    )


def reconfigure():
    """Reconfigure slurmctld and assert that it survived.

    No wait is needed afterwards. A reconfigure re-execs slurmctld, and the
    old process only answers this scontrol once the new one has finished
    read_slurm_conf(), so every node whose topology string stopped resolving
    is already drained by the time this returns.
    """
    assert (
        atf.run_command_exit("scontrol reconfigure", user=atf.properties["slurm-user"])
        == 0
    ), "slurmctld should survive the reconfigure"


def blocks_for_node(name):
    """Return the set of blocks in the default topology that list the node.

    Aggregated blocks are ignored; only the leaf BlockName= lines place
    nodes.
    """
    blocks = set()
    output = atf.run_command_output("scontrol show topology default", fatal=True)
    # scontrol only writes to stderr and still exits 0 when it cannot load a
    # topology, so fatal=True does not catch that and an empty output would
    # make an "is in no block" assertion pass vacuously.
    assert output != "", "scontrol should be able to show the default topology"
    for line in output.splitlines():
        match = re.match(r"BlockName=(\S+) .*Nodes=(\S+)", line)
        if match and name in atf.node_range_to_list(match.group(2)):
            blocks.add(match.group(1))
    return blocks


def update_node(name, parameters):
    """Run scontrol update on a node and return the command result."""
    return atf.run_command(
        f"scontrol update nodename={name} {parameters}",
        user=atf.properties["slurm-user"],
    )


def assert_resume_refused(name):
    """Assert that the node refuses State=RESUME for its node state."""
    result = update_node(name, "state=resume")
    assert result["exit_code"] != 0, f"{name} should refuse state=resume"
    assert (
        "Invalid node state transition" in result["stderr"]
    ), f"{name} should be refused for the node state, not another fault"


def wait_for_open_event(name, reason):
    """Wait until the node has an open accounting event with this reason."""
    for _ in atf.timer():
        if atf.get_open_node_event_reason(name) == reason:
            break
    else:
        pytest.fail(f"{name} should have an open {reason} node event")


def test_block_rename_drains_nodes():
    """Renaming a block drains only the nodes that referenced it."""

    nodes = atf.get_nodes()
    for name in RENAMED:
        assert (
            "DRAIN" in nodes[name]["state"]
        ), f"{name} should be drained after its block was renamed"
        assert (
            nodes[name]["reason"] == "InvalidTopology"
        ), f"{name} should say why it was drained"

    # The drain must also reach accounting as an open node event.
    for name in RENAMED:
        wait_for_open_event(name, "InvalidTopology")

    # block2 was not renamed, so those nodes must be left alone.
    for name in UNTOUCHED:
        assert (
            "DRAIN" not in nodes[name]["state"]
        ), f"{name} still has a valid topology and should not be drained"
        assert nodes[name]["reason"] == "", f"{name} should have no drain reason"

    # Reading the same config again must not add to the reason.
    reconfigure()
    nodes = atf.get_nodes()
    for name in RENAMED:
        assert (
            nodes[name]["reason"] == "InvalidTopology"
        ), f"{name} should not gain InvalidTopology twice"


def test_resume_refused_then_recovered_with_valid_topology():
    """A node refuses resume until its topology names an existing block."""

    for state in ("resume", "resume_keep_tres", "undrain", "idle"):
        result = update_node("node1", f"state={state}")
        assert (
            result["exit_code"] != 0
        ), f"state={state} should be refused while node1's topology is invalid"
        assert (
            "Invalid node state transition" in result["stderr"]
        ), f"state={state} should be refused for the node state, not another fault"

    nodes = atf.get_nodes()
    assert "DRAIN" in nodes["node1"]["state"], "node1 should still be drained"

    assert (
        update_node("node1", "topology=default:block4")["exit_code"] == 0
    ), "node1 should accept a topology that names an existing block"

    nodes = atf.get_nodes()
    assert (
        nodes["node1"]["topology"] == "default:block4"
    ), "node1 should now be affiliated with block4"
    # The string above is echoed back verbatim, so check real placement.
    assert blocks_for_node("node1") == {
        "block4"
    }, "node1 should actually be placed in block4"
    assert "DRAIN" in nodes["node1"]["state"], "node1 should stay drained"
    assert (
        nodes["node1"]["reason"] == "InvalidTopology"
    ), "node1 should keep the reason it was drained for"

    assert (
        update_node("node1", "state=resume")["exit_code"] == 0
    ), "node1 should now resume"

    nodes = atf.get_nodes()
    assert "DRAIN" not in nodes["node1"]["state"], "node1 should no longer be drained"
    assert nodes["node1"]["reason"] == "", "a resumed node should have no reason"


def test_recover_by_clearing_topology():
    """Removing a node from all topology also lets it resume."""

    assert (
        update_node("node2", "topology=")["exit_code"] == 0
    ), "node2 should accept an empty topology"

    nodes = atf.get_nodes()
    assert "DRAIN" in nodes["node2"]["state"], "node2 should stay drained"

    assert (
        update_node("node2", "state=resume")["exit_code"] == 0
    ), "node2 should now resume"

    nodes = atf.get_nodes()
    assert "DRAIN" not in nodes["node2"]["state"], "node2 should no longer be drained"


def test_block_rename_survives_slurmctld_restart():
    """A restart re-reads the topology from state and drains, it does not die.

    The original failure was on the startup path, so this covers the restart as
    well as the reconfigure the module fixture does. block2 is renamed here, so
    this case stands on its own no matter which other tests ran.
    """

    atf.require_config_file("topology.conf", topology_conf("block4", "block5"))
    atf.restart_slurmctld()

    assert atf.wait_for_node_state(
        UNTOUCHED, "DRAIN"
    ), "Nodes under the renamed block2 should be drained after the restart"

    nodes = atf.get_nodes()
    for name in UNTOUCHED:
        assert (
            nodes[name]["reason"] == "InvalidTopology"
        ), f"{name} should say why it was drained"

    # The restarted slurmctld must also refuse to return the node to service.
    assert_resume_refused("node4")


def test_compound_reason_kept():
    """A node drained for another reason gains InvalidTopology and keeps it.

    Nothing removes the InvalidTopology part, so only a resume clears the
    reason.
    """

    other = "hardware maintenance"
    assert (
        update_node("node3", f"state=drain reason='{other}'")["exit_code"] == 0
    ), "node3 should accept an unrelated drain"

    atf.require_config_file("topology.conf", topology_conf("block4", "block6"))
    reconfigure()

    nodes = atf.get_nodes()
    assert (
        nodes["node3"]["reason"] == f"{other} : InvalidTopology"
    ), "node3 should keep the reason it was already drained for"

    # Reading the same config again must not add to the reason.
    reconfigure()
    assert (
        atf.get_nodes()["node3"]["reason"] == f"{other} : InvalidTopology"
    ), "node3 should not gain InvalidTopology twice"
    assert_resume_refused("node3")

    assert (
        update_node("node3", "topology=default:block6")["exit_code"] == 0
    ), "node3 should accept a topology that names an existing block"

    nodes = atf.get_nodes()
    assert (
        nodes["node3"]["reason"] == f"{other} : InvalidTopology"
    ), "the reason is left for the admin to clear when resuming node3"
    assert "DRAIN" in nodes["node3"]["state"], "node3 keeps the drain it already had"

    assert (
        update_node("node3", "state=resume")["exit_code"] == 0
    ), "node3 should resume now that its topology resolves"

    nodes = atf.get_nodes()
    assert "DRAIN" not in nodes["node3"]["state"], "node3 should be undrained"
    assert (
        nodes["node3"]["reason"] == ""
    ), "resuming clears the whole reason, InvalidTopology included"


def test_recover_by_restoring_the_block():
    """Restoring the missing block lets the node resume, but does not undrain it.

    block2 is renamed and then restored here, so this case stands on its own.
    """

    node = NODES[3]
    name = node["name"]
    atf.require_config_file("topology.conf", topology_conf("block4", "block9"))
    reconfigure()

    assert "DRAIN" in atf.get_nodes()[name]["state"], f"{name} should be drained"
    assert_resume_refused(name)

    atf.require_config_file("topology.conf", topology_conf("block4", "block2"))
    reconfigure()

    nodes = atf.get_nodes()
    assert "DRAIN" in nodes[name]["state"], f"{name} should stay drained"
    assert (
        nodes[name]["reason"] == "InvalidTopology"
    ), f"{name} should keep the reason it was drained for"
    assert blocks_for_node(name) == {
        "block2"
    }, f"{name} should be placed in the restored block2"

    assert (
        update_node(name, "state=resume")["exit_code"] == 0
    ), f"{name} should resume now that its topology resolves"
    assert (
        "DRAIN" not in atf.get_nodes()[name]["state"]
    ), f"{name} should no longer be drained"


def test_busy_node_keeps_its_job():
    """A node running a job when its block is renamed drains gracefully.

    The job keeps running, no new job starts on the node, and the node is
    drained once the job ends. block2 is renamed here, so this case stands on
    its own.
    """

    name = "node3"
    atf.require_config_file("topology.conf", topology_conf("block4", "block2"))
    reconfigure()
    assert (
        update_node(name, "topology=default:block2")["exit_code"] == 0
    ), f"{name} should accept a topology that names block2"
    if "DRAIN" in atf.get_nodes()[name]["state"]:
        assert (
            update_node(name, "state=resume")["exit_code"] == 0
        ), f"{name} should resume while block2 exists"
    assert atf.wait_for_node_state(name, "IDLE"), f"{name} should be idle"

    running = atf.submit_job_sbatch(
        f"-w {name} --exclusive --wrap 'sleep 20'", fatal=True
    )
    atf.wait_for_job_state(running, "RUNNING", fatal=True)

    atf.require_config_file("topology.conf", topology_conf("block4", "block8"))
    reconfigure()

    nodes = atf.get_nodes()
    assert "DRAIN" in nodes[name]["state"], f"{name} should be draining"
    assert "ALLOCATED" in nodes[name]["state"], f"{name} should still run its job"
    assert (
        atf.get_job_parameter(running, "JobState") == "RUNNING"
    ), "The running job should survive the reconfigure"

    pending = atf.submit_job_sbatch(f"-w {name} --wrap 'true'", fatal=True)
    atf.wait_for_job_state(running, "COMPLETED", fatal=True)
    assert atf.wait_for_node_state(name, "IDLE"), f"{name} should be idle"
    assert "DRAIN" in atf.get_nodes()[name]["state"], f"{name} should be drained"
    # Give the scheduler a few seconds to try the idle node.
    for _ in atf.timer(timeout=3, xfail=True):
        assert (
            atf.get_job_parameter(pending, "JobState") == "PENDING"
        ), f"A new job should not start on the drained {name}"
    atf.cancel_jobs([pending])
    wait_for_open_event(name, "InvalidTopology")


def test_cancel_reboot_keeps_the_drain():
    """Cancelling a pending ASAP reboot does not undrain an invalid node.

    The reboot waits for the job on the node, so it is still pending when the
    block is renamed. block2 is renamed here, so this case stands on its own.
    """

    name = "node1"
    atf.require_config_file("topology.conf", topology_conf("block4", "block2"))
    reconfigure()
    assert (
        update_node(name, "topology=default:block2")["exit_code"] == 0
    ), f"{name} should accept a topology that names block2"
    if "DRAIN" in atf.get_nodes()[name]["state"]:
        assert (
            update_node(name, "state=resume")["exit_code"] == 0
        ), f"{name} should resume while block2 exists"

    job = atf.submit_job_sbatch(
        f"-w {name} --exclusive --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job, "RUNNING", fatal=True)
    atf.run_command(
        f"scontrol reboot ASAP {name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    nodes = atf.get_nodes()
    assert (
        "REBOOT_REQUESTED" in nodes[name]["state"]
    ), f"{name} should have a pending reboot"
    assert (
        nodes[name]["reason"] == "Reboot ASAP"
    ), f"{name} should be drained for the reboot"

    atf.require_config_file("topology.conf", topology_conf("block4", "block14"))
    reconfigure()
    atf.run_command(
        f"scontrol cancel_reboot {name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    nodes = atf.get_nodes()
    assert (
        "REBOOT_REQUESTED" not in nodes[name]["state"]
    ), f"{name}'s reboot should be cancelled"
    assert "DRAIN" in nodes[name]["state"], f"{name} should stay drained"
    assert (
        nodes[name]["reason"] == "InvalidTopology"
    ), f"{name} should be drained for its topology"


def test_future_node_drained_again_on_registration():
    """A node set to FUTURE is drained again when its slurmd registers.

    State=FUTURE clears the drain, but registering does not make the topology
    valid. block2 is renamed here, so this case stands on its own.
    """

    atf.require_config_file("topology.conf", topology_conf("block4", "block7"))
    reconfigure()

    node = NODES[3]
    name = node["name"]
    assert "DRAIN" in atf.get_nodes()[name]["state"], f"{name} should be drained"

    # Stop slurmd first, or it re-registers at once and is drained again
    atf.stop_slurmd(name)
    assert (
        update_node(name, "state=future")["exit_code"] == 0
    ), f"{name} should accept state=future"
    assert (
        "DRAIN" not in atf.get_nodes()[name]["state"]
    ), "state=future should clear the drain"

    start_dynamic_slurmd(node)

    nodes = atf.get_nodes()
    assert (
        "DRAIN" in nodes[name]["state"]
    ), f"{name} should be drained again once its slurmd registers"
    assert (
        nodes[name]["reason"] == "InvalidTopology"
    ), f"{name} should say why it was drained"


def test_reboot_nextstate_resume_keeps_the_drain():
    """A reboot with nextstate=resume does not return the node to service.

    block2 is renamed here, so this case stands on its own.
    """

    atf.require_config_file("topology.conf", topology_conf("block4", "block3"))
    reconfigure()

    node = NODES[3]
    name = node["name"]
    assert "DRAIN" in atf.get_nodes()[name]["state"], f"{name} should be drained"

    atf.run_command(
        f"scontrol reboot nextstate=resume {name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert atf.wait_for_node_state(
        name, "REBOOT_ISSUED"
    ), f"{name} should reach REBOOT_ISSUED"

    # RebootProgram does nothing, so restart slurmd as if the node rebooted.
    atf.stop_slurmd(name)
    start_dynamic_slurmd(node, "-b", wait="registered")
    assert atf.wait_for_node_state(
        name, "REBOOT_ISSUED", reverse=True
    ), f"{name} should finish rebooting"

    nodes = atf.get_nodes()
    assert "DRAIN" in nodes[name]["state"], f"{name} should still be drained"
    assert (
        nodes[name]["reason"] == "InvalidTopology"
    ), f"{name} should say why it was drained"


def test_reboot_with_a_valid_topology_undrains():
    """A node that reboots with a topology that resolves is not drained again.

    The topology in its slurmd --conf replaces the invalid one. block2 is
    renamed here, so this case stands on its own.
    """

    node = NODES[1]
    name = node["name"]
    atf.require_config_file("topology.conf", topology_conf("block4", "block2"))
    reconfigure()
    assert (
        update_node(name, "topology=default:block2")["exit_code"] == 0
    ), f"{name} should accept a topology that names block2"
    if "DRAIN" in atf.get_nodes()[name]["state"]:
        assert (
            update_node(name, "state=resume")["exit_code"] == 0
        ), f"{name} should resume while block2 exists"

    atf.require_config_file("topology.conf", topology_conf("block4", "block15"))
    reconfigure()
    assert "DRAIN" in atf.get_nodes()[name]["state"], f"{name} should be drained"

    atf.run_command(
        f"scontrol reboot nextstate=resume {name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert atf.wait_for_node_state(
        name, "REBOOT_ISSUED"
    ), f"{name} should reach REBOOT_ISSUED"

    # RebootProgram does nothing, so restart slurmd as if the node rebooted.
    atf.stop_slurmd(name)
    start_dynamic_slurmd({**node, "topo": "default:block4"}, "-b", wait="registered")
    assert atf.wait_for_node_state(
        name, "REBOOT_ISSUED", reverse=True
    ), f"{name} should finish rebooting"

    nodes = atf.get_nodes()
    assert (
        "DRAIN" not in nodes[name]["state"]
    ), f"{name} should be resumed, since its new topology resolves"
    assert nodes[name]["reason"] == "", f"{name} should have no drain reason"
    assert blocks_for_node(name) == {
        "block4"
    }, f"{name} should be placed in the block its slurmd reported"


def test_power_down_keeps_the_drain():
    """Powering a node down does not return it to service.

    idle_on_node_suspend clears the drain when the power down starts, and
    State=RESUME would otherwise be accepted while the node is powering down.
    This powers node4 down, so it runs last. block2 is renamed here, so this
    case stands on its own.
    """

    atf.require_config_file("topology.conf", topology_conf("block4", "block13"))
    reconfigure()

    name = "node4"
    assert "DRAIN" in atf.get_nodes()[name]["state"], f"{name} should be drained"

    atf.run_command(
        f"scontrol power down {name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert atf.wait_for_node_state(
        name, "POWERING_DOWN"
    ), f"{name} should start powering down"
    assert (
        "DRAIN" not in atf.get_nodes()[name]["state"]
    ), "idle_on_node_suspend should clear the drain when the power down starts"
    assert_resume_refused(name)

    assert atf.wait_for_node_state(
        name, "POWERED_DOWN"
    ), f"{name} should finish powering down"
    nodes = atf.get_nodes()
    assert "DRAIN" in nodes[name]["state"], f"{name} should be drained again"
    assert (
        nodes[name]["reason"] == "InvalidTopology"
    ), f"{name} should say why it was drained"
