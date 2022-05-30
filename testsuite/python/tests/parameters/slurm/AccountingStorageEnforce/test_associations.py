############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import os
import pwd
import pytest
import re

approved_account = "account1"
unapproved_account = "account2"
test_user = pwd.getpwuid(os.getuid())[0]

# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter("AccountingStorageEnforce", "associations")
    atf.require_slurm_running()

@pytest.fixture(scope='module')
def setup_account():
    atf.run_command(f"sacctmgr -vi add account {approved_account}", user=atf.properties['slurm-user'], fatal=True)
    atf.run_command(f"sacctmgr -vi add user {test_user} account={approved_account}", user=atf.properties['slurm-user'], fatal=True)

def test_approved_association(setup_account):
    """Verify that a job can be submitted under an approved account"""
    assert atf.run_command_exit(f"srun --account={approved_account} true") == 0

def test_unapproved_association():
    """Verify that a job cannot be submitted under an unapproved account"""
    assert atf.run_command_exit(f"srun --account={unapproved_account} true") != 0
