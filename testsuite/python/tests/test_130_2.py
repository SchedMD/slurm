############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup/Teardown
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("FairShareDampeningFactor", 1)


def test_shares(prio_multifactor):
    """Verify that shares return the expected values"""
    output = atf.run_command_output(prio_multifactor, fatal=True)
    assert (
        re.search(
            r"AccountB\|User1\|1\|0.200000\|20\|0.095238\|0.184524\|0.527550\|\|cpu=0\|",
            output,
        )
        is not None
    )
    assert (
        re.search(
            r"AccountC\|User2\|1\|0.033333\|25\|0.119048\|0.130952\|0.065672\|\|cpu=0\|",
            output,
        )
        is not None
    )
    assert (
        re.search(
            r"AccountC\|User3\|1\|0.033333\|0\|0.000000\|0.071429\|0.226431\|\|cpu=0\|",
            output,
        )
        is not None
    )
    assert (
        re.search(
            r"AccountE\|User4\|1\|0.166667\|25\|0.119048\|0.119048\|0.609507\|\|cpu=0\|",
            output,
        )
        is not None
    )
    assert (
        re.search(
            r"AccountF\|User5\|1\|0.233333\|0\|0.000000\|0.069444\|0.813594\|\|cpu=0\|",
            output,
        )
        is not None
    )
    assert (
        re.search(
            r"AccountG\|User6\|0\|0.000000\|30\|0.142857\|0.142857\|0.000000\|\|cpu=0\|",
            output,
        )
        is not None
    )
    assert (
        re.search(
            r"AccountH\|UHRA1\|parent\|0.333333\|20\|0.095238\|0.523810\|0.336475\|\|cpu=0\|",
            output,
        )
        is not None
    )
    assert (
        re.search(
            r"AccountH\|UHRA2\|40\|0.078431\|20\|0.095238\|0.196078\|0.176777\|\|cpu=0\|",
            output,
        )
        is not None
    )
    assert (
        re.search(
            r"AccountH\|UHRA3\|50\|0.098039\|25\|0.119048\|0.238095\|0.185749\|\|cpu=0\|",
            output,
        )
        is not None
    )
    assert (
        re.search(
            r"AccountHTA\|UHTAStd1\|parent\|0.333333\|10\|0.047619\|0.523810\|0.336475\|\|cpu=0\|",
            output,
        )
        is not None
    )
    assert (
        re.search(
            r"AccountHTA\|UHTAStd2\|30\|0.058824\|10\|0.047619\|0.131653\|0.211966\|\|cpu=0\|",
            output,
        )
        is not None
    )
    assert (
        re.search(
            r"AccountHTA\|UHTAStd3\|50\|0.098039\|25\|0.119048\|0.238095\|0.185749\|\|cpu=0\|",
            output,
        )
        is not None
    )
