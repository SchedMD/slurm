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
        atf.require_nodes(5)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def federation_setup():
    """Module-level federation setup."""
    global user_name, node_count

    user_name = atf.get_user_name()

    # Get node count
    output = atf.run_command_output(
        "sinfo -h --local -o '%D' -t idle,alloc,comp", fatal=True
    )
    node_count = int(output.strip()) if output.strip() else 1

    cancel_federation_jobs()

    yield

    cancel_federation_jobs()


@pytest.fixture(scope="module")
def federation_with_features(federation_setup):
    """Set up cluster features for federation tests."""
    for cluster, feature in [(cls1, "fa"), (cls2, "fb"), (cls3, "fc")]:
        atf.run_command(
            f"sacctmgr -i modify cluster {cluster} set features={feature}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    atf.wait_for_federation_sync()
    return {cls1: "fa", cls2: "fb", cls3: "fc"}


def cancel_federation_jobs():
    """Cancel all federation jobs for the test user."""
    if user_name:
        atf.run_command(f"scancel -M{cls1},{cls2},{cls3} --user {user_name}")
        atf.repeat_until(
            lambda: atf.run_command(
                f"squeue -M{cls1},{cls2},{cls3} --user {user_name} --noheader",
            ),
            lambda result: result["stdout"].strip() == "",
            timeout=10,
        )


def test_sbatch_test_only_basic(federation_setup):
    """Test basic sbatch --test-only functionality."""
    error = atf.run_command_error(
        "sbatch -N1 --exclusive --output=/dev/null -t1 --wrap 'sleep 30' --test-only"
    )
    assert (
        "Job" in error and "to start at" in error
    ), f"Expected test-only scheduling output not found: {error}"
    assert (
        "using" in error and "processors" in error
    ), f"Expected resource allocation info not found: {error}"
    assert "nodes" in error, f"Expected node allocation info not found: {error}"


def test_srun_test_only_basic(federation_setup):
    """Test basic srun --test-only functionality."""
    error = atf.run_command_error("srun -N1 --exclusive --test-only sleep 30")
    assert (
        "Job" in error and "to start at" in error
    ), f"Expected test-only scheduling output not found: {error}"
    assert (
        "using" in error and "processors" in error
    ), f"Expected resource allocation info not found: {error}"
    assert "nodes" in error, f"Expected node allocation info not found: {error}"


def test_sbatch_test_only_federation(federation_setup):
    """Test sbatch --test-only with federation - should show federation cluster."""
    error = atf.run_command_error(
        "sbatch -N1 --exclusive --output=/dev/null -t1 --wrap 'sleep 30' --test-only",
        fatal=True,
    )
    assert re.search(
        rf"({cls1}|{cls2}|{cls3})", error
    ), f"Expected federation cluster selection not found: {error}"


def test_sbatch_test_only_mall(federation_setup):
    """Test sbatch --test-only with -Mall flag."""
    error = atf.run_command_error(
        "sbatch -N1 --exclusive --output=/dev/null -t1 --wrap 'sleep 30' --test-only -Mall",
        fatal=True,
    )
    assert re.search(
        rf"({cls1}|{cls2}|{cls3})", error
    ), f"Expected federation cluster in -Mall output: {error}"


def test_srun_test_only_federation(federation_setup):
    """Test srun --test-only with federation."""
    error = atf.run_command_error(
        "srun -N1 --exclusive --test-only sleep 30", fatal=True
    )
    assert re.search(
        rf"({cls1}|{cls2}|{cls3})", error
    ), f"Expected federation cluster selection not found: {error}"


def test_srun_test_only_mall(federation_setup):
    """Test srun --test-only with -Mall flag."""
    error = atf.run_command_error(
        "srun -N1 --exclusive --test-only -Mall sleep 30", fatal=True
    )
    assert re.search(
        rf"({cls1}|{cls2}|{cls3})", error
    ), f"Expected federation cluster in -Mall output: {error}"


def fill_cluster(cluster, time_limit="5"):
    """Submit an exclusive fill job to a specific cluster, wait for RUNNING."""
    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null -t{time_limit}"
        f" -M{cluster} --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(
        job_id, "RUNNING", siblings=True, clusters=cluster, fatal=True
    )
    return job_id


def sbatch_test_only(extra_args=""):
    """Run sbatch --test-only and return the selected cluster name."""
    error = atf.run_command_error(
        f"sbatch -N{node_count} --exclusive --output=/dev/null -t1"
        f" --wrap 'sleep 30' --test-only {extra_args}",
        fatal=True,
    )
    for cluster in [cls1, cls2, cls3]:
        if cluster in error:
            return cluster
    return ""


def srun_test_only(extra_args=""):
    """Run srun --test-only and return the selected cluster name."""
    error = atf.run_command_error(
        f"srun -N{node_count} --exclusive --test-only {extra_args} sleep 30",
        fatal=True,
    )
    for cluster in [cls1, cls2, cls3]:
        if cluster in error:
            return cluster
    return ""


def test_sbatch_test_only_load_scenarios(federation_setup):
    """Test sbatch --test-only under federation load scenarios."""
    cancel_federation_jobs()

    # Submit job to fill cls1
    fill_cluster(cls1)

    # Test --test-only should route to cls2 or cls3 (cls1 is busy)
    selected = sbatch_test_only()
    assert selected in [cls2, cls3], f"Expected cls2/cls3 with cls1 busy: {selected}"
    assert selected != cls1, f"Should not select busy cls1: {selected}"

    cancel_federation_jobs()


def test_srun_test_only_load_scenarios(federation_setup):
    """Test srun --test-only under federation load scenarios."""
    cancel_federation_jobs()

    # Use srun for fill job in srun test context
    srun_fill = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null -t5"
        f" -M{cls1} --wrap 'srun sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(
        srun_fill, "RUNNING", siblings=True, clusters=cls1, fatal=True
    )

    # Test --test-only should route to cls2 or cls3
    selected = srun_test_only()
    assert selected in [cls2, cls3], f"Expected cls2/cls3 with cls1 busy: {selected}"
    assert selected != cls1, f"Should not select busy cls1: {selected}"

    cancel_federation_jobs()


def test_sbatch_progressive_load(federation_setup):
    """Test sbatch --test-only progressive load across federation clusters.

    Progressively fill clusters and verify --test-only routes to
    the least-loaded available cluster. The stages are:
    Stage 1: Fill cls1 -> test-only not cls1, -Mall not cls1, -M{cls1} -> cls1
    Stage 2: Fill cls2 -> test-only cls3, -Mall cls3, -M{cls1},{cls2} -> cls1
    Stage 3: Fill cls3 -> test-only cls1, -Mall cls1, -M{cls1},{cls2} -> cls1,
             -M{cls2},{cls3} -> cls2
    """
    cancel_federation_jobs()

    # --- Stage 1: Fill cls1 with 5-min job ---
    fill_cluster(cls1, "5")

    selected = sbatch_test_only()
    assert selected != cls1, f"test-only should not select busy cls1: {selected}"
    assert selected in [cls2, cls3], f"Expected cls2 or cls3: {selected}"

    selected = sbatch_test_only("-Mall")
    assert selected != cls1, f"-Mall should not select busy cls1: {selected}"

    selected = sbatch_test_only(f"-M{cls1}")
    assert selected == cls1, f"-M{cls1} should select cls1 even when busy: {selected}"

    # --- Stage 2: Fill cls2 with 10-min job ---
    fill_cluster(cls2, "10")

    selected = sbatch_test_only()
    assert selected == cls3, f"test-only should select cls3 (only idle): {selected}"

    selected = sbatch_test_only("-Mall")
    assert selected == cls3, f"-Mall should select cls3 (only idle): {selected}"

    selected = sbatch_test_only(f"-M{cls1},{cls2}")
    assert (
        selected == cls1
    ), f"-M{{cls1,cls2}} should select cls1 (soonest, 5-min vs 10-min): {selected}"

    # --- Stage 3: Fill cls3 with 15-min job ---
    fill_cluster(cls3, "15")

    selected = sbatch_test_only()
    assert (
        selected == cls1
    ), f"test-only should select cls1 (soonest, 5-min): {selected}"

    selected = sbatch_test_only("-Mall")
    assert selected == cls1, f"-Mall should select cls1 (soonest, 5-min): {selected}"

    selected = sbatch_test_only(f"-M{cls1},{cls2}")
    assert (
        selected == cls1
    ), f"-M{{cls1,cls2}} should select cls1 (5-min < 10-min): {selected}"

    selected = sbatch_test_only(f"-M{cls2},{cls3}")
    assert (
        selected == cls2
    ), f"-M{{cls2,cls3}} should select cls2 (10-min < 15-min): {selected}"

    cancel_federation_jobs()


def test_srun_progressive_load(federation_setup):
    """Test srun --test-only progressive load across federation clusters.

    Mirror of sbatch progressive load test for srun.
    Third fill job omits -M flag to test federation routing.
    """
    cancel_federation_jobs()

    # --- Stage 1: Fill cls1 with 5-min job ---
    fill_cluster(cls1, "5")

    selected = srun_test_only()
    assert selected != cls1, f"test-only should not select busy cls1: {selected}"
    assert selected in [cls2, cls3], f"Expected cls2 or cls3: {selected}"

    selected = srun_test_only("-Mall")
    assert selected != cls1, f"-Mall should not select busy cls1: {selected}"

    selected = srun_test_only(f"-M{cls1}")
    assert selected == cls1, f"-M{cls1} should select cls1 even when busy: {selected}"

    # --- Stage 2: Fill cls2 with 10-min job ---
    fill_cluster(cls2, "10")

    selected = srun_test_only()
    assert selected == cls3, f"test-only should select cls3 (only idle): {selected}"

    selected = srun_test_only("-Mall")
    assert selected == cls3, f"-Mall should select cls3 (only idle): {selected}"

    selected = srun_test_only(f"-M{cls1},{cls2}")
    assert (
        selected == cls1
    ), f"-M{{cls1,cls2}} should select cls1 (soonest, 5-min vs 10-min): {selected}"

    # --- Stage 3: Fill cls3 with 15-min job (omit -M to test federation routing) ---
    fill_job3 = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null -t15 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(
        fill_job3, "RUNNING", siblings=True, clusters=cls3, fatal=True
    )

    selected = srun_test_only()
    assert (
        selected == cls1
    ), f"test-only should select cls1 (soonest, 5-min): {selected}"

    selected = srun_test_only("-Mall")
    assert selected == cls1, f"-Mall should select cls1 (soonest, 5-min): {selected}"

    selected = srun_test_only(f"-M{cls1},{cls2}")
    assert (
        selected == cls1
    ), f"-M{{cls1,cls2}} should select cls1 (5-min < 10-min): {selected}"

    selected = srun_test_only(f"-M{cls2},{cls3}")
    assert (
        selected == cls2
    ), f"-M{{cls2,cls3}} should select cls2 (10-min < 15-min): {selected}"

    cancel_federation_jobs()


def test_sbatch_pending_backlog(federation_setup):
    """Test sbatch --test-only with pending backlog across federation clusters.

    After all 3 clusters are filled, progressively submit pending jobs
    (50-min backlog) to each cluster and verify --test-only routes correctly.

    Round 1: pending on cls1 -> cls2 preferred (10-min running < 5+50 backlog)
    Round 2: pending on cls2 -> cls3 preferred (15-min < 10+50)
    Round 3: pending on cls3 -> cls1 preferred (5+50 < 10+50 < 15+50)
    """
    cancel_federation_jobs()

    # Fill all 3 clusters with different time limits
    fill_cluster(cls1, "5")
    fill_cluster(cls2, "10")
    fill_cluster(cls3, "15")

    # All full, no backlog: should select cls1 (soonest, 5-min)
    selected = sbatch_test_only()
    assert selected == cls1, f"Expected cls1 (soonest) with all full: {selected}"

    # --- Round 1: Submit pending job to cls1 (50-min backlog) ---
    pending1 = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null -t50 -M{cls1} --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(
        pending1, "PENDING", siblings=True, clusters=cls1, fatal=True
    )

    selected = sbatch_test_only()
    assert selected == cls2, f"Round 1: test-only should select cls2: {selected}"

    selected = sbatch_test_only("-Mall")
    assert selected == cls2, f"Round 1: -Mall should select cls2: {selected}"

    selected = sbatch_test_only(f"-M{cls1},{cls2}")
    assert selected == cls2, f"Round 1: -M{{cls1,cls2}} should select cls2: {selected}"

    selected = sbatch_test_only(f"-M{cls2},{cls3}")
    assert selected == cls2, f"Round 1: -M{{cls2,cls3}} should select cls2: {selected}"

    # --- Round 2: Submit pending job to cls2 (50-min backlog) ---
    pending2 = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null -t50 -M{cls2} --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(
        pending2, "PENDING", siblings=True, clusters=cls2, fatal=True
    )

    selected = sbatch_test_only()
    assert selected == cls3, f"Round 2: test-only should select cls3: {selected}"

    selected = sbatch_test_only("-Mall")
    assert selected == cls3, f"Round 2: -Mall should select cls3: {selected}"

    selected = sbatch_test_only(f"-M{cls1}")
    assert selected == cls1, f"Round 2: -M{{cls1}} should select cls1: {selected}"

    selected = sbatch_test_only(f"-M{cls1},{cls2}")
    assert selected == cls1, f"Round 2: -M{{cls1,cls2}} should select cls1: {selected}"

    selected = sbatch_test_only(f"-M{cls2},{cls3}")
    assert selected == cls3, f"Round 2: -M{{cls2,cls3}} should select cls3: {selected}"

    # --- Round 3: Submit pending job to cls3 (50-min backlog) ---
    pending3 = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null -t50 -M{cls3} --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(
        pending3, "PENDING", siblings=True, clusters=cls3, fatal=True
    )

    selected = sbatch_test_only()
    assert selected == cls1, f"Round 3: test-only should select cls1: {selected}"

    selected = sbatch_test_only("-Mall")
    assert selected == cls1, f"Round 3: -Mall should select cls1: {selected}"

    selected = sbatch_test_only(f"-M{cls3}")
    assert selected == cls3, f"Round 3: -M{{cls3}} should select cls3: {selected}"

    cancel_federation_jobs()


def test_srun_pending_backlog(federation_setup):
    """Test srun --test-only with pending backlog across federation clusters.

    Mirror of sbatch pending backlog test for srun with all 3 rounds.
    """
    cancel_federation_jobs()

    # Fill all 3 clusters with different time limits
    fill_cluster(cls1, "5")
    fill_cluster(cls2, "10")
    fill_cluster(cls3, "15")

    # All full, no backlog: should select cls1 (soonest, 5-min)
    selected = srun_test_only()
    assert selected == cls1, f"Expected cls1 (soonest) with all full: {selected}"

    # --- Round 1: Submit pending job to cls1 (50-min backlog) ---
    pending1 = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null -t50 -M{cls1} --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(
        pending1, "PENDING", siblings=True, clusters=cls1, fatal=True
    )

    selected = srun_test_only()
    assert selected == cls2, f"Round 1: test-only should select cls2: {selected}"

    selected = srun_test_only("-Mall")
    assert selected == cls2, f"Round 1: -Mall should select cls2: {selected}"

    selected = srun_test_only(f"-M{cls1},{cls2}")
    assert selected == cls2, f"Round 1: -M{{cls1,cls2}} should select cls2: {selected}"

    selected = srun_test_only(f"-M{cls2},{cls3}")
    assert selected == cls2, f"Round 1: -M{{cls2,cls3}} should select cls2: {selected}"

    # --- Round 2: Submit pending job to cls2 (50-min backlog) ---
    pending2 = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null -t50 -M{cls2} --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(
        pending2, "PENDING", siblings=True, clusters=cls2, fatal=True
    )

    selected = srun_test_only()
    assert selected == cls3, f"Round 2: test-only should select cls3: {selected}"

    selected = srun_test_only("-Mall")
    assert selected == cls3, f"Round 2: -Mall should select cls3: {selected}"

    selected = srun_test_only(f"-M{cls1}")
    assert selected == cls1, f"Round 2: -M{{cls1}} should select cls1: {selected}"

    selected = srun_test_only(f"-M{cls1},{cls2}")
    assert selected == cls1, f"Round 2: -M{{cls1,cls2}} should select cls1: {selected}"

    selected = srun_test_only(f"-M{cls2},{cls3}")
    assert selected == cls3, f"Round 2: -M{{cls2,cls3}} should select cls3: {selected}"

    # --- Round 3: Submit pending job to cls3 (50-min backlog) ---
    pending3 = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null -t50 -M{cls3} --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(
        pending3, "PENDING", siblings=True, clusters=cls3, fatal=True
    )

    selected = srun_test_only()
    assert selected == cls1, f"Round 3: test-only should select cls1: {selected}"

    selected = srun_test_only("-Mall")
    assert selected == cls1, f"Round 3: -Mall should select cls1: {selected}"

    selected = srun_test_only(f"-M{cls3}")
    assert selected == cls3, f"Round 3: -M{{cls3}} should select cls3: {selected}"

    cancel_federation_jobs()


def test_federation_cluster_features(federation_with_features):
    """Test --test-only with cluster feature constraints."""
    # Test with feature constraint fa -> should select cls1
    error = atf.run_command_error(
        "sbatch -N1 --exclusive --output=/dev/null -t1"
        " --wrap 'sleep 30' --test-only --cluster-constraint=fa",
        fatal=True,
    )
    assert cls1 in error, f"Expected {cls1} to be selected for feature 'fa': {error}"


def test_allocation_failure(federation_setup):
    """Test allocation failure with impossible node configuration."""
    exit_code = atf.run_command_exit(
        "sbatch -N99 --exclusive --output=/dev/null -t1"
        " --wrap 'sleep 30' --test-only",
        xfail=True,
    )
    assert (
        exit_code != 0
    ), "Expected allocation failure for impossible node configuration"

    error = atf.run_command_error(
        "sbatch -N99 --exclusive --output=/dev/null -t1"
        " --wrap 'sleep 30' --test-only",
        xfail=True,
    )
    assert re.search(
        r"allocation failure: Requested node configuration is not available",
        error,
    ), f"Expected allocation failure message not found: {error}"


def test_srun_allocation_failure(federation_setup):
    """Test srun allocation failure with impossible node configuration."""
    exit_code = atf.run_command_exit(
        "srun -N99 --exclusive --test-only sleep 30", xfail=True
    )
    assert (
        exit_code != 0
    ), "Expected allocation failure for impossible node configuration"

    error = atf.run_command_error(
        "srun -N99 --exclusive --test-only sleep 30", xfail=True
    )
    assert re.search(
        r"allocation failure: Requested node configuration is not available",
        error,
    ), f"Expected allocation failure message not found: {error}"
