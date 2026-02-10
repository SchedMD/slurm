##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
"""Shared fixtures and helpers for the test_133_17* federation dependency tests.

Fixtures defined here are auto-discovered by pytest for any test in this
directory - tests do NOT need to import them. Helpers and constants are
imported explicitly with `from .conftest import ...`.
"""
import itertools
import pathlib
import re

import pytest

import atf

fed_name = "fed"
# The names of the clusters, set by setup(). It also sets them in the test
# modules, which import them from here and so have these placeholders.
cls1 = cls2 = cls3 = None
job_name = "test_133_17_job"

# Module-private state, mutated inside federation_setup. Tests should
# request the fixtures below rather than importing these directly -
# direct imports would freeze the value at import time.
_file_in_long = None
_file_in_hold = None
_release_flags = {}
_release_counter = itertools.count()


@pytest.fixture(scope="module", autouse=True)
def setup(request):
    global cls1, cls2, cls3
    cls1, cls2, cls3 = atf.require_multicluster(3, federation=True)
    for name, value in (("cls1", cls1), ("cls2", cls2), ("cls3", cls3)):
        if hasattr(request.module, name):
            setattr(request.module, name, value)
    for _ in atf.foreach_cluster():
        atf.require_nodes(4)
        # A module may ask for DependencyParameters, as a list of values
        for value in getattr(request.module, "dependency_parameters", []):
            atf.require_config_parameter_includes("DependencyParameters", value)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def federation_setup():
    global _file_in_long, _file_in_hold

    _file_in_long = str(atf.module_tmp_path / "long.sh")
    _file_in_hold = str(atf.module_tmp_path / "hold.sh")
    atf.make_bash_script(_file_in_long, "sleep infinity")
    atf.make_bash_script(_file_in_hold, "while [ ! -e $1 ]; do sleep 0.1; done")

    cancel_federation_jobs()
    for cluster, feature in [(cls1, "fa"), (cls2, "fb"), (cls3, "fc")]:
        atf.run_command(
            f"sacctmgr -i modify cluster {cluster} set features={feature}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    atf.wait_for_federation_sync()

    cancel_federation_jobs()

    yield

    cancel_federation_jobs()


@pytest.fixture(scope="module")
def file_in_long(federation_setup):
    return _file_in_long


def cancel_federation_jobs():
    atf.run_command(
        f"scancel -M{cls1},{cls2},{cls3} --jobname {job_name}",
    )


def submit_job(options, cluster, file_in, name=job_name):
    cls_num = int(cluster[-1])
    atf.select_cluster(cls_num)
    job_id = atf.submit_job_sbatch(
        f"--job-name={name} {options} --output=/dev/null {file_in}",
        fatal=True,
    )
    atf.select_cluster(1)
    return job_id


def submit_hold_job(options, cluster):
    """Submit a job that keeps running until release_job() is called for it,
    so the test decides when it completes OK."""
    release_file = str(atf.module_tmp_path / f"release_{next(_release_counter)}")
    job_id = submit_job(options, cluster, f"{_file_in_hold} {release_file}")
    _release_flags[job_id] = release_file
    return job_id


def release_job(job_id):
    pathlib.Path(_release_flags[job_id]).touch()


def get_job_dependency(job_id):
    result = atf.run_command(f"scontrol show job {job_id}")
    if result["exit_code"] != 0:
        return "", ""

    reason_match = re.search(r"Reason=([a-zA-Z_]+)", result["stdout"])
    depend_match = re.search(r"Dependency=(\S+)", result["stdout"])

    reason = reason_match.group(1) if reason_match else ""
    dependency = depend_match.group(1) if depend_match else ""

    return reason, dependency


def wait_for_depend(job_id, expected_reason, expected_dependency, timeout=120):
    def check():
        reason, dependency = get_job_dependency(job_id)
        if (
            expected_reason != "DependencyNeverSatisfied"
            and reason == "DependencyNeverSatisfied"
        ):
            pytest.fail(
                f"Job {job_id} dependency failed unexpectedly: "
                f"reason='{reason}', dependency='{dependency}'"
            )
        return reason == expected_reason and dependency == expected_dependency

    assert atf.repeat_until(check, lambda x: x, timeout=timeout, poll_interval=3), (
        f"Timeout waiting for job {job_id}: "
        f"expected reason='{expected_reason}', dependency='{expected_dependency}', "
        f"got {get_job_dependency(job_id)}"
    )


def assert_depend_unchanged(job_id, expected_reason, expected_dependency, timeout=10):
    """Check for a while that the dependency of the job does not change.

    It is the negative of wait_for_depend(), that is true at once if the state
    of the dependency is already the expected one.
    """
    for _ in atf.timer(timeout=timeout, poll_interval=2, xfail=True):
        assert get_job_dependency(job_id) == (
            expected_reason,
            expected_dependency,
        ), f"Job {job_id} dependency changed unexpectedly"


DONE_STATES = {
    "NOT_FOUND",
    "CANCELLED",
    "DEADLINE",
    "FAILED",
    "TIMEOUT",
    "NODE_FAIL",
    "PREEMPTED",
    "COMPLETED",
}


def _state_matches(actual_state, desired_state):
    if desired_state == "DONE":
        return actual_state in DONE_STATES
    return actual_state == desired_state


def my_wait_for_fed_job(job_id, state, cluster):
    def check():
        result = atf.run_command(
            f"scontrol -M{cluster} --local -a -o show job {job_id}",
        )
        if result["exit_code"] == 0:
            match = re.search(r"JobState=(\w+)", result["stdout"])
            if match and _state_matches(match.group(1), state):
                return True
        return False

    assert atf.repeat_until(
        check, lambda x: x, timeout=360
    ), f"Job {job_id} never reached state {state} on cluster {cluster}"


def wait_for_fed_job(job_id, expected_state, clusters=None, timeout=180):
    """Wait for job to reach expected state and return cluster.

    Uses scontrol -a --local so that completed/failed jobs are still
    visible during the polling window.
    """
    if clusters is None:
        cluster_list = [cls1, cls2, cls3]
    elif isinstance(clusters, str):
        cluster_list = clusters.split(",")
    else:
        cluster_list = clusters

    found_cluster = [""]

    def check_job_state():
        for cluster in cluster_list:
            result = atf.run_command(
                f"scontrol -M{cluster} -a --local show job {job_id}",
            )
            if result["exit_code"] == 0:
                match = re.search(r"JobState=(\w+)", result["stdout"])
                if match:
                    state = match.group(1)
                    if state == expected_state.upper():
                        found_cluster[0] = cluster
                        return True
                    elif expected_state.upper() == "DONE" and state in [
                        "COMPLETED",
                        "FAILED",
                        "CANCELLED",
                        "TIMEOUT",
                    ]:
                        found_cluster[0] = cluster
                        return True
            elif expected_state.upper() == "DONE" and "Invalid job id" in result.get(
                "stderr", ""
            ):
                # Job was purged (MinJobAge expired) - treat as done
                found_cluster[0] = cluster
                return True
        return False

    success = atf.repeat_until(check_job_state, lambda result: result, timeout=timeout)

    return found_cluster[0] if success else ""


def cancel_job(job_id, cluster):
    atf.run_command(f"scancel {job_id}")
    clusters = cluster.split(",") if isinstance(cluster, str) else cluster
    for cl in clusters:
        for _ in atf.timer(fatal=True):
            result = atf.run_command(
                f"scontrol -M{cl} --local -a -o show job {job_id}",
                quiet=True,
            )
            if "Invalid job id" in result.get("stderr", ""):
                break
            if result["exit_code"] == 0:
                match = re.search(r"JobState=(\w+)", result["stdout"])
                if match and _state_matches(match.group(1), "DONE"):
                    break


def is_job_on_cluster(job_id, cluster):
    result = atf.run_command(
        f"scontrol -M{cluster} --local -o show job {job_id}",
    )
    if result["exit_code"] == 0 and f"JobId={job_id}" in result["stdout"]:
        return True
    return False


def remove_cluster_from_fed(cluster):
    """Remove a cluster from the (otherwise full) federation and wait until all
    the controllers see the remaining members."""
    atf.run_command(
        f"sacctmgr -i modify federation {fed_name} set clusters-={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_federation_sync()


def add_cluster_to_fed(cluster):
    """Add back the only cluster that was removed from the federation and wait
    until all the clusters are connected again."""
    atf.run_command(
        f"sacctmgr -i modify federation {fed_name} set clusters+={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_federation_sync()
    atf.wait_for_federation_connected()
