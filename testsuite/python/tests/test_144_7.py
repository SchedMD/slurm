############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import logging
from pathlib import Path
import pytest
import re

job_file = None
step_file = None
job_output_file = None
constrain_devices = False


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    global job_file, step_file, job_output_file, constrain_devices

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")

    # Require 8 tty because one test requests 8 "GPU"s (4 GPUS each for 2 nodes)
    for tty_num in range(8):
        atf.require_tty(tty_num)
    atf.require_config_parameter(
        "Name", {"gpu": {"File": "/dev/tty[0-7]"}}, source="gres"
    )
    atf.require_nodes(2, [("Gres", f"gpu:4"), ("CPUs", 8)])

    atf.require_slurm_running()

    job_file = Path(atf.module_tmp_path) / "job_file"
    step_file = Path(atf.module_tmp_path) / "step_file"
    job_output_file = Path(atf.module_tmp_path) / "job_output_file"
    constrain_devices = atf.get_config_parameter("ConstrainDevices") == "yes"


def test_gpus_per_node_parallel_1_delayed():
    """Test --gpus-per-node option by job step"""
    # Delete previous job output file and prepare job scripts
    job_output_file.unlink(missing_ok=True)
    atf.make_bash_script(
        job_file,
        f"""
        scontrol -dd show job ${{SLURM_JOBID}}
        srun -vv --exact -n1 --gpus-per-node=1 --exact -n1 --mem=0 {step_file} &
        srun -vv --exact -n1 --gpus-per-node=1 --exact -n1 --mem=0 {step_file} &
        srun -vv --exact -n1 --gpus-per-node=1 --exact -n1 --mem=0 {step_file} &
        wait
        exit 0""",
    )

    atf.make_bash_script(
        step_file,
        f"""
        echo 'STEP_ID:'$SLURM_STEP_ID 'CUDA_VISIBLE_DEVICES:'$CUDA_VISIBLE_DEVICES
        sleep 3
        if [ $SLURM_STEP_ID -eq 2 ]; then
            squeue -s --name=test_job
        fi
        exit 0""",
    )

    job_id = atf.submit_job_sbatch(
        "--cpus-per-gpu=1 --gpus-per-node=2 -N1 -n3 -t1 "
        + f"-o {job_output_file} -J test_job {job_file}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(job_output_file, fatal=True)

    output = atf.run_command_output(f"cat {job_output_file}", fatal=True)

    # Verify all steps used only 1 GPU
    assert not re.search(
        r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:\d+,", output
    ), "Not all steps used only 1 GPU"
    # Verify a GPU was used 3 times
    assert (
        len(re.findall(r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:\d+", output)) == 3
    ), "A GPU was not used 3 times"
    # Verify one step was delayed
    assert atf.check_steps_delayed(
        job_id, output, 1
    ), "One step should have been delayed"

    if constrain_devices:
        # Verify all GPUs are CUDA_VISIBLE_DEVICES:0 (with ConstrainDevices)
        assert (
            len(re.findall(r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:0", output)) == 3
        ), "Not all GPUs are CUDA_VISIBLE_DEVICES:0 (with ConstrainDevices)"
    else:
        # Verify steps split between the two GPUs (without ConstrainDevices)
        cuda_devices_used = re.findall(
            r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:(\d+)", output
        )
        assert (
            len(set(cuda_devices_used)) > 1
        ), f"The job steps weren't split among the two GPUS (CUDA devices {set(cuda_devices_used)} used instead of 0 and 1 (without ConstrainDevices))"


@pytest.mark.parametrize("step_args", ["-n1 --gpus-per-task=1", "-n1 --gpus=1"])
def test_gpus_per_node_parallel(step_args):
    """Test parallel step args with a job with --gpus-per-node"""
    # Delete previous job output file and prepare job scripts
    job_output_file.unlink(missing_ok=True)
    atf.make_bash_script(
        job_file,
        f"""
        scontrol -dd show job ${{SLURM_JOBID}}
        srun --exact --gpus-per-node=0 --mem=0 {step_args} {step_file} &
        srun --exact --gpus-per-node=0 --mem=0 {step_args} {step_file} &
        wait
        exit 0""",
    )

    atf.make_bash_script(
        step_file,
        f"""
        echo 'STEP_ID:'$SLURM_STEP_ID 'CUDA_VISIBLE_DEVICES:'$CUDA_VISIBLE_DEVICES
        sleep 3
        if [ $SLURM_STEP_ID -eq 1 ]; then
            scontrol show step $SLURM_JOB_ID.$SLURM_STEP_ID
        fi
        exit 0""",
    )

    job_id = atf.submit_job_sbatch(
        "--cpus-per-gpu=2 --gpus-per-node=2 -N1 -n2 -t1 "
        + f"-o {job_output_file} -J test_job {job_file}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(job_output_file, fatal=True)

    output = atf.run_command_output(f"cat {job_output_file}", fatal=True)

    # Verify all steps used only 1 GPU
    assert not re.search(
        r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:\d+,", output
    ), "Not all steps used only 1 GPU"
    # Verify a GPU was used 2 times
    assert (
        len(re.findall(r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:\d+", output)) == 2
    ), "A GPU was not used 2 times"
    # Verify all steps run in parallel
    assert not re.search(
        r"Step completed in JobId=\d+, retrying", output
    ), "Not all steps ran in parallel"

    if constrain_devices:
        # Verify all GPUs are CUDA_VISIBLE_DEVICES:0 (with ConstrainDevices)
        assert (
            len(re.findall(r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:0", output)) == 2
        ), "Not all GPUs are CUDA_VISIBLE_DEVICES:0 (with ConstrainDevices)"
    else:
        # Verify 1 GPU is CUDA_VISIBLE_DEVICES:0 (without ConstrainDevices)
        assert (
            len(re.findall(r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:0", output)) == 1
        ), "Not 1 GPU is CUDA_VISIBLE_DEVICES:0 (without ConstrainDevices)"
        # Verify 1 GPU is CUDA_VISIBLE_DEVICES:1 (without ConstrainDevices)
        assert (
            len(re.findall(r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:1", output)) == 1
        ), "Not 1 GPU is CUDA_VISIBLE_DEVICES:1 (without ConstrainDevices)"


def test_gpus_per_node_different_gpus():
    """Test --gpus (per job or step) option by job step"""
    # Delete previous job output file and prepare job scripts
    job_output_file.unlink(missing_ok=True)
    atf.make_bash_script(
        job_file,
        f"""
        scontrol -dd show job ${{SLURM_JOBID}}
        srun --exact -n2 --gpus=2 --gpus-per-node=0 --mem=0 {step_file} &
        srun --exact -n1 --gpus=1 --gpus-per-node=0 --mem=0 {step_file} &
        wait
        exit 0""",
    )

    atf.make_bash_script(
        step_file,
        f"""
        echo 'STEP_ID:'$SLURM_STEP_ID 'CUDA_VISIBLE_DEVICES:'$CUDA_VISIBLE_DEVICES
        sleep 3
        exit 0""",
    )

    job_id = atf.submit_job_sbatch(
        "--cpus-per-gpu=1 --gpus-per-node=3 -N1 -n3 -t2 "
        + f"-o {job_output_file} -J test_job {job_file}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(job_output_file, fatal=True)

    output = atf.run_command_output(f"cat {job_output_file}")
    step_2gpu = re.search(
        r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:(\d+),(\d+)", output
    ).groups()
    step_1gpu = re.search(
        r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:(\d+$)", output, re.MULTILINE
    ).groups()

    # Verify 1 step used 1 GPU and 2 steps used 2 GPUs
    assert (
        len(step_1gpu) == 1 and len(step_2gpu) == 2
    ), f"Fail to obtain all GPUs index ({len(step_1gpu)} != 1 or {len(step_2gpu)} != 2)"

    if constrain_devices:
        # Verify if devices are constrained, CUDA_VISIBLE_DEVICES start always
        # with 0 in a step
        assert (
            step_2gpu[0] == "0" and step_1gpu[0] == "0"
        ), "CUDA_VISIBLE_DEVICES did not always start with 0 in a step"
    else:
        # Verify if devices are NOT constrained, all CUDA_VISIBLE_DEVICES are
        # unique
        assert step_1gpu[0] not in step_2gpu, "All CUDA_VISIBLE_DEVICES are not unique"


def test_gpus_per_node_with_gpus_per_task():
    """Test --gpus-per-task option by job step"""
    job_gpus = 3
    step_gpus = 2

    # Delete previous job output file and prepare job scripts
    job_output_file.unlink(missing_ok=True)
    atf.make_bash_script(
        job_file,
        f"""
        scontrol -dd show job ${{SLURM_JOBID}}
        srun -vv --exact -n1 --gpus-per-task={step_gpus} --gpus-per-node=0 --mem=0 {step_file} &
        srun -vv --exact -n1 --gpus-per-task={step_gpus} --gpus-per-node=0 --mem=0 {step_file} &
        srun -vv --exact -n1 --gpus-per-task={step_gpus} --gpus-per-node=0 --mem=0 {step_file} &
        wait
        exit 0""",
    )

    atf.make_bash_script(
        step_file,
        f"""
        echo 'STEP_ID:'$SLURM_STEP_ID 'CUDA_VISIBLE_DEVICES:'$CUDA_VISIBLE_DEVICES
        sleep 3
        if [ $SLURM_STEP_ID -eq 2 ]; then
            scontrol show step $SLURM_JOB_ID.$SLURM_STEP_ID
        fi
        exit 0""",
    )

    job_id = atf.submit_job_sbatch(
        f"--cpus-per-gpu=1 --gpus-per-node={job_gpus} -N1 -n3 -t1 "
        + f"-o {job_output_file} -J test_job {job_file}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(job_output_file, fatal=True)

    output = atf.run_command_output(f"cat {job_output_file}", fatal=True)

    # Verify no step has more than 2 GPUs
    assert not re.search(
        r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:\d+,\d+,", output
    ), "A step has more than 2 GPUs"
    # Verify all steps have 2 GPUs
    assert (
        len(re.findall(r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:\d+,\d+", output)) == 3
    ), "Not all steps have 2 GPUs"
    # Verify two steps were delayed, one of them twice
    assert atf.check_steps_delayed(
        job_id, output, 2
    ), "Two steps were not delayed or one of them was not delayed twice"


def test_gpus_per_node_with_gpus():
    """Test --gpus option by job step"""
    job_gpus = 2
    step_gpus = 2

    # Delete previous job output file and prepare job scripts
    job_output_file.unlink(missing_ok=True)
    atf.make_bash_script(
        job_file,
        f"""
        scontrol -dd show job ${{SLURM_JOBID}}
        srun -vv --exact -n2 --gpus={step_gpus} --gpus-per-node=0 --mem=0 {step_file} &
        srun -vv --exact -n2 --gpus={step_gpus} --gpus-per-node=0 --mem=0 {step_file} &
        srun -vv --exact -n2 --gpus={step_gpus} --gpus-per-node=0 --mem=0 {step_file} &
        wait
        exit 0""",
    )

    atf.make_bash_script(
        step_file,
        f"""
        echo 'HOST:'$SLURMD_NODENAME 'NODE_ID:'$SLURM_NODEID 'STEP_ID:'$SLURM_STEP_ID 'CUDA_VISIBLE_DEVICES:'$CUDA_VISIBLE_DEVICES
        sleep 3
        if [ $SLURM_STEP_ID -eq 2 -a $SLURM_NODEID -eq 0 ]; then
            scontrol show step $SLURM_JOB_ID.$SLURM_STEP_ID
        fi
        exit 0""",
    )

    job_id = atf.submit_job_sbatch(
        f"--cpus-per-gpu=2 --gpus-per-node={job_gpus} -N2 -n6 -t1 "
        + f"-o {job_output_file} -J test_job {job_file}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(job_output_file, fatal=True)

    output = atf.run_command_output(f"cat {job_output_file}", fatal=True)

    # Verify no more that 1 GPU is visible (per node)
    assert not re.search(
        r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:\d+,", output
    ), "1 GPU is not visible (per node)"
    # Verify step 0 had access to 2 GPUs
    assert (
        len(re.findall(r"STEP_ID:0 CUDA_VISIBLE_DEVICES:\d+", output)) == 2
    ), "Step 0 did not have access to 2 GPUs"
    # Verify step 1 had access to 2 GPUs
    assert (
        len(re.findall(r"STEP_ID:1 CUDA_VISIBLE_DEVICES:\d+", output)) == 2
    ), "Step 1 did not have access to 2 GPUs"
    # Verify step 2 had access to 2 GPUs
    assert (
        len(re.findall(r"STEP_ID:2 CUDA_VISIBLE_DEVICES:\d+", output)) == 2
    ), "Step 2 did not have access to 2 GPUs"
    # Verify one step was delayed
    assert atf.check_steps_delayed(job_id, output, 1), "One step was not delayed"

    if constrain_devices:
        # Verify all GPUs are CUDA_VISIBLE_DEVICES:0 due to ConstrainDevices
        assert (
            len(re.findall(r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:0", output)) == 6
        ), "Not all GPUs are CUDA_VISIBLE_DEVICES:0 due to ConstrainDevices"
    else:
        cuda_val = []
        cuda_val.append(
            re.search(r"STEP_ID:0 CUDA_VISIBLE_DEVICES:(\d+)", output).group(1)
        )
        cuda_val.append(
            re.search(r"STEP_ID:1 CUDA_VISIBLE_DEVICES:(\d+)", output).group(1)
        )
        cuda_val.append(
            re.search(r"STEP_ID:2 CUDA_VISIBLE_DEVICES:(\d+)", output).group(1)
        )
        # Verify two first steps use different GPUs (without ConstrainDevices)
        assert (
            cuda_val[0] != cuda_val[1]
        ), "The two first steps did not use different GPUs (without ConstrainDevices)"
        # Verify last step used a previous GPU (without ConstrainDevices)
        assert (
            cuda_val[2] == cuda_val[0] or cuda_val[2] == cuda_val[1]
        ), "The last step did not use one of the previous GPUs (without ConstrainDevices)"


def test_gpus_per_node_with_gpus_2_nodes():
    """Test --gpus option across 2 nodes"""
    job_gpus = 4

    # Delete previous job output file and prepare job scripts
    job_output_file.unlink(missing_ok=True)
    atf.make_bash_script(
        job_file,
        f"""
        scontrol -dd show job ${{SLURM_JOBID}}
        srun -vv --exact -n2 --gpus=6 --gpus-per-node=0 --mem=0 {step_file} &
        srun -vv --exact -n2 --gpus=7 --gpus-per-node=0 --mem=0 {step_file} &
        srun -vv --exact -n2 --gpus=8 --gpus-per-node=0 --mem=0 {step_file} &
        wait
        exit 0""",
    )

    atf.make_bash_script(
        step_file,
        f"""
        echo 'HOST:'$SLURMD_NODENAME 'NODE_ID:'$SLURM_NODEID 'STEP_ID:'$SLURM_STEP_ID 'CUDA_VISIBLE_DEVICES:'$CUDA_VISIBLE_DEVICES
        sleep 3
        if [ $SLURM_STEP_ID -eq 2 -a $SLURM_NODEID -eq 0 ]; then
            scontrol show step $SLURM_JOB_ID.$SLURM_STEP_ID
        fi
        exit 0""",
    )

    job_id = atf.submit_job_sbatch(
        f"--cpus-per-gpu=2 --gpus-per-node={job_gpus} -N2 -n6 -t1 "
        + f"-o {job_output_file} -J test_job {job_file}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(job_output_file, fatal=True)

    output = atf.run_command_output(f"cat {job_output_file}", fatal=True)

    # Verify all steps have less than 5 GPUs per node
    assert not re.search(
        r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:\d+,\d+,\d+,\d+,", output
    ), "Not all steps have less than 5 GPUs per node"
    # Verify step 0 used 2 nodes
    assert (
        len(re.findall(r"STEP_ID:0 CUDA_VISIBLE_DEVICES:\d+", output)) == 2
    ), "Step 0 did not use 2 nodes"
    # Verify step 1 used 2 nodes
    assert (
        len(re.findall(r"STEP_ID:1 CUDA_VISIBLE_DEVICES:\d+", output)) == 2
    ), "Step 1 did not use 2 nodes"
    # Verify step 2 used 2 nodes
    assert (
        len(re.findall(r"STEP_ID:2 CUDA_VISIBLE_DEVICES:\d+", output)) == 2
    ), "Step 2 did not use 2 nodes"
    # Verify two steps were delayed, one of them twice
    assert atf.check_steps_delayed(
        job_id, output, 2
    ), "Two steps were not delayed or one of them was not delayed twice"


def test_gpus_per_node_with_gpus_per_task_3():
    """Test --gpus-per-task option by job step"""
    job_gpus = 4
    step_gpus = 2

    # Delete previous job output file and prepare job scripts
    job_output_file.unlink(missing_ok=True)
    atf.make_bash_script(
        job_file,
        f"""
        scontrol -dd show job ${{SLURM_JOBID}}
        srun -vv {step_file}
        srun -vv --exact -n3 --gpus-per-task={step_gpus} --gpus-per-node=0 --mem=0 {step_file} &
        srun -vv --exact -n3 --gpus-per-task={step_gpus} --gpus-per-node=0 --mem=0 {step_file} &
        wait
        exit 0""",
    )

    atf.make_bash_script(
        step_file,
        f"""
        echo 'STEP_ID:'$SLURM_STEP_ID 'CUDA_VISIBLE_DEVICES:'$CUDA_VISIBLE_DEVICES
        sleep 3
        if [ $SLURM_STEP_ID -eq 1 -a $SLURM_PROCID -eq 0 ]; then
            scontrol show step $SLURM_JOB_ID.$SLURM_STEP_ID
        fi
        exit 0""",
    )

    job_id = atf.submit_job_sbatch(
        f"--cpus-per-gpu=1 --gpus-per-node={job_gpus} -N2 -n4 -t1 "
        + f"-o {job_output_file} -J test_job {job_file}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(job_output_file, fatal=True)

    output = atf.run_command_output(f"cat {job_output_file}", fatal=True)

    # Verify no more than 4 GPUs are visible in any step
    assert not re.search(
        r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:\d+,\d+,\d+,\d+,", output
    ), "More than 4 GPUs are visible in a step"
    # Verify job has access to 4 GPUs
    assert (
        len(re.findall(r"STEP_ID:0 CUDA_VISIBLE_DEVICES:\d+,\d+,\d+,\d+", output)) == 4
    ), "Job does not have access to 4 GPUs"
    # Verify step 1 has 3 tasks and 2 GPUs per task
    assert (
        len(re.findall(r"STEP_ID:1 CUDA_VISIBLE_DEVICES:\d+,\d+", output)) == 3
    ), "Step 1 does not have 3 tasks and 2 GPUs per task"
    # Verify step 2 has 3 tasks and 2 GPUs per task
    assert (
        len(re.findall(r"STEP_ID:2 CUDA_VISIBLE_DEVICES:\d+,\d+", output)) == 3
    ), "Step 2 does not have 3 tasks and 2 GPUs per task"
    # Verify one step was delayed
    assert atf.check_steps_delayed(job_id, output, 1), "One step was not delayed"


def test_gpus_per_node_with_gpus_per_task_5():
    " Test --gpus-per-task option by job step " ""
    job_gpus = 4
    step_gpus = 2

    # Delete previous job output file and prepare job scripts
    job_output_file.unlink(missing_ok=True)
    atf.make_bash_script(
        job_file,
        f"""
        scontrol -dd show job ${{SLURM_JOBID}}
        srun -vv --exact -N1 -n1 --gpus-per-task={step_gpus} --gpus-per-node=0 --mem=0 {step_file} &
        srun -vv --exact -N1 -n1 --gpus-per-task={step_gpus} --gpus-per-node=0 --mem=0 {step_file} &
        srun -vv --exact -N1 -n1 --gpus-per-task={step_gpus} --gpus-per-node=0 --mem=0 {step_file} &
        srun -vv --exact -N1 -n1 --gpus-per-task={step_gpus} --gpus-per-node=0 --mem=0 {step_file} &
        srun -vv --exact -N1 -n1 --gpus-per-task={step_gpus} --gpus-per-node=0 --mem=0 {step_file} &
        wait
        exit 0""",
    )

    atf.make_bash_script(
        step_file,
        f"""
        echo 'STEP_ID:'$SLURM_STEP_ID 'CUDA_VISIBLE_DEVICES:'$CUDA_VISIBLE_DEVICES
        sleep 3
        if [ $SLURM_STEP_ID -eq 1 -a $SLURM_PROCID -eq 0 ]; then
            scontrol show step $SLURM_JOB_ID.$SLURM_STEP_ID
        fi
        exit 0""",
    )

    job_id = atf.submit_job_sbatch(
        f"--cpus-per-gpu=1 --gpus-per-node={job_gpus} -N2 -n5 -t1 "
        + f"-o {job_output_file} -J test_job {job_file}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(job_output_file, fatal=True)

    output = atf.run_command_output(f"cat {job_output_file}", fatal=True)

    # Verify no more that 2 GPUs are visible in any step
    assert not re.search(
        r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:\d+,\d+,\d+,", output
    ), "More that 2 GPUs are visible in a step"
    # Verify all 5 steps have access to 2 GPUs
    assert (
        len(re.findall(r"STEP_ID:\d+ CUDA_VISIBLE_DEVICES:\d+,\d+", output)) == 5
    ), "Not all 5 steps have access to 2 GPUs"
    # Verify one step was delayed
    assert atf.check_steps_delayed(job_id, output, 1), "One step was not delayed"
