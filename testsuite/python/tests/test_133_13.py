##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import re

import pytest

import atf

# The names of the clusters, set by setup()
cls1 = cls2 = cls3 = None
user_name = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global cls1, cls2, cls3
    cls1, cls2, cls3 = atf.require_multicluster(3, federation=True)
    for _ in atf.foreach_cluster():
        atf.require_nodes(1)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def federation_setup():
    """Module-level federation setup and cleanup."""
    global user_name

    user_name = atf.get_user_name()

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


def cancel_federation_jobs():
    """Cancel all federation jobs for the test user."""
    if user_name:
        atf.run_command(f"scancel -M{cls1},{cls2},{cls3} --user {user_name}")
        atf.repeat_until(
            lambda: atf.run_command_output(
                f"squeue -M{cls1},{cls2},{cls3} --user {user_name} --noheader",
            ),
            lambda output: output.strip() == "",
            timeout=10,
        )


def submit_fed_array_job(options=""):
    """Submit an array job through the federation. Returns job ID."""
    return atf.submit_job_sbatch(
        f"-N1 --exclusive -a1-15 -o/dev/null {options} --wrap 'sleep 20'",
        fatal=True,
    )


def submit_fed_array_job_xfail(options):
    """Submit an array job expecting failure. Returns the result dict."""
    result = atf.run_command(
        f"sbatch -N1 --exclusive -a1-15 -o/dev/null {options} --wrap 'sleep 20'",
    )
    assert result["exit_code"] != 0, "Expected job submission to fail but it succeeded"
    return result


def fed_squeue():
    """Run squeue across all federation clusters and return output."""
    return atf.run_command_output(
        f"squeue --noheader -a -M{cls1},{cls2},{cls3}",
        fatal=True,
    )


def fed_squeue_array_detail(job_id):
    """Query squeue for array task detail per cluster.

    Returns a dict keyed by cluster with values {"running": count, "pending": count}.
    """
    result = atf.run_command_output(
        f"squeue -M{cls1},{cls2},{cls3} --noheader -a --local -j {job_id} "
        f"-O arraytaskid,statecompact,cluster",
    )
    counts = {}
    for line in result.strip().splitlines():
        parts = line.split()
        if len(parts) >= 3:
            state = parts[1].strip()
            cluster = parts[2].strip()
            if cluster not in counts:
                counts[cluster] = {"running": 0, "pending": 0}
            if state == "R":
                counts[cluster]["running"] += 1
            elif state == "PD":
                counts[cluster]["pending"] += 1
    return counts


def test_federated_arrays(federation_setup):
    """Test federated arrays.

    Verifies that:
    1. Array jobs can be submitted to the local federation cluster
    2. Array jobs with --cluster-constraint matching the local cluster succeed
    3. Array jobs with --cluster-constraint to a remote cluster fail with
       "federated job arrays must run on local cluster"
    4. Array jobs with explicit -M to a remote cluster succeed
    5. Array jobs with conflicting --cluster-constraint and -M fail
    """
    cancel_federation_jobs()

    # Submit basic array job (no cluster constraint)
    jid0 = submit_fed_array_job()

    # Wait for first array task to start running
    atf.wait_for_job_state(
        f"{jid0}_1", "RUNNING", siblings=True, clusters=cls1, fatal=True
    )

    # Verify jid0 tasks appear in squeue on cls1
    output = fed_squeue()
    assert re.search(
        rf"{cls1}\s+{jid0}", output
    ), f"Job {jid0} not found on {cls1} in squeue output"

    # Validate the array task ID format: running tasks show individual IDs,
    # pending tasks show packed ranges. With one node, task 1 runs and the rest
    # are pending.
    result = atf.run_command_output(
        f"squeue -M{cls1} --noheader -a --local -j {jid0} -O arraytaskid,statecompact",
        fatal=True,
    )
    task_ids = {}
    for line in result.strip().splitlines():
        task_id, state = line.split()[:2]
        task_ids.setdefault(state, []).append(task_id)
    assert set(task_ids) == {"R", "PD"}, f"Expected running and pending: {result}"
    for task_id in task_ids["R"]:
        assert re.fullmatch(
            r"\d+", task_id
        ), f"Running task should show individual ID, got '{task_id}'"
    for task_id in task_ids["PD"]:
        assert re.fullmatch(
            r"[\d,\-]+", task_id
        ), f"Pending task should show valid ID or range, got '{task_id}'"

    # Submit array job with --cluster-constraint=fa (local cluster - should succeed)
    jid1 = submit_fed_array_job("--cluster-constraint=fa")

    # Verify both jobs appear in squeue on cls1
    output = fed_squeue()
    assert re.search(rf"{cls1}\s+{jid0}", output), f"Job {jid0} not found on {cls1}"
    assert re.search(rf"{cls1}\s+{jid1}", output), f"Job {jid1} not found on {cls1}"

    # Verify squeue detail for jid0 - check while tasks are still queued
    # (with 1 node, only 1 task runs at a time; pending tasks may show as packed ranges)
    # A task that just started may still be CONFIGURING for a while, so it's
    # neither running nor pending yet.
    for _ in atf.timer(poll_interval=0.5):
        counts0_early = fed_squeue_array_detail(jid0)
        tasks = counts0_early.get(cls1, {})
        if tasks.get("running", 0) + tasks.get("pending", 0) >= 2:
            break
    assert cls1 in counts0_early, f"jid0 should have tasks on {cls1}"
    total0_early = counts0_early[cls1]["running"] + counts0_early[cls1]["pending"]
    assert total0_early >= 2, (
        f"jid0 should have running + pending tasks on cls1, "
        f"got {total0_early}: {counts0_early}"
    )

    # Verify squeue detail for jid1 - should also have tasks queued
    for _ in atf.timer(poll_interval=0.5):
        counts1_early = fed_squeue_array_detail(jid1)
        tasks = counts1_early.get(cls1, {})
        if tasks.get("running", 0) + tasks.get("pending", 0) >= 1:
            break
    assert cls1 in counts1_early, f"jid1 should have tasks on {cls1}"
    total1_early = counts1_early[cls1]["running"] + counts1_early[cls1]["pending"]
    assert (
        total1_early >= 1
    ), f"jid1 should have tasks on cls1, got {total1_early}: {counts1_early}"

    # Wait for the second task to run, proving tasks cycle one at a time on cls1
    # (waiting for the last task would take ~15 * 20s, since each task is exclusive)
    atf.wait_for_job_state(
        f"{jid0}_2", "RUNNING", siblings=True, clusters=cls1, fatal=True
    )

    # After cycling, verify remaining counts. The task may still be
    # CONFIGURING for a while, although it's already RUNNING for the wait.
    for _ in atf.timer(poll_interval=0.5):
        counts0 = fed_squeue_array_detail(jid0)
        if counts0.get(cls1, {}).get("running", 0) > 0:
            break
    assert cls1 in counts0, f"jid0 should have tasks on {cls1}"
    assert counts0[cls1]["running"] > 0, "jid0 should have running tasks on cls1"
    # Verify no jid0 tasks on cls2/cls3
    for cluster in [cls2, cls3]:
        total = counts0.get(cluster, {}).get("running", 0) + counts0.get(
            cluster, {}
        ).get("pending", 0)
        assert total == 0, f"jid0 tasks should not be on {cluster}: {counts0}"

    counts1 = fed_squeue_array_detail(jid1)
    assert cls1 in counts1, f"jid1 should have tasks on {cls1}"

    # Submit array with --cluster-constraint=fb: should FAIL because
    # federated job arrays must run on local cluster
    result = submit_fed_array_job_xfail("--cluster-constraint=fb")
    assert (
        "federated job arrays must run on local cluster" in result["stderr"]
    ), f"Expected 'federated job arrays must run on local cluster' error, got: {result['stderr']}"
    # Also verify "Batch job submission failed" in the output
    assert (
        "Batch job submission failed" in result["stderr"]
    ), f"Expected 'Batch job submission failed' error, got: {result['stderr']}"

    # Submit array with --cluster-constraint=fb -M<cls2>: should succeed
    # because explicit -M allows routing arrays to a specific remote cluster
    jid2 = submit_fed_array_job(f"--cluster-constraint=fb -M{cls2}")

    # Wait for first task of jid2 to start on cls2
    atf.wait_for_job_state(
        f"{jid2}_1", "RUNNING", siblings=True, clusters=cls2, fatal=True
    )

    # Verify jid0 and jid1 are on cls1, jid2 is on cls2
    output = fed_squeue()
    assert re.search(rf"{cls1}\s+{jid0}", output), f"Job {jid0} not found on {cls1}"
    assert re.search(rf"{cls1}\s+{jid1}", output), f"Job {jid1} not found on {cls1}"
    assert re.search(rf"{cls2}\s+{jid2}", output), f"Job {jid2} not found on {cls2}"

    # Submit array with --cluster-constraint=fa -M<cls2>: should FAIL
    # because fa is cls1's feature but -M routes to cls2 (conflict)
    result = submit_fed_array_job_xfail(f"--cluster-constraint=fa -M{cls2}")
    assert re.search(
        r"Batch job submission failed: No eligible clusters for federated job",
        result["stderr"],
    ), f"Expected 'No eligible clusters for federated job' error, got: {result['stderr']}"
