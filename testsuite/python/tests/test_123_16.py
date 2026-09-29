############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Ticket 01261320: jobs that already lost access don't block resv updates.

slurmctld rejects a reservation update when a pending or running job using
the reservation would lose access to it because of that update. A job can
also lose access with no reservation update at all, and such a job must not
block every later update of the reservation for the rest of its life:

- The job's account is moved to a parent that the reservation doesn't allow.
- A pending job's account is changed to one that the reservation doesn't
  allow. Job updates only re-check reservation access when the job's
  reservation is changed.

An update that does take access away from a job must still be rejected, even
when another job in the reservation had already lost access.

Losing access through a group membership refresh (GroupUpdateTime) is not
covered here, since it requires editing group membership on the controller.
"""

import re

import pytest

import atf

test_user = atf.properties["test-user"]
slurm_user = atf.properties["slurm-user"]

# Root and SlurmUser have access to every reservation, so they can never
# lose it.
pytestmark = pytest.mark.skipif(
    test_user in ("root", slurm_user),
    reason=f"This test requires SlurmTestUser ({test_user}) to be neither root nor SlurmUser ({slurm_user}).",
)

parent_a = "test_123_16_parent_a"
parent_b = "test_123_16_parent_b"
child_acct = "test_123_16_child"
child_acct_2 = "test_123_16_child_2"
other_acct = "test_123_16_other"
resv_name = "resv_123_16"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    # Moving an account only changes a job's access when access is checked
    # through the association hierarchy.
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def accounts():
    """Test accounts, with test-user in all of them except the parents.

    child_acct and child_acct_2 are under parent_a. parent_b and other_acct
    are outside of it.
    """
    cluster = atf.get_config_parameter("ClusterName")
    try:
        for acct in (
            parent_a,
            parent_b,
            f"{child_acct} parent={parent_a}",
            f"{child_acct_2} parent={parent_a}",
            other_acct,
        ):
            atf.run_command(
                f"sacctmgr -i add account {acct} cluster={cluster}",
                user=slurm_user,
                fatal=True,
            )
        atf.run_command(
            f"sacctmgr -i add user {test_user} "
            f"account={child_acct},{child_acct_2},{other_acct} cluster={cluster}",
            user=slurm_user,
            fatal=True,
        )
        for acct in (child_acct, child_acct_2, other_acct):
            wait_for_user_assoc(acct)
    except BaseException:
        # Clean up whatever setup added, but ignore the result so that the
        # failure reported is the one that stopped setup
        delete_accounts()
        raise

    yield

    # Retry while the database may still consider cancelled jobs to be running
    for _ in atf.timer():
        result = delete_accounts()
        if result["exit_code"] == 0:
            break
    else:
        pytest.fail(f"Couldn't delete the test accounts: {result['stderr']}")


@pytest.fixture
def reservation(accounts):
    """A one node reservation for parent_a and its subaccounts."""
    atf.run_command(
        f"scontrol create reservationname={resv_name} accounts={parent_a} "
        "start=now duration=10 nodecnt=1",
        user=slurm_user,
        fatal=True,
    )

    yield

    # A reservation cannot be deleted while jobs are still in it, and the
    # conftest job cleanup only runs after this fixture.
    job_ids = atf.run_command_output(
        f"squeue --noheader --reservation={resv_name} --format=%i",
        user=slurm_user,
        fatal=True,
    ).split()
    atf.cancel_jobs([int(job_id) for job_id in job_ids], user=slurm_user, fatal=True)
    atf.run_command(
        f"scontrol delete reservationname={resv_name}",
        user=slurm_user,
        quiet=True,
        fatal=True,
    )


@pytest.fixture
def restore_child_parent(accounts):
    yield
    # Restore even if slurmctld still shows child_acct under parent_a, since
    # an earlier move may have reached the database without reaching
    # slurmctld. sacctmgr fails harmlessly if nothing is modified.
    move_child_to(parent_a, fatal=False)


def delete_accounts():
    """Delete the test accounts, and test-user's associations in them.

    Do it in a single request, since a user's default association can't be
    removed while the user keeps any others.
    """
    return atf.run_command(
        f"sacctmgr -i delete account "
        f"{child_acct},{child_acct_2},{other_acct},{parent_a},{parent_b}",
        user=slurm_user,
        quiet=True,
    )


def wait_for_user_assoc(acct):
    """Wait until slurmctld has test-user's association in acct."""
    pattern = rf"\bAccount={re.escape(acct)} UserName={re.escape(test_user)}\("
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(
            f"scontrol show assoc_mgr accounts={acct} flags=assoc",
            user=slurm_user,
            quiet=True,
            fatal=True,
        )
        if re.search(pattern, output):
            break


def move_child_to(parent, fatal=True):
    """Move child_acct under parent, and wait until slurmctld has it there."""
    atf.run_command(
        f"sacctmgr -i modify account {child_acct} set parent={parent}",
        user=slurm_user,
        fatal=fatal,
    )
    pattern = rf"\bParentAccount={re.escape(parent)}\("
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(
            f"scontrol show assoc_mgr accounts={child_acct} flags=assoc",
            user=slurm_user,
            quiet=True,
            fatal=True,
        )
        if re.search(pattern, output):
            break


def submit_job(acct, hold):
    """Submit a job to the reservation, running or held pending."""
    job_id = atf.submit_job_sbatch(
        f"{'--hold ' if hold else ''}-N1 -t5 --account={acct} "
        f"--reservation={resv_name} --wrap 'sleep infinity'",
        user=test_user,
        fatal=True,
    )
    if hold:
        atf.wait_for_job_state(job_id, "PENDING", "JobHeldUser", fatal=True)
    else:
        atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    return job_id


def assert_access_denied(acct):
    """Assert that a new job in acct is denied access to the reservation.

    Access is evaluated the same way for test-user's jobs in acct that are
    already in the reservation, so this shows that they have lost access too.
    """
    result = atf.run_command(
        f"sbatch --hold -N1 --account={acct} --reservation={resv_name} --wrap true",
        user=test_user,
        xfail=True,
    )
    assert (
        result["exit_code"] != 0
    ), f"A new job in account {acct} should be denied access to the reservation"
    assert (
        "Access denied to requested reservation" in result["stderr"]
    ), f"Expected 'Access denied to requested reservation', got: {result['stderr']}"


def assert_duration_updated(minutes):
    result = atf.run_command(
        f"scontrol update reservationname={resv_name} duration={minutes}",
        user=slurm_user,
    )
    assert (
        result["exit_code"] == 0
    ), f"Reservation update should succeed, got: {result['stderr']}"
    assert (
        atf.get_reservations(user=slurm_user, fatal=True)[resv_name]["Duration"]
        == f"00:{minutes:02d}:00"
    ), f"Reservation duration should be updated to {minutes} minutes"


def assert_job_unaffected(job_id, hold):
    """Assert that the job kept its state and its reservation."""
    assert atf.get_job_parameter(job_id, "JobState") == (
        "PENDING" if hold else "RUNNING"
    ), "The job should not be affected by the reservation update"
    assert (
        atf.get_job_parameter(job_id, "Reservation") == resv_name
    ), "The job should still be in the reservation after the update"


def assert_update_rejected(update, job_id):
    """Assert that job_id blocks the update, and Accounts is still parent_a.

    scontrol reports the first job found that would lose access because of
    the update.
    """
    result = atf.run_command(
        f"scontrol update reservationname={resv_name} {update}",
        user=slurm_user,
        xfail=True,
    )
    assert (
        result["exit_code"] != 0
    ), f"Update '{update}' removes job {job_id}'s access to the reservation, so it should be rejected"
    assert (
        "Requested reservation is in use" in result["stderr"]
    ), f"Expected 'Requested reservation is in use', got: {result['stderr']}"
    match = re.search(r"rejected because of JobId=(\d+)", result["stderr"])
    assert match, f"Expected the job blocking the update, got: {result['stderr']}"
    assert (
        int(match.group(1)) == job_id
    ), f"Update '{update}' should be blocked by job {job_id}, not job {match.group(1)}"
    assert (
        atf.get_reservations(user=slurm_user, fatal=True)[resv_name]["Accounts"]
        == parent_a
    ), "A rejected update should leave the reservation unchanged"


def xfail_before_fix(request):
    """Expect the rest of the test to fail on versions without the fix."""
    if atf.get_version("sbin/slurmctld") < (26, 5, 5):
        request.applymarker(
            pytest.mark.xfail(
                reason="Ticket 01261320: Jobs that already lost access blocked "
                "reservation updates, fixed in 26.05.5"
            )
        )


@pytest.mark.parametrize("hold", [False, True], ids=["running", "pending"])
def test_account_moved(request, reservation, restore_child_parent, hold):
    """A job whose account moved out of the reservation doesn't block updates."""
    job_id = submit_job(child_acct, hold)
    assert_duration_updated(15)

    move_child_to(parent_b)
    assert_access_denied(child_acct)

    xfail_before_fix(request)
    assert_duration_updated(20)
    assert_job_unaffected(job_id, hold)


def test_pending_job_account_changed(request, reservation):
    """A pending job moved to a disallowed account doesn't block updates."""
    job_id = submit_job(child_acct, hold=True)
    assert_duration_updated(15)

    # Changing only the account of a job doesn't re-check its access to the
    # reservation
    atf.run_command(
        f"scontrol update jobid={job_id} account={other_acct}",
        user=test_user,
        fatal=True,
    )
    assert (
        atf.get_job_parameter(job_id, "Account") == other_acct
    ), f"Job account should be {other_acct}"
    assert (
        atf.get_job_parameter(job_id, "Reservation") == resv_name
    ), f"Job should still request reservation {resv_name}"
    assert_access_denied(other_acct)

    xfail_before_fix(request)
    assert_duration_updated(20)
    assert_job_unaffected(job_id, hold=True)


@pytest.mark.parametrize("hold", [False, True], ids=["running", "pending"])
def test_update_removing_access_rejected(reservation, hold):
    """An update that takes away a job's access is still rejected."""
    job_id = submit_job(child_acct, hold)
    assert_update_rejected(f"accounts={parent_b}", job_id)


def test_update_rejected_for_later_job(request, reservation, restore_child_parent):
    """A job that already lost access doesn't end the check of later jobs.

    Jobs are checked in submission order. The first job lost access when
    child_acct moved under parent_b, so each update below must allow it and
    go on to check the second job. The second job would lose access because
    of each update, so each update must be rejected because of it.
    """
    # Hold both jobs, since the one node reservation may not have room to run
    # both
    submit_job(child_acct, hold=True)
    second_job_id = submit_job(child_acct_2, hold=True)

    move_child_to(parent_b)
    assert_access_denied(child_acct)

    # The first job has access after this update, but not before it
    assert_update_rejected(f"accounts={parent_b}", second_job_id)

    # The first job has access neither before nor after this update. Without
    # the fix, the update is rejected because of the first job instead.
    xfail_before_fix(request)
    assert_update_rejected(f"accounts={other_acct}", second_job_id)
