############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""test_130_12.py - accrue limits with PriorityFlags=ACCRUE_ALWAYS.

ACCRUE_ALWAYS ignores accrue limits. Held, dependent and future begin time
jobs accrue too. test_130_11 covers the case without this flag.

A job accrues when squeue shows its AccrueTime.
"""

import re
import time

import pytest

import atf

pytestmark = pytest.mark.slow

test_user = atf.properties["test-user"]

# LIMIT jobs may accrue. Each case submits JOBS.
LIMIT = 2
JOBS = 4

acct_free = "acct_free_130_12"
acct_grp = "acct_grp_130_12"
acct_max = "acct_max_130_12"
acct_parent = "acct_parent_130_12"
acct_children = ["acct_child1_130_12", "acct_child2_130_12"]

qos_free = "qos_free_130_12"
qos_grp = "qos_grp_130_12"
qos_pu = "qos_pu_130_12"
qos_pa = "qos_pa_130_12"

# The unset AccrueTime value is not documented: no timestamp means unset.
ACCRUE_TIME = re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}")

# Each case: the accounts and the QOS of the jobs.
LIMITS = [
    ([acct_grp], qos_free),
    ([acct_max], qos_free),
    (acct_children, qos_free),
    ([acct_free], qos_grp),
    ([acct_free], qos_pu),
    ([acct_free], qos_pa),
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
    atf.require_config_parameter_includes("PriorityFlags", "ACCRUE_ALWAYS")
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
        f"sacctmgr -i add account {acct_free},{acct_max}", user=su, fatal=True
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

    accounts = ",".join([acct_free, acct_grp, acct_max, *acct_children])
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


def accrue_times(job_ids):
    """Return the AccrueTime of each pending job in job_ids."""
    ids = {str(job_id) for job_id in job_ids}
    output = atf.run_command_output(
        f"squeue -h -t PD -u {test_user} -O JobID,AccrueTime", fatal=True
    )

    times = {}
    for line in output.splitlines():
        fields = line.split()
        if fields and fields[0] in ids:
            times[fields[0]] = fields[1] if len(fields) > 1 else ""
    return times


def accruing(job_ids):
    """Return the pending jobs in job_ids that accrue."""
    return {
        job_id
        for job_id, value in accrue_times(job_ids).items()
        if ACCRUE_TIME.fullmatch(value)
    }


@pytest.mark.parametrize("accounts,qos", LIMITS, ids=LIMIT_IDS)
def test_accrue_limit_ignored(blocker, accounts, qos):
    """Verify every pending job accrues, even over an accrue limit."""
    _, node = blocker
    per_account = JOBS // len(accounts)
    job_ids = [
        submit(node, account, qos) for account in accounts for _ in range(per_account)
    ]

    # Once a later job accrues, the controller has checked the earlier ones.
    marker = submit(node, acct_free, qos_free)
    for _ in atf.timer(fatal=True):
        if accruing([marker]):
            break

    jobs = accruing(job_ids)
    assert len(jobs) == JOBS, (
        f"All {JOBS} pending jobs should accrue, as ACCRUE_ALWAYS ignores the "
        f"limit of {LIMIT}, but only {sorted(jobs)} do"
    )


@pytest.mark.parametrize(
    "option",
    ["--hold", "--dependency=afterok:{blocker}", "--begin=now+1hour"],
    ids=["held", "dependent", "begin"],
)
def test_ineligible_job_accrues(blocker, option):
    """Verify held, dependent and future begin time jobs accrue now."""
    blocker_id, node = blocker
    job = submit(node, acct_free, qos_free, option.format(blocker=blocker_id))

    value = ""
    for _ in atf.timer(fatal=True):
        value = accrue_times([job]).get(str(job), "")
        if ACCRUE_TIME.fullmatch(value):
            break

    accrue_time = time.mktime(time.strptime(value, "%Y-%m-%dT%H:%M:%S"))
    assert (
        accrue_time <= time.time()
    ), f"Job {job} should accrue now, but its AccrueTime {value} is in the future"
