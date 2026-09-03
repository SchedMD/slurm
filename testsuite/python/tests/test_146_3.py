############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Tests for the partition MinNodes limit with task-count jobs."""

import os

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
PARTITION_NAME = "min_nodes_part"
MIN_NODES = 2
LOW_MIN_NODES = 1


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("EnforcePartLimits", ["NO", None])
    atf.require_nodes(2, [("CPUs", 4), ("RealMemory", 1024)])
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def partition():
    created = []

    def _create(name, min_nodes):
        nodes = list(atf.get_nodes().keys())
        node_range = atf.node_list_to_range(nodes[0:2])

        atf.run_command(
            f"scontrol create PartitionName={name} "
            f"Nodes={node_range} MinNodes={min_nodes}",
            fatal=True,
            user=atf.properties["slurm-user"],
        )
        created.append(name)

    yield _create

    atf.cancel_jobs(atf.properties["submitted-jobs"], quiet=True)
    for name in created:
        atf.run_command(
            f"scontrol delete PartitionName={name}",
            fatal=True,
            user=atf.properties["slurm-user"],
        )


@pytest.fixture(scope="function")
def min_nodes_partition(partition):
    partition(PARTITION_NAME, MIN_NODES)


@pytest.fixture(scope="function")
def unlimited_partition(partition):
    partition(PARTITION_NAME, LOW_MIN_NODES)


def test_job_meets_min_nodes(min_nodes_partition):
    """Verify the implicit max_nodes is not derived below the partition MinNodes"""

    job_id = atf.submit_job_sbatch(
        f"-p {PARTITION_NAME} -n2 --ntasks-per-node=2 -J {test_name}"
        ' -t 1 --wrap "hostname" -o /dev/null',
        fatal=True,
    )
    completed = atf.wait_for_job_state(job_id, "COMPLETED")
    state = atf.get_job_parameter(job_id, "JobState", quiet=True)
    reason = atf.get_job_parameter(job_id, "Reason", quiet=True)

    assert (
        completed
    ), f"Job should have run on {MIN_NODES} nodes: state={state} reason={reason}"

    node_list = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert (
        len(node_list) == MIN_NODES
    ), f"Job should allocate {MIN_NODES} node(s), got {len(node_list)}: {node_list}"


def test_raised_min_nodes_pends_until_job_updated(unlimited_partition):
    """Verify a raised MinNodes pends a no-node-count job until NumNodes is set"""

    job_id = atf.submit_job_sbatch(
        f"--hold -p {PARTITION_NAME} -n2 --ntasks-per-node=2 -J {test_name}"
        ' -t 1 --wrap "hostname" -o /dev/null',
        fatal=True,
    )
    atf.run_command(
        f"scontrol update PartitionName={PARTITION_NAME} MinNodes={MIN_NODES}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.wait_for_job_state(
        job_id, "PENDING", desired_reason="PartitionNodeLimit", fatal=True
    )

    atf.run_command(
        f"scontrol update jobid={job_id} NumNodes={MIN_NODES}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    node_list = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert (
        len(node_list) == MIN_NODES
    ), f"Job should allocate {MIN_NODES} node(s), got {len(node_list)}: {node_list}"
