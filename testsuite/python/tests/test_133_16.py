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
        atf.require_nodes(10)
    atf.require_slurm_running()

    global user_name

    user_name = atf.get_user_name()

    cancel_federation_jobs()
    setup_cluster_features()

    yield

    cancel_federation_jobs()


def setup_cluster_features():
    """Set cluster features for federation testing."""
    for cluster, feature in [(cls1, "fa"), (cls2, "fb"), (cls3, "fc")]:
        atf.run_command(
            f"sacctmgr -i modify cluster {cluster} set features={feature}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    atf.wait_for_federation_sync()


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


def submit_batch_job(options=""):
    """Submit a batch job with federation options and return job ID."""
    return atf.submit_job_sbatch(
        f"-N10 --exclusive -o/dev/null {options} --wrap 'sleep infinity'",
        fatal=True,
    )


def cancel_job(job_id, cluster):
    """Cancel a specific job on a cluster."""
    atf.run_command(f"scancel -M{cluster} {job_id}")
    atf.repeat_until(
        lambda: atf.run_command_output(
            f"squeue -M{cluster} --local --jobs={job_id} --noheader -o '%T'",
        ),
        lambda output: not output.strip() or "CANCEL" in output.upper(),
        timeout=30,
    )


def remove_cluster_from_federation(cluster):
    """Remove a cluster from the federation and wait until all the controllers
    see the remaining members."""
    atf.run_command(
        f"sacctmgr -i modify federation {fed_name} set clusters-={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_federation_sync()


def restore_federation():
    """Put all the clusters back in the federation and wait until they are
    connected."""
    # sacctmgr fails if all the clusters are already in the federation
    output = atf.run_command_output(
        f"sacctmgr show federation {fed_name} format=clusters -n",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    if set(re.findall(r"cls\d+", output)) != {cls1, cls2, cls3}:
        atf.run_command(
            f"sacctmgr -i modify federation {fed_name} set clusters={cls1},{cls2},{cls3}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    atf.wait_for_federation_sync()
    atf.wait_for_federation_connected()


def wait_for_local_jobs(cluster, job_ids):
    """Wait until the jobs not done on the cluster are exactly job_ids.

    This includes the REVOKED jobs that the origin keeps to track the jobs
    running in its siblings. Their state is PENDING+REVOKED, and it becomes
    CANCELLED+REVOKED when the sibling is removed, but scontrol prints both
    as REVOKED. The json output reports both, the base state and the flags.
    """
    for _ in atf.timer(fatal=True):
        jobs = atf.get_jobs(cluster=cluster, use_json=True, quiet=True)
        active = {
            job_id
            for job_id, job in jobs.items()
            if not any(atf.is_job_state_done(state) for state in job["job_state"])
        }
        if active == set(job_ids):
            break


def wait_for_any_running(job_ids, cluster):
    """Wait until one of the jobs is running in the cluster and return its id.

    Jobs with the same priority can start in any order. E.g. the nodes of a
    cancelled job can get free while the scheduler is evaluating the pending
    jobs, so the first one does not fit yet but the next one already does.
    """
    for _ in atf.timer(fatal=True):
        for job_id in job_ids:
            if atf.get_job_clusters(job_id, "RUNNING", clusters=cluster):
                return job_id


def submit_exclusive_fed_srun(procs, cluster_number, script, cluster_constraint=None):
    """Run an exclusive srun in the background, and return its job id.

    It is not atf.submit_job_srun() because the stderr of srun goes to a file
    instead of a pipe, so it can be read at any time without losing lines.
    The process is added to procs.

    Returns:
        The job id and the file with the stderr of srun.
    """
    err_file = f"srun_{cluster_number}_{len(procs)}.err"
    constraint = (
        f"--cluster-constraint={cluster_constraint}" if cluster_constraint else ""
    )
    atf.select_cluster(cluster_number)
    process = atf.run_command(
        f"srun -N10 --exclusive {constraint} {script} 2> {err_file}",
        background=True,
    )["process"]
    atf.select_cluster(1)
    procs.append(process)

    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(f"cat {err_file}")
        if match := re.search(r"job\s*(\d+)", output, re.IGNORECASE):
            return int(match.group(1)), err_file


@pytest.fixture
def cluster_removal_cleanup():
    procs = []
    yield procs
    for proc in procs:
        proc.terminate()
        proc.wait()
    # Restore the setup for subsequent tests, also if the test failed
    atf.select_cluster(1)
    cancel_federation_jobs()
    restore_federation()


def test_federation_cluster_removal(cluster_removal_cleanup):
    """Test removing clusters from federation.

    Includes a federated srun that is pending in all the clusters, and that
    must not be signaled when the clusters are removed.
    """
    cancel_federation_jobs()

    jobs = {}

    # Submit initial jobs across federation
    jobs[0] = submit_batch_job(f"-M{cls1}")
    atf.wait_for_job_state(
        jobs[0], "RUNNING", siblings=True, clusters=[cls1], fatal=True
    )

    jobs[1] = submit_batch_job(f"-M{cls2}")
    atf.wait_for_job_state(
        jobs[1], "RUNNING", siblings=True, clusters=[cls2], fatal=True
    )

    jobs[2] = submit_batch_job()
    atf.wait_for_job_state(
        jobs[2], "RUNNING", siblings=True, clusters=[cls3], fatal=True
    )

    atf.wait_for_job_state(jobs[0], "RUNNING", clusters=cls1, timeout=60)
    atf.wait_for_job_state(jobs[1], "RUNNING", clusters=cls2, timeout=60)
    atf.wait_for_job_state(jobs[2], "RUNNING", clusters=cls3, timeout=60)

    # Submit jobs with cluster constraints
    jobs[6] = submit_batch_job("--cluster-constraint=fb,fc")
    jobs[7] = submit_batch_job("--cluster-constraint=fb,fc")
    jobs[8] = submit_batch_job("--cluster-constraint=fb,fc")
    jobs[9] = submit_batch_job("--cluster-constraint=fb")
    jobs[10] = submit_batch_job("--cluster-constraint=fa")

    # Submit more jobs from different clusters
    # Submit more jobs, from different origin clusters
    jobs[3] = submit_batch_job()
    # Submit j(4) targeting cls2 as origin by selecting cluster 2
    atf.select_cluster(2)
    jobs[4] = submit_batch_job()
    # Submit j(5) targeting cls3 as origin by selecting cluster 3
    atf.select_cluster(3)
    jobs[5] = submit_batch_job()
    atf.select_cluster(1)

    # Submit a pending federated srun, with origin cls2 and siblings in all of them
    srun_script = atf.module_tmp_path / "removal_srun.sh"
    atf.make_bash_script(str(srun_script), "sleep infinity")
    srun_jid, srun_err = submit_exclusive_fed_srun(
        cluster_removal_cleanup, 2, srun_script
    )
    srun_proc = cluster_removal_cleanup[0]
    atf.wait_for_job_state(
        srun_jid, "PENDING", siblings=True, all_clusters=True, fatal=True
    )

    # Checkpoint 1: Initial state - all 3 clusters in federation
    # j(0) submitted with -M{cls1}: sv=cls1, sa=cls1, origin=cls1
    # j(1) submitted with -M{cls2}: sv=cls2, sa=cls2, origin=cls2
    # j(2) submitted without -M:    sv=cls1,cls2,cls3, sa=cls3, origin=cls1
    all_sv = f"{cls1},{cls2},{cls3}"
    verify_job_fields(cls1, jobs[0], "RUNNING", sv=cls1, sa=cls1, origin=cls1)
    verify_job_fields(cls2, jobs[1], "RUNNING", sv=cls2, sa=cls2, origin=cls2)
    verify_job_fields(cls3, jobs[2], "RUNNING", sv=all_sv, sa=cls3, origin=cls1)

    # Test removing cls1 from federation
    remove_cluster_from_federation(cls1)

    # The pending srun is not signaled when its siblings are revoked
    assert srun_proc.poll() is None, "srun process died after cls1 removal"
    assert "Force Terminated" not in atf.run_command_output(
        f"cat {srun_err}"
    ), "srun got 'Force Terminated' after cls1 removal"

    # Verify job states after cls1 removal.
    # j(4) origin=cls2: should have REVOKED on cls1 (removed) and PENDING on cls2
    def check_j4_sacct():
        output = atf.run_command_output(
            f"sacct -o state,cluster -j {jobs[4]} -M{cls1},{cls2},{cls3} -D",
        )
        return re.search(rf"REVOKED.*{cls1}", output) and re.search(
            rf"PENDING.*{cls2}", output
        )

    assert atf.repeat_until(
        check_j4_sacct, lambda x: x, timeout=15
    ), "Job 4 should be revoked on cls1 and pending on cls2"

    # j(3) origin=cls1 (removed cluster): should be PENDING on origin, siblings REVOKED
    def check_j3_sacct():
        output = atf.run_command_output(
            f"sacct -o state,cluster -j {jobs[3]} -M{cls1},{cls2},{cls3} -D",
        )
        return (
            re.search(rf"PENDING.*{cls1}", output)
            and re.search(rf"REVOKED.*{cls2}", output)
            and re.search(rf"REVOKED.*{cls3}", output)
        )

    assert atf.repeat_until(
        check_j3_sacct, lambda x: x, timeout=15
    ), "Job 3 should be pending on origin cls1 and revoked on siblings"

    # Checkpoint 2: After cls1 removal:
    # j(1) submitted with -M{cls2}: sv=cls2, sa=cls2 (unchanged, single-cluster)
    # j(2) origin=cls1 (removed): becomes non-federated, sv=NA, sa=NA
    verify_job_fields(cls2, jobs[1], "RUNNING", sv=cls2, sa=cls2, origin=cls2)
    verify_job_fields(cls3, jobs[2], "RUNNING", sv="NA", sa="NA")

    # Pending jobs, as documented in federation.shtml:
    # - Origin cls1 removed and pending: stays pending on cls1, non-federated
    verify_job_fields(cls1, jobs[3], "PENDING", sv="NA", sa="NA", origin="NA")
    verify_job_fields(cls1, jobs[10], "PENDING", sv="NA", sa="NA", origin="NA")
    # - Only viable sibling is cls2: becomes non-federated, stays pending there
    verify_job_fields(cls2, jobs[9], "PENDING", sv="NA", sa="NA", origin="NA")
    # - Multiple viable siblings other than the origin: stay federated and
    #   schedule amongst themselves, with cls1 gone from the sibling lists
    remaining = f"{cls2},{cls3}"
    for job in (jobs[6], jobs[7], jobs[8]):
        for cluster in (cls2, cls3):
            verify_job_fields(
                cluster, job, "PENDING", sv=remaining, sa="NA", origin=cls1
            )
    # - Jobs originating on the remaining clusters lose cls1 from their lists
    verify_job_fields(cls2, jobs[4], "PENDING", sv=remaining, sa=remaining, origin=cls2)
    verify_job_fields(cls3, jobs[4], "PENDING", sv=remaining, sa="NA", origin=cls2)
    verify_job_fields(cls2, jobs[5], "PENDING", sv=remaining, sa="NA", origin=cls3)
    verify_job_fields(cls3, jobs[5], "PENDING", sv=remaining, sa=remaining, origin=cls3)

    # Cancel running job to allow scheduling
    cancel_job(jobs[1], cls2)

    # Wait for one of j(6), j(7) and j(8) to start on cls2, any of them can be
    # the first one. As cls1 is not in the federation anymore, look from one of
    # the remaining clusters.
    fb_fc_jobs = [jobs[6], jobs[7], jobs[8]]
    atf.select_cluster(2)
    on_cls2 = wait_for_any_running(fb_fc_jobs, cls2)
    atf.select_cluster(1)

    # Test removing cls2 from federation
    remove_cluster_from_federation(cls2)

    # Checkpoint 3: After cls2 removal:
    # j(2) on cls3: sv=NA, sa=NA (non-federated since origin cls1 was removed earlier)
    verify_job_fields(cls3, jobs[2], "RUNNING", sv="NA", sa="NA")

    # Cancel job to allow scheduling on cls3.
    # Use local scancel on cls3 because j(2) is a non-federated job now
    # (origin cls1 was removed from federation).
    atf.select_cluster(3)
    atf.run_command(f"scancel {jobs[2]}")
    atf.select_cluster(1)
    atf.repeat_until(
        lambda: atf.run_command_output(
            f"squeue -M{cls3} --local --jobs={jobs[2]} --noheader -o '%T'",
        ),
        lambda output: not output.strip() or "CANCEL" in output.upper(),
        timeout=30,
    )

    # Wait for one of the other two jobs to start on cls3
    atf.select_cluster(3)
    on_cls3 = wait_for_any_running([job for job in fb_fc_jobs if job != on_cls2], cls3)
    atf.select_cluster(1)

    # Test removing cls3 from federation
    remove_cluster_from_federation(cls3)

    # Checkpoint 4: After cls3 removal - all jobs are non-federated (sv=NA, sa=NA)
    # Jobs remaining on cls3 should show NA for siblings since no clusters in federation
    verify_job_fields(cls3, on_cls3, "RUNNING", sv="NA", sa="NA", origin="NA")

    # The srun was not signaled by any of the removals
    assert srun_proc.poll() is None, "srun process died after the removals"
    assert "Force Terminated" not in atf.run_command_output(
        f"cat {srun_err}"
    ), "srun got 'Force Terminated' after the removals"


def verify_job_fields(cluster, job_id, expected_state, sv=None, sa=None, origin=None):
    """Verify the federation fields of a job on a specific cluster.

    expected_state = expected JobState (e.g. RUNNING, PENDING)
    sv = expected viable siblings as a comma separated list (or None to skip)
    sa = expected active siblings as a comma separated list (or None to skip)
    origin = expected origin cluster (or None to skip)

    "NA" means the job is not federated on that cluster. The order of the
    sibling lists is not documented, so they are compared as sets.
    """

    def siblings(value):
        if value in (None, "", "NA"):
            return set()
        return set(value.split(","))

    atf.select_cluster(int(cluster.removeprefix("cls")))
    for _ in atf.timer(fatal=True):
        job = atf.get_job(job_id, quiet=True)
        if not job or job["JobState"] != expected_state:
            continue
        if sv is not None and siblings(job.get("FedViableSiblings")) != siblings(sv):
            continue
        if sa is not None and siblings(job.get("FedActiveSiblings")) != siblings(sa):
            continue
        if origin is not None and (job.get("FedOrigin") or "NA") != origin:
            continue
        break
    atf.select_cluster(1)


def test_federation_origin_tracking_removal(cluster_removal_cleanup):
    """Test srun jobs survive cluster removals and receive scontrol notify.

    Submit 3 background srun processes (one per cluster). After each
    cluster removal, verify srun processes are still alive. After all
    removals, send scontrol notify to all 3 and verify each receives
    the message on stderr.
    """
    cancel_federation_jobs()
    setup_cluster_features()

    # Ensure all clusters in federation
    restore_federation()

    # Create a script for srun
    srun_script = atf.module_tmp_path / "origin_tracking_srun.sh"
    atf.make_bash_script(str(srun_script), "sleep infinity")

    # Submit 3 srun jobs from cls1, each one running in the cluster with its
    # feature
    procs = cluster_removal_cleanup
    srun_procs, srun_jids, srun_errs = {}, {}, {}
    for cluster, feature in [(cls1, "fa"), (cls2, "fb"), (cls3, "fc")]:
        srun_jids[cluster], srun_errs[cluster] = submit_exclusive_fed_srun(
            procs, 1, srun_script, feature
        )
        srun_procs[cluster] = procs[-1]

    # Wait for all 3 to be RUNNING
    for cluster in [cls1, cls2, cls3]:
        atf.wait_for_job_state(
            srun_jids[cluster], "RUNNING", siblings=True, clusters=[cluster], fatal=True
        )

    for cluster in [cls1, cls2, cls3]:
        atf.wait_for_job_state(
            srun_jids[cluster], "RUNNING", clusters=cluster, timeout=60
        )

    def assert_sruns_alive(removed_cluster):
        for cluster in [cls1, cls2, cls3]:
            assert (
                srun_procs[cluster].poll() is None
            ), f"srun on {cluster} died after {removed_cluster} removal"

    # The origin cls1 runs its own job and has REVOKED tracking jobs for the
    # ones running in its siblings
    wait_for_local_jobs(cls1, srun_jids.values())

    # Removing a sibling removes its tracking job from the origin, while its
    # job keeps running
    remove_cluster_from_federation(cls2)
    assert_sruns_alive(cls2)
    wait_for_local_jobs(cls1, [srun_jids[cls1], srun_jids[cls3]])

    remove_cluster_from_federation(cls3)
    assert_sruns_alive(cls3)
    wait_for_local_jobs(cls1, [srun_jids[cls1]])

    # Finally remove the origin
    remove_cluster_from_federation(cls1)
    assert_sruns_alive(cls1)

    # Send scontrol notify to all 3 jobs and verify receipt on stderr.
    # Use -M{cluster} to target each cluster directly since they are
    # no longer federated after removal. The "could not load federation
    # information" warning in stderr is expected since clusters have been
    # removed from the federation.
    for cluster in [cls1, cls2, cls3]:
        notify_msg = f"Im Alive {cluster}"
        result = atf.run_command(
            f"scontrol -M{cluster} notify {srun_jids[cluster]} '{notify_msg}'",
        )
        assert (
            result["exit_code"] == 0
        ), f"scontrol notify failed for job on {cluster}: {result}"

        assert atf.repeat_until(
            lambda: atf.run_command_output(f"cat {srun_errs[cluster]}"),
            lambda output: notify_msg in output,
            timeout=10,
        ), f"srun on {cluster} did not receive notify message '{notify_msg}'"

    # Re-add all clusters
    restore_federation()


@pytest.fixture
def srun_survives_cleanup():
    procs = []
    yield procs
    for proc in procs:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
    cancel_federation_jobs()


def test_srun_survives_removals(srun_survives_cleanup):
    """Test that srun job survives cluster removals from federation.

    Submit an srun background job from cls2 (non-origin context), remove
    sibling clusters including cls2 (the origin), verify the srun process
    stays alive and job still RUNNING.
    """
    cancel_federation_jobs()
    setup_cluster_features()

    # Ensure all clusters in federation
    restore_federation()

    # Create a script for srun
    srun_script = atf.module_tmp_path / "srun_test.sh"
    atf.make_bash_script(str(srun_script), "sleep infinity")

    # Submit srun in background from cls2 context (so origin is cls2)
    jid, srun_err = submit_exclusive_fed_srun(
        srun_survives_cleanup, 2, srun_script, "fb"
    )
    srun_proc = srun_survives_cleanup[0]

    atf.wait_for_job_state(jid, "RUNNING", siblings=True, fatal=True)

    # srun was launched from cls2 with cluster-constraint=fb, so origin == running == cls2.
    atf.wait_for_job_state(jid, "RUNNING", clusters=cls2, timeout=60)

    # Remove cls1 from federation (sibling of the srun's origin cls2)
    remove_cluster_from_federation(cls1)

    # Verify srun process still alive
    assert srun_proc.poll() is None, "srun process died after cls1 removal"

    # srun should NOT get "Force Terminated"
    assert "Force Terminated" not in atf.run_command_output(
        f"cat {srun_err}"
    ), "srun got 'Force Terminated' after cls1 removal"

    # Verify job still RUNNING (its origin is cls2, where it runs)
    atf.wait_for_job_state(jid, "RUNNING", cluster=cls2, fatal=True)

    # Remove cls2 from federation (the srun's origin cluster)
    remove_cluster_from_federation(cls2)

    # Verify srun still alive even after its origin cluster is removed
    assert srun_proc.poll() is None, "srun process died after cls2 (origin) removal"

    # srun should still NOT get "Force Terminated"
    assert "Force Terminated" not in atf.run_command_output(
        f"cat {srun_err}"
    ), "srun got 'Force Terminated' after cls2 (origin) removal"

    # Re-add all clusters
    restore_federation()


def test_comprehensive_squeue_during_removals():
    """Test squeue field updates during progressive cluster removals."""
    cancel_federation_jobs()
    setup_cluster_features()

    # Ensure full federation
    restore_federation()

    # Submit jobs to all 3 clusters
    job0 = submit_batch_job(f"-M{cls1}")
    atf.wait_for_job_state(job0, "RUNNING", siblings=True, clusters=[cls1], fatal=True)

    job1 = submit_batch_job(f"-M{cls2}")
    atf.wait_for_job_state(job1, "RUNNING", siblings=True, clusters=[cls2], fatal=True)

    job2 = submit_batch_job()
    atf.wait_for_job_state(job2, "RUNNING", siblings=True, clusters=[cls3], fatal=True)

    atf.wait_for_job_state(job0, "RUNNING", clusters=cls1, timeout=60)
    atf.wait_for_job_state(job1, "RUNNING", clusters=cls2, timeout=60)
    atf.wait_for_job_state(job2, "RUNNING", clusters=cls3, timeout=60)

    # Initial state: squeue should show all 3 jobs with full sibling info
    all_sv = f"{cls1},{cls2},{cls3}"
    output = atf.run_command_output(f"squeue -M{cls1},{cls2},{cls3} --noheader -a")
    for jid in [job0, job1, job2]:
        assert re.search(
            rf"\b{jid}\b", output
        ), f"Job {jid} not in squeue before any removals"

    # Verify squeue fields: initial state
    # job0 (-M{cls1}): sv=cls1, job1 (-M{cls2}): sv=cls2, job2 (no -M): sv=all
    verify_job_fields(cls1, job0, "RUNNING", sv=cls1, sa=cls1, origin=cls1)
    verify_job_fields(cls2, job1, "RUNNING", sv=cls2, sa=cls2, origin=cls2)
    verify_job_fields(cls3, job2, "RUNNING", sv=all_sv, sa=cls3, origin=cls1)

    # Remove cls2
    remove_cluster_from_federation(cls2)

    # job0 on cls1 and job2 on cls3 should still be visible
    output = atf.run_command_output(f"squeue -M{cls1},{cls3} --noheader -a")
    assert re.search(rf"\b{job0}\b", output), "job0 should still be on cls1"
    assert re.search(rf"\b{job2}\b", output), "job2 should still be on cls3"

    # Verify squeue fields after cls2 removal:
    # job0 (-M{cls1}): sv=cls1 (single-cluster, unchanged)
    # job2 (no -M, origin=cls1): sv=cls1,cls3 (cls2 removed from siblings)
    verify_job_fields(cls1, job0, "RUNNING", sv=cls1, sa=cls1, origin=cls1)
    verify_job_fields(cls3, job2, "RUNNING", sv=f"{cls1},{cls3}", sa=cls3, origin=cls1)

    # Remove cls3
    remove_cluster_from_federation(cls3)

    # job0 on cls1 should still be visible
    output = atf.run_command_output(f"squeue -M{cls1} --noheader -a")
    assert re.search(rf"\b{job0}\b", output), "job0 should still be on cls1"

    # Verify squeue fields after cls3 removal:
    # cls1 is still in the federation (only cls2 and cls3 were removed).
    # job0 (-M{cls1}) should show sv=cls1, sa=cls1 (single remaining cluster).
    verify_job_fields(cls1, job0, "RUNNING", sv=cls1, sa=cls1, origin=cls1)

    # Cleanup
    cancel_federation_jobs()
    restore_federation()
