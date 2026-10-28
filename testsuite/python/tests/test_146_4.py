############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""
Verify a partition's QOS cannot be changed or removed while jobs are running
or suspended in it, and that pending jobs do not block the change.

Older versions let the partition QOS change under running jobs, leaking usage.
"""
import re

import pytest

import atf

part = "pq"
acct = "acct_pq"
qos_old = "qos_pq_old"
qos_new = "qos_pq_new"
in_use_err = "Partition is in use"

pytestmark = pytest.mark.skipif(
    atf.get_version() < (26, 11),
    reason="Ticket 25710: Refusing partition QOS changes with active jobs was added in 26.11",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_config_parameter(
        "PartitionName",
        {
            "debug": {"Nodes": "ALL", "Default": "YES"},
            part: {"Nodes": "ALL"},
        },
    )
    atf.require_slurm_running()


@pytest.fixture(scope="module", autouse=True)
def setup_db(setup):
    def _sacctmgr_add(opts):
        atf.run_command(
            f"sacctmgr -i add {opts}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    _sacctmgr_add(f"qos {qos_old} GrpSubmitJobs=100")
    _sacctmgr_add(f"qos {qos_new} GrpSubmitJobs=100")
    _sacctmgr_add(f"account {acct}")
    user = atf.get_user_name()
    _sacctmgr_add(f"user {user} Account={acct}")

    yield

    for cmd in [
        f"user {user} where account={acct}",
        f"account {acct}",
        f"qos {qos_old},{qos_new}",
    ]:
        atf.run_command(
            f"sacctmgr -i remove {cmd}", user=atf.properties["slurm-user"], quiet=True
        )


@pytest.fixture(scope="function")
def job_ids():
    """Jobs submitted by the test, cancelled once it is done"""

    ids = []
    yield ids
    atf.cancel_jobs(ids, fatal=True)


@pytest.fixture(scope="function", autouse=True)
def part_qos(job_ids):
    """Start every test with the partition using qos_old"""

    atf.run_command(
        f"scontrol update partition={part} qos={qos_old}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.cancel_jobs(job_ids, fatal=True)
    job_ids.clear()
    atf.run_command(
        f"scontrol update partition={part} qos=",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def _partition_qos():
    """Name of the partition's QOS, or None if it has none"""

    output = atf.run_command_output(f"scontrol show partition {part}", fatal=True)
    qos = re.search(r"\bQoS=(\S+)", output).group(1)
    return None if qos == "N/A" else qos


def _qos_submit_jobs(qos):
    """Number of submitted jobs currently counted against a QOS"""

    output = atf.run_command_output(
        f"scontrol show assoc_mgr flags=qos qos={qos}", fatal=True
    )
    match = re.search(r"GrpSubmitJobs=\S+\((\d+)\)", output)
    assert match, f"No GrpSubmitJobs usage reported for QOS {qos}"
    return int(match.group(1))


def _update_partition(qos):
    return atf.run_command(
        f"scontrol update partition={part} qos={qos}",
        user=atf.properties["slurm-user"],
    )


def _submit_running_job(job_ids):
    job_id = atf.submit_job_sbatch(
        f"-p {part} -A {acct} -o /dev/null --wrap 'sleep infinity'", fatal=True
    )
    job_ids.append(job_id)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    return job_id


def _submit_pending_job(job_ids):
    job_id = atf.submit_job_sbatch(
        f"-p {part} -A {acct} -H -o /dev/null --wrap 'sleep infinity'", fatal=True
    )
    job_ids.append(job_id)
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    return job_id


def test_change_refused_with_running_job(job_ids):
    """Changing a partition's QOS is refused while a job is running in it"""

    _submit_running_job(job_ids)

    result = _update_partition(qos_new)
    assert result["exit_code"] != 0, "QOS change should be refused"
    assert in_use_err in result["stderr"]
    assert _partition_qos() == qos_old


def test_remove_refused_with_running_job(job_ids):
    """Removing a partition's QOS is refused while a job is running in it"""

    _submit_running_job(job_ids)

    result = _update_partition("")
    assert result["exit_code"] != 0, "QOS removal should be refused"
    assert in_use_err in result["stderr"]
    assert _partition_qos() == qos_old


def test_change_refused_with_suspended_job(job_ids):
    """Changing a partition's QOS is refused while a job is suspended in it"""

    job_id = _submit_running_job(job_ids)
    atf.run_command(
        f"scontrol suspend {job_id}", user=atf.properties["slurm-user"], fatal=True
    )
    atf.wait_for_job_state(job_id, "SUSPENDED", fatal=True)

    result = _update_partition(qos_new)
    assert result["exit_code"] != 0, "QOS change should be refused"
    assert in_use_err in result["stderr"]
    assert _partition_qos() == qos_old


def test_same_qos_allowed_with_running_job(job_ids):
    """Setting the QOS the partition already has is not a change"""

    _submit_running_job(job_ids)

    result = _update_partition(qos_old)
    assert result["exit_code"] == 0, "Setting the same QOS should be allowed"
    assert _partition_qos() == qos_old


def test_change_allowed_after_jobs_end(job_ids):
    """Changing a partition's QOS works once no job is running in it"""

    job_id = _submit_running_job(job_ids)
    assert _update_partition(qos_new)["exit_code"] != 0
    atf.cancel_jobs([job_id], fatal=True)

    result = _update_partition(qos_new)
    assert result["exit_code"] == 0, "QOS change should work with no active jobs"
    assert _partition_qos() == qos_new


def test_pending_job_usage_moves_with_qos(job_ids):
    """Pending jobs do not block a QOS change and their submit usage moves"""

    _submit_pending_job(job_ids)
    assert _qos_submit_jobs(qos_old) == 1
    assert _qos_submit_jobs(qos_new) == 0

    result = _update_partition(qos_new)
    assert result["exit_code"] == 0, "Pending jobs must not block a QOS change"
    assert _partition_qos() == qos_new
    assert _qos_submit_jobs(qos_old) == 0
    assert _qos_submit_jobs(qos_new) == 1


def test_pending_job_usage_moves_on_qos_removal(job_ids):
    """Removing the partition QOS releases the pending job's submit usage"""

    _submit_pending_job(job_ids)
    assert _qos_submit_jobs(qos_old) == 1

    result = _update_partition("")
    assert result["exit_code"] == 0, "Pending jobs must not block QOS removal"
    assert _partition_qos() is None
    assert _qos_submit_jobs(qos_old) == 0
