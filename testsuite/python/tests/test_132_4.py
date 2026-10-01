############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify the job state when PrologSlurmctld runs before an Alloc Prolog.

With PrologSlurmctld the job is CONFIGURING before its nodes are told to run the
Prolog. With PrologFlags=Alloc it must then stay CONFIGURING while the node
Prolog runs, so the job only leaves CONFIGURING once both have finished. NoHold
opts out of the hold on the node Prolog, and an external job has no node Prolog
at all, so both of those must leave CONFIGURING as soon as PrologSlurmctld is
done.

Both scripts block on sentinel files rather than sleeping for a fixed time, so
each window being sampled stays open until the test releases it.
"""

import os

import pytest

import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 15001: CONFIGURING while the Prolog runs added in 26.11",
    )

    # PrologSlurmctld runs as SlurmUser on the controller and the Prolog as root
    # on the node, so both key their files off the job id in the module tmp
    # dir, which conftest makes reachable by every user.
    ctld_script = str(atf.module_tmp_path / "prolog_slurmctld.sh")
    atf.make_bash_script(
        ctld_script,
        f"""touch {atf.module_tmp_path}/ctld_started.$SLURM_JOB_ID
while [ ! -f {atf.module_tmp_path}/ctld_proceed.$SLURM_JOB_ID ]; do
    sleep 0.5
done
touch {atf.module_tmp_path}/ctld_finished.$SLURM_JOB_ID
""",
    )

    prolog_script = str(atf.module_tmp_path / "prolog.sh")
    atf.make_bash_script(
        prolog_script,
        f"""touch {atf.module_tmp_path}/started.$SLURM_JOB_ID
while [ ! -f {atf.module_tmp_path}/proceed.$SLURM_JOB_ID ]; do
    sleep 0.5
done
""",
    )

    # enable_stepmgr is only supported with PrologFlags=contain, and these cases
    # set other flags.
    atf.require_config_parameter_excludes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter("PrologSlurmctld", ctld_script)
    atf.require_config_parameter("Prolog", prolog_script)
    atf.require_slurm_running()


@pytest.fixture
def release_scripts():
    """Release both scripts for every job a case submitted.

    The scripts loop until released, so this keeps a failing case from leaving
    them running into the next one. Only this case's jobs are released:
    submitted-jobs also holds ids from earlier modules in the session, which
    later jobs here can reuse.
    """

    first = len(atf.properties["submitted-jobs"])
    yield
    for job_id in atf.properties["submitted-jobs"][first:]:
        for marker in (f"ctld_proceed.{job_id}", f"proceed.{job_id}"):
            atf.run_command(f"touch {atf.module_tmp_path / marker}", quiet=True)


def touch(name):
    atf.run_command(f"touch {atf.module_tmp_path / name}", fatal=True)


def job_state(job_id):
    return atf.get_job_parameter(job_id, "JobState", default="NOT_FOUND", quiet=True)


def wait_for_marker(name):
    assert atf.wait_for_file(
        str(atf.module_tmp_path / name)
    ), f"{name} was never created"


def assert_stays(job_id, expected, reason):
    """Assert the job stays in expected for a short window.

    Used once the event that could wrongly change the state has happened, so a
    regression shows up as the state changing inside the window.
    """

    # A window to watch, not a wait for success, so the 45 second default would
    # only slow the case down. Any wrong transition happens within seconds.
    for _ in atf.timer(timeout=15, xfail=True, quiet=True):
        state = job_state(job_id)
        if state != expected:
            pytest.fail(f"{reason}: job {job_id} went to {state}")


@pytest.mark.parametrize(
    "prolog_flags,expect_configuring",
    [
        pytest.param("Alloc", True, id="alloc"),
        pytest.param("Alloc,NoHold", False, id="alloc-nohold"),
    ],
)
def test_prolog_after_prolog_slurmctld(
    release_scripts, tmp_path, prolog_flags, expect_configuring
):
    """Verify the node Prolog still holds the job after PrologSlurmctld.

    The job is CONFIGURING while PrologSlurmctld runs and its node Prolog has
    not been launched yet. Once PrologSlurmctld finishes, the node Prolog is
    launched, and the job must stay CONFIGURING until that is done too, unless
    NoHold is set.
    """

    output_file = str(tmp_path / "job_output")

    atf.set_config_parameter("PrologFlags", prolog_flags)

    job_id = atf.submit_job_sbatch(
        f"-N1 -t5 --wrap 'touch {output_file}; sleep 30'", fatal=True
    )

    wait_for_marker(f"ctld_started.{job_id}")
    assert (
        job_state(job_id) == "CONFIGURING"
    ), "Job was not CONFIGURING while PrologSlurmctld ran"
    assert not os.path.isfile(
        atf.module_tmp_path / f"started.{job_id}"
    ), "The node Prolog started before PrologSlurmctld finished"

    touch(f"ctld_proceed.{job_id}")
    wait_for_marker(f"ctld_finished.{job_id}")
    wait_for_marker(f"started.{job_id}")

    if expect_configuring:
        assert_stays(
            job_id,
            "CONFIGURING",
            "Job left CONFIGURING before its node Prolog finished",
        )
        assert not os.path.isfile(
            output_file
        ), "The batch script ran before the node Prolog finished"
        touch(f"proceed.{job_id}")
    else:
        # With NoHold the job is not held for the node Prolog. slurmctld
        # finishes configuring the job before it sends the Prolog launch
        # request, so the job is already RUNNING once that Prolog has started.
        # Sample it at once rather than waiting for RUNNING, which the periodic
        # time limit pass would also eventually produce.
        state = job_state(job_id)
        assert (
            state == "RUNNING"
        ), f"Expected RUNNING while the NoHold node Prolog runs, got {state}"
        touch(f"proceed.{job_id}")

    assert atf.wait_for_job_state(job_id, "RUNNING"), "Job never reached RUNNING"
    assert atf.wait_for_file(output_file), "Batch step never produced output"


def test_external_job_after_prolog_slurmctld(release_scripts):
    """Verify an external job leaves CONFIGURING after PrologSlurmctld.

    An external job gets no node Prolog, so once PrologSlurmctld finishes there
    is nothing left to hold it in CONFIGURING. It must also still end at its
    time limit.
    """

    atf.set_config_parameter("PrologFlags", "Alloc")

    job_id = atf.submit_job_sbatch("--external -N1 -t1", fatal=True)

    wait_for_marker(f"ctld_started.{job_id}")
    assert (
        job_state(job_id) == "CONFIGURING"
    ), "External job was not CONFIGURING while PrologSlurmctld ran"

    touch(f"ctld_proceed.{job_id}")
    wait_for_marker(f"ctld_finished.{job_id}")

    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "External job never left CONFIGURING after PrologSlurmctld"
    assert not os.path.isfile(
        atf.module_tmp_path / f"started.{job_id}"
    ), "A node Prolog ran for an external job"

    # Longer than the default: the job has a one minute limit, and slurmctld only
    # checks time limits every 30 seconds.
    assert atf.wait_for_job_state(
        job_id, "TIMEOUT", timeout=150
    ), "External job did not end at its time limit"
