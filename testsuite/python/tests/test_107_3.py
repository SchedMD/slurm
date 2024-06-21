############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import os
import logging
import pytest
import atf


# Globals
user_name = atf.get_user_name()
test_name = os.path.splitext(os.path.basename(__file__))[0]
acct1 = f"{test_name}_acct1"
acct2 = f"{test_name}_acct2"
name1 = f"{test_name}_job1"
name2 = f"{test_name}_job2"
nodes = ["node1", "node2", "node3"]
partitions = [f"{test_name}_part1", f"{test_name}_part2", f"{test_name}_part3"]
qos1 = f"{test_name}_qos1"
qos2 = f"{test_name}_qos2"
reservations = [f"{test_name}_resv1", f"{test_name}_resv2", f"{test_name}_resv3"]
wckey = f"{test_name}_wckey1"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Test needs to run a 2 het job with 3 components, 2 jobs, 2 arrays of 3 jobs, and 2 arrays with 2 jobs.
    # We need 18 nodes to be able to run with select_linear, and 7+ CPUs per node
    atf.require_nodes(18, [("CPUs", 8), ("RealMemory", 100)])
    atf.require_accounting(True)
    atf.require_config_parameter("TrackWcKey", "yes")
    atf.require_config_parameter("TrackWcKey", "yes", source="slurmdbd")
    # Reducing bf_interval makes the test faster when using het jobs.
    atf.add_config_parameter_value("SchedulerParameters", "bf_interval=2")
    atf.require_slurm_running()

    # Basic account and user setup
    sacctmgr_acct = f"account {acct1} {acct2}"
    sacctmgr_user = f"user {user_name} account={acct1},{acct2}"
    atf.run_command(
        f"sacctmgr -i add {sacctmgr_acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add {sacctmgr_user}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i del {sacctmgr_user} ",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i del {sacctmgr_acct} ",
        user=atf.properties["slurm-user"],
    )


# Cancel all jobs before and after the test
@pytest.fixture(scope="function", autouse=True)
def cancel_jobs():
    atf.cancel_all_jobs()
    yield
    atf.cancel_all_jobs()


#
# Main fixtures
#
@pytest.fixture
def setup_partitions():
    for part in partitions:
        atf.run_command(
            f"scontrol create partitionname={part} Nodes=ALL",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    yield

    atf.cancel_all_jobs()
    for part in partitions:
        atf.run_command(
            f"scontrol delete partitionname={part}",
            user=atf.properties["slurm-user"],
        )


@pytest.fixture
def setup_qos():
    atf.run_command(
        f"sacctmgr -i add qos {qos1} {qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command("sacctmgr show assoc tree")
    atf.run_command(
        f"sacctmgr -i mod user {user_name} set qos+={qos1},{qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    atf.cancel_all_jobs()
    atf.run_command(
        f"sacctmgr -i mod user {user_name} set qos-={qos1},{qos2}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i del qos {qos1} {qos2}",
        user=atf.properties["slurm-user"],
    )


@pytest.fixture
def setup_reservations():
    for resv in reservations:
        # Hetjobs need 3 nodes in the reservation
        atf.run_command(
            f"scontrol create reservationname={resv} starttime=now duration=5 users={user_name} nodecnt=3",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    yield

    atf.cancel_all_jobs()
    for resv in reservations:
        atf.run_command(
            f"scontrol delete reservationname={resv}",
            user=atf.properties["slurm-user"],
        )


@pytest.fixture
def setup_wckeys():
    atf.run_command(
        f"sacctmgr -i add user {user_name} set wckey={wckey}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    atf.cancel_all_jobs()
    atf.run_command(
        f"sacctmgr -i del user {user_name} wckey={wckey}",
        user=atf.properties["slurm-user"],
    )


#
# Custom fixtures, depending on main ones
#
@pytest.fixture
def setup_partition2_down(setup_partitions):
    """Set partitions[2] down so we ensure that matching jobs will run on
    partition[0] and mistmatching ones in partition[1]"""
    atf.run_command(
        f"scontrol update partitionname={partitions[2]} state=down",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.fixture
def setup_reservations_used(setup_reservations):
    """'Run an exlusive job in reservations[1] to ensure that jobs run in
    reservations[0]"""
    job_id = atf.submit_job_sbatch(
        f"--exclusive --reservation={reservations[1]} --wrap 'srun sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)


#
# Helper functions
#
def submit_job(job_list, opt, opt_val, expected_state):
    test_opt = ""
    other_opts = ""

    if opt != None:
        test_opt = f"--{opt}={opt_val}"
    if expected_state == "PENDING":
        other_opts = "--hold"

    job_list.append(
        (
            atf.submit_job_sbatch(
                f"-n1 -c1 --mem=10 {test_opt} {other_opts} --wrap 'srun sleep infinity'",
                fatal=True,
            )
        )
    )


def submit_het_job(job_list, opt, opt_val_1, opt_val_2, expected_state):
    het_opts_1 = f"-n1 -c1 --mem=10"
    het_opts_2 = f"-n1 -c1 --mem=10"
    other_opts = ""

    if expected_state == "PENDING":
        other_opts += " --hold"

    # Nodelist only for the first component as each component needs a different node
    if opt != "nodelist":
        het_opts_1 += f" --{opt}={opt_val_1}"
        het_opts_2 += f" --{opt}={opt_val_2}"
    else:
        het_opts_1 += f" --{opt}={opt_val_1}"

    # We want to cancel the 2nd component because canceling the first one cancels all components
    job_id = atf.submit_job_sbatch(
        f"{other_opts} {het_opts_2} : {het_opts_1} --wrap 'srun sleep infinity'",
        fatal=True,
    )
    job_list.append(job_id)
    job_list.append(job_id + 1)


def wait_for_jobs(job_list, expected_state, timeout):
    for job_id in job_list:
        atf.wait_for_job_state(job_id, expected_state, fatal=True, timeout=timeout)


def run_scancel(opt, opt_filter, job_list):
    jobs_str = " ".join(map(str, job_list))
    atf.run_command(f"scancel --ctld --{opt}={opt_filter} {jobs_str}")


# Parameters of test_filter and test_filter_hetjob
parameters = [
    # Test --account
    (None, "account", acct1, "account", acct1, acct2, "RUNNING", "RUNNING"),
    # Test --jobname
    (None, "jobname", name1, "job-name", name1, name2, "RUNNING", "RUNNING"),
    # Test --nodelist with single node (only works for running)
    (
        None,
        "nodelist",
        nodes[0],
        "nodelist",
        nodes[0],
        nodes[1],
        "RUNNING",
        "RUNNING",
    ),
    # Test multi-node nodelist (scancel --nodelist only works for running)
    (
        None,
        "nodelist",
        nodes[0],
        "nodelist",
        f"{nodes[0]},{nodes[1]}",
        f"{nodes[1]},{nodes[2]}",
        "RUNNING",
        "RUNNING",
    ),
    # Test qos
    ("setup_qos", "qos", qos1, "qos", qos1, qos2, "RUNNING", "RUNNING"),
    # Test --wckey
    # TODO: This wckey testing is very simple. We should also test with DefaultWckey.
    ("setup_wckeys", "wckey", wckey, "wckey", wckey, "", "RUNNING", "RUNNING"),
    # Test --reservation with a single reservation for pending jobs
    (
        "setup_reservations",
        "reservation",
        reservations[0],
        "reservation",
        reservations[0],
        reservations[1],
        "PENDING",
        "PENDING",
    ),
    # Test --reservation with a single reservation for running jobs
    (
        "setup_reservations",
        "reservation",
        reservations[0],
        "reservation",
        reservations[0],
        reservations[1],
        "RUNNING",
        "RUNNING",
    ),
    # Test --reservation with multiple reservations for pending jobs
    (
        "setup_reservations",
        "reservation",
        reservations[0],
        "reservation",
        reservations[0] + "," + reservations[1],
        reservations[1] + "," + reservations[2],
        "PENDING",
        "PENDING",
    ),
    # Test --reservation with multiple reservation for running jobs
    # Needs an exclusive job in reservation[1] to ensure that job runs into [0]
    (
        "setup_reservations_used",
        "reservation",
        reservations[0],
        "reservation",
        reservations[0] + "," + reservations[1],
        reservations[2],
        "RUNNING",
        "RUNNING",
    ),
    # Test --partition with a single partition for pending jobs
    (
        "setup_partitions",
        "partition",
        partitions[0],
        "partition",
        partitions[0],
        partitions[1],
        "PENDING",
        "PENDING",
    ),
    # Test --partition with a single partition for running jobs
    (
        "setup_partitions",
        "partition",
        partitions[0],
        "partition",
        partitions[0],
        partitions[1],
        "RUNNING",
        "RUNNING",
    ),
    # Test --partition with multiple partitions for pending jobs
    (
        "setup_partitions",
        "partition",
        partitions[0],
        "partition",
        partitions[0] + "," + partitions[1],
        partitions[1] + "," + partitions[2],
        "PENDING",
        "PENDING",
    ),
    # Test --partition with multiple partitions for running jobs
    # Needs partition[2] down to ensure that matching jobs run in [0] and
    # mismatching in [1].
    (
        "setup_partition2_down",
        "partition",
        partitions[0],
        "partition",
        partitions[0] + "," + partitions[2],
        partitions[1] + "," + partitions[2],
        "RUNNING",
        "RUNNING",
    ),
    # Test --state for pending in partition[2] and running in partition[0]
    # Needs partition[2] down to ensure that matching jobs run in [0] and
    # mismatching in [1].
    (
        "setup_partition2_down",
        "state",
        "PENDING",
        "partition",
        partitions[2],
        partitions[0],
        "PENDING",
        "RUNNING",
    ),
    # Test --state for running in partition[0] and pending in partition[2]
    # Needs partition[2] down to ensure that matching jobs run in [0] and
    # mismatching in [1].
    (
        "setup_partition2_down",
        "state",
        "RUNNING",
        "partition",
        partitions[0],
        partitions[2],
        "RUNNING",
        "PENDING",
    ),
]


@pytest.mark.parametrize(
    "fixture,scancel_opt,scancel_val,sbatch_opt,sbatch_match_val,sbatch_mismatch_val,match_job_state,mismatch_job_state",
    parameters,
)
def test_filter(
    request,
    fixture,
    scancel_opt,
    scancel_val,
    sbatch_opt,
    sbatch_match_val,
    sbatch_mismatch_val,
    match_job_state,
    mismatch_job_state,
):
    # Custom fixture may be necessary
    if fixture is not None:
        request.getfixturevalue(fixture)

    matching_jobs = []
    mismatching_jobs = []
    matching_hetjobs = []
    mismatching_hetjobs = []
    het_jobs = []

    logging.info(
        f"Test scancel --ctld --{scancel_opt}={scancel_val}, sbatch --{sbatch_opt}, job match:{sbatch_match_val}, job mismatch:{sbatch_mismatch_val}"
    )

    # Submit jobs
    submit_job(matching_jobs, sbatch_opt, sbatch_match_val, match_job_state)
    submit_job(mismatching_jobs, sbatch_opt, sbatch_mismatch_val, mismatch_job_state)

    # Waiting for jobs
    wait_for_jobs(matching_jobs, match_job_state, 60)
    wait_for_jobs(mismatching_jobs, mismatch_job_state, 60)

    # Just to show jobs in the logs
    atf.run_command_output(
        f"echo ''; squeue --Format=JobId,Account,Name,Nodelist,Partition,Qos,Reservation,State,UserName,WCKey"
    )

    # Cancel jobs with --scancel_opt=scancel:val
    run_scancel(scancel_opt, scancel_val, [])

    # Wait for jobs matching the signal filter to finish
    # And verify that jobs not matching the signal filter are still in their expected state
    wait_for_jobs(matching_jobs, "CANCELLED", 10)
    wait_for_jobs(mismatching_jobs, mismatch_job_state, 60)


@pytest.mark.parametrize(
    "fixture,scancel_opt,scancel_val,sbatch_opt,sbatch_match_val,sbatch_mismatch_val,match_job_state,mismatch_job_state",
    parameters,
)
def test_filter_hetjobs(
    request,
    fixture,
    scancel_opt,
    scancel_val,
    sbatch_opt,
    sbatch_match_val,
    sbatch_mismatch_val,
    match_job_state,
    mismatch_job_state,
):
    # Custom fixture may be necessary
    if fixture is not None:
        request.getfixturevalue(fixture)

    matching_hetjobs = []
    mismatching_hetjobs = []
    het_jobs = []

    logging.info(
        f"Test scancel --ctld --{scancel_opt}={scancel_val}, sbatch --{sbatch_opt}, job match:{sbatch_match_val}, job mismatch:{sbatch_mismatch_val} for hetjobs"
    )

    # Submit hetjobs
    # There are two special cases to avoid:
    # - When canceling with --nodelist we may cancel first component and it will cancel the whole hetjob
    # - When canceling with --state we cannot distinguish between components
    if scancel_opt != "nodelist":
        submit_het_job(
            matching_hetjobs,
            sbatch_opt,
            sbatch_match_val,
            sbatch_match_val,
            match_job_state,
        )
        submit_het_job(
            mismatching_hetjobs,
            sbatch_opt,
            sbatch_mismatch_val,
            sbatch_mismatch_val,
            mismatch_job_state,
        )
    if scancel_opt != "state":
        submit_het_job(
            het_jobs, sbatch_opt, sbatch_match_val, sbatch_mismatch_val, match_job_state
        )

    # Waiting for hetjobs
    if scancel_opt != "nodelist":
        wait_for_jobs(matching_hetjobs, match_job_state, 60)
        wait_for_jobs(mismatching_hetjobs, mismatch_job_state, 60)
    if scancel_opt != "state":
        wait_for_jobs([het_jobs[1]], match_job_state, 60)
        wait_for_jobs([het_jobs[0]], mismatch_job_state, 60)

    # Just to show jobs in the logs
    atf.run_command_output(
        f"echo ''; squeue --Format=JobId,Account,Name,Nodelist,Partition,Qos,Reservation,State,UserName,WCKey"
    )

    # Cancel jobs with --scancel_opt=scancel:val
    run_scancel(scancel_opt, scancel_val, [])

    # Same for hetjobs
    if scancel_opt != "nodelist":
        wait_for_jobs(matching_hetjobs, "CANCELLED", 10)
        wait_for_jobs(mismatching_hetjobs, mismatch_job_state, 60)
    if scancel_opt != "state":
        wait_for_jobs([het_jobs[1]], "CANCELLED", 10)
        wait_for_jobs([het_jobs[0]], mismatch_job_state, 60)


def test_signal_job_ids():
    test_jobs = []
    other_jobs = []
    submissions = []

    logging.info("Test scancel --signal with --ctld")

    script = "--wrap 'srun sleep infinity'"
    job_opt = "-n1 -c1 --mem=10"

    het_job_submit = f"{job_opt} : {job_opt} : {job_opt}"
    array_submit1 = f"{job_opt} --array=1-3"
    array_submit2 = f"{job_opt} --array=1-10:5"

    submissions.append(f"{job_opt} {script}")
    submissions.append(f"{het_job_submit} {script}")
    submissions.append(f"{array_submit1} {script}")
    submissions.append(f"{array_submit2} {script}")

    for s in submissions:
        test_jobs.append(atf.submit_job_sbatch(s, fatal=True))
        other_jobs.append(atf.submit_job_sbatch(s, fatal=True))

    wait_for_jobs(test_jobs, "RUNNING", 35)
    wait_for_jobs(other_jobs, "RUNNING", 35)

    run_scancel("signal", "SIGSTOP", test_jobs)
    wait_for_jobs(test_jobs, "STOPPED", 35)
    wait_for_jobs(other_jobs, "RUNNING", 35)

    run_scancel("signal", "SIGCONT", test_jobs)
    wait_for_jobs(test_jobs, "RUNNING", 35)
    wait_for_jobs(other_jobs, "RUNNING", 35)

    # SIGTERM will make the job end in FAILED, not CANCELLED
    run_scancel("signal", "SIGTERM", test_jobs)
    wait_for_jobs(test_jobs, "FAILED", 35)
    wait_for_jobs(other_jobs, "RUNNING", 35)

    # SIGKILL will make the job end in CANCELLED
    run_scancel("signal", "SIGKILL", other_jobs)
    wait_for_jobs(other_jobs, "CANCELLED", 35)

    # Sanity check that there are no running jobs
    output = atf.run_command_output(f"squeue --noheader")
    assert len(output) == 0


# TODO: Test --user with another test user, and specific job ids.
# TODO: Test specific job ids for a mix of pending and running jobs, also in arrays:
#       (1) the whole array job is pending
#       (2) the array job is partially pending and partially running: --array=1-10%3
#       (3) the whole array job is running
#       Also with different arrays expressions:
#       (a) 10: the entire array
#       (b) 10_5: single job of an array
#       (b) 10_[1,4-7,8-10]: complex expression
