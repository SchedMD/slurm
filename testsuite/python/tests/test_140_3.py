############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Batch jobs: license handling across scontrol requeue.

Exercises slurmctld license bookkeeping when a batch job is requeued while
running (SchedMD ticket 25226): allocation on the second start, and hold when
the license request no longer matches configured Licenses= (e.g. count reduced
below the job's request after reconfigure).

Requires: slurm.conf Licenses= entry for the test license, select/cons_tres,
and a node able to run a simple srun step.

SchedulerParameters includes requeue_delay=0 (minimum delay before a requeued
batch job may start again; default would follow cred_expire and take much
longer), plus tight bf_interval/sched_interval so the controller schedules the
second run promptly once eligible.
"""

import os
import re

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
LIC_NAME = f"lrq_{test_name.replace('.', '_')}"
LIC_TOTAL = 100
LIC_REQ = 5
# Configured total below LIC_REQ triggers validate_configured on requeue rebuild.
LIC_TOTAL_REDUCED = 1

# Keep worst-case waits short; second RUNNING should appear within a few
# seconds once requeue_delay=0 and fast scheduling are configured.
_FIRST_RUNNING_TIMEOUT = 45
_LICENSE_SETTLE_TIMEOUT = 20
_POST_REQUEUE_RUNNING_TIMEOUT = 45
_HOLD_AFTER_REQUEUE_TIMEOUT = 45
_NO_SECOND_RUN_TIMEOUT = 15


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Inject Licenses= for batch requeue license test")
    atf.require_nodes(1, [("CPUs", 4)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes(
        "Licenses", f"{LIC_NAME}:{LIC_TOTAL}", source="slurm"
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("requeue_delay", 0), source="slurm"
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("bf_interval", 1), source="slurm"
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("sched_interval", 1), source="slurm"
    )
    atf.require_slurm_running()


def _license_used(name):
    out = atf.run_command_output(f"scontrol show lic {name}", fatal=True)
    m = re.search(r"Used=(\d+)", out)
    assert m is not None, f"Expected Used=N in license output: {out!r}"
    return int(m.group(1))


def _license_total(name):
    out = atf.run_command_output(f"scontrol show lic {name}", fatal=True)
    m = re.search(r"Total=(\d+)", out)
    assert m is not None, f"Expected Total=N in license output: {out!r}"
    return int(m.group(1))


def _wait_running_after_requeue(job_id, timeout=_POST_REQUEUE_RUNNING_TIMEOUT):
    """Poll until JobState=RUNNING and Restarts>=1 (post-requeue resume)."""
    for _ in atf.timer(timeout=timeout, poll_interval=0.2):
        state = atf.get_job_parameter(job_id, "JobState", quiet=True)
        restarts = atf.get_job_parameter(job_id, "Restarts", default=0, quiet=True)
        if isinstance(restarts, str) and restarts.isdigit():
            restarts = int(restarts)
        if state == "RUNNING" and restarts >= 1:
            return True
    return False


def _wait_held_invalid_license(job_id, timeout=_HOLD_AFTER_REQUEUE_TIMEOUT):
    """Poll until requeued job is held with an invalid-license message."""
    pattern = re.compile(r"no.?longer.?valid", re.IGNORECASE)
    for _ in atf.timer(timeout=timeout, poll_interval=0.2):
        state = atf.get_job_parameter(job_id, "JobState", quiet=True)
        restarts = atf.get_job_parameter(job_id, "Restarts", default=0, quiet=True)
        reason = atf.get_job_parameter(job_id, "Reason", quiet=True) or ""
        if isinstance(restarts, str) and restarts.isdigit():
            restarts = int(restarts)
        if state == "PENDING" and restarts >= 1 and pattern.search(reason):
            return True
    return False


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (25, 11)
    or atf.get_version("bin/sbatch") < (25, 5),
    reason="Ticket 25226: Batch requeue license rebuild and hold on invalid request fixed in 25.11+, and another issue with LicensesAlloc fixed in 25.05+",
)
def test_license_alloc_and_used_after_batch_requeue():
    """After requeue, LicensesAlloc must be set and license Used must count the job."""

    job_id = atf.submit_job_sbatch(
        f"-n1 --licenses={LIC_NAME}:{LIC_REQ} --requeue "
        "--wrap 'srun -n1 sleep infinity'",
        fatal=True,
    )
    assert job_id != 0, "Job submission should succeed"

    atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True, timeout=_FIRST_RUNNING_TIMEOUT
    )

    assert atf.repeat_until(
        lambda: _license_used(LIC_NAME),
        lambda used: used >= LIC_REQ,
        timeout=_LICENSE_SETTLE_TIMEOUT,
        poll_interval=0.2,
        fatal=False,
    ), (
        "License Used= should reach requested count on first RUNNING; "
        f"still below {LIC_REQ} after wait"
    )

    alloc_first = atf.get_job_parameter(job_id, "LicensesAlloc", quiet=True)
    assert alloc_first, (
        "Sanity check: first RUNNING should show LicensesAlloc for the request, "
        f"got {alloc_first!r}"
    )

    atf.run_command(
        f"scontrol requeue {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert _wait_running_after_requeue(
        job_id
    ), f"Job {job_id} should return to RUNNING with Restarts>=1 after requeue"

    alloc_after = atf.get_job_parameter(job_id, "LicensesAlloc", quiet=True)
    used_after = _license_used(LIC_NAME)
    assert alloc_after, (
        "After batch requeue and second RUNNING, LicensesAlloc should list "
        "consumed licenses (empty means slurmctld lost parsed license_list). "
        f"LicensesAlloc={alloc_after!r} Licenses="
        f"{atf.get_job_parameter(job_id, 'Licenses', quiet=True)!r} "
        f"scontrol Used={used_after}"
    )

    assert used_after >= LIC_REQ, (
        "Cluster license Used= should include this job again after the second "
        f"start; got Used={used_after}, expected at least {LIC_REQ}"
    )


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (25, 11),
    reason="Ticket 25226: Batch requeue license rebuild and hold on invalid request fixed in 25.11+",
)
def test_batch_requeue_hold_when_license_count_invalid():
    """Requeue after configured license count drops below the request holds the job."""

    job_id = atf.submit_job_sbatch(
        f"-n1 --licenses={LIC_NAME}:{LIC_REQ} --requeue "
        "--wrap 'srun -n1 sleep infinity'",
        fatal=True,
    )
    assert job_id != 0, "Job submission should succeed"

    atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True, timeout=_FIRST_RUNNING_TIMEOUT
    )

    # Shrink configured count below what the job still requests (5).
    atf.set_config_parameter("Licenses", f"{LIC_NAME}:{LIC_TOTAL_REDUCED}")
    atf.run_command(
        "scontrol reconfigure",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert atf.repeat_until(
        lambda: _license_total(LIC_NAME),
        lambda total: total == LIC_TOTAL_REDUCED,
        timeout=_LICENSE_SETTLE_TIMEOUT,
        poll_interval=0.2,
        fatal=False,
    ), (
        f"Reconfigure should set {LIC_NAME} Total={LIC_TOTAL_REDUCED} before "
        f"requeue; got Total={_license_total(LIC_NAME)}"
    )

    atf.run_command(
        f"scontrol requeue {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert _wait_held_invalid_license(job_id), (
        "Job should be PENDING with Restarts>=1 and Reason mentioning an invalid "
        f"license request after requeue; JobState="
        f"{atf.get_job_parameter(job_id, 'JobState', quiet=True)!r} "
        f"Restarts={atf.get_job_parameter(job_id, 'Restarts', quiet=True)!r} "
        f"Reason={atf.get_job_parameter(job_id, 'Reason', quiet=True)!r}"
    )

    assert not atf.wait_for_job_state(
        job_id,
        "RUNNING",
        timeout=_NO_SECOND_RUN_TIMEOUT,
        fatal=False,
    ), (
        "Held job with an invalid license request should not return to RUNNING "
        "without admin action"
    )

    atf.require_config_parameter_includes(
        "Licenses", f"{LIC_NAME}:{LIC_TOTAL}", source="slurm"
    )
