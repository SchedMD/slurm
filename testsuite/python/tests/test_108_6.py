############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


switch_name = "s0"
nodes = list(atf.get_nodes(live=False).keys())


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("TopologyPlugin", "topology/tree")
    atf.require_config_parameter(
        "SwitchName", f"{switch_name} Nodes={nodes[0]}", source="topology"
    )

    atf.require_slurm_running()


def test_show_topo():
    if atf.get_version() >= (25, 5, 0):
        opt = f"switch={switch_name}"
    else:
        opt = f"{switch_name}"

    output = atf.run_command_output(f"scontrol show topology {opt}")
    assert re.search(
        rf"SwitchName={switch_name} .*Nodes={nodes[0]}", output
    ), f"Verify that scontrol returns the right SwitchName={switch_name} with the right Nodes={nodes[0]}"
