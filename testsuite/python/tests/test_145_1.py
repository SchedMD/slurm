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


def get_nnodes(output):
    match = re.search(r"SLURM_NNODES=(\d)", output)
    if match == None:
        return 0
    return int(match.group(1))


def get_nodelist(output):
    match = re.search(r"SLURM_JOB_NODELIST=.*\[(.*)\]", output)
    if match == None:
        return ""
    return match.group(1)


def expected_nodelist(f1, f2):
    nodelist = [
        [
            "1,4",  # 1, 1
            "1,4-5",  # 1, 2
            "1,4-6",  # 1, 3
        ],
        [
            "1-2,4",  # 2, 1
            "1-2,4-5",  # 2, 2
            "1-2,4-6",  # 2, 3
        ],
        [
            "1-4",  # 3, 1
            "1-5",  # 3, 2
            "1-6",  # 3, 3
        ],
    ]
    return nodelist[f1 - 1][f2 - 1]


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

    # Run jobs with egrep
    n = f1 + f2
    params = f'-n{n} --ntasks-per-node=1 -C "[f1*{f1}&f2*{f2}]"'
    job = 'env|egrep -i "SLURM_NNODES|SLURM_JOB_NODELIST"'

    if command == "sbatch":
        job_id = atf.submit_job_sbatch(f"{params} -o out.txt --wrap '{job}'")

        atf.wait_for_job_state(job_id, "DONE")
        atf.wait_for_file("out.txt")

        output = atf.run_command_output("cat out.txt")
    else:
        output = atf.run_command_output(f"{command} {params} {job}", fatal=True)

    # Make sure SLURM_NNODES is correct
    assert get_nnodes(output) == n, f"Verify number of nodes is {n}"

    # Make sure that NODELIST is correct
    nodelist = get_nodelist(output)
    assert nodelist == expected_nodelist(
        f1, f2
    ), f"Verify nodelist is {expected_nodelist(f1, f2)}"


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
