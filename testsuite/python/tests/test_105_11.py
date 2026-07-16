############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sbatch --parsable output format."""

import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_sbatch_parsable():
    """sbatch --parsable writes only the job id (or jobid;cluster) to stdout."""
    result = atf.run_command(
        "sbatch --parsable -t1 --job-name=test_sbatch_parsable -o /dev/null --wrap 'true'",
        fatal=True,
    )

    match = re.match(r"^(\d+)(?:;.+)?\s*$", result["stdout"])
    assert match, (
        f"expected bare jobid (or jobid;cluster) on stdout, "
        f"got: {result['stdout']!r}"
    )
    atf.properties["submitted-jobs"].append(int(match.group(1)))
