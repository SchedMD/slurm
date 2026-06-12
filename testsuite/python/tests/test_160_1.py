############################################################################
# "Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
import atf
import pytest
import logging

pytestmark = pytest.mark.slow

slurm_user = atf.properties["slurm-user"]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "bin/scontrol",
        reason="Issue 50781: Dynamic memory limits were added in 26.05",
    )
    atf.require_nodes(2, [("RealMemory", 500)])
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Alloc")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_config_parameter("JobAcctGatherType", "jobacct_gather/cgroup")
    atf.require_config_parameter_includes("TaskPlugin", "cgroup")
    atf.require_slurm_running()


def test_scontrol_reduce_memory():
    """Test manual memory reduction on a running job via scontrol."""

    job_id = atf.submit_job_sbatch("--mem=400 -N1 --wrap 'sleep 300'", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    node = atf.get_job_parameter(job_id, "BatchHost")

    assert atf.get_job_parameter(job_id, "MinMemoryNode") == "400M"
    assert int(atf.get_node_parameter(node, "alloc_memory")) >= 400

    atf.run_command(
        f"scontrol update JobId={job_id} MinMemoryNode=200",
        user=slurm_user,
        fatal=True,
    )

    for t in atf.timer():
        min_mem = atf.get_job_parameter(job_id, "MinMemoryNode", quiet=True)
        alloc_mem = int(atf.get_node_parameter(node, "alloc_memory"))
        logging.debug(f"MinMemoryNode={min_mem} and alloc_memory={alloc_mem}")
        if min_mem == "200M" and alloc_mem <= 200:
            break
    else:
        assert False, "MinMemoryNode and AllocMem should be reduced to 200 (or less)"

    result = atf.run_command(
        f"scontrol update JobId={job_id} MinMemoryNode=500",
        user=slurm_user,
    )
    assert result["exit_code"] != 0, "Increasing memory on a running job should fail"

    result = atf.run_command(
        f"scontrol update JobId={job_id} MinMemoryNode=0",
        user=slurm_user,
    )
    assert result["exit_code"] != 0, "Setting memory to 0 on a running job should fail"


def test_mem_update_auto_reduce_1node(use_memory_program):
    """Test automatic memory reduction via --mem-update on a single node."""

    job_id = atf.submit_job_sbatch(
        f"--mem=400 -N1 --mem-update=30@1 --wrap '{use_memory_program} 100 120'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    node = atf.get_job_parameter(job_id, "BatchHost")

    assert atf.get_job_parameter(job_id, "MinMemoryNode") == "400M"
    assert int(atf.get_node_parameter(node, "alloc_memory")) >= 400

    # --mem-update=30@1 triggers auto-shrink after 1 minute (minimum delay).
    # Allow an extra 60s for job startup, usage sampling, and RPC round-trips.
    for t in atf.timer(120, poll_interval=1, quiet=True):
        min_mem = int(
            atf.get_job_parameter(job_id, "MinMemoryNode", quiet=True).rstrip("M")
        )
        alloc_mem = int(atf.get_node_parameter(node, "alloc_memory"))

        logging.debug(
            f"Waiting for auto-update ({t}s remaining): MinMemoryNode={min_mem} and alloc_memory={alloc_mem}"
        )
        if 40 <= min_mem <= 250 and alloc_mem < 400:
            break
    else:
        assert (
            False
        ), f"Reduced memory {min_mem}M by auto-update should be between 40M and 250M and AllocMem ({alloc_mem}) on node should drop < 400 after auto-reduce"


def test_mem_update_auto_reduce_2nodes(use_memory_program):
    """Test automatic memory reduction via --mem-update on two nodes."""

    job_id = atf.submit_job_sbatch(
        f"-N2 --mem=400 --mem-update=30@1 --wrap 'srun {use_memory_program} 100 120'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    node_list = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert len(node_list) == 2

    assert atf.get_job_parameter(job_id, "MinMemoryNode") == "400M"
    for node in node_list:
        assert int(atf.get_node_parameter(node, "alloc_memory")) >= 400

    # --mem-update=30@1 triggers auto-shrink after 1 minute (minimum delay).
    # Allow an extra 60s for job startup, usage sampling, and RPC round-trips.
    for t in atf.timer(120, poll_interval=1, quiet=True):
        min_mem = int(
            atf.get_job_parameter(job_id, "MinMemoryNode", quiet=True).rstrip("M")
        )
        alloc_mems = []
        for node in node_list:
            alloc_mems.append(int(atf.get_node_parameter(node, "alloc_memory")))

        logging.debug(
            f"Waiting for auto-update ({t}s remaining): MinMemoryNode={min_mem} and 'alloc_memory of all nodes' = {alloc_mems}"
        )
        if 40 <= min_mem <= 250 and all(alloc_mem < 400 for alloc_mem in alloc_mems):
            break
    else:
        assert (
            False
        ), f"Reduced memory {min_mem}M by auto-update should be between 40M and 250M and AllocMem of all nodes ({alloc_mems}) should drop < 400 after auto-reduce"
