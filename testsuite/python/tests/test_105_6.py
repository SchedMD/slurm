############################################################################
# Copyright (C) SchedMD LLC.
############################################################################

import atf
import os
import pytest
import re

file_in = "input"
job_ids = []

# Different argument and behavior combinations to test:
# args, scontrol, xfail, files.
testing_combinations = [
    (["--job-name='TEST_NAME'"], ["JobName=TEST_NAME"], [], []),
    (["-N1000000k"], [], ["More processors requested than permitted"], []),
    (["-N650000"], [], ["More processors requested than permitted"], []),
    ([r"-o 'spaces\ out'"], [], [], ["spaces out"]),
    ([r"-e 'spaces\ err'"], [], [], ["spaces err"]),
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    # We need jobs to fail if given crazy requirements
    atf.require_config_parameter("EnforcePartLimits", "ALL")
    atf.require_slurm_running()

    yield

    # Manually cancel job at the end because we're not using atf.submit_job_sbatch
    atf.cancel_jobs(job_ids)


# Create job script that includes each SBATCH directive in sbatch_args
def sbatch_script(sbatch_args):
    atf.make_bash_script(
        file_in, "\n".join([f"#SBATCH {s_arg}" for s_arg in sbatch_args]) + "\nid"
    )


# Test each set of SBATCH argument combinations and the expected behavior
@pytest.mark.parametrize("args,scontrol,xfail,files", testing_combinations)
def test_sbatch_directive(args, scontrol, xfail, files):
    # Create and submit job
    sbatch_script(args)
    submission_results = atf.run_command(f"sbatch {file_in}", xfail=xfail)

    # Extract the job id
    job_id_match = re.search(r"Submitted \S+ job (\d+)", submission_results["stdout"])
    job_id = int(job_id_match.group(1)) if job_id_match else 0
    job_ids.append(job_id)

    # Test appropriately if job submission failed
    if xfail:
        assert (
            job_id == 0
        ), f"Job submission was supposed to fail with sbatch args {args}"
        for fail_text in xfail:
            assert (
                fail_text in submission_results["stderr"]
            ), f"Text '{fail_text}' not found in stderr of sbatch command"
        return
    else:
        assert (
            job_id
        ), f"Job submission failed when not expected to with sbatch args {args}"

    # Test "scontrol show job" info matches expectations
    scontrol_info = atf.run_command_output(f"scontrol show job {job_id}", fatal=True)
    for job_property in scontrol:
        assert (
            job_property in scontrol_info
        ), f"'{job_property}' not found in scontrol show"

    # Test if files specified now exist
    for file in files:
        assert atf.wait_for_file(file), f"File {file} should be created"
