############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

# Global variables
qos1 = "qos1"
qos2 = "qos2"
acct1 = "acct1"
acct2 = "acct2"
acct = "acct"


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Test setup with required configurations."""
    atf.require_auto_config("Manually creating and deleting qoses and accounts")
    atf.require_config_parameter("AccountingStorageType", "accounting_storage/slurmdbd")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db():
    # Create test QOS and account
    atf.run_command(
        f"sacctmgr -i add qos {qos1},{qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct},{acct1},{acct2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {atf.get_user_name()} DefaultAccount={acct} account={acct1},{acct2} qos=normal,{qos1},{qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield

    atf.cancel_all_jobs(fatal=True)

    atf.run_command(
        f"sacctmgr -i remove user {atf.get_user_name()} {acct1},{acct2}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {acct1},{acct2}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove qos {qos1},{qos2}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def submit_job_with(extra_params, xfail=False, fatal=False):
    """Submit a job with specified extra params."""
    return atf.submit_job_sbatch(
        f"{extra_params} -N1 --wrap='sleep 300'", xfail=xfail, fatal=fatal
    )


def test_qos_removal_single():
    """Test removing a QOS in use:
    - Verify that running jobs with that QOS keep running.
    - Verify that pending jobs are updated to InvalidQOS
    - Verify that new jobs cannot use the removed QOS.
    """
    # Stop slurmdbd to avoid the job info being saved in the DB
    atf.stop_slurmdbd(quiet=True)

    # Submit a blocking job
    job_id1 = submit_job_with(f"--qos={qos1} --exclusive", fatal=True)
    assert atf.wait_for_job_state(
        job_id1, "RUNNING"
    ), f"Job {job_id1} never started running"

    # Submit another job in the same node to be blocked (due exclusive)
    node = atf.get_job_parameter(job_id1, "NodeList")
    job_id2 = submit_job_with(f"--qos={qos1} -w {node}", fatal=True)
    assert atf.wait_for_job_state(
        job_id2, "PENDING"
    ), f"Job {job_id2} should be pending"

    # Stop slurmctld before starting slurmdbd to keep the jobs info out of
    # the DB, only in slurmctld for the moment.
    atf.stop_slurmctld(quiet=True)
    atf.start_slurmdbd(quiet=True)

    # Remove the QOS from the DB.
    # Note that slurmdbd won't have the QOS or the jobs using it, while
    # slurmctld knows the jobs and still thinks that the QOS exists.
    atf.run_command(
        f"sacctmgr -i remove qos {qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Start slurmctld and verify job states/reasons are the expected now that
    # the QOS doesn't exists anymore.
    atf.start_slurmctld(quiet=True)

    # Running job should continue running
    assert atf.wait_for_job_state(
        job_id1, "RUNNING", desired_reason="None"
    ), f"Previously running job {job_id1} should stay RUNNING with 'None' reason"

    # Pending job should be marked with InvalidQOS
    assert atf.wait_for_job_state(
        job_id2, "PENDING", desired_reason="InvalidQOS"
    ), f"Pending job {job_id2} should be PENDING with InvalidQOS reason"

    # Try to submit a new job with removed QOS - should be rejected
    assert (
        submit_job_with(f"--qos={qos1}", xfail=True) == 0
    ), f"Job submission with removed QOS {qos1} should have failed"


def test_qos_removal_multiple():
    """Test QOS removal when user has multiple QOS access:
    - Verify that running jobs with removed QOS keep running.
    - Verify that pending jobs with removed QOS are updated to InvalidQOS.
    - Verify that jobs with remaining QOS stay valid.
    - Verify that new jobs cannot use the removed QOS.
    - Verify that new job can use the remaining QOS.
    """

    # Stop slurmdbd to avoid the job info being saved in the DB
    atf.stop_slurmdbd(quiet=True)

    # Submit a blocking job
    job_id1 = submit_job_with(f"--qos={qos1} --exclusive", fatal=True)
    assert atf.wait_for_job_state(
        job_id1, "RUNNING"
    ), f"Job {job_id1} never started running"

    # Submit two more jobs in the same node to be blocked (due exclusive)
    node = atf.get_job_parameter(job_id1, "NodeList")
    job_id2 = submit_job_with(f"--qos={qos1} -w {node}", fatal=True)
    job_id3 = submit_job_with(f"--qos={qos2} -w {node}", fatal=True)

    # Verify both jobs are pending
    assert atf.wait_for_job_state(
        job_id2, "PENDING"
    ), f"Job {job_id2} should be pending"
    assert atf.wait_for_job_state(
        job_id3, "PENDING"
    ), f"Job {job_id3} should be pending"

    # Stop slurmctld before starting slurmdbd to keep the jobs info out of
    # the DB, only in slurmctld for the moment.
    atf.stop_slurmctld(quiet=True)
    atf.start_slurmdbd(quiet=True)

    # Remove the QOS from the DB.
    # Note that slurmdbd won't have the QOS or the jobs using it, while
    # slurmctld knows the jobs and still thinks that the QOS exists.
    atf.run_command(
        f"sacctmgr -i remove qos {qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Start slurmctld and verify job states/reasons are the expected now that
    # the QOS doesn't exists anymore.
    atf.start_slurmctld(quiet=True)

    # Running job should continue running
    assert atf.wait_for_job_state(
        job_id1, "RUNNING", desired_reason="None"
    ), f"Previously running job {job_id1} should stay RUNNING with 'None' reason"

    # Pending job with removed QOS should be marked with InvalidQOS
    assert atf.wait_for_job_state(
        job_id2, "PENDING", desired_reason="InvalidQOS"
    ), f"Pending job {job_id2} should be PENDING with InvalidQOS reason"

    # Pending job with remaining QOS should stay valid
    assert atf.wait_for_job_state(
        job_id3, "PENDING"
    ), f"Job {job_id3} should be PENDING"
    assert (
        atf.get_job_parameter(job_id3, "Reason") != "InvalidQOS"
    ), f"Job {job_id3} using qos2 should not have InvalidQOS reason"

    # Try to submit a new job with removed QOS - should be rejected
    assert (
        submit_job_with(f"--qos={qos1}", xfail=True) == 0
    ), f"Job submission with removed QOS {qos1} should have failed"

    # Submit a job with remaining QOS - should succeed
    assert (
        submit_job_with(f"--qos={qos2}") != 0
    ), f"Job submission with valid QOS {qos2} should have succeeded"


def test_account_removal_single():
    """Test removing an account in use:
    - Verify that running jobs with that account keep running.
    - Verify that pending jobs are updated to InvalidAccount.
    - Verify that new jobs cannot use the removed account.
    """

    # Stop slurmdbd to avoid the job info being saved in the DB
    atf.stop_slurmdbd(quiet=True)

    # Submit a blocking job
    job_id1 = submit_job_with(f"--account={acct1} --exclusive", fatal=True)
    assert atf.wait_for_job_state(
        job_id1, "RUNNING"
    ), f"Job {job_id1} never started running"

    # Submit another job in the same node to be blocked (due exclusive)
    node = atf.get_job_parameter(job_id1, "NodeList")
    job_id2 = submit_job_with(f"--account={acct1} -w {node}", fatal=True)
    assert atf.wait_for_job_state(
        job_id2, "PENDING"
    ), f"Job {job_id2} should be pending"

    # Stop slurmctld before starting slurmdbd to keep the jobs info out of
    # the DB, only in slurmctld for the moment.
    atf.stop_slurmctld(quiet=True)
    atf.start_slurmdbd(quiet=True)

    # Remove the account from the DB.
    # Note that slurmdbd won't have the account or the jobs using it, while
    # slurmctld knows the jobs and still thinks that the account exists.
    atf.run_command(
        f"sacctmgr -i remove account {acct1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Start slurmctld and verify job states/reasons are the expected now that
    # the account doesn't exists anymore.
    atf.start_slurmctld(quiet=True)

    # Running job should continue running
    assert atf.wait_for_job_state(
        job_id1, "RUNNING", desired_reason="None"
    ), f"Previously running job {job_id1} should stay RUNNING with 'None' reason"

    # Pending job should be marked with InvalidAccount
    assert atf.wait_for_job_state(
        job_id2, "PENDING", desired_reason="InvalidAccount"
    ), f"Pending job {job_id2} should be PENDING with InvalidAccount reason"

    # Try to submit a new job with removed account - should be rejected
    assert (
        submit_job_with(f"--account={acct1}", xfail=True) == 0
    ), f"Job submission with removed account {acct1} should have failed"


def test_account_removal_multiple():
    """Test removing an account when user has multiple account access:
    - Verify that running jobs with removed account keep running.
    - Verify that pending jobs with removed account are updated to InvalidAccount.
    - Verify that jobs with remaining account stay valid.
    - Verify that new jobs cannot use the removed account.
    - Verify that new jobs can use the remaining account.
    """

    # Stop slurmdbd to avoid the job info being saved in the DB
    atf.stop_slurmdbd(quiet=True)

    # Submit a blocking job
    job_id1 = submit_job_with(f"--account={acct1} --exclusive", fatal=True)
    assert atf.wait_for_job_state(
        job_id1, "RUNNING"
    ), f"Job {job_id1} never started running"

    # Submit two more jobs in the same node to be blocked (due exclusive)
    node = atf.get_job_parameter(job_id1, "NodeList")
    job_id2 = submit_job_with(f"--account={acct1} -w {node}", fatal=True)
    job_id3 = submit_job_with(f"--account={acct2} -w {node}", fatal=True)

    # Verify both jobs are pending
    assert atf.wait_for_job_state(
        job_id2, "PENDING"
    ), f"Job {job_id2} should be pending"
    assert atf.wait_for_job_state(
        job_id3, "PENDING"
    ), f"Job {job_id3} should be pending"

    # Stop slurmctld before starting slurmdbd to keep the jobs info out of
    # the DB, only in slurmctld for the moment.
    atf.stop_slurmctld(quiet=True)
    atf.start_slurmdbd(quiet=True)

    # Remove the account from the DB.
    # Note that slurmdbd won't have the account or the jobs using it, while
    # slurmctld knows the jobs and still thinks that the account exists.
    atf.run_command(
        f"sacctmgr -i remove account {acct1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Start slurmctld and verify job states/reasons are the expected now that
    # the account doesn't exists anymore.
    atf.start_slurmctld(quiet=True)

    # Running job should continue running
    assert atf.wait_for_job_state(
        job_id1, "RUNNING", desired_reason="None"
    ), f"Previously running job {job_id1} should stay RUNNING with 'None' reason"

    # Pending job with removed account should be marked with InvalidAccount
    assert atf.wait_for_job_state(
        job_id2, "PENDING", desired_reason="InvalidAccount"
    ), f"Pending job {job_id2} should be PENDING with InvalidAccount reason"

    # Pending job with remaining account should stay valid
    assert atf.wait_for_job_state(
        job_id3, "PENDING"
    ), f"Job {job_id3} should be PENDING"
    assert (
        atf.get_job_parameter(job_id3, "Reason") != "InvalidAccount"
    ), f"Job {job_id3} using acct2 should not have InvalidAccount reason"

    # Try to submit a new job with removed account - should be rejected
    assert (
        submit_job_with(f"--account={acct1}", xfail=True) == 0
    ), f"Job submission with removed account {acct1} should have failed"

    # Submit a job with remaining account - should succeed
    assert (
        submit_job_with(f"--account={acct2}") != 0
    ), f"Job submission with valid account {acct2} should have succeeded"
