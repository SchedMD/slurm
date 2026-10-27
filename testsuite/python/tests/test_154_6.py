############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scontrol update nodename HRES=.

HRES= replaces the layers a node is a member of. A JSON list with no layer or
an empty HRES= removes the node from every layer. An invalid value changes
nothing, including the other changes in the request.
"""

import pytest

import atf

cloud_node = "cloud1"
static_node = "node1"
dyn_node = "d1"
max_nodes = 10
suspend_timeout = 2

resources_yaml = """
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
- resource: natural
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

# The layers of node1 in resources.yaml
static_conf = {
    ("power", "root"),
    ("power", "row1"),
    ("power", "leaf2"),
    ("natural", "top"),
    ("natural", "low"),
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Issue #51190: scontrol update nodename HRES= was added in 26.11.",
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
    # Check power states every second, not every 10s, so that a node is
    # powered down soon after SuspendTimeout.
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
        atf.start_slurmd(node, fatal=True)

    yield

    atf.stop_slurmctld(also_slurmds=True)


@pytest.fixture(scope="function", autouse=True)
def reset_state():
    """Start every test with the static nodes resumed and in the layers of
    resources.yaml"""
    yield
    scontrol(
        "update nodename=node1 HRES="
        + hres(("power", "leaf2"), ("natural", "top"), ("natural", "low")),
        fatal=True,
    )
    scontrol(
        "update nodename=node2 HRES=" + hres(("power", "leaf3"), ("natural", "top")),
        fatal=True,
    )
    # Keep a node drained by a failed test from failing the tests that follow.
    # A node that is not drained rejects the resume, which is not an error here.
    scontrol("update nodename=node[1-2] state=resume")


@pytest.fixture
def delete_dyn_node():
    yield
    scontrol(f"delete nodename={dyn_node}")


@pytest.fixture
def restart_clean():
    """Restart from resources.yaml, with the cloud node powered down"""
    yield
    atf.stop_slurmd(cloud_node)
    atf.restart_slurmctld(clean=True)


def scontrol(command, **kwargs):
    return atf.run_command(
        f"scontrol {command}", user=atf.properties["slurm-user"], **kwargs
    )


def hres(*layers):
    """Return the HRES= JSON list of the given (resource, layer) pairs."""
    return (
        "'["
        + ",".join(f'{{"hres_name":"{r}","layer_name":"{la}"}}' for r, la in layers)
        + "]'"
    )


def test_replace():
    """HRES= replaces the layers of a node, across several resources"""

    scontrol(
        f"update nodename={static_node} HRES="
        + hres(("power", "leaf3"), ("natural", "top")),
        fatal=True,
    )
    assert atf.get_node_hres_layers(static_node, fatal=True) == {
        ("power", "root"),
        ("power", "row2"),
        ("power", "leaf3"),
        ("natural", "top"),
    }, f"{static_node} should only be in the given layers and the parents of leaf3"
    assert atf.node_range_to_list(
        atf.get_licenses(fatal=True)["power(row1)"]["Nodes"]
    ) == ["cloud1"], f"row1 should lose {static_node} when it leaves leaf2"


def test_node_list():
    """HRES= sets the layers of every node in the list"""

    scontrol("update nodename=node[1-2] HRES=" + hres(("power", "leaf1")), fatal=True)
    for node in ["node1", "node2"]:
        assert atf.get_node_hres_layers(node, fatal=True) == {
            ("power", "root"),
            ("power", "row1"),
            ("power", "leaf1"),
        }, f"{node} should only be in leaf1 and its parents"
    lics = atf.get_licenses(fatal=True)
    assert atf.node_range_to_list(lics["power(leaf1)"]["Nodes"]) == [
        "cloud1",
        "node1",
        "node2",
    ], "leaf1 should keep cloud1 and gain node[1-2]"
    assert not lics["power(leaf2)"]["Nodes"], "leaf2 should have no node left"


@pytest.mark.parametrize("value", ["'[]'", ""], ids=["empty_list", "empty"])
def test_no_layers(value):
    """HRES='[]' or an empty HRES= removes a node from every layer"""

    scontrol(f"update nodename={static_node} HRES={value}", fatal=True)
    assert (
        atf.get_node_hres_layers(static_node, fatal=True) == set()
    ), f"{static_node} should be in no layer after HRES={value}"


def test_dynamic_node(delete_dyn_node):
    """A dynamic node keeps its HRES= layers across reconfigures and restarts"""

    scontrol(
        f"create nodename={dyn_node} state=future HRES=" + hres(("power", "leaf3")),
        fatal=True,
    )
    scontrol(f"update nodename={dyn_node} HRES=" + hres(("power", "leaf1")), fatal=True)
    leaf1 = {("power", "root"), ("power", "row1"), ("power", "leaf1")}
    assert (
        atf.get_node_hres_layers(dyn_node, fatal=True) == leaf1
    ), f"{dyn_node} should be moved to leaf1"

    scontrol("reconfigure", fatal=True)
    assert (
        atf.get_node_hres_layers(dyn_node, fatal=True) == leaf1
    ), f"{dyn_node} should stay in leaf1 after a reconfigure"
    atf.restart_slurmctld()
    assert (
        atf.get_node_hres_layers(dyn_node, fatal=True) == leaf1
    ), f"{dyn_node} should stay in leaf1 after a restart"


@pytest.mark.parametrize(
    "value",
    [
        "'not json'",
        hres(("nosuch", "leaf1")),
        hres(("power", "nosuch")),
        hres(("power", "root")),
        hres(("power", "row1")),
        hres(("power", "leaf1"), ("power", "leaf3")),
    ],
    ids=[
        "bad_json",
        "unknown_hres",
        "unknown_layer",
        "root",
        "middle_layer",
        "two_leaves",
    ],
)
def test_invalid(value):
    """An invalid HRES= rejects the whole request"""

    result = scontrol(
        f"update nodename={static_node} state=drain reason=test HRES={value}",
        xfail=True,
    )
    assert result["exit_code"] != 0, "The update should be rejected"
    assert (
        atf.get_node_hres_layers(static_node, fatal=True) == static_conf
    ), f"{static_node} should keep the layers of resources.yaml"
    assert "DRAIN" not in atf.get_node_parameter(
        static_node, "state"
    ), f"{static_node} should not be drained by a rejected request"


def test_unknown_node():
    """No node's HRES changes when the node list has an unknown node"""

    result = scontrol(
        f"update nodename={static_node},nosuch HRES=" + hres(("power", "leaf3")),
        xfail=True,
    )
    assert result["exit_code"] != 0, "The update should be rejected"
    assert (
        atf.get_node_hres_layers(static_node, fatal=True) == static_conf
    ), f"{static_node} should keep the layers of resources.yaml"


@pytest.mark.parametrize(
    "value,moved",
    [
        (
            hres(("power", "leaf2")),
            {("power", "root"), ("power", "row1"), ("power", "leaf2")},
        ),
        ("'[]'", set()),
    ],
    ids=["leaf2", "no_layers"],
)
def test_cloud_node_recovery(value, moved, restart_clean):
    """A cloud node keeps its HRES= layers until it is powered down"""

    scontrol(f"update nodename={cloud_node} state=power_up", fatal=True)
    atf.wait_for_node_state(cloud_node, "POWERING_UP", fatal=True)
    atf.start_slurmd(cloud_node, "-b", wait="idle", fatal=True)
    atf.wait_for_node_state(cloud_node, "POWERING_UP", reverse=True, fatal=True)

    scontrol(f"update nodename={cloud_node} HRES={value}", fatal=True)
    assert (
        atf.get_node_hres_layers(cloud_node, fatal=True) == moved
    ), f"{cloud_node} should be in the layers of HRES={value}"

    scontrol("reconfigure", fatal=True)
    assert (
        atf.get_node_hres_layers(cloud_node, fatal=True) == moved
    ), f"{cloud_node} should keep the layers of HRES={value} after a reconfigure"

    atf.restart_slurmctld()
    assert (
        atf.get_node_hres_layers(cloud_node, fatal=True) == moved
    ), f"{cloud_node} should keep the layers of HRES={value} after a restart"

    atf.stop_slurmd(cloud_node)
    scontrol(f"update nodename={cloud_node} state=power_down_force", fatal=True)
    atf.wait_for_node_state(cloud_node, "POWERED_DOWN", fatal=True)
    assert atf.get_node_hres_layers(cloud_node, fatal=True) == {
        ("power", "root"),
        ("power", "row1"),
        ("power", "leaf1"),
        ("natural", "top"),
    }, f"{cloud_node} should be back in the layers of resources.yaml after it is powered down"
