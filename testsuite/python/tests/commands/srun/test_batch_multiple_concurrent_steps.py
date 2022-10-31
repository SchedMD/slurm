############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re
import pytest
import time

steps_submitted = 30
memory = 6
total_mem = memory * steps_submitted * 2


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(4, [('RealMemory', total_mem),('CPUs', 1)])
    atf.require_slurm_running()


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
    atf.make_bash_script(file_in, f"""for ((i = 0; i < {steps_submitted}; i++)); do
    srun --overlap -N1 -n1 {step_mem_opt} bash -c "echo STEP_ID=$SLURM_JOB_ID.\\$SLURM_STEP_ID && sleep 30" &
done
wait""")

    # Spawn a batch job with multiple steps in background
    job_id = atf.submit_job(f"-O {job_mem_opt} -n{steps_submitted} --output={file_out} {file_in}")
    atf.wait_for_job_state(job_id, 'RUNNING', fatal=True)

    # Check that all of the steps in background are in squeue at the same time within a time limit
    steps_started = 0
    def count_steps_started():
        nonlocal steps_started
        output = atf.run_command_output(f"squeue --steps --state=RUNNING --format=%i --noheader --job {job_id}", fatal=True)
        steps_started = len(re.findall(fr"{job_id}\.\d+", output))
        return steps_started
    assert atf.repeat_until(count_steps_started, lambda n: n == steps_submitted), f"All steps ({steps_submitted}) should be reported by squeue ({steps_started} != {steps_submitted})"

    # Check that the output of all steps was written to the sbatch output file
    atf.wait_for_file(file_out, fatal=True)
    steps_written = 0
    def count_steps_written():
        nonlocal steps_written
        output = atf.run_command_output(f"cat {file_out}", fatal=True)
        steps_written = len(re.findall(fr"STEP_ID={job_id}\.\d+", output))
        return steps_written
    assert atf.repeat_until(count_steps_written, lambda n: n == steps_submitted), f"All steps ({steps_submitted}) should be written to the output file ({steps_written} != {steps_submitted})"
