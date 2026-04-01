############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import atf
import pytest
import re


namespace_yaml = """
---
defaults:
  auto_base_path: true
  base_path: "/tmp/test_159_1_ns_%n"
  dirs: "/var/tmp,/dev/shm"
  clone_ns_flags:
    - "CLONE_NEWPID"
    - "CLONE_NEWUSER"
..."""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version((25, 11), component="sbin/slurmd")
    atf.require_auto_config("Wants to configure namespace/linux plugin")
    atf.require_config_parameter("NamespaceType", "namespace/linux")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_config_file("namespace.yaml", namespace_yaml)
    atf.require_slurm_running()


def test_dirs_private_mount():
    """dirs creates a private /var/tmp for the job isolated from the host"""
    atf.run_command("touch /var/tmp/file_on_host", fatal=True)

    atf.make_bash_script(
        "job.sh",
        """\
if [[ -f /var/tmp/file_on_host ]]; then
    echo "private mount FAILED: host file visible in job"
else
    echo "private mount OK: host file not visible in job"
fi
""",
    )

    job_id = atf.submit_job_sbatch("--output=output job.sh", fatal=True)
    assert atf.wait_for_job_state(job_id, "COMPLETED"), f"Job {job_id} did not complete"
    atf.wait_for_file("output", timeout=5, fatal=True)

    with open("output") as f:
        content = f.read()

    atf.run_command("rm -f /var/tmp/file_on_host", fatal=True)

    assert (
        "private mount OK" in content
    ), "Host /var/tmp file was visible inside job — /var/tmp is not private"


def test_dirs_dev_shm_is_private():
    """/dev/shm in dirs gives the job a private /dev/shm isolated from the host"""
    atf.run_command("touch /dev/shm/file_on_host", fatal=True)

    atf.make_bash_script(
        "job.sh",
        """\
if [[ -f /dev/shm/file_on_host ]]; then
    echo "private mount FAILED: host file visible in job"
else
    echo "private mount OK: host file not visible in job"
fi
""",
    )

    job_id = atf.submit_job_sbatch("--output=output job.sh", fatal=True)
    assert atf.wait_for_job_state(job_id, "COMPLETED"), f"Job {job_id} did not complete"
    atf.wait_for_file("output", timeout=5, fatal=True)

    with open("output") as f:
        content = f.read()

    atf.run_command("rm -f /dev/shm/file_on_host", fatal=True)

    assert (
        "private mount OK" in content
    ), "Host /dev/shm file was visible inside job — /dev/shm is not private"


def test_dirs_dev_shm_is_tmpfs():
    """/dev/shm in dirs always receives a fresh tmpfs mount"""
    output = atf.run_job_output(
        "findmnt --noheadings -o FSTYPE /dev/shm",
        fatal=True,
    )
    assert re.search(r"\btmpfs\b", output), "/dev/shm should always be a tmpfs mount"


def test_clone_newpid():
    """clone_ns_flags=CLONE_NEWPID puts the job in a private PID namespace"""
    host_pid_ns = atf.run_command_output(
        "readlink /proc/self/ns/pid",
        fatal=True,
    ).strip()
    job_pid_ns = atf.run_job_output(
        "readlink /proc/self/ns/pid",
        fatal=True,
    ).strip()
    assert (
        host_pid_ns != job_pid_ns
    ), "Job does not appear to be running in a private PID namespace"


def test_clone_newuser():
    """clone_ns_flags=CLONE_NEWUSER puts the job in a private user namespace"""
    host_user_ns = atf.run_command_output(
        "readlink /proc/self/ns/user",
        fatal=True,
    ).strip()
    job_user_ns = atf.run_job_output(
        "readlink /proc/self/ns/user",
        fatal=True,
    ).strip()
    assert (
        host_user_ns != job_user_ns
    ), "Job does not appear to be running in a private user namespace"
