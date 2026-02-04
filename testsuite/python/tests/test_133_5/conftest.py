##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
"""Shared fixtures and helpers for the test_133_5* federation requeue tests.

Fixtures defined here are auto-discovered by pytest for any test in this
directory - tests do NOT need to import them. Helpers and constants are
imported explicitly with `from .conftest import ...`.
"""
import re

import pytest

import atf

fed_name = "fed"
# The names of the clusters, set by setup(). It also sets them in the test
# modules, which import them from here and so have these placeholders.
cls1 = cls2 = cls3 = None

# Module-private state, mutated inside federation_setup. Tests should
# request the `node_count` fixture (below) rather than importing this
# directly - direct imports would freeze the value at import time.
_user_name = None
_node_count = 1


@pytest.fixture(scope="module", autouse=True)
def setup(request):
    """Test setup with required configurations."""
    atf.require_version(
        (25, 11),
        "sbin/slurmctld",
        reason="Ticket 25240: SchedulerParameters=requeue_delay was added in 25.11",
    )
    global cls1, cls2, cls3
    cls1, cls2, cls3 = atf.require_multicluster(3, federation=True)
    for name, value in (("cls1", cls1), ("cls2", cls2), ("cls3", cls3)):
        if hasattr(request.module, name):
            setattr(request.module, name, value)
    # A module may ask for RequeueExit and RequeueExitHold only in some clusters,
    # as a list with their numbers
    requeue_exit_clusters = getattr(request.module, "requeue_exit_clusters", None)
    for number, _ in enumerate(atf.foreach_cluster(), 1):
        atf.require_nodes(2)
        atf.require_config_parameter("MinJobAge", 60, ">=")
        if requeue_exit_clusters is None or number in requeue_exit_clusters:
            atf.require_config_parameter("RequeueExit", "3")
            atf.require_config_parameter("RequeueExitHold", "4")
        else:
            atf.require_config_parameter("RequeueExit", None)
            atf.require_config_parameter("RequeueExitHold", None)
        atf.require_config_parameter_includes("SchedulerParameters", "requeue_delay=0")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def federation_setup():
    """Module-level federation setup and cleanup."""
    global _user_name, _node_count

    _user_name = atf.get_user_name()

    # Use ALL available nodes
    output = atf.run_command_output(
        "sinfo -h --local -o '%D' -t idle,alloc,comp", fatal=True
    )
    _node_count = int(output.strip())
    if _node_count < 1:
        _node_count = 1

    cancel_federation_jobs()

    # Add cluster features to the ATF-created federation
    for cluster, feature in [(cls1, "fa"), (cls2, "fb"), (cls3, "fc")]:
        atf.run_command(
            f"sacctmgr -i modify cluster {cluster} set features={feature}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    atf.wait_for_federation_sync()

    yield

    cancel_federation_jobs()


@pytest.fixture(scope="module")
def node_count(federation_setup):
    """Expose _node_count (set in federation_setup) as a fixture.

    Depends on federation_setup so the value is populated before any
    test reads it.
    """
    return _node_count


@pytest.fixture(scope="module")
def scripts():
    """Create all test scripts once at module level."""
    scripts = {
        "requeue_exit": atf.module_tmp_path / "requeue_exit_script",
        "requeue_exithold": atf.module_tmp_path / "requeue_exithold_script",
        "sleep_long": atf.module_tmp_path / "sleep_long",
        "sleep_short": atf.module_tmp_path / "sleep_short",
        "complete_script": atf.module_tmp_path / "complete_script",
        "fail_script": atf.module_tmp_path / "fail_script",
        "restart_counter": atf.module_tmp_path / "restart_counter",
    }

    # Exit with the RequeueExit code only in the first run, so the job is
    # requeued once instead of in a loop
    atf.make_bash_script(
        scripts["requeue_exit"],
        'env; sleep 10; [ "${SLURM_RESTART_COUNT:-0}" -eq 0 ] && exit 3; exit 0',
    )
    atf.make_bash_script(scripts["requeue_exithold"], "env; sleep 10; exit 4")
    atf.make_bash_script(scripts["sleep_long"], "sleep infinity")
    atf.make_bash_script(scripts["sleep_short"], "sleep 5")
    atf.make_bash_script(scripts["complete_script"], "env; sleep 25")
    atf.make_bash_script(scripts["fail_script"], "BadCommand")
    atf.make_bash_script(
        scripts["restart_counter"],
        "echo SLURM_RESTART_COUNT=$SLURM_RESTART_COUNT; sleep infinity",
    )

    return scripts


@pytest.fixture(scope="module")
def output_files():
    """Create output file paths once at module level."""
    return {
        "requeue_exit_output": atf.module_tmp_path / "requeue_exit_output",
        "requeue_exithold_output": atf.module_tmp_path / "requeue_exithold_output",
    }


@pytest.fixture(scope="function")
def cleanup():
    """Function-level cleanup for individual tests."""
    yield
    cancel_federation_jobs()


def cancel_federation_jobs():
    """Cancel all federation jobs for the test user."""
    if _user_name:
        atf.run_command(f"scancel -M{cls1},{cls2},{cls3} --user {_user_name}")
        atf.repeat_until(
            lambda: atf.run_command_output(
                f"squeue -M{cls1},{cls2},{cls3} --user {_user_name} --noheader",
            ),
            lambda output: output.strip() == "",
            timeout=10,
        )


def wait_for_running_cluster(job_id, clusters=None):
    """Wait for the job to run on a cluster and return it."""
    atf.wait_for_job_state(
        job_id, "RUNNING", siblings=True, clusters=clusters, fatal=True
    )
    running = atf.get_job_clusters(job_id, "RUNNING", clusters=clusters)
    assert len(running) == 1, f"Job {job_id} should run on one cluster: {running}"
    return running[0]


def check_hold(job_id):
    """Verify job is in requeuehold state (Priority=0 + Reason=job_requeued_in_held_state)."""

    def check():
        output = atf.run_command_output(f"scontrol -a --local show job {job_id}")
        return re.search(r"Priority=0", output) and re.search(
            r"Reason=job_requeued_in_held_state", output
        )

    assert atf.repeat_until(
        check, lambda x: x, timeout=30
    ), f"Priority was not 0 or reason was not job_requeued_in_held_state for job {job_id}"


def check_exit_hold(job_id):
    """Verify job is in exit held state (Priority=0 + Reason=JobHeldUser)."""

    def check():
        output = atf.run_command_output(f"scontrol -a --local show job {job_id}")
        return re.search(r"Priority=0", output) and re.search(
            r"Reason=JobHeldUser", output
        )

    assert atf.repeat_until(
        check, lambda x: x, timeout=30
    ), f"Priority was not set to 0 or reason was not JobHeldUser for job {job_id}"


def check_dbd_states(job_id, cluster, expected_states):
    """Verify job states in sacct on a specific cluster.

    expected_states is a list of state strings that should all appear.
    Uses --duplicates to ensure all historical records are visible.
    """

    def check():
        output = atf.run_command_output(
            f"sacct -o state -j {job_id} -M{cluster} --duplicates --parsable2 --noheader",
        )
        return all(state in output for state in expected_states)

    assert atf.repeat_until(
        check, lambda x: x, timeout=120
    ), f"Job {job_id} on {cluster} missing expected sacct states: {expected_states}"


def check_ctl_state(job_id, cluster, expected_state):
    """Verify job state via scontrol on a specific cluster."""

    def check():
        output = atf.run_command_output(
            f"scontrol -M{cluster} -a --local show job {job_id}",
        )
        return re.search(rf"JobState={expected_state}", output) is not None

    assert atf.repeat_until(
        check, lambda x: x, timeout=90
    ), f"Job {job_id} not in state {expected_state} on {cluster}"


def check_restart_count_in_output(output_file, expected_count):
    """Read job output file and verify SLURM_RESTART_COUNT=N.

    Uses a generous timeout since jobs on sibling clusters may take
    time to write output to shared storage.
    """

    def check():
        try:
            content = output_file.read_text()
        except (FileNotFoundError, OSError):
            return False
        return f"SLURM_RESTART_COUNT={expected_count}" in content

    assert atf.repeat_until(
        check, lambda x: x, timeout=60
    ), f"Expected SLURM_RESTART_COUNT={expected_count} in {output_file}"


def assert_restarts_count(job_id, expected_count):
    """Verify Restarts=N in scontrol output for a job."""

    def check():
        output = atf.run_command_output(f"scontrol -a show job {job_id}")
        return re.search(rf"Restarts={expected_count}", output) is not None

    assert atf.repeat_until(
        check, lambda x: x, timeout=90
    ), f"Expected Restarts={expected_count} in scontrol output for job {job_id}"


def wait_for_revoked_on_origin(job_id):
    """Wait for origin cluster (cls1) to show REVOKED state for a job."""

    def check():
        output = atf.run_command_output(
            f"scontrol -M{cls1} -a --local show job {job_id}"
        )
        return re.search(r"REVOKED", output) is not None

    assert atf.repeat_until(
        check, lambda x: x, timeout=15
    ), f"Origin cluster {cls1} did not show REVOKED state for job {job_id}"


def wait_for_origin_ack_cancel(job_id, origin=None, timeout=30):
    """After scancel of a federated job running on a sibling, wait for the
    origin's view to acknowledge the cancellation.

    Without this, under heavy host load the cls2->cls1 federation
    notification can lag, leaving the origin tracking job not cancelled yet.
    A subsequent scontrol requeue then routes back to the dead sibling
    record and the federation state goes off the rails (job stays
    REVOKED on cls1 until MinJobAge purge).

    The origin tracking job becomes CANCELLED+REVOKED, but scontrol prints it
    as just REVOKED. The json output reports both, the base state and the
    flags. The job may also have been purged already.

    The origin is cls1 by default.
    """
    if origin is None:
        origin = cls1
    for _ in atf.timer(timeout=timeout):
        jobs = atf.get_jobs(job_id, cluster=origin, use_json=True, quiet=True)
        if job_id not in jobs or any(
            atf.is_job_state_done(state) for state in jobs[job_id]["job_state"]
        ):
            return True
    return False


def wait_for_sacct_state(job_id, cluster, state, timeout=60):
    """Poll sacct until the given state appears for a job on a cluster."""

    def check():
        output = atf.run_command_output(
            f"sacct -o state -j {job_id} -M{cluster} --parsable2 --noheader",
        )
        return state in output

    assert atf.repeat_until(
        check, lambda x: x, timeout=timeout
    ), f"Job {job_id} did not reach {state} state on {cluster} within {timeout}s"


def submit_job_on_sibling(file_in, scripts, node_count):
    """Fill the origin, and submit a job that runs on a sibling."""
    fill_cluster(cls1, scripts)
    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --requeue -o /dev/null -e /dev/null {file_in}",
        fatal=True,
    )
    run_cluster = wait_for_running_cluster(job_id, f"{cls2},{cls3}")
    assert run_cluster in [cls2, cls3], "Job should run on sibling"
    return job_id, run_cluster


def fill_cluster(cluster, scripts):
    """Fill a cluster with an exclusive job. Returns job ID."""
    file_in = scripts["sleep_long"]
    job_id = atf.submit_job_sbatch(
        f"-N{_node_count} --exclusive --output=/dev/null --error=/dev/null"
        f" --requeue --cluster-constraint={'fa' if cluster == cls1 else 'fb' if cluster == cls2 else 'fc'} {file_in}",
        fatal=True,
    )
    c = wait_for_running_cluster(job_id)
    assert c == cluster, f"Fill job not running on {cluster}"
    return job_id
