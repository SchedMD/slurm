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
    atf.require_version((25, 11), "sbin/slurmctld")

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


def test_segment_nodelist():
    """Test simplest segment and nodelist"""

    job_id_1 = atf.submit_job_sbatch(
        '-N 4 -w node[1-4] --segment=2 --exclusive --mem=1 --wrap="exit 0"'
    )
    atf.wait_for_job_state(job_id_1, "COMPLETED", fatal=True, timeout=5)

    job_id_2 = atf.submit_job_sbatch(
        '-N 4 -w node[1,2,5,6] --segment=2 --exclusive --mem=1 --wrap="exit 0"'
    )
    atf.wait_for_job_state(job_id_2, "COMPLETED", fatal=True, timeout=5)

    job_id_3 = atf.submit_job_sbatch(
        '-N 6 -w node[1-8] --segment=3 --exclusive --mem=1 --wrap="exit 0"'
    )
    atf.wait_for_job_state(job_id_3, "COMPLETED", fatal=True, timeout=5)

    job_id_4 = atf.submit_job_sbatch(
        '-N 6 -w node[1-3,5-7] --segment=3 --exclusive --mem=1 --wrap="exit 0"'
    )
    atf.wait_for_job_state(job_id_4, "COMPLETED", fatal=True, timeout=5)

    assert (
        atf.submit_job_sbatch(
            '-N 6 -w node[1-6] --segment=3 --exclusive --mem=1 --wrap="sleep 20"',
            xfail=True,
        )
        == 0
    ), "Job should fail segment #2 not fit on b2 "

    assert (
        atf.submit_job_sbatch(
            '-N 6 -w node[1-5] --segment=3 --exclusive --mem=1 --wrap="sleep 20"',
            xfail=True,
        )
        == 0
    ), "Job should fail requested node count > number of specified nodes"

    atf.cancel_all_jobs(quiet=True)
