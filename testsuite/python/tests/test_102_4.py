############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_slurm_running()

def test_ping():
    """Verify sacctmgr ping works"""

    output = atf.run_command_output(f"sacctmgr ping", fatal=True)

    config_dict = atf.get_config(quiet=True)

    assert output == f"slurmdbd(primary) at {config_dict['AccountingStorageHost']} is UP\n"
