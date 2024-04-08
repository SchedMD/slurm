############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create custom gres and resource file")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("AccountingStorageType", "accounting_storage/slurmdbd")
    atf.require_config_parameter("GresTypes", "r1,r2")
    atf.require_nodes(1, [("Gres", "r1:1,r2:a:1"), ("CPUs", "2")])
    resource_file = f"{atf.module_tmp_path}/resource1"
    atf.run_command(f"touch {resource_file}")
    atf.require_config_parameter(
        "Name", {"r1": {"File": resource_file}, "r2": {"Type": "a"}}, source="gres"
    )
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def cleanup_test():
    yield
    atf.cancel_all_jobs(quiet=True)


def test_gres_alloc_dealloc_file():
    """Test alloc/dealloc of gres when file is set, but not type"""

    alloc = atf.run_command_output(
        "salloc --gres=r1 scontrol show nodes node1 -d | grep GresUsed", fatal=True
    )
    assert "r1:1" in alloc, "Expect allocation of gres with file set"
    dealloc = atf.run_command_output("scontrol show nodes node1 -d | grep GresUsed")
    assert "r1:0" in dealloc, "Expect deallocation of gres with file set"


def test_gres_alloc_dealloc_type():
    """Test alloc/dealloc of gres when type is set, but not file"""

    alloc = atf.run_command_output(
        "salloc --gres=r2:a:1 scontrol show nodes node1 -d | grep GresUsed", fatal=True
    )
    assert "r2:a:1" in alloc, "Expect allocation of gres with type set"
    dealloc = atf.run_command_output("scontrol show nodes node1 -d | grep GresUsed")
    assert "r2:a:0" in dealloc, "Expect deallocation of gres with type set"


def test_gres_overlap():
    """Test gres without file and --overlap"""

    output_file = f"{atf.module_tmp_path}/out"
    job_id = atf.submit_job_sbatch(
        f"-wnode1 -N1 --gres=r2:1 \
            --output={output_file} --wrap='\
            srun --overlap --gres=r2:1 sleep 60 &\
            srun --overlap --gres=r2:1 sleep 60 &\
            wait'",
        fatal=True,
    )
    assert atf.repeat_until(
        lambda: None,
        lambda out: atf.get_step_parameter(f"{job_id}.0", "State") == "RUNNING"
        and atf.get_step_parameter(f"{job_id}.1", "State") == "RUNNING",
    ), "Expect all steps to run in parallel"
