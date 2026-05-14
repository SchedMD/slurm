############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(2)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def nodes():
    return list(atf.get_nodes().keys())


def test_single_node_instance_id(nodes):
    """Verify InstanceId can be set on a single node"""

    node = nodes[0]
    instance_id = "id-single-001"

    atf.run_command(
        f"scontrol update nodename={node} InstanceId={instance_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert (
        atf.get_node_parameter(node, "instance_id") == instance_id
    ), f"InstanceId for {node} should be {instance_id}"


def test_single_node_instance_type(nodes):
    """Verify InstanceType can be set on a single node"""

    node = nodes[0]
    instance_type = "type-single-a"

    atf.run_command(
        f"scontrol update nodename={node} InstanceType={instance_type}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert (
        atf.get_node_parameter(node, "instance_type") == instance_type
    ), f"InstanceType for {node} should be {instance_type}"


@pytest.mark.xfail(
    atf.get_version() < (25, 11, 6),
    reason="Ticket 24886: InstanceType can be set on multiple nodes",
)
def test_multiple_nodes_instance_id(nodes):
    """Verify InstanceId can be set on multiple nodes with comma-separated values"""

    node0, node1 = nodes[0], nodes[1]
    id0, id1 = "id-multi-001", "id-multi-002"

    atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceId={id0},{id1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert (
        atf.get_node_parameter(node0, "instance_id") == id0
    ), f"InstanceId for {node0} should be {id0}"
    assert (
        atf.get_node_parameter(node1, "instance_id") == id1
    ), f"InstanceId for {node1} should be {id1}"


@pytest.mark.xfail(
    atf.get_version() < (25, 11, 6),
    reason="Ticket 24886: InstanceType can be set on multiple nodes",
)
def test_multiple_nodes_instance_type(nodes):
    """Verify InstanceType can be set on multiple nodes with comma-separated values"""

    node0, node1 = nodes[0], nodes[1]
    type0, type1 = "type-multi-a", "type-multi-b"

    atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceType={type0},{type1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert (
        atf.get_node_parameter(node0, "instance_type") == type0
    ), f"InstanceType for {node0} should be {type0}"
    assert (
        atf.get_node_parameter(node1, "instance_type") == type1
    ), f"InstanceType for {node1} should be {type1}"


def test_overwrite_instance_id(nodes):
    """Verify InstanceId can be overwritten with a new value"""

    node = nodes[0]

    atf.run_command(
        f"scontrol update nodename={node} InstanceId=id-original",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        atf.get_node_parameter(node, "instance_id") == "id-original"
    ), "InstanceId should be set to initial value"

    atf.run_command(
        f"scontrol update nodename={node} InstanceId=id-overwritten",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        atf.get_node_parameter(node, "instance_id") == "id-overwritten"
    ), "InstanceId should be updated to new value"


def test_overwrite_instance_type(nodes):
    """Verify InstanceType can be overwritten with a new value"""

    node = nodes[0]

    atf.run_command(
        f"scontrol update nodename={node} InstanceType=type-original",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        atf.get_node_parameter(node, "instance_type") == "type-original"
    ), "InstanceType should be set to initial value"

    atf.run_command(
        f"scontrol update nodename={node} InstanceType=type-overwritten",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        atf.get_node_parameter(node, "instance_type") == "type-overwritten"
    ), "InstanceType should be updated to new value"


@pytest.mark.xfail(
    atf.get_version() < (25, 11, 6),
    reason="Ticket 24886: InstanceType can be set on multiple nodes",
)
def test_overwrite_multiple_nodes_instance_id(nodes):
    """Verify InstanceId can be overwritten on multiple nodes"""

    node0, node1 = nodes[0], nodes[1]

    atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceId=id-orig-a,id-orig-b",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert atf.get_node_parameter(node0, "instance_id") == "id-orig-a"
    assert atf.get_node_parameter(node1, "instance_id") == "id-orig-b"

    atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceId=id-new-a,id-new-b",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        atf.get_node_parameter(node0, "instance_id") == "id-new-a"
    ), f"InstanceId for {node0} should be updated"
    assert (
        atf.get_node_parameter(node1, "instance_id") == "id-new-b"
    ), f"InstanceId for {node1} should be updated"


@pytest.mark.xfail(
    atf.get_version() < (25, 11, 6),
    reason="Ticket 24886: InstanceType can be set on multiple nodes",
)
def test_overwrite_multiple_nodes_instance_type(nodes):
    """Verify InstanceType can be overwritten on multiple nodes"""

    node0, node1 = nodes[0], nodes[1]

    atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceType=type-orig-a,type-orig-b",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert atf.get_node_parameter(node0, "instance_type") == "type-orig-a"
    assert atf.get_node_parameter(node1, "instance_type") == "type-orig-b"

    atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceType=type-new-a,type-new-b",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        atf.get_node_parameter(node0, "instance_type") == "type-new-a"
    ), f"InstanceType for {node0} should be updated"
    assert (
        atf.get_node_parameter(node1, "instance_type") == "type-new-b"
    ), f"InstanceType for {node1} should be updated"


def test_reset_instance_id(nodes):
    """Verify InstanceId can be cleared by setting it to empty"""

    node = nodes[0]

    atf.run_command(
        f"scontrol update nodename={node} InstanceId=id-to-clear",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        atf.get_node_parameter(node, "instance_id") == "id-to-clear"
    ), "InstanceId should be set before clearing"

    atf.run_command(
        f"scontrol update nodename={node} InstanceId=",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert not atf.get_node_parameter(
        node, "instance_id"
    ), "InstanceId should be empty after reset"


def test_reset_instance_type(nodes):
    """Verify InstanceType can be cleared by setting it to empty"""

    node = nodes[0]

    atf.run_command(
        f"scontrol update nodename={node} InstanceType=type-to-clear",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        atf.get_node_parameter(node, "instance_type") == "type-to-clear"
    ), "InstanceType should be set before clearing"

    atf.run_command(
        f"scontrol update nodename={node} InstanceType=",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert not atf.get_node_parameter(
        node, "instance_type"
    ), "InstanceType should be empty after reset"


@pytest.mark.xfail(
    atf.get_version() < (25, 11, 6),
    reason="Ticket 24886: InstanceType can be set on multiple nodes",
)
def test_reset_multiple_nodes_instance_id(nodes):
    """Verify InstanceId can be cleared on multiple nodes"""

    node0, node1 = nodes[0], nodes[1]

    atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceId=id-clear-a,id-clear-b",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert atf.get_node_parameter(node0, "instance_id") == "id-clear-a"
    assert atf.get_node_parameter(node1, "instance_id") == "id-clear-b"

    atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceId=",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert not atf.get_node_parameter(
        node0, "instance_id"
    ), f"InstanceId for {node0} should be empty after reset"
    assert not atf.get_node_parameter(
        node1, "instance_id"
    ), f"InstanceId for {node1} should be empty after reset"


@pytest.mark.xfail(
    atf.get_version() < (25, 11, 6),
    reason="Ticket 24886: InstanceType can be set on multiple nodes",
)
def test_reset_multiple_nodes_instance_type(nodes):
    """Verify InstanceType can be cleared on multiple nodes"""

    node0, node1 = nodes[0], nodes[1]

    atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceType=type-clear-a,type-clear-b",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert atf.get_node_parameter(node0, "instance_type") == "type-clear-a"
    assert atf.get_node_parameter(node1, "instance_type") == "type-clear-b"

    atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceType=",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert not atf.get_node_parameter(
        node0, "instance_type"
    ), f"InstanceType for {node0} should be empty after reset"
    assert not atf.get_node_parameter(
        node1, "instance_type"
    ), f"InstanceType for {node1} should be empty after reset"


def test_broadcast_instance_id(nodes):
    """Verify a single InstanceId value is broadcast to all nodes"""

    node0, node1 = nodes[0], nodes[1]
    instance_id = "id-broadcast-001"

    atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceId={instance_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert (
        atf.get_node_parameter(node0, "instance_id") == instance_id
    ), f"InstanceId for {node0} should be {instance_id}"
    assert (
        atf.get_node_parameter(node1, "instance_id") == instance_id
    ), f"InstanceId for {node1} should be {instance_id}"


def test_broadcast_instance_type(nodes):
    """Verify a single InstanceType value is broadcast to all nodes"""

    node0, node1 = nodes[0], nodes[1]
    instance_type = "type-broadcast-a"

    atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceType={instance_type}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert (
        atf.get_node_parameter(node0, "instance_type") == instance_type
    ), f"InstanceType for {node0} should be {instance_type}"
    assert (
        atf.get_node_parameter(node1, "instance_type") == instance_type
    ), f"InstanceType for {node1} should be {instance_type}"


@pytest.mark.xfail(
    atf.get_version() < (25, 11, 6),
    reason="Ticket 24886: InstanceType can be set on multiple nodes",
)
def test_mismatched_instance_id_count(nodes):
    """Verify mismatched InstanceId count is rejected"""

    node0, node1 = nodes[0], nodes[1]

    result = atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceId=id-a,id-b,id-c",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        result["exit_code"] != 0
    ), "scontrol should fail when InstanceId count does not match node count"


@pytest.mark.xfail(
    atf.get_version() < (25, 11, 6),
    reason="Ticket 24886: InstanceType can be set on multiple nodes",
)
def test_mismatched_instance_type_count(nodes):
    """Verify mismatched InstanceType count is rejected"""

    node0, node1 = nodes[0], nodes[1]

    result = atf.run_command(
        f"scontrol update nodename={node0},{node1} InstanceType=type-a,type-b,type-c",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        result["exit_code"] != 0
    ), "scontrol should fail when InstanceType count does not match node count"
