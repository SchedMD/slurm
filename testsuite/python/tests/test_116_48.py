############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import logging
import re
import time

import pytest

import atf

port_range_size = 9


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup(safe_port_range):
    lo, hi = safe_port_range
    atf.require_auto_config("wants to edit SrunPortRange and create a node")
    atf.require_config_parameter("SrunPortRange", f"{lo}-{hi}")
    atf.require_nodes(145, [("CPUs", 2), ("RealMemory", 2)])
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def safe_port_range():
    """Returns (lo, hi) for a free port range outside ip_local_port_range.

    Placing the range outside the ephemeral range and ensuring that none is
    blocked avoids future interferences and ensures current availability.
    """
    range_str = atf.run_command_output(
        "cat /proc/sys/net/ipv4/ip_local_port_range", fatal=True, quiet=True
    )
    ephem_lo, ephem_hi = (int(x) for x in range_str.split())

    # Prefer just above the ephemeral range; fall back to just below.
    candidates = (ephem_hi + 100, ephem_lo - 100 - port_range_size)
    for lo in candidates:
        hi = lo + port_range_size - 1
        if not (1024 <= lo and hi <= 65535 and (hi < ephem_lo or lo > ephem_hi)):
            continue
        if is_port_range_available(lo, hi):
            return lo, hi
        logging.debug(f"[PORT_PICK] Skipping candidate {lo}-{hi}: already blocked")

    pytest.fail(
        f"Cannot find a {port_range_size}-port range outside ephemeral ({ephem_lo}-{ephem_hi}) with no active binders"
    )


def is_port_range_available(lo, hi):
    """Return True if every port in [lo, hi] is available/bindable.

    Any TCP socket on a local port in the range whose state is NOT TIME-WAIT
    will make srun's bind() fail with EADDRINUSE, even though srun sets
    SO_REUSEADDR.
    """
    logging.debug(f"[PORT_MONITOR] Checking ports {lo}-{hi}")

    ss_result = atf.run_command(
        f'ss -tanH "sport >= :{lo} and sport <= :{hi}"',
        timeout=10,
        quiet=True,
    )
    if ss_result["exit_code"] != 0:
        logging.debug(f"[PORT_MONITOR] ss command failed: {ss_result['stderr']}")
        return False

    # ss -tanH columns: State Recv-Q Send-Q Local-Addr:Port Peer-Addr:Port
    for line in ss_result["stdout"].splitlines():
        line = line.strip()
        if not line:
            continue
        state = line.split()[0]
        if state == "TIME-WAIT":
            continue
        logging.debug(f"[PORT_MONITOR] Blocking socket ({state}): {line}")
        return False

    return True


def wait_for_srun_ports_clear(lo, hi, timeout=90):
    """Wait until all ports in [lo, hi] are released.

    Timeout defaults to 90s so FIN-WAIT-2 sockets from a prior srun can
    drain (bounded by net.ipv4.tcp_fin_timeout, default 60s).
    """
    logging.debug(
        f"[PORT_WAIT] Waiting for ports {lo}-{hi} to be released (timeout: {timeout}s)"
    )
    start_time = time.time()

    while True:
        elapsed = time.time() - start_time
        if is_port_range_available(lo, hi):
            logging.debug(f"[PORT_WAIT] All ports available after {elapsed:.1f}s")
            return True

        if elapsed >= timeout:
            logging.debug(
                f"[PORT_WAIT] TIMEOUT after {elapsed:.1f}s - some port not yet available"
            )
            return False

        if elapsed > 5 and int(elapsed) % 10 == 0:
            logging.debug(
                f"[PORT_WAIT] Still waiting... some ports not yet available (elapsed: {elapsed:.1f}s)"
            )

        time.sleep(1)


@pytest.mark.parametrize("nodes", [1, 10, 48, 49, 96, 100, 144])
def test_srun_ports_in_range(nodes, safe_port_range):
    """Test srun uses the right SrunPortRange"""

    lo, hi = safe_port_range

    # Wait for ports to be open before running test
    wait_for_srun_ports_clear(lo, hi)

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
        assert lo <= port_int <= hi, f"Port {port_int} is not in range {lo}-{hi}"

    # From the docs:
    # "A single srun opens 4 listening ports plus 2 more for every 48 hosts
    # beyond the first 48."
    ports = nodes * (4 + 2 * ((nodes - 1) // 48))
    assert count == ports, f"srun with -N{nodes} should use {ports} ports, not {count}"


@pytest.mark.parametrize("nodes", [145])
def test_srun_ports_out_of_range(nodes, safe_port_range):
    """Test sruns with too many nodes, so with not enough SrunPortRange"""

    lo, hi = safe_port_range

    # Wait for ports to be open before running test
    wait_for_srun_ports_clear(lo, hi)

    result = atf.run_job_error(f"-t1 -N{nodes} sleep 1", fatal=True, xfail=True)

    regex = rf"all ports in range .{lo}, {hi}. exhausted"
    assert (
        re.search(regex, result) is not None
    ), "srun's stderr should contain the 'all ports in range exhausted' message"


def test_out_of_srun_ports(safe_port_range):
    """Test exhausted ports"""

    lo, hi = safe_port_range

    # Wait for ports to be open before running test
    wait_for_srun_ports_clear(lo, hi)

    job_id1 = atf.submit_job_sbatch('-N1 -o/dev/null --wrap="srun sleep 30"')
    job_id2 = atf.submit_job_sbatch('-N1 -o/dev/null --wrap="srun sleep 30"')

    atf.wait_for_step(job_id1, 0, fatal=True)
    atf.wait_for_step(job_id2, 0, fatal=True)

    result = atf.run_job_error("-t1 -N1 sleep 1", fatal=True, xfail=True)

    regex = rf"all ports in range .{lo}, {hi}. exhausted"
    assert (
        re.search(regex, result) is not None
    ), "srun's stderr should contain the 'all ports in range exhausted' message"
