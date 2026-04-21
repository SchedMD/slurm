############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

# import re

num_nodes = 3


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(num_nodes * 2)
    atf.require_slurm_running()


def test_preserve_env():
    """Test that a job correctly uses the -E or --preserve-env flag."""

    file_in = atf.module_tmp_path / "file_in"
    file_out = atf.module_tmp_path / "file_out"
    num_tasks = num_nodes * 2
    srun_nodes = 1
    srun_tasks = 1
    atf.make_bash_script(
        file_in,
        f"""printenv SLURM_NNODES
srun -E -n{srun_tasks} -N{srun_nodes} printenv SLURM_NNODES
printenv SLURM_NTASKS
srun --preserve-env -n{srun_tasks} -N{srun_nodes} printenv SLURM_NTASKS
srun -n{srun_tasks} -N{srun_nodes} printenv SLURM_NNODES
srun -n{srun_tasks} -N{srun_nodes} printenv SLURM_NTASKS""",
    )

    atf.run_command("srun printenv SLURM_NNODES")
    job_id = atf.submit_job_sbatch(
        f"-O --output={file_out} -N{num_nodes} -n{num_tasks} {file_in}"
    )
    atf.wait_for_job_state(job_id, "DONE")

    # Wait until file is complete
    for t in atf.timer():
        output = atf.run_command_output(f"cat {file_out}")
        if len(output.splitlines()) >= 6:
            break
    else:
        assert (
            False
        ), f"Output should contain 6 lines, but got {len(output.splitlines())}"

    lines = output.splitlines()
    error_msg = "Incorrect output from:"
    assert num_nodes == int(lines[0]), f"{error_msg} printenv SLURM_NNODES"
    assert num_nodes == int(
        lines[1]
    ), f"{error_msg} srun -E -n{srun_tasks} -N{srun_nodes} printenv SLURM_NNODES"
    assert num_tasks == int(lines[2]), f"{error_msg} printenv SLURM_NTASKS"
    assert num_tasks == int(
        lines[3]
    ), f"{error_msg} srun --preserve-env -n{srun_tasks} -N{srun_nodes} printenv SLURM_NTASKS"
    assert srun_nodes == int(
        lines[4]
    ), f"{error_msg} srun -n{srun_tasks} -N{srun_nodes} printenv SLURM_NNODES"
    assert srun_tasks == int(
        lines[5]
    ), f"{error_msg} srun -n{srun_tasks} -N{srun_nodes} printenv SLURM_NTASKS"
