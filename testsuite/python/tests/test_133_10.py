##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import re

import pytest

import atf

pytestmark = pytest.mark.slow

fed_name = "fed"
# The names of the clusters, set by setup()
cls1 = cls2 = cls3 = None

user_name = None
node_count = 1


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
    global user_name, node_count

    user_name = atf.get_user_name()

    # Get node count for exclusive allocation
    output = atf.run_command_output("sinfo -h -o %D --local", fatal=True)
    node_count = int(output.strip().split("\n")[0])

    cancel_federation_jobs()

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


def wait_for_running_cluster(job_id):
    """Wait for the job to run on a cluster and return it."""
    atf.wait_for_job_state(job_id, "RUNNING", siblings=True, fatal=True)
    running = atf.get_job_clusters(job_id, "RUNNING")
    assert len(running) == 1, f"Job {job_id} should run on one cluster: {running}"
    return running[0]


@pytest.fixture(scope="function")
def cleanup():
    """Function-level cleanup for individual tests."""
    yield
    cancel_federation_jobs()


def test_basic_federation_job_cancellation(federation_setup, cleanup):
    """Test basic federation job cancellation.

    Submit multiple jobs that will run on different clusters,
    then cancel them one by one and verify cancellation.
    Includes intermediate squeue checks to verify remaining jobs.
    """
    # Create test script
    test_script = atf.module_tmp_path / "fed_test_script.sh"
    atf.make_bash_script(str(test_script), "sleep infinity")

    job_ids = []
    cluster_assignments = {}

    # 4th job stays federation-wide so it lands PENDING across all siblings.
    for i, fill_cluster in enumerate([cls1, cls2, cls3]):
        job_id = atf.submit_job_sbatch(
            f"-N{node_count} --exclusive --output=/dev/null"
            f" --error=/dev/null --requeue -M{fill_cluster} {test_script}",
            fatal=True,
        )
        job_ids.append(job_id)
        atf.wait_for_job_state(
            job_id, "RUNNING", siblings=True, clusters=fill_cluster, fatal=True
        )
        cluster_assignments[job_id] = fill_cluster

    pending_job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null"
        f" --error=/dev/null --requeue {test_script}",
        fatal=True,
    )
    job_ids.append(pending_job_id)
    atf.wait_for_job_state(
        pending_job_id, "PENDING", siblings=True, all_clusters=True, fatal=True
    )

    # Cancel the pending job
    assert len(job_ids) >= 4
    pending_job = job_ids[-1]

    atf.cancel_jobs([pending_job], fatal=True)

    atf.wait_for_job_state(pending_job, "DONE", siblings=True, timeout=30, fatal=True)

    # Verify cancelled job is absent from squeue
    def check_cancelled_absent():
        output = atf.run_command_output(f"squeue -M{cls1},{cls2},{cls3} --noheader -a")
        return not re.search(rf"\b{pending_job}\b.*\s(PD|R)\s", output)

    assert atf.repeat_until(
        check_cancelled_absent, lambda x: x, timeout=15
    ), f"Cancelled job {pending_job} should be absent from squeue"

    # Verify remaining 3 jobs still present in squeue
    output = atf.run_command_output(f"squeue -M{cls1},{cls2},{cls3} --noheader -a")
    for job_id in job_ids[:-1]:
        assert re.search(
            rf"\b{job_id}\b", output
        ), f"Job {job_id} should still be in squeue after cancelling pending job"

    # Cancel running jobs one by one with intermediate checks
    for i, job_id in enumerate(job_ids[:-1]):
        atf.cancel_jobs([job_id], fatal=True)
        atf.wait_for_job_state(job_id, "DONE", siblings=True, timeout=30, fatal=True)

        # Verify cancelled job is absent from squeue
        def check_absent(jid=job_id):
            output = atf.run_command_output(
                f"squeue -M{cls1},{cls2},{cls3} --noheader -a"
            )
            return not re.search(rf"\b{jid}\b.*\s(PD|R)\s", output)

        assert atf.repeat_until(
            check_absent, lambda x: x, timeout=15
        ), f"Cancelled job {job_id} should be absent from squeue"

        # Verify remaining jobs still present
        remaining = job_ids[i + 1 : -1]
        if remaining:
            output = atf.run_command_output(
                f"squeue -M{cls1},{cls2},{cls3} --noheader -a"
            )
            for rem_job in remaining:
                assert re.search(
                    rf"\b{rem_job}\b", output
                ), f"Job {rem_job} should still be in squeue"


def test_cluster_specific_cancellation(federation_setup, cleanup):
    """Test cluster-specific cancellation.

    Submit jobs and cancel them using cluster-specific scancel with -M flag.
    Also test cancelling a pending job via -M on its pending cluster.
    """
    test_script = atf.module_tmp_path / "fed_test_script.sh"
    atf.make_bash_script(str(test_script), "sleep infinity")

    job_ids = []
    cluster_assignments = {}

    for fill_cluster in [cls1, cls2, cls3]:
        job_id = atf.submit_job_sbatch(
            f"-N{node_count} --exclusive --output=/dev/null"
            f" --error=/dev/null --requeue -M{fill_cluster} {test_script}",
            fatal=True,
        )
        job_ids.append(job_id)
        atf.wait_for_job_state(
            job_id, "RUNNING", siblings=True, clusters=fill_cluster, fatal=True
        )
        cluster_assignments[job_id] = fill_cluster

    # Submit a pending job (all clusters full)
    pending_job = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null"
        f" --error=/dev/null --requeue {test_script}",
        fatal=True,
    )
    atf.wait_for_job_state(
        pending_job, "PENDING", siblings=True, all_clusters=True, fatal=True
    )

    # Cancel pending job via -M targeting cls3
    atf.run_command(f"scancel -M{cls3} {pending_job}", fatal=True)
    atf.wait_for_job_state(pending_job, "DONE", siblings=True, timeout=30, fatal=True)

    # Cancel running jobs using cluster-specific scancel
    cancelled_jobs = set()
    for job_id, cluster in cluster_assignments.items():
        atf.run_command(f"scancel -M{cluster} {job_id}", fatal=True)

        atf.wait_for_job_state(
            job_id, "DONE", siblings=True, clusters=cluster, timeout=30, fatal=True
        )

        cancelled_jobs.add(job_id)

        # Verify remaining jobs still present
        remaining = [j for j in cluster_assignments if j not in cancelled_jobs]
        if remaining:
            output = atf.run_command_output(
                f"squeue -M{cls1},{cls2},{cls3} --noheader -a"
            )
            for rem_job in remaining:
                assert re.search(
                    rf"\b{rem_job}\b", output
                ), f"Job {rem_job} should still be in squeue after cancelling {job_id}"


def test_mass_user_cancellation(federation_setup, cleanup):
    """Test mass user cancellation across federation.

    Submit jobs and cancel all of them at once using scancel -u.
    """
    test_script = atf.module_tmp_path / "fed_test_script.sh"
    atf.make_bash_script(str(test_script), "sleep infinity")

    job_ids = []
    for fill_cluster in [cls1, cls2]:
        job_id = atf.submit_job_sbatch(
            f"-N{node_count} --exclusive --output=/dev/null"
            f" --error=/dev/null --requeue -M{fill_cluster} {test_script}",
            fatal=True,
        )
        job_ids.append(job_id)
        atf.wait_for_job_state(
            job_id, "RUNNING", siblings=True, clusters=fill_cluster, fatal=True
        )

    # Cancel all user jobs across all clusters
    atf.run_command(f"scancel -u {user_name}", fatal=True)

    # Verify all jobs are cancelled
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "DONE", siblings=True, timeout=30, fatal=True)


def test_scancel_nonparticipant_has_no_effect(federation_setup, cleanup):
    """Test that scancel from non-participant cluster has no effect.

    NEGATIVE test: cancelling a job via -M on a cluster where
    the job is NOT running should leave the job intact.
    """
    test_script = atf.module_tmp_path / "fed_test_script.sh"
    atf.make_bash_script(str(test_script), "sleep infinity")

    job_ids = {}
    cluster_assignments = {}

    for i in range(3):
        job_id = atf.submit_job_sbatch(
            f"-N{node_count} --exclusive --output=/dev/null"
            f" --error=/dev/null --requeue {test_script}",
            fatal=True,
        )
        cluster = wait_for_running_cluster(job_id)
        job_ids[cluster] = job_id
        cluster_assignments[job_id] = cluster

    for cluster, jid in job_ids.items():
        assert atf.wait_for_job_state(
            jid, "RUNNING", clusters=cluster, timeout=60, fatal=False
        ), f"Federation did not converge for job {jid} on {cluster}"

    assert len(job_ids) == 3, f"Each job should run on a different cluster: {job_ids}"

    # Cancelling a job from a cluster where it is not running should have no
    # effect. As there is no event to wait for, observe the jobs for a while.
    for via, target in [(cls3, cls1), (cls3, cls2), (cls2, cls3)]:
        atf.run_command(f"scancel -M{via} {job_ids[target]}")

        for _ in atf.timer(timeout=5, poll_interval=1):
            assert atf.get_job_clusters(
                job_ids[target], "RUNNING", clusters=target
            ), f"Job {job_ids[target]} should still be RUNNING on {target}"

            output = atf.run_command_output(
                f"squeue -M{cls1},{cls2},{cls3} --noheader -a"
            )
            for jid in job_ids.values():
                assert re.search(
                    rf"\b{jid}\b", output
                ), f"Job {jid} should still be present after non-participant scancel"

    # Now cancel all 3 jobs via -M{cls1} (origin) to verify origin-cluster routing works
    for cluster, job_id in job_ids.items():
        atf.run_command(f"scancel -M{cls1} {job_id}", fatal=True)
        atf.wait_for_job_state(job_id, "DONE", siblings=True, timeout=30, fatal=True)


def test_scancel_ctld_from_nonparticipant(federation_setup, cleanup):
    """Test scancel --ctld from a non-participant cluster cancels the job.

    Unlike plain scancel -M, scancel --ctld routes the cancel through
    the local slurmctld which then contacts the job's actual cluster.
    Note that --ctld is currently ignored for federated jobs,
    but this test is valid now and once it is enforced.
    """
    test_script = atf.module_tmp_path / "fed_test_script.sh"
    atf.make_bash_script(str(test_script), "sleep infinity")

    job_ids = {}

    for i in range(3):
        job_id = atf.submit_job_sbatch(
            f"-N{node_count} --exclusive --output=/dev/null"
            f" --error=/dev/null --requeue {test_script}",
            fatal=True,
        )
        cluster = wait_for_running_cluster(job_id)
        job_ids[cluster] = job_id

    assert len(job_ids) == 3, f"Each job should run on a different cluster: {job_ids}"

    for cluster, jid in job_ids.items():
        assert atf.wait_for_job_state(
            jid, "RUNNING", clusters=cluster, timeout=60, fatal=False
        ), f"Federation did not converge for job {jid} on {cluster}"

    # From cls3, cancel job on cls2 using --ctld
    atf.select_cluster(3)
    atf.run_command(f"scancel --ctld {job_ids[cls2]}", fatal=True)
    atf.wait_for_job_state(job_ids[cls2], "DONE", siblings=True, timeout=30, fatal=True)

    # Verify non-cancelled jobs survive
    for cluster, jid in job_ids.items():
        if cluster != cls2:
            atf.wait_for_job_state(
                jid, "RUNNING", siblings=True, clusters=cluster, fatal=True
            )

    # From cls2, cancel job on cls1 using --ctld
    atf.select_cluster(2)
    atf.run_command(f"scancel --ctld {job_ids[cls1]}", fatal=True)
    atf.wait_for_job_state(job_ids[cls1], "DONE", siblings=True, timeout=30, fatal=True)

    # Verify non-cancelled jobs survive
    for cluster, jid in job_ids.items():
        if cluster not in [cls1, cls2]:
            atf.wait_for_job_state(
                jid, "RUNNING", siblings=True, clusters=cluster, fatal=True
            )

    # From cls2, cancel job on cls3 using --ctld
    atf.select_cluster(2)
    atf.run_command(f"scancel --ctld {job_ids[cls3]}", fatal=True)
    atf.wait_for_job_state(job_ids[cls3], "DONE", siblings=True, timeout=30, fatal=True)


def test_mass_user_cancel_from_sibling(federation_setup, cleanup):
    """Test mass user cancellation from a non-origin cluster.

    Submit 4 jobs (3 running + 1 pending), then cancel all via
    scancel -u from cls3 (a sibling). Verify all reach DONE and
    squeue is empty.
    """
    test_script = atf.module_tmp_path / "fed_test_script.sh"
    atf.make_bash_script(str(test_script), "sleep infinity")

    job_ids = []
    running_clusters = {}
    for i in range(3):
        job_id = atf.submit_job_sbatch(
            f"-N{node_count} --exclusive --output=/dev/null"
            f" --error=/dev/null --requeue {test_script}",
            fatal=True,
        )
        job_ids.append(job_id)
        cluster = wait_for_running_cluster(job_id)
        running_clusters[job_id] = cluster

    # Submit a 4th (pending) job
    pending_job = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null"
        f" --error=/dev/null --requeue {test_script}",
        fatal=True,
    )
    job_ids.append(pending_job)
    atf.wait_for_job_state(
        pending_job, "PENDING", siblings=True, all_clusters=True, fatal=True
    )

    for jid, cluster in running_clusters.items():
        assert atf.wait_for_job_state(
            jid, "RUNNING", clusters=cluster, timeout=60, fatal=False
        ), f"Federation did not converge for job {jid} on {cluster}"

    # Cancel all from cls3 (non-origin)
    atf.select_cluster(3)
    atf.run_command(f"scancel -u {user_name}", fatal=True)
    atf.select_cluster(1)

    # Verify all 4 jobs reach DONE
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "DONE", siblings=True, timeout=30, fatal=True)

    # Verify squeue is empty for our user
    def check_empty():
        result = atf.run_command(
            f"squeue -M{cls1},{cls2},{cls3} --user {user_name} --noheader",
        )
        return result["stdout"].strip() == ""

    assert atf.repeat_until(
        check_empty, lambda x: x, timeout=15
    ), "squeue should be empty after mass cancel from sibling"
