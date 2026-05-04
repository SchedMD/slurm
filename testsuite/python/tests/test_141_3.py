############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
from pathlib import Path
import time

import atf
import pytest

suspend_timeout = 100
resume_timeout = 100
suspend_time = -1

slurmd_bin = atf.properties["slurm-sbin-dir"] + "/slurmd"

script_preamble = """
SCRIPT_DIR="$( cd -- "$( dirname -- "${{BASH_SOURCE[0]:-$0}}"; )" &> /dev/null && pwd 2> /dev/null; )";
exec &> >(tee -a $SCRIPT_DIR/{script_name}.log)
PS4='+ $(date "+%y-%m-%dT%H:%M:%S") ($SLURM_NODE_NAME)\011 '
echo $@
set -x
"""


def resume_ctld_script(path):
    content = f"""
SLURM_NODE_NAME=$1
{script_preamble.format(script_name=Path(path).stem)}
sleep 2 # wait for slurmctld to update node state
for node in $({atf.properties["slurm-bin-dir"]}/scontrol show hostname $SLURM_NODE_NAME); do
    sudo {slurmd_bin} -N $node -b
done
"""
    atf.make_bash_script(path, content)
    return path


def resume_slurmd_script(path):
    """This one won't work; you can't power up a node from slurmd"""
    content = f"""
SLURM_NODE_NAME=$1
{script_preamble.format(script_name=Path(path).stem)}
# This one won't work; you can't power up a node from slurmd
sudo {slurmd_bin} -N $SLURM_NODE_NAME -b
"""
    atf.make_bash_script(path, content)
    return path


def suspend_ctld_script(path):
    pidfile_template = atf.properties["slurm-run-dir"] + "/slurmd.$node.pid"
    content = f"""
SLURM_NODE_NAME=$1
{script_preamble.format(script_name=Path(path).stem)}
for node in $({atf.properties["slurm-bin-dir"]}/scontrol show hostname $SLURM_NODE_NAME); do
    sudo pkill -F {pidfile_template}
done
"""
    atf.make_bash_script(path, content)
    return path


def suspend_slurmd_script(path):
    pidfile_template = atf.properties["slurm-run-dir"] + "/slurmd.$SLURM_NODE_NAME.pid"
    content = f"""
SLURM_NODE_NAME=$1
{script_preamble.format(script_name=Path(path).stem)}
sudo pkill -F {pidfile_template}
"""
    atf.make_bash_script(path, content)
    return path


def reboot_ctld_script(path):
    pidfile_template = atf.properties["slurm-run-dir"] + "/slurmd.$node.pid"
    content = f"""
SLURM_NODE_NAME=$1
{script_preamble.format(script_name=Path(path).stem)}
for node in $({atf.properties["slurm-bin-dir"]}/scontrol show hostnames $SLURM_NODE_NAME); do
    sudo pkill -F {pidfile_template}
done
sleep 5
for node in $({atf.properties["slurm-bin-dir"]}/scontrol show hostname $SLURM_NODE_NAME); do
    sudo {slurmd_bin} -N $node -b
done
"""
    atf.make_bash_script(path, content)
    return path


def reboot_slurmd_script(path):
    pidfile_template = atf.properties["slurm-run-dir"] + "/slurmd.$SLURM_NODE_NAME.pid"
    content = f"""
{script_preamble.format(script_name=Path(path).stem)}
sudo pkill -F {pidfile_template}
sleep 5
sudo {slurmd_bin} -N $SLURM_NODE_NAME -b
"""
    atf.make_bash_script(path, content)
    return path


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        component="bin/scontrol",
        reason="Issue 50669: PowerAction option added in 26.05",
    )
    atf.get_run_dir_path()
    atf.require_config_parameter("ReturnToService", 2)
    atf.require_config_parameter("DebugFlags", "POWER")
    atf.require_nodes(2)
    atf.require_config_parameter("SuspendTime", suspend_time)
    atf.require_config_parameter("SuspendTimeout", suspend_timeout)
    atf.require_config_parameter("ResumeTimeout", resume_timeout)

    atf.require_config_parameter(
        "ResumeProgram", resume_ctld_script(f"{atf.module_tmp_path}/def_resume.sh")
    )
    atf.require_config_parameter(
        "SuspendProgram", suspend_ctld_script(f"{atf.module_tmp_path}/def_suspend.sh")
    )
    atf.require_config_parameter(
        "RebootProgram", reboot_slurmd_script(f"{atf.module_tmp_path}/def_reboot.sh")
    )

    atf.require_config_parameter(
        "PowerAction",
        {
            "resume-ctld": {
                "Location": "slurmctld",
                "Program": resume_ctld_script(f"{atf.module_tmp_path}/resume_ctld.sh"),
            },
            "suspend-ctld": {
                "Location": "slurmctld",
                "Program": suspend_ctld_script(
                    f"{atf.module_tmp_path}/suspend_ctld.sh"
                ),
            },
            "reboot-ctld": {
                "Location": "slurmctld",
                "Program": reboot_ctld_script(f"{atf.module_tmp_path}/reboot_ctld.sh"),
            },
            "resume-slurmd": {  # this one won't work; you can't power up a node from slurmd
                "Location": "slurmd",
                "Program": resume_slurmd_script(
                    f"{atf.module_tmp_path}/resume_slurmd.sh"
                ),
            },
            "suspend-slurmd": {
                "Location": "slurmd",
                "Program": suspend_slurmd_script(
                    f"{atf.module_tmp_path}/suspend_slurmd.sh"
                ),
            },
            "reboot-slurmd": {
                "Location": "slurmd",
                "Program": reboot_slurmd_script(
                    f"{atf.module_tmp_path}/reboot_slurmd.sh"
                ),
            },
        },
    )

    atf.require_slurm_running()

    yield

    atf.run_command(f"rm -f {atf.module_tmp_path}/*.sh")


@pytest.fixture(scope="function")
def current_nodes():
    nodes = atf.get_nodes(quiet=True)
    for node in nodes:
        assert "IDLE" in atf.get_node_parameter(node, "state"), f"{node} must be IDLE"
        assert "POWERED_DOWN" not in atf.get_node_parameter(
            node, "state"
        ), f"{node} must start powered up"
    return list(nodes.keys())


@pytest.fixture(scope="function", autouse=True)
def reset_nodes(current_nodes):
    yield
    atf.cancel_all_jobs()
    atf.restart_slurmctld(clean=True)
    for node in current_nodes:
        atf.start_slurmd(node, quiet=True)
    time.sleep(2)
    for node in current_nodes:
        atf.wait_for_node_state(node, "IDLE", fatal=True)
    atf.run_command(f"rm -f {atf.module_tmp_path}/*.log")


def test_reboot_action_invalid(current_nodes):
    nodelist = atf.node_list_to_range(current_nodes)
    res = atf.run_command(
        f"scontrol reboot {nodelist} action=invalid", xfail=True, user="slurm"
    )
    assert res["exit_code"] == 1, "Expected exit code 1"
    assert "Invalid power action" in res["stderr"], "expected error message"


def test_power_down_action_invalid(current_nodes):
    nodelist = atf.node_list_to_range(current_nodes)
    res = atf.run_command(
        f"scontrol power down {nodelist} action=invalid", xfail=True, user="slurm"
    )
    assert res["exit_code"] == 1, "Expected exit code 1"
    assert "Invalid power action" in res["stderr"], "expected error message"


def test_reboot_force(current_nodes):
    nodelist = atf.node_list_to_range(current_nodes)
    job_id = atf.submit_job_sbatch(
        f"-N {len(current_nodes)} --wrap 'srun sleep 100'", fatal=True
    )
    for node in current_nodes:
        atf.wait_for_node_state(node, "ALLOCATED", timeout=5, fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", timeout=5, fatal=True)

    atf.run_command(f"scontrol reboot {nodelist} force", fatal=True, user="slurm")
    atf.wait_for_job_state(job_id, "PENDING", timeout=5, fatal=True)
    for node in current_nodes:
        atf.wait_for_node_state_any(
            node, ["REBOOT_REQUESTED", "REBOOT_ISSUED"], timeout=10, fatal=True
        )
    for node in current_nodes:
        atf.wait_for_node_state(node, "REBOOT_ISSUED", fatal=True)
    for node in current_nodes:
        atf.wait_for_node_state(node, "IDLE", fatal=True)
    log_path = Path(f"{atf.module_tmp_path}/def_reboot.log")
    assert log_path.exists(), "Log file must exist"
    for node in current_nodes:
        assert f"{node}" in log_path.read_text(), f"Log file must contain {node}"


def test_reboot_action_slurmctld(current_nodes):
    nodelist = atf.node_list_to_range(current_nodes)
    atf.run_command(
        f"scontrol reboot {nodelist} action=reboot-ctld",
        fatal=True,
        user="slurm",
    )
    for node in current_nodes:
        atf.wait_for_node_state_any(
            node, ["REBOOT_REQUESTED", "REBOOT_ISSUED"], timeout=10, fatal=True
        )
    for node in current_nodes:
        atf.wait_for_node_state(node, "REBOOT_ISSUED", fatal=True)
    for node in current_nodes:
        atf.wait_for_node_state(node, "IDLE", fatal=True)
    log_path = Path(f"{atf.module_tmp_path}/reboot_ctld.log")
    assert log_path.exists(), "Log file must exist"
    assert f"{nodelist}" in log_path.read_text(), "Log file must contain the nodelist"


def test_reboot_asap(current_nodes):
    nodelist = atf.node_list_to_range(current_nodes)
    atf.run_command(
        f"scontrol reboot {nodelist} asap",
        fatal=True,
        user="slurm",
    )
    for node in current_nodes:
        atf.wait_for_node_state(node, "DRAIN", fatal=True)
    job_id = atf.submit_job_sbatch(
        f"-N {len(current_nodes)} --wrap 'srun sleep 100'", fatal=True
    )
    atf.wait_for_job_state(job_id, "PENDING", timeout=5, fatal=True)
    for node in current_nodes:
        atf.wait_for_node_state_any(
            node, ["REBOOT_REQUESTED", "REBOOT_ISSUED"], timeout=10, fatal=True
        )
    for node in current_nodes:
        atf.wait_for_node_state(node, "REBOOT_ISSUED", timeout=20, fatal=True)
    for node in current_nodes:
        atf.wait_for_node_state_any(node, ["IDLE", "ALLOCATED"], timeout=20, fatal=True)


def test_reboot_action_slurmd(current_nodes):
    nodelist = atf.node_list_to_range(current_nodes)
    atf.run_command(
        f"scontrol reboot {nodelist} action=reboot-slurmd",
        fatal=True,
        user="slurm",
    )
    for node in current_nodes:
        atf.wait_for_node_state(node, "REBOOT_REQUESTED", fatal=True)
    for node in current_nodes:
        atf.wait_for_node_state(node, "REBOOT_ISSUED", fatal=True)
    for node in current_nodes:
        atf.wait_for_node_state(node, "IDLE", fatal=True)
    log_path = Path(f"{atf.module_tmp_path}/reboot_slurmd.log")
    assert log_path.exists(), "Log file must exist"
    for node in current_nodes:
        assert f"{node}" in log_path.read_text(), f"Log file must contain {node}"


def test_power_down_up_action_slurmctld(current_nodes):
    nodelist = atf.node_list_to_range(current_nodes)
    atf.run_command(
        f"scontrol power down {nodelist} action=suspend-ctld", fatal=True, user="slurm"
    )
    for node in current_nodes:
        atf.wait_for_node_state(node, "POWERING_DOWN", fatal=True)
    atf.run_command(
        f"scontrol update nodename={nodelist} state=RESUME", fatal=True, user="slurm"
    )
    for node in current_nodes:
        atf.wait_for_node_state(
            node, "POWERED_DOWN", timeout=suspend_timeout + 5, fatal=True
        )
    for node in current_nodes:
        atf.wait_for_node_state(node, "IDLE", fatal=True)

    suspend_log_path = Path(f"{atf.module_tmp_path}/suspend_ctld.log")
    assert suspend_log_path.exists(), "Log file must exist"
    assert (
        f"{nodelist}" in suspend_log_path.read_text()
    ), "Log file must contain the nodelist"

    # powered down, ready for power up
    atf.run_command(
        f"scontrol power up {nodelist} action=resume-ctld", fatal=True, user="slurm"
    )
    for node in current_nodes:
        atf.wait_for_node_state(node, "POWERING_UP", fatal=True)
    for node in current_nodes:
        atf.wait_for_node_state(
            node, "POWERING_UP", fatal=True, reverse=True, timeout=resume_timeout + 5
        )

    resume_log_path = Path(f"{atf.module_tmp_path}/resume_ctld.log")
    assert resume_log_path.exists(), "Log file must exist"
    assert (
        f"{nodelist}" in resume_log_path.read_text()
    ), "Log file must contain the nodelist"


def test_power_down_up_action_slurmd(current_nodes):
    nodelist = atf.node_list_to_range(current_nodes)
    atf.run_command(
        f"scontrol power down {nodelist} action=suspend-slurmd",
        fatal=True,
        user="slurm",
    )
    for node in current_nodes:
        atf.wait_for_node_state(node, "POWERING_DOWN", fatal=True)
    atf.run_command(
        f"scontrol update nodename={nodelist} state=RESUME", fatal=True, user="slurm"
    )
    for node in current_nodes:
        atf.wait_for_node_state(
            node, "POWERED_DOWN", timeout=suspend_timeout + 5, fatal=True
        )

    suspend_log_path = Path(f"{atf.module_tmp_path}/suspend_slurmd.log")
    assert suspend_log_path.exists(), "Log file must exist"
    for node in current_nodes:
        assert (
            f"{node}" in suspend_log_path.read_text()
        ), f"Log file must contain {node}"

    # powered down, ready for power up
    # this one won't work; you can't power up a node from slurmd
    res = atf.run_command(
        f"scontrol power up {nodelist} action=resume-slurmd", xfail=True, user="slurm"
    )
    assert res["exit_code"] == 1, "Expected exit code 1"
    assert "Invalid power action" in res["stderr"], "expected error message"
