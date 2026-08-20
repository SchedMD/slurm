############################################################################
# "Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
import re

import pytest

import atf


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(8)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter("TopologyPlugin", "topology/block")
    atf.require_version((26, 5), "sbin/slurmctld")
    topology_config = """
        BlockName=b1 Nodes=node[1,3,5,7]
        BlockName=b2 Nodes=node[2,4,6,8]
        BlockSizes=4,8
    """
    atf.require_config_file("topology.conf", topology_config)
    atf.require_slurm_running()


# b1 before b2, so topology order differs from both bitmap and alphabetical
block_order = ["node1", "node3", "node5", "node7", "node2", "node4", "node6", "node8"]


def _parse_task_nodes(output):
    """Parse 'tid: nodename' lines, return list of (tid, node) sorted by tid."""
    matches = re.findall(r"(\d+): (\S+)", output)
    return sorted(matches, key=lambda x: int(x[0]))


def test_topo_task_order():
    """Verify tasks are distributed following topology block order,
    not bitmap order."""

    output = atf.run_job_output(
        "-N8 -n8 --exclusive --mem=1 -m block -l printenv SLURMD_NODENAME",
        fatal=True,
    )
    tasks = _parse_task_nodes(output)
    actual = [node for _, node in tasks]

    assert actual == block_order, (
        f"Tasks should follow topology block order: "
        f"expected {block_order}, got {actual}"
    )


def test_topo_emitted_lists_match_task_order():
    """The emitted node lists follow the same block order as the tasks.

    SLURM_NODEID is a position in SLURM_STEP_NODELIST, so a list left in
    bitmap order would report task 0 on a node other than the one it ran on.
    """
    for component in ["sbin/slurmctld", "sbin/slurmd"]:
        atf.require_version(
            (26, 11),
            component,
            reason=f"Issue 50993: {component} emits node lists in topology-rank "
            f"order in 26.11+",
        )

    output = atf.run_job_output(
        "-N8 -n8 --exclusive --mem=1 -m block -l bash -c "
        "'echo $SLURM_NODEID $SLURMD_NODENAME $SLURM_STEP_NODELIST "
        "$SLURM_JOB_NODELIST'",
        fatal=True,
    )

    matches = re.findall(r"(\d+): (\d+) (\S+) (\S+) (\S+)", output)
    matches.sort(key=lambda x: int(x[0]))
    assert matches, f"Expected tasks to report their placement, got: {output}"

    nodes = [m[2] for m in matches]
    node_ids = [int(m[1]) for m in matches]
    step_list, job_list = matches[0][3], matches[0][4]

    assert nodes == block_order, f"Tasks should follow {block_order}, got {nodes}"
    assert (
        atf.node_range_to_list(step_list) == block_order
    ), f"SLURM_STEP_NODELIST should be {block_order}, got {step_list}"
    assert (
        atf.node_range_to_list(job_list) == block_order
    ), f"SLURM_JOB_NODELIST should be {block_order}, got {job_list}"
    assert node_ids == list(range(len(block_order))), (
        f"SLURM_NODEID should index SLURM_STEP_NODELIST: expected "
        f"{list(range(len(block_order)))}, got {node_ids}"
    )


def test_topo_multi_step_rotation():
    """Verify consecutive steps rotate across topology blocks."""

    job_id = atf.submit_job_sbatch(
        "-N8 --exclusive --mem=1 --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_step(job_id, "batch")

    step1_output = atf.run_job_output(
        f"--jobid={job_id} -N3 -n3 -m block -l printenv SLURMD_NODENAME", fatal=True
    )
    step2_output = atf.run_job_output(
        f"--jobid={job_id} -N3 -n3 -m block -l printenv SLURMD_NODENAME", fatal=True
    )

    step1_nodes = [node for _, node in _parse_task_nodes(step1_output)]
    step2_nodes = [node for _, node in _parse_task_nodes(step2_output)]

    expected_step1_nodes = ["node1", "node3", "node5"]
    expected_step2_nodes = ["node2", "node4", "node6"]

    assert (
        step1_nodes == expected_step1_nodes
    ), f"Step 1 expected {expected_step1_nodes}, got {step1_nodes}"
    assert (
        step2_nodes == expected_step2_nodes
    ), f"Step 2 expected {expected_step2_nodes}, got {step2_nodes}"
