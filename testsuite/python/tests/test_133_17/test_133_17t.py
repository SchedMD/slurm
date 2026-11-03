##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import (
    cancel_job,
    cls1,
    cls3,
    get_job_dependency,
    my_wait_for_fed_job,
    submit_job,
    wait_for_depend,
)

pytestmark = pytest.mark.slow


@pytest.mark.xfail(
    reason="Issue 51486: a sibling ignores its previously submitted pending "
    "jobs when clearing a singleton dependency"
)
def test_singleton_waits_for_pending_job_on_sibling(federation_setup, file_in_long):
    """Test that a singleton waits for a previously submitted pending job of
    another cluster."""
    # job_id1 is submitted before job_id2, but as its origin is cls3, its job
    # id is higher
    job_id1 = submit_job(f"--hold -M{cls3}", cls3, file_in_long)
    my_wait_for_fed_job(job_id1, "PENDING", cls3)
    job_id2 = submit_job(f"--depend=singleton -M{cls1}", cls1, file_in_long)
    wait_for_depend(job_id2, "Dependency", "singleton(unfulfilled)")

    # The siblings test the singletons every 30s
    for _ in atf.timer(timeout=60, poll_interval=5, xfail=True):
        assert get_job_dependency(job_id2) == (
            "Dependency",
            "singleton(unfulfilled)",
        ), "Singleton should wait on the pending job_id1 of cls3"

    cancel_job(job_id1, cls3)
    wait_for_depend(job_id2, "None", "(null)")
    my_wait_for_fed_job(job_id2, "RUNNING", cls1)
    cancel_job(job_id2, cls1)
