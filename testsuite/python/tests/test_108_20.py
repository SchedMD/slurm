############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A job update's new ReqNodeList is checked against its new partition.

With AccountingStorageEnforce=limits and EnforcePartLimits, slurmctld validates
an unprivileged user's job update that changes the partition, QOS or account. A
new ReqNodeList in that same update naming a node outside the new partition must
be rejected, and the job keeps its partition and node list. A job must never be
left in a partition its required nodes are not in, and a rejected update must
leave the job able to run where it could before.
"""

import pytest

import atf

_PARTITION = "only_first_node"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Ticket 16573: a new ReqNodeList skipped the partition check before 26.11",
    )
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_config_parameter("EnforcePartLimits", "ALL")
    atf.require_nodes(2)
    atf.require_slurm_running()

    test_user = atf.properties["test-user"]
    slurm_user = atf.properties["slurm-user"]
    atf.run_command(
        f"sacctmgr -i add user {test_user} "
        f"cluster={atf.get_config_parameter('ClusterName')} account=root",
        user=slurm_user,
        fatal=True,
    )
    # Wait for slurmctld to get the association
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(
            f"scontrol show assoc_mgr flags=assoc users={test_user} accounts=root",
            user=slurm_user,
            fatal=True,
        )
        if f"Account=root UserName={test_user}(" in output:
            break

    node_a = sorted(atf.get_nodes(quiet=True, fatal=True))[0]
    atf.run_command(
        f"scontrol create PartitionName={_PARTITION} Nodes={node_a}",
        user=slurm_user,
        fatal=True,
    )


@pytest.fixture(scope="module")
def nodes():
    """node_a is in both partitions, node_b only in the default one"""
    return sorted(atf.get_nodes(quiet=True, fatal=True))[:2]


def _update_job(job_id, update):
    return atf.run_command(
        f"scontrol update JobId={job_id} {update}",
        user=atf.properties["test-user"],
    )


def test_req_nodes_outside_partition_rejected(nodes):
    """Verify a ReqNodeList outside the new partition is rejected"""

    node_a, node_b = nodes
    default_partition = atf.default_partition()
    job_id = atf.submit_job_sbatch(
        f'-p {default_partition} -w {node_a} -H --wrap "sleep infinity"', fatal=True
    )

    result = _update_job(job_id, f"Partition={_PARTITION} ReqNodeList={node_b}")
    assert (
        result["exit_code"] != 0
    ), f"Requiring {node_b}, which is not in {_PARTITION}, should be rejected"
    assert (
        "Requested nodes not in this partition" in result["stderr"]
    ), f"Unexpected error for job {job_id}: {result['stderr']}"
    assert (
        atf.get_job_parameter(job_id, "ReqNodeList", fatal=True) == node_a
    ), f"The rejected update should leave job {job_id} requiring {node_a}"
    assert (
        atf.get_job_parameter(job_id, "Partition", fatal=True) == default_partition
    ), f"The rejected update should leave job {job_id} in {default_partition}"


def test_req_nodes_inside_partition_accepted(nodes):
    """Verify a ReqNodeList inside the new partition is accepted"""

    node_a, node_b = nodes
    job_id = atf.submit_job_sbatch(
        f'-p {atf.default_partition()} -H --wrap "sleep infinity"', fatal=True
    )

    result = _update_job(job_id, f"Partition={_PARTITION} ReqNodeList={node_a}")
    assert (
        result["exit_code"] == 0
    ), f"Requiring {node_a}, which is in {_PARTITION}, should succeed: {result['stderr']}"
    assert (
        atf.get_job_parameter(job_id, "Partition", fatal=True) == _PARTITION
    ), f"Job {job_id} should be in {_PARTITION}"
    assert (
        atf.get_job_parameter(job_id, "ReqNodeList", fatal=True) == node_a
    ), f"Job {job_id} should require {node_a}"


def test_later_failure_keeps_req_nodes_in_partition(nodes):
    """Verify a failure after the partition change keeps the nodes in it"""

    node_a, node_b = nodes
    job_id = atf.submit_job_sbatch(
        f'-p {atf.default_partition()} -w {node_b} -H --wrap "sleep infinity"',
        fatal=True,
    )

    # The invalid feature fails the update after the new partition and node
    # list are in place
    result = _update_job(
        job_id, f"Partition={_PARTITION} ReqNodeList={node_a} Features=nosuch"
    )
    assert result["exit_code"] != 0, f"Updating job {job_id} should fail"
    assert (
        "Invalid feature specification" in result["stderr"]
    ), f"Unexpected error for job {job_id}: {result['stderr']}"

    partition = atf.get_job_parameter(job_id, "Partition", fatal=True)
    req_nodes = atf.get_job_parameter(job_id, "ReqNodeList", fatal=True)
    assert not (
        partition == _PARTITION and req_nodes == node_b
    ), f"Job {job_id} is in {_PARTITION} but requires {node_b}, which is not in it"


def test_rejected_update_keeps_longer_req_nodes(nodes):
    """Verify a rejected update keeps a job runnable on any of its nodes"""

    node_a, node_b = nodes
    default_partition = atf.default_partition()

    # Keep both nodes busy so the job has to pend
    blockers = {
        node: atf.submit_job_sbatch(
            f'-p {default_partition} -w {node} --exclusive --wrap "sleep infinity"',
            fatal=True,
        )
        for node in nodes
    }
    for blocker_id in blockers.values():
        atf.wait_for_job_state(blocker_id, "RUNNING", fatal=True)
    job_id = atf.submit_job_sbatch(
        f'-p {default_partition} -N1 -w {node_a},{node_b} --wrap "sleep infinity"',
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    req_nodes = atf.get_job_parameter(job_id, "ReqNodeList", fatal=True)

    # The invalid feature fails the update after the new node list is in place
    result = _update_job(job_id, f"ReqNodeList={node_a} Features=nosuch")
    assert result["exit_code"] != 0, f"Updating job {job_id} should fail"
    assert (
        "Invalid feature specification" in result["stderr"]
    ), f"Unexpected error for job {job_id}: {result['stderr']}"
    assert (
        atf.get_job_parameter(job_id, "ReqNodeList", fatal=True) == req_nodes
    ), f"The rejected update should leave job {job_id} requiring {req_nodes}"

    # The job needs only one of its nodes, so it must run once node_b is free
    atf.cancel_jobs([blockers[node_b]], fatal=True)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Job {job_id} should start on {node_b} once it is free"
    assert (
        atf.get_job_parameter(job_id, "NodeList", fatal=True) == node_b
    ), f"Job {job_id} should run on {node_b}"


def test_rejected_license_update_keeps_longer_req_nodes(nodes):
    """Verify a rejected license update keeps a job runnable on any of its nodes"""

    node_a, node_b = nodes
    default_partition = atf.default_partition()

    # Keep both nodes busy so the job has to pend
    blockers = {
        node: atf.submit_job_sbatch(
            f'-p {default_partition} -w {node} --exclusive --wrap "sleep infinity"',
            fatal=True,
        )
        for node in nodes
    }
    for blocker_id in blockers.values():
        atf.wait_for_job_state(blocker_id, "RUNNING", fatal=True)
    job_id = atf.submit_job_sbatch(
        f'-p {default_partition} -N1 -w {node_a},{node_b} --wrap "sleep infinity"',
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)

    # The invalid license fails the update after its node lists are rebuilt
    result = _update_job(job_id, "Licenses=nosuch:1")
    assert result["exit_code"] != 0, f"Updating job {job_id} should fail"
    assert (
        "Invalid license specification" in result["stderr"]
    ), f"Unexpected error for job {job_id}: {result['stderr']}"

    # The job needs only one of its nodes, so it must run once node_b is free
    atf.cancel_jobs([blockers[node_b]], fatal=True)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Job {job_id} should start on {node_b} once it is free"
    assert (
        atf.get_job_parameter(job_id, "NodeList", fatal=True) == node_b
    ), f"Job {job_id} should run on {node_b}"
