##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import re

import pytest

import atf

pytestmark = pytest.mark.slow

fed_name = "fed"
# The names of the clusters, and all of them as ACTIVE, set by setup()
cls1 = cls2 = cls3 = None
all_active = None

user_name = None
drain_time = 5


@pytest.fixture(scope="module", autouse=True)
def setup():
    global cls1, cls2, cls3, all_active
    cls1, cls2, cls3 = atf.require_multicluster(3, federation=True)
    all_active = {cls1: "ACTIVE", cls2: "ACTIVE", cls3: "ACTIVE"}
    for _ in atf.foreach_cluster():
        atf.require_nodes(1)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def federation_setup():
    """Module-level fixture to set up federation."""
    global user_name

    user_name = atf.get_user_name()
    yield


@pytest.fixture(scope="function", autouse=True)
def reset_federation():
    """Cancel the jobs and restore the federation after each test.

    The tests change the federation and only restore it at their end, so
    without this a failing test leaves it modified and also breaks the next
    ones (e.g. "System submissions disabled" if cls1 is left DRAINED).
    """
    yield

    atf.run_command(
        f"scancel -M{cls1},{cls2},{cls3} --user {atf.properties['test-user']}"
    )
    atf.run_command(
        f"sacctmgr -i mod fed {fed_name} set clusters+={cls1},{cls2},{cls3}",
        user=atf.properties["slurm-user"],
    )
    for cluster in [cls1, cls2, cls3]:
        atf.run_command(
            f"sacctmgr -i mod cluster {cluster} set fedstate=ACTIVE",
            user=atf.properties["slurm-user"],
        )
    atf.wait_for_federation_sync()


def assert_fed_states(actual_states, expected_states):
    """Verify that each cluster of expected_states has a matching FedState.

    The FedState of each cluster is compared by itself, so the state of
    another cluster can't match it, and INACTIVE does not match ACTIVE.

    Args:
        actual_states (dict): The FedState by cluster name.
        expected_states (dict): The regex that the FedState of each cluster
            should fully match, by cluster name.
    """
    for cluster, state in expected_states.items():
        assert re.fullmatch(
            state, actual_states.get(cluster, "")
        ), f"FedState of {cluster} should be {state}, got: {actual_states}"


def sacctmgr_show_federation(expected_states):
    """Show the federation with sacctmgr and verify the FedState of the clusters."""
    output = atf.run_command_output(
        f"sacctmgr show fed {fed_name} format=cluster,fedstate --noheader --parsable2",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    states = dict(
        line.split("|")[:2] for line in output.splitlines() if line.count("|") >= 1
    )
    assert_fed_states(states, expected_states)


def sacctmgr_modify_cluster_state(cluster, state):
    """Modify cluster federation state and verify output patterns."""
    output = atf.run_command_output(
        f"sacctmgr -i mod cluster {cluster} set fedstate={state}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    expected_patterns = [
        (r"Setting", "Expected 'Setting' in sacctmgr output"),
        (rf"FedState\s*=\s*{re.escape(state)}", f"Expected FedState={state} in output"),
        (r"Modified cluster", "Expected 'Modified cluster' in output"),
        (rf"{re.escape(cluster)}", f"Expected cluster name {cluster} in output"),
    ]
    for pattern, error_msg in expected_patterns:
        assert re.search(pattern, output), error_msg
    atf.wait_for_federation_sync()
    return output


def scontrol_show_fed(expected_states):
    """Run scontrol show federation and verify the FedState of the clusters.

    The slurmdbd notifies the controllers after a change made with sacctmgr,
    so it can take a while until scontrol shows it. It polls until the
    FedStates match, or until the timeout.
    """
    for _ in atf.timer():
        output = atf.run_command_output("scontrol show federation", fatal=True)
        states = dict(
            re.findall(
                r"^(?:Self|Sibling):\s+(\w+):\S* ID:\d+ FedState:(\S+)",
                output,
                re.M,
            )
        )
        if all(
            re.fullmatch(state, states.get(cluster, ""))
            for cluster, state in expected_states.items()
        ):
            break
    assert_fed_states(states, expected_states)


def verify_squeue_job_state(cluster, job_id, expected_state):
    """Verify job state on a specific cluster via squeue."""

    def check():
        output = atf.run_command_output(
            f"squeue -M{cluster} --local --jobs={job_id} --noheader -o '%T'",
        )
        return expected_state.upper() in output.upper() if output else False

    assert atf.repeat_until(
        check, lambda x: x, timeout=30
    ), f"Job {job_id} not in state {expected_state} on {cluster}"


def test_initial_federation_setup(federation_setup):
    """Test initial federation setup with all clusters ACTIVE."""
    sacctmgr_show_federation(all_active)

    # Also verify scontrol show federation
    scontrol_show_fed(all_active)


def test_federation_inactive_state(federation_setup):
    """Test federation INACTIVE state functionality.

    Sets a cluster to INACTIVE and verifies:
    - Federation status shows the state change
    - Job submissions to inactive cluster are rejected
    """
    sacctmgr_modify_cluster_state(cls1, "INACTIVE")
    sacctmgr_show_federation({**all_active, cls1: "INACTIVE"})

    # Verify sbatch to inactive cluster fails with submissions disabled
    result = atf.run_command(
        "sbatch --output=/dev/null --error=/dev/null --wrap 'sleep 10'",
    )
    combined = result.get("stdout", "") + result.get("stderr", "")
    assert re.search(
        r"submissions disabled", combined, re.IGNORECASE
    ), f"Expected submissions disabled error, got: {combined}"

    # Reset back to ACTIVE
    sacctmgr_modify_cluster_state(cls1, "ACTIVE")


def test_federation_drain_state(federation_setup):
    """Test federation DRAIN state functionality.

    Sets a cluster to DRAIN and verifies:
    - Federation status shows DRAIN
    - New job submissions are rejected
    - Cluster transitions to DRAINED when empty
    """
    sacctmgr_modify_cluster_state(cls1, "DRAIN")
    sacctmgr_show_federation({**all_active, cls1: "DRAIN(ED)?"})

    # Verify scontrol show federation reflects DRAIN state
    scontrol_show_fed({cls1: "DRAIN(ED)?"})

    # Verify new submissions to draining cluster fail
    result = atf.run_command(
        "sbatch --output=/dev/null --error=/dev/null --wrap 'sleep 10'",
    )
    combined = result.get("stdout", "") + result.get("stderr", "")
    assert re.search(
        r"submissions disabled", combined, re.IGNORECASE
    ), f"Expected submissions disabled error, got: {combined}"

    # Wait for cluster to transition to DRAINED (no jobs running)
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(
            f"sacctmgr show fed {fed_name} --noheader",
            user=atf.properties["slurm-user"],
        )
        if "DRAINED" in output:
            break

    # Reset back to ACTIVE
    sacctmgr_modify_cluster_state(cls1, "ACTIVE")


def test_inactive_with_running_and_pending_jobs(federation_setup):
    """Test INACTIVE with running and pending jobs.

    Submit 2 exclusive jobs (one runs, one pends). Set INACTIVE.
    Verify squeue shows PD+R. Wait for running job to complete.
    Verify squeue shows only PD. Reset ACTIVE.
    Verify the previously-pending job starts RUNNING.
    """

    # Submit first job (will run) - use -M{cls1} to keep it local
    job1 = atf.submit_job_sbatch(
        f'-N1 --exclusive --wrap="sleep 10" -o/dev/null -M{cls1}', fatal=True
    )

    def check_running():
        output = atf.run_command_output(
            f"squeue -M{cls1} --local --jobs={job1} --noheader -o '%T'",
        )
        return "RUNNING" in output if output else False

    atf.repeat_until(check_running, lambda x: x, timeout=30)

    # Submit second job (will pend since cluster is full) - use -M{cls1} to keep local
    job2 = atf.submit_job_sbatch(
        f'-N1 --exclusive --wrap="sleep infinity" -o/dev/null -M{cls1}', fatal=True
    )

    def check_pending():
        output = atf.run_command_output(
            f"squeue -M{cls1} --local --jobs={job2} --noheader -o '%T'",
        )
        return "PENDING" in output if output else False

    atf.repeat_until(check_pending, lambda x: x, timeout=10)

    # Set INACTIVE
    sacctmgr_modify_cluster_state(cls1, "INACTIVE")

    # Verify squeue shows both a RUNNING and a PENDING job.
    # Use polling since transient states (COMPLETING) may briefly appear.
    def check_running_and_pending():
        output = atf.run_command_output(f"squeue -M{cls1} --local --noheader -o '%T'")
        states = [line.strip() for line in output.strip().splitlines() if line.strip()]
        return "RUNNING" in states and "PENDING" in states

    assert atf.repeat_until(
        check_running_and_pending, lambda x: x, timeout=10
    ), "Expected both RUNNING and PENDING jobs in squeue during INACTIVE"

    # Wait for the running job to complete
    def check_job1_done():
        output = atf.run_command_output(
            f"squeue -M{cls1} --local --jobs={job1} --noheader -o '%T'",
        )
        return not output.strip() or "RUNNING" not in output

    atf.repeat_until(check_job1_done, lambda x: x, timeout=30)

    # Verify only PD (or RV for federated) remains
    output = atf.run_command_output(
        f"squeue -M{cls1} --local --noheader --jobs={job2} -o '%T'"
    )
    assert (
        "PENDING" in output or "REVOKED" in output
    ), f"Expected job2 still PENDING/REVOKED after job1 completes, got: {output}"

    # Reset ACTIVE
    sacctmgr_modify_cluster_state(cls1, "ACTIVE")

    # Verify the previously-pending job starts RUNNING
    verify_squeue_job_state(cls1, job2, "RUNNING")


def test_federation_drain_remove_state(federation_setup):
    """Test federation DRAIN+REMOVE state functionality.

    Sets a cluster to DRAIN+REMOVE and verifies:
    - Federation status shows DRAIN+REMOVE
    - New job submissions are rejected during DRAIN+REMOVE
    - Cluster is automatically removed from federation after draining
    - scontrol show fed confirms cluster is gone
    - squeue is empty after removal
    - Removed cluster can still run local jobs
    """
    output = atf.run_command_output(
        f"sacctmgr -i mod cluster {cls1} set fedstate=DRAIN+REMOVE",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert re.search(r"Setting", output)
    assert re.search(r"FedState\s*=\s*DRAIN\+REMOVE", output)
    assert re.search(r"Modified cluster", output)

    # Verify sbatch rejection during DRAIN+REMOVE
    result = atf.run_command(
        "sbatch --output=/dev/null --error=/dev/null --wrap 'sleep 10'",
    )
    combined = result.get("stdout", "") + result.get("stderr", "")
    assert re.search(
        r"submissions disabled", combined, re.IGNORECASE
    ), f"Expected submissions disabled error during DRAIN+REMOVE, got: {combined}"

    # Wait for cluster to be removed from federation
    for _ in atf.timer(timeout=drain_time + 10, fatal=True):
        output = atf.run_command_output(
            f"sacctmgr show fed {fed_name} format=cluster --noheader",
            user=atf.properties["slurm-user"],
        )
        if cls1 not in output:
            break
    atf.wait_for_federation_sync()

    # Verify squeue is empty on cls1
    output = atf.run_command_output(f"squeue -M{cls1} --local --noheader")
    assert (
        not output.strip()
    ), f"squeue should be empty after DRAIN+REMOVE, got: {output}"

    # Verify local jobs still work on removed cluster
    job_id = atf.submit_job_sbatch('--wrap="hostname" -o/dev/null', fatal=True)

    # Non-federated job IDs should be local (bits 0-25 only)
    assert (
        job_id <= 0x3FFFFFF
    ), f"Job ID {job_id} appears to be a federation ID after cluster removal"

    # Wait for the local job to complete
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(
            f"squeue -M{cls1} --local --jobs={job_id} --noheader -o '%T'",
        )
        if not output.strip():
            break

    # Re-add cls1 to federation for subsequent tests
    atf.run_command(
        f"sacctmgr -i modify federation {fed_name} set clusters+={cls1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_federation_sync()


def test_federation_cluster_readd(federation_setup):
    """Test re-adding cluster to federation."""
    # Remove cls1 from federation
    atf.run_command(
        f"sacctmgr -i mod fed {fed_name} set clusters-={cls1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_federation_sync()

    # Re-add cls1 to the federation
    output = atf.run_command_output(
        f"sacctmgr -i mod fed {fed_name} set clusters+={cls1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert re.search(r"Setting", output)
    assert re.search(rf"Cluster\s*\+=\s*{cls1}", output)
    assert re.search(r"Modified federation", output)
    atf.wait_for_federation_sync()

    # Verify the clusters are ACTIVE in the federation again
    sacctmgr_show_federation(all_active)


def test_inactive_rejects_srun(federation_setup):
    """Test that srun on INACTIVE cluster is rejected."""
    sacctmgr_modify_cluster_state(cls1, "INACTIVE")

    result = atf.run_command("srun hostname")
    combined = result.get("stdout", "") + result.get("stderr", "")
    assert re.search(
        r"(submissions disabled|Unable to allocate)", combined, re.IGNORECASE
    ), f"Expected submissions disabled or Unable to allocate for srun, got: {combined}"

    sacctmgr_modify_cluster_state(cls1, "ACTIVE")


def test_inactive_rejects_salloc(federation_setup):
    """Test that salloc on INACTIVE cluster is rejected."""
    sacctmgr_modify_cluster_state(cls1, "INACTIVE")

    result = atf.run_command("salloc -N1 hostname")
    combined = result.get("stdout", "") + result.get("stderr", "")
    assert re.search(
        r"(submissions disabled|Unable to allocate)", combined, re.IGNORECASE
    ), f"Expected submissions disabled or Unable to allocate for salloc, got: {combined}"

    sacctmgr_modify_cluster_state(cls1, "ACTIVE")


def test_scontrol_show_fed_states(federation_setup):
    """Test scontrol show federation at each state transition."""
    # ACTIVE (initial)
    scontrol_show_fed({cls1: "ACTIVE"})

    # INACTIVE
    sacctmgr_modify_cluster_state(cls1, "INACTIVE")
    scontrol_show_fed({cls1: "INACTIVE"})

    # Back to ACTIVE
    sacctmgr_modify_cluster_state(cls1, "ACTIVE")
    scontrol_show_fed({cls1: "ACTIVE"})

    # DRAIN
    sacctmgr_modify_cluster_state(cls1, "DRAIN")
    scontrol_show_fed({cls1: "DRAIN(ED)?"})

    # Wait for DRAINED
    scontrol_show_fed({cls1: "DRAINED"})

    # Reset to ACTIVE
    sacctmgr_modify_cluster_state(cls1, "ACTIVE")


def test_drain_with_running_jobs(federation_setup):
    """Test DRAIN with running jobs - jobs complete, then DRAINED transition."""

    # Submit first short job BEFORE drain. Both jobs are pinned to cls1, or
    # job2 would just run in a free sibling instead of waiting for job1.
    job1 = atf.submit_job_sbatch(
        f'-N1 --exclusive --wrap="sleep 5" -o/dev/null -M{cls1}', fatal=True
    )
    verify_squeue_job_state(cls1, job1, "RUNNING")

    # Submit second job that will run after first completes
    job2 = atf.submit_job_sbatch(
        f'-N1 --exclusive --wrap="sleep 10" -o/dev/null -M{cls1}', fatal=True
    )

    # Now set to DRAIN
    sacctmgr_modify_cluster_state(cls1, "DRAIN")

    # Verify federation shows DRAIN (not DRAINED yet - jobs still running/pending)
    sacctmgr_show_federation({cls1: "DRAIN"})

    # Wait for first job to complete
    def check_job1_done():
        output = atf.run_command_output(
            f"squeue -M{cls1} --local --jobs={job1} --noheader -o '%T'",
        )
        return not output.strip() or "RUNNING" not in output

    atf.repeat_until(check_job1_done, lambda x: x, timeout=30, fatal=True)

    # Verify DRAIN persists (not DRAINED) while second job is still running
    verify_squeue_job_state(cls1, job2, "RUNNING")

    fed_output = atf.run_command_output(
        f"sacctmgr show fed {fed_name} --noheader",
        user=atf.properties["slurm-user"],
    )
    assert re.search(r"\bDRAIN\b", fed_output), "Should still be DRAIN while job2 runs"

    # Wait for both jobs to complete and cluster to transition to DRAINED
    atf.repeat_until(
        lambda: atf.run_command_output(
            f"sacctmgr show fed {fed_name} --noheader",
            user=atf.properties["slurm-user"],
        ),
        lambda output: re.search(r"DRAINED", output) is not None,
        timeout=drain_time + 30,
        fatal=True,
    )

    # Also verify scontrol show fed shows DRAINED
    scontrol_show_fed({cls1: "DRAINED"})

    # Reset
    sacctmgr_modify_cluster_state(cls1, "ACTIVE")


def test_drain_remove_requeue_zero(federation_setup):
    """Test that DRAIN+REMOVE sets Requeue=0 on completed jobs.

    When a cluster is DRAIN+REMOVE'd, the slurmctld's job_watch_thread waits
    for all jobs to complete, then sets Requeue=0 on all job records before
    removing the cluster from the federation. This prevents completed jobs
    from being auto-requeued back into the federation.
    """

    # Submit a SHORT job to cls1 with --requeue so it completes before drain timeout.
    job_id = atf.submit_job_sbatch(
        f'-N1 --exclusive --wrap="sleep 5" -o/dev/null --requeue -M{cls1}',
        fatal=True,
    )
    verify_squeue_job_state(cls1, job_id, "RUNNING")

    # Set DRAIN+REMOVE
    atf.run_command(
        f"sacctmgr -i mod cluster {cls1} set fedstate=DRAIN+REMOVE",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Wait for cluster to be removed from federation
    # (job is short, will complete quickly, then removal happens)
    for _ in atf.timer(timeout=drain_time + 30, fatal=True):
        output = atf.run_command_output(
            f"sacctmgr show fed {fed_name} format=cluster --noheader",
            user=atf.properties["slurm-user"],
        )
        if cls1 not in output:
            break
    atf.wait_for_federation_sync()

    # After removal, check that the completed job record has Requeue=0.
    # The job_watch_thread sets Requeue=0 on all jobs before
    # _remove_self_from_federation().
    output = atf.run_command_output(f"scontrol --local -a show job {job_id}")
    assert re.search(
        r"Requeue=0", output
    ), f"Expected Requeue=0 after DRAIN+REMOVE, got: {output}"

    # Re-add cls1 to federation
    atf.run_command(
        f"sacctmgr -i modify federation {fed_name} set clusters+={cls1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_federation_sync()


def test_drain_cluster_receives_no_new_jobs(federation_setup):
    """Test that a DRAINing cluster does not receive new federated jobs.

    Fills cls1 and cls3 so that cls2, the DRAINing cluster, is the only one
    that could start a job submitted without -M, and verifies that the job
    stays pending instead of running on cls2.
    """
    sacctmgr_modify_cluster_state(cls2, "DRAIN")
    sacctmgr_show_federation({**all_active, cls2: "DRAIN"})

    # Fill the only node of the ACTIVE clusters
    fill_jobs = {}
    for cluster in [cls1, cls3]:
        fill_jobs[cluster] = atf.submit_job_sbatch(
            f'-N1 --exclusive --wrap="sleep infinity" -o/dev/null -M{cluster}',
            fatal=True,
        )
    for cluster, fill_job in fill_jobs.items():
        verify_squeue_job_state(cluster, fill_job, "RUNNING")

    # Submit a job WITHOUT -M: cls2 is its only possible place to run
    job_id = atf.submit_job_sbatch(
        '-N1 --exclusive --wrap="hostname" -o/dev/null', fatal=True
    )
    verify_squeue_job_state(cls1, job_id, "PENDING")

    # The job must neither start on the origin nor on the DRAINing cluster.
    # This is a bounded observation window of something that must NOT happen,
    # so it intentionally runs to its end instead of waiting for a condition.
    for _ in atf.timer(timeout=5, poll_interval=1):
        for cluster in [cls1, cls2, cls3]:
            output = atf.run_command_output(
                f"squeue -M{cluster} --local --jobs={job_id} --noheader -o '%T'",
            )
            assert (
                "RUNNING" not in output
            ), f"Job {job_id} should not be RUNNING on {cluster} while cls2 drains"

    # Clean up
    atf.run_command(f"scancel -M{cls1},{cls2},{cls3} --user {user_name}")
    sacctmgr_modify_cluster_state(cls2, "ACTIVE")


def test_squeue_during_state_changes(federation_setup):
    """Test that squeue shows correct job states during federation state transitions."""

    # Submit a job pinned to cls1 and wait for it to run
    job_id = atf.submit_job_sbatch(
        f'-N1 --exclusive --wrap="sleep infinity" -o/dev/null -M{cls1}', fatal=True
    )

    def check_running():
        output = atf.run_command_output(
            f"squeue -M{cls1} --local --jobs={job_id} --noheader -o '%T'",
        )
        return "RUNNING" in output if output else False

    atf.repeat_until(check_running, lambda x: x, timeout=30)

    # Verify squeue shows RUNNING
    verify_squeue_job_state(cls1, job_id, "RUNNING")

    # Set INACTIVE on cls2 - job on cls1 should still be RUNNING
    sacctmgr_modify_cluster_state(cls2, "INACTIVE")
    verify_squeue_job_state(cls1, job_id, "RUNNING")

    # Reset to ACTIVE
    sacctmgr_modify_cluster_state(cls2, "ACTIVE")
    verify_squeue_job_state(cls1, job_id, "RUNNING")
