############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""
Test that a batch job whose prolog is interrupted by a scontrol reconfigure
still completes normally.

With PrologFlags=Alloc,Contain,DeferBatch the batch launch is held in the
agent defer_list until the prolog finishes.  A reconfigure discards the
in-memory defer_list; on recovery the slurmctld must re-queue the launch
from the persisted step state (launch_sent=False).

Ticket: 24361
"""

import re

import atf
import pytest


pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 24361: deferred batch re-launch fixed in 26.11",
    )
    atf.require_auto_config("needs to set Prolog, PrologFlags, and BatchStartTimeout")
    atf.require_config_parameter_includes("PrologFlags", "Alloc")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_config_parameter_includes("PrologFlags", "DeferBatch")
    atf.require_nodes(1)
    atf.set_config_parameter("BatchStartTimeout", "30")
    atf.require_slurm_running()


def test_batch_survives_reconfigure_during_prolog(tmp_path):
    """Batch job completes after a reconfigure that interrupts the prolog."""

    slurm_user = atf.properties["slurm-user"]

    # A flag file the prolog touches when it starts running so we know when to
    # reconfigure, and a sentinel that tells the prolog it may exit.
    prolog_started = str(tmp_path / "prolog_started")
    prolog_proceed = str(tmp_path / "prolog_proceed")
    prolog_script = str(tmp_path / "prolog.sh")
    output_file = str(tmp_path / "job.out")

    # Prolog: signal that it is running, then busy-wait until released.
    atf.make_bash_script(
        prolog_script,
        f"""touch {prolog_started}
while [ ! -f {prolog_proceed} ]; do sleep 0.5; done
""",
    )

    atf.set_config_parameter("Prolog", prolog_script)

    job_id = atf.submit_job_sbatch(f"-o {output_file} --wrap 'echo OK'", fatal=True)

    # Wait until the prolog is running before we reconfigure.
    assert atf.wait_for_file(
        prolog_started, timeout=30
    ), "Prolog did not start within 30 s"

    # Reconfigure while the prolog (and deferred batch launch) are in flight.
    atf.run_command("scontrol reconfigure", user=slurm_user, fatal=True, quiet=True)

    # Release the prolog so it can complete.
    atf.run_command(f"touch {prolog_proceed}", fatal=True)

    # The job must complete successfully — not stay stuck or get requeued.
    assert atf.wait_for_job_state(
        job_id, "COMPLETED", timeout=60
    ), f"Job {job_id} did not complete after reconfigure during prolog"

    # The batch launch must have been re-issued, not requeued.
    job_info = atf.run_command_output(f"scontrol show job {job_id}", fatal=True)
    assert re.search(
        r"Restarts=0\b", job_info
    ), f"Job {job_id} was requeued instead of relaunched: {job_info}"

    # The batch script must have actually run and produced output.
    assert atf.wait_for_file(
        output_file, timeout=15
    ), f"Job output file {output_file} was not created"

    output = atf.run_command_output(f"cat {output_file}", fatal=True)
    assert "OK" in output, f"Job output did not contain 'OK': {output!r}"
