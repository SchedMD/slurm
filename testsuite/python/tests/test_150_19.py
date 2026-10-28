############################################################################
# Copyright (C) 2026 Google LLC.
############################################################################
"""Test topology/block with TPU cluster layouts and segment allocation."""

import pytest

import atf

NODE_COUNT = 16

TPU_YAML_16NODE_SEG = """tpu_types:
  - type: tpu7x
    pci_device_id: "0x0076"
    shape_per_node: 2x2x1
    shape_per_segment: 4x4x4
    nodes_per_pod: 2304
  - type: v6e
    pci_device_id: "0x006f"
    shape_per_node: 2x2
    shape_per_segment: 16x16
    nodes_per_pod: 64
"""

TPU_YAML_4NODE_SEG = """tpu_types:
  - type: tpu7x
    pci_device_id: "0x0076"
    shape_per_node: 2x2x1
    shape_per_segment: 2x2x4
    nodes_per_pod: 2304
"""

GRES_CONF = """AutoDetect=off
Name=tpu Type=tpu7x Count=4
"""


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/sbatch",
        reason="Issue 51191: sbatch --layout and job_submit/tpu added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="Issue 51191: scontrol shows Layout since 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("JobSubmitPlugins", "tpu")
    atf.require_config_parameter_includes("GresTypes", "tpu")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_config_file("tpu.yaml", TPU_YAML_16NODE_SEG)
    atf.require_config_file("gres.conf", GRES_CONF)
    atf.require_nodes(NODE_COUNT, [("Gres", "tpu:tpu7x:4"), ("CPUs", 8)])
    atf.require_slurm_running()


def _use_topology(topo_yaml, tpu_yaml=TPU_YAML_16NODE_SEG):
    atf.require_config_file("topology.yaml", topo_yaml)
    atf.require_config_file("tpu.yaml", tpu_yaml)
    atf.restart_slurmctld(quiet=True)


def _submit_and_wait(layout_arg):
    job_id = atf.submit_job_sbatch(
        f"{layout_arg} --output=/dev/null --wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Job {job_id} ({layout_arg}) failed to start"
    return job_id


def _nodes(first, last):
    return {f"node{n}" for n in range(first, last + 1)}


def _allocated(job_id):
    return set(atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList")))


def _check_job(job_id, shape, expected_nodes, valid_blocks):
    assert (
        atf.get_job_parameter(job_id, "Layout") == f"tpu7x={shape}"
    ), f"The layout of job {job_id} should be stored"
    assert (
        int(atf.get_job_parameter(job_id, "NumNodes")) == expected_nodes
    ), f"tpu7x={shape} should need {expected_nodes} nodes"
    allocated = _allocated(job_id)
    assert any(
        allocated <= block for block in valid_blocks
    ), f"Expected tpu7x={shape} within one of {valid_blocks}, got {allocated}"


def test_subcube_cluster():
    """Sub-cube cluster (4 nodes): base blocks of size 2, aggregated block of size 4."""
    topo_yaml = """- topology: topo-block
  cluster_default: true
  block:
    block_sizes: [2, 4]
    blocks:
    - block: b0_2x2x2_0
      nodes: node[1-2]
    - block: b0_2x2x2_1
      nodes: node[3-4]
"""
    _use_topology(topo_yaml)

    out = atf.run_command_output("scontrol show topology", fatal=True)
    for expected in (
        "BlockName=b0_2x2x2_0",
        "BlockName=b0_2x2x2_1",
        "BlockSize=2",
        "BlockSize=4",
        "AggregatedBlock=b0_2x2x2_[0-1]",
    ):
        assert expected in out, f"Expected {expected} in the topology:\n{out}"

    base = [_nodes(1, 2), _nodes(3, 4)]
    for shape, expected_nodes, valid_blocks in [
        ("2x2x1", 1, base),
        ("2x2x2", 2, base),
        ("2x2x4", 4, [_nodes(1, 4)]),
    ]:
        job_id = _submit_and_wait(f"--layout=tpu7x={shape}")
        _check_job(job_id, shape, expected_nodes, valid_blocks)
        atf.cancel_all_jobs(fatal=True, quiet=True)


def test_single_cube_ordered():
    """Single-cube cluster (16 nodes, ordered names): full hierarchy and sub-cube allocations."""
    topo_yaml = """- topology: topo-block
  cluster_default: true
  block:
    block_sizes: [2, 4, 8, 16]
    blocks:
    - block: b0_2x2x2_0
      nodes: node[1-2]
    - block: b0_2x2x2_1
      nodes: node[3-4]
    - block: b0_2x2x2_2
      nodes: node[5-6]
    - block: b0_2x2x2_3
      nodes: node[7-8]
    - block: b0_2x2x2_4
      nodes: node[9-10]
    - block: b0_2x2x2_5
      nodes: node[11-12]
    - block: b0_2x2x2_6
      nodes: node[13-14]
    - block: b0_2x2x2_7
      nodes: node[15-16]
"""
    _use_topology(topo_yaml)

    out = atf.run_command_output("scontrol show topology", fatal=True)
    for i in range(8):
        assert (
            f"BlockName=b0_2x2x2_{i}" in out
        ), f"Expected block b0_2x2x2_{i} in the topology:\n{out}"
    for aggregated in ("0-1", "2-3", "4-5", "6-7", "0-3", "4-7", "0-7"):
        assert (
            f"AggregatedBlock=b0_2x2x2_[{aggregated}]" in out
        ), f"Expected aggregated block b0_2x2x2_[{aggregated}]:\n{out}"

    def blocks(size):
        return [_nodes(first, first + size - 1) for first in range(1, 17, size)]

    for shape, expected_nodes in [
        ("2x2x1", 1),
        ("2x2x2", 2),
        ("2x2x4", 4),
        ("2x4x4", 8),
        ("4x4x4", 16),
    ]:
        job_id = _submit_and_wait(f"--layout=tpu7x={shape}")
        _check_job(job_id, shape, expected_nodes, blocks(max(expected_nodes, 2)))
        atf.cancel_all_jobs(fatal=True, quiet=True)


def test_single_cube_unordered():
    """Single-cube cluster with unordered hostnames aggregates by block definition order."""
    topo_yaml = """- topology: topo-block
  cluster_default: true
  block:
    block_sizes: [2, 4, 8, 16]
    blocks:
    - block: b0_2x2x2_0
      nodes: node[13,5]
    - block: b0_2x2x2_1
      nodes: node[10,2]
    - block: b0_2x2x2_2
      nodes: node[16,8]
    - block: b0_2x2x2_3
      nodes: node[4,12]
    - block: b0_2x2x2_4
      nodes: node[1,9]
    - block: b0_2x2x2_5
      nodes: node[6,14]
    - block: b0_2x2x2_6
      nodes: node[3,11]
    - block: b0_2x2x2_7
      nodes: node[15,7]
"""
    _use_topology(topo_yaml)

    out = atf.run_command_output("scontrol show topology", fatal=True)
    for expected in (
        "BlockName=b0_2x2x2_0 BlockIndex=0 Nodes=node[13,5] BlockSize=2",
        "BlockName=b0_2x2x2_1 BlockIndex=1 Nodes=node[10,2] BlockSize=2",
        "AggregatedBlock=b0_2x2x2_[0-1]",
        "Nodes=node[2,5,10,13]",
        "AggregatedBlock=b0_2x2x2_[0-7]",
        "Nodes=node[1-16]",
    ):
        assert expected in out, f"Expected {expected} in the topology:\n{out}"

    valid_4node = [
        {"node2", "node5", "node10", "node13"},
        {"node4", "node8", "node12", "node16"},
        {"node1", "node6", "node9", "node14"},
        {"node3", "node7", "node11", "node15"},
    ]
    job_id = _submit_and_wait("--layout=tpu7x=2x2x4")
    allocated = _allocated(job_id)
    assert (
        allocated in valid_4node
    ), f"Expected 4-node job to land on an aggregated block {valid_4node}, got {allocated}"


def test_multi_segment_cluster():
    """Multi-segment requests allocate discrete segments via topology/block."""
    topo_blocks = []
    for seg in range(4):
        for b in range(2):
            n_start = seg * 4 + b * 2 + 1
            n_end = n_start + 1
            topo_blocks.append(
                f"    - block: b0_c{seg}_{b}\n      nodes: node[{n_start}-{n_end}]"
            )

    topo_yaml = (
        "- topology: topo-block\n"
        "  cluster_default: true\n"
        "  block:\n"
        "    block_sizes: [2, 4]\n"
        "    blocks:\n" + "\n".join(topo_blocks) + "\n"
    )
    _use_topology(topo_yaml, tpu_yaml=TPU_YAML_4NODE_SEG)

    out = atf.run_command_output("scontrol show topology", fatal=True)
    for expected in (
        "AggregatedBlock=b0_c0_[0-1]",
        "Nodes=node[1-4]",
        "AggregatedBlock=b0_c3_[0-1]",
        "Nodes=node[13-16]",
    ):
        assert expected in out, f"Expected {expected} in the topology:\n{out}"

    segments = [_nodes(first, first + 3) for first in range(1, 17, 4)]
    for shape, expected_nodes in [("2x2x4", 4), ("2x2x8", 8), ("2x2x16", 16)]:
        job_id = _submit_and_wait(f"--layout=tpu7x={shape}")
        assert (
            int(atf.get_job_parameter(job_id, "NumNodes")) == expected_nodes
        ), f"tpu7x={shape} should need {expected_nodes} nodes"
        allocated = _allocated(job_id)
        assert all(
            segment <= allocated or not (segment & allocated) for segment in segments
        ), f"Expected tpu7x={shape} on whole segments {segments}, got {allocated}"
        atf.cancel_all_jobs(fatal=True, quiet=True)
