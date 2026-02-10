##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

from .conftest import (
    cancel_job,
    cls1,
    cls2,
    my_wait_for_fed_job,
    release_job,
    submit_hold_job,
    submit_job,
    wait_for_depend,
)

pytestmark = pytest.mark.slow


def test_remote_afterok(federation_setup, file_in_long):
    """Test remote afterok dependency: success and failure."""
    # Success
    job_id1 = submit_hold_job(f"-M{cls2}", cls2)
    job_id2 = submit_job(f"--depend=afterok:{job_id1} -M{cls1}", cls1, file_in_long)

    my_wait_for_fed_job(job_id1, "RUNNING", cls2)
    my_wait_for_fed_job(job_id2, "PENDING", cls1)
    wait_for_depend(job_id2, "Dependency", f"afterok:{job_id1}(unfulfilled)")
    release_job(job_id1)
    my_wait_for_fed_job(job_id1, "DONE", cls2)
    wait_for_depend(job_id2, "None", "(null)")
    my_wait_for_fed_job(job_id2, "RUNNING", cls1)
    cancel_job(job_id2, cls1)

    # Failure
    job_id1 = submit_job(f"-M{cls2}", cls2, file_in_long)
    job_id2 = submit_job(f"--depend=afterok:{job_id1} -M{cls1}", cls1, file_in_long)

    my_wait_for_fed_job(job_id1, "RUNNING", cls2)
    my_wait_for_fed_job(job_id2, "PENDING", cls1)
    wait_for_depend(job_id2, "Dependency", f"afterok:{job_id1}(unfulfilled)")
    cancel_job(job_id1, cls2)
    wait_for_depend(job_id2, "DependencyNeverSatisfied", f"afterok:{job_id1}(failed)")
    cancel_job(job_id2, cls1)
