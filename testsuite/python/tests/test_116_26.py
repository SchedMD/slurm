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


@pytest.fixture(scope="function")
def no_kill_job(request):
    """Submit a job that should not be killed on node failure, and resume node"""

    no_kill = request.param
    if no_kill:
        no_kill_arg = "--no-kill "
    else:
        no_kill_arg = ""
    job_id = atf.submit_job_sbatch(
        f"{no_kill_arg} -N{node_count} --wrap 'sleep 300'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # Get an allocated node that it's not the BatchHost before it is removed
    # from the job so we can clean it up
    batch_node = atf.get_job_parameter(job_id, "BatchHost")
    node_list = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    node = next((n for n in node_list if n != batch_node), None)
    if not node:
        pytest.fail(
            "Unable to find a node different than BatchHost, this shouldn't happen"
        )

    yield job_id, node, no_kill

    # Return the node to the idle state
    atf.cancel_jobs([job_id])
    atf.run_command(f"scontrol update nodename={node} state=resume", user=slurm_user)


@pytest.mark.parametrize("no_kill_job", [True, False], indirect=True)
def test_no_kill(no_kill_job):
    """Verify job with --no-kill option is not killed on node failure"""

    job_id, node, no_kill = no_kill_job

    job_nodes = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    atf.run_command(
        f"scontrol update nodename={node} state=down reason=test_nokill",
        user=slurm_user,
    )
    atf.wait_for_node_state(node, "DOWN", fatal=True)
    job_nodes_post_node_down = atf.node_range_to_list(
        atf.get_job_parameter(job_id, "NodeList")
    )

    assert (
        len(job_nodes_post_node_down) == len(job_nodes) - 1
    ), "The job should lose one node after it was brought down"
    if no_kill:
        assert (
            atf.get_job_parameter(job_id, "JobState") == "RUNNING"
        ), "The job should keep running with --no-kill set"
    else:
        assert (
            atf.get_job_parameter(job_id, "JobState") != "RUNNING"
        ), "The job should not keep running without --no-kill set"
