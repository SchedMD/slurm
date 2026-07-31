############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Job-scoped GPU requests combined with --segment on topology/block.

Ticket 25621: --gpus (and other forms that synthesize a job-scoped GPU
count) combined with --segment never scheduled, because the whole-job
GPU target was not apportioned per segment. Verify the job now allocates
and that a count not divisible by the segment count is rejected.
"""

import itertools

import pytest

import atf

# 8 nodes, each with 4 GPUs, split into four topology/block base blocks.
NUM_NODES = 8
GPUS_PER_NODE = 4

# Jobs take half the cluster, so the block plugin has more than one candidate
# node set and segment placement can be got wrong.
JOB_NODES = 4

topology_conf = """
    BlockName=b1 Nodes=node[1-2]
    BlockName=b2 Nodes=node[3-4]
    BlockName=b3 Nodes=node[5-6]
    BlockName=b4 Nodes=node[7-8]
    BlockSizes=2,4,8
"""

BLOCK_SIZE = 2
BASE_BLOCKS = [
    {"node1", "node2"},
    {"node3", "node4"},
    {"node5", "node6"},
    {"node7", "node8"},
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "sbin/slurmctld",
        reason="Ticket 25621: per-segment job-scoped GRES was added in 26.05",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core")
    atf.require_config_parameter("TopologyPlugin", "topology/block")
    atf.require_config_parameter_includes("GresTypes", "gpu")

    # Fake GPUs: one tty per GPU on a node (shared across nodes on this host).
    for tty_num in range(GPUS_PER_NODE):
        atf.require_tty(tty_num)
    atf.require_config_parameter(
        "Name", {"gpu": {"File": f"/dev/tty[0-{GPUS_PER_NODE - 1}]"}}, source="gres"
    )
    atf.require_nodes(NUM_NODES, [("Gres", f"gpu:{GPUS_PER_NODE}"), ("CPUs", 8)])

    atf.require_config_file("topology.conf", topology_conf)
    atf.require_slurm_running()


def _valid_placements(num_nodes):
    """Node sets that are a union of whole base blocks."""
    return [
        set().union(*combo)
        for combo in itertools.combinations(BASE_BLOCKS, num_nodes // BLOCK_SIZE)
    ]


def _submit_and_get_alloc(job_args):
    """Submit a job, wait until it runs and return (job_id, num_nodes, job_gres)."""
    job_id = atf.submit_job("sbatch", f"-t1 {job_args}", "sleep infinity", fatal=True)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Job with '{job_args}' should have started running"
    num_nodes = int(atf.get_job_parameter(job_id, "NumNodes", fatal=True))
    job_gres = atf.get_job_parameter(job_id, "JOB_GRES", fatal=True)
    return job_id, num_nodes, job_gres


# Job-scoped GPU requests (gres_per_job) combined with multiple segments used
# to fail because the whole-job GPU target was not apportioned per segment.
# --gpus-per-node is node-scoped and always worked; keep it as a control.
# -N8 --segment=2 is four segments, so the accumulator is reset more than once.
# -N4 --segment=4 is a single segment, which the divisibility rule does not apply
# to: the same --gpus=7 is rejected at --segment=2.
runnable_params = [
    ("-N4 --gpus=16 --segment=2", 4, 16),
    ("-N4 --gpus=8 --segment=2", 4, 8),
    ("-N4 --gpus-per-node=4 --segment=2", 4, 16),
    ("-N4 -n16 --gpus-per-task=1 --segment=2", 4, 16),
    ("-N8 --gpus=32 --segment=2", 8, 32),
    ("-N4 --gpus=7 --segment=4", 4, 7),
]


@pytest.mark.parametrize("job_args,exp_nodes,exp_gpus", runnable_params)
def test_segment_gpus_allocates(job_args, exp_nodes, exp_gpus):
    """Segmented job-scoped GPU requests allocate all nodes and GPUs."""
    job_id, num_nodes, job_gres = _submit_and_get_alloc(job_args)
    assert num_nodes == exp_nodes, f"Expected {exp_nodes} nodes, got {num_nodes}"
    assert (
        job_gres == f"gpu:{exp_gpus}"
    ), f"Expected JOB_GRES gpu:{exp_gpus}, got '{job_gres}'"
    node_list = atf.get_job_parameter(job_id, "NodeList", fatal=True)
    valid = _valid_placements(exp_nodes)
    assert (
        set(atf.node_range_to_list(node_list)) in valid
    ), f"Each segment should fill whole base blocks {valid}, got '{node_list}'"


def test_segment_gpus_indivisible_rejected():
    """A job-scoped GPU count not divisible by the segment count is rejected
    at submit.

    -N4 --segment=2 gives 2 segments; --gpus=7 is not divisible by 2.
    """
    result = atf.run_command(
        f'sbatch -t1 -N{JOB_NODES} --segment=2 --gpus=7 --wrap="true"', xfail=True
    )
    assert (
        result["exit_code"] != 0
    ), "Indivisible job-scoped GPU request should be rejected at submit"
    assert (
        "Requested topology configuration is not available" in result["stderr"]
    ), f"Expected the topology rejection, got '{result['stderr']}'"
