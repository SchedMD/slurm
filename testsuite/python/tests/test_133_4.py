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

# Global variables
user_name = None
srun_job_cnt = 0
file_in = None
node_count = 1


@pytest.fixture(scope="module", autouse=True)
def setup():
    global cls1, cls2, cls3
    cls1, cls2, cls3 = atf.require_multicluster(3, federation=True)
    for _ in atf.foreach_cluster():
        atf.require_nodes(2)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def federation_setup():
    """Module-level federation setup and cleanup."""
    global user_name, file_in, node_count

    user_name = atf.get_user_name()

    # Create input script once for the module
    file_in = f"{atf.module_tmp_path}/test133_4_input"
    atf.make_bash_script(file_in, "env; sleep infinity")

    # Compute node_count dynamically: divide available nodes by 2
    # so that with --exclusive, each cluster fits exactly 2 jobs
    output = atf.run_command_output(
        "sinfo -h --local -o '%D' -t idle,alloc,comp", fatal=True
    )
    node_count = int(output.strip()) // 2
    if node_count < 1:
        node_count = 1

    # Clean up any existing jobs
    cancel_federation_jobs()

    yield

    # Module-level cleanup
    cancel_federation_jobs()
    atf.run_command(f"rm -f {atf.module_tmp_path}/test133_4*.out")


@pytest.fixture(scope="module")
def federation_with_features(federation_setup):
    """Module-level fixture that sets up cluster features - only runs once per module."""
    for cluster, feature in [(cls1, "fa"), (cls2, "fb"), (cls3, "fc")]:
        output = atf.run_command_output(
            f"sacctmgr -i modify cluster {cluster} set features={feature}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        assert re.search(
            r"Setting", output
        ), f"sacctmgr modify {cluster}: missing 'Setting' in output"
        assert re.search(
            rf"Feature\s+=\s+{feature}", output
        ), f"sacctmgr modify {cluster}: missing 'Feature = {feature}' in output"
        assert re.search(
            r"Modified cluster", output
        ), f"sacctmgr modify {cluster}: missing 'Modified cluster' in output"
        assert re.search(
            rf"\s+{cluster}", output
        ), f"sacctmgr modify {cluster}: missing '{cluster}' in output"
    atf.wait_for_federation_sync()

    return True


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


@pytest.fixture(scope="function")
def cleanup():
    """Function-level cleanup for individual tests - only cancels jobs between tests."""
    yield
    cancel_federation_jobs()


def submit_fed_batch_job(expected_origin, expected_sib, extra_args, expected_state):
    """Submit federated batch job and validate placement."""
    args = f"-N{node_count} --exclusive --output=/dev/null --error=/dev/null"
    if extra_args:
        args += f" {extra_args}"

    output = atf.run_command_output(f"sbatch {args} {file_in}", fatal=True)

    match = re.search(r"Submitted \S+ job (\d+)", output)
    assert match, f"Failed to get job id from sbatch output: {output}"
    job_id = int(match.group(1))
    atf.properties["submitted-jobs"].append(job_id)

    # Capture the submit cluster from "on cluster X" output
    submit_cluster = ""
    cluster_match = re.search(r"on cluster (\S+)", output)
    if cluster_match:
        submit_cluster = cluster_match.group(1)

    run_cluster = ""
    if expected_state:
        run_cluster = wait_for_fed_job(job_id, expected_state)

    # Use submit_cluster for query if available, else fall back to expected_origin
    query_cluster = (
        submit_cluster
        if submit_cluster
        else expected_origin if expected_origin else run_cluster
    )
    return validate_fed_job(job_id, expected_origin, expected_sib, query_cluster)


def submit_fed_srun_job(
    expected_origin, expected_sib, extra_args, check_env, expected_state
):
    """Submit federated srun job and validate placement."""
    global srun_job_cnt

    file_out = atf.module_tmp_path / f"test133_4_{srun_job_cnt}.out"
    srun_job_cnt += 1

    args = f"-N{node_count} --exclusive -o {file_out}"
    if extra_args:
        args += f" {extra_args}"

    job_id = atf.submit_job_srun(f"{args} {file_in}", background=True, fatal=True)

    run_cluster = ""
    if expected_state:
        run_cluster = wait_for_fed_job(job_id, expected_state)

    submit_cluster = ""
    if check_env:
        # Wait for file to have SLURM_CLUSTER_NAME (not just exist)
        atf.repeat_until(
            lambda: atf.run_command_output(f"grep SLURM_CLUSTER_NAME {file_out}"),
            lambda result: result and "SLURM_CLUSTER_NAME=" in result,
            timeout=30,
            fatal=True,
        )
        output = atf.run_command_output(
            f"grep SLURM_CLUSTER_NAME {file_out}", fatal=True
        )
        cluster_match = re.search(r"SLURM_CLUSTER_NAME=(\S+)", output)
        if cluster_match:
            submit_cluster = cluster_match.group(1)
            assert (
                submit_cluster == expected_sib
            ), f"SLURM_CLUSTER_NAME={submit_cluster} != {expected_sib}"
    else:
        submit_cluster = expected_origin

    query_cluster = submit_cluster if submit_cluster else run_cluster
    return validate_fed_job(job_id, expected_origin, expected_sib, query_cluster)


def submit_fed_salloc_job(
    expected_origin, expected_sib, extra_args, check_env, expected_state
):
    """Submit federated salloc job and validate placement."""
    global srun_job_cnt

    file_out = atf.module_tmp_path / f"test133_4_{srun_job_cnt}.out"
    srun_job_cnt += 1

    args = f"-N{node_count} --exclusive"
    if extra_args:
        args += f" {extra_args}"

    salloc_args = f"{args} srun -o {file_out} {file_in}"

    job_id = atf.submit_job_salloc(salloc_args, background=True, fatal=True)

    run_cluster = ""
    if expected_state:
        run_cluster = wait_for_fed_job(job_id, expected_state)

    submit_cluster = ""
    if check_env:
        # Wait for file to have SLURM_CLUSTER_NAME (not just exist)
        atf.repeat_until(
            lambda: atf.run_command_output(f"grep SLURM_CLUSTER_NAME {file_out}"),
            lambda result: result and "SLURM_CLUSTER_NAME=" in result,
            timeout=30,
            fatal=True,
        )
        output = atf.run_command_output(
            f"grep SLURM_CLUSTER_NAME {file_out}", fatal=True
        )
        cluster_match = re.search(r"SLURM_CLUSTER_NAME=(\S+)", output)
        if cluster_match:
            submit_cluster = cluster_match.group(1)
            assert (
                submit_cluster == expected_sib
            ), f"SLURM_CLUSTER_NAME={submit_cluster} != {expected_sib}"
    else:
        submit_cluster = expected_origin

    query_cluster = submit_cluster if submit_cluster else run_cluster
    return validate_fed_job(job_id, expected_origin, expected_sib, query_cluster)


def wait_for_fed_job(job_id, expected_state):
    """Wait for job to reach expected state and return its first cluster."""
    atf.wait_for_job_state(job_id, expected_state, siblings=True, fatal=True)
    return sorted(atf.get_job_clusters(job_id, expected_state))[0]


def validate_fed_job(job_id, expected_origin, expected_sib, query_cluster):
    """Validate federation job properties.

    query_cluster: the cluster to query squeue from (where the job was found
    by wait_for_fed_job). Uses --local to only show jobs local to that cluster.
    """

    def get_origin_and_siblings(cluster):
        """Wait for the job on the cluster and return (origin, siblings)."""
        for _ in atf.timer(fatal=True):
            output = atf.run_command_output(
                f"squeue -M{cluster} -a --local --jobs={job_id} --noheader -Oorigin,siblingsactive",
            )
            match = re.search(r"(\S+)\s+(\S+)", output)
            if match:
                return match.group(1), match.group(2)

    origin, sibling = get_origin_and_siblings(query_cluster)

    if expected_origin:
        assert (
            origin == expected_origin
        ), f"origin:{origin} != expected_origin:{expected_origin}"

    if expected_sib:
        assert (
            sibling == expected_sib
        ), f"sibling:{sibling} != expected_sib:{expected_sib}"

    if sibling and sibling != "NA":
        for tmp_sib in sibling.split(","):
            if tmp_sib == origin:
                continue

            sib_origin, sib_sibling = get_origin_and_siblings(tmp_sib)

            assert origin == sib_origin, f"Origin not the same on {tmp_sib}"
            if sib_sibling != "NA":
                assert sibling == sib_sibling, f"Sibling not the same on {tmp_sib}"

    return sibling


def test_packing_across_clusters_sbatch(federation_setup, cleanup):
    """Test packing across clusters with sbatch."""
    job_counts = {}

    # Submit 6 jobs that should run
    for i in range(6):
        ret_sib = submit_fed_batch_job(cls1, "", "", "RUNNING")
        job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Submit 1 job that should be pending
    ret_sib = submit_fed_batch_job(cls1, "", "", "PENDING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Verify distribution (each cluster should have 2 jobs, pending goes to all)
    assert (
        job_counts.get(cls1, 0) == 2
    ), f"Cluster 1 ({cls1}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(cls2, 0) == 2
    ), f"Cluster 2 ({cls2}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(cls3, 0) == 2
    ), f"Cluster 3 ({cls3}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(f"{cls1},{cls2},{cls3}", 0) == 1
    ), f"Clusters ({cls1},{cls2},{cls3}) don't have a pending job on them"


def test_packing_across_clusters_srun(federation_setup, cleanup):
    """Test packing across clusters with srun."""
    job_counts = {}

    # Submit 6 jobs that should run
    for i in range(6):
        ret_sib = submit_fed_srun_job(cls1, "", "", False, "RUNNING")
        job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Submit 1 job that should be pending
    ret_sib = submit_fed_srun_job(cls1, "", "", False, "PENDING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Verify distribution
    assert (
        job_counts.get(cls1, 0) == 2
    ), f"Cluster 1 ({cls1}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(cls2, 0) == 2
    ), f"Cluster 2 ({cls2}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(cls3, 0) == 2
    ), f"Cluster 3 ({cls3}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(f"{cls1},{cls2},{cls3}", 0) == 1
    ), f"Clusters ({cls1},{cls2},{cls3}) don't have a pending job on them"


def test_packing_across_clusters_salloc(federation_setup, cleanup):
    """Test packing across clusters with salloc."""
    job_counts = {}

    # Submit 6 jobs that should run
    for i in range(6):
        ret_sib = submit_fed_salloc_job(cls1, "", "", False, "RUNNING")
        job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Submit 1 job that should be pending
    ret_sib = submit_fed_salloc_job(cls1, "", "", False, "PENDING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Verify distribution
    assert (
        job_counts.get(cls1, 0) == 2
    ), f"Cluster 1 ({cls1}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(cls2, 0) == 2
    ), f"Cluster 2 ({cls2}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(cls3, 0) == 2
    ), f"Cluster 3 ({cls3}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(f"{cls1},{cls2},{cls3}", 0) == 1
    ), f"Clusters ({cls1},{cls2},{cls3}) don't have a pending job on them"


def test_cluster_selection_sbatch(federation_setup, cleanup):
    """Test -M<clusters> with federated jobs with sbatch."""
    job_counts = {}

    # Submit job to only cls1
    ret_sib = submit_fed_batch_job(cls1, cls1, f"-M{cls1}", "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_batch_job(cls1, cls1, f"-M{cls1}", "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_batch_job(cls1, cls1, f"-M{cls1}", "PENDING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Submit job to only cls1,cls2
    # Should choose origin cls2 because the jobs can start sooner there
    # The first two start, but the third will pend on active siblings cls1,cls2
    ret_sib = submit_fed_batch_job(cls2, cls2, f"-M{cls1},{cls2}", "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_batch_job(cls2, cls2, f"-M{cls1},{cls2}", "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_batch_job(
        cls2, f"{cls1},{cls2}", f"-M{cls1},{cls2}", "PENDING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Submit job to cls2,cls3
    # Should choose origin cls3 because the jobs can start sooner there
    # The first two start, but the third will pend on active siblings cls2,cls3
    ret_sib = submit_fed_batch_job(cls3, cls3, f"-M{cls2},{cls3}", "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_batch_job(cls3, cls3, f"-M{cls2},{cls3}", "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_batch_job(
        cls3, f"{cls2},{cls3}", f"-M{cls2},{cls3}", "PENDING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Verify distribution
    assert (
        job_counts.get(cls1, 0) == 3
    ), f"Cluster 1 ({cls1}) doesn't have 3 jobs running on it"
    assert (
        job_counts.get(cls2, 0) == 2
    ), f"Cluster 2 ({cls2}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(f"{cls1},{cls2}", 0) == 1
    ), f"Clusters 1 and 2 ({cls1},{cls2}) don't have 1 job running on them"
    assert (
        job_counts.get(cls3, 0) == 2
    ), f"Cluster 3 ({cls3}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(f"{cls2},{cls3}", 0) == 1
    ), f"Clusters 2 and 3 ({cls2},{cls3}) don't have 1 jobs running on them"


def test_cluster_selection_srun(federation_setup, cleanup):
    """Test -M<clusters> with federated jobs with srun."""
    job_counts = {}

    # Submit job to only cls1
    ret_sib = submit_fed_srun_job(cls1, cls1, f"-M{cls1}", True, "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_srun_job(cls1, cls1, f"-M{cls1}", True, "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_srun_job(cls1, cls1, f"-M{cls1}", False, "PENDING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Submit job to only cls1,cls2
    # Should choose origin cls2 because the jobs can start sooner there
    ret_sib = submit_fed_srun_job(cls2, cls2, f"-M{cls1},{cls2}", True, "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_srun_job(cls2, cls2, f"-M{cls1},{cls2}", True, "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_srun_job(
        cls2, f"{cls1},{cls2}", f"-M{cls1},{cls2}", False, "PENDING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Submit job to cls2,cls3
    # Should choose origin cls3 because the jobs can start sooner there
    ret_sib = submit_fed_srun_job(cls3, cls3, f"-M{cls2},{cls3}", True, "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_srun_job(cls3, cls3, f"-M{cls2},{cls3}", True, "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_srun_job(
        cls3, f"{cls2},{cls3}", f"-M{cls2},{cls3}", False, "PENDING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Verify distribution
    assert (
        job_counts.get(cls1, 0) == 3
    ), f"Cluster 1 ({cls1}) doesn't have 3 jobs running on it"
    assert (
        job_counts.get(cls2, 0) == 2
    ), f"Cluster 2 ({cls2}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(f"{cls1},{cls2}", 0) == 1
    ), f"Clusters 1 and 2 ({cls1},{cls2}) don't have 1 job running on them"
    assert (
        job_counts.get(cls3, 0) == 2
    ), f"Cluster 3 ({cls3}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(f"{cls2},{cls3}", 0) == 1
    ), f"Clusters 2 and 3 ({cls2},{cls3}) don't have 1 job running on them"


def test_cluster_selection_salloc(federation_setup, cleanup):
    """Test -M<clusters> with federated jobs with salloc."""
    job_counts = {}

    # Submit job to only cls1
    ret_sib = submit_fed_salloc_job(cls1, cls1, f"-M{cls1}", True, "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_salloc_job(cls1, cls1, f"-M{cls1}", True, "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_salloc_job(cls1, cls1, f"-M{cls1}", False, "PENDING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Submit job to only cls1,cls2
    # Should choose origin cls2 because the jobs can start sooner there
    ret_sib = submit_fed_salloc_job(cls2, cls2, f"-M{cls1},{cls2}", True, "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_salloc_job(cls2, cls2, f"-M{cls1},{cls2}", True, "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_salloc_job(
        cls2, f"{cls1},{cls2}", f"-M{cls1},{cls2}", False, "PENDING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Submit job to cls2,cls3
    # Should choose origin cls3 because the jobs can start sooner there
    ret_sib = submit_fed_salloc_job(cls3, cls3, f"-M{cls2},{cls3}", True, "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_salloc_job(cls3, cls3, f"-M{cls2},{cls3}", True, "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_salloc_job(
        cls3, f"{cls2},{cls3}", f"-M{cls2},{cls3}", False, "PENDING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    # Verify distribution
    assert (
        job_counts.get(cls1, 0) == 3
    ), f"Cluster 1 ({cls1}) doesn't have 3 jobs running on it"
    assert (
        job_counts.get(cls2, 0) == 2
    ), f"Cluster 2 ({cls2}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(f"{cls1},{cls2}", 0) == 1
    ), f"Clusters 1 and 2 ({cls1},{cls2}) don't have 1 job running on them"
    assert (
        job_counts.get(cls3, 0) == 2
    ), f"Cluster 3 ({cls3}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(f"{cls2},{cls3}", 0) == 1
    ), f"Clusters 2 and 3 ({cls2},{cls3}) don't have 1 job running on them"


def test_cluster_features_sbatch(federation_with_features, cleanup):
    """Test sbatch jobs with cluster features."""
    job_counts = {}

    ret_sib = submit_fed_batch_job(cls1, cls3, "--cluster-constraint=fc", "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_batch_job(cls1, cls3, "--cluster-constraint=fc", "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_batch_job(cls1, cls2, "--cluster-constraint=fb", "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_batch_job(cls1, cls2, "--cluster-constraint=fb", "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_batch_job(cls1, cls1, "--cluster-constraint=fa", "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_batch_job(cls1, cls1, "--cluster-constraint=fa", "RUNNING")
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_batch_job(
        cls1,
        f"{cls1},{cls2},{cls3}",
        "--cluster-constraint=fa,fb,fc",
        "PENDING",
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    assert (
        job_counts.get(cls1, 0) == 2
    ), f"Cluster 1 ({cls1}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(cls2, 0) == 2
    ), f"Cluster 2 ({cls2}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(cls3, 0) == 2
    ), f"Cluster 3 ({cls3}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(f"{cls1},{cls2},{cls3}", 0) == 1
    ), f"Clusters ({cls1},{cls2},{cls3}) don't have 1 job running on them"


def test_cluster_features_salloc(federation_with_features, cleanup):
    """Test salloc jobs with cluster features."""
    job_counts = {}

    ret_sib = submit_fed_salloc_job(
        cls1, cls3, "--cluster-constraint=fc", False, "RUNNING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_salloc_job(
        cls1, cls3, "--cluster-constraint=fc", False, "RUNNING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_salloc_job(
        cls1, cls2, "--cluster-constraint=fb", False, "RUNNING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_salloc_job(
        cls1, cls2, "--cluster-constraint=fb", False, "RUNNING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_salloc_job(
        cls1, cls1, "--cluster-constraint=fa", False, "RUNNING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_salloc_job(
        cls1, cls1, "--cluster-constraint=fa", False, "RUNNING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_salloc_job(
        cls1,
        f"{cls1},{cls2},{cls3}",
        "--cluster-constraint=fa,fb,fc",
        False,
        "PENDING",
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    assert (
        job_counts.get(cls1, 0) == 2
    ), f"Cluster 1 ({cls1}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(cls2, 0) == 2
    ), f"Cluster 2 ({cls2}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(cls3, 0) == 2
    ), f"Cluster 3 ({cls3}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(f"{cls1},{cls2},{cls3}", 0) == 1
    ), f"Clusters ({cls1},{cls2},{cls3}) don't have 1 job running on them"


def test_cluster_features_srun(federation_with_features, cleanup):
    """Test srun jobs with cluster features."""
    job_counts = {}

    ret_sib = submit_fed_srun_job(
        cls1, cls3, "--cluster-constraint=fc", False, "RUNNING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_srun_job(
        cls1, cls3, "--cluster-constraint=fc", False, "RUNNING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_srun_job(
        cls1, cls2, "--cluster-constraint=fb", False, "RUNNING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_srun_job(
        cls1, cls2, "--cluster-constraint=fb", False, "RUNNING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_srun_job(
        cls1, cls1, "--cluster-constraint=fa", False, "RUNNING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_srun_job(
        cls1, cls1, "--cluster-constraint=fa", False, "RUNNING"
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1
    ret_sib = submit_fed_srun_job(
        cls1,
        f"{cls1},{cls2},{cls3}",
        "--cluster-constraint=fa,fb,fc",
        False,
        "PENDING",
    )
    job_counts[ret_sib] = job_counts.get(ret_sib, 0) + 1

    assert (
        job_counts.get(cls1, 0) == 2
    ), f"Cluster 1 ({cls1}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(cls2, 0) == 2
    ), f"Cluster 2 ({cls2}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(cls3, 0) == 2
    ), f"Cluster 3 ({cls3}) doesn't have 2 jobs running on it"
    assert (
        job_counts.get(f"{cls1},{cls2},{cls3}", 0) == 1
    ), f"Clusters ({cls1},{cls2},{cls3}) don't have 1 job running on them"


def test_invalid_cluster_features(federation_setup, cleanup):
    """Test jobs with invalid cluster features."""
    result = atf.run_command(
        f"sbatch --output=/dev/null --error=/dev/null -n1 --cluster-constraint=invalid {file_in}",
        fatal=True,
        xfail=True,
    )

    error_text = result.get("stderr", "") + result.get("stdout", "")
    assert (
        "Invalid cluster feature specification" in error_text
    ), f"Expected error message not found in: {error_text}"
