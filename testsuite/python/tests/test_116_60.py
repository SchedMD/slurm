############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the srun/salloc/sbatch --runtime=list option."""

import re

import pytest

import atf


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11), "bin/srun", reason="Issue 50190: --runtime=list added in 26.11"
    )
    atf.require_version(
        (26, 11), "bin/salloc", reason="Issue 50190: --runtime=list added in 26.11"
    )
    atf.require_version(
        (26, 11), "bin/sbatch", reason="Issue 50190: --runtime=list added in 26.11"
    )
    atf.require_slurm_running()


@pytest.mark.parametrize("command", ["srun", "salloc", "sbatch"])
def test_runtime_list(command):
    """--runtime=list prints the available runtime plugins and exits 0."""

    result = atf.run_command(f"{command} --runtime=list", fatal=True)

    # The list is printed to stderr, mimicking --mpi=list.
    output = result["stderr"] + result["stdout"]
    assert "Runtime plugin types are" in output, "missing runtime plugin list header"
    # runtime/none is always built, so require it. runtime/oci is optional
    # (it may not be built on all systems), so it is not required here.
    assert re.search(r"\bnone\b", output), "runtime/none not listed"
