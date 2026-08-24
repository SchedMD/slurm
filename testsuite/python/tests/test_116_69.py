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
        ("--cpu-bind=map_ldom", "0x0"),
        ("--cpu-bind=mask_ldom", "NaN"),
        ("--mem-bind=map_mem", "NaN"),
        ("--mem-bind=map_mem", "0x0"),
        ("--mem-bind=mask_mem", "NaN"),
    ],
)
def test_invalid_bind(bind_flag, spec):
    """Test that invalid cpu-bind and mem-bind values are rejected."""
    flag = f"{bind_flag}:{spec}"
    result = atf.run_job(f"{flag} hostname", xfail=True, fatal=True)
    assert result["exit_code"] != 0, f"srun should reject {flag}"
    assert spec in result["stderr"], f"{flag} should be rejected naming {spec}"
    assert not result["stdout"], f"srun should not run the step with {flag}"


xfail_rep_syntax = pytest.mark.xfail(
    atf.get_version("bin/srun") < (26, 11),
    reason="Issue 51127: List element parsing fixed in 26.11",
)


@pytest.mark.parametrize(
    "bind_flag,spec",
    [
        # A repetition count on a mask that is not preceded by "0x"
        pytest.param("--cpu-bind=mask_cpu", "a*2,c*2", marks=xfail_rep_syntax),
        pytest.param("--cpu-bind=mask_ldom", "a*2,c*2", marks=xfail_rep_syntax),
        pytest.param("--mem-bind=mask_mem", "a*2,c*2", marks=xfail_rep_syntax),
        # The "0x" prefix is optional
        ("--cpu-bind=mask_cpu", "0xa*2,0xc*2"),
        ("--mem-bind=mask_mem", "0xa*2,0xc*2"),
        # Masks with no repetition count, and decimal maps
        ("--cpu-bind=mask_cpu", "a,c"),
        ("--mem-bind=mask_mem", "a,c"),
        ("--cpu-bind=map_cpu", "0*2,1*2"),
        ("--mem-bind=map_mem", "0*2,1*2"),
    ],
)
def test_valid_bind_list(bind_flag, spec):
    """Test that a map or mask list with a repetition count is accepted."""

    # --test-only checks the syntax without launching, so the CPUs and NUMA
    # nodes named by the list need not exist.
    flag = f"{bind_flag}:{spec}"
    result = atf.run_job(f"--test-only -n1 {flag} true")
    assert result["exit_code"] == 0, f"srun should accept {flag}: {result['stderr']}"


@pytest.mark.parametrize(
    "bind_flag,spec",
    [
        # A repetition count with no value in front of it
        ("--cpu-bind=map_cpu", "*4"),
        ("--cpu-bind=mask_cpu", "*4"),
        ("--mem-bind=map_mem", "*4"),
        ("--mem-bind=mask_mem", "*4"),
        # A "0x" prefix with no digits after it
        ("--cpu-bind=mask_cpu", "0x"),
        ("--cpu-bind=mask_cpu", "0x*2"),
        ("--mem-bind=mask_mem", "0x"),
        ("--mem-bind=mask_mem", "0x*2"),
    ],
)
@xfail_rep_syntax
def test_empty_list_element(bind_flag, spec):
    """Test that a list element holding no digits is rejected."""

    flag = f"{bind_flag}:{spec}"
    result = atf.run_job(f"-n1 {flag} hostname", fatal=True, xfail=True)
    assert (
        f"Failed to validate number: {spec}, no digits found" in result["stderr"]
    ), f"{flag} should be rejected as having no digits: {result['stderr']}"
    assert not result["stdout"], f"srun should not run the step with {flag}"


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
