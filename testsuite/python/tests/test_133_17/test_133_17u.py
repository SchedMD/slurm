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
    job_name,
    my_wait_for_fed_job,
    release_job,
    submit_hold_job,
    submit_job,
    wait_for_depend,
)

pytestmark = pytest.mark.slow


@pytest.mark.xfail(
    reason="Issue 51488: a sibling doesn't retract a cleared singleton "
    "dependency when a job with the same name starts on it"
)
def test_singleton_waits_for_job_started_on_sibling(federation_setup, file_in_long):
    """Test that a singleton waits for a job of another cluster that started
    after the siblings cleared the singleton dependency."""
    # job_id1 keeps the singleton of job_id2 pending on cls1
    job_id1 = submit_hold_job(f"-M{cls1}", cls1)
    my_wait_for_fed_job(job_id1, "RUNNING", cls1)
    job_id2 = submit_job(f"--depend=singleton -M{cls1}", cls1, file_in_long)
    wait_for_depend(job_id2, "Dependency", "singleton(unfulfilled)")

    # The siblings test and clear the singletons in submission order, so once a
    # later singleton with another name runs, cls2 and cls3 already cleared the
    # one of job_id2
    probe_id = submit_job(
        f"--depend=singleton -M{cls1}", cls1, file_in_long, name=f"{job_name}_probe"
    )
    my_wait_for_fed_job(probe_id, "RUNNING", cls1)
    cancel_job(probe_id, cls1)

    # job_id3 starts on cls3 after cls3 cleared the singleton of job_id2
    job_id3 = submit_job(f"-M{cls3}", cls3, file_in_long)
    my_wait_for_fed_job(job_id3, "RUNNING", cls3)

    release_job(job_id1)
    my_wait_for_fed_job(job_id1, "COMPLETED", cls1)
    for _ in atf.timer(timeout=10, poll_interval=2, xfail=True):
        assert get_job_dependency(job_id2) == (
            "Dependency",
            "singleton(unfulfilled)",
        ), "Singleton should wait on the running job_id3 of cls3"

    cancel_job(job_id3, cls3)
    wait_for_depend(job_id2, "None", "(null)")
    my_wait_for_fed_job(job_id2, "RUNNING", cls1)
    cancel_job(job_id2, cls1)
