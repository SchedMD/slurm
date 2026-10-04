############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the billable usage sacct reports for a job (ticket 24176).

The Billing and BillingRaw fields of sacct and the billing_raw field its data
parser dumps report the raw, un-decayed billable usage of a job: the billing
TRES of the allocation multiplied by the elapsed seconds of the job. That is
the same quantity the multifactor priority plugin accumulates into the usage
of an association, so with decay turned off (PriorityDecayHalfLife=0) the
value sacct reports for a job has to be exactly the RawUsage that sshare adds
for the association of the user that submitted it.

The jobs run under an account created by the test so that the association
starts without usage and only the jobs of this test contribute to it, and
under a QOS created with a UsageFactor of 1, since the plugin scales the usage
it adds by the UsageFactor of the QOS of the job.
"""

import json
import os
from datetime import datetime, timedelta

import pytest

import atf

pytestmark = pytest.mark.slow

test_name = os.path.splitext(os.path.basename(__file__))[0]
acct_name = f"{test_name}_acct"
qos_name = f"{test_name}_qos"
part_name = f"{test_name}_billing_weights"
user_name = atf.properties["test-user"]

# Seconds the jobs sleep. Long enough to get an unambiguous non-zero usage,
# short enough to keep the test quick.
job_sleep = 10

# Billing weight of the partition the jobs run in. Kept integral so that the
# billing TRES of the allocation is not rounded, and above 1 so that it
# differs from the CPU count.
cpu_billing_weight = 2


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/sacct",
        reason="Ticket 24176: sacct Billing and BillingRaw added in 26.11+",
    )
    atf.require_accounting(modify=True)
    atf.require_config_parameter("PriorityType", "priority/multifactor")
    # Without decay the usage sshare reports is the raw billable usage of the
    # jobs. A reset period has to be given explicitly when the half-life is 0.
    atf.require_config_parameter("PriorityDecayHalfLife", ["00:00:00", "0"])
    atf.require_config_parameter("PriorityUsageResetPeriod", "NONE")
    # The usage of the last (and for short jobs, only) part of a job is added
    # when the job ends, which acct_policy_job_fini() only does when limits
    # are enforced.
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    # A requeued job is only eligible again after requeue_delay, which defaults
    # to the AuthInfo cred_expire (120 seconds), and is only started by the
    # next scheduling cycle. The (key, value) form replaces a value the site
    # config already sets; a "key=value" string would only append, and
    # slurmctld reads the first token for the key.
    atf.require_config_parameter_includes("SchedulerParameters", ("requeue_delay", 0))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def account(setup):
    """Add an association for the submitting user that has no usage yet"""

    # The plugin multiplies the usage it adds by the UsageFactor of the QOS of
    # the job, which BillingRaw leaves out. Give the association its own QOS
    # with a UsageFactor of 1 instead of relying on the default QOS of the
    # cluster, so that the usage sshare adds is the one sacct reports.
    atf.run_command(
        f"sacctmgr -i add qos {qos_name} UsageFactor=1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user_name} account={acct_name} "
        f"qos={qos_name} DefaultQOS={qos_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # sacctmgr commits to slurmdbd, which sends the association on to
    # slurmctld asynchronously. sshare and the job submissions below read it
    # from slurmctld, so wait for it to arrive before a test uses the account.
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(
            f"sshare -n -P -U -u {user_name} -A {acct_name} -o User",
            fatal=True,
            quiet=True,
        )
        if user_name in [line.strip() for line in output.splitlines()]:
            break

    yield acct_name

    atf.run_command(
        f"sacctmgr -i remove user {user_name} where account={acct_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {acct_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove qos {qos_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def _sshare_raw_usage(account):
    """Return the RawUsage sshare reports for the submitting user in account"""

    output = atf.run_command_output(
        f"sshare -n -P -U -u {user_name} -A {account} -o User,RawUsage",
        fatal=True,
        quiet=True,
    )

    usages = []
    for line in output.splitlines():
        fields = [field.strip() for field in line.split("|")]
        if len(fields) == 2 and fields[0] == user_name:
            assert atf.is_integer(
                fields[1]
            ), f"sshare should report a numeric RawUsage, got {fields[1]!r}"
            usages.append(int(fields[1]))

    assert len(usages) == 1, (
        f"sshare should report one association for user {user_name} in account "
        f"{account}, got {output!r}"
    )

    return usages[0]


def _wait_for_raw_usage(account, usage_before, expected_usage):
    """Return the RawUsage of the association once the job usage was added

    The priority plugin adds the usage of a job in chunks (one per
    PriorityCalcPeriod while it runs plus the remainder when it ends), so wait
    for the usage to reach the expected one instead of taking the first change
    seen. The last value read is returned when it never does, letting the
    caller report how much usage was added instead of failing the timer.
    """

    usage = usage_before
    for _ in atf.timer():
        usage = _sshare_raw_usage(account)
        if usage - usage_before >= expected_usage:
            break

    return usage


def _wait_completed(job_id, runs=1):
    """Wait for a job that sleeps once per run to complete"""

    # The job sleeps to accrue usage, so it needs that long on top of the
    # usual time to be scheduled and to complete.
    atf.wait_for_job_state(
        job_id,
        "COMPLETED",
        fatal=True,
        timeout=runs * job_sleep + atf.default_polling_timeout,
    )


def _submit_and_run(sbatch_args):
    """Submit a sleeping job and return its id once it ran for a second"""

    job_id = atf.submit_job_sbatch(
        f'{sbatch_args} --wrap "sleep {job_sleep}"', fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    # Only a run of at least a second gives its record a billable usage. Poll
    # every second instead of at the default rate, a tenth of the timeout, so
    # that the caller still has most of the run left to act on the job.
    for _ in atf.timer(poll_interval=1, fatal=True):
        run_time = atf.get_job_parameter(job_id, "RunTime", default="", quiet=True)
        if run_time not in ("", "00:00:00"):
            break

    return job_id


def _submit_and_wait(sbatch_args):
    """Submit a sleeping job and return its id once it completed"""

    job_id = atf.submit_job_sbatch(
        f'{sbatch_args} --wrap "sleep {job_sleep}"', fatal=True
    )
    _wait_completed(job_id)

    return job_id


def _sacct_job_records(job_id, fields, options=""):
    """Return the sacct values of fields for every record of the job

    A requeued job is accounted as one record per run, all of them reported
    by sacct with --duplicates. options are added to the sacct command line.
    """

    atf.wait_for_job_accounted(job_id, "State", "COMPLETED", fatal=True)
    output = atf.run_command_output(
        f"sacct -j {job_id} -X -D -n -P {options} -o {','.join(fields)}",
        fatal=True,
    )

    records = []
    for line in output.splitlines():
        values = [value.strip() for value in line.split("|")]
        assert len(values) == len(
            fields
        ), f"sacct should report {fields} for job {job_id}, got {line!r}"
        records.append(dict(zip(fields, values)))

    return records


def _sacct_job_fields(job_id, fields, options=""):
    """Return a dict with the sacct values of fields for the job (not steps)"""

    records = _sacct_job_records(job_id, fields, options)

    assert (
        len(records) == 1
    ), f"sacct should report one record for job {job_id}, got {len(records)}"

    return records[0]


def _sacct_job_json(job_id):
    """Return the entry the sacct data parser dumps for the job"""

    atf.wait_for_job_accounted(job_id, "State", "COMPLETED", fatal=True)
    output = atf.run_command_output(f"sacct -j {job_id} --json", fatal=True)
    jobs = [job for job in json.loads(output)["jobs"] if job["job_id"] == job_id]

    assert (
        len(jobs) == 1
    ), f"sacct --json should dump one entry for job {job_id}, got {len(jobs)}"

    return jobs[0]


def _tres_count(tres_string, tres_name):
    """Return the count of tres_name in a comma separated TRES string"""

    for tres in tres_string.split(","):
        name, _, count = tres.partition("=")
        if name == tres_name:
            return int(count)

    pytest.fail(f"No {tres_name} TRES found in {tres_string!r}")


def _json_tres_count(tres_list, tres_name):
    """Return the count of tres_name in a list of TRES dumped by the parser"""

    for tres in tres_list:
        if tres["type"] == tres_name:
            return tres["count"]

    pytest.fail(f"No {tres_name} TRES found in {tres_list}")


@pytest.fixture(scope="module")
def billing_weights_partition(setup):
    """Create a partition that bills each allocated CPU more than once

    Every job runs in it so that its billing TRES differs from its CPU count
    and does not depend on the TRESBillingWeights of the default partition.
    """

    atf.run_command(
        f"scontrol create PartitionName={part_name} Nodes=ALL "
        f"TRESBillingWeights=CPU={cpu_billing_weight}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield part_name

    atf.run_command(
        f"scontrol delete PartitionName={part_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="module")
def completed_job(account, billing_weights_partition):
    """Run a job and return it with the usage of the account before it ran"""

    usage_before = _sshare_raw_usage(account)
    job_id = _submit_and_wait(f"--account={account} -p {billing_weights_partition}")

    alloc_tres = _sacct_job_fields(job_id, ["AllocTRES"])["AllocTRES"]
    billing = _tres_count(alloc_tres, "billing")
    cpus = _tres_count(alloc_tres, "cpu")
    assert billing == cpu_billing_weight * cpus, (
        f"TRESBillingWeights=CPU={cpu_billing_weight} should bill job {job_id} "
        f"{cpu_billing_weight} per allocated CPU, got {alloc_tres}"
    )

    return job_id, usage_before


def test_billing_matches_sshare_raw_usage(account, completed_job):
    """Verify BillingRaw and the dumped billing_raw are the RawUsage sshare adds"""

    job_id, usage_before = completed_job
    billing_raw = int(_sacct_job_fields(job_id, ["BillingRaw"])["BillingRaw"])
    dumped = _sacct_job_json(job_id)["billing_raw"]

    assert (
        billing_raw > 0
    ), f"sacct should report a non-zero BillingRaw for job {job_id}"

    usage_after = _wait_for_raw_usage(account, usage_before, billing_raw)
    added = usage_after - usage_before

    assert added == billing_raw, (
        f"The sacct BillingRaw of job {job_id} ({billing_raw}) should be "
        f"the RawUsage sshare added for user {user_name} "
        f"({usage_after} - {usage_before} = {added})"
    )
    assert added == dumped, (
        f"The dumped billing_raw of job {job_id} ({dumped}) should be "
        f"the RawUsage sshare added for user {user_name} "
        f"({usage_after} - {usage_before} = {added})"
    )


def test_billing_matches_billing_tres_and_elapsed(completed_job):
    """Verify BillingRaw is the billing TRES times the elapsed seconds"""

    job_id, _ = completed_job
    values = _sacct_job_fields(job_id, ["AllocTRES", "ElapsedRaw", "BillingRaw"])
    expected = _tres_count(values["AllocTRES"], "billing") * int(values["ElapsedRaw"])

    assert int(values["BillingRaw"]) == expected, (
        f"The sacct BillingRaw of job {job_id} should be the billing TRES "
        f"of {values['AllocTRES']} times {values['ElapsedRaw']} elapsed seconds "
        f"({expected}), got {values['BillingRaw']}"
    )


def test_billing_is_truncated_to_the_time_window(completed_job):
    """Verify -T narrows BillingRaw to the part of the job in the time window"""

    job_id, _ = completed_job
    full = _sacct_job_fields(job_id, ["Start", "ElapsedRaw", "BillingRaw"])

    # Open the window half way through the run so that -T cuts off the first
    # half. sacct prints and parses times in local time, so shift the Start it
    # reports instead of building a time of our own.
    window_start = datetime.fromisoformat(full["Start"]) + timedelta(
        seconds=job_sleep // 2
    )
    truncated = _sacct_job_fields(
        job_id,
        ["AllocTRES", "ElapsedRaw", "BillingRaw"],
        f"-T -S {window_start.isoformat()}",
    )
    expected = _tres_count(truncated["AllocTRES"], "billing") * int(
        truncated["ElapsedRaw"]
    )

    assert int(truncated["ElapsedRaw"]) == int(full["ElapsedRaw"]) - job_sleep // 2, (
        f"-T -S {window_start.isoformat()} should cut the first "
        f"{job_sleep // 2} of the {full['ElapsedRaw']} elapsed seconds of job "
        f"{job_id}, got {truncated['ElapsedRaw']}"
    )
    assert int(truncated["BillingRaw"]) == expected, (
        f"The truncated sacct BillingRaw of job {job_id} should be the billing "
        f"TRES of {truncated['AllocTRES']} times its {truncated['ElapsedRaw']} "
        f"truncated elapsed seconds ({expected}), got {truncated['BillingRaw']}"
    )
    assert int(truncated["BillingRaw"]) < int(full["BillingRaw"]), (
        f"-T -S {window_start.isoformat()} should narrow the sacct BillingRaw "
        f"of job {job_id} below its untruncated {full['BillingRaw']}, got "
        f"{truncated['BillingRaw']}"
    )


def test_json_billing_matches_billing_tres_and_elapsed(completed_job):
    """Verify the dumped billing_raw usage is its billing TRES times its elapsed"""

    job_id, _ = completed_job
    job = _sacct_job_json(job_id)
    billing = _json_tres_count(job["tres"]["allocated"], "billing")
    expected = billing * job["time"]["elapsed"]

    assert job["billing_raw"] == expected, (
        f"The billing_raw sacct --json dumps for job {job_id} should be "
        f"its {billing} billing TRES times its {job['time']['elapsed']} elapsed "
        f"seconds ({expected}), got {job['billing_raw']}"
    )


def _assert_records_match_sshare_raw_usage(account, job_id, records, usage_before):
    """Assert the records of a job add up to the RawUsage sshare added"""

    billing_raws = [int(record["BillingRaw"]) for record in records]

    assert all(billing_raw > 0 for billing_raw in billing_raws), (
        f"Every record of job {job_id} should have a non-zero BillingRaw, "
        f"got {billing_raws}"
    )

    total = sum(billing_raws)
    usage_after = _wait_for_raw_usage(account, usage_before, total)
    added = " + ".join(str(billing_raw) for billing_raw in billing_raws)

    assert usage_after - usage_before == total, (
        f"The BillingRaw of the records of job {job_id} ({added} = {total}) "
        f"should be the RawUsage sshare added for user {user_name} "
        f"({usage_after} - {usage_before} = {usage_after - usage_before})"
    )


@pytest.fixture(scope="module")
def requeued_job(account, billing_weights_partition):
    """Requeue a running job, returning the usage of the account before it"""

    usage_before = _sshare_raw_usage(account)
    job_id = _submit_and_run(
        f"--account={account} --requeue -p {billing_weights_partition}"
    )

    atf.run_command(
        f"scontrol requeue {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    _wait_completed(job_id, runs=2)

    return job_id, usage_before


def test_requeued_job_billing_matches_sshare_raw_usage(account, requeued_job):
    """Verify both runs of a requeued job add their billable usage to RawUsage"""

    job_id, usage_before = requeued_job
    records = _sacct_job_records(job_id, ["BillingRaw"])

    assert len(records) == 2, (
        f"sacct should report the run before and the run after the requeue of "
        f"job {job_id}, got {len(records)} record(s)"
    )

    _assert_records_match_sshare_raw_usage(account, job_id, records, usage_before)
