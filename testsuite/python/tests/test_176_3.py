############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test batched DBD_SEND_MULT_MSG delivery after slurmdbd downtime."""

import pathlib

import pytest

import atf

JOB_COUNT = 3


@pytest.fixture(scope="module", autouse=True)
def setup(dbd_log_dir):
    """Log slurmdbd with Protocol flags before it starts."""
    atf.set_config_parameter(
        "LogFile", str(dbd_log_dir / "slurmdbd.log"), source="slurmdbd"
    )
    atf.require_config_parameter_includes("DebugFlags", "Protocol", source="slurmdbd")
    atf.require_accounting()
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def dbd_log_dir():
    """A slurmdbd-writable log directory (slurmdbd writes as SlurmUser)."""
    path = pathlib.Path("dbd_log").resolve()
    path.mkdir(exist_ok=True)
    path.chmod(0o777)
    yield path


@pytest.fixture()
def dbd_stopped():
    """Stop slurmdbd for the test and leave it running afterwards."""
    atf.stop_slurmdbd(quiet=True)
    yield
    atf.start_slurmdbd(quiet=True)


def test_batched_mult_msg(dbd_stopped, dbd_log_dir):
    """Messages queued while slurmdbd is down flush as one DBD_SEND_MULT_MSG.

    The slurmdbd agent batches a backlog into DBD_SEND_MULT_MSG. Complete
    several jobs with slurmdbd down, then check that every job is recorded
    after reconnect and that slurmdbd logged the batched RPC.
    """
    job_ids = []
    for _ in range(JOB_COUNT):
        job_ids.append(atf.submit_job_sbatch("--wrap='true'", fatal=True))
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "DONE", fatal=True)

    # Bring slurmdbd back; the agent flushes its backlog through it.
    atf.start_slurmdbd(quiet=True)

    def _accounted_jobs():
        output = atf.run_command_output(
            f"sacct -n -X -j {','.join(map(str, job_ids))} --format=jobid%10"
        )
        return {int(line) for line in output.split()} if output else set()

    for _ in atf.timer():
        if _accounted_jobs() == set(job_ids):
            break
    else:
        assert False, (
            f"jobs {set(job_ids) - _accounted_jobs()} missing from "
            "accounting after slurmdbd reconnect"
        )

    assert atf.run_command_output(
        f"grep -F 'received opcode DBD_SEND_MULT_MSG ' {dbd_log_dir / 'slurmdbd.log'}",
        user=atf.properties["slurm-user"],
    ), "slurmdbd did not receive a batched DBD_SEND_MULT_MSG"
