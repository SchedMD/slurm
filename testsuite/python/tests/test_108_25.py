############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test multi-QOS job priority ordering and display (ticket 25566)."""

import os
import re

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
user_name = atf.properties["test-user"]
account = f"{test_name}_acct"
high_qos = f"{test_name}_high"
mid_qos = f"{test_name}_mid"
low_qos = f"{test_name}_low"
partition = test_name
node_name = None
node_cpus = 4


@pytest.fixture(scope="module", autouse=True)
def setup():
    global node_name, node_cpus

    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 25566: multi-QOS job priority ordering and display",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_nodes(1, [("CPUs", node_cpus), ("RealMemory", 1)])
    atf.require_config_parameter(
        "PartitionName",
        {partition: {"Nodes": "ALL", "OverSubscribe": "NO", "State": "UP"}},
    )
    # The QOS must be the only thing the priority depends on.
    atf.require_config_parameter("PriorityType", "priority/multifactor")
    atf.require_config_parameter("PriorityWeightQOS", 10000)
    atf.require_config_parameter("PriorityWeightAge", 0)
    atf.require_config_parameter("PriorityWeightAssoc", 0)
    atf.require_config_parameter("PriorityWeightFairshare", 0)
    atf.require_config_parameter("PriorityWeightJobSize", 0)
    atf.require_config_parameter("PriorityWeightPartition", 0)
    atf.require_config_parameter("PriorityWeightTRES", None)
    # Without this the QOS factor is normalized: qos_priority divided by the
    # highest QOS priority on the cluster. The expected values below would
    # then depend on unrelated QOS. With it the factor is the raw priority.
    atf.require_config_parameter_includes("PriorityFlags", "NO_NORMAL_QOS")
    # Keep periodic priority recalculation frequent during the test.
    atf.require_config_parameter("PriorityCalcPeriod", 1)
    # So two main scheduler cycles take seconds rather than two minutes.
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_config_parameter("EnforcePartLimits", "ALL")
    atf.require_slurm_running()
    for name, node in sorted(atf.get_nodes().items()):
        if node["effective_cpus"] >= node_cpus:
            node_name = name
            node_cpus = node["effective_cpus"]
            break
    assert node_name is not None, "Need a node with at least four effective CPUs"


@pytest.fixture(scope="function")
def setup_qos_account():
    _cleanup()

    atf.run_command(
        f"sacctmgr -i add qos {high_qos},{mid_qos},{low_qos}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # Only the QOS priorities matter here; the jobs are ordered by them alone
    atf.run_command(
        f"sacctmgr -i modify qos {high_qos} set Priority=100",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # mid_qos sits between the two; it is what the multi-QOS job has to be
    # ordered against
    atf.run_command(
        f"sacctmgr -i modify qos {mid_qos} set Priority=50",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i modify qos {low_qos} set Priority=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {account}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user_name} account={account} "
        f"qos={high_qos},{mid_qos},{low_qos}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    _wait_for_qos_priority(high_qos, 100)
    _wait_for_qos_priority(mid_qos, 50)
    _wait_for_qos_priority(low_qos, 10)

    yield

    _cleanup()


def _cleanup():
    atf.cancel_all_jobs()
    atf.run_command(
        f"sacctmgr -i remove user {user_name} account={account}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {account}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove qos {high_qos},{mid_qos},{low_qos}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def _qos_assoc_mgr_output(qos):
    return atf.run_command_output(
        f"scontrol show assoc_mgr flags=qos qos={qos}", fatal=True, quiet=True
    )


def _wait_for_qos_priority(qos, priority):
    for _ in atf.timer(fatal=True):
        if re.search(
            rf"QOS={re.escape(qos)}\(\d+\).*Priority={priority}\b",
            _qos_assoc_mgr_output(qos),
            re.S,
        ):
            break


def _submit(qos, name):
    return atf.submit_job_sbatch(
        f"--account={account} --partition={partition} --nodelist={node_name} "
        f"-n1 --mem=0 --time=10 --output=/dev/null --wrap='sleep infinity' "
        f"--qos={qos} --job-name={name}",
        fatal=True,
    )


def _main_sched_cycles():
    output = atf.run_command_output("sdiag", fatal=True, quiet=True)
    match = re.search(r"Main schedule statistics.*?Total cycles:\s*(\d+)", output, re.S)
    assert match, f"Unable to find the main scheduler cycle count in:\n{output}"
    return int(match.group(1))


def _wait_for_sched_cycles(count):
    start = _main_sched_cycles()
    for _ in atf.timer(fatal=True):
        if _main_sched_cycles() - start >= count:
            break


def test_multi_qos_priority_recovers_after_group_limit(setup_qos_account):
    """Fallback uses the low QOS until the high QOS group limit becomes free."""
    atf.run_command(
        f"sacctmgr -i modify qos {high_qos} set GrpTRES=cpu=1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for _ in atf.timer(fatal=True):
        if re.search(r"GrpTRES=cpu=1\b", _qos_assoc_mgr_output(high_qos)):
            break

    high_job = _submit(f"{high_qos},{low_qos}", name="highfiller")
    atf.wait_for_job_state(high_job, "RUNNING", fatal=True)
    assert (
        atf.get_job_parameter(high_job, "QOS") == high_qos
    ), "The first multi-QOS job must occupy the high QOS group limit"

    low_jobs = [
        _submit(f"{high_qos},{low_qos}", name="fallback") for _ in range(node_cpus - 1)
    ]
    for job_id in low_jobs:
        atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
        assert (
            atf.get_job_parameter(job_id, "QOS") == low_qos
        ), "Multi-QOS jobs must fall back while the high QOS group limit is full"

    mid_job = _submit(mid_qos, name="midlimited")
    multi_job = _submit(f"{high_qos},{low_qos}", name="multilimited")
    atf.wait_for_job_state(mid_job, "PENDING", fatal=True)
    atf.wait_for_job_state(multi_job, "PENDING", fatal=True)
    _wait_for_sched_cycles(2)

    atf.cancel_jobs([low_jobs[0]], fatal=True)
    atf.wait_for_job_state(mid_job, "RUNNING", fatal=True)
    assert (
        atf.get_job_parameter(multi_job, "JobState") == "PENDING"
    ), "The intermediate QOS must outrank the available low-QOS fallback"

    recovery_job = _submit(mid_qos, name="midrecovery")
    atf.wait_for_job_state(recovery_job, "PENDING", fatal=True)
    _wait_for_sched_cycles(2)
    atf.cancel_jobs([high_job], fatal=True)
    atf.wait_for_job_state(multi_job, "RUNNING", fatal=True)
    assert (
        atf.get_job_parameter(multi_job, "QOS") == high_qos
    ), "The pending multi-QOS job must regain the high QOS when its limit frees"
    assert (
        atf.get_job_parameter(recovery_job, "JobState") == "PENDING"
    ), "The recovered high-QOS priority must outrank the intermediate QOS"


def test_multi_qos_job_is_scheduled_with_the_qos_it_is_on(setup_qos_account):
    """A multi-QOS job outranks a job requesting only an intermediate QOS."""
    # Fill all CPUs so the competitors stay pending through scheduler cycles
    fillers = [_submit(mid_qos, name="filler") for _ in range(node_cpus)]
    for job_id in fillers:
        atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # mid_job is submitted first and is the one to beat: it outranks low_qos
    # (the last QOS of the multi-QOS request) but not high_qos (the one the
    # multi-QOS job is on).
    mid_job = _submit(mid_qos, name="midjob")
    multi_job = _submit(f"{high_qos},{low_qos}", name="multijob")
    atf.wait_for_job_state(mid_job, "PENDING", fatal=True)
    atf.wait_for_job_state(multi_job, "PENDING", fatal=True)

    _wait_for_sched_cycles(2)

    # Free exactly one CPU so only one of the two can start
    atf.cancel_jobs([fillers[0]], fatal=True)

    atf.wait_for_job_state(multi_job, "RUNNING", fatal=True)
    assert (
        atf.get_job_parameter(multi_job, "QOS") == high_qos
    ), f"multi_job should run in {high_qos}, the highest QOS it requested"
    assert (
        atf.get_job_parameter(mid_job, "JobState") == "PENDING"
    ), f"mid_job should stay pending, {high_qos} outranks {mid_qos}"


@pytest.mark.parametrize("qos_req", [f"{high_qos},{low_qos}", f"{low_qos},{high_qos}"])
def test_multi_qos_job_priority_displayed(setup_qos_account, qos_req):
    """Both clients report the highest requested QOS priority in either order."""
    # Fill all CPUs so the job stays pending through scheduler cycles
    fillers = [_submit(mid_qos, name="filler") for _ in range(node_cpus)]
    for job_id in fillers:
        atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    job_id = _submit(qos_req, name="dispjob")
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    _wait_for_sched_cycles(2)

    # Priority = highest QOS priority * PriorityWeightQOS = 100 * 10000
    expected = 100 * 10000
    assert (
        atf.get_job_parameter(job_id, "Priority") == expected
    ), f"scontrol should report {high_qos}'s priority for a '{qos_req}' job"

    # squeue is a separate display path, and it is the one squeue.1 documents.
    # PriorityLong is %Q, the integer; Priority is %p, normalized to 0.0-1.0.
    squeue_prio = atf.run_command_output(
        f"squeue -h -j {job_id} -O PriorityLong", fatal=True
    ).strip()
    assert (
        int(squeue_prio) == expected
    ), f"squeue should report {expected} for a '{qos_req}' job, got {squeue_prio}"
