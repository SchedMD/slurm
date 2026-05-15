############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
import atf
import itertools
import pytest


topology_yaml = """
- topology: topo1
  cluster_default: true
  torus3d:
    toruses:
      - name: pod1
        dims:
          x: 4
          y: 4
          z: 2
        regions:
          - anchor: {x: 0, y: 0, z: 0}
            dims: {x: 4, y: 4, z: 1}
            nodes: node[1-16]
          - anchor: {x: 0, y: 0, z: 1}
            dims: {x: 4, y: 4, z: 1}
            nodes: node[17-32]
        placements:
          - dims:
              x: 2
              y: 2
              z: 2
          - dims:
              x: 2
              y: 2
              z: 1
          - dims:
              x: 4
              y: 4
              z: 2
"""

# All valid anchored 2x2x1 sub-cubes in the 4x4x2 torus (anchor_spacing=dims)
VALID_4NODE = [
    {"node1", "node2", "node5", "node6"},
    {"node3", "node4", "node7", "node8"},
    {"node9", "node10", "node13", "node14"},
    {"node11", "node12", "node15", "node16"},
    {"node17", "node18", "node21", "node22"},
    {"node19", "node20", "node23", "node24"},
    {"node25", "node26", "node29", "node30"},
    {"node27", "node28", "node31", "node32"},
]

# All valid anchored 2x2x2 sub-cubes
VALID_8NODE = [
    {"node1", "node2", "node5", "node6", "node17", "node18", "node21", "node22"},
    {"node3", "node4", "node7", "node8", "node19", "node20", "node23", "node24"},
    {"node9", "node10", "node13", "node14", "node25", "node26", "node29", "node30"},
    {"node11", "node12", "node15", "node16", "node27", "node28", "node31", "node32"},
]

# Valid 16-node allocations under --segment=8: union of any two distinct
# 8-node placements (C(4, 2) = 6 pairs)
VALID_16NODE_SEGMENT = [a | b for a, b in itertools.combinations(VALID_8NODE, 2)]


def assert_valid_placement(job_id, valid_sets):
    """Assert that the job's NodeList matches one of the valid placement sets."""
    node_list = atf.get_job_parameter(job_id, "NodeList")
    nodes = set(atf.node_range_to_list(node_list))
    assert any(
        nodes == v for v in valid_sets
    ), f"Job {job_id} should be allocated in a valid placement ({valid_sets}), but was allocated in {node_list}"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        component="sbin/slurmd",
        reason="topology/torus3d was added in 26.05",
    )
    atf.require_config_parameter_includes("SchedulerParameters", "bf_interval=1")
    atf.require_nodes(32)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_file("topology.yaml", topology_yaml)
    atf.require_slurm_running()


def test_basic_placement():
    """Test basic 4-node placement (2x2x1)"""

    job_id = atf.submit_job_sbatch('-N 4 --exclusive --mem=1 --wrap="hostname"')
    assert job_id != 0, "Job should be accepted"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    assert_valid_placement(job_id, VALID_4NODE)


def test_8_node_placement():
    """Test 8-node placement (2x2x2)"""

    job_id = atf.submit_job_sbatch('-N 8 --exclusive --mem=1 --wrap="hostname"')
    assert job_id != 0, "Job should be accepted"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    assert_valid_placement(job_id, VALID_8NODE)


def test_full_torus():
    """Test full torus allocation (4x4x2 = 32 nodes)"""

    job_id = atf.submit_job_sbatch('-N 32 --exclusive --mem=1 --wrap="hostname"')
    assert job_id != 0, "Job should be accepted"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)


def test_unsupported_size():
    """Test that unsupported placement size is rejected"""

    assert (
        atf.submit_job_sbatch('-N 3 --exclusive --mem=1 --wrap="hostname"', xfail=True)
        == 0
    ), "3-node job should fail -- no 3-node placement exists"


def test_concurrent_placements():
    """Test two concurrent 4-node placements fill without conflict"""

    job_id_1 = atf.submit_job_sbatch('-N 4 --exclusive --mem=1 --wrap="sleep infinity"')
    job_id_2 = atf.submit_job_sbatch('-N 4 --exclusive --mem=1 --wrap="sleep infinity"')
    atf.wait_for_job_state(job_id_1, "RUNNING", fatal=True)
    atf.wait_for_job_state(job_id_2, "RUNNING", fatal=True)

    nodes_1 = set(atf.node_range_to_list(atf.get_job_parameter(job_id_1, "NodeList")))
    nodes_2 = set(atf.node_range_to_list(atf.get_job_parameter(job_id_2, "NodeList")))
    assert not nodes_1.intersection(nodes_2), "Concurrent jobs should not share nodes"


def test_frag_aware_placement():
    """Test that frag-aware placement fills a partially used 2x2x2 block.

    Pin job1 to z=0 anchor (0,0,0): node[3,4,7,8].
    Job2 (also 2x2x1) should land at z=1 same (x,y): node[19,20,23,24],
    completing the 2x2x2 block rather than consuming a fresh z=0 placement
    which would block another 2x2x2 anchor.
    """

    expected_nodes = {"node19", "node20", "node23", "node24"}

    job_id_1 = atf.submit_job_sbatch(
        '-N 4 -w node[3,4,7,8] --exclusive --mem=1 --wrap="sleep infinity"'
    )
    atf.wait_for_job_state(job_id_1, "RUNNING", fatal=True)

    job_id_2 = atf.submit_job_sbatch('-N 4 --exclusive --mem=1 --wrap="sleep infinity"')
    atf.wait_for_job_state(job_id_2, "RUNNING", fatal=True)

    nodes_2 = set(atf.node_range_to_list(atf.get_job_parameter(job_id_2, "NodeList")))
    assert nodes_2 == expected_nodes, (
        f"Frag-aware placement should fill z=1 below job1 (node[19,20,23,24]), "
        f"but job2 got {nodes_2}"
    )


def test_torus_constrained():
    """Test that allocating all z=0 nodes prevents 8-node (2x2x2) jobs.

    Torus 4x4x2 flat mapping (x varies fastest):
      z=0: node[1-16], z=1: node[17-32]
    Every 2x2x2 placement needs 4 nodes from z=0.
    With all z=0 nodes busy, no 2x2x2 placement can be satisfied,
    even though 16 z=1 nodes are idle.
    """

    z0_placements = [
        "node[1,2,5,6]",
        "node[3,4,7,8]",
        "node[9,10,13,14]",
        "node[11,12,15,16]",
    ]
    job_ids = []
    for nodes in z0_placements:
        job_id = atf.submit_job_sbatch(
            f'-N 4 -w {nodes} --exclusive --mem=1 --wrap="sleep infinity"'
        )
        job_ids.append(job_id)

    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    job_id_big = atf.submit_job_sbatch(
        '-N 8 --exclusive --mem=1 --wrap="sleep infinity"'
    )
    assert job_id_big != 0, "Job should be accepted"
    atf.wait_for_job_state(
        job_id_big,
        "PENDING",
        desired_reason="No_suitable_topology_unit_found",
        fatal=True,
    )


def test_segment():
    """Test segment support -- 16 nodes as 2 segments of 8"""

    job_id = atf.submit_job_sbatch(
        '-N 16 --segment=8 --exclusive --mem=1 --wrap="hostname"'
    )
    assert job_id != 0, "Segmented job should be accepted"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    assert_valid_placement(job_id, VALID_16NODE_SEGMENT)


def test_segment_bad_divisor():
    """Test that segment size not dividing job size is rejected"""

    assert (
        atf.submit_job_sbatch(
            '-N 8 --segment=3 --exclusive --mem=1 --wrap="hostname"',
            xfail=True,
        )
        == 0
    ), "Job should fail -- 8 % 3 != 0"
