############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    # Ensure conflicting options are not configured
    # TODO: Remove DeferBatch requirement once issue #50628 is fixed
    atf.require_config_parameter_excludes("PrologFlags", "DeferBatch")
    atf.require_config_parameter_excludes("PrologFlags", "ForceRequeueOnFail")

    # Not strictly required, but let's make sure requeue is enabled
    atf.require_config_parameter("JobRequeue", "1")

    atf.require_nodes(1)
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def cleanup_state(setup):
    yield
    atf.cancel_all_jobs()

    atf.run_command(
        "scontrol update nodename=ALL state=resume",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.mark.xfail(
    atf.get_version() < (25, 5),
    reason="Ticket 20604: Spank error codes were not properly propagated. Fixed in 25.05.",
)
@pytest.mark.parametrize("mode", ["job", "node"])
def test_slurm_spank_init_failure_mode(mode, cleanup_state, spank_fail_lib):
    """
    Test ESPANK_[JOB|NODE]_FAILURE
    """

    # Ensure the SPANK plugin is included in plugstack.conf
    atf.require_config_parameter(
        "required",
        f"{spank_fail_lib} slurm_spank_init remote {mode}",
        delimiter=" ",
        source="plugstack",
    )

    node = next(iter(atf.nodes))
    job_id = atf.submit_job_sbatch(f"-w {node} --wrap 'srun true'", fatal=True)

    if mode == "job":
        # The node should NOT be DRAIN
        assert atf.repeat_until(
            lambda: atf.get_node_parameter(node, "state"),
            lambda state: "IDLE" in state,
        ), f"Node {node} should be IDLE when 'remote {mode}' is configured"

        # The job should be FAILED (not requeued)
        assert atf.wait_for_job_state(
            job_id, "FAILED"
        ), f"Job {job_id} should be FAILED when 'remote {mode}' is configured"
    else:
        # The node should be DRAIN
        assert atf.repeat_until(
            lambda: atf.get_node_parameter(node, "state"),
            lambda state: "DRAIN" in state,
        ), f"Node {node} should be DRAIN when 'remote {mode}' is configured"

        # The job should be requeued
        assert atf.wait_for_job_state(
            job_id, "PENDING"
        ), f"Job {job_id} should be PENDING when 'remote {mode}' is configured"
        assert atf.repeat_command_until(
            f"scontrol show job {job_id}",
            lambda results: re.search(r"Restarts=1", results["stdout"]),
        ), f"Job {job_id} should be requeued when 'remote {mode}' is configured"
