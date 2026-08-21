############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify pending synchronous step id-at-submit holds under stepmgr.

Issue 50938: a synchronous step that has to queue is assigned its StepId at
submission, is visible in PENDING under that id, launches under it, and can
be cancelled by it. test_116_61.py covers that against the controller's own
step manager; under SlurmctldParameters=enable_stepmgr the step create is
served by the batch host's slurmstepd instead, so the same promises are
pinned here against that path.
"""

from pathlib import Path

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Daemons are always >= every client command, so requiring srun >= 26.11
    # already implies the stepmgr serving the step create is new enough.
    atf.require_version(
        (26, 11),
        "bin/srun",
        reason="Issue 50938: pending-step id-at-submit requires a 26.11+ srun",
    )
    # stepmgr is enabled when both SlurmctldParameters=enable_stepmgr and
    # PrologFlags=Contain are set.
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    # The hog and the queued step are both --exclusive, so they contend
    # whatever allocation unit the site's SelectTypeParameters names.
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_slurm_running()


def _hog_ready_snippet(ready):
    """Bash lines that hog both CPUs and block until the task actually holds
    them, so a following srun sees a genuinely busy allocation.

    Aborts the script if the hog never took them: the busy allocation is a
    precondition, so a test that ran anyway would report the missing
    contention as a product failure."""
    return (
        f"srun --exclusive -n2 sh -c 'touch \"{ready}\"; exec sleep infinity' &\n"
        f"for _ in $(seq 1 60); do [ -f '{ready}' ] && break; sleep 0.5; done\n"
        f"[ -f '{ready}' ] || {{ echo HOG_NOT_READY; exit 1; }}\n"
    )


def test_pending_sync_step_shows_real_id_and_reuses_it_under_stepmgr():
    """A synchronous step queued behind a CPU-hogging step shows a real StepId
    while PENDING and launches with that same id once the hog is freed."""

    ready = Path("stepmgr_hog_ready")
    script = Path("stepmgr_hog_and_pending.sh")
    atf.make_bash_script(
        script,
        _hog_ready_snippet(ready)
        + "srun --exclusive -n2 sleep infinity &\n"
        + "wait\n",
    )
    job_id = atf.submit_job(
        "sbatch", "-N1 -n2 -t5", str(script), wrap_job=False, fatal=True
    )
    assert job_id != 0, "sbatch should submit the job"

    hog_id, pending_id = f"{job_id}.0", f"{job_id}.1"
    atf.wait_for_step(job_id, 0, fatal=True)
    atf.wait_for_step(job_id, 1, state="PENDING", fatal=True)

    step_show = atf.run_command_output(
        f"scontrol -o show step {pending_id}", quiet=True, fatal=True
    )
    assert (
        f"StepId={pending_id}" in step_show
    ), f"the queued step should be addressable as {pending_id}, got: {step_show!r}"
    assert (
        "State=PENDING" in step_show
    ), f"the queued step should show State=PENDING, got: {step_show!r}"
    assert "TBD" not in atf.run_command_output(
        f"scontrol -o show step {job_id}", quiet=True, fatal=True
    ), "no step should render as TBD once a real id is assigned at submit"

    # Free the resources; the pending step launches under the SAME id.
    atf.run_command_exit(f"scancel {hog_id}", quiet=True, fatal=True)
    atf.wait_for_step(job_id, 1, fatal=True)


def test_scancel_pending_sync_step_by_id_under_stepmgr():
    """scancel <jobid>.<stepid> reaps a queued synchronous step before it
    launches and its waiting srun aborts non-zero, leaving the hog running."""

    out = Path("stepmgr_cancel.out")
    ready = Path("stepmgr_cancel_ready")
    script = Path("stepmgr_cancel.sh")
    # The pending srun runs in the foreground so its exit status reaches the
    # job output. The trailing wait blocks on the backgrounded hog, which
    # never ends on its own, so the job outlives the cancelled step and the
    # hog-survival check below cannot race the script exiting.
    atf.make_bash_script(
        script,
        _hog_ready_snippet(ready)
        + "srun --exclusive -n2 sleep infinity\n"
        + 'echo "PENDING_SRUN_RC=$?"\n'
        + "wait\n",
    )
    job_id = atf.submit_job(
        "sbatch",
        f"-N1 -n2 -t2 --output={out} --error={out}",
        str(script),
        wrap_job=False,
        fatal=True,
    )
    assert job_id != 0, "sbatch should submit the job"

    hog_id, pending_id = f"{job_id}.0", f"{job_id}.1"
    # The hog must be running before the queued step can pend on it, and
    # before the post-cancel check below can claim it survived.
    atf.wait_for_step(job_id, 0, fatal=True)
    atf.wait_for_step(job_id, 1, state="PENDING", fatal=True)

    assert (
        atf.run_command_exit(f"scancel {pending_id}", quiet=True) == 0
    ), f"scancel {pending_id} should succeed against a pending step"

    # The placeholder is reaped outright, not merely moved out of PENDING.
    for _ in atf.timer(fatal=True):
        if f"StepId={pending_id}" not in atf.run_command_output(
            f"scontrol -o show step {job_id}", quiet=True
        ):
            break
    assert f"StepId={hog_id}" in atf.run_command_output(
        f"scontrol -o show step {job_id}", quiet=True, fatal=True
    ), f"the hogging step {hog_id} should still be running after the step cancel"

    # The job is still running (held by the hog), so poll for the aborted
    # srun's line rather than waiting for the job to finish.
    atf.assert_file_contents(out, "PENDING_SRUN_RC=", contains=True)
    text = atf.run_command_output(f"cat {out}", quiet=True, fatal=True)
    assert (
        "PENDING_SRUN_RC=0" not in text
    ), f"the cancelled pending srun should exit non-zero, got: {text}"
    assert (
        "Pending job step cancelled" in text
    ), f"srun should log the pending-step cancellation, got: {text}"
