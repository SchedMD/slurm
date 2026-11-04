############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A node list shorter than the job is not its whole allocation.

Only a --nodelist as long as a fixed node count is the whole allocation of a
job, and excludes every other node. sbatch(1) documents that otherwise
"additional resources will be allocated on other nodes as needed", so a job
with a node range, a node count derived from its tasks, or --ntasks-per-node
must still add nodes beyond a shorter list.
"""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_nodes(2, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def nodes():
    """(node_a, node_b, exclude): two test nodes and -x for the rest.

    require_nodes() guarantees at least two nodes. The job below could take
    any further node as its second one, so it excludes the others.
    """

    pair = atf.run_job_nodes("-N2 true", fatal=True)
    assert len(pair) == 2, f"Expected two allocated nodes, got {pair}"
    others = sorted(set(atf.get_nodes(quiet=True)) - set(pair))
    exclude = f"-x {','.join(others)}" if others else ""
    return pair[0], pair[1], exclude


@pytest.mark.parametrize(
    "args",
    ["-N1-2 -n{tasks}", "-n{tasks}", "-n{tasks} --ntasks-per-node={cpus}"],
    ids=["node_range", "derived_count", "ntasks_per_node"],
)
def test_node_list_shorter_than_job_adds_nodes(nodes, args):
    """-w node_a with one task more than node_a has CPUs also gets node_b."""

    node_a, node_b, exclude = nodes
    cpus = int(atf.get_node_parameter(node_a, "cpus"))
    args = args.format(tasks=cpus + 1, cpus=cpus)
    job = atf.submit_job_sbatch(
        f"{args} -w {node_a} {exclude} -t1 -o /dev/null --wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job, "RUNNING"
    ), f"Job ({job}) never started; it should add node_b to its node list"
    allocated = atf.node_range_to_list(
        atf.get_job_parameter(job, "NodeList", quiet=True)
    )
    assert sorted(allocated) == sorted(
        [node_a, node_b]
    ), f"Job ({job}) should run on {[node_a, node_b]}, got {allocated}"
