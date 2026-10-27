############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Updating a job whose node lists name a node that no longer exists.

A dynamic node in a pending job's --exclude or --nodelist can be deleted. Like a
restart, an update of the job must then ignore an excluded name: the update
succeeds, and the job still keeps off the excluded nodes that do exist. A
required name that is gone can not be resolved, so the update fails and leaves
the job pending as it was, still limited to its required nodes.
"""

import pytest

import atf

_DYNAMIC_NODE = "exclude_dyn1"
_DYNAMIC_REQ_NODE = "required_dyn1"
_DYNAMIC_REQ_NODE_2 = "required_dyn2"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Ticket 16573: a deleted node in a job's node lists broke job updates before 26.11",
    )
    atf.require_config_parameter("MaxNodeCount", 10)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_nodes(2)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def nodes():
    """The two configured nodes, taken before any dynamic node is created"""
    return sorted(atf.get_nodes(quiet=True, fatal=True))[:2]


def test_update_ignores_deleted_excluded_node(nodes):
    """Verify an update succeeds and keeps the excluded nodes that still exist"""

    node_a, node_b = nodes
    slurm_user = atf.properties["slurm-user"]
    atf.run_command(
        f"scontrol create NodeName={_DYNAMIC_NODE} State=FUTURE",
        user=slurm_user,
        fatal=True,
    )

    # Keep node_b busy so the job, which excludes node_a, has to pend
    blocker_id = atf.submit_job_sbatch(
        f'-w {node_b} --exclusive --wrap "sleep infinity"', fatal=True
    )
    atf.wait_for_job_state(blocker_id, "RUNNING", fatal=True)
    job_id = atf.submit_job_sbatch(
        f'-N1 -x {node_a},{_DYNAMIC_NODE} --wrap "sleep infinity"', fatal=True
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)

    atf.run_command(
        f"scontrol delete NodeName={_DYNAMIC_NODE}", user=slurm_user, fatal=True
    )

    result = atf.run_command(
        f"scontrol update JobId={job_id} TimeLimit=5", user=slurm_user
    )

    # The job must not start on node_a, which it still excludes. This negative
    # wait burns its whole timeout, and with sched_interval=1 a few seconds
    # already cover several scheduling cycles.
    assert not atf.wait_for_job_state(
        job_id, "RUNNING", timeout=5, xfail=True
    ), f"Job {job_id} started on {atf.get_job_parameter(job_id, 'NodeList')}"

    assert (
        result["exit_code"] == 0
    ), f"Updating job {job_id} should succeed: {result['stderr']}"
    assert (
        atf.get_job_parameter(job_id, "TimeLimit", fatal=True) == "00:05:00"
    ), f"Job {job_id} should have the updated TimeLimit"

    # Once node_b is free, the job must still be able to run there
    atf.cancel_jobs([blocker_id], fatal=True)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Job {job_id} should start on {node_b} once it is free"
    assert (
        atf.get_job_parameter(job_id, "NodeList", fatal=True) == node_b
    ), f"Job {job_id} should run on {node_b}, the node it does not exclude"


def test_update_keeps_job_with_deleted_required_node(nodes):
    """Verify a failed update leaves a job with a deleted required node pending"""

    node_a = nodes[0]
    slurm_user = atf.properties["slurm-user"]
    atf.run_command(
        f"scontrol create NodeName={_DYNAMIC_REQ_NODE} State=FUTURE",
        user=slurm_user,
        fatal=True,
    )
    job_id = atf.submit_job_sbatch(
        f'-N2 -w {node_a},{_DYNAMIC_REQ_NODE} --wrap "sleep infinity"', fatal=True
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    time_limit = atf.get_job_parameter(job_id, "TimeLimit", fatal=True)
    req_nodes = atf.get_job_parameter(job_id, "ReqNodeList", fatal=True)

    atf.run_command(
        f"scontrol delete NodeName={_DYNAMIC_REQ_NODE}", user=slurm_user, fatal=True
    )

    result = atf.run_command(
        f"scontrol update JobId={job_id} TimeLimit=5", user=slurm_user
    )

    # The job can never get its required nodes, so it must not start on others.
    # This negative wait burns its whole timeout, and with sched_interval=1 a few
    # seconds already cover several scheduling cycles.
    assert not atf.wait_for_job_state(
        job_id, "RUNNING", timeout=5, xfail=True
    ), f"Job {job_id} started on {atf.get_job_parameter(job_id, 'NodeList')}"

    assert (
        result["exit_code"] != 0
    ), f"Updating job {job_id} with a deleted required node should fail"
    assert (
        atf.get_job_parameter(job_id, "JobState", fatal=True) == "PENDING"
    ), f"Job {job_id} should still be pending"
    assert (
        atf.get_job_parameter(job_id, "TimeLimit", fatal=True) == time_limit
    ), f"The failed update should leave the TimeLimit of job {job_id} as it was"
    assert (
        atf.get_job_parameter(job_id, "ReqNodeList", fatal=True) == req_nodes
    ), f"The failed update should leave the ReqNodeList of job {job_id} as it was"


def test_update_keeps_job_limited_to_required_nodes(nodes):
    """Verify a failed update keeps a job off nodes outside its longer list"""

    node_a, node_b = nodes
    slurm_user = atf.properties["slurm-user"]
    atf.run_command(
        f"scontrol create NodeName={_DYNAMIC_REQ_NODE_2} State=FUTURE",
        user=slurm_user,
        fatal=True,
    )

    # Keep node_a busy so the job, which may only run there, has to pend
    blocker_id = atf.submit_job_sbatch(
        f'-w {node_a} --exclusive --wrap "sleep infinity"', fatal=True
    )
    atf.wait_for_job_state(blocker_id, "RUNNING", fatal=True)
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node_a},{_DYNAMIC_REQ_NODE_2} --wrap "sleep infinity"',
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)

    atf.run_command(
        f"scontrol delete NodeName={_DYNAMIC_REQ_NODE_2}",
        user=slurm_user,
        fatal=True,
    )

    result = atf.run_command(
        f"scontrol update JobId={job_id} TimeLimit=5", user=slurm_user
    )
    assert (
        result["exit_code"] != 0
    ), f"Updating job {job_id} with a deleted required node should fail"

    # node_b is idle but not in the job's list, so the job must not start there.
    # This negative wait burns its whole timeout, and with sched_interval=1 a few
    # seconds already cover several scheduling cycles.
    assert not atf.wait_for_job_state(
        job_id, "RUNNING", timeout=5, xfail=True
    ), f"Job {job_id} started on {atf.get_job_parameter(job_id, 'NodeList')}"

    # Once node_a is free, the job must still be able to run there
    atf.cancel_jobs([blocker_id], fatal=True)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Job {job_id} should start on {node_a} once it is free"
    assert (
        atf.get_job_parameter(job_id, "NodeList", fatal=True) == node_a
    ), f"Job {job_id} should run on {node_a}, the node it requires"
