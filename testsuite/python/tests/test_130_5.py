############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Tests for TRESRunMins give-back with priority/basic and a QOS UsageFactor."""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

# Global variables
account = "acct1"
qos1 = "qos1"
test_user = atf.properties["test-user"]

job_time_limit = 10


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Test setup with required configurations."""
    atf.require_version(
        (26, 5, 5),
        "sbin/slurmctld",
        reason="Ticket 25339 TRESRunMins give-back at the booked QOS "
        "UsageFactor is fixed in 26.05.5",
    )
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    # priority/basic gives the usage back itself, in priority_p_job_end().
    atf.require_config_parameter("PriorityType", "priority/basic")
    atf.require_nodes(2, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    atf.require_config_parameter(
        "PartitionName",
        {"part1": {"Nodes": "ALL", "Default": "YES", "MaxTime": "INFINITE"}},
    )
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db():
    yield

    atf.cancel_all_jobs(quiet=True)
    atf.run_command(
        "scontrol update partitionname=part1 qos=",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove user {test_user} where account={account}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {account}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove qos {qos1}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def create_qos(usage_factor):
    """Create the test QOS with the given UsageFactor and grant it to the user.

    The factor is set when the QOS is created, so that every job of this test
    is booked with it and no mid-run QOS update is involved.
    """

    atf.run_command(
        f"sacctmgr -i add qos {qos1} set usagefactor={usage_factor}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {account}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {test_user} account={account} qos=normal,{qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    cached = False
    for _ in atf.timer(timeout=60, poll_interval=2):
        cached = qos_reported()
        if cached:
            break
    assert cached, f"slurmctld should have picked up QOS {qos1}"

    # Reaching the cache does not prove the factor in the record was applied.
    stored_factor = atf.run_command_output(
        f"sacctmgr -n show qos {qos1} format=UsageFactor",
        user=atf.properties["slurm-user"],
        fatal=True,
    ).strip()
    assert float(stored_factor) == usage_factor, (
        f"The QOS UsageFactor should be {usage_factor}, but sacctmgr reports "
        f"{stored_factor}"
    )


def qos_reported():
    """Return whether slurmctld reports a GrpTRESRunMins line for the QOS."""

    output = atf.run_command_output(
        f"scontrol show assoc_mgr qos={qos1} flags=qos",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    return any(
        line.strip().startswith("GrpTRESRunMins=") for line in output.splitlines()
    )


def used_run_mins():
    """Return the non-zero GrpTRESRunMins usage of the test QOS."""

    output = atf.run_command_output(
        f"scontrol show assoc_mgr qos={qos1} flags=qos",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    usage = {}
    reported = False
    for line in output.splitlines():
        line = line.strip()
        if not line.startswith("GrpTRESRunMins="):
            continue
        reported = True
        for tres, used in re.findall(r"([\w/:]+)=[^=,]*?\((\d+)\)", line):
            usage[tres] = max(usage.get(tres, 0), int(used))
    assert reported, (
        f"slurmctld reports no GrpTRESRunMins for QOS {qos1}; an empty reading "
        "would satisfy this file's negative assertions without measuring"
    )
    return {tres: used for tres, used in usage.items() if used}


def booked_cpu_mins(job_id, usage_factor):
    """Return the cpu TRESRunMins that starting this job should have booked.

    Truncation happens twice, once when the scaled time limit is reduced to
    whole seconds and once when scontrol reports minutes.
    """

    num_cpus = int(atf.get_job_parameter(job_id, "NumCPUs", quiet=True))
    time_limit_secs = int(job_time_limit * 60 * usage_factor)
    return (num_cpus * time_limit_secs) // 60


def submit_and_wait():
    job_id = atf.submit_job_sbatch(
        f"-N1 --exclusive -t{job_time_limit} -q {qos1} -A {account} "
        f"--wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True
    ), f"Job {job_id} should be running"
    return job_id


def test_usage_factor_above_one_given_back():
    """Verify all usage is given back for a QOS with UsageFactor > 1.

    Usage is booked scaled by the UsageFactor, so it has to be given back
    scaled by it too. Giving it back unscaled leaves part of the reservation
    booked forever, and it accumulates with every job that runs under the QOS.
    """

    create_qos(2)

    assert not used_run_mins(), "No usage should be tracked before a job starts"

    job_id = submit_and_wait()
    booked = booked_cpu_mins(job_id, 2)
    usage = used_run_mins()
    assert usage.get("cpu") == booked, (
        f"Starting the job should have booked {booked} cpu TRESRunMins, "
        f"but {usage.get('cpu')} is booked"
    )

    atf.cancel_jobs([job_id], fatal=True)
    remaining = None
    for _ in atf.timer(timeout=60, poll_interval=2):
        remaining = used_run_mins()
        if not remaining:
            break
    assert not remaining, (
        "All usage should have been given back after the job ended, "
        f"but {remaining} is still booked"
    )


def test_usage_factor_zero_books_nothing():
    """Verify a QOS with UsageFactor 0 books no TRESRunMins at all.

    A factor of 0 is documented to add no timed usage from the job, so nothing
    is booked to give back either, and a give-back that removes anything would
    be taking usage that belongs to whatever else is running.
    """

    create_qos(0)

    job1_id = submit_and_wait()
    assert not used_run_mins(), (
        "A UsageFactor of 0 should add no timed usage, but "
        f"{used_run_mins()} is booked while the job runs"
    )

    job2_id = submit_and_wait()
    assert not used_run_mins(), (
        "A UsageFactor of 0 should add no timed usage, but "
        f"{used_run_mins()} is booked while two jobs run"
    )

    atf.cancel_jobs([job2_id], fatal=True)
    assert not used_run_mins(), (
        "Ending a job booked at a UsageFactor of 0 should leave the counters "
        f"alone, but {used_run_mins()} is booked with a job still running"
    )

    atf.cancel_jobs([job1_id], fatal=True)
    assert not used_run_mins(), (
        f"No usage should be booked once every job ended, but "
        f"{used_run_mins()} is still booked"
    )


def test_usage_factor_below_one_leaves_running_usage():
    """Verify a QOS with UsageFactor < 1 does not over-remove on job end.

    Giving back unscaled usage removes more than the job ever booked, and
    since the counter is shared by the whole QOS, the excess is taken from
    the jobs that are still running until the underflow clamp zeroes it.
    """

    create_qos(0.5)

    job1_id = submit_and_wait()
    booked1 = booked_cpu_mins(job1_id, 0.5)
    one_job_usage = used_run_mins()
    assert one_job_usage.get("cpu") == booked1, (
        f"Starting a job should have booked {booked1} cpu TRESRunMins, "
        f"but {one_job_usage.get('cpu')} is booked"
    )

    job2_id = submit_and_wait()
    booked2 = booked_cpu_mins(job2_id, 0.5)
    two_job_usage = used_run_mins()
    assert two_job_usage.get("cpu") == booked1 + booked2, (
        f"Starting a second job should have added {booked2} cpu TRESRunMins "
        f"to {booked1}, but {two_job_usage.get('cpu')} is booked"
    )

    # End only the second job. Exactly its own contribution should come back
    # off, leaving the first job's usage untouched.
    atf.cancel_jobs([job2_id], fatal=True)
    for _ in atf.timer(timeout=60, poll_interval=2, fatal=True):
        if used_run_mins() != two_job_usage:
            break

    remaining_usage = used_run_mins()
    assert remaining_usage == one_job_usage, (
        f"The still-running job's usage should be {one_job_usage} after the "
        f"other job ended, but it is {remaining_usage}"
    )


def test_partition_qos_ignores_usage_factor():
    """Verify a partition QOS books TRESRunMins at the job QOS's UsageFactor.

    The usage factor only applies to the job's QOS and not the partition QOS,
    so a job whose own QOS is normal books unscaled minutes even when the
    partition QOS carries a factor of 2. The give-back has to match, which is
    the pairing of the two priority/basic fixes: one chooses the factor, the
    other adds the partition QOS to the records usage is returned to.
    """

    create_qos(2)

    atf.run_command(
        f"scontrol update partitionname=part1 qos={qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    job_id = atf.submit_job_sbatch(
        f"-N1 --exclusive -t{job_time_limit} -A {account} --wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True
    ), f"Job {job_id} should be running"
    assert atf.get_job_parameter(job_id, "QOS") == "normal", (
        f"The job must hold normal as its job QOS so that {qos1} is reached "
        f"only as the partition QOS"
    )

    unscaled = booked_cpu_mins(job_id, 1)
    usage = used_run_mins()
    assert usage.get("cpu") == unscaled, (
        f"The partition QOS should have booked {unscaled} cpu TRESRunMins, "
        f"unscaled by its own UsageFactor, but {usage.get('cpu')} is booked"
    )

    atf.cancel_jobs([job_id], fatal=True)
    remaining = None
    for _ in atf.timer(timeout=60, poll_interval=2):
        remaining = used_run_mins()
        if not remaining:
            break
    assert not remaining, (
        "All usage should have been given back on the partition QOS after the "
        f"job ended, but {remaining} is still booked"
    )
