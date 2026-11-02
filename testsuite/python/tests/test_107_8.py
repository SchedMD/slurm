############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scancel --full signals the batch script and the steps on every node."""

import pytest

import atf

pytestmark = pytest.mark.slow


def setup_module():
    atf.require_nodes(2, [("CPUs", 2)])
    atf.require_slurm_running()


def submit_heartbeat_job():
    """Submit a 2-node job whose batch script and step tasks update a
    heartbeat file every 0.2 seconds. Returns (job_id, heartbeat_files).
    """
    atf.run_command("rm -f hb_*", fatal=True)
    atf.make_bash_script(
        "heartbeat.sh",
        """
srun --mpi=none -N2 --ntasks-per-node=1 bash -c 'while true; do date +%s%N > hb_$SLURMD_NODENAME; sleep 0.2; done' &
while true; do date +%s%N > hb_batch; sleep 0.2; done
""",
    )
    job_id = atf.submit_job_sbatch(
        "-J test_107_8 -N2 -t2 -o /dev/null heartbeat.sh", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    nodes = atf.node_range_to_list(
        atf.get_job_parameter(job_id, "NodeList", fatal=True)
    )
    hb_files = ["hb_batch"] + [f"hb_{node}" for node in nodes]
    for hb_file in hb_files:
        atf.wait_for_file(hb_file, fatal=True)

    return job_id, hb_files


def assert_beating(hb_files, beating):
    """Assert that each heartbeat file is (or is not) updated within a
    poll interval longer than the heartbeat period.
    """
    for hb_file in hb_files:
        last = None
        for _ in atf.timer(poll_interval=1):
            current = atf.run_command_output(f"cat {hb_file}", fatal=True)
            if (last is not None) and ((current != last) == beating):
                break
            last = current
        else:
            pytest.fail(f"{hb_file} should {'' if beating else 'not '}be updated")


@pytest.mark.skipif(
    atf.get_version("sbin/slurmd") < (26, 11),
    reason="Issue 51356: scancel --full only sends SIGSTOP/SIGCONT to the steps since 26.11",
)
@pytest.mark.parametrize("flags", ["--full", "--batch --full"])
def test_stop_cont(flags):
    """Verify scancel --full stops and resumes the batch script and the steps
    on every node, also when --batch is given.
    """
    job_id, hb_files = submit_heartbeat_job()

    atf.run_command(f"scancel {flags} --signal=STOP {job_id}", fatal=True)
    assert_beating(hb_files, False)

    atf.run_command(f"scancel {flags} --signal=CONT {job_id}", fatal=True)
    assert_beating(hb_files, True)


@pytest.mark.parametrize(
    "flags",
    [
        "--full",
        pytest.param(
            "--batch --full",
            marks=pytest.mark.skipif(
                atf.get_version("sbin/slurmctld") < (26, 11),
                reason="Issue 51356: --batch is only ignored with --full since 26.11",
            ),
        ),
    ],
)
def test_kill(flags):
    """Verify scancel --full --signal=KILL cancels the job, also when --batch
    is given.
    """
    job_id, _ = submit_heartbeat_job()

    atf.run_command(f"scancel {flags} --signal=KILL {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "CANCELLED", fatal=True)
