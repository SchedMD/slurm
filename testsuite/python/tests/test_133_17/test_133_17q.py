##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

from .conftest import (
    add_cluster_to_fed,
    assert_depend_unchanged,
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


def test_add_remove_clusters(federation_setup, file_in_long):
    """Test adding/removing clusters from federation affects dependencies."""
    job_id1 = submit_job(f"-M{cls1}", cls1, file_in_long)
    job_id2 = submit_job(f"-M{cls2}", cls2, file_in_long)
    job_id3 = submit_job(f"-M{cls3}", cls3, file_in_long)
    my_wait_for_fed_job(job_id1, "RUNNING", cls1)
    my_wait_for_fed_job(job_id2, "RUNNING", cls2)
    my_wait_for_fed_job(job_id3, "RUNNING", cls3)
    job_id4 = submit_job(f"--depend=afterok:{job_id3} -M{cls1}", cls1, file_in_long)
    job_id5 = submit_job(f"--depend=singleton -M{cls1}", cls1, file_in_long)
    my_wait_for_fed_job(job_id4, "PENDING", cls1)
    wait_for_depend(job_id4, "Dependency", f"afterok:{job_id3}(unfulfilled)")
    wait_for_depend(job_id5, "Dependency", "singleton(unfulfilled)")

    # Remove cls3 -> afterok dependency on job_id3 should fail
    remove_cluster_from_fed(cls3)
    wait_for_depend(job_id4, "DependencyNeverSatisfied", f"afterok:{job_id3}(failed)")
    cancel_job(job_id4, cls1)

    # Add cls3 back -> singleton should wait on it again
    add_cluster_to_fed(cls3)
    cancel_job(job_id1, cls1)
    cancel_job(job_id2, cls2)
    assert_depend_unchanged(job_id5, "Dependency", "singleton(unfulfilled)")
    cancel_job(job_id3, cls3)
    wait_for_depend(job_id5, "None", "(null)")
    my_wait_for_fed_job(job_id5, "RUNNING", cls1)
    cancel_job(job_id5, cls1)
