############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""test_130_11.py - accrue limits on pending jobs.

An accrue limit caps how many pending jobs accrue age priority at once. Jobs
over the limit stay pending and can still run. Held, dependent and future
begin time jobs do not accrue. test_130_12 covers ACCRUE_ALWAYS and
test_130_13 covers ResetAccrueTime and controller restarts.

A job accrues when squeue shows its AccrueTime. The scontrol show assoc_mgr
count is also checked where possible.
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

acct_free = "acct_free_130_11"
acct_free2 = "acct_free2_130_11"
acct_grp = "acct_grp_130_11"
acct_max = "acct_max_130_11"
acct_parent = "acct_parent_130_11"
acct_children = ["acct_child1_130_11", "acct_child2_130_11"]
acct_max_parent = "acct_mparent_130_11"
acct_max_children = ["acct_mchild1_130_11", "acct_mchild2_130_11"]

qos_free = "qos_free_130_11"
qos_grp = "qos_grp_130_11"
qos_pu = "qos_pu_130_11"
qos_pa = "qos_pa_130_11"
qos_part = "qos_part_130_11"

part_free = "part_free_130_11"
part_qos = "part_qos_130_11"

# Above any multifactor priority in this test.
HIGH_PRIORITY = 1000000

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
        acct_max_children,
        qos_free,
        2 * LIMIT,
        LIMIT,
        (
            f"accounts={acct_max_children[0]} users={test_user} flags=assoc",
            "MaxJobsAccrue",
        ),
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
    "inherited_MaxJobsAccrue",
    "parent_GrpJobsAccrue",
    "qos_GrpJobsAccrue",
    "qos_MaxJobsAccruePerUser",
    "qos_MaxJobsAccruePerAccount",
]

# Each case: the sbatch option, then the commands that make the job eligible
# and not eligible again.
INELIGIBLE = [
    ("--hold", "scontrol release {job}", "scontrol hold {job}"),
    (
        "--dependency=afterok:{blocker}",
        "scontrol update jobid={job} Dependency=",
        "scontrol update jobid={job} Dependency=afterok:{blocker}",
    ),
    (
        "--begin=now+1hour",
        "scontrol update jobid={job} StartTime=now",
        "scontrol update jobid={job} StartTime=now+1hour",
    ),
]
INELIGIBLE_IDS = ["held", "dependent", "begin"]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    atf.require_config_parameter("PriorityType", "priority/multifactor")
    atf.require_config_parameter_excludes("PriorityFlags", "ACCRUE_ALWAYS")
    atf.require_config_parameter(
        "PartitionName",
        {
            part_free: {"Nodes": "ALL", "Default": "YES"},
            part_qos: {"Nodes": "ALL"},
        },
    )
    atf.require_slurm_running()

    su = atf.properties["slurm-user"]
    qos_limits = {
        qos_free: "",
        qos_grp: f"GrpJobsAccrue={LIMIT}",
        qos_pu: f"MaxJobsAccruePerUser={LIMIT}",
        qos_pa: f"MaxJobsAccruePerAccount={LIMIT}",
        # Limits of 1 show if the partition QOS is used.
        qos_part: "GrpJobsAccrue=1 MaxJobsAccruePerUser=1 MaxJobsAccruePerAccount=1",
    }
    for qos, limits in qos_limits.items():
        atf.run_command(f"sacctmgr -i add qos {qos} {limits}", user=su, fatal=True)

    atf.run_command(
        f"sacctmgr -i add account {acct_free},{acct_free2},{acct_max}",
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
    atf.run_command(
        f"sacctmgr -i add account {acct_max_parent} MaxJobsAccrue={LIMIT}",
        user=su,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {','.join(acct_max_children)} "
        f"parent={acct_max_parent}",
        user=su,
        fatal=True,
    )

    accounts = ",".join(
        [acct_free, acct_free2, acct_grp, acct_max, *acct_children, *acct_max_children]
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


@pytest.fixture
def partition_qos():
    """Give part_qos a partition QOS with accrue limits of 1."""
    su = atf.properties["slurm-user"]
    atf.run_command(
        f"scontrol update partitionname={part_qos} qos={qos_part}",
        user=su,
        fatal=True,
    )

    yield

    atf.run_command(
        f"scontrol update partitionname={part_qos} qos=", user=su, fatal=True
    )


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

    Then the controller has also checked the earlier jobs, except an array that
    a scheduling pass skipped: see wait_for_accruing().
    """
    marker = submit(node, acct_free, qos_free)
    for _ in atf.timer(fatal=True):
        if accruing([marker]):
            break


def wait_for_accruing(job_ids, expected):
    """Return the accruing jobs in job_ids once at least expected accrue.

    The limits cap how many jobs accrue, not how soon a job starts accruing.
    An array can start accruing in a later scheduling pass than the marker.
    """
    jobs = {}
    for _ in atf.timer(fatal=True):
        jobs = accruing(job_ids)
        if len(jobs) >= expected:
            break
    return jobs


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


@pytest.mark.parametrize("array", [False, True], ids=["jobs", "array"])
@pytest.mark.parametrize(
    "accounts,qos,expected,per_account,count", LIMITS, ids=LIMIT_IDS
)
def test_accrue_limit(blocker, accounts, qos, expected, per_account, count, array):
    """Verify an accrue limit caps the accruing jobs.

    Array tasks count as jobs. When an accruing job leaves, a waiting job takes
    its slot and the others keep accruing.
    """
    _, node = blocker
    job_ids = submit_jobs(node, accounts, qos, array)
    wait_for_marker(node)

    jobs = wait_for_accruing(job_ids, expected)
    assert len(jobs) == expected, (
        f"{expected} of the {len(accounts) * JOBS_PER_ACCOUNT} pending jobs "
        f"should accrue, but {sorted(jobs)} do"
    )
    if per_account:
        assert_accruing_per_account(job_ids, accounts, per_account)
    if count:
        assert_accrue_count(*count)

    cancelled = min(jobs)
    kept = {key: value for key, value in jobs.items() if key != cancelled}
    cancel_time = int(time.time())
    atf.run_command(f"scancel {cancelled}", fatal=True)
    wait_for_marker(node)

    jobs = wait_for_accruing(job_ids, expected)
    assert len(jobs) == expected, (
        f"A waiting job should accrue in place of cancelled job {cancelled}, "
        f"so {expected} jobs should accrue, but {sorted(jobs)} do"
    )
    for key, value in kept.items():
        assert jobs.get(key) == value, (
            f"Job {key} should still accrue since {value}, but its AccrueTime "
            f"is {jobs.get(key)}"
        )
    [new] = set(jobs) - set(kept)
    assert accrue_epoch(jobs[new]) >= cancel_time, (
        f"Job {new} should accrue from when job {cancelled} was cancelled, "
        f"but its AccrueTime is {jobs[new]}"
    )
    if per_account:
        assert_accruing_per_account(job_ids, accounts, per_account)
    if count:
        assert_accrue_count(*count)


def test_cancel_whole_array(blocker):
    """Verify cancelling a pending array frees all its slots."""
    _, node = blocker
    array_id = submit(node, acct_grp, qos_free, f"--array=0-{JOBS_PER_ACCOUNT - 1}")
    waiting_id = submit(node, acct_grp, qos_free)
    wait_for_marker(node)

    jobs = wait_for_accruing([array_id, waiting_id], LIMIT)
    assert len(jobs) == LIMIT, (
        f"{LIMIT} of the {JOBS_PER_ACCOUNT + 1} pending jobs should accrue, "
        f"but {sorted(jobs)} do"
    )

    atf.run_command(f"scancel {array_id}", fatal=True)
    wait_for_marker(node)

    jobs = accruing([array_id, waiting_id])
    assert set(jobs) == {str(waiting_id)}, (
        f"After cancelling array {array_id}, only job {waiting_id} should "
        f"accrue, but {sorted(jobs)} do"
    )
    assert_accrue_count(*GRP_ASSOC_COUNT, expected=1)


@pytest.mark.parametrize("accrues", [True, False], ids=["accruing", "waiting"])
def test_job_start(blocker, accrues):
    """Verify the accrue count when a limited job starts.

    An accruing job gives its slot to a waiting job. A waiting job can start
    too, as the limit does not block running.
    """
    blocker_id, node = blocker
    su = atf.properties["slurm-user"]
    job_ids = submit_jobs(node, [acct_grp], qos_grp)
    wait_for_marker(node)

    jobs = accruing(job_ids)
    assert len(jobs) == LIMIT, (
        f"{LIMIT} of the {JOBS_PER_ACCOUNT} pending jobs should accrue, "
        f"but {sorted(jobs)} do"
    )

    waiting = {str(job_id) for job_id in job_ids} - set(jobs)
    started = min(jobs) if accrues else min(waiting)
    # The highest priority job starts first.
    atf.run_command(
        f"scontrol update jobid={started} Priority={HIGH_PRIORITY}",
        user=su,
        fatal=True,
    )
    atf.run_command(f"scancel {blocker_id}", fatal=True)
    atf.wait_for_job_state(int(started), "RUNNING", fatal=True)
    wait_for_marker(node)

    after = accruing(job_ids)
    if accrues:
        assert len(after) == LIMIT, (
            f"A waiting job should accrue in place of started job {started}, "
            f"so {LIMIT} jobs should accrue, but {sorted(after)} do"
        )
    else:
        assert after == jobs, (
            f"The jobs {jobs} should still accrue after waiting job {started} "
            f"started, but {after} do"
        )
    assert_accrue_count(*GRP_ASSOC_COUNT)
    assert_accrue_count(*GRP_QOS_COUNT)


@pytest.mark.parametrize(
    "option,make_eligible,make_ineligible", INELIGIBLE, ids=INELIGIBLE_IDS
)
def test_ineligible_job(blocker, option, make_eligible, make_ineligible):
    """Verify an ineligible job does not accrue or take a slot.

    Once eligible, it waits for a slot. When ineligible again, it frees it.
    """
    blocker_id, node = blocker
    su = atf.properties["slurm-user"]
    job = submit(node, acct_grp, qos_grp, option.format(blocker=blocker_id))
    regular = [submit(node, acct_grp, qos_grp) for _ in range(LIMIT)]
    wait_for_marker(node)

    jobs = set(accruing([job, *regular]))
    assert jobs == {str(r) for r in regular}, (
        f"Only the {LIMIT} eligible jobs {regular} should accrue, "
        f"but {sorted(jobs)} do"
    )

    atf.run_command(
        make_eligible.format(job=job, blocker=blocker_id), user=su, fatal=True
    )
    wait_for_marker(node)

    jobs = set(accruing([job, *regular]))
    assert jobs == {str(r) for r in regular}, (
        f"Job {job} should not accrue while {LIMIT} jobs accrue, "
        f"but {sorted(jobs)} do"
    )

    atf.run_command(f"scancel {regular[0]}", fatal=True)
    wait_for_marker(node)

    jobs = set(accruing([job, *regular]))
    assert jobs == {str(job), str(regular[1])}, (
        f"Job {job} should accrue in place of cancelled job {regular[0]}, "
        f"but {sorted(jobs)} do"
    )

    atf.run_command(
        make_ineligible.format(job=job, blocker=blocker_id), user=su, fatal=True
    )
    last = submit(node, acct_grp, qos_grp)
    wait_for_marker(node)

    jobs = set(accruing([job, regular[1], last]))
    assert jobs == {str(regular[1]), str(last)}, (
        f"Job {job} is not eligible any more, so it should not accrue and job "
        f"{last} should accrue in its place, but {sorted(jobs)} do"
    )
    assert_accrue_count(*GRP_ASSOC_COUNT)
    assert_accrue_count(*GRP_QOS_COUNT)


@pytest.mark.parametrize("field", ["Account", "QOS"], ids=["account", "qos"])
def test_change_job_group(blocker, field):
    """Verify moving an accruing job frees its slot in the old group.

    The moved job keeps accruing in its new group.
    """
    _, node = blocker
    su = atf.properties["slurm-user"]
    if field == "Account":
        account, qos, new_value, count = (
            acct_grp,
            qos_free,
            f"Account={acct_free}",
            GRP_ASSOC_COUNT,
        )
    else:
        account, qos, new_value, count = (
            acct_free,
            qos_grp,
            f"QOS={qos_free}",
            GRP_QOS_COUNT,
        )

    job_ids = submit_jobs(node, [account], qos)
    wait_for_marker(node)

    jobs = accruing(job_ids)
    assert len(jobs) == LIMIT, (
        f"{LIMIT} of the {JOBS_PER_ACCOUNT} pending jobs should accrue, "
        f"but {sorted(jobs)} do"
    )

    moved = min(jobs)
    atf.run_command(f"scontrol update jobid={moved} {new_value}", user=su, fatal=True)
    wait_for_marker(node)

    after = accruing(job_ids)
    assert moved in after, (
        f"Job {moved} should still accrue under its new group, but its "
        f"AccrueTime is {accrue_times([moved]).get(moved)}"
    )
    others = {key for key in after if key != moved}
    assert len(others) == LIMIT, (
        f"A waiting job should accrue in place of moved job {moved}, so "
        f"{LIMIT} jobs should accrue in the old group, but {sorted(others)} do"
    )
    assert_accrue_count(*count)


@pytest.mark.parametrize(
    "partitions", [[part_qos], [part_free, part_qos]], ids=["one", "two"]
)
@pytest.mark.parametrize(
    "qos,expected,count",
    [(qos_free, JOBS_PER_ACCOUNT, None), (qos_grp, LIMIT, GRP_QOS_COUNT)],
    ids=["no_job_qos_limit", "job_qos_limit"],
)
def test_partition_qos_limit_ignored(
    blocker, partition_qos, partitions, qos, expected, count
):
    """Verify partition QOS accrue limits are ignored.

    Only the job QOS limits apply, also with more than one partition.
    """
    _, node = blocker
    job_ids = [
        submit(node, acct_free, qos, f"-p {','.join(partitions)}")
        for _ in range(JOBS_PER_ACCOUNT)
    ]
    wait_for_marker(node)

    jobs = accruing(job_ids)
    assert len(jobs) == expected, (
        f"{expected} of the {JOBS_PER_ACCOUNT} pending jobs should accrue, as "
        f"the partition QOS limits do not apply, but {sorted(jobs)} do"
    )
    if count:
        assert_accrue_count(*count)
