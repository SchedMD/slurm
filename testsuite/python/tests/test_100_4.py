############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
"""
Meta-test: This is not testing Slurm but the test environment.

This test intentionally runs a python app that crashes to verify that the resulting
coredump file is created and detected by atf.get_coredumps(), and that the
build-id-based binary resolution used by module_teardown() (via
atf.resolve_core_binary()) can identify the crashing binary from the core.
This ensures that module_teardown() in conftest.py will detect, symbolize
and report daemon/client crashes correctly.

Key environment factors that affect coredump generation:
  - /proc/sys/kernel/core_pattern  (kernel setting, shared with host in Docker)
  - RLIMIT_CORE (ulimit -c) for the crashing process
  - sudoers config: "Defaults rlimit_core=default" is required
  - Container capabilities (some restrict core dumps)
  - readelf (binutils) and eu-unstrip (elfutils) available in the container
"""
import logging
import os

import pytest

import atf


@pytest.fixture(scope="function", autouse=True)
def setup(function_setup):
    core_pattern = atf.run_command_output(
        "cat /proc/sys/kernel/core_pattern", quiet=True
    )
    logging.info(f"Kernel core_pattern: {core_pattern}")

    yield

    for coredump in atf.get_coredumps():
        atf.run_command(f"rm -rf {coredump}", fatal=True, user="root")


@pytest.mark.parametrize(
    "sudo_cmd,sudo_msg",
    [
        ("", "as current user"),
        ("sudo", "as root"),
        ("sudo -u slurm", "as slurm user"),
    ],
)
@pytest.mark.parametrize(
    "cwd_key,cwd_msg",
    [
        (None, "current cwd"),
        ("slurm-logs-dir", "slurm logs dir"),
    ],
)
def test_coredumps(sudo_cmd, sudo_msg, cwd_key, cwd_msg):
    """Verify crashes produce coredumps detectable by atf.get_coredumps()"""

    crash_cmd = "python3 -c 'import os, signal; os.kill(os.getpid(), signal.SIGABRT)'"
    chdir = atf.properties[cwd_key] if cwd_key else None

    atf.run_command(f"{sudo_cmd} {crash_cmd}", xfail=True, chdir=chdir)
    coredumps = atf.get_coredumps()
    assert (
        len(coredumps) == 1
    ), f"One coredump should be generated {sudo_msg} in {cwd_msg}, but got {coredumps}."

    # Assert atf.resolve_core_binary() works correctly
    python3_path = atf.run_command_output("which python3", quiet=True).strip()
    resolved = atf.resolve_core_binary(coredumps[0], python3_path)
    assert resolved == os.path.realpath(
        python3_path
    ), f"resolve_core_binary should return {os.path.realpath(python3_path)} for the python3 core generated {sudo_msg} in {cwd_msg}, but got {resolved}. Check that elfutils (eu-unstrip) and readelf are installed in the container."
