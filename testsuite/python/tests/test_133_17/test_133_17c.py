##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

from .conftest import (
    cancel_job,
    cls1,
    my_wait_for_fed_job,
    release_job,
    submit_hold_job,
    submit_job,
    wait_for_depend,
)

pytestmark = pytest.mark.slow


def test_local_aftercorr(federation_setup, file_in_long):
    """Test local aftercorr dependencies with job arrays."""
    # Sub-test 1: non-array job behaves like afterok
    job1 = submit_hold_job(f"-M{cls1}", cls1)
    job_array2 = submit_job(
        f"--depend=aftercorr:{job1} -M{cls1} --array=1-2", cls1, file_in_long
    )
    my_wait_for_fed_job(job1, "RUNNING", cls1)
    my_wait_for_fed_job(f"{job_array2}_1", "PENDING", cls1)
    my_wait_for_fed_job(f"{job_array2}_2", "PENDING", cls1)
    wait_for_depend(f"{job_array2}_1", "Dependency", f"aftercorr:{job1}(unfulfilled)")
    wait_for_depend(f"{job_array2}_2", "Dependency", f"aftercorr:{job1}(unfulfilled)")
    release_job(job1)
    my_wait_for_fed_job(job1, "DONE", cls1)
    wait_for_depend(f"{job_array2}_1", "None", "(null)")
    wait_for_depend(f"{job_array2}_2", "None", "(null)")
    my_wait_for_fed_job(f"{job_array2}_1", "RUNNING", cls1)
    my_wait_for_fed_job(f"{job_array2}_2", "RUNNING", cls1)
    cancel_job(job_array2, cls1)

    # Sub-test 2: array-to-array success
    job_array1 = submit_hold_job(f"-M{cls1} --array=1-2", cls1)
    job_array2 = submit_job(
        f"--depend=aftercorr:{job_array1} -M{cls1} --array=1-2", cls1, file_in_long
    )
    my_wait_for_fed_job(f"{job_array1}_1", "RUNNING", cls1)
    my_wait_for_fed_job(f"{job_array1}_2", "RUNNING", cls1)
    my_wait_for_fed_job(f"{job_array2}_1", "PENDING", cls1)
    my_wait_for_fed_job(f"{job_array2}_2", "PENDING", cls1)
    wait_for_depend(
        f"{job_array2}_1",
        "Dependency",
        f"aftercorr:{job_array1}_*(unfulfilled)",
    )
    wait_for_depend(
        f"{job_array2}_2",
        "Dependency",
        f"aftercorr:{job_array1}_*(unfulfilled)",
    )
    release_job(job_array1)
    my_wait_for_fed_job(f"{job_array1}_1", "DONE", cls1)
    my_wait_for_fed_job(f"{job_array1}_2", "DONE", cls1)
    wait_for_depend(f"{job_array2}_1", "None", "(null)")
    wait_for_depend(f"{job_array2}_2", "None", "(null)")
    my_wait_for_fed_job(f"{job_array2}_1", "RUNNING", cls1)
    my_wait_for_fed_job(f"{job_array2}_2", "RUNNING", cls1)
    cancel_job(job_array2, cls1)

    # Sub-test 3: array-to-array failure
    job_array1 = submit_job(f"-M{cls1} --array=1-2", cls1, file_in_long)
    job_array2 = submit_job(
        f"--depend=aftercorr:{job_array1} -M{cls1} --array=1-2", cls1, file_in_long
    )
    my_wait_for_fed_job(f"{job_array1}_1", "RUNNING", cls1)
    my_wait_for_fed_job(f"{job_array1}_2", "RUNNING", cls1)
    my_wait_for_fed_job(f"{job_array2}_1", "PENDING", cls1)
    my_wait_for_fed_job(f"{job_array2}_2", "PENDING", cls1)
    wait_for_depend(
        f"{job_array2}_1",
        "Dependency",
        f"aftercorr:{job_array1}_*(unfulfilled)",
    )
    wait_for_depend(
        f"{job_array2}_2",
        "Dependency",
        f"aftercorr:{job_array1}_*(unfulfilled)",
    )
    cancel_job(f"{job_array1}_1", cls1)
    wait_for_depend(
        f"{job_array2}_1",
        "DependencyNeverSatisfied",
        f"aftercorr:{job_array1}_*(failed)",
    )
    cancel_job(f"{job_array1}_2", cls1)
    wait_for_depend(
        f"{job_array2}_2",
        "DependencyNeverSatisfied",
        f"aftercorr:{job_array1}_*(failed)",
    )
    cancel_job(job_array2, cls1)
