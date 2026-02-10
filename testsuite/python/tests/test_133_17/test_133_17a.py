##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

from .conftest import cancel_job, cls1, my_wait_for_fed_job, submit_job, wait_for_depend

pytestmark = pytest.mark.slow


def test_local_after(federation_setup, file_in_long):
    """Test local after dependency succeeds."""
    job_id1 = submit_job(f"-M{cls1} --begin=now+5", cls1, file_in_long)
    job_id2 = submit_job(f"--depend=after:{job_id1} -M{cls1}", cls1, file_in_long)

    wait_for_depend(job_id2, "Dependency", f"after:{job_id1}(unfulfilled)")
    my_wait_for_fed_job(job_id1, "RUNNING", cls1)
    wait_for_depend(job_id2, "None", "(null)")
    my_wait_for_fed_job(job_id2, "RUNNING", cls1)
    cancel_job(job_id1, cls1)
    cancel_job(job_id2, cls1)
