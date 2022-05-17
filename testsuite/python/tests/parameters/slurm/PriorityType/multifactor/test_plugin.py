############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

# Setup/Teardown
@pytest.fixture(scope='module', autouse=True)
def setup():
    atf.require_config_parameter('FairShareDampeningFactor', 1)
#    atf.require_slurm_running()


@pytest.fixture(scope='function')
def compiled_program(tmp_path):
    """Compile test program that uses plugin"""
    test_program = str(tmp_path / "test.exe")
    source_file = re.sub(r'\.py$', '.c', __file__)
    atf.compile_against_libslurm(source_file, test_program, full=True, build_args=f"-ldl -lm -export-dynamic {atf.properties['slurm-build-dir']}/src/slurmctld/locks.o {atf.properties['slurm-build-dir']}/src/sshare/process.o")
    return test_program


def test_shares(compiled_program):
    """Verify that shares return the expected values"""
    output = atf.run_command_output(compiled_program, fatal=True)
    assert re.search(r'AccountA||40|0.400000|45|0.450000|0.450000|0.458502|', output) is not None
    assert re.search(r'AccountB||30|0.300000|20|0.200000|0.387500|0.408479|', output) is not None
    assert re.search(r'AccountB|User1|1|0.300000|20|0.200000|0.387500|0.408479|', output) is not None
    assert re.search(r'AccountC||10|0.100000|25|0.250000|0.300000|0.125000|', output) is not None
    assert re.search(r'AccountC|User2|1|0.050000|25|0.250000|0.275000|0.022097|', output) is not None
    assert re.search(r'AccountC|User3|1|0.050000|0|0.000000|0.150000|0.125000|', output) is not None
    assert re.search(r'AccountD||60|0.600000|25|0.250000|0.250000|0.749154|', output) is not None
    assert re.search(r'AccountE||25|0.250000|25|0.250000|0.250000|0.500000|', output) is not None
    assert re.search(r'AccountE|User4|1|0.250000|25|0.250000|0.250000|0.500000|', output) is not None
    assert re.search(r'AccountF||35|0.350000|0|0.000000|0.145833|0.749154|', output) is not None
    assert re.search(r'AccountF|User5|1|0.350000|0|0.000000|0.145833|0.749154|', output) is not None
    assert re.search(r'AccountG||0|0.000000|30|0.300000|0.300000|0.000000|', output) is not None
    assert re.search(r'AccountG|User6|0|', output) is not None
