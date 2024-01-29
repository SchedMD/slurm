############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to add custom generic resources")
    atf.require_config_parameter("GresTypes", "r1")
    atf.require_nodes(1, [("Gres", "r1:1")])
    atf.require_slurm_running()


def test_job_array_with_gres():
    """Test creating job array with gres requested"""

    output_pattern = f"{atf.module_tmp_path}/%A-%a.out"
    job_id = atf.submit_job_sbatch(
        f"--array=1-2 --gres=r1:1 --wrap='echo DONE' \
                    --output={output_pattern}"
    )
    output_file_1 = f"{atf.module_tmp_path}/{job_id}-1.out"
    output_file_2 = f"{atf.module_tmp_path}/{job_id}-2.out"
    atf.wait_for_job_state(job_id, "DONE", timeout=15, fatal=True)
    with open(output_file_1, "r") as f:
        output = f.read()
        assert "DONE" in output, "Expect job to finish"
    with open(output_file_2, "r") as f:
        output = f.read()
        assert "DONE" in output, "Expect job to finish"
    assert atf.is_slurmctld_running()
