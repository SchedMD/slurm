############################################################################
# "Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
import atf
import pytest
import re


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
    expected = ["node1", "node3", "node5", "node7", "node2", "node4", "node6", "node8"]

    assert actual == expected, (
        f"Tasks should follow topology block order: "
        f"expected {expected}, got {actual}"
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
