############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import os

# Global test variables
file_in = "input"
file_out = "output"
file_err = "error"
tasks_over_nodes = 2


@pytest.fixture(scope="module", autouse=True)
def setup():
    global file_in, file_out, file_err, test_prog

    # Tell ATF we need to modify configuration
    atf.require_auto_config("needs MPI/multi-node configuration")

    # Configure MPI support
    atf.require_config_parameter("MpiDefault", "pmix")

    # Ensure sufficient nodes for test
    # Each node needs at least 2 CPUs to handle the task distribution in multi-slurmd mode
    atf.require_nodes(5, [("CPUs", 2)])

    atf.require_slurm_running()


def test_mpi_basic_functionality(mpi_program):
    """Test basic MPI functionality via srun with different distributions."""

    # Create the job script
    atf.make_bash_script(
        file_in,
        f"""
date

env | grep SLURM_JOB_NUM_NODES
TASKS=$((SLURM_JOB_NUM_NODES+{tasks_over_nodes}))
echo MPI_TASKS=$TASKS

# Disable OpenMPI shared memory BTL to avoid segfaults in multi-slurmd setup
# where all "nodes" are actually on the same physical machine
echo test1_cyclic
srun -n $TASKS --distribution=cyclic -t1 --export=ALL,OMPI_MCA_btl=^sm {mpi_program}

date
echo test2_block
srun -n $TASKS --distribution=block -t1 --export=ALL,OMPI_MCA_btl=^sm {mpi_program}

date
echo test3_one_node
srun -n $TASKS -N1 -O -t1 {mpi_program}

date
echo TEST_COMPLETE
""",
    )

    # Submit job
    job_id = atf.submit_job_sbatch(
        f"-N1-6 -n8 --output={file_out} --error={file_err} -t2 {file_in}",
        fatal=True,
    )

    # Wait for job completion
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    # Analyze the output
    atf.wait_for_file(file_out, fatal=True)
    output = atf.run_command_output(f"cat {file_out}", fatal=True)

    # Parse job information
    node_cnt = None
    task_cnt = None
    matches = 0
    rank_sum = 0
    complete = False

    for line in output.split("\n"):
        # Extract node count
        node_match = re.search(r"SLURM_JOB_NUM_NODES=(\d+)", line)
        if node_match:
            node_cnt = int(node_match.group(1))

        # Extract task count
        task_match = re.search(r"MPI_TASKS=(\d+)", line)
        if task_match:
            task_cnt = int(task_match.group(1))

        # Count MPI communication messages and sum ranks
        msg_match = re.search(
            r"Rank.(\d+). on \S+ just received msg from Rank (\d+)", line
        )
        if msg_match:
            rank1 = int(msg_match.group(1))
            rank2 = int(msg_match.group(2))
            rank_sum += rank1 + rank2
            matches += 1

        # Check for completion
        if "TEST_COMPLETE" in line:
            complete = True

    assert task_cnt is not None, "Could not determine task count from job output"
    assert node_cnt is not None, "Could not determine node count from job output"

    # Expected task_cnt * 3 messages from all three tests
    expected_msg = task_cnt * 3
    expected_sum = 0
    for i in range(1, task_cnt):
        expected_sum += i
    expected_sum *= 6  # Each rank appears 6 times across all three tests

    # Check for various failure conditions with detailed error reporting
    failure_message = ""
    if matches == 0:
        # Check for specific error patterns in stderr
        if os.path.exists(file_err):
            error_output = atf.run_command_output(f"head {file_err}", fatal=False)
            if "Error creating CQ" in error_output:
                failure_message = "MVAPICH configuration issue detected. Configure with 'PropagateResourceLimitsExcept=MEMLOCK' and start slurmd with 'ulimit -l unlimited'"
            else:
                failure_message = "No MPI communications occurred. The version of MPI you are using may be incompatible with the configured switch. Core files may be present from failed MPI tasks"
        else:
            failure_message = "No MPI communications occurred. The version of MPI you are using may be incompatible with the configured switch. Core files may be present from failed MPI tasks"
    elif matches != expected_msg:
        failure_message = f"Unexpected output ({matches} of {expected_msg})"
    elif not complete:
        failure_message = "Test failed to complete"
    elif rank_sum != expected_sum:
        failure_message = f"Invalid rank values ({rank_sum} != {expected_sum})"

    if failure_message:
        pytest.fail(failure_message)
