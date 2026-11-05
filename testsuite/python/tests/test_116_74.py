############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Wake an older srun after its pending-step retry changes the callback port.

An older srun opens a new listener for each create attempt. Exercise a retry
while resources are busy and a retry while the job is suspended. In both
cases, releasing the resources must wake srun before its next timeout.
Run with pre-26.11 srun and a 26.11+ controller to expose the regression.

Delete this module when 26.05 leaves the supported version matrix: the
skipif below can never be true after that.
"""

from pathlib import Path

import pexpect
import pytest

import atf

pytestmark = [
    pytest.mark.slow,
    pytest.mark.skipif(
        atf.get_version("bin/srun") >= (26, 11),
        reason="Issue 51470: old srun must reopen its listener on retry",
    ),
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Issue 51470: requires retained pending steps in slurmctld 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_excludes("SlurmctldParameters", "enable_stepmgr")
    # srun waits MAX(60, SlurmctldTimeout) between retries; keep it at 60s.
    atf.require_config_parameter("SlurmctldTimeout", 60, "<=")
    # A narrow SrunPortRange can reuse the old port and mask a stale wake.
    atf.require_config_parameter("SrunPortRange", None)
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_slurm_running()


def _expect_step_create_error(child, timeout):
    """Wait until srun has received an error for its next step-create request."""
    for message in (
        "Sending message REQUEST_JOB_STEP_CREATE",
        "Received message RESPONSE_SLURM_RC",
    ):
        assert (
            child.expect([message, pexpect.EOF, pexpect.TIMEOUT], timeout=timeout) == 0
        ), f"srun should report {message!r}. Output:\n{child.before}"


@pytest.fixture
def pending_step():
    """Yield an external srun waiting behind a CPU hog."""
    script = Path("cpu_hog.sh")
    atf.make_bash_script(
        script, "srun --exclusive -n2 sleep infinity &\nsleep infinity\n"
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n2 -t5 {script}", fatal=True)
    child = None
    try:
        atf.wait_for_step(job_id, 0, fatal=True)
        # Run outside the allocation so suspending the job does not stop srun.
        child = atf.run_command_pexpect(
            f"env SLURM_DEBUG_FLAGS=net srun -vv --jobid={job_id} "
            "--exact -n1 echo PORT_REFRESH_STARTED"
        )
        _expect_step_create_error(child, atf.default_command_timeout)
        yield job_id, child
    finally:
        if child is not None:
            child.close(force=True)


@pytest.mark.parametrize("suspended", [False, True], ids=["busy", "suspended"])
def test_pending_step_wakes_after_retry(pending_step, suspended):
    """A retry must refresh the wake address even while the job is suspended."""
    job_id, child = pending_step
    if suspended:
        atf.run_command(
            f"scontrol suspend {job_id}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        atf.wait_for_job_state(job_id, "SUSPENDED", fatal=True)

    # The wait floor is 60s plus up to 9s of PID jitter. Wait for the
    # retry's RESPONSE, not the notice printed before sending the retry.
    _expect_step_create_error(child, 90)

    if suspended:
        atf.run_command(
            f"scontrol resume {job_id}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    atf.run_command(f"scancel {job_id}.0", fatal=True)
    # Less than the 60s retry floor: a stale-port wake cannot be hidden
    # by srun eventually retrying and finding the resources free.
    assert (
        child.expect(
            [r"(?m)^PORT_REFRESH_STARTED\r?$", pexpect.EOF, pexpect.TIMEOUT],
            timeout=30,
        )
        == 0
    ), f"Pending srun should launch on the resource-release wake. Output:\n{child.before}"
    child.expect(pexpect.EOF, timeout=atf.default_command_timeout)
    child.close()
    assert child.exitstatus == 0, f"srun should succeed. Output:\n{child.before}"
