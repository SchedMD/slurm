############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create custom gpu files and custom gres")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("GresTypes", "gpu")
    atf.require_nodes(1, [("Gres", "gpu:2 Sockets=2 CoresPerSocket=1")])
    # GPU's need to point to existing files
    gpu_file = f"{str(atf.module_tmp_path)}/gpu"
    atf.run_command(f"touch {gpu_file + '1'}")
    atf.run_command(f"touch {gpu_file + '2'}")
    atf.require_config_parameter(
        "NodeName", f"node1 Name=gpu Cores=0-1 File={gpu_file}[1-2]", source="gres"
    )
    atf.require_slurm_running()


def test_gpu_socket_sharing():
    """Test allocating multiple gpus on the same core group with enforce-binding"""

    output = atf.run_command_output(
        "srun --gres-flags=enforce-binding --ntasks-per-socket=1 \
                    --cpus-per-task=1 --ntasks-per-node=2 -N1 \
                    --gpus-per-task=1 scontrol show nodes node1 -d",
        timeout=2,
        fatal=True,
    )
    assert (
        re.search(r"GresUsed=gpu.*:2", output) is not None
    ), "Verify that job allocated 2 gpus"


def test_gpu_socket_sharing_no_alloc():
    """Test allocating multiple gpus on the same core group with enforce-binding without enough resources"""

    output = atf.run_command(
        "srun --gres-flags=enforce-binding --ntasks-per-socket=1 \
                    --cpus-per-task=2 --ntasks-per-node=2 -N1 \
                    --gpus-per-task=1 scontrol show nodes node1 -d",
        timeout=1,
        fatal=False,
    )
    assert output["exit_code"] != 0, "Verify that srun command failed"
    assert (
        re.search(
            r"srun: error: .+ Requested node configuration is not available",
            str(output["stderr"]),
        )
        is not None
    ), "Verify that job is rejected."
