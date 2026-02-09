##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import re

import pytest

import atf

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

    global user_name, node_count

    user_name = atf.get_user_name()

    # Get node count
    output = atf.run_command_output(
        "sinfo --local -h -t idle,alloc,comp -o '%D'", fatal=True
    )
    if output and output.strip():
        node_count = int(output.strip().split()[0])

    # Clean up any existing jobs
    cancel_federation_jobs()

    # Set up cluster features on the ATF-created federation
    setup_cluster_features()

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
            timeout=15,
        )


def setup_cluster_features():
    """Set up cluster features fa, fb, fc on cls1, cls2, cls3."""
    for cluster, feature in [(cls1, "fa"), (cls2, "fb"), (cls3, "fc")]:
        output = atf.run_command_output(
            f"sacctmgr -i modify cluster {cluster} set features={feature}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        assert "Setting" in output
        assert re.search(rf"Feature\s+=\s+{feature}", output)
        assert "Modified cluster" in output
    atf.wait_for_federation_sync()


def sbatch_job(cluster=None):
    """Submit a batch job. Pass cluster=clsN to pin a fill job to a
    specific cluster; omit to submit federation-wide (needed to exercise
    multi-sibling behavior).
    """
    extra = f" -M{cluster}" if cluster else ""
    return atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null"
        f" --error=/dev/null --requeue{extra} --wrap 'sleep infinity'",
        fatal=True,
    )


def scontrol_show_job(option, job_id, extra_args=""):
    """Run scontrol with --local or --sibling to show job."""
    cmd = f"scontrol {option} show job {job_id}"
    if option == "--local":
        cmd += " -a"
    if extra_args:
        cmd += f" {extra_args}"
    return atf.run_command_output(cmd)


def squeue_job(job_id, clusters=None):
    """Check job state via squeue across clusters."""
    if clusters is None:
        clusters = f"{cls1},{cls2},{cls3}"
    return atf.run_command_output(
        f"squeue --noheader -a --local -Ocluster:10,statecompact:.5"
        f" -j {job_id} -M{clusters}",
    )


def scancel_sibling(sibling, job_id):
    """Run scancel --sibling=<cluster> <job_id>."""
    return atf.run_command(f"scancel --sibling={sibling} {job_id}")


def verify_job_all_clusters(job_id, running_cluster, origin_cluster=None):
    """Verify job state on ALL clusters:

    - Running cluster: scontrol shows RUNNING
    - Origin cluster (if not running): scontrol shows REVOKED
    - Other clusters: scontrol shows "Invalid job id"
    - squeue cross-check: job shows R on the correct cluster

    origin_cluster defaults to cls1 since all jobs in this test are submitted
    from cls1.
    """
    if origin_cluster is None:
        origin_cluster = cls1
    all_clusters = [cls1, cls2, cls3]

    atf.wait_for_job_state(job_id, "RUNNING", clusters=running_cluster, timeout=60)

    # Running cluster should show RUNNING
    output = scontrol_show_job("--local", job_id, f"-M{running_cluster}")
    assert re.search(r"JobId", output), f"Job not found on {running_cluster}: {output}"
    assert re.search(
        r"JobState=RUNNING", output
    ), f"Job not RUNNING on {running_cluster}: {output}"

    # Non-running clusters: the origin shows REVOKED and the other ones have
    # their sibling removed ("Invalid job id"). Use polling since sibling
    # propagation takes time.
    for cluster in all_clusters:
        if cluster == running_cluster:
            continue
        expected = "REVOKED" if cluster == origin_cluster else "Invalid job id"
        for _ in atf.timer(fatal=True):
            result = atf.run_command(
                f"scontrol -M{cluster} --local -a -o show job {job_id}",
            )
            if expected in result["stdout"] + result["stderr"]:
                break

    # Cross-check via squeue: verify job shows R on the correct cluster
    sq_output = squeue_job(job_id)
    assert re.search(
        rf"{running_cluster}\s+R", sq_output
    ), f"squeue should show job {job_id} as R on {running_cluster}: {sq_output}"


def verify_sibling_state_cumulative(
    job_id, canceled_clusters, pending_clusters, origin_cluster=None
):
    """After scancel --sibling operations, verify ALL cluster states cumulatively.

    canceled_clusters: list of clusters that have been canceled via --sibling
    pending_clusters: list of clusters that should still be pending
    origin_cluster: the cluster where the job was submitted (defaults to cls1).
        Canceled origin cluster should show REVOKED; canceled non-origin
        clusters should show Invalid job id.
    """
    if origin_cluster is None:
        origin_cluster = cls1

    for cluster in canceled_clusters:
        if cluster == origin_cluster:

            def check_revoked(c=cluster):
                result = atf.run_command(
                    f"scontrol -M{c} --local -a -o show job {job_id}",
                )
                combined = result.get("stdout", "") + result.get("stderr", "")
                return re.search(r"REVOKED", combined)

            assert atf.repeat_until(
                check_revoked, lambda x: x, timeout=15
            ), f"Job should be REVOKED on origin cluster {cluster} after scancel --sibling"
        else:

            def check_invalid(c=cluster):
                result = atf.run_command(
                    f"scontrol -M{c} --local -a -o show job {job_id}",
                )
                combined = result.get("stdout", "") + result.get("stderr", "")
                return "Invalid job id" in combined

            assert atf.repeat_until(
                check_invalid, lambda x: x, timeout=15
            ), f"Job should be Invalid job id on non-origin cluster {cluster} after scancel --sibling"

    for cluster in pending_clusters:
        output = squeue_job(job_id, cluster)
        assert re.search(
            r"\s+PD\s+", output
        ), f"Job should still be pending on {cluster}: {output}"


@pytest.fixture(scope="function", autouse=True)
def cleanup_between_tests():
    """Function-level fixture to clean up jobs after each test."""
    yield
    cancel_federation_jobs()


def test_scontrol_local_and_sibling():
    """Test scontrol --local and --sibling options with federated jobs."""
    ji0 = sbatch_job(cls1)
    ji1 = sbatch_job(cls2)
    ji2 = sbatch_job(cls3)

    atf.wait_for_job_state(ji0, "RUNNING", siblings=True, clusters=cls1, fatal=True)
    atf.wait_for_job_state(ji1, "RUNNING", siblings=True, clusters=cls2, fatal=True)
    atf.wait_for_job_state(ji2, "RUNNING", siblings=True, clusters=cls3, fatal=True)

    # Submit 4th job - should be pending across all clusters
    job_id = sbatch_job()

    # Wait for pending job to propagate to all clusters
    atf.wait_for_job_state(
        job_id, "PENDING", siblings=True, all_clusters=True, fatal=True
    )

    # Test scontrol --sibling: should show one record of the job per cluster
    output = scontrol_show_job("--sibling", job_id)
    job_id_matches = re.findall(r"JobId", output)
    assert len(job_id_matches) == 3, (
        f"scontrol --sibling should show job from all 3 clusters, "
        f"found {len(job_id_matches)} JobId entries: {output}"
    )

    # Verify --local on each cluster sees the pending fed job
    for cluster in [cls1, cls2, cls3]:
        output = scontrol_show_job("--local", job_id, f"-M{cluster}")
        assert re.search(
            r"JobId", output
        ), f"--local on {cluster} should see fed job {job_id}: {output}"


def test_running_sibling_revokes_origin():
    """Test that a job started on a sibling revokes the origin job and
    removes the other siblings."""
    jobs = [sbatch_job() for _ in range(3)]

    # With one node per cluster, each job starts on a different cluster
    for job_id in jobs:
        atf.wait_for_job_state(job_id, "RUNNING", siblings=True, fatal=True)
        running = atf.get_job_clusters(job_id, "RUNNING")
        assert len(running) == 1, f"Job {job_id} should run on one cluster: {running}"
        verify_job_all_clusters(job_id, running[0])


def test_scancel_sibling():
    """Test scancel --sibling option to remove pending siblings."""
    ji0 = sbatch_job(cls1)
    ji1 = sbatch_job(cls2)
    ji2 = sbatch_job(cls3)

    atf.wait_for_job_state(ji0, "RUNNING", siblings=True, clusters=cls1, fatal=True)

    atf.wait_for_job_state(ji1, "RUNNING", siblings=True, clusters=cls2, fatal=True)

    atf.wait_for_job_state(ji2, "RUNNING", siblings=True, clusters=cls3, fatal=True)

    # Test scancel --sibling on running jobs - should fail with error
    result = scancel_sibling(cls1, ji0)
    output = result.get("stdout", "") + result.get("stderr", "")
    assert re.search(
        r"Job is no longer pending execution", output
    ), f"Expected 'no longer pending' error for running job {ji0}: {output}"

    result = scancel_sibling(cls1, ji1)
    output = result.get("stdout", "") + result.get("stderr", "")
    assert re.search(
        r"Job is no longer pending execution", output
    ), f"Expected 'no longer pending' error for running job {ji1}: {output}"

    result = scancel_sibling(cls1, ji2)
    output = result.get("stdout", "") + result.get("stderr", "")
    assert re.search(
        r"Job is no longer pending execution", output
    ), f"Expected 'no longer pending' error for running job {ji2}: {output}"

    # Submit a new pending job (clusters are full)
    job_id = sbatch_job()

    # Wait for pending state to propagate to all 3 clusters
    atf.wait_for_job_state(
        job_id, "PENDING", siblings=True, all_clusters=True, fatal=True
    )

    # Remove sibling from cls1 - cumulative verification
    scancel_sibling(cls1, job_id)
    verify_sibling_state_cumulative(job_id, [cls1], [cls2, cls3])

    # Remove sibling from cls2 - verify BOTH cls1 and cls2 are gone
    scancel_sibling(cls2, job_id)
    verify_sibling_state_cumulative(job_id, [cls1, cls2], [cls3])

    # Remove sibling from cls3 - verify ALL three are gone
    scancel_sibling(cls3, job_id)
    verify_sibling_state_cumulative(job_id, [cls1, cls2, cls3], [])
