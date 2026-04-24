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
  base_path: "/tmp/test_159_2_ns_%n"
  dir_confs:
    - path: "/var/tmp"
      base_path: "/tmp/test_159_2_per_dir_ns"
      options: "noexec"
    - path: "/home/%u/test_159_2_ns"
    - path: "/dev/shm"
      tmpfs: true
      options: "size=64m"
..."""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version((26, 5), component="sbin/slurmd")
    atf.require_auto_config("Wants to configure namespace/linux plugin with dir_confs")
    atf.require_config_parameter("NamespaceType", "namespace/linux")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_config_file("namespace.yaml", namespace_yaml)
    username = atf.run_command_output("whoami", fatal=True).strip()
    atf.run_command(f"mkdir -p /home/{username}/test_159_2_ns", fatal=True)
    atf.require_slurm_running()
    yield
    cleanup()


def cleanup():
    username = atf.run_command_output("whoami", fatal=False).strip()
    atf.run_command(f"rmdir /home/{username}/test_159_2_ns", fatal=False)


def test_bind_mount_is_private():
    """dir_confs bind mount gives the job a private /var/tmp isolated from the host"""
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

    atf.run_command("rm -f output /var/tmp/file_on_host", fatal=True)

    assert (
        "private mount OK" in content
    ), "Host /var/tmp file was visible inside job — /var/tmp is not private"


def test_bind_mount_noexec():
    """noexec in dir_confs options prevents execution from the bind-mounted /var/tmp"""
    atf.make_bash_script(
        "job.sh",
        """\
cp "$0" /var/tmp/test_exec.sh
chmod +x /var/tmp/test_exec.sh
/var/tmp/test_exec.sh
echo "exit_code:$?"
""",
    )

    job_id = atf.submit_job_sbatch("--output=output job.sh", fatal=True)
    assert atf.wait_for_job_state(job_id, "COMPLETED"), f"Job {job_id} did not complete"
    atf.wait_for_file("output", timeout=5, fatal=True)

    with open("output") as f:
        content = f.read()

    atf.run_command("rm -f output", fatal=True)

    assert re.search(r"exit_code:126", content) or re.search(
        r"[Pp]ermission denied", content
    ), "Execution from noexec /var/tmp should be denied (expected exit code 126)"


def test_tmpfs_mount_type():
    """dir_confs with tmpfs:true mounts a fresh tmpfs at /dev/shm"""
    output = atf.run_job_output(
        "findmnt --noheadings -o FSTYPE /dev/shm",
        fatal=True,
    )
    assert re.search(r"\btmpfs\b", output), "/dev/shm should be a tmpfs mount"


def test_tmpfs_is_private():
    """dir_confs tmpfs mount gives the job a private /dev/shm isolated from the host"""
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

    atf.run_command("rm -f output /dev/shm/file_on_host", fatal=True)

    assert (
        "private mount OK" in content
    ), "Host /dev/shm file was visible inside job — /dev/shm is not private"


def test_tmpfs_default_nosuid_nodev():
    """tmpfs mounts always have nosuid and nodev applied regardless of options"""
    output = atf.run_job_output(
        "findmnt --noheadings -o OPTIONS /dev/shm",
        fatal=True,
    )
    assert re.search(
        r"\bnosuid\b", output
    ), "tmpfs mount should always have nosuid applied"
    assert re.search(
        r"\bnodev\b", output
    ), "tmpfs mount should always have nodev applied"


def test_tmpfs_size_option():
    """size= in dir_confs options is enforced on a tmpfs mount"""
    atf.make_bash_script(
        "job.sh",
        """\
# Writing within the limit should succeed
dd if=/dev/zero of=/dev/shm/small bs=1M count=32 2>/dev/null
echo "small_write_exit:$?"

# Writing beyond the 64m limit should fail
dd if=/dev/zero of=/dev/shm/large bs=1M count=65 2>&1
echo "large_write_exit:$?"
""",
    )

    job_id = atf.submit_job_sbatch("--output=output job.sh", fatal=True)
    assert atf.wait_for_job_state(job_id, "COMPLETED"), f"Job {job_id} did not complete"
    atf.wait_for_file("output", timeout=10, fatal=True)

    with open("output") as f:
        content = f.read()

    atf.run_command("rm -f output", fatal=True)

    assert re.search(
        r"small_write_exit:0", content
    ), "Writing within size=64m limit should succeed"
    assert re.search(
        r"[Nn]o space left", content
    ), "Writing beyond size=64m should fail with 'No space left'"
    assert re.search(
        r"large_write_exit:1", content
    ), "dd should exit non-zero when no space left"


def test_per_dir_base_path():
    """per-dir base_path in dir_confs overrides the global base_path for that mount"""
    # findmnt SOURCE format is "device[/bind/source/path]"; extract the bracketed part
    output = atf.run_job_output(
        "findmnt --noheadings -o SOURCE /var/tmp | sed 's/.*\\[\\(.*\\)\\]/\\1/'",
        fatal=True,
    )
    assert re.search(
        r"^/tmp/test_159_2_per_dir_ns/", output, re.MULTILINE
    ), "Bind mount backing store was not in the per-dir base_path"


def test_path_user_expansion():
    """%%u in a dir_confs path is expanded to the job's username"""
    username = atf.run_command_output("whoami", fatal=True).strip()
    output = atf.run_job_output(
        f"findmnt --noheadings -o TARGET /home/{username}/test_159_2_ns",
        fatal=True,
    )
    assert re.search(
        rf"/home/{re.escape(username)}/test_159_2_ns", output
    ), f"No private mount found at /home/{username}/test_159_2_ns — %u path expansion may not be working"
