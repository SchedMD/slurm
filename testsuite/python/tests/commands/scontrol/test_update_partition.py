############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import time


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to change the partition state")
    atf.require_slurm_running()


@pytest.fixture(scope='module')
def default_partition():
    """Determine the default partition"""
    return atf.default_partition()


def test_partition_up(default_partition):
    """Verify that a job will run in an UP partition"""

    partitions_dict = atf.get_partitions()
    assert partitions_dict[default_partition]['State'] == 'UP'
    assert atf.run_job_exit(f"-p {default_partition} -N1 true") == 0


def test_partition_down(default_partition):
    """Verify that a job will not run in a DOWN partition"""

    atf.run_command(f"scontrol update PartitionName={default_partition} State=DOWN", user=atf.properties['slurm-user'], fatal=True)
    partitions_dict = atf.get_partitions()
    assert partitions_dict[default_partition]['State'] == 'DOWN'
    assert atf.run_job_exit(f"-p {default_partition} -N1 true") != 0
