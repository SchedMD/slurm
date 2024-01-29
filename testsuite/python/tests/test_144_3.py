############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create sh files")
    atf.require_config_parameter("GresTypes", "gpu")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_nodes(2, [("Gres", "gpu:2"), ("CPUs", 4)])

    gpu_file_pattern = make_gpu_files(4)
    atf.require_config_parameter(
        "Name", {"gpu": {"File": gpu_file_pattern}}, source="gres"
    )

    atf.make_bash_script(
        "step.sh",
        """
            echo 'NODE_ID:'$SLURM_NODEID 'STEP_ID:'$SLURM_STEP_ID
            sleep 1
            exit 0""",
    )
    step_path = f"{atf.module_tmp_path}/step.sh"
    atf.make_bash_script(
        "job.sh",
        f"""
            echo 'step 1'
            srun --exact -n2 --gpus=2 --gpus-per-node=0 --mem=0 {step_path} &
            echo 'step 2'
            srun --exact -n2 --gpus=2 --gpus-per-node=0 --mem=0 {step_path} &
            wait
            exit 0""",
    )
    atf.require_slurm_running()


def make_gpu_files(count):
    """Make files in the tmp path for gpu's to point to
    Returns pattern TMP/gpu[1-COUNT]"""

    for i in range(1, count + 1):
        atf.run_command(f"touch {atf.module_tmp_path}/gpu{i}")
    return f"{atf.module_tmp_path}/gpu[1-{count}]"


def validate_job(job_id, out_file):
    """Both jobs have the same validation process"""

    assert job_id != 0, "Expect job to submit properly"
    atf.wait_for_job_state(job_id, "DONE")
    with open(out_file, "r") as f:
        out = f.read()
        assert out.startswith("step 1"), "Expect step 1 to start without issues"
        assert "step 2" in out, "Expect step 2 to start"
        assert "NODE_ID:0 STEP_ID:0" in out, "Expect the first node to be in first step"
        assert (
            "NODE_ID:1 STEP_ID:0" in out
        ), "Expect the second node to be in first step"
        assert (
            "NODE_ID:0 STEP_ID:1" in out
        ), "Expect the first node to be in second step"
        assert (
            "NODE_ID:1 STEP_ID:1" in out
        ), "Expect the second node to be in second step"


def test_exact_gpu_full_resources():
    """Test --exact with all of node resources"""

    out_file = f"{atf.module_tmp_path}/out"
    job_path = f"{atf.module_tmp_path}/job.sh"
    job_id = atf.submit_job_sbatch(
        f"--cpus-per-gpu=2 --gpus-per-node=2 -N2 \
        -n4 -t1 --output={out_file} {job_path}"
    )
    validate_job(job_id, out_file)


def test_exact_gpu_parial_resources():
    """Test --exact with partial node resources"""

    job_path = f"{atf.module_tmp_path}/job.sh"
    out_file = f"{atf.module_tmp_path}/out"
    job_id = atf.submit_job_sbatch(
        f"--cpus-per-gpu=1 --gpus-per-node=2 -N2 -n4 -t1 \
          --output={out_file} {job_path}"
    )
    validate_job(job_id, out_file)
