############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


node_count = 2
slurm_user = atf.properties["slurm-user"]


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(node_count)
    atf.require_config_parameter("SlurmdTimeout", 5)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def no_kill_job():
    """Submit a job that should not be killed on node failure"""
    job_id = atf.submit_job_sbatch(f"--no-kill -N{node_count} --wrap 'sleep 300'")
    atf.wait_for_job_state(job_id, "RUNNING")

    # Get an allocated node that it's not the BatchHost before it is removed
    # from the job so we can clean it up
    batch_node = atf.get_job_parameter(job_id, "BatchHost")
    node_list = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    node = next((n for n in node_list if n != batch_node), None)
    if not node:
        pytest.fail(
            "Unable to find a node different than BatchHost, this shouldn't happen"
        )

    yield job_id, node

    # Return the node to the idle state
    atf.run_command(f"scontrol update nodename={node} state=resume", user=slurm_user)


def test_no_kill(no_kill_job):
    """Verify job with --no-kill option is not killed on node failure"""

    job_id, node = no_kill_job

    job_nodes = atf.get_job_parameter(job_id, "NodeList")
    atf.run_command(
        f"scontrol update nodename={node} state=down reason=test_nokill",
        user=slurm_user,
    )
    atf.wait_for_node_state(node, "DOWN", fatal=True)
    job_nodes_post_node_down = atf.get_job_parameter(job_id, "NodeList")
    assert (
        job_nodes != job_nodes_post_node_down
    ), "The job did not lose any nodes after a node was brought down"
    assert (
        atf.get_job_parameter(job_id, "JobState") == "RUNNING"
    ), "The whole job was still canceled with --no-kill set"
