############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Overcommit with cpus-per-task across a topology-configured cluster.

Ticket 25536: -cN sets tres_per_task=cpu=N. The topology node-evaluation
path (eval_nodes) must keep the overcommit placement for a cpu-only
tres_per_task so the reported multi-node -N8 -n272 -c1 -O request runs
instead of remaining pending with Reason Resources.
"""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Ticket 25536: overcommit with cpus-per-task placement fix",
    )
    atf.require_nodes(8, [("CPUs", 1)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TopologyPlugin", "topology/block")
    topology_config = """
        BlockName=b1 Nodes=node[1-4]
        BlockName=b2 Nodes=node[5-8]
        BlockSizes=4,8
    """
    atf.require_config_file("topology.conf", topology_config)
    atf.require_slurm_running()


def test_overcommit_cpus_per_task_multinode():
    """Verify -N8 -n272 -c1 --overcommit runs across a topology cluster.

    With one CPU per node the 272 tasks cannot fit unless overcommit
    relaxes the per-node CPU requirement in the topology eval_nodes path.
    """
    job_id = atf.submit_job_sbatch(
        "-N8 -n272 -c1 -O -t1 --wrap '/bin/true'", fatal=True
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
