############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""ResumeTimeout accepts values too large for 16 bits.

Flexible and spot cloud reservations can take days to be fulfilled, so the
global and per-partition ResumeTimeout must allow at least 7 days. Before 26.11
ResumeTimeout was 16 bits and slurm.conf rejected values above 65535.
"""

import json

import pytest

import atf

# 7 and 14 days, both more than the old limit of 65533 seconds
g_resume_timeout = 7 * 24 * 60 * 60
p1_resume_timeout = 14 * 24 * 60 * 60
# The largest finite value. NO_VAL (4294967294) and INFINITE (4294967295) are
# the two values above it.
p3_resume_timeout = 4294967293


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="Issue 51038: ResumeTimeout above 65533 seconds added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="Issue 51038: ResumeTimeout above 65533 seconds added in 26.11",
    )
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("ResumeTimeout", g_resume_timeout)
    atf.require_config_parameter(
        "PartitionName",
        {
            "all": {"Nodes": "ALL"},
            "p1": {"Nodes": "ALL", "ResumeTimeout": p1_resume_timeout},
            "p2": {"Nodes": "ALL", "ResumeTimeout": "INFINITE"},
            "p3": {"Nodes": "ALL", "ResumeTimeout": p3_resume_timeout},
        },
    )
    atf.require_slurm_running()


def test_show_config():
    """scontrol show config reports the full global ResumeTimeout."""

    assert (
        atf.get_config_parameter("ResumeTimeout") == f"{g_resume_timeout} sec"
    ), "show config should report the full global ResumeTimeout"


def test_show_partition():
    """scontrol show partition reports the full partition ResumeTimeout."""

    parts = atf.get_partitions(fatal=True)
    assert (
        parts["all"]["ResumeTimeout"] == "GLOBAL"
    ), "A partition without ResumeTimeout should show GLOBAL"
    assert (
        parts["p1"]["ResumeTimeout"] == p1_resume_timeout
    ), "show partition should report the full partition ResumeTimeout"
    assert (
        parts["p2"]["ResumeTimeout"] == "INFINITE"
    ), "A partition with ResumeTimeout=INFINITE should show INFINITE"
    assert (
        parts["p3"]["ResumeTimeout"] == p3_resume_timeout
    ), "show partition should report the largest finite ResumeTimeout"


@pytest.mark.parametrize("data_parser", ["v0.0.45", "v0.0.46"])
def test_json(data_parser):
    """data_parser v0.0.45 and v0.0.46 dump the full values. v0.0.45 keeps its
    published schemas, so its global ResumeTimeout is a plain number."""

    conf = json.loads(
        atf.run_command_output(f"scontrol --json={data_parser} show config", fatal=True)
    )["slurm_conf"]
    if data_parser == "v0.0.45":
        assert (
            conf["ResumeTimeout"] == g_resume_timeout
        ), f"{data_parser} should dump the full global ResumeTimeout"
    else:
        assert conf["ResumeTimeout"] == {
            "set": True,
            "infinite": False,
            "number": g_resume_timeout,
        }, f"{data_parser} should dump the full global ResumeTimeout"
    parts = json.loads(
        atf.run_command_output(
            f"scontrol --json={data_parser} show partition", fatal=True
        )
    )["partitions"]
    resume = {part["name"]: part["timeouts"]["resume"] for part in parts}
    assert resume["all"] == {
        "set": False,
        "infinite": False,
        "number": 0,
    }, f"{data_parser} should dump a partition without ResumeTimeout as unset"
    assert resume["p1"] == {
        "set": True,
        "infinite": False,
        "number": p1_resume_timeout,
    }, f"{data_parser} should dump the full partition ResumeTimeout"
    assert resume["p2"] == {
        "set": False,
        "infinite": True,
        "number": 0,
    }, f"{data_parser} should dump ResumeTimeout=INFINITE as infinite"
    assert resume["p3"] == {
        "set": True,
        "infinite": False,
        "number": p3_resume_timeout,
    }, f"{data_parser} should dump the largest finite ResumeTimeout"


@pytest.mark.parametrize(
    "value, shown, dumped",
    [
        pytest.param(
            "INFINITE",
            "infinite",
            {"set": False, "infinite": True, "number": 0},
            id="infinite",
        ),
        pytest.param(
            p3_resume_timeout,
            f"{p3_resume_timeout} sec",
            {"set": True, "infinite": False, "number": p3_resume_timeout},
            id="largest",
        ),
    ],
)
def test_global_extremes(value, shown, dumped):
    """The global ResumeTimeout reports INFINITE and the largest finite value
    as is, and data_parser v0.0.46 dumps them with the right flags."""

    atf.set_config_parameter("ResumeTimeout", value)

    assert (
        atf.get_config_parameter("ResumeTimeout") == shown
    ), f"show config should report a global ResumeTimeout of {value}"
    conf = json.loads(
        atf.run_command_output("scontrol --json=v0.0.46 show config", fatal=True)
    )["slurm_conf"]
    assert (
        conf["ResumeTimeout"] == dumped
    ), f"v0.0.46 should dump a global ResumeTimeout of {value}"
