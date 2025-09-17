############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create custom topology.conf")
    atf.require_nodes(8)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TopologyPlugin", "topology/block")

    # Mark topology for teardown and overwrite with proper data.
    # require_config_parameter marks a file for teardown,
    #  but it doesn't allow us to write multiple lines easily to an external conf.
    # We're using it to create/mark the file.
    atf.require_config_parameter("", "", source="topology")
    # This is where we write the actual data
    overwrite_topology_conf()
    atf.add_config_parameter_value(
        "SchedulerParameters", "bf_interval=1,sched_interval=1"
    )
    atf.require_slurm_running()


def overwrite_topology_conf():
    conf = atf.properties["slurm-config-dir"] + "/topology.conf"
    content = """
        BlockName=b1 Nodes=node[1-4]
        BlockName=b2 Nodes=node[5-8]
        BlockSizes=4,8
    """
    atf.run_command(f"cat > {conf}", input=content, user="slurm", fatal=True)


def test_const1():
    """Test simplest topology-constrained"""

    job_id_1 = atf.submit_job_sbatch('-N 2 --exclusive --mem=1 --wrap="sleep 20"')
    job_id_2 = atf.submit_job_sbatch('-N 3 --exclusive --mem=1 --wrap="sleep 20"')
    atf.wait_for_job_state(job_id_1, "RUNNING")
    atf.wait_for_job_state(job_id_2, "RUNNING")
    job_id_3 = atf.submit_job_sbatch('-N 3 --exclusive --mem=1 --wrap="sleep 20"')

    assert not atf.wait_for_job_state(
        job_id_3, "RUNNING", timeout=5, xfail=True
    ), "Verify that job #3 is not run"

    assert atf.wait_for_job_state(
        job_id_3, "PENDING", "Resources"
    ), "Verify that job #3 is not run due resources"
    atf.cancel_all_jobs(quiet=True)


def test_const2():
    """Test multi-block job"""

    job_id_1 = atf.submit_job_sbatch('-N 3 --exclusive --mem=1 --wrap="sleep 20"')
    job_id_2 = atf.submit_job_sbatch('-N 5 --exclusive --mem=1 --wrap="sleep 20"')
    atf.wait_for_job_state(job_id_1, "RUNNING", fatal=True, timeout=5)
    atf.wait_for_job_state(job_id_2, "RUNNING", fatal=True, timeout=5)
    atf.cancel_all_jobs(quiet=True)


def test_segment1():
    """Test simplest segment"""

    job_id_1 = atf.submit_job_sbatch(
        '-N 4 --segment=2 --exclusive --mem=1 --wrap="sleep 20"'
    )
    atf.wait_for_job_state(job_id_1, "RUNNING", fatal=True, timeout=5)
    assert (
        atf.submit_job_sbatch(
            '-N 5 --segment=2 --exclusive --mem=1 --wrap="sleep 20"', xfail=True
        )
        == 0
    ), "Job should fail -- 5%2 "
    atf.cancel_all_jobs(quiet=True)
