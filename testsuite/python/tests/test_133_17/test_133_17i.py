##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import (
    cancel_job,
    cls1,
    cls2,
    get_job_dependency,
    my_wait_for_fed_job,
    submit_job,
    wait_for_depend,
)

pytestmark = pytest.mark.slow


def test_remote_after(federation_setup, file_in_long):
    """Test remote after dependency: success + after+time."""
    # Remote dependency succeeds
    job_id1 = submit_job(f"-M{cls2} --begin=now+5", cls2, file_in_long)
    job_id2 = submit_job(f"--depend=after:{job_id1} -M{cls1}", cls1, file_in_long)

    wait_for_depend(job_id2, "Dependency", f"after:{job_id1}(unfulfilled)")
    my_wait_for_fed_job(job_id1, "RUNNING", cls2)
    wait_for_depend(job_id2, "None", "(null)")
    my_wait_for_fed_job(job_id2, "RUNNING", cls1)
    cancel_job(job_id1, cls2)
    cancel_job(job_id2, cls1)

    # Test after+time: it is fulfilled 1 minute after job_id1 starts
    job_id1 = submit_job(f"-M{cls1}", cls1, file_in_long)
    job_id2 = submit_job(f"--depend=after:{job_id1}+1 -M{cls1}", cls1, file_in_long)

    my_wait_for_fed_job(job_id1, "RUNNING", cls1)
    my_wait_for_fed_job(job_id2, "PENDING", cls1)
    wait_for_depend(job_id2, "Dependency", f"after:{job_id1}+1(unfulfilled)")
    # Assert dependency remains unfulfilled for at least 45 seconds
    atf.repeat_until(
        lambda: get_job_dependency(job_id2),
        lambda result: result != ("Dependency", f"after:{job_id1}+1(unfulfilled)"),
        timeout=45,
        xfail=True,
        fatal=True,
    )
    # Wait for dependency to be fulfilled
    wait_for_depend(job_id2, "None", "(null)")
    my_wait_for_fed_job(job_id2, "RUNNING", cls1)
    cancel_job(job_id2, cls1)
