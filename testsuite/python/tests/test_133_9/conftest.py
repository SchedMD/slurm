##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
"""Shared fixtures and helpers for the test_133_9* federated client output tests.

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

# Module-private state, mutated inside federation_setup.
user_name = None
node_count = 10
fed_job_script_path = None


@pytest.fixture(scope="module", autouse=True)
def setup(request):
    global cls1, cls2, cls3
    cls1, cls2, cls3 = atf.require_multicluster(3, federation=True)
    for name, value in (("cls1", cls1), ("cls2", cls2), ("cls3", cls3)):
        if hasattr(request.module, name):
            setattr(request.module, name, value)
    for _ in atf.foreach_cluster():
        atf.require_nodes(10)
        # These tests check the federated view of the client commands
        atf.require_config_parameter_includes("FederationParameters", "fed_display")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def federation_setup():
    """Module-level federation setup."""
    global user_name, fed_job_script_path

    user_name = atf.get_user_name()

    # Create job script
    fed_job_script_path = atf.module_tmp_path / "fed_job_shared.sh"
    atf.make_bash_script(
        fed_job_script_path,
        "env\nsrun --overlap sleep infinity &\nsrun --overlap sleep infinity",
    )

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


@pytest.fixture(scope="function")
def cleanup():
    """Function-level cleanup."""
    yield
    cancel_federation_jobs()


def cancel_federation_jobs():
    """Cancel all federation jobs for the test user."""
    if user_name:
        atf.run_command(f"scancel -M{cls1},{cls2},{cls3} --user {user_name}")
        atf.repeat_until(
            lambda: atf.run_command(
                f"squeue -M{cls1},{cls2},{cls3} --user {user_name} --noheader",
            ),
            lambda result: result["stdout"].strip() == "",
            timeout=15,
        )


def submit_fed_job(cluster_suffix=""):
    """Submit a job to the federation. Returns job ID."""
    options = (
        f"-N{node_count} --exclusive --output=/dev/null --error=/dev/null --requeue"
    )
    if cluster_suffix:
        options += f" -M{cluster_suffix}"

    return atf.submit_job_sbatch(f"{options} {fed_job_script_path}", fatal=True)


def wait_for_running_cluster(job_id):
    """Wait for the job to run on a cluster and return it."""
    atf.wait_for_job_state(job_id, "RUNNING", siblings=True, fatal=True)
    running = atf.get_job_clusters(job_id, "RUNNING")
    assert len(running) == 1, f"Job {job_id} should run on one cluster: {running}"
    return running[0]


def run_squeue_test(options, expected_pattern):
    """Run squeue command and verify pattern."""
    result = atf.run_command_output(f"squeue --noheader {options}", fatal=True)
    assert (
        re.search(expected_pattern, result, re.DOTALL) is not None
    ), f"Pattern {expected_pattern} not found in squeue output:\n{result}"


def run_sinfo_test(options, expected_pattern):
    """Run sinfo command and verify pattern."""
    result = atf.run_command_output(f"sinfo --noheader {options}", fatal=True)
    assert (
        re.search(expected_pattern, result, re.DOTALL) is not None
    ), f"Pattern {expected_pattern} not found in sinfo output:\n{result}"


def run_sprio_test(options, expected_pattern, match_count=1):
    """Run sprio command and verify pattern."""
    result = atf.run_command_output(f"sprio --noheader {options}", fatal=True)
    matches = len(re.findall(expected_pattern, result))
    assert (
        matches == match_count
    ), f"Expected {match_count} matches of {expected_pattern}, got {matches}:\n{result}"


def run_scontrol_test(category, tasks, expected_pattern, option=""):
    """Run scontrol show command and verify pattern."""
    result = atf.run_command_output(f"scontrol {option} show {category}", fatal=True)
    filtered_result = "\n".join(
        [line for line in result.split("\n") if re.search(tasks, line)]
    )
    assert (
        re.search(expected_pattern, filtered_result, re.DOTALL) is not None
    ), f"Pattern {expected_pattern} not found in scontrol output:\n{filtered_result}"


def squeue_steps():
    """Returns the steps of the jobs of fed_job_shared.sh that squeue -s lists,
    in its default order (by step id).

    squeue does not list the steps managed by stepmgr (see squeue.1), so with
    it the srun steps are not listed. The extern step is listed when it exists,
    that is with PrologFlags=contain, which stepmgr requires.
    """
    steps = []
    if not atf.config_parameter_includes("SlurmctldParameters", "enable_stepmgr"):
        steps += ["0", "1"]
    steps.append("batch")
    if atf.config_parameter_includes("PrologFlags", "contain"):
        steps.append("extern")
    return steps


def wait_for_steps_started(cluster_map):
    """Wait for the srun steps .0 and .1 of the jobs to be running.

    The batch (and extern) steps are created when the job starts, but the srun
    steps only a bit later, so waiting just for the job is not enough. Each job
    is looked up from the cluster where it runs, as with stepmgr its srun steps
    are only known by its stepmgr, a node of that cluster.

    Args:
        cluster_map (dict): The cluster name where each job id runs.
    """
    selected = atf.properties["cluster"].number
    numbers = {
        cluster.name: cluster.number for cluster in atf.properties["all-clusters"]
    }
    try:
        for job_id, cluster in cluster_map.items():
            atf.select_cluster(numbers[cluster])
            for step in [0, 1]:
                atf.wait_for_step(job_id, step, fatal=True)
    finally:
        atf.select_cluster(selected)


def submit_jobs_with_steps():
    """Submit 3 jobs (one per cluster) with steps. Returns (ids, cluster_map).

    Non-pinned: test_scontrol_operations needs cls1 as origin for all 3
    so --local shows 1 RUNNING + 2 REVOKED sibling records.
    """
    ji0 = submit_fed_job()
    ji1 = submit_fed_job()
    ji2 = submit_fed_job()

    c0 = wait_for_running_cluster(ji0)
    c1 = wait_for_running_cluster(ji1)
    c2 = wait_for_running_cluster(ji2)

    cluster_map = {ji0: c0, ji1: c1, ji2: c2}
    wait_for_steps_started(cluster_map)

    return [ji0, ji1, ji2], cluster_map
