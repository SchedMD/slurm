############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Ticket 26017: Het job components must each count as a submitted job.

Each component of a heterogeneous job counts as a "job" with respect to
resource limits (heterogeneous_jobs.shtml). Two limits are covered, each in
its own account and QOS so their counters cannot interfere:

- qos: QOS MaxSubmitJobsPerUser, observed as the user's MaxSubmitJobsPU and
  MaxJobsPU usage in "scontrol show assoc_mgr flags=qos".
- assoc: association MaxSubmitJobs, observed as the user association's
  GrpSubmitJobs and GrpJobs usage in "scontrol show assoc_mgr flags=assoc".

Since 26.05 the submit of components after the first is charged to the
temporary het job validation usage, which is freed at the end of the RPC,
while every component is uncharged on its own when it ends. A het job is then
counted once at submit, and cancelling it uncharges other jobs of the user.
The same cleanup also undid the simulated start of the later components
against the real usage, uncounting running jobs at submit.

A het job whose components stack over an association GrpTRES is rejected at
submit after the over-limit component was already created. That component
must not be left behind as a separate job.

A het job component submitted to several partitions is counted in the QOS of
each of them. The simulated start of the component during the het job
validation must not remove the submit count from the partition QOS the
component is not going to run in.

Het job components are submitted with --hold so that the counters only move
at submit and cancel, never because backfill started a component.

Requires: AccountingStorageEnforce=limits, accounting, SchedulerType=
sched/backfill (het jobs are rejected without it), one node for the running
job of the running-job test.
"""

import os
import re

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
# Jobs run as test-user. get_user_name() is the runner, a different
# account when slurmtestuser is configured.
test_user = atf.properties["test-user"]
acct_qos = f"{test_name}_acct_qos"
acct_assoc = f"{test_name}_acct_assoc"
acct_reject = f"{test_name}_acct_reject"
acct_parts = f"{test_name}_acct_parts"
qos_limited = f"{test_name}_qos_limited"
qos_plain = f"{test_name}_qos_plain"
rejected_name = f"{test_name}_rejected"
part_default = f"{test_name}_part"
part_a = f"{test_name}_part_a"
part_b = f"{test_name}_part_b"
qos_part_a = f"{test_name}_qos_part_a"
qos_part_b = f"{test_name}_qos_part_b"

het_components = 3
# Room for one het job plus one regular job, so the limit can be hit exactly.
submit_limit = het_components + 1

LIMITS = [
    pytest.param(("qos", acct_qos, qos_limited), id="qos"),
    pytest.param(("assoc", acct_assoc, qos_plain), id="assoc"),
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_nodes(1)
    # Replacing PartitionName drops the config's own default partition.
    # Jobs that do not pass -p still need one.
    atf.require_config_parameter(
        "PartitionName",
        {
            part_default: {"Nodes": "ALL", "Default": "YES"},
            part_a: {"Nodes": "ALL"},
            part_b: {"Nodes": "ALL"},
        },
    )
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def setup_limits(setup):
    """Create one account and QOS per limit kind, then wait for slurmctld."""
    user = test_user
    atf.run_command(
        f"sacctmgr -i add qos {qos_limited} MaxSubmitJobsPerUser={submit_limit}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_plain}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct_qos},{acct_assoc}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct_qos} qos={qos_limited}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct_assoc} qos={qos_plain} "
        f"MaxSubmitJobs={submit_limit}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # slurmdbd pushes the QOS before the associations that reference it, so
    # the associations being cached implies the QOS limit is too.
    atf.repeat_until(
        lambda: (_user_assoc_record(acct_qos), _user_assoc_record(acct_assoc)),
        lambda records: records[0] is not None
        and records[1] is not None
        and f"MaxSubmitJobs={submit_limit}(" in records[1],
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i del user {user} account={acct_qos},{acct_assoc}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i del account {acct_qos},{acct_assoc}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i del qos {qos_limited},{qos_plain}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="module")
def setup_reject_limit(setup_limits):
    """Create an account whose GrpTRES fits one 1 node component, not two."""
    atf.require_version(
        (26, 5),
        "sbin/slurmctld",
        reason="Ticket 24603: het job components are checked together against "
        "the limits at submit since 26.05",
    )
    user = test_user
    atf.run_command(
        f"sacctmgr -i add account {acct_reject}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct_reject} qos={qos_plain} "
        "GrpTRES=node=1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: _user_assoc_record(acct_reject),
        lambda record: record is not None,
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i del user {user} account={acct_reject}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i del account {acct_reject}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="module")
def setup_part_qos(setup_limits):
    """Give each of two partitions its own QOS, in an account of its own."""
    user = test_user
    for qos in (qos_part_a, qos_part_b):
        atf.run_command(
            f"sacctmgr -i add qos {qos} MaxSubmitJobsPerUser=100",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    atf.run_command(
        f"sacctmgr -i add account {acct_parts}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct_parts} "
        f"qos={qos_plain},{qos_part_a}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: _user_assoc_record(acct_parts),
        lambda record: record is not None,
        fatal=True,
    )
    for part, qos in ((part_a, qos_part_a), (part_b, qos_part_b)):
        # The QOS has to be known to slurmctld before the partition uses it
        atf.repeat_until(
            lambda: atf.run_command(
                f"scontrol update partitionname={part} qos={qos}",
                user=atf.properties["slurm-user"],
                quiet=True,
            )["exit_code"],
            lambda exit_code: exit_code == 0,
            fatal=True,
        )

    yield

    for part in (part_a, part_b):
        atf.run_command(
            f"scontrol update partitionname={part} qos=",
            user=atf.properties["slurm-user"],
            quiet=True,
        )
    atf.run_command(
        f"sacctmgr -i del user {user} account={acct_parts}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i del account {acct_parts}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i del qos {qos_part_a},{qos_part_b}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture
def cancel_rejected():
    """Cancel jobs of a rejected het job, which the testsuite does not track."""
    yield
    atf.run_command(
        f"scancel -n {rejected_name} -u {test_user}",
        quiet=True,
    )


@pytest.fixture
def limit(request, setup_limits):
    """Return (kind, account, qos) once the user's counters are back to zero."""
    kind, acct, qos = request.param
    _wait_for_usage(kind, acct, qos, submit=0, jobs=0)
    return kind, acct, qos


def _user_assoc_record(acct):
    """Return the one-line assoc_mgr record of the user's association."""
    output = atf.run_command_output(
        f"scontrol -o show assoc_mgr flags=assoc accounts={acct} users={test_user}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    for line in output.splitlines():
        if f"Account={acct} UserName={test_user}(" in line:
            return line
    return None


def _usage(kind, acct, qos):
    """Return the user's {"submit": n, "jobs": n} usage for the limit kind."""
    if kind == "assoc":
        record = _user_assoc_record(acct)
        if record is None:
            pytest.fail(f"slurmctld has no association for account {acct}")
        submit = re.search(r"GrpSubmitJobs=[^(\s]*\((\d+)\)", record)
        jobs = re.search(r"GrpJobs=[^(\s]*\((\d+)\)", record)
        if not submit or not jobs:
            pytest.fail(f"Expected GrpSubmitJobs and GrpJobs in: {record}")
        return {"submit": int(submit.group(1)), "jobs": int(jobs.group(1))}

    output = atf.run_command_output(
        f"scontrol -o show assoc_mgr flags=qos qos={qos}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    record = next(
        (line for line in output.splitlines() if line.startswith(f"QOS={qos}(")),
        None,
    )
    if record is None:
        pytest.fail(f"slurmctld has no record for QOS {qos}")
    user_limits = re.search(
        rf"\s{re.escape(test_user)}\(\d+\)=\{{"
        r"MaxJobsPU=[^(\s]*\((\d+)\) MaxJobsAccruePU=[^(\s]*\(\d+\) "
        r"MaxSubmitJobsPU=[^(\s]*\((\d+)\)",
        record,
    )
    # The per-user entry is only created by the user's first job in the QOS
    if not user_limits:
        return {"submit": 0, "jobs": 0}
    return {
        "submit": int(user_limits.group(2)),
        "jobs": int(user_limits.group(1)),
    }


def _wait_for_usage(kind, acct, qos, submit, jobs):
    """Wait until the user's submitted and running job counts are as given."""
    expected = {"submit": submit, "jobs": jobs}
    atf.repeat_until(
        lambda: _usage(kind, acct, qos),
        lambda usage: usage == expected,
        fatal=True,
    )


def _job_args(acct, qos, hold=True, partitions=None):
    """sbatch arguments for one job (or het component) in acct and qos."""
    hold_arg = " --hold" if hold else ""
    partition_arg = f" -p {partitions}" if partitions else ""
    return (
        f"-J {test_name} -A {acct} --qos={qos} -t2 -o /dev/null{hold_arg}"
        f"{partition_arg} --wrap 'sleep infinity'"
    )


def _het_job_args(acct, qos):
    """sbatch arguments for a held het job, every component in acct and qos."""
    return " : ".join([_job_args(acct, qos)] * het_components)


def _submit_het_job(acct, qos):
    """Submit a held het job and return (leader_id, component_ids)."""
    leader_id = atf.submit_job_sbatch(_het_job_args(acct, qos), fatal=True)
    component_ids = atf.range_to_list(
        atf.get_job_parameter(leader_id, "HetJobIdSet", fatal=True)
    )
    assert len(component_ids) == het_components, (
        f"het job {leader_id} should have {het_components} components, got "
        f"{component_ids}"
    )
    return leader_id, component_ids


@pytest.mark.parametrize("limit", LIMITS, indirect=True)
def test_het_job_submit_counts_each_component(limit):
    """Every component of a submitted het job is counted as a submitted job."""
    kind, acct, qos = limit

    _submit_het_job(acct, qos)

    usage = _usage(kind, acct, qos)
    assert usage["submit"] == het_components, (
        f"a {het_components} component het job should count as "
        f"{het_components} submitted jobs, got {usage['submit']}"
    )


@pytest.mark.parametrize("limit", LIMITS, indirect=True)
def test_het_job_submit_limit_enforced(limit):
    """A het job uses one submit slot per component, so the limit is reached.

    The limit leaves room for one het job and one regular job. Once both are
    submitted, neither another regular job nor another het job may be.
    """
    kind, acct, qos = limit

    _submit_het_job(acct, qos)
    assert (
        atf.submit_job_sbatch(_job_args(acct, qos)) != 0
    ), f"a regular job should fit in the remaining submit slot of {submit_limit}"

    assert atf.submit_job_sbatch(_job_args(acct, qos), xfail=True) == 0, (
        f"a regular job should be rejected once the het job and a regular job "
        f"used all {submit_limit} submit slots"
    )
    assert atf.submit_job_sbatch(_het_job_args(acct, qos), xfail=True) == 0, (
        f"a second het job should be rejected once {submit_limit} submit slots "
        "are used"
    )


@pytest.mark.parametrize("limit", LIMITS, indirect=True)
def test_het_job_cancel_keeps_other_jobs_counted(limit):
    """Cancelling a het job uncounts only its own components.

    Without this, every submit and cancel of a het job lowers the counter by
    the number of components minus one, and the user can submit past the
    limit over and over.
    """
    kind, acct, qos = limit

    atf.submit_job_sbatch(_job_args(acct, qos), fatal=True)
    leader_id, component_ids = _submit_het_job(acct, qos)

    atf.run_command(f"scancel {leader_id}", fatal=True)
    for job_id in component_ids:
        atf.wait_for_job_state(job_id, "CANCELLED", fatal=True)

    usage = _usage(kind, acct, qos)
    assert usage["submit"] == 1, (
        "the regular job should still be counted as submitted after the het "
        f"job was cancelled, got {usage['submit']} submitted jobs"
    )


@pytest.mark.parametrize("limit", LIMITS, indirect=True)
def test_het_job_submit_keeps_running_jobs_counted(limit):
    """Submitting a held het job does not uncount the user's running jobs.

    The het job limit validation simulates the start of each component and
    undoes it before the RPC returns. That undo must only touch the simulated
    usage, so the running job stays counted.
    """
    kind, acct, qos = limit

    running_id = atf.submit_job_sbatch(_job_args(acct, qos, hold=False), fatal=True)
    atf.wait_for_job_state(running_id, "RUNNING", fatal=True)
    _wait_for_usage(kind, acct, qos, submit=1, jobs=1)

    _submit_het_job(acct, qos)

    usage = _usage(kind, acct, qos)
    assert usage["jobs"] == 1, (
        "submitting a held het job should leave the running job counted, got "
        f"{usage['jobs']} running jobs"
    )


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (26, 5, 6),
    reason="Ticket 26017: a het job rejected at submit left its over-limit "
    "component pending before 26.05.6",
)
@pytest.mark.parametrize("command", ["sbatch", "salloc"])
def test_het_job_rejected_component_not_left_behind(
    command, setup_reject_limit, cancel_rejected
):
    """A het job rejected at its second component leaves no job behind.

    With association GrpTRES=node=1, each 1 node component passes the limit
    validation on its own and is created, then the het job limit check stacks
    both and rejects the second one. Every created component must be removed
    and uncounted, including the rejected one.
    """
    _wait_for_usage("assoc", acct_reject, qos_plain, submit=0, jobs=0)

    component = f"-J {rejected_name} -A {acct_reject} --qos={qos_plain} -N1 -t2 --hold"
    if command == "sbatch":
        batch_component = f"{component} -o /dev/null --wrap 'sleep infinity'"
        args = f"{batch_component} : {batch_component}"
    else:
        args = f"{component} --no-shell : {component}"
    result = atf.run_command(f"{command} {args}", timeout=30)
    assert result["exit_code"] != 0, f"{command} of the het job should be rejected"
    assert "accounting/QOS policy" in result["stderr"], (
        f"{command} should be rejected by the het job limit check, got: "
        f"{result['stderr']}"
    )

    left_behind = {
        job_id: job.get("JobState")
        for job_id, job in atf.get_jobs(quiet=True).items()
        if job.get("JobName") == rejected_name and job.get("JobState") != "CANCELLED"
    }
    assert not left_behind, (
        "no component of the rejected het job should be left behind, got "
        f"{left_behind}"
    )

    usage = _usage("assoc", acct_reject, qos_plain)
    assert usage["submit"] == 0, (
        "no component of the rejected het job should stay counted as "
        f"submitted, got {usage['submit']}"
    )


def test_het_job_partition_qos_shared_with_job_qos_not_leaked(setup_part_qos):
    """Simulated het job usage is undone in a QOS used as partition and job QOS.

    The first component runs in part_a, whose partition QOS is qos_part_a. The
    second component uses qos_part_a as its job QOS. The het job validation
    simulates the start of both components and then undoes it. The first
    component's start in qos_part_a must be undone from the same usage it was
    charged to, or the user's running job count in qos_part_a stays above zero
    although the het job is only held.
    """
    qos_names = (qos_part_a, qos_part_b)
    for qos in qos_names:
        _wait_for_usage("qos", acct_parts, qos, submit=0, jobs=0)

    first = _job_args(acct_parts, qos_plain, partitions=part_a)
    second = _job_args(acct_parts, qos_part_a, partitions=part_b)
    leader_id = atf.submit_job_sbatch(f"{first} : {second}", fatal=True)

    for qos in qos_names:
        usage = _usage("qos", acct_parts, qos)
        assert usage["jobs"] == 0, (
            "submitting a held het job should not leave running jobs counted "
            f"in QOS {qos}, got {usage['jobs']}"
        )

    atf.run_command(f"scancel {leader_id}", fatal=True)
    for qos in qos_names:
        _wait_for_usage("qos", acct_parts, qos, submit=0, jobs=0)


def test_het_job_multi_partition_keeps_partition_qos_counted(setup_part_qos):
    """A het job in several partitions counts in the QOS of each partition.

    Each component is submitted to both partitions, whose QOS differ. At the
    real start of a job the submit count is removed from the partition QOS it
    does not run in. The het job validation only simulates that start, so the
    submit counts must stay in both partition QOS until the het job ends.
    """
    partitions = f"{part_a},{part_b}"
    qos_names = (qos_part_a, qos_part_b)
    for qos in qos_names:
        _wait_for_usage("qos", acct_parts, qos, submit=0, jobs=0)

    job_args = _job_args(acct_parts, qos_plain, partitions=partitions)
    atf.submit_job_sbatch(job_args, fatal=True)
    het_job_args = " : ".join([job_args] * het_components)
    leader_id = atf.submit_job_sbatch(het_job_args, fatal=True)

    expected = 1 + het_components
    for qos in qos_names:
        usage = _usage("qos", acct_parts, qos)
        assert usage["submit"] == expected, (
            f"the regular job and the {het_components} het job components "
            f"should count as {expected} submitted jobs in partition QOS "
            f"{qos}, got {usage['submit']}"
        )

    atf.run_command(f"scancel {leader_id}", fatal=True)
    atf.repeat_until(
        lambda: [_usage("qos", acct_parts, qos)["submit"] for qos in qos_names],
        lambda counts: counts == [1, 1],
        fatal=True,
    )
