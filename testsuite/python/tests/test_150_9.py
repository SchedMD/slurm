############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test how slurmctld handles bad topology.yaml files"""

import os
import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5, 3),
        "sbin/slurmctld",
        reason="Fixed xassert instead of fatal for bad formatted topology.yaml fails, and enforce BlockSizes",
    )
    atf.require_nodes(17)
    # intentionally not starting slurm


testcase_dir = os.path.splitext(__file__)[0] + "_testcases"


def read_testcase(filename):
    """Return the contents of a topology.yaml testcase file"""

    with open(f"{testcase_dir}/{filename}") as f:
        return f.read()


# One case per parse_error() call site reachable from PARSER_ARRAY(TOPOLOGY_CONF)
#
# Note "mutually exclusive" replaced with "mutually [a-z]*" because it is
# misspelled before data_parser/v0.0.46
@pytest.mark.parametrize(
    ("topology_file", "expected_error"),
    [
        (
            "root_not_list.yaml",
            "source: #/, description: Unexpected type string when expecting a list, rc: 9202",
        ),
        (
            "block_not_dict.yaml",
            "source: #/block/, description: Rejecting list when dictionary expected, rc: 9209",
        ),
        (
            "block_blocks_not_list.yaml",
            "source: #/block/blocks/, description: Unexpected type string when expecting a list, rc: 9202",
        ),
        (
            "flat_bool_mutually_exclusive.yaml",
            "source: #/flat/, description: Field flat is mutually [a-z]* with other plugins, rc: -1",
        ),
        (
            "flat_dict_mutually_exclusive.yaml",
            "source: #/flat/, description: Field flat is mutually [a-z]* with other plugins, rc: -1",
        ),
        (
            "ring_not_dict.yaml",
            "source: #/ring/, description: Rejecting string when dictionary expected, rc: 9209",
        ),
        (
            "ring_mutually_exclusive.yaml",
            "source: #/ring/, description: Field ring is mutually [a-z]* with fields block, tree and flat, rc: -1",
        ),
        (
            "ring_rings_not_list.yaml",
            "source: #/ring/rings/, description: Unexpected type string when expecting a list, rc: 9202",
        ),
        (
            "torus3d_not_dict.yaml",
            "source: #/torus3d/, description: Rejecting string when dictionary expected, rc: 9209",
        ),
        (
            "torus3d_mutually_exclusive.yaml",
            "source: #/torus3d/, description: Field torus3d is mutually [a-z]* with fields block, tree, flat and ring, rc: -1",
        ),
        (
            "torus3d_toruses_not_list.yaml",
            "source: #/torus3d/toruses/, description: Unexpected type string when expecting a list, rc: 9202",
        ),
        (
            "torus3d_placements_not_list.yaml",
            "source: #/torus3d/toruses/placements/, description: Unexpected type string when expecting a list, rc: 9202",
        ),
        (
            "torus3d_regions_not_list.yaml",
            "source: #/torus3d/toruses/regions/, description: Unexpected type string when expecting a list, rc: 9202",
        ),
        (
            "tree_not_dict.yaml",
            "source: #/tree/, description: Rejecting string when dictionary expected, rc: 9209",
        ),
        (
            "tree_mutually_exclusive.yaml",
            "source: #/tree/, description: Field tree is mutually [a-z]* with fields block and flat, rc: -1",
        ),
        (
            "tree_switches_not_list.yaml",
            "source: #/tree/switches/, description: Unexpected type string when expecting a list, rc: 9202",
        ),
    ],
    ids=[
        "root_not_list",
        "block_not_dict",
        "block_blocks_not_list",
        "flat_bool_mutually_exclusive",
        "flat_dict_mutually_exclusive",
        "ring_not_dict",
        "ring_mutually_exclusive",
        "ring_rings_not_list",
        "torus3d_not_dict",
        "torus3d_mutually_exclusive",
        "torus3d_toruses_not_list",
        "torus3d_placements_not_list",
        "torus3d_regions_not_list",
        "tree_not_dict",
        "tree_mutually_exclusive",
        "tree_switches_not_list",
    ],
)
def test_failed_parsing(topology_file, expected_error):
    atf.require_config_file("topology.yaml", read_testcase(topology_file))

    result = atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmctld -D",
        user=atf.properties["slurm-user"],
        xfail=True,
        quiet=True,
    )
    # Prevent the slurmctld continuing to run on timeout since it is launched
    # under sudo
    if result["exit_code"] == 110:
        atf.stop_slurmctld()

    assert re.search(
        expected_error, result["stderr"]
    ), f"slurmctld should log {expected_error} but didn't, got: {result['stderr']}"
    assert (
        "error: Failed to load topology.yaml" in result["stderr"]
    ), f"slurmctld should report a topology.yaml load failure, got: {result['stderr']}"
    assert (
        "fatal: Failed to initialize topology plugin" in result["stderr"]
    ), f"slurmctld should fatal on topology init, got: {result['stderr']}"
    assert (
        result["exit_code"] == 1
    ), f"slurmctld should fatal (exit 1), got {result['exit_code']}"


@pytest.mark.parametrize(
    ("topology_file", "fatal_message"),
    [
        ("tree_bad_options.yaml", "fatal: Switch configuration sw_root has invalid"),
        (
            "block_bad_options.yaml",
            "fatal: Invalid BlockSizes value in topology block_bad_options",
        ),
        ("ring_bad_options.yaml", "fatal: Ring (ring0) is bigger than 16"),
        ("flat_bad_options.yaml", "fatal: Failed to initialize topology plugin"),
        ("torus_bad_options.yaml", "fatal: Torus3d (pod1) has invalid configuration"),
    ],
    ids=["tree", "block", "ring", "flat", "torus"],
)
def test_bad_options(topology_file, fatal_message):
    atf.require_config_file("topology.yaml", read_testcase(topology_file))

    result = atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmctld -D",
        user=atf.properties["slurm-user"],
        xfail=True,
        quiet=True,
    )
    # Prevent the slurmctld continuing to run on timeout since it is launched
    # under sudo
    if result["exit_code"] == 110:
        atf.stop_slurmctld()

    assert (
        fatal_message in result["stderr"]
    ), f"Expected '{fatal_message}' in slurmctld stderr, got: {result['stderr']}"
    assert (
        result["exit_code"] == 1
    ), f"slurmctld should fatal (exit 1), got {result['exit_code']}"
