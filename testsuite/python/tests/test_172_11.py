############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test jobcomp/script running JobCompLoc once for each finished job."""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    # slurmctld runs the script from another directory, so use absolute paths
    script = atf.module_tmp_path / "jobcomp.sh"
    atf.make_bash_script(
        script,
        f"""umask 022
env > {atf.module_tmp_path}/$$.tmp
mv {atf.module_tmp_path}/$$.tmp {atf.module_tmp_path}/$$.env
""",
    )

    atf.require_config_parameter("JobCompType", "jobcomp/script")
    atf.require_config_parameter("JobCompLoc", str(script))
    # Job ids too long to match the small numbers in the environment
    atf.require_config_parameter("FirstJobId", 1000000)
    # One node for a running job and one for the jobs that finish
    atf.require_nodes(2)
    atf.require_slurm_running()


def run_job(name, command="true"):
    """Run a batch job until it is done and return its id."""
    job_id = atf.submit_job_sbatch(
        f"-J {name} -o /dev/null --wrap '{command}'", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    return job_id


def get_runs(name):
    """Return the environment values of each script run for the job name."""
    output = atf.run_command_output(f"grep -H '' {atf.module_tmp_path}/*.env")
    runs = {}
    for line in output.splitlines():
        path, _, variable = line.partition(":")
        runs.setdefault(path, set()).add(variable.partition("=")[2])
    return [values for values in runs.values() if name in values]


def test_script_runs_once_per_job():
    """JobCompLoc runs once per finished job only, with the job in its environment."""
    held_id = atf.submit_job_sbatch(
        "-H -J jobcomp_held -o /dev/null --wrap true", fatal=True
    )
    running_id = atf.submit_job_sbatch(
        "-J jobcomp_running -o /dev/null --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(running_id, "RUNNING", fatal=True)
    ok_id = run_job("jobcomp_ok")
    failed_id = run_job("jobcomp_failed", "exit 3")

    for _ in atf.timer():
        ok_runs = get_runs("jobcomp_ok")
        failed_runs = get_runs("jobcomp_failed")
        if ok_runs and failed_runs:
            break

    assert len(ok_runs) == 1, f"The script should run once for job {ok_id}"
    assert str(ok_id) in ok_runs[0], f"The environment should have job id {ok_id}"
    assert len(failed_runs) == 1, f"The script should run once for job {failed_id}"
    assert (
        str(failed_id) in failed_runs[0]
    ), f"The environment should have job id {failed_id}"
    assert not get_runs(
        "jobcomp_held"
    ), f"The script should not run for pending job {held_id}"
    assert not get_runs(
        "jobcomp_running"
    ), f"The script should not run for running job {running_id}"
