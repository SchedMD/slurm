############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
import pytest

import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("PreemptType", "preempt/partition_prio")
    atf.require_config_parameter("PreemptMode", "SUSPEND,GANG")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
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

    # The preemptor must not have started either. Under SUSPEND,GANG the way
    # the exemption fails is that the preemptor is placed alongside the exempt
    # job on its cores, which leaves the victim RUNNING and never SUSPENDED --
    # indistinguishable from a working exemption unless the preemptor is
    # checked too.
    assert (
        atf.get_job_parameter(job_id2, "JobState") == "PENDING"
    ), f"High-priority job ({job_id2}) started during the exempt window"

    # After PreemptExemptTime expires the low-priority job must be preempted
    assert atf.wait_for_job_state(
        job_id1, preempted_state, timeout=15
    ), f"Low-priority job ({job_id1}) was not preempted ({preempted_state}) after exempt time expired"

    # High-priority job should now be running
    assert atf.wait_for_job_state(
        job_id2, "RUNNING", timeout=10
    ), f"High-priority job ({job_id2}) did not start after preemption"

    atf.cancel_jobs([job_id1, job_id2], fatal=True)


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Ticket 23654: exempt cores were rebuilt at most once a second, "
    "leaving a window at job start, until 26.11",
)
def test_preempt_exempt_time_covers_job_start():
    """A job is exempt from the moment it starts holding its allocation.

    test_preempt_exempt_time lets the low-priority job settle before the
    preemptor is submitted. That hides a window at job start: a preemptor
    scheduled in the same moment could be placed on the new job's cores before
    they were protected, and the victim stays RUNNING and unsuspended
    throughout, so only the preemptor shows it.

    Submit the preemptor as soon as the low-priority job is running, and repeat,
    so a window that only opens around job start is exercised rather than
    stepped over.
    """

    atf.set_config_parameter("PreemptMode", "SUSPEND,GANG", restart=True)
    atf.set_config_parameter("PreemptExemptTime", "00:00:10")

    for attempt in range(3):
        job_id1 = atf.submit_job_sbatch(
            '-c2 -o /dev/null -p lowprio --wrap "sleep infinity"',
            fatal=True,
        )
        # Poll hard so the preemptor lands as close to the job start as the
        # test can manage.
        assert atf.wait_for_job_state(
            job_id1, "RUNNING", poll_interval=0.1
        ), f"Attempt {attempt}: low-priority job ({job_id1}) did not start"

        job_id2 = atf.submit_job_sbatch(
            '-c2 -o /dev/null -p highprio --wrap "sleep 20"',
            fatal=True,
        )

        # Bounded by the 10 second PreemptExemptTime above, not by the atf
        # default: past that window the job is no longer exempt and the
        # preemptor is meant to start, so a longer wait would assert the
        # opposite of this test.
        assert not atf.wait_for_job_state(job_id2, "RUNNING", timeout=5, xfail=True), (
            f"Attempt {attempt}: high-priority job ({job_id2}) started while "
            f"({job_id1}) was still within its PreemptExemptTime"
        )
        assert atf.get_job_parameter(job_id2, "JobState") == "PENDING", (
            f"Attempt {attempt}: high-priority job ({job_id2}) should be "
            f"pending, not in a terminal state, during the exempt window"
        )
        assert atf.get_job_parameter(job_id1, "JobState") == "RUNNING", (
            f"Attempt {attempt}: low-priority job ({job_id1}) should still be "
            f"running during its exempt window"
        )

        atf.cancel_jobs([job_id1, job_id2], fatal=True)
