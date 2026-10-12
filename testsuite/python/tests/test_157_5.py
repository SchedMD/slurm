############################################################################
# Copyright (C) 2026 Google LLC.
############################################################################
"""Test job_submit/tpu --layout expansion, validation, and GRES preservation."""

import os

import pytest

import atf

TPU_YAML = """tpu_types:
  - type: tpu7x
    pci_device_id: "0x0076"
    shape_per_node: 2x2x1
    shape_per_segment: 2x2x4
    nodes_per_pod: 16
  - type: v6e
    pci_device_id: "0x006f"
    shape_per_node: 2x2
    shape_per_segment: 16x16
    nodes_per_pod: 64
"""

GRES_CONF = """AutoDetect=off
Name=tpu Type=tpu7x Count=4
Name=fake Count=1
"""


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="job_submit/tpu plugin added in 26.11",
    )
    atf.require_auto_config("wants to configure tpu.yaml and JobSubmitPlugins=tpu")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("JobSubmitPlugins", "tpu")
    atf.require_config_parameter_includes("GresTypes", "tpu")
    atf.require_config_parameter_includes("GresTypes", "fake")
    atf.require_config_file("tpu.yaml", TPU_YAML)
    atf.require_config_file("gres.conf", GRES_CONF)
    atf.require_nodes(8, [("Gres", "tpu:tpu7x:4,fake:1"), ("CPUs", 8)])
    atf.require_slurm_running()


def _submit_held(extra_args=""):
    args = "--hold --output=/dev/null --wrap='true'"
    if extra_args:
        args = f"{extra_args} {args}"
    return atf.submit_job_sbatch(args, fatal=True)


def _field(job_id, param):
    return atf.get_job_parameter(job_id, param, default=None, quiet=True)


def test_intra_segment_request():
    """A request fitting inside one segment sets Layout, NumNodes, and TresPerNode."""
    # 2x2x2 = 8 chips / 4 chips_per_node = 2 nodes <= 4 (nodes_per_segment)
    job_id = _submit_held("--layout=tpu7x=2x2x2")
    assert _field(job_id, "Layout") == "tpu7x=2x2x2"
    assert _field(job_id, "NumNodes") == "2-2"
    assert _field(job_id, "TresPerNode") == "gres/tpu:tpu7x:4"


def test_multi_segment_request():
    """A request spanning segments sets Layout, NumNodes, and TresPerNode."""
    # 2x2x8 = 32 chips / 4 chips_per_node = 8 nodes > 4 (nodes_per_segment)
    job_id = _submit_held("--layout=tpu7x=2x2x8")
    assert _field(job_id, "Layout") == "tpu7x=2x2x8"
    assert _field(job_id, "NumNodes") == "8-8"
    assert _field(job_id, "TresPerNode") == "gres/tpu:tpu7x:4"


def test_tres_per_node_duplicate_refused():
    """Submitting both --layout and a conflicting --gres=tpu is rejected."""
    result = atf.run_command(
        "sbatch --hold --layout=tpu7x=2x2x2 --gres=tpu:tpu7x:4 --wrap='true'",
        xfail=True,
    )
    assert result["exit_code"] != 0
    assert "already requests TPUs" in result["stderr"]


def test_unrelated_gres_preserved():
    """Unrelated GRES on the command line is preserved alongside layout's TPUs."""
    job_id = _submit_held("--layout=tpu7x=2x2x2 --gres=fake:1")
    tres = _field(job_id, "TresPerNode")
    assert tres is not None
    assert "gres/fake:1" in tres
    assert "gres/tpu:tpu7x:4" in tres


def test_scontrol_update_layout_preserves_unrelated_gres():
    """Updating Layout via scontrol re-sizes the job and preserves unrelated GRES."""
    job_id = _submit_held("--layout=tpu7x=2x2x2 --gres=fake:1")
    atf.run_command(
        f"scontrol update JobId={job_id} Layout=tpu7x=2x2x8",
        fatal=True,
    )
    assert _field(job_id, "Layout") == "tpu7x=2x2x8"
    assert _field(job_id, "NumNodes") == "8-8"
    assert _field(job_id, "TresPerNode") == "gres/fake:1,gres/tpu:tpu7x:4"


def test_subhost_gres_without_layout():
    """Sub-host TPU jobs via --gres=tpu without --layout succeed."""
    job_id = _submit_held("--gres=tpu:tpu7x:2")
    tres = _field(job_id, "TresPerNode")
    assert tres is not None
    assert "gres/tpu:tpu7x:2" in tres
    assert _field(job_id, "Layout") is None


def test_cpu_only_without_layout():
    """CPU-only jobs on a TPU partition without --layout succeed."""
    job_id = _submit_held()
    assert _field(job_id, "Layout") is None


@pytest.mark.parametrize(
    "layout_arg,expected_err",
    [
        # Out of order: 4x2x2 is 16 chips, but dimensions must not decrease
        ("--layout=tpu7x=4x2x2", "Invalid accelerator layout"),
        # Non-canonical family: 7x instead of tpu7x
        ("--layout=7x=2x2x2", "Unknown accelerator family"),
        # Incompatible shape: 3x3x3 is not a multiple of node shape 2x2x1
        ("--layout=tpu7x=3x3x3", "Invalid accelerator layout"),
        # Unbalanced intra-segment aspect ratio: 4 v6e nodes are 4x4, not 2x8
        ("--layout=v6e=2x8", "Invalid accelerator layout"),
    ],
    ids=[
        "out_of_order",
        "non_canonical_family",
        "incompatible_shape",
        "unbalanced_aspect_ratio",
    ],
)
def test_rejections(layout_arg, expected_err):
    """Invalid geometries and non-canonical families must be rejected."""
    result = atf.run_command(
        f"sbatch --hold {layout_arg} --wrap='true'",
        xfail=True,
    )
    assert result["exit_code"] != 0
    assert expected_err in result["stderr"]


def test_yaml_clean_load():
    """Verify that tpu.yaml was parsed without error in the controller log."""
    ctld_log = atf.get_config()["SlurmctldLogFile"]
    if os.path.exists(ctld_log):
        log_content = atf.run_command_output(f"cat {ctld_log}", user="root", quiet=True)
        assert "no usable tpu.yaml" not in log_content
