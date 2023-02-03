############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import shutil


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()
    if shutil.which('lsid') is None:
        pytest.skip("This test requires the contribs to be built and for lsid to be in your path", allow_module_level=True)


def test_lsid():
    """Verify basic lsid functionality"""

    config_dict = atf.get_config()
    cluster_name = config_dict['ClusterName']
    controller_host = re.sub(r'\(.*', '', config_dict['SlurmctldHost[0]'])

    output = atf.run_command_output("lsid", fatal=True)
    assert re.search(rf"My cluster name is {cluster_name}", output) is not None
    assert re.search(rf"My master name is {controller_host}", output) is not None
