############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test --gres parsing and help in srun, sbatch and salloc."""

import pytest

import atf

COMMANDS = {
    "srun": "srun --gres={gres} true",
    "sbatch": "sbatch --gres={gres} --wrap true",
    "salloc": "salloc --gres={gres} true",
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_valid_gres():
    """Test that --gres=none is accepted."""
    result = atf.run_job("--gres=none true")
    assert result["exit_code"] == 0, "srun should accept --gres=none"


@pytest.mark.parametrize("command", COMMANDS)
def test_invalid_gres(command):
    """Test that a gres name that is not configured is rejected."""
    result = atf.run_command(COMMANDS[command].format(gres="bogus"), xfail=True)
    assert result["exit_code"] != 0, f"{command} should reject --gres=bogus"
    assert (
        "Invalid generic resource" in result["stderr"]
    ), f"{command} should report an invalid gres: {result['stderr']}"


@pytest.mark.parametrize("gres", ["help", "list"])
@pytest.mark.parametrize("command", COMMANDS)
def test_gres_help(command, gres):
    """Test that --gres=help and --gres=list print the options and succeed."""
    result = atf.run_command(COMMANDS[command].format(gres=gres))
    assert (
        result["exit_code"] == 0
    ), f"{command} --gres={gres} should exit 0, not {result['exit_code']}"
    assert (
        "Valid gres options are:" in result["stdout"]
    ), f"{command} --gres={gres} should print the gres options: {result['stdout']}"
