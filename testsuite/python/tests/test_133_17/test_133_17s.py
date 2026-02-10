##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
# Tests of the DependencyParameters options that change the behavior of the
# dependencies of a federation, so they need them in the slurm.conf of all the
# clusters. The rest of the tests of this package use the default behavior.
import pytest

import atf

from .conftest import (
    add_cluster_to_fed,
    cancel_job,
    cls1,
    cls2,
    cls3,
    my_wait_for_fed_job,
    remove_cluster_from_fed,
    submit_job,
    wait_for_depend,
)

pytestmark = pytest.mark.slow

# Read by the setup() of the conftest, to configure all the clusters
dependency_parameters = ["kill_invalid_depend", "disable_remote_singleton"]


def test_kill_invalid_depend(federation_setup, file_in_long):
    """Test that a job with a dependency that can never be satisfied is
    cancelled, instead of staying pending with reason DependencyNeverSatisfied."""
    # Local dependency
    job_id1 = submit_job(f"-M{cls1}", cls1, file_in_long)
    job_id2 = submit_job(f"--depend=afterok:{job_id1} -M{cls1}", cls1, file_in_long)

    my_wait_for_fed_job(job_id1, "RUNNING", cls1)
    my_wait_for_fed_job(job_id2, "PENDING", cls1)
    wait_for_depend(job_id2, "Dependency", f"afterok:{job_id1}(unfulfilled)")
    cancel_job(job_id1, cls1)
    atf.wait_for_job_state(
        job_id2, "CANCELLED", siblings=True, clusters=cls1, fatal=True
    )

    # Remote dependency
    job_id3 = submit_job(f"-M{cls2}", cls2, file_in_long)
    job_id4 = submit_job(f"--depend=afterok:{job_id3} -M{cls1}", cls1, file_in_long)

    my_wait_for_fed_job(job_id3, "RUNNING", cls2)
    my_wait_for_fed_job(job_id4, "PENDING", cls1)
    wait_for_depend(job_id4, "Dependency", f"afterok:{job_id3}(unfulfilled)")
    cancel_job(job_id3, cls2)
    atf.wait_for_job_state(
        job_id4, "CANCELLED", siblings=True, clusters=cls1, fatal=True
    )


def test_remote_singleton_disabled(federation_setup, file_in_long):
    """Test that with disable_remote_singleton the jobs of the other clusters
    don't affect the singleton dependency."""
    job_id1 = submit_job(f"-M{cls1}", cls1, file_in_long)
    job_id2 = submit_job(f"-M{cls2}", cls2, file_in_long)
    job_id3 = submit_job(f"--depend=singleton -M{cls1}", cls1, file_in_long)

    my_wait_for_fed_job(job_id1, "RUNNING", cls1)
    my_wait_for_fed_job(job_id2, "RUNNING", cls2)
    my_wait_for_fed_job(job_id3, "PENDING", cls1)
    wait_for_depend(job_id3, "Dependency", "singleton(unfulfilled)")
    # Cancel local job - job3 should start even though job2 is on cls2
    cancel_job(job_id1, cls1)
    wait_for_depend(job_id3, "None", "(null)")
    my_wait_for_fed_job(job_id3, "RUNNING", cls1)
    cancel_job(job_id2, cls2)
    cancel_job(job_id3, cls1)


def test_add_remove_clusters(federation_setup, file_in_long):
    """Test adding/removing clusters from federation affects dependencies, with
    the dependency options."""
    job_id1 = submit_job(f"-M{cls1}", cls1, file_in_long)
    job_id2 = submit_job(f"-M{cls2}", cls2, file_in_long)
    job_id3 = submit_job(f"-M{cls3}", cls3, file_in_long)
    job_id4 = submit_job(f"--depend=afterok:{job_id3} -M{cls1}", cls1, file_in_long)
    job_id5 = submit_job(f"--depend=singleton -M{cls1}", cls1, file_in_long)
    my_wait_for_fed_job(job_id1, "RUNNING", cls1)
    my_wait_for_fed_job(job_id2, "RUNNING", cls2)
    my_wait_for_fed_job(job_id3, "RUNNING", cls3)
    my_wait_for_fed_job(job_id4, "PENDING", cls1)
    wait_for_depend(job_id4, "Dependency", f"afterok:{job_id3}(unfulfilled)")
    wait_for_depend(job_id5, "Dependency", "singleton(unfulfilled)")

    # Remove cls3 -> afterok dependency on job_id3 should fail, so job_id4 is
    # cancelled
    remove_cluster_from_fed(cls3)
    atf.wait_for_job_state(
        job_id4, "CANCELLED", siblings=True, clusters=cls1, fatal=True
    )

    cancel_job(job_id1, cls1)
    cancel_job(job_id2, cls2)
    # job_id3 is not in the federation anymore, so only cls3 knows it
    atf.run_command(f"scancel -M{cls3} {job_id3}", fatal=True)
    atf.wait_for_job_state(job_id3, "CANCELLED", cluster=cls3, fatal=True)
    cancel_job(job_id5, cls1)
    # Re-add cls3 for subsequent tests
    add_cluster_to_fed(cls3)
