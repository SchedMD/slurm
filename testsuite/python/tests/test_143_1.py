############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create custom topology.conf")
    atf.require_nodes(3)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TopologyPlugin", "topology/tree")

    # Mark topology for teardown and overwrite with proper data.
    # require_config_parameter marks a file for teardown,
    #  but it doesn't allow us to write multiple lines easily to an external conf.
    # We're using it to create/mark the file.
    atf.require_config_parameter("", "", source="topology")
    # This is where we write the actual data
    overwrite_topology_conf()
    atf.require_slurm_running()


def overwrite_topology_conf():
    conf = atf.properties["slurm-config-dir"] + "/topology.conf"
    content = """
        SwitchName=s01 Nodes=node1
        SwitchName=s02 Nodes=node2
        SwitchName=root Switches=s01,s02
    """
    atf.run_command(f"cat > {conf}", input=content, user="slurm", fatal=True)


def test_switches():
    """Verify topology/tree + select/cons_tree works with --switches"""

    # Positive tests
    positive_output = atf.run_job("--switches=2 -N2 true")
    assert (
        positive_output["exit_code"] == 0
    ), "Expected command to complete when asking for 2 nodes 2 switches apart"
    positive_output = atf.run_job("--switches=1 --exclusive -N1 true")
    assert (
        positive_output["exit_code"] == 0
    ), "Expected command to complete when asking for 1 node 1 switch apart (just 1 node)"
    # Negative test
    negative_output = atf.run_job("--switches=1 -N2 true", timeout=0.5)
    assert (
        negative_output["exit_code"] == 110
    ), "Expected command to time out when asking for 2 nodes 1 switch apart"
    assert (
        re.search(
            r"srun: job [0-9]+ queued and waiting for resources",
            str(negative_output["stderr"]),
        )
        is not None
    )
