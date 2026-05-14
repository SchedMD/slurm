############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
import atf
import pytest

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("PreemptType", "preempt/partition_prio")
    atf.require_config_parameter("PreemptMode", "SUSPEND,GANG")
    atf.require_config_parameter(
        "SchedulerParameters", "bf_interval=1,sched_interval=1"
    )
    atf.require_nodes(1, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    atf.require_config_parameter(
        "PartitionName",
        {
            "lowprio": {"Nodes": "ALL", "PriorityTier": "1"},
            "highprio": {"Nodes": "ALL", "PriorityTier": "2"},
        },
    )

    atf.require_version((26, 5), "sbin/slurmctld")
    atf.require_slurm_running()


@pytest.mark.parametrize(
    "preempt_mode,preempted_state",
    [
        ("SUSPEND,GANG", "SUSPENDED"),
        ("REQUEUE", "PENDING"),
        ("CANCEL", "PREEMPTED"),
    ],
)
def test_preempt_exempt_time(preempt_mode, preempted_state):
    """Verify that PreemptExemptTime protects a job across preemption modes.

    For each PreemptMode (SUSPEND,GANG / REQUEUE / CANCEL), submit a
    low-priority job, then a high-priority job on the same node.  The
    low-priority job should remain running during the 10-second exempt
    window and only be preempted after it expires.
    """

    atf.set_config_parameter("PreemptMode", preempt_mode, restart=True)
    atf.set_config_parameter("PreemptExemptTime", "00:00:10")

    job_id1 = atf.submit_job_sbatch(
        '-c2 -o /dev/null -p lowprio --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id1, "RUNNING", timeout=10
    ), f"Low-priority job ({job_id1}) did not start"

    job_id2 = atf.submit_job_sbatch(
        '-c2 -o /dev/null -p highprio --wrap "sleep 20"',
        fatal=True,
    )

    # Low-priority job should NOT be preempted during the exempt window
    assert not atf.wait_for_job_state(
        job_id1, preempted_state, timeout=5, xfail=True
    ), f"Low-priority job ({job_id1}) was preempted during exempt window"

    assert (
        atf.get_job_parameter(job_id1, "JobState") == "RUNNING"
    ), f"Low-priority job ({job_id1}) should still be running during exempt window"

    # After PreemptExemptTime expires the low-priority job must be preempted
    assert atf.wait_for_job_state(
        job_id1, preempted_state, timeout=15
    ), f"Low-priority job ({job_id1}) was not preempted ({preempted_state}) after exempt time expired"

    # High-priority job should now be running
    assert atf.wait_for_job_state(
        job_id2, "RUNNING", timeout=10
    ), f"High-priority job ({job_id2}) did not start after preemption"

    atf.cancel_jobs([job_id1, job_id2], fatal=True)
