############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


# Setup/Teardown
@pytest.fixture(scope='module', autouse=True)
def setup():
    atf.require_config_parameter('PriorityType', 'priority/multifactor')
    atf.require_slurm_running()


@pytest.fixture(scope='module')
def section_description():
    """Submit three jobs (with one held) and evaluate the priorities"""


@pytest.fixture(scope='module')
def first_job():
    """Submit a non-held job"""
    return atf.submit_job(fatal=True)


@pytest.fixture(scope='module')
def second_job():
    """Submit a second non-held job"""
    return atf.submit_job(fatal=True)


@pytest.fixture(scope='module')
def held_job():
    """Submit a held job"""
    return atf.submit_job("--hold --wrap=\"sleep 60\"", fatal=True)


@pytest.fixture(scope='module')
def jobs_dict(section_description, first_job, second_job, held_job):
    """Return dictionary from get_jobs"""
    return atf.get_jobs()


def test_relative_job_priorities(jobs_dict, first_job, second_job):
    """Verify that both non-held jobs have the same priority"""
    assert jobs_dict[first_job]['Priority'] == jobs_dict[second_job]['Priority']


def test_held_job_has_zero_priority(jobs_dict, held_job):
    """Verify that the held job has zero priority"""
    assert jobs_dict[held_job]['Priority'] == 0
