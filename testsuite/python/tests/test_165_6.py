############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test a running job keeps its allocated partition across slurmctld restarts.

Ticket 22010: recovery rebuilt a multi-partition job's partition list sorted by
PriorityTier and took its head, so a running job allocated a lower tier
partition was reported in a different partition after a restart.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_config_parameter_includes("GresTypes", "f1")
    atf.require_nodes(5)
    for node in ["node1", "node2"]:
        atf.set_node_parameter(node, "Gres", "f1:2")

    # Only the debug nodes have f1, so a job requesting f1 that also lists
    # debug2 runs in the lower tier preemptable partition instead.
    atf.require_config_parameter(
        "PartitionName",
        {
            "debug": {
                "Nodes": "node[1-2]",
                "Default": "YES",
                "MaxTime": "INFINITE",
                "State": "UP",
                "PriorityTier": "100",
            },
            "debug2": {
                "Nodes": "node[3-5]",
                "MaxTime": "INFINITE",
                "State": "UP",
                "PriorityTier": "100",
            },
            "preemptable": {
                "Nodes": "ALL",
                "MaxTime": "INFINITE",
                "State": "UP",
                "PriorityTier": "1",
            },
        },
    )
    atf.require_slurm_running()
    yield


@pytest.mark.parametrize(
    "sbatch_args, restarts, expected",
    [
        pytest.param(
            "-p preemptable,debug2 --gres=f1:1", 1, "preemptable", id="one_restart"
        ),
        pytest.param(
            "-p debug2,preemptable --gres=f1:1",
            3,
            "preemptable",
            id="multiple_restarts",
        ),
        pytest.param(
            "-N2 -p debug2,preemptable --gres=f1:2",
            1,
            "preemptable",
            id="multi_node_job",
        ),
        pytest.param("-p debug --gres=f1:2", 1, "debug", id="single_partition_debug"),
        pytest.param(
            "-p preemptable --gres=f1:1",
            1,
            "preemptable",
            id="single_partition_preemptable",
        ),
    ],
)
def test_partition_persistence_cases(sbatch_args, restarts, expected):
    job_id = atf.submit_job_sbatch(f"{sbatch_args} --wrap='sleep infinity'", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    partition = atf.run_command_output(
        f"squeue -hj {job_id} -O partition", fatal=True
    ).strip()
    assert (
        partition == expected
    ), f"Job should run in partition {expected}, got {partition}"
    atf.wait_for_job_accounted(
        job_id, field="Partition", value=rf"^{re.escape(partition)}$", fatal=True
    )

    for i in range(restarts):
        atf.restart_slurmctld()

        # Confirm the job survived the restart before checking its partition
        atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

        partition_after = atf.run_command_output(
            f"squeue -hj {job_id} -O partition", fatal=True
        ).strip()
        assert partition_after == partition, (
            f"Partition changed after restart {i + 1}: "
            f"expected {partition}, got {partition_after}"
        )
