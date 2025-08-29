############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


port_range = 9
srun_port_lower = 60000
srun_port_upper = srun_port_lower + port_range - 1  # 60008 inclusive


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to edit SrunPortRange and create a node")
    atf.require_config_parameter(
        "SrunPortRange", f"{srun_port_lower}-{srun_port_upper}"
    )
    atf.require_nodes(145, [("CPUs", 2), ("RealMemory", 2)])
    atf.require_slurm_running()


@pytest.mark.parametrize("nodes", [1, 10, 48, 49, 96, 100, 144])
def test_srun_ports_in_range(nodes):
    """Test srun uses the right SrunPortRange"""

    command = '''bash -c "
        task_id=\\$(scontrol show step \\$SLURM_JOBID.\\$SLURM_STEPID \
        | grep SrunHost | awk -F: '{print \\$3}')
        lsof -P -p \\$task_id 2>/dev/null | grep LISTEN | awk '{print \\$9}' \
        | awk -F: '{print \\$2}'"'''
    output = atf.run_job_output(f"-N{nodes} {command}", timeout=120).split("\n")
    count = 0
    for port_string in output:
        # Ignore blank lines
        if len(port_string) < 4:
            continue
        count += 1
        port_int = int(port_string)
        assert (
            port_int >= srun_port_lower and port_int <= srun_port_upper
        ), f"Port {port_int} is not in range {srun_port_lower}-{srun_port_upper}"

    # From the docs:
    # "A single srun opens 4 listening ports plus 2 more for every 48 hosts
    # beyond the first 48."
    ports = nodes * (4 + 2 * ((nodes - 1) // 48))
    assert count == ports, f"srun with -N{nodes} should use {ports} ports, not {count}"


@pytest.mark.parametrize("nodes", [145])
def test_srun_ports_out_of_range(nodes):
    """Test sruns with too many nodes, so with not enough SrunPortRange"""

    result = atf.run_job_error(f"-t1 -N{nodes} sleep 1", fatal=True, xfail=True)

    regex = rf"all ports in range .{srun_port_lower}, {srun_port_upper}. exhausted"
    assert (
        re.search(regex, result) is not None
    ), "srun's stderr should contain the 'all ports in range exhausted' message"


def test_out_of_srun_ports():
    """Test exhausted ports"""

    job_id1 = atf.submit_job_sbatch('-N1 -o/dev/null --wrap="srun sleep 30"')
    job_id2 = atf.submit_job_sbatch('-N1 -o/dev/null --wrap="srun sleep 30"')

    atf.wait_for_step(job_id1, 0, fatal=True)
    atf.wait_for_step(job_id2, 0, fatal=True)

    result = atf.run_job_error("-t1 -N1 sleep 1", fatal=True, xfail=True)

    regex = rf"all ports in range .{srun_port_lower}, {srun_port_upper}. exhausted"
    assert (
        re.search(regex, result) is not None
    ), "srun's stderr should contain the 'all ports in range exhausted' message"
