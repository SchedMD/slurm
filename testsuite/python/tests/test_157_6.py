############################################################################
# Copyright (C) 2026 Google LLC.
############################################################################
"""Test job_submit/tpu --layout expansion, validation, and GRES preservation."""

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
        "bin/sbatch",
        reason="sbatch --layout and job_submit/tpu added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="scontrol Layout= added in 26.11",
    )
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
    assert _field(job_id, "Layout") == "tpu7x=2x2x2", "The layout should be stored"
    assert _field(job_id, "NumNodes") == "2-2", "2x2x2 should need 2 nodes"
    assert (
        _field(job_id, "TresPerNode") == "gres/tpu:tpu7x:4"
    ), "Each node should get the 4 chips of a tpu7x node"


def test_multi_segment_request():
    """A request spanning segments sets Layout, NumNodes, and TresPerNode."""
    # 2x2x8 = 32 chips / 4 chips_per_node = 8 nodes > 4 (nodes_per_segment)
    job_id = _submit_held("--layout=tpu7x=2x2x8")
    assert _field(job_id, "Layout") == "tpu7x=2x2x8", "The layout should be stored"
    assert _field(job_id, "NumNodes") == "8-8", "2x2x8 should need 8 nodes"
    assert (
        _field(job_id, "TresPerNode") == "gres/tpu:tpu7x:4"
    ), "Each node should get the 4 chips of a tpu7x node"


def test_tres_per_node_duplicate_refused():
    """Submitting both --layout and a conflicting --gres=tpu is rejected."""
    result = atf.run_command(
        "sbatch --hold --layout=tpu7x=2x2x2 --gres=tpu:tpu7x:4 --wrap='true'",
        xfail=True,
    )
    assert result["exit_code"] != 0, "sbatch should fail when TPUs are requested twice"
    assert (
        "already requests TPUs" in result["stderr"]
    ), "The error should name the duplicate TPU request"


def test_unrelated_gres_preserved():
    """Unrelated GRES on the command line is preserved alongside layout's TPUs."""
    job_id = _submit_held("--layout=tpu7x=2x2x2 --gres=fake:1")
    tres = _field(job_id, "TresPerNode")
    assert tres is not None, "The job should request GRES"
    assert "gres/fake:1" in tres, f"The fake GRES should be kept: {tres}"
    assert "gres/tpu:tpu7x:4" in tres, f"The layout should add its TPUs: {tres}"


def test_scontrol_update_layout_preserves_unrelated_gres():
    """Updating Layout via scontrol re-sizes the job and preserves unrelated GRES."""
    job_id = _submit_held("--layout=tpu7x=2x2x2 --gres=fake:1")
    atf.run_command(
        f"scontrol update JobId={job_id} Layout=tpu7x=2x2x8",
        fatal=True,
    )
    assert _field(job_id, "Layout") == "tpu7x=2x2x8", "The new layout should be stored"
    assert _field(job_id, "NumNodes") == "8-8", "The job should be resized to 8 nodes"
    assert (
        _field(job_id, "TresPerNode") == "gres/fake:1,gres/tpu:tpu7x:4"
    ), "The fake GRES should be kept with the layout's TPUs"


def test_scontrol_clear_layout_preserves_unrelated_gres():
    """Clearing Layout via scontrol strips TPU GRES and preserves unrelated GRES."""
    job_id = _submit_held("--layout=tpu7x=2x2x2 --gres=fake:1")
    atf.run_command(
        f"scontrol update JobId={job_id} Layout= NumNodes=1",
        fatal=True,
    )
    assert _field(job_id, "Layout") is None, "The layout should be cleared"
    assert (
        _field(job_id, "TresPerNode") == "gres/fake:1"
    ), "Only the layout's TPUs should be removed"
    assert _field(job_id, "NumNodes") in (
        "1",
        "1-1",
    ), "The job should take the node count given with the update"


def test_scontrol_clear_layout_needs_numnodes():
    """Clearing a TPU layout without NumNodes is refused and changes nothing."""
    job_id = _submit_held("--layout=tpu7x=2x2x2 --gres=fake:1")
    result = atf.run_command(
        f"scontrol update JobId={job_id} Layout=",
        xfail=True,
    )
    assert (
        result["exit_code"] != 0
    ), "Clearing a TPU layout without NumNodes should be refused"
    assert _field(job_id, "Layout") == "tpu7x=2x2x2", "The layout should be kept"
    assert _field(job_id, "NumNodes") == "2-2", "The node count should be kept"
    assert (
        _field(job_id, "TresPerNode") == "gres/fake:1,gres/tpu:tpu7x:4"
    ), "The GRES should be kept"


def test_layout_pairs_for_other_plugins_kept():
    """Pairs that request no TPUs are kept and do not change the sizing."""
    job_id = _submit_held("--layout=tpu7x=2x2x2,ocs=on")
    assert (
        _field(job_id, "Layout") == "tpu7x=2x2x2,ocs=on"
    ), "The layout should be stored with the pair for other plugins"
    assert (
        _field(job_id, "NumNodes") == "2-2"
    ), "The TPU pair alone should size the job to 2 nodes"
    assert (
        _field(job_id, "TresPerNode") == "gres/tpu:tpu7x:4"
    ), "The TPU pair alone should add gres/tpu:tpu7x:4"


def test_layout_without_tpu_pair():
    """A layout that requests no TPUs is accepted and adds no TPU GRES."""
    job_id = _submit_held("--layout=foo=bar")
    assert _field(job_id, "Layout") == "foo=bar", "The layout should be stored as given"
    assert (
        _field(job_id, "TresPerNode") is None
    ), "A layout with no TPU pair should not add TPU GRES"


@pytest.mark.parametrize(
    "layout,tres",
    [
        ("ranks=8", None),
        ("tpu7x=2x2x2,ranks=8", "gres/tpu:tpu7x:4"),
    ],
    ids=["number_only", "number_with_tpu_pair"],
)
def test_layout_number_pair_for_other_plugins(layout, tres):
    """A pair whose value is a plain number is left to other plugins."""
    job_id = _submit_held(f"--layout={layout}")
    assert _field(job_id, "Layout") == layout, "The layout should be stored as given"
    assert (
        _field(job_id, "TresPerNode") == tres
    ), "Only the TPU pair should add TPU GRES"


def test_tres_per_task_duplicate_refused():
    """TPUs requested in another TRES field are refused alongside --layout."""
    result = atf.run_command(
        "sbatch --hold --layout=tpu7x=2x2x2 --ntasks=2 --tres-per-task=gres/tpu=1 --wrap='true'",
        xfail=True,
    )
    assert result["exit_code"] != 0, "sbatch should fail when TPUs are requested twice"
    assert (
        "already requests TPUs" in result["stderr"]
    ), "The error should name the duplicate TPU request"


def test_scontrol_update_gres_keeps_layout_tpus():
    """Replacing the GRES of a pending job keeps the TPUs its layout requests."""
    job_id = _submit_held("--layout=tpu7x=2x2x2")
    atf.run_command(f"scontrol update JobId={job_id} Gres=fake:1", fatal=True)
    assert (
        _field(job_id, "Layout") == "tpu7x=2x2x2"
    ), "Updating the GRES should not change the layout"
    assert (
        _field(job_id, "TresPerNode") == "gres/fake:1,gres/tpu:tpu7x:4"
    ), "The new GRES should be followed by the TPUs the layout requests"


def test_scontrol_update_numnodes_checked_against_layout():
    """A node count that contradicts the layout is refused."""
    job_id = _submit_held("--layout=tpu7x=2x2x2")
    result = atf.run_command(
        f"scontrol update JobId={job_id} NumNodes=4",
        xfail=True,
    )
    assert (
        result["exit_code"] != 0
    ), "A node count the layout does not need should be refused"
    assert (
        _field(job_id, "NumNodes") == "2-2"
    ), "A refused update should leave the node count as it was"

    atf.run_command(f"scontrol update JobId={job_id} NumNodes=2", fatal=True)
    assert (
        _field(job_id, "NumNodes") == "2-2"
    ), "The node count the layout needs should be accepted"


def test_subhost_gres_without_layout():
    """Sub-host TPU jobs via --gres=tpu without --layout succeed."""
    job_id = _submit_held("--gres=tpu:tpu7x:2")
    tres = _field(job_id, "TresPerNode")
    assert tres is not None, "The job should request GRES"
    assert "gres/tpu:tpu7x:2" in tres, f"The requested TPUs should be kept: {tres}"
    assert _field(job_id, "Layout") is None, "The job should have no layout"


def test_cpu_only_without_layout():
    """CPU-only jobs on a TPU partition without --layout succeed."""
    job_id = _submit_held()
    assert _field(job_id, "Layout") is None, "The job should have no layout"


@pytest.mark.parametrize(
    "layout_arg,expected_err",
    [
        # Out of order: 4x2x2 is 16 chips, but dimensions must not decrease
        ("--layout=tpu7x=4x2x2", "dimensions must be in non-decreasing order"),
        # Non-canonical family: 7x instead of tpu7x
        ("--layout=7x=2x2x2", "Unknown accelerator family"),
        # Incompatible shape: 3x3x3 is not a multiple of node shape 2x2x1
        ("--layout=tpu7x=3x3x3", "whole number of 2x2x1 nodes along every axis"),
        # Unbalanced intra-segment aspect ratio: 4 v6e nodes are 4x4, not 2x8
        ("--layout=v6e=2x8", "largest dimension at most twice its smallest"),
        # TPUs from two families in one job
        ("--layout=tpu7x=2x2x2,v6e=2x2", "one accelerator family only"),
    ],
    ids=[
        "out_of_order",
        "non_canonical_family",
        "incompatible_shape",
        "unbalanced_aspect_ratio",
        "two_families",
    ],
)
def test_rejections(layout_arg, expected_err):
    """Invalid geometries and non-canonical families must be rejected."""
    result = atf.run_command(
        f"sbatch --hold {layout_arg} --wrap='true'",
        xfail=True,
    )
    assert result["exit_code"] != 0, f"sbatch {layout_arg} should be refused"
    assert (
        expected_err in result["stderr"]
    ), f"Expected '{expected_err}' in: {result['stderr']}"
    assert (
        "Invalid layout specification" in result["stderr"]
    ), "The job should be refused for its layout"


def test_yaml_clean_load():
    """Verify that tpu.yaml was parsed without error in the controller log."""
    ctld_log = atf.get_config()["SlurmctldLogFile"]
    log_content = atf.run_command_output(
        f"cat {ctld_log}", user="root", fatal=True, quiet=True
    )
    assert (
        "no usable tpu.yaml" not in log_content
    ), "job_submit/tpu should load tpu.yaml without error"
