############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import os
import pwd


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to load a user into the accounting database")
    atf.require_accounting(modify=True)
    atf.require_config_parameter("Parameters", "PreserveCaseUser", source='dbd')
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def setup_account():
    test_user = pwd.getpwuid(os.getuid())[0]
    atf.run_command(f"sacctmgr -vi add account test_account", user=atf.properties["slurm-user"], fatal=True)
    atf.run_command(f"sacctmgr -vi add user {test_user} account=test_account AdminLevel=Admin", user=atf.properties["slurm-user"], fatal=True)


def test_PreserveCaseUser_on(setup_account):
    """Verify text case is preserved on the imported user name"""

    config_file = f"{atf.module_tmp_path}/my_cluster.cfg"
    user = "Test.User"
    config_text = f"""
Cluster - atf
User - {user}:DefaultAccount=test_account
"""
    with open(config_file, "w") as f:
        f.writelines(config_text)

    atf.run_command(f"sacctmgr -inQ load {config_file} clean", fatal=True)
    user_out = atf.run_command_output(f"sacctmgr list user {user} format=User -nP").rstrip()
    assert user_out == user, f"Case was not be preserved for imported user name ({user_out})"
