############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""test_130_13.py - ResetAccrueTime and accrue limits across restarts.

ResetAccrueTime sets the AccrueTime of a pending job to now, so it loses its
accrued age. Accrue limits and times survive a reconfigure and a restart.
test_130_13 covers the accrue limits themselves.

A job accrues when squeue shows its AccrueTime. The scontrol show assoc_mgr
count is also checked where possible.

Ticket 13223: ResetAccrueTime was undone at the next priority recalculation.
"""

import re
import time

import pytest

import atf

pytestmark = pytest.mark.slow

test_user = atf.properties["test-user"]

# LIMIT jobs may accrue. Each case submits one more job per account.
LIMIT = 2
JOBS_PER_ACCOUNT = LIMIT + 1

acct_free = "acct_free_130_13"
acct_free2 = "acct_free2_130_13"
acct_grp = "acct_grp_130_13"
acct_max = "acct_max_130_13"
acct_parent = "acct_parent_130_13"
acct_children = ["acct_child1_130_13", "acct_child2_130_13"]
acct_newlimit = "acct_newlimit_130_13"

qos_free = "qos_free_130_13"
qos_grp = "qos_grp_130_13"
qos_pu = "qos_pu_130_13"
qos_pa = "qos_pa_130_13"

# The weighted age grows by one point per second of accrual.
AGE_WEIGHT = 3600
MAX_AGE_MINUTES = 60
CALC_PERIOD_MINUTES = 1
CALC_PERIOD = CALC_PERIOD_MINUTES * 60
AGE_GAP = 10
# A priority recalculation can take a full PriorityCalcPeriod.
PRIORITY_TIMEOUT = 3 * CALC_PERIOD

# The unset AccrueTime value is not documented: no timestamp means unset.
ACCRUE_TIME = re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}")

GRP_ASSOC_COUNT = (f"accounts={acct_grp} flags=assoc", "GrpJobsAccrue")
GRP_QOS_COUNT = (f"qos={qos_grp} flags=qos", "GrpJobsAccrue")

# Each case: accounts, QOS, accruing jobs, accruing jobs in each account when
# the limit is per account, and the assoc_mgr filter and label.
# The per-user and per-account QOS cases only check AccrueTime. QOS cases use
# two accounts: PerAccount allows LIMIT jobs in each one.
LIMITS = [
    ([acct_grp], qos_free, LIMIT, None, GRP_ASSOC_COUNT),
    (
        [acct_max],
        qos_free,
        LIMIT,
        None,
        (f"accounts={acct_max} users={test_user} flags=assoc", "MaxJobsAccrue"),
    ),
    (
        acct_children,
        qos_free,
        LIMIT,
        None,
        (f"accounts={acct_parent} flags=assoc", "GrpJobsAccrue"),
    ),
    ([acct_free, acct_free2], qos_grp, LIMIT, None, GRP_QOS_COUNT),
    ([acct_free, acct_free2], qos_pu, LIMIT, None, None),
    ([acct_free, acct_free2], qos_pa, 2 * LIMIT, LIMIT, None),
]
LIMIT_IDS = [
    "assoc_GrpJobsAccrue",
    "assoc_MaxJobsAccrue",
    "parent_GrpJobsAccrue",
    "qos_GrpJobsAccrue",
    "qos_MaxJobsAccruePerUser",
    "qos_MaxJobsAccruePerAccount",
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    atf.require_config_parameter("PriorityType", "priority/multifactor")
    atf.require_config_parameter_excludes("PriorityFlags", "ACCRUE_ALWAYS")
    atf.require_config_parameter("PriorityWeightAge", AGE_WEIGHT)
    atf.require_config_parameter("PriorityMaxAge", MAX_AGE_MINUTES)
    atf.require_config_parameter("PriorityCalcPeriod", CALC_PERIOD_MINUTES)
    atf.require_slurm_running()

    su = atf.properties["slurm-user"]
    qos_limits = {
        qos_free: "",
        qos_grp: f"GrpJobsAccrue={LIMIT}",
        qos_pu: f"MaxJobsAccruePerUser={LIMIT}",
        qos_pa: f"MaxJobsAccruePerAccount={LIMIT}",
    }
    for qos, limits in qos_limits.items():
        atf.run_command(f"sacctmgr -i add qos {qos} {limits}", user=su, fatal=True)

    atf.run_command(
        f"sacctmgr -i add account {acct_free},{acct_free2},{acct_max},"
        f"{acct_newlimit}",
        user=su,
        fatal=True,
    )
    for account in (acct_grp, acct_parent):
        atf.run_command(
            f"sacctmgr -i add account {account} GrpJobsAccrue={LIMIT}",
            user=su,
            fatal=True,
        )
    atf.run_command(
        f"sacctmgr -i add account {','.join(acct_children)} parent={acct_parent}",
        user=su,
        fatal=True,
    )

    accounts = ",".join(
        [acct_free, acct_free2, acct_grp, acct_max, acct_newlimit, *acct_children]
    )
    qos_list = ",".join([qos_free, qos_grp, qos_pu, qos_pa])
    atf.run_command(
        f"sacctmgr -i add user {test_user} account={accounts} qos={qos_list}",
        user=su,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i modify user where name={test_user} account={acct_max} "
        f"set MaxJobsAccrue={LIMIT}",
        user=su,
        fatal=True,
    )

    # Load the new accounts, QOS and limits before any job.
    atf.restart_slurmctld()


@pytest.fixture
def blocker():
    """Keep one node busy. Returns the job ID and the node name."""
    job_id = atf.submit_job_sbatch(
        f"-A {acct_free} --qos={qos_free} -N1 --exclusive --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    return job_id, atf.get_job_parameter(job_id, "NodeList", fatal=True)


def submit(node, account, qos, options=""):
    """Submit a job that stays pending on node."""
    return atf.submit_job_sbatch(
        f"-A {account} --qos={qos} -w {node} --exclusive {options} "
        "--wrap 'sleep infinity'",
        fatal=True,
    )


def submit_jobs(node, accounts, qos, array=False):
    """Submit JOBS_PER_ACCOUNT pending jobs, or array tasks, in each account."""
    job_ids = []
    for account in accounts:
        if array:
            options = f"--array=0-{JOBS_PER_ACCOUNT - 1}"
            job_ids.append(submit(node, account, qos, options))
        else:
            job_ids += [submit(node, account, qos) for _ in range(JOBS_PER_ACCOUNT)]
    return job_ids


def accrue_times(job_ids):
    """Return the AccrueTime of each pending job or array task.

    Array tasks use the "<array job ID>_<task ID>" key.
    """
    ids = {str(job_id) for job_id in job_ids}
    output = atf.run_command_output(
        f"squeue -h -r -t PD -u {test_user} -O ArrayJobID,ArrayTaskID,AccrueTime",
        fatal=True,
    )

    times = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 2 or fields[0] not in ids:
            continue
        key = f"{fields[0]}_{fields[1]}" if fields[1].isdigit() else fields[0]
        times[key] = fields[2] if len(fields) > 2 else ""
    return times


def accruing(job_ids):
    """Return the AccrueTime of the pending jobs in job_ids that accrue."""
    return {
        key: value
        for key, value in accrue_times(job_ids).items()
        if ACCRUE_TIME.fullmatch(value)
    }


def assert_accruing_per_account(job_ids, accounts, expected):
    """Check that expected pending jobs in job_ids accrue in each account."""
    ids = {str(job_id) for job_id in job_ids}
    output = atf.run_command_output(
        f"squeue -h -r -t PD -u {test_user} -O ArrayJobID,Account,AccrueTime",
        fatal=True,
    )

    counts = {account: 0 for account in accounts}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0] in ids and ACCRUE_TIME.fullmatch(fields[2]):
            counts[fields[1]] = counts.get(fields[1], 0) + 1
    assert counts == {account: expected for account in accounts}, (
        f"{expected} pending jobs should accrue in each account, but the "
        f"accruing jobs per account are {counts}"
    )


def wait_for_marker(node):
    """Wait until a new job with no accrue limit accrues.

    Then the controller has also checked all earlier jobs.
    """
    marker = submit(node, acct_free, qos_free)
    for _ in atf.timer(fatal=True):
        if accruing([marker]):
            break


def assert_accrue_count(filters, label, expected=LIMIT):
    """Check the used count of "label=LIMIT(used)" in assoc_mgr."""
    output = atf.run_command_output(
        f"scontrol show assoc_mgr {filters}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    match = re.search(rf"\b{label}={LIMIT}\((\d+)\)", output)
    count = int(match.group(1)) if match else None
    assert count == expected, (
        f"scontrol show assoc_mgr {filters} should show {label}={LIMIT}"
        f"({expected}), but the count is {count}"
    )


def accrue_epoch(value):
    """Return an AccrueTime timestamp as seconds since the epoch."""
    return time.mktime(time.strptime(value, "%Y-%m-%dT%H:%M:%S"))


def ages(job_ids):
    """Return the sprio weighted age of each job."""
    ids = {str(job_id) for job_id in job_ids}
    # sprio -j with several jobs fails before 25.11.3 (Ticket 24625).
    if len(ids) > 1 and atf.get_version("bin/sprio") < (25, 11, 3):
        jobs = f"-u {test_user}"
    else:
        jobs = f"-j {','.join(map(str, job_ids))}"
    output = atf.run_command_output(f"sprio -h {jobs} -o '%i %A'", fatal=True)

    job_ages = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 2 and fields[0] in ids:
            job_ages[int(fields[0])] = int(float(fields[1]))
    return job_ages


def test_reset_accrue_time(blocker):
    """Verify ResetAccrueTime makes a job lose its accrued age.

    The new AccrueTime must survive the next priority recalculation (Ticket
    13223).
    """
    _, node = blocker
    su = atf.properties["slurm-user"]
    reset_job = submit(node, acct_free, qos_free)
    other_job = submit(node, acct_free, qos_free)
    for _ in atf.timer(fatal=True):
        if len(accruing([reset_job, other_job])) == 2:
            break
    accrued = accrue_epoch(accrue_times([reset_job])[str(reset_job)])

    # Give the other job some age first. This can take a recalculation.
    for _ in atf.timer(timeout=PRIORITY_TIMEOUT, fatal=True):
        if ages([other_job]).get(other_job, 0) >= AGE_GAP:
            break

    before = int(time.time())
    atf.run_command(
        f"scontrol update jobid={reset_job} ResetAccrueTime", user=su, fatal=True
    )
    value = ""
    for _ in atf.timer(fatal=True):
        value = accrue_times([reset_job]).get(str(reset_job), "")
        if ACCRUE_TIME.fullmatch(value):
            break
    reset = accrue_epoch(value)
    assert reset >= before and reset > accrued, (
        f"ResetAccrueTime should set the AccrueTime of job {reset_job} to "
        f"now, but it is {value}"
    )

    # Wait for at least one priority recalculation after the reset.
    start_age = ages([other_job])[other_job]
    job_ages = {}
    for _ in atf.timer(timeout=PRIORITY_TIMEOUT, fatal=True):
        job_ages = ages([reset_job, other_job])
        if job_ages.get(other_job, 0) >= start_age + CALC_PERIOD:
            break

    after = accrue_times([reset_job]).get(str(reset_job))
    assert after == value, (
        f"The AccrueTime of job {reset_job} should still be {value} after a "
        f"priority recalculation, but it is {after}"
    )
    reset_age = job_ages.get(reset_job)
    assert reset_age is not None and reset_age < job_ages[other_job], (
        f"Job {reset_job} should be younger than job {other_job} after the "
        f"reset, but the ages are {job_ages}"
    )


@pytest.mark.xfail(
    reason="Issue 51241: ResetAccrueTime frees the accrue slot of a job that "
    "keeps accruing"
)
@pytest.mark.parametrize(
    "accounts,qos,expected,per_account,count", LIMITS, ids=LIMIT_IDS
)
def test_reset_accrue_time_under_limit(
    blocker, accounts, qos, expected, per_account, count
):
    """Verify ResetAccrueTime on an accruing job keeps the limit."""
    _, node = blocker
    job_ids = submit_jobs(node, accounts, qos)
    wait_for_marker(node)

    jobs = accruing(job_ids)
    assert len(jobs) == expected, (
        f"{expected} of the {len(accounts) * JOBS_PER_ACCOUNT} pending jobs "
        f"should accrue, but {sorted(jobs)} do"
    )
    if per_account:
        assert_accruing_per_account(job_ids, accounts, per_account)

    reset_job = min(jobs)
    atf.run_command(
        f"scontrol update jobid={reset_job} ResetAccrueTime",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    wait_for_marker(node)

    jobs = accruing(job_ids)
    assert len(jobs) == expected, (
        f"After ResetAccrueTime on job {reset_job}, {expected} jobs should "
        f"still accrue, but {sorted(jobs)} do"
    )
    if per_account:
        assert_accruing_per_account(job_ids, accounts, per_account)
    if count:
        assert_accrue_count(*count)


@pytest.mark.xfail(
    reason="Issue 51241: ResetAccrueTime frees the accrue slot of a job that "
    "keeps accruing"
)
def test_reset_accrue_time_new_limit(blocker):
    """Verify ResetAccrueTime brings a queue under a new accrue limit.

    This is the use case scontrol.1 gives for ResetAccrueTime.
    """
    _, node = blocker
    su = atf.properties["slurm-user"]
    job_ids = submit_jobs(node, [acct_newlimit], qos_free)
    wait_for_marker(node)

    jobs = accruing(job_ids)
    assert len(jobs) == JOBS_PER_ACCOUNT, (
        f"All {JOBS_PER_ACCOUNT} pending jobs should accrue before the limit "
        f"is added, but {sorted(jobs)} do"
    )

    atf.run_command(
        f"sacctmgr -i modify account {acct_newlimit} set GrpJobsAccrue={LIMIT}",
        user=su,
        fatal=True,
    )
    # The limit reaches slurmctld later, through slurmdbd.
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(
            f"scontrol show assoc_mgr accounts={acct_newlimit} flags=assoc",
            user=su,
            fatal=True,
        )
        if re.search(rf"\bGrpJobsAccrue={LIMIT}\(", output):
            break
    wait_for_marker(node)

    jobs = accruing(job_ids)
    assert len(jobs) == JOBS_PER_ACCOUNT, (
        f"All {JOBS_PER_ACCOUNT} jobs should still accrue after the limit is "
        f"added and before the reset, but {sorted(jobs)} do"
    )

    for job_id in job_ids:
        atf.run_command(
            f"scontrol update jobid={job_id} ResetAccrueTime", user=su, fatal=True
        )
    wait_for_marker(node)

    jobs = accruing(job_ids)
    assert len(jobs) == LIMIT, (
        f"After ResetAccrueTime under the new limit, {LIMIT} of the "
        f"{JOBS_PER_ACCOUNT} jobs should accrue, but {sorted(jobs)} do"
    )


@pytest.mark.parametrize("event", ["reconfigure", "restart"])
def test_accrue_limit_after_controller_event(blocker, event):
    """Verify accrue limits and times survive a reconfigure or restart.

    The jobs are under an association and a QOS limit.
    """
    _, node = blocker
    job_ids = submit_jobs(node, [acct_grp], qos_grp)
    wait_for_marker(node)

    jobs = accruing(job_ids)
    assert len(jobs) == LIMIT, (
        f"{LIMIT} of the {JOBS_PER_ACCOUNT} pending jobs should accrue, "
        f"but {sorted(jobs)} do"
    )

    if event == "reconfigure":
        atf.run_command(
            "scontrol reconfigure", user=atf.properties["slurm-user"], fatal=True
        )
    else:
        atf.restart_slurmctld()
    wait_for_marker(node)

    after = accruing(job_ids)
    assert after == jobs, (
        f"After the {event}, the jobs {jobs} should still accrue from the "
        f"same time, but {after} do"
    )
    assert_accrue_count(*GRP_ASSOC_COUNT)
    assert_accrue_count(*GRP_QOS_COUNT)

    cancelled = min(jobs)
    atf.run_command(f"scancel {cancelled}", fatal=True)
    wait_for_marker(node)

    jobs = accruing(job_ids)
    assert len(jobs) == LIMIT, (
        f"After the {event}, a waiting job should accrue in place of cancelled "
        f"job {cancelled}, but {sorted(jobs)} do"
    )
    assert_accrue_count(*GRP_ASSOC_COUNT)
    assert_accrue_count(*GRP_QOS_COUNT)
