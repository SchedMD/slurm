############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re

import pytest

import atf

gpu_uuid0 = "GPU-a1b2c3d4-e5f6-7890-abcd-ef1234567890"
gpu_uuid1 = "GPU-f9e8d7c6-b5a4-3210-fedc-ba9876543210"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to set gres Flags and fake_gpus.conf")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_config_parameter(
        "Name",
        {"gpu": {"File": "/dev/tty[0-1]", "Flags": "env_uuid"}},
        source="gres",
    )
    atf.require_config_file(
        "fake_gpus.conf",
        f"(null)|2|0-1|(null)|/dev/tty0|{gpu_uuid0}|nvidia_gpu_env,amd_gpu_env\n"
        f"(null)|2|0-1|(null)|/dev/tty1|{gpu_uuid1}|nvidia_gpu_env,amd_gpu_env\n",
    )
    atf.require_nodes(1, [("Gres", "gpu:2"), ("CPUs", 2)])
    atf.require_version((26, 5), "sbin/slurmd")
    atf.require_slurm_running()


def test_single_gpu_uuid():
    """Verify CUDA_VISIBLE_DEVICES is set to one of the known GPU UUIDs"""

    output = atf.run_job_output(
        "-n1 --gpus=1 printenv CUDA_VISIBLE_DEVICES",
        fatal=True,
    )
    value = output.strip()
    assert value in (
        gpu_uuid0,
        gpu_uuid1,
    ), f"CUDA_VISIBLE_DEVICES should be one of the test GPU UUIDs, got: {value!r}"


def test_two_gpus_uuid():
    """Verify CUDA_VISIBLE_DEVICES lists both GPU UUIDs"""

    output = atf.run_job_output(
        "-n1 --gpus=2 printenv CUDA_VISIBLE_DEVICES",
        fatal=True,
    )
    value = output.strip()
    assert value in (
        f"{gpu_uuid0},{gpu_uuid1}",
        f"{gpu_uuid1},{gpu_uuid0}",
    ), f"CUDA_VISIBLE_DEVICES should contain both test GPU UUIDs, got: {value!r}"


def test_rocr_visible_devices_uuid():
    """Verify ROCR_VISIBLE_DEVICES uses UUID (default env flags include RSMI)"""

    output = atf.run_job_output(
        "-n1 --gpus=1 printenv ROCR_VISIBLE_DEVICES",
        fatal=True,
    )
    value = output.strip()
    assert value in (
        gpu_uuid0,
        gpu_uuid1,
    ), f"ROCR_VISIBLE_DEVICES should be one of the test GPU UUIDs, got: {value!r}"


def test_step_uuid_isolation():
    """Verify each step gets its own GPU UUID in CUDA_VISIBLE_DEVICES"""

    n_steps = 2

    job_file = atf.module_tmp_path / "job_file"
    step_file = atf.module_tmp_path / "step_file"
    job_output_file = atf.module_tmp_path / "job_output"
    # Force all steps to overlap so each holds its GPU while the others allocate
    barrier_dir = atf.module_tmp_path / "step_barrier"

    job_output_file.unlink(missing_ok=True)
    atf.run_command(f"rm -rf {barrier_dir} && mkdir -p {barrier_dir}", fatal=True)
    atf.make_bash_script(
        step_file,
        f"""echo STEP_ID:$SLURM_STEP_ID CUDA_VISIBLE_DEVICES:$CUDA_VISIBLE_DEVICES
touch {barrier_dir}/step.$SLURM_STEP_ID
for _ in $(seq 1 100); do
    [ "$(ls {barrier_dir} | wc -l)" -ge {n_steps} ] && exit 0
    sleep 0.1
done
echo "TIMEOUT waiting for all {n_steps} steps" >&2
exit 1""",
    )
    atf.make_bash_script(
        job_file,
        f"""
        for _ in $(seq 1 {n_steps}); do
            srun --exact -n1 --gpus=1 --mem=0 {step_file} &
        done
        wait
        exit 0""",
    )

    job_id = atf.submit_job_sbatch(
        f"--cpus-per-gpu=1 --gpus={n_steps} -N1 -n{n_steps} -t1 -o {job_output_file} {job_file}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(job_output_file, fatal=True)
    output = atf.run_command_output(f"cat {job_output_file}", fatal=True)

    uuids = re.findall(
        rf"CUDA_VISIBLE_DEVICES:({re.escape(gpu_uuid0)}|{re.escape(gpu_uuid1)})",
        output,
    )
    assert (
        len(uuids) == n_steps
    ), f"Expected {n_steps} steps with GPU UUID, got {len(uuids)}: {output}"
    assert (
        len(set(uuids)) == n_steps
    ), f"Each step should get a different GPU UUID, but got duplicates: {uuids}"
