############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify swait on a cluster that does not have stepmgr enabled site-wide.

swait(1) REQUIREMENTS makes stepmgr a hard requirement, satisfied either
site-wide or per-job with --stepmgr. Every other test_162_* module enables
it site-wide, so it can exercise neither the rejection nor the per-job
path. This module deliberately omits the site-wide setting, which is what
lets it cover both.
"""

import pytest

import atf

# 26.11 renumbered swait's exit codes: an error moved 2 -> 1. This module
# runs against clients of both vintages, so name the code rather than
# asserting a bare number.
RC_ERROR = 1 if atf.get_version("bin/swait") >= (26, 11) else 2


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        reason="Issue 50928: swait was added in 26.05",
    )
    atf.require_tool("swait")
    atf.require_nodes(1)
    # See the module docstring: the rejection only exists without stepmgr.
    atf.require_config_parameter_excludes("SlurmctldParameters", "enable_stepmgr")
    # The stepmgr runs in the job's extern step, so the per-job --stepmgr
    # test needs Contain. It does not enable stepmgr on its own, so the
    # rejection test above is unaffected.
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


def test_non_stepmgr_job_rejected():
    """swait against a job without stepmgr exits RC_ERROR and says why.

    Misconfiguring stepmgr is the most likely user error, so the rejection
    must be prompt and name the cause rather than hanging until --timeout.
    """

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_9_nostepmgr --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    result = atf.run_command(
        f"env -u SLURM_STEPMGR -u SLURM_JOB_ID -u SLURM_JOB_SLUID swait {job_id}",
        xfail=True,
    )
    assert result["exit_code"] == RC_ERROR, f"expected rc {RC_ERROR}; got: {result!r}"
    assert (
        "does not have stepmgr enabled" in result["stderr"]
    ), f"expected the stepmgr requirement to be named; got: {result['stderr']!r}"


@pytest.mark.slow
def test_per_job_stepmgr_satisfies_requirement():
    """sbatch --stepmgr satisfies the requirement without the site-wide
    setting. This is the form every swait(1) EXAMPLE uses, and it is only
    testable on a cluster where enable_stepmgr is absent.
    """

    STEP_SECS = 5
    out_file = "per_job_stepmgr.out"
    job_id = atf.submit_job_sbatch(
        "--stepmgr -N1 --time=5:00 --job-name=test_162_9_perjob "
        "--wrap '"
        f"srun --async -n1 sleep {STEP_SECS}; "
        f"swait > {out_file} 2>&1'",
        fatal=True,
    )
    # swait is the script's last command, so its exit code is the job's.
    # Without a stepmgr swait exits 1 (see the test above) and the job would
    # be FAILED; reaching COMPLETED is what proves --stepmgr took effect.
    assert atf.wait_for_job_state(
        job_id, "COMPLETED", timeout=180, fatal=False
    ), "the job failed; swait rejected a --stepmgr job as having no stepmgr"

    # Before 26.11 swait printed nothing, so only the wait can be asserted.
    if atf.get_version("bin/swait") >= (26, 11):
        atf.assert_file_contents(
            out_file,
            f"JobId={job_id} steps drained",
            contains=True,
            message="swait did not report the drain for a --stepmgr job",
        )
