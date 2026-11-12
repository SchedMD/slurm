############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that a reservation job cancels or requeues a --signal=R: job."""

import pytest

import atf

RESV_NAME = "resv_start_delay"

# MaxStartDelay=1 lets a job delay a reservation job by up to 60 s (scontrol.1)
PREEMPT_TIMEOUT = 90

node = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global node

    atf.require_config_parameter("PreemptType", "preempt/partition_prio")
    atf.require_config_parameter("PreemptMode", "SUSPEND,GANG")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    node = atf.require_nodes(1, [("CPUs", 2), ("RealMemory", 512)])[0]
    atf.require_config_parameter("DefMemPerNode", "128")
    # Same PriorityTier, so only the R: option can make a job preemptable
    atf.require_config_parameter(
        "PartitionName",
        {
            "pm_off": {
                "Nodes": "ALL",
                "Default": "YES",
                "OverSubscribe": "FORCE:1",
                "PreemptMode": "OFF",
            },
            "pm_suspend": {
                "Nodes": "ALL",
                "OverSubscribe": "FORCE:1",
                "PreemptMode": "SUSPEND",
            },
            # No PreemptMode, so it uses the global one
            "pm_inherit": {
                "Nodes": "ALL",
                "OverSubscribe": "FORCE:1",
            },
            "pm_cancel": {
                "Nodes": "ALL",
                "OverSubscribe": "FORCE:1",
                "PreemptMode": "CANCEL",
            },
            "pm_requeue": {
                "Nodes": "ALL",
                "OverSubscribe": "FORCE:1",
                "PreemptMode": "REQUEUE",
            },
        },
    )
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def node_opts():
    """Reserve one node with MaxStartDelay and return options to fill it."""
    cpus = atf.get_node_parameter(node, "cpus", fatal=True)
    atf.run_command(
        f"scontrol create reservation reservationname={RESV_NAME} "
        f"users={atf.properties['test-user']} start=now duration=UNLIMITED "
        f"nodes={node} maxstartdelay=1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield f"-w {node} -c {cpus} -o /dev/null"

    atf.run_command(
        f"scontrol delete reservation {RESV_NAME}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def _preempt_r_job(node_opts, partition, requeue_opt):
    """Start an R: job on the reserved node, then submit a reservation job."""
    r_job = atf.submit_job_sbatch(
        f"{node_opts} -p {partition} {requeue_opt} --signal=R:USR1@0 "
        "--wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        r_job, "RUNNING"
    ), f"R: job {r_job} should run on the reserved node"

    resv_job = atf.submit_job_sbatch(
        f"{node_opts} -p {partition} --reservation={RESV_NAME} "
        "--wrap 'sleep infinity'",
        fatal=True,
    )
    return r_job, resv_job


def test_plain_job_blocked(node_opts):
    """Test that a job without R: does not start on the reserved node."""
    job_id = atf.submit_job_sbatch(f"{node_opts} --wrap 'sleep infinity'", fatal=True)
    assert atf.job_stays_pending(
        job_id
    ), f"Job {job_id} without R: should not start on the reserved node"


@pytest.mark.parametrize(
    "partition,requeue_opt",
    [
        ("pm_off", "--requeue"),
        ("pm_suspend", "--requeue"),
        ("pm_inherit", "--requeue"),
        ("pm_cancel", "--requeue"),
        ("pm_requeue", "--no-requeue"),
    ],
    ids=["off", "suspend", "inherit", "cancel", "requeue-not-allowed"],
)
def test_r_job_canceled(node_opts, partition, requeue_opt):
    """Test that the reservation job cancels the R: job."""
    r_job, resv_job = _preempt_r_job(node_opts, partition, requeue_opt)

    # OFF and SUSPEND act as CANCEL for R: jobs (CHANGELOG 24.11.1)
    assert atf.wait_for_job_state(
        r_job, "PREEMPTED", timeout=PREEMPT_TIMEOUT
    ), f"R: job {r_job} should be canceled by preemption"
    assert (
        atf.get_job_parameter(r_job, "Restarts", quiet=True) == 0
    ), f"R: job {r_job} should not be requeued"
    assert atf.wait_for_job_state(
        resv_job, "RUNNING"
    ), f"Reservation job {resv_job} should start"


def test_r_job_requeued(node_opts):
    """Test that the reservation job requeues the R: job if it can."""
    r_job, resv_job = _preempt_r_job(node_opts, "pm_requeue", "--requeue")

    assert atf.wait_for_job_state(
        r_job, "PENDING", timeout=PREEMPT_TIMEOUT
    ), f"R: job {r_job} should be requeued"
    assert (
        atf.get_job_parameter(r_job, "Restarts", quiet=True) == 1
    ), f"R: job {r_job} should be requeued once"
    assert atf.wait_for_job_state(
        resv_job, "RUNNING"
    ), f"Reservation job {resv_job} should start"
