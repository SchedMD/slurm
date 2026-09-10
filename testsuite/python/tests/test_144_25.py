############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify --gres-flags=enforce-links on a GRES configured by count alone.

No File= means no per-device topology and so no links, so more than one such
device cannot be shown to be linked and the node is not used. Same answer as
for devices with File= but no Links=.

'fpga' has no GRES plugin; a gpu without File= is refused by slurmd.
"""

import pytest

import atf

pytestmark = pytest.mark.slow

DEVICES = 4


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/sbatch",
        reason="Issue 51169: --gres-flags=enforce-links added in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Overwrite rather than add to what a config variant set: adding CR_CORE to
    # a variant that already selected another CR_* type is rejected by Slurm.
    atf.require_config_parameter("SelectTypeParameters", "CR_CORE")
    atf.require_config_parameter_includes("GresTypes", "fpga")
    atf.require_nodes(
        1,
        [("Gres", f"fpga:{DEVICES}"), ("Sockets", 1), ("CoresPerSocket", 4)],
    )
    atf.require_config_parameter(
        "NodeName",
        f"node1 Name=fpga Count={DEVICES}",
        source="gres",
    )
    atf.require_slurm_running()


def test_count_only_gres_runs_without_the_flag():
    """Control: without the flag a two device job runs on the count-only GRES,
    so a refusal below is caused by enforce-links and not by a shortage."""
    job_id = atf.submit_job_sbatch(
        '--gres=fpga:2 -w node1 --wrap "sleep infinity"', fatal=True
    )
    assert atf.wait_for_job_state(job_id, "RUNNING"), "no-flag job should run"


def test_count_only_gres_refused_with_enforce_links():
    """Two count-only devices cannot be shown to be linked, so the request is
    refused rather than given devices that were never checked."""
    result = atf.run_command(
        "sbatch --gres-flags=enforce-links --gres=fpga:2 -w node1 "
        '--wrap "sleep infinity"',
        xfail=True,
    )
    assert result["exit_code"] != 0, f"should be refused, got {result}"
    assert (
        "Requested node configuration is not available" in result["stderr"]
    ), f"should be refused for the node configuration, got {result}"


def test_count_only_gres_single_device_runs():
    """A lone device has nothing to link to, so it is unaffected."""
    job_id = atf.submit_job_sbatch(
        "--gres-flags=enforce-links --gres=fpga:1 -w node1 " '--wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "single device enforce-links job should run"
