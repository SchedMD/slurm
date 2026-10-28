############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""The Epilog environment reports a job's exit status and Slurm return code.

An Epilog receives SLURM_JOB_EXIT_CODE (the raw wait(2) status),
SLURM_JOB_EXIT_CODE2 (the same value as <status>:<signal>) and SLURM_JOB_RC
(the Slurm error code, or 0). A job Slurm failed must arrive with a wait(2)
status of 1:0 and the error code only in SLURM_JOB_RC.

Both Epilog and EpilogSlurmctld are covered: the slurmd Epilog is fed from
the kill job RPC and EpilogSlurmctld from job_common_env_vars(), so one is
not evidence for the other.

Each hook writes its environment to a file the test reads back, since they
run as SlurmdUser and SlurmUser rather than as the test user.
"""

import pathlib
import re

import pytest

import atf

# The two hooks that receive the variable, per prolog_epilog.shtml.
EPILOG_PARAMETERS = ["Epilog", "EpilogSlurmctld"]

# A working directory the stepd cannot open a job's output file in, which is
# how these tests make Slurm fail a job before its script ever runs. Two
# properties are load-bearing:
#   - The job must use the DEFAULT output file. expand_stdio_fields() joins
#     this directory onto slurm-%j.out when the step record is built, so the
#     stepd opens an absolute path under it. An explicit --output would
#     disarm the primitive and the job would simply run.
#   - The directory must be one the stepd cannot CREATE, not merely one that
#     is absent: on ENOENT it calls mkdirpath() and retries the open, so a
#     missing directory under a writable parent is created and the job runs.
#     Nesting it under /dev/null gives ENOTDIR instead, on every node and
#     whichever user the job runs as.
UNUSABLE_WORK_DIR = "/dev/null/slurm_test_workdir"


@pytest.fixture(scope="module", autouse=True)
def setup(epilog_env_dir):
    # Both hooks are configured BEFORE the daemons start. Setting them
    # afterwards only triggers `scontrol reconfigure`, which the controller
    # acknowledges before the slurmds have re-read their config - so the
    # first job could finish before slurmd knew it had an Epilog to run.
    for parameter in EPILOG_PARAMETERS:
        script = f"{epilog_env_dir}/{parameter}.sh"
        atf.make_bash_script(
            script,
            f'echo "rc=${{SLURM_JOB_RC-UNSET}}'
            f" ec=${{SLURM_JOB_EXIT_CODE-UNSET}}"
            f' ec2=${{SLURM_JOB_EXIT_CODE2-UNSET}}"'
            f" > {epilog_env_dir}/{parameter}_$SLURM_JOB_ID\n",
        )
        atf.set_config_parameter(parameter, script)

    atf.require_slurm_running()

    yield

    # ATF restores the configuration at module teardown, so this is only to
    # stop the daemons running these scripts any earlier than that.
    for parameter in EPILOG_PARAMETERS:
        atf.set_config_parameter(parameter, None)


@pytest.fixture(scope="module")
def epilog_env_dir():
    """Directory the hooks drop one file per job and parameter into.

    Tests already run inside an isolated working directory, so this is just a
    subdirectory of it. The Epilog runs as SlurmdUser and EpilogSlurmctld as
    SlurmUser, so the directory has to be writable by another account, and
    the absolute path is what gets baked into the scripts.
    """
    path = pathlib.Path("epilog_env")
    path.mkdir(exist_ok=True)
    path.chmod(0o777)
    yield str(path.absolute())


# SLURM_JOB_RC is exported by slurmd for the Epilog and by slurmctld for
# EpilogSlurmctld. slurmctld is never older than slurmd under Slurm's upgrade
# order, so gating on slurmd covers both.
requires_job_rc = pytest.mark.skipif(
    atf.get_version("sbin/slurmd") < (26, 11),
    reason="Ticket 24195: SLURM_JOB_RC was added in 26.11",
)


@requires_job_rc
@pytest.mark.parametrize("parameter", EPILOG_PARAMETERS)
@pytest.mark.parametrize(
    "wrap, expected_env",
    [
        # SLURM_JOB_EXIT_CODE is the raw wait(2) status, so exit 3 is 3 << 8.
        ("exit 0", "rc=0 ec=0 ec2=0:0"),
        ("exit 3", "rc=0 ec=768 ec2=3:0"),
    ],
)
def test_epilog_sees_rc_zero_for_a_job_that_ended_on_its_own(
    epilog_env_dir, parameter, wrap, expected_env
):
    """Verify SLURM_JOB_RC is 0 when Slurm did not fail the job"""

    job_id = atf.submit_job_sbatch(f"-N1 --no-requeue --wrap '{wrap}'", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    atf.assert_file_contents(
        f"{epilog_env_dir}/{parameter}_{job_id}",
        expected_env,
        message=(
            f"A job running '{wrap}' ended on its own, so {parameter} should "
            f"see '{expected_env}'. A non-zero rc means the exit status leaked "
            "into the return code."
        ),
    )


@requires_job_rc
@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="Ticket 24195: scontrol reports SlurmRC since 26.11",
)
@pytest.mark.parametrize("parameter", EPILOG_PARAMETERS)
def test_epilog_sees_the_error_code_only_in_rc(epilog_env_dir, parameter):
    """Verify a Slurm failure reaches SLURM_JOB_RC and not the exit code

    The job's working directory cannot be used, so the stepd fails while
    opening its output file and the script never runs.
    """

    job_id = atf.submit_job_sbatch(
        f"-N1 --no-requeue --chdir={UNUSABLE_WORK_DIR} --wrap 'exit 0'", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    env_file = f"{epilog_env_dir}/{parameter}_{job_id}"

    # 256 is W_EXITCODE(1, 0): a genuine wait(2) status meaning "exited 1".
    # A four-digit value here would be the Slurm error code leaking into the
    # exit code.
    atf.assert_file_contents(
        env_file,
        "ec=256 ec2=1:0",
        contains=True,
        message=(
            f"A job Slurm failed should reach {parameter} with a real wait(2) "
            "status of 1:0, not a value derived from the error code"
        ),
    )

    details = atf.run_command_output(
        f"scontrol show job --details {job_id}", fatal=True
    )
    match = re.search(r"\bSlurmRC=(\d+)", details)
    assert match, f"scontrol --details should print SlurmRC: {details}"
    job_rc = match.group(1)
    assert job_rc != "0", f"A job Slurm failed should have a SlurmRC: {details}"

    contents = atf.run_command_output(f"cat {env_file}", fatal=True).strip()
    assert contents.split()[0] == f"rc={job_rc}", (
        f"{parameter} should see the job's SlurmRC, {job_rc}, in SLURM_JOB_RC; "
        f"it saw {contents}"
    )
