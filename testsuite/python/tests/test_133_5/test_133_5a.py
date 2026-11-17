##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import (
    check_ctl_state,
    check_dbd_states,
    check_hold,
    cls1,
    cls2,
    cls3,
    wait_for_running_cluster,
)

pytestmark = pytest.mark.slow


def test_federation_requeue_scenarios(federation_setup, cleanup, scripts, node_count):
    """Test various federation requeue scenarios.

    Validates requeue'ing of federated jobs:
    - Running jobs on origin cluster can be requeued
    - Jobs running on sibling clusters can be requeued from origin
    - Requeued jobs get rescheduled
    - requeuehold places jobs in held state
    - release from requeuehold allows job to run again
    """
    file_in = scripts["sleep_long"]

    # Submit jobs to fill clusters (one per cluster)
    ji0 = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null --error=/dev/null --requeue --cluster-constraint=fa {file_in}",
        fatal=True,
    )
    ji1 = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null --error=/dev/null --requeue --cluster-constraint=fb {file_in}",
        fatal=True,
    )
    ji2 = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null --error=/dev/null --requeue --cluster-constraint=fc {file_in}",
        fatal=True,
    )

    # Wait for all jobs to start running on their respective clusters
    c0 = wait_for_running_cluster(ji0)
    assert c0 == cls1, f"Job {ji0} not running on {cls1}"
    c1 = wait_for_running_cluster(ji1)
    assert c1 == cls2, f"Job {ji1} not running on {cls2}"
    c2 = wait_for_running_cluster(ji2)
    assert c2 == cls3, f"Job {ji2} not running on {cls3}"

    # Requeue running job on origin cluster (cls1)
    atf.run_command(f"scontrol requeue {ji0}", fatal=True)

    # With requeue_delay=0 the requeued job is eligible right away, so its
    # PENDING state is transient; the REQUEUED record in the dbd is not.
    # Verify sacct shows REQUEUED state on origin
    check_dbd_states(ji0, cls1, ["REQUEUED"])

    # Requeue running job on sibling cluster (cls2) from origin
    atf.run_command(f"scontrol requeue {ji1}", fatal=True)

    # Verify sacct states for ji1
    check_dbd_states(ji1, cls2, ["REQUEUED"])

    # Requeue running job on sibling cluster (cls3) from origin
    atf.run_command(f"scontrol requeue {ji2}", fatal=True)

    # Verify sacct states for ji2
    check_dbd_states(ji2, cls3, ["REQUEUED"])

    # Verify all jobs get rescheduled
    atf.wait_for_job_state(ji0, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(ji1, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(ji2, "RUNNING", siblings=True, fatal=True)

    # Verify scontrol shows RUNNING state
    check_ctl_state(ji0, cls1, "RUNNING")

    # Test requeuehold - requeue and hold a running job
    atf.run_command(f"scontrol requeuehold {ji0}", fatal=True)

    # Verify job is pending and held
    check_hold(ji0)

    # Release the held job
    atf.run_command(f"scontrol release {ji0}", fatal=True)

    # Verify it starts running again
    atf.wait_for_job_state(ji0, "RUNNING", siblings=True, fatal=True)
