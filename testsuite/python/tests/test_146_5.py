############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that jobs using --mem and --*-per-* options respect MaxMemPerCPU."""

import pytest

import atf

MAX_MEM_PER_CPU = 1875
MEM_PER_NODE = 15360
LIMIT_PART = "max_mem_cpu"
CONTROL_PART = "no_mem_limit"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter("MaxMemPerCPU", None)
    atf.require_config_parameter("DefCpuPerGPU", None)
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_config_parameter(
        "Name", {"gpu": {"File": "/dev/tty[0-1]"}}, source="gres"
    )
    nodes = atf.require_nodes(
        4,
        [
            ("CPUs", 16),
            ("Sockets", 2),
            ("CoresPerSocket", 8),
            ("ThreadsPerCore", 1),
            ("RealMemory", 30000),
            ("Gres", "gpu:2"),
        ],
    )
    atf.require_config_parameter(
        "PartitionName",
        {
            "primary": {"Nodes": "ALL", "Default": "YES"},
            LIMIT_PART: {
                "Nodes": atf.node_list_to_range(nodes[0:2]),
                "MaxMemPerCPU": MAX_MEM_PER_CPU,
            },
            CONTROL_PART: {"Nodes": atf.node_list_to_range(nodes[2:4])},
        },
    )
    atf.require_slurm_running()

    if atf.get_partition_parameter(LIMIT_PART, "MaxMemPerCPU") != MAX_MEM_PER_CPU:
        pytest.fail(f"Partition {LIMIT_PART} needs MaxMemPerCPU={MAX_MEM_PER_CPU}")


def _allocation(job_id):
    job = atf.get_jobs(job_id, use_json=True, fatal=True)[job_id]
    return job["job_resources"]["nodes"]["allocation"]


@pytest.mark.parametrize(
    "job_args",
    [
        pytest.param("-N1 --ntasks-per-node=4", id="ntasks-per-node"),
        pytest.param("-N1 --cpus-per-task=5", id="cpus-per-task"),
        pytest.param("-N1 -n4 --ntasks-per-socket=2", id="ntasks-per-socket"),
        pytest.param("-N1 -n1", id="ntasks"),
        pytest.param("-N2 --ntasks-per-node=4", id="ntasks-per-node-2-nodes"),
        pytest.param("-N1-2 --ntasks-per-node=4", id="ntasks-per-node-range"),
        pytest.param("-N1 --gpus=1 --cpus-per-gpu=5", id="cpus-per-gpu"),
        pytest.param("-N1 --gpus-per-node=2", id="gpus-per-node"),
        pytest.param(
            "-N1 -n1 -c1 --gpus-per-node=1", id="ntasks-cpus-per-task-gpus-per-node"
        ),
        pytest.param(
            "-N1 --gpus-per-socket=1 --sockets-per-node=2", id="gpus-per-socket"
        ),
        pytest.param("-N1 -n2 --gpus-per-task=1", id="gpus-per-task"),
        pytest.param("-N1 --gpus=2 --ntasks-per-gpu=2", id="ntasks-per-gpu"),
        pytest.param("-N2 -n4 --gpus=2", id="gpus-2-nodes"),
    ],
)
def test_max_mem_per_cpu(job_args):
    """Verify a job gets enough CPUs on each node to respect MaxMemPerCPU"""

    sbatch_args = f"{job_args} --mem={MEM_PER_NODE} --wrap 'sleep infinity'"
    control_id = atf.submit_job_sbatch(f"-p {CONTROL_PART} {sbatch_args}", fatal=True)
    job_id = atf.submit_job_sbatch(f"-p {LIMIT_PART} {sbatch_args}")

    # Without the limit, the request has too few CPUs for its memory
    atf.wait_for_job_state(control_id, "RUNNING", fatal=True)
    if all(
        node["cpus"]["count"] * MAX_MEM_PER_CPU >= MEM_PER_NODE
        for node in _allocation(control_id)
    ):
        pytest.fail(f"Control job {control_id} has enough CPUs, no upscaling needed")

    assert job_id, "Job should be accepted"
    assert atf.wait_for_job_state(job_id, "RUNNING"), "Job should start"

    allocation = _allocation(job_id)
    assert allocation, "Job should have allocated nodes"
    for node in allocation:
        name = node["name"]
        cpus = node["cpus"]["count"]
        mem = node["memory"]["allocated"]
        assert (
            mem == MEM_PER_NODE
        ), f"Node {name} should have {MEM_PER_NODE} MB, not {mem} MB"
        assert (
            mem <= cpus * MAX_MEM_PER_CPU
        ), f"Node {name} has {mem} MB for {cpus} CPUs, over MaxMemPerCPU"
