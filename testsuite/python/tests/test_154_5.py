############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the recovery of runtime changes to the nodes of HRES layers.

Changes to the nodes of a layer are saved in the node state:
- Dynamic and cloud nodes keep them across a restart or reconfigure.
- Other nodes keep them only when slurmctld is started with -R.
- A node that is powered down, or fails to resume within ResumeTimeout,
  returns to its configured layers: those in resources.yaml, or the ones a
  dynamic node was created with. So does a node that is powered down or
  powering down when slurmctld starts.
- If a saved layer is no longer in resources.yaml, or is no longer a leaf, all
  the saved layers of the node are discarded and it returns to its configured
  layers. Other edits to resources.yaml do not change the saved layers.

Changes to the count, base, and disabled state of a layer are not saved.
"""

import re

import pytest

import atf

cloud_node = "cloud1"
static_node = "node1"
dyn_node = "d1"
max_nodes = 10
suspend_timeout = 10
resume_timeout = 15

# A leaf without nodes, which a test removes from resources.yaml.
leaf4_layer = """    - layer_name: "leaf4"
      parent_name: "row2"
      count: 1000
"""

# leaf4 as an interior layer, with a leaf of its own, keeps every leaf at the
# same depth.
interior_leaf4_layers = """    - layer_name: "leaf4"
      parent_name: "root"
      count: 1000
    - layer_name: "leaf5"
      parent_name: "leaf4"
      count: 1000
"""

# Each node starts in its own leaf, so moving a node to another leaf is a
# change that a reset to resources.yaml or the creation HRES undoes. The rows
# between the root and the leaves show whether a move updates every ancestor.
resources_yaml = f"""
- resource: power
  mode: MODE_3
  layers:
    - layer_name: "root"
      count: 100000
    - layer_name: "row1"
      parent_name: "root"
      count: 50000
    - layer_name: "row2"
      parent_name: "root"
      count: 50000
    - layer_name: "leaf1"
      parent_name: "row1"
      nodes:
        - "cloud1"
      count: 1000
    - layer_name: "leaf2"
      parent_name: "row1"
      nodes:
        - "node1"
      count: 1000
    - layer_name: "leaf3"
      parent_name: "row2"
      nodes:
        - "node2"
      count: 1000
{leaf4_layer}- resource: natural
  mode: MODE_1
  layers:
    - layer_name: "top"
      nodes:
        - "cloud1"
        - "node[1-2]"
      count: 100
    - layer_name: "low"
      nodes:
        - "node1"
      count: 10
"""

leaves = ["leaf1", "leaf2", "leaf3", "leaf4"]
rows = {"leaf1": "row1", "leaf2": "row1", "leaf3": "row2", "leaf4": "row2"}
leaf3_hres = '[{"hres_name":"power","layer_name":"leaf3"}]'


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Issue #50445: HRES layer nodes are saved in the node state in "
        "26.11, and LayerName was added to scontrol show license output in 26.11.",
    )
    atf.require_nodes(2)
    # A cloud node next to the static ones, with its own slurmd port.
    node1 = atf.get_nodes(live=False, quiet=True, fatal=True)["node1"]
    atf.create_node(
        {
            "NodeName": cloud_node,
            "NodeHostname": node1["NodeHostname"],
            "Port": int(node1["Port"]) + 100,
            "State": "CLOUD",
        }
    )
    atf.require_config_parameter("MaxNodeCount", max_nodes)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    # Nodes are only powered down when a test asks for it.
    atf.require_config_parameter("SuspendTime", "INFINITE")
    atf.require_config_parameter("SuspendTimeout", suspend_timeout)
    atf.require_config_parameter("ResumeTimeout", resume_timeout)
    # Check power states every second, not every 10s, so that a node is
    # powered down soon after SuspendTimeout or ResumeTimeout.
    atf.require_config_parameter_includes(
        "SlurmctldParameters", ("power_save_interval", 1)
    )
    atf.require_config_parameter_includes(
        "SlurmctldParameters", ("power_save_min_interval", 0)
    )
    atf.require_config_file("resources.yaml", resources_yaml)

    # node0 is unused. Without a slurmd, pings to it delay slurmctld shutdowns.
    atf.clean_slurm_conf_nodes()
    atf.start_slurmctld(clean=True)
    for node in ["node1", "node2"]:
        atf.start_slurmd(node)

    yield

    atf.stop_slurmctld(also_slurmds=True)


@pytest.fixture(scope="function", autouse=True)
def reset_state():
    """Start every test from resources.yaml, with the cloud and dynamic nodes powered down"""
    yield
    atf.stop_slurmd(cloud_node)
    atf.stop_slurmd(dyn_node)
    atf.restart_slurmctld(clean=True)
    if not atf.slurmd_pids(static_node):
        atf.start_slurmd(static_node)


@pytest.fixture(scope="function")
def restore_resources_yaml():
    """Put the original resources.yaml back after a test that edits it"""
    yield
    atf.require_config_file("resources.yaml", resources_yaml)


def scontrol(command):
    atf.run_command(
        f"scontrol {command}", user=atf.properties["slurm-user"], fatal=True
    )


def layers_of(node):
    """Return the leaves of the power HRES that node belongs to."""
    lics = atf.get_licenses(fatal=True)
    found = set()
    for leaf in leaves:
        nodes = lics[f"power({leaf})"]["Nodes"]
        if nodes and node in atf.node_range_to_list(nodes):
            found.add(leaf)
    return found


def assert_layers(node, leaf, msg):
    """Assert that node is in leaf and its ancestors, and in no other power layer"""
    assert layers_of(node) == {leaf}, f"{node} should be in {leaf} {msg}"
    lics = atf.get_licenses(fatal=True)
    for parent in ["root", "row1", "row2"]:
        nodes = lics[f"power({parent})"]["Nodes"]
        is_member = bool(nodes) and node in atf.node_range_to_list(nodes)
        is_ancestor = parent in ("root", rows[leaf])
        assert (
            is_member == is_ancestor
        ), f"{node} should {'' if is_ancestor else 'not '}be in {parent}, as for {leaf}, {msg}"


def memberships(node):
    """Return the (resource, layer) pairs whose nodes include node."""
    found = set()
    for lic in atf.get_licenses(fatal=True).values():
        if lic["Nodes"] and node in atf.node_range_to_list(lic["Nodes"]):
            found.add((lic["LicenseName"], lic["LayerName"]))
    return found


def move_node(node, src, dst):
    """Move node from leaf src to leaf dst at runtime."""
    # Leaves of a MODE_3 HRES may not overlap, so remove before adding.
    scontrol(f"update HRESName=power LayerName={src} Nodes=-{node}")
    scontrol(f"update HRESName=power LayerName={dst} Nodes=+{node}")
    assert_layers(node, dst, "after the update")


def power_up_cloud_node():
    scontrol(f"update nodename={cloud_node} state=power_up")
    atf.wait_for_node_state(cloud_node, "POWERING_UP", fatal=True)
    atf.start_slurmd(cloud_node, "-b", wait="idle")
    atf.wait_for_node_state(cloud_node, "POWERING_UP", reverse=True, fatal=True)


def power_down(node):
    scontrol(f"update nodename={node} state=power_down_force")
    atf.wait_for_node_state(node, "POWERED_DOWN", fatal=True)


def test_static_node_recovery():
    """A static node keeps its layers only on a restart with -R"""

    move_node(static_node, "leaf2", "leaf1")
    scontrol("reconfigure")
    assert_layers(static_node, "leaf2", "after a reconfigure")

    move_node(static_node, "leaf2", "leaf1")
    atf.restart_slurmctld()
    assert_layers(static_node, "leaf2", "after a default restart")

    move_node(static_node, "leaf2", "leaf1")
    atf.restart_slurmctld(full_recovery=True)
    assert_layers(static_node, "leaf1", "after a restart with -R")


def test_dynamic_node_recovery():
    """A dynamic node keeps its layers across a reconfigure and a restart"""

    scontrol(f"create nodename={dyn_node} state=future HRES='{leaf3_hres}'")
    assert_layers(dyn_node, "leaf3", "after its creation")

    move_node(dyn_node, "leaf3", "leaf2")
    scontrol("reconfigure")
    assert_layers(dyn_node, "leaf2", "after a reconfigure")

    atf.restart_slurmctld()
    assert_layers(dyn_node, "leaf2", "after a default restart")

    atf.restart_slurmctld(full_recovery=True)
    assert_layers(dyn_node, "leaf2", "after a restart with -R")


def test_cloud_node_recovery():
    """A cloud node keeps its layers across restarts until it is powered down"""

    power_up_cloud_node()
    move_node(cloud_node, "leaf1", "leaf2")

    scontrol("reconfigure")
    assert_layers(cloud_node, "leaf2", "after a reconfigure")

    atf.restart_slurmctld()
    assert_layers(cloud_node, "leaf2", "after a default restart")

    atf.stop_slurmd(cloud_node)
    power_down(cloud_node)
    assert_layers(cloud_node, "leaf1", "after it is powered down")

    atf.restart_slurmctld()
    assert_layers(cloud_node, "leaf1", "after a restart once powered down")


def test_powered_down_at_start():
    """A node powered down when slurmctld starts uses its configured layers"""

    # The cloud node starts powered down.
    atf.wait_for_node_state(cloud_node, "POWERED_DOWN", fatal=True)
    move_node(cloud_node, "leaf1", "leaf2")

    atf.restart_slurmctld()
    assert_layers(cloud_node, "leaf1", "after a restart while powered down")


def test_static_node_power_down():
    """Powering down a static node returns it to its configured layers"""

    move_node(static_node, "leaf2", "leaf3")
    power_down(static_node)
    assert_layers(static_node, "leaf2", "after it is powered down")


def test_resume_while_powering_down():
    """Resuming a node that is powering down returns it to its configured layers"""

    scontrol(f"update nodename={static_node} state=power_down")
    atf.wait_for_node_state(static_node, "POWERING_DOWN", fatal=True)
    move_node(static_node, "leaf2", "leaf3")

    # The resume powers the node down at once, before SuspendTimeout does.
    scontrol(f"update nodename={static_node} state=resume")
    atf.wait_for_node_state(static_node, "POWERED_DOWN", fatal=True)
    assert_layers(static_node, "leaf2", "after it is resumed while powering down")


def test_dynamic_node_resume_timeout():
    """A dynamic node that fails to resume returns to its creation layers"""

    scontrol(f"create nodename={dyn_node} state=cloud HRES='{leaf3_hres}'")
    scontrol(f"update nodename={dyn_node} state=power_up")
    atf.wait_for_node_state(dyn_node, "POWERING_UP", fatal=True)
    move_node(dyn_node, "leaf3", "leaf1")

    # No slurmd registers, so the node is powered down after ResumeTimeout.
    atf.wait_for_node_state(dyn_node, "POWERED_DOWN", fatal=True)
    assert_layers(dyn_node, "leaf3", "after it fails to resume")


def test_dynamic_node_creation_layers_recovery():
    """A dynamic node's creation layers are recovered from the node state"""

    scontrol(f"create nodename={dyn_node} state=cloud HRES='{leaf3_hres}'")
    atf.wait_for_node_state(dyn_node, "POWERED_DOWN", fatal=True)
    move_node(dyn_node, "leaf3", "leaf1")
    atf.restart_slurmctld()
    assert_layers(dyn_node, "leaf3", "after a restart while powered down")

    scontrol(f"update nodename={dyn_node} state=power_up")
    atf.wait_for_node_state(dyn_node, "POWERING_UP", fatal=True)
    move_node(dyn_node, "leaf3", "leaf1")
    atf.restart_slurmctld()
    assert_layers(dyn_node, "leaf1", "after a restart while powering up")

    # No slurmd registers, so the node is powered down after ResumeTimeout.
    atf.wait_for_node_state(dyn_node, "POWERED_DOWN", fatal=True)
    assert_layers(dyn_node, "leaf3", "after it fails to resume")


def test_static_node_powered_down_at_start():
    """A static node powered down at start uses its configured layers with -R"""

    power_down(static_node)
    move_node(static_node, "leaf2", "leaf3")
    atf.restart_slurmctld(full_recovery=True)
    assert_layers(static_node, "leaf2", "after a restart with -R while powered down")


def test_static_node_powering_down_at_start():
    """A static node powering down at start uses its configured layers with -R"""

    scontrol(f"update nodename={static_node} state=power_down")
    atf.wait_for_node_state(static_node, "POWERING_DOWN", fatal=True)
    move_node(static_node, "leaf2", "leaf3")
    atf.restart_slurmctld(full_recovery=True)
    assert_layers(static_node, "leaf2", "after a restart with -R while powering down")

    # Once SuspendTimeout powers the node down, its layers are reset anyway. If
    # it is still powering down, the layers above were restored at the start.
    assert "POWERING_DOWN" in atf.get_node_parameter(
        static_node, "state"
    ), f"{static_node} should still be powering down, or the test checked the wrong reset"


@pytest.mark.parametrize(
    "replacement,reason",
    [
        ("", "not found"),
        (interior_leaf4_layers, "is not a leaf layer"),
    ],
    ids=["removed", "not_a_leaf"],
)
def test_invalid_layer_discards_saved_layers(
    restore_resources_yaml, replacement, reason
):
    """A saved layer that is gone or no longer a leaf discards all the saved layers"""

    power_up_cloud_node()
    scontrol(f"update HRESName=natural LayerName=low Nodes=+{cloud_node}")
    move_node(cloud_node, "leaf1", "leaf4")

    atf.require_config_file(
        "resources.yaml", resources_yaml.replace(leaf4_layer, replacement)
    )
    atf.restart_slurmctld()

    # Keeping the other saved layers would leave the node in natural(low).
    assert memberships(cloud_node) == {
        ("natural", "top"),
        ("power", "root"),
        ("power", "row1"),
        ("power", "leaf1"),
    }, f"{cloud_node} should be in the layers of resources.yaml, as leaf4 is invalid"
    assert re.search(
        rf"Ignoring HRES=.* of node {cloud_node}: HRES=power layer=leaf4 {reason}",
        atf.get_log("slurmctld", fatal=True),
    ), f"slurmctld should log an error for the saved layer leaf4 of {cloud_node}"


def test_yaml_edit_keeps_saved_layers(restore_resources_yaml):
    """Editing resources.yaml does not change saved layers until the node is powered down"""

    power_up_cloud_node()
    move_node(cloud_node, "leaf1", "leaf2")

    # Configure the cloud node in leaf3 and in natural(low) instead of leaf1.
    edited = resources_yaml
    for old, new in [
        (
            '      nodes:\n        - "cloud1"\n      count: 1000\n',
            "      count: 1000\n",
        ),
        (
            '        - "node2"\n      count: 1000\n',
            '        - "node2"\n        - "cloud1"\n      count: 1000\n',
        ),
        (
            '        - "node1"\n      count: 10\n',
            '        - "node1"\n        - "cloud1"\n      count: 10\n',
        ),
    ]:
        assert old in edited, f"resources.yaml of the test has no {old!r}"
        edited = edited.replace(old, new, 1)
    atf.require_config_file("resources.yaml", edited)

    atf.restart_slurmctld()
    assert memberships(cloud_node) == {
        ("natural", "top"),
        ("power", "root"),
        ("power", "row1"),
        ("power", "leaf2"),
    }, f"{cloud_node} should keep its saved layers after resources.yaml is edited"

    atf.stop_slurmd(cloud_node)
    power_down(cloud_node)
    assert memberships(cloud_node) == {
        ("natural", "top"),
        ("natural", "low"),
        ("power", "root"),
        ("power", "row2"),
        ("power", "leaf3"),
    }, f"{cloud_node} should use the edited resources.yaml once it is powered down"


def test_layer_settings_not_saved():
    """Count, base, and disabled state are not saved, unlike the nodes of a layer"""

    power_up_cloud_node()
    move_node(cloud_node, "leaf1", "leaf2")
    scontrol("update HRESName=power LayerName=leaf2 Count=500")
    scontrol("update HRESName=power LayerName=leaf2 Base=storage:100")
    scontrol("update HRESName=power LayerName=leaf2 DisableLayer=true")
    scontrol("update HRESName=natural DisableHRES=true")

    lics = atf.get_licenses(fatal=True)
    leaf2 = lics["power(leaf2)"]
    assert (leaf2["ConfTotal"], leaf2["BaseUsage"], leaf2["DisableLayer"]) == (
        500,
        100,
        True,
    ), "leaf2 should have the updated count, base, and disabled state"
    assert lics["natural(top)"]["DisableHRES"], "natural should be disabled"

    atf.restart_slurmctld()
    assert_layers(cloud_node, "leaf2", "after a default restart")
    lics = atf.get_licenses(fatal=True)
    leaf2 = lics["power(leaf2)"]
    assert (leaf2["ConfTotal"], leaf2["BaseUsage"], leaf2["DisableLayer"]) == (
        1000,
        0,
        False,
    ), "leaf2 should be back to the count, base, and disabled state of resources.yaml"
    assert not lics["natural(top)"][
        "DisableHRES"
    ], "natural should be enabled again, as in resources.yaml"


def test_dynamic_registration_recovery():
    """A node registered by slurmd -Z keeps its layers until powered down"""

    node1 = atf.get_nodes(live=False, quiet=True, fatal=True)["node1"]
    port = int(node1["Port"]) + 101
    atf.start_slurmd(
        dyn_node,
        f"-Z -b --conf 'Port={port} HRES={leaf3_hres}'",
        wait="idle",
    )
    assert_layers(dyn_node, "leaf3", "after its registration")

    move_node(dyn_node, "leaf3", "leaf2")
    atf.restart_slurmctld()
    assert_layers(dyn_node, "leaf2", "after a default restart")

    atf.stop_slurmd(dyn_node)
    power_down(dyn_node)
    assert_layers(dyn_node, "leaf3", "after it is powered down")


def test_layer_sets():
    """Saved layers may span several layers and resources, or none"""

    # The configured layers of the static node, and those the cloud node is
    # moved to.
    layer_set = {
        ("natural", "top"),
        ("natural", "low"),
        ("power", "root"),
        ("power", "row1"),
        ("power", "leaf2"),
    }
    assert (
        memberships(static_node) == layer_set
    ), f"{static_node} should be in the layers of resources.yaml"

    power_up_cloud_node()
    scontrol(f"update HRESName=natural LayerName=low Nodes=+{cloud_node}")
    move_node(cloud_node, "leaf1", "leaf2")
    for layer in ["top", "low"]:
        scontrol(f"update HRESName=natural LayerName={layer} Nodes=-{static_node}")
    scontrol(f"update HRESName=power LayerName=leaf2 Nodes=-{static_node}")
    assert (
        memberships(cloud_node) == layer_set
    ), f"{cloud_node} should be in the layers of both resources after the updates"
    assert (
        memberships(static_node) == set()
    ), f"{static_node} should be in no layer after the updates"

    atf.restart_slurmctld(full_recovery=True)
    assert (
        memberships(cloud_node) == layer_set
    ), f"{cloud_node} should keep the layers of both resources after a restart with -R"
    assert (
        memberships(static_node) == set()
    ), f"{static_node} should still be in no layer after a restart with -R"

    atf.restart_slurmctld()
    assert (
        memberships(cloud_node) == layer_set
    ), f"{cloud_node} should keep the layers of both resources after a default restart"
    assert (
        memberships(static_node) == layer_set
    ), f"{static_node} should be back in the layers of resources.yaml after a default restart"


@pytest.mark.parametrize(
    "node,conf_layer", [(cloud_node, "leaf1"), (static_node, "leaf2")]
)
def test_resume_timeout(node, conf_layer):
    """A node that fails to resume returns to its configured layers"""

    if node == static_node:
        power_down(node)
    atf.stop_slurmd(node)
    atf.wait_for_node_state(node, "POWERED_DOWN", fatal=True)

    scontrol(f"update nodename={node} state=power_up")
    atf.wait_for_node_state(node, "POWERING_UP", fatal=True)
    move_node(node, conf_layer, "leaf3")

    # No slurmd registers, so the node is powered down after ResumeTimeout.
    atf.wait_for_node_state(node, "POWERED_DOWN", fatal=True)
    assert_layers(node, conf_layer, "after it fails to resume")
