############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Het job submission must not change association or QOS usage.

At submit, slurmctld checks a het job's components against the limits
together. That check must leave the real usage as it found it, whether the
job is accepted or rejected, and on both the batch and the allocation RPC.

Each test runs with GrpTRES=cpu=10 on a parent association, with the het
components in one child account or in sibling child accounts, and on a QOS.
A blocker job holds 3 CPUs of the limit while the het jobs are submitted.
"""

import re

import pytest

import atf

parent = "test_124_2_parent"
child_blocker = "test_124_2_blocker"
child_het = "test_124_2_het"
qos_acct = "test_124_2_qos_acct"
qos = "test_124_2_qos"
grp_cpus = 10
blocker_cpus = 3

pytestmark = pytest.mark.skipif(
    (26, 5) <= atf.get_version() < (26, 5, 6),
    reason="Bug 25538: het submit dropped association usage until 26.05.6",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    # Het jobs are only started by backfill; don't wait for its full cycle
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_nodes(2, [("CPUs", 8)])
    atf.require_slurm_running()

    for command in [
        f"add qos {qos} GrpTRES=cpu={grp_cpus}",
        f"add account {parent} GrpTRES=cpu={grp_cpus}",
        f"add account {qos_acct}",
        f"add account {child_blocker},{child_het} parent={parent}",
        f"add user {atf.get_user_name()} "
        f"account={child_blocker},{child_het},{qos_acct} "
        f"qos=normal,{qos} defaultqos=normal",
    ]:
        atf.run_command(
            f"sacctmgr -i {command}", user=atf.properties["slurm-user"], fatal=True
        )


@pytest.fixture(params=["assoc", "assoc_siblings", "qos"])
def limit(request):
    """Where GrpTRES is set, and the options that put each job under it"""

    if request.param == "qos":
        return {
            "blocker": f"-A {qos_acct} --qos={qos}",
            "het_first": f"-A {qos_acct} --qos={qos}",
            "het_last": f"-A {qos_acct} --qos={qos}",
            "show": f"flags=qos qos={qos}",
        }

    # With siblings, the het components only meet in the parent association
    first = child_blocker if request.param == "assoc_siblings" else child_het
    return {
        "blocker": f"-A {child_blocker}",
        "het_first": f"-A {first}",
        "het_last": f"-A {child_het}",
        "show": f"flags=assoc accounts={parent}",
    }


def het_args(limit, first, last):
    return f"{limit['het_first']} -N1 {first} : {limit['het_last']} -N1 {last}"


@pytest.fixture(autouse=True)
def blocker(limit):
    job_id = atf.submit_job_sbatch(
        f"{limit['blocker']} -N1 -c{blocker_cpus} --wrap='sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert cpus_used(limit) == blocker_cpus, "Blocker CPUs should count in the limit"

    return job_id


def cpus_used(limit):
    output = atf.run_command_output(
        f"scontrol show assoc_mgr {limit['show']}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    match = re.search(rf"GrpTRES=cpu={grp_cpus}\((\d+)\)", output)
    assert match, f"assoc_mgr {limit['show']} should report the CPU usage"
    return int(match.group(1))


def test_batch_accepted(limit, blocker):
    """A held het job is accepted, then waits for the blocker to end"""

    # 7 + 2 fits under the limit on its own, but not with the blocker's 3
    job_id = atf.submit_job_sbatch(
        f"-H {het_args(limit, '-c7', '-c2')} --wrap='sleep infinity'",
        fatal=True,
    )
    assert (
        cpus_used(limit) == blocker_cpus
    ), "Submitting a held het job should not change the usage"

    # Backfill runs every second, so give it several passes
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    assert not atf.wait_for_job_state(
        job_id, "RUNNING", timeout=10, xfail=True
    ), "Het job should not start past the GrpTRES CPU limit"

    atf.cancel_jobs([blocker], fatal=True)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "Het job should start once the blocker frees its CPUs"


@pytest.mark.skipif(
    atf.get_version() < (26, 5),
    reason="Ticket 24603: het jobs over a limit are rejected at submit since 26.05",
)
@pytest.mark.parametrize(
    "command",
    ["sbatch {} --wrap='sleep infinity'", "salloc --no-shell {}"],
    ids=["batch", "allocation"],
)
def test_rejected(limit, blocker, command):
    """A het job that alone exceeds the limit is rejected, leaving no job"""

    # 7 + 4 is over the limit even without the blocker
    result = atf.run_command(command.format(het_args(limit, "-c7", "-c4")))
    assert result["exit_code"] != 0, "Het job over the limit should be rejected"
    assert (
        "accounting/QOS policy" in result["stderr"]
    ), "Het job should be rejected for the limit"

    leftover = [
        job_id
        for job_id, job in atf.get_jobs(fatal=True).items()
        if job_id != blocker and job["JobState"] in ["PENDING", "RUNNING"]
    ]
    assert not leftover, "No component of a rejected het job should stay queued"
    assert (
        cpus_used(limit) == blocker_cpus
    ), "A rejected het job should not change the usage"


def test_allocation_accepted(limit):
    """A het allocation reaching exactly the limit runs, adding its CPUs"""

    job_id = atf.submit_job_salloc(
        f"--no-shell {het_args(limit, '-c5', '-c2')}", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert (
        cpus_used(limit) == grp_cpus
    ), "Usage should be the blocker plus both components"
