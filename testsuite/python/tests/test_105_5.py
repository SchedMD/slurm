############################################################################
# Copyright (C) SchedMD LLC.
############################################################################

import atf
import os
import pytest
import re

user_name = atf.get_user_name()


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


test_combinations = (
    # in_file, out_file, err_file, out_env, err_env, expect_files, out_result, err_result
    (None, None, None, None, None, True, "slurm-%j.out", "slurm-%j.out"),
    (None, "output", "error", None, None, True, "output", "error"),
    (None, "none", "none", None, None, False, "slurm-%j.out", "slurm-%j.err"),
    (
        None,
        "none",
        "none",
        "slurm-%j.out",
        "slurm-%j.err",
        False,
        "slurm-%j.out",
        "slurm-%j.err",
    ),
    ("input.%%", "output.%%", "error.%%", None, None, True, "output.%%", "error.%%"),
    ("input.%%", None, None, "output.%%", "error.%%", True, "output.%%", "error.%%"),
    (
        "input.j.%j",
        "output.j.%j",
        "error.j.%j",
        None,
        None,
        True,
        "output.j.%j",
        "error.j.%j",
    ),
    (
        "input.j.%j",
        None,
        None,
        "output.j.%j",
        "error.j.%j",
        True,
        "output.j.%j",
        "error.j.%j",
    ),
    (
        "input.u.%u",
        "output.u.%u",
        "error.u.%u",
        None,
        None,
        True,
        "output.u.%u",
        "error.u.%u",
    ),
    (
        "input.u.%u",
        None,
        None,
        "output.u.%u",
        "error.u.%u",
        True,
        "output.u.%u",
        "error.u.%u",
    ),
    (
        r"input\ spaces",
        r"output\ spaces",
        r"error\ spaces",
        None,
        None,
        True,
        r"output\ spaces",
        r"error\ spaces",
    ),
    (
        r"input\ spaces",
        None,
        None,
        r"output\ spaces",
        r"error\ spaces",
        True,
        r"output\ spaces",
        r"error\ spaces",
    ),
)


@pytest.mark.parametrize(
    "in_file,out_file,err_file,out_env,err_env,expect_files,out_result,err_result",
    test_combinations,
)
def test_file_io_options(
    in_file,
    out_file,
    err_file,
    out_env,
    err_env,
    expect_files,
    out_result,
    err_result,
):
    """Create jobs and check if expected output and error files exist.

    Args:
        in_file (string/Path): Desired path of job script. If None, default from file_in() will be used.
        out_file (string/Path): Value passed to "--output" of sbatch. Sbatch argument only used if value is given.
        err_file (string/Path): Value passed to "--error" of sbatch. Sbatch argument only used if value is given.
        out_env (string/Path): Value to set environmental variable "SBATCH_OUTPUT" to. "SBATCH_OUTPUT" is unset if no value is given.
        err_env (string/Path): Value to set environmental variable "SBATCH_ERROR" to. "SBATCH_ERROR" is unset if no value is given.
        expect_files (boolean): If True, tests files were created. If False, tests that files are never created.
        out_result (string/Path): Expected path to output file. File existence only tested if path is given.
        err_result (string/Path): Expected path to error file. File existence only tested if path is given.

    Returns:
        The job id of the sbatch job submitted.
    """

    # Build job script that will make content for both output and error files
    atf.make_bash_script(
        "original_in_file",
        f"""
        id
        sleep aaa
        """,
    )

    # Rename job script as necessary
    if in_file is None:
        in_file = "original_in_file"
    elif in_file != "original_in_file":
        atf.run_command(f"mv original_in_file {in_file}", fatal=True)

    # Create environment variables variable appropriately
    env_vars = ""
    if out_env:
        env_vars += f"SBATCH_OUTPUT={out_env} "
    if err_env:
        env_vars += f"SBATCH_ERROR={err_env} "

    # Submit sbatch job with appropriate arguments
    job_args = ""
    if out_file:
        job_args += f" --output={out_file}"
    if err_file:
        job_args += f" --error={err_file}"
    job_id = atf.submit_job_sbatch(
        f"{job_args} -t1 -N1 {in_file}", env_vars=env_vars, fatal=True
    )

    # Wait for job to complete
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    # Make substitutions in result file names, exactly as we expect Slurm to
    if out_result:
        out_result = str(out_result).replace("%j", str(job_id))
        out_result = out_result.replace("%u", user_name)
        out_result = out_result.replace("%%", "%")
        out_result = out_result.replace(r"\ ", " ")
    if err_result:
        err_result = str(err_result).replace("%j", str(job_id))
        err_result = err_result.replace("%u", user_name)
        err_result = err_result.replace("%%", "%")
        err_result = err_result.replace(r"\ ", " ")

    # Check files were created or not, and their contents
    if expect_files:
        if out_result:
            assert atf.wait_for_file(
                out_result
            ), f"Output file {out_result} should be created"

            output = atf.run_command_output(f'cat "{out_result}"', fatal=True)
            assert (
                len(re.findall(r"uid=", output)) == 1
            ), "Couldn't find 'uid=' in output from 'id' call"
        if err_result:
            assert atf.wait_for_file(
                err_result
            ), f"Error file {err_result} should be created"

            errors = atf.run_command_output(f'cat "{err_result}"', fatal=True)
            assert (
                len(re.findall(r"invalid time interval", errors)) == 1
            ), "Couldn't find error in error file from invalid sleep command ('sleep aaa')"
    else:
        if out_result:
            assert not atf.wait_for_file(
                out_result, timeout=5, xfail=True
            ), f"Output file {out_result} should not be created"
        if err_result:
            assert not atf.wait_for_file(
                err_result, timeout=5, xfail=True
            ), f"Error file {err_result} should not be created"
