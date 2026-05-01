############################################################################
# "Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
import atf
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
        nodes: node[1-32]
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


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        reason="topology/torus3d was added in 26.05",
    )
    atf.require_nodes(32)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_file("topology.yaml", topology_yaml)
    atf.require_slurm_running()


def test_basic_placement():
    """Test basic 4-node placement (2x2x1)"""

    job_id = atf.submit_job_sbatch('-N 4 --exclusive --mem=1 --wrap="hostname"')
    assert job_id != 0, "Job should be accepted"
    assert atf.wait_for_job_state(
        job_id, "DONE", fatal=True, timeout=10
    ), "4-node job should run"


def test_8_node_placement():
    """Test 8-node placement (2x2x2)"""

    job_id = atf.submit_job_sbatch('-N 8 --exclusive --mem=1 --wrap="hostname"')
    assert job_id != 0, "Job should be accepted"
    assert atf.wait_for_job_state(
        job_id, "DONE", fatal=True, timeout=10
    ), "8-node job should run"


def test_full_torus():
    """Test full torus allocation (4x4x2 = 32 nodes)"""

    job_id = atf.submit_job_sbatch('-N 32 --exclusive --mem=1 --wrap="hostname"')
    assert job_id != 0, "Job should be accepted"
    assert atf.wait_for_job_state(
        job_id, "DONE", fatal=True, timeout=10
    ), "Full torus job should run"


def test_unsupported_size():
    """Test that unsupported placement size is rejected"""

    assert (
        atf.submit_job_sbatch('-N 3 --exclusive --mem=1 --wrap="hostname"', xfail=True)
        == 0
    ), "3-node job should fail -- no 3-node placement exists"


def test_concurrent_placements():
    """Test two concurrent 4-node placements fill without conflict"""

    job_id_1 = atf.submit_job_sbatch('-N 4 --exclusive --mem=1 --wrap="sleep 20"')
    job_id_2 = atf.submit_job_sbatch('-N 4 --exclusive --mem=1 --wrap="sleep 20"')
    atf.wait_for_job_state(job_id_1, "RUNNING", fatal=True, timeout=10)
    atf.wait_for_job_state(job_id_2, "RUNNING", fatal=True, timeout=10)

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
        '-N 4 -w node[3,4,7,8] --exclusive --mem=1 --wrap="sleep 30"'
    )
    atf.wait_for_job_state(job_id_1, "RUNNING", fatal=True, timeout=10)

    job_id_2 = atf.submit_job_sbatch('-N 4 --exclusive --mem=1 --wrap="sleep 30"')
    atf.wait_for_job_state(job_id_2, "RUNNING", fatal=True, timeout=10)

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
            f'-N 4 -w {nodes} --exclusive --mem=1 --wrap="sleep 30"'
        )
        job_ids.append(job_id)

    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=10)

    job_id_big = atf.submit_job_sbatch('-N 8 --exclusive --mem=1 --wrap="sleep 20"')
    assert not atf.wait_for_job_state(
        job_id_big, "RUNNING", timeout=5, xfail=True
    ), "8-node job should not run -- all z=0 nodes busy, no 2x2x2 available"


def test_segment():
    """Test segment support -- 16 nodes as 2 segments of 8"""

    job_id = atf.submit_job_sbatch(
        '-N 16 --segment=8 --exclusive --mem=1 --wrap="hostname"'
    )
    assert job_id != 0, "Segmented job should be accepted"
    assert atf.wait_for_job_state(
        job_id, "DONE", fatal=True, timeout=10
    ), "Segmented job should run"


def test_segment_bad_divisor():
    """Test that segment size not dividing job size is rejected"""

    assert (
        atf.submit_job_sbatch(
            '-N 8 --segment=3 --exclusive --mem=1 --wrap="hostname"',
            xfail=True,
        )
        == 0
    ), "Job should fail -- 8 % 3 != 0"
