############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test srun in het job nested in non-het job (Ticket 23961)."""

import logging
import re

import pexpect
import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(3, [("CPUs", 1)])
    atf.require_config_parameter_includes("PrologFlags", "contain")
    atf.require_config_parameter_includes(
        "LaunchParameters",
        "use_interactive_step",
    )

    # enable_stepmgr is required to trigger t23961
    atf.require_config_parameter_includes(
        "SlurmctldParameters",
        "enable_stepmgr",
    )

    # Reducing bf_interval and KillWait makes the test faster
    atf.require_config_parameter("SchedulerParameters", "bf_interval=1")
    atf.require_config_parameter("KillWait", "1")

    atf.require_slurm_running()


@pytest.fixture(scope="function")
def salloc_child():
    """
    Spawn an interactive salloc and tear it down (along with any sallocs
    nested inside it during the test) at the end.
    """
    child = atf.run_command_pexpect("salloc")

    yield child
    child.close(force=True)


@pytest.mark.xfail(
    atf.get_version("bin/salloc") < (25, 11),
    reason="Ticket 23961: Fix het salloc nested in non-het alloc",
)
def test_srun_in_het_salloc_nested_in_nonhet_salloc(salloc_child):
    """Test nest het salloc in non-het salloc"""

    child = salloc_child

    # wait for non-het salloc to start
    index = child.expect([r"Nodes.*are ready for job", pexpect.TIMEOUT, pexpect.EOF])
    if index != 0:
        pytest.fail("Non-het salloc not started.")
    logging.info("Non-het job should be ready")

    # Switch to prompt-based sync now that the outer interactive-step shell is up.
    atf.setup_pexpect_prompt(child)

    # start nested het salloc (which execs an srun)
    child.sendline("salloc :")

    logging.debug("Non-het salloc started. Waiting for het salloc to start.")
    index = child.expect(
        [
            re.compile(
                r"Nodes.*are ready for job\r\n.*Nodes.*are ready for job", re.DOTALL
            ),
            pexpect.TIMEOUT,
            pexpect.EOF,
        ]
    )
    if index != 0:
        pytest.fail("Het salloc not started.")
    logging.info("Both het components should be ready")

    # At this point either the het salloc failed and we're back in the
    # non-het salloc, or the het salloc succeeded and we're in the het
    # salloc. Identify which case happened.

    # SLURM_HET_SIZE will exist with a non-zero value for a het job
    child.sendline("echo hetsize=${SLURM_HET_SIZE:-0}")
    index = child.expect([r"hetsize=(\d+)", pexpect.TIMEOUT, pexpect.EOF])
    if index != 0:
        pytest.fail("Could not get het size from salloc.")
    logging.info("We should get the het size")

    hetsize = int(child.match.group(1))

    # remaining salloc should be the het one, if not fail
    assert hetsize == 2, "Hetsize should be 2"

    logging.info("Het salloc started as expected.")


@pytest.mark.xfail(
    atf.get_version("bin/salloc") < (25, 11),
    reason="Ticket 23961: Fix het salloc nested in non-het alloc",
)
def test_srun_in_het_sbatch_nested_in_nonhet_sbatch():
    """Test nest het sbatch in non-het sbatch"""

    out_file_nonhet = "slurm-nonhet-%j.out"

    # Submit sbatch job with appropriate arguments
    job_id_nonhet = atf.submit_job_sbatch(
        f"--output={out_file_nonhet} --wrap=\"sbatch : --parsable --wrap='srun true'\"",
        fatal=True,
    )

    # Wait for non-het job to complete
    atf.wait_for_job_state(job_id_nonhet, "COMPLETED", fatal=True)

    # Get het job id from non-het job output
    out_file_nonhet = out_file_nonhet.replace("%j", str(job_id_nonhet))
    atf.wait_for_file(out_file_nonhet, fatal=True)
    for t in atf.timer():
        content = atf.run_command_output(f'cat "{out_file_nonhet}"').strip()
        if content.isdigit():
            break
    else:
        pytest.fail(
            f"Non-het job {job_id_nonhet} should have written the nested het job id to {out_file_nonhet}, but got: {content!r}"
        )
    job_id_het = int(content)

    # Wait for het job to finish
    atf.wait_for_job_state(job_id_het, "COMPLETED", fatal=True)
