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
node_count = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global cls1, cls2, cls3
    cls1, cls2, cls3 = atf.require_multicluster(3, federation=True)
    for _ in atf.foreach_cluster():
        atf.require_nodes(1)
    atf.require_slurm_running()

    global user_name, node_count

    user_name = atf.get_user_name()

    # Use all the nodes of a cluster, so a job fills it
    output = atf.run_command_output(
        "sinfo --local -h -t idle,alloc,comp -o '%D'", fatal=True
    )
    node_count = int(output.split()[0])

    cancel_federation_jobs()
    setup_cluster_features()

    yield

    cancel_federation_jobs()


def cancel_federation_jobs():
    """Cancel all federation jobs for the test user."""
    if user_name:
        atf.run_command(
            f"scancel -M{cls1},{cls2},{cls3} --user {user_name}",
        )
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
        assert re.search(rf"^\s+{cluster}", output, re.MULTILINE)
    atf.wait_for_federation_sync()


def sbatch_fed_job(constraint, extra_args=""):
    """Submit a federated batch job with cluster constraint. Returns job ID."""
    args = f"-N{node_count} --exclusive --output=/dev/null --error=/dev/null"
    if constraint:
        args += f" --cluster-constraint={constraint}"
    if extra_args:
        args += f" {extra_args}"

    return atf.submit_job_sbatch(f"{args} --wrap 'sleep infinity'", fatal=True)


def modify_job_clusterfeatures(job_id, constraint, expected_error=""):
    """Modify job cluster features using scontrol update."""
    result = atf.run_command(
        f"scontrol update jobid={job_id} clusterfeatures={constraint}",
    )

    output = result.get("stdout", "") + result.get("stderr", "")

    if expected_error:
        assert re.search(
            expected_error, output
        ), f"Expected error '{expected_error}' not found in output: {output}"
    else:
        assert (
            result["exit_code"] == 0 or not output.strip()
        ), f"Unexpected error in output: {output}"


def modify_job_clusters(job_id, clusters, expected_error=""):
    """Modify job clusters using scontrol update."""
    result = atf.run_command(
        f"scontrol update jobid={job_id} clusters={clusters}",
    )

    output = result.get("stdout", "") + result.get("stderr", "")

    if expected_error:
        assert re.search(
            expected_error, output
        ), f"Expected error '{expected_error}' not found in output: {output}"
    else:
        assert (
            result["exit_code"] == 0 or not output.strip()
        ), f"Unexpected error in output: {output}"


def verify_job_state(
    cluster,
    job_id,
    expected_state,
    expected_siblings_viable,
    expected_siblings_active,
    error_message,
):
    """Verify job state on a specific cluster.

    Polls with repeat_until to allow federation propagation time.
    """

    def check():
        result = atf.run_command(
            f"squeue -M{cluster} -a --local -j {job_id} --noheader "
            f"-Ostatecompact:.5,siblingsviable:.30,siblingsactive:.30",
        )
        output = result["stdout"]

        # If expecting "Invalid" pattern, empty output or stderr with "Invalid" counts
        if "Invalid" in expected_state:
            combined = output + result.get("stderr", "")
            return not output.strip() or "Invalid" in combined

        pattern = rf"\s*{expected_state}\s+{expected_siblings_viable}\s+{expected_siblings_active}"
        return re.search(pattern, output)

    assert atf.repeat_until(check, lambda x: x, timeout=60), error_message


@pytest.fixture(scope="function", autouse=True)
def clean_jobs():
    """Function-level fixture to clean up jobs between tests."""
    cancel_federation_jobs()
    yield
    cancel_federation_jobs()


def test_modify_running_job_features():
    """Test that modifying cluster features of a running job fails."""
    job_id = sbatch_fed_job("fa")

    atf.wait_for_job_state(job_id, "RUNNING", siblings=True, fatal=True)
    run_cluster = atf.get_job_clusters(job_id, "RUNNING")
    assert run_cluster == [cls1], f"Job not running on {cls1}"

    verify_job_state(
        cls1, job_id, "R", cls1, cls1, "Job not running on designated cluster"
    )

    modify_job_clusterfeatures(
        job_id, "fb", expected_error="Job is no longer pending execution for job"
    )


def test_invalid_feature():
    """Test that invalid cluster feature specification fails."""
    fill_job = sbatch_fed_job("fa")
    atf.wait_for_job_state(fill_job, "RUNNING", siblings=True, fatal=True)

    job_id = sbatch_fed_job("fa")
    atf.wait_for_job_state(job_id, "PENDING", siblings=True, fatal=True)

    verify_job_state(
        cls1, job_id, "PD", cls1, cls1, "Job not pending on designated cluster"
    )

    modify_job_clusterfeatures(
        job_id, "invalid", expected_error="Invalid cluster feature specification"
    )


def test_move_pending_job_to_fb():
    """Test moving pending job from fa to fb cluster."""
    fill_job = sbatch_fed_job("fa")
    atf.wait_for_job_state(fill_job, "RUNNING", siblings=True, fatal=True)

    job_id = sbatch_fed_job("fa")
    atf.wait_for_job_state(job_id, "PENDING", siblings=True, fatal=True)

    verify_job_state(cls1, job_id, "PD", cls1, cls1, "Job not pending on cls1")

    modify_job_clusterfeatures(job_id, "fb")

    atf.wait_for_job_state(job_id, "RUNNING", siblings=True, fatal=True)
    run_cluster = atf.get_job_clusters(job_id, "RUNNING")
    assert run_cluster == [cls2], f"Job not running on {cls2}"

    verify_job_state(cls1, job_id, "RV", cls2, cls2, "Job not revoked on cls1")
    verify_job_state(cls2, job_id, "R", cls2, cls2, "Job not running on cls2")


def test_move_job_to_multiple_clusters():
    """Test moving job to multiple clusters using features fb,fc."""
    fill_job = sbatch_fed_job("fa")
    atf.wait_for_job_state(fill_job, "RUNNING", siblings=True, fatal=True)

    job_id = sbatch_fed_job("fa")
    atf.wait_for_job_state(job_id, "PENDING", siblings=True, fatal=True)

    modify_job_clusterfeatures(job_id, "fb,fc")

    atf.wait_for_job_state(job_id, "RUNNING", siblings=True, fatal=True)
    running = atf.get_job_clusters(job_id, "RUNNING")
    assert len(running) == 1 and running[0] in [
        cls2,
        cls3,
    ], f"Job not running on {cls2} or {cls3}: {running}"
    run_cluster = running[0]

    verify_job_state(
        cls1, job_id, "RV", f"{cls2},{cls3}", run_cluster, "Job not revoked on cls1"
    )
    verify_job_state(
        run_cluster,
        job_id,
        "R",
        f"{cls2},{cls3}",
        run_cluster,
        f"Job not running on {run_cluster}",
    )


def test_move_job_to_fc_only():
    """Test moving job to fc cluster only."""
    fill1 = sbatch_fed_job("fa")
    fill2 = sbatch_fed_job("fb")
    fill3 = sbatch_fed_job("fc")
    atf.wait_for_job_state(fill1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill2, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill3, "RUNNING", siblings=True, fatal=True)

    job_id = sbatch_fed_job("fa")
    atf.wait_for_job_state(job_id, "PENDING", siblings=True, fatal=True)

    modify_job_clusterfeatures(job_id, "fc")

    verify_job_state(cls1, job_id, "RV", cls3, cls3, "Job not revoked on cls1")
    verify_job_state(cls3, job_id, "PD", cls3, "NA", "Job not pending on cls3")


def test_move_job_fa_fc():
    """Test moving job to fa,fc clusters."""
    fill1 = sbatch_fed_job("fa")
    fill2 = sbatch_fed_job("fb")
    fill3 = sbatch_fed_job("fc")
    atf.wait_for_job_state(fill1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill2, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill3, "RUNNING", siblings=True, fatal=True)

    job_id = sbatch_fed_job("fc")

    modify_job_clusterfeatures(job_id, "fa,fc")

    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls3}",
        f"{cls1},{cls3}",
        "Job not pending on cls1,cls3",
    )
    verify_job_state(
        cls2, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls2"
    )
    verify_job_state(
        cls3, job_id, "PD", f"{cls1},{cls3}", "NA", "Job not pending on cls3"
    )


def test_move_job_to_fa():
    """Test moving job to fa cluster only."""
    fill1 = sbatch_fed_job("fa")
    fill2 = sbatch_fed_job("fb")
    fill3 = sbatch_fed_job("fc")
    atf.wait_for_job_state(fill1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill2, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill3, "RUNNING", siblings=True, fatal=True)

    job_id = sbatch_fed_job("fa,fc")

    modify_job_clusterfeatures(job_id, "fa")

    verify_job_state(cls1, job_id, "PD", cls1, cls1, "Job not pending on cls1")
    verify_job_state(
        cls2, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls2"
    )
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )


def test_clear_features():
    """Test clearing job features makes job eligible on all clusters."""
    fill1 = sbatch_fed_job("fa")
    fill2 = sbatch_fed_job("fb")
    fill3 = sbatch_fed_job("fc")
    atf.wait_for_job_state(fill1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill2, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill3, "RUNNING", siblings=True, fatal=True)

    job_id = sbatch_fed_job("fa")

    modify_job_clusterfeatures(job_id, "")

    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        f"{cls1},{cls2},{cls3}",
        "Job not pending on all clusters",
    )
    verify_job_state(
        cls2, job_id, "PD", f"{cls1},{cls2},{cls3}", "NA", "Job not pending on cls2"
    )
    verify_job_state(
        cls3, job_id, "PD", f"{cls1},{cls2},{cls3}", "NA", "Job not pending on cls3"
    )


def test_modify_held_job_features():
    """Test modifying cluster features of a held job."""
    job_id = sbatch_fed_job("fa", "-H")

    verify_job_state(cls1, job_id, "PD", cls1, cls1, "Job not pending on cls1")
    verify_job_state(
        cls2, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls2"
    )
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    modify_job_clusterfeatures(job_id, "fb,fc")

    verify_job_state(
        cls1, job_id, "RV", f"{cls2},{cls3}", "NA", "Job not revoked on cls1"
    )
    verify_job_state(
        cls2,
        job_id,
        ".+Invalid.+",
        ".*",
        ".*",
        "Job should not be visible on cls2 (held)",
    )
    verify_job_state(
        cls3,
        job_id,
        ".+Invalid.+",
        ".*",
        ".*",
        "Job should not be visible on cls3 (held)",
    )

    modify_job_clusterfeatures(job_id, "")

    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        "NA",
        "Job not pending on all clusters",
    )

    modify_job_clusterfeatures(job_id, "fc")

    verify_job_state(cls1, job_id, "PD", cls3, "NA", "Job not pending on cls3 only")
    verify_job_state(
        cls2,
        job_id,
        ".+Invalid.+",
        ".*",
        ".*",
        "Job should not be visible on cls2 after fc",
    )

    atf.run_command(
        f"scontrol release {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "RUNNING", siblings=True, fatal=True)
    run_cluster = atf.get_job_clusters(job_id, "RUNNING")
    assert run_cluster == [cls3], f"Job not running on {cls3}"

    verify_job_state(cls1, job_id, "RV", cls3, cls3, "Job not revoked on cls1")
    verify_job_state(cls3, job_id, "R", cls3, cls3, "Job not running on cls3")
    verify_job_state(
        cls2,
        job_id,
        ".+Invalid.+",
        ".*",
        ".*",
        "Job should not be visible on cls2 after release with fc constraint",
    )


def test_clear_features_requeued_job():
    """Test clearing and setting features on a pending job across all clusters."""
    fill1 = sbatch_fed_job("fa")
    fill2 = sbatch_fed_job("fb")
    fill3 = sbatch_fed_job("fc")
    atf.wait_for_job_state(fill1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill2, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill3, "RUNNING", siblings=True, fatal=True)

    job_id = sbatch_fed_job("", "--requeue")

    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        f"{cls1},{cls2},{cls3}",
        "Job not pending on all clusters",
    )

    modify_job_clusterfeatures(job_id, "fa")

    verify_job_state(cls1, job_id, "PD", cls1, cls1, "Job not pending on cls1 only")
    verify_job_state(
        cls2, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls2"
    )
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    modify_job_clusterfeatures(job_id, "")

    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        f"{cls1},{cls2},{cls3}",
        "Job not pending on all clusters",
    )
    verify_job_state(
        cls2, job_id, "PD", f"{cls1},{cls2},{cls3}", "NA", "Job not pending on cls2"
    )
    verify_job_state(
        cls3, job_id, "PD", f"{cls1},{cls2},{cls3}", "NA", "Job not pending on cls3"
    )


def test_modify_clusters():
    """Test modifying job clusters using scontrol update clusters=."""
    fill1 = sbatch_fed_job("fa")
    fill2 = sbatch_fed_job("fb")
    fill3 = sbatch_fed_job("fc")
    atf.wait_for_job_state(fill1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill2, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill3, "RUNNING", siblings=True, fatal=True)

    job_id = sbatch_fed_job("")

    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        f"{cls1},{cls2},{cls3}",
        "Job not pending on all clusters",
    )

    modify_job_clusters(job_id, cls1)

    verify_job_state(cls1, job_id, "PD", cls1, cls1, "Job not pending on cls1 only")
    verify_job_state(
        cls2, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls2"
    )
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    modify_job_clusters(job_id, f"{cls1},{cls2}")

    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls2}",
        f"{cls1},{cls2}",
        "Job not pending on cls1,cls2",
    )
    verify_job_state(
        cls2, job_id, "PD", f"{cls1},{cls2}", "NA", "Job not pending on cls2"
    )
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    modify_job_clusters(job_id, cls2)

    verify_job_state(cls1, job_id, "RV", cls2, cls2, "Job not revoked on cls1")
    verify_job_state(cls2, job_id, "PD", cls2, "NA", "Job not pending on cls2")
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    modify_job_clusters(job_id, "")

    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        f"{cls1},{cls2},{cls3}",
        "Job not pending on all clusters",
    )
    verify_job_state(
        cls2, job_id, "PD", f"{cls1},{cls2},{cls3}", "NA", "Job not pending on cls2"
    )
    verify_job_state(
        cls3, job_id, "PD", f"{cls1},{cls2},{cls3}", "NA", "Job not pending on cls3"
    )


def test_route_update_from_cls1_to_cls2():
    """Test routing job update from cls1 to job on cls2."""
    fill1 = sbatch_fed_job("fa")
    fill2 = sbatch_fed_job("fb")
    fill3 = sbatch_fed_job("fc")
    atf.wait_for_job_state(fill1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill2, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill3, "RUNNING", siblings=True, fatal=True)

    # Submit job directly to cls2
    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null "
        f"--error=/dev/null -M{cls2} --wrap 'sleep infinity'",
        fatal=True,
    )

    verify_job_state(cls2, job_id, "PD", cls2, cls2, "Job not pending on cls2")
    verify_job_state(
        cls1, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls1"
    )
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    modify_job_clusters(job_id, cls1)

    verify_job_state(cls2, job_id, "RV", cls1, cls1, "Job not revoked on cls2")
    verify_job_state(cls1, job_id, "PD", cls1, "NA", "Job not pending on cls1")
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    modify_job_clusters(job_id, "")

    verify_job_state(
        cls2,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        f"{cls1},{cls2},{cls3}",
        "Job not pending on all clusters",
    )
    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        "NA",
        "Job not pending on cls1 after clearing clusters",
    )
    verify_job_state(
        cls3,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        "NA",
        "Job not pending on cls3 after clearing clusters",
    )


def test_clusters_and_features_conflict():
    """Test conflict between clusters and clusterfeatures constraints."""
    fill1 = sbatch_fed_job("fa")
    fill2 = sbatch_fed_job("fb")
    fill3 = sbatch_fed_job("fc")
    atf.wait_for_job_state(fill1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill2, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill3, "RUNNING", siblings=True, fatal=True)

    # Submit job directly to cls2
    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null "
        f"--error=/dev/null -M{cls2} --wrap 'sleep infinity'",
        fatal=True,
    )

    modify_job_clusters(job_id, cls1)

    verify_job_state(cls2, job_id, "RV", cls1, cls1, "Job not revoked on cls2")
    verify_job_state(cls1, job_id, "PD", cls1, "NA", "Job not pending on cls1")
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    # Set clusterfeatures=fb - creates conflict (cls1 has fa, not fb)
    modify_job_clusterfeatures(job_id, "fb")

    verify_job_state(cls2, job_id, "RV", "NA", "NA", "Job should show NA for siblings")
    verify_job_state(
        cls1, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls1"
    )
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    # Set clusterfeatures=fa - resolves conflict
    modify_job_clusterfeatures(job_id, "fa")

    verify_job_state(cls2, job_id, "RV", cls1, cls1, "Job not revoked on cls2")
    verify_job_state(cls1, job_id, "PD", cls1, "NA", "Job not pending on cls1")
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )


def test_negative_cluster_features_submission():
    """Test submitting jobs with negative cluster features."""
    job1 = sbatch_fed_job("!fc,fb")
    job2 = sbatch_fed_job("!fa,fc")
    job3 = sbatch_fed_job("!fb,fa")

    atf.wait_for_job_state(job1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(job2, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(job3, "RUNNING", siblings=True, fatal=True)

    verify_job_state(cls1, job1, "R", cls1, cls1, "Job 1 not running on cls1")
    verify_job_state(cls2, job2, "R", cls2, cls2, "Job 2 not running on cls2")
    verify_job_state(cls3, job3, "R", cls3, cls3, "Job 3 not running on cls3")


def test_negative_features_modification():
    """Test modifying job to use negative cluster features."""
    fill1 = sbatch_fed_job("!fc,fb")
    fill2 = sbatch_fed_job("!fa,fc")
    fill3 = sbatch_fed_job("!fb,fa")
    atf.wait_for_job_state(fill1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill2, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill3, "RUNNING", siblings=True, fatal=True)

    job_id = sbatch_fed_job("")

    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        f"{cls1},{cls2},{cls3}",
        "Job not pending on all clusters",
    )

    # Modify to !fb (exclude cls2)
    modify_job_clusterfeatures(job_id, "!fb")

    verify_job_state(
        cls2, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls2"
    )
    verify_job_state(
        cls1, job_id, "PD", f"{cls1},{cls3}", ".*", "Job not pending on cls1,cls3"
    )
    verify_job_state(
        cls3, job_id, "PD", f"{cls1},{cls3}", ".*", "Job not pending on cls3"
    )

    # Modify to !fc (exclude cls3)
    modify_job_clusterfeatures(job_id, "!fc")

    verify_job_state(
        cls2, job_id, "PD", f"{cls1},{cls2}", ".*", "Job not pending on cls1,cls2"
    )
    verify_job_state(
        cls1, job_id, "PD", f"{cls1},{cls2}", ".*", "Job not pending on cls1,cls2"
    )
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    # Modify to !fa (exclude cls1)
    modify_job_clusterfeatures(job_id, "!fa")

    verify_job_state(
        cls1, job_id, "RV", f"{cls2},{cls3}", ".*", "Job should be revoked on cls1"
    )
    verify_job_state(
        cls2, job_id, "PD", f"{cls2},{cls3}", ".*", "Job not pending on cls2,cls3"
    )
    verify_job_state(
        cls3, job_id, "PD", f"{cls2},{cls3}", ".*", "Job not pending on cls3"
    )

    # Modify to !fb,fc (exclude cls2 and cls3)
    modify_job_clusterfeatures(job_id, "!fb,fc")

    verify_job_state(
        cls2, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls2"
    )
    verify_job_state(cls1, job_id, "PD", cls1, ".*", "Job not pending on cls1 only")
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    # Modify to !fa,fc (exclude cls1 and cls3)
    modify_job_clusterfeatures(job_id, "!fa,fc")

    verify_job_state(cls1, job_id, "RV", cls2, cls2, "Job should be revoked on cls1")
    verify_job_state(cls2, job_id, "PD", cls2, ".*", "Job not pending on cls2 only")
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    # Modify to !fb,fa (exclude cls2 and cls1)
    modify_job_clusterfeatures(job_id, "!fb,fa")

    verify_job_state(
        cls2, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls2"
    )
    verify_job_state(cls3, job_id, "PD", cls3, ".*", "Job not pending on cls3 only")
    verify_job_state(cls1, job_id, "RV", cls3, cls3, "Job should be revoked on cls1")

    # Clear features
    modify_job_clusterfeatures(job_id, "")

    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        f"{cls1},{cls2},{cls3}",
        "Job not pending on all clusters",
    )


def test_negative_features_single_cluster_submission():
    """Test submitting jobs with negative features targeting single clusters."""
    fill1 = sbatch_fed_job("!fc,fb")
    fill2 = sbatch_fed_job("!fa,fc")
    fill3 = sbatch_fed_job("!fb,fa")
    atf.wait_for_job_state(fill1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill2, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill3, "RUNNING", siblings=True, fatal=True)

    # Submit job with !fc,fb - should only be eligible on cls1
    job1 = sbatch_fed_job("!fc,fb")
    atf.wait_for_job_state(job1, "PENDING", siblings=True, fatal=True)

    verify_job_state(
        cls2, job1, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls2"
    )
    verify_job_state(cls1, job1, "PD", cls1, cls1, "Job not pending on cls1")
    verify_job_state(
        cls3, job1, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    # Submit job with !fa,fb - should only be eligible on cls3
    job2 = sbatch_fed_job("!fa,fb")

    verify_job_state(
        cls2, job2, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls2"
    )
    verify_job_state(cls3, job2, "PD", cls3, "NA", "Job not pending on cls3")
    verify_job_state(cls1, job2, "RV", cls3, cls3, "Job should be revoked on cls1")

    # Submit job with !fc,fa - should only be eligible on cls2
    job3 = sbatch_fed_job("!fc,fa")

    verify_job_state(cls1, job3, "RV", cls2, cls2, "Job should be revoked on cls1")
    verify_job_state(cls2, job3, "PD", cls2, "NA", "Job not pending on cls2")
    verify_job_state(
        cls3, job3, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )


def test_negate_all_clusters_error():
    """Test that negating all cluster features fails."""
    result = atf.run_command(
        f"sbatch -N{node_count} --exclusive --output=/dev/null "
        f"--error=/dev/null --cluster-constraint=!fa,fb,fc --wrap 'sleep infinity'",
    )

    output = result.get("stdout", "") + result.get("stderr", "")
    assert (
        "Invalid cluster feature specification" in output
    ), f"Expected 'Invalid cluster feature specification' error, got: {output}"


def test_invalid_then_valid_modification_same_job():
    """Test invalid modification followed by valid modification on same job.

    Submit a pending job with fa, attempt invalid modification, then
    successfully modify to fb on the SAME job.
    """
    fill1 = sbatch_fed_job("fa")
    atf.wait_for_job_state(fill1, "RUNNING", siblings=True, fatal=True)

    job_id = sbatch_fed_job("fa")
    atf.wait_for_job_state(job_id, "PENDING", siblings=True, fatal=True)

    verify_job_state(cls1, job_id, "PD", cls1, cls1, "Job not pending on cls1")

    # Attempt invalid modification
    modify_job_clusterfeatures(
        job_id, "invalid", expected_error="Invalid cluster feature specification"
    )

    # Job should still be pending on cls1 after invalid modification
    verify_job_state(
        cls1, job_id, "PD", cls1, cls1, "Job not pending on cls1 after invalid mod"
    )

    # Now successfully modify to fb
    modify_job_clusterfeatures(job_id, "fb")

    atf.wait_for_job_state(job_id, "RUNNING", siblings=True, fatal=True)
    run_cluster = atf.get_job_clusters(job_id, "RUNNING")
    assert run_cluster == [cls2], f"Job not running on {cls2}"

    verify_job_state(cls1, job_id, "RV", cls2, cls2, "Job not revoked on cls1")
    verify_job_state(cls2, job_id, "R", cls2, cls2, "Job not running on cls2")


def test_chained_feature_modifications_single_job():
    """Test chaining multiple feature modifications on a single job.

    Chain: fa -> fc -> fa,fc -> fa -> clear, all on ONE job.
    """
    fill1 = sbatch_fed_job("fa")
    fill2 = sbatch_fed_job("fb")
    fill3 = sbatch_fed_job("fc")
    atf.wait_for_job_state(fill1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill2, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill3, "RUNNING", siblings=True, fatal=True)

    job_id = sbatch_fed_job("fa")
    atf.wait_for_job_state(job_id, "PENDING", siblings=True, fatal=True)

    verify_job_state(cls1, job_id, "PD", cls1, cls1, "Job not pending on cls1")

    # Chain 1: fa -> fc
    modify_job_clusterfeatures(job_id, "fc")
    verify_job_state(cls1, job_id, "RV", cls3, cls3, "Job not revoked on cls1 after fc")
    verify_job_state(cls3, job_id, "PD", cls3, "NA", "Job not pending on cls3 after fc")

    # Chain 2: fc -> fa,fc
    modify_job_clusterfeatures(job_id, "fa,fc")
    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls3}",
        f"{cls1},{cls3}",
        "Job not pending on cls1,cls3",
    )
    verify_job_state(
        cls3, job_id, "PD", f"{cls1},{cls3}", "NA", "Job not pending on cls3"
    )

    # Chain 3: fa,fc -> fa
    modify_job_clusterfeatures(job_id, "fa")
    verify_job_state(cls1, job_id, "PD", cls1, cls1, "Job not pending on cls1 after fa")
    verify_job_state(
        cls3, job_id, ".+Invalid.+", ".*", ".*", "Job should not be visible on cls3"
    )

    # Chain 4: fa -> clear (all clusters)
    modify_job_clusterfeatures(job_id, "")
    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        f"{cls1},{cls2},{cls3}",
        "Job not pending on all clusters after clear",
    )
    verify_job_state(
        cls2, job_id, "PD", f"{cls1},{cls2},{cls3}", "NA", "Job not pending on cls2"
    )
    verify_job_state(
        cls3, job_id, "PD", f"{cls1},{cls2},{cls3}", "NA", "Job not pending on cls3"
    )


def test_held_job_clear_features_full_verification():
    """Test clearing features on held job verifies cls2/cls3 show Invalid.

    After clearing features on a held job, cls2 and cls3 should show
    Invalid because held jobs don't propagate to siblings.
    """
    job_id = sbatch_fed_job("fa", "-H")

    verify_job_state(cls1, job_id, "PD", cls1, cls1, "Job not pending on cls1")

    # Modify to fb,fc - held job goes to REVOKED on cls1, Invalid on cls2/cls3
    modify_job_clusterfeatures(job_id, "fb,fc")

    verify_job_state(
        cls1, job_id, "RV", f"{cls2},{cls3}", "NA", "Job not revoked on cls1"
    )
    verify_job_state(
        cls2,
        job_id,
        ".+Invalid.+",
        ".*",
        ".*",
        "Held job should not be visible on cls2",
    )
    verify_job_state(
        cls3,
        job_id,
        ".+Invalid.+",
        ".*",
        ".*",
        "Held job should not be visible on cls3",
    )

    # Clear features - job should return to cls1 but still held
    modify_job_clusterfeatures(job_id, "")

    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        "NA",
        "Job not pending on all clusters",
    )
    # cls2 and cls3 should still show Invalid because the job is held
    verify_job_state(
        cls2,
        job_id,
        ".+Invalid.+",
        ".*",
        ".*",
        "Held job should not be visible on cls2 after clear",
    )
    verify_job_state(
        cls3,
        job_id,
        ".+Invalid.+",
        ".*",
        ".*",
        "Held job should not be visible on cls3 after clear",
    )

    # Release and verify it runs
    atf.run_command(
        f"scontrol release {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "RUNNING", siblings=True, fatal=True)


def test_requeued_job_initial_cls2_cls3_verification():
    """Test initial verification of requeued job on cls2 and cls3.

    After submitting with --requeue and no constraint, verify PD on
    cls2 and cls3 with siblings_active=NA.
    """
    fill1 = sbatch_fed_job("fa")
    fill2 = sbatch_fed_job("fb")
    fill3 = sbatch_fed_job("fc")
    atf.wait_for_job_state(fill1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill2, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(fill3, "RUNNING", siblings=True, fatal=True)

    job_id = sbatch_fed_job("", "--requeue")

    # Verify initial state: PD on all clusters
    verify_job_state(
        cls1,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        f"{cls1},{cls2},{cls3}",
        "Job not pending on all clusters",
    )

    # Verify cls2 and cls3 show PD with siblings_active=NA
    verify_job_state(
        cls2,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        "NA",
        "Job not pending on cls2 with NA",
    )
    verify_job_state(
        cls3,
        job_id,
        "PD",
        f"{cls1},{cls2},{cls3}",
        "NA",
        "Job not pending on cls3 with NA",
    )
