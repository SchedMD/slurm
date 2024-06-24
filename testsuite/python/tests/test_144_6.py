############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to add custom gres")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("GresTypes", "r1,r2")
    atf.require_nodes(
        1, [("Gres", "r1:no_consume:1,r2:1"), ("CPUs", 2), ("RealMemory", 2)]
    )
    atf.require_slurm_running()


def test_no_consume():
    """Test gres with no_consume"""

    # Get the last node only
    no_consume_output = atf.run_command_output(
        "srun --gres=r1 scontrol show node -d"
    ).split("NodeName")[-1]
    assert (
        re.search(r"GresUsed=.*r1:0", no_consume_output) is not None
    ), "Expect no_consume resources to not be consumed"
    consume_output = atf.run_command_output(
        "srun --gres=r2 scontrol show node -d"
    ).split("NodeName")[-1]
    assert (
        re.search(r"GresUsed=.*r2:1", consume_output) is not None
    ), "Expect consumable resource to be consumed"


def test_no_consume_parallel():
    """Test no_consume gres with parallel jobs"""

    job_id_1 = atf.submit_job_sbatch('--gres=r1 --mem=1 --wrap="sleep 20"')
    job_id_2 = atf.submit_job_sbatch('--gres=r1 --mem=1 --wrap="sleep 20"')
    atf.wait_for_job_state(job_id_1, "RUNNING")
    atf.wait_for_job_state(job_id_2, "RUNNING")
    squeue = atf.run_command_output("squeue")
    assert (
        re.search(rf"{job_id_1}( +[^ ]+)( +[^ ]+)( +[^ ]+) +R", squeue) is not None
    ), "Expect first job to be running"
    assert (
        re.search(rf"{job_id_2}( +[^ ]+)( +[^ ]+)( +[^ ]+) +R", squeue) is not None
    ), "Expect second job to be running"
