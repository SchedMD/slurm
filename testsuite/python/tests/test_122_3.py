############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import time

# Test parameters
array_size = 4
file_script = "script"


@pytest.fixture(scope="module", autouse=True)
def setup():
    max_array_size = atf.get_config_parameter("MaxArraySize", live=False)
    if max_array_size is not None and int(max_array_size) < array_size + 1:
        pytest.skip("MaxArraySize is too small for this test")

    atf.require_nodes(array_size)
    atf.require_config_parameter_includes("PreemptMode", "GANG")
    atf.require_slurm_running()


def test_job_array_suspend_resume():
    atf.make_bash_script(
        file_script,
        f"srun python3 {atf.properties['testsuite_scripts_dir']}/wait_for_suspend.py 10",
    )

    # Submit job array and wait for the first job to be RUNNING
    job_id = atf.submit_job_sbatch(
        f"-N1 --array=0-{array_size-1} --output=%A_%a.out -t1 {file_script}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # Wait for each job in the job array to have an out file with at least two
    # lines of output
    for i in range(array_size):
        assert atf.wait_for_file(
            f"{job_id}_{i}.out"
        ), f"Step {i} of the job array never created an output file"
        assert atf.repeat_command_until(
            f"wc -l {job_id}_{i}.out",
            lambda line_cnt_str: int(line_cnt_str["stdout"].split()[0]) >= 2,
        ), f"Step {i} of the job array never started running and recording output every second"

    # Suspend the job and wait for all jobs in array to be SUSPENDED
    atf.run_command(f"scontrol suspend {job_id}", user="slurm", fatal=True)
    for i in range(array_size):
        raw_job_id = atf.get_job_id_from_array_task(job_id, i)
        assert atf.wait_for_job_state(
            raw_job_id, "SUSPENDED"
        ), f"Step {i} never entered the SUSPENDED state after the job array was suspended"

    # Wait, giving the program time to recognize it was suspended
    time.sleep(4)

    # Check all jobs start running and record in their out file that they were
    # suspended
    atf.run_command(f"scontrol resume {job_id}", user="slurm", fatal=True)
    for i in range(array_size):
        raw_job_id = atf.get_job_id_from_array_task(job_id, i)
        assert atf.wait_for_job_state(
            raw_job_id, "RUNNING"
        ), f"Step {i} never entered the RUNNING state after the job array was resumed"

        assert atf.repeat_command_until(
            f"cat {job_id}_{i}.out",
            lambda job_output: "JobSuspended" in job_output["stdout"],
        ), f"Verify that task {i} of the array was suspended"
