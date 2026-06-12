############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

# A dictionary to store the nodes used in the test
node_list = {}
node_range = ""


# Setup for all tests
@pytest.fixture(scope="module", autouse=True)
def setup():
    global node_range

    atf.require_nodes(8)
    atf.require_slurm_running()

    # Get a list of 8 idle nodes to be used in the tests
    nodes = atf.run_job_nodes("-N8 true", fatal=True)
    for i, node in enumerate(nodes):
        node_list[i] = node
    node_range = atf.node_list_to_range(node_list.values())


@pytest.fixture(scope="function", autouse=True)
def reset_node_states():
    """Ensure all jobs are canceled and nodes are resumed after each test."""
    yield
    atf.cancel_all_jobs()
    atf.run_command(
        f"scontrol update nodename={node_range} state=resume",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_allocated_state():
    """Test sinfo filtering for ALLOCATED state."""
    # Submit a job to allocate the first two nodes
    job_id = atf.submit_job_sbatch(
        f"-N2 -w {node_list[0]},{node_list[1]} --exclusive --wrap 'sleep 60'"
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # Get sinfo output for allocated nodes
    output = atf.run_command_output(
        f"sinfo -Nh -n {node_range} -O NodeList --state=allocated"
    )

    # Verify that only the two allocated nodes are in the output
    assert len(output.splitlines()) == 2, "Expected 2 allocated nodes"
    assert node_list[0] in output, f"{node_list[0]} should be allocated"
    assert node_list[1] in output, f"{node_list[1]} should be allocated"


def test_idle_state():
    """Test sinfo filtering for IDLE state."""
    # Change the state of some nodes
    atf.run_command(
        f"scontrol update nodename={node_list[4]},{node_list[5]} state=down reason=test",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"scontrol update nodename={node_list[6]},{node_list[7]} state=drain reason=test",
        user=atf.properties["slurm-user"],
    )

    # Get sinfo output for idle nodes
    output = atf.run_command_output(
        f"sinfo -Nh -n {node_range} -O NodeList,StateComplete --state=idle"
    )

    # Verify that the correct nodes are reported as idle (including idle+drain)
    lines = [line.strip().split() for line in output.strip().splitlines()]
    nodes = {node: status for node, status in lines}

    nodes_idle = [n for n, s in nodes.items() if s == "idle"]
    nodes_drain = [n for n, s in nodes.items() if "drain" in s]

    assert len(nodes) == 6, "Expected 6 idle nodes, 4 only idle and 2 idle+drain nodes"

    assert len(nodes_idle) == 4, "Expected 4 idle nodes"
    for i in range(4):
        assert node_list[i] in nodes_idle, f"{node_list[i]} should be only idle"

    # Drained nodes are also idle
    assert len(nodes_drain) == 2, "Expected 2 drained* nodes (that are also idle)"
    assert node_list[6] in nodes_drain, f"{node_list[6]} should be idle+drain"
    assert node_list[7] in nodes_drain, f"{node_list[7]} should be idle+drain"


def test_down_state():
    """Test sinfo filtering for DOWN state."""
    # Change the state of some nodes
    atf.run_command(
        f"scontrol update nodename={node_list[4]},{node_list[5]} state=down reason=test",
        user=atf.properties["slurm-user"],
    )

    # Get sinfo output for down nodes
    output = atf.run_command_output(
        f"sinfo -Nh -n {node_range} -O NodeList,StateComplete --state=down"
    )

    # Verify that the correct nodes are reported as down
    lines = [line.strip().split() for line in output.strip().splitlines()]
    nodes = {node: status for node, status in lines}
    nodes_down = [n for n, s in nodes.items() if "down" in s]
    assert len(nodes) == 2, "Expected 2 down nodes"
    assert node_list[4] in nodes_down, f"{node_list[4]} should be down"
    assert node_list[5] in nodes_down, f"{node_list[5]} should be down"


def test_drain_state():
    """Test sinfo filtering for DRAIN state."""
    # Change the state of some nodes
    atf.run_command(
        f"scontrol update nodename={node_list[6]},{node_list[7]} state=drain reason=test",
        user=atf.properties["slurm-user"],
    )

    # Get sinfo output for drained nodes
    output = atf.run_command_output(
        f"sinfo -Nh -n {node_range} -O NodeList,StateComplete --state=drain"
    )

    # Verify that the correct nodes are reported as drained
    lines = [line.strip().split() for line in output.strip().splitlines()]
    nodes = {node: status for node, status in lines}

    nodes_drain = [n for n, s in nodes.items() if "drain" in s]

    assert len(nodes_drain) == 2, "Expected 2 drained nodes"
    assert node_list[6] in nodes_drain, f"{node_list[6]} should be drained"
    assert node_list[7] in nodes_drain, f"{node_list[7]} should be drained"


def test_down_and_drain_state():
    """Test sinfo filtering for DOWN and DRAIN states with a comma."""
    # Change the state of some nodes
    atf.run_command(
        f"scontrol update nodename={node_list[4]},{node_list[5]} state=down reason=test",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"scontrol update nodename={node_list[6]},{node_list[7]} state=drain reason=test",
        user=atf.properties["slurm-user"],
    )

    # Get sinfo output for down and drained nodes
    output = atf.run_command_output(
        f"sinfo -Nh -n {node_range} -O NodeList,StateComplete --state=down,drain"
    )
    lines = [line.strip().split() for line in output.strip().splitlines()]
    nodes = {node: status for node, status in lines}

    nodes_down = [n for n, s in nodes.items() if "down" in s]
    nodes_drain = [n for n, s in nodes.items() if "drain" in s]
    # Verify the output
    assert len(nodes_down) == 2, "Expected 2 down nodes"
    assert len(nodes_drain) == 2, "Expected 2 drained nodes"
    assert node_list[4] in nodes_down, f"{node_list[4]} should be down"
    assert node_list[5] in nodes_down, f"{node_list[5]} should be down"
    assert node_list[6] in nodes_drain, f"{node_list[6]} should be drained"
    assert node_list[7] in nodes_drain, f"{node_list[7]} should be drained"


def test_down_and_drain_state_ampersand():
    """Test sinfo filtering for DOWN&DRAIN states with an ampersand."""
    # Change the state of some nodes to be both down and drained
    atf.run_command(
        f"scontrol update nodename={node_list[4]},{node_list[5]} state=down reason=test",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"scontrol update nodename={node_list[4]},{node_list[5]} state=drain reason=test",
        user=atf.properties["slurm-user"],
    )

    # Get sinfo output for down&drained nodes
    output = atf.run_command_output(
        f"sinfo -Nh -n {node_range} -O NodeList,StateComplete --state='down&drain'"
    )

    # Verify the output for nodes that are both down and drained (state will show as `drained`)
    lines = [line.strip().split() for line in output.strip().splitlines()]
    nodes = {node: status for node, status in lines}

    down_drained_nodes = [n for n, s in nodes.items() if "drain" in s]
    assert len(down_drained_nodes) == 2, "Expected 2 drained nodes"
    assert node_list[4] in down_drained_nodes, f"{node_list[4]} should be down&drained"
    assert node_list[5] in down_drained_nodes, f"{node_list[5]} should be down&drained"
