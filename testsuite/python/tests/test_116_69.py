############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test srun --cpu-bind and --mem-bind parsing and help."""

import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


@pytest.mark.parametrize(
    "bind_flag,spec",
    [
        ("--cpu-bind=map_cpu", "NaN"),
        ("--cpu-bind=map_cpu", "0x0"),
        ("--cpu-bind=mask_cpu", "NaN"),
        ("--cpu-bind=map_ldom", "NaN"),
        ("--cpu-bind=mask_ldom", "NaN"),
        ("--mem-bind=map_mem", "NaN"),
        ("--mem-bind=mask_mem", "NaN"),
    ],
)
def test_invalid_bind(bind_flag, spec):
    """Test that invalid cpu-bind and mem-bind values are rejected."""
    flag = f"{bind_flag}:{spec}"
    result = atf.run_job(f"{flag} hostname", xfail=True, fatal=True)
    assert result["exit_code"] != 0, f"srun should reject {flag}"
    assert spec in result["stderr"], f"{flag} should be rejected naming {spec}"


CPU_BIND_HELP_ENTRIES = {
    "quiet",
    "verbose",
    "none",
    "map_cpu",
    "mask_cpu",
    "rank_ldom",
    "map_ldom",
    "mask_ldom",
    "sockets",
    "cores",
    "threads",
    "ldoms",
    "help",
}
MEM_BIND_HELP_ENTRIES = {
    "quiet",
    "verbose",
    "prefer",
    "none",
    "rank",
    "local",
    "map_mem",
    "mask_mem",
    "help",
}


@pytest.mark.parametrize(
    "bind_type,expected",
    [
        ("cpu", CPU_BIND_HELP_ENTRIES),
        pytest.param(
            "mem",
            MEM_BIND_HELP_ENTRIES,
            marks=pytest.mark.xfail(
                atf.get_version("bin/srun") < (26, 11),
                reason="Issue 50625: --mem-bind=help lists prefer since 26.11",
            ),
        ),
    ],
)
def test_bind_help(bind_type, expected):
    """Test that a help bind type lists every documented bind option."""

    # srun reports an option error after printing the help, so the exit code
    # says nothing about whether the help was shown. See issue 51193.
    result = atf.run_command(f"srun --{bind_type}-bind=help true", xfail=True)

    # Entries may be printed like "q[uiet]"
    normalized = result["stdout"].replace("[", "").replace("]", "")
    found = set(re.split(r"[\s:]+", normalized))
    missing = expected - found
    assert (
        not missing
    ), f"--{bind_type}-bind=help should list {missing}: {result['stdout']}"
