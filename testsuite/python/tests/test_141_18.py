############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that the ResumeProgram JSON gives the layout of each job."""

import json

import pytest

import atf

pytestmark = pytest.mark.slow

CLOUD_NODE = "node1"
LAYOUT = "tpu7x=2x2x4,foo=bar"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/sbatch",
        reason="Issue 51191: --layout added in 26.11",
    )

    # SLURM_RESUME_FILE can only be read while ResumeProgram runs, so the
    # program keeps a copy where the test can read it.
    resume_program = f"{atf.module_tmp_path}/resume.sh"
    atf.make_bash_script(
        resume_program,
        f'cat "$SLURM_RESUME_FILE" > {resume_copy()}.tmp\n'
        f"mv {resume_copy()}.tmp {resume_copy()}\n",
    )
    atf.require_config_parameter("ResumeProgram", resume_program)
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("SuspendTime", 10)
    atf.require_config_parameter("SuspendTimeout", 10)
    atf.require_config_parameter("ResumeTimeout", 10)
    atf.require_config_parameter("NodeName", {CLOUD_NODE: {"State": "CLOUD"}})
    atf.require_config_parameter("PartitionName", {"cloud": {"Nodes": CLOUD_NODE}})

    # The cloud node is never started, so ResumeProgram runs for every job.
    atf.start_slurmctld(clean=True)

    yield

    atf.stop_slurmctld()


@pytest.fixture(scope="function", autouse=True)
def powered_down():
    """Start each case with the cloud node powered down and no resume copy."""
    atf.run_command(f"rm -f {resume_copy()}", user="root", fatal=True, quiet=True)
    yield
    atf.restart_slurmctld(clean=True)


def resume_copy():
    return f"{atf.module_tmp_path}/resume.json"


def resumed_job(job_id):
    """The entry for the job in the JSON ResumeProgram was given."""
    assert atf.wait_for_file(
        resume_copy()
    ), "ResumeProgram should run for a job on a powered down node"
    resume = json.loads(atf.run_command_output(f"cat {resume_copy()}", fatal=True))
    jobs = [job for job in resume["jobs"] if job["job_id"] == job_id]
    assert len(jobs) == 1, f"Expected job {job_id} once in the resume JSON: {resume}"
    return jobs[0]


@pytest.mark.parametrize(
    "layout_arg,expected",
    [(f"--layout={LAYOUT}", LAYOUT), ("", None)],
    ids=["layout", "no_layout"],
)
def test_layout_in_resume_file(layout_arg, expected):
    """The layout of a job, or null without one, is in SLURM_RESUME_FILE."""

    job_id = atf.submit_job_sbatch(f"-p cloud {layout_arg} --wrap true", fatal=True)
    job = resumed_job(job_id)

    assert "layout" in job, f"The resume JSON should have a layout key: {job}"
    assert (
        job["layout"] == expected
    ), f"Expected layout {expected!r} for job {job_id}, got {job['layout']!r}"
