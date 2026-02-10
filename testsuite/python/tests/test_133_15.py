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


@pytest.fixture(scope="module", autouse=True)
def setup():
    global cls1, cls2, cls3
    cls1, cls2, cls3 = atf.require_multicluster(3, federation=True)
    for _ in atf.foreach_cluster():
        atf.require_nodes(10, [("RealMemory", 100)])
    atf.require_slurm_running()

    global user_name

    user_name = atf.get_user_name()

    # Module-level federation setup
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
    """Cancel all user jobs across federation clusters."""
    if user_name:
        atf.run_command(f"scancel -M{cls1},{cls2},{cls3} --user {user_name}")
        atf.repeat_until(
            lambda: atf.run_command_output(
                f"squeue -M{cls1},{cls2},{cls3} --user {user_name} --noheader",
            ),
            lambda output: output.strip() == "",
            timeout=10,
        )


def wait_for_fed_job(job_id, desired_state, clusters=None):
    """Wait for federated job to reach desired state on any cluster."""
    if clusters is None:
        cluster_list = [cls1, cls2, cls3]
    elif isinstance(clusters, str):
        cluster_list = clusters.split(",")
    else:
        cluster_list = clusters

    found_cluster = [None]

    def check_job_state():
        for cluster in cluster_list:
            result = atf.run_command(
                f"scontrol -M{cluster} --local -a -o show job {job_id}",
            )
            if result["exit_code"] == 0:
                match = re.search(r"JobState\s*=\s*(\w+)", result["stdout"])
                if match and match.group(1) == desired_state:
                    found_cluster[0] = cluster
                    return True
        return False

    assert atf.repeat_until(
        check_job_state, lambda x: x, timeout=360
    ), f"Job {job_id} never reached state {desired_state}"

    return found_cluster[0]


def fed_sbatch(options=""):
    """Submit a job through the federation. Returns job ID."""
    return atf.submit_job_sbatch(
        f"-N10 --exclusive -o/dev/null {options} --wrap 'sleep infinity'",
        fatal=True,
    )


def fed_squeue(cluster, pattern):
    """Run squeue on a cluster and verify the pattern matches."""

    def check_pattern():
        result = atf.run_command(
            f"squeue --noheader -a -M{cluster}",
        )
        if result["exit_code"] == 0:
            return re.search(pattern, result["stdout"])
        return False

    assert atf.repeat_until(
        check_pattern, lambda x: x, timeout=30
    ), f"Pattern '{pattern}' not found in squeue output on {cluster}"


def fed_scontrol(command, job_id, option="", expected_output=""):
    """Execute scontrol command and optionally verify output."""
    result = atf.run_command(
        f"scontrol {command} {job_id} {option}",
        user=atf.properties["slurm-user"],
    )
    if expected_output:
        combined = result["stdout"] + result["stderr"]
        assert re.search(
            expected_output, combined, re.IGNORECASE
        ), f"Expected '{expected_output}' in scontrol output, got: {combined}"


def test_federated_scontrol_suspend():
    """Test federated scontrol suspend/resume with cross-cluster routing.

    Verifies that suspend/resume commands are properly routed between clusters:
    - Suspend from origin routes to running cluster
    - Suspend from running cluster works directly
    - Suspend from uninvolved cluster routes via origin to running cluster
    - Suspend origin job from remote cluster routes correctly
    """
    cancel_federation_jobs()

    # Submit 3 jobs: jid0 explicitly to cls1, jid1 and jid2 auto-routed
    jid0 = fed_sbatch(f"-M{cls1}")
    jid1 = fed_sbatch()
    jid2 = fed_sbatch()

    # Wait for jobs to start running
    wait_for_fed_job(jid0, "RUNNING", cls1)
    job2_cluster = wait_for_fed_job(jid1, "RUNNING", f"{cls2},{cls3}")
    job3_cluster = wait_for_fed_job(jid2, "RUNNING", f"{cls2},{cls3}")

    atf.wait_for_job_state(jid0, "RUNNING", clusters=cls1, timeout=60)
    atf.wait_for_job_state(jid1, "RUNNING", clusters=job2_cluster, timeout=60)
    atf.wait_for_job_state(jid2, "RUNNING", clusters=job3_cluster, timeout=60)

    # Suspend from origin (no -M) should route to job2_cluster
    fed_scontrol("suspend", jid1)
    fed_squeue(job2_cluster, rf"{jid1}.+S")
    fed_scontrol("resume", jid1)
    fed_squeue(job2_cluster, rf"{jid1}.+R")

    # Suspend from cluster running the job should work directly
    fed_scontrol("suspend", jid1, f"-M{job2_cluster}")
    fed_squeue(job2_cluster, rf"{jid1}.+S")
    fed_scontrol("resume", jid1, f"-M{job2_cluster}")
    fed_squeue(job2_cluster, rf"{jid1}.+R")

    # Suspend from cluster that doesn't know about job should route
    # via origin to running cluster
    fed_scontrol("suspend", jid1, f"-M{job3_cluster}")
    fed_squeue(job2_cluster, rf"{jid1}.+S")
    fed_scontrol("resume", jid1, f"-M{job3_cluster}")
    fed_squeue(job2_cluster, rf"{jid1}.+R")

    # Suspend origin job (jid0 on cls1) from remote cluster (cls3)
    fed_scontrol("suspend", jid0, f"-M{cls3}")
    fed_squeue(cls1, rf"{jid0}.+S")
    fed_scontrol("resume", jid0, f"-M{cls3}")
    fed_squeue(cls1, rf"{jid0}.+R")


def test_federated_job_array_suspend():
    """Test federated job array suspend/resume with cross-cluster routing."""
    cancel_federation_jobs()

    # Use fixed clusters, as discovering where the jobs ran by probing was
    # flaky under load.
    job2_cluster = cls2
    job3_cluster = cls3

    # Submit array job to job2_cluster.
    jid1 = atf.submit_job_sbatch(
        f"-N10 --exclusive --mem=10 -a1-15 -o/dev/null"
        f" -M{job2_cluster} --wrap 'sleep infinity'",
        fatal=True,
    )

    # Wait for first array task to start
    wait_for_fed_job(f"{jid1}_1", "RUNNING", job2_cluster)

    # Suspend pending array tasks from each cluster - expect "pending" in output
    fed_scontrol("suspend", f"{jid1}_[2-15]", f"-M{cls1}", "pending")
    fed_scontrol("suspend", f"{jid1}_[2-15]", f"-M{cls2}", "pending")
    fed_scontrol("suspend", f"{jid1}_[2-15]", f"-M{cls3}", "pending")

    # Suspend running task 1 from origin (cls1) - routes to job2_cluster
    fed_scontrol("suspend", f"{jid1}_1", f"-M{cls1}")
    fed_squeue(job2_cluster, rf"{jid1}_1.+S")

    # Wait for task 2 to start running
    wait_for_fed_job(f"{jid1}_2", "RUNNING", job2_cluster)

    # Suspend running task 2 from running cluster directly
    fed_scontrol("suspend", f"{jid1}_2", f"-M{job2_cluster}")
    # Verify task 1 is still suspended
    fed_squeue(job2_cluster, rf"{jid1}_1.+S")

    # Wait for task 3 to start running
    wait_for_fed_job(f"{jid1}_3", "RUNNING", job2_cluster)

    # Suspend running task 3 from uninvolved cluster - routes via origin
    fed_scontrol("suspend", f"{jid1}_3", f"-M{job3_cluster}")
    fed_squeue(job2_cluster, rf"{jid1}_3.+S")

    # Resume all from different clusters
    fed_scontrol("resume", f"{jid1}_1", f"-M{cls1}")
    fed_squeue(job2_cluster, rf"{jid1}_1.+R")
    fed_scontrol("resume", f"{jid1}_2", f"-M{cls2}")
    fed_squeue(job2_cluster, rf"{jid1}_2.+R")
    fed_scontrol("resume", f"{jid1}_3", f"-M{cls3}")
    fed_squeue(job2_cluster, rf"{jid1}_3.+R")
