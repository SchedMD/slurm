############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(1, [("CPUs", 1), ("Features", "f1")])
    atf.require_nodes(1, [("CPUs", 1), ("Features", "f2")])
    atf.require_nodes(1, [("CPUs", 1), ("Features", "f1,f2")])
    atf.require_slurm_running()


@pytest.mark.parametrize(
    "constraint,xfail",
    [
        ("invalid", True),
        ("f1,invalid", True),
        ("f2,invalid", True),
        ("invalid,f1", True),
        ("invalid,f2", True),
        ("f1", False),
        ("f2", False),
        ("f1,f2", False),
        ("f2,f1", False),
    ],
)
@pytest.mark.parametrize("command", ["srun", "salloc", "sbatch"])
def test_constraint(constraint, xfail, command):
    """Verify --constraint option"""

    job = 'env|egrep -i "SLURM_NNODES|SLURM_JOB_NODELIST"'
    if command == "sbatch":
        result = atf.run_command(
            f"{command} --constraint={constraint} -o job.out --wrap '{job}'",
            xfail=xfail,
        )
    else:
        result = atf.run_command(
            f"{command} --constraint={constraint} {job}", xfail=xfail
        )

    if xfail:
        assert result["exit_code"] != 0, "Verify tha job was NOT submitted"
    else:
        assert result["exit_code"] == 0, "Verify tha job was submitted"

    stdout = result["stdout"]
    stderr = result["stderr"]
    if command == "sbatch" and not xfail:
        job_id = 0
        if match := re.search(r"Submitted \S+ job (\d+)", stdout):
            job_id = int(match.group(1))
            atf.properties["submitted-jobs"].append(job_id)
            atf.wait_for_job_state(job_id, "DONE")
            atf.wait_for_file("job.out")
            stdout = atf.run_command_output(f"cat job.out", fatal=True)

    node = ""
    if match := re.search(r"SLURM_JOB_NODELIST=(.*)", stdout):
        node = match.group(1)

    if not xfail:
        node_features = atf.get_node_parameter(node, "AvailableFeatures")
        constraints = constraint.split(",")
        for c in constraints:
            assert c in node_features, "Verify {node} has feature {c}"
    else:
        assert (
            "Invalid feature specification" in stderr
        ), f"Verify that 'Invalid feature specification' is in stderr"
