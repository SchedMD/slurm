############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

array_size = 8


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_submit_and_cancel():
    """Test basic submission and cancellation of job arrays"""

    # Submit a job array
    job_id = atf.submit_job(f"-N 1 --array=0-{array_size - 1} --begin=midnight --output=/dev/null --wrap=\"sleep 10\"", fatal=True)

    # Verify the task count
    array_task_id = atf.get_job_parameter(job_id, 'ArrayTaskId')
    assert (match := re.search(r'^(\d+)-(\d+)$', array_task_id)) is not None
    task_count = int(match.group(2)) - int(match.group(1)) + 1
    assert task_count == array_size, "Job should have task count equal to array size"

    # Verify the job array ids
    array_index = 0
    output = atf.run_command_output("squeue -r", fatal=True)
    for array_id in re.findall(fr"{job_id}_(\d+)", output):
        assert int(array_id) == array_index
        array_index += 1

    # Cancel a job with a specific job array index
    atf.run_command(f"scancel -v {job_id}_{array_id}", fatal=True)
    assert atf.run_command_exit(f"scontrol show job {job_id}_{array_id}") != 0

    # Cancel the entire job array
    atf.run_command(f"scancel -v {job_id}", fatal=True)
    assert atf.get_job_parameter(job_id, 'JobState') == 'CANCELLED'
