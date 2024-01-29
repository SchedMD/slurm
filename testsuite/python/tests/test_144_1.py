############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re
import pytest


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create custom resources")
    atf.require_accounting()
    atf.require_config_parameter("AccountingStorageTRES", "gres/r1")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("GresTypes", "r1,r2")
    atf.require_nodes(1, [("Gres", "r1:1,r2:2")])
    atf.require_config_parameter("Name", {"r2": {"Flags": "explicit"}}, source="gres")
    atf.require_slurm_running()


def test_explicit_gres_requested():
    """Test gres.conf flag explicit with --exclusive allocation
    and explicitly requesting 'explicit' gres"""

    with_gres_output = atf.run_command_output(
        "salloc -wnode2 --gres=r2 --exclusive scontrol show node node2 -d",
        timeout=2,
        fatal=True,
    )
    assert (
        re.search("GresUsed=r1:1,r2:1", with_gres_output) is not None
    ), "Allocation should give all of non-explicit gres (r1:1) and only requested explicit gres (r2:1)"


def test_explicit_gres_not_requested():
    """Test gres.conf flag explict with --exclusive allocation
    and not explicitly requesting 'explict' gres"""

    without_gres_output = atf.run_command_output(
        "salloc -wnode2 --exclusive scontrol show node node2 -d", timeout=2, fatal=True
    )
    assert (
        re.search("GresUsed=r1:1,r2:0", without_gres_output) is not None
    ), "Allocation should give all of non-explicit gres (r1:1) and none of the not requested explicit gres (r2:0)"
