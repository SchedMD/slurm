############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Tests for TRESRunMins usage when the QOS UsageFactor is modified mid-run."""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

# Global variables
account = "acct1"
qos1 = "qos1"
test_user = atf.properties["test-user"]

job_time_limit = 10

# Non-binding GrpJobs value, only used to observe the QOS update landing.
grp_jobs_marker = 1000


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
    # The exact-value assertions need the decay thread held off between
    # snapshots, and PriorityCalcPeriod only exists under multifactor.
    atf.require_config_parameter("PriorityType", "priority/multifactor")
    atf.require_config_parameter("PriorityCalcPeriod", 9999)
    atf.require_nodes(2, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    atf.require_config_parameter(
        "PartitionName",
        {"part1": {"Nodes": "ALL", "Default": "YES", "MaxTime": "INFINITE"}},
    )
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db():
    atf.run_command(
        f"sacctmgr -i add qos {qos1} set usagefactor=1",
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

    yield

    atf.cancel_all_jobs(quiet=True)
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


def used_run_mins():
    """Return the non-zero GrpTRESRunMins usage of the test QOS."""

    output = atf.run_command_output(
        f"scontrol show assoc_mgr qos={qos1} flags=qos",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    usage = {}
    for line in output.splitlines():
        line = line.strip()
        if not line.startswith("GrpTRESRunMins="):
            continue
        for tres, used in re.findall(r"([\w/:]+)=[^=,]*?\((\d+)\)", line):
            usage[tres] = max(usage.get(tres, 0), int(used))
    return {tres: used for tres, used in usage.items() if used}


def qos_grp_jobs():
    """Return the GrpJobs limit of the test QOS as slurmctld has it cached."""

    output = atf.run_command_output(
        f"scontrol show assoc_mgr qos={qos1} flags=qos",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    match = re.search(r"GrpJobs=(\d+)", output)
    return int(match.group(1)) if match else None


def set_usage_factor(usage_factor, marker):
    """Set the QOS UsageFactor and wait for slurmctld to have the new value.

    GrpJobs travels in the same update record and, unlike the factor, is
    reported by scontrol, so it is what tells us the record landed. Booking or
    giving back before it does would use the old factor, which is what the
    unfixed code does anyway, so the test would pass without the fix.
    """

    atf.run_command(
        f"sacctmgr -i modify qos {qos1} set usagefactor={usage_factor} "
        f"grpjobs={marker}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cached_marker = None
    for _ in atf.timer(timeout=60, poll_interval=2):
        cached_marker = qos_grp_jobs()
        if cached_marker == marker:
            break
    assert cached_marker == marker, "slurmctld should have picked up the modified QOS"

    # GrpJobs arriving only proves the update record landed, not that the
    # factor in it was applied.
    stored_factor = atf.run_command_output(
        f"sacctmgr -n show qos {qos1} format=UsageFactor",
        user=atf.properties["slurm-user"],
        fatal=True,
    ).strip()
    assert float(stored_factor) == usage_factor, (
        f"The QOS UsageFactor should be {usage_factor} after the modify, but "
        f"sacctmgr reports {stored_factor}"
    )


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


@pytest.mark.parametrize("start_factor,new_factor", [(1, 2), (2, 1)])
def test_usage_factor_modified_while_running(start_factor, new_factor):
    """Verify usage is given back with the factor it was booked at.

    TRESRunMins usage is booked when a job starts, scaled by the QOS
    UsageFactor in effect at that moment. If the QOS is modified while the
    job runs, the usage still has to be given back with the original factor.

    Raising the factor makes an unfixed give-back remove more than was ever
    added, and since the counter is shared, the excess is taken from the jobs
    that are still running. Lowering it makes the give-back remove too little,
    which leaks and is only visible once every job has ended.
    """

    set_usage_factor(start_factor, grp_jobs_marker)

    job1_id = submit_and_wait()
    booked1 = booked_cpu_mins(job1_id, start_factor)
    one_job_usage = used_run_mins()
    assert one_job_usage.get("cpu") == booked1, (
        f"Starting a job should have booked {booked1} cpu TRESRunMins, "
        f"but {one_job_usage.get('cpu')} is booked"
    )

    job2_id = submit_and_wait()
    booked2 = booked_cpu_mins(job2_id, start_factor)
    two_job_usage = used_run_mins()
    assert two_job_usage.get("cpu") == booked1 + booked2, (
        f"Starting a second job should have added {booked2} cpu TRESRunMins "
        f"to {booked1}, but {two_job_usage.get('cpu')} is booked"
    )

    # Change the factor out from under both running jobs.
    set_usage_factor(new_factor, grp_jobs_marker + 1)

    # End only the second job. Its usage was booked at the old factor, so
    # exactly its own contribution should come back off, leaving the first
    # job's usage untouched.
    atf.cancel_jobs([job2_id], fatal=True)
    for _ in atf.timer(timeout=60, poll_interval=2, fatal=True):
        if used_run_mins() != two_job_usage:
            break

    remaining_usage = used_run_mins()
    assert remaining_usage == one_job_usage, (
        f"The still-running job's usage should be {one_job_usage} after the "
        f"other job ended, but it is {remaining_usage}"
    )

    # Nothing is left running to measure against, so a give-back that removed
    # too little only shows up here.
    atf.cancel_jobs([job1_id], fatal=True)
    remaining = None
    for _ in atf.timer(timeout=60, poll_interval=2):
        remaining = used_run_mins()
        if not remaining:
            break
    assert not remaining, (
        "All usage should have been given back once every job ended, "
        f"but {remaining} is still booked"
    )


def test_usage_factor_rebooked_on_reconfigure():
    """Verify a reconfigure rebooks running jobs at the current UsageFactor.

    Modifying the factor leaves running jobs alone, but a reconfigure clears
    the booked usage and books every running job again, so from that point the
    job counts at the factor in effect then rather than the one it started
    with.
    """

    set_usage_factor(1, grp_jobs_marker)

    job_id = submit_and_wait()
    booked_at_start = booked_cpu_mins(job_id, 1)
    usage = used_run_mins()
    assert usage.get("cpu") == booked_at_start, (
        f"Starting the job should have booked {booked_at_start} cpu "
        f"TRESRunMins, but {usage.get('cpu')} is booked"
    )

    set_usage_factor(2, grp_jobs_marker + 1)
    usage = used_run_mins()
    assert usage.get("cpu") == booked_at_start, (
        f"Modifying the UsageFactor should have left the running job at "
        f"{booked_at_start} cpu TRESRunMins, but {usage.get('cpu')} is booked"
    )

    atf.run_command(
        "scontrol reconfigure",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Rebooking charges the time the job has left, not its whole limit, so the
    # scaled figure is bounded by the factor rather than equal to it.
    rebooked = booked_cpu_mins(job_id, 2)
    reconfigured = None
    for _ in atf.timer(timeout=60, poll_interval=2):
        reconfigured = used_run_mins().get("cpu")
        if reconfigured is not None and reconfigured > booked_at_start:
            break
    assert booked_at_start < reconfigured <= rebooked, (
        f"A reconfigure should have rebooked the running job above "
        f"{booked_at_start} and at most {rebooked} cpu TRESRunMins, but "
        f"{reconfigured} is booked"
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
