############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

acct = "test_acct"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_slurm_running()


def assert_account(job_id, account):
    """Verify that job_id's account is the expected account passed, in slurmctld and slurmdbd"""
    job_info = atf.get_job(job_id)
    assert (
        job_info["Account"] == account
    ), f"Job's account in slurmctld should be {account}"

    rc = atf.repeat_until(
        lambda: atf.get_jobs(dbd=True), lambda jobs: jobs[job_id]["account"] == account
    )
    assert rc, f"Job's account in slurmdbd should be {account}"


@pytest.mark.parametrize("command", ["sbatch", "srun", "salloc"])
def test_account_option(command):
    job_id = atf.submit_job(
        command, f"--account={acct} -t1", "true", wrap_job=True, fatal=True
    )
    assert_account(job_id, acct)


@pytest.mark.parametrize(
    "command,env_var",
    [
        ("sbatch", "SBATCH_ACCOUNT"),
        ("srun", "SLURM_ACCOUNT"),
        ("salloc", "SALLOC_ACCOUNT"),
    ],
)
def test_account_env_var(command, env_var):
    job_id = atf.submit_job(
        command,
        "-t1",
        "true",
        wrap_job=True,
        env_vars=f"{env_var}={acct}",
        fatal=True,
    )
    assert_account(job_id, acct)
