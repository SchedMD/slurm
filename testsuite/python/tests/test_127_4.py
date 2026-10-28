############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A RunInJob Epilog gets none of the job exit variables.

With PrologFlags=RunInJob, slurmstepd runs the Epilog and does not know how
the job ended, so prolog_epilog.shtml says SLURM_JOB_EXIT_CODE,
SLURM_JOB_EXIT_CODE2, SLURM_JOB_DERIVED_EC and SLURM_JOB_RC are not set
there. EpilogSlurmctld still gets them, which shows the job did end with a
status the Epilog could have been told about.
"""

import pathlib

import pytest

import atf

EPILOG_PARAMETERS = ["Epilog", "EpilogSlurmctld"]


@pytest.fixture(scope="module", autouse=True)
def setup(epilog_env_dir):
    # Configured before the daemons start, as in test_127_3.
    atf.require_config_parameter_includes("PrologFlags", "RunInJob")
    for parameter in EPILOG_PARAMETERS:
        script = f"{epilog_env_dir}/{parameter}.sh"
        atf.make_bash_script(
            script,
            f'echo "rc=${{SLURM_JOB_RC-UNSET}}'
            f" ec=${{SLURM_JOB_EXIT_CODE-UNSET}}"
            f" ec2=${{SLURM_JOB_EXIT_CODE2-UNSET}}"
            f' dec=${{SLURM_JOB_DERIVED_EC-UNSET}}"'
            f" > {epilog_env_dir}/{parameter}_$SLURM_JOB_ID\n",
        )
        # set_config_parameter, since require_config_parameter lowercases
        # the value and so the script path.
        atf.set_config_parameter(parameter, script)

    atf.require_slurm_running()


@pytest.fixture(scope="module")
def epilog_env_dir():
    """Directory the hooks write to. They run as other users, so it is
    world-writable, and the absolute path is baked into the scripts."""
    path = pathlib.Path("epilog_env")
    path.mkdir(exist_ok=True)
    path.chmod(0o777)
    yield str(path.absolute())


@pytest.mark.skipif(
    atf.get_version("sbin/slurmd") < (26, 11),
    reason="Ticket 24195: a RunInJob Epilog got the job exit variables as 0 "
    "before 26.11",
)
@pytest.mark.parametrize(
    "parameter, expected_env",
    [
        ("Epilog", "rc=UNSET ec=UNSET ec2=UNSET dec=UNSET"),
        # SLURM_JOB_EXIT_CODE is the raw wait(2) status, so exit 3 is 3 << 8.
        ("EpilogSlurmctld", "rc=0 ec=768 ec2=3:0 dec=0"),
    ],
)
def test_runinjob_epilog_gets_no_job_exit_variables(
    epilog_env_dir, parameter, expected_env
):
    """Verify a RunInJob Epilog gets no job exit variables, and
    EpilogSlurmctld still does"""

    job_id = atf.submit_job_sbatch("-N1 --no-requeue --wrap 'exit 3'", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    atf.assert_file_contents(
        f"{epilog_env_dir}/{parameter}_{job_id}",
        expected_env,
        message=f"{parameter} under PrologFlags=RunInJob should see '{expected_env}'",
    )
