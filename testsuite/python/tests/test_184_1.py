############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test --hint parsing and help in srun, sbatch and salloc."""

import pytest

import atf

HELP_COMMANDS = {
    "srun": "srun --hint=help true",
    "sbatch": "sbatch --hint=help --wrap true",
    "salloc": "salloc --hint=help true",
}

INVALID_COMMANDS = {
    "srun": "srun --hint=bogus true",
    "sbatch": "sbatch --hint=bogus --wrap true",
    "salloc": "salloc --hint=bogus true",
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


@pytest.mark.parametrize(
    "hint",
    [
        "compute_bound",
        "memory_bound",
        "multithread",
        "nomultithread",
        "compute_bound,nomultithread",
        "COMPUTE_BOUND",
    ],
)
def test_valid_hint(hint):
    """Test that documented hints are accepted."""
    result = atf.run_job(f"--hint={hint} true")
    assert (
        result["exit_code"] == 0
    ), f"srun should accept --hint={hint}: {result['stderr']}"


@pytest.mark.parametrize("command", INVALID_COMMANDS)
def test_invalid_hint(command):
    """Test that an unrecognized hint is rejected and points to --hint=help."""
    result = atf.run_command(INVALID_COMMANDS[command], xfail=True)
    assert result["exit_code"] != 0, f"{command} should reject --hint=bogus"
    assert (
        "unrecognized --hint argument" in result["stderr"]
    ), f"{command} should name the bad --hint argument: {result['stderr']}"
    assert "--hint=help" in result["stderr"], f"{command} should point to --hint=help"


@pytest.mark.parametrize("command", HELP_COMMANDS)
def test_hint_help_output(command):
    """Test that --hint=help lists every documented hint."""

    # The exit code is checked in test_hint_help_exit_code
    result = atf.run_command(
        HELP_COMMANDS[command],
        xfail=atf.get_version(f"bin/{command}") < (26, 11),
    )

    text = result["stdout"].replace("[", "").replace("]", "")
    for hint in [
        "compute_bound",
        "memory_bound",
        "multithread",
        "nomultithread",
        "help",
    ]:
        assert hint in text, f"{command} --hint=help should list {hint}"


@pytest.mark.xfail(
    atf.get_version("bin/srun") < (26, 11),
    reason="Issue 51193: --hint=help exits non-zero before 26.11",
)
@pytest.mark.parametrize("command", HELP_COMMANDS)
def test_hint_help_exit_code(command):
    """Test that asking for --hint=help is a successful outcome."""
    result = atf.run_command(HELP_COMMANDS[command])
    assert (
        result["exit_code"] == 0
    ), f"{command} --hint=help should exit 0, not {result['exit_code']}"
