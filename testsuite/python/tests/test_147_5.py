############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify a process is only adopted into the extern step once it is set up.

pam_slurm_adopt can reach the extern step as soon as its slurmstepd has a
socket, which is before the step has run its SPANK init and the task plugins
have set it up. A process added in that window reaches task_p_add_pid() before
there is anything to add it to, so slurmstepd has to hold the request until the
step is running.

The generic SPANK plugin holds the extern step in slurm_spank_init() until
the test releases it, and a small client sends the same request
pam_slurm_adopt does.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup(spank_plugin):
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 50728: adoption waits for the extern step setup in 26.11",
    )
    # The client must run as root, like pam_slurm_adopt.
    atf.require_sudo_rights()
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    # Only proctrack/cgroup tracks an adopted pid, as pam_slurm_adopt needs.
    atf.require_config_parameter("ProctrackType", "proctrack/cgroup")

    atf.require_config_parameter(
        "required", spank_plugin, delimiter=" ", source="plugstack"
    )
    atf.require_slurm_running()


@pytest.fixture
def held_job(spank_tmp):
    """Submit a job whose extern step is held in slurm_spank_init()."""

    # The plugin holds the extern step of every job while this file exists.
    atf.run_command(f"touch {spank_tmp}/hold_extern", fatal=True)

    # Still running when the test checks the adopted pid: the end of the job
    # kills everything in its extern step. conftest cancels it afterwards.
    job_id = atf.submit_job_sbatch(
        "-N1 -t2 -o /dev/null --wrap 'sleep infinity'", fatal=True
    )
    yield job_id
    atf.run_command(f"touch {spank_tmp}/release.{job_id}")


@pytest.fixture
def target_pid():
    """Start a process for the extern step to adopt."""

    # The pid of the shell, which becomes sleep: the Popen pid is sudo's when
    # run_command() runs it as the test user.
    process = atf.run_command("echo $$; exec sleep infinity", background=True)[
        "process"
    ]
    yield int(process.stdout.readline())
    # sudo passes SIGTERM on, SIGKILL would leave sleep behind.
    if process.poll() is None:
        process.terminate()
    process.wait()


def test_adopt_waits_for_extern_step_setup(spank_tmp, adopt_pid, held_job, target_pid):
    """Adopting a pid waits until the extern step has been set up."""

    job_id = held_job
    assert atf.wait_for_file(
        f"{spank_tmp}/blocked.{job_id}"
    ), f"The extern step of job {job_id} never reached slurm_spank_init()"
    node = atf.get_job_parameter(job_id, "NodeList", fatal=True)

    request = atf.run_command(
        f"{adopt_pid} {job_id} {node} {target_pid}",
        user="root",
        background=True,
    )["process"]

    # A window to watch, not a wait for success: while the step is held the
    # request must not be answered. Kept short, as slurmd is waiting on the
    # held step too.
    for _ in atf.timer(timeout=3, xfail=True, quiet=True):
        if request.poll() is not None:
            pytest.fail(
                f"Adopting returned during the extern step setup: {request.stdout.read()}"
            )

    atf.run_command(f"touch {spank_tmp}/release.{job_id}", fatal=True)
    output, _ = request.communicate(timeout=60)
    assert output.startswith("RC=0"), f"Adopting failed once released: {output}"

    pids = atf.run_command_output(
        f"scontrol listpids {job_id} {node}", user="root", fatal=True
    )
    assert re.search(
        rf"^{target_pid}\s+{job_id}\s", pids, re.MULTILINE
    ), f"Process {target_pid} is not in job {job_id}:\n{pids}"
