##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

from .conftest import (
    assert_depend_unchanged,
    cancel_job,
    cls1,
    cls2,
    cls3,
    my_wait_for_fed_job,
    submit_job,
    wait_for_depend,
)

pytestmark = pytest.mark.slow


def test_remote_singleton(federation_setup, file_in_long):
    """Test remote singleton dependency."""
    # Singleton waits for all clusters to complete.
    #
    # Based on the documentation (sbatch.1) a singleton job "can begin execution
    # after any previously launched jobs sharing the same job name and user have
    # terminated. In other words, only one job by that name and owned by that user
    # can be running or suspended at any point in time. In a federation, a
    # singleton dependency must be fulfilled on all clusters unless
    # DependencyParameters=disable_remote_singleton is used in slurm.conf".
    job_id1 = submit_job(f"-M{cls1}", cls1, file_in_long)
    job_id2 = submit_job(f"-M{cls2}", cls2, file_in_long)
    job_id3 = submit_job(f"-M{cls3}", cls3, file_in_long)
    my_wait_for_fed_job(job_id1, "RUNNING", cls1)
    my_wait_for_fed_job(job_id2, "RUNNING", cls2)
    my_wait_for_fed_job(job_id3, "RUNNING", cls3)

    job_id4 = submit_job(f"--depend=singleton -M{cls1}", cls1, file_in_long)
    my_wait_for_fed_job(job_id4, "PENDING", cls1)
    wait_for_depend(job_id4, "Dependency", "singleton(unfulfilled)")

    # Job4 shouldn't start until jobs 1,2,3 are done
    cancel_job(job_id1, cls1)
    assert_depend_unchanged(job_id4, "Dependency", "singleton(unfulfilled)")
    cancel_job(job_id2, cls2)
    assert_depend_unchanged(job_id4, "Dependency", "singleton(unfulfilled)")
    cancel_job(job_id3, cls3)
    wait_for_depend(job_id4, "None", "(null)")
    my_wait_for_fed_job(job_id4, "RUNNING", cls1)
    cancel_job(job_id4, cls1)
