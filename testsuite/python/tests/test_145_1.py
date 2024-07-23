############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
def setup():
    atf.require_nodes(3, [("CPUs", 1), ("Features", "f1")])
    atf.require_nodes(3, [("CPUs", 1), ("Features", "f2")])
    atf.require_slurm_running()


def get_nodes_with_feature(feat, nodelist):
    nodes = 0
    for node in nodelist:
        if re.search(feat, atf.get_node_parameter(node, "AvailableFeatures")):
            nodes += 1
    return nodes


@pytest.mark.parametrize(
    "f1, f2",
    [
        (1, 1),
        (1, 2),
        (2, 3),
    ],
)
@pytest.mark.parametrize("command", ["srun", "salloc", "sbatch"])
def test_ntasks_multiple_count(f1, f2, command):
    """Test that number of nodes and nodes are the right ones requesting constrains."""

    # Submit job with constraints
    n = f1 + f2
    params = f'-n{n} --ntasks-per-node=1 -C "[f1*{f1}&f2*{f2}]"'

    job_id = atf.submit_job(command, params, "hostname", fatal=True)
    atf.wait_for_job_state(job_id, "DONE")

    # Make sure NumNodes is correct
    assert (
        atf.get_job_parameter(job_id, "NumNodes") == n
    ), f"Verify number of nodes is {n}"

    # Make sure that NodeList is correct
    nodelist = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert f1 == get_nodes_with_feature(
        "f1", nodelist
    ), f"Verify nodelist contains {f1} nodes with f1"
    assert f2 == get_nodes_with_feature(
        "f2", nodelist
    ), f"Verify nodelist contains {f2} nodes with f2"


@pytest.mark.parametrize(
    "f1, f2",
    [
        (0, 1),
        (1, 0),
        ("!", "*"),
        ("*", "!"),
        ("#", "^"),
        ("^", "#"),
    ],
)
def test_bad_constraint(f1, f2):
    """Test that invalid feature requests should fail."""

    params = f'--ntasks-per-node=1 -C "[f1*{f1}&f2*{f2}]"'
    job = ' env|egrep -i "SLURM.NNODES|SLURM_JOB_NODELIST"'
    output = atf.run_command_error(f"srun {params} {job}", xfail=True)

    assert (
        "Invalid feature specification" in output
    ), f"Verify that 'Invalid feature specification' message"
