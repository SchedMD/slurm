############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re
import pytest
import time

memory = 6
steps_started = 30
total_mem = memory * steps_started * 2
max_wait_time = 10


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()
    atf.require_nodes(4, [('RealMemory', total_mem),('CPUs', 1)])


def test_batch_multiple_concurrent_steps():
    """Test of batch job with multiple concurrent job steps"""

    file_in = atf.module_tmp_path / 'file_in'
    file_out = atf.module_tmp_path / 'file_out'
    file_err = atf.module_tmp_path / 'file_err'
    job_mem_opt = f"--mem-per-cpu={total_mem}M"
    step_mem_opt = f"--mem-per-cpu={memory}M"
    
    # Build input script file
    #
    # NOTE: Explicitly set a small memory limit. Without explicitly setting the step
    #   memory limit, it will use the system default (same as the job) which may
    #   prevent the level of parallelism desired.
    #
    atf.make_bash_script(file_in, f"""for ((i = 0; i < {steps_started}; i++)); do
    srun --overlap -N1 -n1 {step_mem_opt} printenv SLURM_JOB_ID
    srun --overlap -N1 -n1 {step_mem_opt} sleep 30 &
done
wait""")

    # Spawn a batch job with multiple steps in background
    job_id = atf.submit_job(f"-O {job_mem_opt} -n{steps_started} --output={file_out} {file_in}")
    atf.wait_for_job_state(job_id, 'RUNNING', fatal=True)
    
    # Check that all the steps in background are in squeue at the same time within a time limit
    step_count = 0
    time_end =  time.time() + max_wait_time
    while step_count != steps_started and time.time() < time_end:
        output = atf.run_command_output("squeue -s --state=RUNNING", quiet=True)
        match = re.findall(rf'{job_id}.(\d+)', output)
        step_count = len(match)
        time.sleep(1)
    assert step_count == steps_started, f"All steps ({steps_started}) should be reported by squeue ({step_count} != {steps_started})"
    atf.wait_for_job_state(job_id, 'DONE', fatal=True, timeout=60)

    # Check that the output of all srun was written to the sbatch output file
    atf.wait_for_file(file_out)
    step_count = 0
    output_file = open(file_out, 'r')
    for line in output_file:
        if re.search(f'{job_id}', line) is not None:
            step_count += 1
    assert step_count == steps_started, f"All steps ({steps_started}) should be reported on the output file ({step_count} != {steps_started})"
