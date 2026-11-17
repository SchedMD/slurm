##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import (
    cancel_job,
    cls1,
    cls2,
    my_wait_for_fed_job,
    submit_job,
    wait_for_depend,
)

pytestmark = pytest.mark.slow


def test_remote_or_dependencies(federation_setup, file_in_long):
    """Test remote OR dependencies."""
    atf.require_version(
        (25, 11),
        "sbin/slurmctld",
        reason="Ticket 19658: failed remote dependencies are marked as failed since 25.11",
    )
    # Sub-test 1: one fulfills
    job_id1 = submit_job(f"-M{cls1}", cls1, file_in_long)
    job_id2 = submit_job(f"-M{cls2}", cls2, file_in_long)
    job_id3 = submit_job(
        f"--depend=afternotok:{job_id1}?afternotok:{job_id2} -M{cls1}",
        cls1,
        file_in_long,
    )

    my_wait_for_fed_job(job_id1, "RUNNING", cls1)
    my_wait_for_fed_job(job_id2, "RUNNING", cls2)
    my_wait_for_fed_job(job_id3, "PENDING", cls1)
    wait_for_depend(
        job_id3,
        "Dependency",
        f"afternotok:{job_id1}(unfulfilled)?afternotok:{job_id2}(unfulfilled)",
    )
    cancel_job(job_id2, cls2)
    wait_for_depend(job_id3, "None", "(null)")
    my_wait_for_fed_job(job_id3, "RUNNING", cls1)
    cancel_job(job_id1, cls1)
    cancel_job(job_id3, cls1)

    # Sub-test 2: all fail
    job_id1 = submit_job(f"-M{cls1}", cls1, file_in_long)
    job_id2 = submit_job(f"-M{cls2}", cls2, file_in_long)
    job_id3 = submit_job(
        f"--depend=afterok:{job_id1}?afterok:{job_id2} -M{cls1}",
        cls1,
        file_in_long,
    )

    my_wait_for_fed_job(job_id1, "RUNNING", cls1)
    my_wait_for_fed_job(job_id2, "RUNNING", cls2)
    my_wait_for_fed_job(job_id3, "PENDING", cls1)
    wait_for_depend(
        job_id3,
        "Dependency",
        f"afterok:{job_id1}(unfulfilled)?afterok:{job_id2}(unfulfilled)",
    )
    cancel_job(job_id1, cls1)
    wait_for_depend(
        job_id3,
        "Dependency",
        f"afterok:{job_id1}(failed)?afterok:{job_id2}(unfulfilled)",
    )
    cancel_job(job_id2, cls2)
    wait_for_depend(
        job_id3,
        "DependencyNeverSatisfied",
        f"afterok:{job_id1}(failed)?afterok:{job_id2}(failed)",
    )
    cancel_job(job_id3, cls1)

    # Sub-test 3: non-existent remote job
    job_id1 = submit_job(f"-M{cls1}", cls1, file_in_long)
    fake_job_id = job_id1 + 123
    job_id2 = submit_job(
        f"--depend=afternotok:{job_id1}?afternotok:{fake_job_id} -M{cls2}",
        cls2,
        file_in_long,
    )

    my_wait_for_fed_job(job_id1, "RUNNING", cls1)
    my_wait_for_fed_job(job_id2, "PENDING", cls2)
    wait_for_depend(
        job_id2,
        "Dependency",
        f"afternotok:{job_id1}(unfulfilled)?afternotok:{fake_job_id}(failed)",
    )
    cancel_job(job_id1, cls1)
    wait_for_depend(job_id2, "None", "(null)")
    cancel_job(job_id2, cls2)
