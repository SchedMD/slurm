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
    """Test that removal of a single QOS:
    1. Marks pending jobs with InvalidQOS
    2. Rejects new job submissions using removed QOS
    """

    # Stop slurmdbd and submit job
    atf.stop_slurmdbd(quiet=True)
    job_id = submit_job_with(f"--qos={qos1}", fatal=True)

    # Stop slurmctld, start slurmdbd, remove QOS
    atf.stop_slurmctld(quiet=True)
    atf.start_slurmdbd(quiet=True)
    atf.run_command(
        f"sacctmgr -i remove qos {qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Start slurmctld and verify job state/reason
    atf.start_slurmctld(quiet=True)

    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason="InvalidQOS"
    ), f"Job {job_id} not in PENDING state with InvalidQOS reason"

    # Try to submit a new job with removed QOS - should be rejected
    assert (
        submit_job_with(f"--qos={qos1}", xfail=True) == 0
    ), f"Job submission with removed QOS {qos1} should have failed but got job id {job_id2}"

    # Submit a job with remaining QOS - should succeed
    assert (
        submit_job_with(f"--qos={qos2}") != 0
    ), f"Job submission with valid QOS {qos2} should have succeeded"


def test_qos_removal_multiple():
    """Test QOS removal when user has multiple QOS access:
    1. Verifies jobs with removed QOS get marked InvalidQOS
    2. Verifies jobs with remaining QOS stay valid
    3. Verifies new job submissions with removed QOS are rejected
    4. Verifies new job submissions with remaining QOS succeed
    """

    # Stop slurmdbd and submit jobs with different QOSs
    atf.stop_slurmdbd(quiet=True)
    job_id1 = submit_job_with(f"--qos={qos1}", fatal=True)
    job_id2 = submit_job_with(f"--qos={qos2}", fatal=True)

    # Stop slurmctld, start slurmdbd, remove first QOS
    atf.stop_slurmctld(quiet=True)
    atf.start_slurmdbd(quiet=True)
    atf.run_command(
        f"sacctmgr -i remove qos {qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Start slurmctld and verify jobs state/reason
    atf.start_slurmctld(quiet=True)

    # First job should be PENDING with InvalidQOS
    assert atf.wait_for_job_state(
        job_id1, "PENDING", desired_reason="InvalidQOS"
    ), f"Job {job_id1} not in PENDING state and InvalidQOS reason"

    # Second job should stay PENDING with different reason
    assert atf.wait_for_job_state(
        job_id2, "PENDING", timeout=10
    ), f"Job {job_id2} should be in PENDING state"
    assert (
        atf.get_job_parameter(job_id2, "Reason") != "InvalidQOS"
    ), "The second job whose QOS was not deleted should not be pending due to 'InvalidQOS'"

    # Try to submit a new job with removed QOS - should be rejected
    assert (
        submit_job_with(f"--qos={qos1}", xfail=True) == 0
    ), f"Job submission with removed QOS {qos1} should have failed"

    # Submit a job with remaining QOS - should succeed
    assert (
        submit_job_with(f"--qos={qos2}") != 0
    ), f"Job submission with valid QOS {qos2} should have succeeded"


def test_qos_removal_running_vs_pending():
    """Test QOS removal impact on running vs pending jobs:
    1. Submit two jobs with same QOS - one running, one pending
    2. Remove the QOS
    3. Verify running job continues running
    4. Verify pending job gets marked with InvalidQOS
    """

    # Stop slurmdbd and submit jobs - use exclusive to ensure only one can run
    atf.stop_slurmdbd(quiet=True)
    job_id1 = submit_job_with(f"--qos={qos1} --exclusive", fatal=True)
    job_id2 = submit_job_with(f"--qos={qos1} --exclusive", fatal=True)

    # Wait for first job to start running
    assert atf.wait_for_job_state(
        job_id1, "RUNNING"
    ), f"Job {job_id1} never started running"

    # Verify second job is pending (due to exclusive)
    assert atf.wait_for_job_state(
        job_id2, "PENDING"
    ), f"Job {job_id2} should be pending"

    # Stop slurmctld, start slurmdbd, remove QOS
    atf.stop_slurmctld(quiet=True)
    atf.start_slurmdbd(quiet=True)
    atf.run_command(
        f"sacctmgr -i remove qos {qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Start slurmctld and verify jobs state/reason
    atf.start_slurmctld(quiet=True)

    # Running job should continue running
    assert atf.wait_for_job_state(
        job_id1, "RUNNING"
    ), f"Previously running job {job_id1} should stay RUNNING"
    assert (
        atf.get_job_parameter(job_id1, "Reason") == "None"
    ), f"Running job {job_id1} should have 'None' as reason"

    # Pending job should be marked with InvalidQOS
    assert atf.wait_for_job_state(
        job_id2, "PENDING", desired_reason="InvalidQOS"
    ), f"Pending job {job_id2} should be PENDING with InvalidQOS reason"


def test_account_removal_single():
    """Test that removal of a single account:
    1. Marks pending jobs with InvalidAccount
    2. Rejects new job submissions using removed account
    """

    # Stop slurmdbd and submit job
    atf.stop_slurmdbd(quiet=True)
    job_id = submit_job_with(f"--account={acct1}", fatal=True)

    # Stop slurmctld, start slurmdbd, remove account
    atf.stop_slurmctld(quiet=True)
    atf.start_slurmdbd(quiet=True)
    atf.run_command(
        f"sacctmgr -i remove account {acct1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Start slurmctld and verify job state/reason
    atf.start_slurmctld(quiet=True)

    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason="InvalidAccount"
    ), f"Job {job_id} not in PENDING state and InvalidAccount reason"

    # Try to submit a new job with removed account - should be rejected
    assert (
        submit_job_with(f"--account={acct1}", xfail=True) == 0
    ), f"Job submission with removed account {acct1} should have failed"


def test_account_removal_multiple():
    """Test account removal when user has multiple account access:
    1. Verifies jobs with removed account get marked InvalidAccount
    2. Verifies jobs with remaining account stay valid
    3. Verifies new job submissions with removed account are rejected
    4. Verifies new job submissions with remaining account succeed
    """

    # Stop slurmdbd and submit jobs with different accounts
    atf.stop_slurmdbd(quiet=True)
    job_id1 = submit_job_with(f"--account={acct1}", fatal=True)
    job_id2 = submit_job_with(f"--account={acct2}", fatal=True)

    # Stop slurmctld, start slurmdbd, remove first account
    atf.stop_slurmctld(quiet=True)
    atf.start_slurmdbd(quiet=True)
    atf.run_command(
        f"sacctmgr -i remove account {acct1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Start slurmctld and verify jobs state/reason
    atf.start_slurmctld(quiet=True)

    # First job should be PENDING with InvalidAccount
    assert atf.wait_for_job_state(
        job_id1, "PENDING", desired_reason="InvalidAccount"
    ), f"Job {job_id1} not in PENDING state and InvalidAccount reason"

    # Second job should stay PENDING with different reason
    assert atf.wait_for_job_state(
        job_id2, "PENDING", timeout=10
    ), f"Job {job_id2} should be in PENDING state"
    assert (
        atf.get_job_parameter(job_id2, "Reason") != "InvalidAccount"
    ), "The second job whose account was not deleted should not be pending due to 'InvalidAccount'"

    # Try to submit a new job with removed account - should be rejected
    assert (
        submit_job_with(f"--account={acct1}", xfail=True) == 0
    ), f"Job submission with removed account {acct1} should have failed"

    # Submit a job with remaining account - should succeed
    assert (
        submit_job_with(f"--account={acct2}") != 0
    ), f"Job submission with valid account {acct2} should have succeeded"


def test_account_removal_running_vs_pending():
    """Test account removal impact on running vs pending jobs:
    1. Submit two jobs with same account - one running, one pending
    2. Remove the account
    3. Verify running job continues running
    4. Verify pending job gets marked with InvalidAccount
    """

    # Stop slurmdbd and submit jobs - use exclusive to ensure only one can run
    atf.stop_slurmdbd(quiet=True)
    job_id1 = submit_job_with(f"--account={acct1} --exclusive", fatal=True)
    job_id2 = submit_job_with(f"--account={acct1} --exclusive", fatal=True)

    # Wait for first job to start running
    assert atf.wait_for_job_state(
        job_id1, "RUNNING"
    ), f"Job {job_id1} never started running"

    # Verify second job is pending (due to exclusive)
    assert atf.wait_for_job_state(
        job_id2, "PENDING"
    ), f"Job {job_id2} should be pending"

    # Stop slurmctld, start slurmdbd, remove account
    atf.stop_slurmctld(quiet=True)
    atf.start_slurmdbd(quiet=True)
    atf.run_command(
        f"sacctmgr -i remove account {acct1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Start slurmctld and verify jobs state/reason
    atf.start_slurmctld(quiet=True)

    # Running job should continue running
    assert atf.wait_for_job_state(
        job_id1, "RUNNING"
    ), f"Previously running job {job_id1} should stay RUNNING"
    assert (
        atf.get_job_parameter(job_id1, "Reason") == "None"
    ), f"Running job {job_id1} should have 'None' as reason"

    # Pending job should be marked with InvalidAccount
    assert atf.wait_for_job_state(
        job_id2, "PENDING", desired_reason="InvalidAccount"
    ), f"Pending job {job_id2} should be PENDING with InvalidAccount reason"
