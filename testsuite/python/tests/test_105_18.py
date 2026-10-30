############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sbatch --dependency after+time delays and special-exit afternotok."""

from datetime import datetime

import pytest

import atf

# Each job takes one CPU, so a dependent can run beside its target and only
# its dependency can hold it back.
MIN_NODE_CPUS = 2
JOB_OPTIONS = "-N1 -n1 -t5 -o /dev/null -e /dev/null"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(1, [("CPUs", MIN_NODE_CPUS)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_slurm_running()


def _submit_running_target():
    """Submit a target that runs until target_release exists."""
    atf.make_bash_script(
        "target.sh",
        "touch target_started\nuntil [ -f target_release ]; do sleep 0.2; done",
    )
    target_id = atf.submit_job_sbatch(f"{JOB_OPTIONS} --requeue target.sh", fatal=True)
    atf.wait_for_file("target_started", fatal=True)
    return target_id


def test_after_with_time_waits_from_target_start():
    """after:<job_id>+<time> holds the job until 'time' minutes after the start."""
    target_id = _submit_running_target()
    dependent_id = atf.submit_job_sbatch(
        f"{JOB_OPTIONS} --dependency=after:{target_id}+1 --wrap true", fatal=True
    )
    atf.wait_for_job_state(
        dependent_id, "PENDING", desired_reason="Dependency", fatal=True
    )

    # One minute of delay, plus up to one sched_interval (60s by default) for
    # the scheduler to notice it elapsed.
    atf.wait_for_job_state(dependent_id, "COMPLETED", timeout=180, fatal=True)
    atf.run_command("touch target_release", fatal=True)

    start = {
        job_id: datetime.fromisoformat(
            atf.get_job_parameter(job_id, "StartTime", fatal=True)
        )
        for job_id in (target_id, dependent_id)
    }
    delay = (start[dependent_id] - start[target_id]).total_seconds()
    assert delay >= 60, f"after+1 released the job {delay}s after the target started"


def test_afternotok_released_by_special_exit():
    """A target requeued and held with State=SpecialExit satisfies afternotok.

    The target never ends while the dependent waits, so only the special exit
    state can release it."""
    target_id = _submit_running_target()
    dependent_id = atf.submit_job_sbatch(
        f"{JOB_OPTIONS} --dependency=afternotok:{target_id} --wrap true", fatal=True
    )
    atf.wait_for_job_state(
        dependent_id, "PENDING", desired_reason="Dependency", fatal=True
    )

    atf.run_command(f"scontrol requeuehold State=SpecialExit {target_id}", fatal=True)
    atf.wait_for_job_state(target_id, "SPECIAL_EXIT", fatal=True)
    atf.wait_for_job_state(dependent_id, "COMPLETED", fatal=True)
