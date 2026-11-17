##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
"""Test the --federation option of the client commands.

It is tested on a federation without FederationParameters=fed_display (which
test_133_9 and test_133_11 set for the federated views). The documentation says
that fed_display is equivalent to using --federation on each command, and that
--local overrides it (see slurm.conf.5, squeue.1, sinfo.1 and sacct.1).
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

# The names of the clusters, set by setup()
cls1 = cls2 = cls3 = None
job_name = "test_133_18_job"


@pytest.fixture(scope="module", autouse=True)
def setup():
    global cls1, cls2, cls3
    cls1, cls2, cls3 = atf.require_multicluster(3, federation=True)
    for _ in atf.foreach_cluster():
        atf.require_config_parameter_excludes("FederationParameters", "fed_display")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def fed_jobs():
    """Run a job in each cluster and return their ids by cluster."""
    jobs = {}
    for cluster in [cls1, cls2, cls3]:
        jobs[cluster] = atf.submit_job_sbatch(
            f"-M{cluster} -N1 --job-name={job_name} --output=/dev/null"
            f" --error=/dev/null --wrap 'sleep infinity'",
            fatal=True,
        )
    for cluster, job_id in jobs.items():
        atf.wait_for_job_state(
            job_id, "RUNNING", siblings=True, clusters=cluster, fatal=True
        )

    yield jobs

    atf.run_command(f"scancel -M{cls1},{cls2},{cls3} --jobname {job_name}")


def test_squeue_federation(fed_jobs):
    """Test squeue --federation, and that --local overrides it.

    Without fed_display the default view is the local cluster, and
    --federation adds the jobs of the other clusters of the federation.
    """

    def squeue_job_ids(options):
        output = atf.run_command_output(
            f"squeue --noheader -O jobid {options}", fatal=True
        )
        return {int(line) for line in output.split()}

    local_jobs = {fed_jobs[cls1]}
    assert squeue_job_ids("") == local_jobs, "The default view should be local"
    assert squeue_job_ids("--federation") == set(
        fed_jobs.values()
    ), "--federation should show the jobs of all the clusters"
    assert (
        squeue_job_ids("--local --federation") == local_jobs
    ), "--local should override --federation"


def test_sinfo_federation(fed_jobs):
    """Test sinfo --federation, and that --local overrides it."""

    def sinfo_clusters(options):
        output = atf.run_command_output(f"sinfo --noheader {options}", fatal=True)
        return set(re.findall(r"cls\d", output))

    # The federated view has a column with the cluster
    assert sinfo_clusters("") == set(), "The default view should be local"
    assert sinfo_clusters("--federation") == {
        cls1,
        cls2,
        cls3,
    }, "--federation should show all the clusters"
    assert (
        sinfo_clusters("--local --federation") == set()
    ), "--local should override --federation"


def test_sacct_federation(fed_jobs):
    """Test sacct --federation, and that --local overrides it.

    Without fed_display the default view is the local cluster, and
    --federation adds the jobs of the other clusters of the federation.
    """
    for cluster, job_id in fed_jobs.items():
        atf.wait_for_job_accounted(job_id, cluster=cluster, fatal=True)

    def sacct_job_clusters(options):
        output = atf.run_command_output(
            f"sacct --noheader --parsable2 -X -o jobid,cluster {options}"
            f" -j {','.join(str(job_id) for job_id in fed_jobs.values())}",
            fatal=True,
        )
        rows = [line.split("|") for line in output.split()]
        return {int(job_id): cluster for job_id, cluster in rows}

    all_clusters = {job_id: cluster for cluster, job_id in fed_jobs.items()}
    local_jobs = {fed_jobs[cls1]: cls1}
    assert sacct_job_clusters("") == local_jobs, "The default view should be local"
    assert (
        sacct_job_clusters("--federation") == all_clusters
    ), "--federation should show all the clusters"
    assert (
        sacct_job_clusters("--local --federation") == local_jobs
    ), "--local should override --federation"
