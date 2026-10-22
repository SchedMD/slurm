############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Retry a dependency-ready step after its allocation's reserved ports free."""

import re

import pytest

import atf

PORT_COUNT = 5


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/srun",
        reason="Issue 50938: reserved-port retries require real pending step IDs",
    )
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="step dependency reporting requires a 26.11 scontrol",
    )
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="step dependencies require a 26.11 stepmgr",
    )
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")

    ports = re.search(
        r"(?:^|,)ports=(\d+)-(\d+)(?:,|$)",
        atf.get_config_parameter("MpiParams", live=False) or "",
    )
    if not ports or int(ports[2]) - int(ports[1]) + 1 < PORT_COUNT:
        atf.require_tool("ss")
        ephemeral = atf.run_command_output(
            "cat /proc/sys/net/ipv4/ip_local_port_range", fatal=True
        )
        ephemeral_low, ephemeral_high = map(int, ephemeral.split())
        for low in (ephemeral_high + 100, ephemeral_low - 100 - PORT_COUNT):
            high = low + PORT_COUNT - 1
            if not (1024 <= low <= high <= 65535):
                continue
            occupied = atf.run_command_output(
                f'ss -tanH "sport >= :{low} and sport <= :{high}"', fatal=True
            )
            if not occupied.strip():
                atf.require_config_parameter_includes(
                    "MpiParams", ("ports", f"{low}-{high}")
                )
                break
        else:
            pytest.fail("no available reserved-port range outside ephemeral ports")
    atf.require_slurm_running()


def test_ports_busy_wake_retains_dependency_and_step_id():
    """A late ports-busy failure keeps the accepted step and its resolved edge."""
    atf.make_bash_script(
        "ports_busy.sh",
        f"""set -e
srun --mpi=none --exact -n1 --resv-ports=0 bash -c 'touch blocker_started; until [ -f blocker_release ]; do sleep 0.2; done' &
until [ -f blocker_started ]; do sleep 0.2; done
srun --mpi=none --exact -n1 --resv-ports={PORT_COUNT} bash -c 'touch holder_started; until [ -f holder_release ]; do sleep 0.2; done' &
until [ -f holder_started ]; do sleep 0.2; done
srun --mpi=none --exact -n1 --async --parsable --resv-ports=1 --step-dependency=afterok:$SLURM_JOB_ID.0 --output=target.out --error=target.err bash -c 'touch target_started; until [ -f target_release ]; do sleep 0.2; done; echo TARGET_DONE' > target_id
until scontrol -o show step "$SLURM_JOB_ID.2" | grep -q 'Reason=Dependency '; do sleep 0.2; done
touch blocker_release
while scontrol -o show step "$SLURM_JOB_ID.0" 2>/dev/null | grep -q 'StepId='; do sleep 0.2; done
# One CPU is free and the dependency resolved, but holder owns every job port.
until scontrol -o show step "$SLURM_JOB_ID.2" | grep -q 'Reason=Resources '; do sleep 0.2; done
scontrol -o show step "$SLURM_JOB_ID.2" > ports_busy_state
test ! -f target_started
touch holder_release
until [ -f target_started ]; do sleep 0.2; done
until scontrol -o show step "$SLURM_JOB_ID.2" | grep -q 'State=RUNNING'; do sleep 0.2; done
scontrol -o show step "$SLURM_JOB_ID.2" > ports_running_state
touch target_release
wait
swait --timeout=60
echo SCRIPT_DONE
""",
    )
    # The allocation owns exactly PORT_COUNT ports, independently of a larger
    # site-wide pool; holder exhausts only this job's stepmgr port table.
    job_id = atf.submit_job_sbatch(
        f"-N1 -n2 -t3 --resv-ports={PORT_COUNT} "
        "--output=ports_busy.out --error=ports_busy.out ports_busy.sh",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    atf.assert_file_contents("ports_busy.out", "SCRIPT_DONE", contains=True)
    atf.assert_file_contents("target_id", f"{job_id}.2")
    atf.assert_file_contents("ports_busy_state", f"StepId={job_id}.2 ", contains=True)
    atf.assert_file_contents("ports_busy_state", "State=PENDING", contains=True)
    atf.assert_file_contents("ports_busy_state", "Reason=Resources", contains=True)
    atf.assert_file_contents(
        "ports_running_state", f"StepId={job_id}.2 ", contains=True
    )
    atf.assert_file_contents("ports_running_state", "State=RUNNING", contains=True)
    atf.assert_file_contents(
        "ports_running_state", f"StepDependency=afterok:{job_id}.0", contains=True
    )
    atf.assert_file_contents("target.out", "TARGET_DONE", contains=True)
