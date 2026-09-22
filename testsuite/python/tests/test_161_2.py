############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Job-scoped GPU requests combined with --segment on topology/torus3d.

Ticket 25621: --gpus (and other forms that synthesize a job-scoped GPU
count) combined with --segment never scheduled, because the whole-job
GPU target was not apportioned per segment. Verify the job now allocates
and that a count not divisible by the segment count is rejected.
"""

import itertools

import pytest

import atf

# 32 nodes, each with 4 GPUs, in a 4x4x2 torus with 2x2x2 (8-node) placements.
NUM_NODES = 32
GPUS_PER_NODE = 4

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
"""


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


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        component="sbin/slurmd",
        reason="Ticket 25621: per-segment job-scoped GRES was added in 26.05",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core")
    atf.require_config_parameter_includes("GresTypes", "gpu")

    # Fake GPUs: one tty per GPU on a node (shared across nodes on this host).
    for tty_num in range(GPUS_PER_NODE):
        atf.require_tty(tty_num)
    atf.require_config_parameter(
        "Name", {"gpu": {"File": f"/dev/tty[0-{GPUS_PER_NODE - 1}]"}}, source="gres"
    )
    atf.require_nodes(NUM_NODES, [("Gres", f"gpu:{GPUS_PER_NODE}"), ("CPUs", 8)])

    atf.require_config_file("topology.yaml", topology_yaml)
    atf.require_slurm_running()


def assert_valid_placement(job_id, valid_sets):
    """Assert that the job's NodeList matches one of the valid placement sets."""
    node_list = atf.get_job_parameter(job_id, "NodeList", fatal=True)
    nodes = set(atf.node_range_to_list(node_list))
    assert any(
        nodes == v for v in valid_sets
    ), f"Job {job_id} should be allocated in a valid placement ({valid_sets}), but was allocated in {node_list}"


def _submit_and_get_alloc(job_args):
    """Submit a job, wait until it runs and return (job_id, num_nodes, job_gres)."""
    job_id = atf.submit_job("sbatch", f"-t1 {job_args}", "sleep infinity", fatal=True)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Job with '{job_args}' should have started running"
    num_nodes = int(atf.get_job_parameter(job_id, "NumNodes", fatal=True))
    job_gres = atf.get_job_parameter(job_id, "JOB_GRES", fatal=True)
    return job_id, num_nodes, job_gres


# 16 nodes = 2 segments of 8, each an 8-node (2x2x2) placement. Job-scoped GPU
# requests used to fail because the whole-job GPU target was not apportioned
# per segment. --gpus-per-node is node-scoped and always worked; it is a
# control.
runnable_params = [
    ("--gpus=64 --segment=8", 16, 64),
    ("--gpus=32 --segment=8", 16, 32),
    ("--gpus-per-node=4 --segment=8", 16, 64),
]


@pytest.mark.parametrize("job_args,exp_nodes,exp_gpus", runnable_params)
def test_segment_gpus_allocates(job_args, exp_nodes, exp_gpus):
    """Segmented job-scoped GPU requests allocate all nodes and GPUs."""
    job_id, num_nodes, job_gres = _submit_and_get_alloc(f"-N16 {job_args}")
    assert num_nodes == exp_nodes, f"Expected {exp_nodes} nodes, got {num_nodes}"
    assert (
        job_gres == f"gpu:{exp_gpus}"
    ), f"Expected JOB_GRES gpu:{exp_gpus}, got '{job_gres}'"
    assert_valid_placement(job_id, VALID_16NODE_SEGMENT)


def test_segment_gpus_indivisible_rejected():
    """A job-scoped GPU count not divisible by the segment count is rejected
    at submit.

    -N16 --segment=8 gives 2 segments; --gpus=63 is not divisible by 2.
    """
    result = atf.run_command(
        'sbatch -t1 -N16 --segment=8 --gpus=63 --wrap="true"', xfail=True
    )
    assert (
        result["exit_code"] != 0
    ), "Indivisible job-scoped GPU request should be rejected at submit"
    assert (
        "Requested topology configuration is not available" in result["stderr"]
    ), f"Expected the topology rejection, got '{result['stderr']}'"
