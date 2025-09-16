############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import logging
import pytest
import re
import time


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

    command = """bash -c '
        echo "[DEBUG] Starting port check" >&2
        echo "[DEBUG] Environment: SLURM_JOBID=$SLURM_JOBID SLURM_STEPID=$SLURM_STEPID" >&2
        echo "[DEBUG] Hostname: $(hostname)" >&2
        echo "[DEBUG] PID: $$" >&2

        echo "[DEBUG] Running scontrol show step..." >&2
        step_info=$(scontrol show step $SLURM_JOBID.$SLURM_STEPID 2>&1)
        scontrol_exit_code=$?
        echo "[DEBUG] scontrol exit code: $scontrol_exit_code" >&2
        echo "[DEBUG] scontrol output length: ${#step_info}" >&2
        echo "[DEBUG] scontrol output: $step_info" >&2

        if [[ $scontrol_exit_code -ne 0 ]]; then
            echo "[ERROR] scontrol failed with exit code $scontrol_exit_code" >&2
            exit 1
        fi

        echo "[DEBUG] Extracting SrunHost info..." >&2
        srun_host_line=$(echo "$step_info" | grep SrunHost)
        echo "[DEBUG] SrunHost line: $srun_host_line" >&2

        if [[ -z "$srun_host_line" ]]; then
            echo "[ERROR] No SrunHost found in step info" >&2
            exit 2
        fi

        task_id=$(echo "$srun_host_line" | awk -F: '"'"'{print $3}'"'"')
        echo "[DEBUG] Extracted task_id: $task_id" >&2

        if [[ -z "$task_id" ]]; then
            echo "[ERROR] Could not extract task_id" >&2
            exit 3
        fi

        echo "[DEBUG] Checking if process $task_id exists..." >&2
        if ! ps -p $task_id > /dev/null 2>&1; then
            echo "[ERROR] Process $task_id does not exist" >&2
            echo "[DEBUG] Current srun processes:" >&2
            ps aux | grep srun | grep -v grep >&2
            exit 4
        fi

        echo "[DEBUG] Running ss to find listening ports for PID $task_id..." >&2
        ss_output=$(ss -tlnp 2>&1)
        ss_exit_code=$?
        echo "[DEBUG] ss exit code: $ss_exit_code" >&2

        if [[ $ss_exit_code -ne 0 ]]; then
            echo "[ERROR] ss failed with exit code $ss_exit_code" >&2
            echo "[DEBUG] ss error output: $ss_output" >&2
            exit 5
        fi

        echo "[DEBUG] Filtering ss output for PID $task_id..." >&2
        filtered_output=$(echo "$ss_output" | grep "pid=$task_id,")
        echo "[DEBUG] Filtered output: $filtered_output" >&2

        echo "[DEBUG] Using filtered output as listen ports..." >&2
        listen_ports="$filtered_output"
        echo "[DEBUG] LISTEN lines: $listen_ports" >&2

        echo "[DEBUG] Extracting port numbers..." >&2
        port_numbers=$(echo "$listen_ports" | awk '"'"'{print $4}'"'"' | awk -F: '"'"'{print $NF}'"'"')
        echo "[DEBUG] Port numbers: $port_numbers" >&2

        echo "[DEBUG] Final output:" >&2
        echo "$port_numbers"

        echo "[DEBUG] Completed" >&2
    \'    """

    start_time = time.time()

    # Log existing srun processes before test
    ps_result = atf.run_command("ps aux | grep srun | grep -v grep")
    # Logging for Ticket 19089
    logging.debug(f"[TEST] Existing srun processes before test: {ps_result['stdout']}")

    result = atf.run_job(f"-N{nodes} {command}", timeout=180)
    end_time = time.time()

    # Logging for Ticket 19089
    logging.debug(f"[TEST] Command took {end_time - start_time:.3f} seconds")

    output = result["stdout"].split("\n")
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
