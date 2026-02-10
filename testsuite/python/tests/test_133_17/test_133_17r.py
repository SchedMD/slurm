##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

from .conftest import (
    cancel_job,
    cls1,
    cls2,
    cls3,
    is_job_on_cluster,
    my_wait_for_fed_job,
    submit_job,
    wait_for_depend,
    wait_for_fed_job,
)

pytestmark = pytest.mark.slow


def test_submit_to_all_clusters(federation_setup, file_in_long):
    """Test that a dependent job is only on its origin cluster while dependent."""
    job_id1 = submit_job(f"-M{cls2}", cls2, file_in_long)
    job_id2 = submit_job(
        f"--depend=afternotok:{job_id1} -M{cls1},{cls2},{cls3} --begin=now+300",
        cls1,
        file_in_long,
    )

    my_wait_for_fed_job(job_id1, "RUNNING", cls2)
    job2_cluster = wait_for_fed_job(job_id2, "PENDING", f"{cls1},{cls2},{cls3}")
    wait_for_depend(job_id2, "Dependency", f"afternotok:{job_id1}(unfulfilled)")

    # Verify job_id2 is only on origin cluster
    for cluster in [cls1, cls2, cls3]:
        if cluster != job2_cluster:
            assert not is_job_on_cluster(
                job_id2, cluster
            ), f"Job {job_id2} should not be on non-origin cluster {cluster}"

    # Fulfill dependency -> job should be submitted to all clusters
    cancel_job(job_id1, cls2)
    wait_for_depend(job_id2, "BeginTime", "(null)")
    my_wait_for_fed_job(job_id2, "PENDING", cls1)
    my_wait_for_fed_job(job_id2, "PENDING", cls2)
    my_wait_for_fed_job(job_id2, "PENDING", cls3)
    cancel_job(job_id2, f"{cls1},{cls2},{cls3}")
