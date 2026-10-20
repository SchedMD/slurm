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
        "sbin/slurmctld",
        reason="TPU layout added in 26.11",
    )
    atf.require_auto_config("wants to configure topology.yaml and tpu.yaml")
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


@pytest.fixture(scope="function", autouse=True)
def cleanup():
    yield
    atf.cancel_all_jobs(quiet=True)


def _use_topology(topo_yaml, tpu_yaml=TPU_YAML_16NODE_SEG):
    conf_dir = atf.properties["slurm-config-dir"]
    slurm_user = atf.properties["slurm-user"]
    atf.run_command(
        f"cat > {conf_dir}/topology.yaml",
        input=topo_yaml,
        user=slurm_user,
        fatal=True,
        quiet=True,
    )
    atf.run_command(
        f"cat > {conf_dir}/tpu.yaml",
        input=tpu_yaml,
        user=slurm_user,
        fatal=True,
        quiet=True,
    )
    atf.restart_slurmctld(quiet=True)


def _submit_and_wait(layout_arg):
    job_id = atf.submit_job_sbatch(
        f"{layout_arg} --output=/dev/null --wrap='sleep 60'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", timeout=30
    ), f"Job {job_id} ({layout_arg}) failed to start"
    return job_id


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
    assert "BlockName=b0_2x2x2_0" in out
    assert "BlockName=b0_2x2x2_1" in out
    assert "BlockSize=2" in out
    assert "BlockSize=4" in out
    assert "AggregatedBlock=b0_2x2x2_[0-1]" in out

    for shape, expected_nodes in [("2x2x1", 1), ("2x2x2", 2), ("2x2x4", 4)]:
        job_id = _submit_and_wait(f"--layout=tpu7x={shape}")
        assert atf.get_job_parameter(job_id, "Layout") == f"tpu7x={shape}"
        assert int(atf.get_job_parameter(job_id, "NumNodes")) == expected_nodes
        atf.cancel_all_jobs(quiet=True)


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
        assert f"BlockName=b0_2x2x2_{i}" in out
    assert "AggregatedBlock=b0_2x2x2_[0-1]" in out
    assert "AggregatedBlock=b0_2x2x2_[2-3]" in out
    assert "AggregatedBlock=b0_2x2x2_[4-5]" in out
    assert "AggregatedBlock=b0_2x2x2_[6-7]" in out
    assert "AggregatedBlock=b0_2x2x2_[0-3]" in out
    assert "AggregatedBlock=b0_2x2x2_[4-7]" in out
    assert "AggregatedBlock=b0_2x2x2_[0-7]" in out

    for shape, expected_nodes in [
        ("2x2x1", 1),
        ("2x2x2", 2),
        ("2x2x4", 4),
        ("2x4x4", 8),
        ("4x4x4", 16),
    ]:
        job_id = _submit_and_wait(f"--layout=tpu7x={shape}")
        assert atf.get_job_parameter(job_id, "Layout") == f"tpu7x={shape}"
        assert int(atf.get_job_parameter(job_id, "NumNodes")) == expected_nodes
        atf.cancel_all_jobs(quiet=True)


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
    assert "BlockName=b0_2x2x2_0 BlockIndex=0 Nodes=node[13,5] BlockSize=2" in out
    assert "BlockName=b0_2x2x2_1 BlockIndex=1 Nodes=node[10,2] BlockSize=2" in out
    assert "AggregatedBlock=b0_2x2x2_[0-1]" in out
    assert "Nodes=node[2,5,10,13]" in out
    assert "AggregatedBlock=b0_2x2x2_[0-7]" in out
    assert "Nodes=node[1-16]" in out

    valid_4node = [
        {"node2", "node5", "node10", "node13"},
        {"node4", "node8", "node12", "node16"},
        {"node1", "node6", "node9", "node14"},
        {"node3", "node7", "node11", "node15"},
    ]
    job_id = _submit_and_wait("--layout=tpu7x=2x2x4")
    allocated = set(atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList")))
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
    assert "AggregatedBlock=b0_c0_[0-1]" in out
    assert "Nodes=node[1-4]" in out
    assert "AggregatedBlock=b0_c3_[0-1]" in out
    assert "Nodes=node[13-16]" in out

    # Single segment (4 nodes)
    job4 = _submit_and_wait("--layout=tpu7x=2x2x4")
    assert int(atf.get_job_parameter(job4, "NumNodes")) == 4
    atf.cancel_all_jobs(quiet=True)

    # Two segments (8 nodes)
    job8 = _submit_and_wait("--layout=tpu7x=2x2x8")
    assert int(atf.get_job_parameter(job8, "NumNodes")) == 8
    atf.cancel_all_jobs(quiet=True)

    # Four segments (16 nodes)
    job16 = _submit_and_wait("--layout=tpu7x=2x2x16")
    assert int(atf.get_job_parameter(job16, "NumNodes")) == 16
