############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re

import pytest

import atf


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_slurm_running()


def test_overcommit():
    """Verify that srun --overcommit will run with more tasks than the node has cpus"""

    # Find an idle node with at least 1 cpu
    eligible_node = None
    for node in atf.nodes:
        if "IDLE" in atf.nodes[node]["state"] and atf.nodes[node]["cpus"] > 0:
            eligible_node = node
            cpu_count = atf.nodes[node]["cpus"]
            break

    if eligible_node is None:
        pytest.skip("This test requires at least one idle node with a cpu")

    results = atf.run_command(
        f"srun -N 1 -w {eligible_node} -n {cpu_count + 1} --overcommit -v true"
    )

    assert results["exit_code"] == 0
    assert (
        re.search(rf"srun: ntasks\s+: {cpu_count + 1}", results["stderr"]) is not None
    )


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Ticket 25536: overcommit with cpus-per-task placement fix",
)
def test_overcommit_cpus_per_task():
    """Verify srun -n2 -c1 --overcommit runs on a one-CPU node.

    Ticket 25536: -cN sets tres_per_task=cpu=N; a cpu-only tres_per_task
    must keep the overcommit placement path instead of leaving the job
    pending with Reason Resources.
    """
    # Pin to a one-CPU node so -n2 cannot fit without overcommit.
    eligible_node = None
    for node in atf.nodes:
        if "IDLE" in atf.nodes[node]["state"] and atf.nodes[node]["cpus"] == 1:
            eligible_node = node
            break
    if eligible_node is None:
        pytest.skip("This test requires one idle node with exactly 1 CPU")

    results = atf.run_command(
        f"srun -N1 -w {eligible_node} -n2 -c1 --overcommit -t1 -v /bin/true",
        fatal=True,
    )
    assert (
        re.search(r"srun: ntasks\s+: 2", results["stderr"]) is not None
    ), "srun should launch 2 tasks on the one-CPU node with overcommit"
