##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import check_dbd_states, check_hold, cls1, wait_for_running_cluster

pytestmark = pytest.mark.slow


def test_requeuehold_running_job(federation_setup, cleanup, scripts, node_count):
    """Test requeuehold on a running federated job.

    Verifies job goes to PENDING+held state with Priority=0.
    """
    file_in = scripts["sleep_long"]

    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null --error=/dev/null"
        f" --requeue --cluster-constraint=fa {file_in}",
        fatal=True,
    )

    c = wait_for_running_cluster(job_id)
    assert c == cls1, f"Job not running on {cls1}"

    # Requeuehold
    atf.run_command(f"scontrol requeuehold {job_id}", fatal=True)

    # Verify sacct states after requeuehold
    check_dbd_states(job_id, cls1, ["REQUEUED"])

    # Verify job is pending and held
    check_hold(job_id)

    # Release and verify it runs again
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", siblings=True, fatal=True)
