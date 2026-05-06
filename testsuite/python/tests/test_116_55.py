############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
"""Tests for srun --async stdout submit message and --parsable.

The async submit message moved from stderr info() to stdout printf so the
step id can be captured with $(...). --parsable strips the prefix to mirror
sbatch --parsable. --parsable on srun requires --async.
"""

import re

import atf
import pytest


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version((26, 5), "bin/srun")
    atf.require_version((26, 5), "sbin/slurmctld")
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_nodes(1)
    atf.require_slurm_running()


def _run_in_alloc(srun_cmd):
    """Run an srun command inside a sbatch allocation and return stdout/stderr.

    The wrapper script writes srun's stdout and stderr to separate files so
    the test can assert the submit message routes to stdout (not stderr).
    """
    out_file = atf.module_tmp_path / "srun_out.txt"
    err_file = atf.module_tmp_path / "srun_err.txt"
    if out_file.exists():
        out_file.unlink()
    if err_file.exists():
        err_file.unlink()

    job_id = atf.submit_job_sbatch(
        f"-N1 -o /dev/null --wrap '"
        f"{srun_cmd} >{out_file} 2>{err_file}; "
        f"echo srun_rc=$? >>{out_file}'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    return out_file.read_text(), err_file.read_text()


def test_async_stdout():
    """srun --async writes "Submitted step <jobid>.<stepid>" to stdout."""
    stdout, stderr = _run_in_alloc("srun --async true")

    assert re.search(
        r"^Submitted step \d+\.\d+$", stdout, re.MULTILINE
    ), f"expected 'Submitted step <jobid>.<stepid>' in stdout, got: {stdout!r}"
    assert (
        "Submitted step" not in stderr
    ), f"submit message should not appear on stderr, got: {stderr!r}"
    assert "srun_rc=0" in stdout, f"srun should exit 0, got stdout: {stdout!r}"


def test_srun_parsable_async():
    """srun --async --parsable writes bare "<jobid>.<stepid>" to stdout."""
    stdout, stderr = _run_in_alloc("srun --async --parsable true")

    match = re.search(r"^(\d+\.\d+)$", stdout, re.MULTILINE)
    assert match, f"expected bare '<jobid>.<stepid>' in stdout, got: {stdout!r}"
    assert (
        "Submitted step" not in stdout
    ), f"--parsable should strip the prefix, got: {stdout!r}"
    assert (
        "Submitted step" not in stderr
    ), f"submit message should not appear on stderr, got: {stderr!r}"
    assert "srun_rc=0" in stdout, f"srun should exit 0, got stdout: {stdout!r}"


def test_srun_parsable_without_async():
    """srun --parsable without --async errors at option-validation."""
    result = atf.run_command("srun --parsable -N1 true", xfail=True)

    assert result["exit_code"] != 0, "--parsable without --async should fail"
    assert (
        "--parsable requires --async" in result["stderr"]
    ), f"expected validation error, got stderr: {result['stderr']!r}"


def test_sbatch_parsable_unchanged():
    """sbatch --parsable behavior is unaffected by the field move."""
    result = atf.run_command("sbatch --parsable -o /dev/null --wrap 'true'", fatal=True)

    match = re.match(r"^(\d+)(?:;.+)?\s*$", result["stdout"])
    assert match, (
        f"expected bare jobid (or jobid;cluster) on stdout, "
        f"got: {result['stdout']!r}"
    )
    # Register the job for cleanup since --parsable output can't be parsed
    # by submit_job_sbatch.
    atf.properties["submitted-jobs"].append(int(match.group(1)))
