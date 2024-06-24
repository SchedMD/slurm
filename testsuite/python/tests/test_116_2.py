############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("AccountingStorageType", None)
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "associations")
    atf.require_slurm_running()


def test_account():
    """Test of job account (--account option)."""

    my_acct = "MY_ACCT"
    qa_acct = "QA_ACCT"
    job_id = atf.submit_job_sbatch(f'--account={my_acct} --wrap="sleep 5"', env_vars=f"SLURM_ACCOUNT={qa_acct}")
    assert job_id != 0, f"Batch submit failure"
    output = atf.run_command_output(f"scontrol show job {job_id}")
    assert re.search(
        rf"Account={my_acct.lower()}", output
    ), f"Account information not processed from sbatch"

    file_in = atf.module_tmp_path / "file_in"
    atf.make_bash_script(file_in, """env | grep SLURM_ACCOUNT""")
    result = atf.run_job(f"-v {file_in}", env_vars=f"SLURM_ACCOUNT={qa_acct}")
    job_id = re.search(r"jobid (\d+)", result["stderr"]).group(1)
    assert (
        re.search(rf"SLURM_ACCOUNT={qa_acct}", result["stdout"]) is not None
    ), f"Account information not processed in srun from env var"
    output = atf.run_command_output(f"scontrol show job {job_id}")
    assert (
        re.search(rf"Account={qa_acct.lower()}", output) is not None
    ), f"Account information not processed from env var in scontrol"
