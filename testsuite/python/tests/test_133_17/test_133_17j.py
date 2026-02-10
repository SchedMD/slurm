##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

from .conftest import (
    cancel_federation_jobs,
    cancel_job,
    cls1,
    cls2,
    my_wait_for_fed_job,
    submit_job,
    wait_for_depend,
)

pytestmark = pytest.mark.slow


def test_remote_afterany(federation_setup, file_in_long):
    """Test remote afterany dependency + old syntax."""
    # Remote dependency succeeds
    job_id1 = submit_job(f"-M{cls2}", cls2, file_in_long)
    job_id2 = submit_job(f"--depend=afterany:{job_id1} -M{cls1}", cls1, file_in_long)

    wait_for_depend(job_id2, "Dependency", f"afterany:{job_id1}(unfulfilled)")
    my_wait_for_fed_job(job_id1, "RUNNING", cls2)
    cancel_job(job_id1, cls2)
    wait_for_depend(job_id2, "None", "(null)")
    my_wait_for_fed_job(job_id2, "RUNNING", cls1)
    cancel_job(job_id2, cls1)

    # Test old syntax: --depend=jobid[,jobid,...]
    job_id1 = submit_job(f"-M{cls1}", cls1, file_in_long)
    job_id2 = submit_job(f"--depend={job_id1} -M{cls1}", cls1, file_in_long)
    job_id3 = submit_job(f"--depend={job_id1},{job_id2} -M{cls1}", cls1, file_in_long)

    my_wait_for_fed_job(job_id1, "RUNNING", cls1)
    my_wait_for_fed_job(job_id2, "PENDING", cls1)
    my_wait_for_fed_job(job_id3, "PENDING", cls1)
    wait_for_depend(job_id2, "Dependency", f"afterany:{job_id1}(unfulfilled)")
    wait_for_depend(
        job_id3,
        "Dependency",
        f"afterany:{job_id1}(unfulfilled),afterany:{job_id2}(unfulfilled)",
    )
    cancel_federation_jobs()
