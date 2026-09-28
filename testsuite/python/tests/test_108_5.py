############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import json
import re

import pytest

import atf

topology_names = ["tree", "block", "flat1", "flat2", "ring", "torus"]
topology_yaml = """
---
- topology: tree
  cluster_default: true
  tree:
    switches:
      - switch: sw_root
        children: s[1-2]
      - switch: s1
        nodes: node[1-2]
      - switch: s2
        nodes: node[3-4]
- topology: block
  cluster_default: false
  block:
    block_sizes:
      - 2
      - 4
    blocks:
      - block: b1
        nodes: node[1-2]
      - block: b2
        nodes: node[3-4]
      - block: b3
        nodes: node[5-6]
      - block: b4
        nodes: node[7-8]
- topology: flat1
  cluster_default: false
  flat:
    alpha_step_rank: false
- topology: flat2
  cluster_default: false
  flat:
    alpha_step_rank: true
- topology: ring
  cluster_default: false
  ring:
    rings:
      - ring: ring0
        nodes: node[1-8]
- topology: torus
  cluster_default: false
  torus3d:
    toruses:
      - name: pod1
        dims:
          x: 2
          y: 2
          z: 2
        nodes: node[1-8]
        placements:
          - dims:
              x: 1
              y: 1
              z: 1
          - dims:
              x: 1
              y: 1
              z: 2
          - dims:
              x: 2
              y: 2
              z: 2
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    if atf.get_version("bin/scontrol") >= (26, 11):
        atf.require_auto_config("Need to set specific topology.yaml and node count")
        atf.require_config_file("topology.yaml", topology_yaml)
        atf.require_nodes(8)
    atf.require_slurm_running()


@pytest.mark.parametrize(
    "action, min_version",
    [
        ("show licenses", None),
        ("ping", None),
        ("show jobs", None),
        ("show job", None),
        ("show steps", None),
        ("show nodes", None),
        ("show partitions", None),
        ("show reservations", None),
        ("show topology", (26, 11)),
        (f"show topology {topology_names[0]}", (26, 11)),
        (f"show topology {topology_names[1]}", (26, 11)),
        (f"show topology {topology_names[2]}", (26, 11)),
        (f"show topology {topology_names[3]}", (26, 11)),
        (f"show topology {topology_names[4]}", (26, 11)),
        (f"show topology {topology_names[5]}", (26, 11)),
    ],
)
def test_json(action, min_version):
    """Verify scontrol --json has the correct format and meta data command"""

    if (min_version is not None) and (atf.get_version("bin/scontrol") < min_version):
        pytest.skip(
            f"Skipping scontrol --json {action} because it requires version {min_version}"
        )

    expected_command = ["scontrol", "--json"]
    expected_command.extend(action.split())

    output = atf.run_command_output(f"scontrol --json {action}", fatal=True)
    json_data = json.loads(output)
    assert json_data is not None, f"scontrol --json {action} dumped a bare null"
    assert (
        json_data["errors"] == []
    ), f"scontrol --json {action} reported errors: {json_data['errors']}"
    if atf.get_version("bin/scontrol") >= (26, 5):
        assert (
            json_data["meta"]["command"] == expected_command
        ), f"meta.command did not echo the invocation: {json_data['meta']['command']}"


@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="Issue 50235: scontrol --json show topology was added in 26.11",
)
def test_show_topology_unknown_name_json():
    """Verify scontrol --json show topology reports an unknown name as JSON"""

    result = atf.run_command(
        "scontrol --json show topology no_such_topology", xfail=True, fatal=True
    )
    assert result["stdout"].strip(), (
        "scontrol --json produced no output at all; scontrol.1 documents --json "
        "as dumping information as JSON, and the topology REST endpoint returns "
        "a structured error envelope for this same condition"
    )
    errors = [error["error"] for error in json.loads(result["stdout"])["errors"]]
    assert (
        "Requested topology configuration is not available" in errors
    ), f"unknown topology name not reported in the --json errors: {errors}"


@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="Issue 50235: scontrol --json show topology and the interactive"
    " stale-topology fix were added in 26.11",
)
def test_json_interactive_show_topology_by_name():
    """Verify interactive scontrol re-reads the topology for each name"""

    output = atf.run_command_output(
        "scontrol --json",
        input="show topology tree\nshow topology block\n",
        fatal=True,
    )

    # Filter out the interactive terminal log so a valid json can be processed
    filtered_output = "[" + re.sub("scontrol:.*", ",", output)[1:-2] + "]"

    tree_data = json.loads(filtered_output)[0]["topology"]["tree"]
    block_data = json.loads(filtered_output)[1]["topology"]["block"]

    assert (
        len(tree_data) == 3
    ), f"interactive 'show topology tree' did not report the tree topology: {output}"
    assert (
        len(block_data) == 6
    ), f"interactive 'show topology block' reused the response cached for tree: {output}"
